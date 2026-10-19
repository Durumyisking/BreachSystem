// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Surface/BreachSurfaceTypes.h"
#include "BreachableSurfaceComponent.generated.h"

class UBreachSurfaceProxy;

/** 원본 StaticMesh 또는 입력한 치수로 초기화하는 사각형 파괴 표면. */
UCLASS(ClassGroup=(Breach), meta=(BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableSurfaceComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UBreachableSurfaceComponent();

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	void HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser);

protected:

	// Surface
	// 로컬 XZ가 판의 표면이고 Y가 두께인 사각형 메시를 사용한다.
	// 패키징 실행에서 변환하려면 원본 메시의 Allow CPU Access가 필요하다.
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool InitializeSurface();

	// DynamicMesh를 제거하고 원본 StaticMesh 표시와 충돌을 복구한다.
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool ResetSurface();

	// Breach
	void DebugDrawSurface();


	static bool TouchesOriginalBorder(const UE::Geometry::FPolygon2d& Outer, const UE::Geometry::FAxisAlignedBox2d& Bounds);
private:
	UPROPERTY(Transient)
	TObjectPtr<UBreachSurfaceProxy> DynamicMeshProxy;

	ECollisionEnabled::Type OriginalCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bOriginalVisible = true;
	bool bOriginalHiddenInGame = false;

	UE::Geometry::FPolygon2d OriginalOuter;
	TArray<FVector2d> OriginalVertices;
	UE::Geometry::FAxisAlignedBox2d OriginBounds;

	FBreachSurfaceState SurfaceState;
};
