#include "Components/BreachableSurfaceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Surface/BreachCellGrid.h"
#include "UObject/UObjectIterator.h"

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachCellDebugTest, "BreachSystem.Cell.DebugLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachCellDebugTest::RunTest(const FString& Parameters)
{
	IConsoleObject* ConsoleObject = IConsoleManager::Get().FindConsoleObject(TEXT("BreachSystem.ShowCell"));
	if (!TestNotNull(TEXT("Cell console object is registered"), ConsoleObject))
	{
		return false;
	}
	IConsoleCommand* Command = ConsoleObject->AsCommand();
	if (!TestNotNull(TEXT("Cell command is registered by the module"), Command))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}
	AActor* Wall = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Wall exists"), Wall))
	{
		World->DestroyWorld(false);
		return false;
	}
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Surface->RegisterComponent();

	ULineBatchComponent* OtherLines = NewObject<ULineBatchComponent>(GetTransientPackage());
	OtherLines->RegisterComponentWithWorld(World);
	OtherLines->DrawLine(FVector::ZeroVector, FVector(1, 0, 0), FLinearColor::White, 0, 1, -1);

	auto FindCellBatch = [World]() -> ULineBatchComponent*
	{
		for (TObjectIterator<ULineBatchComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsRegistered() && It->GetName().StartsWith(TEXT("BreachCellDebugLines")))
			{
				return *It;
			}
		}
		return nullptr;
	};
	auto Execute = [Command](const TArray<FString>& Args)
	{
		if (!ensureMsgf(Command, TEXT("Cell lifecycle test requires its registered console command.")))
		{
			return;
		}
		Command->Execute(Args, nullptr, *GLog);
	};

	Execute({ TEXT("0") });
	Execute({ TEXT("1") });
	ULineBatchComponent* Batch = FindCellBatch();
	if (TestNotNull(TEXT("Enabled command creates its own batch"), Batch))
	{
		TestTrue(TEXT("Grid has lines"), Batch->BatchedLines.Num() > 0);
		TestTrue(TEXT("Debug component is transient"), Batch->HasAnyFlags(RF_Transient));
		FBreachCellGrid Grid;
		UE::Geometry::FAxisAlignedBox2d Bounds;
		FVector2d Depth;
		if (TestTrue(TEXT("Drawing uses a readable grid"), Surface->GetCellGridForDebug(Grid, Bounds, Depth)))
		{
			// Rendering offset is normal-only; the actual cm grid remains unchanged.
			const double DepthOffset = 0.1 / FMath::Abs(Surface->GetComponentScale().Y);
			Depth.X -= DepthOffset;
			Depth.Y += DepthOffset;
			bool bFront = false;
			bool bBack = false;
			for (const FBatchedLine& Line : Batch->BatchedLines)
			{
				const FVector Start = Surface->GetComponentTransform().InverseTransformPosition(Line.Start);
				const FVector End = Surface->GetComponentTransform().InverseTransformPosition(Line.End);
				bFront |= FMath::IsNearlyEqual(Start.Y, Depth.X);
				bBack |= FMath::IsNearlyEqual(Start.Y, Depth.Y);
				TestEqual(TEXT("Wall depth hides the opposite face grid"), Line.DepthPriority, uint8(0));
				TestTrue(TEXT("Line remains just outside one original wall face"), FMath::IsNearlyEqual(Start.Y, End.Y)
					&& (FMath::IsNearlyEqual(Start.Y, Depth.X) || FMath::IsNearlyEqual(Start.Y, Depth.Y)));
				TestTrue(TEXT("Start is clipped to wall bounds"), Bounds.Contains(FVector2d(Start.X, Start.Z)));
				TestTrue(TEXT("End is clipped to wall bounds"), Bounds.Contains(FVector2d(End.X, End.Z)));
				const FVector2d A = Grid.ToMetric(FVector2d(Start.X, Start.Z));
				const FVector2d B = Grid.ToMetric(FVector2d(End.X, End.Z));
				bool bCellBoundary = false;
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					const double Index = A[Axis] / Grid.GetCellSize();
					bCellBoundary |= FMath::IsNearlyEqual(A[Axis], B[Axis], 1.e-7)
						&& FMath::IsNearlyEqual(Index, FMath::RoundToDouble(Index), 1.e-7);
				}
				TestTrue(TEXT("Drawn line is an exact cell boundary"), bCellBoundary);
			}
			TestTrue(TEXT("Both wall faces are drawn"), bFront && bBack);
		}
	}
	TArray<UDynamicMeshComponent*> Proxies;
	Wall->GetComponents(Proxies);
	TestEqual(TEXT("Displaying cells never creates a destruction proxy"), Proxies.Num(), 0);
	Execute({});
	TestNull(TEXT("No arguments toggles drawing off and removes its batch"), FindCellBatch());
	TestEqual(TEXT("Turning cells off preserves unrelated debug lines"), OtherLines->BatchedLines.Num(), 1);
	Execute({});
	TestNotNull(TEXT("No arguments toggles drawing on"), FindCellBatch());
	Execute({ TEXT("1") });
	TestNotNull(TEXT("Explicit enable is idempotent"), FindCellBatch());
	World->WorldType = EWorldType::EditorPreview;
	Execute({ TEXT("1") });
	TestNull(TEXT("Preview worlds are excluded"), FindCellBatch());
	World->WorldType = EWorldType::Game;
	Execute({ TEXT("1") });
	OtherLines->DestroyComponent();
	World->DestroyWorld(false);
	TestNull(TEXT("World cleanup removes the cell batch"), FindCellBatch());
	Execute({ TEXT("0") });
	return true;
}

#endif
