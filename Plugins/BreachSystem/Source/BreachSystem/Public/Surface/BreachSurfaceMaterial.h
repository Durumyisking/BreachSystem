#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Surface/BreachDamageAccumulator.h"
#include "Surface/BreachAccumulationShape.h"
#include "Surface/BreachDecorationOptions.h"
#include "BreachSurfaceMaterial.generated.h"

class UBreachPattern;
class UMaterialInterface;
class UNiagaraSystem;
class UStaticMesh;

/** 여러 표면이 공유하는 파괴 규칙. 피격 기록과 남은 형상은 각 표면이 소유한다. */
UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachSurfaceMaterial : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Appearance", meta=(DisplayName="표시 머티리얼"))
	TObjectPtr<UMaterialInterface> DisplayMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Appearance", meta=(DisplayName="물리 크기로 텍스처 반복"))
	bool bUseWorldSizeUV = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Appearance", meta=(DisplayName="텍스처 한 타일 크기 (cm)", ClampMin="0.01"))
	FVector2D TileSizeCm = FVector2D(100, 100);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Pattern")
	TObjectPtr<UBreachPattern> Pattern;

	/** 패턴의 "선 따라 찢기" 강도. 찢김 길이와 가시 길이에 곱해지며, 0이면 선을 무시하고 맞은 영역만 자른다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Pattern", meta=(ClampMin="0", ClampMax="1"))
	float GrainInfluence = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Accumulation")
	FBreachAccumulationOptions Accumulation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Accumulation")
	FBreachAccumulationShapeOptions AccumulationShape;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Decoration")
	FBreachCutDecorationOptions CutDecoration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Decoration")
	FBreachFeatureDecorationOptions FeatureDecoration;

	/** 파편으로 사용할 사전 제작 메시. 각 메시에는 단순 충돌체가 필요하다. */
	UPROPERTY(EditAnywhere, Category="Breach|Debris")
	TArray<TObjectPtr<UStaticMesh>> DebrisMeshes;

	/** 작은 칩과 먼지에 사용할 선택적 일회성 효과. */
	UPROPERTY(EditAnywhere, Category="Breach|Debris")
	TObjectPtr<UNiagaraSystem> DebrisEffect;

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
