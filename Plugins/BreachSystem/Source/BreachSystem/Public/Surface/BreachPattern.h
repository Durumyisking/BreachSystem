#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BreachPattern.generated.h"

class UTexture2D;
class UBreachSurfaceMaterial;

UENUM()
enum class EBreachPatternAuthoringType : uint8 { PointsAndLines, Texture };

UENUM(BlueprintType)
enum class EBreachPatternMode : uint8
{
	None,
	Regions,
	GrainGuides
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachPatternLoop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	TArray<FVector2D> Vertices;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachPatternRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	TArray<FVector2D> Outer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	TArray<FBreachPatternLoop> Holes;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachPatternGuide
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pattern")
	TArray<FVector2D> Points;
};

/** Unit-period UV geometry shared by surfaces. Loops may cross a tile boundary. */
UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachPattern : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern")
	EBreachPatternMode Mode = EBreachPatternMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern", meta=(EditCondition="Mode == EBreachPatternMode::Regions"))
	TArray<FBreachPatternRegion> Regions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern", meta=(EditCondition="Mode == EBreachPatternMode::GrainGuides"))
	TArray<FBreachPatternGuide> Guides;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category="Pattern")
	EBreachPatternAuthoringType AuthoringType = EBreachPatternAuthoringType::PointsAndLines;
	UPROPERTY(EditAnywhere, Category="Pattern")
	TObjectPtr<UTexture2D> RuleTexture;
	UPROPERTY(EditAnywhere, Category="Pattern")
	TSoftObjectPtr<UBreachSurfaceMaterial> PreviewSurfaceMaterial;

	UPROPERTY(EditAnywhere, Category="Pattern")
	TObjectPtr<UTexture2D> ReferenceTexture;
#endif
};
