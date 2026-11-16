#include "Misc/AutomationTest.h"

// 테스트 전용: 저장하지 않는 PIE에서 예제 UV0, 실제 누적 피해와 Complex 충돌, 렌더 캡처를 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Editor.h"
#include "Engine/DamageEvents.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "ShaderCompiler.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "Surface/BreachPattern.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Tests/AutomationEditorCommon.h"
#include "TextureResource.h"

namespace
{
	using namespace UE::Geometry;

	// 테스트 전용: 원본 삼각형의 barycentric 보간으로 생성 메시의 양면 UV0를 독립 검증한다.
	bool SourceUVAt(const FDynamicMesh3& Source, const FVector3d& Point, FVector2f& OutUV)
	{
		if (!Source.HasAttributes() || !Source.Attributes()->PrimaryUV())
		{
			return false;
		}
		const FDynamicMeshUVOverlay* UV = Source.Attributes()->PrimaryUV();
		for (int32 ID : Source.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Source.GetTriVertices(ID, A, B, C);
			if (FMath::Abs(A.Y - Point.Y) > .001 || FMath::Abs(B.Y - Point.Y) > .001 || FMath::Abs(C.Y - Point.Y) > .001 || !UV->IsSetTriangle(ID))
			{
				continue;
			}
			const double Denominator = (B.Z - C.Z) * (A.X - C.X) + (C.X - B.X) * (A.Z - C.Z);
			if (FMath::Abs(Denominator) < UE_SMALL_NUMBER)
			{
				continue;
			}
			const double U = ((B.Z - C.Z) * (Point.X - C.X) + (C.X - B.X) * (Point.Z - C.Z)) / Denominator;
			const double V = ((C.Z - A.Z) * (Point.X - C.X) + (A.X - C.X) * (Point.Z - C.Z)) / Denominator;
			if (U >= -.0001 && V >= -.0001 && U + V <= 1.0001)
			{
				const FIndex3i Elements = UV->GetTriangle(ID);
				OutUV = UV->GetElement(Elements.A) * U + UV->GetElement(Elements.B) * V + UV->GetElement(Elements.C) * (1 - U - V);
				return true;
			}
		}
		return false;
	}

	void CheckProxyUV(FAutomationTestBase& Test, const FDynamicMesh3& Source, const UBreachSurfaceProxy& Proxy)
	{
		const FDynamicMesh3* Mesh = Proxy.GetMesh();
		if (!Test.TestNotNull(TEXT("Cut mesh exists"), Mesh))
		{
			return;
		}
		if (!Test.TestTrue(TEXT("Cut mesh has UV0"), Mesh->HasAttributes() && Mesh->Attributes()->PrimaryUV()))
		{
			return;
		}
		const FDynamicMeshUVOverlay* UV = Mesh->Attributes()->PrimaryUV();
		int32 FrontSamples = 0;
		int32 BackSamples = 0;
		bool bUVMatches = true;
		for (int32 ID : Mesh->TriangleIndicesItr())
		{
			const FVector3d Normal = Mesh->GetTriNormal(ID);
			if (FMath::Abs(Normal.Y) < .99)
			{
				continue;
			}
			if (!UV->IsSetTriangle(ID))
			{
				bUVMatches = false;
				continue;
			}
			const FIndex3i Vertices = Mesh->GetTriangle(ID);
			const FIndex3i Elements = UV->GetTriangle(ID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				FVector2f Expected;
				if (!SourceUVAt(Source, Mesh->GetVertex(Vertices[Corner]), Expected))
				{
					bUVMatches = false;
					continue;
				}
				bUVMatches &= Expected.Equals(UV->GetElement(Elements[Corner]), .0001f);
				if (Normal.Y < 0)
				{
					++FrontSamples;
				}
				else
				{
					++BackSamples;
				}
			}
		}
		Test.TestTrue(TEXT("Front face checked against source UV0"), FrontSamples > 0);
		Test.TestTrue(TEXT("Back face checked against source UV0"), BackSamples > 0);
		Test.TestTrue(TEXT("Every surviving face corner matches original UV0"), bUVMatches);
	}

	bool BlocksAt(UBreachSurfaceProxy& Proxy, const FVector2d& Point)
	{
		const FTransform Transform = Proxy.GetComponentTransform();
		FHitResult Hit;
		return Proxy.LineTraceComponent(Hit, Transform.TransformPosition(FVector(Point.X, -100, Point.Y)), Transform.TransformPosition(FVector(Point.X, 100, Point.Y)), FCollisionQueryParams(SCENE_QUERY_STAT(PatternPIE), true));
	}

	double FrontArea(const FDynamicMesh3& Mesh)
	{
		double Area = 0;
		for (int32 ID : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriNormal(ID).Y < -.99)
			{
				Area += Mesh.GetTriArea(ID);
			}
		}
		return Area;
	}

	class FExercisePatternPIE final : public IAutomationLatentCommand
	{
		FAutomationTestBase* Test;
		TArray<TWeakObjectPtr<AActor>> Actors;
		TWeakObjectPtr<ASceneCapture2D> Capture;
		int32 Frames = 0;
		int32 CaptureIndex = 0;
		double ReadyTime = 0;

	public:
		explicit FExercisePatternPIE(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			check(Test);
			if (!GEditor || !GEditor->PlayWorld)
			{
				Test->AddError(TEXT("Pattern PIE requires a disposable active PIE world."));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (++Frames < 10)
			{
				return false;
			}
			if (Actors.IsEmpty())
			{
				Test->TestEqual(TEXT("Fixture executes in PIE"), World->WorldType, EWorldType::PIE);
				UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				if (!Test->TestNotNull(TEXT("Source cube"), Cube) || !Cube->GetRenderData() || Cube->GetRenderData()->LODResources.IsEmpty())
				{
					return true;
				}
				FDynamicMesh3 SourceMesh;
				const FStaticMeshLODResourcesToDynamicMesh::ConversionOptions Options;
				if (!Test->TestTrue(TEXT("Read original UV0 fixture"), FStaticMeshLODResourcesToDynamicMesh::Convert(&Cube->GetRenderData()->LODResources[0], Options, SourceMesh)))
				{
					return true;
				}
				AActor* Gun = World->SpawnActor<AActor>();
				check(Gun);
				Actors.Add(Gun);
				UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Gun);
				Gun->AddInstanceComponent(Source);
				Source->RegisterComponent();
				Source->BindCircle(.8f);
				Source->BreachDamage = 1;
				const TCHAR* Kinds[] = {TEXT("Basic"), TEXT("Brick"), TEXT("Wood")};
				const TCHAR* SurfaceNames[] = {TEXT("SurfaceMaterial002_Concrete"), TEXT("SurfaceMaterial003_Brick"), TEXT("SurfaceMaterial001_Wood")};
				// 테스트 전용: 작은 입력 회귀 후 같은 여섯 벽을 초기화해 세 재질에 동일한 긴 입력을 적용한다.
				for (int32 Scenario = 0; Scenario < 2; ++Scenario)
				{
					const bool bTall = Scenario == 1;
					for (int32 Variant = 0; Variant < 2; ++Variant)
					{
						double BasicArea = 0;
						for (int32 Kind = 0; Kind < 3; ++Kind)
						{
							UBreachSurfaceMaterial* Example = LoadObject<UBreachSurfaceMaterial>(nullptr, *(FString(TEXT("/BreachSystem/SurfaceMaterial/")) + SurfaceNames[Kind]));
							UMaterialInterface* Display = Example ? Example->DisplayMaterial.Get() : nullptr;
							if (!Test->TestNotNull(TEXT("Existing project SurfaceMaterial"), Example) || !Test->TestNotNull(TEXT("Configured Wall material instance"), Display))
							{
								return true;
							}
							if (!Test->TestNotNull(TEXT("Example has a configured pattern"), Example->Pattern.Get()))
							{
								return true;
							}
							const EBreachPatternMode Modes[] = {EBreachPatternMode::None, EBreachPatternMode::Regions, EBreachPatternMode::GrainGuides};
							Test->TestEqual(TEXT("Fixture uses the intended pattern mode"), Example->Pattern->Mode, Modes[Kind]);
							AActor* Wall = nullptr;
							UBreachableSurfaceComponent* Surface = nullptr;
							UBreachSurfaceMaterial* Material = nullptr;
							if (!bTall)
							{
								Wall = World->SpawnActor<AActor>();
								check(Wall);
								Actors.Add(Wall);
								Surface = NewObject<UBreachableSurfaceComponent>(Wall);
								Wall->SetRootComponent(Surface);
								Surface->SetStaticMesh(Cube);
								Surface->SetWorldTransform(FTransform(FRotator(0, Variant ? 17 : 0, 0), FVector((Kind - 1) * 310, 0, Variant ? -260 : 0), FVector(Variant ? -2.5 : 2, .1, Variant ? 1.75 : 2)));
								Surface->SetMaterial(0, Display);
								Material = DuplicateObject<UBreachSurfaceMaterial>(Example, Wall);
							}
							else
							{
								Wall = Actors[1 + Variant * 3 + Kind].Get();
								if (!Test->TestNotNull(TEXT("Tall scenario reuses its disposable wall"), Wall))
								{
									return true;
								}
								Surface = Wall->FindComponentByClass<UBreachableSurfaceComponent>();
								if (!Test->TestNotNull(TEXT("Tall scenario surface"), Surface))
								{
									return true;
								}
								struct FResetParameters
								{
									bool ReturnValue = false;
								} ResetParameters;
								Surface->ProcessEvent(Surface->FindFunctionChecked(TEXT("ResetSurface")), &ResetParameters);
								if (!Test->TestTrue(TEXT("Tall scenario resets the original small breach"), ResetParameters.ReturnValue))
								{
									return true;
								}
								Material = Surface->GetSurfaceMaterial();
							}
							if (!Test->TestNotNull(TEXT("Scenario owns transient material rules"), Material))
							{
								return true;
							}
							Material->Accumulation.bEnabled = true;
							// 테스트 전용: 실제 절단 경계가 보이도록 복제본의 장식만 끈다. 원본 에셋은 보존한다.
							Material->CutDecoration.Material = nullptr;
							Material->FeatureDecoration.Material = nullptr;
							Material->Accumulation.BreachHP = 3;
							Material->Accumulation.WindowSize = bTall ? FVector2D(40, 100) : FVector2D(40, 40);
							Material->AccumulationShape.Padding = bTall ? 22 : 13;
							Material->AccumulationShape.Contour.bEnabled = true;
							Material->AccumulationShape.Contour.Seed = 31;
							Material->AccumulationShape.Contour.VertexCount = 48;
							Material->AccumulationShape.Contour.bRandomizeVertexSpacing = false;
							Surface->SetSurfaceMaterial(Material);
							if (!bTall)
							{
								Surface->RegisterComponent();
							}
							UMaterialInstanceDynamic* RuntimeMaterial = Cast<UMaterialInstanceDynamic>(Surface->GetMaterial(0));
							if (Test->TestNotNull(TEXT("Runtime owns a material instance"), RuntimeMaterial))
							{
								Test->TestEqual(TEXT("Runtime instance derives from Wall instance"), RuntimeMaterial->Parent.Get(), Display);
								Test->TestTrue(TEXT("Runtime texture uses physical tiling"), RuntimeMaterial->K2_GetScalarParameterValue(TEXT("BreachUseWorldSizeUV")) == 1.f);
							}
							for (int32 Shot = 0; Shot < 3; ++Shot)
							{
								FPointDamageEvent Damage;
								Damage.Damage = 1;
								Damage.HitInfo.Component = Surface;
								const FVector LocalImpact = bTall ? FVector(6, Variant ? 50 : -50, -14 + Shot * 20) : FVector(6 + (Shot - 1) * 2, Variant ? 50 : -50, 6);
								Damage.HitInfo.ImpactPoint = Surface->GetComponentTransform().TransformPosition(LocalImpact);
								Damage.HitInfo.ImpactNormal = Surface->GetComponentTransform().TransformVectorNoScale(FVector(0, Variant ? 1 : -1, 0));
								Damage.ShotDirection = -Damage.HitInfo.ImpactNormal;
								Surface->HandleBreachDamage(Surface, Damage, Gun, 1);
								Test->TestEqual(TEXT("Two direct shots retain records, third accumulated cut consumes them"), Surface->GetAccumulatedHitCount(), Shot < 2 ? Shot + 1 : 0);
							}
							UBreachSurfaceProxy* Proxy = Wall->FindComponentByClass<UBreachSurfaceProxy>();
							if (!Test->TestNotNull(TEXT("HandleBreachDamage published a real surface proxy"), Proxy))
							{
								continue;
							}
							if (!Test->TestNotNull(TEXT("Published proxy owns a cut mesh"), Proxy->GetMesh()))
							{
								continue;
							}
							Test->TestTrue(TEXT("Proxy is registered and visible in PIE"), Proxy->IsRegistered() && Proxy->IsVisible() && !Proxy->bHiddenInGame);
							Test->TestTrue(TEXT("Cut wall is a closed volume"), Proxy->GetMesh()->IsClosed());
							Test->TestEqual(TEXT("Proxy and source share the exact material instance"), Proxy->GetMaterial(0), Surface->GetMaterial(0));
							Test->TestFalse(TEXT("Complex trace passes through accumulated opening beyond direct bullet holes"), BlocksAt(*Proxy, FVector2d(6, 9)));
							Test->TestTrue(TEXT("Complex trace still blocks surviving corner"), BlocksAt(*Proxy, FVector2d(43, 43)));
							if (bTall)
							{
								Test->TestFalse(TEXT("Tall opening reaches below the small-case footprint"), BlocksAt(*Proxy, FVector2d(6, -10)));
								Test->TestFalse(TEXT("Tall opening reaches above the small-case footprint"), BlocksAt(*Proxy, FVector2d(6, 22)));
							}
							CheckProxyUV(*Test, SourceMesh, *Proxy);
							const double Area = FrontArea(*Proxy->GetMesh());
							if (Kind == 0)
							{
								BasicArea = Area;
							}
							else if (Kind == 1)
							{
								Test->TestTrue(TEXT("Brick removes complete blocks beyond the basic contour"), Area < BasicArea - 50);
							}
							else
							{
								Test->TestTrue(TEXT("Grain guides change actual cut geometry from identical basic contour"), FMath::Abs(Area - BasicArea) > .001);
							}
							Test->AddInfo(FString::Printf(TEXT("Pattern PIE %s %s: side=%s rotated=%d mirrored=%d triangles=%d frontArea=%.6f"), bTall ? TEXT("Tall") : TEXT("Small"), Kinds[Kind], Variant ? TEXT("back") : TEXT("front"), Variant != 0, Variant != 0, Proxy->GetMesh()->TriangleCount(), Area));
						}
					}
				}
				if (!FParse::Param(FCommandLine::Get(), TEXT("BreachPatternCapture")))
				{
					return true;
				}
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->FinishAllCompilation();
				}
				ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>();
				check(Camera);
				Capture = Camera;
				Actors.Add(Camera);
				Camera->SetActorLocation(FVector(0, -900, -130));
				Camera->SetActorRotation(FRotator(0, 90, 0));
				USceneCaptureComponent2D* View = Camera->GetCaptureComponent2D();
				check(View);
				View->bCaptureEveryFrame = false;
				View->bCaptureOnMovement = false;
				View->ProjectionType = ECameraProjectionMode::Orthographic;
				View->OrthoWidth = 1050;
				View->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
				View->ShowFlags.SetLighting(false);
				View->PostProcessSettings.bOverride_AutoExposureMethod = true;
				View->PostProcessSettings.AutoExposureMethod = AEM_Manual;
				View->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
				View->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
				View->TextureTarget = NewObject<UTextureRenderTarget2D>(Camera);
				View->TextureTarget->InitAutoFormat(1536, 1024);
				ReadyTime = FPlatformTime::Seconds() + 3;
				return false;
			}
			if (!Capture.IsValid())
			{
				return true;
			}
			USceneCaptureComponent2D* View = Capture->GetCaptureComponent2D();
			check(View);
			View->CaptureScene();
			if (FPlatformTime::Seconds() < ReadyTime)
			{
				return false;
			}
			TArray<FColor> Pixels;
			Test->TestTrue(TEXT("Genuine renderer returns capture pixels"), View->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
			Test->TestTrue(TEXT("Captured examples contain textured non-gray wall pixels"), Pixels.ContainsByPredicate([](const FColor& P) { return P.R > 40 && P.R > P.G * 1.15 && P.G > P.B * 1.1; }));
			FBufferArchive PNG;
			Test->TestTrue(TEXT("Export real PIE render target"), FImageUtils::ExportRenderTarget2DAsPNG(View->TextureTarget, PNG));
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/Pattern");
			IFileManager::Get().MakeDirectory(*Directory, true);
			const FString Path = Directory / (CaptureIndex == 0 ? TEXT("front.png") : TEXT("back.png"));
			Test->TestTrue(TEXT("Save screenshot outside Content"), FFileHelper::SaveArrayToFile(PNG, *Path));
			Test->AddInfo(FString::Printf(TEXT("Pattern PIE screenshot: %s"), *Path));
			if (++CaptureIndex < 2)
			{
				Capture->SetActorLocation(FVector(0, 900, -130));
				Capture->SetActorRotation(FRotator(0, -90, 0));
				ReadyTime = FPlatformTime::Seconds() + 3;
				return false;
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternPIETest, "BreachSystem.Pattern.PIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternPIETest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor exists"), GEditor))
	{
		return false;
	}
	if (!FApp::IsUnattended() || !FParse::Param(FCommandLine::Get(), TEXT("BreachPatternDisposablePIE")) || GEditor->PlayWorld)
	{
		AddWarning(TEXT("Pattern PIE requires a separate unattended editor with -BreachPatternDisposablePIE; current map was left untouched."));
		return true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BreachPatternCapture")) && FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
	{
		AddError(TEXT("-BreachPatternCapture requires a genuine renderer; remove -NullRHI."));
		return false;
	}
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("Unsaved disposable map"), Map))
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
	ADD_LATENT_AUTOMATION_COMMAND(FExercisePatternPIE(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
