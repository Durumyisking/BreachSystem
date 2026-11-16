#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"
#include "Misc/AutomationTest.h"
#include <limits>

// 테스트 전용: 패턴 선택, 구멍 보존, 매핑과 결정성을 Editor 자동화에서 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d PatternBox(double X0, double Y0, double X1, double Y1)
	{
		return FPolygon2d(TArray<FVector2d>{{X0,Y0}, {X1,Y0}, {X1,Y1}, {X0,Y1}});
	}

	FBreachPatternRegion PatternRegion(double X0, double Y0, double X1, double Y1)
	{
		FBreachPatternRegion Result;
		Result.Outer = PatternBox(X0, Y0, X1, Y1).GetVertices();
		return Result;
	}

	bool SamePatternBoundary(const FPolygon2d& A, const FPolygon2d& B)
	{
		if (A.VertexCount() != B.VertexCount())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.VertexCount(); ++Index)
		{
			if ((A[Index] - B[Index]).Length() > 1.e-10)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternRegionsTest, "BreachSystem.Pattern.Regions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachPatternRegionsTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::Regions;
	Pattern->Regions.Add(PatternRegion(0.1, 0.1, 0.8, 0.8));
	FBreachContourOptions Contour;
	FBreachPatternMapping Mapping;
	TArray<FGeneralPolygon2d> Result;
	FString Error;
	TestTrue(TEXT("Region with center outside cutter is selected by overlap"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.05, 0.2, 0.15, 0.3), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Only actual overlapping region selected"), Result.Num(), 1);
	if (Result.Num() == 1)
	{
		TestTrue(TEXT("Selection retains complete region"), FMath::IsNearlyEqual(Result[0].SignedArea(), 0.49, 1.e-8));
	}
	TestTrue(TEXT("Edge touch selects region"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.05, 0.2, 0.1, 0.3), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Touch is included"), Result.Num(), 1);
	TestTrue(TEXT("Regions ignore zero grain influence"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.05, 0.2, 0.15, 0.3), Mapping, Contour, 0, Result, Error));
	TestEqual(TEXT("Zero grain influence still selects region"), Result.Num(), 1);

	FBreachPatternLoop Hole;
	Hole.Vertices = PatternBox(0.3, 0.3, 0.6, 0.6).GetVertices();
	Pattern->Regions[0].Holes.Add(Hole);
	TestTrue(TEXT("Valid hole accepted"), BreachPatternGeometry::Validate(*Pattern, Error));
	TestTrue(TEXT("Cutter inside a hole generates no region"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.4, 0.4, 0.5, 0.5), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Hole interior excluded"), Result.Num(), 0);
	TestTrue(TEXT("Cutter inside hole touching its edge is selected"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.4, 0.3, 0.5, 0.4), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Hole boundary contact included"), Result.Num(), 1);
	TestTrue(TEXT("Hole boundary touch selects region"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.4, 0.25, 0.5, 0.3), Mapping, Contour, 1, Result, Error));
	if (Result.Num() == 1)
	{
		TestEqual(TEXT("Whole hole survives"), Result[0].GetHoles().Num(), 1);
	}

	Pattern->Regions.Reset();
	Pattern->Regions.Add(PatternRegion(0.9, 0.2, 1.1, 0.4));
	TestTrue(TEXT("Seam copy participates across tile boundary"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.01, 0.25, 0.05, 0.3), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Exactly one local seam copy"), Result.Num(), 1);
	if (Result.Num() == 1)
	{
		TestTrue(TEXT("Copy is translated, not clipped"), Result[0].GetOuter().Bounds().Min.X < 0);
	}
	Pattern->Regions.Add(PatternRegion(0.15, 0.2, 0.95, 0.4));
	TestTrue(TEXT("Local selection does not recurse into a touching neighbor"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.01, 0.25, 0.05, 0.3), Mapping, Contour, 1, Result, Error));
	if (Result.Num() == 1)
	{
		TestTrue(TEXT("Unhit neighbor does not expand output"), Result[0].GetOuter().Bounds().Max.X < 0.11);
	}
	Pattern->Regions.Reset();
	FBreachPatternRegion Triangle;
	Triangle.Outer = {{0.1,0.1}, {0.8,0.1}, {0.1,0.8}};
	Pattern->Regions.Add(Triangle);
	TestTrue(TEXT("AABB overlap alone is insufficient"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.65, 0.65, 0.7, 0.7), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Disjoint geometry inside overlapping bounds is excluded"), Result.Num(), 0);
	Pattern->Regions.Reset();
	Pattern->Regions.Add(PatternRegion(0.1, 0.1, 0.5, 0.5));
	Pattern->Regions.Add(PatternRegion(0.3, 0.1, 0.7, 0.5));
	TestTrue(TEXT("Hit regions unite"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0.35, 0.2, 0.4, 0.3), Mapping, Contour, 1, Result, Error));
	TestEqual(TEXT("Overlapping regions yield one union"), Result.Num(), 1);
	if (Result.Num() == 1)
	{
		TestTrue(TEXT("Union does not double-count overlap"), FMath::IsNearlyEqual(Result[0].SignedArea(), 0.24, 1.e-8));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternValidationTest, "BreachSystem.Pattern.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachPatternValidationTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::Regions;
	Pattern->Regions.Add(PatternRegion(0.1, 0.1, 0.9, 0.9));
	FString Error;
	TestTrue(TEXT("Simple region accepted"), BreachPatternGeometry::Validate(*Pattern, Error));
	Pattern->Regions[0].Outer = {{0,0}, {1,1}, {0,1}, {0.8,0}};
	TestFalse(TEXT("Nonzero-area crossing rejected"), BreachPatternGeometry::Validate(*Pattern, Error));
	Pattern->Regions[0] = PatternRegion(0.1, 0.1, 0.9, 0.9);
	FBreachPatternLoop Hole;
	Hole.Vertices = PatternBox(0.8, 0.2, 1.1, 0.3).GetVertices();
	Pattern->Regions[0].Holes.Add(Hole);
	TestFalse(TEXT("Escaping hole rejected"), BreachPatternGeometry::Validate(*Pattern, Error));
	Pattern->Regions[0].Holes[0].Vertices = PatternBox(0.2, 0.2, 0.5, 0.5).GetVertices();
	Hole.Vertices = PatternBox(0.3, 0.3, 0.6, 0.6).GetVertices();
	Pattern->Regions[0].Holes.Add(Hole);
	TestFalse(TEXT("Intersecting holes rejected"), BreachPatternGeometry::Validate(*Pattern, Error));
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	FBreachPatternGuide Guide;
	Guide.Points = {{0,0}, {1,1}, {0,1}, {1,0}};
	Pattern->Guides.Add(Guide);
	TestFalse(TEXT("Self-crossing open guide rejected"), BreachPatternGeometry::Validate(*Pattern, Error));
	Pattern->Guides[0].Points = {{0,0}, {1,std::numeric_limits<double>::infinity()}};
	TestFalse(TEXT("Nonfinite guide rejected"), BreachPatternGeometry::Validate(*Pattern, Error));
	TArray<FGeneralPolygon2d> Result{FGeneralPolygon2d(PatternBox(2, 2, 3, 3))};
	TestFalse(TEXT("Invalid generation rejected"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0, 0, 1, 1), FBreachPatternMapping(), FBreachContourOptions(), 1, Result, Error));
	TestEqual(TEXT("Failure preserves output"), Result.Num(), 1);
	TestTrue(TEXT("Failure preserves original coordinates"), SamePatternBoundary(Result[0].GetOuter(), PatternBox(2, 2, 3, 3)));
	Pattern->Guides[0].Points = {{0.4,0}, {0.4,1}};
	FBreachPatternMapping Singular;
	Singular.UVPerY = Singular.UVPerX;
	TestFalse(TEXT("Singular UV mapping rejected"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0, 0, 1, 1), Singular, FBreachContourOptions(), 1, Result, Error));
	TestEqual(TEXT("Mapping rejection preserves output"), Result.Num(), 1);
	Pattern->Mode = EBreachPatternMode::Regions;
	Pattern->Regions.Reset();
	Pattern->Regions.Add(PatternRegion(0, 0, 1, 1));
	TestFalse(TEXT("Unbounded local tile workload rejected"), BreachPatternGeometry::Generate(*Pattern, PatternBox(0, 0, 1000, 1000), FBreachPatternMapping(), FBreachContourOptions(), 1, Result, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainTest, "BreachSystem.Pattern.Grain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachPatternGrainTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	FBreachPatternGuide Guide;
	Guide.Points = {{0.4,0}, {0.4,1}};
	Pattern->Guides.Add(Guide);
	FBreachContourOptions Contour;
	Contour.bEnabled = true;
	Contour.Seed = 0;
	Contour.VertexCount = 32;
	Contour.bRandomizeVertexSpacing = false;
	FBreachPatternMapping Mapping;
	FString Error;
	const TArray<FPolygon2d> Bases = {
		PatternBox(0.2, 0.2, 0.7, 0.7),
		FPolygon2d(TArray<FVector2d>{{0.2,0.2}, {0.7,0.2}, {0.7,0.7}, {0.5,0.7}, {0.5,0.4}, {0.2,0.4}})
	};
	for (const FPolygon2d& Base : Bases)
	{
		TArray<FGeneralPolygon2d> Zero, General, First, Second;
		Pattern->Mode = EBreachPatternMode::None;
		TestTrue(TEXT("General generation"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Contour, 1, General, Error));
		Pattern->Mode = EBreachPatternMode::GrainGuides;
		TestTrue(TEXT("Influence zero generation"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Contour, 0, Zero, Error));
		if (General.Num() == 1 && Zero.Num() == 1)
		{
			TestTrue(TEXT("Influence zero equals general contour"), SamePatternBoundary(General[0].GetOuter(), Zero[0].GetOuter()));
			FBreachContourOptions Normalized = Contour;
			Normalized.Seed = 1;
			FPolygon2d Direct;
			TestTrue(TEXT("Direct contour baseline"), BreachContour::Generate(Base, Normalized, Direct, Error));
			TestTrue(TEXT("Influence zero preserves current contour behavior"), SamePatternBoundary(Direct, Zero[0].GetOuter()));
		}
		TestTrue(TEXT("Grain supports noncircle cutter"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Contour, 1, First, Error));
		TestTrue(TEXT("Repeated seed zero generation"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Contour, 1, Second, Error));
		if (First.Num() == 1 && Second.Num() == 1)
		{
			TestTrue(TEXT("Seed zero reproducible"), SamePatternBoundary(First[0].GetOuter(), Second[0].GetOuter()));
			TestTrue(TEXT("Grain result simple"), BreachPatternGeometry::ValidateRegions(First, Error));
			BreachContour::FPreparedContour Prepared;
			FBreachContourOptions Normalized = Contour;
			Normalized.Seed = 1;
			TestTrue(TEXT("Prepared samples for displacement envelope"), BreachContour::Prepare(Base, Normalized, Prepared, Error));
			if (Prepared.Samples.Num() == First[0].GetOuter().VertexCount())
			{
				const double Cap = Prepared.CutterSize * Contour.MaxDisplacementFraction;
				for (int32 Index = 0; Index < Prepared.Samples.Num(); ++Index)
				{
					TestTrue(TEXT("Combined grain and noise displacement stays within contour cap"), (First[0].GetOuter()[Index] - Prepared.Samples[Index].Point).Length() <= Cap + 1.e-10);
				}
			}
			if (Zero.Num() == 1)
			{
				TestFalse(TEXT("Grain influence changes boundary"), SamePatternBoundary(First[0].GetOuter(), Zero[0].GetOuter()));
			}
		}
	}
	for (int32 Orientation = 0; Orientation < 3; ++Orientation)
	{
		if (Orientation == 0)
		{
			Pattern->Guides[0].Points = {{0,0.4}, {1,0.4}};
		}
		else if (Orientation == 1)
		{
			Pattern->Guides[0].Points = {{0.4,0}, {0.4,1}};
		}
		else
		{
			Mapping.UVPerX = FVector2d(0.7, -0.7);
			Mapping.UVPerY = FVector2d(0.7, 0.7);
		}
		TArray<FGeneralPolygon2d> Result;
		TestTrue(TEXT("Horizontal, vertical and rotated metric mapping guides"), BreachPatternGeometry::Generate(*Pattern, Bases[0], Mapping, Contour, 1, Result, Error));
		TestTrue(TEXT("Generated orientation remains simple"), BreachPatternGeometry::ValidateRegions(Result, Error));
	}

	// 테스트 전용: 잡음을 작게 고정하여 결 방향 유도량을 일반 윤곽과 분리해 비교한다.
	FBreachContourOptions AttractionOnly = Contour;
	AttractionOnly.MacroDepthRatio = 0.00001f;
	AttractionOnly.MediumDepthRatio = 0;
	AttractionOnly.MicroDepthRatio = 0;
	Mapping = FBreachPatternMapping();
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		Pattern->Guides[0].Points = Axis == 0 ? TArray<FVector2D>{{0.4,0}, {0.4,1}} : TArray<FVector2D>{{0,0.4}, {1,0.4}};
		TArray<FGeneralPolygon2d> Plain, Grain;
		TestTrue(TEXT("Plain reference for attraction"), BreachPatternGeometry::Generate(*Pattern, Bases[0], Mapping, AttractionOnly, 0, Plain, Error));
		TestTrue(TEXT("Directional guide attraction"), BreachPatternGeometry::Generate(*Pattern, Bases[0], Mapping, AttractionOnly, 1, Grain, Error));
		if (Plain.Num() == 1 && Grain.Num() == 1 && Plain[0].GetOuter().VertexCount() == Grain[0].GetOuter().VertexCount())
		{
			FVector2d Total = FVector2d::ZeroVector;
			for (int32 Index = 0; Index < Plain[0].GetOuter().VertexCount(); ++Index)
			{
				const FVector2d Difference = Grain[0].GetOuter()[Index] - Plain[0].GetOuter()[Index];
				Total.X += FMath::Abs(Difference.X);
				Total.Y += FMath::Abs(Difference.Y);
			}
			TestTrue(TEXT("Parallel tangent attraction dominates cross-grain tangent attraction"), Axis == 0 ? Total.X > Total.Y * 2 : Total.Y > Total.X * 2);
			if (Axis == 0)
			{
				FBreachContourOptions Normalized = AttractionOnly;
				Normalized.Seed = 1;
				BreachContour::FPreparedContour Prepared;
				TestTrue(TEXT("Cross-grain noise prepared baseline"), BreachContour::Prepare(Bases[0], Normalized, Prepared, Error));
				bool bStrongerCrossNoise = false;
				for (int32 Index = 0; Index < Prepared.Samples.Num(); ++Index)
				{
					const BreachContour::FPreparedSample& Sample = Prepared.Samples[Index];
					if (FMath::Abs(Sample.Normal.X) < 1.e-10)
					{
						const double PlainNoise = FMath::Abs(Plain[0].GetOuter()[Index].Y - Sample.Point.Y);
						const double GrainNoise = FMath::Abs(Grain[0].GetOuter()[Index].Y - Sample.Point.Y);
						bStrongerCrossNoise |= GrainNoise > PlainNoise + 1.e-10;
					}
				}
				TestTrue(TEXT("Cross-grain noise is stronger than the same general noise field"), bStrongerCrossNoise);
			}
		}
	}

	Pattern->Guides[0].Points = {{0.4,0}, {0.4,1}};
	const FPolygon2d SmallBase = PatternBox(0.35, 0.35, 0.45, 0.45);
	TArray<FGeneralPolygon2d> Identity, Anisotropic;
	TestTrue(TEXT("Identity metric guide baseline"), BreachPatternGeometry::Generate(*Pattern, SmallBase, Mapping, Contour, 1, Identity, Error));
	Mapping.UVPerX = FVector2d(2, 0);
	Pattern->Guides[0].Points = {{0.8,0}, {0.8,1}};
	TestTrue(TEXT("Anisotropic UV scale representing the same local metric guide"), BreachPatternGeometry::Generate(*Pattern, SmallBase, Mapping, Contour, 1, Anisotropic, Error));
	if (Identity.Num() == 1 && Anisotropic.Num() == 1)
	{
		TestTrue(TEXT("Guide distance is measured after metric transformation"), SamePatternBoundary(Identity[0].GetOuter(), Anisotropic[0].GetOuter()));
	}
	Mapping = FBreachPatternMapping();
	Pattern->Guides[0].Points = {{0.4,0}, {0.4,1}};
	Contour.bMoveCorners = false;
	TArray<FGeneralPolygon2d> FixedCorners;
	TestTrue(TEXT("Grain respects fixed base corners"), BreachPatternGeometry::Generate(*Pattern, Bases[0], Mapping, Contour, 1, FixedCorners, Error));
	if (FixedCorners.Num() == 1)
	{
		for (const FVector2d& Corner : Bases[0].GetVertices())
		{
			bool bFound = false;
			for (const FVector2d& Point : FixedCorners[0].GetOuter().GetVertices())
			{
				bFound |= (Point - Corner).Length() < 1.e-10;
			}
			TestTrue(TEXT("Original corner retained"), bFound);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGuideWithoutNoiseTest, "BreachSystem.Pattern.GrainWithoutNoise", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachPatternGuideWithoutNoiseTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	FBreachPatternGuide Guide;
	Guide.Points = {{0.4,0}, {0.4,1}};
	Pattern->Guides.Add(Guide);
	const FPolygon2d Base = PatternBox(0.2, 0.2, 0.7, 0.7);
	const FBreachPatternMapping Mapping;
	FString Error;
	for (int32 Case = 0; Case < 2; ++Case)
	{
		FBreachContourOptions Options;
		Options.bEnabled = Case == 1;
		Options.VertexCount = 32;
		Options.bMoveCorners = false;
		if (Case == 1)
		{
			Options.MacroDepthRatio = 0;
			Options.MediumDepthRatio = 0;
			Options.MicroDepthRatio = 0;
		}
		TArray<FGeneralPolygon2d> Plain, Guided;
		TestTrue(TEXT("No-noise influence zero baseline generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Options, 0, Plain, Error));
		if (Plain.Num() == 1)
		{
			TestTrue(TEXT("Influence zero retains exact no-noise base"), SamePatternBoundary(Plain[0].GetOuter(), Base));
		}
		TestTrue(TEXT("Guides generate independently of general noise enable/depth"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Options, 1, Guided, Error));
		TestEqual(TEXT("Guide-only output count"), Guided.Num(), 1);
		if (Guided.Num() == 1)
		{
			TestTrue(TEXT("Guide-only boundary is finite and simple"), BreachPatternGeometry::ValidateRegions(Guided, Error));
			bool bActuallyDisplaced = false;
			const double Cap = 0.5 * Options.MaxDisplacementFraction;
			for (const FVector2d& Point : Guided[0].GetOuter().GetVertices())
			{
				const double BoundaryDistance = FMath::Sqrt(Base.DistanceSquared(Point));
				bActuallyDisplaced |= BoundaryDistance > 1.e-5;
				TestTrue(TEXT("Guide-only displacement respects existing cap"), BoundaryDistance <= Cap + 1.e-10);
			}
			TestTrue(TEXT("Positive influence moves geometry even with no general noise"), bActuallyDisplaced);
			for (const FVector2d& Corner : Base.GetVertices())
			{
				bool bFound = false;
				for (const FVector2d& Point : Guided[0].GetOuter().GetVertices())
				{
					bFound |= (Point - Corner).Length() < 1.e-10;
				}
				TestTrue(TEXT("Guide-only fixed corners remain fixed"), bFound);
			}
		}
		Options.MaxDisplacementFraction = 0;
		TArray<FGeneralPolygon2d> Capped;
		TestTrue(TEXT("Zero displacement cap generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Options, 1, Capped, Error));
		if (Capped.Num() == 1)
		{
			TestTrue(TEXT("Zero cap still disables all displacement"), SamePatternBoundary(Capped[0].GetOuter(), Base));
		}
	}
	return !HasAnyErrors();
}
#endif
