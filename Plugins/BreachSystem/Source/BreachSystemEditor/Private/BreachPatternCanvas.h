#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Data/BreachPattern.h"
#include "Curve/GeneralPolygon2.h"
#include "ScopedTransaction.h"
#include "BreachPatternEditGraph.h"
#include "UObject/StrongObjectPtr.h"

class FScopedTransaction;

class SBreachPatternCanvas : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBreachPatternCanvas) : _Pattern(nullptr), _PreviewOnly(false) {}
		SLATE_ARGUMENT(UBreachPattern*, Pattern)
		SLATE_ARGUMENT(bool, PreviewOnly)
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void RebuildPreview();
	void SetTool(int32 Value);
	void DeleteSelection();
	void ResetPattern(bool bConfirmed);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	FString GetError() const { return Error; }
private:
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	// 테스트 전용: 실제 Slate 입력으로 선택·그리기 상태 전이를 검증한다.
	friend class FBreachPatternCanvasInputTest;
#endif
	FVector2D ToUV(const FGeometry& Geometry, FVector2D Position) const;
	FVector2D DrawingPoint(const FGeometry& Geometry, const FPointerEvent& Event) const;
	void CancelGesture();
	void RebuildGraph();
	void HitTest(FVector2D UV, FVector2D Size, int32& Node, int32& Edge) const;
	TWeakObjectPtr<UBreachPattern> Pattern;
	FSimpleDelegate OnChanged;
	TUniquePtr<FScopedTransaction> DragTransaction;
	FBreachPatternEditGraph Graph;
	FBreachPatternEditGraph DragGraph;
	int32 SelectedNode = INDEX_NONE;
	int32 SelectedEdge = INDEX_NONE;
	FVector2D DragAnchor;
	FVector2D DragScreenStart;
	bool bPendingDrag = false;
	TArray<FBreachPatternRegion> BeforeRegions;
	TArray<FBreachPatternGuide> BeforeGuides;
	FVector2D PreviewUV = FVector2D(0.5, 0.5);
	FString Error;
	TArray<UE::Geometry::FGeneralPolygon2d> Preview;
	bool bPreviewOnly = false;
	int32 Tool = 0;
	TArray<FVector2D> DraftPoints;
	FBreachPatternPathSelection Selection;
	void FinishPath();
	TWeakObjectPtr<UTexture2D> DisplaySource;
	TStrongObjectPtr<UTexture2D> DisplayTexture;
};
