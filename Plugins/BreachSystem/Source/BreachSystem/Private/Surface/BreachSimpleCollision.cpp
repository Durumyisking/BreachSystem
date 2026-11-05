#include "Surface/BreachSimpleCollision.h"
#include "CompGeom/ConvexHull2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Settings/BreachSystemSettings.h"

#include "BreachSurfaceGeometry.h"

namespace
{
	using namespace UE::Geometry;

	double Area(TConstArrayView<FGeneralPolygon2d> Regions)
	{
		double Result = 0;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			Result += FMath::Abs(Region.SignedArea());
		}

		return Result;
	}

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
}

void FBreachSimpleCollision::Initialize(const FPolygon2d& Outer, const FVector2d& Depth)
{
	OriginalOuter = Outer;
	DepthRange = Depth;
	Holes.Reset();
	RemainingRegions = {FGeneralPolygon2d(Outer)};
	bDirty = true;
}

										// 이번에 컷팅될 구멍,		// 고립된 벽
bool FBreachSimpleCollision::ApplyCut(const FPolygon2d& Cutter, TConstArrayView<FPolygon2d> Detached, FString& OutError)
{
	return ApplyCutDebug(nullptr, FTransform::Identity, Cutter, Detached, OutError);
}

bool FBreachSimpleCollision::ApplyCutDebug(UObject* WorldContext, const FTransform ComponentTransform, const UE::Geometry::FPolygon2d& Cutter, TConstArrayView<UE::Geometry::FPolygon2d> Detached, FString& OutError)
{
	// 검증
	const double AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	const double Bias = 1.e-7;
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | UpdateRegions");
	OutError.Reset();

	if (OriginalOuter.VertexCount() < 3 || !FMath::IsFinite(AreaThreshold) || AreaThreshold <= 0)
	{
		OutError = TEXT("Simple collision requires an initialized surface and a positive area threshold.");
		return false;
	}

	TArray<FGeneralPolygon2d> Incoming; // 이번 파괴에 간단화될 구멍 폴리곤
	const TArray<FGeneralPolygon2d> Original = { FGeneralPolygon2d(OriginalOuter) };
	const TArray<FGeneralPolygon2d> Cutters = { FGeneralPolygon2d(Cutter) };
	const bool bClipped = PolygonsIntersection(Original, Cutters, Incoming); // 벽 범위를 넘어가는 Cutter을 제거한다.
	if (!bClipped)
	{
		OutError = TEXT("Simple cutter could not be clipped to the original wall.");
		return false;
	}

	// 간단화 될 폴리곤에 고립된 벽 부분 추가한다.
	for (const FPolygon2d& Region : Detached)
	{
		Incoming.Add(FGeneralPolygon2d(Region));
	}

	if (Incoming.IsEmpty())
	{
		return true;
	}

	FAxisAlignedBox2d Affected = FAxisAlignedBox2d::Empty();
	for (const FGeneralPolygon2d& Region : Incoming)
	{
		Affected.Contain(Region.GetOuter().Bounds());
	}
	// 맞닿은 구멍을 경계 오차 때문에 놓치지 않도록 여유준다.
	Affected.Expand(Bias);

	TArray<bool> Selected;
	Selected.Init(false, Holes.Num());

	bool bExpanded = true;
	while (bExpanded)
	{
		bExpanded = false;
		for (int32 Index = 0; Index < Holes.Num(); ++Index)
		{
			if (Selected[Index])
			{
				continue;
			}

			if (!Affected.Intersects(Holes[Index].Bounds)) // 기존 구멍과 이번에 처리할 구멍의 겹치는 부분을 구한다.
			{
				continue;
			}

			Selected[Index] = true;
			Incoming.Add(Holes[Index].Polygon); // 이번에 처리할 구멍에 겹쳤던 구멍 추가한다.
			Affected.Contain(Holes[Index].Bounds);  // 이번에 처리할 구멍의 AABB에 겹쳤던 구멍의 AABB 추가한다.
			bExpanded = true;
		}
	}

	TArray<FGeneralPolygon2d> Connected;
	const bool bUnited = PolygonsUnion(Incoming, Connected, false); // 겹친 구멍들을 하나로 합친다. (겹치지 않는 구멍은 그대로 유지)
	if (!bUnited)
	{
		OutError = TEXT("Connected simple Holes could not be united.");
		return false;
	}
	// 현재 상태 : 모든 구멍들이 Connected로 통합되어있음

	TArray<FGeneralPolygon2d> SimpleCuts;
	for (const FGeneralPolygon2d& Hole : Connected)
	{
		const double LocalArea = FMath::Abs(Hole.SignedArea());
		//UE_LOG(LogTemp, Log, TEXT("Local %f"), LocalArea);
		if (LocalArea + Bias < AreaThreshold)
		{
			// 완전히 없어진 벽에는 임계값과 무관하게 충돌도 남기지 않는다.
			if (LocalArea < FMath::Abs(OriginalOuter.SignedArea()) - 1.e-7)
			{
				continue;
			}
		}

		// 컴포넌트 Scale이 오차판정에 영향이 가지 않게 한다.
		FGeneralPolygon2d Simplified = Hole;
		Simplified.Simplify(0.0, 1.0);
		if (WorldContext)
		{
			BreachSurfaceGeometry::DebugDrawPolygon2d(WorldContext, ComponentTransform, Simplified);
		}

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

	TArray<FGeneralPolygon2d> CandidateRegions;
	bool bChanged = false;
	if (!SimpleCuts.IsEmpty())
	{
		const bool bCut = PolygonsDifference(RemainingRegions, SimpleCuts, CandidateRegions);
		if (!bCut)
		{
			OutError = TEXT("Simple wall regions could not be cut.");
			return false;
		}

		bChanged = Area(RemainingRegions) - Area(CandidateRegions) > 1.e-8;
	}

	// 구멍 기록 캐시 갱신
	for (int32 Index = Holes.Num() - 1; Index >= 0; --Index)
	{
		if (Selected[Index])
		{
			Holes.RemoveAt(Index);
		}
	}
	for (FGeneralPolygon2d& Region : Connected)
	{
		const FAxisAlignedBox2d Bounds = Region.GetOuter().Bounds();
		Holes.Add({ MoveTemp(Region), Bounds });
	}


	if (bChanged)
	{
		RemainingRegions = MoveTemp(CandidateRegions);
		bDirty = true;
	}

	return true;
}

bool FBreachSimpleCollision::BuildMesh(FKAggregateGeom& OutCollision, FString& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | BuildConvex");
	OutError.Reset();

	FKAggregateGeom Collision;
	for (const FGeneralPolygon2d& Region : RemainingRegions)
	{
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles;
		const bool bTriangulated = BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, OutError);
		if (!bTriangulated)
		{
			return false;
		}

		TArray<TArray<int32>> Parts;
		for (const FIndex3i& Triangle : Triangles)
		{
			TArray<int32> Part = {Triangle.A, Triangle.B, Triangle.C};
			Parts.Add(MoveTemp(Part));
		}

		bool bMerged = true;
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
		}

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
