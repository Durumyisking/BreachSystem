#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"

struct FKAggregateGeom;

/** 정밀 파손과 별도로 보관하는 이동용 영역. 작은 구멍도 연결 판정을 위해 기록한다. */
class BREACHSYSTEM_API FBreachSimpleCollision
{
public:
	void Initialize(const UE::Geometry::FPolygon2d& Outer, const FVector2d& Depth);

	/** 성공한 절단과 고립 조각만 반영한다. 임계값은 월드 면적(cm^2)이며, 실패 시 기존 상태를 유지한다. */
	bool ApplyCut(const UE::Geometry::FPolygon2d& Cutter, TConstArrayView<UE::Geometry::FPolygon2d> Detached, FString& OutError);
	bool ApplyCutDebug(UObject* WorldContext, const FTransform ComponentTransform, const UE::Geometry::FPolygon2d& Cutter, TConstArrayView<UE::Geometry::FPolygon2d> Detached, FString& OutError);

	/** 이동용 영역을 볼록 기둥으로 만든다. 실제 등록과 물리 갱신은 호출자가 한 번 수행한다. */
	bool BuildMesh(FKAggregateGeom& OutCollision, FString& OutError) const;
	bool IsDirty() const { return bDirty; }
	void ClearDirty() { bDirty = false; }

private:
	struct FHole
	{
		UE::Geometry::FGeneralPolygon2d Polygon;
		UE::Geometry::FAxisAlignedBox2d Bounds;
	};

	UE::Geometry::FPolygon2d OriginalOuter;
	FVector2d DepthRange = FVector2d::ZeroVector;
	TArray<FHole> Holes;
	TArray<UE::Geometry::FGeneralPolygon2d> RemainingRegions;
	bool bDirty = false;
};
