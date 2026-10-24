#include "Shape/BreachContour.h"
#include "BreachPluginUtil.h"
#include "Shape/BreachShape.h"
#include "Components/BreachSourceComponent.h"
#include "Misc/AutomationTest.h"
#include "SegmentTypes.h"
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
		for (const FVector2d& Point : Points)
		{
			Result.AppendVertex(Point);
		}
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
		Polygon({{0,0}, {10,0}, {10,0}, {0,10}}),
		Polygon({{0,0}, {10,0}, {20,0}}),
		Polygon({{0,0}, {1.e-8,0}, {10,10}, {0,10}}),
		Polygon({{0,0}, {std::numeric_limits<double>::infinity(),0}, {0,10}})
	};
	for (const FPolygon2d& Bad : Invalid)
	{
		TestFalse(TEXT("Invalid boundary is rejected"), BreachContour::Validate(Bad, Error));
		TestFalse(TEXT("Rejection explains the cause"), Error.IsEmpty());
	}
	TestTrue(TEXT("Basic validation does not test non-adjacent crossings"),
		BreachContour::Validate(Polygon({{0,0}, {10,10}, {0,10}, {8,0}}), Error));
	TestTrue(TEXT("Basic validation does not test boundary contact"),
		BreachContour::Validate(Polygon({{0,0}, {10,0}, {10,10}, {5,0}, {0,10}}), Error));
	FBreachContourOptions Options;
	TestTrue(TEXT("Default source settings are valid"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexCount = 129;
	TestFalse(TEXT("Vertex limit rejects 129"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexCount = 128;
	TestTrue(TEXT("Vertex limit accepts 128"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexCount = 16;
	Options.MacroDepthRatio = 0.46f;
	TestFalse(TEXT("Excessive macro ratio is rejected"), BreachContour::ValidateOptions(Options, Error));
	Options.MacroDepthRatio = 0.25f;
	Options.VertexCount = 3;
	TestFalse(TEXT("Fewer than four contour vertices are rejected"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexCount = 16;
	Options.VertexPositionJitter = 1.2f;
	TestTrue(TEXT("Jitter maximum can exceed one neighbor interval"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexPositionJitter = -0.1f;
	TestFalse(TEXT("Negative maximum jitter is rejected"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexPositionJitter = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Non-finite maximum jitter is rejected"), BreachContour::ValidateOptions(Options, Error));
	Options.VertexPositionJitter = 0.6f;
	Options.Pattern = static_cast<EBreachContourPattern>(255);
	TestFalse(TEXT("Unknown contour pattern is rejected"), BreachContour::ValidateOptions(Options, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourNormalJitterTest, "BreachSystem.Contour.NormalJitter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourNormalJitterTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(123), Repeated(123);
	double Sum = 0, SumSquares = 0;
	int32 CentralCount = 0;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		const float Value = BreachPluginUtil::SampleBoundedNormal(Random);
		TestTrue(TEXT("Normal sample is finite and bounded"), FMath::IsFinite(Value) && FMath::Abs(Value) <= 1);
		TestEqual(TEXT("Fixed seed repeats the normal sample"), Value, BreachPluginUtil::SampleBoundedNormal(Repeated));
		Sum += Value;
		SumSquares += Value * Value;
		CentralCount += FMath::Abs(Value) <= 1.f / 3.f;
	}
	TestTrue(TEXT("Normal samples stay centered"), FMath::Abs(Sum / 4096) < 0.025);
	TestTrue(TEXT("Normal variance differs from uniform jitter"), SumSquares / 4096 > 0.09 && SumSquares / 4096 < 0.13);
	TestTrue(TEXT("About two thirds stay within one sigma"), CentralCount > 2600 && CentralCount < 3000);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourJitterFrequencyTest, "BreachSystem.Contour.JitterFrequency",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourJitterFrequencyTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Base = FPolygon2d::MakeCircle(50, 16);
	const float Ratios[] = {0, 0.6f, 1.2f, 2.1f, 0.6f};
	const int32 CachedIntervals[] = {1, 1, 2, 3, 1};
	const uint32 ExpectedMasks[] = {0, 0xffff, 0xaaaa, 0x4924, 0};
	for (int32 Fixture = 0; Fixture < UE_ARRAY_COUNT(Ratios); ++Fixture)
	{
		FBreachContourOptions Options;
		Options.bEnabled = true;
		Options.Seed = 123;
		Options.VertexPositionJitter = Ratios[Fixture];
		Options.JitterInterval = CachedIntervals[Fixture];
		Options.bRandomizeVertexSpacing = Fixture != 4;
		// Leave clearance for both motions so this fixture isolates the spacing cadence.
		Options.MaxDisplacementFraction = 0.01f;
		BreachContour::FPreparedContour Prepared;
		FString Error;
		const bool bPrepared = BreachContour::Prepare(Base, Options, Prepared, Error);
		TestTrue(FString::Printf(TEXT("Jitter ratio %g prepares: %s"), Ratios[Fixture], *Error), bPrepared);
		if (!bPrepared)
		{
			continue;
		}
		FPolygon2d Result, Repeated;
		BreachContour::Generate(Prepared, Result);
		BreachContour::Generate(Prepared, Repeated);
		TestTrue(TEXT("Cadence resets and repeats for each fixed-seed cutter"), Result.GetVertices() == Repeated.GetVertices());
		uint32 MovedMask = 0;
		bool bDepthMoved = false;
		for (int32 Index = 0; Index < Result.VertexCount(); ++Index)
		{
			const auto& Sample = Prepared.Samples[Index];
			const FVector2d Offset = Result[Index] - Sample.Point;
			const FVector2d Tangent(-Sample.Normal.Y, Sample.Normal.X);
			if (FMath::Abs(Offset.Dot(Tangent)) > 1.e-8)
			{
				MovedMask |= 1u << Index;
			}
			bDepthMoved |= FMath::Abs(Offset.Dot(Sample.Normal)) > 1.e-8;
		}
		TestEqual(TEXT("Only every nth vertex receives lateral jitter"), MovedMask, ExpectedMasks[Fixture]);
		TestTrue(TEXT("Spacing cadence preserves depth deformation"), bDepthMoved);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourGenerationTest, "BreachSystem.Contour.Generation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourGenerationTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Box = Polygon({{-60,-40}, {60,-40}, {60,40}, {-60,40}});
	FBreachContourOptions Options;
	Options.Pattern = EBreachContourPattern::TriangleSections;
	Options.VertexCount = 64;
	Options.bMoveCorners = false;
	Options.MicroDepthRatio = 0.025f;
	Options.Seed = 123;
	FString Error;
	FPolygon2d Result, Repeated;
	TestTrue(TEXT("Disabled modifier returns input"), BreachContour::Generate(Box, Options, Result, Error));
	TestTrue(TEXT("Disabled modifier preserves exact vertices"), Result.GetVertices() == Box.GetVertices());
	Options.bEnabled = true;
	Options.MacroDepthRatio = 0.175f;
	Options.MediumDepthRatio = 0.05f;
	Options.MicroDepthRatio = 0.0125f;
	const bool bGenerated = BreachContour::Generate(Box, Options, Result, Error);
	TestTrue(TEXT("Complex box generates"), bGenerated);
	if (bGenerated)
	{
		TestTrue(TEXT("Generated contour passes basic validation"), BreachContour::Validate(Result, Error));
		BreachContour::Generate(Box, Options, Repeated, Error);
		TestTrue(TEXT("Fixed inputs and seed reproduce vertices"), Result.GetVertices() == Repeated.GetVertices());
		TestTrue(TEXT("Contour inserts boundary detail"), Result.VertexCount() > Box.VertexCount());
		double MaxInward = 0, MaxOutward = 0;
		for (const FVector2d& Point : Result.GetVertices())
		{
			const double Distance = FMath::Sqrt(Box.DistanceSquared(Point));
			TestTrue(TEXT("Displacement remains within base shape envelope"), Distance <= 20.001);
			if (Box.Contains(Point))
			{
				MaxInward = FMath::Max(MaxInward, Distance);
			}
			else
			{
				MaxOutward = FMath::Max(MaxOutward, Distance);
			}
		}
		TestTrue(TEXT("Macro produces deep notches beyond micro roughness"), MaxInward > 5);
		TestTrue(TEXT("Macro produces large protrusions beyond micro roughness"), MaxOutward > 5);
		for (const FVector2d& Corner : Box.GetVertices())
		{
			TestTrue(TEXT("Box corners are retained"), Result.GetVertices().Contains(Corner));
		}
		FString CSV = TEXT("shape,x,y\n");
		for (const FVector2d& P : Box.GetVertices())
		{
			CSV += FString::Printf(TEXT("base,%.9f,%.9f\n"), P.X, P.Y);
		}
		for (const FVector2d& P : Result.GetVertices())
		{
			CSV += FString::Printf(TEXT("contour,%.9f,%.9f\n"), P.X, P.Y);
		}
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Codex/ComplexContour");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FFileHelper::SaveStringToFile(CSV, *(Directory / TEXT("generated-box.csv")));
	}
	// Triangular macro damage retains intact samples between breaks.
	Options.MacroDepthRatio = 0.2f;
	Options.MediumDepthRatio = 0;
	Options.MicroDepthRatio = 0;
	Options.Seed = 123;
	const bool bMacroOnly = BreachContour::Generate(Box, Options, Result, Error);
	TestTrue(TEXT("Macro-only contour generates"), bMacroOnly);
	if (bMacroOnly)
	{
		int32 FlatRun = 0, LongestFlatRun = 0;
		double LargestBreak = 0;
		for (const FVector2d& Point : Result.GetVertices())
		{
			const double Distance = FMath::Sqrt(Box.DistanceSquared(Point));
			LargestBreak = FMath::Max(LargestBreak, Distance);
			FlatRun = Distance < 1.e-6 ? FlatRun + 1 : 0;
			LongestFlatRun = FMath::Max(LongestFlatRun, FlatRun);
		}
		TestTrue(TEXT("Macro profile preserves intact boundary samples"), LongestFlatRun > 0);
		TestTrue(TEXT("Macro profile also contains pronounced damage"), LargestBreak > 6);
		TestTrue(TEXT("Macro-only contour passes basic validation"), BreachContour::Validate(Result, Error));
	}
	const FPolygon2d MacroOnly = Result;
	Options.MediumDepthRatio = 0.05f;
	Options.MicroDepthRatio = 0.0125f;
	FPolygon2d Detailed;
	TestTrue(TEXT("Detailed macro contour generates"), BreachContour::Generate(Box, Options, Detailed, Error));
	TestEqual(TEXT("Fine detail preserves the contour sampling"), Detailed.VertexCount(), MacroOnly.VertexCount());
	if (bMacroOnly && MacroOnly.VertexCount() == Detailed.VertexCount())
	{
		for (int32 Index = 0; Index < MacroOnly.VertexCount(); ++Index)
		{
			if (Box.DistanceSquared(MacroOnly[Index]) < 1.e-12)
			{
				TestTrue(TEXT("Intact macro sections suppress medium and micro damage"),
					MacroOnly[Index].Equals(Detailed[Index], 1.e-6));
			}
		}
	}
	Options.MacroDepthRatio = 0.175f;
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
				TestTrue(TEXT("Vertex work is bounded"), Result.VertexCount() <= 128);
				const auto Bounds = Base.Bounds();
				const double Envelope = FMath::Min(Bounds.Width(), Bounds.Height()) * Options.MaxDisplacementFraction;
				for (const FVector2d& Point : Result.GetVertices())
				{
					TestTrue(TEXT("Every shape stays within its displacement envelope"), Base.DistanceSquared(Point) <= FMath::Square(Envelope + 0.001));
				}
				if (Base.VertexCount() == Concave.VertexCount())
				{
					TestFalse(TEXT("Deep U opening remains outside the cutter"), Result.Contains(FVector2d(40,65)));
					TestTrue(TEXT("U connecting base remains inside"), Result.Contains(FVector2d(40,10)));
					for (const FVector2d& Corner : Concave.GetVertices())
					{
						TestTrue(TEXT("Concave corners retain their positions"), Result.GetVertices().Contains(Corner));
					}
				}
			}
		}
	}
	Options.Seed = 123;
	Options.VertexCount = 3;
	TestFalse(TEXT("Too few contour vertices are rejected"), BreachContour::Generate(Box, Options, Result, Error));
	TestEqual(TEXT("Failure clears output"), Result.VertexCount(), 0);
	Options.VertexCount = 129;
	TestFalse(TEXT("Excessive vertex count is rejected before allocation"), BreachContour::Generate(Box, Options, Result, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourPatternTest, "BreachSystem.Contour.Patterns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourPatternTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Small = Polygon({{-10,-10}, {10,-10}, {10,10}, {-10,10}});
	const FPolygon2d Large = Polygon({{-40,-40}, {40,-40}, {40,40}, {-40,40}});
	FBreachContourOptions Options;
	Options.bEnabled = true;
	Options.VertexCount = 16;
	Options.Seed = 123;
	Options.MacroDepthRatio = 0.15f;
	Options.MediumDepthRatio = 0.05f;
	Options.MicroDepthRatio = 0.015f;
	FString Error;
	FPolygon2d SmallCut, LargeCut, Repeated;

	for (const EBreachContourPattern Pattern : { EBreachContourPattern::TriangleSections,
		EBreachContourPattern::IndependentContinuous })
	{
		Options.Pattern = Pattern;
		const bool bSmall = BreachContour::Generate(Small, Options, SmallCut, Error);
		TestTrue(TEXT("Each pattern generates a valid small cutter"), bSmall);
		if (bSmall)
		{
			TestEqual(TEXT("Small cutter has fixed vertex count"), SmallCut.VertexCount(), 16);
			TestTrue(TEXT("Small cutter passes basic validation"), BreachContour::Validate(SmallCut, Error));
		}
		const bool bLarge = BreachContour::Generate(Large, Options, LargeCut, Error);
		TestTrue(TEXT("Each pattern generates a valid large cutter"), bLarge);
		if (bLarge)
		{
			TestEqual(TEXT("Scaling the base does not change vertex count"), LargeCut.VertexCount(), 16);
			TestTrue(TEXT("Large cutter passes basic validation"), BreachContour::Validate(LargeCut, Error));
			TestTrue(TEXT("Repeat uses identical fixed-seed vertices"), BreachContour::Generate(Large, Options, Repeated, Error));
			TestTrue(TEXT("Fixed seed reproduces the contour"), LargeCut.GetVertices() == Repeated.GetVertices());
		}
	}

	Options.Pattern = EBreachContourPattern::IndependentContinuous;
	Options.bMoveCorners = false;
	TestTrue(TEXT("Locked-corner cutter generates"), BreachContour::Generate(Small, Options, SmallCut, Error));
	for (const FVector2d& Corner : Small.GetVertices())
	{
		TestTrue(TEXT("Locked original corners are preserved"), SmallCut.GetVertices().Contains(Corner));
	}
	Options.bMoveCorners = true;
	TestTrue(TEXT("Movable-corner cutter generates"), BreachContour::Generate(Small, Options, LargeCut, Error));
	bool bMovedCorner = false;
	for (const FVector2d& Corner : Small.GetVertices())
	{
		bMovedCorner |= !LargeCut.GetVertices().Contains(Corner);
	}
	TestTrue(TEXT("At least one box corner is displaced"), bMovedCorner);

	Options.bRandomizeVertexSpacing = false;
	TestTrue(TEXT("Uniformly sampled cutter generates"), BreachContour::Generate(Small, Options, SmallCut, Error));
	Options.bRandomizeVertexSpacing = true;
	TestTrue(TEXT("Randomly sampled cutter generates"), BreachContour::Generate(Small, Options, LargeCut, Error));
	TestTrue(TEXT("Sampling mode changes vertex placement"), SmallCut.GetVertices() != LargeCut.GetVertices());
	TestEqual(TEXT("Sampling mode does not change vertex count"), LargeCut.VertexCount(), 16);

	Options.MacroDepthRatio = 0;
	Options.MediumDepthRatio = 0;
	Options.MicroDepthRatio = 0.035f;
	TestTrue(TEXT("Independent micro-only contour generates"), BreachContour::Generate(Small, Options, SmallCut, Error));
	TestTrue(TEXT("Micro-only detail remains active without macro or medium"), SmallCut.GetVertices() != Small.GetVertices());
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
			Shape.Size = FVector2D(120,80);
			Shape.CornerRadius = 5;
		}
		FPolygon2d Legacy;
		Option.ShapeClass->GetDefaultObject<UBreachShape>()->GeneratePolygon(Option, Legacy);
		Source->UpdateBreachSourceOption(Option);
		FGeneralPolygon2d Resolved, Repeated;
		TestTrue(TEXT("Disabled resolve succeeds"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved));
		TestTrue(TEXT("Disabled output matches legacy generator exactly"), Resolved.GetOuter().GetVertices() == Legacy.GetVertices());
		Option.Contour.bEnabled = true;
		Option.Contour.MicroDepthRatio = 0.02f;
		Option.Contour.Seed = 42;
		Source->UpdateBreachSourceOption(Option);
		TestTrue(TEXT("Enabled resolve succeeds"), Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved));
		TestTrue(TEXT("Contour changes legacy shape"), Resolved.GetOuter().GetVertices() != Legacy.GetVertices());
		Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated);
		TestTrue(TEXT("Fixed seed caches final contour"), Resolved.GetOuter().GetVertices() == Repeated.GetOuter().GetVertices());
		FBreachSourceOption InvalidOption = Option;
		InvalidOption.Contour.VertexCount = 129;
		Source->UpdateBreachSourceOption(InvalidOption);
		TestTrue(TEXT("Invalid contour settings still resolve a base cutter"),
			Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated));
		TestTrue(TEXT("Invalid contour settings fall back to the base shape"),
			Legacy.GetVertices() == Repeated.GetOuter().GetVertices());
		if (bCircle)
		{
			FBreachSourceOption OversizedBase = Option;
			FBreachCircleShapeOption& Circle = OversizedBase.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
			Circle.Radius = 50;
			Circle.Segments = 256;
			Source->UpdateBreachSourceOption(OversizedBase);
			TestTrue(TEXT("Oversized contour base still resolves"),
				Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Repeated));
			TestTrue(TEXT("Oversized contour falls back to all base vertices"),
				Repeated.GetOuter().VertexCount() == 256);
		}
		const FTransform Wall(FRotator(0,35,0), FVector(100,200,300), FVector(-2,1,3));
		TestTrue(TEXT("Contour resolves on mirrored scaled wall"), Source->ResolveCutterPolygon(Wall, Wall.TransformPosition(FVector(4,0,6)), Repeated));
		TestTrue(TEXT("Mirror corrects winding"), !Repeated.GetOuter().IsClockwise());
		Option.Contour.Seed = 0;
		Option.Contour.MicroDepthRatio = 0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourCircleCornerTest, "BreachSystem.Contour.CircleCornerLock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourCircleCornerTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>();
	for (const EBreachContourPattern Pattern : { EBreachContourPattern::TriangleSections,
		EBreachContourPattern::IndependentContinuous })
	{
		for (const int32 Count : { 16, 32, 64, 128 })
		{
			FBreachSourceOption Option;
			Option.ShapeClass = UBreachCircleShape::StaticClass();
			auto& Shape = Option.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
			Shape.Radius = 50;
			Shape.Segments = Count;
			Option.Contour.bEnabled = true;
			Option.Contour.Pattern = Pattern;
			Option.Contour.VertexCount = Count;
			Option.Contour.bMoveCorners = false;
			Option.Contour.Seed = 123;
			Source->UpdateBreachSourceOption(Option);
			FGeneralPolygon2d Resolved;
			const bool bResolved = Source->ResolveCutterPolygon(FTransform::Identity, FVector::ZeroVector, Resolved);
			TestTrue(TEXT("Circle resolves when corner locking is requested"), bResolved);
			if (bResolved)
			{
				FPolygon2d Base;
				Option.ShapeClass->GetDefaultObject<UBreachShape>()->GeneratePolygon(Option, Base);
				TestTrue(FString::Printf(TEXT("Circle still deforms with corner lock: pattern %d, vertices %d"), int32(Pattern), Count),
					Resolved.GetOuter().GetVertices() != Base.GetVertices());
			}
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourDepthClearanceTest, "BreachSystem.Contour.DepthClearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourDepthClearanceTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Box = Polygon({{-200,-100}, {200,-100}, {200,100}, {-200,100}});
	for (const float Jitter : {0.f, 1.2f})
	{
		FBreachContourOptions Options;
		Options.bEnabled = true;
		Options.VertexCount = 32;
		Options.VertexPositionJitter = Jitter;
		Options.MaxDisplacementFraction = 0.1f;
		BreachContour::FPreparedContour Prepared;
		FString Error;
		const bool bPrepared = BreachContour::Prepare(Box, Options, Prepared, Error);
		TestTrue(FString::Printf(TEXT("Depth clearance fixture prepares: %s"), *Error), bPrepared);
		if (!bPrepared)
		{
			continue;
		}
		int32 Checked = 0;
		for (const auto& Sample : Prepared.Samples)
		{
			if (FMath::Abs(Sample.Point.X) < 30 && FMath::IsNearlyEqual(Sample.Point.Y, -100.0))
			{
				++Checked;
				TestTrue(TEXT("Tangential limits preserve 20 units of safe normal depth"),
					FMath::IsNearlyEqual(Sample.MaxDisplacement, 20.0, 1.e-5));
				if (Jitter > 0)
				{
					TestTrue(TEXT("Safe lateral variation remains available"), Sample.DistanceAfter > 0);
				}
			}
		}
		TestTrue(TEXT("Straight-edge interior samples were checked"), Checked > 0);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourPreparedBoundsTest, "BreachSystem.Contour.PreparedBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourPreparedBoundsTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Bases[] = {
		FPolygon2d::MakeCircle(50, 128),
		FPolygon2d::MakeCircle(50, 128),
		Polygon({{-60,-40}, {60,-40}, {60,40}, {-60,40}}),
		Polygon({{0,0}, {100,0}, {100,100}, {66,100}, {66,1}, {34,1}, {34,100}, {0,100}})
	};
	const int32 Counts[] = {128, 16, 16, 64};
	for (int32 Fixture = 0; Fixture < UE_ARRAY_COUNT(Bases); ++Fixture)
	{
		FBreachContourOptions Options;
		Options.bEnabled = true;
		Options.VertexCount = Counts[Fixture];
		Options.VertexPositionJitter = 1.9f;
		Options.MacroDepthRatio = Options.MediumDepthRatio = Options.MicroDepthRatio = 0.45f;
		Options.MaxDisplacementFraction = 0.45f;
		BreachContour::FPreparedContour Prepared;
		FString Error;
		const bool bPrepared = BreachContour::Prepare(Bases[Fixture], Options, Prepared, Error);
		TestTrue(FString::Printf(TEXT("Worst-case fixture %d prepares: %s"), Fixture, *Error), bPrepared);
		if (!bPrepared)
		{
			continue;
		}
		TestEqual(TEXT("Preparation preserves requested vertex count"), Prepared.Samples.Num(), Counts[Fixture]);
		bool bCanMove = false;
		for (const BreachContour::FPreparedSample& Sample : Prepared.Samples)
		{
			bCanMove |= Sample.MaxDisplacement > 0;
		}
		TestTrue(TEXT("Safety limits retain contour deformation"), bCanMove);
		// Exercise full spacing jitter and full inward/outward displacement, beyond random field outputs.
		for (int32 Mask = 0; Mask < 8; ++Mask)
		{
			FPolygon2d Extreme;
			for (int32 Index = 0; Index < Prepared.Samples.Num(); ++Index)
			{
				const BreachContour::FPreparedSample& Sample = Prepared.Samples[Index];
				const double Sign = (Mask & (1 << (Index % 3))) ? 1 : -1;
				const FVector2d Jitter = (Mask & (1 << ((Index + 1) % 3))) ? Sample.JitterAfter : Sample.JitterBefore;
				Extreme.AppendVertex(Sample.Point + Jitter + Sample.Normal * (Sign * Sample.MaxDisplacement));
			}
			TestTrue(TEXT("Extreme contour remains nondegenerate and counterclockwise"), Extreme.SignedArea() > 0);
			bool bCrosses = false;
			for (int32 Index = 0; Index < Extreme.VertexCount() && !bCrosses; ++Index)
			{
				const FSegment2d Edge(Extreme[Index], Extreme[(Index + 1) % Extreme.VertexCount()]);
				for (int32 Other = Index + 2; Other < Extreme.VertexCount(); ++Other)
				{
					if (Index == 0 && Other == Extreme.VertexCount() - 1)
					{
						continue;
					}
					bCrosses = Edge.Intersects(FSegment2d(Extreme[Other], Extreme[(Other + 1) % Extreme.VertexCount()]), 1.e-10, 1.e-8);
					if (bCrosses)
					{
						break;
					}
				}
			}
			TestFalse(FString::Printf(TEXT("Fixture %d extreme movement %d does not cross or touch"), Fixture, Mask), bCrosses);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachContourVertexIntervalsTest, "BreachSystem.Contour.VertexIntervals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachContourVertexIntervalsTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Base = Polygon({{-30,-10}, {30,-10}, {30,10}, {-30,10}});
	// Six samples per long edge and two per short edge, without position jitter.
	const FVector2d Samples[] = {
		{-30,-10}, {-20,-10}, {-10,-10}, {0,-10}, {10,-10}, {20,-10}, {30,-10}, {30,0},
		{30,10}, {20,10}, {10,10}, {0,10}, {-10,10}, {-20,10}, {-30,10}, {-30,0}
	};
	const double RootTwo = FMath::Sqrt(2.0);
	auto Rotate = [RootTwo](const FVector2d& Point)
	{
		return FVector2d((Point.X - Point.Y) / RootTwo, (Point.X + Point.Y) / RootTwo);
	};
	FPolygon2d RotatedBase;
	for (const FVector2d& Point : Base.GetVertices())
	{
		RotatedBase.AppendVertex(Rotate(Point));
	}
	for (const EBreachContourPattern Pattern : { EBreachContourPattern::TriangleSections,
		EBreachContourPattern::IndependentContinuous })
	{
		FBreachContourOptions Options;
		Options.bEnabled = true;
		Options.Pattern = Pattern;
		Options.VertexCount = 16;
		Options.bRandomizeVertexSpacing = false;
		Options.MacroDepthRatio = 0.01f;
		Options.MediumDepthRatio = 0.005f;
		Options.MicroDepthRatio = 0.002f;
		Options.Seed = 123;
		FPolygon2d Result, RotatedResult;
		FString Error;
		const bool bGenerated = BreachContour::Generate(Base, Options, Result, Error);
		const bool bRotatedGenerated = BreachContour::Generate(RotatedBase, Options, RotatedResult, Error);
		TestTrue(TEXT("Original contour generates"), bGenerated);
		TestTrue(TEXT("Rotated contour generates"), bRotatedGenerated);
		if (!bGenerated || !bRotatedGenerated)
		{
			continue;
		}
		if (!TestEqual(TEXT("Original contour has 16 vertices"), Result.VertexCount(), 16)
			|| !TestEqual(TEXT("Rotated contour has 16 vertices"), RotatedResult.VertexCount(), 16))
		{
			continue;
		}
		for (int32 Index = 0; Index < 16; ++Index)
		{
			// Rotation changes the AABB shorter dimension from 20 to 40*sqrt(2).
			const FVector2d Expected = Rotate(Samples[Index] + (Result[Index] - Samples[Index]) * (2 * RootTwo));
			TestTrue(FString::Printf(TEXT("The 16-interval profile survives rotation, pattern %d vertex %d"), int32(Pattern), Index),
				RotatedResult[Index].Equals(Expected, 1.e-6));
		}
	}
	return !HasAnyErrors();
}
#endif
