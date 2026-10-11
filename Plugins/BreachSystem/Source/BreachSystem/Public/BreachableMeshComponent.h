// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "BreachableMeshComponent.generated.h"

class UDynamicMeshComponent;
class UBreachableDynamicMeshComponent;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/**
 * 
 */
UCLASS(ClassGroup = (Breach), meta = (BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableMeshComponent : public UGeometryCollectionComponent
{
	GENERATED_BODY()
	
public:
	UBreachableMeshComponent();
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	friend class UBreachableDynamicMeshComponent;

	void HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser);
	void ApplyBreachHit(UPrimitiveComponent* HitComponent, AActor* SourceActor, const FHitResult& Hit);

	virtual void BeginPlay() override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UDynamicMeshComponent> DynamicMeshProxy;

};
