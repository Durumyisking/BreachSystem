#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Data/BreachSurfaceMaterial.h"
#include "BreachSyncBreachGuard.h"

// 테스트 전용: 실제 포인트 피해 경로의 절단 및 파편 충돌을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDebrisTest, "BreachSystem.Debris.CommittedCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachDebrisTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube fixture"), Cube))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World fixture"), World))
	{
		return false;
	}

	UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>(World);
	Material->Accumulation.bEnabled = false;
	Material->DebrisMeshes.Add(Cube);

	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(Cube);
	Surface->SetSurfaceMaterial(Material);
	Surface->RegisterComponent();

	AActor* Weapon = World->SpawnActor<AActor>();
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->RegisterComponent();
	Source->BindCircle(5.f, 16);

	auto HitWall = [Surface, Weapon](double X)
	{
		FPointDamageEvent Damage;
		Damage.HitInfo = FHitResult(Surface->GetOwner(), Surface, FVector(X, -50, 0), -FVector::YAxisVector);
		Damage.ShotDirection = FVector::YAxisVector;
		const FBreachSyncBreachGuard SyncBreach;
		Surface->HandleBreachDamage(Surface, Damage, Weapon, 1.f);
	};
	auto CountDebris = [this, World, Cube]()
	{
		int32 Count = 0;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			if (Mesh->GetStaticMesh() == Cube)
			{
				++Count;
				TestTrue(TEXT("Debris simulates physics"), Mesh->IsSimulatingPhysics());
				TestEqual(TEXT("Debris uses physics-only collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::PhysicsOnly);
				TestEqual(TEXT("Debris collides with the floor"), Mesh->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);
				TestEqual(TEXT("Debris ignores players"), Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
				TestEqual(TEXT("Debris ignores other debris"), Mesh->GetCollisionResponseToChannel(ECC_PhysicsBody), ECR_Ignore);
				TestTrue(TEXT("Debris has a finite lifetime"), It->GetLifeSpan() > 0.f);
			}
		}
		return Count;
	};

	HitWall(200);
	TestEqual(TEXT("A proxy-only update spawns no debris"), CountDebris(), 0);
	HitWall(0);
	const int32 AfterCut = CountDebris();
	TestTrue(TEXT("A committed cut spawns the material's debris"), AfterCut > 0);
	HitWall(0);
	TestEqual(TEXT("Cutting the same hole again adds no debris"), CountDebris(), AfterCut);

	Material->DebrisCountMultiplier = 2.f;
	HitWall(20);
	const int32 AfterDouble = CountDebris();
	TestEqual(TEXT("Doubling the multiplier spawns twice as many pieces for the same-sized cut"), AfterDouble - AfterCut, 2 * AfterCut);
	Material->DebrisCountMultiplier = 0.f;
	HitWall(-20);
	TestEqual(TEXT("Zero multiplier disables physical debris"), CountDebris(), AfterDouble);

	World->DestroyWorld(false);
	return !HasAnyErrors();
}

#endif
