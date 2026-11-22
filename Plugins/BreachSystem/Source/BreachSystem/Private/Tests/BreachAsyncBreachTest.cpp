#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Data/BreachSystemSettings.h"
#include "Data/BreachSurfaceMaterial.h"
#include "BreachSyncBreachGuard.h"
#include "BreachAsyncBreachTestUtils.h"

// 테스트 전용: 비동기 브리칭이 동기 처리와 같은 결과를 내고, 반영 전 피격·수명 변경·동기 API와 순서가 어긋나지 않는지 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace BreachAsyncBreachTest
{
	// 앞면 중앙 부근을 쏘는 정육면체 벽. 누적 파손을 켜면 세 발이 한 창 안에 들어와 큰 구멍을 만든다.
	struct FFixture
	{
		UStaticMesh* Cube = nullptr;
		UWorld* World = nullptr;
		UBreachSurfaceMaterial* Material = nullptr;
		AActor* Wall = nullptr;
		UBreachableSurfaceComponent* Surface = nullptr;
		AActor* Weapon = nullptr;

		explicit FFixture(bool bAccumulate)
		{
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			World = UWorld::CreateWorld(EWorldType::Game, false);
			check(Cube);
			check(World);
			Material = NewObject<UBreachSurfaceMaterial>(World);
			Material->Accumulation.bEnabled = bAccumulate;
			Material->DebrisMeshes.Add(Cube);

			Wall = World->SpawnActor<AActor>();
			Surface = NewObject<UBreachableSurfaceComponent>(Wall);
			Wall->SetRootComponent(Surface);
			Surface->SetStaticMesh(Cube);
			Surface->SetSurfaceMaterial(Material);
			Surface->RegisterComponent();

			Weapon = World->SpawnActor<AActor>();
			UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
			Source->RegisterComponent();
			Source->BindCircle(5.f, 16);
			Source->BreachDamage = bAccumulate ? .4f : 0.f;
		}

		~FFixture()
		{
			World->DestroyWorld(false);
		}

		void Hit(double X, double Z) const
		{
			FPointDamageEvent Damage;
			Damage.HitInfo = FHitResult(Wall, Surface, FVector(X, -50, Z), -FVector::YAxisVector);
			Damage.ShotDirection = FVector::YAxisVector;
			Surface->HandleBreachDamage(Surface, Damage, Weapon, 1.f);
		}

		int32 Debris() const
		{
			int32 Count = 0;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				if (It->GetStaticMeshComponent()->GetStaticMesh() == Cube)
				{
					++Count;
				}
			}
			return Count;
		}
	};

	// 앞의 세 발은 누적 파손 하나로, 나머지는 총알 구멍으로 끝난다.
	const FVector2d Shots[] = {{-12, 0}, {0, 0}, {12, 0}, {35, 30}, {-35, -30}, {0, 35}};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachAsyncBreachMatchesSyncTest, "BreachSystem.Surface.AsyncBreach.MatchesSync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachAsyncBreachMatchesSyncTest::RunTest(const FString& Parameters)
{
	using namespace BreachAsyncBreachTest;
	TGuardValue<bool> Async(GetMutableDefault<UBreachSystemSettings>()->bAsyncBreach, true);

	FFixture Sync(true);
	for (const FVector2d& Shot : Shots)
	{
		const FBreachSyncBreachGuard SyncBreach;
		Sync.Hit(Shot.X, Shot.Y);
	}

	// 반영을 기다리지 않고 연사한다. 첫 발이 작업을 시작하고 나머지는 다음 작업 하나에 모인다.
	FFixture Queued(true);
	for (const FVector2d& Shot : Shots)
	{
		Queued.Hit(Shot.X, Shot.Y);
	}
	TestTrue(TEXT("Hits wait for their job"), FBreachAsyncBreachTestAccess::IsPending(Queued.Surface));
	TestFalse(TEXT("The wall shows no hole before its job is applied"), FBreachAsyncBreachTestAccess::IsPublished(Queued.Surface));
	TestEqual(TEXT("No debris before the job is applied"), Queued.Debris(), 0);
	if (!TestTrue(TEXT("Queued jobs finish"), FBreachAsyncBreachTestAccess::Wait(Queued.Surface)))
	{
		return false;
	}

	TestTrue(TEXT("The applied job publishes the proxy"), FBreachAsyncBreachTestAccess::IsPublished(Queued.Surface));
	TestTrue(TEXT("Remaining area matches the hits processed one by one"), FMath::IsNearlyEqual(FBreachAsyncBreachTestAccess::RemainingArea(Queued.Surface), FBreachAsyncBreachTestAccess::RemainingArea(Sync.Surface), 1.e-9));
	TestEqual(TEXT("Remaining outline matches"), FBreachAsyncBreachTestAccess::RemainingVertices(Queued.Surface), FBreachAsyncBreachTestAccess::RemainingVertices(Sync.Surface));
	TestEqual(TEXT("Wall mesh matches"), FBreachAsyncBreachTestAccess::ProxyTriangles(Queued.Surface), FBreachAsyncBreachTestAccess::ProxyTriangles(Sync.Surface));
	TestEqual(TEXT("Accumulated records match"), Queued.Surface->GetAccumulatedHitCount(), Sync.Surface->GetAccumulatedHitCount());
	TestEqual(TEXT("Debris follows every cut hit, as in sync"), Queued.Debris(), Sync.Debris());
	TestTrue(TEXT("The sequence cut the wall"), FBreachAsyncBreachTestAccess::RemainingArea(Sync.Surface) < 100 * 100 - 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachAsyncBreachLifetimeTest, "BreachSystem.Surface.AsyncBreach.Lifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachAsyncBreachLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace BreachAsyncBreachTest;
	TGuardValue<bool> Async(GetMutableDefault<UBreachSystemSettings>()->bAsyncBreach, true);

	// 실행 중에 되돌린 표면에는 늦게 끝난 결과가 반영되지 않는다.
	{
		FFixture F(false);
		F.Hit(0, 0);
		TestTrue(TEXT("Hit waits for its job"), FBreachAsyncBreachTestAccess::IsPending(F.Surface));
		FBreachAsyncBreachTestAccess::Reset(F.Surface);
		TestTrue(TEXT("Late completion is consumed"), FBreachAsyncBreachTestAccess::Wait(F.Surface));
		TestFalse(TEXT("A reset surface keeps no proxy"), FBreachAsyncBreachTestAccess::HasProxy(F.Surface));
		TestEqual(TEXT("A discarded job spawns no debris"), F.Debris(), 0);

		F.Hit(0, 0);
		TestTrue(TEXT("A later hit finishes"), FBreachAsyncBreachTestAccess::Wait(F.Surface));
		TestTrue(TEXT("A later hit opens the wall"), FBreachAsyncBreachTestAccess::IsPublished(F.Surface) && !FBreachAsyncBreachTestAccess::Solid(F.Surface, FVector2d::ZeroVector));
		TestTrue(TEXT("Its debris arrives with the hole"), F.Debris() > 0);
	}

	// 실행 중에 파괴해도 계산이 끝날 때까지 기다리고, 늦게 온 완료 알림은 무시된다.
	{
		FFixture F(false);
		F.Hit(0, 0);
		F.Surface->DestroyComponent();
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestEqual(TEXT("A destroyed surface applies nothing"), F.Debris(), 0);
	}

	// 동기 절단은 먼저 들어온 피격을 반영한 뒤 자른다.
	{
		FFixture F(false);
		F.Hit(0, 0);
		UE::Geometry::FPolygon2d Box;
		for (const FVector2d Point : {FVector2d(25, -5), FVector2d(35, -5), FVector2d(35, 5), FVector2d(25, 5)})
		{
			Box.AppendVertex(Point);
		}
		TestTrue(TEXT("Sync cut succeeds while a hit waits"), F.Surface->ApplyCutterPolygon(Box));
		TestFalse(TEXT("The waiting hit is applied first"), FBreachAsyncBreachTestAccess::IsPending(F.Surface));
		TestFalse(TEXT("The earlier hole is there"), FBreachAsyncBreachTestAccess::Solid(F.Surface, FVector2d::ZeroVector));
		TestFalse(TEXT("The sync cut is there"), FBreachAsyncBreachTestAccess::Solid(F.Surface, FVector2d(30, 0)));
	}
	return !HasAnyErrors();
}
#endif
