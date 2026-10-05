// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DTTest.h"
#include "DATest.generated.h"


UCLASS(BlueprintType)
class BREACHTEST_API UDATest : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	TArray<UMyClass*> MyClassArray;
	
};
