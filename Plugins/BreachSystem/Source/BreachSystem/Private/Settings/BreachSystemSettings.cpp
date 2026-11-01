#include "Settings/BreachSystemSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachSystemSettings, Log, All);

UBreachSystemSettings::UBreachSystemSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("BreachSystem");
}

float UBreachSystemSettings::GetCellSizeCm()
{
	const UBreachSystemSettings* Settings = GetDefault<UBreachSystemSettings>();
	checkf(Settings, TEXT("BreachSystemSettings default object is required to read the global cell size."));
	if (!FMath::IsFinite(Settings->CellSizeCm) || Settings->CellSizeCm < 0.1f)
	{
		UE_LOG(LogBreachSystemSettings, Warning,
			TEXT("Invalid CellSizeCm (%g) in [/Script/BreachSystem.BreachSystemSettings]. Set a finite value >= 0.1 cm in Project Settings > Plugins > Breach System. Using 10 cm."),
			Settings->CellSizeCm);
		return 10.f;
	}
	return Settings->CellSizeCm;
}
