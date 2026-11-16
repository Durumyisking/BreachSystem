#include "BreachPatternEditGraph.h"
#include "BreachPatternCanvas.h"
#include "Misc/AutomationTest.h"
#include "InputCoreTypes.h"
#include "Editor.h"
#include "BreachPatternImport.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 공유 접점의 실제 이웃 관계와 실제 Slate 조작 계약을 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternTopologyTest, "BreachSystem.Pattern.Editor.Topology", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternTopologyTest::RunTest(const FString& Parameters)
{
	UBreachPattern* P = NewObject<UBreachPattern>();
	P->Mode = EBreachPatternMode::GrainGuides;
	P->Guides.AddDefaulted_GetRef().Points = {{.1,.1},{.3,.1},{.3,.4}};
	FBreachPatternEditGraph G;
	FString Error;
	if (!TestTrue(TEXT("Graph builds"), G.Build(*P, Error)))
	{
		return false;
	}
	TestEqual(TEXT("CAB keeps three unique nodes"), G.Vertices.Num(), 3);
	TestEqual(TEXT("CAB keeps only CA and AB"), G.Edges.Num(), 2);
	const int32 C = G.FindVertex(FVector2D(.1,.1));
	const int32 A = G.FindVertex(FVector2D(.3,.1));
	const int32 B = G.FindVertex(FVector2D(.3,.4));
	TestTrue(TEXT("CA edge exists"), G.Edges.Contains(FIntPoint(FMath::Min(C,A),FMath::Max(C,A))));
	TestTrue(TEXT("AB edge exists"), G.Edges.Contains(FIntPoint(FMath::Min(A,B),FMath::Max(A,B))));
	TestFalse(TEXT("CB is not invented"), G.Edges.Contains(FIntPoint(FMath::Min(C,B),FMath::Max(C,B))));
	P->Guides.Reset();
	P->Mode = EBreachPatternMode::Regions;
	P->Regions.AddDefaulted_GetRef().Outer = {{0,0},{.5,0},{.5,1},{0,1}};
	P->Regions.AddDefaulted_GetRef().Outer = {{.5,0},{1,0},{1,.5},{.5,.5}};
	P->Regions.AddDefaulted_GetRef().Outer = {{.5,.5},{1,.5},{1,1},{.5,1}};
	if (!TestTrue(TEXT("Shared graph builds"), G.Build(*P, Error)))
	{
		return false;
	}
	const int32 Junction = G.FindVertex(FVector2D(.5,.5));
	int32 Owners = 0;
	for (const auto& Path : G.Paths)
	{
		Owners += Path.Nodes.Contains(Junction) ? 1 : 0;
	}
	TestEqual(TEXT("Three regions refer to the same junction node ID"), Owners, 3);
	int32 Degree = 0;
	for (const FIntPoint& Edge : G.Edges)
	{
		Degree += Edge.X == Junction || Edge.Y == Junction ? 1 : 0;
	}
	TestEqual(TEXT("T junction has three actual neighboring edges"), Degree, 3);
	const auto OriginalEdges = G.Edges;
	const int32 Selection[] = {Junction};
	TArray<FVector2D> Positions;
	TestTrue(TEXT("First drag frame applies"), G.ApplyMove(MakeArrayView(Selection), FVector2D(.12,.08), *P, Positions, Error));
	TArray<int32> Counts;
	for (const auto& Region : P->Regions)
	{
		Counts.Add(Region.Outer.Num());
	}
	if (!TestTrue(TEXT("Second frame uses immutable graph"), G.ApplyMove(MakeArrayView(Selection), FVector2D(.2,.1), *P, Positions, Error)))
	{
		return false;
	}
	for (int32 I = 0; I < Counts.Num(); ++I)
	{
		TestEqual(TEXT("Dragging never grows path point counts"), P->Regions[I].Outer.Num(), Counts[I]);
		TestTrue(TEXT("Every owner has the exact same moved junction"), P->Regions[I].Outer.Contains(FVector2D(.7,.6)));
	}
	TestTrue(TEXT("Dragging never changes edges"), G.Edges == OriginalEdges);
	TestTrue(TEXT("Second drag delta is absolute, not accumulated"), Positions[Junction].Equals(FVector2D(.7,.6),1.e-10));
	FBreachPatternEditGraph Reloaded;
	TestTrue(TEXT("Moved geometry rebuilds"), Reloaded.Build(*P, Error));
	TestTrue(TEXT("Save and reload preserve connectivity"), G.HasSameConnections(Reloaded));
	G.ApplyMove(MakeArrayView(Selection), FVector2D(.5,0), *P, Positions, Error);
	const bool bWeldBuilds = Reloaded.Build(*P, Error);
	TestFalse(TEXT("Welding a node to another node is rejected"), bWeldBuilds && G.HasSameConnections(Reloaded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternCanvasInputTest, "BreachSystem.Pattern.Editor.CanvasInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternCanvasInputTest::RunTest(const FString& Parameters)
{
	UBreachPattern* P = NewObject<UBreachPattern>(GetTransientPackage(), NAME_None, RF_Transactional);
	const auto Canvas = SNew(SBreachPatternCanvas).Pattern(P);
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(1000,1000), FSlateLayoutTransform());
	auto Mouse = [](FVector2D Pos, FKey Button, bool bShift = false)
	{
		const FModifierKeysState Mods(bShift,false,false,false,false,false,false,false,false);
		return FPointerEvent(0,Pos,Pos,TSet<FKey>{Button},Button,0,Mods);
	};
	Canvas->OnMouseButtonDown(Geometry, Mouse({100,100},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonDown(Geometry, Mouse({200,200},EKeys::RightMouseButton));
	TestEqual(TEXT("Right click cancels pending points, never adds a point"), Canvas->DraftPoints.Num(), 0);
	Canvas->SetTool(0);
	Canvas->OnMouseButtonDown(Geometry, Mouse({100,100},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonDown(Geometry, Mouse({400,120},EKeys::LeftMouseButton,true));
	TestTrue(TEXT("Shift chooses horizontal from screen angle"), Canvas->DraftPoints.Last().Equals(FVector2D(.4,.1)));
	Canvas->OnMouseButtonDown(Geometry, Mouse({420,500},EKeys::LeftMouseButton,true));
	TestTrue(TEXT("Shift chooses vertical from previous point"), Canvas->DraftPoints.Last().Equals(FVector2D(.4,.5)));
	Canvas->OnKeyDown(Geometry,FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
	TestEqual(TEXT("Escape cancels without closing"), Canvas->DraftPoints.Num(), 0);
	P->Mode = EBreachPatternMode::Regions;
	P->Regions.AddDefaulted_GetRef().Outer = {{.1,.1},{.5,.1},{.5,.5},{.1,.5}};
	Canvas->RebuildPreview();
	Canvas->SetTool(2);
	const auto Original = P->Regions[0].Outer;
	Canvas->OnMouseButtonDown(Geometry,Mouse({500,500},EKeys::LeftMouseButton));
	Canvas->OnMouseMove(Geometry,Mouse({600,600},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonUp(Geometry,Mouse({600,600},EKeys::LeftMouseButton));
	TestTrue(TEXT("First press only selects; drag cannot move yet"), P->Regions[0].Outer == Original);
	TestTrue(TEXT("Selected point has a highlight node"), Canvas->SelectedNode != INDEX_NONE);
	Canvas->OnMouseButtonDown(Geometry,Mouse({500,500},EKeys::LeftMouseButton));
	Canvas->OnMouseMove(Geometry,Mouse({550,550},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonUp(Geometry,Mouse({550,550},EKeys::LeftMouseButton));
	TestTrue(TEXT("Second press on selected node allows movement"), P->Regions[0].Outer.Contains(FVector2D(.55,.55)));
	const auto BeforeCancel = P->Regions[0].Outer;
	Canvas->OnMouseButtonDown(Geometry,Mouse({550,550},EKeys::LeftMouseButton));
	Canvas->OnMouseMove(Geometry,Mouse({580,580},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonDown(Geometry,Mouse({580,580},EKeys::RightMouseButton));
	TestTrue(TEXT("Right click cancels active movement"), P->Regions[0].Outer == BeforeCancel);
	Canvas->OnMouseButtonDown(Geometry,Mouse({300,100},EKeys::LeftMouseButton));
	Canvas->OnMouseMove(Geometry,Mouse({300,150},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonUp(Geometry,Mouse({300,150},EKeys::LeftMouseButton));
	TestTrue(TEXT("First line press only selects"), P->Regions[0].Outer == BeforeCancel);
	TestTrue(TEXT("Selected line has a highlight edge"), Canvas->SelectedEdge != INDEX_NONE);
	Canvas->OnMouseButtonDown(Geometry,Mouse({300,100},EKeys::LeftMouseButton));
	Canvas->OnMouseMove(Geometry,Mouse({300,150},EKeys::LeftMouseButton));
	Canvas->OnMouseButtonUp(Geometry,Mouse({300,150},EKeys::LeftMouseButton));
	TestTrue(TEXT("Second line press moves both connected endpoints"), P->Regions[0].Outer.Contains(FVector2D(.1,.15)) && P->Regions[0].Outer.Contains(FVector2D(.5,.15)));
	Canvas->ResetPattern(false);
	TestEqual(TEXT("Cancelled reset preserves geometry"), P->Regions.Num(), 1);
	Canvas->ResetPattern(true);
	if (!TestTrue(TEXT("Confirmed reset empties geometry"), P->Regions.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("Reset restores None mode"), P->Mode, EBreachPatternMode::None);
	GEditor->UndoTransaction();
	TestEqual(TEXT("Reset can be undone"), P->Regions.Num(), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachBrickMortarTest, "BreachSystem.Pattern.Editor.BrickMortar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachBrickMortarTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = LoadObject<UBreachPattern>(nullptr, TEXT("/BreachSystem/PatternExamples/BPAT_Brick.BPAT_Brick"));
	if (!TestNotNull(TEXT("Existing brick pattern"), Pattern))
	{
		return false;
	}
	UTexture2D* Texture = Pattern->RuleTexture;
	if (!TestNotNull(TEXT("Brick rule source"), Texture))
	{
		return false;
	}
	TestEqual(TEXT("Half-source-pixel boundaries are exact in 2x ID map"), Texture->Source.GetSizeX(), int64(2048));
	FBreachPatternImportOptions Options;
	Options.MinFeaturePixels = 1;
	FBreachPatternImportResult Imported;
	if (!TestTrue(TEXT("Actual project ID map imports"), BreachPatternImport::Preview(Texture, Options, Imported)))
	{
		AddError(Imported.Error);
		return false;
	}
	TestEqual(TEXT("Map contains 36 independent brick IDs"), Imported.Regions.Num(), 36);
	TestEqual(TEXT("Drawn example contains 36 bricks"), Pattern->Regions.Num(), 36);
	FBreachPatternEditGraph Graph;
	FString Error;
	if (!TestTrue(TEXT("Authored brick connectivity builds"), Graph.Build(*Pattern, Error)))
	{
		return false;
	}
	for (FIntPoint Edge : Graph.Edges)
	{
		const FVector2D D = Graph.Vertices[Edge.X] - Graph.Vertices[Edge.Y];
		TestTrue(TEXT("Mortar edges have no invented diagonals"), FMath::Abs(D.X) < 1.e-8 || FMath::Abs(D.Y) < 1.e-8);
	}
	auto Bounds = [](const FBreachPatternRegion& R)
	{
		FBox2D Box(ForceInit);
		for (FVector2D P : R.Outer)
		{
			Box += P;
		}
		return Box;
	};
	double TotalArea = 0;
	for (const FBreachPatternRegion& R : Pattern->Regions)
	{
		const FBox2D Box = Bounds(R);
		int32 Matches = 0;
		for (const FBreachPatternRegion& Candidate : Imported.Regions)
		{
			const FBox2D Other = Bounds(Candidate);
			Matches += Box.Min.Equals(Other.Min, 1.e-8) && Box.Max.Equals(Other.Max, 1.e-8) ? 1 : 0;
		}
		TestEqual(TEXT("Each drawn brick matches exactly one texture ID extent"), Matches, 1);
		TotalArea += Box.GetArea();
	}
	TestTrue(TEXT("Bricks cover the full tile, no thin exterior gaps"), FMath::IsNearlyEqual(TotalArea, 1., 1.e-8));
	return true;
}
#endif
