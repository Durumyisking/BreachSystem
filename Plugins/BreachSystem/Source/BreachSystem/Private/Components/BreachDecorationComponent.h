#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Surface/BreachDecorationGeometry.h"
#include "BreachDecorationComponent.generated.h"

class UBreachableSurfaceComponent;
class UBreachSurfaceMaterial;
class UDynamicMeshComponent;

/** 표면이 자동 생성하며, 충돌과 분리된 Deco 기록 및 표시의 수명을 소유한다. */
UCLASS(Transient, NotBlueprintable)
class UBreachDecorationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBreachDecorationComponent();
	void Update(UBreachableSurfaceComponent* Surface, UBreachSurfaceMaterial* Material, const UE::Geometry::FPolygon2d& Original, TConstArrayView<UE::Geometry::FGeneralPolygon2d> Remaining, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> Cuts, EBreachSurfaceSide Side, bool bVisible, bool bHidden);
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void Clear();
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

	TArray<BreachDecoration::FPatch> Patches;
	TArray<BreachDecoration::FBinding> Bindings;
	TArray<BreachDecoration::FFeatureEdge> FeatureEdges;
	FVector FeatureScale = FVector::ZeroVector;
};
