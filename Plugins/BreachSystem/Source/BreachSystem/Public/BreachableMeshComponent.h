// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "BreachableMeshComponent.generated.h"

/**
 * 
 */
UCLASS(ClassGroup = (Breach), meta = (BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableMeshComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()
	
protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnBreachableMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

public:
	void ConvertStaticMeshToDynamicMesh(UStaticMesh* InSourceStaticMesh);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UStaticMesh> SourceStaticMesh;

};
