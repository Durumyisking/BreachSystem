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
	/** 표시·충돌·Tick을 끄고, 충돌 갱신을 명시적으로 요청하도록 초기화한다. */
	UBreachSurfaceProxy();
	/** 피해를 처리할 원본 표면을 약한 참조로 연결한다. nullptr로 연결을 해제할 수 있다. */
	void SetSurface(UBreachableSurfaceComponent* InSurface);
	/** Proxy가 받은 피해를 부모 처리 후 유효한 원본 표면의 공통 브리칭 경로로 전달한다. */
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UBreachableSurfaceComponent> Surface;
};
