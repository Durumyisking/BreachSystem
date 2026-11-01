// Copyright Epic Games, Inc. All Rights Reserved.

#include "BreachSystem.h"
#include "Debug/BreachCellDebug.h"

#define LOCTEXT_NAMESPACE "FBreachSystemModule"

FBreachSystemModule::FBreachSystemModule() = default;
FBreachSystemModule::~FBreachSystemModule() = default;

void FBreachSystemModule::StartupModule()
{
	CellDebug = MakeUnique<FBreachCellDebug>();
}

void FBreachSystemModule::ShutdownModule()
{
	CellDebug.Reset();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBreachSystemModule, BreachSystem)
