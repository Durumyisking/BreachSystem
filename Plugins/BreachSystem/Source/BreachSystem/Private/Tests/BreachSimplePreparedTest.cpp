#include "Surface/BreachSimpleCollision.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Misc/AutomationTest.h"
#include "../Surface/BreachPassageSolver.h"

// 테스트 전용: 공간 인덱스의 결과 동등성과 실제 검사량, 캐시 재사용을 비교한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;
	FPolygon2d PreparedBox(double X0, double Y0, double X1, double Y1)
	{
		FPolygon2d Polygon;
		for (const FVector2d P : {FVector2d(X0, Y0), FVector2d(X1, Y0), FVector2d(X1, Y1), FVector2d(X0, Y1)})
		{
			Polygon.AppendVertex(P);
		}
		return Polygon;
	}
	double PreparedFootprint(const FKAggregateGeom& Collision)
	{
		double Area = 0;
		for (const FKConvexElem& Convex : Collision.ConvexElems)
		{
			FPolygon2d Polygon;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector P = Convex.VertexData[Index];
				Polygon.AppendVertex({P.X, P.Z});
			}
			Area += FMath::Abs(Polygon.SignedArea());
		}
		return Area;
	}
	TArray<FGeneralPolygon2d> PreparedFootprints(const FKAggregateGeom& Collision)
	{
		TArray<FGeneralPolygon2d> Result;
		for (const FKConvexElem& Convex : Collision.ConvexElems)
		{
			FPolygon2d Polygon;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector P = Convex.VertexData[Index];
				Polygon.AppendVertex({P.X, P.Z});
			}
			Result.Emplace(Polygon);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimplePreparedTest, "BreachSystem.Surface.SimplePrepared", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachSimplePreparedTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Wall = PreparedBox(-150, -100, 150, 100);
	FPolygon2d Teeth;
	Teeth.AppendVertex({-60, -50});
	Teeth.AppendVertex({60, -50});
	for (int32 X = 60; X >= -60; --X)
	{
		Teeth.AppendVertex({static_cast<double>(X), X % 2 == 0 ? 50.0 : 46.0});
	}
	FBreachSimpleBuildPolicy Policy;
	Policy.AreaThreshold = 500;
	Policy.MaxCount = 3;
	FBreachSimpleCollision Indexed, Linear;
	Indexed.Initialize(Wall, {-5, 5});
	Linear.Initialize(Wall, {-5, 5});
	FString Error;
	FKAggregateGeom IndexedShapes, LinearShapes;
	TestTrue(TEXT("Indexed fixture cut"), Indexed.ApplyCut(Teeth, {}, Error));
	TestTrue(TEXT("Linear fixture cut"), Linear.ApplyCut(Teeth, {}, Error));
	if (!TestTrue(*Error, Indexed.BuildMeshFromCuts(Policy, IndexedShapes, Error)))
	{
		return false;
	}
	Policy.bLinearBoundaryQueries = true;
	if (!TestTrue(*Error, Linear.BuildMeshFromCuts(Policy, LinearShapes, Error)))
	{
		return false;
	}
	TestTrue(TEXT("Boundary acceleration preserves collision area"), FMath::IsNearlyEqual(PreparedFootprint(IndexedShapes), PreparedFootprint(LinearShapes), 1.e-5));
	auto SameFootprint = [&](const FKAggregateGeom& A, const FKAggregateGeom& B)
	{
		TArray<FGeneralPolygon2d> Added, Lost;
		if (!PolygonsDifference(PreparedFootprints(A), PreparedFootprints(B), Added))
		{
			return false;
		}
		if (!PolygonsDifference(PreparedFootprints(B), PreparedFootprints(A), Lost))
		{
			return false;
		}
		double DifferenceArea = 0;
		for (const auto& Part : Added)
		{
			DifferenceArea += FMath::Abs(Part.SignedArea());
		}
		for (const auto& Part : Lost)
		{
			DifferenceArea += FMath::Abs(Part.SignedArea());
		}
		return DifferenceArea < 1.e-5;
	};
	TestTrue(TEXT("Boundary acceleration preserves the complete spatial footprint"), SameFootprint(IndexedShapes, LinearShapes));
	const FBreachSimpleBuildStats Fast = Indexed.GetLastBuildStats(), Slow = Linear.GetLastBuildStats();
	TestTrue(TEXT("AABB hierarchy performs fewer boundary-box tests than a full scan"), Fast.BoundaryNodeVisits < Slow.BoundaryNodeVisits);
	AddInfo(FString::Printf(TEXT("Prepared boundary comparison: indexed=%llu linear=%llu box tests, indexed=%.3fms linear=%.3fms, boolean=%llu/%llu."), Fast.BoundaryNodeVisits, Slow.BoundaryNodeVisits, Fast.GeometryMs, Slow.GeometryMs, Fast.BooleanOperations, Slow.BooleanOperations));
	Policy.bLinearBoundaryQueries = false;
	Policy.MaxCount = 4;
	TestTrue(TEXT("Count-only policy change rebuilds selection"), Indexed.BuildMeshFromCuts(Policy, IndexedShapes, Error));
	TestEqual(TEXT("Count-only change reuses opening morphology"), Indexed.GetLastBuildStats().PreparedOpenings, 0);
	Policy.MaxCount = 3;
	TestTrue(TEXT("Restoring the count builds"), Indexed.BuildMeshFromCuts(Policy, IndexedShapes, Error));
	TestTrue(TEXT("Unrelated subthreshold cut"), Indexed.ApplyCut(PreparedBox(100, 70, 102, 72), {}, Error));
	TestTrue(TEXT("Unchanged eligible openings build"), Indexed.BuildMeshFromCuts(Policy, IndexedShapes, Error));
	TestEqual(TEXT("Unchanged openings perform no polygon boolean"), Indexed.GetLastBuildStats().BooleanOperations, uint64(0));
	TestEqual(TEXT("Unchanged openings perform no boundary query"), Indexed.GetLastBuildStats().BoundaryTests, uint64(0));
	TestFalse(TEXT("Unchanged passages do not request physics recooking"), Indexed.GetLastBuildStats().bCollisionChanged);
	TestTrue(TEXT("Another broad body cut"), Indexed.ApplyCut(PreparedBox(90, -40, 140, 20), {}, Error));
	TestTrue(TEXT("Another body builds"), Indexed.BuildMeshFromCuts(Policy, IndexedShapes, Error));
	TestEqual(TEXT("Only the changed opening is prepared"), Indexed.GetLastBuildStats().PreparedOpenings, 1);
	TestTrue(TEXT("The unchanged broad body is reused"), Indexed.GetLastBuildStats().ReusedBodies > 0);

	FBreachSimpleCollision Anisotropic;
	Anisotropic.Initialize(Wall, {-5, 5});
	TestTrue(TEXT("Anisotropic fixture cut"), Anisotropic.ApplyCut(PreparedBox(-50, -50, 50, 50), {}, Error));
	Policy.WorldScale = FVector2d(5, 0.2);
	TestTrue(TEXT("Anisotropic build uses world dimensions"), Anisotropic.BuildMeshFromCuts(Policy, IndexedShapes, Error));
	TestTrue(TEXT("Large area with insufficient world width remains blocked"), FMath::IsNearlyEqual(PreparedFootprint(IndexedShapes), 60000.0, 1.e-5));

	FBreachPreparedOpening Rounded;
	FBreachSimpleBuildStats Stats;
	const double Radius = FMath::Sqrt(1000.0 / UE_DOUBLE_PI);
	TestTrue(TEXT("Analytic rounded-square fixture prepares"), BreachPassageSolver::Prepare(FGeneralPolygon2d(PreparedBox(-50, -50, 50, 50)), Wall, Radius, Rounded, Stats, Error));
	double RoundedArea = 0;
	for (const auto& Part : Rounded.Available)
	{
		RoundedArea += FMath::Abs(Part.SignedArea());
	}
	const double Side = 100 - 2 * Radius * 0.004;
	const double AnalyticArea = Side * Side - (4 - UE_DOUBLE_PI) * Radius * Radius;
	TestTrue(FString::Printf(TEXT("Rounded restoration follows quarter circles: measured %.3f vs analytic %.3f"), RoundedArea, AnalyticArea), FMath::Abs(RoundedArea - AnalyticArea) < 5);

	FBreachPassageState InvalidPrevious;
	InvalidPrevious.MaxCount = 3;
	auto WrongBody = MakeShared<FBreachPassageBody, ESPMode::ThreadSafe>();
	WrongBody->Domain = FGeneralPolygon2d(PreparedBox(100, -10, 120, 10));
	WrongBody->Pieces.Add(WrongBody->Domain.GetOuter());
	InvalidPrevious.Bodies.Add(WrongBody);
	InvalidPrevious.Passages.Add(WrongBody->Domain);
	auto Actual = MakeShared<FBreachPreparedOpening, ESPMode::ThreadSafe>();
	Actual->Damage.Emplace(PreparedBox(-20, -20, 20, 20));
	Actual->Available = Actual->Damage;
	Actual->Candidates.Add(Actual->Damage[0].GetOuter());
	FBreachPassageState Checked;
	TestFalse(TEXT("Independent final containment rejects inherited passage outside actual damage"), BreachPassageSolver::Solve(TArray<TSharedPtr<const FBreachPreparedOpening, ESPMode::ThreadSafe>>{Actual}, InvalidPrevious, 3, 4, Checked, Stats, Error));

	FPolygon2d RankingFixture;
	for (const FVector2d Point : {FVector2d(308,240), FVector2d(292,270), FVector2d(272,307), FVector2d(232,294), FVector2d(199,277), FVector2d(201,240), FVector2d(197,202), FVector2d(234,190), FVector2d(275,162), FVector2d(320,189)})
	{
		RankingFixture.AppendVertex(Point);
	}
	FBreachSimpleCollision Incremental, Rebuilt;
	const FPolygon2d RankingWall = PreparedBox(0, 0, 500, 500);
	Incremental.Initialize(RankingWall, {-5, 5});
	Rebuilt.Initialize(RankingWall, {-5, 5});
	FBreachSimpleBuildPolicy RankingPolicy;
	RankingPolicy.AreaThreshold = UE_DOUBLE_PI * 64;
	RankingPolicy.MaxCount = 1;
	TestTrue(TEXT("Incremental ranking fixture"), Incremental.ApplyCut(RankingFixture, {}, Error));
	TestTrue(TEXT("Full hull ranking fixture"), Rebuilt.ApplyCut(RankingFixture, {}, Error));
	TestTrue(TEXT("Incremental trial hulls build"), Incremental.BuildMeshFromCuts(RankingPolicy, IndexedShapes, Error));
	RankingPolicy.bRebuildTrialHulls = true;
	TestTrue(TEXT("Engine full trial hulls build"), Rebuilt.BuildMeshFromCuts(RankingPolicy, LinearShapes, Error));
	TestTrue(TEXT("Incremental hulls preserve the original best candidate and its complete footprint"), SameFootprint(IndexedShapes, LinearShapes));
	AddInfo(FString::Printf(TEXT("Incremental trial hull ranking: %.3fms vs engine full hull %.3fms."), Incremental.GetLastBuildStats().GeometryMs, Rebuilt.GetLastBuildStats().GeometryMs));
	return !HasAnyErrors();
}
#endif
