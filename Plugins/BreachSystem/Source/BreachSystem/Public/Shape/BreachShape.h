#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BreachSourceOption.h"
#include "Polygon2.h"
#include "BreachShape.generated.h"

UCLASS(Abstract, BlueprintType)
class BREACHSYSTEM_API UBreachShape : public UObject
{
	GENERATED_BODY()

public:
	/** 형상별 옵션으로 원점 중심의 기본 2D 커터를 만든다. 파생 클래스가 구현하며, 생성 실패 시 false를 반환한다. */
	virtual bool GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const
		PURE_VIRTUAL(UBreachShape::GeneratePolygon, return false;);
};

UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachCircleShape : public UBreachShape
{
	GENERATED_BODY()

public:
	/** 원형 옵션의 반경으로 반시계 방향 다각형을 만든다. 분할 수는 3~512로 제한하며, 옵션·반경이 잘못되면 빈 결과와 false를 반환한다. */
	virtual bool GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const override;
};

UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachBoxShape : public UBreachShape
{
	GENERATED_BODY()

public:
	/** 전체 폭·높이로 사각형 또는 둥근 사각형을 만든다. 모서리 반경은 짧은 변의 절반까지 제한하며, 잘못된 옵션은 거부한다. */
	virtual bool GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const override;
};
