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
struct BREACHSYSTEM_API FBreachCylinderShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D Radius = FVector2D(10.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Depth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Segments = 16;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachSphereShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Radius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ThetaSegments = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 PhiSegments = 8;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachBoxShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D Size = FVector2D(10.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Depth = 100.f;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachCapsuleShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Radius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Depth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 CircleSegments = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 HemisphereSegments = 4;
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