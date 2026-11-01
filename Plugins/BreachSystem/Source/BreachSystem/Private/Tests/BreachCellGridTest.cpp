#include "Surface/BreachCellGrid.h"
#include "Misc/AutomationTest.h"
#include <limits>

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachCellGridTest, "BreachSystem.Cells.Mapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachCellGridTest::RunTest(const FString& Parameters)
{
	FBreachCellGrid Grid;
	FString Error;
	FIntPoint Cell;
	FVector2d Center;
	TestFalse(TEXT("Unconfigured grid rejects hits"), Grid.Quantize({0,0}, Cell, Center, Error));
	if (!TestTrue(TEXT("Linear UV grid configures"), Grid.Configure({0.5,0.5}, {0.01,0}, {0,0.02}, {2,3}, 10, Error)))
	{
		return false;
	}
	TestTrue(TEXT("UV origin anchors grid"), Grid.ToMetric({-50,-25}).Equals({0,0}, 1.e-9));
	TestTrue(TEXT("Non-square UV density keeps physical units"), Grid.ToMetric({-45,-15}).Equals({10,30}, 1.e-9));
	TestTrue(TEXT("Negative coordinates supported"), Grid.Quantize({-50.1,-25.1}, Cell, Center, Error));
	TestEqual(TEXT("Negative cells use floor"), Cell, FIntPoint(-1,-1));
	TestTrue(TEXT("Representative is cell center"), Center.Equals({-5,-5}, 1.e-9));
	TestTrue(TEXT("Exact boundary supported"), Grid.Quantize({-45,-15}, Cell, Center, Error));
	TestEqual(TEXT("Boundary enters next cell"), Cell, FIntPoint(1,3));
	const FVector2d Point(17,-9);
	TestTrue(TEXT("Coordinate round trip"), Grid.ToLocal(Grid.ToMetric(Point)).Equals(Point, 1.e-9));
	// Rotated, tiled and sheared UVs still produce perpendicular 10 cm axes.
	for (const FVector2d PerZ : {FVector2d(-0.02,0), FVector2d(-0.02,0.007)})
	{
		TestTrue(TEXT("Rotated/sheared UV configures"), Grid.Configure({-2,3}, {0,0.01}, PerZ, {-2,3}, 10, Error));
		const FVector2d Base = Grid.ToLocal({0,0});
		const FVector2d U = (Grid.ToLocal({10,0}) - Base) * FVector2d(2,3);
		const FVector2d V = (Grid.ToLocal({0,10}) - Base) * FVector2d(2,3);
		TestTrue(TEXT("Physical U size"), FMath::IsNearlyEqual(U.Length(), 10.0, 1.e-8));
		TestTrue(TEXT("Physical V size"), FMath::IsNearlyEqual(V.Length(), 10.0, 1.e-8));
		TestTrue(TEXT("Physical axes orthogonal"), FMath::Abs(U.Dot(V)) < 1.e-8);
		TestTrue(TEXT("Rotated round trip"), Grid.ToLocal(Grid.ToMetric(Point)).Equals(Point, 1.e-8));
	}
	TestFalse(TEXT("Non-finite hit rejected"), Grid.Quantize({std::numeric_limits<double>::infinity(),0}, Cell, Center, Error));
	TestFalse(TEXT("Cell index overflow rejected"), Grid.Quantize({1.e20,0}, Cell, Center, Error));
	TestFalse(TEXT("Singular UV rejected"), Grid.Configure({0,0}, {1,0}, {2,0}, {1,1}, 10, Error));
	TestFalse(TEXT("Failed config invalidates old mapping"), Grid.IsValid());
	TestFalse(TEXT("Zero scale rejected"), Grid.Configure({0,0}, {1,0}, {0,1}, {0,1}, 10, Error));
	TestFalse(TEXT("Invalid cell size rejected"), Grid.Configure({0,0}, {1,0}, {0,1}, {1,1}, 0, Error));
	return !HasAnyErrors();
}
#endif
