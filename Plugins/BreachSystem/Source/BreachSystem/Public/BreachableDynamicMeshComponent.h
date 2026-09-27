#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "BreachableDynamicMeshComponent.generated.h"

class UBreachableMeshComponent;

/** Cut 이후 동적 메시가 받은 데미지를 원본 브리칭 컴포넌트로 전달한다. */
UCLASS(Transient)
class UBreachableDynamicMeshComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UBreachableMeshComponent> BreachableMesh;

	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
};
