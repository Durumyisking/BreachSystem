// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "BreachableMeshComponent.generated.h"

class UDynamicMeshComponent;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/**
 * 
 */
UCLASS(ClassGroup = (Breach), meta = (BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableMeshComponent : public UStaticMeshComponent
{
	GENERATED_BODY()
	
protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnBreachableMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	bool ConvertStaticMeshToDynamicMesh(UE::Geometry::FDynamicMesh3& OutMesh) const;
	void CreateDynamicMeshProxy(UE::Geometry::FDynamicMesh3&& Mesh);

protected:
	UPROPERTY(Transient)
	TObjectPtr<UDynamicMeshComponent> DynamicMeshProxy;

};
