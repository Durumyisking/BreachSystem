#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "HAL/PlatformTime.h"
#include "CompGeom/ConvexHull3.h"
#include "Misc/AutomationTest.h"
#include "../Surface/BreachPassageSolver.h"

// 테스트 전용: 연결된 개구부의 총면적과 개별 Convex 통과 영역의 최소 면적을 구분한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d ConvexTestPolygon(std::initializer_list<FVector2d> Points)
	{
		FPolygon2d Polygon;
		for (const FVector2d& Point : Points)
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	double ConvexTestCollisionArea(const FKAggregateGeom& Collision)
	{
		double Area = 0;
		for (const FKConvexElem& Convex : Collision.ConvexElems)
		{
			FPolygon2d Footprint;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector& Point = Convex.VertexData[Index];
				Footprint.AppendVertex({Point.X, Point.Z});
			}
			Area += FMath::Abs(Footprint.SignedArea());
		}
		return Area;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleConvexTest, "BreachSystem.Surface.SimpleConvex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSimpleConvexTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Convex opening settings"), Settings))
	{
		return false;
	}
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 500.0);
	auto PublishedPieces = [](const FBreachSimpleCollision& Model)
	{
		TArray<FGeneralPolygon2d> Pieces;
		if (Model.PassageState.IsValid())
		{
			for (const auto& Body : Model.PassageState->Bodies)
			{
				for (const FPolygon2d& Piece : Body->Pieces)
				{
					Pieces.Emplace(Piece);
				}
			}
		}
		return Pieces;
	};
	const FPolygon2d Wall = ConvexTestPolygon({{-50, -50}, {50, -50}, {50, 50}, {-50, 50}});
	// 두 20x20 방은 폭 2의 통로로 연결된다. 총면적은 840이지만 내부 Convex는 500에 못 미친다.
	const FPolygon2d Opening = ConvexTestPolygon({{-30, -10}, {-10, -10}, {-10, -1}, {10, -1}, {10, -10}, {30, -10}, {30, 10}, {10, 10}, {10, 1}, {-10, 1}, {-10, 10}, {-30, 10}});
	FBreachSimpleCollision Collision;
	Collision.Initialize(Wall, {-5, 5});
	FString Error;
	FKAggregateGeom Shapes;
	TestTrue(TEXT("Connected opening is recorded"), Collision.ApplyCut(Opening, {}, Error));
	if (!TestTrue(FString::Printf(TEXT("Collision builds: %s"), *Error), Collision.BuildMeshFromCuts(1.0, Shapes, Error)))
	{
		return false;
	}
	TestTrue(TEXT("Subthreshold convex portions keep the complete wall"), FMath::IsNearlyEqual(ConvexTestCollisionArea(Shapes), 10000.0, 1.e-6));

	TGuardValue<int32> CountGuard(Settings->MaxSimpleConvexCount, 1);
	Settings->ComplexHoleSizeThreshold = 200.0;
	const double StartTime = FPlatformTime::Seconds();
	TestTrue(TEXT("Lower threshold rebuilds the same opening"), Collision.BuildMeshFromCuts(1.0, Shapes, Error));
	const double OneOpeningArea = 10000.0 - ConvexTestCollisionArea(Shapes);
	if (Collision.PassageState.IsValid())
	{
		AddInfo(FString::Printf(TEXT("Broad bodies: %d, candidates: %d, available: %d"), Collision.PassageState->Bodies.Num(), Collision.HoleComponents[0].PreparedOpening->Candidates.Num(), Collision.HoleComponents[0].PreparedOpening->Available.Num()));
		for (const auto& B : Collision.PassageState->Bodies)
		{
			AddInfo(FString::Printf(TEXT("Body bounds %.3f..%.3f area %.3f pieces %d"), B->Domain.GetOuter().Bounds().Min.X, B->Domain.GetOuter().Bounds().Max.X, FMath::Abs(B->Domain.SignedArea()), B->Pieces.Num()));
		}
		for (const auto& C : Collision.HoleComponents[0].PreparedOpening->Candidates)
		{
			AddInfo(FString::Printf(TEXT("Candidate bounds %.3f..%.3f area %.3f"), C.Bounds().Min.X, C.Bounds().Max.X, FMath::Abs(C.SignedArea())));
		}
	}
	TestTrue(TEXT("One convex opens a substantial part"), OneOpeningArea > 200.0);
	TestEqual(TEXT("One convex per broad body opens both rooms"), PublishedPieces(Collision).Num(), 2);
	Settings->MaxSimpleConvexCount = 3;
	TestTrue(TEXT("Count change rebuilds the same opening"), Collision.BuildMeshFromCuts(1.0, Shapes, Error));
	const double ThreeOpeningArea = 10000.0 - ConvexTestCollisionArea(Shapes);
	TestTrue(TEXT("A larger budget preserves both rooms"), ThreeOpeningArea >= OneOpeningArea - 1.e-6);
	TestTrue(TEXT("Count limit holds"), PublishedPieces(Collision).Num() <= 3);
	TestTrue(TEXT("At least two convexes are selected"), PublishedPieces(Collision).Num() >= 2);
	for (const FGeneralPolygon2d& Cut : PublishedPieces(Collision))
	{
		TestTrue(TEXT("Each passage is convex"), Cut.GetOuter().IsConvex(1.e-8, false));
		TestTrue(TEXT("Passages have no interior rings"), Cut.GetHoles().IsEmpty());
		TestTrue(TEXT("Each passage meets the area threshold"), FMath::Abs(Cut.SignedArea()) >= 200.0 - 1.e-7);
		TestTrue(TEXT("Each passage stays in the original opening"), FGeneralPolygon2d(Opening).Contains(Cut.GetOuter()));
	}
	AddInfo(FString::Printf(TEXT("Two uncached collision builds took %.3f ms; one/three passage area: %.3f / %.3f"), (FPlatformTime::Seconds() - StartTime) * 1000.0, OneOpeningArea, ThreeOpeningArea));

	TestTrue(TEXT("Scale change rebuilds the same opening"), Collision.BuildMeshFromCuts(0.25, Shapes, Error));
	TestTrue(TEXT("Scale-adjusted portions below the threshold keep the wall"), FMath::IsNearlyEqual(ConvexTestCollisionArea(Shapes), 10000.0, 1.e-6));
	FBreachSimpleCollision Fresh;
	Fresh.Initialize(Wall, {-5, 5});
	FKAggregateGeom FreshShapes;
	TestTrue(TEXT("Fresh same-policy input recorded"), Fresh.ApplyCut(Opening, {}, Error));
	TestTrue(TEXT("Fresh same-policy input builds"), Fresh.BuildMeshFromCuts(1.0, FreshShapes, Error));
	const double FreshOpeningArea = 10000.0 - ConvexTestCollisionArea(FreshShapes);
	TestTrue(TEXT("Restoring scale restores passages"), Collision.BuildMeshFromCuts(1.0, Shapes, Error));
	TestTrue(TEXT("Restored scale gives the same result"), FMath::IsNearlyEqual(10000.0 - ConvexTestCollisionArea(Shapes), FreshOpeningArea, 1.e-6));

	TArray<FGeneralPolygon2d> Remaining;
	const TArray<FGeneralPolygon2d> Walls = {FGeneralPolygon2d(Wall)};
	const TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Opening)};
	TestTrue(TEXT("Remaining-region fixture builds"), PolygonsDifference(Walls, Cutters, Remaining));
	TestTrue(TEXT("Region-derived path uses convex passages"), Collision.BuildMesh(Remaining, 1.0, Shapes, Error));
	TestTrue(TEXT("Both paths remove the same passage area"), FMath::IsNearlyEqual(10000.0 - ConvexTestCollisionArea(Shapes), FreshOpeningArea, 1.e-5));

	// 벽 밖의 오목한 팔은 실제 벽 안의 직사각형 통과 영역을 줄이지 않아야 한다.
	const FPolygon2d BorderCut = ConvexTestPolygon({{-90, 0}, {-70, 0}, {-70, -30}, {-20, -30}, {-20, 30}, {-70, 30}, {-70, 10}, {-90, 10}});
	FBreachSimpleCollision Border;
	Border.Initialize(Wall, {-5, 5});
	Settings->ComplexHoleSizeThreshold = 1800.0;
	TestTrue(TEXT("Nonconvex border cut recorded"), Border.ApplyCut(BorderCut, {}, Error));
	TestTrue(TEXT("Cached border collision builds"), Border.BuildMeshFromCuts(1.0, Shapes, Error));
	TestTrue(TEXT("Cached path excludes the narrow border rectangle"), FMath::IsNearlyEqual(ConvexTestCollisionArea(Shapes), 10000.0, 1.e-6));
	const TArray<FGeneralPolygon2d> BorderCutters = {FGeneralPolygon2d(BorderCut)};
	TestTrue(TEXT("Border remaining-region fixture builds"), PolygonsDifference(Walls, BorderCutters, Remaining));
	TestTrue(TEXT("Region-derived border collision builds"), Border.BuildMesh(Remaining, 1.0, Shapes, Error));
	TestTrue(TEXT("Region-derived path also excludes the narrow border rectangle"), FMath::IsNearlyEqual(ConvexTestCollisionArea(Shapes), 10000.0, 1.e-6));
	Settings->ComplexHoleSizeThreshold = 200.0;

	// 내부에 남은 섬은 제거 영역의 내부 링이다. Convex 후보가 섬을 덮지 않아야 한다.
	FBreachSimpleCollision RingCollision;
	RingCollision.Initialize(Wall, {-5, 5});
	for (const FPolygon2d& Cut : {
		ConvexTestPolygon({{-30, -30}, {30, -30}, {30, -5}, {-30, -5}}),
		ConvexTestPolygon({{-30, 5}, {30, 5}, {30, 30}, {-30, 30}}),
		ConvexTestPolygon({{-30, -5}, {-5, -5}, {-5, 5}, {-30, 5}}),
		ConvexTestPolygon({{5, -5}, {30, -5}, {30, 5}, {5, 5}})})
	{
		TestTrue(TEXT("Ring cut recorded without detaching the island"), RingCollision.ApplyCut(Cut, {}, Error));
	}
	TestTrue(TEXT("Ring collision builds"), RingCollision.BuildMeshFromCuts(1.0, Shapes, Error));
	TestTrue(TEXT("Ring produces convex passages"), !PublishedPieces(RingCollision).IsEmpty());
	for (const FGeneralPolygon2d& Cut : PublishedPieces(RingCollision))
	{
		TestTrue(TEXT("Ring passage is convex"), Cut.GetOuter().IsConvex(1.e-8, false));
		TestTrue(TEXT("Ring passage preserves the interior island"), RingCollision.HoleComponents[0].Polygon.Contains(Cut.GetOuter()));
	}

	// Chaos는 단정밀도 정점을 사용한다. Convex가 겹친 뒤 생긴 삼각형도 변환 후 넓이를 유지해야 한다.
	Settings->ComplexHoleSizeThreshold = 1000.0;
	FPolygon2d Teeth;
	Teeth.AppendVertex({-30, -20});
	Teeth.AppendVertex({30, -20});
	for (int32 X = 30; X >= -30; --X)
	{
		Teeth.AppendVertex({static_cast<double>(X), X % 2 == 0 ? 20.0 : 16.0});
	}
	for (const FPolygon2d& Cut : {ConvexTestPolygon({{-30, -30}, {30, -30}, {30, 30}, {10, 30}, {10, -10}, {-10, -10}, {-10, 30}, {-30, 30}}), Teeth})
	{
		FBreachSimpleCollision Precision;
		Precision.Initialize(Wall, {-50, 50});
		TestTrue(TEXT("Precision fixture cut recorded"), Precision.ApplyCut(Cut, {}, Error));
		TestTrue(TEXT("Precision fixture collision builds"), Precision.BuildMeshFromCuts(1.0, Shapes, Error));
		TArray<FGeneralPolygon2d> ExpectedRegions;
		TestTrue(TEXT("Expected retained-wall area can be measured"), PolygonsDifference(Walls, PublishedPieces(Precision), ExpectedRegions));
		double ExpectedArea = 0;
		for (const FGeneralPolygon2d& Region : ExpectedRegions)
		{
			ExpectedArea += FMath::Abs(Region.SignedArea());
		}
		TestTrue(TEXT("Precision repair preserves the complete retained-wall area"), FMath::IsNearlyEqual(ConvexTestCollisionArea(Shapes), ExpectedArea, 1.e-6));
		for (const FKConvexElem& Convex : Shapes.ConvexElems)
		{
			const FVector3f A(Convex.VertexData[0]);
			const FVector3f B(Convex.VertexData[1]);
			const FVector3f C(Convex.VertexData[2]);
			const float Area = FVector3f::CrossProduct(B - A, C - A).SizeSquared();
			TestTrue(FString::Printf(TEXT("Collision triangle survives float conversion (%.9g)"), Area), Area > 0);
			TArray<FVector3f> Points;
			for (const FVector& Point : Convex.VertexData)
			{
				Points.Add(FVector3f(Point));
			}
			TConvexHull3<float> Hull;
			TestTrue(FString::Printf(TEXT("Chaos hull solver accepts the prism (triangle area squared %.9g)"), Area), Hull.Solve<FVector3f>(Points));
		}
	}

	FBreachSimpleCollision Removed;
	Removed.Initialize(Wall, {-5, 5});
	Settings->ComplexHoleSizeThreshold = 20000.0;
	const FPolygon2d LargeCut = ConvexTestPolygon({{-100, -100}, {100, -100}, {100, 100}, {-100, 100}});
	TestTrue(TEXT("Complete removal recorded"), Removed.ApplyCut(LargeCut, {}, Error));
	TestTrue(TEXT("Complete removal builds below the threshold"), Removed.BuildMeshFromCuts(0.25, Shapes, Error));
	TestEqual(TEXT("Complete removal leaves no collision"), Shapes.ConvexElems.Num(), 0);
	TestTrue(TEXT("Empty remaining-region path builds"), Removed.BuildMesh({}, 0.25, Shapes, Error));
	TestEqual(TEXT("Empty remaining-region path leaves no collision"), Shapes.ConvexElems.Num(), 0);
	TestFalse(TEXT("Zero scale is rejected"), Collision.BuildMeshFromCuts(0.0, Shapes, Error));
	TestFalse(TEXT("Negative scale is rejected"), Collision.BuildMesh(Remaining, -1.0, Shapes, Error));

	Settings->MaxSimpleConvexCount = 0;
	AddExpectedError(TEXT("Invalid MaxSimpleConvexCount"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Invalid count falls back to three"), UBreachSystemSettings::GetMaxSimpleConvexCount(), 3);
	return !HasAnyErrors();
}
#endif
