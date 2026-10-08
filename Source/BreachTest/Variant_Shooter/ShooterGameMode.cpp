// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

#include "ShooterGameState.h"

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamByte)
{
	if (AShooterGameState* GS = GetGameState<AShooterGameState>())
	{
		GS->IncrementTeamScore(TeamByte); // 일반 함수 호출
	}
}
