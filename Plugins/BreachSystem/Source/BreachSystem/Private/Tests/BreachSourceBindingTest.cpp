#include "Components/BreachSourceComponent.h"
#include "Shape/BreachShape.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSourceBindingTest, "BreachSystem.Source.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachSourceBindingTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>();
	FGeneralPolygon2d Polygon;
	const FTransform Wall(FRotator(15, 45, 5), FVector(100, 200, 300), FVector(2, 0.1, 4));
	const FVector Hit = Wall.TransformPosition(FVector(10, -50, 20));
	Source->BindBox(FVector2D(40, 80));
	TestTrue(TEXT("Resolve rotated and scaled wall"), Source->ResolveCutterPolygon(Wall, Hit, Polygon));
	const FAxisAlignedBox2d Bounds = Polygon.GetOuter().Bounds();
	TestTrue(TEXT("World dimensions converted to local XZ"),
		FMath::IsNearlyEqual(Bounds.Min.X, 0.0, 0.001) && FMath::IsNearlyEqual(Bounds.Max.X, 20.0, 0.001)
		&& FMath::IsNearlyEqual(Bounds.Min.Y, 10.0, 0.001) && FMath::IsNearlyEqual(Bounds.Max.Y, 30.0, 0.001));
	TestFalse(TEXT("Boundary is counterclockwise"), Polygon.GetOuter().IsClockwise());
	FGeneralPolygon2d Repeated;
	Source->ResolveCutterPolygon(Wall, Hit, Repeated);
	TestTrue(TEXT("Resolving does not mutate cache"), Polygon.GetOuter().GetVertices() == Repeated.GetOuter().GetVertices());
	FTransform Mirrored = Wall;
	Mirrored.SetScale3D(FVector(-2, 0.1, 4));
	TestTrue(TEXT("Mirrored scale is supported"), Source->ResolveCutterPolygon(Mirrored, Hit, Repeated));
	TestFalse(TEXT("Mirrored scale preserves winding"), Repeated.GetOuter().IsClockwise());
	Source->BindCircle(10, 32, 2, 7);
	TestTrue(TEXT("Rough circle binding"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Polygon));
	Source->BindCircle(0);
	TestFalse(TEXT("Invalid rebind discards previous cache"), Source->ResolveCutterPolygon(Wall, Hit, Polygon));
	Source->BindBox(FVector2D(40, 20), 5, 16, 0, 0);
	TestTrue(TEXT("Rounded rectangle binding"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Polygon));
	TestTrue(TEXT("Rounded binding keeps shape options"), FMath::IsNearlyEqual(Polygon.SignedArea(), 778.54, 0.2));
	FTransform ZeroScale = Wall;
	ZeroScale.SetScale3D(FVector(0, 1, 1));
	TestFalse(TEXT("Degenerate transform rejected"), Source->ResolveCutterPolygon(ZeroScale, Hit, Polygon));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSourceSeedPolicyTest, "BreachSystem.Source.SeedPolicy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachSourceSeedPolicyTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>();
	auto CheckRepeated = [this, Source](const FString& Label, bool bExpectSame)
	{
		FGeneralPolygon2d First, Repeated;
		TestTrue(Label + TEXT(" first resolve"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, First));
		bool bAllSame = true;
		// A random seed can repeat once without indicating a cached shape.
		for (int32 Attempt = 0; Attempt < 4; ++Attempt)
		{
			TestTrue(Label + TEXT(" repeated resolve"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated));
			if (First.GetOuter().GetVertices() != Repeated.GetOuter().GetVertices())
			{
				bAllSame = false;
			}
		}
		TestEqual(Label + TEXT(" repeated boundary"), bAllSame, bExpectSame);
	};
	for (bool bCircle : { true, false })
	{
		const FString Name = bCircle ? TEXT("Circle") : TEXT("Box");
		auto Bind = [Source, bCircle](float MicroDepth, int32 Seed)
		{
			if (bCircle)
			{
				Source->BindCircle(10, 32, MicroDepth, Seed);
			}
			else
			{
				Source->BindBox(FVector2D(40, 20), 0, 8, MicroDepth, Seed);
			}
		};
		Bind(2, 123);
		CheckRepeated(Name + TEXT(" fixed seed"), true);
		Bind(2, 0);
		CheckRepeated(Name + TEXT(" zero seed with roughness"), false);
		Bind(0, 0);
		CheckRepeated(Name + TEXT(" zero seed without roughness"), true);
		Bind(2, -7);
		CheckRepeated(Name + TEXT(" negative fixed seed"), true);

		// Direct options cover Blueprint defaults, which bypass BindCircle/BindBox.
		FBreachSourceOption Option;
		if (bCircle)
		{
			Option.ShapeClass = UBreachCircleShape::StaticClass();
			Option.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
		}
		else
		{
			Option.ShapeClass = UBreachBoxShape::StaticClass();
			Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
		}
		Option.Contour.bEnabled = true;
		Option.Contour.MacroDepth = 0;
		Option.Contour.MediumDepth = 0;
		Option.Contour.MicroDepth = 2;
		Option.Contour.Seed = 77;
		Source->UpdateBreachSourceOption(Option);
		CheckRepeated(Name + TEXT(" direct fixed contour seed"), true);
		Option.Contour.Seed = 0;
		Source->UpdateBreachSourceOption(Option);
		CheckRepeated(Name + TEXT(" direct zero-seed options"), false);
	}
	return !HasAnyErrors();
}

#endif
