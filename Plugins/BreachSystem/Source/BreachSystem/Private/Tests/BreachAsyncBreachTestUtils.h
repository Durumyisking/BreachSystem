#pragma once

// 테스트 전용: 비동기 브리칭 작업의 대기·반영 상태를 조회하고, 실제 완료 알림을 처리하며 반영을 기다린다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

struct FBreachAsyncBreachTestAccess
{
	static bool IsPending(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return Surface->BreachJob.IsValid() || Surface->PendingBreachJob.IsValid();
	}

	// 실제 완료 알림을 게임 스레드에서 처리하며 반영을 기다린다.
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

	static bool HasProxy(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return IsValid(Surface->DynamicMeshProxy);
	}

	static bool IsPublished(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		return HasProxy(Surface) && Surface->DynamicMeshProxy->IsRegistered();
	}

	static int32 ProxyTriangles(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		if (!IsPublished(Surface))
		{
			return 0;
		}
		return Surface->DynamicMeshProxy->GetMesh()->TriangleCount();
	}

	static double RemainingArea(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		double Area = 0;
		for (const UE::Geometry::FGeneralPolygon2d& Region : Surface->SurfaceState.RemainingRegions)
		{
			Area += FMath::Abs(Region.SignedArea());
		}
		return Area;
	}

	static int32 RemainingVertices(const UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		int32 Count = 0;
		for (const UE::Geometry::FGeneralPolygon2d& Region : Surface->SurfaceState.RemainingRegions)
		{
			Count += Region.GetOuter().VertexCount();
			for (const UE::Geometry::FPolygon2d& Hole : Region.GetHoles())
			{
				Count += Hole.VertexCount();
			}
		}
		return Count;
	}

	static bool Solid(const UBreachableSurfaceComponent* Surface, const FVector2d& Point)
	{
		check(Surface);
		for (const UE::Geometry::FGeneralPolygon2d& Region : Surface->SurfaceState.RemainingRegions)
		{
			if (Region.Contains(Point))
			{
				return true;
			}
		}
		return false;
	}

	static void Reset(UBreachableSurfaceComponent* Surface)
	{
		check(Surface);
		Surface->ResetSurface();
	}
};
#endif
