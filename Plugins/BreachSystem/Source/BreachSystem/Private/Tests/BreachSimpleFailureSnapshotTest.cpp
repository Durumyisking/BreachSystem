#include "Surface/BreachSimpleCollisionDiagnostics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// 테스트 전용: 실패 입력의 실제 파일 저장, double 정밀도와 누락 경계 기록을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleFailureSnapshotTest, "BreachSystem.Diagnostics.SimpleFailureSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSimpleFailureSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	using namespace BreachSimpleCollisionDiagnostics;
	FPolygon2d Outer;
	for (const FVector2d& Point : {FVector2d(0, 0), FVector2d(10, 0), FVector2d(10, 10), FVector2d(0, 10)})
	{
		Outer.AppendVertex(Point);
	}
	FPolygon2d Precise;
	for (const FVector2d& Point : {FVector2d(1.0000000000000002, 2), FVector2d(3, 2), FVector2d(3, 4), FVector2d(1, 4)})
	{
		Precise.AppendVertex(Point);
	}
	FPolygon2d Crossing;
	for (const FVector2d& Point : {FVector2d(0, 0), FVector2d(4, 4), FVector2d(0, 4), FVector2d(4, 0)})
	{
		Crossing.AppendVertex(Point);
	}
	FGeneralPolygon2d Remaining(Outer);
	FPolygon2d ClockwiseHole = Precise;
	ClockwiseHole.Reverse();
	Remaining.AddHole(ClockwiseHole);
	const TArray<FGeneralPolygon2d> A = {Remaining};
	const TArray<FGeneralPolygon2d> B = {FGeneralPolygon2d(Precise)};
	const TArray<FGeneralPolygon2d> C = B;
	const TArray<FGeneralPolygon2d> D = {FGeneralPolygon2d(Crossing)};
	const FString Error = TEXT("Synthetic crossing fixture, not the user's runtime failure");
	const FFailureSnapshot Snapshot{Outer, {-5, 5}, A, B, C, D, 450.0, 1.e-7, true, 0, Error};
	const FString Text = SerializeSnapshot(Snapshot);
	TestTrue(TEXT("Original and all four calculation stages are captured"), Text.Contains(TEXT("STAGE Original\n")) && Text.Contains(TEXT("STAGE A_RemainingRegions\n")) && Text.Contains(TEXT("STAGE B_Holes\n")) && Text.Contains(TEXT("STAGE C_SimpleCuts\n")) && Text.Contains(TEXT("STAGE D_CollisionRegions\n")));
	TestTrue(TEXT("Hole boundary and its ordering are captured"), Text.Contains(TEXT("LOOP 0 COUNT 4\n1 4\n3 4\n3 2\n1.0000000000000002 2\n")));
	TestTrue(TEXT("Adjacent double value is not rounded to one"), Text.Contains(TEXT("1.0000000000000002 2\n")));
	TestTrue(TEXT("Area configuration and branch are captured"), Text.Contains(TEXT("AREA_THRESHOLD 450\n")) && Text.Contains(TEXT("FINAL_DIFFERENCE_APPLIED 1\n")));
	TestTrue(TEXT("Missing boundary edges include the failed stage"), Text.Contains(TEXT("MISSING stage=D_CollisionRegions region=0")));
	TestTrue(TEXT("Serialization leaves the source hole unchanged"), A[0].GetHoles()[0][3] == FVector2d(1.0000000000000002, 2));
	TestTrue(TEXT("Serialization leaves the failed output unchanged"), D[0].GetOuter()[1] == FVector2d(4, 4));
	FString Path;
	if (!TestTrue(TEXT("Failure snapshot writes an actual file"), SaveSnapshot(Snapshot, FPaths::ProjectSavedDir() / TEXT("Automation/SimpleFailureSnapshotFiles"), Path)))
	{
		return false;
	}
	FString Saved;
	TestTrue(TEXT("Written failure snapshot can be read"), FFileHelper::LoadFileToString(Saved, *Path));
	TestEqual(TEXT("Saved payload preserves every captured value"), Saved, Text);
	AddInfo(FString::Printf(TEXT("Synthetic diagnostic fixture saved to %s"), *Path));
	return !HasAnyErrors();
}
#endif
