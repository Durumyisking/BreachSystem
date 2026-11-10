#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"

struct FKAggregateGeom;

/** 전달받은 표면 영역에서 면적 조건을 적용한 이동 충돌을 파생한다. */
class BREACHSYSTEM_API FBreachSimpleCollision
{
public:
	void Initialize(const UE::Geometry::FPolygon2d& Outer, const FVector2d& Depth);

	/** 면적 조건을 적용한 충돌 영역을 볼록 기둥으로 만든다. 입력 영역은 변경하거나 보관하지 않는다. */
	bool BuildMesh(TConstArrayView<UE::Geometry::FGeneralPolygon2d> RemainingRegions, const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError) const;

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

	UE::Geometry::FPolygon2d OriginalOuter;
	FVector2d DepthRange = FVector2d::ZeroVector;
#if WITH_EDITOR
	// Editor 진단용: 표면 수명마다 최초 실패 입력만 보존한다.
	mutable bool bFailureSnapshotSaved = false;
#endif
};
