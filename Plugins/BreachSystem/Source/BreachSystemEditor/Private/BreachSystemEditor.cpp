#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_Base.h"
#include "BreachPatternEditor.h"
#include "Surface/BreachPattern.h"

class FBreachPatternAssetActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return FText::FromString(TEXT("Breach Pattern")); }
	virtual FColor GetTypeColor() const override { return FColor::Cyan; }
	virtual UClass* GetSupportedClass() const override { return UBreachPattern::StaticClass(); }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }
	virtual void OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host) override
	{
		for (UObject* Object : Objects)
		{
			UBreachPattern* Pattern = Cast<UBreachPattern>(Object);
			if (Pattern)
			{
				MakeShared<FBreachPatternEditor>()->Init(Pattern);
			}
		}
	}
};

class FBreachSystemEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Actions = MakeShared<FBreachPatternAssetActions>();
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().RegisterAssetTypeActions(Actions.ToSharedRef());
	}
	virtual void ShutdownModule() override
	{
		if (Actions.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
		{
			FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().UnregisterAssetTypeActions(Actions.ToSharedRef());
		}
		Actions.Reset();
	}
private:
	TSharedPtr<IAssetTypeActions> Actions;
};

IMPLEMENT_MODULE(FBreachSystemEditorModule, BreachSystemEditor)
