#pragma once

#include "CoreMinimal.h"
#include "Polygon2.h"
#include "BreachContour.generated.h"

/** World-centimeter boundary detail, applied before the surface transform. */
USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachContourOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm", EditCondition="bEnabled"))
	float MacroDepth = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm", EditCondition="bEnabled"))
	float MacroWidth = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm", EditCondition="bEnabled"))
	float MediumDepth = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm", EditCondition="bEnabled"))
	float MediumWidth = 10.f;

	/** Fine-grained boundary displacement, previously called Roughness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="cm", EditCondition="bEnabled"))
	float MicroDepth = 0.f;

	/** Zero means a fresh contour per generation; nonzero produces repeatable results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="bEnabled"))
	int32 Seed = 0;

	/** Upper bound on sample spacing. Smaller features may require finer sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm", EditCondition="bEnabled"))
	float SampleSpacing = 2.f;

	/** Combined displacement is capped to this fraction of the base's shorter dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="0.45", EditCondition="bEnabled"))
	float MaxDisplacementFraction = 0.25f;
};

namespace BreachContour
{
	/** Rejects non-finite, degenerate, crossing or touching boundaries. No repair is implicit. */
	BREACHSYSTEM_API bool Validate(const UE::Geometry::FPolygon2d& Polygon, FString& OutError);

	/** MicroDepth controls fine boundary detail. Options.Seed owns deterministic generation. */
	BREACHSYSTEM_API bool Generate(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
		UE::Geometry::FPolygon2d& OutPolygon, FString& OutError);
}
