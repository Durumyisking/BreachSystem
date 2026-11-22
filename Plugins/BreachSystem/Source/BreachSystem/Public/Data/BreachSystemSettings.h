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

	/**
	 * 끄면 피격한 프레임에 총알·트레이스용 Complex 충돌까지 갱신한다.
	 * 켜면 Cooking을 작업 스레드로 넘겨 게임 스레드 비용을 줄이지만, 구멍이 보인 뒤 몇 프레임 동안 이전 충돌이 남는다.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Collision", meta = (DisplayName = "Async Complex Collision"))
	bool bAsyncComplexCollision = false;

	/**
	 * 켜면 피격의 절단·메시·Deco 계산을 작업 스레드에서 하고, 끝나면 게임 스레드에서 한 번에 반영한다.
	 * 구멍·충돌·OnBreached가 보통 0~1프레임 늦게 나타난다. 그 사이 들어온 피격은 반영된 상태에 이어서 순서대로 처리한다.
	 * 끄면 같은 계산을 피격한 프레임에 바로 실행한다.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (DisplayName = "Async Breach"))
	bool bAsyncBreach = true;

	/**
	 * 이동용 Simple 충돌을 만들 때 통로 가장자리에서 지울 꼭짓점의 기준(월드 cm).
	 * 지워도 가장자리가 이 값 이내로만 통로 안쪽으로 움직이는 꼭짓점을 지워 벽 충돌체 수를 줄인다. 통로는 그만큼 좁아질 수 있다.
	 * 0이면 지우지 않는다. 표시 메시와 총알용 Complex 충돌에는 영향이 없다.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Collision", meta = (DisplayName = "Simple Passage Tolerance (cm)", ClampMin = "0", UIMax = "10"))
	int32 SimplePassageToleranceCm = 1;

	static int32 GetMaxSimpleConvexCount();

	static int32 GetSimplePassageToleranceCm();

	static bool IsComplexCollisionAsync();

	static bool IsBreachAsync();

	/** 전역 셀 크기를 읽는다. 잘못된 Config 값은 경고를 남기고 기본값 10cm를 반환한다. */
	UFUNCTION(BlueprintPure, Category="Breach|Settings", meta=(DisplayName="Get Breach Cell Size (cm)"))
	static float GetCellSizeCm();

	UFUNCTION(BlueprintPure, Category = "Breach|Settings", meta = (DisplayName = "Complex Hole Area Threshold (cm^2)"))
	static double GetComplexHoleSizeThreshold();

};
