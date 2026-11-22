#pragma once

#include "CoreMinimal.h"
#include "BoxTypes.h"
#include "Curve/GeneralPolygon2.h"
#include "PhysicsEngine/AggregateGeom.h"

struct FBreachPreparedOpening;
struct FBreachPassageState;

/** 게임 스레드에서 읽어 작업에 값으로 전달하는 계산 정책. */
struct FBreachSimpleBuildPolicy
{
	FVector2d WorldScale = FVector2d(1, 1);
	double AreaThreshold = 500;
	int32 MaxCount = 3;
	// 통로 가장자리에서 지울 꼭짓점의 기준(월드 cm). 0이면 통로 윤곽을 그대로 쓴다.
	double PassageTolerance = 1;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 같은 도형으로 경계 전수 검사와 AABB 검색 비용을 비교한다.
	bool bLinearBoundaryQueries = false;
	// 테스트 전용: 증분 볼록 껍질을 엔진의 전체 재계산과 비교한다.
	bool bRebuildTrialHulls = false;
	// 테스트 전용: 동일 입력으로 경계 검사 결과 캐시의 동등성과 비용을 비교한다.
	bool bDisableBoundaryCache = false;
	// 테스트 전용: 이 변경 번호의 개구부 전처리를 실패시켜, 한 개구부의 실패가 다른 개구부를 막지 않는지 검증한다.
	uint64 FailPrepareRevision = 0;
	// 테스트 전용: 이 변경 번호의 개구부가 고정되지 않은 통로 계산을 실패시켜, 원인 개구부만 고정되는지 검증한다.
	uint64 FailSolveRevision = 0;
#endif

	bool operator==(const FBreachSimpleBuildPolicy& Other) const
	{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		if (bLinearBoundaryQueries != Other.bLinearBoundaryQueries || bRebuildTrialHulls != Other.bRebuildTrialHulls || bDisableBoundaryCache != Other.bDisableBoundaryCache)
		{
			return false;
		}
		if (FailPrepareRevision != Other.FailPrepareRevision || FailSolveRevision != Other.FailSolveRevision)
		{
			return false;
		}
#endif
		return WorldScale == Other.WorldScale && AreaThreshold == Other.AreaThreshold && MaxCount == Other.MaxCount && PassageTolerance == Other.PassageTolerance;
	}
};

/** 실제 런타임 프로파일링과 동일 피격 기록의 계산 비용 비교에 사용한다. */
struct FBreachSimpleBuildStats
{
	uint64 BooleanOperations = 0;
	uint64 BoundaryTests = 0;
	uint64 BoundaryNodeVisits = 0;
	int32 PreparedOpenings = 0;
	int32 ReusedBodies = 0;
	int32 PassagePieceCount = 0;
	// 계산에 실패해 이전 통로만 유지한 개구부 수. 0보다 크면 빌드는 성공했지만 일부 구멍이 열리지 않았다.
	int32 FrozenOpenings = 0;
	double DiscardedWorldArea = 0;
	double GeometryMs = 0;
	bool bCollisionChanged = false;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 경계 인덱스의 동일 입력 비교에만 사용한다.
	bool bLinearBoundaryQueries = false;
	// 테스트 전용: 증분 갱신의 결과 동등성 비교에 사용한다.
	bool bRebuildTrialHulls = false;
	// 테스트 전용: 캐시 유무의 형상 동등성·검색 절감·임시 메모리를 비교한다.
	bool bDisableBoundaryCache = false;
	uint64 PointCacheHits = 0;
	uint64 EdgeCacheHits = 0;
	uint64 ExactCacheHits = 0;
	uint64 BoundaryCacheBytes = 0;
#endif
};

/** 전달받은 표면 영역에서 면적 조건을 적용한 이동 충돌을 파생한다. */
class BREACHSYSTEM_API FBreachSimpleCollision
{
public:
	void Initialize(const UE::Geometry::FPolygon2d& Outer, const FVector2d& Depth);

	/** 면적 조건을 적용한 충돌 영역을 볼록 기둥으로 만든다. 입력 영역은 변경하거나 보관하지 않는다. */
	bool BuildMesh(TConstArrayView<UE::Geometry::FGeneralPolygon2d> RemainingRegions, const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError) const;

	/**
	 * 확정된 절단을 구멍 캐시에 누적한다. Cutter는 원본 벽으로 자르지 않은 도형이고, DetachedRegions는 이번 절단으로 제거된 고립 영역의 외곽이다.
	 * 실패하면 캐시를 무효화한다. 호출자는 RebuildHoleCache로 캐시를 다시 만들어야 이동 충돌이 계속 갱신된다.
	 */
	bool ApplyCut(const UE::Geometry::FPolygon2d& Cutter, TConstArrayView<UE::Geometry::FPolygon2d> DetachedRegions, FString& OutError);

	/**
	 * 무효화된 구멍 캐시를 현재 남은 영역에서 다시 만든다. 구멍은 원본 벽 안쪽으로 잘린 도형이 되고 모두 새 변경 번호를 받는다.
	 * 실패하면 캐시는 무효인 채로 남으며 다음 절단에서 다시 시도할 수 있다.
	 */
	bool RebuildHoleCache(TConstArrayView<UE::Geometry::FGeneralPolygon2d> RemainingRegions, FString& OutError);

	/** 구멍 캐시에서 면적 조건을 적용한 충돌을 만든다. 캐시가 무효이거나 생성에 실패하면 false다. */
	bool BuildMeshFromCuts(const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError);

	/**
	 * 독립적인 복사본에서 호출할 수 있다. UObject나 전역 설정을 읽지 않는다.
	 * 이번에 바뀐 개구부의 계산이 실패하면 그 개구부만 이전 통로로 고정하고 true를 돌려준다.
	 * 이때 통계의 FrozenOpenings가 0보다 크고 OutError에 첫 실패 사유가 남는다.
	 */
	bool BuildMeshFromCuts(const FBreachSimpleBuildPolicy& Policy, FKAggregateGeom& OutCollision, FString& OutError);

	/** 실제 초기 차단 도형을 캐시에 등록한다. 구멍 캐시는 변경하지 않는다. */
	void InitializeCollision(const FBreachSimpleBuildPolicy& Policy, const FKAggregateGeom& Collision);

	/** 완료된 파생 상태만 받아들인다. 현재 절단 도형과 변경 번호는 덮어쓰지 않는다. */
	void CommitBuild(const FBreachSimpleCollision& Completed);

	uint64 GetRevision() const { return NextHoleRevision; }
	const FBreachSimpleBuildStats& GetLastBuildStats() const { return LastBuildStats; }

	/** ApplyCut으로 누적한 구멍 캐시를 BuildMeshFromCuts에 쓸 수 있는지 돌려준다. */
	bool HasHoleCache() const
	{
		return bHoleCacheValid;
	}

	const UE::Geometry::FPolygon2d& GetOuter() const
	{
		return OriginalOuter;
	}

	/**
	 * 추적 중인 구멍을 변경 번호·도형·경계와 함께 방문한다. 번호가 그대로인 구멍은 도형도 그대로다.
	 * 도형은 원본 벽 밖으로 나갈 수 있으므로 벽 안쪽 형상이 필요하면 호출자가 자른다.
	 */
	void ForEachHole(TFunctionRef<void(uint64 Revision, const UE::Geometry::FGeneralPolygon2d& Polygon, const UE::Geometry::FAxisAlignedBox2d& Bounds)> Visit) const;

private:
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 충돌 파생 단계의 출력을 보존해 수치 오류의 발생 지점을 추적한다.
	friend class FBreachGeometryDiagnosisTest;
	// 테스트 전용: 선택된 통과 영역의 볼록성, 포함 관계, 면적과 개수 제한을 검증한다.
	friend class FBreachSimpleConvexTest;
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
		// 같은 도형·월드 배율·면적 기준에서 공유하는 불변 전처리 결과.
		TSharedPtr<const FBreachPreparedOpening, ESPMode::ThreadSafe> PreparedOpening;
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
	double BuiltAreaThreshold = 0;
	int32 BuiltMaxConvexCount = 0;
	double BuiltPassageTolerance = 0;
	// 마지막으로 만든 충돌이 실제로 열어 둔 통로(단순화한 윤곽, 월드 좌표). 다음 단순화가 이 영역을 다시 막지 않게 한다.
	TArray<UE::Geometry::FGeneralPolygon2d> BuiltPassages;
	FVector2d BuiltWorldScale = FVector2d::ZeroVector;
	TSharedPtr<const FBreachPassageState, ESPMode::ThreadSafe> PassageState;
	FBreachSimpleBuildStats LastBuildStats;

#if WITH_EDITOR
	// Editor 진단용: 표면 수명마다 최초 실패 입력만 보존한다.
	mutable bool bFailureSnapshotSaved = false;
#endif
};
