#include "BreachPatternEditing.h"
#include "Geometry/BreachPatternGeometry.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "BreachPatternImport.h"
#include "Polygon2.h"
#include "BreachPatternCanvas.h"
#include "Data/BreachSurfaceMaterial.h"

// 테스트 전용: 반복 타일 초안, 공유 정점 및 실제 에디터 undo 복원을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternCanvasContractTest, "BreachSystem.Pattern.Editor.CanvasContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternCanvasContractTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Data = NewObject<UBreachPattern>();
	const auto Work = SNew(SBreachPatternCanvas).Pattern(Data);
	TestTrue(TEXT("Authoring canvas needs no independent preview settings"), Work->GetError().IsEmpty());
	const auto Preview = SNew(SBreachPatternCanvas).Pattern(Data).PreviewOnly(true);
	TestFalse(TEXT("Preview reports missing SurfaceMaterial"), Preview->GetError().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternSharedDeleteTest, "BreachSystem.Pattern.Editor.SharedDelete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternSharedDeleteTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::Regions;
	Pattern->Regions.AddDefaulted_GetRef().Outer = {{0,0},{.5,0},{.6,.5},{.5,1},{0,1}};
	Pattern->Regions.AddDefaulted_GetRef().Outer = {{.5,0},{1,0},{1,1},{.5,1},{.6,.5}};
	FString Error;
	TestTrue(TEXT("Shared vertex deletes"), BreachPatternEditing::DeleteVertex(*Pattern, {0, INDEX_NONE, 2}, Error));
	TestFalse(TEXT("Neighbor removes same bend to avoid a gap"), Pattern->Regions[1].Outer.Contains(FVector2D(.6,.5)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternAuthoringTest, "BreachSystem.Pattern.Editor.Authoring", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternAuthoringTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>(GetTransientPackage(), NAME_None, RF_Transactional);
	if (!TestNotNull(TEXT("Pattern exists"), Pattern))
	{
		return false;
	}
	FString Error;
	const TArray<FVector2D> Square = { {0.1, 0.1}, {0.8, 0.1}, {0.8, 0.8}, {0.1, 0.8} };
	if (!TestTrue(TEXT("Closed drawing creates a region"), BreachPatternEditing::CreatePath(*Pattern, Square, true, Error)))
	{
		return false;
	}
	TestEqual(TEXT("Closed drawing chooses Regions"), Pattern->Mode, EBreachPatternMode::Regions);
	TestEqual(TEXT("Closing edge is implicit and has no duplicate endpoint"), Pattern->Regions[0].Outer.Num(), 4);
	const TArray<FVector2D> BowTie = { {0.1, 0.1}, {0.8, 0.8}, {0.8, 0.1}, {0.1, 0.8} };
	TestFalse(TEXT("Self intersecting drawing rejects"), BreachPatternEditing::CreatePath(*Pattern, BowTie, true, Error));
	TestEqual(TEXT("Rejected drawing preserves existing region"), Pattern->Regions.Num(), 1);
	FBreachPatternPathSelection Selection;
	Selection.Path = 0;
	Selection.Vertex = 1;
	TestTrue(TEXT("Selected region vertex deletes"), BreachPatternEditing::DeleteVertex(*Pattern, Selection, Error));
	TestEqual(TEXT("Region keeps three corners"), Pattern->Regions[0].Outer.Num(), 3);
	TestFalse(TEXT("Deleting below three corners rejects"), BreachPatternEditing::DeleteVertex(*Pattern, Selection, Error));
	TestEqual(TEXT("Rejected deletion restores triangle"), Pattern->Regions[0].Outer.Num(), 3);
	TestTrue(TEXT("Selected region deletes"), BreachPatternEditing::DeletePath(*Pattern, Selection, Error));
	TestEqual(TEXT("Deleting final path leaves valid None mode"), Pattern->Mode, EBreachPatternMode::None);
	const TArray<FVector2D> Guide = { {0.2, 0.2}, {0.5, 0.5}, {0.8, 0.6} };
	TestTrue(TEXT("Open drawing creates guide"), BreachPatternEditing::CreatePath(*Pattern, Guide, false, Error));
	TestEqual(TEXT("Open drawing chooses GrainGuides"), Pattern->Mode, EBreachPatternMode::GrainGuides);
	if (!TestEqual(TEXT("One guide created"), Pattern->Guides.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Open guide endpoints remain distinct"), Pattern->Guides[0].Points.Last(), FVector2D(0.8, 0.6));
	TestTrue(TEXT("Selected path translates"), BreachPatternEditing::MovePath(*Pattern, Selection, FVector2D(0.05, 0), Error));
	TestEqual(TEXT("Whole path start translated"), Pattern->Guides[0].Points[0], FVector2D(0.25, 0.2));
	TestTrue(TEXT("Selected guide vertex deletes"), BreachPatternEditing::DeleteVertex(*Pattern, Selection, Error));
	TestFalse(TEXT("Deleting guide below two points rejects"), BreachPatternEditing::DeleteVertex(*Pattern, Selection, Error));
	TestFalse(TEXT("Closed region cannot silently replace open guides"), BreachPatternEditing::CreatePath(*Pattern, Square, true, Error));
	TestTrue(TEXT("Selected guide deletes"), BreachPatternEditing::DeletePath(*Pattern, Selection, Error));
	const TArray<FVector2D> TooShort = { {0.2, 0.2} };
	TestFalse(TEXT("Single point guide rejects"), BreachPatternEditing::CreatePath(*Pattern, TooShort, false, Error));
	TestEqual(TEXT("Rejected empty drawing preserves None mode"), Pattern->Mode, EBreachPatternMode::None);
	if (!TestNotNull(TEXT("Editor transaction system exists"), GEditor))
	{
		return false;
	}
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Create region automation")));
		Pattern->Modify();
		TestTrue(TEXT("Transactional region creates"), BreachPatternEditing::CreatePath(*Pattern, Square, true, Error));
	}
	GEditor->UndoTransaction();
	TestEqual(TEXT("Undo removes created region"), Pattern->Regions.Num(), 0);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo restores created region"), Pattern->Regions.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternMaterialPreviewTest, "BreachSystem.Pattern.Editor.MaterialPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternMaterialPreviewTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>();
	if (!TestNotNull(TEXT("Pattern exists"), Pattern) || !TestNotNull(TEXT("Material exists"), Material))
	{
		return false;
	}
	TArray<UE::Geometry::FGeneralPolygon2d> Preview;
	FString Error;
	TestFalse(TEXT("Preview requires selected surface material"), BreachPatternEditing::GeneratePreview(*Pattern, nullptr, FVector2D(0.5, 0.5), Preview, Error));
	Material->AccumulationShape.Contour.bEnabled = false;
	Material->AccumulationShape.Padding = 7;
	if (!TestTrue(TEXT("Preview uses selected material"), BreachPatternEditing::GeneratePreview(*Pattern, Material, FVector2D(0.5, 0.5), Preview, Error)))
	{
		return false;
	}
	if (!TestEqual(TEXT("Unpatterned preview produces one cutter"), Preview.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("Material padding determines 14cm width on canonical 100cm tile"), FMath::IsNearlyEqual(Preview[0].GetOuter().Bounds().Width(), 0.14, 1.e-6));
	Material->AccumulationShape.Padding = 12;
	TestTrue(TEXT("Changed material updates preview"), BreachPatternEditing::GeneratePreview(*Pattern, Material, FVector2D(0.5, 0.5), Preview, Error));
	TestTrue(TEXT("Preview has no independent cutter size"), FMath::IsNearlyEqual(Preview[0].GetOuter().Bounds().Width(), 0.24, 1.e-6));
	Material->AccumulationShape.Padding = -1;
	TestFalse(TEXT("Invalid material rejects instead of substituting preview defaults"), BreachPatternEditing::GeneratePreview(*Pattern, Material, FVector2D(0.5, 0.5), Preview, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEdgeTranslationTest, "BreachSystem.Pattern.Editor.EdgeTranslation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEdgeTranslationTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	if (!TestNotNull(TEXT("Transient pattern exists"), Pattern))
	{
		return false;
	}
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	const FVector2D A(0.125, 0.25);
	const FVector2D B(0.375, 0.25);
	const FVector2D Delta = B - A;
	Pattern->Guides.AddDefaulted_GetRef().Points = { A, B };
	Pattern->Guides.AddDefaulted_GetRef().Points = { A + FVector2D(1, 0), B + FVector2D(1, 0) };
	BreachPatternEditing::MoveSharedEdge(*Pattern, A, B, Delta);
	TestEqual(TEXT("Start moves exactly once when landing on old end"), Pattern->Guides[0].Points[0], A + Delta);
	TestEqual(TEXT("End receives the same translation"), Pattern->Guides[0].Points[1], B + Delta);
	TestEqual(TEXT("Periodic start moves exactly once"), Pattern->Guides[1].Points[0], A + FVector2D(1, 0) + Delta);
	TestEqual(TEXT("Periodic end receives the same translation"), Pattern->Guides[1].Points[1], B + FVector2D(1, 0) + Delta);
	FString Error;
	TestTrue(TEXT("Translation preserves valid open edges"), BreachPatternGeometry::Validate(*Pattern, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternImportedTJunctionTest, "BreachSystem.Pattern.Editor.ImportedTJunction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternImportedTJunctionTest::RunTest(const FString& Parameters)
{
	TArray<FColor> Pixels;
	for (int32 Y = 0; Y < 4; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			Pixels.Add(X < 2 ? FColor::Red : (Y < 2 ? FColor::Green : FColor::Blue));
		}
	}
	FBreachPatternImportOptions Options;
	Options.MinFeaturePixels = 1;
	FBreachPatternImportResult Result;
	if (!TestTrue(TEXT("T junction ID map imports"), BreachPatternImport::FromPixels(4, 4, Pixels, Options, Result)))
	{
		return false;
	}
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	if (!TestNotNull(TEXT("Transient pattern exists"), Pattern))
	{
		return false;
	}
	Pattern->Mode = Result.Mode;
	Pattern->Regions = Result.Regions;
	BreachPatternEditing::MoveSharedVertex(*Pattern, FVector2D(0.5, 0.5), FVector2D(0.55, 0.5));
	int32 Coverage = 0;
	bool bLongEdgeSplit = false;
	for (const FBreachPatternRegion& Region : Pattern->Regions)
	{
		UE::Geometry::FPolygon2d Polygon;
		for (const FVector2D& Point : Region.Outer)
		{
			Polygon.AppendVertex(Point);
		}
		if (Polygon.Contains(FVector2d(0.525, 0.49)))
		{
			++Coverage;
		}
		if (Polygon.Contains(FVector2d(0.1, 0.5)))
		{
			bLongEdgeSplit = Region.Outer.Contains(FVector2D(0.55, 0.5));
		}
	}
	TestTrue(TEXT("Long red boundary receives and moves its shared T junction"), bLongEdgeSplit);
	TestEqual(TEXT("Shared edit leaves exactly one covering region near junction"), Coverage, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEditingTest, "BreachSystem.Pattern.Editor.SharedGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEditingTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>(GetTransientPackage(), NAME_None, RF_Transactional);
	if (!TestNotNull(TEXT("Transient pattern exists"), Pattern))
	{
		return false;
	}
	BreachPatternEditing::MakeRunningBond(*Pattern, 4, 5);
	TestEqual(TEXT("Odd rows become even for vertical tile continuity"), Pattern->Regions.Num(), 24);
	TestTrue(TEXT("Cross-seam brick kept as one region"), Pattern->Regions[7].Outer[2].X > 1.0);
	FString Error;
	TestTrue(TEXT("Running bond validates"), BreachPatternGeometry::Validate(*Pattern, Error));
	BreachPatternEditing::MoveSharedVertex(*Pattern, FVector2D(0, 0), FVector2D(0.01, 0.02));
	TestEqual(TEXT("Origin point moves"), Pattern->Regions[0].Outer[0], FVector2D(0.01, 0.02));
	TestEqual(TEXT("Periodic right seam follows same edit"), Pattern->Regions[3].Outer[2], FVector2D(1.01, 0.02));
	TestEqual(TEXT("Periodic lower seam follows same edit"), Pattern->Regions[23].Outer[4], FVector2D(1.01, 1.02));
	if (!TestNotNull(TEXT("Editor transaction system exists"), GEditor))
	{
		return false;
	}
	const FVector2D Before = Pattern->Regions[0].Outer[0];
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Pattern edit automation")));
		Pattern->Modify();
		BreachPatternEditing::MoveSharedVertex(*Pattern, Before, Before + FVector2D(0.01, 0));
	}
	GEditor->UndoTransaction();
	TestEqual(TEXT("Undo restores pattern geometry"), Pattern->Regions[0].Outer[0], Before);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo reapplies pattern geometry"), Pattern->Regions[0].Outer[0], Before + FVector2D(0.01, 0));
	return true;
}
#endif
