#include "Components/BreachableSurfaceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "VectorUtil.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachSurfaceLifecycleTest, "BreachSystem.Surface.ProxyLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachSurfaceLifecycleTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("OriginalUV"));
	OutTestCommands.Add(TEXT("OriginalUV"));
	OutBeautifiedNames.Add(TEXT("AuthoredUV"));
	OutTestCommands.Add(TEXT("AuthoredUV"));
}

bool FBreachSurfaceLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	UStaticMeshComponent* StaticSurface = Cast<UStaticMeshComponent>(Surface);
	if (!TestNotNull(TEXT("Unbroken wall uses the original static mesh"), StaticSurface))
	{
		World->DestroyWorld(false);
		return false;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube fixture exists"), Cube))
	{
		World->DestroyWorld(false);
		return false;
	}
	if (!TestNotNull(TEXT("Cube fixture has render data"), Cube->GetRenderData()))
	{
		World->DestroyWorld(false);
		return false;
	}
	UStaticMesh* SourceMesh = Cube;
	if (Parameters == TEXT("AuthoredUV"))
	{
		const FMeshDescription* OriginalDescription = Cube->GetMeshDescription(0);
		if (!TestNotNull(TEXT("Cube fixture has a source description"), OriginalDescription))
		{
			World->DestroyWorld(false);
			return false;
		}
		FMeshDescription Description = *OriginalDescription;
		FStaticMeshAttributes Attributes(Description);
		auto UVs = Attributes.GetVertexInstanceUVs();
		const auto Normals = Attributes.GetVertexInstanceNormals();
		for (FVertexInstanceID Instance : Description.VertexInstances().GetElementIDs())
		{
			const FVector2f UV = UVs.Get(Instance, 0);
			UVs.Set(Instance, 0, Normals[Instance].Y < 0
				? FVector2f(0.25f - 1.5f * UV.Y, 0.4f + 2.25f * UV.X)
				: FVector2f(-0.6f + 0.75f * UV.X, 1.2f - 1.25f * UV.Y));
		}
		// Build a transient fixture; the engine asset and its render buffers stay untouched.
		SourceMesh = NewObject<UStaticMesh>(GetTransientPackage());
		SourceMesh->GetStaticMaterials() = Cube->GetStaticMaterials();
		UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
		BuildParams.bFastBuild = true;
		BuildParams.bAllowCpuAccess = true;
		BuildParams.bMarkPackageDirty = false;
		BuildParams.PerLODOverrides.SetNum(1);
		BuildParams.PerLODOverrides[0].bUseFullPrecisionUVs = true;
		if (!TestTrue(TEXT("Authored UV fixture builds"), SourceMesh->BuildFromMeshDescriptions({ &Description }, BuildParams)))
		{
			World->DestroyWorld(false);
			return false;
		}
	}
	StaticSurface->SetStaticMesh(SourceMesh);
	UE::Geometry::FDynamicMesh3 OriginalMesh;
	if (!TestTrue(TEXT("Original render mesh can be read for UV comparison"),
		UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::Convert(&SourceMesh->GetRenderData()->LODResources[0], {}, OriginalMesh)))
	{
		World->DestroyWorld(false);
		return false;
	}
	auto CheckOriginalUVs = [this, &OriginalMesh](const UE::Geometry::FDynamicMesh3& Mesh, const FString& Stage)
	{
		const auto* OriginalUV = OriginalMesh.Attributes()->PrimaryUV();
		const auto* ResultUV = Mesh.Attributes()->PrimaryUV();
		if (!TestNotNull(Stage + TEXT(" has source UVs"), OriginalUV))
		{
			return;
		}
		if (!TestNotNull(Stage + TEXT(" has result UVs"), ResultUV))
		{
			return;
		}
		int32 ComparedFrontCorners = 0;
		int32 ComparedBackCorners = 0;
		double MaxUVError = 0.0;
		for (int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const FVector3d Normal = Mesh.GetTriNormal(TriangleID);
			if (FMath::Abs(Normal.Y) < 0.99)
			{
				continue;
			}
			for (int32 OriginalTriangleID : OriginalMesh.TriangleIndicesItr())
			{
				if (Normal.Dot(OriginalMesh.GetTriNormal(OriginalTriangleID)) < 0.99)
				{
					continue;
				}
				FVector3d A, B, C;
				OriginalMesh.GetTriVertices(OriginalTriangleID, A, B, C);
				const UE::Geometry::FIndex3i OriginalElements = OriginalUV->GetTriangle(OriginalTriangleID);
				const UE::Geometry::FIndex3i Elements = ResultUV->GetTriangle(TriangleID);
				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					// The cube fixture has one affine UV map per face, so this also checks newly cut vertices.
					const FVector3d Barycentric = UE::Geometry::VectorUtil::BarycentricCoords(Mesh.GetVertex(Triangle[Corner]), A, B, C);
					const FVector2d Expected = FVector2d(OriginalUV->GetElement(OriginalElements.A)) * Barycentric.X
						+ FVector2d(OriginalUV->GetElement(OriginalElements.B)) * Barycentric.Y
						+ FVector2d(OriginalUV->GetElement(OriginalElements.C)) * Barycentric.Z;
					MaxUVError = FMath::Max(MaxUVError, (FVector2d(ResultUV->GetElement(Elements[Corner])) - Expected).Length());
					if (Normal.Y < 0)
					{
						++ComparedFrontCorners;
					}
					else
					{
						++ComparedBackCorners;
					}
				}
				break;
			}
		}
		TestTrue(Stage + TEXT(" compares original UVs on front face"), ComparedFrontCorners > 0);
		TestTrue(Stage + TEXT(" compares original UVs on back face"), ComparedBackCorners > 0);
		TestTrue(Stage + FString::Printf(TEXT(" preserves StaticMesh UVs (max error %.6f)"), MaxUVError), MaxUVError < 0.0001);
	};
	StaticSurface->SetCollisionProfileName(TEXT("BlockAll"));
	Surface->SetWorldTransform(FTransform(FRotator(0, 35, 0), FVector(100, 200, 300), FVector(2, 0.1, 3)));
	Surface->RegisterComponent();
	auto CallSurfaceFunction = [Surface](FName Name)
	{
		struct { bool ReturnValue = false; } Params;
		Surface->ProcessEvent(Surface->FindFunctionChecked(Name), &Params);
		return Params.ReturnValue;
	};
	TArray<UDynamicMeshComponent*> Proxies;
	Wall->GetComponents(Proxies);
	TestEqual(TEXT("No dynamic component before first damage"), Proxies.Num(), 0);
	AActor* Weapon = World->SpawnActor<AActor>();
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->RegisterComponent();
	Source->BindBox(FVector2D(20, 30));
	auto Shoot = [Wall, Weapon, Surface](UPrimitiveComponent* Target, FVector LocalPoint)
	{
		FHitResult Hit;
		Hit.Component = Target;
		Hit.ImpactPoint = Surface->GetComponentTransform().TransformPosition(LocalPoint);
		Hit.ImpactNormal = Surface->GetComponentTransform().TransformVectorNoScale(FVector(0, -1, 0));
		UGameplayStatics::ApplyPointDamage(Wall, 1, -Hit.ImpactNormal, Hit, nullptr, Weapon, nullptr);
	};
	Shoot(Surface, FVector(-20, -50, 0));
	Wall->GetComponents(Proxies);
	if (TestEqual(TEXT("First damage creates exactly one proxy"), Proxies.Num(), 1))
	{
		UDynamicMeshComponent* Proxy = Proxies[0];
		TestFalse(TEXT("Proxy has no tick"), Proxy->IsComponentTickEnabled());
		TestFalse(TEXT("Original is hidden after successful breach"), Surface->IsVisible());
		TestEqual(TEXT("Original collision disabled"), Surface->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Proxy is closed"), Proxy->GetMesh()->IsClosed());
		CheckOriginalUVs(*Proxy->GetMesh(), TEXT("First breach"));
		auto HitsWall = [World, Surface](FVector LocalPoint)
		{
			FHitResult Hit;
			const FTransform Transform = Surface->GetComponentTransform();
			return World->LineTraceSingleByChannel(Hit,
				Transform.TransformPosition(LocalPoint + FVector(0, -100, 0)),
				Transform.TransformPosition(LocalPoint + FVector(0, 150, 0)), ECC_Visibility);
		};
		TestFalse(TEXT("First hole is open to traces"), HitsWall(FVector(-20, 0, 0)));
		TestTrue(TEXT("Untouched region still blocks"), HitsWall(FVector(20, 0, 0)));
		Shoot(Proxy, FVector(20, -50, 0));
		TestFalse(TEXT("Proxy forwards second damage, opening another hole"), HitsWall(FVector(20, 0, 0)));
		TestTrue(TEXT("Second breach remains closed"), Proxy->GetMesh()->IsClosed());
		CheckOriginalUVs(*Proxy->GetMesh(), TEXT("Second breach"));
		TestTrue(TEXT("Reset succeeds"), CallSurfaceFunction(TEXT("ResetSurface")));
		TestTrue(TEXT("Reset restores original visibility"), Surface->IsVisible());
		TestTrue(TEXT("Reset restores wall collision"), HitsWall(FVector(-20, 0, 0)));
		Wall->GetComponents(Proxies);
		TestEqual(TEXT("Reset removes proxy"), Proxies.Num(), 0);
		Shoot(Surface, FVector(0, -50, 20));
		Wall->GetComponents(Proxies);
		if (TestEqual(TEXT("Breach after reset creates one proxy"), Proxies.Num(), 1))
		{
			CheckOriginalUVs(*Proxies[0]->GetMesh(), TEXT("Breach after reset"));
		}
		TestTrue(TEXT("Reset before full removal"), CallSurfaceFunction(TEXT("ResetSurface")));
		Source->BindBox(FVector2D(1000, 1000));
		Shoot(Surface, FVector(0, -50, 0));
		Wall->GetComponents(Proxies);
		if (TestEqual(TEXT("Full removal also switches away from static wall"), Proxies.Num(), 1))
		{
			TestEqual(TEXT("Full removal produces empty mesh"), Proxies[0]->GetMesh()->TriangleCount(), 0);
			TestFalse(TEXT("Fully removed wall no longer blocks"), HitsWall(FVector(0, 0, 0)));
			TestTrue(TEXT("Reset after full removal"), CallSurfaceFunction(TEXT("ResetSurface")));
			TestTrue(TEXT("Reset after full removal restores collision"), HitsWall(FVector(0, 0, 0)));
		}
	}
	World->DestroyWorld(false);
	return !HasAnyErrors();
}

#endif

