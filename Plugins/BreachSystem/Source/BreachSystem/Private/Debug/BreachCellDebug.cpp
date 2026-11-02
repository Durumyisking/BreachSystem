#include "Debug/BreachCellDebug.h"

#include "BreachSystemLog.h"
#include "Components/BreachableSurfaceComponent.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"
#include "SceneManagement.h"
#include "Surface/BreachCellGrid.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr int32 MaxLinesPerRefresh = 20000;

	bool CanDrawWorld(const UWorld* World)
	{
		if (!IsValid(World))
		{
			return false;
		}
		if (World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
		{
			return false;
		}
		return World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::PIE
			|| World->WorldType == EWorldType::Game;
	}

	// cm 격자선을 로컬로 되돌린 뒤 원본 사각형에 자른다. 비직교 UV에서도 경계를 넘지 않는다.
	bool ClipToBounds(FVector2d& Start, FVector2d& End, const UE::Geometry::FAxisAlignedBox2d& Bounds)
	{
		const FVector2d Direction = End - Start;
		double Enter = 0.0;
		double Exit = 1.0;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (FMath::Abs(Direction[Axis]) < 1.e-12)
			{
				if (Start[Axis] < Bounds.Min[Axis] || Start[Axis] > Bounds.Max[Axis])
				{
					return false;
				}
				continue;
			}
			double Near = (Bounds.Min[Axis] - Start[Axis]) / Direction[Axis];
			double Far = (Bounds.Max[Axis] - Start[Axis]) / Direction[Axis];
			if (Near > Far)
			{
				Swap(Near, Far);
			}
			Enter = FMath::Max(Enter, Near);
			Exit = FMath::Min(Exit, Far);
			if (Enter >= Exit)
			{
				return false;
			}
		}
		End = Start + Direction * Exit;
		Start += Direction * Enter;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			Start[Axis] = FMath::Clamp(Start[Axis], Bounds.Min[Axis], Bounds.Max[Axis]);
			End[Axis] = FMath::Clamp(End[Axis], Bounds.Min[Axis], Bounds.Max[Axis]);
		}
		return (End - Start).SquaredLength() > 1.e-16;
	}

	bool BuildLines(const UBreachableSurfaceComponent* Surface, int32 Budget, TArray<FBatchedLine>& OutLines)
	{
		if (!IsValid(Surface))
		{
			return false;
		}
		FBreachCellGrid Grid;
		UE::Geometry::FAxisAlignedBox2d Bounds;
		FVector2d Depth;
		if (!Surface->GetCellGridForDebug(Grid, Bounds, Depth))
		{
			return false;
		}
		if (!Grid.IsValid())
		{
			return false;
		}

		UE::Geometry::FAxisAlignedBox2d MetricBounds;
		MetricBounds.Contain(Grid.ToMetric(Bounds.Min));
		MetricBounds.Contain(Grid.ToMetric(FVector2d(Bounds.Max.X, Bounds.Min.Y)));
		MetricBounds.Contain(Grid.ToMetric(Bounds.Max));
		MetricBounds.Contain(Grid.ToMetric(FVector2d(Bounds.Min.X, Bounds.Max.Y)));
		const double Size = Grid.GetCellSize();
		FVector2d First;
		FVector2d Last;
		double LineCount = 0;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			First[Axis] = FMath::CeilToDouble(MetricBounds.Min[Axis] / Size);
			Last[Axis] = FMath::FloorToDouble(MetricBounds.Max[Axis] / Size);
			if (!FMath::IsFinite(First[Axis]) || !FMath::IsFinite(Last[Axis])
				|| First[Axis] < MIN_int32 || Last[Axis] > static_cast<double>(MAX_int32) + 1.0)
			{
				return false;
			}
			LineCount += FMath::Max(0.0, Last[Axis] - First[Axis] + 1.0) * 2.0;
		}
		if (LineCount > Budget)
		{
			return false;
		}

		OutLines.Reserve(static_cast<int32>(LineCount));
		const FTransform Transform = Surface->GetComponentTransform();
		const double DepthScale = FMath::Abs(Transform.GetScale3D().Y);
		if (!FMath::IsFinite(DepthScale) || DepthScale <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		// 셀 평면 좌표는 유지한다. 표면 밖으로 0.1 cm만 띄워 깜빡임과 뒷면 선의 겹침을 막는다.
		const double DepthOffset = 0.1 / DepthScale;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			for (int64 Index = static_cast<int64>(First[Axis]); Index <= static_cast<int64>(Last[Axis]); ++Index)
			{
				FVector2d MetricStart = MetricBounds.Min;
				FVector2d MetricEnd = MetricBounds.Max;
				MetricStart[Axis] = MetricEnd[Axis] = static_cast<double>(Index) * Size;
				FVector2d Start = Grid.ToLocal(MetricStart);
				FVector2d End = Grid.ToLocal(MetricEnd);
				if (!ClipToBounds(Start, End, Bounds))
				{
					continue;
				}
				for (int32 Face = 0; Face < 2; ++Face)
				{
					const double DrawDepth = Depth[Face] + (Face == 0 ? -DepthOffset : DepthOffset);
					OutLines.Emplace(Transform.TransformPosition(FVector(Start.X, DrawDepth, Start.Y)),
						Transform.TransformPosition(FVector(End.X, DrawDepth, End.Y)),
						FLinearColor(0.0f, 0.75f, 1.0f), -1.0f, 1.0f, SDPG_World);
				}
			}
		}
		return true;
	}
}

FBreachCellDebug::FBreachCellDebug()
{
	Command = MakeUnique<FAutoConsoleCommand>(TEXT("BreachSystem.ShowCell"),
		TEXT("Toggle physical breach cells. Optional argument: 1 on, 0 off. Maximum 20000 lines per refresh; oversized surfaces are skipped."),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FBreachCellDebug::Execute), ECVF_Cheat);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FBreachCellDebug::OnWorldCleanup);
}

FBreachCellDebug::~FBreachCellDebug()
{
	Command.Reset();
	SetEnabled(false);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
}

void FBreachCellDebug::Execute(const TArray<FString>& Args)
{
	if (Args.IsEmpty())
	{
		SetEnabled(!bEnabled);
		return;
	}
	if (Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
	{
		SWARN(TEXT("Usage: BreachSystem.ShowCell [0|1]; no argument toggles drawing."));
		return;
	}
	SetEnabled(Args[0] == TEXT("1"));
}

void FBreachCellDebug::SetEnabled(bool bInEnabled)
{
	if (bInEnabled && IsRunningDedicatedServer())
	{
		SWARN(TEXT("ShowCell is unavailable on a dedicated server."));
		return;
	}
	bEnabled = bInEnabled;
	if (!bEnabled)
	{
		FTSTicker::RemoveTicker(TickHandle);
		TickHandle.Reset();
		ClearBatches();
		WarnedSurfaces.Reset();
		return;
	}
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FBreachCellDebug::Tick), 0.1f);
	}
	Tick(0.0f);
}

bool FBreachCellDebug::Tick(float DeltaSeconds)
{
	if (!bEnabled)
	{
		return false;
	}
	for (auto It = Batches.CreateIterator(); It; ++It)
	{
		ULineBatchComponent* Batch = It.Value().Get();
		if (!CanDrawWorld(It.Key().Get()))
		{
			if (IsValid(Batch))
			{
				Batch->DestroyComponent();
			}
			It.RemoveCurrent();
			continue;
		}
		if (IsValid(Batch))
		{
			Batch->Flush();
		}
	}
	for (auto It = WarnedSurfaces.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}

	int32 RemainingLines = MaxLinesPerRefresh;
	for (TObjectIterator<UBreachableSurfaceComponent> It; It; ++It)
	{
		UBreachableSurfaceComponent* Surface = *It;
		if (!IsValid(Surface))
		{
			continue;
		}
		if (Surface->IsTemplate() || !Surface->IsRegistered())
		{
			continue;
		}
		UWorld* World = Surface->GetWorld();
		if (!CanDrawWorld(World))
		{
			continue;
		}
		TArray<FBatchedLine> Lines;
		if (!BuildLines(Surface, RemainingLines, Lines))
		{
			if (!WarnedSurfaces.Contains(Surface))
			{
				SWARN(TEXT("ShowCell skipped %s: invalid cell geometry or remaining line budget (%d of %d). Cell size was not changed."),
					*Surface->GetPathName(), RemainingLines, MaxLinesPerRefresh);
				WarnedSurfaces.Add(Surface);
			}
			continue;
		}
		WarnedSurfaces.Remove(Surface);
		if (Lines.IsEmpty())
		{
			continue;
		}
		TStrongObjectPtr<ULineBatchComponent>& Batch = Batches.FindOrAdd(World);
		if (!Batch.IsValid())
		{
			Batch.Reset(NewObject<ULineBatchComponent>(GetTransientPackage(),
				MakeUniqueObjectName(GetTransientPackage(), ULineBatchComponent::StaticClass(), TEXT("BreachCellDebugLines")), RF_Transient));
			Batch->PrimaryComponentTick.bCanEverTick = false;
			Batch->RegisterComponentWithWorld(World);
		}
		Batch->DrawLines(Lines);
		RemainingLines -= Lines.Num();
	}
	return true;
}

void FBreachCellDebug::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!World)
	{
		return;
	}
	if (TStrongObjectPtr<ULineBatchComponent>* Batch = Batches.Find(World))
	{
		if (IsValid(Batch->Get()))
		{
			(*Batch)->DestroyComponent();
		}
		Batches.Remove(World);
	}
}

void FBreachCellDebug::ClearBatches()
{
	for (auto& Pair : Batches)
	{
		if (IsValid(Pair.Value.Get()))
		{
			Pair.Value->DestroyComponent();
		}
	}
	Batches.Reset();
}
