// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachSimpleProxy.h"
#include "Components/BreachDecorationComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/BreachSurfaceSource.h"
#include "Subsystems/BreachDebrisSubsystem.h"
#include "Data/BreachSurfaceMaterial.h"
#include "Data/BreachSystemSettings.h"
#include "GameFramework/Actor.h"
#include "Engine/CollisionProfile.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/DamageEvents.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Async/Async.h"
#include "Tasks/Task.h"
#include "Engine/World.h"

#include "BreachSystemLog.h"

#include "Geometry/BreachSurfaceGeometry.h"
#include "Geometry/BreachSurfaceWork.h"

#include "PhysicsEngine/BodySetup.h"

/** 표면 하나의 브리칭 계산 한 번. 실행 중 들어온 피격은 다음 작업에 모인다. */
struct FBreachJob
{
	FBreachSurfaceWork Work;
	TArray<FBreachHitInput> Hits;
	/** 첫 피격이 Proxy를 처음 만들었다. 아무것도 자르지 못하면 표면을 되돌린다. */
	bool bCreatedProxy = false;
	bool bAsync = false;
	uint64 Generation = 0;
	UE::Tasks::FTask Task;
};

UBreachableSurfaceComponent::UBreachableSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

#if WITH_EDITOR
void UBreachableSurfaceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	DebugSourceMesh.Reset();
	RefreshDisplayMaterial();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UBreachableSurfaceComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// 작업 스레드가 이 표면이 붙잡은 재질을 읽고 있을 수 있으므로 계산이 끝날 때까지 기다린다.
	InvalidateBreachJobs(true);
	SimpleCollisionQueue.Invalidate();
	if (IsValid(Decorations))
	{
		Decorations->DestroyComponent();
		Decorations = nullptr;
	}
	if (IsValid(SimpleProxy))
	{
		SimpleProxy->SetSurface(nullptr);
		SimpleProxy->DestroyComponent();
		SimpleProxy = nullptr;
	}
	if (IsValid(SurfaceProxy))
	{
		SurfaceProxy->SetSurface(nullptr);
		SurfaceProxy->DestroyComponent();
		SurfaceProxy = nullptr;
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

//void UBreachableSurfaceComponent::ReceiveComponentDamage_Implementation(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
//{
//	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
//	HandleBreachDamage(this, DamageEvent, DamageCauser);
//}
void UBreachableSurfaceComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	HandleBreachDamage(this, DamageEvent, DamageCauser, DamageAmount);
}

bool UBreachableSurfaceComponent::CreateSurfaceProxy(UE::Geometry::FDynamicMesh3& OutMesh)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Initialize collision proxies");
	FPlanarUVMapping InitialUVs[2];
	if (!BreachSurfaceSource::ReadSurface(GetStaticMesh(), OutMesh, InitialUVs))
	{
		return false;
	}
	const UE::Geometry::FAxisAlignedBox3d MeshBounds = OutMesh.GetBounds();
	FBreachSurfaceState InitialState;
	InitialState.Size.Width = MeshBounds.Max.X - MeshBounds.Min.X;
	InitialState.Size.Height = MeshBounds.Max.Z - MeshBounds.Min.Z;

	Layout.SurfaceUVs[0] = InitialUVs[0];
	Layout.SurfaceUVs[1] = InitialUVs[1];

	UE::Geometry::FPolygon2d Outer;
	Outer.AppendVertex(FVector2d(MeshBounds.Min.X, MeshBounds.Min.Z));
	Outer.AppendVertex(FVector2d(MeshBounds.Max.X, MeshBounds.Min.Z));
	Outer.AppendVertex(FVector2d(MeshBounds.Max.X, MeshBounds.Max.Z));
	Outer.AppendVertex(FVector2d(MeshBounds.Min.X, MeshBounds.Max.Z));
	InitialState.RemainingRegions.Add(UE::Geometry::FGeneralPolygon2d(Outer));
	OriginalOuter = Outer;
	Layout.OriginalBounds = OriginalOuter.Bounds();
	Layout.OriginalDepthRange = FVector2d(MeshBounds.Min.Y, MeshBounds.Max.Y);
	SimpleCollisionQueue.Invalidate();
	Model.SimpleCollision.Initialize(OriginalOuter, Layout.OriginalDepthRange);

	// 6. 복사한 삼각형의 머티리얼 ID와 대응하도록 원본의 슬롯 순서를 유지한다.
	TArray<UMaterialInterface*> Materials;
	for (int32 Index = 0; Index < GetNumMaterials(); ++Index)
	{
		Materials.Add(GetMaterial(Index));
	}

	// 7. SurfaceProxy를 생성하고 원본과 같은 위치에 부착한다.
	AActor* Owner = GetOwner();
	if (!ensureMsgf(Owner, TEXT("Breach surface requires an owner to create its proxy.")))
	{
		return false;
	}

	// 생성 및 메시 설정 가져오기
	SurfaceProxy = NewObject<UBreachSurfaceProxy>(Owner);
	SurfaceProxy->SetSurface(this);
	SurfaceProxy->SetupAttachment(this);
	SurfaceProxy->SetRelativeTransform(FTransform::Identity);
	SurfaceProxy->SetMobility(GetMobility());
	SurfaceProxy->SetCastShadow(CastShadow);
	SurfaceProxy->SetCollisionObjectType(GetCollisionObjectType());
	SurfaceProxy->SetCollisionResponseToChannels(GetCollisionResponseToChannels());
	SurfaceProxy->ConfigureMaterialSet(Materials);
	SurfaceProxy->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);

	// 두 프록시가 각자 형상만 보관하고 질의의 Simple/Complex 구분은 유지한다.
	SurfaceProxy->bEnableComplexCollision = true;
	SurfaceProxy->CollisionType = CTF_UseSimpleAndComplex;
	SurfaceProxy->BodyInstance.bAutoWeld = false;

	SimpleProxy = NewObject<UBreachSimpleProxy>(Owner);
	SimpleProxy->SetSurface(this);
	SimpleProxy->SetupAttachment(this);
	SimpleProxy->SetRelativeTransform(FTransform::Identity);
	SimpleProxy->SetMobility(GetMobility());
	SimpleProxy->SetCollisionObjectType(GetCollisionObjectType());
	SimpleProxy->SetCollisionResponseToChannels(GetCollisionResponseToChannels());
	const FBox InitialBounds(FVector(Layout.OriginalBounds.Min.X, Layout.OriginalDepthRange.X, Layout.OriginalBounds.Min.Y), FVector(Layout.OriginalBounds.Max.X, Layout.OriginalDepthRange.Y, Layout.OriginalBounds.Max.Y));
	SimpleProxy->InitializeCollision(InitialBounds);
	UBodySetup* InitialBody = SimpleProxy->GetBodySetup();
	check(InitialBody);
	Model.SimpleCollision.InitializeCollision(FBreachSimpleCollisionQueue::MakePolicy(GetComponentScale()), InitialBody->AggGeom);

	Model.SurfaceState = MoveTemp(InitialState);

	// 한 표면의 수명 동안 같은 전역 규격과 -Y 면 UV 기준을 사용한다.
	Layout.CapturedCellSize = UBreachSystemSettings::GetCellSizeCm();
	const FVector Scale = GetComponentScale();
	Model.CachedMeshScaleXZ = FVector2d(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	FString GridError;
	if (!Model.CellGrid.Configure(Layout.SurfaceUVs[0].Offset, Layout.SurfaceUVs[0].PerX, Layout.SurfaceUVs[0].PerZ,
		Model.CachedMeshScaleXZ, Layout.CapturedCellSize, GridError))
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s cannot accumulate cell damage: %s"), *GetName(), *GridError);
	}


	// 실제 표시와 충돌 전환은 최종 메시가 준비된 뒤 한다.
	OriginalCollisionEnabled = GetCollisionEnabled();
	bOriginalVisible = IsVisible();
	bOriginalHiddenInGame = bHiddenInGame;

	return true;
}

FBreachSimpleCollisionQueue::FInput UBreachableSurfaceComponent::MakeSimpleCollisionInput()
{
	check(IsInGameThread());
	FBreachSimpleCollisionQueue::FInput Input{Model.SimpleCollision};
	Input.Proxy = SimpleProxy;
	Input.ComponentScale = GetComponentScale();
	Input.bRegistered = IsRegistered();
	const UWorld* World = GetWorld();
	if (World)
	{
		Input.bWorldAlive = !World->bIsTearingDown;
	}
	Input.SurfaceName = GetName();

	// 완료 알림은 게임 스레드에서 온다. 그 사이 표면이 사라졌으면 결과를 버리고, 살아 있으면 그때의 값으로 대기열을 이어 간다.
	const TWeakObjectPtr<UBreachableSurfaceComponent> WeakThis(this);
	Input.OnBuilt = [WeakThis](const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job)
	{
		UBreachableSurfaceComponent* Surface = WeakThis.Get();
		if (IsValid(Surface))
		{
			Surface->SimpleCollisionQueue.CompleteBuild(Surface->MakeSimpleCollisionInput(), Job);
		}
	};
	Input.OnCooked = [WeakThis](const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job, bool bCookSucceeded)
	{
		UBreachableSurfaceComponent* Surface = WeakThis.Get();
		if (IsValid(Surface))
		{
			Surface->SimpleCollisionQueue.CompleteCook(Surface->MakeSimpleCollisionInput(), Job, bCookSucceeded);
		}
	};
	return Input;
}

void UBreachableSurfaceComponent::OnUnregister()
{
	InvalidateBreachJobs(true);
	SimpleCollisionQueue.Invalidate();
	if (IsValid(Decorations))
	{
		Decorations->UnregisterComponent();
	}
	if (IsValid(SimpleProxy))
	{
		SimpleProxy->UnregisterComponent();
	}
	if (IsValid(SurfaceProxy))
	{
		SurfaceProxy->UnregisterComponent();
	}
	Super::OnUnregister();
}

void UBreachableSurfaceComponent::OnRegister()
{
	Super::OnRegister();
	RefreshDisplayMaterial();
	if (IsValid(Decorations))
	{
		Decorations->RegisterComponent();
	}
	if (IsValid(SurfaceProxy) && IsValid(SimpleProxy))
	{
		SurfaceProxy->RegisterComponent();
		SimpleProxy->RegisterComponent();
		SimpleCollisionQueue.Queue(MakeSimpleCollisionInput());
	}
}

bool UBreachableSurfaceComponent::UpdateMesh(UE::Geometry::FDynamicMesh3&& Mesh, EBreachSurfaceSide Side)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Publish wall update");
	if (!ensureMsgf(IsValid(SurfaceProxy) && IsValid(SimpleProxy), TEXT("%s requires both proxies to update its mesh."), *GetName()))
	{
		return false;
	}

	// 기본은 피격한 프레임에 Complex까지 굽는다. 설정으로 비동기를 고르면 완료될 때까지 이전 충돌이 남는다.
	// 첫 전환은 원본 충돌을 끄는 시점이므로 항상 동기로 굽는다. 비동기로 두면 그 사이 총알이 벽 전체를 통과한다.
	bool bAsyncCook = false;
	if (SurfaceProxy->IsRegistered())
	{
		bAsyncCook = UBreachSystemSettings::IsComplexCollisionAsync();
	}
	SurfaceProxy->bUseAsyncCooking = bAsyncCook;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Set proxy mesh");
		SurfaceProxy->SetMesh(MoveTemp(Mesh));
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | ComplexCollisionCook");
		TRACE_CPUPROFILER_EVENT_SCOPE_STR_CONDITIONAL("PlanarBreach | Complex sync cook and apply", !bAsyncCook);
		TRACE_CPUPROFILER_EVENT_SCOPE_STR_CONDITIONAL("PlanarBreach | Complex queue async cook", bAsyncCook);
		SurfaceProxy->UpdateCollision(false);
	}

	if (!SurfaceProxy->IsRegistered())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Register collision proxies");
		SimpleProxy->RegisterComponent();
		SurfaceProxy->RegisterComponent();
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetVisibility(false, false);
		SimpleProxy->SetCollisionEnabled(OriginalCollisionEnabled);
		SurfaceProxy->SetCollisionEnabled(OriginalCollisionEnabled);
		SurfaceProxy->SetVisibility(bOriginalVisible, false);
		SurfaceProxy->SetHiddenInGame(bOriginalHiddenInGame, false);
	}

	SimpleCollisionQueue.Queue(MakeSimpleCollisionInput());
	if (!PendingDecorationCuts.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Update decorations");
		const bool bHasMaterial = IsValid(SurfaceMaterial);
		bool bConfigured = false;
		if (bHasMaterial)
		{
			bConfigured = IsValid(SurfaceMaterial->CutDecoration.Material) || IsValid(SurfaceMaterial->FeatureDecoration.Material);
		}
		if (bConfigured && !IsValid(Decorations))
		{
			AActor* Owner = GetOwner();
			if (ensureMsgf(IsValid(Owner), TEXT("%s requires an owner for Deco."), *GetName()))
			{
				Decorations = NewObject<UBreachDecorationComponent>(Owner);
				Decorations->RegisterComponent();
			}
		}
		if (IsValid(Decorations))
		{
			Decorations->Update(this, SurfaceMaterial, Model.SimpleCollision, Layout.OriginalDepthRange, PendingDecorationCuts, PendingDecorationRemoved, Side, bOriginalVisible, bOriginalHiddenInGame);
		}
	}
	PendingDecorationCuts.Reset();
	PendingDecorationRemoved.Reset();
	return true;
}

bool UBreachableSurfaceComponent::ResetSurface()
{
	InvalidateBreachJobs(false);
	SimpleCollisionQueue.Invalidate();
	PendingDecorationCuts.Reset();
	PendingDecorationRemoved.Reset();
	if (IsValid(Decorations))
	{
		Decorations->DestroyComponent();
		Decorations = nullptr;
	}
	// 처리: 누적 기록을 비우고 Proxy가 대신하던 원본 표시·충돌을 복구한다.
	Model.DamageAccumulator.Reset();
	Model.CachedMeshScaleXZ = FVector2d::ZeroVector;
	Model.CellGrid = FBreachCellGrid();
	Layout.CapturedCellSize = 0;
#if WITH_EDITOR
	DebugSourceMesh.Reset();
#endif

	if (IsValid(SimpleProxy))
	{
		SimpleProxy->SetSurface(nullptr);
		SimpleProxy->DestroyComponent();
		SimpleProxy = nullptr;
	}
	if (IsValid(SurfaceProxy))
	{
		SurfaceProxy->SetSurface(nullptr);
		SurfaceProxy->DestroyComponent();
		SurfaceProxy = nullptr;
		SetCollisionEnabled(OriginalCollisionEnabled);
		SetVisibility(bOriginalVisible, false);
		SetHiddenInGame(bOriginalHiddenInGame, false);
	}

	Model.SurfaceState = FBreachSurfaceState();
	Model.SimpleCollision = FBreachSimpleCollision();
	Layout.SurfaceUVs[0] = FPlanarUVMapping();
	Layout.SurfaceUVs[1] = FPlanarUVMapping();

	return true;
}

void UBreachableSurfaceComponent::HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser, float DamageAmount)
{
	// 검증: 포인트 피해만 처리하며, 커터를 제공하는 BreachSource가 필요하다.
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Total");

		// 검증: 이 표면의 피격이며 피해량이 유효해야 한다.
		if (!IsValid(HitComponent) || (HitComponent != this && HitComponent != SurfaceProxy && HitComponent != SimpleProxy))
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s ignored accumulation damage for an unrelated component."), *GetName());
			return;
		}

		if (!FMath::IsFinite(DamageAmount) || DamageAmount <= 0)
		{
			return;
		}

		if (!IsValid(DamageCauser))
		{
			return;
		}

		UBreachSourceComponent* Source = DamageCauser->FindComponentByClass<UBreachSourceComponent>();
		if (!IsValid(Source))
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s has no BreachSourceComponent."), *DamageCauser->GetName());
			return;
		}

		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);

		// 누적 규칙은 Material에서만 읽는다. 미지정 표면은 직접 절단만 수행한다.
		const UBreachSurfaceMaterial* Material = SurfaceMaterial.Get();
		bool bAccumulate = false;
		if (IsValid(Material))
		{
			bAccumulate = Material->Accumulation.bEnabled && Source->BreachDamage != 0;
		}
		else
		{
			UE_LOG(LogBreachableSurface, Verbose, TEXT("%s has no SurfaceMaterial; using direct cutting only. Assign a SurfaceMaterial asset to enable accumulation."), *GetName());
		}

		// 앞의 ValidCheck이 통과하면 프록시를 생성한다. 원본 메시를 읽으므로 피격한 프레임에 한다.
		const bool bHadProxy = IsValid(SurfaceProxy);
		if (!bHadProxy)
		{
			FDynamicMesh3 SourceMesh;
			const bool bInitialized = CreateSurfaceProxy(SourceMesh);
			if (!bInitialized)
			{
				return;
			}
		}

		const FVector LocalImpact = GetComponentTransform().InverseTransformPosition(PointDamage.HitInfo.ImpactPoint);
		const double FrontDistance = FMath::Abs(LocalImpact.Y - Layout.OriginalDepthRange.X);
		const double BackDistance = FMath::Abs(LocalImpact.Y - Layout.OriginalDepthRange.Y);
		const EBreachSurfaceSide Side = FrontDistance <= BackDistance ? EBreachSurfaceSide::Front : EBreachSurfaceSide::Back;

		// 입력: 계산이 컴포넌트와 피해 원인을 읽지 않도록 피격 순간의 값을 담는다.
		FBreachHitInput Hit;
		Hit.HitInfo = PointDamage.HitInfo;
		Hit.LocalPoint = FVector2d(LocalImpact.X, LocalImpact.Z);
		Hit.Side = Side;
		Hit.ComponentScale = GetComponentScale();
		Hit.Material = Material;
		Hit.BreachDamage = Source->BreachDamage;
		Hit.bAccumulate = bAccumulate;
		// 누적 파손이 없을 때 쓸 총알 커터와, 그 윤곽을 자를 수 없을 때 다시 시도할 기본 커터
		Hit.bHasCutter = Source->ResolveCutterPolygon(GetComponentTransform(), PointDamage.HitInfo.ImpactPoint, Hit.Cutter);
		if (Hit.bHasCutter)
		{
			Hit.bHasBaseCutter = Source->ResolveCutterPolygon(GetComponentTransform(), PointDamage.HitInfo.ImpactPoint, Hit.BaseCutter, false);
		}

		EnqueueBreachHit(MoveTemp(Hit), !bHadProxy);
	}
}

void UBreachableSurfaceComponent::EnqueueBreachHit(FBreachHitInput&& Hit, bool bCreatedProxy)
{
	check(IsInGameThread());
	if (!PendingBreachJob.IsValid())
	{
		PendingBreachJob = MakeShared<FBreachJob, ESPMode::ThreadSafe>();
	}
	PendingBreachJob->bCreatedProxy |= bCreatedProxy;
	if (IsValid(Hit.Material))
	{
		BreachJobMaterials.AddUnique(const_cast<UBreachSurfaceMaterial*>(Hit.Material));
	}
	PendingBreachJob->Hits.Add(MoveTemp(Hit));
	StartBreachJob();
}

void UBreachableSurfaceComponent::StartBreachJob(bool bForceSync)
{
	check(IsInGameThread());
	if (BreachJob.IsValid() || !PendingBreachJob.IsValid())
	{
		return;
	}
	const TSharedPtr<FBreachJob, ESPMode::ThreadSafe> Job = MoveTemp(PendingBreachJob);
	PendingBreachJob.Reset();

	// 기다리는 동안 앞선 피격이 표면을 되돌렸으면, 그 뒤 피격이 하던 것처럼 Proxy를 다시 만든다.
	if (!IsValid(SurfaceProxy))
	{
		FDynamicMesh3 SourceMesh;
		if (!CreateSurfaceProxy(SourceMesh))
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s dropped %d queued hit(s) because its surface could not be prepared again."), *GetName(), Job->Hits.Num());
			return;
		}
		Job->bCreatedProxy = true;
	}

	Job->bAsync = !bForceSync && UBreachSystemSettings::IsBreachAsync();
	Job->Generation = BreachGeneration;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Snapshot breach state");
		Job->Work = TakeWork(Job->bAsync);
		Job->Work.bCreatedProxy = Job->bCreatedProxy;
		if (IsValid(Decorations))
		{
			Decorations->PrepareWork(Job->Work.Decoration);
			Job->Work.bDecorationExists = true;
		}
	}
	BreachJob = Job;

	if (!Job->bAsync)
	{
		Job->Work.ProcessHits(Job->Hits);
		CommitBreachJob(Job);
		return;
	}

	// 계산은 작업 스레드에서, 반영은 게임 스레드에서 한다. 표면이 사라졌으면 결과를 버린다.
	const TWeakObjectPtr<UBreachableSurfaceComponent> WeakThis(this);
	Job->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [WeakThis, Job]
	{
		Job->Work.ProcessHits(Job->Hits);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Job]
		{
			UBreachableSurfaceComponent* Surface = WeakThis.Get();
			if (IsValid(Surface))
			{
				Surface->CommitBreachJob(Job);
			}
		});
	});
}

void UBreachableSurfaceComponent::CommitBreachJob(const TSharedPtr<FBreachJob, ESPMode::ThreadSafe>& Job)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Commit");
	check(IsInGameThread());
	check(Job.IsValid());
	if (BreachJob != Job)
	{
		return;
	}
	BreachJob.Reset();

	// 표면을 되돌리거나 등록 해제한 뒤 도착한 결과는 버린다. 그 뒤 들어온 피격은 새 상태에서 시작한다.
	if (Job->Generation != BreachGeneration)
	{
		if (!PendingBreachJob.IsValid())
		{
			BreachJobMaterials.Reset();
		}
		StartBreachJob();
		return;
	}

	FBreachSurfaceWork& Work = Job->Work;
	bool bAnyChanged = false;
	bool bAllFailed = true;
	for (const FBreachHitResult& Result : Work.Results)
	{
		bAnyChanged |= Result.bChanged;
		bAllFailed &= Result.bFailed;
	}
	RestoreWork(Work, Job->bAsync);

	if (bAnyChanged)
	{
		// 피격 순서대로 처리한 최종 메시와 Deco를 한 번 반영한 뒤, 피격마다 파편과 이벤트를 순서대로 알린다.
		PendingDecorationCuts.Reset();
		PendingDecorationRemoved.Reset();
		const bool bUpdated = UpdateMesh(MoveTemp(Work.Mesh));
		if (bUpdated)
		{
			if (Work.bDecorationExists)
			{
				ApplyDecorationWork(Work.Decoration);
			}

			for (int32 Index = 0; Index < Work.Results.Num(); ++Index)
			{
				const FBreachHitResult& Result = Work.Results[Index];
				const FBreachHitInput& Hit = Job->Hits[Index];
				if (!Result.bChanged)
				{
					continue;
				}

				SpawnHitDebris(Hit, Result);

				// 최종 메시와 충돌이 반영된 뒤 피격을 알린다.
				TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Breached event");
				OnBreachedDelegate.Broadcast(Hit.HitInfo, SurfaceProxy);
			}
		}
	}
	else if (bAllFailed && Work.bCreatedProxy && !Work.bPublished)
	{
		// 처음 만든 Proxy에서 아무것도 자르지 못했다. 기다리는 피격과 그 재질은 남겨 두고 표면만 원래대로 되돌린다.
		TSharedPtr<FBreachJob, ESPMode::ThreadSafe> Pending = MoveTemp(PendingBreachJob);
		TArray<TObjectPtr<UBreachSurfaceMaterial>> Materials = BreachJobMaterials;
		ResetSurface();
		PendingBreachJob = MoveTemp(Pending);
		BreachJobMaterials = MoveTemp(Materials);
	}

	if (!BreachJob.IsValid() && !PendingBreachJob.IsValid())
	{
		BreachJobMaterials.Reset();
	}
	StartBreachJob();
}

void UBreachableSurfaceComponent::ApplyDecorationWork(FBreachDecorationWork& Decoration)
{
	if (!IsValid(Decorations))
	{
		AActor* Owner = GetOwner();
		if (ensureMsgf(IsValid(Owner), TEXT("%s requires an owner for Deco."), *GetName()))
		{
			Decorations = NewObject<UBreachDecorationComponent>(Owner);
			Decorations->RegisterComponent();
		}
	}
	if (IsValid(Decorations))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Update decorations");
		Decorations->ApplyWork(this, Decoration, bOriginalVisible, bOriginalHiddenInGame);
	}
}

void UBreachableSurfaceComponent::SpawnHitDebris(const FBreachHitInput& Hit, const FBreachHitResult& Result)
{
	if (Result.RemovedArea > 0 && IsValid(Hit.Material))
	{
		UWorld* World = GetWorld();
		if (IsValid(World))
		{
			UBreachDebrisSubsystem* Debris = World->GetSubsystem<UBreachDebrisSubsystem>();
			if (IsValid(Debris))
			{
				const FTransform& Transform = GetComponentTransform();
				const double AreaScale = FMath::Abs(Transform.GetScale3D().X * Transform.GetScale3D().Z);
				const FVector Face = Hit.Side == EBreachSurfaceSide::Front ? -FVector::YAxisVector : FVector::YAxisVector;
				const FVector Outward = Transform.TransformVector(Face).GetSafeNormal();
				Debris->Spawn(*Hit.Material, Hit.HitInfo.ImpactPoint, Outward, Result.RemovedArea * AreaScale);
			}
		}
	}
}

void UBreachableSurfaceComponent::FlushBreachJobs()
{
	check(IsInGameThread());
	while (BreachJob.IsValid() || PendingBreachJob.IsValid())
	{
		if (!BreachJob.IsValid())
		{
			StartBreachJob(true);
			continue;
		}
		const TSharedPtr<FBreachJob, ESPMode::ThreadSafe> Job = BreachJob;
		if (Job->Task.IsValid())
		{
			Job->Task.Wait();
		}
		CommitBreachJob(Job);
	}
}

void UBreachableSurfaceComponent::InvalidateBreachJobs(bool bWait)
{
	check(IsInGameThread());
	++BreachGeneration;
	PendingBreachJob.Reset();
	// 기다리지 않으면 실행 중인 작업이 재질을 계속 읽으므로, 결과를 버리는 완료 알림까지 작업과 재질을 붙잡아 둔다.
	if (BreachJob.IsValid() && bWait)
	{
		if (BreachJob->Task.IsValid())
		{
			BreachJob->Task.Wait();
		}
		BreachJob.Reset();
	}
	if (!BreachJob.IsValid())
	{
		BreachJobMaterials.Reset();
	}
}

FBreachSurfaceWork UBreachableSurfaceComponent::TakeWork(bool bCopy)
{
	// 그 자리에서 실행할 때는 상태를 옮겨 와 복사 없이 계산한다. 작업 스레드로 보낼 때는 복사해 반영 전에도 표면 상태를 읽을 수 있게 한다.
	FBreachSurfaceWork Work;
	Work.SurfaceName = GetName();
	if (bCopy)
	{
		Work.Model = Model;
	}
	else
	{
		Work.Model = MoveTemp(Model);
	}
	// 고정 값은 계산이 바꾸지 않으므로 복사만 하고 RestoreWork도 돌려받지 않는다.
	Work.Layout = Layout;
	Work.bPublished = IsValid(SurfaceProxy) && SurfaceProxy->IsRegistered();
	return Work;
}

void UBreachableSurfaceComponent::RestoreWork(FBreachSurfaceWork& Work, bool bMergeSimpleBuild)
{
	// 계산하는 동안 끝난 Simple 충돌 결과는 표면 쪽에만 있다. 절단 상태는 작업 쪽이 최신이므로 파생 결과만 옮겨 온다.
	if (bMergeSimpleBuild)
	{
		Work.Model.SimpleCollision.CommitBuild(Model.SimpleCollision);
	}
	Model = MoveTemp(Work.Model);
}

bool UBreachableSurfaceComponent::ApplyCutterPolygon(const FPolygon2d& InCutter, EBreachSurfaceSide Side)
{
	// 먼저 들어온 피격을 반영한 뒤 자른다. 그래야 피격 순서와 표면 상태가 어긋나지 않는다.
	FlushBreachJobs();
	PendingDecorationCuts.Reset();
	PendingDecorationRemoved.Reset();
	FDynamicMesh3 Mesh;
	bool bChanged = false;
	const TArray<FGeneralPolygon2d> Cutters{FGeneralPolygon2d(InCutter)};
	const bool bCut = CutRegions(Cutters, Mesh, bChanged);
	if (!bCut)
	{
		return false;
	}

	// 표시 중인 표면을 바꾸지 않은 절단은 반영할 것이 없다.
	if (!bChanged)
	{
		return true;
	}

	FBreachSurfaceWork::BuildSurfaceAttributes(Mesh, Layout.OriginalBounds, Layout.SurfaceUVs);
	const bool bUpdated = UpdateMesh(MoveTemp(Mesh), Side);
	return bUpdated;
}

bool UBreachableSurfaceComponent::CutRegions(TConstArrayView<FGeneralPolygon2d> Cutters, FDynamicMesh3& OutMesh, bool& bOutChanged)
{
	bOutChanged = false;

	// Ensure SurfaceProxy
	const bool bHadProxy = IsValid(SurfaceProxy);
	if (!bHadProxy)
	{
		const bool bInitialized = CreateSurfaceProxy(OutMesh);
		if (!bInitialized)
		{
			return false;
		}
	}

	FBreachSurfaceWork Work = TakeWork();
	FBreachHitResult Result;
	const bool bCut = Work.CutRegions(Cutters, bOutChanged, Result);
	RestoreWork(Work);

	// 실패 처리: 이번 호출에서 만든 Proxy만 되돌리고 기존 표면은 유지한다.
	if (!bCut)
	{
		if (!bHadProxy)
		{
			ResetSurface();
		}
		return false;
	}

	if (bOutChanged)
	{
		OutMesh = MoveTemp(Work.Mesh);
	}
	PendingDecorationCuts.Append(Result.DecorationCuts);
	PendingDecorationRemoved.Append(Result.DecorationRemoved);
	return true;
}

UBreachSurfaceMaterial* UBreachableSurfaceComponent::GetSurfaceMaterial() const
{
	return SurfaceMaterial.Get();
}

void UBreachableSurfaceComponent::SetSurfaceMaterial(UBreachSurfaceMaterial* InSurfaceMaterial)
{
	SurfaceMaterial = InSurfaceMaterial;
	RefreshDisplayMaterial();
}

void UBreachableSurfaceComponent::RefreshDisplayMaterial()
{
	const UBreachSurfaceMaterial* Rules = SurfaceMaterial.Get();
	if (!Rules || !Rules->DisplayMaterial)
	{
		DisplayMaterialInstance = nullptr;
		if (bDisplayOverridesCaptured)
		{
			OverrideMaterials = OriginalDisplayOverrides;
			bDisplayOverridesCaptured = false;
			OriginalDisplayOverrides.Reset();
		}
		if (IsValid(SurfaceProxy))
		{
			for (int32 Index = 0; Index < FMath::Max(1, GetNumMaterials()); ++Index)
			{
				SurfaceProxy->SetMaterial(Index, GetMaterial(Index));
			}
		}
		MarkRenderStateDirty();
		return;
	}
	if (Rules->bUseWorldSizeUV && (!FMath::IsFinite(Rules->TileSizeCm.X) || !FMath::IsFinite(Rules->TileSizeCm.Y) || Rules->TileSizeCm.X <= 0 || Rules->TileSizeCm.Y <= 0))
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s cannot display its configured world-size material: invalid tile dimensions."), *GetName());
		DisplayMaterialInstance = nullptr;
		return;
	}
	if (!bDisplayOverridesCaptured)
	{
		OriginalDisplayOverrides = OverrideMaterials;
		bDisplayOverridesCaptured = true;
	}
	UMaterialInterface* Applied = Rules->DisplayMaterial;
	// 에디터에는 저장 가능한 인스턴스만 연결하고 게임 중 파라미터만 MID가 소유한다.
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		DisplayMaterialInstance = UMaterialInstanceDynamic::Create(Rules->DisplayMaterial, this);
		if (!ensureMsgf(DisplayMaterialInstance, TEXT("%s could not create the configured display material."), *GetName()))
		{
			return;
		}
		DisplayMaterialInstance->SetScalarParameterValue(TEXT("BreachUseWorldSizeUV"), Rules->bUseWorldSizeUV ? 1.f : 0.f);
		DisplayMaterialInstance->SetScalarParameterValue(TEXT("BreachTileSizeU"), Rules->TileSizeCm.X);
		DisplayMaterialInstance->SetScalarParameterValue(TEXT("BreachTileSizeV"), Rules->TileSizeCm.Y);
		Applied = DisplayMaterialInstance;
	}
	for (int32 Index = 0; Index < FMath::Max(1, GetNumMaterials()); ++Index)
	{
		SetMaterial(Index, Applied);
		if (IsValid(SurfaceProxy))
		{
			SurfaceProxy->SetMaterial(Index, Applied);
		}
	}
	MarkRenderStateDirty();
}

