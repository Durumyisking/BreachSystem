#include "Components/BreachSimpleProxy.h"
#include "Components/BreachableSurfaceComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "Chaos/Convex.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UBreachSimpleProxy::UBreachSimpleProxy()
{
	PrimaryComponentTick.bCanEverTick = false;
	BodyInstance.bAutoWeld = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetVisibility(false);
	SetCanEverAffectNavigation(false);
}

void UBreachSimpleProxy::SetSurface(UBreachableSurfaceComponent* InSurface)
{
	Surface = InSurface;
}

UBodySetup* UBreachSimpleProxy::CreateBodySetup()
{
	check(IsInGameThread());
	UBodySetup* Setup = NewObject<UBodySetup>(this, NAME_None, RF_Transient);
	check(Setup);
	Setup->BodySetupGuid = FGuid::NewGuid();
	Setup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	Setup->bGenerateMirroredCollision = true;
	Setup->bSupportUVsAndFaceRemap = false;
	return Setup;
}

void UBreachSimpleProxy::InitializeCollision(const FBox& InLocalBounds)
{
	check(IsInGameThread());
	check(InLocalBounds.IsValid);
	check(!IsRegistered());
	LocalBounds = InLocalBounds;
	ActiveBodySetup = CreateBodySetup();
	check(ActiveBodySetup);
	ActiveBodySetup->bNeverNeedsCookedCollisionData = true;
	const FVector Size = LocalBounds.GetSize();
	FKBoxElem& Box = ActiveBodySetup->AggGeom.BoxElems.AddDefaulted_GetRef();
	Box.Center = LocalBounds.GetCenter();
	Box.X = Size.X;
	Box.Y = Size.Y;
	Box.Z = Size.Z;
}

UBodySetup* UBreachSimpleProxy::GetBodySetup()
{
	return ActiveBodySetup;
}

FBoxSphereBounds UBreachSimpleProxy::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(LocalBounds.TransformBy(LocalToWorld));
}

void UBreachSimpleProxy::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	UBreachableSurfaceComponent* Current = Surface.Get();
	if (IsValid(Current))
	{
		Current->HandleBreachDamage(this, DamageEvent, DamageCauser, DamageAmount);
	}
}

void UBreachSimpleProxy::CookCollision(const FKAggregateGeom& Collision, TFunction<void(bool)> OnComplete)
{
	check(IsInGameThread());
	check(OnComplete);
	check(!bCooking);
	check(!PendingBodySetup);
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | QueueCook");
	PendingBodySetup = CreateBodySetup();
	check(PendingBodySetup);
	PendingBodySetup->AddCollisionFrom(Collision);
	bCookReady = false;

	// 완전 제거는 Cooking할 도형이 없는 정상 결과다.
	if (Collision.GetElementCount() == 0)
	{
		PendingBodySetup->bNeverNeedsCookedCollisionData = true;
		bCookReady = true;
		OnComplete(true);
		return;
	}

	bCooking = true;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	++CookRequestCount;
	bLastCookHadIndices = true;
	for (const FKConvexElem& Convex : Collision.ConvexElems)
	{
		bLastCookHadIndices &= !Convex.IndexData.IsEmpty();
	}
#endif
	const TWeakObjectPtr<UBreachSimpleProxy> WeakThis(this);
	// 프록시가 파괴되어도 엔진의 완료 콜백까지 BodySetup과 요청 정리를 보장한다.
	const auto KeepAlive = MakeShared<TStrongObjectPtr<UBodySetup>>(PendingBodySetup.Get());
	PendingBodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateLambda([WeakThis, KeepAlive, OnComplete = MoveTemp(OnComplete)](bool bSucceeded)
	{
		check(IsInGameThread());
		UBreachSimpleProxy* Proxy = WeakThis.Get();
		if (IsValid(Proxy))
		{
			bSucceeded = Proxy->FinishCook(bSucceeded);
		}
		else
		{
			bSucceeded = false;
		}
		OnComplete(bSucceeded);
	}));
}

bool UBreachSimpleProxy::FinishCook(bool bSucceeded)
{
	check(IsInGameThread());
	check(PendingBodySetup);
	bCooking = false;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (bDropNextCookedHull)
	{
		bDropNextCookedHull = false;
		check(!PendingBodySetup->AggGeom.ConvexElems.IsEmpty());
		PendingBodySetup->AggGeom.ConvexElems[0].ResetChaosConvexMesh();
	}
	if (bFailNextCook)
	{
		bFailNextCook = false;
		bSucceeded = false;
	}
#endif
	bCookReady = bSucceeded;
	for (const FKConvexElem& Convex : PendingBodySetup->AggGeom.ConvexElems)
	{
		const auto& Hull = Convex.GetChaosConvexMesh();
		if (Hull == nullptr)
		{
			bCookReady = false;
			break;
		}
		if (!Hull->IsValidGeometry())
		{
			bCookReady = false;
			break;
		}
	}
	return bCookReady;
}

bool UBreachSimpleProxy::ApplyCollision()
{
	check(IsInGameThread());
	if (!ensureMsgf(PendingBodySetup && bCookReady && !bCooking, TEXT("%s requires a completed Simple cook before publication."), *GetName()))
	{
		return false;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | Apply");
	const bool bNeedsBody = GetCollisionEnabled() != ECollisionEnabled::NoCollision && PendingBodySetup->AggGeom.GetElementCount() > 0;
	if (bNeedsBody)
	{
		UWorld* World = GetWorld();
		if (!IsRegistered() || !World || !World->GetPhysicsScene())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s cannot apply Simple collision without a registered physics scene; previous collision is retained."), *GetName());
			return false;
		}
	}
	const TStrongObjectPtr<UBodySetup> Previous(ActiveBodySetup.Get());
	ActiveBodySetup = PendingBodySetup;
	RecreatePhysicsState();
	if (bNeedsBody)
	{
		if (!BodyInstance.IsValidBodyInstance())
		{
			ActiveBodySetup = Previous.Get();
			RecreatePhysicsState();
			UE_LOG(LogTemp, Warning, TEXT("%s could not create its new Simple physics body; previous collision was restored."), *GetName());
			return false;
		}
	}
	PendingBodySetup = nullptr;
	bCookReady = false;
	return true;
}

void UBreachSimpleProxy::DiscardCollision()
{
	check(IsInGameThread());
	check(!bCooking);
	PendingBodySetup = nullptr;
	bCookReady = false;
}

