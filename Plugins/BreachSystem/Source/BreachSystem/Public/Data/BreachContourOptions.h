#pragma once

#include "CoreMinimal.h"
#include "BreachContourOptions.generated.h"

/** Selects how the contour is displaced along the base polygon boundary. */
UENUM(BlueprintType)
enum class EBreachContourPattern : uint8
{
	/** Separate triangular damage sections; produces sharp notches, but can leave long perfectly flat stretches. */
	TriangleSections UMETA(DisplayName="Triangle Sections"),

	/** Independent continuous Macro, Medium and Micro fields; produces varied damage without suppressing smaller details. */
	IndependentContinuous UMETA(DisplayName="Independent Continuous")
};

/** Cutter-relative boundary detail, applied before the surface transform. */
USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachContourOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bEnabled"))
	EBreachContourPattern Pattern = EBreachContourPattern::IndependentContinuous;

	/** Target number of boundary vertices and detail intervals, independent of cutter dimensions; existing base corners are preserved when possible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="4", ClampMax="128", UIMin="4", UIMax="128", EditCondition="bEnabled"))
	int32 VertexCount = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bEnabled"))
	bool bRandomizeVertexSpacing = true;

	/** Maximum signed normal-distributed spacing offset, relative to the original neighbor interval (1 = one interval). Safety limits may reduce it. Applied every max(1, ceil(ratio)) vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", DisplayName="Max Jitter Ratio", EditCondition="bEnabled && bRandomizeVertexSpacing"))
	float VertexPositionJitter = 0.6f;

	/** Cached in the editor and serialized for runtime use. Native runtime options must supply their matching interval. */
	UPROPERTY(VisibleAnywhere, meta=(EditCondition="bEnabled && bRandomizeVertexSpacing"))
	int32 JitterInterval = 1;

	/** Allow the original corners of Box / concave polygons to move along their angle bisectors. Circle sources always move their smooth boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bEnabled"))
	bool bMoveCorners = true;

	/** Fraction of the base cutter's shorter dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="0.45", EditCondition="bEnabled"))
	float MacroDepthRatio = 0.25f;

	/** Fraction of the base cutter's shorter dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="0.45", EditCondition="bEnabled"))
	float MediumDepthRatio = 0.075f;

	/** Fine detail amplitude as a fraction of the base cutter\'s shorter dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="0.45", EditCondition="bEnabled"))
	float MicroDepthRatio = 0.f;

	/** Combined displacement is capped to this fraction of the base's shorter dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="0.45", EditCondition="bEnabled"))
	float MaxDisplacementFraction = 0.25f;

	/** Zero means a fresh contour per generation; nonzero produces repeatable results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bEnabled"))
	int32 Seed = 0;

#if WITH_EDITOR
	/** 정점 간격 변형 주기를 max(1, ceil(VertexPositionJitter))로 캐시한다. 비유한값은 1, 정수 범위 초과는 최댓값을 사용한다. */
	void CacheJitterInterval();
#endif

};
