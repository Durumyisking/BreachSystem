#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BreachSourceOption.h"
#include "BreachShape.generated.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

UCLASS(Abstract, BlueprintType)
class BREACHSYSTEM_API UBreachShape : public UObject
{
	GENERATED_BODY()

public:
	virtual void GenerateMesh(
		const FBreachSourceOption& Option,
		double Length,
		UE::Geometry::FDynamicMesh3& OutMesh) const
		PURE_VIRTUAL(UBreachShape::GenerateMesh, );
};


UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachCylinderShape : public UBreachShape
{
	GENERATED_BODY()

public:
	virtual void GenerateMesh(
		const FBreachSourceOption& Option,
		double Length,
		UE::Geometry::FDynamicMesh3& OutMesh) const override;
};


UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachSphereShape : public UBreachShape
{
	GENERATED_BODY()

public:
	virtual void GenerateMesh(
		const FBreachSourceOption& Option,
		double Length,
		UE::Geometry::FDynamicMesh3& OutMesh) const override;
};


UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachBoxShape : public UBreachShape
{
	GENERATED_BODY()

public:
	virtual void GenerateMesh(
		const FBreachSourceOption& Option,
		double Length,
		UE::Geometry::FDynamicMesh3& OutMesh) const override;
};


UCLASS(BlueprintType)
class BREACHSYSTEM_API UBreachCapsuleShape : public UBreachShape
{
	GENERATED_BODY()

public:
	virtual void GenerateMesh(
		const FBreachSourceOption& Option,
		double Length,
		UE::Geometry::FDynamicMesh3& OutMesh) const override;
};
