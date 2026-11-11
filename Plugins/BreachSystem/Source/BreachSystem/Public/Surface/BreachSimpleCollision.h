#pragma once

#include "CoreMinimal.h"
#include "BoxTypes.h"
#include "Curve/GeneralPolygon2.h"
#include "PhysicsEngine/AggregateGeom.h"

/** 전달받은 표면 영역에서 면적 조건을 적용한 이동 충돌을 파생한다. */
class BREACHSYSTEM_API FBreachSimpleCollision
{
public:
	void Initialize(const UE::Geometry::FPolygon2d& Outer, const FVector2d& Depth);

	/** 면적 조건을 적용한 충돌 영역을 볼록 기둥으로 만든다. 입력 영역은 변경하거나 보관하지 않는다. */
	bool BuildMesh(TConstArrayView<UE::Geometry::FGeneralPolygon2d> RemainingRegions, const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError) const;

	/**
	 * 확정된 절단을 구멍 캐시에 누적한다. Cutter는 원본 벽으로 자르지 않은 도형이고, DetachedRegions는 이번 절단으로 제거된 고립 영역의 외곽이다.
	 * 실패하면 캐시를 무효화한다. 그 뒤의 충돌은 BuildMesh로 RemainingRegions에서 파생해야 한다.
	 */
	bool ApplyCut(const UE::Geometry::FPolygon2d& Cutter, TConstArrayView<UE::Geometry::FPolygon2d> DetachedRegions, FString& OutError);

	/** 구멍 캐시에서 면적 조건을 적용한 충돌을 만든다. 캐시가 무효이거나 생성에 실패하면 false이며, 호출자는 BuildMesh로 대체한다. */
	bool BuildMeshFromCuts(const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError);

	/** ApplyCut으로 누적한 구멍 캐시를 BuildMeshFromCuts에 쓸 수 있는지 돌려준다. */
	bool HasHoleCache() const
	{
		return bHoleCacheValid;
	}

private:
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 충돌 파생 단계의 출력을 보존해 수치 오류의 발생 지점을 추적한다.
	friend class FBreachGeometryDiagnosisTest;
	TFunction<void(const TCHAR*, TConstArrayView<UE::Geometry::FGeneralPolygon2d>)> GeometryObserver;
	void ObserveGeometry(const TCHAR* Stage, TConstArrayView<UE::Geometry::FGeneralPolygon2d> Regions) const
	{
		if (GeometryObserver)
		{
			GeometryObserver(Stage, Regions);
		}
	}
#endif

	// 벽 안에서 서로 이어진 절단 영역 하나.
	// 원본 벽으로 자르지 않고 보관한다. 잘라 두면 구멍의 변이 벽 외곽선 위에 겹치고, 그 상태로 원본에서 빼면 Clipper가 역방향 겹침 변을 남긴다.
	struct FHoleComponent
	{
		UE::Geometry::FGeneralPolygon2d Polygon;
		UE::Geometry::FAxisAlignedBox2d Bounds;
		// 원본 벽 안쪽에 놓인 면적. 표면 로컬 단위다.
		double SurfaceArea = 0;
		// 도형이 바뀔 때마다 새 값을 받는다. 마지막 충돌을 다시 쓸 수 있는지 판정한다.
		uint64 Revision = 0;
		// 면적 조건을 처음 통과할 때 계산하는 단순화 결과.
		UE::Geometry::FGeneralPolygon2d SimpleCut;
		bool bSimpleCutBuilt = false;
	};

	bool IsInsideOriginal(const UE::Geometry::FAxisAlignedBox2d& Bounds) const;
	bool MeasureSurfaceArea(const UE::Geometry::FGeneralPolygon2d& Polygon, const UE::Geometry::FAxisAlignedBox2d& Bounds, double& OutArea) const;
	void InvalidateHoleCache();

	UE::Geometry::FPolygon2d OriginalOuter;
	FVector2d DepthRange = FVector2d::ZeroVector;

	TArray<FHoleComponent> HoleComponents;
	UE::Geometry::FAxisAlignedBox2d OriginalBounds = UE::Geometry::FAxisAlignedBox2d::Empty();
	// 원본이 축 정렬 사각형일 때만 bounds 비교로 벽 안쪽 여부를 판정한다.
	bool bOriginalIsBox = false;
	bool bHoleCacheValid = false;
	uint64 NextHoleRevision = 1;

	// 마지막으로 만든 충돌과 그때 면적 조건을 통과한 구멍. 통과한 구멍이 같으면 삼각분할을 반복하지 않는다.
	TArray<uint64> BuiltRevisions;
	FKAggregateGeom BuiltCollision;
	bool bBuiltCollisionValid = false;

#if WITH_EDITOR
	// Editor 진단용: 표면 수명마다 최초 실패 입력만 보존한다.
	mutable bool bFailureSnapshotSaved = false;
#endif
};
