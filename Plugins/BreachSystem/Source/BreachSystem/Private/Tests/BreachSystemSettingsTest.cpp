#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
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

#endif
