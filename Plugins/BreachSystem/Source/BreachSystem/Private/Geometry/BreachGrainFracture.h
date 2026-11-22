#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"

struct FBreachLineTearOptions;

namespace BreachGrainFracture
{
	struct FGuideSegment
	{
		FVector2d A;
		FVector2d B;
		int32 PathIndex;
	};

	/**
	 * Tear the contour along the pattern lines. Each lane between neighboring lines is cut as strips whose sides stay on the
	 * lines and whose ends move along them. Contour area outside every lane is kept as is.
	 * Lines that double back along their own direction cannot bound a lane and are ignored. No lanes returns the contour.
	 */
	bool Build(const FBreachLineTearOptions& Options, const UE::Geometry::FPolygon2d& Contour, TConstArrayView<FGuideSegment> Guides, int32 Seed, double Influence, TArray<UE::Geometry::FGeneralPolygon2d>& Out, FString& OutError);
}
