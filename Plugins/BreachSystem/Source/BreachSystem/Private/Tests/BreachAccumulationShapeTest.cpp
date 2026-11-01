#include "Surface/BreachAccumulationShape.h"
#include "Misc/AutomationTest.h"
#include <limits>

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachAccumulationShapeTest, "BreachSystem.Accumulation.Footprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachAccumulationShapeTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	FPolygon2d Polygon;
	FString Error;
	const TArray<FVector2d> Single = {{0, 0}};
	if (!TestTrue(TEXT("One strong impact has a padded footprint"), BreachAccumulationShape::GenerateBase(Single, 10, Polygon, Error)))
	{
		return false;
	}
	TestTrue(TEXT("Single impact has 20 cm width"), FMath::IsNearlyEqual(Polygon.Bounds().Width(), 20.0, 1.e-6));
	TestTrue(TEXT("Single impact has 20 cm height"), FMath::IsNearlyEqual(Polygon.Bounds().Height(), 20.0, 1.e-6));
	TestTrue(TEXT("Single footprint contains its impact"), Polygon.Contains(FVector2d::ZeroVector));
	const TArray<FVector2d> Line = {{-20,0}, {-12,0}, {-4,0}, {4,0}, {12,0}, {20,0}};
	TestTrue(TEXT("Collinear impacts make a non-degenerate footprint"), BreachAccumulationShape::GenerateBase(Line, 10, Polygon, Error));
	TestTrue(TEXT("Line footprint has 60 cm width"), FMath::IsNearlyEqual(Polygon.Bounds().Width(), 60.0, 1.e-6));
	TestTrue(TEXT("Line footprint has 20 cm height"), FMath::IsNearlyEqual(Polygon.Bounds().Height(), 20.0, 1.e-6));
	TestTrue(TEXT("Line footprint opens material between bullets"), Polygon.Contains(FVector2d(0, 8)));
	TestFalse(TEXT("Line footprint leaves material beyond padding"), Polygon.Contains(FVector2d(0, 12)));
	TestFalse(TEXT("Rounded endpoint does not become an AABB corner"), Polygon.Contains(FVector2d(28, 8)));
	const int32 LineVertices = Polygon.VertexCount();
	const TArray<FVector2d> NearlyCollinear = {{-20,0}, {-12,5.e-14}, {-4,-5.e-14}, {4,5.e-14}, {12,-5.e-14}, {20,0}};
	TestTrue(TEXT("Numerically collinear impacts create a footprint"), BreachAccumulationShape::GenerateBase(NearlyCollinear, 10, Polygon, Error));
	TestEqual(TEXT("Trace roundoff must not add almost-collinear hull corners"), Polygon.VertexCount(), LineVertices);
	const TArray<FVector2d> Compact = {{-10,-6}, {0,-6}, {10,-6}, {-10,6}, {0,6}, {10,6}};
	TestTrue(TEXT("Compact cloud creates a footprint"), BreachAccumulationShape::GenerateBase(Compact, 10, Polygon, Error));
	TestTrue(TEXT("Compact width follows impact spread"), FMath::IsNearlyEqual(Polygon.Bounds().Width(), 40.0, 1.e-6));
	TestTrue(TEXT("Compact height follows impact spread"), FMath::IsNearlyEqual(Polygon.Bounds().Height(), 32.0, 1.e-6));
	const TArray<FVector2d> LShape = {{-24,-24}, {-24,-8}, {-24,8}, {-24,24}, {-8,-24}, {8,-24}};
	TestTrue(TEXT("L-shaped cloud creates a convex footprint"), BreachAccumulationShape::GenerateBase(LShape, 10, Polygon, Error));
	TestTrue(TEXT("Convex policy fills the inside of an L"), Polygon.Contains(FVector2d(-6, -6)));
	TArray<FVector2d> Shifted;
	for (const FVector2d& Point : Compact)
	{
		Shifted.Add(Point + FVector2d(10000000, -10000000));
	}
	TestTrue(TEXT("Translated footprint remains valid"), BreachAccumulationShape::GenerateBase(Shifted, 10, Polygon, Error));
	TestTrue(TEXT("Translation preserves footprint width"), FMath::IsNearlyEqual(Polygon.Bounds().Width(), 40.0, 1.e-6));
	const TArray<FVector2d> Duplicate = {{5,7}, {5,7}, {5,7}};
	TestTrue(TEXT("Coincident points still have area"), BreachAccumulationShape::GenerateBase(Duplicate, 10, Polygon, Error));
	const TArray<FVector2d> Empty;
	const TArray<FVector2d> Unordered = {{17, 3}, {-11, 9}, {4, -13}, {17, 3}, {-7, -2}};
	FPolygon2d Canonical;
	TestTrue(TEXT("Unordered footprint"), BreachAccumulationShape::GenerateBase(Unordered, 10, Canonical, Error));
	for (int32 Offset = 1; Offset < Unordered.Num(); ++Offset)
	{
		TArray<FVector2d> Permuted;
		for (int32 Index = 0; Index < Unordered.Num(); ++Index)
		{
			Permuted.Add(Unordered[(Offset + Unordered.Num() - Index) % Unordered.Num()]);
		}
		TestTrue(TEXT("Permuted footprint"), BreachAccumulationShape::GenerateBase(Permuted, 10, Polygon, Error));
		TestTrue(TEXT("Same group has identical vertex order before contour noise"), Polygon.GetVertices() == Canonical.GetVertices());
	}
	TestFalse(TEXT("Empty cloud rejected"), BreachAccumulationShape::GenerateBase(Empty, 10, Polygon, Error));
	TestEqual(TEXT("Failure clears previous output"), Polygon.VertexCount(), 0);
	TestFalse(TEXT("Zero padding rejected"), BreachAccumulationShape::GenerateBase(Single, 0, Polygon, Error));
	const TArray<FVector2d> Invalid = {{std::numeric_limits<double>::infinity(), 0}};
	TestFalse(TEXT("Non-finite coordinate rejected"), BreachAccumulationShape::GenerateBase(Invalid, 10, Polygon, Error));
	return !HasAnyErrors();
}
#endif
