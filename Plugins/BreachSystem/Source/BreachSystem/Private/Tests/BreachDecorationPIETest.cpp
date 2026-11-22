#include "Misc/AutomationTest.h"

// 테스트 전용: 별도 에디터의 저장하지 않는 PIE 월드에서 장식 표시와 캡처를 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "Data/BreachSurfaceMaterial.h"
#include "Editor.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
	// 테스트 전용: 저장하지 않는 나무결/섬유 마스크로 실제 OpacityMask 렌더링을 비교한다.
	UTexture2D* MakeWoodTexture(bool bMask = false)
	{
		constexpr int32 Size = 256;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);
		FRandomStream Random(9051);
		struct FFiber { double X; double Drift; double End; double Width; double Phase; };
		TArray<FFiber> Fibers;
		for (int32 I = 0; I < 13; ++I)
		{
			Fibers.Add({(I + Random.FRand()) / 13.0, Random.FRandRange(-0.1f, 0.1f), Random.FRandRange(0.35f, 1.f), Random.FRandRange(0.014f, 0.049f), Random.FRandRange(0.f, 2.f * PI)});
		}
		for (int32 Y = 0; Y < Size; ++Y)
		{
			const double V = static_cast<double>(Y) / (Size - 1);
			for (int32 X = 0; X < Size; ++X)
			{
				const double U = static_cast<double>(X) / (Size - 1);
				bool bSolid = V < 0.15;
				for (const FFiber& Fiber : Fibers)
				{
					const double Center = Fiber.X + Fiber.Drift * V + FMath::Sin(V * 24 + Fiber.Phase) * 0.01;
					if (V < Fiber.End && FMath::Abs(U - Center) < Fiber.Width * (1 - 0.68 * V / Fiber.End))
					{
						bSolid = true;
					}
				}
				const double Grain = 0.65 + 0.2 * FMath::Sin(U * 210 + FMath::Sin(V * 18) * 2) + Random.FRandRange(-0.08f, 0.08f);
				Pixels[Y * Size + X] = FColor(180 * Grain, 116 * Grain, 57 * Grain, bSolid ? 255 : 0);
				if (bMask)
				{
					Pixels[Y * Size + X] = bSolid ? FColor::White : FColor::Black;
				}
			}
		}
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, MakeArrayView(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor)));
		check(Texture);
		Texture->UpdateResource();
		return Texture;
	}

	UMaterial* MakePreviewMaterial(UObject* Owner, UTexture2D* Texture, bool bMasked, const FLinearColor& Color)
	{
		check(Owner);
		UMaterial* Material = NewObject<UMaterial>(Owner);
		Material->SetShadingModel(MSM_Unlit);
		Material->BlendMode = bMasked ? BLEND_Masked : BLEND_Opaque;
		if (Texture)
		{
			UMaterialExpressionTextureSample* Sample = NewObject<UMaterialExpressionTextureSample>(Material);
			Sample->Texture = Texture;
			Sample->SamplerType = SAMPLERTYPE_Color;
			Material->GetExpressionCollection().AddExpression(Sample);
			Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Sample);
			if (bMasked)
			{
				UMaterialExpressionTextureSample* Mask = NewObject<UMaterialExpressionTextureSample>(Material);
				Mask->Texture = MakeWoodTexture(true);
				Mask->SamplerType = SAMPLERTYPE_Color;
				Material->GetExpressionCollection().AddExpression(Mask);
				Material->GetEditorOnlyData()->OpacityMask.Connect(1, Mask);
			}
		}
		else
		{
			UMaterialExpressionConstant3Vector* Constant = NewObject<UMaterialExpressionConstant3Vector>(Material);
			Constant->Constant = Color;
			Material->GetExpressionCollection().AddExpression(Constant);
			Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Constant);
		}
		Material->PostEditChange();
		return Material;
	}

	class FExerciseDecorationPIE final : public IAutomationLatentCommand
	{
		FAutomationTestBase* Test;
		int32 Frames = 0;
		TWeakObjectPtr<AActor> Wall;
		TWeakObjectPtr<ASceneCapture2D> Capture;
		TWeakObjectPtr<UDynamicMeshComponent> Feature;
		TStrongObjectPtr<UMaterial> MaskedMaterial;
		int32 Captures = 0;
		int32 OpaqueColoredPixels = 0;
		double CaptureReadyTime = 0;
		TArray<FColor> OpaquePixels;

	public:
		explicit FExerciseDecorationPIE(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			check(Test);
			if (!GEditor || !GEditor->PlayWorld)
			{
				Test->AddError(TEXT("Deco test requires an active disposable PIE world."));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (++Frames < 10)
			{
				return false;
			}
			if (!Wall.IsValid())
			{
				AActor* Actor = World->SpawnActor<AActor>();
				check(Actor);
				Wall = Actor;
				UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Actor);
				Actor->SetRootComponent(Surface);
				UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				Surface->SetStaticMesh(Cube);
				Surface->SetWorldScale3D(FVector(2, 0.1, 2));
				UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>(Actor);
				Material->CutDecoration.Material = UMaterial::GetDefaultMaterial(MD_Surface);
				Material->FeatureDecoration.Material = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Actor);
				if (FParse::Param(FCommandLine::Get(), TEXT("BreachDecoCapture")))
				{
					UTexture2D* Texture = MakeWoodTexture();
					Material->FeatureDecoration.Material = MakePreviewMaterial(Actor, Texture, false, FLinearColor::White);
					MaskedMaterial.Reset(MakePreviewMaterial(Actor, Texture, true, FLinearColor::White));
					Test->TestEqual(TEXT("Fixture material is actually masked"), MaskedMaterial->GetBlendMode(), BLEND_Masked);
					Test->AddInfo(FString::Printf(TEXT("Mask expression=%s clip=%g"), *GetNameSafe(MaskedMaterial->GetEditorOnlyData()->OpacityMask.Expression), MaskedMaterial->OpacityMaskClipValue));
					UMaterial* WallMaterial = MakePreviewMaterial(Actor, nullptr, false, FLinearColor(0.28, 0.3, 0.32));
					Surface->SetMaterial(0, WallMaterial);
					Material->CutDecoration.Material = nullptr;
					if (GShaderCompilingManager)
					{
						GShaderCompilingManager->FinishAllCompilation();
					}
				}
				Surface->SetSurfaceMaterial(Material);
				Surface->RegisterComponent();
				UE::Geometry::FPolygon2d Cutter;
				Cutter.AppendVertex(FVector2d(-15, -15));
				Cutter.AppendVertex(FVector2d(15, -15));
				Cutter.AppendVertex(FVector2d(15, 15));
				Cutter.AppendVertex(FVector2d(-15, 15));
				Test->TestTrue(TEXT("Actual PIE surface cuts"), Surface->ApplyCutterPolygon(Cutter));
				Test->TestEqual(TEXT("Fixture runs in PIE"), World->WorldType, EWorldType::PIE);
				TInlineComponentArray<UDynamicMeshComponent*> Meshes(Actor);
				int32 Traces = 0;
				for (UDynamicMeshComponent* Mesh : Meshes)
				{
					if (!Mesh->IsA<UBreachSurfaceProxy>() && Mesh->GetMaterial(0) == Material->CutDecoration.Material)
					{
						++Traces;
						Test->TestTrue(TEXT("PIE trace is registered"), Mesh->IsRegistered());
						Test->TestTrue(TEXT("PIE trace is visible"), Mesh->IsVisible() && !Mesh->bHiddenInGame);
					}
				}
				Test->TestEqual(TEXT("PIE has only configured impacted face trace"), Traces, Material->CutDecoration.Material ? 1 : 0);
				int32 Features = 0;
				for (UDynamicMeshComponent* Mesh : Meshes)
				{
					if (Mesh->GetMaterial(0) == Material->FeatureDecoration.Material)
					{
						++Features;
						Feature = Mesh;
						Test->TestTrue(TEXT("PIE procedural features are closed meshes"), Mesh->GetMesh()->IsClosed());
					}
				}
				Test->TestEqual(TEXT("PIE has one generated feature display"), Features, 1);
				if (FParse::Param(FCommandLine::Get(), TEXT("BreachDecoCapture")))
				{
					ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>();
					check(Camera);
					Capture = Camera;
					Camera->SetActorLocation(FVector(140, -300, 90));
					Camera->SetActorRotation((-Camera->GetActorLocation()).Rotation());
					USceneCaptureComponent2D* View = Camera->GetCaptureComponent2D();
					check(View);
					View->bCaptureEveryFrame = false;
					View->bCaptureOnMovement = false;
					View->ProjectionType = ECameraProjectionMode::Orthographic;
					View->OrthoWidth = 270;
					View->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDRNoAlpha;
					View->bAlwaysPersistRenderingState = true;
					View->PostProcessSettings.bOverride_AutoExposureMethod = true;
					View->PostProcessSettings.AutoExposureMethod = AEM_Manual;
					View->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
					View->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
					View->PostProcessSettings.bOverride_AutoExposureBias = true;
					View->PostProcessSettings.AutoExposureBias = 0.f;
					View->TextureTarget = NewObject<UTextureRenderTarget2D>(Camera);
					View->TextureTarget->InitAutoFormat(1024, 768);
					CaptureReadyTime = FPlatformTime::Seconds() + 3;
				}
				return false;
			}
			if (Frames < 40 || (Capture.IsValid() && FPlatformTime::Seconds() < CaptureReadyTime))
			{
				if (Capture.IsValid())
				{
					USceneCaptureComponent2D* View = Capture->GetCaptureComponent2D();
					check(View);
					View->CaptureScene();
				}
				return false;
			}
			if (Capture.IsValid() && Frames == 40)
			{
				USceneCaptureComponent2D* View = Capture->GetCaptureComponent2D();
				check(View);
				View->CaptureScene();
				return false;
			}
			if (Capture.IsValid())
			{
				USceneCaptureComponent2D* View = Capture->GetCaptureComponent2D();
				check(View);
				TArray<FColor> Pixels;
				Test->AddInfo(FString::Printf(TEXT("Capture %d masked shader compiling=%d"), Captures, MaskedMaterial.IsValid() ? MaskedMaterial->IsCompiling() : false));
				View->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
				Test->TestTrue(TEXT("Capture contains visible wall/feature pixels"), Pixels.ContainsByPredicate([](const FColor& P) { return P.R > 10 || P.G > 10 || P.B > 10; }));
				Test->TestTrue(TEXT("Capture contains colored wood fibers"), Pixels.ContainsByPredicate([](const FColor& P) { return P.R > 40 && P.R > P.G * 1.2 && P.G > P.B * 1.2; }));
				int32 ColoredPixels = 0;
				for (const FColor& P : Pixels)
				{
					if (P.R > 40 && P.R > P.G * 1.2 && P.G > P.B * 1.2)
					{
						++ColoredPixels;
					}
				}
				Test->AddInfo(FString::Printf(TEXT("Capture %d wood pixels=%d"), Captures, ColoredPixels));
				if (Captures == 0)
				{
					OpaquePixels = Pixels;
					OpaqueColoredPixels = ColoredPixels;
				}
				if (Captures == 1)
				{
					Test->TestTrue(TEXT("Masked material changes visible coverage on identical geometry"), Pixels != OpaquePixels);
					Test->TestTrue(TEXT("Mask removes a substantial part of each plate"), ColoredPixels < OpaqueColoredPixels * 0.85);
				}
				FBufferArchive PNG;
				Test->TestTrue(TEXT("PIE capture exported"), FImageUtils::ExportRenderTarget2DAsPNG(View->TextureTarget, PNG));
				const TCHAR* Names[] = {TEXT("opaque-front.png"), TEXT("masked-front.png"), TEXT("masked-back.png")};
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/ProceduralDeco") / Names[Captures];
				Test->TestTrue(TEXT("PIE capture saved outside Content"), FFileHelper::SaveArrayToFile(PNG, *Path));
				++Captures;
				if (Captures == 1)
				{
					check(Feature.IsValid());
					Feature->SetMaterial(0, MaskedMaterial.Get());
					if (GShaderCompilingManager)
					{
						GShaderCompilingManager->FinishAllCompilation();
					}
					CaptureReadyTime = FPlatformTime::Seconds() + 3;
					Frames = 30;
					return false;
				}
				if (Captures == 2)
				{
					Capture->SetActorLocation(FVector(-140, 300, 90));
					Capture->SetActorRotation((-Capture->GetActorLocation()).Rotation());
					CaptureReadyTime = FPlatformTime::Seconds() + 3;
					Frames = 30;
					return false;
				}
				Capture->Destroy();
			}
			Wall->Destroy();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationPIETest, "BreachSystem.Decoration.PIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationPIETest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor exists"), GEditor))
	{
		return false;
	}
	if (!FApp::IsUnattended() || !FParse::Param(FCommandLine::Get(), TEXT("BreachDisposablePIE")) || GEditor->PlayWorld)
	{
		AddWarning(TEXT("Deco PIE requires a separate unattended editor with -BreachDisposablePIE; the current level was left untouched."));
		return true;
	}
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("Unsaved temporary map"), Map))
	{
		return false;
	}
	AWorldSettings* Settings = Map->GetWorldSettings();
	check(Settings);
	Settings->DefaultGameMode = AGameModeBase::StaticClass();
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::SimulateInEditor;
	Params.bAllowOnlineSubsystem = false;
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIEForAutomationCommand(Params));
	ADD_LATENT_AUTOMATION_COMMAND(FExerciseDecorationPIE(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
