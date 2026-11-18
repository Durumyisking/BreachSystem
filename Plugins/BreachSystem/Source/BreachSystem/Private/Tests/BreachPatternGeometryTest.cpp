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
#endif
