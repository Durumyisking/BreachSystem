#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "BreachPatternFactory.generated.h"

UCLASS()
class UBreachPatternFactory : public UFactory
{
	GENERATED_BODY()

public:
	UBreachPatternFactory();
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
