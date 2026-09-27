#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BreachSystemSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Breach System"))
class BREACHSYSTEM_API UBreachSystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** 바인딩 시 사용하는 공통 깊이. 이미 생성된 커터에는 변경을 반영하지 않는다. */
	UPROPERTY(Config, EditAnywhere, Category="Depth", meta=(ClampMin="0.0001", Units="cm"))
	float SharedDepth = 100.f;
};
