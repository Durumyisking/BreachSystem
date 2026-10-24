#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachAccumulationSurfaceTest, "BreachSystem.Accumulation.Surface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachAccumulationSurfaceTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	OutNames.Add(TEXT("UnitScale"));
	OutCommands.Add(TEXT("UnitScale"));
	OutNames.Add(TEXT("MirroredScale"));
	OutCommands.Add(TEXT("MirroredScale"));
}

bool FBreachAccumulationSurfaceTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World fixture"), World))
	{
		return false;
	}
	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube fixture"), Cube))
	{
		World->DestroyWorld(false);
		return false;
	}
	Surface->SetStaticMesh(Cube);
	const FVector Scale = Parameters == TEXT("MirroredScale") ? FVector(-2, 0.1, 3) : FVector::OneVector;
	Surface->SetWorldTransform(FTransform(FRotator(0, 35, 0), FVector(100, 200, 300), Scale));
	Surface->RegisterComponent();
	Surface->Accumulation.WindowSize = FVector2D(44, 30);
	AActor* WeaponA = World->SpawnActor<AActor>();
	AActor* WeaponB = World->SpawnActor<AActor>();
	UBreachSourceComponent* SourceA = NewObject<UBreachSourceComponent>(WeaponA);
	UBreachSourceComponent* SourceB = NewObject<UBreachSourceComponent>(WeaponB);
	SourceA->RegisterComponent();
	SourceB->RegisterComponent();
	SourceA->BindCircle(0.7f, 16);
	SourceB->BindCircle(0.7f, 16);
	SourceA->AccumulationContribution = 0.17f;
	SourceB->AccumulationContribution = 0.17f;

	auto Trace = [World, Surface, Scale](double X, double Z, FHitResult& Hit)
	{
		const FTransform Transform = Surface->GetComponentTransform();
		const FVector LocalPoint(X / FMath::Abs(Scale.X), 0, Z / FMath::Abs(Scale.Z));
		return World->LineTraceSingleByChannel(Hit, Transform.TransformPosition(LocalPoint + FVector(0, -100, 0)),
			Transform.TransformPosition(LocalPoint + FVector(0, 100, 0)), ECC_Visibility);
	};
	auto Shoot = [this, Wall, Trace](double X, AActor* Weapon)
	{
		FHitResult Hit;
		if (TestTrue(TEXT("Shot strikes remaining wall material"), Trace(X, 0, Hit)))
		{
			UGameplayStatics::ApplyPointDamage(Wall, 1, -Hit.ImpactNormal, Hit, nullptr, Weapon, nullptr);
		}
	};
	auto IsSolid = [Trace](double X, double Z)
	{
		FHitResult Hit;
		return Trace(X, Z, Hit);
	};
	auto Reset = [Surface]()
	{
		struct FResetParameters
		{
			bool ReturnValue = false;
		} Params;
		Surface->ProcessEvent(Surface->FindFunctionChecked(TEXT("ResetSurface")), &Params);
	};

	for (double X : {-20.0, -12.0, -4.0, 4.0, 12.0, 20.0})
	{
		Shoot(X, WeaponA);
	}
	TestEqual(TEXT("Disabled accumulation preserves direct-only behavior"), Surface->GetAccumulatedHitCount(), 0);
	TestTrue(TEXT("Disabled accumulation leaves material between small holes"), IsSolid(0, 8));
	Reset();
	Surface->Accumulation.bEnabled = true;
	for (double X : {-20.0, -12.0, -4.0, 4.0, 12.0})
	{
		Shoot(X, WeaponA);
	}
	TestEqual(TEXT("Five direct cuts retain five damage records"), Surface->GetAccumulatedHitCount(), 5);
	TestTrue(TEXT("Five light impacts do not open a large breach"), IsSolid(0, 8));
	Shoot(20, WeaponB);
	TestFalse(TEXT("Sixth impact from another source opens a large breach"), IsSolid(0, 8));
	TestEqual(TEXT("Successful large breach retires records"), Surface->GetAccumulatedHitCount(), 0);
	TArray<UDynamicMeshComponent*> Proxies;
	Wall->GetComponents(Proxies);
	if (TestEqual(TEXT("Accumulated breach keeps one proxy"), Proxies.Num(), 1))
	{
		FHitResult StaleHit;
		StaleHit.Component = Proxies[0];
		StaleHit.ImpactPoint = Surface->GetComponentTransform().TransformPosition(FVector(0, -50, 0));
		const FPointDamageEvent StaleDamage(1, StaleHit, FVector(0, 1, 0), nullptr);
		Surface->HandleBreachDamage(Proxies[0], StaleDamage, WeaponA, 1);
		TestEqual(TEXT("Queued impact inside a now-empty breach adds no record"), Surface->GetAccumulatedHitCount(), 0);
	}
	Reset();
	for (double X : {-40.0, -32.0, -24.0, -16.0, -8.0, 8.0, 16.0, 24.0, 32.0, 40.0})
	{
		Shoot(X, WeaponA);
	}
	TestEqual(TEXT("Separated halves retain ten records"), Surface->GetAccumulatedHitCount(), 10);
	TestTrue(TEXT("Left region solid before central shot"), IsSolid(-20, 8));
	TestTrue(TEXT("Right region solid before central shot"), IsSolid(20, 8));
	Shoot(0, WeaponB);
	TestFalse(TEXT("One central shot opens left region"), IsSolid(-20, 8));
	TestFalse(TEXT("Same central shot opens right region"), IsSolid(20, 8));
	TestEqual(TEXT("Both regions clean up one shared set of records"), Surface->GetAccumulatedHitCount(), 0);
	Reset();
	Shoot(-20, WeaponA);
	TestEqual(TEXT("New series starts with one record"), Surface->GetAccumulatedHitCount(), 1);
	Reset();
	TestEqual(TEXT("Surface reset clears the spatial index"), Surface->GetAccumulatedHitCount(), 0);
	SourceB->AccumulationContribution = 1.05f;
	Shoot(0, WeaponB);
	TestFalse(TEXT("Single strong impact opens a large breach"), IsSolid(0, 8));
	Reset();
	Shoot(-20, WeaponA);
	SourceB->AccumulationContribution = 0;
	SourceB->BindBox(FVector2D(50, 30));
	Shoot(0, WeaponB);
	TestEqual(TEXT("Non-accumulating direct cutter retires removed damage"), Surface->GetAccumulatedHitCount(), 0);
	// The failure fixture requires exact point contact; transformed copies can become valid separate holes.
	if (Parameters == TEXT("UnitScale"))
	{
		Reset();
		SourceB->BindCircle(0.7f, 16);
		SourceB->AccumulationContribution = 1.05f;
		UE::Geometry::FPolygon2d TouchingHole;
		for (const FVector2d& Point : TArray<FVector2d>{{22,15}, {32,15}, {32,25}, {22,25}})
		{
			TouchingHole.AppendVertex(FVector2d(Point.X / FMath::Abs(Scale.X), Point.Y / FMath::Abs(Scale.Z)));
		}
		TestTrue(TEXT("Prepare hole touching the future large cutter at one point"), Surface->ApplyCutterPolygon(TouchingHole));
		Shoot(0, WeaponB);
		TestEqual(TEXT("Late large-cut failure preserves impact for accumulation"), Surface->GetAccumulatedHitCount(), 1);
		TestTrue(TEXT("Late large-cut failure preserves uncut collision"), IsSolid(0, 8));
		TestFalse(TEXT("Failed large cut still keeps the successful small bullet hole"), IsSolid(0, 0));
		TestFalse(TEXT("Failed large cut preserves the existing neighboring hole"), IsSolid(27, 20));
	}

	World->DestroyWorld(false);
	return !HasAnyErrors();
}
#endif
