// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BreachSourceOption.h"
#include "Components/ActorComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "TransformTypes.h"
#include "BreachSourceComponent.generated.h"


UCLASS(ClassGroup = (Breach), meta = (BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachSourceComponent : public UActorComponent
{
	GENERATED_BODY()
	
public: 
	/** 월드 UE 단위의 반지름으로 원형 관통 단면을 바인딩한다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindCylinder(float Radius);

	/** 월드 UE 단위의 가로/세로 전체 크기로 사각형 관통 단면을 바인딩한다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindBox(FVector2D Size);

	/** 구의 투영 모양인 원형 단면을 바인딩한다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindSphere(float Radius);

	/** 캡슐 축 방향의 투영 모양인 원형 단면을 바인딩한다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindCapsule(float Radius);

	UFUNCTION(BlueprintCallable)
	void UpdateBreachSourceOption(const FBreachSourceOption& InBreachSourceOption)
	{
		BreachSourceOption = InBreachSourceOption;
		CacheCutterMesh();
	}

	/** 캐시된 커터를 반환하고, 피격 대상의 월드 범위를 관통하는 Transform을 계산한다. */
	const UE::Geometry::FDynamicMesh3* ResolveCutterMesh(const UE::Geometry::FAxisAlignedBox3d& TargetWorldBounds,
		const FVector& HitPosition, const FVector& HitDirection,
		UE::Geometry::FTransformSRT3d& OutTransform);

protected:
	virtual void BeginPlay() override;

	/** 단면 크기를 유지하고 Z 길이만 1인 커터를 캐시한다. */
	UFUNCTION(BlueprintCallable)
	bool CacheCutterMesh();

	UPROPERTY(EditAnywhere)
	FBreachSourceOption BreachSourceOption;

	// 커터는 로컬 Z=-0.5~0.5에 저장하고, 피격마다 정점 대신 Transform을 바꾼다.
	UE::Geometry::FDynamicMesh3 CachedCutterMesh;
};
