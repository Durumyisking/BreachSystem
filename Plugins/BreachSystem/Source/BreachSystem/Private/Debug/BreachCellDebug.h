#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"

class FAutoConsoleCommand;
class UBreachableSurfaceComponent;
class ULineBatchComponent;
class UWorld;

/** 모듈이 소유하며 켜진 동안만 표면 셀 격자를 읽고 그린다. */
class FBreachCellDebug
{
public:
	FBreachCellDebug();
	~FBreachCellDebug();

private:
	void Execute(const TArray<FString>& Args);
	void SetEnabled(bool bInEnabled);
	bool Tick(float DeltaSeconds);
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void ClearBatches();

	TUniquePtr<FAutoConsoleCommand> Command;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldCleanupHandle;
	TMap<TWeakObjectPtr<UWorld>, TStrongObjectPtr<ULineBatchComponent>> Batches;
	TSet<TWeakObjectPtr<UBreachableSurfaceComponent>> WarnedSurfaces;
	bool bEnabled = false;
};
