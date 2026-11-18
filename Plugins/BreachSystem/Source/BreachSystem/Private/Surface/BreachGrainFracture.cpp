#include "BreachGrainFracture.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"

namespace BreachGrainFracture
{
namespace
{
	constexpr double Tolerance = 1.e-8;
	constexpr int32 VertexBudget = 128;

	double Cross2(const FVector2d& A, const FVector2d& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	struct FLocation
	{
		int32 Segment = 0;
		double Alpha = 0;
		int32 Forward = 1;
	};

	struct FGuideBend
	{
		FVector2d Point;
		double Distance;
	};

	struct FArcSample
	{
		double Distance;
		FVector2d Point;
		bool bJunction;
	};

	struct FCrossing
	{
		double Distance = 0;
		FVector2d Point;
		FVector2d Direction;
		FVector2d Offset;
		FLocation Location;
	};

	struct FEnd
	{
		bool bActive = false;
		bool bAlong = false;
		double AlongLength = 0;
		double StartShift = 0;
		double EndShift = 0;
		int32 ProfileStart = 0;
		int32 ProfileCount = 0;
	};

	FVector2d AtLocation(TConstArrayView<FGuideSegment> Guides, const FLocation& Location)
	{
		return FMath::Lerp(Guides[Location.Segment].A, Guides[Location.Segment].B, Location.Alpha);
	}

	// Walk only connected authored segments. A finite endpoint never jumps across a gap.
	double WalkGuide(TConstArrayView<FGuideSegment> Guides, FLocation& Location, double Distance, TArray<FGuideBend>* Bends = nullptr)
	{
		const double Sign = Distance < 0 ? -1 : 1;
		double Remaining = FMath::Abs(Distance);
		double Travelled = 0;
		for (int32 Step = 0; Step <= Guides.Num() && Remaining > Tolerance; ++Step)
		{
			const auto& Segment = Guides[Location.Segment];
			const FVector2d Delta = Segment.B - Segment.A;
			const double Length = Delta.Length();
			if (Length <= Tolerance)
			{
				break;
			}
			const double Direction = Sign * Location.Forward;
			const double Available = Length * (Direction > 0 ? 1 - Location.Alpha : Location.Alpha);
			const double Move = FMath::Min(Remaining,Available);
			Location.Alpha += Direction * Move / Length;
			Remaining -= Move;
			Travelled += Move;
			if (Remaining <= Tolerance)
			{
				break;
			}
			const FVector2d Endpoint = AtLocation(Guides,Location);
			const FVector2d Heading = Delta / Length * Direction;
			int32 Next = Location.Segment + (Direction > 0 ? 1 : -1);
			int32 Entry = Direction > 0 ? 0 : 1;
			if (Next < 0 || Next >= Guides.Num() || Guides[Next].PathIndex != Segment.PathIndex || !(Entry == 0 ? Guides[Next].A : Guides[Next].B).Equals(Endpoint,Tolerance))
			{
				Next = INDEX_NONE;
			}
			// Same-path adjacency is authoritative even at a sharp turn. Only seam joins need direction matching.
			if (Next == INDEX_NONE)
			{
				for (int32 G = 0; G < Guides.Num(); ++G)
				{
					if (Guides[G].PathIndex == Segment.PathIndex)
					{
						continue;
					}
					const FVector2d Tangent = (Guides[G].B - Guides[G].A).GetSafeNormal();
					if (Guides[G].A.Equals(Endpoint,Tolerance) && Tangent.Dot(Heading) > .5)
					{
						Next = G;
						Entry = 0;
						break;
					}
					if (Guides[G].B.Equals(Endpoint,Tolerance) && -Tangent.Dot(Heading) > .5)
					{
						Next = G;
						Entry = 1;
						break;
					}
				}
			}
			if (Next == INDEX_NONE)
			{
				break;
			}
			if (Bends)
			{
				Bends->Add({Endpoint,Travelled * Sign});
			}
			Location = {Next,double(Entry),int32(Sign) * (Entry == 0 ? 1 : -1)};
		}
		return Travelled * Sign;
	}

	bool ConnectedDistance(TConstArrayView<FGuideSegment> Guides, FLocation Location, FVector2d Goal, double MaxDistance, double& OutDistance)
	{
		const FVector2d Heading = (Guides[Location.Segment].B - Guides[Location.Segment].A).GetSafeNormal() * Location.Forward;
		const double Sign = (Goal - AtLocation(Guides,Location)).Dot(Heading) >= 0 ? 1 : -1;
		double Travelled = 0;
		for (int32 Step = 0; Step <= Guides.Num() && Travelled <= MaxDistance; ++Step)
		{
			const auto& Segment = Guides[Location.Segment];
			const FVector2d Delta = Segment.B - Segment.A;
			const double Length = Delta.Length();
			if (Length <= Tolerance)
			{
				return false;
			}
			const double T = (Goal - Segment.A).Dot(Delta) / Delta.SquaredLength();
			const double Direction = Sign * Location.Forward;
			if (T >= -Tolerance && T <= 1 + Tolerance && (T - Location.Alpha) * Direction >= -Tolerance && (Segment.A + Delta * T).Equals(Goal,Tolerance))
			{
				OutDistance = Sign * (Travelled + FMath::Abs(T - Location.Alpha) * Length);
				return FMath::Abs(OutDistance) <= MaxDistance;
			}
			const double Available = Length * (Direction > 0 ? 1 - Location.Alpha : Location.Alpha);
			const double Moved = FMath::Abs(WalkGuide(Guides,Location,Sign * (Available + Tolerance * 4)));
			Travelled += Moved;
			if (Moved <= Available + Tolerance)
			{
				return false;
			}
		}
		return false;
	}

	struct FBoundary
	{
		const BreachContour::FPreparedContour& Prepared;
		const UE::Geometry::FPolygon2d& Noisy;
		TArray<double> Starts;
		double Cap;
		double Strength;

		FBoundary(const BreachContour::FPreparedContour& InPrepared, const UE::Geometry::FPolygon2d& InNoisy, double InStrength) : Prepared(InPrepared), Noisy(InNoisy), Cap(InPrepared.CutterSize * InPrepared.Options.MaxDisplacementFraction), Strength(InStrength)
		{
			double Distance = 0;
			for (int32 I = 0; I < Prepared.Base.VertexCount(); ++I)
			{
				Starts.Add(Distance);
				Distance += (Prepared.Base[(I + 1) % Prepared.Base.VertexCount()] - Prepared.Base[I]).Length();
			}
			Starts.Add(Distance);
		}

		double Wrap(double Distance) const
		{
			return FMath::Fmod(Distance,Prepared.Perimeter);
		}

		FVector2d Point(double Distance, FVector2d& Normal) const
		{
			Distance = Wrap(Distance);
			for (int32 I = 0; I < Prepared.Base.VertexCount(); ++I)
			{
				if (Distance <= Starts[I + 1] || I + 1 == Prepared.Base.VertexCount())
				{
					const FVector2d Delta = Prepared.Base[(I + 1) % Prepared.Base.VertexCount()] - Prepared.Base[I];
					Normal = FVector2d(Delta.Y,-Delta.X).GetSafeNormal();
					return Prepared.Base[I] + Delta * ((Distance - Starts[I]) / (Starts[I + 1] - Starts[I]));
				}
			}
			return Prepared.Base[0];
		}

		double Limit(double Distance) const
		{
			Distance = Wrap(Distance);
			const auto& Samples = Prepared.Samples;
			for (int32 I = 0; I < Samples.Num(); ++I)
			{
				if (FMath::Abs(Distance - Samples[I].Distance) <= Tolerance)
				{
					return Samples[I].MaxDisplacement;
				}
				const int32 Next = (I + 1) % Samples.Num();
				const double End = Next == 0 ? Prepared.Perimeter : Samples[Next].Distance;
				if (Distance > Samples[I].Distance && Distance < End)
				{
					return FMath::Min(Samples[I].MaxDisplacement,Samples[Next].MaxDisplacement);
				}
			}
			return 0;
		}

		FVector2d NoiseAt(double Distance) const
		{
			Distance = Wrap(Distance);
			for (int32 I = 0; I < Prepared.Samples.Num(); ++I)
			{
				const int32 Next = (I + 1) % Prepared.Samples.Num();
				const double Start = Prepared.Samples[I].Distance;
				const double End = Next == 0 ? Prepared.Perimeter : Prepared.Samples[Next].Distance;
				if (Distance >= Start && Distance < End)
				{
					return FMath::Lerp(Noisy[I] - Prepared.Samples[I].Point,Noisy[Next] - Prepared.Samples[Next].Point,(Distance - Start) / (End - Start));
				}
			}
			return FVector2d::ZeroVector;
		}

		void Append(TArray<FVertex>& Out, double Distance, FVector2d Position, bool bJunction) const
		{
			FVector2d Normal;
			const FVector2d Reference = Point(Distance,Normal);
			const FVector2d Noise = NoiseAt(Distance);
			FVector2d Delta = Noise * (1 - Strength) + (Position - Reference) * Strength;
			const double Before = Delta.Dot(Normal);
			// The existing baseline must remain reachable as influence approaches zero.
			const double Allowed = FMath::Max(Limit(Distance),FMath::Abs(Noise.Dot(Normal)));
			Delta += Normal * (FMath::Clamp(Before,-Allowed,Allowed) - Before);
			const double Length = Delta.Length();
			if (Length > Cap)
			{
				Delta *= Cap / Length;
			}
			Position = Reference + Delta;
			if (!Out.IsEmpty() && Out.Last().Position.Equals(Position,Tolerance))
			{
				Out.Last().bJunction |= bJunction;
				return;
			}
			bool bFixed = false;
			const double Wrapped = Wrap(Distance);
			for (const auto& Sample : Prepared.Samples)
			{
				bFixed |= FMath::Abs(Wrapped - Sample.Distance) <= Tolerance && Sample.MaxDisplacement == 0;
			}
			Out.Add({Reference,Position,bJunction,bFixed});
		}
	};
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 프로덕션과 같은 결선 탐색 경로를 호출한다.
bool TraceGuideForTest(TConstArrayView<FGuideSegment> Guides, int32 Segment, double Alpha, double Distance, FVector2d& End, TArray<FVector2d>& Bends)
{
	if (Segment < 0 || Segment >= Guides.Num())
	{
		return false;
	}
	FLocation Location{Segment,Alpha,1};
	TArray<FGuideBend> Traversed;
	const double Travelled = WalkGuide(Guides,Location,Distance,&Traversed);
	for (const auto& Bend : Traversed)
	{
		Bends.Add(Bend.Point);
	}
	End = AtLocation(Guides,Location);
	return FMath::Abs(Travelled - Distance) < 1.e-6;
}
#endif

void AppendEndProfile(int32 MaxSplinters, double Reach, double Span, FRandomStream& Random, TArray<FVector2d>& OutProfile)
{
	check(MaxSplinters >= 0 && MaxSplinters <= 8);
	OutProfile.Add({0,0});
	if (Reach > Tolerance && Span > Tolerance)
	{
		const int32 Count = Random.RandRange(0,MaxSplinters);
		TArray<double,TInlineAllocator<8>> Weights;
		double Total = 0;
		for (int32 I = 0; I < Count; ++I)
		{
			const double Weight = Random.FRandRange(.35f,1.65f);
			Weights.Add(Weight);
			Total += Weight;
		}
		double Start = .08;
		for (double Weight : Weights)
		{
			const double Length = .84 * Weight / Total;
			const double Center = Start + Length * Random.FRandRange(.1f,.9f);
			const double Height = Reach * Random.FRandRange(.35f,1.f) * (Random.RandRange(0,1) ? 1 : -1);
			const double Margin = FMath::Min(Center - Start,Start + Length - Center);
			const double Width = FMath::Min(Margin * .8,FMath::Abs(Height) / Span * Random.FRandRange(.25f,.65f));
			OutProfile.Add({Center - Width,0});
			OutProfile.Add({Center,Height});
			OutProfile.Add({Center + Width,0});
			Start += Length;
		}
	}
	OutProfile.Add({1,0});
}

bool Build(const BreachContour::FPreparedContour& Prepared, const UE::Geometry::FPolygon2d& Noisy, TConstArrayView<FGuideSegment> Guides, double Influence, double Sharpness, int32 MaxSplinters, TArray<FVertex>& Out, FString& OutError)
{
	Out.Reset();
	const double Strength = Influence * (2 - Influence);
	const FBoundary Boundary(Prepared,Noisy,1 - FMath::Pow(1 - Strength,3));
	TArray<FCrossing> Crossings;
	for (int32 E = 0; E < Prepared.Base.VertexCount(); ++E)
	{
		const FVector2d A = Prepared.Base[E];
		const FVector2d Edge = Prepared.Base[(E + 1) % Prepared.Base.VertexCount()] - A;
		for (int32 G = 0; G < Guides.Num(); ++G)
		{
			const FVector2d Line = Guides[G].B - Guides[G].A;
			const double Denominator = Cross2(Edge,Line);
			if (FMath::Abs(Denominator) <= Tolerance * Edge.Length() * Line.Length())
			{
				continue;
			}
			const double T = Cross2(Guides[G].A - A,Line) / Denominator;
			const double U = Cross2(Guides[G].A - A,Edge) / Denominator;
			if (T >= -Tolerance && T < 1 - Tolerance && U >= -Tolerance && U <= 1 + Tolerance)
			{
				Crossings.Add({Boundary.Starts[E] + FMath::Max(0.,T) * Edge.Length(),A + Edge * FMath::Max(0.,T),Line.GetSafeNormal(),FVector2d::ZeroVector,{G,FMath::Clamp(U,0.,1.),1}});
			}
		}
	}
	Crossings.Sort([](const FCrossing& A, const FCrossing& B) { return A.Distance == B.Distance ? A.Location.Segment < B.Location.Segment : A.Distance < B.Distance; });
	for (int32 I = Crossings.Num() - 1; I > 0; --I)
	{
		if (Crossings[I].Point.Equals(Crossings[I - 1].Point,Tolerance))
		{
			Crossings.RemoveAt(I);
		}
	}
	if (Crossings.Num() > VertexBudget / 2)
	{
		OutError = TEXT("Grain crossings exceed the 128-vertex fracture budget.");
		return false;
	}
	const int32 Count = Crossings.Num();
	TArray<FEnd> Ends;
	Ends.SetNum(Count);
	FRandomStream Random(Prepared.Options.Seed ^ 0x73b4a91);
	for (auto& Crossing : Crossings)
	{
		const FVector2d Across(-Crossing.Direction.Y,Crossing.Direction.X);
		Crossing.Offset = Across * (FMath::Min(Boundary.Cap,Boundary.Limit(Crossing.Distance)) * .04 * Random.FRandRange(-1.f,1.f) * Strength);
	}
	int32 ActiveEnds = 0;
	TArray<FVector2d> Profiles;
	Profiles.Reserve(Count * 6);
	for (int32 I = 0; I < Count; ++I)
	{
		const auto& A = Crossings[I];
		const auto& B = Crossings[(I + 1) % Count];
		const double EndDistance = B.Distance > A.Distance ? B.Distance : B.Distance + Prepared.Perimeter;
		const FVector2d Chord = B.Point - A.Point;
		FVector2d Direction = A.Direction;
		if (Guides[A.Location.Segment].PathIndex == Guides[B.Location.Segment].PathIndex || FMath::Abs(Direction.Dot(B.Direction)) < .707 || FMath::Abs(Chord.GetSafeNormal().Dot(Direction)) > .7)
		{
			continue;
		}
		bool bLockedCorner = false;
		for (const auto& Sample : Prepared.Samples)
		{
			const double Distance = Sample.Distance <= A.Distance ? Sample.Distance + Prepared.Perimeter : Sample.Distance;
			bLockedCorner |= Distance < EndDistance - Tolerance && Sample.MaxDisplacement == 0;
		}
		if (bLockedCorner)
		{
			continue;
		}
		const double LimitA = FMath::Min(Boundary.Limit(A.Distance),Boundary.Cap) * .9 * Strength;
		const double LimitB = FMath::Min(Boundary.Limit(B.Distance),Boundary.Cap) * .9 * Strength;
		const double AlongA = A.Point.Dot(Direction), AlongB = B.Point.Dot(Direction);
		const double Low = FMath::Max(AlongA - LimitA,AlongB - LimitB);
		const double High = FMath::Min(AlongA + LimitA,AlongB + LimitB);
		if (High - Low <= Tolerance)
		{
			continue;
		}
		double Target = FMath::Lerp(Low,High,double(Random.FRandRange(.08f,.92f)));
		const FEnd& Previous = Ends[(I + Count - 1) % Count];
		if (Previous.bActive)
		{
			const double PreviousTarget = AlongA + Previous.EndShift * A.Direction.Dot(Direction);
			const double Separation = FMath::Min(LimitA,LimitB) * .6;
			if (FMath::Abs(Target - PreviousTarget) < Separation)
			{
				const double Sign = Target >= PreviousTarget ? 1 : -1;
				Target = FMath::Clamp(PreviousTarget + Sign * Separation,Low,High);
				if (FMath::Abs(Target - PreviousTarget) < Separation)
				{
					Target = FMath::Clamp(PreviousTarget - Sign * Separation,Low,High);
				}
			}
		}
		FEnd& End = Ends[I];
		End.bActive = true;
		End.StartShift = Target - AlongA;
		End.EndShift = (Target - AlongB) / B.Direction.Dot(Direction);
		FLocation Location = A.Location;
		End.StartShift = WalkGuide(Guides,Location,End.StartShift);
		Location = B.Location;
		End.EndShift = WalkGuide(Guides,Location,End.EndShift);
		const double Reach = FMath::Min(Boundary.Cap * .25,Chord.Length() * .15) * Random.FRandRange(.8f,1.2f) * Sharpness * Strength;
		FRandomStream DetailRandom(static_cast<int32>(GetTypeHash(Prepared.Options.Seed) ^ (uint32(I + 1) * 0x9e3779b9u)));
		End.ProfileStart = Profiles.Num();
		AppendEndProfile(MaxSplinters,Reach,Chord.Length(),DetailRandom,Profiles);
		End.ProfileCount = Profiles.Num() - End.ProfileStart;
		++ActiveEnds;
	}
	if (ActiveEnds == 0)
	{
		return true;
	}

	// A crown between two ends follows one continuous fiber, rather than retaining a rounded cap.
	for (int32 I = 0; I < Count; ++I)
	{
		auto& A = Crossings[I];
		auto& B = Crossings[(I + 1) % Count];
		if (Ends[I].bActive)
		{
			continue;
		}
		if (FMath::Abs((B.Point - A.Point).GetSafeNormal().Dot(A.Direction)) < .8)
		{
			continue;
		}
		const double EndDistance = B.Distance > A.Distance ? B.Distance : B.Distance + Prepared.Perimeter;
		bool bLocked = false;
		for (const auto& Sample : Prepared.Samples)
		{
			const double Distance = Sample.Distance <= A.Distance ? Sample.Distance + Prepared.Perimeter : Sample.Distance;
			bLocked |= Distance < EndDistance - Tolerance && Sample.MaxDisplacement == 0;
		}
		if (!bLocked)
		{
			FLocation Start = A.Location, Finish = B.Location;
			WalkGuide(Guides,Start,Ends[(I + Count - 1) % Count].bActive ? Ends[(I + Count - 1) % Count].EndShift : 0);
			WalkGuide(Guides,Finish,Ends[(I + 1) % Count].bActive ? Ends[(I + 1) % Count].StartShift : 0);
			if (ConnectedDistance(Guides,Start,AtLocation(Guides,Finish),EndDistance - A.Distance + Boundary.Cap * 2,Ends[I].AlongLength))
			{
				Ends[I].bAlong = true;
				A.Offset = B.Offset = (A.Offset + B.Offset) * .5;
			}
		}
	}

	for (int32 I = 0; I < Count; ++I)
	{
		const auto& A = Crossings[I];
		const auto& B = Crossings[(I + 1) % Count];
		const auto& End = Ends[I];
		const auto& Previous = Ends[(I + Count - 1) % Count];
		const double IncomingShift = Previous.bActive ? Previous.EndShift : Previous.bAlong && End.bActive ? End.StartShift : 0;
		FLocation Location = A.Location;
		WalkGuide(Guides,Location,IncomingShift);
		const double EndDistance = B.Distance > A.Distance ? B.Distance : B.Distance + Prepared.Perimeter;
		if (End.bAlong)
		{
			FLocation Finish = B.Location;
			WalkGuide(Guides,Finish,Ends[(I + 1) % Count].bActive ? Ends[(I + 1) % Count].StartShift : 0);
			const FLocation Start = Location;
			Boundary.Append(Out,A.Distance,AtLocation(Guides,Start) + A.Offset,true);
			TArray<FArcSample> Points;
			TArray<FGuideBend> Bends;
			WalkGuide(Guides,Location,End.AlongLength,&Bends);
			for (const auto& Bend : Bends)
			{
				const double T = Bend.Distance / End.AlongLength;
				if (T > Tolerance && T < 1 - Tolerance)
				{
					Points.Add({FMath::Lerp(A.Distance,EndDistance,T),Bend.Point + A.Offset,true});
				}
			}
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (const auto& Sample : Prepared.Samples)
				{
					const double Distance = Sample.Distance + Pass * Prepared.Perimeter;
					if (Distance <= A.Distance + Tolerance || Distance >= EndDistance - Tolerance)
					{
						continue;
					}
					const double T = (Distance - A.Distance) / (EndDistance - A.Distance);
					Location = Start;
					WalkGuide(Guides,Location,End.AlongLength * T);
					Points.Add({Distance,AtLocation(Guides,Location) + A.Offset,false});
				}
			}
			Points.Sort([](const FArcSample& L, const FArcSample& R) { return L.Distance < R.Distance; });
			for (const auto& Point : Points)
			{
				Boundary.Append(Out,Point.Distance,Point.Point,Point.bJunction);
			}
			Boundary.Append(Out,B.Distance,AtLocation(Guides,Finish) + B.Offset,true);
			continue;
		}
		Boundary.Append(Out,A.Distance,AtLocation(Guides,Location) + A.Offset,true);
		TArray<FGuideBend> Bends;
		WalkGuide(Guides,Location,(End.bActive ? End.StartShift : 0) - IncomingShift,&Bends);
		for (const auto& Bend : Bends)
		{
			Boundary.Append(Out,A.Distance,Bend.Point + A.Offset,true);
		}
		Boundary.Append(Out,A.Distance,AtLocation(Guides,Location) + A.Offset,true);
		if (End.bActive)
		{
			const FVector2d Start = AtLocation(Guides,Location) + A.Offset;
			Location = B.Location;
			WalkGuide(Guides,Location,End.EndShift);
			const FVector2d Finish = AtLocation(Guides,Location) + B.Offset;
			FVector2d Across = (A.Direction + B.Direction * (A.Direction.Dot(B.Direction) >= 0 ? 1 : -1)).GetSafeNormal();
			if (Cross2(B.Point - A.Point,Across) > 0)
			{
				Across = -Across;
			}
			TArray<double> Fractions;
			for (int32 P = End.ProfileStart + 1; P < End.ProfileStart + End.ProfileCount - 1; ++P)
			{
				Fractions.Add(Profiles[P].X);
			}
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (const auto& Sample : Prepared.Samples)
				{
					const double Distance = Sample.Distance + Pass * Prepared.Perimeter;
					if (Distance > A.Distance + Tolerance && Distance < EndDistance - Tolerance)
					{
						Fractions.Add((Distance - A.Distance) / (EndDistance - A.Distance));
					}
				}
			}
			Fractions.Sort();
			int32 Interval = End.ProfileStart;
			for (double T : Fractions)
			{
				while (Interval + 1 < End.ProfileStart + End.ProfileCount - 1 && T > Profiles[Interval + 1].X)
				{
					++Interval;
				}
				const auto& L = Profiles[Interval];
				const auto& R = Profiles[Interval + 1];
				const double Height = FMath::Lerp(L.Y,R.Y,(T - L.X) / (R.X - L.X));
				Boundary.Append(Out,FMath::Lerp(A.Distance,EndDistance,T),FMath::Lerp(Start,Finish,T) + Across * Height,false);
			}
			Boundary.Append(Out,B.Distance,Finish,true);
		}
		else
		{
			// Free arcs keep baseline detail, tapering to the authored-guide junctions.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (int32 S = 0; S < Prepared.Samples.Num(); ++S)
				{
					const auto& Sample = Prepared.Samples[S];
					const double Distance = Sample.Distance + Pass * Prepared.Perimeter;
					if (Distance <= A.Distance + Tolerance || Distance >= EndDistance - Tolerance)
					{
						continue;
					}
					const double T = (Distance - A.Distance) / (EndDistance - A.Distance);
					const FVector2d Tangent(-Sample.Normal.Y,Sample.Normal.X);
					const double Alignment = FMath::Square(Tangent.Dot(A.Direction));
					const double Taper = FMath::Min(1.,FMath::Min(T,1 - T) * 6);
					const FVector2d Noise = Noisy[S] - Sample.Point;
					Boundary.Append(Out,Distance,Sample.Point + Noise * (Taper * FMath::Pow(1 - Strength * Alignment,3)),Sample.MaxDisplacement == 0);
				}
			}
			Boundary.Append(Out,B.Distance,B.Point + B.Offset,true);
		}
	}
	if (Out.Num() > 1 && Out[0].Position.Equals(Out.Last().Position,Tolerance))
	{
		Out.Pop();
	}
	// At full influence retain authored junctions. During blending, prioritize actual geometric detail and fixed corners.
	while (Out.Num() > VertexBudget)
	{
		int32 Best = INDEX_NONE;
		double Smallest = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Out.Num(); ++I)
		{
			if (Out[I].bFixed || (Boundary.Strength == 1 && Out[I].bJunction))
			{
				continue;
			}
			const double Area = FMath::Abs(Cross2(Out[I].Position - Out[(I + Out.Num() - 1) % Out.Num()].Position,Out[(I + 1) % Out.Num()].Position - Out[I].Position)) * (Out[I].bJunction ? 1 + Boundary.Strength * 8 : 1);
			if (Area < Smallest)
			{
				Smallest = Area;
				Best = I;
			}
		}
		if (Best == INDEX_NONE)
		{
			OutError = TEXT("Grain junctions exceed the 128-vertex fracture budget.");
			return false;
		}
		Out.RemoveAt(Best);
	}
	return true;
}

namespace
{
	using namespace UE::Geometry;

	double ClosestParameter(const FVector2d& Point, const FVector2d& Start, const FVector2d& Delta)
	{
		return FMath::Clamp((Point - Start).Dot(Delta) / Delta.SquaredLength(), 0., 1.);
	}

	FVector2d Perpendicular(const FVector2d& Direction)
	{
		return FVector2d(-Direction.Y, Direction.X);
	}

	struct FImpactContact
	{
		FLocation Location;
		FVector2d Point;
		FVector2d Normal;
		double Width;
	};

	// Measure the spacing across the fiber at this point, including irregular and angled guides.
	double FiberWidth(TConstArrayView<FGuideSegment> Guides, int32 Index, const FVector2d& Point, double Radius)
	{
		const FVector2d Along = (Guides[Index].B - Guides[Index].A).GetSafeNormal();
		double Width = TNumericLimits<double>::Max();
		for (const FGuideSegment& Other : Guides)
		{
			if (Other.PathIndex == Guides[Index].PathIndex)
			{
				continue;
			}
			const FVector2d Delta = Other.B - Other.A;
			const double AlongLength = Delta.Dot(Along);
			if (FMath::Abs(AlongLength) < .75 * Delta.Length() || FMath::Abs(AlongLength) <= Tolerance)
			{
				continue;
			}
			const double T = (Point - Other.A).Dot(Along) / AlongLength;
			if (T >= 0 && T <= 1)
			{
				const double AcrossDistance = (Other.A + T * Delta - Point).Length();
				if (AcrossDistance > Tolerance)
				{
					Width = FMath::Min(Width, AcrossDistance);
				}
			}
		}
		return Width == TNumericLimits<double>::Max() ? Radius * .22 : Width;
	}

	void GatherImpactContacts(const FPolygon2d& Base, TConstArrayView<FGuideSegment> Guides, double Radius, TArray<FImpactContact>& Contacts)
	{
		const double Winding = Base.IsClockwise() ? -1 : 1;
		const FAxisAlignedBox2d Bounds = Base.Bounds();
		const double Reach = Radius * .44;
		for (int32 G = 0; G < Guides.Num(); ++G)
		{
			const FGuideSegment& Guide = Guides[G];
			const FVector2d Line = Guide.B - Guide.A;
			if (Line.Length() <= Tolerance ||
				FMath::Min(Guide.A.X, Guide.B.X) > Bounds.Max.X + Reach || FMath::Max(Guide.A.X, Guide.B.X) < Bounds.Min.X - Reach ||
				FMath::Min(Guide.A.Y, Guide.B.Y) > Bounds.Max.Y + Reach || FMath::Max(Guide.A.Y, Guide.B.Y) < Bounds.Min.Y - Reach)
			{
				continue;
			}
			const int32 FirstContact = Contacts.Num();
			double BestDistance = FMath::Square(Reach);
			FImpactContact Closest;
			bool bNear = false;
			for (int32 E = 0; E < Base.VertexCount(); ++E)
			{
				const FVector2d A = Base[E];
				const FVector2d Edge = Base[(E + 1) % Base.VertexCount()] - A;
				const FVector2d Normal = -Winding * Perpendicular(Edge.GetSafeNormal());
				const double Denominator = Cross2(Edge, Line);
				if (FMath::Abs(Denominator) > Tolerance * Edge.Length() * Line.Length())
				{
					const double T = Cross2(Guide.A - A, Line) / Denominator;
					const double U = Cross2(Guide.A - A, Edge) / Denominator;
					if (T >= 0 && T < 1 && U >= 0 && U <= 1)
					{
						const FVector2d Point = A + T * Edge;
						Contacts.Add({{G,U,1}, Point, Normal, 0});
						continue;
					}
				}

				// For disjoint segments their nearest points include at least one endpoint.
				const auto Consider = [&](double AlongEdge, double AlongGuide)
				{
					const FVector2d Point = A + AlongEdge * Edge;
					const double Distance = FVector2d::DistSquared(Point, Guide.A + AlongGuide * Line);
					if (Distance <= BestDistance)
					{
						BestDistance = Distance;
						Closest = {{G,AlongGuide,1}, Point, Normal, 0};
						bNear = true;
					}
				};
				Consider(0, ClosestParameter(A, Guide.A, Line));
				Consider(1, ClosestParameter(A + Edge, Guide.A, Line));
				Consider(ClosestParameter(Guide.A, A, Edge), 0);
				Consider(ClosestParameter(Guide.B, A, Edge), 1);
			}
			if (bNear && FirstContact == Contacts.Num())
			{
				const FVector2d Point = AtLocation(Guides, Closest.Location);
				Closest.Width = FiberWidth(Guides, G, Point, Radius);
				if (BestDistance <= FMath::Square(Closest.Width * .5) &&
					!Base.Contains(Guide.A) && !Base.Contains(Guide.B))
				{
					Contacts.Add(Closest);
				}
			}
		}

		// Segments meeting at a guide bend or tile seam can report the same contact.
		for (int32 I = Contacts.Num() - 1; I >= 0; --I)
		{
			const FImpactContact& Contact = Contacts[I];
			for (int32 J = 0; J < I; ++J)
			{
				if (Contact.Point.Equals(Contacts[J].Point, Radius * .008))
				{
					Contacts.RemoveAt(I);
					break;
				}
			}
		}
	}

	struct FImpactFrame
	{
		FVector2d Center;
		FVector2d Across;
		FVector2d Along;
		double HalfAcross;
		double HalfAlong;
	};

	FImpactFrame ImpactFrame(const FPolygon2d& Base, TConstArrayView<FGuideSegment> Guides, TConstArrayView<FImpactContact> Contacts)
	{
		const FGuideSegment& First = Guides[Contacts[0].Location.Segment];
		const FVector2d Reference = (First.B - First.A).GetSafeNormal();
		FVector2d Along = FVector2d::ZeroVector;
		for (const FImpactContact& Contact : Contacts)
		{
			const FGuideSegment& Guide = Guides[Contact.Location.Segment];
			const FVector2d Local = (Guide.B - Guide.A).GetSafeNormal();
			const double Agreement = Local.Dot(Reference);
			Along += Local * Agreement;
		}
		Along.Normalize();
		const FVector2d Across(Along.Y, -Along.X);
		FAxisAlignedBox2d Bounds;
		const FVector2d Origin = Base[0];
		for (const FVector2d& Vertex : Base.GetVertices())
		{
			const FVector2d Delta = Vertex - Origin;
			Bounds.Contain(FVector2d(Delta.Dot(Across), Delta.Dot(Along)));
		}
		const FVector2d Center = Bounds.Center();
		return {Origin + Across * Center.X + Along * Center.Y, Across, Along, Bounds.Width() * .5, Bounds.Height() * .5};
	}

	FPolygon2d PrimaryImpact(const FPolygon2d& Base, const FImpactFrame& Frame, int32 Seed)
	{
		if (!Base.Contains(Frame.Center))
		{
			return Base;
		}
		FRandomStream Random(Seed ^ 0x48053a1);
		const auto Shift = [&Random](float Radius) { return Random.FRandRange(-Radius, Radius); };
		// The long sides use continuous fiber runs. Short, independent shoulders close each end.
		FVector2d Shoulders[] = {
			{-.51 + Shift(.08f), -.84 + Shift(.10f)}, {-.13 + Shift(.13f), -.93 + Shift(.035f)},
			{.32 + Shift(.09f), -.84 + Shift(.09f)}, {.67 + Shift(.075f), -.66 + Shift(.13f)},
			{.93, -.49 + Shift(.10f)}, {.93, .39 + Shift(.11f)},
			{.70 + Shift(.095f), .68 + Shift(.115f)}, {.49 + Shift(.10f), .88 + Shift(.09f)},
			{.03 + Shift(.12f), .95 + Shift(.035f)}, {-.51 + Shift(.08f), .87 + Shift(.09f)},
			{-.70 + Shift(.095f), .62 + Shift(.11f)}, {-.93, .40 + Shift(.115f)},
			{-.93, -.46 + Shift(.13f)}, {-.67 + Shift(.085f), -.64 + Shift(.12f)}
		};
		const auto SupportAcross = [&](double Along, double Sign)
		{
			double Reach = 0;
			for (int32 Index = 0; Index < Base.VertexCount(); ++Index)
			{
				const FVector2d A = Base[Index] - Frame.Center;
				const FVector2d B = Base[(Index + 1) % Base.VertexCount()] - Frame.Center;
				const double SideA = A.Dot(Frame.Along) - Along * Frame.HalfAlong;
				const double SideB = B.Dot(Frame.Along) - Along * Frame.HalfAlong;
				if ((SideA > 0) != (SideB > 0))
				{
					const FVector2d Point = FMath::Lerp(A, B, SideA / (SideA - SideB));
					Reach = FMath::Max(Reach, Sign * Point.Dot(Frame.Across) / Frame.HalfAcross);
				}
			}
			return Reach;
		};
		for (int32 Side : {4, 11})
		{
			const double Sign = FMath::Sign(Shoulders[Side].X);
			const double Support = FMath::Min(SupportAcross(Shoulders[Side].Y, Sign), SupportAcross(Shoulders[Side + 1].Y, Sign));
			if (Support > .8)
			{
				Shoulders[Side].X = Sign * FMath::Min(.947, Support * .992);
				Shoulders[Side + 1].X = Shoulders[Side].X + Shift(.012f);
			}
		}
		FPolygon2d Candidate;
		for (const FVector2d& Shoulder : Shoulders)
		{
			const FVector2d Ray = Frame.Across * (Shoulder.X * Frame.HalfAcross) + Frame.Along * (Shoulder.Y * Frame.HalfAlong);
			double Reach = 1;
			// Keep the shoulders inside the actual impact polygon, including anisotropic accumulated hits.
			for (int32 Edge = 0; Edge < Base.VertexCount(); ++Edge)
			{
				const FVector2d A = Base[Edge] - Frame.Center;
				const FVector2d Delta = Base[(Edge + 1) % Base.VertexCount()] - Base[Edge];
				const double Denominator = Cross2(Ray, Delta);
				if (FMath::Abs(Denominator) <= Tolerance)
				{
					continue;
				}
				const double T = Cross2(A, Delta) / Denominator;
				const double U = Cross2(A, Ray) / Denominator;
				if (T > 0 && U >= 0 && U <= 1)
				{
					Reach = FMath::Min(Reach, T * .993);
				}
			}
			Candidate.AppendVertex(Frame.Center + Ray * Reach);
		}
		FString Error;
		if (BreachPatternGeometry::ValidateRegions({FGeneralPolygon2d(Candidate)}, Error))
		{
			return Candidate;
		}
		return Base;
	}

	void AddImpactTear(const FImpactContact& Contact, const FPolygon2d& Primary, TConstArrayView<FGuideSegment> Guides, const FBreachImpactGrainOptions& Options, double Radius, double AlongOdds, bool bLong, FRandomStream& Random, TArray<FGeneralPolygon2d>& Fragments)
	{
		const FGuideSegment& Guide = Guides[Contact.Location.Segment];
		const FVector2d Along = (Guide.B - Guide.A).GetSafeNormal();
		const FVector2d Across = Perpendicular(Along);
		const double Alignment = Contact.Normal.Dot(Along);
		const bool bSide = FMath::Abs(Alignment) < .63;
		const double AlongSign = bSide && Random.FRand() < .5 ? -FMath::Sign(Alignment) : FMath::Sign(Alignment);
		const double AcrossSign = bSide ? FMath::Sign(Contact.Normal.Dot(Across)) : (Random.FRand() < .5 ? -1 : 1);
		const double StartOdds = bSide ? FMath::Pow(Options.StartProbability, 2.8) : Options.StartProbability;
		const double Energy = Random.FRandRange(.80f, .94f);
		const double Resistance = Random.FRandRange(.063f, .107f) * (1 + .9 * (.55 - Options.Persistence));
		double AlongTravel = 0;
		double AcrossTravel = 0;
		if (bLong && Random.FRand() < StartOdds)
		{
			for (int32 Step = 0; Random.FRand() < Energy - Resistance * Step; ++Step)
			{
				const double Travel = Radius * Random.FRandRange(.034f, .078f);
				if (Random.FRand() < AlongOdds)
				{
					AlongTravel += Travel;
				}
				else
				{
					AcrossTravel += Travel;
				}
			}
		}

		const FVector2d OnGuide = AtLocation(Guides, Contact.Location);
		const double Spacing = Contact.Width > 0 ? Contact.Width : FiberWidth(Guides, Contact.Location.Segment, OnGuide, Radius);
		const double HalfWidth = FMath::Clamp(Spacing * Random.FRandRange(.24f, .58f), Radius * .018, Radius * .11);
		double Nearest = TNumericLimits<double>::Max();
		FVector2d Root = Contact.Point;
		FVector2d Outward = Contact.Normal;
		const double Winding = Primary.IsClockwise() ? -1 : 1;
		for (int32 Edge = 0; Edge < Primary.VertexCount(); ++Edge)
		{
			const FVector2d A = Primary[Edge];
			const FVector2d Delta = Primary[(Edge + 1) % Primary.VertexCount()] - A;
			const FVector2d Point = A + ClosestParameter(Contact.Point, A, Delta) * Delta;
			const double Distance = FVector2d::DistSquared(Point, Contact.Point);
			if (Distance < Nearest)
			{
				Nearest = Distance;
				Root = Point;
				Outward = -Winding * Perpendicular(Delta.GetSafeNormal());
			}
		}

		FLocation Location = Contact.Location;
		const double Nominal = Radius * (bLong ? Random.FRandRange(.17f, .28f) : Random.FRandRange(.023f, .071f));
		const double Remaining = Radius * Options.EndCutback * .24;
		const double Travel = bSide ? AlongTravel : FMath::Max(0., Nominal + AlongTravel - Remaining);
		WalkGuide(Guides, Location, AlongSign * Travel);
		const FVector2d GrainMotion = AtLocation(Guides, Location) - OnGuide;
		if (!bSide)
		{
			double Outermost = -TNumericLimits<double>::Max();
			for (int32 Edge = 0; Edge < Primary.VertexCount(); ++Edge)
			{
				const FVector2d A = Primary[Edge] - Contact.Point;
				const FVector2d Delta = Primary[(Edge + 1) % Primary.VertexCount()] - Primary[Edge];
				const double Denominator = Cross2(Along, Delta);
				if (FMath::Abs(Denominator) <= Tolerance)
				{
					continue;
				}
				const double Fraction = Cross2(A, Along) / Denominator;
				const double Reach = AlongSign * Cross2(A, Delta) / Denominator;
				if (Fraction >= 0 && Fraction <= 1 && Reach > Outermost)
				{
					Outermost = Reach;
					Root = Contact.Point + Along * (Reach * AlongSign);
					Outward = -Winding * Perpendicular(Delta.GetSafeNormal());
				}
			}
		}
		const FVector2d RootToRim = Contact.Point - Root;
		const double RimReach = bLong ? Random.FRandRange(.78f, 1.0f) : Random.FRandRange(.30f, .74f);
		const double SideDepth = bSide ? Nominal * .35 : 0;
		const FVector2d AcrossMotion = Across * AcrossSign * (AcrossTravel + SideDepth);
		const FVector2d Start = Root - Outward * FMath::Max(HalfWidth * 1.6, Radius * .09);
		const FVector2d Finish = Root + RootToRim * RimReach + GrainMotion + AcrossMotion;
		const FVector2d Run = Finish - Start;
		if (Run.Dot(Outward) < HalfWidth * 1.15)
		{
			return;
		}
		const FVector2d Span = bSide ? Perpendicular(Run.GetSafeNormal()) : Across;
		const FVector2d Left = Span * HalfWidth;
		const FVector2d EndShift = Span * HalfWidth * Random.FRandRange(-.38f, .38f);
		const FVector2d EndLeft = Finish + EndShift + Left * Random.FRandRange(.45f, 1.05f);
		const FVector2d EndRight = Finish + EndShift - Left * Random.FRandRange(.45f, .98f);
		const FVector2d TipAxis = Run.GetSafeNormal();
		FPolygon2d Fragment;
		Fragment.AppendVertex(Start + Left);
		Fragment.AppendVertex(FMath::Lerp(Start + Left, EndLeft, double(Random.FRandRange(.28f, .62f))) + Left * Random.FRandRange(-.24f, .24f));
		Fragment.AppendVertex(EndLeft - TipAxis * HalfWidth * Random.FRandRange(0.f, .72f));
		Fragment.AppendVertex(FMath::Lerp(EndLeft, EndRight, double(Random.FRandRange(.27f, .71f))) + TipAxis * HalfWidth * Random.FRandRange(-.49f, .59f));
		Fragment.AppendVertex(EndRight - TipAxis * HalfWidth * Random.FRandRange(.02f, .72f));
		Fragment.AppendVertex(FMath::Lerp(Start - Left, EndRight, double(Random.FRandRange(.28f, .61f))) - Left * Random.FRandRange(-.24f, .24f));
		Fragment.AppendVertex(Start - Left);
		if (Fragment.IsClockwise())
		{
			Fragment.Reverse();
		}
		FString Error;
		if (BreachPatternGeometry::ValidateRegions({FGeneralPolygon2d(Fragment)}, Error))
		{
			Fragments.Emplace(Fragment);
		}
	}
	FGeneralPolygon2d BreakShoulders(const FGeneralPolygon2d& Region, const FImpactFrame& Frame, double Radius, double Cutback, int32 Seed)
	{
		const FPolygon2d& Outer = Region.GetOuter();
		FRandomStream Random(Seed ^ 0x6491b52);
		FPolygon2d Outline;
		for (int32 Index = 0; Index < Outer.VertexCount(); ++Index)
		{
			const FVector2d A = Outer[Index];
			const FVector2d Edge = Outer[(Index + 1) % Outer.VertexCount()] - A;
			const double Span = Edge.Length();
			const double Across = Edge.Dot(Frame.Across);
			Outline.AppendVertex(A);
			if (Span < Radius * .135 || FMath::Abs(Across) < Span * .49 || Random.FRand() < .24f)
			{
				continue;
			}
			const double Sign = FMath::Sign(Across);
			const int32 Count = Span > Radius * .31 && Random.FRand() < .68f ? 2 : 1;
			for (int32 Crack = 0; Crack < Count; ++Crack)
			{
				const double Start = (Crack + Random.FRandRange(.14f, .40f)) / Count;
				const double Width = Random.FRandRange(.23f, .46f) / Count;
				const double Depth = FMath::Min(Radius * Random.FRandRange(.032f, .112f), Span * .43);
				const double Side = Random.FRand() < .38f ? -1 : 1;
				const FVector2d Split = Frame.Along * (Sign * Side * Depth * (1 + Cutback));
				Outline.AppendVertex(A + Edge * Start);
				Outline.AppendVertex(A + Edge * (Start + Width * .19) + Split);
				Outline.AppendVertex(A + Edge * (Start + Width * Random.FRandRange(.54f, .86f)) + Split * Random.FRandRange(.38f, .91f));
				Outline.AppendVertex(A + Edge * (Start + Width));
			}
		}
		FGeneralPolygon2d Result(Outline);
		for (const FPolygon2d& Hole : Region.GetHoles())
		{
			Result.AddHole(Hole, false, false);
		}
		FString Error;
		return BreachPatternGeometry::ValidateRegions({Result}, Error) ? Result : Region;
	}

	FGeneralPolygon2d DetailExposedEnds(const FGeneralPolygon2d& Region, const UBreachPattern& Pattern, TConstArrayView<FGuideSegment> Guides, double Radius, int32 Seed)
	{
		check(IsValid(&Pattern));
		const FPolygon2d& Outer = Region.GetOuter();
		TArray<TArray<FVector2d, TInlineAllocator<24>>> Patches;
		Patches.SetNum(Outer.VertexCount());
		for (int32 I = 0; I < Outer.VertexCount(); ++I)
		{
			const FVector2d A = Outer[I];
			const FVector2d B = Outer[(I + 1) % Outer.VertexCount()];
			const FVector2d Edge = B - A;
			const double Span = Edge.Length();
			if (Span < Radius * .034 || Span > Radius * .35)
			{
				continue;
			}
			const FVector2d Mid = (A + B) * .5;
			FVector2d Along = FVector2d::ZeroVector;
			double Nearest = FMath::Square(Radius * .30);
			for (const FGuideSegment& Guide : Guides)
			{
				const FVector2d Line = Guide.B - Guide.A;
				if (Line.Length() <= Tolerance)
				{
					continue;
				}
				const double T = ClosestParameter(Mid, Guide.A, Line);
				const double Distance = FVector2d::DistSquared(Mid, Guide.A + T * Line);
				if (Distance < Nearest)
				{
					Along = Line.GetSafeNormal();
					Nearest = Distance;
				}
			}
			const FVector2d Before = A - Outer[(I + Outer.VertexCount() - 1) % Outer.VertexCount()];
			const FVector2d After = Outer[(I + 2) % Outer.VertexCount()] - B;
			const auto IsAlong = [&](const FVector2d& Delta)
			{
				return FMath::Abs(Delta.Dot(Along)) > FMath::Max(Radius * .021, 1.25 * FMath::Abs(Cross2(Delta, Along)));
			};
			if (Along.IsNearlyZero() || FMath::Abs(Cross2(Edge, Along)) < .72 * Span || (!IsAlong(Before) && !IsAlong(After)))
			{
				continue;
			}
			FRandomStream Random(static_cast<int32>(GetTypeHash(Seed) ^ (uint32(I + 1) * 0x9e3779b9u)));
			if (Random.FRand() < .22f)
			{
				continue;
			}
			const int32 Count = FMath::Min(Random.RandRange(1, Pattern.GrainMaxEndSplinters), FMath::Max(1, FMath::FloorToInt(Span / (Radius * .047))));
			const double Sharpness = Random.FRandRange(Pattern.ImpactGrain.MinEndSharpness, Pattern.GrainEndSharpness);
			const int32 Dominant = Random.RandRange(0, Count - 1);
			for (int32 Tooth = 0; Tooth < Count; ++Tooth)
			{
				const double Start = (Tooth + Random.FRandRange(.14f, .26f)) / Count;
				const double End = (Tooth + Random.FRandRange(.76f, .86f)) / Count;
				const double At = FMath::Lerp(Start, End, double(Random.FRandRange(.21f, .76f)));
				const double Scale = Tooth == Dominant ? Random.FRandRange(.70f, .98f) : Random.FRandRange(.25f, .60f);
				const double Depth = FMath::Min(Span * 1.32, Radius * .152) * Sharpness * Scale;
				const double Direction = Random.FRand() < .5 ? -1 : 1;
				Patches[I].Add(A + Edge * Start);
				Patches[I].Add(A + Edge * At + Along * Depth * Direction);
				Patches[I].Add(A + Edge * End);
			}
		}
		FString Error;
		for (double Scale : {1., .65, .35})
		{
			FPolygon2d Detailed;
			for (int32 I = 0; I < Outer.VertexCount(); ++I)
			{
				Detailed.AppendVertex(Outer[I]);
				const FVector2d Edge = Outer[(I + 1) % Outer.VertexCount()] - Outer[I];
				for (const FVector2d& Point : Patches[I])
				{
					const FVector2d OnEdge = Outer[I] + Edge * ClosestParameter(Point, Outer[I], Edge);
					Detailed.AppendVertex(FMath::Lerp(OnEdge, Point, Scale));
				}
			}
			FGeneralPolygon2d Candidate(Detailed);
			for (const auto& Hole : Region.GetHoles())
			{
				Candidate.AddHole(Hole, false, false);
			}
			if (BreachPatternGeometry::ValidateRegions({Candidate}, Error))
			{
				return Candidate;
			}
		}
		return Region;
	}
}

bool BuildImpact(const UBreachPattern& Pattern, const UE::Geometry::FPolygon2d& Base, TConstArrayView<FGuideSegment> Guides, int32 Seed, double Influence, TArray<UE::Geometry::FGeneralPolygon2d>& Out, FString& OutError)
{
	check(IsValid(&Pattern));
	const FBreachImpactGrainOptions& Options = Pattern.ImpactGrain;
	const FAxisAlignedBox2d Bounds = Base.Bounds();
	const double Radius = .5 * FMath::Min(Bounds.Width(), Bounds.Height());
	TArray<FImpactContact> Contacts;
	GatherImpactContacts(Base, Guides, Radius, Contacts);
	FPolygon2d Primary = Base;
	FImpactFrame Frame{};
	if (!Contacts.IsEmpty())
	{
		Frame = ImpactFrame(Base, Guides, Contacts);
		if (Contacts.Num() >= 4)
		{
			Primary = PrimaryImpact(Base, Frame, Seed);
		}
	}
	if (Primary.IsClockwise())
	{
		Primary.Reverse();
	}
	TArray<FGeneralPolygon2d> Fragments{FGeneralPolygon2d(Primary)};
	if (!Contacts.IsEmpty())
	{
		const double AlongOdds = .5 + .44 * Influence;
		TArray<int32> Groups[4];
		for (int32 Index = 0; Index < Contacts.Num(); ++Index)
		{
			const FVector2d Normal = Contacts[Index].Normal;
			const double Direction = Normal.Dot(Frame.Along);
			const double Across = Normal.Dot(Frame.Across);
			const int32 Group = FMath::Abs(Direction) >= .63 ? (Direction < 0 ? 0 : 1) : (Across < 0 ? 2 : 3);
			Groups[Group].Add(Index);
		}

		// A few major failures set the silhouette. Nearby ends stay intact or break in smaller groups.
		for (int32 Group = 0; Group < 4; ++Group)
		{
			const bool bAlongEnd = Group < 2;
			const auto Rank = [&](int32 Index)
			{
				FRandomStream Random(static_cast<int32>(GetTypeHash(Seed) ^ (uint32(Index + 1) * 0xc2b2ae35u)));
				const FVector2d FromCenter = Contacts[Index].Point - Frame.Center;
				if (bAlongEnd)
				{
					const double Across = FMath::Abs(FromCenter.Dot(Frame.Across)) / Frame.HalfAcross;
					return Random.FRand() + FMath::Abs(Across - .30) * 1.4;
				}
				const double Along = FMath::Abs(FromCenter.Dot(Frame.Along)) / Frame.HalfAlong;
				return Random.FRand() + FMath::Abs(Along - .48) * 1.5;
			};
			Groups[Group].Sort([&Rank](int32 A, int32 B) { return Rank(A) < Rank(B); });
			FRandomStream GroupRandom(static_cast<int32>(GetTypeHash(Seed) ^ (uint32(Group + 1) * 0x27d4eb2du)));
			const int32 LongCount = bAlongEnd ? (GroupRandom.FRand() < .38f ? 2 : 1) : 1;
			const int32 Limit = bAlongEnd ? LongCount + GroupRandom.RandRange(1, 3) : GroupRandom.RandRange(1, 2);
			TArray<FVector2d, TInlineAllocator<5>> Roots;
			for (int32 Index : Groups[Group])
			{
				const FImpactContact& Contact = Contacts[Index];
				if (Roots.ContainsByPredicate([&](const FVector2d& Point) { return FVector2d::DistSquared(Point, Contact.Point) < FMath::Square(Radius * .11); }))
				{
					continue;
				}
				FRandomStream Random(static_cast<int32>(GetTypeHash(Seed) ^ (uint32(Index + 1) * 0x85ebca6bu)));
				AddImpactTear(Contact, Primary, Guides, Options, Radius, AlongOdds, Roots.Num() < LongCount, Random, Fragments);
				Roots.Add(Contact.Point);
				if (Roots.Num() >= Limit)
				{
					break;
				}
			}
		}
	}
	if (Fragments.Num() > 1)
	{
		if (!PolygonsUnion(Fragments, Out, false) || Out.IsEmpty())
		{
			OutError = TEXT("Impact grain fragments could not form a valid cut.");
			return false;
		}
		// Only material connected to the main opening is cut.
		Out.Sort([](const FGeneralPolygon2d& A, const FGeneralPolygon2d& B) { return A.SignedArea() > B.SignedArea(); });
		Out.SetNum(1);
	}
	else
	{
		Out = MoveTemp(Fragments);
	}
	if (Contacts.Num() >= 4)
	{
		Out[0] = BreakShoulders(Out[0], Frame, Radius, Options.EndCutback, Seed);
	}
	if (!Contacts.IsEmpty() && Pattern.GrainMaxEndSplinters > 0 && Pattern.GrainEndSharpness > 0)
	{
		Out[0] = DetailExposedEnds(Out[0], Pattern, Guides, Radius, Seed);
	}
	return BreachPatternGeometry::ValidateRegions(Out, OutError);
}
}
