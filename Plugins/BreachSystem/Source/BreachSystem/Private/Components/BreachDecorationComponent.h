#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Surface/BreachDecorationGeometry.h"
#include "BreachDecorationComponent.generated.h"

class UBreachableSurfaceComponent;
class UBreachSurfaceMaterial;
class UDynamicMeshComponent;
class FBreachSimpleCollision;

/** 표면이 자동 생성하며, 충돌과 분리된 Deco 기록 및 표시의 수명을 소유한다. */
UCLASS(Transient, NotBlueprintable)
class UBreachDecorationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBreachDecorationComponent();
	/**
	 * 이번 브리칭으로 바뀐 부분만 반영한다. Holes는 그 절단까지 반영한 표면의 구멍 캐시다.
	 * Cuts는 채택된 커터의 경계, Removed는 사라진 도형(커터와 고립 조각의 외곽)이다.
	 */
	void Update(UBreachableSurfaceComponent* Surface, UBreachSurfaceMaterial* Material, const FBreachSimpleCollision& Holes, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> Cuts, TConstArrayView<UE::Geometry::FPolygon2d> Removed, EBreachSurfaceSide Side, bool bVisible, bool bHidden);
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void Clear();
	/** Feature 기록과 표시를 비운다. 다음 갱신은 현재 구멍 전체에서 다시 배치한다. */
	void ResetFeatures();
	/** Cut 패치 기록은 남기고 표시 메시만 버린다. 다음 갱신에서 모든 패치를 다시 넣는다. */
	void ResetCutDisplays();
	bool BuildDisplays(UBreachableSurfaceComponent* Surface, const FVector2d& Depth, bool bVisible, bool bHidden, FString& Error);

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
