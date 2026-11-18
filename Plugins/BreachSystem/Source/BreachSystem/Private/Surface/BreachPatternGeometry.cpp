#include "Surface/BreachPatternGeometry.h"
#include "Surface/BreachPattern.h"
#include "BreachGrainFracture.h"
#include "Curve/PolygonIntersectionUtils.h"

namespace
{
	using namespace UE::Geometry;
	constexpr double GeometryTolerance = 1.e-10;
	constexpr int64 MaxTileCandidates = 65536;

	double PatternCross(const FVector2d& A, const FVector2d& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	bool Finite(const FVector2d& Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);
	}

	bool OnSegment(const FVector2d& Point, const FVector2d& A, const FVector2d& B)
	{
		const double Tolerance = GeometryTolerance * (B - A).Length();
		return FMath::Abs(PatternCross(B - A, Point - A)) <= Tolerance
			&& Point.X >= FMath::Min(A.X, B.X) - GeometryTolerance && Point.X <= FMath::Max(A.X, B.X) + GeometryTolerance
			&& Point.Y >= FMath::Min(A.Y, B.Y) - GeometryTolerance && Point.Y <= FMath::Max(A.Y, B.Y) + GeometryTolerance;
	}

	bool SegmentsTouch(const FVector2d& A, const FVector2d& B, const FVector2d& C, const FVector2d& D)
	{
		const double AB_C = PatternCross(B - A, C - A);
		const double AB_D = PatternCross(B - A, D - A);
		const double CD_A = PatternCross(D - C, A - C);
		const double CD_B = PatternCross(D - C, B - C);
		if (((AB_C > 0 && AB_D < 0) || (AB_C < 0 && AB_D > 0))
			&& ((CD_A > 0 && CD_B < 0) || (CD_A < 0 && CD_B > 0)))
		{
			return true;
		}
		return OnSegment(C, A, B) || OnSegment(D, A, B) || OnSegment(A, C, D) || OnSegment(B, C, D);
	}

	bool SimplePath(TConstArrayView<FVector2d> Points, bool bClosed, FString& OutError)
	{
		if (Points.Num() < (bClosed ? 3 : 2))
		{
			OutError = TEXT("A region loop needs at least three vertices; an open guide needs at least two.");
			return false;
		}
		for (const FVector2d& Point : Points)
		{
			if (!Finite(Point))
			{
				OutError = TEXT("Pattern geometry contains a non-finite coordinate.");
				return false;
			}
		}
		const int32 EdgeCount = Points.Num() - (bClosed ? 0 : 1);
		for (int32 Index = 0; Index < EdgeCount; ++Index)
		{
			const FVector2d& A = Points[Index];
			const FVector2d& B = Points[(Index + 1) % Points.Num()];
			if (!Finite(B - A) || (B - A).Length() <= GeometryTolerance)
			{
				OutError = TEXT("Pattern geometry contains a degenerate edge.");
				return false;
			}
			if (bClosed || Index + 2 < Points.Num())
			{
				const FVector2d& C = Points[(Index + 2) % Points.Num()];
				if (OnSegment(C, A, B) || OnSegment(A, B, C))
				{
					OutError = TEXT("Adjacent pattern edges overlap or reverse direction.");
					return false;
				}
			}
			for (int32 Other = Index + 2; Other < EdgeCount; ++Other)
			{
				if (bClosed && Index == 0 && Other == EdgeCount - 1)
				{
					continue;
				}
				if (SegmentsTouch(A, B, Points[Other], Points[(Other + 1) % Points.Num()]))
				{
					OutError = TEXT("Pattern path intersects or touches itself.");
					return false;
				}
			}
		}
		if (bClosed)
		{
			double Area = 0;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				Area += PatternCross(Points[Index] - Points[0], Points[(Index + 1) % Points.Num()] - Points[0]);
			}
			if (!FMath::IsFinite(Area) || FMath::Abs(Area) <= GeometryTolerance * GeometryTolerance)
			{
				OutError = TEXT("Pattern loop has degenerate area.");
				return false;
			}
		}
		return true;
	}

	bool BoundariesTouch(const FPolygon2d& A, const FPolygon2d& B)
	{
		for (int32 Index = 0; Index < A.VertexCount(); ++Index)
		{
			for (int32 Other = 0; Other < B.VertexCount(); ++Other)
			{
				if (SegmentsTouch(A[Index], A[(Index + 1) % A.VertexCount()], B[Other], B[(Other + 1) % B.VertexCount()]))
				{
					return true;
				}
			}
		}
		return false;
	}

	FPolygon2d OrientedLoop(TConstArrayView<FVector2d> Points, bool bClockwise)
	{
		FPolygon2d Result{TArray<FVector2d>(Points)};
		if (Result.IsClockwise() != bClockwise)
		{
			Result.Reverse();
		}
		return Result;
	}

	FGeneralPolygon2d RegionPolygon(const FBreachPatternRegion& Region)
	{
		FGeneralPolygon2d Result(OrientedLoop(Region.Outer, false));
		for (const FBreachPatternLoop& Hole : Region.Holes)
		{
			Result.AddHole(OrientedLoop(Hole.Vertices, true), false, false);
		}
		return Result;
	}

	struct FMapping
	{
		FBreachPatternMapping UV;
		double Determinant = 1;

		FVector2d ToUV(const FVector2d& Point) const
		{
			return UV.UVOffset + UV.UVPerX * Point.X + UV.UVPerY * Point.Y;
		}

		FVector2d ToMetric(const FVector2d& Point) const
		{
			const FVector2d Relative = Point - UV.UVOffset;
			return FVector2d(PatternCross(Relative, UV.UVPerY), PatternCross(UV.UVPerX, Relative)) / Determinant;
		}
	};

	FPolygon2d TransformLoop(const FPolygon2d& Loop, const FMapping& Mapping, const FVector2d& Tile, bool bToUV)
	{
		FPolygon2d Result;
		for (const FVector2d& Point : Loop.GetVertices())
		{
			Result.AppendVertex(bToUV ? Mapping.ToUV(Point) : Mapping.ToMetric(Point + Tile));
		}
		return Result;
	}

	FGeneralPolygon2d MetricRegion(const FGeneralPolygon2d& Region, const FMapping& Mapping, const FVector2d& Tile)
	{
		FPolygon2d Outer = TransformLoop(Region.GetOuter(), Mapping, Tile, false);
		if (Outer.IsClockwise())
		{
			Outer.Reverse();
		}
		FGeneralPolygon2d Result(Outer);
		for (const FPolygon2d& Hole : Region.GetHoles())
		{
			FPolygon2d MetricHole = TransformLoop(Hole, Mapping, Tile, false);
			if (!MetricHole.IsClockwise())
			{
				MetricHole.Reverse();
			}
			Result.AddHole(MetricHole, false, false);
		}
		return Result;
	}

	bool TileRange(const FAxisAlignedBox2d& Target, const FAxisAlignedBox2d& Source, FIntPoint& OutMin, FIntPoint& OutMax, int64& Remaining, FString& OutError)
	{
		const FVector2d Minimum = Target.Min - Source.Max;
		const FVector2d Maximum = Target.Max - Source.Min;
		if (!Finite(Minimum) || !Finite(Maximum) || Minimum.X <= MIN_int32 + 1.0 || Minimum.Y <= MIN_int32 + 1.0 || Maximum.X >= MAX_int32 - 1.0 || Maximum.Y >= MAX_int32 - 1.0)
		{
			OutError = TEXT("Pattern UV tile range exceeds supported integer coordinates.");
			return false;
		}
		OutMin = FIntPoint(FMath::CeilToInt(Minimum.X - GeometryTolerance), FMath::CeilToInt(Minimum.Y - GeometryTolerance));
		OutMax = FIntPoint(FMath::FloorToInt(Maximum.X + GeometryTolerance), FMath::FloorToInt(Maximum.Y + GeometryTolerance));
		const int64 Width = FMath::Max<int64>(0, int64(OutMax.X) - OutMin.X + 1);
		const int64 Height = FMath::Max<int64>(0, int64(OutMax.Y) - OutMin.Y + 1);
		if (Width == 0 || Height == 0)
		{
			return true;
		}
		if (Width > Remaining || (Width > 0 && Height > Remaining / Width))
		{
			OutError = TEXT("Pattern generation exceeds the 65536 local tile candidate budget.");
			return false;
		}
		Remaining -= Width * Height;
		return true;
	}

	bool RegionTouches(const FGeneralPolygon2d& Region, const FPolygon2d& Base)
	{
		if (BoundariesTouch(Region.GetOuter(), Base))
		{
			return true;
		}
		for (const FPolygon2d& Hole : Region.GetHoles())
		{
			if (BoundariesTouch(Hole, Base))
			{
				return true;
			}
		}
		return Region.Contains(Base[0]) || Base.Contains(Region.GetOuter()[0]);
	}

	bool GenerateRegions(const UBreachPattern& Pattern, const FPolygon2d& Base, const FMapping& Mapping, TArray<FGeneralPolygon2d>& Out, FString& OutError)
	{
		check(IsValid(&Pattern));
		const FPolygon2d BaseUV = TransformLoop(Base, Mapping, FVector2d::ZeroVector, true);
		TArray<FGeneralPolygon2d> Selected;
		int64 Remaining = MaxTileCandidates;
		for (const FBreachPatternRegion& Region : Pattern.Regions)
		{
			const FGeneralPolygon2d Canonical = RegionPolygon(Region);
			FIntPoint Min, Max;
			if (!TileRange(BaseUV.Bounds(), Canonical.Bounds(), Min, Max, Remaining, OutError))
			{
				return false;
			}
			if (Min.X > Max.X || Min.Y > Max.Y)
			{
				continue;
			}
			// Each region index and integer tile is visited once, using only the original cutter.
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					FGeneralPolygon2d Candidate = MetricRegion(Canonical, Mapping, FVector2d(X, Y));
					if (RegionTouches(Candidate, Base))
					{
						Selected.Add(MoveTemp(Candidate));
					}
				}
			}
		}
		if (Selected.IsEmpty())
		{
			Out.Reset();
			return true;
		}
		if (!PolygonsUnion(Selected, Out, false))
		{
			OutError = TEXT("Selected pattern regions could not be united.");
			return false;
		}
		return BreachPatternGeometry::ValidateRegions(Out, OutError);
	}

	using BreachGrainFracture::FGuideSegment;

	bool GatherGuides(const UBreachPattern& Pattern, const FPolygon2d& Base, const FMapping& Mapping, double Radius, TArray<FGuideSegment>& Out, FString& OutError)
	{
		check(IsValid(&Pattern));
		const FAxisAlignedBox2d Bounds = Base.Bounds();
		FPolygon2d Search;
		Search.AppendVertex(Mapping.ToUV(Bounds.Min - FVector2d(Radius, Radius)));
		Search.AppendVertex(Mapping.ToUV(FVector2d(Bounds.Max.X + Radius, Bounds.Min.Y - Radius)));
		Search.AppendVertex(Mapping.ToUV(Bounds.Max + FVector2d(Radius, Radius)));
		Search.AppendVertex(Mapping.ToUV(FVector2d(Bounds.Min.X - Radius, Bounds.Max.Y + Radius)));
		int64 Remaining = MaxTileCandidates;
		int32 PathIndex = 0;
		for (const FBreachPatternGuide& Guide : Pattern.Guides)
		{
			FPolygon2d GuideBounds(Guide.Points);
			FIntPoint Min, Max;
			if (!TileRange(Search.Bounds(), GuideBounds.Bounds(), Min, Max, Remaining, OutError))
			{
				return false;
			}
			if (Min.X > Max.X || Min.Y > Max.Y)
			{
				continue;
			}
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					const int32 Instance = PathIndex++;
					for (int32 Index = 0; Index + 1 < Guide.Points.Num(); ++Index)
					{
						const FVector2d Tile(X, Y);
						Out.Add({Mapping.ToMetric(Guide.Points[Index] + Tile), Mapping.ToMetric(Guide.Points[Index + 1] + Tile), Instance});
					}
				}
			}
		}
		return true;
	}

	bool GenerateContour(const FPolygon2d& Base, const FBreachContourOptions& Options, TArray<FGeneralPolygon2d>& Out, FString& OutError)
	{
		FBreachContourOptions Deterministic = Options;
		if (Deterministic.Seed == 0)
		{
			Deterministic.Seed = 1;
		}
		BreachContour::FPreparedContour Prepared;
		if (!BreachContour::Prepare(Base, Deterministic, Prepared, OutError))
		{
			return false;
		}
		if (Prepared.Samples.IsEmpty())
		{
			Out.Emplace(Prepared.Base);
			return true;
		}
		FPolygon2d Noisy;
		BreachContour::Generate(Prepared, Noisy);

		for (int32 Attempt = 0; Attempt <= 4; ++Attempt)
		{
			FPolygon2d Candidate;
			const double Scale = FMath::Pow(0.5, Attempt);
			for (int32 Index = 0; Index < Prepared.Samples.Num(); ++Index)
			{
				const FVector2d& Reference = Prepared.Samples[Index].Point;
				Candidate.AppendVertex(Reference + (Noisy[Index] - Reference) * Scale);
			}
			if (SimplePath(Candidate.GetVertices(), true, OutError))
			{
				if (!Candidate.IsClockwise())
				{
					Out.Emplace(Candidate);
					OutError.Reset();
					return true;
				}
			}
		}
		OutError = TEXT("Pattern contour remained invalid after the initial proposal and four halved-displacement retries.");
		return false;
	}
}

bool BreachPatternGeometry::Validate(const UBreachPattern& Pattern, FString& OutError)
{
	OutError.Reset();
	if (!IsValid(&Pattern))
	{
		OutError = TEXT("Pattern asset is not valid.");
		return false;
	}
	if (Pattern.Mode == EBreachPatternMode::None)
	{
		return true;
	}
	if (Pattern.Mode == EBreachPatternMode::Regions)
	{
		if (Pattern.Regions.IsEmpty())
		{
			OutError = TEXT("Regions mode requires at least one region.");
			return false;
		}
		TArray<FGeneralPolygon2d> Regions;
		for (const FBreachPatternRegion& Region : Pattern.Regions)
		{
			Regions.Add(RegionPolygon(Region));
		}
		return ValidateRegions(Regions, OutError);
	}
	if (Pattern.Mode == EBreachPatternMode::GrainGuides)
	{
		const FBreachLineTearOptions& Tear = Pattern.LineTear;
		const auto InRange = [](float Value, float Min, float Max) { return FMath::IsFinite(Value) && Value >= Min && Value <= Max; };
		if (!InRange(Tear.TearLengthMin, -.5f, 1.5f) || !InRange(Tear.TearLengthMax, -.5f, 1.5f) || Tear.TearLengthMin > Tear.TearLengthMax)
		{
			OutError = TEXT("Tear length must be finite, between -0.5 and 1.5, with minimum not above maximum.");
			return false;
		}
		if (!InRange(Tear.MinTearLength, 0, 1))
		{
			OutError = TEXT("Minimum tear length must be finite and between zero and one.");
			return false;
		}
		if (Tear.MaxSplits < 0 || Tear.MaxSplits > 4 || Tear.MaxSpikes < 0 || Tear.MaxSpikes > 8)
		{
			OutError = TEXT("Max splits must be 0..4 and max spikes 0..8.");
			return false;
		}
		if (!InRange(Tear.SpikeLengthMin, 0, 3) || !InRange(Tear.SpikeLengthMax, 0, 3) || Tear.SpikeLengthMin > Tear.SpikeLengthMax)
		{
			OutError = TEXT("Spike length must be finite, between zero and three strip widths, with minimum not above maximum.");
			return false;
		}
		if (Pattern.Guides.IsEmpty())
		{
			OutError = TEXT("GrainGuides mode requires at least one open guide.");
			return false;
		}
		for (const FBreachPatternGuide& Guide : Pattern.Guides)
		{
			if (!SimplePath(Guide.Points, false, OutError))
			{
				return false;
			}
		}
		return true;
	}
	OutError = TEXT("Unsupported pattern mode.");
	return false;
}

bool BreachPatternGeometry::ValidateRegions(TConstArrayView<UE::Geometry::FGeneralPolygon2d> Regions, FString& OutError)
{
	OutError.Reset();
	for (const UE::Geometry::FGeneralPolygon2d& Region : Regions)
	{
		const UE::Geometry::FPolygon2d& Outer = Region.GetOuter();
		if (!SimplePath(Outer.GetVertices(), true, OutError))
		{
			return false;
		}
		const TArray<UE::Geometry::FPolygon2d>& Holes = Region.GetHoles();
		for (int32 Index = 0; Index < Holes.Num(); ++Index)
		{
			const UE::Geometry::FPolygon2d& Hole = Holes[Index];
			if (!SimplePath(Hole.GetVertices(), true, OutError))
			{
				return false;
			}
			if (BoundariesTouch(Outer, Hole) || !Outer.Contains(Hole[0]))
			{
				OutError = TEXT("A pattern hole must be strictly inside its outer loop.");
				return false;
			}
			for (int32 Other = 0; Other < Index; ++Other)
			{
				if (BoundariesTouch(Hole, Holes[Other]) || Hole.Contains(Holes[Other][0]) || Holes[Other].Contains(Hole[0]))
				{
					OutError = TEXT("Pattern holes cannot intersect, touch or contain one another.");
					return false;
				}
			}
		}
	}
	return true;
}

bool BreachPatternGeometry::Generate(const UBreachPattern& Pattern, const UE::Geometry::FPolygon2d& BaseMetric, const FBreachPatternMapping& Mapping, const FBreachContourOptions& Contour, double Influence, TArray<UE::Geometry::FGeneralPolygon2d>& OutCutters, FString& OutError)
{
	OutError.Reset();
	if (!Validate(Pattern, OutError))
	{
		return false;
	}
	if (!SimplePath(BaseMetric.GetVertices(), true, OutError))
	{
		return false;
	}
	if (!FMath::IsFinite(Influence) || Influence < 0 || Influence > 1)
	{
		OutError = TEXT("Pattern grain influence must be finite and between zero and one.");
		return false;
	}
	FMapping Transform;
	Transform.UV = Mapping;
	Transform.Determinant = PatternCross(Mapping.UVPerX, Mapping.UVPerY);
	const double AxisProduct = Mapping.UVPerX.Length() * Mapping.UVPerY.Length();
	if (!Finite(Mapping.UVOffset) || !Finite(Mapping.UVPerX) || !Finite(Mapping.UVPerY) || !FMath::IsFinite(Transform.Determinant) || !FMath::IsFinite(AxisProduct) || AxisProduct <= 0 || FMath::Abs(Transform.Determinant) <= AxisProduct * 1.e-12)
	{
		OutError = TEXT("Pattern mapping requires finite independent UV axes.");
		return false;
	}
	TArray<UE::Geometry::FGeneralPolygon2d> Generated;
	if (Pattern.Mode == EBreachPatternMode::Regions)
	{
		if (!GenerateRegions(Pattern, BaseMetric, Transform, Generated, OutError))
		{
			return false;
		}
	}
	else
	{
		if (!GenerateContour(BaseMetric, Contour, Generated, OutError))
		{
			return false;
		}
		if (Pattern.Mode == EBreachPatternMode::GrainGuides && Influence > 0)
		{
			// Lines must cover the furthest strip end: contour noise plus the longest tear at each end.
			const FAxisAlignedBox2d Bounds = BaseMetric.Bounds();
			const double SearchMargin = FMath::Max(Bounds.Width(), Bounds.Height()) * (1 + 2 * FMath::Max(0.f, Pattern.LineTear.TearLengthMax));
			TArray<FGuideSegment> Guides;
			if (!GatherGuides(Pattern, BaseMetric, Transform, SearchMargin, Guides, OutError))
			{
				return false;
			}
			TArray<UE::Geometry::FGeneralPolygon2d> Grain;
			if (!BreachGrainFracture::Build(Pattern.LineTear, Generated[0].GetOuter(), Guides, Contour.Seed == 0 ? 1 : Contour.Seed, Influence, Grain, OutError))
			{
				return false;
			}
			Generated = MoveTemp(Grain);
		}
	}
	OutCutters = MoveTemp(Generated);
	return true;
}
