#include "BreachPatternFactory.h"
#include "Surface/BreachPattern.h"

UBreachPatternFactory::UBreachPatternFactory()
{
	SupportedClass = UBreachPattern::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UBreachPatternFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (!ensureMsgf(Class && InParent, TEXT("Pattern creation requires a class and destination package.")))
	{
		return nullptr;
	}
	return NewObject<UBreachPattern>(InParent, Class, Name, Flags | RF_Transactional);
}
