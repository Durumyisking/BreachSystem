#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "BreachSourceOption.generated.h"

class UBreachShape;

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachShapeOption
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachCircleShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.001", Units="cm"))
	float Radius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="3", ClampMax="512"))
	int32 Segments = 32;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="cm"))
	float Roughness = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Seed = 0;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachBoxShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D Size = FVector2D(10.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="cm"))
	float CornerRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="128"))
	int32 CornerSegments = 8;

	/** Maximum spacing of samples along straight edges, in world centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.1", Units="cm"))
	float EdgeSpacing = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="cm"))
	float Roughness = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Seed = 0;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachSourceOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UBreachShape> ShapeClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TInstancedStruct<FBreachShapeOption> ShapeOption;
};
