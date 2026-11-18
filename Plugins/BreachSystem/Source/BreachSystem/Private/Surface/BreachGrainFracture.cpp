#include "BreachGrainFracture.h"
#include "Algo/Reverse.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"

namespace BreachGrainFracture
{
namespace
{
	using namespace UE::Geometry;

	/** X runs across the grain and Y along it. The basis is right-handed, so loop winding survives conversion. */
	struct FGrainFrame
	{
		FVector2d Origin = FVector2d::ZeroVector;
		FVector2d Across = FVector2d(1, 0);
		FVector2d Along = FVector2d(0, 1);

		FVector2d ToFrame(const FVector2d& Point) const
		{
			const FVector2d Relative = Point - Origin;
			return FVector2d(Relative.Dot(Across), Relative.Dot(Along));
		}

		FVector2d ToWorld(const FVector2d& Point) const
		{
			return Origin + Across * Point.X + Along * Point.Y;
		}
	};

	/** One continuous guide line in frame coordinates, strictly increasing along the grain. */
	struct FChain
	{
		TArray<FVector2d> Points;

		double AcrossAt(double Along) const
		{
			if (Along <= Points[0].Y)
			{
				return Points[0].X;
			}
			for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
			{
				const FVector2d& A = Points[Index];
				const FVector2d& B = Points[Index + 1];
				if (Along <= B.Y)
				{
					return FMath::Lerp(A.X, B.X, (Along - A.Y) / (B.Y - A.Y));
				}
			}
			return Points.Last().X;
		}

		double Span() const
		{
			return Points.Last().Y - Points[0].Y;
		}
	};

	bool FindFrame(TConstArrayView<FGuideSegment> Guides, FGrainFrame& OutFrame)
	{
		FVector2d Reference = FVector2d::ZeroVector;
		FVector2d Sum = FVector2d::ZeroVector;
		for (const FGuideSegment& Guide : Guides)
		{
			const FVector2d Direction = (Guide.B - Guide.A).GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				continue;
			}
			if (Reference.IsNearlyZero())
			{
				Reference = Direction;
			}
			// Guides authored in either direction describe the same grain.
			Sum += Direction.Dot(Reference) < 0 ? -Direction : Direction;
		}
		const double Length = Sum.Length();
		if (Length <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		OutFrame.Along = Sum / Length;
		OutFrame.Across = FVector2d(OutFrame.Along.Y, -OutFrame.Along.X);
		return true;
	}

	void BuildChains(TConstArrayView<FGuideSegment> Guides, const FGrainFrame& Frame, double Tolerance, double SeamLimit, TArray<FChain>& OutChains)
	{
		// GatherGuides emits the segments of one guide instance consecutively.
		TArray<FChain> Paths;
		for (int32 Index = 0; Index < Guides.Num(); ++Index)
		{
			if (Index == 0 || Guides[Index].PathIndex != Guides[Index - 1].PathIndex)
			{
				Paths.AddDefaulted_GetRef().Points.Add(Frame.ToFrame(Guides[Index].A));
			}
			Paths.Last().Points.Add(Frame.ToFrame(Guides[Index].B));
		}

		for (FChain& Path : Paths)
		{
			if (Path.Points.Last().Y < Path.Points[0].Y)
			{
				Algo::Reverse(Path.Points);
			}
			bool bMonotone = true;
			for (int32 Index = 0; Index + 1 < Path.Points.Num(); ++Index)
			{
				bMonotone &= Path.Points[Index + 1].Y - Path.Points[Index].Y > Tolerance;
			}
			// A line that doubles back along its own direction cannot bound a strip, so it is left out of every lane.
			if (bMonotone)
			{
				OutChains.Add(MoveTemp(Path));
			}
		}

		// Repeated tiles meet end to end; one physical seam must be one lane boundary, or the lane splits at every tile edge.
		// Hand-authored guides rarely end exactly where their next tile copy starts, so an end joins the nearest start ahead
		// of it when that start is unambiguous (four times closer than any other) and within SeamLimit.
		// Anything else is treated as an authored gap in the grain.
		bool bJoined = true;
		while (bJoined)
		{
			bJoined = false;
			for (int32 First = 0; First < OutChains.Num() && !bJoined; ++First)
			{
				const FVector2d End = OutChains[First].Points.Last();
				int32 Nearest = INDEX_NONE;
				double NearestDistance = TNumericLimits<double>::Max();
				double SecondDistance = TNumericLimits<double>::Max();
				for (int32 Second = 0; Second < OutChains.Num(); ++Second)
				{
					const FVector2d Start = OutChains[Second].Points[0];
					if (Second == First || Start.Y < End.Y - Tolerance)
					{
						continue;
					}
					const double Distance = FVector2d::Distance(End, Start);
					if (Distance < NearestDistance)
					{
						SecondDistance = NearestDistance;
						NearestDistance = Distance;
						Nearest = Second;
					}
					else
					{
						SecondDistance = FMath::Min(SecondDistance, Distance);
					}
				}
				if (Nearest == INDEX_NONE || NearestDistance > SeamLimit || NearestDistance > SecondDistance * .25)
				{
					continue;
				}
				// A start level with the end would make a zero-length step along the grain; the end vertex stands in for it.
				const TArray<FVector2d>& Next = OutChains[Nearest].Points;
				const int32 Skip = Next[0].Y - End.Y <= Tolerance ? 1 : 0;
				OutChains[First].Points.Append(Next.GetData() + Skip, Next.Num() - Skip);
				OutChains.RemoveAt(Nearest);
				bJoined = true;
			}
		}
	}

	FGeneralPolygon2d WorldLoop(const FGrainFrame& Frame, TConstArrayView<FVector2d> FramePoints)
	{
		FPolygon2d Loop;
		for (const FVector2d& Point : FramePoints)
		{
			Loop.AppendVertex(Frame.ToWorld(Point));
		}
		if (Loop.IsClockwise())
		{
			Loop.Reverse();
		}
		return FGeneralPolygon2d(Loop);
	}

	/**
	 * The polygon boolean returns quantized coordinates with repeated and collinear vertices where strips meet.
	 * A vertex within Epsilon of the line through its neighbors carries no shape (straight run or zero-width spike).
	 */
	FPolygon2d CleanLoop(const FPolygon2d& Loop, double Epsilon)
	{
		TArray<FVector2d> Points(Loop.GetVertices());
		bool bRemoved = true;
		while (bRemoved && Points.Num() > 3)
		{
			bRemoved = false;
			for (int32 Index = 0; Index < Points.Num() && Points.Num() > 3; ++Index)
			{
				const FVector2d& Previous = Points[(Index + Points.Num() - 1) % Points.Num()];
				const FVector2d& Current = Points[Index];
				const FVector2d& Next = Points[(Index + 1) % Points.Num()];
				const FVector2d Chord = Next - Previous;
				const double ChordLength = Chord.Length();
				const bool bDuplicate = FVector2d::Distance(Previous, Current) <= Epsilon;
				// Neighbors that coincide mean Current is the tip of an out-and-back spike.
				const bool bSpike = ChordLength <= Epsilon;
				const bool bOnChord = !bSpike && FMath::Abs(Chord.X * (Current.Y - Previous.Y) - Chord.Y * (Current.X - Previous.X)) <= Epsilon * ChordLength;
				if (bDuplicate || bSpike || bOnChord)
				{
					Points.RemoveAt(Index);
					bRemoved = true;
					--Index;
				}
			}
		}
		return FPolygon2d(Points);
	}

	FGeneralPolygon2d CleanRegion(const FGeneralPolygon2d& Region, double Epsilon, double MinArea)
	{
		FGeneralPolygon2d Result(CleanLoop(Region.GetOuter(), Epsilon));
		for (const FPolygon2d& Hole : Region.GetHoles())
		{
			// Slivers left where a line steps across a tile seam are not material left standing in the opening.
			if (FMath::Abs(Hole.SignedArea()) > MinArea)
			{
				Result.AddHole(CleanLoop(Hole, Epsilon), false, false);
			}
		}
		return Result;
	}

	/**
	 * One strip end as (fraction across the strip, outward offset) from fraction 0 to 1.
	 * Positive outward moves the end away from the opening; spikes point either way.
	 * Each spike picks its own length in [MinLength, MaxLength], then shortens to Limit so it cannot cross the opposite end.
	 */
	TArray<FVector2d> EndProfile(FRandomStream& Random, int32 MaxSpikes, double MinLength, double MaxLength, double Limit)
	{
		TArray<FVector2d> Profile{{0, 0}};
		const int32 Count = MaxLength > 0 && Limit > 0 ? Random.RandRange(0, MaxSpikes) : 0;
		for (int32 Spike = 0; Spike < Count; ++Spike)
		{
			const double Start = double(Spike) / Count;
			const double End = double(Spike + 1) / Count;
			const double Center = FMath::Lerp(Start, End, double(Random.FRandRange(.35f, .65f)));
			// Each spike stays strictly inside its own cell, so neighboring spikes never share a vertex.
			const double Half = FMath::Min(Center - Start, End - Center) * Random.FRandRange(.75f, .98f);
			const double Peak = FMath::Lerp(Center - Half, Center + Half, double(Random.FRandRange(.3f, .7f)));
			const double Length = FMath::Min(FMath::Lerp(MinLength, MaxLength, double(Random.FRand())), Limit);
			Profile.Add({Center - Half, 0});
			Profile.Add({Peak, Length * (Random.FRand() < .5f ? -1 : 1)});
			Profile.Add({Center + Half, 0});
		}
		Profile.Add({1, 0});
		return Profile;
	}
}

bool Build(const FBreachLineTearOptions& Options, const FPolygon2d& Contour, TConstArrayView<FGuideSegment> Guides, int32 Seed, double Influence, TArray<FGeneralPolygon2d>& Out, FString& OutError)
{
	Out.Reset();
	OutError.Reset();
	FPolygon2d ContourLoop = Contour;
	if (ContourLoop.IsClockwise())
	{
		ContourLoop.Reverse();
	}
	const FGeneralPolygon2d ContourRegion(ContourLoop);

	// Preparation: grain frame centered on the contour, and lane boundaries ordered across the grain.
	FGrainFrame Frame;
	if (!FindFrame(Guides, Frame))
	{
		Out.Add(ContourRegion);
		return true;
	}
	const FAxisAlignedBox2d Bounds = ContourLoop.Bounds();
	Frame.Origin = Bounds.Center();
	const double Size = FMath::Max(Bounds.Width(), Bounds.Height());
	const double Tolerance = Size * 1.e-7;
	const double Overlap = Size * 1.e-5;
	// Tear lengths are fractions of the hole's length along the lines, so every strip, even one the hit barely touched,
	// can tear by the same random amount; the outline is not just the contour stretched.
	double ContourLow = TNumericLimits<double>::Max();
	double ContourHigh = TNumericLimits<double>::Lowest();
	for (const FVector2d& Point : ContourLoop.GetVertices())
	{
		const double AlongValue = Frame.ToFrame(Point).Y;
		ContourLow = FMath::Min(ContourLow, AlongValue);
		ContourHigh = FMath::Max(ContourHigh, AlongValue);
	}
	const double HoleLength = ContourHigh - ContourLow;
	TArray<FChain> Chains;
	// Seams that miss by more than 5% of the cut are authored gaps, not tile copies of one line.
	BuildChains(Guides, Frame, Tolerance, Size * .05, Chains);
	Chains.Sort([](const FChain& A, const FChain& B) { return A.AcrossAt(0) < B.AcrossAt(0); });
	const auto SharedSpan = [](const FChain& A, const FChain& B)
	{
		return FMath::Min(A.Points.Last().Y, B.Points.Last().Y) - FMath::Max(A.Points[0].Y, B.Points[0].Y);
	};
	for (int32 First = Chains.Num() - 1; First >= 0; --First)
	{
		for (int32 Second = Chains.Num() - 1; Second > First; --Second)
		{
			// Guides authored on both tile edges coincide after tiling; keep the longer copy.
			if (FMath::Abs(Chains[First].AcrossAt(0) - Chains[Second].AcrossAt(0)) <= Tolerance && SharedSpan(Chains[First], Chains[Second]) > Tolerance)
			{
				Chains.RemoveAt(Chains[Second].Span() > Chains[First].Span() ? First : Second);
				break;
			}
		}
	}

	// Strips: each lane is split across the grain, and each strip keeps its sides on the lane boundaries.
	const uint32 BaseHash = HashCombine(HashCombine(GetTypeHash(Frame.Origin.X), GetTypeHash(Frame.Origin.Y)), GetTypeHash(Seed));
	TArray<FGeneralPolygon2d> Lanes;
	TArray<FGeneralPolygon2d> Pieces;
	for (int32 LaneIndex = 0; LaneIndex < Chains.Num(); ++LaneIndex)
	{
		// A lane's right side is the nearest guide that shares part of its along-grain range.
		// Guides stacked in separate tiles share an across position but bound different lanes.
		const FChain& Left = Chains[LaneIndex];
		int32 RightIndex = INDEX_NONE;
		for (int32 Candidate = LaneIndex + 1; Candidate < Chains.Num(); ++Candidate)
		{
			const bool bRightOf = Chains[Candidate].AcrossAt(0) - Left.AcrossAt(0) > Tolerance;
			if (bRightOf && SharedSpan(Left, Chains[Candidate]) > Tolerance)
			{
				RightIndex = Candidate;
				break;
			}
		}
		if (RightIndex == INDEX_NONE)
		{
			continue;
		}
		const FChain& Right = Chains[RightIndex];
		const double Low = FMath::Max(Left.Points[0].Y, Right.Points[0].Y);
		const double High = FMath::Min(Left.Points.Last().Y, Right.Points.Last().Y);
		if (High - Low <= Tolerance)
		{
			continue;
		}
		TArray<double> Samples{Low, High};
		for (const FChain* Chain : {&Left, &Right})
		{
			for (const FVector2d& Point : Chain->Points)
			{
				if (Point.Y > Low && Point.Y < High)
				{
					Samples.Add(Point.Y);
				}
			}
		}
		Samples.Sort();
		for (int32 Index = Samples.Num() - 1; Index > 0; --Index)
		{
			if (Samples[Index] - Samples[Index - 1] <= Tolerance)
			{
				Samples.RemoveAt(Index);
			}
		}
		bool bOrdered = true;
		for (double Along : Samples)
		{
			bOrdered &= Right.AcrossAt(Along) - Left.AcrossAt(Along) > Tolerance;
		}
		// Crossing lines cannot bound a strip; that area keeps the plain contour.
		if (!bOrdered)
		{
			continue;
		}
		const double Center = FMath::Clamp(0., Low, High);
		const double LaneWidth = Right.AcrossAt(Center) - Left.AcrossAt(Center);
		// Lane edges must reproduce the guide exactly; a one-ulp Lerp difference leaves a hairline gap between lanes.
		const auto BoundaryAt = [&](double Fraction, double Along)
		{
			if (Fraction == 0)
			{
				return Left.AcrossAt(Along);
			}
			if (Fraction == 1)
			{
				return Right.AcrossAt(Along);
			}
			return FMath::Lerp(Left.AcrossAt(Along), Right.AcrossAt(Along), Fraction);
		};
		const auto StripLoop = [&](double From, double To)
		{
			TArray<FVector2d> Points;
			for (double Along : Samples)
			{
				Points.Add({BoundaryAt(To, Along), Along});
			}
			for (int32 Index = Samples.Num() - 1; Index >= 0; --Index)
			{
				Points.Add({BoundaryAt(From, Samples[Index]), Samples[Index]});
			}
			return WorldLoop(Frame, Points);
		};
		Lanes.Add(StripLoop(0, 1));

		const uint32 LaneHash = HashCombine(BaseHash, GetTypeHash(LaneIndex));
		FRandomStream LaneRandom(static_cast<int32>(LaneHash));
		TArray<double> Cuts;
		const int32 SplitCount = LaneRandom.RandRange(0, Options.MaxSplits);
		for (int32 Split = 0; Split < SplitCount; ++Split)
		{
			Cuts.Add(LaneRandom.FRandRange(.15f, .85f));
		}
		Cuts.Sort();
		// Strips narrower than an eighth of the lane read as noise rather than a step.
		TArray<double> Fractions{0.};
		for (double Cut : Cuts)
		{
			if (Cut - Fractions.Last() >= .125 && 1 - Cut >= .125)
			{
				Fractions.Add(Cut);
			}
		}
		Fractions.Add(1.);

		for (int32 StripIndex = 0; StripIndex + 1 < Fractions.Num(); ++StripIndex)
		{
			const double From = Fractions[StripIndex];
			const double To = Fractions[StripIndex + 1];
			const FGeneralPolygon2d Strip = StripLoop(From, To);
			TArray<FGeneralPolygon2d> Cut;
			if (!PolygonsIntersection(MakeArrayView(&Strip, 1), MakeArrayView(&ContourRegion, 1), Cut))
			{
				OutError = TEXT("A line tear strip could not be clipped to the contour.");
				return false;
			}
			// Piece vertices are placed by strip fraction, so neighbors evaluate shared sides with the same guide function.
			// Sides reach Overlap past the guides: separate boolean calls quantize differently, and touching sides would leave hairline gaps.
			const auto Place = [&](double T, double Along)
			{
				if (T == 0)
				{
					return FVector2d(BoundaryAt(From, Along) - Overlap, Along);
				}
				if (T == 1)
				{
					return FVector2d(BoundaryAt(To, Along) + Overlap, Along);
				}
				return FVector2d(BoundaryAt(FMath::Lerp(From, To, T), Along), Along);
			};
			// Only the contour's extent along this strip is measured from the boolean result.
			// A strip breaks once, so separate contour pieces in it (a notched contour) still remove one span; no material floats inside the opening.
			double Bottom = TNumericLimits<double>::Max();
			double Top = TNumericLimits<double>::Lowest();
			for (const FGeneralPolygon2d& Piece : Cut)
			{
				for (const FVector2d& Point : Piece.GetOuter().GetVertices())
				{
					const double AlongValue = Frame.ToFrame(Point).Y;
					Bottom = FMath::Min(Bottom, AlongValue);
					Top = FMath::Max(Top, AlongValue);
				}
			}
			const double Length = Top - Bottom;
			if (Length <= Tolerance)
			{
				continue;
			}

			FRandomStream Random(static_cast<int32>(HashCombine(LaneHash, GetTypeHash(StripIndex))));
			Bottom -= HoleLength * FMath::Lerp(Options.TearLengthMin, Options.TearLengthMax, Random.FRand()) * Influence;
			Top += HoleLength * FMath::Lerp(Options.TearLengthMin, Options.TearLengthMax, Random.FRand()) * Influence;
			// The lane ends where its lines stop; the contour beyond it belongs to the remainder.
			Bottom = FMath::Max(Bottom, Low - Overlap);
			Top = FMath::Min(Top, High + Overlap);
			// A strip that would tear only a little stays whole, instead of leaving a small notch at the hole's edge.
			if (Top - Bottom <= FMath::Max(Tolerance, HoleLength * Options.MinTearLength * Influence))
			{
				continue;
			}
			// Opposite spikes may not cross and outward spikes stay inside the lane, which keeps the piece simple.
			const double SpikeLimit = FMath::Min((Top - Bottom) * .45, FMath::Min(Bottom - (Low - Overlap), High + Overlap - Top));
			const double SpikeMin = LaneWidth * Options.SpikeLengthMin * Influence;
			const double SpikeMax = LaneWidth * Options.SpikeLengthMax * Influence;
			const TArray<FVector2d> BottomProfile = EndProfile(Random, Options.MaxSpikes, SpikeMin, SpikeMax, SpikeLimit);
			const TArray<FVector2d> TopProfile = EndProfile(Random, Options.MaxSpikes, SpikeMin, SpikeMax, SpikeLimit);

			TArray<FVector2d> Points;
			for (const FVector2d& Offset : BottomProfile)
			{
				Points.Add(Place(Offset.X, Bottom - Offset.Y));
			}
			for (double Along : Samples)
			{
				if (Along > Bottom && Along < Top)
				{
					Points.Add(Place(1, Along));
				}
			}
			for (int32 Index = TopProfile.Num() - 1; Index >= 0; --Index)
			{
				Points.Add(Place(TopProfile[Index].X, Top + TopProfile[Index].Y));
			}
			for (int32 Index = Samples.Num() - 1; Index >= 0; --Index)
			{
				if (Samples[Index] > Bottom && Samples[Index] < Top)
				{
					Points.Add(Place(0, Samples[Index]));
				}
			}
			Pieces.Add(WorldLoop(Frame, Points));
		}
	}

	// Assembly: contour outside every lane stays as is, then all strip pieces join it.
	if (Lanes.IsEmpty())
	{
		Out.Add(ContourRegion);
		return true;
	}
	TArray<FGeneralPolygon2d> Remainder;
	if (!PolygonsDifference(MakeArrayView(&ContourRegion, 1), Lanes, Remainder))
	{
		OutError = TEXT("Contour outside the line tear lanes could not be separated.");
		return false;
	}
	Pieces.Append(Remainder);
	if (Pieces.IsEmpty())
	{
		OutError = TEXT("Line tear strips produced no cut area.");
		return false;
	}
	TArray<FGeneralPolygon2d> United;
	if (!PolygonsUnion(Pieces, United, false))
	{
		OutError = TEXT("Line tear strips could not form a cut.");
		return false;
	}
	const double MinArea = Size * Size * 1.e-6;
	for (const FGeneralPolygon2d& Region : United)
	{
		if (FMath::Abs(Region.SignedArea()) > MinArea)
		{
			Out.Add(CleanRegion(Region, Size * 1.e-6, MinArea));
		}
	}
	if (Out.IsEmpty())
	{
		OutError = TEXT("Line tear strips produced no cut area.");
		return false;
	}
	return BreachPatternGeometry::ValidateRegions(Out, OutError);
}
}
