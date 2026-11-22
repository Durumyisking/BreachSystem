#pragma once

#include "CoreMinimal.h"
#include "Polygon2.h"
#include "Data/BreachContourOptions.h"

namespace BreachContour
{
	struct FPreparedSample
	{
		FVector2d Point = FVector2d::ZeroVector;
		FVector2d Normal = FVector2d::ZeroVector;
		FVector2d JitterBefore = FVector2d::ZeroVector;
		FVector2d JitterAfter = FVector2d::ZeroVector;
		double Distance = 0;
		double DistanceBefore = 0;
		double DistanceAfter = 0;
		double MaxDisplacement = 0;
	};

	/** Source-owned geometry and worst-case movement limits, independent of the random seed. */
	struct FPreparedContour
	{
		UE::Geometry::FPolygon2d Base;
		FBreachContourOptions Options;
		TArray<FPreparedSample> Samples;
		double Perimeter = 0;
		double CutterSize = 0;
	};

	/** 기본 다각형과 설정을 검증하고 샘플·이동 한계를 준비한다. 무작위 윤곽 생성은 하지 않으며, 실패 이유는 OutError로 반환한다. */
	BREACHSYSTEM_API bool Prepare(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
		FPreparedContour& OutPrepared, FString& OutError);

	/** Prepare에 성공한 상태에서 윤곽을 생성한다. 추가 기하 검증·이웃 탐색은 생략하며, Seed 0은 호출마다 새 난수를 사용한다. */
	BREACHSYSTEM_API void Generate(const FPreparedContour& Prepared, UE::Geometry::FPolygon2d& OutPolygon);

	/** 정점 수 4~128, 깊이·변위 비율 0~0.45, 유한한 비음수 간격 변형값과 지원 패턴을 검사한다. 실패 이유는 OutError로 반환한다. */
	BREACHSYSTEM_API bool ValidateOptions(const FBreachContourOptions& Options, FString& OutError);

	/** 정점 수·유한 좌표·변 길이·면적을 검사한다. 자기 교차는 검사하지 않으며, 실패 이유는 OutError로 반환한다. */
	BREACHSYSTEM_API bool Validate(const UE::Geometry::FPolygon2d& Polygon, FString& OutError);

	/**
	 * 준비와 윤곽 생성을 한 번에 수행한다. 깊이 비율은 기본 커터의 짧은 변 기준이다.
	 * 변형 비활성 시 검증 없이 Base를 복사한다. 준비 실패 시 결과를 비우고 OutError와 false를 반환한다.
	 */
	BREACHSYSTEM_API bool Generate(const UE::Geometry::FPolygon2d& Base, const FBreachContourOptions& Options,
		UE::Geometry::FPolygon2d& OutPolygon, FString& OutError);
}
