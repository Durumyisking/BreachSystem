// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Surface/BreachSurfaceTypes.h"
#include "Surface/BreachDamageAccumulator.h"
#include "Surface/BreachAccumulationShape.h"
#include "Surface/BreachCellGrid.h"
#include "Surface/BreachSimpleCollision.h"
#include "Surface/BreachDecorationOptions.h"
#include "Surface/BreachSurfaceUV.h"
#include "BreachableSurfaceComponent.generated.h"


class UBreachSurfaceProxy;
class UBreachSimpleProxy;
class UBreachDecorationComponent;
class UBreachSurfaceMaterial;
class UBreachSourceComponent;
class UMaterialInstanceDynamic;
struct FBreachSimpleBuildJob;
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
	/** 원본 표시 캐시를 무효화한다. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** 원본 컴포넌트가 파괴될 때 표시·이동 충돌 프록시도 정리한다. */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnUnregister() override;
	virtual void OnRegister() override;

	//UFUNCTION(Server, Reliable)
	/** 원본 StaticMesh로 전달된 피해를 부모 처리 후 공통 브리칭 경로로 넘긴다. */
	virtual void ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/**
	 * 원본과 Proxy의 포인트 피해를 처리한다. 누적 파손이 성공하면 총알 구멍을 생략한다.
	 * 표시 메시와 Complex를 반영하고 Simple 비동기 갱신을 요청한 뒤 브리칭 이벤트를 알린다.
	 * 이미 Proxy로 표시 중인 표면을 바꾸지 않은 피격은 아무것도 갱신하지 않고 이벤트도 알리지 않는다.
	 */
	void HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser, float DamageAmount);

	/**
	 * 표면 로컬 XZ의 단순 다각형을 빼고, 원본 경계와 변을 공유하지 않는 분리 영역도 제거한다.
	 * 삼각분할·입체 메시·UV·충돌을 갱신한다. 실패 시 기존 표면을 유지하며, true가 실제 면적 감소를 뜻하지는 않는다.
	 */
	bool ApplyCutterPolygon(const UE::Geometry::FPolygon2d& Cutter, EBreachSurfaceSide Side = EBreachSurfaceSide::Front);

#if WITH_EDITOR
	/** 실제 격자와 원본 경계를 조회한다. Proxy 생성이나 표면 변경은 하지 않는다. */
	bool GetCellGridForDebug(FBreachCellGrid& OutGrid, UE::Geometry::FAxisAlignedBox2d& OutLocalBounds, FVector2d& OutDepthRange) const;
#endif

protected:
	 /** 원본을 읽고 표면 상태와 미등록 Proxy를 준비한다. 메시와 충돌은 아직 반영하지 않는다. */
	bool CreateDynamicMeshProxy(UE::Geometry::FDynamicMesh3& OutMesh);

	/** Proxy와 누적 피해·표면 상태를 비우고, 저장해 둔 원본 StaticMesh의 표시·충돌 상태를 복구한다. */
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool ResetSurface();

#if WITH_EDITOR
	/** 남은 표면 영역의 외곽선은 녹색, 구멍 경계는 빨간색으로 월드에 표시한다. */
	void DebugDrawSurface();
#endif

private:
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 기존 동기 충돌 질의 테스트가 실제 작업 완료를 기다릴 수 있게 한다.
	friend struct FBreachSimpleAsyncTestAccess;
	// 테스트 전용: 일괄 절단의 원자성과 재질별 누적 경로를 검증한다.
	friend struct FBreachPatternSurfaceTestAccess;
#endif
	/**
	 * 절단 결과를 계산한다. 성공한 표면 상태와 노멀·UV가 없는 메시만 보관하고 실제 반영은 미룬다.
	 * 이미 Proxy로 표시 중인 표면에서 면적이 줄지 않은 절단은 true를 돌려주되 bOutChanged가 false이며, 표면·충돌·Deco 상태를 건드리지 않는다.
	 * 아직 전환하지 않은 표면은 효과 없는 절단도 bOutChanged가 true여서 Proxy 표시로 전환된다.
	 */
	bool CutPolygon(const UE::Geometry::FPolygon2d& Cutter, UE::Geometry::FDynamicMesh3& OutMesh, bool& bOutChanged);
	bool CutRegions(TConstArrayView<UE::Geometry::FGeneralPolygon2d> Cutters, UE::Geometry::FDynamicMesh3& OutMesh, bool& bOutChanged);

	/** CutPolygon이 만든 메시에 노멀과 UV를 채운다. 여러 절단이 이어져도 최종 메시에 한 번만 실행한다. */
	void BuildSurfaceAttributes(UE::Geometry::FDynamicMesh3& Mesh) const;

	/** 최종 메시와 충돌을 반영하고, 첫 갱신이면 원본에서 Proxy로 표시와 충돌을 전환한다. */
	bool UpdateMesh(UE::Geometry::FDynamicMesh3&& Mesh, EBreachSurfaceSide Side = EBreachSurfaceSide::Front);

	FBreachSimpleBuildPolicy GetSimpleBuildPolicy() const;
	void QueueSimpleCollision();
	void CompleteSimpleCollision(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job);
	void CompleteSimpleCook(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job, bool bCookSucceeded);
	bool CanPublishSimpleCollision(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job) const;
	void InvalidateSimpleCollisionJobs();

	/** 총알 커터로 절단하고, 실패하면 모양이 다른 기본 커터로만 재시도한다. */
	bool ApplyDirectCut(UBreachSourceComponent* Source, const FVector& ImpactPoint, UE::Geometry::FGeneralPolygon2d& InOutCutter,
		UE::Geometry::FDynamicMesh3& OutMesh, bool& bOutChanged);

	/** 피격점에 남은 표면을 확인하고 누적 설정과 셀 좌표를 준비한다. 실패하면 누적을 건너뛴다. */
	bool PrepareAccumulation(const FVector& ImpactPoint, double BreachDamage, const FBreachAccumulationOptions& Options,
		FVector2d& OutGridHitPoint);

	/** 셀 탄착을 누적하고 파손을 시도한다. 하나라도 성공하면 true와 최종 계산 메시를 돌려준다. 표면이 실제로 바뀌었으면 bOutChanged가 true다. */
	bool AccumulateHit(const FVector2d& GridHitPoint, double BreachDamage, const FBreachAccumulationShapeOptions& ShapeOptions,
		UE::Geometry::FDynamicMesh3& OutMesh, bool& bOutChanged, EBreachSurfaceSide Side = EBreachSurfaceSide::Front);

	/** 절단으로 사라진 영역의 누적 탄착 기록을 지운다. 입력은 표면 로컬 XZ 좌표다. */
	void RemoveHitsInside(const UE::Geometry::FPolygon2d& LocalPolygon);

	/** 외곽선의 길이 있는 변이 원본 사각 경계의 한 변 위에 있는지 0.001 허용 오차로 검사한다. 점 접촉만으로는 true가 아니다. */
	static bool TouchesOriginalBorder(const UE::Geometry::FPolygon2d& Outer, const UE::Geometry::FAxisAlignedBox2d& Bounds);

public:
	// Variables

	UPROPERTY(BlueprintAssignable)
	FOnBreachedDelegate OnBreachedDelegate;

private:
	/** 공유 누적 파괴 규칙. 미지정 시 직접 절단만 적용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breach", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBreachSurfaceMaterial> SurfaceMaterial;

	// 표면별 누적 피해 상태
	FBreachDamageAccumulator DamageAccumulator;
	FVector2d CachedMeshScaleXZ = FVector2d::ZeroVector;
	FBreachCellGrid CellGrid;
	double CapturedCellSize = 0;

	FPlanarUVMapping SurfaceUVs[2]; // -Y 면, +Y 면 순서
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMaterialInstance;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalDisplayOverrides;
	bool bDisplayOverridesCaptured = false;
	void RefreshDisplayMaterial();

	/** 초기화와 표시 조회가 동일한 LOD0/UV 검증을 사용한다. 원본은 수정하지 않는다. */
	bool ReadSourceSurface(UE::Geometry::FDynamicMesh3& Mesh, FPlanarUVMapping (&OutUVs)[2]) const;

#if WITH_EDITOR
	// 표시용 원본 캐시. 런타임 파손 상태를 생성하거나 변경하지 않는다.
	mutable TWeakObjectPtr<UStaticMesh> DebugSourceMesh;
	mutable const void* DebugRenderData = nullptr;
	mutable bool bDebugSourceValid = false;
	mutable FPlanarUVMapping DebugUVs[2];
	mutable UE::Geometry::FAxisAlignedBox2d DebugBounds;
	mutable FVector2d DebugDepth = FVector2d::ZeroVector;
#endif

	// Proxy 수명과 원본 표시·충돌 복원 상태
	UPROPERTY(Transient)
	TObjectPtr<UBreachSurfaceProxy> DynamicMeshProxy;

	UPROPERTY(Transient)
	TObjectPtr<UBreachSimpleProxy> SimpleProxy;

	UPROPERTY(Transient)
	TObjectPtr<UBreachDecorationComponent> Decorations;

	// 한 번의 피격에서 채택된 절삭만 모아 최종 메시 반영 뒤 한 번 처리한다.
	TArray<UE::Geometry::FAxisAlignedBox2d> PendingDecorationCuts;
	// 같은 피격으로 사라진 도형(커터와 고립 조각의 외곽). Deco가 이 범위만 다시 계산한다.
	TArray<UE::Geometry::FPolygon2d> PendingDecorationRemoved;
	// 한 번의 피격에서 성공한 절단으로 제거된 로컬 면적.
	double PendingRemovedArea = 0;

	ECollisionEnabled::Type OriginalCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bOriginalVisible = true;
	bool bOriginalHiddenInGame = false;

	// 원본 경계와 절단 후 남은 영역 // 지워도 될듯하다.
	UE::Geometry::FPolygon2d OriginalOuter;
	UE::Geometry::FAxisAlignedBox2d OriginBounds;
	FVector2d OriginalDepthRange = FVector2d::ZeroVector;

	// 현재 표면상태 (필수)
	FBreachSurfaceState SurfaceState;
	FBreachSimpleCollision SimpleCollision;
	// 한 표면 수명에는 실행 작업 하나와 최신 변경을 나타내는 대기 표시만 둔다.
	TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe> SimpleBuildJob;
	uint64 SimpleBuildGeneration = 0;
	uint64 CompletedSimpleRevision = 0;
	FBreachSimpleBuildPolicy CompletedSimplePolicy;
	bool bSimpleBuildPending = false;
	double SimplePendingSince = 0;

public:
	// Getters / Setters

	UBreachSurfaceMaterial* GetSurfaceMaterial() const;
	void SetSurfaceMaterial(UBreachSurfaceMaterial* InSurfaceMaterial);

	/** 아직 제거되지 않은 누적 피해 기록 수를 반환한다. 총 피격 횟수와는 다르다. */
	int32 GetAccumulatedHitCount() const
	{
		return DamageAccumulator.Num();
	}
};
