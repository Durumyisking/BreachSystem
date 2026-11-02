// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Surface/BreachSurfaceTypes.h"
#include "Surface/BreachDamageAccumulator.h"
#include "Surface/BreachAccumulationShape.h"
#include "Surface/BreachCellGrid.h"
#include "BreachableSurfaceComponent.generated.h"


class UBreachSurfaceProxy;
class UBreachSurfaceMaterial;
class UBreachSourceComponent;
namespace UE::Geometry { class FDynamicMesh3; }

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBreachedDelegate, const FHitResult&, HitInfo, UPrimitiveComponent*, BreachedMesh);

/** 원본 StaticMesh 또는 입력한 치수로 초기화하는 사각형 파괴 표면. */
UCLASS(ClassGroup=(Breach), meta=(BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachableSurfaceComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	// Functions

	/** Tick을 끄고 기본 충돌 프로필을 BlockAll로 설정한다. */
	UBreachableSurfaceComponent();

#if WITH_EDITOR
	/** 표면 파손 설정의 정점 간격 캐시를 갱신한다. */
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** 원본 컴포넌트가 파괴될 때 연결된 동적 메시 Proxy도 정리한다. */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	//UFUNCTION(Server, Reliable)
	/** 원본 StaticMesh로 전달된 피해를 부모 처리 후 공통 브리칭 경로로 넘긴다. */
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/**
	 * 원본과 Proxy의 포인트 피해를 처리한다. 누적 파손이 성공하면 총알 구멍을 생략한다.
	 * 최종 메시와 충돌을 한 번 반영한 뒤 브리칭 이벤트를 알린다.
	 */
	void HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser, float DamageAmount);

	/**
	 * 표면 로컬 XZ의 단순 다각형을 빼고, 원본 경계와 변을 공유하지 않는 분리 영역도 제거한다.
	 * 삼각분할·입체 메시·UV·충돌을 갱신한다. 실패 시 기존 표면을 유지하며, true가 실제 면적 감소를 뜻하지는 않는다.
	 */
	bool ApplyCutterPolygon(const UE::Geometry::FPolygon2d& Cutter);

	/** 실제 격자와 원본 경계를 조회한다. Proxy 생성이나 표면 변경은 하지 않는다. */
	bool GetCellGridForDebug(FBreachCellGrid& OutGrid, UE::Geometry::FAxisAlignedBox2d& OutLocalBounds, FVector2d& OutDepthRange) const;

protected:
	 /** 원본을 읽고 표면 상태와 미등록 Proxy를 준비한다. 메시와 충돌은 아직 반영하지 않는다. */
	bool PrepareSurface(UE::Geometry::FDynamicMesh3& OutMesh);

	/** Proxy와 누적 피해·표면 상태를 비우고, 저장해 둔 원본 StaticMesh의 표시·충돌 상태를 복구한다. */
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool ResetSurface();

	/** 남은 표면 영역의 외곽선은 녹색, 구멍 경계는 빨간색으로 월드에 표시한다. */
	void DebugDrawSurface();

private:
	/** 절단 결과를 계산한다. 성공한 표면 상태와 메시만 보관하고 실제 반영은 미룬다. */
	bool CutPolygon(const UE::Geometry::FPolygon2d& Cutter, UE::Geometry::FDynamicMesh3& OutMesh);

	/** 최종 메시와 충돌을 반영하고, 첫 갱신이면 원본에서 Proxy로 표시와 충돌을 전환한다. */
	bool UpdateMesh(UE::Geometry::FDynamicMesh3&& Mesh);

	/** 총알 커터를 만들고 절단을 계산한다. 실패하면 기본 커터로 재시도한다. */
	bool ApplyDirectCut(UBreachSourceComponent* Source, const FVector& ImpactPoint, UE::Geometry::FGeneralPolygon2d& InOutCutter,
		UE::Geometry::FDynamicMesh3& OutMesh);

	/** 피격점에 남은 표면을 확인하고 누적 설정과 셀 좌표를 준비한다. 실패하면 누적을 건너뛴다. */
	bool PrepareAccumulation(const FVector& ImpactPoint, double BreachDamage, const FBreachAccumulationOptions& Options,
		FVector2d& OutGridHitPoint);

	/** 셀 탄착을 누적하고 파손을 시도한다. 하나라도 성공하면 true와 최종 계산 메시를 돌려준다. */
	bool AccumulateHit(const FVector2d& GridHitPoint, double BreachDamage, const FBreachAccumulationShapeOptions& ShapeOptions,
		UE::Geometry::FDynamicMesh3& OutMesh);

	/** 절단으로 사라진 영역의 누적 탄착 기록을 지운다. 입력은 표면 로컬 XZ 좌표다. */
	void RemoveHitsInside(const UE::Geometry::FPolygon2d& LocalPolygon);

	/** 외곽선의 길이 있는 변이 원본 사각 경계의 한 변 위에 있는지 0.001 허용 오차로 검사한다. 점 접촉만으로는 true가 아니다. */
	static bool TouchesOriginalBorder(const UE::Geometry::FPolygon2d& Outer, const UE::Geometry::FAxisAlignedBox2d& Bounds);

public:
	// Variables

	UPROPERTY(BlueprintAssignable)
	FOnBreachedDelegate OnBreachedDelegate;

private:
	/** 미지정 시 기존 에셋에 저장된 누적 파괴 설정을 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breach", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBreachSurfaceMaterial> SurfaceMaterial;

	/** 기존 에셋과 Blueprint의 호환용. 새 설정은 SurfaceMaterial에서 편집한다. */
	UPROPERTY(BlueprintReadWrite, Category="Breach|Accumulation", meta=(AllowPrivateAccess="true", DeprecatedProperty, DeprecationMessage="Assign a SurfaceMaterial asset instead."))
	FBreachAccumulationOptions Accumulation;

	UPROPERTY(BlueprintReadWrite, Category="Breach|Accumulation", meta=(AllowPrivateAccess="true", DeprecatedProperty, DeprecationMessage="Assign a SurfaceMaterial asset instead."))
	FBreachAccumulationShapeOptions AccumulationShape;

	// 표면별 누적 피해 상태
	FBreachDamageAccumulator DamageAccumulator;
	FVector2d CachedMeshScaleXZ = FVector2d::ZeroVector;
	FBreachCellGrid CellGrid;
	double CapturedCellSize = 0;

	// 벽의 로컬 XZ 위치를 텍스처 UV로 바꾸는 규칙을 저장하는 구조체
	// UV = Offset + X * PerX + Z * PerZ;
	struct FPlanarUVMapping
	{
		FVector2d Offset; // X=0, Z=0에서의 UV
		FVector2d PerX;   // X가 1 증가할 때 UV 변화량
		FVector2d PerZ;   // Z가 1 증가할 때 UV 변화량
	};
	FPlanarUVMapping SurfaceUVs[2]; // -Y 면, +Y 면 순서

	/** 초기화와 표시 조회가 동일한 LOD0/UV 검증을 사용한다. 원본은 수정하지 않는다. */
	bool ReadSourceSurface(UE::Geometry::FDynamicMesh3& Mesh, FPlanarUVMapping (&OutUVs)[2]) const;

	// 표시용 원본 캐시. 런타임 파손 상태를 생성하거나 변경하지 않는다.
	mutable TWeakObjectPtr<UStaticMesh> DebugSourceMesh;
	mutable const void* DebugRenderData = nullptr;
	mutable bool bDebugSourceValid = false;
	mutable FPlanarUVMapping DebugUVs[2];
	mutable UE::Geometry::FAxisAlignedBox2d DebugBounds;
	mutable FVector2d DebugDepth = FVector2d::ZeroVector;

	// Proxy 수명과 원본 표시·충돌 복원 상태
	UPROPERTY(Transient)
	TObjectPtr<UBreachSurfaceProxy> DynamicMeshProxy;

	ECollisionEnabled::Type OriginalCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bOriginalVisible = true;
	bool bOriginalHiddenInGame = false;

	// 원본 경계와 절단 후 남은 영역
	UE::Geometry::FPolygon2d OriginalOuter;
	TArray<FVector2d> OriginalVertices;
	UE::Geometry::FAxisAlignedBox2d OriginBounds;
	FVector2d OriginalDepthRange = FVector2d::ZeroVector;

	FBreachSurfaceState SurfaceState;

public:
	// Getters / Setters

	UBreachSurfaceMaterial* GetSurfaceMaterial() const;
	void SetSurfaceMaterial(UBreachSurfaceMaterial* InSurfaceMaterial);

	/** SurfaceMaterial이 없을 때 사용하는 호환 설정이다. */
	const FBreachAccumulationOptions& GetLegacyAccumulation() const
	{
		return Accumulation;
	}

	void SetLegacyAccumulation(const FBreachAccumulationOptions& InAccumulation)
	{
		Accumulation = InAccumulation;
	}

	const FBreachAccumulationShapeOptions& GetLegacyAccumulationShape() const
	{
		return AccumulationShape;
	}

	void SetLegacyAccumulationShape(const FBreachAccumulationShapeOptions& InAccumulationShape)
	{
		AccumulationShape = InAccumulationShape;
	}

	/** 아직 제거되지 않은 누적 피해 기록 수를 반환한다. 총 피격 횟수와는 다르다. */
	int32 GetAccumulatedHitCount() const
	{
		return DamageAccumulator.Num();
	}
};
