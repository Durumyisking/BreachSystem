#include "Components/BreachSimpleCollisionQueue.h"

#include "Components/BreachSimpleProxy.h"
#include "Data/BreachSystemSettings.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"

#include "BreachSystemLog.h"

TRACE_DECLARE_FLOAT_COUNTER(BreachSimpleRequestLatency, TEXT("BreachSimpleCollision | Request to apply (ms)"));
TRACE_DECLARE_FLOAT_COUNTER(BreachSimpleCookLatency, TEXT("BreachSimpleCollision | Cook request to apply (ms)"));

struct FBreachSimpleBuildJob
{
	FBreachSimpleCollision Snapshot;
	FBreachSimpleBuildPolicy Policy;
	FKAggregateGeom Collision;
	FString Error;
	uint64 Generation = 0;
	uint64 Revision = 0;
	double RequestedAt = 0;
	double CookStarted = 0;
	TWeakObjectPtr<UBreachSimpleProxy> Proxy;
	bool bSucceeded = false;
};

FBreachSimpleBuildPolicy FBreachSimpleCollisionQueue::MakePolicy(const FVector& ComponentScale)
{
	check(IsInGameThread());
	FBreachSimpleBuildPolicy Policy;
	Policy.WorldScale = FVector2d(FMath::Abs(ComponentScale.X), FMath::Abs(ComponentScale.Z));
	Policy.AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	Policy.MaxCount = UBreachSystemSettings::GetMaxSimpleConvexCount();
	Policy.PassageTolerance = UBreachSystemSettings::GetSimplePassageToleranceCm();
	return Policy;
}

void FBreachSimpleCollisionQueue::Invalidate()
{
	++Generation;
	// 진행 중인 이전 수명의 작업도 완료까지 보관하여 같은 표면의 작업이 겹치지 않게 한다.
	bPending = false;
	PendingSince = 0;
	CompletedRevision = 0;
}

void FBreachSimpleCollisionQueue::Queue(const FInput& Input)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | Queue build");
	check(IsInGameThread());
	if (!IsValid(Input.Proxy) || !Input.bRegistered || !Input.Collision.HasHoleCache())
	{
		bPending = false;
		PendingSince = 0;
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s cannot queue Simple collision without a valid proxy and hole cache; previous collision is retained."), *Input.SurfaceName);
		return;
	}
	const FBreachSimpleBuildPolicy Policy = MakePolicy(Input.ComponentScale);
	if (!BuildJob.IsValid() && Input.Collision.GetRevision() == CompletedRevision && Policy == CompletedPolicy)
	{
		return;
	}
	if (!bPending)
	{
		PendingSince = FPlatformTime::Seconds();
	}
	bPending = true;
	if (BuildJob.IsValid())
	{
		return;
	}

	auto Job = MakeShared<FBreachSimpleBuildJob, ESPMode::ThreadSafe>();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | Snapshot collision state");
		Job->Snapshot = Input.Collision;
	}
	Job->Policy = Policy;
	Job->Generation = Generation;
	Job->Revision = Input.Collision.GetRevision();
	Job->RequestedAt = PendingSince;
	Job->Proxy = Input.Proxy;
	BuildJob = Job;
	bPending = false;
	PendingSince = 0;

	// 완료 알림은 표면이 살아 있을 때만 그때의 표면 값으로 이 대기열에 돌아온다.
	check(Input.OnBuilt);
	Async(EAsyncExecution::ThreadPool, [OnBuilt = Input.OnBuilt, Job]
	{
		Job->bSucceeded = Job->Snapshot.BuildMeshFromCuts(Job->Policy, Job->Collision, Job->Error);
		AsyncTask(ENamedThreads::GameThread, [OnBuilt, Job]
		{
			OnBuilt(Job);
		});
	});
}

bool FBreachSimpleCollisionQueue::CanPublish(const FInput& Input, const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job) const
{
	check(IsInGameThread());
	check(Job.IsValid());
	if (BuildJob != Job || Job->Generation != Generation)
	{
		return false;
	}
	if (!Input.bWorldAlive || !Input.bRegistered)
	{
		return false;
	}
	if (!IsValid(Input.Proxy) || Job->Proxy.Get() != Input.Proxy || !Input.Proxy->IsRegistered())
	{
		return false;
	}
	return Input.Collision.HasHoleCache() && MakePolicy(Input.ComponentScale) == Job->Policy;
}

void FBreachSimpleCollisionQueue::CompleteBuild(const FInput& Input, const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | Complete build");
	check(IsInGameThread());
	check(Job.IsValid());
	if (BuildJob != Job)
	{
		return;
	}
	if (CanPublish(Input, Job))
	{
		if (Job->bSucceeded)
		{
			const FBreachSimpleBuildStats& Stats = Job->Snapshot.GetLastBuildStats();
			if (Stats.bCollisionChanged)
			{
				UBreachSimpleProxy* Proxy = Job->Proxy.Get();
				check(Proxy);
				check(Input.OnCooked);
				Job->CookStarted = FPlatformTime::Seconds();
				Proxy->CookCollision(Job->Collision, [OnCooked = Input.OnCooked, Job](bool bSucceeded)
				{
					OnCooked(Job, bSucceeded);
				});
				return;
			}
		}
	}
	CompleteCook(Input, Job, Job->bSucceeded);
}

void FBreachSimpleCollisionQueue::CompleteCook(const FInput& Input, const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job, bool bCookSucceeded)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | Complete cook and publish");
	check(IsInGameThread());
	check(Job.IsValid());
	if (BuildJob != Job)
	{
		return;
	}
	UBreachSimpleProxy* Proxy = Job->Proxy.Get();
	const bool bCanPublish = CanPublish(Input, Job);
	if (bCanPublish)
	{
		if (Job->bSucceeded && bCookSucceeded)
		{
			check(Proxy);
			const double ApplyStarted = FPlatformTime::Seconds();
			const FBreachSimpleBuildStats& Stats = Job->Snapshot.GetLastBuildStats();
			bool bApplied = true;
			if (Stats.bCollisionChanged)
			{
				bApplied = Proxy->ApplyCollision();
			}
			if (bApplied)
			{
				Input.Collision.CommitBuild(Job->Snapshot);
				CompletedRevision = Job->Revision;
				CompletedPolicy = Job->Policy;
				const double ApplyMs = (FPlatformTime::Seconds() - ApplyStarted) * 1000;
				const double LatencyMs = (FPlatformTime::Seconds() - Job->RequestedAt) * 1000;
				const double CookLatencyMs = Job->CookStarted > 0 ? (ApplyStarted - Job->CookStarted) * 1000 : 0;
				TRACE_COUNTER_SET(BreachSimpleRequestLatency, LatencyMs);
				if (Job->CookStarted > 0)
				{
					TRACE_COUNTER_SET(BreachSimpleCookLatency, CookLatencyMs);
				}
				UE_LOG(LogBreachableSurface, Verbose, TEXT("[BreachSimpleAsync] %s revision=%llu geometry=%.3fms cookLatency=%.3fms apply=%.3fms latency=%.3fms prepared=%d reused=%d changed=%d"), *Input.SurfaceName, Job->Revision, Stats.GeometryMs, CookLatencyMs, ApplyMs, LatencyMs, Stats.PreparedOpenings, Stats.ReusedBodies, Stats.bCollisionChanged);
				if (Stats.FrozenOpenings > 0)
				{
					UE_LOG(LogBreachableSurface, Warning, TEXT("%s updated its Simple collision but kept the previous passages of %d opening(s) whose calculation failed: %s"), *Input.SurfaceName, Stats.FrozenOpenings, *Job->Error);
				}
			}
		}
		else
		{
			if (Job->bSucceeded)
			{
				Job->Error = TEXT("Simple physics cooking failed or produced incomplete geometry.");
			}
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous Simple collision after calculation or cooking failed: %s"), *Input.SurfaceName, *Job->Error);
		}
	}
	if (IsValid(Proxy))
	{
		Proxy->DiscardCollision();
	}

	// 설정 변경도 연사와 같은 대기 요청 하나로 병합한다.
	if (Job->Generation == Generation && Input.bRegistered)
	{
		if (!(MakePolicy(Input.ComponentScale) == Job->Policy))
		{
			if (!bPending)
			{
				PendingSince = FPlatformTime::Seconds();
			}
			bPending = true;
		}
	}
	BuildJob.Reset();
	if (bPending && Input.bRegistered)
	{
		Queue(Input);
	}
}
