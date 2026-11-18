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

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachImpactGrainOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grain")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grain", meta=(ClampMin="0", ClampMax="1"))
	float StartProbability = .76f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grain", meta=(ClampMin="0", ClampMax="1"))
	float Persistence = .55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grain", meta=(ClampMin="0", ClampMax="0.25"))
	float EndCutback = .06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grain", meta=(ClampMin="0", ClampMax="1"))
	float MinEndSharpness = .39f;
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

	/** Shapes short cross-grain ends; zero keeps long grain tears with blunt ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern|Grain", meta=(DisplayName="파단 끝 뾰족함", ClampMin="0", ClampMax="1", EditCondition="Mode == EBreachPatternMode::GrainGuides", EditConditionHides))
	float GrainEndSharpness = 0.f;

	/** Maximum total inward/outward splinters per short end; zero keeps blunt grain tears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern|Grain", meta=(DisplayName="파단 끝 최대 가시 수", ClampMin="0", ClampMax="8", EditCondition="Mode == EBreachPatternMode::GrainGuides", EditConditionHides))
	int32 GrainMaxEndSplinters = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern|Grain", meta=(EditCondition="Mode == EBreachPatternMode::GrainGuides", EditConditionHides))
	FBreachImpactGrainOptions ImpactGrain;

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
