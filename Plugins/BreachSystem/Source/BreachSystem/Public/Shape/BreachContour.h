#pragma once

#include "CoreMinimal.h"
#include "Polygon2.h"
#include "BreachContour.generated.h"

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
	void CacheJitterInterval();
#endif

};

namespace BreachContour
{
	struct FPreparedSample
	{
		FVector2d Point = FVector2d::ZeroVector;
		FVector2d Normal = FVector2d::ZeroVector;
		FVector2d JitterBefore = FVector2d::ZeroVector;
		FVector2d JitterAfter = FVector2d::ZeroVector;
		double Distance = 0;
		double DistanceBefore = 0;
		double DistanceAfter = 0;
		double MaxDisplacement = 0;
	};

	/** Source-owned geometry and worst-case movement limits, independent of the random seed. */
	struct FPreparedContour
	{
		UE::Geometry::FPolygon2d Base;
		FBreachContourOptions Options;
		TArray<FPreparedSample> Samples;
		double Perimeter = 0;
		double CutterSize = 0;
	};

	/** Prepare once when source options change. No random contour is generated here. */
	BREACHSYSTEM_API bool Prepare(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
		FPreparedContour& OutPrepared, FString& OutError);

	/** Generate only from successfully prepared state; performs no geometry validation or neighbor searches. */
	BREACHSYSTEM_API void Generate(const FPreparedContour& Prepared, UE::Geometry::FPolygon2d& OutPolygon);

	/** Validate user-editable contour settings before accepting a source option. */
	BREACHSYSTEM_API bool ValidateOptions(const FBreachContourOptions& Options, FString& OutError);

	/** Checks vertex count, finite coordinates, edge lengths and area. Does not test self-intersections. */
	BREACHSYSTEM_API bool Validate(const UE::Geometry::FPolygon2d& Polygon, FString& OutError);

	/** Depth ratios are relative to the base cutter's shorter dimension. */
	BREACHSYSTEM_API bool Generate(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
		UE::Geometry::FPolygon2d& OutPolygon, FString& OutError);
}
