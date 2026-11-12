#include "Surface/BreachSimpleCollision.h"
#include "CompGeom/ConvexHull2.h"
#include "CompGeom/ConvexHull3.h"
#include "CompGeom/ExactPredicates.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Settings/BreachSystemSettings.h"

#include "BreachSurfaceGeometry.h"
#include "BreachPassageSolver.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformTime.h"

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

	bool BuildPrisms(TConstArrayView<FVector2d> Vertices, TConstArrayView<FIndex3i> Triangles, const FVector2d& Depth, FKAggregateGeom& OutCollision, FString& OutError, double WorldAreaScale, double& DiscardedWorldArea)
	{
		TArray<TArray<int32>> Parts;
		TArray<bool> Cookable;
		auto CanCook = [&](TConstArrayView<int32> Part)
		{
			TArray<FVector3f> Points;
			for (double Y : {Depth.X, Depth.Y})
			{
				for (int32 Index : Part)
				{
					Points.Add(FVector3f(Vertices[Index].X, Y, Vertices[Index].Y));
				}
			}
			TConvexHull3<float> Hull;
			return Hull.Solve<FVector3f>(Points);
		};
		for (const FIndex3i& Triangle : Triangles)
		{
			Parts.Add({Triangle.A, Triangle.B, Triangle.C});
			Cookable.Add(CanCook(Parts.Last()));
		}

		// Chaos가 평면으로 판정하는 조각만 인접한 볼록 합집합으로 복구한다. 벽 면적은 버리거나 추가하지 않는다.
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			if (Cookable[Index] || Parts[Index].IsEmpty())
			{
				continue;
			}
			int32 SharingNeighbors = 0, ConvexNeighbors = 0;
			for (int32 Neighbor = 0; Neighbor < Parts.Num(); ++Neighbor)
			{
				if (Neighbor == Index || Parts[Neighbor].IsEmpty())
				{
					continue;
				}
				int32 SharedVertices = 0;
				for (int32 Vertex : Parts[Index])
				{
					SharedVertices += Parts[Neighbor].Contains(Vertex) ? 1 : 0;
				}
				if (SharedVertices < 2)
				{
					continue;
				}
				++SharingNeighbors;
				TArray<int32> Merged;
				if (!MergeConvex(Parts[Index], Parts[Neighbor], Vertices, Merged))
				{
					continue;
				}
				++ConvexNeighbors;
				if (!CanCook(Merged))
				{
					continue;
				}
				Parts[Index] = MoveTemp(Merged);
				Parts[Neighbor].Reset();
				Cookable[Index] = true;
				break;
			}
			if (!Cookable[Index])
			{
				if (Parts[Index].Num() == 3)
				{
					const FVector2d A = Vertices[Parts[Index][0]], B = Vertices[Parts[Index][1]], C = Vertices[Parts[Index][2]];
					const FVector2d AF(static_cast<float>(A.X), static_cast<float>(A.Y)), BF(static_cast<float>(B.X), static_cast<float>(B.Y)), CF(static_cast<float>(C.X), static_cast<float>(C.Y));
					const double WorldArea = FMath::Abs(ExactPredicates::Orient2(A, B, C)) * 0.5 * WorldAreaScale;
					// Chaos에서 면적이 정확히 0이며 전체 벽의 누적 오차가 1e-8 cm² 이내인 조각만 제외한다.
					if (ExactPredicates::Orient2(AF, BF, CF) == 0 && DiscardedWorldArea + WorldArea <= 1.e-8)
					{
						DiscardedWorldArea += WorldArea;
						Parts[Index].Reset();
						continue;
					}
				}
#if WITH_EDITOR
				FString Coordinates;
				for (int32 Vertex : Parts[Index])
				{
					const FVector2d P = Vertices[Vertex];
					Coordinates += FString::Printf(TEXT(" [%.17g,%.17g float %.9g,%.9g]"), P.X, P.Y, static_cast<double>(static_cast<float>(P.X)), static_cast<double>(static_cast<float>(P.Y)));
				}
				UE_LOG(LogTemp, Verbose, TEXT("[BreachPrismDiag] part=%d area=%.17g shared=%d convex=%d depth=%.17g..%.17g%s"), Index, FMath::Abs(PolygonFromIndices(Parts[Index], Vertices).SignedArea()), SharingNeighbors, ConvexNeighbors, Depth.X, Depth.Y, *Coordinates);
#endif
				OutError = TEXT("A remaining wall prism is planar at Chaos precision and has no cookable convex neighbor.");
				return false;
			}
		}

		for (const TArray<int32>& Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}
			FKConvexElem& Convex = OutCollision.ConvexElems.AddDefaulted_GetRef();
			for (double Y : {Depth.X, Depth.Y})
			{
				for (int32 Index : Part)
				{
					Convex.VertexData.Add(FVector(Vertices[Index].X, Y, Vertices[Index].Y));
				}
			}
			Convex.UpdateElemBox();
		}
		return true;
	}

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

	// 고정 방향의 반평면을 교차시킨다. 거리를 늘리면 후보는 이전 영역을 포함하며 확장된다.
	// Boolean 출력에서 생긴 연속 중복 정점만 제거한다. 좌표나 면적은 바꾸지 않는다.
	void RemoveRepeatedBoundaryVertices(TArray<FGeneralPolygon2d>& Regions)
	{
		auto Clean = [](const FPolygon2d& Ring)
		{
			FPolygon2d Out;
			for (const FVector2d Point : Ring.GetVertices())
			{
				if (Out.VertexCount() == 0 || Out[Out.VertexCount() - 1] != Point)
				{
					Out.AppendVertex(Point);
				}
			}
			if (Out.VertexCount() > 1 && Out[0] == Out[Out.VertexCount() - 1])
			{
				Out.RemoveVertex(Out.VertexCount() - 1);
			}
			return Out;
		};
		TArray<FGeneralPolygon2d> Result;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			FPolygon2d Outer = Clean(Region.GetOuter());
			if (Outer.VertexCount() < 3)
			{
				continue;
			}
			FGeneralPolygon2d Part(Outer);
			for (const FPolygon2d& Hole : Region.GetHoles())
			{
				FPolygon2d Ring = Clean(Hole);
				if (Ring.VertexCount() >= 3)
				{
					Part.AddHole(MoveTemp(Ring), false, false);
				}
			}
			Result.Add(MoveTemp(Part));
		}
		Regions = MoveTemp(Result);
	}

	bool SimplifyInside(const FGeneralPolygon2d& Region, const FPolygon2d& Wall, double MinArea, int32 MaxCount, TArray<FGeneralPolygon2d>& OutCuts, FString& OutError)
	{
		FBreachSimpleBuildStats Stats;
		auto Opening = MakeShared<FBreachPreparedOpening, ESPMode::ThreadSafe>();
		const double Radius = FMath::Sqrt(MinArea / UE_DOUBLE_PI);
		if (!BreachPassageSolver::Prepare(Region, Wall, Radius, *Opening, Stats, OutError))
		{
			return false;
		}
		const TArray<TSharedPtr<const FBreachPreparedOpening, ESPMode::ThreadSafe>> Inputs = {Opening};
		FBreachPassageState Result;
		if (!BreachPassageSolver::Solve(Inputs, FBreachPassageState(), MaxCount, Radius, Result, Stats, OutError))
		{
			return false;
		}
		OutCuts.Reset();
		for (const auto& Body : Result.Bodies)
		{
			for (const FPolygon2d& Piece : Body->Pieces)
			{
				OutCuts.Emplace(Piece);
			}
		}
		return true;
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
	PassageState.Reset();
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
	PassageState.Reset();
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
		Owner.PreparedOpening.Reset();

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
	FBreachSimpleBuildPolicy Policy;
	Policy.WorldScale = FVector2d(FMath::Sqrt(ComponentArea), FMath::Sqrt(ComponentArea));
	Policy.AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	Policy.MaxCount = UBreachSystemSettings::GetMaxSimpleConvexCount();
	return BuildMeshFromCuts(Policy, OutCollision, OutError);
}

bool FBreachSimpleCollision::BuildInitialCollision(const FBreachSimpleBuildPolicy& Policy, FKAggregateGeom& OutCollision, FString& OutError)
{
	TArray<FVector2d> Vertices;
	TArray<FIndex3i> Triangles;
	if (!BreachSurfaceGeometry::TriangulateRegion(FGeneralPolygon2d(OriginalOuter), Vertices, Triangles, OutError))
	{
		return false;
	}
	FKAggregateGeom Collision;
	if (!BuildPrisms(Vertices, Triangles, DepthRange, Collision, OutError, Policy.WorldScale.X * Policy.WorldScale.Y, LastBuildStats.DiscardedWorldArea))
	{
		return false;
	}
	BuiltCollision = MoveTemp(Collision);
	BuiltRevisions.Reset();
	BuiltWorldScale = Policy.WorldScale;
	BuiltAreaScale = Policy.WorldScale.X * Policy.WorldScale.Y;
	BuiltAreaThreshold = Policy.AreaThreshold;
	BuiltMaxConvexCount = Policy.MaxCount;
	bBuiltCollisionValid = true;
	PassageState = MakeShared<FBreachPassageState, ESPMode::ThreadSafe>();
	OutCollision = BuiltCollision;
	return true;
}

void FBreachSimpleCollision::CommitBuild(const FBreachSimpleCollision& Completed)
{
	// 현재 절단 상태는 게임 스레드 소유다. 오래된 스냅샷으로 덮어쓰지 않는다.
	TMap<uint64, const FHoleComponent*> BuiltByRevision;
	BuiltByRevision.Reserve(Completed.HoleComponents.Num());
	for (const FHoleComponent& Built : Completed.HoleComponents)
	{
		if (Built.PreparedOpening.IsValid())
		{
			BuiltByRevision.Add(Built.Revision, &Built);
		}
	}
	for (FHoleComponent& Current : HoleComponents)
	{
		const FHoleComponent* const* Built = BuiltByRevision.Find(Current.Revision);
		if (Built != nullptr)
		{
			check(*Built);
			Current.PreparedOpening = (*Built)->PreparedOpening;
		}
	}
	PassageState = Completed.PassageState;
	BuiltRevisions = Completed.BuiltRevisions;
	BuiltCollision = Completed.BuiltCollision;
	bBuiltCollisionValid = Completed.bBuiltCollisionValid;
	BuiltAreaScale = Completed.BuiltAreaScale;
	BuiltWorldScale = Completed.BuiltWorldScale;
	BuiltAreaThreshold = Completed.BuiltAreaThreshold;
	BuiltMaxConvexCount = Completed.BuiltMaxConvexCount;
	LastBuildStats = Completed.LastBuildStats;
}

bool FBreachSimpleCollision::BuildMeshFromCuts(const FBreachSimpleBuildPolicy& Policy, FKAggregateGeom& OutCollision, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | BuildConvexFromCuts");
	const double Started = FPlatformTime::Seconds();
	bool bSucceeded = false;
	LastBuildStats = FBreachSimpleBuildStats();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	LastBuildStats.bLinearBoundaryQueries = Policy.bLinearBoundaryQueries;
	LastBuildStats.bRebuildTrialHulls = Policy.bRebuildTrialHulls;
#endif
	ON_SCOPE_EXIT
	{
		LastBuildStats.GeometryMs = (FPlatformTime::Seconds() - Started) * 1000;
		if (!bSucceeded)
		{
			bBuiltCollisionValid = false;
		}
	};
	OutError.Reset();
	const double AreaScale = Policy.WorldScale.X * Policy.WorldScale.Y;
	if (OriginalOuter.VertexCount() < 3 || !FMath::IsFinite(AreaScale) || Policy.WorldScale.X <= 0 || Policy.WorldScale.Y <= 0 || !FMath::IsFinite(Policy.AreaThreshold) || Policy.AreaThreshold <= 0 || Policy.MaxCount < 1 || Policy.MaxCount > 16)
	{
		OutError = TEXT("Simple collision requires an initialized wall, positive world scale and area threshold, and a convex count in [1,16].");
		return false;
	}
	if (!bHoleCacheValid)
	{
		OutError = TEXT("Hole cache is not available; previous collision is retained.");
		return false;
	}
	const bool bSamePolicy = BuiltWorldScale == Policy.WorldScale && BuiltAreaThreshold == Policy.AreaThreshold && BuiltMaxConvexCount == Policy.MaxCount;
	if (!bSamePolicy)
	{
		// 개수 변경은 전처리와 확정 통로를 보존한다. 좌표·면적 기준만 재분류한다.
		if (BuiltWorldScale != Policy.WorldScale || BuiltAreaThreshold != Policy.AreaThreshold)
		{
			for (FHoleComponent& Component : HoleComponents)
			{
				Component.PreparedOpening.Reset();
			}
			PassageState.Reset();
			bBuiltCollisionValid = false;
		}
		BuiltWorldScale = Policy.WorldScale;
		BuiltAreaScale = AreaScale;
		BuiltAreaThreshold = Policy.AreaThreshold;
		BuiltMaxConvexCount = Policy.MaxCount;
	}
	const double OriginalArea = FMath::Abs(OriginalOuter.SignedArea());
	TArray<int32> Selected;
	TArray<uint64> Revisions;
	bool bFullRemoval = false;
	for (int32 Index = 0; Index < HoleComponents.Num(); ++Index)
	{
		const FHoleComponent& Component = HoleComponents[Index];
		if (Component.SurfaceArea >= OriginalArea - 1.e-7)
		{
			bFullRemoval = true;
		}
		if (Component.SurfaceArea * AreaScale + 1.e-7 >= Policy.AreaThreshold || bFullRemoval)
		{
			Selected.Add(Index);
			Revisions.Add(Component.Revision);
		}
	}
	Revisions.Sort();
	if (bBuiltCollisionValid && bSamePolicy && Revisions == BuiltRevisions)
	{
		OutCollision = BuiltCollision;
		LastBuildStats.PassagePieceCount = PassageState.IsValid() ? PassageState->PieceCount : 0;
		bSucceeded = true;
		return true;
	}
	const double Radius = FMath::Sqrt(Policy.AreaThreshold / UE_DOUBLE_PI);
	const FPolygon2d WorldWall = BreachPassageSolver::Transform(OriginalOuter, Policy.WorldScale);
	auto NewState = MakeShared<FBreachPassageState, ESPMode::ThreadSafe>();
	if (bFullRemoval)
	{
		NewState->Passages.Emplace(WorldWall);
	}
	else
	{
		TArray<TSharedPtr<const FBreachPreparedOpening, ESPMode::ThreadSafe>> Openings;
		for (int32 Index : Selected)
		{
			FHoleComponent& Component = HoleComponents[Index];
			if (!Component.PreparedOpening.IsValid())
			{
				auto Prepared = MakeShared<FBreachPreparedOpening, ESPMode::ThreadSafe>();
				const FGeneralPolygon2d WorldDamage = BreachPassageSolver::Transform(Component.Polygon, Policy.WorldScale);
				if (!BreachPassageSolver::Prepare(WorldDamage, WorldWall, Radius, *Prepared, LastBuildStats, OutError))
				{
					return false;
				}
				Component.PreparedOpening = Prepared;
			}
			Openings.Add(Component.PreparedOpening);
		}
		const FBreachPassageState Empty;
		const FBreachPassageState& Previous = PassageState.IsValid() ? *PassageState : Empty;
		if (!BreachPassageSolver::Solve(Openings, Previous, Policy.MaxCount, Radius, *NewState, LastBuildStats, OutError))
		{
			return false;
		}
	}

	TArray<FGeneralPolygon2d> ChangedA, ChangedB;
	if (bBuiltCollisionValid && PassageState.IsValid())
	{
		LastBuildStats.BooleanOperations += 2;
		if (!PolygonsDifference(NewState->Passages, PassageState->Passages, ChangedA))
		{
			OutError = TEXT("New passage area could not be compared with published collision.");
			return false;
		}
		if (!PolygonsDifference(PassageState->Passages, NewState->Passages, ChangedB))
		{
			OutError = TEXT("Published passage area could not be compared with the new passages.");
			return false;
		}
	}
	double ChangedArea = 0;
	for (const auto& Part : ChangedA)
	{
		ChangedArea += FMath::Abs(Part.SignedArea());
	}
	for (const auto& Part : ChangedB)
	{
		ChangedArea += FMath::Abs(Part.SignedArea());
	}
	LastBuildStats.bCollisionChanged = !bBuiltCollisionValid || !PassageState.IsValid() || ChangedArea > 1.e-6;

	FKAggregateGeom Collision;
	if (LastBuildStats.bCollisionChanged)
	{
		TArray<FGeneralPolygon2d> Cuts;
		const FVector2d InverseScale(1 / Policy.WorldScale.X, 1 / Policy.WorldScale.Y);
		for (const auto& Passage : NewState->Passages)
		{
			Cuts.Add(BreachPassageSolver::Transform(Passage, InverseScale));
		}
		TArray<FGeneralPolygon2d> Regions = {FGeneralPolygon2d(OriginalOuter)};
		if (!Cuts.IsEmpty())
		{
			++LastBuildStats.BooleanOperations;
			if (!PolygonsDifference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(OriginalOuter)}, Cuts, Regions))
			{
				OutError = TEXT("Simple collision regions could not be built from the passages.");
				return false;
			}
			RemoveRepeatedBoundaryVertices(Regions);
			if (!BreachSurfaceGeometry::ResolveBoundaryOverlaps(Regions, OutError))
			{
				return false;
			}
		}
		for (const FGeneralPolygon2d& Region : Regions)
		{
			TArray<FVector2d> Vertices;
			TArray<FIndex3i> Triangles;
			if (!BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, OutError))
			{
				return false;
			}
			if (!BuildPrisms(Vertices, Triangles, DepthRange, Collision, OutError, AreaScale, LastBuildStats.DiscardedWorldArea))
			{
				return false;
			}
		}
	}
	else
	{
		Collision = BuiltCollision;
	}

	// 확정된 파생 상태만 공개한다. 입력 구멍 도형은 변경하지 않는다.
	PassageState = NewState;
	BuiltRevisions = MoveTemp(Revisions);
	BuiltCollision = MoveTemp(Collision);
	bBuiltCollisionValid = true;
	OutCollision = BuiltCollision;
	bSucceeded = true;
	return true;
}

bool FBreachSimpleCollision::BuildMesh(TConstArrayView<FGeneralPolygon2d> RemainingRegions, const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | BuildConvex");
	OutError.Reset();

	const double AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	const int32 MaxConvexCount = UBreachSystemSettings::GetMaxSimpleConvexCount();
	const double Bias = 1.e-7;
	if (OriginalOuter.VertexCount() < 3 || !FMath::IsFinite(ComponentArea) || ComponentArea <= 0)
	{
		OutError = TEXT("Simple collision requires an initialized surface and a finite positive area scale.");
		return false;
	}
	if (RemainingRegions.IsEmpty())
	{
		OutCollision = FKAggregateGeom();
		return true;
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

		TArray<FGeneralPolygon2d> SafeHoles;
		const bool bSimplified = SimplifyInside(Hole, OriginalOuter, AreaThreshold / ComponentArea, MaxConvexCount, SafeHoles, OutError);
		if (!bSimplified)
		{
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

		RemoveRepeatedBoundaryVertices(CollisionRegions);
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
	double DiscardedWorldArea = 0;
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

		const bool bPrismsBuilt = BuildPrisms(Vertices, Triangles, DepthRange, Collision, OutError, ComponentArea, DiscardedWorldArea);
		if (!bPrismsBuilt)
		{
			return false;
		}
	}

	OutCollision = MoveTemp(Collision);
	return true;
}
