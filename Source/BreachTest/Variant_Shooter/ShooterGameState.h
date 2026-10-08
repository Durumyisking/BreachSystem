// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ShooterGameState.generated.h"

USTRUCT()
struct FTeamScore
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 TeamByte = 0;

	UPROPERTY()
	int32 Score = 0;
};

DECLARE_MULTICAST_DELEGATE(FOnTeamScoresChanged);

UCLASS(abstract)
class BREACHTEST_API AShooterGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** Increases the score for the given team */
	void IncrementTeamScore(uint8 TeamByte);

	const TArray<FTeamScore>& GetTeamScores() const
	{
		return TeamScores;
	}

	FOnTeamScoresChanged OnTeamScoresChanged;


protected:
	UPROPERTY(ReplicatedUsing = OnRep_TeamScores)
	TArray<FTeamScore> TeamScores;

	UFUNCTION()
	void OnRep_TeamScores();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
