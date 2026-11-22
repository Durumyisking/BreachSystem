#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Geometry/BreachDecorationGeometry.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "BreachDecorationComponent.generated.h"

class UBreachableSurfaceComponent;
class UBreachSurfaceMaterial;
class UDynamicMeshComponent;
class FBreachSimpleCollision;

/**
 * Deco 기록과 표시 메시의 사본. 컴포넌트를 건드리지 않으므로 작업 스레드에서 갱신할 수 있다.
 * 컴포넌트가 PrepareWork로 채우고, Update가 한 번의 브리칭을 반영하며, 컴포넌트가 ApplyWork로 표시에 옮긴다.
 */
struct FBreachDecorationWork
{
	/** 마지막으로 적용한 재질과 설정. 재질이나 설정이 바뀌면 모든 기록을 처음부터 다시 만든다. */
	const UBreachSurfaceMaterial* Material = nullptr;
	FBreachCutDecorationOptions Cut;
	FBreachFeatureDecorationOptions Feature;
	/** 여백·표면 오프셋·면적 기준을 계산한 스케일. 바뀌면 표시를 다시 만든다. */
	FVector Scale = FVector::ZeroVector;
	BreachDecoration::FCutState Cuts;
	BreachDecoration::FFeatureState Features;

	// 표시가 없는 면은 메시도 없다. Changed는 컴포넌트 메시를 이 사본으로 바꿔야 한다는 뜻이다.
	bool bHasCutMesh[2] = {false, false};
	bool bCutMeshChanged[2] = {false, false};
	UE::Geometry::FDynamicMesh3 CutMeshes[2];
	bool bHasFeatureMesh = false;
	bool bFeatureMeshChanged = false;
	UE::Geometry::FDynamicMesh3 FeatureMesh;
	/** 기록을 비워 기존 표시 컴포넌트를 버려야 한다. 새 재질로 다시 만든다. */
	bool bCutDisplaysReset = false;
	bool bFeatureDisplayReset = false;
	/** 안전하지 않아 Deco를 비운 이유. 반영할 때 경고로 남긴다. */
	TArray<FString> Warnings;

	/**
	 * 한 번의 브리칭으로 바뀐 부분만 반영한다. Holes는 그 절단까지 반영한 표면의 구멍 캐시다.
	 * Cuts는 채택된 커터의 경계, Removed는 사라진 도형(커터와 고립 조각의 외곽)이다.
	 */
	void Update(const UBreachSurfaceMaterial* InMaterial, const FBreachSimpleCollision& Holes, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> InCuts, TConstArrayView<UE::Geometry::FPolygon2d> Removed, EBreachSurfaceSide Side, const FVector& InScale);

private:
	void Clear();
	/** Feature 기록과 표시를 비운다. 다음 갱신은 현재 구멍 전체에서 다시 배치한다. */
	void ResetFeatures();
	/** Cut 패치 기록은 남기고 표시 메시만 버린다. 다음 갱신에서 모든 패치를 다시 넣는다. */
	void ResetCutDisplays();
	bool BuildDisplays(const FVector2d& Depth, FString& Error);
};

/** 표면이 자동 생성하며, 충돌과 분리된 Deco 기록 및 표시의 수명을 소유한다. */
UCLASS(Transient, NotBlueprintable)
class UBreachDecorationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBreachDecorationComponent();
	/** 그 자리에서 PrepareWork, FBreachDecorationWork::Update, ApplyWork를 차례로 실행한다. */
	void Update(UBreachableSurfaceComponent* Surface, UBreachSurfaceMaterial* Material, const FBreachSimpleCollision& Holes, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> Cuts, TConstArrayView<UE::Geometry::FPolygon2d> Removed, EBreachSurfaceSide Side, bool bVisible, bool bHidden);
	/** 현재 기록과 표시 메시를 사본으로 넘긴다. 반영 전까지 컴포넌트는 기록을 바꾸지 않는다. */
	void PrepareWork(FBreachDecorationWork& Work) const;
	/** 갱신된 사본으로 기록을 바꾸고 표시 컴포넌트를 생성·교체·제거한다. */
	void ApplyWork(UBreachableSurfaceComponent* Surface, FBreachDecorationWork& Work, bool bVisible, bool bHidden);
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void Clear();
	/** Feature 기록과 표시를 비운다. 다음 갱신은 현재 구멍 전체에서 다시 배치한다. */
	void ResetFeatures();

	UPROPERTY(Transient)
	TObjectPtr<UBreachSurfaceMaterial> AppliedMaterial;

	UPROPERTY(Transient)
	FBreachCutDecorationOptions AppliedCut;

	UPROPERTY(Transient)
	FBreachFeatureDecorationOptions AppliedFeature;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> CutDisplays;

	UPROPERTY(Transient)
	TObjectPtr<UDynamicMeshComponent> FeatureDisplay;

	BreachDecoration::FCutState Cuts;
	BreachDecoration::FFeatureState Features;
	// 여백·표면 오프셋·면적 기준을 계산한 스케일. 바뀌면 표시를 다시 만든다.
	FVector AppliedScale = FVector::ZeroVector;
};
