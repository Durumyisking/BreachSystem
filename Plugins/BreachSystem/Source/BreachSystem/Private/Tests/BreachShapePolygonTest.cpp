#include "BreachSourceOption.h"
#include "Curve/GeneralPolygon2.h"
#include "Misc/AutomationTest.h"

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachShapePolygonTest, "BreachSystem.Shape.PolygonOptions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachShapePolygonTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	FBreachSourceOption Option;
	FBreachCircleShapeOption& Circle = Option.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
	Circle.Radius = 10;
	Circle.Segments = 32;
	FPolygon2d Smooth, Rough, Repeated;
	// 컴포넌트와 같이 기반 타입으로 호출해 파생 구현으로 가는지 함께 확인한다.
	TestTrue(TEXT("Circle generates"), Option.ShapeOption.Get().GeneratePolygon(Smooth));
	for (const FVector2d& Point : Smooth.GetVertices())
	{
		TestTrue(TEXT("Zero roughness keeps radius"), FMath::IsNearlyEqual(Point.Length(), 10.0, 0.0001));
	}
	Option.Contour.bEnabled = true;
	Option.Contour.MicroDepthRatio = 0.1f;
	TestTrue(TEXT("Base circle stays smooth"), Option.ShapeOption.Get().GeneratePolygon(Rough));
	TestTrue(TEXT("Contour options do not modify base shape"), Smooth.GetVertices() == Rough.GetVertices());
	Circle.Radius = 0;
	TestFalse(TEXT("Zero radius rejected"), Option.ShapeOption.Get().GeneratePolygon(Rough));

	FBreachBoxShapeOption& Box = Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	Box.Size = FVector2D(40, 20);
	TestTrue(TEXT("Rectangle generates"), Option.ShapeOption.Get().GeneratePolygon(Smooth));
	TestTrue(TEXT("Rectangle area is width times height"), FMath::IsNearlyEqual(FMath::Abs(Smooth.Area()), 800.0, 0.001));
	Box.CornerRadius = 5;
	Box.CornerSegments = 16;
	TestTrue(TEXT("Rounded rectangle generates"), Option.ShapeOption.Get().GeneratePolygon(Rough));
	TestTrue(TEXT("Round corners remove corner area"), FMath::IsNearlyEqual(FMath::Abs(Rough.Area()), 778.54, 0.2));
	TestTrue(TEXT("Round corners preserve overall size"), FMath::IsNearlyEqual(Rough.Bounds().Width(), 40.0) && FMath::IsNearlyEqual(Rough.Bounds().Height(), 20.0));
	Box.CornerRadius = 100;
	TestTrue(TEXT("Oversized corner radius is limited to half short side"), Option.ShapeOption.Get().GeneratePolygon(Rough));
	TestTrue(TEXT("Fully rounded box retains positive area"), Rough.Area() > 0);
	Box.CornerRadius = 0;
	Option.Contour.Seed = 42;
	TestTrue(TEXT("Rough rectangle generates"), Option.ShapeOption.Get().GeneratePolygon(Rough));
	TestEqual(TEXT("Sharp box has only four corner vertices"), Rough.VertexCount(), 4);
	Option.ShapeOption.Get().GeneratePolygon(Repeated);
	TestTrue(TEXT("Base box is deterministic"), Rough.GetVertices() == Repeated.GetVertices());
	Box.Size.X = -1;
	TestFalse(TEXT("Negative size rejected"), Option.ShapeOption.Get().GeneratePolygon(Rough));

	Option.ShapeOption.InitializeAs<FBreachShapeOption>();
	TestFalse(TEXT("Base option without a shape kind rejected"), Option.ShapeOption.Get().GeneratePolygon(Rough));
	return !HasAnyErrors();
}

#endif

