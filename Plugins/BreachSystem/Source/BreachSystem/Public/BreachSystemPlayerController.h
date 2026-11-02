// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BreachSystemPlayerController.generated.h"

UCLASS()
class BREACHSYSTEM_API ABreachSystemPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** 마우스 커서 표시와 클릭 이벤트를 활성화한다. */
	ABreachSystemPlayerController();


};
