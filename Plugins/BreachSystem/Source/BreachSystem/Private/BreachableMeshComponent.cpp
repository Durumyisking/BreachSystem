// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshComponent.h"

#include "Engine/DamageEvents.h"


UBreachableMeshComponent::UBreachableMeshComponent()
{
	/*Physics*/
}

void UBreachableMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UBreachableMeshComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	HandleBreachDamage(this, DamageEvent, DamageCauser);
}

void UBreachableMeshComponent::HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser)
{
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
		// Trace의 표면 바깥 법선을 벽이 받는 이동 충돌의 법선 방향에 맞춘다.
		FHitResult BreachHit = PointDamage.HitInfo;
		BreachHit.Normal = -BreachHit.Normal;
		BreachHit.ImpactNormal = -BreachHit.ImpactNormal;
		ApplyBreachHit(HitComponent, DamageCauser, BreachHit);
	}
}

void UBreachableMeshComponent::ApplyBreachHit(UPrimitiveComponent* HitComponent, AActor* SourceActor, const FHitResult& Hit)
{
	UE_LOG(LogTemp, Warning, TEXT("Breach Hit: %s"), *Hit.ToString());
	ApplyExternalStrain(
		Hit.Item,        // 피격된 대상 인덱스
		Hit.ImpactPoint, // 실제 피격 위치
		0.f,            // 반경
		0,              // 주변 연결 조각으로 전파하지 않음
		1.f,            // 전파 배율
		10.f            // 테스트용 파괴 강도
	);
	SetAnchoredByIndex(9, false);
}
