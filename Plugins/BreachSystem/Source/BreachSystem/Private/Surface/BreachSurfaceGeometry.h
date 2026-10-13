#pragma once

#include "Curve/GeneralPolygon2.h"
#include "DynamicMesh/DynamicMesh3.h"

using namespace UE::Geometry;

namespace BreachSurfaceGeometry
{
	FGeneralPolygon2d MakeCircleCutter(const FVector2d& Center, double Radius, int32 Segments);
	FGeneralPolygon2d MakeBoxCutter(const FVector2d& Center, FVector2d Size);
	FGeneralPolygon2d MakeRoughCircleCutter(const FVector2d& Center, double Radius, int32 Segments, double Roughness, int32 Seed);

	void DebugDrawPolygon2ds(UObject* InWorldContext, const FTransform& InTransform, TArray<FGeneralPolygon2d> InGeneralPolygon2ds);
	void DebugDrawPolygon2d(UObject* InWorldContext, const FTransform& InTransform, FGeneralPolygon2d InGeneralPolygon2d, const FColor& InColor = FColor::Green);

	FVector2D WorldToSurface(const FTransform& InTransform, const FVector& InWorldPoint);
	FVector SurfaceToWorld(const FTransform& InTransform, const FVector2D& InSurfacePoint);

}
