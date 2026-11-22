#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Polygon2.h"
#include "Shape/BreachContour.h"
#include "BreachSourceOption.generated.h"

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachShapeOption
{
	GENERATED_BODY()

	virtual ~FBreachShapeOption() = default;

	/**
	 * 원점 중심의 기본 2D 커터를 만든다. 형상이 없는 기본 구조체는 빈 결과와 false를 반환한다.
	 * TInstancedStruct가 실제 파생 타입으로 생성하므로 기반 포인터로 호출해도 파생 구현이 실행된다.
	 */
	virtual bool GeneratePolygon(UE::Geometry::FPolygon2d& OutPolygon) const;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachCircleShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.001", Units="cm"))
	float Radius = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="3", ClampMax="512"))
	int32 Segments = 32;

	/** 반경으로 반시계 방향 다각형을 만든다. 분할 수는 3~512로 제한하며, 반경이 잘못되면 빈 결과와 false를 반환한다. */
	virtual bool GeneratePolygon(UE::Geometry::FPolygon2d& OutPolygon) const override;

};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachBoxShapeOption : public FBreachShapeOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector2D Size = FVector2D(10.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="cm"))
	float CornerRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="128", EditCondition="CornerRadius > 0", EditConditionHides))
	int32 CornerSegments = 8;

	/** 전체 폭·높이로 사각형 또는 둥근 사각형을 만든다. 모서리 반경은 짧은 변의 절반까지 제한하며, 잘못된 옵션은 거부한다. */
	virtual bool GeneratePolygon(UE::Geometry::FPolygon2d& OutPolygon) const override;

};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachSourceOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TInstancedStruct<FBreachShapeOption> ShapeOption;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FBreachContourOptions Contour;
};
