#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 실제 나무 에셋을 에디터 프리뷰 경로로 잘라 판 옆면이 결선 위에 놓이는지 검증하고, 시안 비교용 좌표를 내보낸다.
#include "BreachPatternEditing.h"
#include "Surface/BreachPattern.h"
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

	// Unit tangent (cm space) of the tiled guide nearest to a UV point.
	const FVector2d Tile(Material->TileSizeCm);
	const auto GuideTangent = [&](const FVector2d& UV)
	{
		double Nearest = TNumericLimits<double>::Max();
		FVector2d Tangent = FVector2d::ZeroVector;
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				const FVector2d Point = (UV - FVector2d(FMath::FloorToDouble(UV.X) + X, FMath::FloorToDouble(UV.Y) + Y)) * Tile;
				for (const FBreachPatternGuide& Guide : Pattern->Guides)
				{
					for (int32 I = 0; I + 1 < Guide.Points.Num(); ++I)
					{
						const FVector2d A = FVector2d(Guide.Points[I]) * Tile;
						const FVector2d Edge = FVector2d(Guide.Points[I + 1]) * Tile - A;
						const double T = FMath::Clamp((Point - A).Dot(Edge) / Edge.SquaredLength(), 0., 1.);
						const double Distance = (Point - A - Edge * T).Length();
						if (Distance < Nearest)
						{
							Nearest = Distance;
							Tangent = Edge.GetSafeNormal();
						}
					}
				}
			}
		}
		return Tangent;
	};
	const auto Points = [](TConstArrayView<FVector2d> Path)
	{
		FString Text = TEXT("[");
		for (int32 I = 0; I < Path.Num(); ++I)
		{
			Text += FString::Printf(TEXT("%s[%.8f,%.8f]"), I ? TEXT(",") : TEXT(""), Path[I].X, Path[I].Y);
		}
		return Text + TEXT("]");
	};

	FString Export = FString::Printf(TEXT("{\"padding\":%.4f,\"sourceInfluence\":%.4f,\"tile\":[%.4f,%.4f],\"guides\":["), Source->AccumulationShape.Padding, Source->GrainInfluence, Tile.X, Tile.Y);
	for (int32 I = 0; I < Pattern->Guides.Num(); ++I)
	{
		TArray<FVector2d> Guide;
		for (const FVector2D& Point : Pattern->Guides[I].Points)
		{
			Guide.Add(Point);
		}
		Export += (I ? TEXT(",") : TEXT("")) + Points(Guide);
	}
	Export += TEXT("],\"cases\":[");
	int32 Case = 0;
	for (FVector2D Center : {FVector2D(.3,.3), FVector2D(.5,.5), FVector2D(.7,.6)})
	{
		for (int32 Seed : {1, 2, 3})
		{
			for (double Influence : {0., .5, 1.})
			{
				Material->GrainInfluence = Influence;
				Material->AccumulationShape.Contour.Seed = Seed;
				TArray<FGeneralPolygon2d> Preview;
				FString Error;
				if (!TestTrue(TEXT("Actual Wood editor preview generates"), BreachPatternEditing::GeneratePreview(*Pattern, Material, Center, Preview, Error)) || Preview.IsEmpty())
				{
					AddError(Error);
					continue;
				}
				// Plank sides and steps run along the grain; the plain contour does not.
				double Perimeter = 0;
				double Aligned = 0;
				FString Regions = TEXT("[");
				for (int32 R = 0; R < Preview.Num(); ++R)
				{
					const FPolygon2d& Outer = Preview[R].GetOuter();
					for (int32 I = 0; I < Outer.VertexCount(); ++I)
					{
						const FVector2d A = Outer[I];
						const FVector2d B = Outer[(I + 1) % Outer.VertexCount()];
						const FVector2d Edge = (B - A) * Tile;
						const double Length = Edge.Length();
						Perimeter += Length;
						if (FMath::Abs(Edge.Dot(GuideTangent((A + B) * .5))) >= Length * FMath::Cos(FMath::DegreesToRadians(10.)))
						{
							Aligned += Length;
						}
					}
					Regions += (R ? TEXT(",") : TEXT("")) + Points(Outer.GetVertices());
				}
				const double Fraction = Aligned / Perimeter;
				if (Influence == 1.)
				{
					TestTrue(TEXT("Most of the wood cut outline runs along the authored grain"), Fraction >= .6);
				}
				Export += FString::Printf(TEXT("%s{\"center\":[%.2f,%.2f],\"seed\":%d,\"influence\":%.2f,\"aligned\":%.3f,\"regions\":%s]}"), Case++ ? TEXT(",") : TEXT(""), Center.X, Center.Y, Seed, Influence, Fraction, *Regions);
			}
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/LineTear");
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Export actual project preview"), FFileHelper::SaveStringToFile(Export + TEXT("]}"), *(Directory / TEXT("project-preview.json"))));
	return !HasAnyErrors();
}
#endif
