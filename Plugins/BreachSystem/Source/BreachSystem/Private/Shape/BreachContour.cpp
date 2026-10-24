#include "Shape/BreachContour.h"
#include "BreachPluginUtil.h"

namespace
{
	using namespace UE::Geometry;
	constexpr int32 MaxVertices = 2048;
	constexpr int32 MaxContourVertices = 128;

	double Cross(const FVector2d& A, const FVector2d& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	// Variable-length intact sections and inward/outward breaks along the boundary.
	struct FBoundaryDetail
	{
		struct FSection
		{
			double End = 0;
			double Peak = 0.5;
			double Depth = 0;
		};

		TArray<FSection> Sections;
		double Perimeter = 1;
		double Phase = 0;

		void Initialize(double InPerimeter, int32 Count, double Depth, FRandomStream& Random)
		{
			if (Depth == 0)
			{
				return;
			}
			Perimeter = InPerimeter;
			Phase = Random.FRand() * Perimeter;
			const int32 FlatIndex = Random.RandRange(0, Count - 1);
			const int32 OutwardIndex = (FlatIndex + 1) % Count;
			const int32 InwardIndex = (FlatIndex + 2) % Count;
			TArray<double> Lengths;
			Lengths.Reserve(Count);
			Sections.Reserve(Count);
			double TotalLength = 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const bool bFlat = Index == FlatIndex
					|| (Index != OutwardIndex && Index != InwardIndex && Random.FRand() < 0.3f);
				const double Length = Random.FRandRange(0.6f, 1.6f) * (bFlat ? 1.6 : 1.0);
				Lengths.Add(Length);
				TotalLength += Length;
				double Sign = Random.RandRange(0, 1) ? 1.0 : -1.0;
				if (Index == OutwardIndex)
				{
					Sign = 1.0;
				}
				else if (Index == InwardIndex)
				{
					Sign = -1.0;
				}
				FSection& Section = Sections.AddDefaulted_GetRef();
				Section.Peak = Random.FRandRange(0.2f, 0.8f);
				Section.Depth = bFlat ? 0 : Depth * Random.FRandRange(0.65f, 1.f) * Sign;
			}
			double End = 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				End += Perimeter * Lengths[Index] / TotalLength;
				Sections[Index].End = End;
			}
			Sections.Last().End = Perimeter;
		}

		double Evaluate(double Distance) const
		{
			if (Sections.IsEmpty())
			{
				return 0;
			}
			const double Position = FMath::Fmod(Distance + Phase, Perimeter);
			double Start = 0;
			for (const FSection& Section : Sections)
			{
				if (Position < Section.End)
				{
					const double T = (Position - Start) / (Section.End - Start);
					return Section.Depth * (T < Section.Peak ? T / Section.Peak : (1 - T) / (1 - Section.Peak));
				}
				Start = Section.End;
			}
			return 0;
		}

		// Suppress smaller damage within intact macro sections.
		double FineDetailWeight(double Distance) const
		{
			if (Sections.IsEmpty())
			{
				return 1;
			}
			const double Position = FMath::Fmod(Distance + Phase, Perimeter);
			double Start = 0;
			for (const FSection& Section : Sections)
			{
				if (Position < Section.End)
				{
					if (Section.Depth == 0)
					{
						return 0;
					}
					const double T = (Position - Start) / (Section.End - Start);
					return FMath::Clamp(FMath::Min(T, 1 - T) * 8.0, 0.0, 1.0);
				}
				Start = Section.End;
			}
			return 0;
		}
	};

	// Independent, smoothly interpolated signed offsets. Samples do not consume random values.
	struct FContinuousDetail
	{
		TArray<double> Values;
		double Perimeter = 1;
		double Phase = 0;

		void Initialize(double InPerimeter, int32 Count, double Depth, FRandomStream& Random)
		{
			if (Depth == 0)
			{
				return;
			}
			Perimeter = InPerimeter;
			Phase = Random.FRand() * Count;
			Values.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const double Strength = Random.FRand() < 0.2f ? 0.12 : Random.FRandRange(0.55f, 1.f);
				Values.Add(Depth * Random.FRandRange(-1.f, 1.f) * Strength);
			}
		}

		double Evaluate(double Distance) const
		{
			if (Values.IsEmpty())
			{
				return 0;
			}
			const double Position = FMath::Fmod(Distance / Perimeter * Values.Num() + Phase, double(Values.Num()));
			const int32 Index = FMath::FloorToInt(Position);
			const double T = Position - Index;
			const double Weight = T * T * (3 - 2 * T);
			return FMath::Lerp(Values[Index], Values[(Index + 1) % Values.Num()], Weight);
		}
	};

	// Cache nominal samples and their complete spacing ranges before any random draws.
	void SampleBoundary(const FPolygon2d& Boundary, const TArray<double>& Lengths,
		const TArray<double>& Starts, double Perimeter, double MaxDisplacement,
		const FBreachContourOptions& Options, TArray<BreachContour::FPreparedSample>& OutSamples)
	{
		const int32 Count = Boundary.VertexCount();
		const int32 TargetCount = Options.VertexCount;
		const double Jitter = Options.bRandomizeVertexSpacing ? Options.VertexPositionJitter : 0;
		TArray<int32> Steps;
		if (Count <= TargetCount)
		{
			Steps.Init(1, Count);
			for (int32 Extra = Count; Extra < TargetCount; ++Extra)
			{
				int32 Longest = 0;
				for (int32 Index = 1; Index < Count; ++Index)
				{
					if (Lengths[Index] / Steps[Index] > Lengths[Longest] / Steps[Longest])
					{
						Longest = Index;
					}
				}
				++Steps[Longest];
			}
		}
		OutSamples.Reserve(TargetCount);
		auto Append = [&](int32 EdgeIndex, double T, bool bCorner)
		{
			const FVector2d A = Boundary[EdgeIndex];
			const FVector2d B = Boundary[(EdgeIndex + 1) % Count];
			const FVector2d Tangent = (B - A) / Lengths[EdgeIndex];
			FVector2d Direction = Tangent;
			if (bCorner)
			{
				const int32 PreviousIndex = (EdgeIndex + Count - 1) % Count;
				const FVector2d Previous = (A - Boundary[PreviousIndex]) / Lengths[PreviousIndex];
				Direction = (Previous + Tangent).GetSafeNormal();
			}
			BreachContour::FPreparedSample& Sample = OutSamples.AddDefaulted_GetRef();
			Sample.Point = FMath::Lerp(A, B, T);
			Sample.Normal = FVector2d(Direction.Y, -Direction.X);
			Sample.Distance = Starts[EdgeIndex] + T * Lengths[EdgeIndex];
			Sample.MaxDisplacement = bCorner && !Options.bMoveCorners ? 0 : MaxDisplacement;
		};
		if (!Steps.IsEmpty())
		{
			for (int32 EdgeIndex = 0; EdgeIndex < Count; ++EdgeIndex)
			{
				for (int32 Step = 0; Step < Steps[EdgeIndex]; ++Step)
				{
					Append(EdgeIndex, double(Step) / Steps[EdgeIndex], Step == 0);
				}
			}
		}
		else
		{
			int32 EdgeIndex = 0;
			for (int32 Index = 0; Index < TargetCount; ++Index)
			{
				const double Distance = Perimeter * Index / TargetCount;
				while (EdgeIndex + 1 < Count && Distance >= Starts[EdgeIndex + 1])
				{
					++EdgeIndex;
				}
				const double T = FMath::Clamp((Distance - Starts[EdgeIndex]) / Lengths[EdgeIndex], 0.0, 1.0);
				Append(EdgeIndex, T, T == 0);
			}
		}
		// Anchor every offset to the original samples, including movable source vertices.
		for (int32 Index = 0; Index < TargetCount; ++Index)
		{
			BreachContour::FPreparedSample& Sample = OutSamples[Index];
			if (Sample.MaxDisplacement == 0)
			{
				continue;
			}
			const auto& Before = OutSamples[(Index + TargetCount - 1) % TargetCount];
			const auto& After = OutSamples[(Index + 1) % TargetCount];
			Sample.JitterBefore = (Before.Point - Sample.Point) * Jitter;
			Sample.JitterAfter = (After.Point - Sample.Point) * Jitter;
			const double BeforeGap = Sample.Distance - Before.Distance;
			const double AfterGap = After.Distance - Sample.Distance;
			Sample.DistanceBefore = -(BeforeGap > 0 ? BeforeGap : BeforeGap + Perimeter) * Jitter;
			Sample.DistanceAfter = (AfterGap > 0 ? AfterGap : AfterGap + Perimeter) * Jitter;
		}
	}

	// Shrinking a movement envelope preserves every half-plane already applied to it.
	void LimitMovement(BreachContour::FPreparedSample& Sample, const FVector2d& Axis, double Margin)
	{
		const double JitterReach = FMath::Max(0.0,
			FMath::Max(Sample.JitterBefore.Dot(Axis), Sample.JitterAfter.Dot(Axis)));
		double DepthReach = FMath::Abs(Sample.Normal.Dot(Axis)) * Sample.MaxDisplacement;
		const double Allowed = Margin * 0.9;
		// Preserve safe depth first; tangential constraints must not reduce normal movement.
		if (DepthReach > Allowed)
		{
			Sample.MaxDisplacement *= Allowed / DepthReach;
			DepthReach = Allowed;
		}
		const double JitterAllowed = Allowed - DepthReach;
		if (JitterReach > JitterAllowed)
		{
			const double Scale = JitterAllowed / JitterReach;
			Sample.JitterBefore *= Scale;
			Sample.JitterAfter *= Scale;
			Sample.DistanceBefore *= Scale;
			Sample.DistanceAfter *= Scale;
		}
	}

	FVector2d ClosestEdgeDirection(const FVector2d& A, const FVector2d& B,
		const FVector2d& C, const FVector2d& D)
	{
		auto Nearest = [](const FVector2d& Point, const FVector2d& Start, const FVector2d& End)
		{
			const FVector2d Edge = End - Start;
			const double T = FMath::Clamp((Point - Start).Dot(Edge) / Edge.SquaredLength(), 0.0, 1.0);
			return Start + Edge * T;
		};
		FVector2d Direction = Nearest(A, C, D) - A;
		auto Consider = [&](const FVector2d& Candidate)
		{
			if (Candidate.SquaredLength() < Direction.SquaredLength())
			{
				Direction = Candidate;
			}
		};
		Consider(Nearest(B, C, D) - B);
		Consider(C - Nearest(C, A, B));
		Consider(D - Nearest(D, A, B));
		return Direction;
	}

	bool PrepareMovementLimits(TArray<BreachContour::FPreparedSample>& Samples, double Tolerance, FString& OutError)
	{
		const int32 Count = Samples.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Previous = (Index + Count - 1) % Count;
			const int32 Next = (Index + 1) % Count;
			const FVector2d Before = Samples[Index].Point - Samples[Previous].Point;
			const FVector2d After = Samples[Next].Point - Samples[Index].Point;
			FVector2d Axis = Before / Before.Length() + After / After.Length();
			const double AxisLength = Axis.Length();
			if (AxisLength <= 1.e-12)
			{
				OutError = TEXT("Sampled boundary folds back at a vertex.");
				return false;
			}
			Axis /= AxisLength;
			const double BeforeGap = Before.Dot(Axis);
			const double AfterGap = After.Dot(Axis);
			if (FMath::Min(BeforeGap, AfterGap) <= Tolerance)
			{
				OutError = TEXT("Sampled boundary has insufficient corner clearance.");
				return false;
			}
			// A common projection order keeps both incident edges on opposite sides of their shared vertex.
			LimitMovement(Samples[Previous], Axis, BeforeGap * 0.5);
			LimitMovement(Samples[Index], -Axis, BeforeGap * 0.5);
			LimitMovement(Samples[Index], Axis, AfterGap * 0.5);
			LimitMovement(Samples[Next], -Axis, AfterGap * 0.5);
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Next = (Index + 1) % Count;
			const FVector2d A = Samples[Index].Point, B = Samples[Next].Point;
			for (int32 Other = Index + 2; Other < Count; ++Other)
			{
				const int32 OtherNext = (Other + 1) % Count;
				if (Index == 0 && OtherNext == 0)
				{
					continue;
				}
				const FVector2d C = Samples[Other].Point, D = Samples[OtherNext].Point;
				FVector2d Axis = ClosestEdgeDirection(A, B, C, D);
				const double AxisLength = Axis.Length();
				if (AxisLength <= Tolerance)
				{
					OutError = TEXT("Sampled edges have insufficient clearance for a contour.");
					return false;
				}
				Axis /= AxisLength;
				const double BProjection = (B - A).Dot(Axis);
				const double CProjection = (C - A).Dot(Axis);
				const double DProjection = (D - A).Dot(Axis);
				const double Left = FMath::Max(0.0, BProjection);
				const double Right = FMath::Min(CProjection, DProjection);
				if (Right - Left <= Tolerance)
				{
					OutError = TEXT("Sampled edges cannot be separated for contour movement.");
					return false;
				}
				const double Plane = (Left + Right) * 0.5;
				// Every possible endpoint stays on its own side, so their entire edges stay separated.
				LimitMovement(Samples[Index], Axis, Plane);
				LimitMovement(Samples[Next], Axis, Plane - BProjection);
				LimitMovement(Samples[Other], -Axis, CProjection - Plane);
				LimitMovement(Samples[OtherNext], -Axis, DProjection - Plane);
			}
		}
		return true;
	}

	// Basic numeric checks only; boundary intersections are not tested.
	bool ValidateBoundary(const FPolygon2d& Polygon, FString& OutError)
	{
		OutError.Reset();
		const int32 Count = Polygon.VertexCount();
		if (Count < 3 || Count > MaxVertices)
		{
			OutError = TEXT("Boundary must contain 3 to 2048 vertices.");
			return false;
		}
		for (const FVector2d& Point : Polygon.GetVertices())
		{
			if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y))
			{
				OutError = TEXT("Boundary contains a non-finite coordinate.");
				return false;
			}
		}
		const auto Bounds = Polygon.Bounds();
		const double Scale = FMath::Max(Bounds.Width(), Bounds.Height());
		const double Tolerance = FMath::Max(1.e-6, Scale * 1.e-8);
		if (!FMath::IsFinite(Scale))
		{
			OutError = TEXT("Boundary extent is not finite.");
			return false;
		}
		double TwiceArea = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2d A = Polygon[Index], B = Polygon[(Index + 1) % Count];
			if ((B - A).Length() <= Tolerance)
			{
				OutError = TEXT("Boundary contains a duplicate vertex or an edge below the geometric tolerance.");
				return false;
			}
			TwiceArea += Cross(A - Polygon[0], B - Polygon[0]);
		}
		if (!FMath::IsFinite(TwiceArea) || FMath::Abs(TwiceArea) <= Tolerance * Tolerance)
		{
			OutError = TEXT("Boundary area is degenerate.");
			return false;
		}
		return true;
	}
}

#if WITH_EDITOR
void FBreachContourOptions::CacheJitterInterval()
{
	if (!FMath::IsFinite(VertexPositionJitter))
	{
		JitterInterval = 1;
		return;
	}
	// Saturate before converting very large, but finite, authored ratios to an integer.
	JitterInterval = VertexPositionJitter >= float(MAX_int32)
		? MAX_int32 : FMath::Max(1, FMath::CeilToInt(VertexPositionJitter));
}
#endif

bool BreachContour::ValidateOptions(const FBreachContourOptions& Options, FString& OutError)
{
	OutError.Reset();
	const auto ValidDepth = [](float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.f && Value <= 0.45f;
	};
	if (!ValidDepth(Options.MacroDepthRatio) || !ValidDepth(Options.MediumDepthRatio)
		|| !ValidDepth(Options.MicroDepthRatio)
		|| !FMath::IsFinite(Options.MaxDisplacementFraction)
		|| Options.MaxDisplacementFraction < 0.f || Options.MaxDisplacementFraction > 0.45f
		|| Options.VertexCount < 4 || Options.VertexCount > MaxContourVertices
		|| !FMath::IsFinite(Options.VertexPositionJitter)
		|| Options.VertexPositionJitter < 0.f
		|| (Options.Pattern != EBreachContourPattern::TriangleSections
			&& Options.Pattern != EBreachContourPattern::IndependentContinuous))
	{
		OutError = TEXT("Contour requires 4..128 vertices, depth ratios 0..0.45, finite nonnegative jitter and displacement fraction 0..0.45.");
		return false;
	}
	return true;
}

bool BreachContour::Validate(const UE::Geometry::FPolygon2d& Polygon, FString& OutError)
{
	return ValidateBoundary(Polygon, OutError);
}

bool BreachContour::Prepare(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
	FPreparedContour& OutPrepared, FString& OutError)
{
	OutPrepared = FPreparedContour();
	OutError.Reset();
	if (Options.bEnabled)
	{
		if (!ValidateOptions(Options, OutError))
		{
			return false;
		}
		if (Base.VertexCount() > MaxContourVertices)
		{
			OutError = TEXT("Contour source boundary exceeds 128 vertices.");
			return false;
		}
	}
	if (!Validate(Base, OutError))
	{
		return false;
	}
	FPreparedContour Prepared;
	Prepared.Base = Base;
	Prepared.Options = Options;
	if (Prepared.Base.IsClockwise())
	{
		Prepared.Base.Reverse();
	}
	if (!Options.bEnabled || Options.MaxDisplacementFraction == 0
		|| (Options.MacroDepthRatio == 0 && Options.MediumDepthRatio == 0 && Options.MicroDepthRatio == 0))
	{
		OutPrepared = MoveTemp(Prepared);
		return true;
	}
	const FPolygon2d& Boundary = Prepared.Base;
	const int32 Count = Boundary.VertexCount();
	TArray<double> Starts, Lengths;
	Starts.Reserve(Count);
	Lengths.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Starts.Add(Prepared.Perimeter);
		const double Length = (Boundary[(Index + 1) % Count] - Boundary[Index]).Length();
		Lengths.Add(Length);
		Prepared.Perimeter += Length;
	}
	const auto Bounds = Boundary.Bounds();
	Prepared.CutterSize = FMath::Min(Bounds.Width(), Bounds.Height());
	// Layer amplitudes must not change spacing or move previously intact macro samples.
	const double MaxDisplacement = Prepared.CutterSize * Options.MaxDisplacementFraction;
	SampleBoundary(Boundary, Lengths, Starts, Prepared.Perimeter, MaxDisplacement, Options, Prepared.Samples);
	FPolygon2d SampledBoundary;
	for (const FPreparedSample& Sample : Prepared.Samples)
	{
		SampledBoundary.AppendVertex(Sample.Point);
	}
	if (!Validate(SampledBoundary, OutError))
	{
		return false;
	}
	const double Tolerance = FMath::Max(1.e-6, FMath::Max(Bounds.Width(), Bounds.Height()) * 1.e-8);
	if (SampledBoundary.IsClockwise())
	{
		OutError = TEXT("Sampled boundary reverses the source orientation.");
		return false;
	}
	if (!PrepareMovementLimits(Prepared.Samples, Tolerance, OutError))
	{
		return false;
	}
	OutPrepared = MoveTemp(Prepared);
	return true;
}

void BreachContour::Generate(const FPreparedContour& Prepared, UE::Geometry::FPolygon2d& OutPolygon)
{
	if (Prepared.Samples.IsEmpty())
	{
		OutPolygon = Prepared.Base;
		return;
	}
	const FBreachContourOptions& Options = Prepared.Options;
	const double Perimeter = Prepared.Perimeter;
	const double CutterSize = Prepared.CutterSize;
	const int32 Seed = Options.Seed == 0 ? FMath::Rand() : Options.Seed;
	FRandomStream SamplingRandom(Seed ^ 0x2794af13);
	FRandomStream TriangleRandom(Seed);
	FBoundaryDetail MacroSections, MediumSections;
	FRandomStream MacroRandom(Seed ^ 0x2715);
	FRandomStream MediumRandom(Seed ^ 0x7839);
	FRandomStream MicroRandom(Seed ^ 0x94d3);
	FContinuousDetail MacroContinuous, MediumContinuous, MicroContinuous;
	if (Options.Pattern == EBreachContourPattern::TriangleSections)
	{
		MacroSections.Initialize(Perimeter, Options.VertexCount, Options.MacroDepthRatio * CutterSize, TriangleRandom);
		MediumSections.Initialize(Perimeter, Options.VertexCount, Options.MediumDepthRatio * CutterSize, TriangleRandom);
	}
	else
	{
		MacroContinuous.Initialize(Perimeter, Options.VertexCount, Options.MacroDepthRatio * CutterSize, MacroRandom);
		MediumContinuous.Initialize(Perimeter, Options.VertexCount, Options.MediumDepthRatio * CutterSize, MediumRandom);
		MicroContinuous.Initialize(Perimeter, Options.VertexCount, Options.MicroDepthRatio * CutterSize, MicroRandom);
	}
	TArray<FVector2d> Points;
	Points.Reserve(Prepared.Samples.Num());
	int32 UntilJitter = Options.JitterInterval;
	for (const FPreparedSample& Sample : Prepared.Samples)
	{
		FVector2d Point = Sample.Point;
		double Distance = Sample.Distance;
		if (--UntilJitter == 0)
		{
			UntilJitter = Options.JitterInterval;
			if (Sample.DistanceBefore != 0 || Sample.DistanceAfter != 0)
			{
				const double Jitter = BreachPluginUtil::SampleBoundedNormal(SamplingRandom);
				const double Weight = FMath::Abs(Jitter);
				Point += (Jitter < 0 ? Sample.JitterBefore : Sample.JitterAfter) * Weight;
				Distance += (Jitter < 0 ? Sample.DistanceBefore : Sample.DistanceAfter) * Weight;
				if (Distance < 0)
				{
					Distance += Perimeter;
				}
				else if (Distance >= Perimeter)
				{
					Distance -= Perimeter;
				}
			}
		}
		double Detail = 0;
		if (Options.Pattern == EBreachContourPattern::TriangleSections)
		{
			const double Weight = MacroSections.FineDetailWeight(Distance);
			Detail = MacroSections.Evaluate(Distance) + Weight * (MediumSections.Evaluate(Distance)
				+ MediumSections.FineDetailWeight(Distance)
				* TriangleRandom.FRandRange(-Options.MicroDepthRatio * CutterSize, Options.MicroDepthRatio * CutterSize));
		}
		else
		{
			Detail = MacroContinuous.Evaluate(Distance) + MediumContinuous.Evaluate(Distance)
				+ MicroContinuous.Evaluate(Distance);
		}
		Points.Add(Point + Sample.Normal * FMath::Clamp(Detail, -Sample.MaxDisplacement, Sample.MaxDisplacement));
	}
	OutPolygon = FPolygon2d(MoveTemp(Points));
}

bool BreachContour::Generate(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
	UE::Geometry::FPolygon2d& OutPolygon, FString& OutError)
{
	if (!Options.bEnabled)
	{
		OutPolygon = Base;
		OutError.Reset();
		return true;
	}
	FPreparedContour Prepared;
	if (!Prepare(Base, Options, Prepared, OutError))
	{
		OutPolygon = FPolygon2d();
		return false;
	}
	Generate(Prepared, OutPolygon);
	return true;
}
