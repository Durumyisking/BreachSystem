#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 실제 나무 에셋과 동일한 에디터 프리뷰 경로의 결 방향 형태를 검증하고 좌표를 내보낸다.
#include "BreachPatternEditing.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternWoodPreviewTest, "BreachSystem.Pattern.Editor.WoodPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternWoodPreviewTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachSurfaceMaterial* Source = LoadObject<UBreachSurfaceMaterial>(nullptr, TEXT("/BreachSystem/SurfaceMaterial/SurfaceMaterial001_Wood.SurfaceMaterial001_Wood"));
	if (!TestNotNull(TEXT("Existing Wood SurfaceMaterial"), Source))
	{
		return false;
	}
	UBreachPattern* Pattern = Source->Pattern;
	if (!TestNotNull(TEXT("Wood references a pattern"), Pattern))
	{
		return false;
	}
	if (!TestTrue(TEXT("Actual wood pattern uses open guides"), Pattern->Mode == EBreachPatternMode::GrainGuides && !Pattern->Guides.IsEmpty()))
	{
		return false;
	}
	UBreachSurfaceMaterial* Material = DuplicateObject<UBreachSurfaceMaterial>(Source, GetTransientPackage());
	FString Export = FString::Printf(TEXT("{\"padding\":%.4f,\"vertices\":%d,\"sourceInfluence\":%.4f,\"tile\":[%.4f,%.4f],\"guides\":["), Source->AccumulationShape.Padding, Source->AccumulationShape.Contour.VertexCount, Source->GrainInfluence, Source->TileSizeCm.X, Source->TileSizeCm.Y);
	auto Points = [](TConstArrayView<FVector2d> Path)
	{
		FString Text = TEXT("[");
		for (int32 I = 0; I < Path.Num(); ++I)
		{
			Text += FString::Printf(TEXT("%s[%.10f,%.10f]"), I ? TEXT(",") : TEXT(""), Path[I].X, Path[I].Y);
		}
		return Text + TEXT("]");
	};
	for (int32 I = 0; I < Pattern->Guides.Num(); ++I)
	{
		Export += (I ? TEXT(",") : TEXT("")) + Points(Pattern->Guides[I].Points);
	}
	Export += TEXT("],\"cases\":[");
	int32 Case = 0;
	for (FVector2D Center : {FVector2D(.3,.3), FVector2D(.5,.5), FVector2D(.7,.6)})
	{
		for (double Influence : {0., .6, 1.})
		{
			Material->GrainInfluence = Influence;
			TArray<FGeneralPolygon2d> Preview;
			FString Error;
			if (!TestTrue(TEXT("Actual Wood editor preview generates"), BreachPatternEditing::GeneratePreview(*Pattern, Material, Center, Preview, Error)) || Preview.Num() != 1)
			{
				AddError(Error);
				continue;
			}
			const FPolygon2d& Polygon = Preview[0].GetOuter();
			double Run = 0, Longest = 0;
			for (int32 I = 0; I < Polygon.VertexCount() * 2; ++I)
			{
				const FVector2d Edge = (Polygon[(I + 1) % Polygon.VertexCount()] - Polygon[I % Polygon.VertexCount()]) * FVector2d(Material->TileSizeCm);
				const double Length = Edge.Length();
				if (Length > 1.e-8 && FMath::Abs(Edge.X) >= Length * FMath::Cos(FMath::DegreesToRadians(10.)))
				{
					Run += Length;
					Longest = FMath::Max(Longest, Run);
				}
				else
				{
					Run = 0;
				}
			}
			AddInfo(FString::Printf(TEXT("Actual preview center %.2f,%.2f influence %.1f: longest run %.3f cm"), Center.X, Center.Y, Influence, Longest));
			if (Influence == 1.)
			{
				TestTrue(TEXT("Actual Wood preview contains a visible long fiber tear"), Longest >= Material->AccumulationShape.Padding * .6);
			}
			Export += FString::Printf(TEXT("%s{\"center\":[%.2f,%.2f],\"influence\":%.1f,\"run\":%.5f,\"points\":%s}"), Case++ ? TEXT(",") : TEXT(""), Center.X, Center.Y, Influence, Longest, *Points(Polygon.GetVertices()));
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PatternGrainRuns");
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Export actual project preview"), FFileHelper::SaveStringToFile(Export + TEXT("]}"), *(Directory / TEXT("project-preview.json"))));
	return !HasAnyErrors();
}
#endif
