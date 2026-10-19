#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachableSurfaceComponent.h"

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

void UBreachSurfaceProxy::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!Surface.IsValid())
	{
		return;
	}
	Surface->HandleBreachDamage(this, DamageEvent, DamageCauser);
}
