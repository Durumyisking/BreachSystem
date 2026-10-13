// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "Surface/BreachSurfaceTypes.h"
#include "BreachableSurfaceComponent.generated.h"

/** 원본 StaticMesh 또는 입력한 치수로 초기화하는 사각형 파괴 표면. */
UCLASS(ClassGroup=(Breach), meta=(BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableSurfaceComponent : public UDynamicMeshComponent
{
	GENERATED_BODY()

public:
	UBreachableSurfaceComponent();

	virtual void BeginPlay() override;
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:

	// Surface
	// 로컬 XY가 판의 표면이고 Z가 두께인 사각형 메시를 사용한다.
	// 패키징 실행에서 변환하려면 원본 메시의 Allow CPU Access가 필요하다.
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool InitializeSurface();

	// cm 단위의 폭(X), 높이(Y), 두께(Z)로 원점 중심의 사각판을 생성한다.
	bool InitializeSurface(double Width, double Height, double Thickness);

	// 블루프린트에서는 같은 이름의 함수 오버로드를 지원하지 않아 별도 이름으로 노출한다.
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool InitializeSurfaceFromSize(double Width, double Height, double Thickness);

	// 마지막으로 성공한 초기화 방식과 원본을 사용해 복구한다.
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool ResetSurface();

	// Breach
	void HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser);

	void DebugDrawSurface();

private:
	UPROPERTY(EditAnywhere, Category = "Breach")
	TObjectPtr<UStaticMesh> SourceStaticMesh = nullptr;

	bool bInitializedFromSize = false;
	FBreachSurfaceSize InitialSize;
	FBreachSurfaceState SurfaceState;
};
