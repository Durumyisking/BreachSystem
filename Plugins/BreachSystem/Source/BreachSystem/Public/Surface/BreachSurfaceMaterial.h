#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Surface/BreachDamageAccumulator.h"
#include "Surface/BreachAccumulationShape.h"
#include "BreachSurfaceMaterial.generated.h"

/** 여러 표면이 공유하는 파괴 규칙. 피격 기록과 남은 형상은 각 표면이 소유한다. */
UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachSurfaceMaterial : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Accumulation")
	FBreachAccumulationOptions Accumulation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Breach|Accumulation")
	FBreachAccumulationShapeOptions AccumulationShape;

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
