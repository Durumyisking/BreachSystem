// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DTTest.generated.h"


UCLASS(BlueprintType, Blueprintable)
class BREACHTEST_API UMyClass : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MyClass")
	int32 MyIntProperty;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MyClass")
	float MyFloatProperty;
};




USTRUCT(BlueprintType)
struct BREACHTEST_API FDTTest : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced)
	TArray<UMyClass*> MyClassArray;
	
};
