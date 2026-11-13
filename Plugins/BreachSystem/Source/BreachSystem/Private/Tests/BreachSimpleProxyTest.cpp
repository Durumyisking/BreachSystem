#include "Misc/AutomationTest.h"

// 테스트 전용: 별도 물리 바디, Cooking 완료 전 보존, 실패와 파괴 중 수명을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSimpleProxy.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachSourceComponent.h"
#include "Settings/BreachSystemSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "BreachSimpleAsyncTestUtils.h"

namespace
{
	struct FCookResult
	{
		bool bComplete = false;
		bool bSucceeded = false;
	};

	bool WaitForCook(const TSharedRef<FCookResult>& Result)
	{
		const double Deadline = FPlatformTime::Seconds() + 5;
		while (!Result->bComplete)
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			if (FPlatformTime::Seconds() > Deadline)
			{
				return false;
			}
			FPlatformProcess::SleepNoStats(0.001f);
		}
		return true;
	}

	UE::Geometry::FPolygon2d ProxyCut(double Right)
	{
		UE::Geometry::FPolygon2d Polygon;
		for (const FVector2d Point : {FVector2d(-30, -30), FVector2d(Right, -30), FVector2d(Right, 30), FVector2d(-30, 30)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleProxyTest, "BreachSystem.Surface.SimpleProxy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachSimpleProxyTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	check(Settings);
	TGuardValue<double> Threshold(Settings->ComplexHoleSizeThreshold, 500.0);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Proxy test world"), World))
	{
		return false;
	}
	const TStrongObjectPtr<UWorld> KeepWorld(World);
	AActor* Wall = World->SpawnActor<AActor>();
	check(Wall);
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Surface->RegisterComponent();
	const UE::Geometry::FPolygon2d Tiny = ProxyCut(-29);
	TestTrue(TEXT("Small initial cut succeeds"), Surface->ApplyCutterPolygon(Tiny));
	UBreachSimpleProxy* Simple = Wall->FindComponentByClass<UBreachSimpleProxy>();
	UBreachSurfaceProxy* Complex = Wall->FindComponentByClass<UBreachSurfaceProxy>();
	if (!TestNotNull(TEXT("Simple primitive exists"), Simple) || !TestNotNull(TEXT("Complex mesh exists"), Complex))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Initial revision finishes"), FBreachSimpleAsyncTestAccess::Wait(Surface));
	TestEqual(TEXT("Subthreshold damage does not cook the initial box"), FBreachSimpleAsyncTestAccess::CookCount(Simple), 0u);
	TestEqual(TEXT("Complex has no Simple shapes"), Complex->GetSimpleCollisionShapes().GetElementCount(), 0);
	TestNull(TEXT("Simple has no triangle collision data provider"), Cast<IInterface_CollisionDataProvider>(Simple));
	TestFalse(TEXT("Simple has no Tick"), Simple->PrimaryComponentTick.bCanEverTick);
	TestNull(TEXT("Simple has no render proxy"), Simple->CreateSceneProxy());
	TestTrue(TEXT("Broad opening succeeds"), Surface->ApplyCutterPolygon(ProxyCut(30)));
	TestTrue(TEXT("Geometry and Cooking both finish"), FBreachSimpleAsyncTestAccess::Wait(Surface));
	const uint32 CookCount = FBreachSimpleAsyncTestAccess::CookCount(Simple);
	TestEqual(TEXT("Broad opening requires one Simple cook"), CookCount, 1u);
	TestTrue(TEXT("Worker prepares hull indices before requesting Cooking"), FBreachSimpleAsyncTestAccess::InputIndicesReady(Simple));
	UBodySetup* Active = Simple->GetBodySetup();
	check(Active);
	TestTrue(TEXT("Published Simple has cooked physics meshes"), Active->bCreatedPhysicsMeshes);
	FCookBodySetupInfo SimpleInfo;
	Active->GetCookInfo(SimpleInfo, static_cast<EPhysXMeshCookFlags>(0));
	TestFalse(TEXT("Simple cook never includes Complex triangles"), SimpleInfo.bCookTriMesh);
	TestTrue(TEXT("Mirrored Simple cooking is preserved"), SimpleInfo.bCookMirroredConvex);
	FCookBodySetupInfo ComplexInfo;
	UBodySetup* ComplexBody = Complex->GetBodySetup();
	check(ComplexBody);
	TestTrue(TEXT("Complex retains negative-scale collision support"), ComplexBody->bGenerateMirroredCollision);
	ComplexBody->GetCookInfo(ComplexInfo, static_cast<EPhysXMeshCookFlags>(0));
	TestFalse(TEXT("Complex cook never includes Simple convexes"), ComplexInfo.bCookNonMirroredConvex || ComplexInfo.bCookMirroredConvex);
	TestTrue(TEXT("Repeated opening succeeds"), Surface->ApplyCutterPolygon(ProxyCut(30)));
	TestTrue(TEXT("Repeated opening finishes"), FBreachSimpleAsyncTestAccess::Wait(Surface));
	TestEqual(TEXT("Unchanged collision skips Cooking"), FBreachSimpleAsyncTestAccess::CookCount(Simple), CookCount);
	TestTrue(TEXT("Unchanged collision preserves the active BodySetup"), Simple->GetBodySetup() == Active);

	AActor* Weapon = World->SpawnActor<AActor>();
	check(Weapon);
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->BindBox(FVector2D(6, 6));
	Source->RegisterComponent();
	FPointDamageEvent Damage;
	Damage.HitInfo.Component = Simple;
	Damage.HitInfo.ImpactPoint = FVector(-40, -50, 40);
	Damage.HitInfo.ImpactNormal = FVector(0, -1, 0);
	Simple->ReceiveComponentDamage(1, Damage, nullptr, Weapon);
	FCollisionQueryParams BulletParams(SCENE_QUERY_STAT(BreachSimpleProxyDamage), true);
	FHitResult BulletHit;
	TestFalse(TEXT("Simple proxy forwards damage and updates Complex immediately"), World->LineTraceSingleByChannel(BulletHit, FVector(-40, -100, 40), FVector(-40, 100, 40), ECC_Visibility, BulletParams));
	TestTrue(TEXT("Forwarded damage finishes its async update"), FBreachSimpleAsyncTestAccess::Wait(Surface));
	AddInfo(FString::Printf(TEXT("Collision-only proxy object size=%llu bytes; active Simple shapes=%d; Complex Simple shapes=%d."), static_cast<uint64>(sizeof(UBreachSimpleProxy)), Active->AggGeom.GetElementCount(), Complex->GetSimpleCollisionShapes().GetElementCount()));

	FBreachSimpleAsyncTestAccess::FailNextCook(Simple);
	const uint64 Published = FBreachSimpleAsyncTestAccess::PublishedRevision(Surface);
	AddExpectedError(TEXT("kept its previous Simple collision after calculation or cooking failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Cut before forced Cooking failure"), Surface->ApplyCutterPolygon(ProxyCut(40)));
	TestTrue(TEXT("Failed Cooking request retires"), FBreachSimpleAsyncTestAccess::Wait(Surface));
	TestTrue(TEXT("Failed cook preserves active physics"), Simple->GetBodySetup() == Active);
	TestEqual(TEXT("Failed cook does not publish a revision"), FBreachSimpleAsyncTestAccess::PublishedRevision(Surface), Published);
	FBreachSimpleAsyncTestAccess::DropNextCookedHull(Simple);
	AddExpectedError(TEXT("kept its previous Simple collision after calculation or cooking failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Cut before missing-hull result"), Surface->ApplyCutterPolygon(ProxyCut(42)));
	TestTrue(TEXT("Incomplete cooked result retires"), FBreachSimpleAsyncTestAccess::Wait(Surface));
	TestTrue(TEXT("Missing cooked hull preserves the previous BodySetup"), Simple->GetBodySetup() == Active);
	TestEqual(TEXT("Missing cooked hull does not publish a revision"), FBreachSimpleAsyncTestAccess::PublishedRevision(Surface), Published);

	// 완료 콜백 전에는 실제 물리 바디가 바뀌지 않아야 한다.
	const FKAggregateGeom Shapes = Active->AggGeom;
	const auto Result = MakeShared<FCookResult>();
	Simple->CookCollision(Shapes, [Result](bool bSucceeded)
	{
		Result->bSucceeded = bSucceeded;
		Result->bComplete = true;
	});
	TestTrue(TEXT("Cook remains pending until its GameThread completion"), FBreachSimpleAsyncTestAccess::IsCooking(Simple));
	TestFalse(TEXT("Completion is not synchronous"), Result->bComplete);
	TestTrue(TEXT("In-flight Cooking preserves active physics"), Simple->GetBodySetup() == Active);
	TestTrue(TEXT("Direct Cooking completes"), WaitForCook(Result));
	TestTrue(TEXT("Real engine Cooking succeeded"), Result->bSucceeded);
	TestTrue(TEXT("Completed cook waits for publication approval"), Simple->GetBodySetup() == Active);
	Simple->DiscardCollision();

	const auto Retired = MakeShared<FCookResult>();
	Simple->CookCollision(Shapes, [Retired](bool bSucceeded)
	{
		Retired->bSucceeded = bSucceeded;
		Retired->bComplete = true;
	});
	Surface->DestroyComponent();
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("Destroyed proxy still retires the in-flight cook after GC"), WaitForCook(Retired));
	TestFalse(TEXT("Destroyed proxy cannot publish Cooking success"), Retired->bSucceeded);
	World->DestroyWorld(false);

	UWorld::InitializationValues Init;
	Init.CreatePhysicsScene(false);
	UWorld* NoPhysics = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	check(NoPhysics);
	AActor* PreviewOwner = NoPhysics->SpawnActor<AActor>();
	check(PreviewOwner);
	UBreachSimpleProxy* Preview = NewObject<UBreachSimpleProxy>(PreviewOwner);
	PreviewOwner->SetRootComponent(Preview);
	Preview->InitializeCollision(FBox(FVector(-50, -5, -50), FVector(50, 5, 50)));
	Preview->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Preview->RegisterComponent();
	UBodySetup* Previous = Preview->GetBodySetup();
	const auto NoPhysicsResult = MakeShared<FCookResult>();
	Preview->CookCollision(Shapes, [NoPhysicsResult](bool bSucceeded)
	{
		NoPhysicsResult->bSucceeded = bSucceeded;
		NoPhysicsResult->bComplete = true;
	});
	TestTrue(TEXT("No-physics fixture still finishes Cooking"), WaitForCook(NoPhysicsResult));
	AddExpectedError(TEXT("cannot apply Simple collision without a registered physics scene"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Missing physics scene cannot publish a collision-enabled body"), Preview->ApplyCollision());
	TestTrue(TEXT("Missing physics scene keeps the previous setup"), Preview->GetBodySetup() == Previous);
	Preview->DiscardCollision();
	NoPhysics->DestroyWorld(false);
	return true;
}
#endif
