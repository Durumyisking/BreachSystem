#include "Misc/AutomationTest.h"

// 테스트 전용: 저장하지 않는 임시 맵의 실제 PIE 월드에서 연사·물리 질의·반영 지연을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "Tests/AutomationEditorCommon.h"
#include "BreachSimpleAsyncTestUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	using namespace UE::Geometry;
	class FExerciseBreachPIE final : public IAutomationLatentCommand
	{
		struct FPendingShot
		{
			uint64 Revision;
			double RequestedAt;
		};
		FAutomationTestBase* Test;
		TWeakObjectPtr<UBreachableSurfaceComponent> Surface;
		TArray<FPendingShot> Pending;
		TArray<double> Latencies;
		int32 Shots = 0, WarmFrames = 0;
		double NextShotAt = 0, Started = FPlatformTime::Seconds();

		bool Hits(UWorld* World, const UBreachableSurfaceComponent* Current, const FVector2d Point, bool bComplex, bool bCapsule = false) const
		{
			check(World);
			check(Current);
			const FTransform Transform = Current->GetComponentTransform();
			const FVector A = Transform.TransformPosition(FVector(Point.X, -100, Point.Y));
			const FVector B = Transform.TransformPosition(FVector(Point.X, 100, Point.Y));
			FCollisionQueryParams Params(SCENE_QUERY_STAT(BreachSimplePIE), bComplex);
			FHitResult Hit;
			if (bCapsule)
			{
				return World->SweepSingleByChannel(Hit, A, B, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(2, 4), Params);
			}
			return World->LineTraceSingleByChannel(Hit, A, B, bComplex ? ECC_Visibility : ECC_Pawn, Params);
		}

	public:
		explicit FExerciseBreachPIE(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			check(Test);
			if (!GEditor || !GEditor->PlayWorld)
			{
				Test->AddError(TEXT("Breach PIE verification requires a live PIE world."));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (++WarmFrames < 10)
			{
				return false;
			}
			if (!Surface.IsValid())
			{
				Test->TestEqual(TEXT("The runtime fixture is an actual PIE world"), World->WorldType, EWorldType::PIE);
				AActor* Actor = World->SpawnActor<AActor>();
				if (!Test->TestNotNull(TEXT("PIE breach actor"), Actor))
				{
					return true;
				}
				UBreachableSurfaceComponent* Current = NewObject<UBreachableSurfaceComponent>(Actor);
				Actor->SetRootComponent(Current);
				Current->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Current->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(0, 0, 500), FVector(8, 0.1, 5.4)));
				Current->RegisterComponent();
				Surface = Current;
				Started = FPlatformTime::Seconds();
			}
			UBreachableSurfaceComponent* Current = Surface.Get();
			check(Current);
			const double Now = FPlatformTime::Seconds();
			for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
			{
				if (FBreachSimpleAsyncTestAccess::PublishedRevision(Current) >= Pending[Index].Revision)
				{
					Latencies.Add((Now - Pending[Index].RequestedAt) * 1000);
					Test->TestFalse(TEXT("The first published broad passage remains traversable"), Hits(World, Current, {-25, 0}, false, true));
					Pending.RemoveAt(Index);
				}
				else if (Now - Pending[Index].RequestedAt > 1)
				{
					Test->AddError(FString::Printf(TEXT("PIE revision %llu did not publish within one second."), Pending[Index].Revision));
					Current->GetOwner()->Destroy();
					return true;
				}
			}
			if (Shots < 20 && Now >= NextShotAt)
			{
				const FVector2d Center(-25 + Shots * 2.5, FMath::Sin(Shots * 0.6) * 5);
				FPolygon2d Cutter;
				FRandomStream Random(200 + Shots);
				for (int32 Vertex = 0; Vertex < 24; ++Vertex)
				{
					const double Angle = UE_DOUBLE_TWO_PI * Vertex / 24;
					Cutter.AppendVertex(Center + Random.FRandRange(3.8f, 4.2f) * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)));
				}
				const double RequestedAt = FPlatformTime::Seconds();
				Test->TestTrue(TEXT("PIE sequential cutter succeeds"), Current->ApplyCutterPolygon(Cutter));
				Test->TestFalse(TEXT("Each bullet hole is immediately queryable through Complex collision"), Hits(World, Current, Center, true));
				Pending.Add({FBreachSimpleAsyncTestAccess::InputRevision(Current), RequestedAt});
				++Shots;
				NextShotAt = Now + 0.05;
			}
			if (Shots == 20 && Pending.IsEmpty())
			{
				Latencies.Sort();
				Test->TestEqual(TEXT("All PIE shots were incorporated into published revisions"), Latencies.Num(), 20);
				Test->TestTrue(TEXT("PIE passage delay target is 100ms"), Latencies.Last() <= 100);
				Test->TestTrue(TEXT("Untouched PIE wall still blocks movement"), Hits(World, Current, {42, 35}, false, true));
				Test->AddInfo(FString::Printf(TEXT("PIE 20Hz, 20 irregular cutters, wall 800x540cm: median=%.3fms p95=%.3fms max=%.3fms; elapsed=%.3fs."), Latencies[10], Latencies[18], Latencies.Last(), Now - Started));
				Current->GetOwner()->Destroy();
				return true;
			}
			return false;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimplePIETest, "BreachSystem.Surface.SimplePIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachSimplePIETest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor exists for PIE automation"), GEditor))
	{
		return false;
	}
	// 임시 맵 생성은 현재 에디터 월드를 교체한다. 전용 테스트 프로세스에서만 허용한다.
	if (!FApp::IsUnattended() || !FParse::Param(FCommandLine::Get(), TEXT("BreachDisposablePIE")) || GEditor->PlayWorld)
	{
		AddWarning(TEXT("SimplePIE requires a separate unattended editor with -BreachDisposablePIE and no active play session; the current editor level was left untouched."));
		return true;
	}
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("Unsaved temporary PIE map exists"), Map))
	{
		return false;
	}
	AWorldSettings* WorldSettings = Map->GetWorldSettings();
	if (!TestNotNull(TEXT("Temporary map settings exist"), WorldSettings))
	{
		return false;
	}
	WorldSettings->DefaultGameMode = AGameModeBase::StaticClass();
	TestNotNull(TEXT("PIE cube fixture is preloaded"), LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::SimulateInEditor;
	Params.bAllowOnlineSubsystem = false;
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIEForAutomationCommand(Params));
	ADD_LATENT_AUTOMATION_COMMAND(FExerciseBreachPIE(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
