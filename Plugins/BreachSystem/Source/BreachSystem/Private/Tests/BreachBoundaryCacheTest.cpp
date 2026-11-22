#include "Misc/AutomationTest.h"

// 테스트 전용: 동일한 성장 이력에서 캐시 유무에 따른 실제 형상·실패 결과·검사량·시간을 비교한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Geometry/BreachSimpleCollision.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	using namespace UE::Geometry;
	FPolygon2d CacheBox(double X0, double Y0, double X1, double Y1)
	{
		FPolygon2d Polygon;
		for (const FVector2d Point : {FVector2d(X0, Y0), FVector2d(X1, Y0), FVector2d(X1, Y1), FVector2d(X0, Y1)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	bool SameShapes(const FKAggregateGeom& A, const FKAggregateGeom& B)
	{
		if (A.ConvexElems.Num() != B.ConvexElems.Num() || A.GetElementCount() != B.GetElementCount())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.ConvexElems.Num(); ++Index)
		{
			if (A.ConvexElems[Index].VertexData != B.ConvexElems[Index].VertexData || A.ConvexElems[Index].IndexData != B.ConvexElems[Index].IndexData)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachBoundaryCacheTest, "BreachSystem.Surface.SimpleBoundaryCache", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachBoundaryCacheTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Wall = CacheBox(-50, -50, 50, 50);
	FString Csv = TEXT("Round,Step,CachedMs,UncachedMs,PointHits,EdgeHits,ExactHits,CacheBytes,CachedNodes,UncachedNodes\n");
	uint64 Hits = 0, CachedNodes = 0, UncachedNodes = 0, MaxBytes = 0;
	double CachedMs = 0, UncachedMs = 0;
	int32 Compared = 0;
	for (int32 Round = 0; Round < 3; ++Round)
	{
		FBreachSimpleCollision Cached, Uncached;
		Cached.Initialize(Wall, {-5, 5});
		Uncached.Initialize(Wall, {-5, 5});
		FBreachSimpleBuildPolicy Policy;
		Policy.WorldScale = FVector2d(8, 5.4);
		Policy.AreaThreshold = 500;
		Policy.MaxCount = 4;
		int32 Step = 0;
		auto Compare = [&](const FPolygon2d& Cutter)
		{
			FString CachedError, UncachedError;
			const bool bCachedCut = Cached.ApplyCut(Cutter, {}, CachedError);
			const bool bUncachedCut = Uncached.ApplyCut(Cutter, {}, UncachedError);
			TestEqual(TEXT("Cut success is identical"), bCachedCut, bUncachedCut);
			TestEqual(TEXT("Cut failure reason is identical"), CachedError, UncachedError);
			FBreachSimpleBuildPolicy ReferencePolicy = Policy;
			ReferencePolicy.bDisableBoundaryCache = true;
			FKAggregateGeom CachedShapes, UncachedShapes;
			bool bCachedBuilt, bUncachedBuilt;
			if ((Round + Step) % 2 == 0)
			{
				bCachedBuilt = Cached.BuildMeshFromCuts(Policy, CachedShapes, CachedError);
				bUncachedBuilt = Uncached.BuildMeshFromCuts(ReferencePolicy, UncachedShapes, UncachedError);
			}
			else
			{
				bUncachedBuilt = Uncached.BuildMeshFromCuts(ReferencePolicy, UncachedShapes, UncachedError);
				bCachedBuilt = Cached.BuildMeshFromCuts(Policy, CachedShapes, CachedError);
			}
			TestEqual(TEXT("Build success is identical"), bCachedBuilt, bUncachedBuilt);
			TestEqual(TEXT("Build failure reason is identical"), CachedError, UncachedError);
			if (bCachedBuilt && bUncachedBuilt)
			{
				TestTrue(*FString::Printf(TEXT("Exact prism vertices and indices match at round %d step %d"), Round, Step), SameShapes(CachedShapes, UncachedShapes));
				const FBreachSimpleBuildStats& A = Cached.GetLastBuildStats();
				const FBreachSimpleBuildStats& B = Uncached.GetLastBuildStats();
				TestEqual(TEXT("Physics recook decision matches"), A.bCollisionChanged, B.bCollisionChanged);
				TestEqual(TEXT("Passage count matches"), A.PassagePieceCount, B.PassagePieceCount);
				if (Step < 20)
				{
					Hits += A.PointCacheHits + A.EdgeCacheHits + A.ExactCacheHits;
					CachedNodes += A.BoundaryNodeVisits;
					UncachedNodes += B.BoundaryNodeVisits;
					MaxBytes = FMath::Max(MaxBytes, A.BoundaryCacheBytes);
					CachedMs += A.GeometryMs;
					UncachedMs += B.GeometryMs;
					++Compared;
					Csv += FString::Printf(TEXT("%d,%d,%.6f,%.6f,%llu,%llu,%llu,%llu,%llu,%llu\n"), Round, Step, A.GeometryMs, B.GeometryMs, A.PointCacheHits, A.EdgeCacheHits, A.ExactCacheHits, A.BoundaryCacheBytes, A.BoundaryNodeVisits, B.BoundaryNodeVisits);
				}
			}
			++Step;
			return bCachedBuilt && bUncachedBuilt;
		};

		for (int32 Shot = 0; Shot < 20; ++Shot)
		{
			const FVector2d Center(-25 + Shot * 2.5, FMath::Sin(Shot * 0.6) * 5);
			FPolygon2d Cutter;
			FRandomStream Random(200 + Shot);
			for (int32 Vertex = 0; Vertex < 24; ++Vertex)
			{
				const double Angle = UE_DOUBLE_TWO_PI * Vertex / 24;
				Cutter.AppendVertex(Center + Random.FRandRange(3.8f, 4.2f) * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)));
			}
			Compare(Cutter);
		}
		Compare(CacheBox(-45, 25, -30, 45));
		Compare(CacheBox(-35, -5, -20, 30));
		Compare(CacheBox(45, -10, 60, 10));
		Compare(CacheBox(45, -10, 60, 10));
		Policy.MaxCount = 1;
		Compare(CacheBox(-0.0, -25, 0.5, -24));
		Policy.WorldScale = FVector2d(2, 0.2);
		Compare(CacheBox(-20, -20, 20, 20));
		Policy.AreaThreshold = 1.e8;
		Compare(CacheBox(-100, -100, 100, 100));
		FPolygon2d Invalid;
		Invalid.AppendVertex({0, 0});
		Invalid.AppendVertex({1, 0});
		Compare(Invalid);

		Cached.Initialize(Wall, {-5, 5});
		Uncached.Initialize(Wall, {-5, 5});
		Policy.WorldScale = FVector2d(8, 5.4);
		Policy.AreaThreshold = 500;
		Policy.MaxCount = 4;
		// 내부 섬이 있는 경계, 섬과 바깥 경계의 연결, 섬 제거를 차례로 비교한다.
		TestTrue(TEXT("Ring bottom builds in both modes"), Compare(CacheBox(-20, -20, 20, -10)));
		TestTrue(TEXT("Ring top builds in both modes"), Compare(CacheBox(-20, 10, 20, 20)));
		TestTrue(TEXT("Ring left builds in both modes"), Compare(CacheBox(-20, -10, -10, 10)));
		TestTrue(TEXT("Ring closes in both modes"), Compare(CacheBox(10, -10, 20, 10)));
		TestTrue(TEXT("Island bridge builds in both modes"), Compare(CacheBox(-12, -2, 12, 2)));
		TestTrue(TEXT("Island removal builds in both modes"), Compare(CacheBox(-11, -11, 11, 11)));
		FPolygon2d Collinear;
		for (const FVector2d Point : {FVector2d(-25, -0.0), FVector2d(-23, 1.e-10), FVector2d(-21, 0), FVector2d(-21, 5), FVector2d(-25, 5)})
		{
			Collinear.AppendVertex(Point);
		}
		Compare(Collinear);
	}
	TestEqual(TEXT("All growth samples build successfully in both modes"), Compared, 60);
	TestTrue(TEXT("Repeated boundary queries actually hit the cache"), Hits > 0);
	TestTrue(TEXT("Caching reduces boundary search work"), CachedNodes < UncachedNodes);
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/BoundaryMemo"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Paired timing samples are saved"), FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, TEXT("paired.csv"))));
	AddInfo(FString::Printf(TEXT("Boundary memo: %d paired growth builds, cached=%.3fms uncached=%.3fms, nodes=%llu/%llu, hits=%llu, cache allocation upper bound=%llu bytes."), Compared, CachedMs, UncachedMs, CachedNodes, UncachedNodes, Hits, MaxBytes));
	return !HasAnyErrors();
}
#endif
