#include "BreachPatternCanvas.h"
#include "BreachPatternEditing.h"
#include "Surface/BreachPatternGeometry.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Framework/Application/SlateApplication.h"



void SBreachPatternCanvas::Construct(const FArguments& Args)
{
	Pattern = Args._Pattern;
	if (Args._Pattern && Args._Pattern->Mode == EBreachPatternMode::GrainGuides)
	{
		Tool = 1;
	}
	bPreviewOnly = Args._PreviewOnly;
	OnChanged = Args._OnChanged;
	SetCanTick(false);
	SetClipping(EWidgetClipping::ClipToBounds);
	RebuildPreview();
}

FVector2D SBreachPatternCanvas::ToUV(const FGeometry& Geometry, FVector2D Position) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Local = Geometry.AbsoluteToLocal(Position);
	return FVector2D(FMath::Clamp(Local.X / FMath::Max(Size.X, 1.0), 0.0, 1.0), FMath::Clamp(Local.Y / FMath::Max(Size.Y, 1.0), 0.0, 1.0));
}

void SBreachPatternCanvas::RebuildPreview()
{
	Preview.Reset();
	Error.Reset();
	UBreachPattern* Data = Pattern.Get();
	if (!ensureMsgf(Data, TEXT("Pattern preview requires an asset.")))
	{
		Error = TEXT("Missing pattern or editor settings.");
		return;
	}
	UTexture2D* Source = !bPreviewOnly && Data->AuthoringType == EBreachPatternAuthoringType::Texture ? Data->RuleTexture.Get() : Data->ReferenceTexture.Get();
	if (DisplaySource.Get() != Source || !DisplayTexture.IsValid())
	{
		DisplaySource = Source;
		DisplayTexture.Reset();
		if (Source)
		{
			FImage Image;
			if (Source->Source.GetMipImage(Image, 0))
			{
				// Slate에는 씬 기반 스트리밍 요청이 없으므로 소스 해상도의 임시 표시본을 소유한다.
				DisplayTexture.Reset(FImageUtils::CreateTexture2DFromImage(Image));
				if (DisplayTexture.IsValid())
				{
					DisplayTexture->SRGB = Source->SRGB;
					DisplayTexture->Filter = Source->Filter;
					DisplayTexture->UpdateResource();
				}
			}
		}
	}
	if (!bPreviewOnly && !bPendingDrag)
	{
		RebuildGraph();
	}
	if (!BreachPatternGeometry::Validate(*Data, Error))
	{
		return;
	}
	if (bPreviewOnly)
	{
		BreachPatternEditing::GeneratePreview(*Data, Data->PreviewSurfaceMaterial.LoadSynchronous(), PreviewUV, Preview, Error);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SBreachPatternCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	const UBreachPattern* Data = Pattern.Get();
	if (!Data)
	{
		return Layer;
	}
	const FVector2D Size = Geometry.GetLocalSize();
	UTexture2D* Texture = DisplayTexture.IsValid() ? DisplayTexture.Get() : DisplaySource.Get();
	if (Texture)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(), &Brush);
	}
	auto Draw = [&](const TArray<FVector2D>& Vertices, bool bClosed, FLinearColor Color, FVector2D Offset, bool bDrawHandles = true)
	{
		if (Vertices.IsEmpty())
		{
			return;
		}
		TArray<FVector2D> Points;
		for (const FVector2D& Point : Vertices)
		{
			Points.Add((Point + Offset) * Size);
		}
		if (bClosed)
		{
			const FVector2D FirstPoint = Points[0];
			Points.Add(FirstPoint);
		}
		if (Points.Num() > 1)
		{
			FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 2);
		}
		if (!bDrawHandles)
		{
			return;
		}
		for (const FVector2D& Point : Points)
		{
			TArray<FVector2D> Handle = { Point - FVector2D(3, 0), Point + FVector2D(3, 0), Point, Point - FVector2D(0, 3), Point + FVector2D(0, 3) };
			FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(), Handle, ESlateDrawEffect::None, Color, true, 1);
		}
	};
	if (!bPreviewOnly)
	{
		const FLinearColor Color = Data->Mode == EBreachPatternMode::GrainGuides ? FLinearColor::Green : FLinearColor(0, 1, 1);
		const FSlateBrush* PointBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				const FVector2D Offset(X, Y);
				for (int32 I = 0; I < Graph.Edges.Num(); ++I)
				{
					const FIntPoint E = Graph.Edges[I];
					Draw({Graph.Vertices[E.X], Graph.Vertices[E.Y]}, false, I == SelectedEdge ? FLinearColor::Yellow : Color, Offset, false);
				}
				for (int32 I = 0; I < Graph.Vertices.Num(); ++I)
				{
					const FVector2D P = (Graph.Vertices[I] + Offset) * Size;
					const FLinearColor PointColor = I == SelectedNode ? FLinearColor::Yellow : FLinearColor(1.f, .2f, .6f);
					FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(FVector2D(8, 8), FSlateLayoutTransform(P - FVector2D(4, 4))), PointBrush, ESlateDrawEffect::None, FLinearColor::Black);
					FSlateDrawElement::MakeBox(Elements, Layer + 3, Geometry.ToPaintGeometry(FVector2D(6, 6), FSlateLayoutTransform(P - FVector2D(3, 3))), PointBrush, ESlateDrawEffect::None, PointColor);
				}
				if (Graph.Vertices.IsValidIndex(SelectedNode))
				{
					const FVector2D P = (Graph.Vertices[SelectedNode] + Offset) * Size;
					TArray<FVector2D> Mark = {P + FVector2D(-5,-5), P + FVector2D(5,-5), P + FVector2D(5,5), P + FVector2D(-5,5), P + FVector2D(-5,-5)};
					FSlateDrawElement::MakeLines(Elements, Layer + 4, Geometry.ToPaintGeometry(), Mark, ESlateDrawEffect::None, FLinearColor::Yellow, true, 3);
				}
			}
		}
		Draw(DraftPoints, false, FLinearColor::Yellow, FVector2D::ZeroVector);
	}
	for (const UE::Geometry::FGeneralPolygon2d& Cutter : Preview)
	{
		TArray<FVector2D> Points;
		for (const FVector2d& Point : Cutter.GetOuter().GetVertices())
		{
			Points.Add(Point);
		}
		Draw(Points, true, FLinearColor::Red, FVector2D::ZeroVector);
		for (const UE::Geometry::FPolygon2d& Hole : Cutter.GetHoles())
		{
			Points.Reset();
			for (const FVector2d& Point : Hole.GetVertices())
			{
				Points.Add(Point);
			}
			Draw(Points, true, FLinearColor::Red, FVector2D::ZeroVector);
		}
	}
	return Layer + 4;
}

void SBreachPatternCanvas::SetTool(int32 Value)
{
	CancelGesture();
	Tool = Value;
	Selection = {};
	SelectedNode = SelectedEdge = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SBreachPatternCanvas::RebuildGraph()
{
	UBreachPattern* Data = Pattern.Get();
	if (!ensure(Data))
	{
		return;
	}
	const bool bNode = Graph.Vertices.IsValidIndex(SelectedNode);
	const FVector2D OldNode = bNode ? Graph.Vertices[SelectedNode] : FVector2D::ZeroVector;
	const bool bEdge = Graph.Edges.IsValidIndex(SelectedEdge);
	const FIntPoint OldEdge = bEdge ? Graph.Edges[SelectedEdge] : FIntPoint::ZeroValue;
	const FVector2D A = bEdge ? Graph.Vertices[OldEdge.X] : FVector2D::ZeroVector;
	const FVector2D B = bEdge ? Graph.Vertices[OldEdge.Y] : FVector2D::ZeroVector;
	Graph.Build(*Data, Error);
	SelectedNode = bNode ? Graph.FindVertex(OldNode) : INDEX_NONE;
	SelectedEdge = INDEX_NONE;
	if (bEdge)
	{
		const int32 NA = Graph.FindVertex(A);
		const int32 NB = Graph.FindVertex(B);
		SelectedEdge = Graph.Edges.Find(FIntPoint(FMath::Min(NA, NB), FMath::Max(NA, NB)));
	}
	Selection = Graph.OwnerOf(SelectedNode, SelectedEdge);
}

FVector2D SBreachPatternCanvas::DrawingPoint(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	FVector2D UV = ToUV(Geometry, Event.GetScreenSpacePosition());
	if (Event.IsShiftDown() && !DraftPoints.IsEmpty())
	{
		const FVector2D Last = DraftPoints.Last();
		const FVector2D Delta = (UV - Last) * Geometry.GetLocalSize();
		if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
		{
			UV.Y = Last.Y;
		}
		else
		{
			UV.X = Last.X;
		}
	}
	return UV;
}

void SBreachPatternCanvas::HitTest(FVector2D UV, FVector2D Size, int32& Node, int32& Edge) const
{
	Node = Edge = INDEX_NONE;
	double BestNode = 8 * 8;
	double BestEdge = 6 * 6;
	for (int32 Y = -1; Y <= 1; ++Y)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			const FVector2D Offset(X, Y);
			for (int32 I = 0; I < Graph.Vertices.Num(); ++I)
			{
				const double Distance = ((Graph.Vertices[I] + Offset - UV) * Size).SizeSquared();
				if (Distance < BestNode)
				{
					BestNode = Distance;
					Node = I;
				}
			}
			for (int32 I = 0; I < Graph.Edges.Num(); ++I)
			{
				const FIntPoint E = Graph.Edges[I];
				const FVector2D A = (Graph.Vertices[E.X] + Offset) * Size;
				const FVector2D D = (Graph.Vertices[E.Y] - Graph.Vertices[E.X]) * Size;
				const FVector2D P = UV * Size;
				const double Alpha = FMath::Clamp(FVector2D::DotProduct(P - A, D) / FMath::Max(D.SizeSquared(), UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER), 0.0, 1.0);
				const double Distance = FVector2D::DistSquared(P, A + D * Alpha);
				if (Distance < BestEdge)
				{
					BestEdge = Distance;
					Edge = I;
				}
			}
		}
	}
	if (Node != INDEX_NONE)
	{
		Edge = INDEX_NONE;
	}
}

void SBreachPatternCanvas::CancelGesture()
{
	UBreachPattern* Data = Pattern.Get();
	if (DragTransaction)
	{
		if (ensure(Data))
		{
			Data->Regions = BeforeRegions;
			Data->Guides = BeforeGuides;
		}
		DragTransaction->Cancel();
		DragTransaction.Reset();
	}
	bPendingDrag = false;
	DraftPoints.Reset();
	if (Data)
	{
		RebuildGraph();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SBreachPatternCanvas::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UBreachPattern* Data = Pattern.Get();
	if (!ensure(Data))
	{
		return FReply::Unhandled();
	}
	if (Event.GetEffectingButton() == EKeys::RightMouseButton && !bPreviewOnly)
	{
		CancelGesture();
		return FReply::Handled().ReleaseMouseCapture().SetUserFocus(SharedThis(this));
	}
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	PreviewUV = ToUV(Geometry, Event.GetScreenSpacePosition());
	if (bPreviewOnly)
	{
		RebuildPreview();
		return FReply::Handled().SetUserFocus(SharedThis(this));
	}
	if (Data->AuthoringType == EBreachPatternAuthoringType::Texture)
	{
		return FReply::Handled();
	}
	if (Tool != 2)
	{
		const FVector2D Point = DrawingPoint(Geometry, Event);
		if (Tool == 0 && DraftPoints.Num() >= 3 && ((DraftPoints[0] - Point) * Geometry.GetLocalSize()).Size() < 12)
		{
			FinishPath();
		}
		else
		{
			DraftPoints.Add(Point);
		}
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().SetUserFocus(SharedThis(this));
	}
	int32 Node, Edge;
	HitTest(PreviewUV, Geometry.GetLocalSize(), Node, Edge);
	const bool bAlreadySelected = (Node != INDEX_NONE && Node == SelectedNode) || (Edge != INDEX_NONE && Edge == SelectedEdge);
	SelectedNode = Node;
	SelectedEdge = Edge;
	Selection = Graph.OwnerOf(Node, Edge);
	if (bAlreadySelected)
	{
		DragGraph = Graph;
		DragAnchor = PreviewUV;
		DragScreenStart = Event.GetScreenSpacePosition();
		BeforeRegions = Data->Regions;
		BeforeGuides = Data->Guides;
		bPendingDrag = true;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().SetUserFocus(SharedThis(this));
}

FReply SBreachPatternCanvas::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	UBreachPattern* Data = Pattern.Get();
	if (!Data || !bPendingDrag)
	{
		return FReply::Unhandled();
	}
	if (!DragTransaction)
	{
		const double Threshold = FSlateApplication::Get().GetDragTriggerDistance();
		if (FVector2D::DistSquared(Event.GetScreenSpacePosition(), DragScreenStart) < Threshold * Threshold)
		{
			return FReply::Handled();
		}
		DragTransaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("파괴 경계 이동")));
		Data->Modify();
	}
	TArray<int32> Nodes;
	if (SelectedNode != INDEX_NONE)
	{
		Nodes.Add(SelectedNode);
	}
	else if (DragGraph.Edges.IsValidIndex(SelectedEdge))
	{
		Nodes = {DragGraph.Edges[SelectedEdge].X, DragGraph.Edges[SelectedEdge].Y};
	}
	TArray<FVector2D> Positions;
	if (DragGraph.ApplyMove(Nodes, ToUV(Geometry, Event.GetScreenSpacePosition()) - DragAnchor, *Data, Positions, Error))
	{
		Graph = DragGraph;
		Graph.Vertices = MoveTemp(Positions);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

FReply SBreachPatternCanvas::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bPendingDrag || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPendingDrag = false;
	UBreachPattern* Data = Pattern.Get();
	FString ValidationError;
	if (DragTransaction)
	{
		FBreachPatternEditGraph Candidate;
		bool bValid = false;
		if (ensure(Data))
		{
			bValid = Candidate.Build(*Data, ValidationError);
			if (bValid && !DragGraph.HasSameConnections(Candidate))
			{
				bValid = false;
				ValidationError = TEXT("다른 점이나 선에 합쳐지는 이동은 허용하지 않습니다.");
			}
			if (!bValid)
			{
				Data->Regions = BeforeRegions;
				Data->Guides = BeforeGuides;
			}
			else
			{
				Data->MarkPackageDirty();
			}
		}
		if (!bValid)
		{
			DragTransaction->Cancel();
		}
		DragTransaction.Reset();
	}
	RebuildPreview();
	OnChanged.ExecuteIfBound();
	if (!ValidationError.IsEmpty())
	{
		Error = TEXT("이동 취소: ") + ValidationError;
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SBreachPatternCanvas::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const UBreachPattern* Data = Pattern.Get();
	if (!Data || bPreviewOnly || Data->AuthoringType != EBreachPatternAuthoringType::PointsAndLines)
	{
		return FReply::Unhandled();
	}
	if (Event.GetKey() == EKeys::Enter && Tool != 2)
	{
		FinishPath();
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Escape)
	{
		CancelGesture();
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (Event.GetKey() == EKeys::Delete)
	{
		DeleteSelection();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geometry, Event);
}

void SBreachPatternCanvas::FinishPath()
{
	UBreachPattern* Data = Pattern.Get();
	if (!Data || bPreviewOnly || DraftPoints.IsEmpty())
	{
		return;
	}
	FScopedTransaction Change(FText::FromString(TEXT("파괴 경계 작성")));
	Data->Modify();
	if (BreachPatternEditing::CreatePath(*Data, DraftPoints, Tool == 0, Error))
	{
		DraftPoints.Reset();
		Data->MarkPackageDirty();
		RebuildPreview();
		OnChanged.ExecuteIfBound();
	}
	else
	{
		Change.Cancel();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SBreachPatternCanvas::DeleteSelection()
{
	UBreachPattern* Data = Pattern.Get();
	if (!Data || bPreviewOnly || Data->AuthoringType != EBreachPatternAuthoringType::PointsAndLines || Selection.Path == INDEX_NONE)
	{
		return;
	}
	FScopedTransaction Change(FText::FromString(TEXT("파괴 경계 삭제")));
	Data->Modify();
	const auto OldRegions = Data->Regions;
	const auto OldGuides = Data->Guides;
	TArray<FVector2D> Positions;
	Graph.ApplyMove({}, FVector2D::ZeroVector, *Data, Positions, Error);
	const bool bChanged = Selection.Vertex == INDEX_NONE ? BreachPatternEditing::DeletePath(*Data, Selection, Error) : BreachPatternEditing::DeleteVertex(*Data, Selection, Error);
	if (bChanged)
	{
		Selection = {};
		SelectedNode = SelectedEdge = INDEX_NONE;
		Data->MarkPackageDirty();
		RebuildPreview();
		OnChanged.ExecuteIfBound();
	}
	else
	{
		Data->Regions = OldRegions;
		Data->Guides = OldGuides;
		Change.Cancel();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SBreachPatternCanvas::ResetPattern(bool bConfirmed)
{
	UBreachPattern* Data = Pattern.Get();
	if (!Data || bPreviewOnly || !bConfirmed)
	{
		return;
	}
	CancelGesture();
	const FScopedTransaction Change(FText::FromString(TEXT("파괴 패턴 리셋")));
	Data->Modify();
	Data->Regions.Reset();
	Data->Guides.Reset();
	Data->Mode = EBreachPatternMode::None;
	Data->RuleTexture = nullptr;
	Selection = {};
	SelectedNode = SelectedEdge = INDEX_NONE;
	Data->MarkPackageDirty();
	RebuildPreview();
	OnChanged.ExecuteIfBound();
}
