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

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSurfaceLifecycleTest, "BreachSystem.Surface.ProxyLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
	StaticSurface->SetStaticMesh(Cube);
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
		TestTrue(TEXT("Reset succeeds"), CallSurfaceFunction(TEXT("ResetSurface")));
		TestTrue(TEXT("Reset restores original visibility"), Surface->IsVisible());
		TestTrue(TEXT("Reset restores wall collision"), HitsWall(FVector(-20, 0, 0)));
		Wall->GetComponents(Proxies);
		TestEqual(TEXT("Reset removes proxy"), Proxies.Num(), 0);
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

