#include "Surface/BreachDamageAccumulator.h"
#include "Misc/AutomationTest.h"

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachCellOrderTest, "BreachSystem.Cells.ContributionOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachCellOrderTest::RunTest(const FString& Parameters)
{
	// Loss of small terms must not make a different shot order miss the threshold.
	for (const TArray<double>& Weights : {TArray<double>{1.e16, 1, 1}, TArray<double>{1, 1, 1.e16}})
	{
		FBreachDamageAccumulator Accumulator;
		FString Error;
		TestTrue(TEXT("Configure threshold"), Accumulator.Configure({40,40}, 1.e16 + 2, Error));
		int32 Last = INDEX_NONE;
		for (double Weight : Weights)
		{
			Last = Accumulator.AddHit({5,5}, Weight, Error);
		}
		TArray<FBreachAccumulationCandidate> Candidates;
		FBreachAccumulationQueryStats Stats;
		Accumulator.FindCandidates(Last, Candidates, &Stats);
		TestEqual(TEXT("Same geometric group qualifies regardless of contribution insertion order"), Candidates.Num(), 1);
	}
	return !HasAnyErrors();
}
#endif
