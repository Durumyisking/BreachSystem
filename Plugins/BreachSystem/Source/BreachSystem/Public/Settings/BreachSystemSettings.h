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

	/** 연결된 구멍의 월드 면적이 이 값을 초과할 때만 이동용 Simple 충돌을 제거한다. */
	UPROPERTY(Config, EditAnywhere, Category = "Grid", meta = (DisplayName = "Complex Hole Area Threshold (cm^2)", ClampMin = "0.1"))
	double ComplexHoleSizeThreshold = 500.f;

	/** 연결된 개구부 하나에서 선택할 Convex 통과 영역 수. 실제 벽 충돌체 수와는 다르다. */
	UPROPERTY(Config, EditAnywhere, Category = "Collision", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSimpleConvexCount = 3;

	static int32 GetMaxSimpleConvexCount();

	/** 전역 셀 크기를 읽는다. 잘못된 Config 값은 경고를 남기고 기본값 10cm를 반환한다. */
	UFUNCTION(BlueprintPure, Category="Breach|Settings", meta=(DisplayName="Get Breach Cell Size (cm)"))
	static float GetCellSizeCm();

	UFUNCTION(BlueprintPure, Category = "Breach|Settings", meta = (DisplayName = "Complex Hole Area Threshold (cm^2)"))
	static double GetComplexHoleSizeThreshold();

};
