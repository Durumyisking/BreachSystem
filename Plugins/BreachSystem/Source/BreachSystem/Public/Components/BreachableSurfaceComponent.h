// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Geometry/BreachSurfaceTypes.h"
#include "Geometry/BreachDamageAccumulator.h"
#include "Geometry/BreachAccumulationShape.h"
#include "Geometry/BreachCellGrid.h"
#include "Geometry/BreachSimpleCollision.h"
#include "Data/BreachDecorationOptions.h"
#include "Geometry/BreachSurfaceUV.h"
#include "BreachableSurfaceComponent.generated.h"


class UBreachSurfaceProxy;
class UBreachSimpleProxy;
class UBreachDecorationComponent;
class UBreachSurfaceMaterial;
class UMaterialInstanceDynamic;
struct FBreachSimpleBuildJob;
struct FBreachJob;
struct FBreachHitInput;
class FBreachSurfaceWork;
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
	bool CreateSurfaceProxy(UE::Geometry::FDynamicMesh3& OutMesh);

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
	// 테스트 전용: 비동기 브리칭의 대기·반영 순서와 수명 무효화를 검증한다.
	friend struct FBreachAsyncBreachTestAccess;
#endif
	/**
	 * 절단 결과를 계산한다. 성공한 표면 상태와 노멀·UV가 없는 메시만 보관하고 실제 반영은 미룬다.
	 * 이미 Proxy로 표시 중인 표면에서 면적이 줄지 않은 절단은 true를 돌려주되 bOutChanged가 false이며, 표면·충돌·Deco 상태를 건드리지 않는다.
	 * 아직 전환하지 않은 표면은 효과 없는 절단도 bOutChanged가 true여서 Proxy 표시로 전환된다.
	 */
	bool CutRegions(TConstArrayView<UE::Geometry::FGeneralPolygon2d> Cutters, UE::Geometry::FDynamicMesh3& OutMesh, bool& bOutChanged);

	/**
	 * 계산에 쓸 표면 상태를 넘긴다. bCopy가 false면 상태를 옮겨 오므로 RestoreWork 전까지 표면은 그 상태를 읽지 않는다.
	 * 작업 스레드로 보낼 때는 복사해서, 반영 전에도 게임 스레드가 마지막으로 반영된 상태를 읽을 수 있게 한다.
	 */
	FBreachSurfaceWork TakeWork(bool bCopy = false);
	/**
	 * 계산이 끝난 상태를 돌려받는다. bMergeSimpleBuild면 계산하는 동안 끝난 Simple 충돌 결과를 먼저 합친다.
	 * 복사한 작업에만 쓴다. 옮겨 온 작업이면 표면의 Simple 상태가 비어 있어 합칠 것이 없다.
	 */
	void RestoreWork(FBreachSurfaceWork& Work, bool bMergeSimpleBuild = false);

	/** 피격을 대기열에 넣고, 실행 중인 브리칭 작업이 없으면 바로 시작한다. bCreatedProxy는 이 피격이 Proxy를 처음 만들었다는 뜻이다. */
	void EnqueueBreachHit(FBreachHitInput&& Hit, bool bCreatedProxy);
	/** 대기열의 피격을 작업 하나로 묶어 시작한다. 비동기 설정이 꺼져 있거나 bForceSync면 그 자리에서 계산하고 반영한다. */
	void StartBreachJob(bool bForceSync = false);
	/** 끝난 작업을 게임 스레드에서 반영한다. 표면 수명이 바뀐 뒤 도착한 결과는 버린다. */
	void CommitBreachJob(const TSharedPtr<FBreachJob, ESPMode::ThreadSafe>& Job);
	/** 실행 중인 작업과 대기 중인 피격을 모두 반영할 때까지 기다린다. 동기 API가 순서를 지키기 위해 쓴다. */
	void FlushBreachJobs();
	/** 대기 중인 피격을 버리고 실행 중인 결과가 반영되지 않게 한다. bWait면 실행 중인 계산이 끝날 때까지 기다린다. */
	void InvalidateBreachJobs(bool bWait);

	/** 노멀과 UV를 채운 최종 메시와 충돌을 반영하고, 첫 갱신이면 원본에서 Proxy로 표시와 충돌을 전환한다. */
	bool UpdateMesh(UE::Geometry::FDynamicMesh3&& Mesh, EBreachSurfaceSide Side = EBreachSurfaceSide::Front);

	FBreachSimpleBuildPolicy GetSimpleBuildPolicy() const;
	void QueueSimpleCollision();
	void CompleteSimpleCollision(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job);
	void CompleteSimpleCook(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job, bool bCookSucceeded);
	bool CanPublishSimpleCollision(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job) const;
	void InvalidateSimpleCollisionJobs();

public:
	// Variables

	UPROPERTY(BlueprintAssignable)
	FOnBreachedDelegate OnBreachedDelegate;

private:
	/** 공유 누적 파괴 규칙. 미지정 시 직접 절단만 적용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breach", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBreachSurfaceMaterial> SurfaceMaterial;

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
	TObjectPtr<UBreachSurfaceProxy> SurfaceProxy;

	UPROPERTY(Transient)
	TObjectPtr<UBreachSimpleProxy> SimpleProxy;

	UPROPERTY(Transient)
	TObjectPtr<UBreachDecorationComponent> Decorations;

	// 한 번의 피격에서 채택된 절삭만 모아 최종 메시 반영 뒤 한 번 처리한다.
	TArray<UE::Geometry::FAxisAlignedBox2d> PendingDecorationCuts;
	// 같은 피격으로 사라진 도형(커터와 고립 조각의 외곽). Deco가 이 범위만 다시 계산한다.
	TArray<UE::Geometry::FPolygon2d> PendingDecorationRemoved;

	// 브리칭 작업은 표면마다 하나만 실행한다. 실행 중 들어온 피격은 다음 작업 하나에 순서대로 모은다.
	TSharedPtr<FBreachJob, ESPMode::ThreadSafe> BreachJob;
	TSharedPtr<FBreachJob, ESPMode::ThreadSafe> PendingBreachJob;
	uint64 BreachGeneration = 0;
	// 작업 스레드가 읽는 재질 에셋을 반영이 끝날 때까지 붙잡는다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBreachSurfaceMaterial>> BreachJobMaterials;

	ECollisionEnabled::Type OriginalCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bOriginalVisible = true;
	bool bOriginalHiddenInGame = false;

	// 원본 경계와 절단 후 남은 영역 // 지워도 될듯하다.
	UE::Geometry::FPolygon2d OriginalOuter;
	// 원본 UV·경계·두께와 셀 크기. CreateSurfaceProxy가 정하고 작업에는 복사만 한다.
	FBreachSurfaceLayout Layout;

	// 현재 표면상태 (필수). 계산하는 동안 작업이 들고 있다가 RestoreWork로 돌려준다.
	FBreachSurfaceModel Model;
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
		return Model.DamageAccumulator.Num();
	}
};
