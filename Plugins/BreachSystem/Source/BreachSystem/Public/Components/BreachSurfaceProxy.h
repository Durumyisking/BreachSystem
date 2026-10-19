#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "BreachSurfaceProxy.generated.h"

class UBreachableSurfaceComponent;

UCLASS(Transient, NotBlueprintable)
class BREACHSYSTEM_API UBreachSurfaceProxy : public UDynamicMeshComponent
{
	GENERATED_BODY()

public:
	UBreachSurfaceProxy();
	void SetSurface(UBreachableSurfaceComponent* InSurface);
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UBreachableSurfaceComponent> Surface;
};
