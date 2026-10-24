		#pragma once

#include "Curve/GeneralPolygon2.h"
#include "DynamicMesh/DynamicMesh3.h"

using namespace UE::Geometry;

namespace BreachSurfaceGeometry
{
	/** 표면 2D 좌표의 Center에 원형 커터를 만든다. 반경이 0 이하면 1, 분할 수가 8 이하면 16으로 보정한다. */
	FGeneralPolygon2d MakeCircleCutter(const FVector2d& Center, double Radius, int32 Segments);
	/** Center를 중심으로 폭·높이가 Size인 사각 커터를 만든다. 0 이하인 크기 성분은 1로 보정한다. */
	FGeneralPolygon2d MakeBoxCutter(const FVector2d& Center, FVector2d Size);
	/** 원형 정점을 반경 방향으로 ±Roughness만큼 무작위 이동한다. Seed -1은 새 난수, 나머지는 재현 가능한 시드로 사용한다. */
	FGeneralPolygon2d MakeRoughCircleCutter(const FVector2d& Center, double Radius, int32 Segments, double Roughness, int32 Seed);

	/** 로컬 XZ 영역들을 월드에 그린다. 외곽은 녹색, 구멍은 빨간색이며, 유효한 월드가 없으면 생략한다. */
	void DebugDrawPolygon2ds(UObject* InWorldContext, const FTransform& InTransform, TArray<FGeneralPolygon2d> InGeneralPolygon2ds);
	/** 다각형의 외곽선만 지정 색상으로 2초간 그린다. 로컬 Y=0 평면을 사용하며, 유효한 월드가 없으면 생략한다. */
	void DebugDrawPolygon2d(UObject* InWorldContext, const FTransform& InTransform, FGeneralPolygon2d InGeneralPolygon2d, const FColor& InColor = FColor::Green);

	/** 월드 위치를 역변환한 뒤 로컬 XZ만 추출한다. 표면 두께 방향인 Y 좌표는 버린다. */
	FVector2D WorldToSurface(const FTransform& InTransform, const FVector& InWorldPoint);
	/** 표면 2D 좌표를 로컬 (X, 0, Z)에 놓고 월드 위치로 변환한다. */
	FVector SurfaceToWorld(const FTransform& InTransform, const FVector2D& InSurfacePoint);

}
