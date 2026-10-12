#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"

// Dimensions in centimeters. The surface lies in local XY; thickness runs along Z.
struct BREACHSYSTEM_API FBreachSurfaceSize
{
	double Width = 400.0;
	double Height = 300.0;
	double Thickness = 10.0;
};

struct BREACHSYSTEM_API FBreachSurfaceState
{
	FBreachSurfaceSize Size; // 원본 Surface 사이즈
	TArray<UE::Geometry::FGeneralPolygon2d> RemainingRegions; // 벽이 파괴되어 서로 떨어진 여러 영역으로 나뉠 수 있기 때문에 배열로 저장합니다.
};

struct BREACHSYSTEM_API FBreachSurfaceCutInput
{
	FVector2d SurfacePoint = FVector2d::ZeroVector;
	double Radius = 20.0;
	int32 Seed = 0;
};
