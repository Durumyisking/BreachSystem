#include "BreachPatternEditing.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Surface/BreachPatternGeometry.h"

namespace
{
	bool SamePeriodicPoint(FVector2D A, FVector2D B)
	{
		const FVector2D Difference = A - B;
		return FMath::IsNearlyEqual(Difference.X, FMath::RoundToDouble(Difference.X), 1.e-7) && FMath::IsNearlyEqual(Difference.Y, FMath::RoundToDouble(Difference.Y), 1.e-7);
	}

	void MoveSharedPoints(UBreachPattern& Pattern, TConstArrayView<FVector2D> Sources, FVector2D Delta)
	{
		checkf(IsValid(&Pattern), TEXT("Shared geometry editing requires a valid pattern."));
		if (Delta == FVector2D::ZeroVector)
		{
			return;
		}
		auto MovePath = [Sources, Delta](TArray<FVector2D>& Points, bool bClosed)
		{
			TArray<FVector2D> Updated;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const FVector2D A = Points[Index];
				bool bMove = false;
				for (const FVector2D& Source : Sources)
				{
					bMove |= SamePeriodicPoint(A, Source);
				}
				Updated.Add(bMove ? A + Delta : A);
				if (!bClosed && Index == Points.Num() - 1)
				{
					continue;
				}
				const FVector2D B = Points[(Index + 1) % Points.Num()];
				const FVector2D Segment = B - A;
				const double LengthSquared = Segment.SizeSquared();
				if (LengthSquared <= 1.e-14)
				{
					continue;
				}
				TArray<TPair<double, FVector2D>> Splits;
				for (const FVector2D& Source : Sources)
				{
					const int64 MinX = FMath::CeilToInt64(FMath::Min(A.X, B.X) - Source.X - 1.e-7);
					const int64 MaxX = FMath::FloorToInt64(FMath::Max(A.X, B.X) - Source.X + 1.e-7);
					const int64 MinY = FMath::CeilToInt64(FMath::Min(A.Y, B.Y) - Source.Y - 1.e-7);
					const int64 MaxY = FMath::FloorToInt64(FMath::Max(A.Y, B.Y) - Source.Y + 1.e-7);
					for (int64 Y = MinY; Y <= MaxY; ++Y)
					{
						for (int64 X = MinX; X <= MaxX; ++X)
						{
							const FVector2D Candidate = Source + FVector2D(double(X), double(Y));
							const double Alpha = FVector2D::DotProduct(Candidate - A, Segment) / LengthSquared;
							if (Alpha > 1.e-7 && Alpha < 1 - 1.e-7 && (A + Segment * Alpha).Equals(Candidate, 1.e-7))
							{
								Splits.Emplace(Alpha, Candidate);
							}
						}
					}
				}
				Splits.Sort([](const auto& Left, const auto& Right) { return Left.Key < Right.Key; });
				double LastAlpha = -1;
				for (const auto& Split : Splits)
				{
					if (!FMath::IsNearlyEqual(LastAlpha, Split.Key, 1.e-7))
					{
						Updated.Add(Split.Value + Delta);
						LastAlpha = Split.Key;
					}
				}
			}
			Points = MoveTemp(Updated);
		};
		for (FBreachPatternRegion& Region : Pattern.Regions)
		{
			MovePath(Region.Outer, true);
			for (FBreachPatternLoop& Hole : Region.Holes)
			{
				MovePath(Hole.Vertices, true);
			}
		}
		for (FBreachPatternGuide& Guide : Pattern.Guides)
		{
			MovePath(Guide.Points, false);
		}
	}
}

void BreachPatternEditing::MoveSharedVertex(UBreachPattern& Pattern, FVector2D From, FVector2D To)
{
	checkf(IsValid(&Pattern), TEXT("Shared vertex editing requires a valid pattern."));
	const FVector2D Sources[] = { From };
	MoveSharedPoints(Pattern, MakeArrayView(Sources), To - From);
}

void BreachPatternEditing::MoveSharedEdge(UBreachPattern& Pattern, FVector2D FromA, FVector2D FromB, FVector2D Delta)
{
	checkf(IsValid(&Pattern), TEXT("Shared edge editing requires a valid pattern."));
	const FVector2D Sources[] = { FromA, FromB };
	MoveSharedPoints(Pattern, MakeArrayView(Sources), Delta);
}

void BreachPatternEditing::MakeRunningBond(UBreachPattern& Pattern, int32 Columns, int32 Rows)
{
	checkf(IsValid(&Pattern), TEXT("Running bond requires a valid pattern."));
	Columns = FMath::Clamp(Columns, 1, 64);
	Rows = FMath::Clamp(Rows, 2, 64);
	Rows += Rows % 2;
	Pattern.Mode = EBreachPatternMode::Regions;
	Pattern.Regions.Reset();
	Pattern.Guides.Reset();
	for (int32 Y = 0; Y < Rows; ++Y)
	{
		for (int32 X = 0; X < Columns; ++X)
		{
			const double Left = (X + (Y % 2 ? 0.5 : 0.0)) / Columns;
			const double Right = Left + 1.0 / Columns;
			const double Top = double(Y) / Rows;
			const double Bottom = double(Y + 1) / Rows;
			const double Middle = (Left + Right) * 0.5;
			FBreachPatternRegion& Region = Pattern.Regions.AddDefaulted_GetRef();
			Region.Outer = { FVector2D(Left, Top), FVector2D(Middle, Top), FVector2D(Right, Top), FVector2D(Right, Bottom), FVector2D(Middle, Bottom), FVector2D(Left, Bottom) };
		}
	}
}


namespace
{
	TArray<FVector2D>* SelectedPoints(UBreachPattern& Pattern, FBreachPatternPathSelection Selection)
	{
		check(IsValid(&Pattern));
		if (Pattern.Mode == EBreachPatternMode::GrainGuides)
		{
			return Pattern.Guides.IsValidIndex(Selection.Path) ? &Pattern.Guides[Selection.Path].Points : nullptr;
		}
		if (Pattern.Regions.IsValidIndex(Selection.Path))
		{
			FBreachPatternRegion& Region = Pattern.Regions[Selection.Path];
			if (Selection.Hole == INDEX_NONE)
			{
				return &Region.Outer;
			}
			return Region.Holes.IsValidIndex(Selection.Hole) ? &Region.Holes[Selection.Hole].Vertices : nullptr;
		}
		return nullptr;
	}
}

bool BreachPatternEditing::CreatePath(UBreachPattern& Pattern, TConstArrayView<FVector2D> Points, bool bClosed, FString& OutError)
{
	check(IsValid(&Pattern));
	const EBreachPatternMode Mode = bClosed ? EBreachPatternMode::Regions : EBreachPatternMode::GrainGuides;
	if (Pattern.Mode != EBreachPatternMode::None && Pattern.Mode != Mode)
	{
		OutError = TEXT("영역과 열린 결선은 한 패턴에 섞을 수 없습니다.");
		return false;
	}
	UBreachPattern* Candidate = NewObject<UBreachPattern>();
	Candidate->Mode = Mode;
	Candidate->Regions = Pattern.Regions;
	Candidate->Guides = Pattern.Guides;
	Candidate->GrainEndSharpness = Pattern.GrainEndSharpness;
	Candidate->GrainMaxEndSplinters = Pattern.GrainMaxEndSplinters;
	Candidate->ImpactGrain = Pattern.ImpactGrain;
	TArray<FVector2D> Path(Points);
	if (bClosed && Path.Num() > 1 && Path[0].Equals(Path.Last(), 1.e-7))
	{
		Path.Pop();
	}
	if (bClosed)
	{
		Candidate->Regions.AddDefaulted_GetRef().Outer = MoveTemp(Path);
	}
	else
	{
		Candidate->Guides.AddDefaulted_GetRef().Points = MoveTemp(Path);
	}
	if (!BreachPatternGeometry::Validate(*Candidate, OutError))
	{
		return false;
	}
	Pattern.Mode = Mode;
	Pattern.Regions = MoveTemp(Candidate->Regions);
	Pattern.Guides = MoveTemp(Candidate->Guides);
	return true;
}

bool BreachPatternEditing::DeletePath(UBreachPattern& Pattern, FBreachPatternPathSelection Selection, FString& OutError)
{
	check(IsValid(&Pattern));
	if (!SelectedPoints(Pattern, Selection))
	{
		OutError = TEXT("삭제할 경계를 선택하세요.");
		return false;
	}
	if (Pattern.Mode == EBreachPatternMode::GrainGuides)
	{
		Pattern.Guides.RemoveAt(Selection.Path);
	}
	else if (Selection.Hole != INDEX_NONE)
	{
		Pattern.Regions[Selection.Path].Holes.RemoveAt(Selection.Hole);
	}
	else
	{
		Pattern.Regions.RemoveAt(Selection.Path);
	}
	if (Pattern.Regions.IsEmpty() && Pattern.Guides.IsEmpty())
	{
		Pattern.Mode = EBreachPatternMode::None;
	}
	OutError.Reset();
	return true;
}

bool BreachPatternEditing::DeleteVertex(UBreachPattern& Pattern, FBreachPatternPathSelection Selection, FString& OutError)
{
	check(IsValid(&Pattern));
	TArray<FVector2D>* Points = SelectedPoints(Pattern, Selection);
	if (!Points || !Points->IsValidIndex(Selection.Vertex))
	{
		OutError = TEXT("삭제할 점을 선택하세요.");
		return false;
	}
	const auto BeforeRegions = Pattern.Regions;
	const auto BeforeGuides = Pattern.Guides;
	const FVector2D Selected = (*Points)[Selection.Vertex];
	if (Pattern.Mode == EBreachPatternMode::Regions)
	{
		for (FBreachPatternRegion& Region : Pattern.Regions)
		{
			Region.Outer.RemoveAll([Selected](FVector2D Point) { return SamePeriodicPoint(Point, Selected); });
			for (FBreachPatternLoop& Hole : Region.Holes)
			{
				Hole.Vertices.RemoveAll([Selected](FVector2D Point) { return SamePeriodicPoint(Point, Selected); });
			}
		}
	}
	else
	{
		Points->RemoveAt(Selection.Vertex);
	}
	if (!BreachPatternGeometry::Validate(Pattern, OutError))
	{
		Pattern.Regions = BeforeRegions;
		Pattern.Guides = BeforeGuides;
		return false;
	}
	return true;
}

bool BreachPatternEditing::MovePath(UBreachPattern& Pattern, FBreachPatternPathSelection Selection, FVector2D Delta, FString& OutError)
{
	check(IsValid(&Pattern));
	TArray<FVector2D>* Points = SelectedPoints(Pattern, Selection);
	if (!Points || Delta.ContainsNaN())
	{
		OutError = TEXT("이동할 경계와 유효한 이동량이 필요합니다.");
		return false;
	}
	const TArray<FVector2D> Before = *Points;
	for (FVector2D& Point : *Points)
	{
		Point += Delta;
	}
	if (!BreachPatternGeometry::Validate(Pattern, OutError))
	{
		*Points = Before;
		return false;
	}
	return true;
}

bool BreachPatternEditing::GeneratePreview(const UBreachPattern& Pattern, const UBreachSurfaceMaterial* Material, FVector2D CenterUV, TArray<UE::Geometry::FGeneralPolygon2d>& OutPreview, FString& OutError)
{
	check(IsValid(&Pattern));
	if (!IsValid(Material))
	{
		OutError = TEXT("프리뷰에 사용할 SurfaceMaterial을 선택하세요.");
		return false;
	}
	const FVector2D Tile = Material->TileSizeCm;
	if (!FMath::IsFinite(Tile.X) || !FMath::IsFinite(Tile.Y) || Tile.X <= 0 || Tile.Y <= 0)
	{
		OutError = TEXT("SurfaceMaterial의 타일 크기가 올바르지 않습니다.");
		return false;
	}
	const FVector2d Impact(CenterUV.X * Tile.X, CenterUV.Y * Tile.Y);
	const FVector2d Impacts[] = {Impact};
	UE::Geometry::FPolygon2d Base;
	if (!BreachAccumulationShape::GenerateBase(MakeArrayView(Impacts), Material->AccumulationShape.Padding, Base, OutError))
	{
		return false;
	}
	FBreachPatternMapping Mapping;
	Mapping.UVOffset = FVector2d::ZeroVector;
	Mapping.UVPerX = FVector2d(1 / Tile.X, 0);
	Mapping.UVPerY = FVector2d(0, 1 / Tile.Y);
	TArray<UE::Geometry::FGeneralPolygon2d> Generated;
	if (!BreachPatternGeometry::Generate(Pattern, Base, Mapping, Material->AccumulationShape.Contour, Material->GrainInfluence, Generated, OutError))
	{
		return false;
	}
	OutPreview.Reset();
	for (const UE::Geometry::FGeneralPolygon2d& Region : Generated)
	{
		auto ToUV = [Tile](const UE::Geometry::FPolygon2d& Loop)
		{
			UE::Geometry::FPolygon2d Result;
			for (const FVector2d& Point : Loop.GetVertices())
			{
				Result.AppendVertex(FVector2d(Point.X / Tile.X, Point.Y / Tile.Y));
			}
			return Result;
		};
		UE::Geometry::FGeneralPolygon2d Converted(ToUV(Region.GetOuter()));
		for (const auto& Hole : Region.GetHoles())
		{
			Converted.AddHole(ToUV(Hole), false, false);
		}
		OutPreview.Add(MoveTemp(Converted));
	}
	return true;
}
