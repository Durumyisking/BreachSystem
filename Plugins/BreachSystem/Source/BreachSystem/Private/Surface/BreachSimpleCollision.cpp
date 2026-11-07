#include "Surface/BreachSimpleCollision.h"
#include "CompGeom/ConvexHull2.h"
#include "CompGeom/ConvexHull3.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
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
	for (FGeneralPolygon2d& Hole : Connected)
	{
		// 구멍에 둘러싸인 벽은 이미 고립 제거 대상이다. Detached 경계 오차로 남은 내부 틈도 채운다.
		Hole.ClearHoles();

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

		if (SafeHoles.IsEmpty())
		{
			continue;
		}

		// 같은 경계를 다시 자를 때 미세한 틈이 남지 않도록 구멍에 0.001cm 여유를 준다.
		TArray<FGeneralPolygon2d> ExpandedHoles;
		const bool bExpandedHole = PolygonsOffset(1.e-3, SafeHoles, ExpandedHoles, false, 2.0);
		if (!bExpandedHole)
		{
			OutError = TEXT("Simple hole boundary could not be expanded.");
			return false;
		}

		SimpleCuts.Append(MoveTemp(ExpandedHoles));
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

	// 중간에 실패해도 기존 출력 충돌을 유지하도록 임시 결과에 구성한다.
	FKAggregateGeom Collision;
	for (FGeneralPolygon2d Region : RemainingRegions)
	{
		// 누적 영역은 유지하고 삼각분할에 사용할 복사본의 미세 정점만 정리한다.
		Region.Simplify(1.e-4, 1.e-4);
		if (FMath::Abs(Region.GetOuter().SignedArea()) <= 1.e-8)
		{
			continue;
		}
		Region.FilterHoles([](const FPolygon2d& Hole)
		{
			return FMath::Abs(Hole.SignedArea()) <= 1.e-8;
		});

		// 구멍을 제외한 남은 벽 영역을 2D 삼각형으로 분할한다.
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles;
		const bool bTriangulated = BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, OutError);
		if (!bTriangulated)
		{
			return false;
		}

		// 각 삼각형을 병합의 시작점이 되는 볼록 조각으로 등록한다.
		TArray<TArray<int32>> Parts;
		for (const FIndex3i& Triangle : Triangles)
		{
			TArray<int32> Part = {Triangle.A, Triangle.B, Triangle.C};
			Parts.Add(MoveTemp(Part));
		}

		// 변을 공유하는 조각 중 합쳐도 볼록한 쌍을 더 이상 없을 때까지 병합한다.
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
					// 변의 방향과 무관하게 같은 키를 사용해 인접 조각을 찾는다.
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

		// 남은 볼록 조각을 로컬 XZ 평면에 놓고 Y축 두께를 주어 충돌 기둥으로 만든다.
		for (const TArray<int32>& Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}

			// Chaos와 같은 float 정밀도로 확인하여 선이나 평면으로 무너진 조각은 제외한다.
			const FExtremePoints3f Extreme(Part.Num() * 2, [&](int32 VertexIndex)
			{
				const int32 Index = Part[VertexIndex % Part.Num()];
				const double Depth = VertexIndex < Part.Num() ? DepthRange.X : DepthRange.Y;
				return FVector3f(static_cast<float>(Vertices[Index].X),
					static_cast<float>(Depth), static_cast<float>(Vertices[Index].Y));
			});
			if (Extreme.Dimension < 3)
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

			// 완성된 3D 정점에 맞춰 각 충돌 기둥의 경계 상자를 갱신한다.
			Convex.UpdateElemBox();
		}
	}

	OutCollision = MoveTemp(Collision);
	return true;
}
