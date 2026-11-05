#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "Settings/BreachSystemSettings.h"
#include <limits>

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSystemSettingsTest, "BreachSystem.Settings.CellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSystemSettingsTest::RunTest(const FString& Parameters)
{
	UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/BreachSystem.BreachSystemSettings"));
	if (!TestNotNull(TEXT("Global breach settings are registered"), SettingsClass))
	{
		return false;
	}
	UObject* Settings = SettingsClass->GetDefaultObject();
	FFloatProperty* CellSize = FindFProperty<FFloatProperty>(SettingsClass, TEXT("CellSizeCm"));
	UFunction* Getter = SettingsClass->FindFunctionByName(TEXT("GetCellSizeCm"));
	if (!TestNotNull(TEXT("Global settings object"), Settings)
		|| !TestNotNull(TEXT("Configurable cell size"), CellSize)
		|| !TestNotNull(TEXT("Blueprint cell size getter"), Getter))
	{
		return false;
	}
	const float OriginalValue = CellSize->GetPropertyValue_InContainer(Settings);
	const FString ConfigPath = FConfigCacheIni::NormalizeConfigIniPath(
		FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("BreachSettings-"), TEXT(".ini")));
	ON_SCOPE_EXIT
	{
		CellSize->SetPropertyValue_InContainer(Settings, OriginalValue);
		if (GConfig)
		{
			GConfig->UnloadFile(ConfigPath);
		}
		IFileManager::Get().Delete(*ConfigPath);
	};
	const FString ConfigText = TEXT("[/Script/BreachSystem.BreachSystemSettings]\nCellSizeCm=25\n");
	if (!TestTrue(TEXT("Write isolated settings fixture"), FFileHelper::SaveStringToFile(ConfigText, *ConfigPath)))
	{
		return false;
	}
	Settings->LoadConfig(SettingsClass, *ConfigPath);
	auto ReadCellSize = [Settings, Getter]()
	{
		checkf(Settings && Getter, TEXT("The reflected settings getter must exist before invocation."));
		struct FGetterResult { float ReturnValue = 0; } Result;
		Settings->ProcessEvent(Getter, &Result);
		return Result.ReturnValue;
	};
	TestEqual(TEXT("Config override reaches the Blueprint getter"), ReadCellSize(), 25.f);
	CellSize->SetPropertyValue_InContainer(Settings, 0.1f);
	TestEqual(TEXT("Minimum supported cell size is preserved"), ReadCellSize(), 0.1f);
	for (float Invalid : {0.f, -10.f, 0.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		CellSize->SetPropertyValue_InContainer(Settings, Invalid);
		AddExpectedError(TEXT("Invalid CellSizeCm"), EAutomationExpectedErrorFlags::Contains, 1);
		TestEqual(TEXT("Invalid config uses the safe 10 cm default"), ReadCellSize(), 10.f);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachHoleAreaSettingsTest, "BreachSystem.Settings.HoleArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachHoleAreaSettingsTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Hole area settings"), Settings)) return false;
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 500.0);
	TestEqual(TEXT("Hole threshold uses the existing 500 square cm default"), UBreachSystemSettings::GetComplexHoleSizeThreshold(), 500.0);
	Settings->ComplexHoleSizeThreshold = 0.1;
	TestEqual(TEXT("Minimum supported area is preserved"), UBreachSystemSettings::GetComplexHoleSizeThreshold(), 0.1);
	for (double Invalid : {0.0, -1.0, 0.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		Settings->ComplexHoleSizeThreshold = Invalid;
		AddExpectedError(TEXT("Invalid ComplexHoleSizeThreshold"), EAutomationExpectedErrorFlags::Contains, 1);
		TestEqual(TEXT("Invalid area uses the same 500 square cm default"), UBreachSystemSettings::GetComplexHoleSizeThreshold(), 500.0);
	}
	return !HasAnyErrors();
}

#endif
