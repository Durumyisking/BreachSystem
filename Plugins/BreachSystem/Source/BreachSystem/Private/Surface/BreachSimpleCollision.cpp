#include "Surface/BreachSimpleCollision.h"
#include "CompGeom/ConvexHull2.h"
#include "CompGeom/ExactPredicates.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Settings/BreachSystemSettings.h"

#include "BreachSurfaceGeometry.h"

#if WITH_EDITOR
#include "Surface/BreachSimpleCollisionDiagnostics.h"
#include "Misc/Paths.h"
#endif

namespace
{
	using namespace UE::Geometry;

	FPolygon2d PolygonFromIndices(TConstArrayView<int32> Indices, TConstArrayView<FVector2d> Vertices)
	{
		FPolygon2d Polygon;
		for (int32 Index : Indices)
		{
			Polygon.AppendVertex(Vertices[Index]);
		}

		return Polygon;
	}

	bool MergeConvex(TConstArrayView<int32> A, TConstArrayView<int32> B,
		TConstArrayView<FVector2d> Vertices, TArray<int32>& OutIndices)
	{
		TArray<int32> Indices(A);
		for (int32 Index : B)
		{
			Indices.AddUnique(Index);
		}

		TArray<FVector2d> Points;
		for (int32 Index : Indices)
		{
			Points.Add(Vertices[Index]);
		}

		TConvexHull2<double> Hull;
		if (!Hull.Solve(Points))
		{
			return false;
		}

		TArray<int32> Merged;
		for (int32 Index : Hull.GetPolygonIndices())
		{
			Merged.Add(Indices[Index]);
		}

		const double CombinedArea = FMath::Abs(PolygonFromIndices(A, Vertices).SignedArea())
			+ FMath::Abs(PolygonFromIndices(B, Vertices).SignedArea());
		const double HullArea = FMath::Abs(PolygonFromIndices(Merged, Vertices).SignedArea());

		// 인접 조각의 합집합이 이미 볼록할 때만 병합한다. 구멍을 가로질러 덮지 않는다.
		if (FMath::Abs(HullArea - CombinedArea) > FMath::Max(1.e-8, CombinedArea * 1.e-10))
		{
			return false;
		}

		OutIndices = MoveTemp(Merged);
		return true;
	}

	// 구멍 단순화가 허용하는 경계 이동 거리. 비활성화돼 있는 BuildMesh의 Simplify(0.0, 1.0)과 같은 값이다.
	constexpr double SimplifyTolerance = 1.0;

	// 반시계 삼각형 ABC의 내부 또는 변 위에 Point가 있는지 정확한 부호로 판정한다.
	bool InsideTriangle(const FVector2d& A, const FVector2d& B, const FVector2d& C, const FVector2d& Point)
	{
		if (ExactPredicates::Orient2(A, B, Point) < 0)
		{
			return false;
		}
		if (ExactPredicates::Orient2(B, C, Point) < 0)
		{
			return false;
		}
		return ExactPredicates::Orient2(C, A, Point) >= 0;
	}

	// 단순 다각형의 내부에 놓인 점을 돌려준다.
	// 가장 왼쪽 꼭짓점은 항상 볼록하다. 그 귀 삼각형에 다른 정점이 없으면 무게중심을, 있으면 맞은편 변에서 가장 먼 정점과의 중점을 쓴다.
	FVector2d FindInteriorPoint(const FPolygon2d& Polygon)
	{
		const int32 Count = Polygon.VertexCount();
		int32 Corner = 0;
		for (int32 Index = 1; Index < Count; ++Index)
		{
			const bool bFartherLeft = Polygon[Index].X < Polygon[Corner].X;
			const bool bSameColumnLower = Polygon[Index].X == Polygon[Corner].X && Polygon[Index].Y < Polygon[Corner].Y;
			if (bFartherLeft || bSameColumnLower)
			{
				Corner = Index;
			}
		}

		const int32 PreviousIndex = (Corner + Count - 1) % Count;
		const int32 NextIndex = (Corner + 1) % Count;
		const FVector2d& V = Polygon[Corner];
		FVector2d A = Polygon[PreviousIndex];
		FVector2d B = Polygon[NextIndex];
		if (ExactPredicates::Orient2(A, V, B) < 0)
		{
			Swap(A, B);
		}

		bool bBlocked = false;
		double DeepestDistance = 0;
		FVector2d Deepest = V;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Index == Corner || Index == PreviousIndex || Index == NextIndex)
			{
				continue;
			}

			const FVector2d& Point = Polygon[Index];
			if (!InsideTriangle(A, V, B, Point))
			{
				continue;
			}

			const double Distance = FMath::Abs(ExactPredicates::Orient2(B, A, Point));
			if (!bBlocked || Distance > DeepestDistance)
			{
				bBlocked = true;
				DeepestDistance = Distance;
				Deepest = Point;
			}
		}

		if (bBlocked)
		{
			return (V + Deepest) * 0.5;
		}
		return (A + V + B) / 3.0;
	}

	// 구멍 영역의 정점 수를 줄이되 결과가 원래 영역 밖으로 나가지 않게 한다.
	// 영역 쪽으로 볼록한 꼭짓점만, 이웃 두 정점과 이루는 삼각형 안에 다른 정점이 없을 때 제거한다. 잘려 나가는 삼각형이 영역 안에 있으므로 교집합으로 다시 제한할 필요가 없다.
	// 한 번만 훑고 이웃한 두 정점을 함께 제거하지 않아, 경계가 원래 위치에서 Tolerance보다 멀리 움직이지 않는다. 새 정점은 만들지 않는다.
	FGeneralPolygon2d SimplifyInside(const FGeneralPolygon2d& Region, double Tolerance)
	{
		TArray<const FPolygon2d*> Rings;
		Rings.Add(&Region.GetOuter());
		for (const FPolygon2d& Hole : Region.GetHoles())
		{
			Rings.Add(&Hole);
		}

		TArray<FPolygon2d> Simplified;
		for (int32 RingIndex = 0; RingIndex < Rings.Num(); ++RingIndex)
		{
			const FPolygon2d& Ring = *Rings[RingIndex];
			const int32 Count = Ring.VertexCount();

			// 영역은 반시계 외곽의 왼쪽, 시계 방향 내부 링의 왼쪽에 있다.
			const bool bCounterClockwise = Ring.SignedArea() >= 0;
			const bool bRegionOnLeft = RingIndex == 0 ? bCounterClockwise : !bCounterClockwise;

			TArray<bool> Removed;
			Removed.Init(false, Count);
			int32 KeptCount = Count;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (KeptCount <= 3)
				{
					break;
				}

				const int32 PreviousIndex = (Index + Count - 1) % Count;
				const int32 NextIndex = (Index + 1) % Count;
				if (Removed[PreviousIndex] || Removed[NextIndex])
				{
					continue;
				}

				FVector2d A = Ring[PreviousIndex];
				const FVector2d& V = Ring[Index];
				FVector2d B = Ring[NextIndex];
				if (!bRegionOnLeft)
				{
					Swap(A, B);
				}

				// 영역 쪽으로 오목하거나 일직선인 꼭짓점을 지우면 영역이 넓어지므로 남긴다.
				if (ExactPredicates::Orient2(A, V, B) <= 0)
				{
					continue;
				}

				const FVector2d Chord = B - A;
				const double ChordLength = Chord.Length();
				if (ChordLength <= 0)
				{
					continue;
				}
				const double Deviation = FMath::Abs(Chord.X * (V.Y - A.Y) - Chord.Y * (V.X - A.X)) / ChordLength;
				if (Deviation > Tolerance)
				{
					continue;
				}

				// 삼각형 안에 다른 정점이 있으면 새 변이 다른 경계를 가로지를 수 있다.
				bool bBlocked = false;
				for (int32 OtherRing = 0; OtherRing < Rings.Num(); ++OtherRing)
				{
					const FPolygon2d& Other = *Rings[OtherRing];
					for (int32 OtherIndex = 0; OtherIndex < Other.VertexCount(); ++OtherIndex)
					{
						if (OtherRing == RingIndex)
						{
							if (OtherIndex == PreviousIndex || OtherIndex == Index || OtherIndex == NextIndex)
							{
								continue;
							}
						}
						if (InsideTriangle(A, V, B, Other[OtherIndex]))
						{
							bBlocked = true;
							break;
						}
					}
					if (bBlocked)
					{
						break;
					}
				}
				if (bBlocked)
				{
					continue;
				}

				Removed[Index] = true;
				--KeptCount;
			}

			FPolygon2d& Kept = Simplified.AddDefaulted_GetRef();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (!Removed[Index])
				{
					Kept.AppendVertex(Ring[Index]);
				}
			}
		}

		FGeneralPolygon2d Result(Simplified[0]);
		for (int32 RingIndex = 1; RingIndex < Simplified.Num(); ++RingIndex)
		{
			Result.AddHole(Simplified[RingIndex], false, false);
		}
		return Result;
	}
}

void FBreachSimpleCollision::Initialize(const FPolygon2d& Outer, const FVector2d& Depth)
{
	OriginalOuter = Outer;
	DepthRange = Depth;
#if WITH_EDITOR
	bFailureSnapshotSaved = false;
#endif

	// 구멍 캐시는 초기화된 표면에서만 쓴다. 이전 표면의 구멍과 충돌 출력을 넘겨받지 않는다.
	HoleComponents.Reset();
	BuiltRevisions.Reset();
	BuiltCollision = FKAggregateGeom();
	bBuiltCollisionValid = false;
	NextHoleRevision = 1;
	OriginalBounds = OriginalOuter.Bounds();
	bHoleCacheValid = OriginalOuter.VertexCount() >= 3;

	bOriginalIsBox = OriginalOuter.VertexCount() == 4;
	for (const FVector2d& Point : OriginalOuter.GetVertices())
	{
		const bool bOnVerticalSide = Point.X == OriginalBounds.Min.X || Point.X == OriginalBounds.Max.X;
		const bool bOnHorizontalSide = Point.Y == OriginalBounds.Min.Y || Point.Y == OriginalBounds.Max.Y;
		if (!bOnVerticalSide || !bOnHorizontalSide)
		{
			bOriginalIsBox = false;
		}
	}
}

bool FBreachSimpleCollision::IsInsideOriginal(const FAxisAlignedBox2d& Bounds) const
{
	if (!bOriginalIsBox)
	{
		return false;
	}
	return OriginalBounds.Contains(Bounds);
}

bool FBreachSimpleCollision::MeasureSurfaceArea(const FGeneralPolygon2d& Polygon, const FAxisAlignedBox2d& Bounds, double& OutArea) const
{
	if (IsInsideOriginal(Bounds))
	{
		OutArea = FMath::Abs(Polygon.SignedArea());
		return true;
	}

	// 벽 밖으로 나간 부분을 뺀 면적만 읽는다. 원본으로 자른 도형은 벽 외곽선과 겹치는 변을 가지므로 보관하지 않는다.
	const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(OriginalOuter)};
	TArray<FGeneralPolygon2d> InsidePieces;
	const bool bClipped = PolygonsIntersection(MakeArrayView(&Polygon, 1), Original, InsidePieces);
	if (!bClipped)
	{
		return false;
	}

	OutArea = 0;
	for (const FGeneralPolygon2d& Piece : InsidePieces)
	{
		OutArea += FMath::Abs(Piece.SignedArea());
	}
	return true;
}

void FBreachSimpleCollision::InvalidateHoleCache()
{
	bHoleCacheValid = false;
	HoleComponents.Reset();
	BuiltRevisions.Reset();
	BuiltCollision = FKAggregateGeom();
	bBuiltCollisionValid = false;
}

bool FBreachSimpleCollision::ApplyCut(const FPolygon2d& Cutter, TConstArrayView<FPolygon2d> DetachedRegions, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | ApplyCut");
	OutError.Reset();

	// 검증: 캐시가 표면 상태를 따라가지 못하게 되는 입력은 캐시를 무효화한다.
	if (!bHoleCacheValid)
	{
		OutError = TEXT("Hole cache is not available for this surface.");
		return false;
	}
	if (Cutter.VertexCount() < 3)
	{
		OutError = TEXT("Hole cache requires a cutter with at least three vertices.");
		InvalidateHoleCache();
		return false;
	}

	// 준비
	const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(OriginalOuter)};
	const FGeneralPolygon2d CutterRegion(Cutter);
	const FAxisAlignedBox2d CutterBounds = Cutter.Bounds();
	const bool bCutterInside = IsInsideOriginal(CutterBounds);

	// 처리: 벽 안에서 커터와 겹치는 구멍을 고른다. 나머지 구멍은 건드리지 않는다.
	TArray<FGeneralPolygon2d> Touched;
	Touched.Add(CutterRegion);
	TArray<int32> TouchedIndices;
	for (int32 Index = 0; Index < HoleComponents.Num(); ++Index)
	{
		const FHoleComponent& Component = HoleComponents[Index];
		if (!Component.Bounds.Intersects(CutterBounds))
		{
			continue;
		}

		bool bTouches = true;
		if (!bCutterInside)
		{
			if (!IsInsideOriginal(Component.Bounds))
			{
				// 둘 다 벽 밖으로 나가 있으면 벽 밖에서만 겹칠 수 있다. 벽 안에서 겹칠 때만 하나의 구멍으로 본다.
				TArray<FGeneralPolygon2d> Overlap;
				const bool bOverlapBuilt = PolygonsIntersection(MakeArrayView(&CutterRegion, 1), MakeArrayView(&Component.Polygon, 1), Overlap);
				if (!bOverlapBuilt)
				{
					OutError = TEXT("Cutter overlap with a tracked opening could not be computed.");
					InvalidateHoleCache();
					return false;
				}

				double OverlapArea = 0;
				if (!Overlap.IsEmpty())
				{
					TArray<FGeneralPolygon2d> InsideOverlap;
					const bool bInsideBuilt = PolygonsIntersection(Overlap, Original, InsideOverlap);
					if (!bInsideBuilt)
					{
						OutError = TEXT("Cutter overlap could not be limited to the original wall.");
						InvalidateHoleCache();
						return false;
					}
					for (const FGeneralPolygon2d& Piece : InsideOverlap)
					{
						OverlapArea += FMath::Abs(Piece.SignedArea());
					}
				}
				bTouches = OverlapArea > 0;
			}
		}

		if (bTouches)
		{
			Touched.Add(Component.Polygon);
			TouchedIndices.Add(Index);
		}
	}

	// 처리: 커터와 겹친 구멍을 합친다. bounds만 겹치고 실제로는 떨어진 구멍은 합집합 결과에서 다시 따로 나온다.
	TArray<FGeneralPolygon2d> Merged;
	if (Touched.Num() == 1)
	{
		Merged = MoveTemp(Touched);
	}
	else
	{
		const bool bMerged = PolygonsUnion(Touched, Merged, false);
		if (!bMerged)
		{
			OutError = TEXT("Cutter could not be merged into the tracked openings.");
			InvalidateHoleCache();
			return false;
		}
	}

	// 상태 반영: 합쳐진 구멍을 새 도형으로 바꾼다. 면적은 아래에서 다시 잰다.
	for (int32 Offset = TouchedIndices.Num() - 1; Offset >= 0; --Offset)
	{
		HoleComponents.RemoveAtSwap(TouchedIndices[Offset]);
	}
	for (FGeneralPolygon2d& Polygon : Merged)
	{
		FHoleComponent& Component = HoleComponents.AddDefaulted_GetRef();
		Component.Bounds = Polygon.GetOuter().Bounds();
		Component.Polygon = MoveTemp(Polygon);
		Component.SurfaceArea = -1;
		Component.Revision = NextHoleRevision++;
	}

	// 처리: 고립 조각이 제거되면 그 조각을 둘러싼 내부 링을 지워 조각의 면적을 구멍에 포함한다.
	for (const FPolygon2d& Detached : DetachedRegions)
	{
		if (Detached.VertexCount() < 3)
		{
			OutError = TEXT("Detached region requires at least three vertices.");
			InvalidateHoleCache();
			return false;
		}

		const FVector2d InsidePoint = FindInteriorPoint(Detached);
		int32 OwnerIndex = INDEX_NONE;
		int32 OwnerRing = INDEX_NONE;
		for (int32 Index = 0; Index < HoleComponents.Num(); ++Index)
		{
			const FHoleComponent& Component = HoleComponents[Index];
			if (!Component.Bounds.Contains(InsidePoint))
			{
				continue;
			}

			const TArray<FPolygon2d>& Rings = Component.Polygon.GetHoles();
			for (int32 RingIndex = 0; RingIndex < Rings.Num(); ++RingIndex)
			{
				if (Rings[RingIndex].Contains(InsidePoint))
				{
					OwnerIndex = Index;
					OwnerRing = RingIndex;
					break;
				}
			}
			if (OwnerIndex != INDEX_NONE)
			{
				break;
			}
		}
		if (OwnerIndex == INDEX_NONE)
		{
			OutError = TEXT("Detached region is not enclosed by a tracked opening.");
			InvalidateHoleCache();
			return false;
		}

		FHoleComponent& Owner = HoleComponents[OwnerIndex];
		FGeneralPolygon2d Filled(Owner.Polygon.GetOuter());
		const TArray<FPolygon2d>& OwnerRings = Owner.Polygon.GetHoles();
		for (int32 RingIndex = 0; RingIndex < OwnerRings.Num(); ++RingIndex)
		{
			if (RingIndex != OwnerRing)
			{
				Filled.AddHole(OwnerRings[RingIndex], false, false);
			}
		}
		Owner.Polygon = MoveTemp(Filled);
		Owner.SurfaceArea = -1;
		Owner.Revision = NextHoleRevision++;
		Owner.bSimpleCutBuilt = false;

		// 조각 안에 있던 작은 구멍은 채워진 링에 포함됐으므로 따로 두지 않는다. 조각을 둘러싼 구멍은 bounds가 더 커서 여기에 걸리지 않는다.
		const FAxisAlignedBox2d DetachedBounds = Detached.Bounds();
		for (int32 Index = HoleComponents.Num() - 1; Index >= 0; --Index)
		{
			const FHoleComponent& Component = HoleComponents[Index];
			if (!DetachedBounds.Contains(Component.Bounds))
			{
				continue;
			}
			if (Detached.Contains(Component.Polygon.GetOuter()[0]))
			{
				HoleComponents.RemoveAtSwap(Index);
			}
		}
	}

	// 처리: 도형이 바뀐 구멍만 벽 안쪽 면적을 다시 잰다.
	for (FHoleComponent& Component : HoleComponents)
	{
		if (Component.SurfaceArea < 0)
		{
			double Area = 0;
			const bool bMeasured = MeasureSurfaceArea(Component.Polygon, Component.Bounds, Area);
			if (!bMeasured)
			{
				OutError = TEXT("Tracked opening area could not be measured inside the original wall.");
				InvalidateHoleCache();
				return false;
			}
			Component.SurfaceArea = Area;
		}
	}

	return true;
}

bool FBreachSimpleCollision::BuildMeshFromCuts(const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | BuildConvexFromCuts");
	OutError.Reset();

	const double AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	const double Bias = 1.e-7;
	if (OriginalOuter.VertexCount() < 3 || !FMath::IsFinite(AreaThreshold) || AreaThreshold <= 0)
	{
		OutError = TEXT("Simple collision requires an initialized surface and a positive area threshold.");
		return false;
	}
	if (!bHoleCacheValid)
	{
		OutError = TEXT("Hole cache is not available; derive the collision from the remaining regions.");
		return false;
	}

	// 면적 조건: BuildMesh와 같은 식을 쓴다. 임계보다 작은 구멍은 제외하되, 벽 전체가 제거된 경우는 예외다.
	const double OriginalArea = FMath::Abs(OriginalOuter.SignedArea());
	TArray<int32> SelectedIndices;
	TArray<uint64> SelectedRevisions;
	for (int32 Index = 0; Index < HoleComponents.Num(); ++Index)
	{
		const FHoleComponent& Component = HoleComponents[Index];
		const double WorldArea = Component.SurfaceArea * ComponentArea;
		if (WorldArea + Bias < AreaThreshold)
		{
			if (WorldArea < OriginalArea - 1.e-7)
			{
				continue;
			}
		}
		SelectedIndices.Add(Index);
		SelectedRevisions.Add(Component.Revision);
	}
	SelectedRevisions.Sort();

	// 면적 조건을 통과한 구멍이 지난번과 같으면 충돌 영역도 같다. 작은 총탄 구멍만 늘어난 피격은 여기서 끝난다.
	if (bBuiltCollisionValid)
	{
		if (SelectedRevisions == BuiltRevisions)
		{
			OutCollision = BuiltCollision;
			return true;
		}
	}

	TArray<FGeneralPolygon2d> SimpleCuts;
	for (int32 Index : SelectedIndices)
	{
		FHoleComponent& Component = HoleComponents[Index];
		if (!Component.bSimpleCutBuilt)
		{
			Component.SimpleCut = SimplifyInside(Component.Polygon, SimplifyTolerance);
			Component.bSimpleCutBuilt = true;
		}
		SimpleCuts.Add(Component.SimpleCut);
	}

	// 충돌은 원본 벽에서 파생한다. 빼는 구멍이 벽 외곽선을 가로질러 지나가므로 외곽선 위에 겹치는 변이 생기지 않는다.
	TArray<FGeneralPolygon2d> CollisionRegions = {FGeneralPolygon2d(OriginalOuter)};
	if (!SimpleCuts.IsEmpty())
	{
		const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(OriginalOuter)};
		const bool bCollisionBuilt = PolygonsDifference(Original, SimpleCuts, CollisionRegions);
		if (!bCollisionBuilt)
		{
			OutError = TEXT("Simple collision regions could not be built from the tracked openings.");
			return false;
		}
	}

	// 메시 생성: BuildMesh와 같이 삼각형마다 두께를 가진 볼록 기둥을 만든다.
	FKAggregateGeom Collision;
	for (const FGeneralPolygon2d& Region : CollisionRegions)
	{
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles;
		const bool bTriangulated = BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, OutError);
		if (!bTriangulated)
		{
			return false;
		}

		for (const FIndex3i& Triangle : Triangles)
		{
			FKConvexElem& Convex = Collision.ConvexElems.AddDefaulted_GetRef();
			for (double Depth : {DepthRange.X, DepthRange.Y})
			{
				for (int32 Index : {Triangle.A, Triangle.B, Triangle.C})
				{
					Convex.VertexData.Add(FVector(Vertices[Index].X, Depth, Vertices[Index].Y));
				}
			}

			Convex.UpdateElemBox();
		}
	}

	BuiltRevisions = MoveTemp(SelectedRevisions);
	BuiltCollision = Collision;
	bBuiltCollisionValid = true;
	OutCollision = MoveTemp(Collision);
	return true;
}

bool FBreachSimpleCollision::BuildMesh(TConstArrayView<FGeneralPolygon2d> RemainingRegions, const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | BuildConvex");
	OutError.Reset();

	const double AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	const double Bias = 1.e-7;
	if (OriginalOuter.VertexCount() < 3 || !FMath::IsFinite(AreaThreshold) || AreaThreshold <= 0)
	{
		OutError = TEXT("Simple collision requires an initialized surface and a positive area threshold.");
		return false;
	}

	// 모든 구멍들을 가져온다
	const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(OriginalOuter)};
	TArray<FGeneralPolygon2d> Holes;
	const bool bOpeningsBuilt = PolygonsDifference(Original, RemainingRegions, Holes);
	if (!bOpeningsBuilt)
	{
		OutError = TEXT("Simple openings could not be derived from the damage regions.");
		return false;
	}

	//UE_LOG(LogTemp, Log, TEXT("Openings Count %d"), Holes.Num());
	TArray<FGeneralPolygon2d> SimpleCuts;
	for (FGeneralPolygon2d& Hole : Holes)
	{
		const double WorldArea = FMath::Abs(Hole.SignedArea()) * ComponentArea;
		// 임계보다 작은 구멍은 스킵
		if (WorldArea + Bias < AreaThreshold)
		{
			const double OriginalArea = FMath::Abs(OriginalOuter.SignedArea());
			if (WorldArea < OriginalArea - 1.e-7)
			{
				continue;
			}
		}

		FGeneralPolygon2d Simplified = Hole;
		//Simplified.Simplify(0.0, 1.0);

		// 단순화한 구멍이 원래 구멍 밖으로 나가지 않도록 잘라내는 작업
		TArray<FGeneralPolygon2d> SafeHoles;
		const bool bContained = PolygonsIntersection(MakeArrayView(&Simplified, 1), MakeArrayView(&Hole, 1), SafeHoles);
		if (!bContained)
		{
			OutError = TEXT("Simplified hole could not be confined to the actual Hole.");
			return false;
		}
		SimpleCuts.Append(MoveTemp(SafeHoles));
	}

	// 충돌은 원본 벽에서 파생한다. 전달받은 표면 영역은 변경하지 않는다.
	TArray<FGeneralPolygon2d> CollisionRegions(Original); 
#if WITH_EDITOR
	bool bFinalDifferenceApplied = false;
#endif
	if (!SimpleCuts.IsEmpty())
	{
		const bool bCollisionBuilt = PolygonsDifference(Original, SimpleCuts, CollisionRegions);
#if WITH_EDITOR
		bFinalDifferenceApplied = true;
#endif
		if (!bCollisionBuilt)
		{
			OutError = TEXT("Simple collision regions could not be built from the original wall.");
			return false;
		}

		const bool bBoundariesResolved = BreachSurfaceGeometry::ResolveBoundaryOverlaps(CollisionRegions, OutError);
		if (!bBoundariesResolved)
		{
			return false;
		}
	}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	ObserveGeometry(TEXT("CollisionRegions"), CollisionRegions);
#endif

	FKAggregateGeom Collision;
	for (int32 RegionIndex = 0; RegionIndex < CollisionRegions.Num(); ++RegionIndex)
	{
		const FGeneralPolygon2d& Region = CollisionRegions[RegionIndex];
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles;
		const bool bTriangulated = BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, OutError);
		if (!bTriangulated)
		{
#if WITH_EDITOR
			if (!bFailureSnapshotSaved)
			{
				const BreachSimpleCollisionDiagnostics::FFailureSnapshot Snapshot{OriginalOuter, DepthRange, RemainingRegions, Holes, SimpleCuts, CollisionRegions, AreaThreshold, Bias, bFinalDifferenceApplied, RegionIndex, OutError};
				FString SnapshotPath;
				bFailureSnapshotSaved = BreachSimpleCollisionDiagnostics::SaveSnapshot(Snapshot, FPaths::ProjectSavedDir() / TEXT("Diagnostics/SimpleCollisionFailures"), SnapshotPath);
				if (bFailureSnapshotSaved)
				{
					UE_LOG(LogTemp, Warning, TEXT("[BreachSimpleDiag] Failure snapshot saved: %s"), *SnapshotPath);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[BreachSimpleDiag] Failed to save collision failure snapshot under Saved/Diagnostics/SimpleCollisionFailures."));
				}
			}
#endif
			return false;
		}

		TArray<TArray<int32>> Parts;
		for (const FIndex3i& Triangle : Triangles)
		{
			TArray<int32> Part = {Triangle.A, Triangle.B, Triangle.C};
			Parts.Add(MoveTemp(Part));
		}

	/*	bool bMerged = true;
		while (bMerged)
		{
			bMerged = false;
			TMap<FIntPoint, int32> EdgeOwners;
			for (int32 Index = 0; Index < Parts.Num() && !bMerged; ++Index)
			{
				for (int32 Edge = 0; Edge < Parts[Index].Num(); ++Edge)
				{
					const int32 A = Parts[Index][Edge];
					const int32 B = Parts[Index][(Edge + 1) % Parts[Index].Num()];
					const FIntPoint Key(FMath::Min(A, B), FMath::Max(A, B));
					const int32* Other = EdgeOwners.Find(Key);
					if (Other)
					{
						TArray<int32> Merged;
						if (MergeConvex(Parts[*Other], Parts[Index], Vertices, Merged))
						{
							Parts[*Other] = MoveTemp(Merged);
							Parts[Index].Reset();
							bMerged = true;
							break;
						}
					}
					else
					{
						EdgeOwners.Add(Key, Index);
					}
				}
			}
		}*/

		for (const TArray<int32>& Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}

			FKConvexElem& Convex = Collision.ConvexElems.AddDefaulted_GetRef();
			for (double Depth : {DepthRange.X, DepthRange.Y})
			{
				for (int32 Index : Part)
				{
					Convex.VertexData.Add(FVector(Vertices[Index].X, Depth, Vertices[Index].Y));
				}
			}

			Convex.UpdateElemBox();
		}
	}

	OutCollision = MoveTemp(Collision);
	return true;
}
