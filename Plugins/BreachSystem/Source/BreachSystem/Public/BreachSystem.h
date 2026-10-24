// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FBreachSystemModule : public IModuleInterface
{
public:

	/** 모듈 로드 후 호출되는 초기화 훅이다. 현재 추가 초기화 동작은 없다. */
	virtual void StartupModule() override;
	/** 모듈 종료·언로드 전에 호출되는 정리 훅이다. 현재 추가 정리 동작은 없다. */
	virtual void ShutdownModule() override;
};
