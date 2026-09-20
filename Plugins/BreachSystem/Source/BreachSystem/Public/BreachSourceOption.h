#pragma once

#include "CoreMinimal.h"
#include "BreachSourceOption.generated.h"

class UBreachShape;

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachSourceOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Depth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D Size = FVector2D(10.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UBreachShape> ShapeClass;
};