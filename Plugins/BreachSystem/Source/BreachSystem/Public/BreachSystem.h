// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"

class FBreachCellDebug;

class FBreachSystemModule : public IModuleInterface
{
public:
	FBreachSystemModule();
	virtual ~FBreachSystemModule() override;

	/** 셀 격자 콘솔 명령과 시각화 수명을 등록한다. */
	virtual void StartupModule() override;
	/** 콘솔 명령, ticker와 이 모듈의 디버그 선만 정리한다. */
	virtual void ShutdownModule() override;

private:
	TUniquePtr<FBreachCellDebug> CellDebug;
};
