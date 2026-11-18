#pragma once
#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"
class UBreachPattern;
class SBreachPatternCanvas;
class FBreachPatternEditor : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	virtual ~FBreachPatternEditor() override;
	void Init(UBreachPattern* InPattern);
	virtual FName GetToolkitFName() const override { return TEXT("BreachPatternEditor"); }
	virtual FText GetBaseToolkitName() const override { return FText::FromString(TEXT("파괴 패턴 편집기")); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("파괴 패턴"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(.1f,.6f,.7f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	virtual void SaveAsset_Execute() override;
private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
	FReply SetAuthoring(bool bTexture);
	bool ApplyTexture();
	void Refresh();
	TWeakObjectPtr<UBreachPattern> Pattern;
	TSharedPtr<SBreachPatternCanvas> Canvas, Preview;
	TArray<TSharedPtr<FString>> Tools;
	FString Status;
	FString ActiveTool = TEXT("영역 그리기");
};
