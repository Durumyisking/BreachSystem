#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BreachSystemSettings.generated.h"

/** 프로젝트 전체가 공유하는 브리칭 격자 규격. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Breach System"))
class BREACHSYSTEM_API UBreachSystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBreachSystemSettings();

	/** 전역 격자의 한 변 길이. 사용처는 표면 초기화 시 값을 고정하고 서버와 클라이언트가 같은 규격을 사용해야 한다. */
	UPROPERTY(Config, EditAnywhere, Category="Grid", meta=(DisplayName="Cell Size", ClampMin="0.1", Units="cm"))
	float CellSizeCm = 10.f;

	/** 전역 셀 크기를 읽는다. 잘못된 Config 값은 경고를 남기고 기본값 10cm를 반환한다. */
	UFUNCTION(BlueprintPure, Category="Breach|Settings", meta=(DisplayName="Get Breach Cell Size (cm)"))
	static float GetCellSizeCm();
};
