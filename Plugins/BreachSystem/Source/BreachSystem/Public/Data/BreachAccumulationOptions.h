#pragma once

#include "CoreMinimal.h"
#include "Data/BreachContourOptions.h"
#include "BreachAccumulationOptions.generated.h"

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachAccumulationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	/** Physical dimensions of the moving damage window. Changing settings clears old records. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm", EditCondition="bEnabled"))
	FVector2D WindowSize = FVector2D(40.f, 40.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001", EditCondition="bEnabled"))
	float BreachHP = 1.f;
};

/** 표면이 결정하는 누적 파손 윤곽. 판정 창 크기와 독립적이다. */
USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachAccumulationShapeOptions
{
	GENERATED_BODY()

	FBreachAccumulationShapeOptions()
	{
		Contour.bEnabled = true;
	}

	/** 탄착군 외곽에 더하는 물리적 여유 폭. 한 점도 이 반경의 파손을 만든다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm"))
	float Padding = 10.f;

	/** 누적 파손에서는 Seed 0도 재현 가능하도록 고정 기본 시드 1을 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FBreachContourOptions Contour;
};
