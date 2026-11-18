#pragma once

#include "Shape/BreachContour.h"
#include "Curve/GeneralPolygon2.h"

class UBreachPattern;

namespace BreachGrainFracture
{
	struct FGuideSegment
	{
		FVector2d A;
		FVector2d B;
		int32 PathIndex;
	};

	struct FVertex
	{
		FVector2d Reference;
		FVector2d Position;
		bool bJunction = false;
		bool bFixed = false;
	};

	/** Append one randomized short-end profile, including its baseline endpoints. */
	void AppendEndProfile(int32 MaxSplinters, double Reach, double Span, FRandomStream& Random, TArray<FVector2d>& OutProfile);

	/** Assemble staggered ends and open-guide paths. Empty success means no end intervals; use free grain contour. */
	bool Build(const BreachContour::FPreparedContour& Prepared, const UE::Geometry::FPolygon2d& Noisy, TConstArrayView<FGuideSegment> Guides, double Influence, double Sharpness, int32 MaxSplinters, TArray<FVertex>& Out, FString& OutError);

	bool BuildImpact(const UBreachPattern& Pattern, const UE::Geometry::FPolygon2d& Base, TConstArrayView<FGuideSegment> Guides, int32 Seed, double Influence, TArray<UE::Geometry::FGeneralPolygon2d>& Out, FString& OutError);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 같은 경로의 급한 굽힘 및 역방향 탐색을 실제 추적 함수로 검증한다.
	bool TraceGuideForTest(TConstArrayView<FGuideSegment> Guides, int32 Segment, double Alpha, double Distance, FVector2d& End, TArray<FVector2d>& Bends);
#endif
}
