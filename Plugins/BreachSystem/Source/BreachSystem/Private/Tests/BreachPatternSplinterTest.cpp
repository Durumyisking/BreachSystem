#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 가시 개수 상한, 생략, 안팎 방향, 재현성과 실제 커터의 해제를 검증한다.
#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"
#include "Surface/BreachGrainFracture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternSplinterProfileTest, "BreachSystem.Pattern.Splinters.Profile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternSplinterProfileTest::RunTest(const FString& Parameters)
{
	bool bOmitted = false, bMultiple = false, bInward = false, bOutward = false, bOffCenter = false, bSingleOffCenter = false;
	double MinLength = 10, MaxLength = 0;
	for (int32 Maximum : {0,1,4,8})
	{
		for (int32 Seed = 1; Seed <= 128; ++Seed)
		{
			FRandomStream Random(Seed), Repeat(Seed);
			TArray<FVector2d> Profile, Repeated;
			BreachGrainFracture::AppendEndProfile(Maximum,10,40,Random,Profile);
			BreachGrainFracture::AppendEndProfile(Maximum,10,40,Repeat,Repeated);
			TestTrue(TEXT("Same profile seed repeats exactly"),Profile == Repeated);
			int32 Count = 0;
			for (int32 I = 0; I < Profile.Num(); ++I)
			{
				TestTrue(TEXT("Profile positions stay inside its short end"),Profile[I].X >= 0 && Profile[I].X <= 1);
				TestTrue(TEXT("Profile lengths stay inside reach"),FMath::Abs(Profile[I].Y) <= 10);
				if (I > 0)
				{
					TestTrue(TEXT("Profile positions increase without overlap"),Profile[I].X > Profile[I - 1].X);
				}
				if (FMath::Abs(Profile[I].Y) > 1.e-8)
				{
					++Count;
					bInward |= Profile[I].Y < 0;
					bOutward |= Profile[I].Y > 0;
					bOffCenter |= Profile[I].X < .25 || Profile[I].X > .75;
					bSingleOffCenter |= Maximum == 1 && (Profile[I].X < .25 || Profile[I].X > .75);
					MinLength = FMath::Min(MinLength,FMath::Abs(Profile[I].Y));
					MaxLength = FMath::Max(MaxLength,FMath::Abs(Profile[I].Y));
				}
			}
			TestTrue(TEXT("Total inward plus outward count obeys the asset maximum"),Count <= Maximum);
			bOmitted |= Maximum > 0 && Count == 0;
			bMultiple |= Count > 1;
		}
	}
	TestTrue(TEXT("A nonzero maximum can randomly produce no splinter"),bOmitted);
	TestTrue(TEXT("Several splinters can occur on one end"),bMultiple);
	TestTrue(TEXT("Splinters can point both inward and outward"),bInward && bOutward);
	TestTrue(TEXT("Splinters are not fixed to the center"),bOffCenter);
	TestTrue(TEXT("Even a single splinter can occur away from the center"),bSingleOffCenter);
	TestTrue(TEXT("Splinter length varies"),MaxLength - MinLength > 3);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternSplinterAssetTest, "BreachSystem.Pattern.Splinters.Asset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternSplinterAssetTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	for (double Y : {-36.,-12.,12.,36.})
	{
		Pattern->Guides.AddDefaulted_GetRef().Points = {{-.5,Y/200},{.5,Y/200}};
	}
	FString Error;
	for (int32 Value : {-1,9})
	{
		Pattern->GrainMaxEndSplinters = Value;
		TestFalse(TEXT("Invalid active maximum rejects"),BreachPatternGeometry::Validate(*Pattern,Error));
	}
	Pattern->GrainMaxEndSplinters = 0;
	Pattern->GrainEndSharpness = 1;
	const FPolygon2d Base(TArray<FVector2d>{{-30,-45},{30,-45},{30,45},{-30,45}});
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.005,0);
	Mapping.UVPerY = FVector2d(0,.005);
	FBreachContourOptions Options;
	Options.bEnabled = false;
	Options.VertexCount = 48;
	Options.Seed = 31;
	TArray<FGeneralPolygon2d> Disabled, Blunt;
	TestTrue(TEXT("Zero maximum generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Disabled,Error));
	Pattern->GrainMaxEndSplinters = 8;
	Pattern->GrainEndSharpness = 0;
	TestTrue(TEXT("Zero sharpness reference generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Blunt,Error));
	if (Disabled.Num() == 1 && Blunt.Num() == 1)
	{
		double Difference = 0;
		for (int32 Direction = 0; Direction < 2; ++Direction)
		{
			const auto& A = Direction == 0 ? Disabled[0].GetOuter() : Blunt[0].GetOuter();
			const auto& B = Direction == 0 ? Blunt[0].GetOuter() : Disabled[0].GetOuter();
			for (FVector2d P : A.GetVertices())
			{
				Difference = FMath::Max(Difference,FMath::Sqrt(B.DistanceSquared(P)));
			}
		}
		TestTrue(TEXT("Zero asset maximum disables teeth while preserving grain tears"),Difference < 1.e-7);
	}
	Pattern->GrainEndSharpness = 1;
	Options.bEnabled = true;
	Options.VertexCount = 128;
	for (int32 Seed : {1,31,77})
	{
		Options.Seed = Seed;
		for (double Influence : {.6,1.})
		{
			TArray<FGeneralPolygon2d> Result;
			TestTrue(TEXT("Maximum8 with128 samples generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,Influence,Result,Error));
			if (Result.Num() == 1)
			{
				TestTrue(TEXT("Maximum8 remains simple"),BreachPatternGeometry::ValidateRegions(Result,Error));
				TestTrue(TEXT("Maximum8 retains the128 vertex budget"),Result[0].GetOuter().VertexCount() <= 128);
			}
		}
	}

	return !HasAnyErrors();
}
#endif
