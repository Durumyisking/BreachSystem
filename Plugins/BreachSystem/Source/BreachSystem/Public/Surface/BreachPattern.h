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

/**
 * 패턴에 그린 선을 따라 찢는 설정. 이웃한 두 선 사이 한 칸을 "조각"이라 부른다.
 * 맞은 영역(컨투어)에 걸친 조각은 옆면이 선 위에 그대로 남고, 양 끝만 선 방향으로 더 찢기거나 덜 찢긴다.
 * SurfaceMaterial의 GrainInfluence가 찢김 길이와 가시 길이에 곱해지며, 0이면 선을 무시하고 컨투어만 자른다.
 */
USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachLineTearOptions
{
	GENERATED_BODY()

	/** 조각 끝이 맞은 영역보다 더 찢어지는 길이의 최솟값. 구멍 크기 대비 비율이며, 음수면 맞은 영역보다 덜 찢어진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="찢김 길이 최소", ClampMin="-0.5", ClampMax="1.5"))
	float TearLengthMin = -.05f;

	/** 조각 끝이 맞은 영역보다 더 찢어지는 길이의 최댓값. 조각의 양 끝은 최소~최대 사이에서 각각 따로 정해진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="찢김 길이 최대", ClampMin="-0.5", ClampMax="1.5"))
	float TearLengthMax = .2f;

	/** 이보다 짧게 찢길 조각은 찢지 않고 남긴다. 구멍 크기 대비 비율이며, 가장자리에 생기는 작은 홈을 없앤다. 0이면 모두 찢는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="최소 찢김 길이", ClampMin="0", ClampMax="1"))
	float MinTearLength = .1f;

	/** 한 조각을 선 방향으로 최대 몇 번 더 쪼갤지. 쪼갠 조각은 각자 다른 길이로 찢겨 계단이 생긴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="조각 나눔 최대 수", ClampMin="0", ClampMax="4"))
	int32 MaxSplits = 2;

	/** 조각 끝마다 나는 가시의 최대 개수. 0~최대 사이에서 정해지며, 가시는 구멍 안쪽이나 바깥쪽을 향한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="가시 최대 개수", ClampMin="0", ClampMax="8"))
	int32 MaxSpikes = 4;

	/** 가시 길이의 최솟값. 조각 폭 대비 배수다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="가시 길이 최소", ClampMin="0", ClampMax="3"))
	float SpikeLengthMin = .25f;

	/** 가시 길이의 최댓값. 조각 폭 대비 배수이며, 가시마다 최소~최대 사이에서 따로 정해진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Line Tear", meta=(DisplayName="가시 길이 최대", ClampMin="0", ClampMax="3"))
	float SpikeLengthMax = .9f;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pattern", meta=(DisplayName="선 따라 찢기", EditCondition="Mode == EBreachPatternMode::GrainGuides", EditConditionHides))
	FBreachLineTearOptions LineTear;

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
