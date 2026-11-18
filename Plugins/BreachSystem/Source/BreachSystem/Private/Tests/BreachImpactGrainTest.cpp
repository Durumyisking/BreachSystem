#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"
#include "Misc/AutomationTest.h"

// 테스트 전용: 원에 닿는 결의 기본 절단, 확률 전파, 최종 외곽의 가시를 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachImpactGrainTest, "BreachSystem.Pattern.ImpactGrain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachImpactGrainTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->ImpactGrain.bEnabled = true;
	Pattern->ImpactGrain.StartProbability = 0;
	Pattern->GrainEndSharpness = .85f;
	Pattern->GrainMaxEndSplinters = 0;
	for (double X : {.25, .35, .45, .55, .65, .75})
	{
		Pattern->Guides.Add({{{X, 0}, {X, 1}}});
	}
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.005, 0);
	Mapping.UVPerY = FVector2d(0, .005);
	FPolygon2d Base;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		const double Angle = 2 * UE_DOUBLE_PI * Index / 24;
		Base.AppendVertex(FVector2d(100, 100) + 52 * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)));
	}
	FBreachContourOptions Contour;
	Contour.Seed = 19;
	Contour.bEnabled = true;
	auto Generate = [&](float Influence, TArray<FGeneralPolygon2d>& Result)
	{
		FString Error;
		const bool bSuccess = BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Contour, Influence, Result, Error);
		return TestTrue(FString::Printf(TEXT("Generate one valid impact cutter: %s"), *Error), bSuccess && Result.Num() == 1);
	};
	TArray<FGeneralPolygon2d> Primary, OtherDirection;
	if (!Generate(0, Primary) || !Generate(1, OtherDirection))
	{
		return false;
	}
	const FPolygon2d& Outline = Primary[0].GetOuter();
	TestTrue(TEXT("Without extra propagation, influence does not change the impact outline"), Outline.GetVertices() == OtherDirection[0].GetOuter().GetVertices());
	TestTrue(TEXT("The basic cutter reaches the grains across the hit footprint"), Outline.Bounds().Width() > Base.Bounds().Width() * .8);
	TestTrue(TEXT("The basic cutter retains the impact extent along the grain"), Outline.Bounds().Height() > Base.Bounds().Height() * .8);

	Pattern->ImpactGrain.StartProbability = 1;
	Pattern->ImpactGrain.Persistence = 1;
	bool bPropagatedBeyondBase = false;
	bool bAddedVisibleTips = false;
	for (int32 Seed = 1; Seed <= 16; ++Seed)
	{
		Contour.Seed = Seed;
		TArray<FGeneralPolygon2d> Grown, Detailed, Repeat;
		Pattern->GrainMaxEndSplinters = 0;
		if (!Generate(1, Grown))
		{
			return false;
		}
		for (const FVector2d& Point : Grown[0].GetOuter().GetVertices())
		{
			bPropagatedBeyondBase |= Point.Y < Base.Bounds().Min.Y - 1 || Point.Y > Base.Bounds().Max.Y + 1;
		}
		Pattern->GrainMaxEndSplinters = 3;
		if (!Generate(1, Detailed) || !Generate(1, Repeat))
		{
			return false;
		}
		TestTrue(TEXT("Same seed preserves the detailed boundary"), Detailed[0].GetOuter().GetVertices() == Repeat[0].GetOuter().GetVertices());
		bAddedVisibleTips |= Detailed[0].GetOuter().VertexCount() > Grown[0].GetOuter().VertexCount();
	}
	TestTrue(TEXT("Tears from hit grains can continue past the original boundary"), bPropagatedBeyondBase);
	TestTrue(TEXT("Sharp tips survive on the final cut outline"), bAddedVisibleTips);

	Pattern->GrainMaxEndSplinters = 0;
	bool bTouchedBandTore = false;
	for (int32 Seed = 1; Seed <= 8; ++Seed)
	{
		Contour.Seed = Seed;
		Pattern->Guides = {{{{.79, 0}, {.79, 1}}}, {{{.89, 0}, {.89, 1}}}};
		TArray<FGeneralPolygon2d> Touched;
		if (!Generate(1, Touched))
		{
			return false;
		}
		bTouchedBandTore |= Touched[0].GetOuter().GetVertices() != Base.GetVertices();
		Pattern->Guides = {{{{.91, 0}, {.91, 1}}}, {{{.99, 0}, {.99, 1}}}};
		TArray<FGeneralPolygon2d> Untouched;
		if (!Generate(1, Untouched))
		{
			return false;
		}
		TestTrue(TEXT("A neighboring band that misses the radius cannot start a tear"), Untouched[0].GetOuter().GetVertices() == Base.GetVertices());
	}
	TestTrue(TEXT("A band's touching edge can start a tear even if its guide misses the radius"), bTouchedBandTore);
	return true;
}
#endif
