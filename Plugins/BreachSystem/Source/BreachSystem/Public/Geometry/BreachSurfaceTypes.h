#pragma once

#include "CoreMinimal.h"
#include "BoxTypes.h"
#include "Curve/GeneralPolygon2.h"
#include "Geometry/BreachCellGrid.h"
#include "Geometry/BreachDamageAccumulator.h"
#include "Geometry/BreachSimpleCollision.h"
#include "Geometry/BreachSurfaceUV.h"

// Dimensions in centimeters. The surface lies in local XZ.
struct BREACHSYSTEM_API FBreachSurfaceSize
{
	double Width = 400.0;
	double Height = 300.0;
};

struct BREACHSYSTEM_API FBreachSurfaceState
{
	FBreachSurfaceSize Size; // 원본 Surface 사이즈
	TArray<UE::Geometry::FGeneralPolygon2d> RemainingRegions; // 벽이 파괴되어 서로 떨어진 여러 영역으로 나뉠 수 있기 때문에 배열로 저장합니다.
};

/** 브리칭 계산이 읽고 바꾸는 상태. 표면과 작업 사이를 한 덩어리로 오간다. */
struct BREACHSYSTEM_API FBreachSurfaceModel
{
	FBreachSurfaceState SurfaceState;
	// 이동 충돌용 구멍 캐시와 마지막으로 반영된 Simple 빌드 결과
	FBreachSimpleCollision SimpleCollision;
	// 표면별 누적 피해 상태
	FBreachDamageAccumulator DamageAccumulator;
	FBreachCellGrid CellGrid;
	// CellGrid를 맞춘 스케일. 피격 스케일과 다르면 누적 기록을 비우고 격자를 다시 맞춘다.
	FVector2d CachedMeshScaleXZ = FVector2d::ZeroVector;
};

/** 한 표면 수명 동안 고정되는 값. 작업에 복사만 하고 되돌려 받지 않는다. */
struct BREACHSYSTEM_API FBreachSurfaceLayout
{
	FPlanarUVMapping SurfaceUVs[2]; // -Y 면, +Y 면 순서
	UE::Geometry::FAxisAlignedBox2d OriginalBounds;
	FVector2d OriginalDepthRange = FVector2d::ZeroVector;
	// 표면을 만들 때 읽은 전역 셀 크기. 수명 중 설정이 바뀌어도 격자 규격을 유지한다.
	double CapturedCellSize = 0;
};
