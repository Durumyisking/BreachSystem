#pragma once

// 테스트 전용: 피해를 준 직후 결과를 확인하는 테스트가 비동기 브리칭 설정과 관계없이 그 자리에서 반영되게 한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Data/BreachSystemSettings.h"
#include "Templates/UnrealTemplate.h"

struct FBreachSyncBreachGuard
{
	TGuardValue<bool> Guard;

	FBreachSyncBreachGuard()
		: Guard(GetMutableDefault<UBreachSystemSettings>()->bAsyncBreach, false)
	{
	}
};
#endif
