#include "Surface/BreachDamageAccumulator.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	UE::Geometry::FPolygon2d AccumulationBox(const UE::Geometry::FAxisAlignedBox2d& Bounds)
	{
		UE::Geometry::FPolygon2d Polygon;
		Polygon.AppendVertex(Bounds.Min);
		Polygon.AppendVertex(FVector2d(Bounds.Max.X, Bounds.Min.Y));
		Polygon.AppendVertex(Bounds.Max);
		Polygon.AppendVertex(FVector2d(Bounds.Min.X, Bounds.Max.Y));
		return Polygon;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachAccumulationSpatialTest, "BreachSystem.Accumulation.SpatialIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachAccumulationSpatialTest::RunTest(const FString& Parameters)
{
	FBreachDamageAccumulator Accumulator;
	FString Error;
	if (!TestTrue(TEXT("Configure physical damage window"), Accumulator.Configure(FVector2d(80, 80), 1, Error)))
	{
		return false;
	}
	for (int32 Index = 0; Index < 10000; ++Index)
	{
		Accumulator.AddHit(FVector2d(10000 + Index * 1000.0, 10000), 0.17, Error);
	}
	int32 LastID = INDEX_NONE;
	for (double X : {-39.0, -25.0, -10.0, 5.0, 20.0, 35.0})
	{
		LastID = Accumulator.AddHit(FVector2d(X, -1), 0.17, Error);
	}
	TArray<FBreachAccumulationCandidate> Candidates;
	FBreachAccumulationQueryStats Stats;
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestEqual(TEXT("Six nearby records breach across negative bucket boundary"), Candidates.Num(), 1);
	TestEqual(TEXT("Ten thousand distant records are never examined"), Stats.ExaminedRecords, 6);
	TestTrue(TEXT("Full-window neighborhood visits at most nine cells"), Stats.VisitedCells <= 9);
	Accumulator.RemoveInside(AccumulationBox(UE::Geometry::FAxisAlignedBox2d(FVector2d(-50, -10), FVector2d(80, 10))));
	TestEqual(TEXT("Local retirement preserves distant records"), Accumulator.Num(), 10000);
	TestFalse(TEXT("Invalid reconfiguration rejected"), Accumulator.Configure(FVector2d(0, 80), 1, Error));
	TestEqual(TEXT("Invalid settings preserve records"), Accumulator.Num(), 10000);
	TestEqual(TEXT("Non-positive contribution rejected"), Accumulator.AddHit(FVector2d::ZeroVector, 0, Error), INDEX_NONE);
	Accumulator.Reset();
	TestEqual(TEXT("Reset clears records and index"), Accumulator.Num(), 0);
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestTrue(TEXT("Removed IDs cannot return stale candidates"), Candidates.IsEmpty());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachAccumulationWindowTest, "BreachSystem.Accumulation.Windows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachAccumulationWindowTest::RunTest(const FString& Parameters)
{
	FBreachDamageAccumulator Accumulator;
	FString Error;
	if (!TestTrue(TEXT("Configure moving window"), Accumulator.Configure(FVector2d(110, 80), 1, Error)))
	{
		return false;
	}
	int32 LastID = INDEX_NONE;
	for (double X : {-100.0, -80.0, -60.0, -40.0, -20.0, 20.0, 40.0, 60.0, 80.0, 100.0})
	{
		LastID = Accumulator.AddHit(FVector2d(X, 0), 0.17, Error);
	}
	TArray<FBreachAccumulationCandidate> Candidates;
	FBreachAccumulationQueryStats Stats;
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestTrue(TEXT("Ten spread records are not one chain-connected breach"), Candidates.IsEmpty());
	const int32 CenterID = Accumulator.AddHit(FVector2d::ZeroVector, 0.17, Error);
	Accumulator.FindCandidates(CenterID, Candidates, Stats);
	if (TestEqual(TEXT("Central hit produces left and right candidates together"), Candidates.Num(), 2))
	{
		TestTrue(TEXT("Left window can use the central record"), Candidates[0].RecordIDs.Contains(CenterID));
		TestTrue(TEXT("Right window can use the same central record"), Candidates[1].RecordIDs.Contains(CenterID));
		TestEqual(TEXT("Left candidate contains six hits"), Candidates[0].RecordIDs.Num(), 6);
		TestEqual(TEXT("Right candidate contains six hits"), Candidates[1].RecordIDs.Num(), 6);
		TestTrue(TEXT("Candidates meet weighted threshold"), Candidates[0].Contribution >= 1 && Candidates[1].Contribution >= 1);
		Accumulator.RemoveInside(AccumulationBox(Candidates[0].Bounds));
		TestTrue(TEXT("Frozen right candidate survives retirement of shared input"), Candidates[1].RecordIDs.Contains(CenterID));
		Accumulator.RemoveInside(AccumulationBox(Candidates[1].Bounds));
		TestEqual(TEXT("Both successful regions retire their inputs"), Accumulator.Num(), 0);
	}
	Accumulator.Reset();
	LastID = Accumulator.AddHit(FVector2d(0, 0), 0.6, Error);
	LastID = Accumulator.AddHit(FVector2d(10, 0), 0.4, Error);
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestEqual(TEXT("Different contribution weights add in one pool"), Candidates.Num(), 1);
	Accumulator.Reset();
	LastID = Accumulator.AddHit(FVector2d::ZeroVector, 1.05, Error);
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestEqual(TEXT("Single strong impact breaches"), Candidates.Num(), 1);
	Accumulator.Reset();
	for (int32 Index = 0; Index < 6; ++Index)
	{
		LastID = Accumulator.AddHit(FVector2d(Index * 100, 0), 0.17, Error);
	}
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestTrue(TEXT("Far-apart impacts stay below local threshold"), Candidates.IsEmpty());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachAccumulationDenseTest, "BreachSystem.Accumulation.DenseAndSparse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachAccumulationDenseTest::RunTest(const FString& Parameters)
{
	FBreachDamageAccumulator Accumulator;
	FString Error;
	Accumulator.Configure(FVector2d(80, 80), 1, Error);
	constexpr int32 Count = 96;
	int32 LastID = INDEX_NONE;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		LastID = Accumulator.AddHit(FVector2d(Index * 0.25, (Index * 37 % Count) * 0.25), 0.02, Error);
	}
	TArray<FBreachAccumulationCandidate> Candidates;
	FBreachAccumulationQueryStats Stats;
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	TestEqual(TEXT("Dense group produces one complete window"), Candidates.Num(), 1);
	TestTrue(TEXT("Window search does not rescan every record for every X/Y pair"),
		Stats.MembershipEvaluations <= 8LL * Count * Count);
	Accumulator.Reset();
	Accumulator.Configure(FVector2d(0.1, 0.1), 1, Error);
	Accumulator.AddHit(FVector2d::ZeroVector, 0.17, Error);
	Accumulator.AddHit(FVector2d(1000, 1000), 0.17, Error);
	Accumulator.RemoveInside(AccumulationBox(UE::Geometry::FAxisAlignedBox2d(FVector2d(-50, -50), FVector2d(50, 50))), &Stats);
	TestEqual(TEXT("Large cleanup visits only occupied cells in range"), Stats.VisitedCells, 1);
	TestEqual(TEXT("Large cleanup does not inspect distant records"), Stats.ExaminedRecords, 1);
	TestEqual(TEXT("Large cleanup preserves the distant record"), Accumulator.Num(), 1);
	Accumulator.Reset();
	const double Left = -13683.934321557477;
	const double Right = Left + 0.1;
	Accumulator.AddHit(FVector2d(Left, 0), 0.6, Error);
	LastID = Accumulator.AddHit(FVector2d(Right, 0), 0.4, Error);
	Accumulator.FindCandidates(LastID, Candidates, Stats);
	if (TestEqual(TEXT("Recentering at a fractional boundary produces one candidate"), Candidates.Num(), 1))
	{
		TestEqual(TEXT("Boundary records stay in the qualifying window"), Candidates[0].RecordIDs.Num(), 2);
		TestTrue(TEXT("New boundary impact stays inside its candidate"), Candidates[0].Bounds.Contains(FVector2d(Right, 0)));
		TestTrue(TEXT("Recentered candidate still meets the threshold"), Candidates[0].Contribution >= 1);
	}
	return !HasAnyErrors();
}
#endif
