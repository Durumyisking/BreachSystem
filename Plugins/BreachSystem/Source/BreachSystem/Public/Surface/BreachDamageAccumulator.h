#pragma once

#include "CoreMinimal.h"
#include "Polygon2.h"
#include "BreachDamageAccumulator.generated.h"

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachAccumulationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnabled = false;

	/** Physical dimensions of the moving damage window. Changing settings clears old records. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1", Units="cm", EditCondition="bEnabled"))
	FVector2D WindowSize = FVector2D(40.f, 40.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.001", EditCondition="bEnabled"))
	float BreachHP = 1.f;
};

// 탄착 한 건의 누적 기록
struct FBreachDamageRecord
{
	int32 ID = INDEX_NONE;
	FVector2d Point = FVector2d::ZeroVector;
	double Damaged = 0;
};

// 임계값을 채운 파손 후보 묶음
struct FBreachAccumulationCandidate
{
	/** 절삭 전에 고정한 좌표. 다른 후보가 공유 기록을 제거해도 유효하다. */
	TArray<FVector2d> ImpactPoints;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	/** 테스트 전용: 후보의 탐색 범위, 공유 탄착, 임계값 충족을 검증한다. */
	UE::Geometry::FAxisAlignedBox2d Bounds;
	TArray<int32> RecordIDs;
	double Contribution = 0;
#endif
};

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
/** 테스트 전용: 공간 조회와 후보 탐색의 작업량을 검증한다. */
struct FBreachAccumulationQueryStats
{
	int32 VisitedCells = 0;
	int32 ExaminedRecords = 0;
	int64 MembershipEvaluations = 0;
};
#endif

/** Surface-owned weighted records. Buckets accelerate queries without anchoring damage windows. */
class BREACHSYSTEM_API FBreachDamageAccumulator
{
public:
	/** 창 크기와 임계값을 설정한다. 크기는 각 축 0.1cm 이상, 임계값은 양의 유한값이어야 하며, 설정 변경 시 기존 기록을 비운다. */
	bool Configure(const FVector2d& InWindowSize, double InThreshold, FString& OutError);
	/** Configure 이후 cm 좌표와 양의 기여도를 기록하고 ID를 반환한다. 잘못된 입력은 OutError와 INDEX_NONE으로 알린다. */
	int32 AddHit(const FVector2d& Point, double BreachDamage, FString& OutError);
	/** 새 기록을 포함하는 창들을 탐색해 임계값을 채운 후보를 반환한다. 기록은 소비하지 않아 후보 간 공유될 수 있다. */
	void FindCandidates(int32 NewRecordID, TArray<FBreachAccumulationCandidate>& OutCandidates
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		/** 테스트 전용: 탐색 비용을 검증할 때만 통계를 요청한다. */
		, FBreachAccumulationQueryStats* OutStats = nullptr
#endif
		) const;
	/** 누적 기록과 같은 cm 좌표계의 다각형 내부·경계 기록을 제거한다. */
	void RemoveInside(const UE::Geometry::FPolygon2d& Polygon
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		/** 테스트 전용: 삭제 범위의 공간 조회 비용을 검증한다. */
		, FBreachAccumulationQueryStats* OutStats = nullptr
#endif
		);
	/** 모든 기록·공간 인덱스·다음 ID를 초기화한다. 설정한 창 크기와 임계값은 유지한다. */
	void Reset();
	/** 현재 보관 중인 피격 기록 수를 반환한다. */
	int32 Num() const;

private:
	struct FColumn
	{
		TArray<int32> Rows;
		TMap<int32, TArray<int32>> Cells;
	};

	/** cm 좌표를 창 크기의 격자 셀로 변환한다. 정수 범위를 벗어난 좌표는 경곗값으로 제한한다. */
	FIntPoint CellFor(const FVector2d& Point) const;
	/** Bounds와 겹치는 점유 셀만 방문해 범위 안의 기록을 모은다. 출력 배열은 매 호출 초기화한다. */
	void Query(const UE::Geometry::FAxisAlignedBox2d& Bounds, TArray<FBreachDamageRecord>& OutRecords
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		/** 테스트 전용: 공간 조회 비용을 수집한다. */
		, FBreachAccumulationQueryStats* OutStats = nullptr
#endif
		) const;

	TMap<int32, FBreachDamageRecord> Records;
	TMap<int32, FColumn> Columns;
	TArray<int32> OccupiedColumns;
	FVector2d WindowSize = FVector2d::ZeroVector;
	double Threshold = 1;
	int32 NextRecordID = 0;
};
