#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "UObject/UnrealType.h"
#include "Settings/BreachSystemSettings.h"

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSurfaceMaterialTest, "BreachSystem.Surface.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSurfaceMaterialTest::RunTest(const FString& Parameters)
{
	UClass* MaterialClass = FindObject<UClass>(nullptr, TEXT("/Script/BreachSystem.BreachSurfaceMaterial"));
	if (!TestNotNull(TEXT("Surface material is a registered native asset class"), MaterialClass))
	{
		return false;
	}
	TestTrue(TEXT("Surface material can be authored as a DataAsset"), MaterialClass->IsChildOf(UDataAsset::StaticClass()));
	FObjectProperty* MaterialProperty = FindFProperty<FObjectProperty>(UBreachableSurfaceComponent::StaticClass(), TEXT("SurfaceMaterial"));
	if (!TestNotNull(TEXT("Surface exposes its material reference"), MaterialProperty))
	{
		return false;
	}
	TestEqual(TEXT("Surface reference accepts the material asset class"), MaterialProperty->PropertyClass.Get(), MaterialClass);
	TestTrue(TEXT("Surface material reference is editable"), MaterialProperty->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("Surface material reference is accessible to Blueprint"), MaterialProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSurfaceMaterialSharedTest, "BreachSystem.Surface.SharedMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSurfaceMaterialSharedTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Global cell settings"), Settings)) return false;
	TGuardValue<float> CellSizeGuard(Settings->CellSizeCm, 0.1f);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Shared-material world fixture"), World))
	{
		return false;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Shared-material cube fixture"), Cube))
	{
		World->DestroyWorld(false);
		return false;
	}
	UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>(World);
	Material->Accumulation.bEnabled = true;
	Material->Accumulation.WindowSize = FVector2D(44, 30);
	Material->AccumulationShape.Contour.bEnabled = false;
	auto CreateSurface = [World, Cube, Material](double Offset)
	{
		AActor* Wall = World->SpawnActor<AActor>();
		UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
		Wall->SetRootComponent(Surface);
		Surface->SetStaticMesh(Cube);
		Surface->SetWorldLocation(FVector(Offset, 0, 0));
		Surface->SetSurfaceMaterial(Material);
		// An assigned asset must override both legacy rule and shape settings.
		FBreachAccumulationOptions LegacyOptions = Surface->GetLegacyAccumulation();
		LegacyOptions.bEnabled = false;
		Surface->SetLegacyAccumulation(LegacyOptions);
		FBreachAccumulationShapeOptions LegacyShapeOptions = Surface->GetLegacyAccumulationShape();
		LegacyShapeOptions.Padding = 0;
		Surface->SetLegacyAccumulationShape(LegacyShapeOptions);
		Surface->RegisterComponent();
		return Surface;
	};
	UBreachableSurfaceComponent* SurfaceA = CreateSurface(0);
	UBreachableSurfaceComponent* SurfaceB = CreateSurface(300);
	AActor* Weapon = World->SpawnActor<AActor>();
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->RegisterComponent();
	Source->BindCircle(0.7f, 16);
	Source->BreachDamage = 0.6f;
	auto Trace = [World](UBreachableSurfaceComponent* Surface, double X, double Z, FHitResult& Hit)
	{
		if (!IsValid(Surface))
		{
			return false;
		}
		const FTransform Transform = Surface->GetComponentTransform();
		return World->LineTraceSingleByChannel(Hit,
			Transform.TransformPosition(FVector(X, -100, Z)),
			Transform.TransformPosition(FVector(X, 100, Z)), ECC_Visibility);
	};
	auto Shoot = [this, Weapon, Trace](UBreachableSurfaceComponent* Surface, double X)
	{
		if (!IsValid(Surface))
		{
			return;
		}
		FHitResult Hit;
		if (TestTrue(TEXT("Shot hits the selected wall"), Trace(Surface, X, 0, Hit)))
		{
			UGameplayStatics::ApplyPointDamage(Surface->GetOwner(), 1, -Hit.ImpactNormal, Hit, nullptr, Weapon, nullptr);
		}
	};
	Shoot(SurfaceA, -8);
	Shoot(SurfaceB, -8);
	TestEqual(TEXT("First wall owns one record"), SurfaceA->GetAccumulatedHitCount(), 1);
	TestEqual(TEXT("Second wall owns its separate record"), SurfaceB->GetAccumulatedHitCount(), 1);
	FHitResult Hit;
	TestTrue(TEXT("Shared settings do not combine damage across walls"), Trace(SurfaceA, 0, 8, Hit));
	Shoot(SurfaceA, 8);
	TestFalse(TEXT("Asset rules and padding open first wall's breach"), Trace(SurfaceA, 0, 8, Hit));
	TestTrue(TEXT("First wall's breach leaves second wall geometry intact"), Trace(SurfaceB, 0, 8, Hit));
	TestEqual(TEXT("First wall consumes its own records"), SurfaceA->GetAccumulatedHitCount(), 0);
	TestEqual(TEXT("First wall does not consume second wall's records"), SurfaceB->GetAccumulatedHitCount(), 1);
	struct { bool ReturnValue = false; } ResetParameters;
	SurfaceA->ProcessEvent(SurfaceA->FindFunctionChecked(TEXT("ResetSurface")), &ResetParameters);
	TestTrue(TEXT("First wall resets"), ResetParameters.ReturnValue);
	TestEqual(TEXT("Reset retains the shared material reference"), SurfaceA->GetSurfaceMaterial(), Material);
	TestEqual(TEXT("Reset leaves other wall's records intact"), SurfaceB->GetAccumulatedHitCount(), 1);
	Shoot(SurfaceB, 8);
	TestFalse(TEXT("Second wall can finish its own breach after first wall resets"), Trace(SurfaceB, 0, 8, Hit));
	TestTrue(TEXT("Reset wall's collision stays intact"), Trace(SurfaceA, 0, 8, Hit));
	TestEqual(TEXT("Gameplay preserves asset threshold"), Material->Accumulation.BreachHP, 1.f);
	TestEqual(TEXT("Gameplay preserves asset shape padding"), Material->AccumulationShape.Padding, 10.f);
	World->DestroyWorld(false);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSurfaceMaterialCacheTest, "BreachSystem.Surface.MaterialEditorCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSurfaceMaterialCacheTest::RunTest(const FString& Parameters)
{
	UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>();
	Material->AccumulationShape.Contour.VertexPositionJitter = 1.2f;
	FProperty* ShapeProperty = FindFProperty<FProperty>(UBreachSurfaceMaterial::StaticClass(), TEXT("AccumulationShape"));
	if (!TestNotNull(TEXT("Shape settings can be edited on the asset"), ShapeProperty))
	{
		return false;
	}
	FPropertyChangedEvent Event(ShapeProperty);
	static_cast<UObject*>(Material)->PostEditChangeProperty(Event);
	TestEqual(TEXT("Asset edits update the serialized contour interval"), Material->AccumulationShape.Contour.JitterInterval, 2);
	return !HasAnyErrors();
}

#endif
