#include "Shape/BreachContour.h"
#include "Shape/BreachShape.h"
#include "Components/BreachSourceComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d Polygon(std::initializer_list<FVector2d> Points)
	{
		FPolygon2d Result;
		for (const FVector2d& Point : Points) { Result.AppendVertex(Point); }
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourValidationTest, "BreachSystem.Contour.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourValidationTest::RunTest(const FString& Parameters)
{
	FString Error;
	const FPolygon2d Concave = Polygon({{0,0}, {40,0}, {40,40}, {30,40}, {30,10}, {10,10}, {10,40}, {0,40}});
	TestTrue(TEXT("Deep concavity is valid"), BreachContour::Validate(Concave, Error));
	const TArray<FPolygon2d> Invalid = {
		Polygon({{0,0}, {10,10}, {0,10}, {10,0}}),
		Polygon({{0,0}, {10,0}, {10,10}, {5,0}, {0,10}}),
		Polygon({{0,0}, {10,0}, {10,0}, {0,10}}),
		Polygon({{0,0}, {10,0}, {5,0}, {5,10}, {0,10}}),
		Polygon({{0,0}, {10,0}, {20,0}}),
		Polygon({{0,0}, {1.e-8,0}, {10,10}, {0,10}}),
		Polygon({{0,0}, {std::numeric_limits<double>::infinity(),0}, {0,10}})
	};
	for (const FPolygon2d& Bad : Invalid)
	{
		TestFalse(TEXT("Invalid boundary is rejected"), BreachContour::Validate(Bad, Error));
		TestFalse(TEXT("Rejection explains the cause"), Error.IsEmpty());
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourGenerationTest, "BreachSystem.Contour.Generation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourGenerationTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Box = Polygon({{-60,-40}, {60,-40}, {60,40}, {-60,40}});
	FBreachContourOptions Options;
	Options.MicroDepth = 2;
	Options.Seed = 123;
	FString Error;
	FPolygon2d Result, Repeated;
	TestTrue(TEXT("Disabled modifier returns input"), BreachContour::Generate(Box, Options, Result, Error));
	TestTrue(TEXT("Disabled modifier preserves exact vertices"), Result.GetVertices() == Box.GetVertices());
	Options.bEnabled = true;
	Options.MacroDepth = 14;
	Options.MacroWidth = 45;
	Options.MediumDepth = 4;
	Options.MediumWidth = 12;
	Options.MicroDepth = 1;
	const bool bGenerated = BreachContour::Generate(Box, Options, Result, Error);
	TestTrue(TEXT("Complex box generates"), bGenerated);
	if (bGenerated)
	{
		TestTrue(TEXT("Generated contour is simple"), BreachContour::Validate(Result, Error));
		BreachContour::Generate(Box, Options, Repeated, Error);
		TestTrue(TEXT("Fixed inputs and seed reproduce vertices"), Result.GetVertices() == Repeated.GetVertices());
		TestTrue(TEXT("Contour inserts boundary detail"), Result.VertexCount() > Box.VertexCount());
		double MaxInward = 0, MaxOutward = 0;
		for (const FVector2d& Point : Result.GetVertices())
		{
			const double Distance = FMath::Sqrt(Box.DistanceSquared(Point));
			TestTrue(TEXT("Displacement remains within base shape envelope"), Distance <= 20.001);
			if (Box.Contains(Point)) { MaxInward = FMath::Max(MaxInward, Distance); }
			else { MaxOutward = FMath::Max(MaxOutward, Distance); }
		}
		TestTrue(TEXT("Macro produces deep notches beyond micro roughness"), MaxInward > 5);
		TestTrue(TEXT("Macro produces large protrusions beyond micro roughness"), MaxOutward > 5);
		for (const FVector2d& Corner : Box.GetVertices())
		{
			TestTrue(TEXT("Box corners are retained"), Result.GetVertices().Contains(Corner));
		}
		FString CSV = TEXT("shape,x,y\n");
		for (const FVector2d& P : Box.GetVertices()) { CSV += FString::Printf(TEXT("base,%.9f,%.9f\n"), P.X, P.Y); }
		for (const FVector2d& P : Result.GetVertices()) { CSV += FString::Printf(TEXT("contour,%.9f,%.9f\n"), P.X, P.Y); }
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Codex/ComplexContour");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FFileHelper::SaveStringToFile(CSV, *(Directory / TEXT("generated-box.csv")));
	}
	const FPolygon2d Concave = Polygon({{0,0}, {80,0}, {80,80}, {55,80}, {55,20}, {25,20}, {25,80}, {0,80}});
	for (int32 Seed = 1; Seed <= 32; ++Seed)
	{
		Options.Seed = Seed;
		const FPolygon2d Bases[] = { Box, FPolygon2d::MakeCircle(50, 64), Concave };
		for (const FPolygon2d& Base : Bases)
		{
			const bool bSuccess = BreachContour::Generate(Base, Options, Result, Error);
			TestTrue(FString::Printf(TEXT("Normal settings generate seed %d"), Seed), bSuccess);
			if (bSuccess)
			{
				TestTrue(TEXT("Every generated boundary is valid"), BreachContour::Validate(Result, Error));
				TestTrue(TEXT("Vertex work is bounded"), Result.VertexCount() <= 2048);
			}
		}
	}
	Options.Seed = 123;
	Options.SampleSpacing = 0;
	TestFalse(TEXT("Invalid sample spacing rejected"), BreachContour::Generate(Box, Options, Result, Error));
	TestEqual(TEXT("Failure clears output"), Result.VertexCount(), 0);
	Options.SampleSpacing = 0.1f;
	const FPolygon2d Huge = Polygon({{-100000, -100000}, {100000,-100000}, {100000,100000}, {-100000,100000}});
	TestFalse(TEXT("Excessive sampling is rejected before allocation"), BreachContour::Generate(Huge, Options, Result, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourBindingTest, "BreachSystem.Contour.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourBindingTest::RunTest(const FString& Parameters)
{
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>();
	for (bool bCircle : {true, false})
	{
		FBreachSourceOption Option;
		if (bCircle)
		{
			Option.ShapeClass = UBreachCircleShape::StaticClass();
			auto& Shape = Option.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
			Shape.Radius = 50;
		}
		else
		{
			Option.ShapeClass = UBreachBoxShape::StaticClass();
			auto& Shape = Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
			Shape.Size = FVector2D(120,80); Shape.CornerRadius = 5;
		}
		FPolygon2d Legacy;
		Option.ShapeClass->GetDefaultObject<UBreachShape>()->GeneratePolygon(Option, Legacy);
		Source->UpdateBreachSourceOption(Option);
		FGeneralPolygon2d Resolved, Repeated;
		TestTrue(TEXT("Disabled resolve succeeds"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved));
		TestTrue(TEXT("Disabled output matches legacy generator exactly"), Resolved.GetOuter().GetVertices() == Legacy.GetVertices());
		Option.Contour.bEnabled = true;
		Option.Contour.MicroDepth = 2;
		Option.Contour.Seed = 42;
		Source->UpdateBreachSourceOption(Option);
		TestTrue(TEXT("Enabled resolve succeeds"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved));
		TestTrue(TEXT("Contour changes legacy shape"), Resolved.GetOuter().GetVertices() != Legacy.GetVertices());
		Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated);
		TestTrue(TEXT("Fixed seed caches final contour"), Resolved.GetOuter().GetVertices() == Repeated.GetOuter().GetVertices());
		const FTransform Wall(FRotator(0,35,0), FVector(100,200,300), FVector(-2,1,3));
		TestTrue(TEXT("Contour resolves on mirrored scaled wall"), Source->ResolveCutterPolygon(Wall, Wall.TransformPosition(FVector(4,0,6)), Repeated));
		TestTrue(TEXT("Mirror corrects winding"), !Repeated.GetOuter().IsClockwise());
		Option.Contour.Seed = 0;
		Option.Contour.MicroDepth = 0;
		Source->UpdateBreachSourceOption(Option);
		Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved);
		Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated);
		TestTrue(TEXT("Seed zero randomizes contour even with micro depth zero"), Resolved.GetOuter().GetVertices() != Repeated.GetOuter().GetVertices());
		Option.Contour.bEnabled = false;
		Source->UpdateBreachSourceOption(Option);
		Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved);
		Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated);
		TestTrue(TEXT("Disabling restores smooth cached result"), Resolved.GetOuter().GetVertices() == Repeated.GetOuter().GetVertices());
	}
	return !HasAnyErrors();
}
#endif
