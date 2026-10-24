// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Surface/BreachSurfaceTypes.h"
#include "Surface/BreachDamageAccumulator.h"
#include "BreachableSurfaceComponent.generated.h"


class UBreachSurfaceProxy;
struct FBreachContourOptions;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBreachedDelegate, const FHitResult&, HitInfo, UPrimitiveComponent*, BreachedMesh);

/** 원본 StaticMesh 또는 입력한 치수로 초기화하는 사각형 파괴 표면. */
UCLASS(ClassGroup=(Breach), meta=(BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableSurfaceComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	/** Tick을 끄고 기본 충돌 프로필을 BlockAll로 설정한다. */
	UBreachableSurfaceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breach|Accumulation")
	FBreachAccumulationOptions Accumulation;

	/** 아직 제거되지 않은 누적 피해 기록 수를 반환한다. 총 피격 횟수와는 다르다. */
	int32 GetAccumulatedHitCount() const
	{
		return DamageAccumulator.Num();
	}

	/** 원본 컴포넌트가 파괴될 때 연결된 동적 메시 Proxy도 정리한다. */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	//UFUNCTION(Server, Reliable)
	/** 원본 StaticMesh로 전달된 피해를 부모 처리 후 공통 브리칭 경로로 넘긴다. */
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/**
	 * 원본과 Proxy의 포인트 피해를 처리한다. DamageCauser의 BreachSource 커터로 직접 절단한 뒤 조건에 따라 피해를 누적한다.
	 * 변형 커터 적용 실패 시 기본 형상으로 재시도하며, 직접 절단 성공 후 브리칭 이벤트를 알린다.
	 */
	void HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser, float DamageAmount);

	/**
	 * 표면 로컬 XZ의 단순 다각형을 빼고, 원본 경계와 변을 공유하지 않는 분리 영역도 제거한다.
	 * 삼각분할·입체 메시·UV·충돌을 갱신한다. 실패 시 기존 표면을 유지하며, true가 실제 면적 감소를 뜻하지는 않는다.
	 */
	bool ApplyCutterPolygon(const UE::Geometry::FPolygon2d& Cutter);

	UPROPERTY(BlueprintAssignable)
	FOnBreachedDelegate OnBreachedDelegate;
protected:

	/**
	 * 원본 LOD0을 동적 메시 Proxy로 복사하고 표시·충돌을 전환한다. 이미 초기화되었으면 true를 반환한다.
	 * 로컬 XZ가 표면, Y가 두께인 사각 판을 전제로 하며, CPU 접근 가능한 버퍼와 양쪽 표면의 선형 UV0가 필요하다.
	 */
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool InitializeSurface();

	/** Proxy와 누적 피해·표면 상태를 비우고, 저장해 둔 원본 StaticMesh의 표시·충돌 상태를 복구한다. */
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool ResetSurface();

	// Breach
	/** 남은 표면 영역의 외곽선은 녹색, 구멍 경계는 빨간색으로 월드에 표시한다. */
	void DebugDrawSurface();


	/** 외곽선의 길이 있는 변이 원본 사각 경계의 한 변 위에 있는지 0.001 허용 오차로 검사한다. 점 접촉만으로는 true가 아니다. */
	static bool TouchesOriginalBorder(const UE::Geometry::FPolygon2d& Outer, const UE::Geometry::FAxisAlignedBox2d& Bounds);

protected:
	UPROPERTY(EditAnywhere)
	TObjectPtr<UTexture2D> MaskTexture;

private:
	/** 스케일을 반영한 cm 좌표의 피격을 기록하고 임계값을 채운 후보 창을 절단한다. 성공한 절단 내부의 기록만 제거한다. */
	void ApplyAccumulatedDamage(const FVector2d& MetricPoint, double Contribution, const FBreachContourOptions& ContourOptions);
	/** 표면 로컬 XZ 다각형을 누적 기록의 cm 좌표로 바꾸고 내부·경계 기록을 제거한다. */
	void RetireAccumulationInside(const UE::Geometry::FPolygon2d& LocalPolygon);

	FBreachDamageAccumulator DamageAccumulator;
	FVector2d AccumulationScale = FVector2d::ZeroVector;

	struct FPlanarUVMapping
	{
		FVector2d Offset = FVector2d::ZeroVector;
		FVector2d PerX = FVector2d::ZeroVector;
		FVector2d PerZ = FVector2d::ZeroVector;
	};
	FPlanarUVMapping SurfaceUVs[2]; // -Y 면, +Y 면 순서

	UPROPERTY(Transient)
	TObjectPtr<UBreachSurfaceProxy> DynamicMeshProxy;

	ECollisionEnabled::Type OriginalCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bOriginalVisible = true;
	bool bOriginalHiddenInGame = false;

	UE::Geometry::FPolygon2d OriginalOuter;
	TArray<FVector2d> OriginalVertices;
	UE::Geometry::FAxisAlignedBox2d OriginBounds;
	FVector2d OriginalDepthRange = FVector2d::ZeroVector;

	FBreachSurfaceState SurfaceState;
};
