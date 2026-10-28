#pragma once

#include "CoreMinimal.h"
#include "Polygon2.h"
#include "Shape/BreachContour.h"
#include "BreachAccumulationShape.generated.h"

/** 표면이 결정하는 누적 파손 윤곽. 판정 창 크기와 독립적이다. */
USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachAccumulationShapeOptions
{
	GENERATED_BODY()

	FBreachAccumulationShapeOptions()
	{
		Contour.bEnabled = true;
	}

	/** 탄착군 외곽에 더하는 물리적 여유 폭. 한 점도 이 반경의 파손을 만든다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm"))
	float Padding = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FBreachContourOptions Contour;
};

namespace BreachAccumulationShape
{
	/** cm 탄착 좌표의 볼록 외곽을 여유 폭만큼 확장한다. 실패 시 결과를 비우고 이유를 반환한다. */
	BREACHSYSTEM_API bool GenerateBase(TConstArrayView<FVector2d> ImpactPoints, double Padding,
		UE::Geometry::FPolygon2d& OutPolygon, FString& OutError);
}
