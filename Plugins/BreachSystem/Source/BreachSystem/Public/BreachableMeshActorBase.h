// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BreachableMeshActorBase.generated.h"

class UBreachableMeshComponent;

UCLASS()
class BREACHSYSTEM_API ABreachableMeshActorBase : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABreachableMeshActorBase();

protected:
	// Called when the game starts or when spawned
	virtual void OnConstruction(const FTransform& Transform) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;


protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UBreachableMeshComponent> BreachableMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UStaticMesh> SourceStaticMesh;
	
};
