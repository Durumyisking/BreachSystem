#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/AggregateGeom.h"

// 테스트 전용: 전체 손상 원본과 면적 조건을 적용한 출력 충돌의 분리를 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d MakeRegionRectangle(double MinX, double MinY, double MaxX, double MaxY)
	{
		FPolygon2d Polygon;
		for (const FVector2d& Point : {FVector2d(MinX, MinY), FVector2d(MaxX, MinY), FVector2d(MaxX, MaxY), FVector2d(MinX, MaxY)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	FString RegionSnapshot(TConstArrayView<FGeneralPolygon2d> Regions)
	{
		FString Text;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			auto Append = [&](const FPolygon2d& Polygon)
			{
				Text += FString::Printf(TEXT("[%d]"), Polygon.VertexCount());
				for (const FVector2d& Point : Polygon.GetVertices())
				{
					Text += FString::Printf(TEXT("%.17g,%.17g;"), Point.X, Point.Y);
				}
			};
			Append(Region.GetOuter());
			Text += TEXT("holes");
			for (const FPolygon2d& Hole : Region.GetHoles())
			{
				Append(Hole);
			}
			Text += TEXT("end");
		}
		return Text;
	}

	double CollisionFootprintArea(const FKAggregateGeom& Collision)
	{
		double Area = 0;
		for (const FKConvexElem& Convex : Collision.ConvexElems)
		{
			FPolygon2d Footprint;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector& Point = Convex.VertexData[Index];
				Footprint.AppendVertex(FVector2d(Point.X, Point.Z));
			}
			Area += FMath::Abs(Footprint.SignedArea());
		}
		return Area;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleRemainingRegionsTest, "BreachSystem.Surface.SimpleRemainingRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSimpleRemainingRegionsTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Remaining-region settings"), Settings))
	{
		return false;
	}
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 450.0);
	const FPolygon2d Wall = MakeRegionRectangle(-50, -50, 50, 50);
	const TArray<FGeneralPolygon2d> UncutRegions = {FGeneralPolygon2d(Wall)};
	TArray<FGeneralPolygon2d> Regions = UncutRegions;
	FBreachSimpleCollision Collision;
	Collision.Initialize(Wall, {-5, 5});
	FString Error;
	FKAggregateGeom Shapes;

	auto ApplySurfaceCut = [&](const FPolygon2d& Cutter)
	{
		const TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Cutter)};
		TArray<FGeneralPolygon2d> Candidate;
		if (!PolygonsDifference(Regions, Cutters, Candidate))
		{
			return false;
		}
		Regions = MoveTemp(Candidate);
		return true;
	};

	TestTrue(TEXT("Small cut recorded in the supplied regions"), ApplySurfaceCut(MakeRegionRectangle(-25, -5, -5, 5)));
	if (!TestEqual(TEXT("One remaining wall region"), Regions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Subthreshold cut remains in the supplied region"), Regions[0].GetHoles().Num(), 1);
	const FString AfterSmallCut = RegionSnapshot(Regions);
	TestTrue(TEXT("Small cut collision builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Small cut stays solid in output"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestEqual(TEXT("Building collision does not fill the supplied hole"), RegionSnapshot(Regions), AfterSmallCut);
	TestTrue(TEXT("Second separate cut recorded"), ApplySurfaceCut(MakeRegionRectangle(5, -5, 25, 5)));
	TestTrue(TEXT("Separate cuts stay solid"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Separate output retains full wall"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestTrue(TEXT("Connecting cut recorded"), ApplySurfaceCut(MakeRegionRectangle(-6, -5, 6, 5)));
	const TArray<FGeneralPolygon2d> ConnectedRegions = Regions;
	const FString AfterBridge = RegionSnapshot(Regions);
	TestTrue(TEXT("Connected opening collision builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Connected but narrow opening stays blocked"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestEqual(TEXT("Connected collision build preserves supplied regions"), RegionSnapshot(Regions), AfterBridge);
	Settings->ComplexHoleSizeThreshold = 600.0;
	TestTrue(TEXT("Changed output threshold builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Higher threshold closes only output"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestEqual(TEXT("Threshold filtering never overwrites supplied regions"), RegionSnapshot(Regions), AfterBridge);

	Settings->ComplexHoleSizeThreshold = 450.0;
	TestTrue(TEXT("Same builder accepts an uncut wall without reinitialization"), Collision.BuildMesh(UncutRegions, 1.0, Shapes, Error));
	TestTrue(TEXT("Previous damage is not retained by the builder"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestTrue(TEXT("Same builder accepts the previous input again"), Collision.BuildMesh(ConnectedRegions, 1.0, Shapes, Error));
	TestTrue(TEXT("Supplying the narrow opening again keeps it blocked"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestEqual(TEXT("Repeated builds preserve the connected input"), RegionSnapshot(ConnectedRegions), AfterBridge);

	Regions = UncutRegions;
	TestTrue(TEXT("Small border cut recorded"), ApplySurfaceCut(MakeRegionRectangle(-60, -5, -45, 5)));
	TestTrue(TEXT("Border notch is stored in Outer"), FMath::IsNearlyEqual(FMath::Abs(Regions[0].SignedArea()), 9950.0, 1.e-6));
	TestTrue(TEXT("Small border notch output builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Small border notch stays blocked"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestTrue(TEXT("Large border cut recorded"), ApplySurfaceCut(MakeRegionRectangle(-60, -20, -35, 20)));
	TestTrue(TEXT("Large border opening output builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Narrow border opening remains blocked"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));

	Regions = UncutRegions;
	for (const FPolygon2d& RingCut : {MakeRegionRectangle(-20, -20, 20, -15), MakeRegionRectangle(-20, 15, 20, 20), MakeRegionRectangle(-20, -15, -15, 15), MakeRegionRectangle(15, -15, 20, 15)})
	{
		TestTrue(TEXT("Ring cut recorded without detaching its center"), ApplySurfaceCut(RingCut));
	}
	const FString AfterRing = RegionSnapshot(Regions);
	TestTrue(TEXT("Ring opening collision builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Thin ring has no convex passage above the threshold"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	Settings->ComplexHoleSizeThreshold = 1000.0;
	TestTrue(TEXT("Ring opening below threshold builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Ring threshold uses removed area excluding retained center"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	TestEqual(TEXT("Ring output filtering preserves supplied regions"), RegionSnapshot(Regions), AfterRing);

	Settings->ComplexHoleSizeThreshold = 450.0;
	Regions = UncutRegions;
	TestTrue(TEXT("Wall-splitting cut recorded"), ApplySurfaceCut(MakeRegionRectangle(-5, -60, 5, 60)));
	TestEqual(TEXT("Supplied wall has two remaining regions"), Regions.Num(), 2);
	TestTrue(TEXT("Split wall collision builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("Narrow wall-splitting opening remains blocked"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	Regions = UncutRegions;
	TestTrue(TEXT("Broad cut recorded"), ApplySurfaceCut(MakeRegionRectangle(-25, -25, 25, 25)));
	TestTrue(TEXT("Broad supplied opening builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestTrue(TEXT("A broad opening changes the stateless output"), CollisionFootprintArea(Shapes) > 7500 && CollisionFootprintArea(Shapes) < 8500);
	TestTrue(TEXT("Uncut input restores the stateless output"), Collision.BuildMesh(UncutRegions, 1.0, Shapes, Error));
	TestTrue(TEXT("Stateless builder retains no previous passage"), FMath::IsNearlyEqual(CollisionFootprintArea(Shapes), 10000.0, 1.e-6));
	const TArray<FGeneralPolygon2d> RemovedRegions;
	TestTrue(TEXT("Empty supplied regions represent complete removal"), Collision.BuildMesh(RemovedRegions, 1.0, Shapes, Error));
	TestEqual(TEXT("Complete removal clears previous collision output"), Shapes.ConvexElems.Num(), 0);
	return !HasAnyErrors();
}
#endif
