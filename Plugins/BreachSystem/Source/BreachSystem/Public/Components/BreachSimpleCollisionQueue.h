#pragma once

#include "CoreMinimal.h"
#include "Geometry/BreachSimpleCollision.h"

class UBreachSimpleProxy;
struct FBreachSimpleBuildJob;

/**
 * 표면 하나의 Simple 충돌 빌드 대기열. 한 표면 수명에는 실행 작업 하나와 최신 변경을 나타내는 대기 표시만 둔다.
 * 계산은 작업 스레드에서, Cooking 요청과 반영은 게임 스레드에서 한다. 표면 값은 호출마다 FInput으로 받는다.
 */
class FBreachSimpleCollisionQueue
{
public:
	/** 대기열이 호출마다 표면에서 받는 현재 값. 게임 스레드에서 만들고 그 호출 안에서만 쓴다. */
	struct FInput
	{
		// 표면의 구멍 캐시와 마지막 Simple 결과. 스냅샷을 떠서 계산하고, 반영할 때 완료된 파생 상태만 합친다.
		FBreachSimpleCollision& Collision;
		UBreachSimpleProxy* Proxy = nullptr;
		// 설정을 읽으면 경고가 날 수 있어 정책은 미리 만들지 않고, 원래 읽던 시점에 이 스케일로 만든다.
		FVector ComponentScale = FVector::OneVector;
		bool bRegistered = false;
		// 월드가 있고 해체 중이 아니다.
		bool bWorldAlive = false;
		FString SurfaceName;
		// 게임 스레드에서 계산·Cooking 완료를 표면에 돌려준다. 표면이 사라졌으면 아무것도 하지 않아 결과를 버린다.
		TFunction<void(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job)> OnBuilt;
		TFunction<void(const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job, bool bCookSucceeded)> OnCooked;
	};

	/** 표면 스케일과 전역 설정으로 계산 정책을 만든다. 게임 스레드에서만 읽는다. */
	static FBreachSimpleBuildPolicy MakePolicy(const FVector& ComponentScale);

	/** 최신 구멍 캐시의 빌드를 요청한다. 실행 중인 작업이 있으면 대기 표시만 남겨, 끝난 뒤 한 번에 다시 빌드한다. */
	void Queue(const FInput& Input);
	/** 작업 스레드 계산이 끝난 작업을 받는다. 반영할 충돌이 바뀌었으면 Cooking을 요청하고, 아니면 바로 반영 단계로 넘긴다. */
	void CompleteBuild(const FInput& Input, const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job);
	/** Cooking이 끝난 작업을 반영하고, 그동안 쌓인 변경이 있으면 다음 작업을 시작한다. */
	void CompleteCook(const FInput& Input, const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job, bool bCookSucceeded);
	/** 표면 수명이 바뀌었다. 진행 중인 작업은 끝날 때까지 두되 결과는 반영하지 않는다. */
	void Invalidate();

private:
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 반영된 변경 번호와 대기 상태를 읽어 실제 완료를 기다린다.
	friend struct FBreachSimpleAsyncTestAccess;
#endif

	bool CanPublish(const FInput& Input, const TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe>& Job) const;

	TSharedPtr<FBreachSimpleBuildJob, ESPMode::ThreadSafe> BuildJob;
	uint64 Generation = 0;
	uint64 CompletedRevision = 0;
	FBreachSimpleBuildPolicy CompletedPolicy;
	bool bPending = false;
	double PendingSince = 0;
};
