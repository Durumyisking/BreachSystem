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
	/** UE 단위의 반지름으로 실린더를 바인딩한다. 깊이는 이 호출 시 확정한다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindCylinder(float Radius, EBreachDepthMode DepthMode = EBreachDepthMode::Shared, float IndividualDepth = 100.f);

	/** UE 단위의 가로/세로 전체 크기로 박스를 바인딩한다. 깊이는 이 호출 시 확정한다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindBox(FVector2D Size, EBreachDepthMode DepthMode = EBreachDepthMode::Shared, float IndividualDepth = 100.f);

	/** 구는 반지름만 사용하며 공통/개별 깊이를 적용하지 않는다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindSphere(float Radius);

	/** 캡슐의 깊이는 반구를 포함한 전체 길이이며 최소 지름 이상이다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindCapsule(float Radius, EBreachDepthMode DepthMode = EBreachDepthMode::Shared, float IndividualDepth = 100.f);

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
