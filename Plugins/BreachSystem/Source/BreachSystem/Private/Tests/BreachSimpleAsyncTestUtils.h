#pragma once

// 테스트 전용: 실제 게임 스레드 완료 콜백을 처리하며 충돌 질의 전 작업을 기다린다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSimpleProxy.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

struct FBreachSimpleAsyncTestAccess
{
	static int32 PassageCount(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->Model.SimpleCollision.GetLastBuildStats().PassagePieceCount;
	}
	static uint64 InputRevision(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->Model.SimpleCollision.GetRevision();
	}
	static uint64 PublishedRevision(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->SimpleCollisionQueue.CompletedRevision;
	}
	static bool IsPending(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->SimpleCollisionQueue.BuildJob.IsValid() || Surface->SimpleCollisionQueue.bPending;
	}
	static bool HasHoleCache(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->Model.SimpleCollision.HasHoleCache();
	}
	// 구멍 캐시가 설명할 수 없는 고립 조각을 넣어 추적 실패를 재현한다. 표면 상태는 바꾸지 않는다.
	static void BreakHoleCache(UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		UE::Geometry::FPolygon2d Cutter;
		for (const FVector2d Point : {FVector2d(-49, -49), FVector2d(-48, -49), FVector2d(-48, -48), FVector2d(-49, -48)})
		{
			Cutter.AppendVertex(Point);
		}
		UE::Geometry::FPolygon2d Unmatched;
		for (const FVector2d Point : {FVector2d(40, 40), FVector2d(45, 40), FVector2d(45, 45), FVector2d(40, 45)})
		{
			Unmatched.AppendVertex(Point);
		}
		FString Error;
		Surface->Model.SimpleCollision.ApplyCut(Cutter, MakeArrayView(&Unmatched, 1), Error);
	}
	static uint32 CookCount(const UBreachSimpleProxy* Proxy)
	{
		check(Proxy);
		return Proxy->CookRequestCount;
	}
	static bool IsCooking(const UBreachSimpleProxy* Proxy)
	{
		check(Proxy);
		return Proxy->bCooking;
	}
	static void FailNextCook(UBreachSimpleProxy* Proxy)
	{
		check(Proxy);
		Proxy->bFailNextCook = true;
	}
	static void DropNextCookedHull(UBreachSimpleProxy* Proxy)
	{
		check(Proxy);
		Proxy->bDropNextCookedHull = true;
	}
	static bool InputIndicesReady(const UBreachSimpleProxy* Proxy)
	{
		check(Proxy);
		return Proxy->bLastCookHadIndices;
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
