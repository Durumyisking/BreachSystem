#include "Shape/BreachContour.h"
#include "SegmentTypes.h"

namespace
{
	using namespace UE::Geometry;
	constexpr int32 MaxVertices = 2048;

	double Cross(const FVector2d& A, const FVector2d& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	// Periodic asymmetric teeth. Width is measured along the boundary, not by vertex count.
	struct FBoundaryDetail
	{
		TArray<FVector2d> Peaks; // position within interval, signed amplitude
		double Period = 1;
		double Phase = 0;

		void Initialize(double Perimeter, double Width, double Depth, FRandomStream& Random)
		{
			if (Depth == 0)
			{
				return;
			}
			const int32 Count = FMath::Clamp(FMath::CeilToInt(Perimeter / Width), 2, MaxVertices);
			Period = Perimeter / Count;
			Phase = Random.FRand() * Perimeter;
			const double Sign = Random.RandRange(0, 1) ? 1.0 : -1.0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Peaks.Emplace(Random.FRandRange(0.25f, 0.75f),
					Depth * Random.FRandRange(0.6f, 1.f) * (Index % 2 ? -Sign : Sign));
			}
		}

		double Evaluate(double Distance) const
		{
			if (Peaks.IsEmpty())
			{
				return 0;
			}
			const double Position = FMath::Fmod(Distance + Phase, Period * Peaks.Num()) / Period;
			const int32 Index = FMath::Min(FMath::FloorToInt(Position), Peaks.Num() - 1);
			const double T = Position - Index;
			const FVector2d Peak = Peaks[Index];
			return Peak.Y * (T < Peak.X ? T / Peak.X : (1 - T) / (1 - Peak.X));
		}
	};
}

bool BreachContour::Validate(const UE::Geometry::FPolygon2d& Polygon, FString& OutError)
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
		const FVector2d Edge = B - A;
		if (Edge.Length() <= Tolerance)
		{
			OutError = TEXT("Boundary contains a duplicate vertex or an edge below the geometric tolerance.");
			return false;
		}
		const FVector2d Previous = A - Polygon[(Index + Count - 1) % Count];
		if (Previous.Dot(Edge) < 0 && FMath::Abs(Cross(Previous, Edge)) <= Tolerance * (Previous.Length() + Edge.Length()))
		{
			OutError = TEXT("Adjacent boundary edges fold back over each other.");
			return false;
		}
		TwiceArea += Cross(A - Polygon[0], B - Polygon[0]);
		const FSegment2d Segment(A, B);
		for (int32 Other = Index + 2; Other < Count; ++Other)
		{
			if (Index == 0 && Other == Count - 1)
			{
				continue;
			}
			const FSegment2d OtherSegment(Polygon[Other], Polygon[(Other + 1) % Count]);
			if (Segment.Intersects(OtherSegment, 1.e-10, Tolerance))
			{
				OutError = TEXT("Non-adjacent boundary edges cross or touch.");
				return false;
			}
		}
	}
	if (!FMath::IsFinite(TwiceArea) || FMath::Abs(TwiceArea) <= Tolerance * Tolerance)
	{
		OutError = TEXT("Boundary area is degenerate.");
		return false;
	}
	return true;
}

bool BreachContour::Generate(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
	UE::Geometry::FPolygon2d& OutPolygon, FString& OutError)
{
	const UE::Geometry::FPolygon2d Input = Base; // Also permits an in-place caller.
	OutPolygon = UE::Geometry::FPolygon2d();
	OutError.Reset();
	if (!Options.bEnabled)
	{
		OutPolygon = Input;
		return true;
	}
	if (!Validate(Input, OutError))
	{
		return false;
	}
	if (!FMath::IsFinite(Options.MacroDepth) || Options.MacroDepth < 0
		|| !FMath::IsFinite(Options.MediumDepth) || Options.MediumDepth < 0
		|| !FMath::IsFinite(Options.MicroDepth) || Options.MicroDepth < 0
		|| !FMath::IsFinite(Options.MacroWidth) || Options.MacroWidth < 0.1f
		|| !FMath::IsFinite(Options.MediumWidth) || Options.MediumWidth < 0.1f
		|| !FMath::IsFinite(Options.MicroWidth) || Options.MicroWidth < 0.1f
		|| !FMath::IsFinite(Options.MaxDisplacementFraction) || Options.MaxDisplacementFraction < 0
		|| Options.MaxDisplacementFraction > 0.45f)
	{
		OutError = TEXT("Contour requires finite non-negative depths, widths/spacing >= 0.1 cm, and displacement fraction in [0, 0.45].");
		return false;
	}
	if (Options.MaxDisplacementFraction == 0 || (Options.MacroDepth == 0 && Options.MediumDepth == 0 && Options.MicroDepth == 0))
	{
		OutPolygon = Input;
		return true;
	}
	FPolygon2d Boundary = Input;
	if (Boundary.IsClockwise())
	{
		Boundary.Reverse();
	}
	const int32 Count = Boundary.VertexCount();
	double Spacing = Options.MicroWidth;
	if (Options.MacroDepth > 0)
	{
		Spacing = FMath::Min(Spacing, Options.MacroWidth / 6.0);
	}
	if (Options.MediumDepth > 0)
	{
		Spacing = FMath::Min(Spacing, Options.MediumWidth / 6.0);
	}
	TArray<double> Starts, Lengths, CornerDistances;
	TArray<int32> Steps;
	double Perimeter = 0;
	int32 SampleCount = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Starts.Add(Perimeter);
		const double Length = (Boundary[(Index + 1) % Count] - Boundary[Index]).Length();
		Lengths.Add(Length);
		const double NumSteps = FMath::CeilToDouble(Length / Spacing);
		if (NumSteps > MaxVertices - SampleCount)
		{
			OutError = TEXT("Contour sampling exceeds 2048 vertices; increase spacing or feature widths.");
			return false;
		}
		Steps.Add(int32(NumSteps));
		SampleCount += int32(NumSteps);
		const FVector2d Prev = (Boundary[Index] - Boundary[(Index + Count - 1) % Count]).GetSafeNormal();
		const FVector2d Next = (Boundary[(Index + 1) % Count] - Boundary[Index]).GetSafeNormal();
		if (Prev.Dot(Next) < 0.8660254037844386)
		{
			CornerDistances.Add(Perimeter);
		}
		Perimeter += Length;
	}
	const auto Bounds = Boundary.Bounds();
	const double MaxDisplacement = FMath::Min(Bounds.Width(), Bounds.Height()) * Options.MaxDisplacementFraction;
	FRandomStream Random(Options.Seed == 0 ? FMath::Rand() : Options.Seed);
	FBoundaryDetail Macro, Medium;
	Macro.Initialize(Perimeter, Options.MacroWidth, Options.MacroDepth, Random);
	Medium.Initialize(Perimeter, Options.MediumWidth, Options.MediumDepth, Random);
	TArray<FVector2d> Points, Offsets;
	Points.Reserve(SampleCount);
	Offsets.Reserve(SampleCount);
	for (int32 EdgeIndex = 0; EdgeIndex < Count; ++EdgeIndex)
	{
		const FVector2d A = Boundary[EdgeIndex], B = Boundary[(EdgeIndex + 1) % Count];
		const FVector2d Tangent = (B - A) / Lengths[EdgeIndex];
		for (int32 Step = 0; Step < Steps[EdgeIndex]; ++Step)
		{
			const double T = double(Step) / Steps[EdgeIndex];
			const FVector2d Point = FMath::Lerp(A, B, T);
			const double Distance = Starts[EdgeIndex] + T * Lengths[EdgeIndex];
			FVector2d Direction = Tangent;
			if (Step == 0)
			{
				const FVector2d Previous = (A - Boundary[(EdgeIndex + Count - 1) % Count]).GetSafeNormal();
				Direction = (Previous + Tangent).GetSafeNormal();
			}
			const FVector2d Normal(Direction.Y, -Direction.X);
			double CornerWeight = 1;
			for (double Corner : CornerDistances)
			{
				const double Delta = FMath::Abs(Distance - Corner);
				CornerWeight = FMath::Min(CornerWeight, FMath::Min(Delta, Perimeter - Delta) / (Spacing * 2));
			}
			double Limit = MaxDisplacement;
			// Limit movement across thin necks by the next boundary along this normal.
			for (int32 Other = 0; Other < Count; ++Other)
			{
				if (Other == EdgeIndex || Other == (EdgeIndex + Count - 1) % Count || Other == (EdgeIndex + 1) % Count)
				{
					continue;
				}
				const FVector2d C = Boundary[Other], Edge = Boundary[(Other + 1) % Count] - C;
				const double Denominator = Cross(Normal, Edge);
				if (FMath::Abs(Denominator) < 1.e-10)
				{
					continue;
				}
				const double U = Cross(C - Point, Normal) / Denominator;
				if (U >= 0 && U <= 1)
				{
					Limit = FMath::Min(Limit, 0.4 * FMath::Abs(Cross(C - Point, Edge) / Denominator));
				}
			}
			const double Detail = Macro.Evaluate(Distance) + Medium.Evaluate(Distance) + Random.FRandRange(-Options.MicroDepth, Options.MicroDepth);
			Points.Add(Point);
			Offsets.Add(Normal * (FMath::Clamp(Detail, -Limit, Limit) * CornerWeight));
		}
	}
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		FPolygon2d Candidate;
		const double Strength = 1.0 / (1 << Attempt);
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Candidate.AppendVertex(Points[Index] + Offsets[Index] * Strength);
		}
		if (Validate(Candidate, OutError))
		{
			if (Candidate.Area() > 0)
			{
				OutPolygon = MoveTemp(Candidate);
				return true;
			}
			OutError = TEXT("Contour reversed the base orientation.");
		}
	}
	OutError = TEXT("Contour remained invalid after four bounded attempts: ") + OutError;
	return false;
}
