// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

#include "Net/UnrealNetwork.h"


void AShooterGameState::IncrementTeamScore(uint8 TeamByte)
{
	// retrieve the team score if any
	FTeamScore* TeamScore = TeamScores.FindByPredicate([TeamByte](const FTeamScore& TeamScore) {
		return TeamScore.TeamByte == TeamByte;
	});

	if (!TeamScore)
	{
		TeamScore = &TeamScores.AddDefaulted_GetRef();
		TeamScore->TeamByte = TeamByte;
	}
	++TeamScore->Score;

	// 서버에서는 C++ 값 변경만으로 OnRep가 호출되지 않음.
	// 리슨 서버의 로컬 UI / 싱글플레이 UI를 위한 알림.
	OnTeamScoresChanged.Broadcast();
}
void AShooterGameState::OnRep_TeamScores()
{
	OnTeamScoresChanged.Broadcast();
}

void AShooterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterGameState, TeamScores);
}

