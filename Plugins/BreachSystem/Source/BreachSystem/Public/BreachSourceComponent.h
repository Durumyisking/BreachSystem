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
	/** 설정된 브리칭 형상을 게임 시작 시 한 번 캐시한다. */
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable)
	bool CacheCutterMesh();
public: 
	UFUNCTION(BlueprintCallable)
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
	/** 캐시가 비어 있으면 한 번 재생성을 시도하고, 사용할 수 없으면 nullptr를 반환한다. */
	const UE::Geometry::FDynamicMesh3* ResolveCutterMesh();
};
