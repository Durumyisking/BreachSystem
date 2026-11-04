// Copyright Epic Games, Inc. All Rights Reserved.

#include "BreachSystem.h"
#if WITH_EDITOR
#include "Debug/BreachCellDebug.h"
#endif

#define LOCTEXT_NAMESPACE "FBreachSystemModule"

FBreachSystemModule::FBreachSystemModule() = default;
FBreachSystemModule::~FBreachSystemModule() = default;

void FBreachSystemModule::StartupModule()
{
#if WITH_EDITOR
	CellDebug = MakeUnique<FBreachCellDebug>();
#endif
}

void FBreachSystemModule::ShutdownModule()
{
#if WITH_EDITOR
	CellDebug.Reset();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBreachSystemModule, BreachSystem)
