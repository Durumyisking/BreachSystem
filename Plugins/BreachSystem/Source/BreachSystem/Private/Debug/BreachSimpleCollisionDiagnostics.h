#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"

#if WITH_EDITOR
namespace BreachSimpleCollisionDiagnostics
{
	// Editor 진단용: 동기 저장 중에만 참조하는 실제 계산 단계와 설정값이다.
	struct FFailureSnapshot
	{
		const UE::Geometry::FPolygon2d& OriginalOuter;
		FVector2d DepthRange;
		TConstArrayView<UE::Geometry::FGeneralPolygon2d> RemainingRegions;
		TConstArrayView<UE::Geometry::FGeneralPolygon2d> Holes;
		TConstArrayView<UE::Geometry::FGeneralPolygon2d> SimpleCuts;
		TConstArrayView<UE::Geometry::FGeneralPolygon2d> CollisionRegions;
		double AreaThreshold;
		double Bias;
		bool bFinalDifferenceApplied;
		int32 FailedRegionIndex;
		const FString& Error;
	};

	FString SerializeSnapshot(const FFailureSnapshot& Snapshot);
	bool SaveSnapshot(const FFailureSnapshot& Snapshot, const FString& Directory, FString& OutPath);
}
#endif
