#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachableSurfaceComponent.h"
#include "PhysicsEngine/BodySetup.h"

UBreachSurfaceProxy::UBreachSurfaceProxy()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetVisibility(false);
	bUseAsyncCooking = false;
	SetDeferredCollisionUpdatesEnabled(true, false);
}

void UBreachSurfaceProxy::SetSurface(UBreachableSurfaceComponent* InSurface)
{
	Surface = InSurface;
}

UBodySetup* UBreachSurfaceProxy::CreateBodySetupHelper()
{
	UBodySetup* Setup = Super::CreateBodySetupHelper();
	if (!ensureMsgf(Setup, TEXT("%s could not create a Complex BodySetup."), *GetName()))
	{
		return nullptr;
	}

	// 엔진은 이 값이 꺼진 BodySetup으로 음수 스케일 바디를 만들지 않는다. 삼각형 충돌은 이 값으로 추가 Cooking을 하지 않는다.
	Setup->bGenerateMirroredCollision = true;
	return Setup;
}

void UBreachSurfaceProxy::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!Surface.IsValid())
	{
		return;
	}
	Surface->HandleBreachDamage(this, DamageEvent, DamageCauser, DamageAmount);
}
