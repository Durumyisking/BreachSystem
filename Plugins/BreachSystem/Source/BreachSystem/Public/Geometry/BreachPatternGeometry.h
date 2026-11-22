#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"
#include "Geometry/BreachContour.h"

class UBreachPattern;

/** UV = UVOffset + Metric.X * UVPerX + Metric.Y * UVPerY. */
struct BREACHSYSTEM_API FBreachPatternMapping
{
	FVector2d UVOffset = FVector2d::ZeroVector;
	FVector2d UVPerX = FVector2d(1, 0);
	FVector2d UVPerY = FVector2d(0, 1);
};

namespace BreachPatternGeometry
{
	BREACHSYSTEM_API bool Validate(const UBreachPattern& Pattern, FString& OutError);
	BREACHSYSTEM_API bool ValidateRegions(TConstArrayView<UE::Geometry::FGeneralPolygon2d> Regions, FString& OutError);
	/** Failure preserves OutCutters. Seed zero becomes one. Rejects more than 65536 local tile candidates. */
	BREACHSYSTEM_API bool Generate(const UBreachPattern& Pattern, const UE::Geometry::FPolygon2d& BaseMetric, const FBreachPatternMapping& Mapping, const FBreachContourOptions& Contour, double Influence, TArray<UE::Geometry::FGeneralPolygon2d>& OutCutters, FString& OutError);
}
