// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "BreachableMeshComponent.generated.h"

/**
 * 
 */
UCLASS()
class BREACHSYSTEM_API UBreachableMeshComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()
	
	

public:
	void ConvertStaticMeshToDynamicMesh(UStaticMesh* InSourceStaticMesh);
	
	
};
