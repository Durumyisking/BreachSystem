// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BreachSourceOption.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "BreachSourceComponent.generated.h"


UCLASS(ClassGroup = (Breach), meta = (BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachSourceComponent : public UActorComponent
{
	GENERATED_BODY()
	
protected:
	void CacheCutterMesh();
public: 
	void UpdateBreachSourceOption(const FBreachSourceOption& InBreachSourceOption)
	{
		BreachSourceOption = InBreachSourceOption;
		CacheCutterMesh();
	}

protected:
	UPROPERTY(EditAnywhere)
	FBreachSourceOption BreachSourceOption;

	UE::Geometry::FDynamicMesh3 CachedCutterMesh;

public:
	const UE::Geometry::FDynamicMesh3& GetCutterMesh() const { return CachedCutterMesh; }
};
