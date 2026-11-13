#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "BreachSimpleProxy.generated.h"

class UBreachableSurfaceComponent;
class UBodySetup;
struct FKAggregateGeom;

/** 표시 데이터 없이 이동용 Simple 충돌만 보관하는 표면 프록시. */
UCLASS(Transient, NotBlueprintable)
class BREACHSYSTEM_API UBreachSimpleProxy : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UBreachSimpleProxy();
	void SetSurface(UBreachableSurfaceComponent* InSurface);
	void InitializeCollision(const FBox& InLocalBounds);
	virtual UBodySetup* GetBodySetup() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** 완료 통지는 게임 스레드에서 실행된다. 성공해도 Surface의 승인 전에는 활성 충돌을 바꾸지 않는다. */
	void CookCollision(const FKAggregateGeom& Collision, TFunction<void(bool)> OnComplete);
	bool ApplyCollision();
	void DiscardCollision();

private:
	UBodySetup* CreateBodySetup();
	bool FinishCook(bool bSucceeded);

	UPROPERTY(Transient)
	TWeakObjectPtr<UBreachableSurfaceComponent> Surface;

	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> ActiveBodySetup;

	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> PendingBodySetup;

	FBox LocalBounds = FBox(ForceInit);
	bool bCooking = false;
	bool bCookReady = false;

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 실제 Cooking 이후 실패 처리와 변경 없는 결과의 요청 생략을 검증한다.
	friend struct FBreachSimpleAsyncTestAccess;
	bool bFailNextCook = false;
	bool bDropNextCookedHull = false;
	bool bLastCookHadIndices = false;
	uint32 CookRequestCount = 0;
#endif
};
