#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/BreachSourceComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Data/BreachSurfaceMaterial.h"
#include "UObject/UnrealType.h"
#include "BreachSyncBreachGuard.h"

// 테스트 전용: 평면 흔적이 피격면에만 별도 표시 메시로 생성되는지 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationProceduralContractTest, "BreachSystem.Decoration.ProceduralMaterial", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachDecorationProceduralContractTest::RunTest(const FString& Parameters)
{
	FStructProperty* Options = FindFProperty<FStructProperty>(UBreachSurfaceMaterial::StaticClass(), TEXT("FeatureDecoration"));
	if (!TestNotNull(TEXT("Feature decoration settings exist"), Options))
	{
		return false;
	}
	FObjectProperty* Material = FindFProperty<FObjectProperty>(Options->Struct, TEXT("Material"));
	TestNotNull(TEXT("Procedural features accept a material without a static mesh"), Material);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationCutTest, "BreachSystem.Decoration.Cut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachDecorationCutTest::RunTest(const FString& Parameters)
{
	FStructProperty* Options = FindFProperty<FStructProperty>(UBreachSurfaceMaterial::StaticClass(), TEXT("CutDecoration"));
	if (!TestNotNull(TEXT("Surface material supports cut decoration authoring"), Options))
	{
		return false;
	}
	FObjectProperty* MaterialProperty = FindFProperty<FObjectProperty>(Options->Struct, TEXT("Material"));
	if (!TestNotNull(TEXT("Cut decoration accepts a surface material"), MaterialProperty))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Fixture world"), World))
	{
		return false;
	}
	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>(Wall);
	void* OptionData = Options->ContainerPtrToValuePtr<void>(Material);
	MaterialProperty->SetObjectPropertyValue_InContainer(OptionData, UMaterial::GetDefaultMaterial(MD_Surface));
	Surface->SetSurfaceMaterial(Material);
	Surface->RegisterComponent();
	UE::Geometry::FPolygon2d Cutter;
	Cutter.AppendVertex(FVector2d(-5, -5));
	Cutter.AppendVertex(FVector2d(5, -5));
	Cutter.AppendVertex(FVector2d(5, 5));
	Cutter.AppendVertex(FVector2d(-5, 5));
	TestTrue(TEXT("Cut succeeds"), Surface->ApplyCutterPolygon(Cutter));
	TInlineComponentArray<UDynamicMeshComponent*> Meshes(Wall);
	int32 DecorationCount = 0;
	for (UDynamicMeshComponent* Mesh : Meshes)
	{
		if (!Mesh->IsA<UBreachSurfaceProxy>())
		{
			++DecorationCount;
			TestTrue(TEXT("Decoration has triangles"), Mesh->GetMesh()->TriangleCount() > 0);
			TestEqual(TEXT("Decoration does not block bullets or movement"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			for (int32 VertexID : Mesh->GetMesh()->VertexIndicesItr())
			{
				TestTrue(TEXT("Default decoration lies on the minus Y face"), Mesh->GetMesh()->GetVertex(VertexID).Y < -50);
			}
		}
	}
	TestEqual(TEXT("Only the impacted face gets a decoration mesh"), DecorationCount, 1);
	World->DestroyWorld(false);
	return !HasAnyErrors();
}

namespace
{
	// 테스트 전용: 사용자 에셋을 변경하지 않고 엔진 기본 메시와 임시 재질 설정으로 표면을 만든다.
	struct FDecorationFixture
	{
		UWorld* World = nullptr;
		AActor* Wall = nullptr;
		UBreachableSurfaceComponent* Surface = nullptr;
		UBreachSurfaceMaterial* Material = nullptr;

		FDecorationFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			check(World);
			Wall = World->SpawnActor<AActor>();
			check(Wall);
			Surface = NewObject<UBreachableSurfaceComponent>(Wall);
			Wall->SetRootComponent(Surface);
			Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Material = NewObject<UBreachSurfaceMaterial>(Wall);
			Material->CutDecoration.Material = UMaterial::GetDefaultMaterial(MD_Surface);
			Surface->SetSurfaceMaterial(Material);
			Surface->RegisterComponent();
		}

		~FDecorationFixture()
		{
			check(World);
			World->DestroyWorld(false);
		}

		bool Cut(double X, double Z, double Half, EBreachSurfaceSide Side = EBreachSurfaceSide::Front)
		{
			check(Surface);
			UE::Geometry::FPolygon2d Polygon;
			Polygon.AppendVertex(FVector2d(X - Half, Z - Half));
			Polygon.AppendVertex(FVector2d(X + Half, Z - Half));
			Polygon.AppendVertex(FVector2d(X + Half, Z + Half));
			Polygon.AppendVertex(FVector2d(X - Half, Z + Half));
			return Surface->ApplyCutterPolygon(Polygon, Side);
		}

		TArray<UDynamicMeshComponent*> Traces() const
		{
			check(Wall);
			check(Material);
			TArray<UDynamicMeshComponent*> Result;
			TInlineComponentArray<UDynamicMeshComponent*> Meshes(Wall);
			for (UDynamicMeshComponent* Mesh : Meshes)
			{
				check(Mesh);
				if (!Mesh->IsA<UBreachSurfaceProxy>() && Mesh->GetMaterial(0) == Material->CutDecoration.Material)
				{
					Result.Add(Mesh);
				}
			}
			return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationLifecycleTest, "BreachSystem.Decoration.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationLifecycleTest::RunTest(const FString& Parameters)
{
	FDecorationFixture F;
	TestTrue(TEXT("Initial cut"), F.Cut(-20, 0, 2));
	TestEqual(TEXT("One front trace"), F.Traces().Num(), 1);
	TestTrue(TEXT("No-op cut is compatible"), F.Cut(-20, 0, 2, EBreachSurfaceSide::Back));
	TestEqual(TEXT("No-op cut creates no back trace"), F.Traces().Num(), 1);
	TestTrue(TEXT("Back cut"), F.Cut(20, 0, 2, EBreachSurfaceSide::Back));
	TestEqual(TEXT("One display per impacted face"), F.Traces().Num(), 2);
	F.Surface->UnregisterComponent();
	for (UDynamicMeshComponent* Trace : F.Traces())
	{
		TestFalse(TEXT("Unregister removes rendering"), Trace->IsRegistered());
	}
	F.Surface->RegisterComponent();
	for (UDynamicMeshComponent* Trace : F.Traces())
	{
		TestTrue(TEXT("Reregister restores rendering"), Trace->IsRegistered());
		TestTrue(TEXT("Reregister preserves trace geometry"), Trace->GetMesh()->TriangleCount() > 0);
	}
	F.Surface->SetWorldLocation(FVector(100, 200, 300));
	for (UDynamicMeshComponent* Trace : F.Traces())
	{
		TestTrue(TEXT("Traces follow wall movement"), Trace->GetComponentLocation().Equals(FVector(100, 200, 300)));
	}
	struct { bool ReturnValue = false; } Reset;
	F.Surface->ProcessEvent(F.Surface->FindFunctionChecked(TEXT("ResetSurface")), &Reset);
	TestTrue(TEXT("Surface reset succeeds"), Reset.ReturnValue);
	TestEqual(TEXT("Reset destroys trace displays"), F.Traces().Num(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationSettingsTest, "BreachSystem.Decoration.SettingsReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationSettingsTest::RunTest(const FString& Parameters)
{
	FDecorationFixture F;
	TestTrue(TEXT("Front mark"), F.Cut(-20, 0, 2));
	UBreachSurfaceMaterial* Replacement = NewObject<UBreachSurfaceMaterial>(F.Wall);
	Replacement->CutDecoration = F.Material->CutDecoration;
	F.Surface->SetSurfaceMaterial(Replacement);
	TestEqual(TEXT("Replacing settings waits for a successful cut"), F.Traces().Num(), 1);
	TestTrue(TEXT("New back mark"), F.Cut(20, 0, 2, EBreachSurfaceSide::Back));
	TestEqual(TEXT("A replacement asset clears old marks even with identical settings"), F.Traces().Num(), 1);
	for (UDynamicMeshComponent* Trace : F.Traces())
	{
		for (int32 ID : Trace->GetMesh()->VertexIndicesItr())
		{
			TestTrue(TEXT("Only replacement back-side mark remains"), Trace->GetMesh()->GetVertex(ID).Y > 50);
		}
	}
	F.Surface->SetSurfaceMaterial(nullptr);
	TestTrue(TEXT("Cut without settings still succeeds"), F.Cut(0, 20, 2));
	TestEqual(TEXT("Removing settings removes old Deco"), F.Traces().Num(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationGeneratedTest, "BreachSystem.Decoration.GeneratedDisplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationGeneratedTest::RunTest(const FString& Parameters)
{
	FDecorationFixture F;
	F.Material->FeatureDecoration.Material = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), F.Wall);
	F.Surface->SetWorldScale3D(FVector(2, 1, 3));
	TestTrue(TEXT("Below world area threshold"), F.Cut(0, 0, 3));
	TInlineComponentArray<UDynamicMeshComponent*> Small(F.Wall);
	TestEqual(TEXT("216cm2 has only wall and flat trace"), Small.Num(), 2);
	TestTrue(TEXT("Above world area threshold"), F.Cut(0, 0, 5));
	TInlineComponentArray<UDynamicMeshComponent*> Large(F.Wall);
	TestEqual(TEXT("600cm2 adds exactly one generated feature display"), Large.Num(), 3);
	UDynamicMeshComponent* Feature = nullptr;
	for (UDynamicMeshComponent* Display : Large)
	{
		if (Display->GetMaterial(0) == F.Material->FeatureDecoration.Material)
		{
			Feature = Display;
		}
	}
	if (!TestNotNull(TEXT("Feature display uses the configured material"), Feature))
	{
		return false;
	}
	TestTrue(TEXT("Generated plates have thickness and closed sides"), Feature->GetMesh()->IsClosed());
	TestEqual(TEXT("Generated plates have no collision"), Feature->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Generated plates do not affect navigation"), Feature->CanEverAffectNavigation());
	TArray<FVector3d> Before;
	for (int32 ID : Feature->GetMesh()->VertexIndicesItr())
	{
		Before.Add(Feature->GetMesh()->GetVertex(ID));
	}
	F.Material->FeatureDecoration.Material = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), F.Wall);
	TestTrue(TEXT("Disjoint small cut updates the material"), F.Cut(30, 30, 0.5));
	TestTrue(TEXT("Material-only change retains the feature component"), IsValid(Feature));
	if (IsValid(Feature))
	{
		TArray<FVector3d> After;
		for (int32 ID : Feature->GetMesh()->VertexIndicesItr())
		{
			After.Add(Feature->GetMesh()->GetVertex(ID));
		}
		TestTrue(TEXT("Material swap preserves feature vertices"), Before == After);
		TestEqual(TEXT("Material swap updates the shader reference"), Feature->GetMaterial(0), F.Material->FeatureDecoration.Material.Get());
	}
	// 테스트 전용: UV 크기를 바꾸면 다음 성공한 절삭에서 기존 표시에도 새 밀도를 적용한다.
	const FVector2f BeforeUV = Feature->GetMesh()->Attributes()->PrimaryUV()->GetElement(1);
	F.Material->FeatureDecoration.TextureWorldSize *= 2;
	TestTrue(TEXT("Texture world size change applies on the next successful cut"), F.Cut(32, 32, 0.5));
	TInlineComponentArray<UDynamicMeshComponent*> Remapped(F.Wall);
	UDynamicMeshComponent* RemappedFeature = nullptr;
	for (UDynamicMeshComponent* Display : Remapped)
	{
		if (Display->GetMaterial(0) == F.Material->FeatureDecoration.Material)
		{
			RemappedFeature = Display;
		}
	}
	if (TestNotNull(TEXT("Remapped feature exists"), RemappedFeature))
	{
		const FVector2f AfterUV = RemappedFeature->GetMesh()->Attributes()->PrimaryUV()->GetElement(1);
		TestTrue(TEXT("Doubling texture world size halves existing plate UVs"), AfterUV.Equals(BeforeUV * 0.5f, 1.e-5f));
	}
	F.Surface->DestroyComponent();
	TInlineComponentArray<UDynamicMeshComponent*> Destroyed(F.Wall);
	TestEqual(TEXT("Destroying surface removes all generated displays"), Destroyed.Num(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationDamageFaceTest, "BreachSystem.Decoration.DamageFace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationDamageFaceTest::RunTest(const FString& Parameters)
{
	FDecorationFixture F;
	AActor* Weapon = F.World->SpawnActor<AActor>();
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->RegisterComponent();
	Source->BindCircle(1, 16);
	FPointDamageEvent Damage;
	Damage.HitInfo.Component = F.Surface;
	Damage.HitInfo.ImpactPoint = FVector(0, 50, 0);
	const FBreachSyncBreachGuard SyncBreach;
	F.Surface->HandleBreachDamage(F.Surface, Damage, Weapon, 1);
	TestEqual(TEXT("Gameplay damage creates one hit-face trace"), F.Traces().Num(), 1);
	for (UDynamicMeshComponent* Trace : F.Traces())
	{
		for (int32 ID : Trace->GetMesh()->VertexIndicesItr())
		{
			TestTrue(TEXT("Gameplay hit chooses actual plus Y face"), Trace->GetMesh()->GetVertex(ID).Y > 50);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationRepeatedTest, "BreachSystem.Decoration.RepeatedCuts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationRepeatedTest::RunTest(const FString& Parameters)
{
	FDecorationFixture F;
	for (int32 I = 0; I < 100; ++I)
	{
		const double X = -36 + (I % 10) * 8;
		const double Z = -36 + (I / 10) * 8;
		if (!TestTrue(TEXT("Independent repeated cut succeeds"), F.Cut(X, Z, 0.5)))
		{
			break;
		}
		TestEqual(TEXT("100 cuts reuse one face display"), F.Traces().Num(), 1);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationAccumulationTest, "BreachSystem.Decoration.Accumulation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationAccumulationTest::RunTest(const FString& Parameters)
{
	FDecorationFixture F;
	F.Material->Accumulation.bEnabled = true;
	F.Material->Accumulation.WindowSize = FVector2D(40, 40);
	F.Material->AccumulationShape.Padding = 10;
	F.Material->AccumulationShape.Contour.bEnabled = false;
	F.Material->FeatureDecoration.Material = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), F.Wall);
	AActor* Weapon = F.World->SpawnActor<AActor>();
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->RegisterComponent();
	Source->BindCircle(0.5f, 16);
	Source->BreachDamage = 0.6f;
	for (double X : {-10.0, 10.0})
	{
		TInlineComponentArray<UBreachSurfaceProxy*> Proxies(F.Wall);
		UPrimitiveComponent* Hit = Proxies.IsEmpty() ? static_cast<UPrimitiveComponent*>(F.Surface) : Proxies[0];
		FPointDamageEvent Damage;
		Damage.HitInfo.Component = Hit;
		Damage.HitInfo.ImpactPoint = FVector(X, 50, 0);
		const FBreachSyncBreachGuard SyncBreach;
		F.Surface->HandleBreachDamage(Hit, Damage, Weapon, 1);
	}
	TestEqual(TEXT("Accumulated breach consumes damage records"), F.Surface->GetAccumulatedHitCount(), 0);
	TestEqual(TEXT("Accumulation reuses one impacted-face display"), F.Traces().Num(), 1);
	TInlineComponentArray<UDynamicMeshComponent*> Meshes(F.Wall);
	TArray<UDynamicMeshComponent*> Chunks;
	for (UDynamicMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetMaterial(0) == F.Material->FeatureDecoration.Material)
		{
			Chunks.Add(Mesh);
		}
	}
	TestEqual(TEXT("Accumulation creates chunks on its large breach"), Chunks.Num(), 1);
	for (UDynamicMeshComponent* Trace : F.Traces())
	{
		for (int32 ID : Trace->GetMesh()->VertexIndicesItr())
		{
			TestTrue(TEXT("Accumulated cut keeps the originating hit side"), Trace->GetMesh()->GetVertex(ID).Y > 50);
		}
	}
	return !HasAnyErrors();
}
#endif
