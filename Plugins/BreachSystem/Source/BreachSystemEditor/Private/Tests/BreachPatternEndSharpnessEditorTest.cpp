#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 결 에디터의 설정 소유권, 트랜잭션 및 직렬화를 검증한다.
#include "BreachPatternEditor.h"
#include "BreachPatternEditing.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Surface/BreachPattern.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEndSharpnessEditorTest, "BreachSystem.Pattern.EndSharpness.Editor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEndSharpnessEditorTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>(GetTransientPackage(),NAME_None,RF_Transactional);
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->Guides.AddDefaulted_GetRef().Points = {{0,0},{1,0}};
	const auto Editor = MakeShared<FBreachPatternEditor>();
	Editor->Pattern = Pattern;
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->NotifyAssetOpened(Pattern,&Editor.Get());
	Editor->SetEndSharpness(.7f);
	if (!TestEqual(TEXT("Grain editor updates the pattern, not SurfaceMaterial"),Pattern->GrainEndSharpness,.7f))
	{
		return false;
	}
	GEditor->UndoTransaction();
	TestEqual(TEXT("End sharpness is undoable"),Pattern->GrainEndSharpness,0.f);
	GEditor->RedoTransaction();
	TestEqual(TEXT("End sharpness is redoable"),Pattern->GrainEndSharpness,.7f);
	Editor->SetMaxEndSplinters(5);
	if (!TestEqual(TEXT("Maximum splinters belongs to the pattern asset"),Pattern->GrainMaxEndSplinters,5))
	{
		return false;
	}
	GEditor->UndoTransaction();
	TestEqual(TEXT("Maximum splinters supports Undo"),Pattern->GrainMaxEndSplinters,3);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Maximum splinters supports Redo"),Pattern->GrainMaxEndSplinters,5);
	TArray<uint8> Bytes;
	FObjectWriter Writer(Pattern,Bytes);
	UBreachPattern* Reloaded = NewObject<UBreachPattern>();
	FObjectReader Reader(Reloaded,Bytes);
	TestEqual(TEXT("Pattern sharpness survives serialization"),Reloaded->GrainEndSharpness,.7f);
	TestEqual(TEXT("Maximum splinters survives serialization"),Reloaded->GrainMaxEndSplinters,5);
	const FProperty* Property = FindFProperty<FProperty>(UBreachPattern::StaticClass(),TEXT("GrainEndSharpness"));
	TestTrue(TEXT("End sharpness is runtime data, not editor-only data"),Property && !Property->HasAnyPropertyFlags(CPF_EditorOnly));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEndSharpnessPreviewTest, "BreachSystem.Pattern.EndSharpness.ProjectPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEndSharpnessPreviewTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachSurfaceMaterial* Source = LoadObject<UBreachSurfaceMaterial>(nullptr,TEXT("/BreachSystem/SurfaceMaterial/SurfaceMaterial001_Wood.SurfaceMaterial001_Wood"));
	if (!TestNotNull(TEXT("Actual Wood SurfaceMaterial"),Source))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Actual Wood pattern"),Source->Pattern.Get()))
	{
		return false;
	}
	UBreachPattern* Pattern = DuplicateObject<UBreachPattern>(Source->Pattern,GetTransientPackage());
	const float Original = Source->Pattern->GrainEndSharpness;
	FString Export = FString::Printf(TEXT("{\"tile\":[%.6f,%.6f],\"influence\":%.6f,\"cases\":["),Source->TileSizeCm.X,Source->TileSizeCm.Y,Source->GrainInfluence);
	int32 Case = 0;
	for (FVector2D Center : {FVector2D(.3,.3),FVector2D(.5,.5),FVector2D(.7,.6)})
	{
		for (float Value : {0.f,.7f,1.f})
		{
			Pattern->GrainEndSharpness = Value;
			TArray<FGeneralPolygon2d> Out;
			FString Error;
			if (!TestTrue(TEXT("Actual editor preview with sharpness generates"),BreachPatternEditing::GeneratePreview(*Pattern,Source,Center,Out,Error)) || Out.Num() != 1)
			{
				AddError(Error);
				continue;
			}
			FString Points = TEXT("[");
			const FPolygon2d& Polygon = Out[0].GetOuter();
			for (int32 I = 0; I < Polygon.VertexCount(); ++I)
			{
				Points += FString::Printf(TEXT("%s[%.9f,%.9f]"),I ? TEXT(",") : TEXT(""),Polygon[I].X,Polygon[I].Y);
			}
			Export += FString::Printf(TEXT("%s{\"center\":[%.2f,%.2f],\"sharpness\":%.2f,\"points\":%s]}"),Case++ ? TEXT(",") : TEXT(""),Center.X,Center.Y,Value,*Points);
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PatternEndSharpness");
	IFileManager::Get().MakeDirectory(*Directory,true);
	TestTrue(TEXT("Native actual-asset preview export"),FFileHelper::SaveStringToFile(Export + TEXT("]}"),*(Directory / TEXT("project-preview.json"))));
	TestEqual(TEXT("Preview comparison leaves source asset unchanged"),Source->Pattern->GrainEndSharpness,Original);
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternSplinterPreviewTest, "BreachSystem.Pattern.Splinters.Preview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternSplinterPreviewTest::RunTest(const FString& Parameters)
{
	UBreachSurfaceMaterial* Source = LoadObject<UBreachSurfaceMaterial>(nullptr,TEXT("/BreachSystem/SurfaceMaterial/SurfaceMaterial001_Wood.SurfaceMaterial001_Wood"));
	if (!TestNotNull(TEXT("Existing Wood surface"),Source))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Existing Wood pattern"),Source->Pattern.Get()))
	{
		return false;
	}
	UBreachPattern* Pattern = DuplicateObject<UBreachPattern>(Source->Pattern,GetTransientPackage());
	const int32 Original = Source->Pattern->GrainMaxEndSplinters;
	Pattern->GrainEndSharpness = 1;
	FString Export = TEXT("[");
	for (int32 Maximum : {0,1,3,8})
	{
		Pattern->GrainMaxEndSplinters = Maximum;
		TArray<UE::Geometry::FGeneralPolygon2d> Result;
		FString Error;
		if (!TestTrue(TEXT("Asset-driven splinter preview generates"),BreachPatternEditing::GeneratePreview(*Pattern,Source,FVector2D(.5,.5),Result,Error)) || Result.Num() != 1)
		{
			AddError(Error);
			continue;
		}
		Export += FString::Printf(TEXT("%s{\"maximum\":%d,\"points\":["),Export.Len() > 1 ? TEXT(",") : TEXT(""),Maximum);
		const auto& Polygon = Result[0].GetOuter();
		for (int32 I = 0; I < Polygon.VertexCount(); ++I)
		{
			Export += FString::Printf(TEXT("%s[%.9f,%.9f]"),I ? TEXT(",") : TEXT(""),Polygon[I].X,Polygon[I].Y);
		}
		Export += TEXT("]}");
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PatternSplinters");
	IFileManager::Get().MakeDirectory(*Directory,true);
	TestTrue(TEXT("Export actual C++ preview"),FFileHelper::SaveStringToFile(Export + TEXT("]"),*(Directory / TEXT("preview.json"))));
	TestEqual(TEXT("Preview does not modify saved source pattern"),Source->Pattern->GrainMaxEndSplinters,Original);
	return !HasAnyErrors();
}

#endif
