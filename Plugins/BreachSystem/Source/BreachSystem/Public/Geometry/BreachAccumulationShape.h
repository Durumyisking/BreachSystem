#pragma once

#include "CoreMinimal.h"
#include "Polygon2.h"
#include "Geometry/BreachContour.h"
#include "Data/BreachAccumulationOptions.h"

namespace BreachAccumulationShape
{
	/** cm 탄착 좌표의 볼록 외곽을 여유 폭만큼 확장한다. 실패 시 결과를 비우고 이유를 반환한다. */
	BREACHSYSTEM_API bool GenerateBase(TConstArrayView<FVector2d> ImpactPoints, double Padding,
		UE::Geometry::FPolygon2d& OutPolygon, FString& OutError);
}
