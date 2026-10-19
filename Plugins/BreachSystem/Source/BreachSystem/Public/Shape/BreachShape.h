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
	virtual bool GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const
		PURE_VIRTUAL(UBreachShape::GeneratePolygon, return false;);
};

UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachCircleShape : public UBreachShape
{
	GENERATED_BODY()

public:
	virtual bool GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const override;
};

UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachBoxShape : public UBreachShape
{
	GENERATED_BODY()

public:
	virtual bool GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const override;
};
