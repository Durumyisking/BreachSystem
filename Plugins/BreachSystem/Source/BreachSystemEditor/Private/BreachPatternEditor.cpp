#include "BreachPatternEditor.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "BreachPatternCanvas.h"
#include "BreachPatternImport.h"
#include "Surface/BreachPatternGeometry.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

namespace { const FName PatternTab(TEXT("BreachPatternCanvas")); }
FBreachPatternEditor::~FBreachPatternEditor()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}
void FBreachPatternEditor::Init(UBreachPattern* InPattern)
{
	if (!ensure(InPattern))
	{
		return;
	}
	Pattern = InPattern;
	InPattern->SetFlags(RF_Transactional);
	Tools = {MakeShared<FString>(TEXT("영역 그리기")), MakeShared<FString>(TEXT("결선 그리기")), MakeShared<FString>(TEXT("점·선 선택 / 이동"))};
	ActiveTool = *Tools[InPattern->Mode == EBreachPatternMode::GrainGuides ? 1 : 0];
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	const auto Layout = FTabManager::NewLayout(TEXT("BreachPatternEditor_v2"))->AddArea(FTabManager::NewPrimaryArea()->Split(FTabManager::NewStack()->AddTab(PatternTab, ETabState::OpenedTab)->SetHideTabWell(true)));
	InitAssetEditor(EToolkitMode::Standalone, nullptr, TEXT("BreachPatternEditor"), Layout, true, true, InPattern);
}
void FBreachPatternEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	FAssetEditorToolkit::RegisterTabSpawners(Manager);
	Manager->RegisterTabSpawner(PatternTab, FOnSpawnTab::CreateSP(this, &FBreachPatternEditor::SpawnTab)).SetDisplayName(FText::FromString(TEXT("파괴 패턴")));
}
void FBreachPatternEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	Manager->UnregisterTabSpawner(PatternTab);
	FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}
TSharedRef<SDockTab> FBreachPatternEditor::SpawnTab(const FSpawnTabArgs& Args)
{
	UBreachPattern* Data = Pattern.Get();
	if (!ensure(Data))
	{
		return SNew(SDockTab);
	}
	const auto Controls = SNew(SVerticalBox);
	Controls->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("SurfaceMaterial")))];
	Controls->AddSlot().AutoHeight().Padding(4)
	[
		SNew(SObjectPropertyEntryBox).AllowedClass(UBreachSurfaceMaterial::StaticClass())
		.ObjectPath_Lambda([this]() { const auto* P = Pattern.Get(); return P ? P->PreviewSurfaceMaterial.ToSoftObjectPath().ToString() : FString(); })
		.OnShouldFilterAsset_Lambda([](const FAssetData& A) { return !A.PackageName.ToString().StartsWith(TEXT("/BreachSystem/SurfaceMaterial/")); })
		.OnObjectChanged_Lambda([this](const FAssetData& A)
		{
			UBreachPattern* P = Pattern.Get();
			if (!ensure(P))
			{
				return;
			}
			const FScopedTransaction Change(FText::FromString(TEXT("프리뷰 SurfaceMaterial 선택")));
			P->Modify();
			P->PreviewSurfaceMaterial = Cast<UBreachSurfaceMaterial>(A.GetAsset());
			P->MarkPackageDirty();
			Refresh();
		})
	];
	Controls->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("작성 타입")))];
	Controls->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()[SNew(SButton).Text_Lambda([this]() { const auto* P = Pattern.Get(); return FText::FromString(P && P->AuthoringType == EBreachPatternAuthoringType::PointsAndLines ? TEXT("● 점·선 편집") : TEXT("점·선 편집")); }).OnClicked(this, &FBreachPatternEditor::SetAuthoring, false)]
		+ SHorizontalBox::Slot()[SNew(SButton).Text_Lambda([this]() { const auto* P = Pattern.Get(); return FText::FromString(P && P->AuthoringType == EBreachPatternAuthoringType::Texture ? TEXT("● 텍스처 기반") : TEXT("텍스처 기반")); }).OnClicked(this, &FBreachPatternEditor::SetAuthoring, true)]];
	Controls->AddSlot().AutoHeight().Padding(4)
	[
		SNew(SVerticalBox).Visibility_Lambda([this]() { const auto* P = Pattern.Get(); return P && P->AuthoringType == EBreachPatternAuthoringType::PointsAndLines ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Tools).InitiallySelectedItem(Tools[Data->Mode == EBreachPatternMode::GrainGuides ? 1 : 0])
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString())); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				if (Item.IsValid())
				{
					ActiveTool = *Item;
				}
				if (Canvas.IsValid())
				{
					Canvas->SetTool(Tools.IndexOfByKey(Item));
				}
			})
			[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(ActiveTool); })]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SButton).Text(FText::FromString(TEXT("선택 삭제"))).OnClicked_Lambda([this]()
		{
			if (Canvas.IsValid())
			{
				Canvas->DeleteSelection();
			}
			return FReply::Handled();
		})]
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("클릭으로 점 추가 · 시작점을 클릭해 영역 완성\nEnter: 선 완성 · 우클릭 / Esc: 작성 취소\nShift: 가로·세로 고정\n첫 클릭: 선택 · 선택된 점/선을 다시 눌러 드래그: 이동")))]
	];
	Controls->AddSlot().AutoHeight().Padding(4)
	[
		SNew(SVerticalBox).Visibility_Lambda([this]() { const auto* P = Pattern.Get(); return P && P->AuthoringType == EBreachPatternAuthoringType::Texture ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("규칙 텍스처 · 색상 ID 맵")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SObjectPropertyEntryBox).AllowedClass(UTexture2D::StaticClass()).ObjectPath_Lambda([this]() { const auto* P = Pattern.Get(); return P && P->RuleTexture ? P->RuleTexture->GetPathName() : FString(); })
			.OnObjectChanged_Lambda([this](const FAssetData& A)
			{
				UBreachPattern* P = Pattern.Get();
				if (!ensure(P))
				{
					return;
				}
				const FScopedTransaction C(FText::FromString(TEXT("규칙 텍스처 선택")));
				P->Modify();
				P->RuleTexture = Cast<UTexture2D>(A.GetAsset());
				P->MarkPackageDirty();
				ApplyTexture();
			})]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("영역마다 중복되지 않는 단색을 사용합니다.\n밝기나 사진의 무늬를 분석하지 않습니다.")))]
	];
	const TSharedRef<SVerticalBox> ImpactControls = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			const UBreachPattern* P = Pattern.Get();
			return P && P->ImpactGrain.bEnabled ? EVisibility::Visible : EVisibility::Collapsed;
		});
	const auto AddImpactOption = [this, ImpactControls](const TCHAR* Label, float FBreachImpactGrainOptions::* Field, float Max)
	{
		const FText Name = FText::FromString(Label);
		ImpactControls->AddSlot().AutoHeight()[SNew(STextBlock).Text(Name)];
		ImpactControls->AddSlot().AutoHeight().Padding(0,4)
		[
			SNew(SNumericEntryBox<float>).AllowSpin(false).MinValue(0.f).MaxValue(Max)
			.Value_Lambda([this, Field]() -> TOptional<float>
			{
				const UBreachPattern* P = Pattern.Get();
				return P ? TOptional<float>(P->ImpactGrain.*Field) : TOptional<float>();
			})
			.OnValueCommitted_Lambda([this, Field, Max, Name](float Value, ETextCommit::Type)
			{
				UBreachPattern* P = Pattern.Get();
				if (!P || !P->ImpactGrain.bEnabled || !FMath::IsFinite(Value) || Value < 0 || Value > Max || P->ImpactGrain.*Field == Value)
				{
					return;
				}
				const FScopedTransaction Change(Name);
				P->Modify();
				P->ImpactGrain.*Field = Value;
				if (Field == &FBreachImpactGrainOptions::MinEndSharpness && Value > P->GrainEndSharpness)
				{
					P->GrainEndSharpness = Value;
				}
				P->MarkPackageDirty();
				Status.Reset();
				Refresh();
			})
		];
	};
	AddImpactOption(TEXT("찢김 시작 확률"), &FBreachImpactGrainOptions::StartProbability, 1.f);
	AddImpactOption(TEXT("찢김 지속성"), &FBreachImpactGrainOptions::Persistence, 1.f);
	AddImpactOption(TEXT("끝 파단 후퇴량"), &FBreachImpactGrainOptions::EndCutback, .25f);
	AddImpactOption(TEXT("파단 끝 최소 뾰족함"), &FBreachImpactGrainOptions::MinEndSharpness, 1.f);
	Controls->AddSlot().AutoHeight().Padding(4)
	[
		SNew(SVerticalBox).Visibility_Lambda([this]() { const auto* P = Pattern.Get(); return P && P->Mode == EBreachPatternMode::GrainGuides ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]()
			{
				const UBreachPattern* P = Pattern.Get();
				return P && P->ImpactGrain.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				UBreachPattern* P = Pattern.Get();
				if (!P)
				{
					return;
				}
				const FScopedTransaction Change(FText::FromString(TEXT("피격 범위로 결 파단 설정")));
				P->Modify();
				P->ImpactGrain.bEnabled = State == ECheckBoxState::Checked;
				if (P->ImpactGrain.bEnabled && P->GrainEndSharpness > 0)
				{
					P->ImpactGrain.MinEndSharpness = FMath::Min(P->ImpactGrain.MinEndSharpness, P->GrainEndSharpness);
				}
				P->MarkPackageDirty();
				Status.Reset();
				Refresh();
			})
			[SNew(STextBlock).Text(FText::FromString(TEXT("피격 범위로 결 파단")))]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,6)[ImpactControls]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text_Lambda([this]()
			{
				const UBreachPattern* P = Pattern.Get();
				return FText::FromString(P && P->ImpactGrain.bEnabled ? TEXT("파단 끝 최대 뾰족함") : TEXT("파단 끝 뾰족함"));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SNumericEntryBox<float>).AllowSpin(false).MinValue(0.f).MaxValue(1.f)
			.Value_Lambda([this]() -> TOptional<float> { const auto* P = Pattern.Get(); return P ? TOptional<float>(P->GrainEndSharpness) : TOptional<float>(); })
			.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { SetEndSharpness(Value); })]
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("파단 끝 최대 가시 수")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SNumericEntryBox<int32>).AllowSpin(false).MinValue(0).MaxValue(8)
			.ToolTipText(FText::FromString(TEXT("각 끝에서 0~최대 개수를 선택합니다. 방향과 길이는 랜덤이며, 0이면 가시만 끕니다.")))
			.Value_Lambda([this]() -> TOptional<int32> { const auto* P = Pattern.Get(); return P ? TOptional<int32>(P->GrainMaxEndSplinters) : TOptional<int32>(); })
			.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { SetMaxEndSplinters(Value); })]
	];
	Controls->AddSlot().AutoHeight().Padding(4,8)[SNew(SButton).Text(FText::FromString(TEXT("리셋"))).OnClicked_Lambda([this]()
	{
		if (Canvas.IsValid())
		{
			const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(TEXT("작성한 경계를 모두 지우고 리셋할까요?\nSurfaceMaterial과 배경 텍스처는 유지됩니다.")), FText::FromString(TEXT("패턴 리셋")));
			Canvas->ResetPattern(Answer == EAppReturnType::Yes);
		}
		return FReply::Handled();
	})];
	Controls->AddSlot().AutoHeight().Padding(4,12)[SNew(STextBlock).Text(FText::FromString(TEXT("파손 미리보기")))];
	Controls->AddSlot().AutoHeight().Padding(4)[SNew(SBox).HeightOverride(220)[SAssignNew(Preview, SBreachPatternCanvas).Pattern(Data).PreviewOnly(true)]];
	Controls->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("클릭해서 시험 · 설정은 SurfaceMaterial 사용")))];
	Controls->AddSlot().AutoHeight().Padding(4)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]()
	{
		FString S = Status;
		if (Canvas.IsValid() && !Canvas->GetError().IsEmpty())
		{
			S += TEXT("\n") + Canvas->GetError();
		}
		if (Preview.IsValid() && !Preview->GetError().IsEmpty())
		{
			S += TEXT("\n") + Preview->GetError();
		}
		return FText::FromString(S);
	})];
	return SNew(SDockTab)[SNew(SSplitter)
		+ SSplitter::Slot().Value(.68f)[SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock).Text(FText::FromString(TEXT("배경 텍스처")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(SObjectPropertyEntryBox).AllowedClass(UTexture2D::StaticClass()).ObjectPath_Lambda([this]() { const auto* P = Pattern.Get(); return P && P->ReferenceTexture ? P->ReferenceTexture->GetPathName() : FString(); }).OnObjectChanged_Lambda([this](const FAssetData& A)
			{
				UBreachPattern* P = Pattern.Get();
				if (!ensure(P))
				{
					return;
				}
				const FScopedTransaction C(FText::FromString(TEXT("배경 텍스처 선택")));
				P->Modify();
				P->ReferenceTexture = Cast<UTexture2D>(A.GetAsset());
				P->MarkPackageDirty();
				Refresh();
			})]
			+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(Canvas, SBreachPatternCanvas).Pattern(Data).PreviewOnly(false).OnChanged(FSimpleDelegate::CreateSP(this, &FBreachPatternEditor::Refresh))]]
		+ SSplitter::Slot().Value(.32f)[SNew(SScrollBox) + SScrollBox::Slot()[Controls]]];
}
FReply FBreachPatternEditor::SetAuthoring(bool bTexture)
{
	UBreachPattern* P = Pattern.Get();
	if (ensure(P))
	{
		const FScopedTransaction C(FText::FromString(TEXT("작성 타입 변경")));
		P->Modify();
		P->AuthoringType = bTexture ? EBreachPatternAuthoringType::Texture : EBreachPatternAuthoringType::PointsAndLines;
		P->MarkPackageDirty();
		if (Canvas.IsValid())
		{
			const int32 ToolIndex = Tools.IndexOfByPredicate([this](const TSharedPtr<FString>& Item) { return Item.IsValid() && *Item == ActiveTool; });
			Canvas->SetTool(FMath::Max(0, ToolIndex));
		}
		Refresh();
	}
	return FReply::Handled();
}
bool FBreachPatternEditor::ApplyTexture()
{
	UBreachPattern* P = Pattern.Get();
	if (!ensure(P))
	{
		return false;
	}
	if (P->Mode == EBreachPatternMode::GrainGuides)
	{
		Status = TEXT("색상 ID 맵은 닫힌 영역용입니다. 열린 결선은 점·선 편집에서 작성하세요.");
		return false;
	}
	FBreachPatternImportOptions Options;
	Options.Mode = EBreachPatternImportMode::IdMap;
	Options.MinFeaturePixels = 1;
	FBreachPatternImportResult Result;
	if (!BreachPatternImport::Preview(P->RuleTexture, Options, Result))
	{
		Status = Result.Error;
		return false;
	}
	const FScopedTransaction C(FText::FromString(TEXT("색상 ID 맵 적용")));
	P->Modify();
	P->Mode = Result.Mode;
	P->Regions = MoveTemp(Result.Regions);
	P->Guides.Reset();
	P->MarkPackageDirty();
	Status = FString::Printf(TEXT("%d개 색상 영역 적용"), P->Regions.Num());
	Refresh();
	return true;
}
void FBreachPatternEditor::Refresh()
{
	if (Canvas.IsValid())
	{
		Canvas->RebuildPreview();
	}
	if (Preview.IsValid())
	{
		Preview->RebuildPreview();
	}
}
void FBreachPatternEditor::PostUndo(bool bSuccess) { Refresh(); }
void FBreachPatternEditor::PostRedo(bool bSuccess) { Refresh(); }
void FBreachPatternEditor::SaveAsset_Execute()
{
	UBreachPattern* P = Pattern.Get();
	if (!ensure(P))
	{
		return;
	}
	if (P->AuthoringType == EBreachPatternAuthoringType::Texture && P->RuleTexture)
	{
		if (!ApplyTexture())
		{
			return;
		}
	}
	if (!BreachPatternGeometry::Validate(*P, Status))
	{
		return;
	}
	FAssetEditorToolkit::SaveAsset_Execute();
}

void FBreachPatternEditor::SetEndSharpness(float Value)
{
	UBreachPattern* P = Pattern.Get();
	if (!ensure(P))
	{
		return;
	}
	if (P->Mode != EBreachPatternMode::GrainGuides || !FMath::IsFinite(Value) || Value < 0 || Value > 1 || P->GrainEndSharpness == Value)
	{
		return;
	}
	const FScopedTransaction Change(FText::FromString(TEXT("파단 끝 뾰족함 변경")));
	P->Modify();
	P->GrainEndSharpness = Value;
	if (P->ImpactGrain.bEnabled && Value > 0)
	{
		P->ImpactGrain.MinEndSharpness = FMath::Min(P->ImpactGrain.MinEndSharpness, Value);
	}
	P->MarkPackageDirty();
	Status.Reset();
	Refresh();
}

void FBreachPatternEditor::SetMaxEndSplinters(int32 Value)
{
	UBreachPattern* P = Pattern.Get();
	if (!ensure(P))
	{
		return;
	}
	if (P->Mode != EBreachPatternMode::GrainGuides || Value < 0 || Value > 8 || P->GrainMaxEndSplinters == Value)
	{
		return;
	}
	const FScopedTransaction Change(FText::FromString(TEXT("파단 끝 최대 가시 수 변경")));
	P->Modify();
	P->GrainMaxEndSplinters = Value;
	P->MarkPackageDirty();
	Status.Reset();
	Refresh();
}
