#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Misc/AutomationTest.h"

// 테스트 전용: 승인된 넓은 본체 추출과 시간에 따른 통과 영역 보존을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d OpeningBox(double X0, double Y0, double X1, double Y1)
	{
		FPolygon2d Polygon;
		for (const FVector2d Point : {FVector2d(X0, Y0), FVector2d(X1, Y0), FVector2d(X1, Y1), FVector2d(X0, Y1)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	TArray<FGeneralPolygon2d> PassageFromCollision(const FPolygon2d& Wall, const FKAggregateGeom& Collision)
	{
		TArray<FGeneralPolygon2d> Footprints;
		for (const FKConvexElem& Convex : Collision.ConvexElems)
		{
			FPolygon2d Polygon;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector Point = Convex.VertexData[Index];
				Polygon.AppendVertex({Point.X, Point.Z});
			}
			Footprints.Emplace(Polygon);
		}
		TArray<FGeneralPolygon2d> Passage;
		PolygonsDifference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Wall)}, Footprints, Passage);
		return Passage;
	}

	double OpeningArea(TConstArrayView<FGeneralPolygon2d> Polygons)
	{
		double Area = 0;
		for (const FGeneralPolygon2d& Polygon : Polygons)
		{
			Area += FMath::Abs(Polygon.SignedArea());
		}
		return Area;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleOpeningTest, "BreachSystem.Surface.SimpleOpening", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSimpleOpeningTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Opening settings exist"), Settings))
	{
		return false;
	}
	TGuardValue<double> AreaGuard(Settings->ComplexHoleSizeThreshold, UE_DOUBLE_PI * 27.0 * 27.0);
	TGuardValue<int32> CountGuard(Settings->MaxSimpleConvexCount, 1);
	const FPolygon2d Wall = OpeningBox(0, 0, 800, 540);
	FBreachSimpleCollision Collision;
	Collision.Initialize(Wall, {-5, 5});
	FKAggregateGeom Shapes;
	FString Error;
	TestTrue(TEXT("Lower body cut"), Collision.ApplyCut(OpeningBox(230, 350, 645, 540), {}, Error));
	if (!TestTrue(*Error, Collision.BuildMeshFromCuts(1.0, Shapes, Error)))
	{
		return false;
	}
	const TArray<FGeneralPolygon2d> Lower = PassageFromCollision(Wall, Shapes);
	TestTrue(TEXT("Lower body opens"), Lower.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.Contains(FVector2d(400, 450)); }));
	TestTrue(TEXT("Upper body cut"), Collision.ApplyCut(OpeningBox(180, 80, 650, 290), {}, Error));
	TestTrue(TEXT("Thin neck cut"), Collision.ApplyCut(OpeningBox(250, 280, 276, 370), {}, Error));
	if (!TestTrue(*Error, Collision.BuildMeshFromCuts(1.0, Shapes, Error)))
	{
		return false;
	}
	const TArray<FGeneralPolygon2d> Separate = PassageFromCollision(Wall, Shapes);
	TestTrue(TEXT("Upper body opens with one convex per broad body"), Separate.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.Contains(FVector2d(400, 160)); }));
	TestFalse(TEXT("Thin neck stays excluded"), Separate.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.Contains(FVector2d(263, 320)); }));
	TArray<FGeneralPolygon2d> Lost;
	TestTrue(TEXT("Preservation can be measured"), PolygonsDifference(Lower, Separate, Lost));
	TestTrue(TEXT("Adding another body preserves the lower passage"), OpeningArea(Lost) < 0.001);

	Settings->MaxSimpleConvexCount = 3;
	TestTrue(TEXT("Broad connection cut"), Collision.ApplyCut(OpeningBox(260, 220, 590, 440), {}, Error));
	if (!TestTrue(*Error, Collision.BuildMeshFromCuts(1.0, Shapes, Error)))
	{
		return false;
	}
	const TArray<FGeneralPolygon2d> Joined = PassageFromCollision(Wall, Shapes);
	TestTrue(TEXT("Broad connection opens"), Joined.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.Contains(FVector2d(400, 320)); }));
	PolygonsDifference(Separate, Joined, Lost);
	TestTrue(TEXT("Connecting bodies preserves all existing passages"), OpeningArea(Lost) < 0.001);

	Settings->ComplexHoleSizeThreshold = 50;
	FBreachSimpleCollision Detached;
	Detached.Initialize(Wall, {-5, 5});
	for (const FPolygon2d& Strip : {OpeningBox(100, 100, 140, 105), OpeningBox(100, 135, 140, 140), OpeningBox(100, 105, 105, 135), OpeningBox(135, 105, 140, 135)})
	{
		TestTrue(TEXT("Ring boundary recorded without removing its center"), Detached.ApplyCut(Strip, {}, Error));
	}
	TestTrue(TEXT("Thin closed ring is prepared"), Detached.BuildMeshFromCuts(1.0, Shapes, Error));
	const auto BeforeDetached = PassageFromCollision(Wall, Shapes);
	TestFalse(TEXT("Ring center is still solid"), BeforeDetached.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.Contains(FVector2d(120, 120)); }));
	const TArray<FPolygon2d> Island = {OpeningBox(105, 105, 135, 135)};
	TestTrue(TEXT("Independent cut also removes the cached ring center"), Detached.ApplyCut(OpeningBox(400, 100, 402, 102), Island, Error));
	TestTrue(TEXT("Filled cached ring rebuilds"), Detached.BuildMeshFromCuts(1.0, Shapes, Error));
	const auto AfterDetached = PassageFromCollision(Wall, Shapes);
	TestTrue(TEXT("Removing an interior ring invalidates its prepared opening"), AfterDetached.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.Contains(FVector2d(120, 120)); }));
	return !HasAnyErrors();
}
#endif
