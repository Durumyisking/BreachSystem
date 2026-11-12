#pragma once

// 테스트 전용: 실제 게임 스레드 완료 콜백을 처리하며 충돌 질의 전 작업을 기다린다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

struct FBreachSimpleAsyncTestAccess
{
	static int32 PassageCount(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->SimpleCollision.GetLastBuildStats().PassagePieceCount;
	}
	static uint64 InputRevision(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->SimpleCollision.GetRevision();
	}
	static uint64 PublishedRevision(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->CompletedSimpleRevision;
	}
	static bool IsPending(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->SimpleBuildJob.IsValid() || Surface->bSimpleBuildPending;
	}

	static bool Wait(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		check(IsInGameThread());
		const double Deadline = FPlatformTime::Seconds() + 5;
		while (IsPending(Surface))
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			if (FPlatformTime::Seconds() > Deadline)
			{
				return false;
			}
			FPlatformProcess::SleepNoStats(0.001f);
		}
		return true;
	}
};
#endif
