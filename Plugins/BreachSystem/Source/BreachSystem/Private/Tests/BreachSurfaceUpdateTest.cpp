#include "Misc/AutomationTest.h"

// 테스트 전용: 무변화 절단의 생략, 구멍 캐시 복구, Complex 비동기 옵션, 음수 스케일의 Simple Cooking을 실제 Game 월드에서 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSimpleProxy.h"
#include "Components/BreachSurfaceProxy.h"
#include "Settings/BreachSystemSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "BreachSimpleAsyncTestUtils.h"

namespace
{
	UE::Geometry::FPolygon2d UpdateBox(double MinX, double MinZ, double MaxX, double MaxZ)
	{
		UE::Geometry::FPolygon2d Polygon;
		for (const FVector2d Point : {FVector2d(MinX, MinZ), FVector2d(MaxX, MinZ), FVector2d(MaxX, MaxZ), FVector2d(MinX, MaxZ)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	struct FUpdateFixture
	{
		UWorld* World = nullptr;
		AActor* Wall = nullptr;
		UBreachableSurfaceComponent* Surface = nullptr;

		explicit FUpdateFixture(const FVector& Scale = FVector::OneVector)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			check(World);
			Wall = World->SpawnActor<AActor>();
			check(Wall);
			Surface = NewObject<UBreachableSurfaceComponent>(Wall);
			Wall->SetRootComponent(Surface);
			Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Surface->SetWorldScale3D(Scale);
			Surface->RegisterComponent();
		}

		~FUpdateFixture()
		{
			check(World);
			World->DestroyWorld(false);
		}

		// 총알처럼 Complex 충돌만 보는 트레이스가 표면 로컬 (X, Z)에서 벽에 막히는지 돌려준다.
		bool BlocksBullet(double X, double Z) const
		{
			check(World);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(BreachSurfaceUpdate), true);
			FHitResult Hit;
			return World->LineTraceSingleByChannel(Hit, FVector(X, -200, Z), FVector(X, 200, Z), ECC_Visibility, Params);
		}

		// 이동용 Simple 충돌의 Convex가 표면 로컬 (X, Z)를 덮는지 돌려준다.
		bool BlocksMovement(double X, double Z) const
		{
			check(Wall);
			UBreachSimpleProxy* Proxy = Wall->FindComponentByClass<UBreachSimpleProxy>();
			check(Proxy);
			UBodySetup* Body = Proxy->GetBodySetup();
			check(Body);
			for (const FKBoxElem& Box : Body->AggGeom.BoxElems)
			{
				if (FMath::Abs(X - Box.Center.X) <= Box.X * 0.5 && FMath::Abs(Z - Box.Center.Z) <= Box.Z * 0.5)
				{
					return true;
				}
			}
			for (const FKConvexElem& Convex : Body->AggGeom.ConvexElems)
			{
				UE::Geometry::FPolygon2d Polygon;
				for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
				{
					const FVector Point = Convex.VertexData[Index];
					Polygon.AppendVertex(FVector2d(Point.X, Point.Z));
				}
				if (Polygon.Contains(FVector2d(X, Z)))
				{
					return true;
				}
			}
			return false;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachSurfaceUpdateTest, "BreachSystem.Surface.UpdatePolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachSurfaceUpdateTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("NoChange"), TEXT("CacheRecovery"), TEXT("AsyncComplex"), TEXT("MirroredSimple")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FBreachSurfaceUpdateTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Update policy settings"), Settings))
	{
		return false;
	}
	TGuardValue<double> Threshold(Settings->ComplexHoleSizeThreshold, 500.0);
	TGuardValue<bool> AsyncComplex(Settings->bAsyncComplexCollision, false);

	if (Parameters == TEXT("NoChange"))
	{
		// 회귀: 면적이 줄지 않는 절단은 메시·Complex·구멍 변경 번호를 건드리지 않는다.
		FUpdateFixture F;
		TestTrue(TEXT("A cutter outside a pristine wall is accepted"), F.Surface->ApplyCutterPolygon(UpdateBox(200, 200, 210, 210)));
		TestNotNull(TEXT("A first cut still switches the wall to its proxy even when it removes nothing"), F.Wall->FindComponentByClass<UBreachSurfaceProxy>());
		TestTrue(TEXT("A wall-sized first mesh blocks bullets"), F.BlocksBullet(0, 0));
		TestEqual(TEXT("A cut that removes nothing is not recorded as an opening"), FBreachSimpleAsyncTestAccess::InputRevision(F.Surface), static_cast<uint64>(1));

		TestTrue(TEXT("Real cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(-30, -30, 30, 30)));
		UBreachSurfaceProxy* Complex = F.Wall->FindComponentByClass<UBreachSurfaceProxy>();
		if (!TestNotNull(TEXT("Real cut creates the proxy"), Complex))
		{
			return false;
		}
		TestTrue(TEXT("Real cut finishes"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
		const uint64 Revision = FBreachSimpleAsyncTestAccess::InputRevision(F.Surface);
		int32 MeshUpdates = 0;
		const FDelegateHandle Handle = Complex->OnMeshChanged.AddLambda([&MeshUpdates]()
		{
			++MeshUpdates;
		});

		TestTrue(TEXT("Repeating the same cut is accepted"), F.Surface->ApplyCutterPolygon(UpdateBox(-30, -30, 30, 30)));
		TestTrue(TEXT("A cutter inside the existing hole is accepted"), F.Surface->ApplyCutterPolygon(UpdateBox(-10, -10, 10, 10)));
		TestTrue(TEXT("A cutter outside the wall is accepted"), F.Surface->ApplyCutterPolygon(UpdateBox(200, 200, 210, 210)));
		TestEqual(TEXT("No-change cuts do not rebuild the display mesh"), MeshUpdates, 0);
		TestEqual(TEXT("No-change cuts do not bump the hole revision"), FBreachSimpleAsyncTestAccess::InputRevision(F.Surface), Revision);
		TestFalse(TEXT("No-change cuts do not queue Simple collision work"), FBreachSimpleAsyncTestAccess::IsPending(F.Surface));

		TestTrue(TEXT("A cut that removes area still updates"), F.Surface->ApplyCutterPolygon(UpdateBox(-30, -30, 40, 30)));
		TestTrue(TEXT("A changing cut rebuilds the display mesh"), MeshUpdates > 0);
		TestTrue(TEXT("A changing cut bumps the hole revision"), FBreachSimpleAsyncTestAccess::InputRevision(F.Surface) > Revision);
		Complex->OnMeshChanged.Remove(Handle);
		TestTrue(TEXT("Changing cut finishes"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
	}
	else if (Parameters == TEXT("CacheRecovery"))
	{
		// 회귀: 구멍 추적에 실패한 뒤에도 다음 절단에서 캐시를 복구해 이동 충돌이 열린다.
		FUpdateFixture F;
		TestTrue(TEXT("Small cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(-45, -45, -43, -43)));
		TestTrue(TEXT("Small cut finishes"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
		FBreachSimpleAsyncTestAccess::BreakHoleCache(F.Surface);
		TestFalse(TEXT("Fixture lost its hole cache"), FBreachSimpleAsyncTestAccess::HasHoleCache(F.Surface));

		AddExpectedError(TEXT("rebuilt its movement openings from the remaining regions"), EAutomationExpectedErrorFlags::Contains, 1);
		TestTrue(TEXT("Broad cut after the failure succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(-30, -30, 30, 30)));
		TestTrue(TEXT("The next cut restores the hole cache"), FBreachSimpleAsyncTestAccess::HasHoleCache(F.Surface));
		TestTrue(TEXT("Recovered build finishes"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
		TestEqual(TEXT("The recovered revision is published"), FBreachSimpleAsyncTestAccess::PublishedRevision(F.Surface), FBreachSimpleAsyncTestAccess::InputRevision(F.Surface));
		TestFalse(TEXT("Movement can pass the broad opening after recovery"), F.BlocksMovement(0, 0));
		TestTrue(TEXT("Movement is still blocked by the remaining wall"), F.BlocksMovement(-40, 40));

		TestTrue(TEXT("Later cut is tracked without another rebuild"), F.Surface->ApplyCutterPolygon(UpdateBox(-30, -30, 45, 30)));
		TestTrue(TEXT("Later build finishes"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
		TestFalse(TEXT("Movement can pass the widened opening"), F.BlocksMovement(38, 0));
	}
	else if (Parameters == TEXT("AsyncComplex"))
	{
		// 기본값은 피격한 프레임에 Complex까지 반영한다. 옵션을 켜면 완료될 때까지 이전 충돌이 남는다.
		{
			FUpdateFixture F;
			TestTrue(TEXT("Default first cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(-40, -10, -20, 10)));
			TestFalse(TEXT("Default first cut opens bullets immediately"), F.BlocksBullet(-30, 0));
			TestTrue(TEXT("Default second cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(20, -10, 40, 10)));
			TestFalse(TEXT("Default second cut opens bullets in the same frame"), F.BlocksBullet(30, 0));
			TestTrue(TEXT("Default wall still blocks bullets elsewhere"), F.BlocksBullet(0, 40));
			TestTrue(TEXT("Default cuts finish"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
		}

		Settings->bAsyncComplexCollision = true;
		TestTrue(TEXT("Option reaches its getter"), UBreachSystemSettings::IsComplexCollisionAsync());
		FUpdateFixture F;
		TestTrue(TEXT("Async first cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(-40, -10, -20, 10)));
		TestFalse(TEXT("The first switch to the proxy is always cooked immediately"), F.BlocksBullet(-30, 0));
		TestTrue(TEXT("The first switch keeps the rest of the wall solid"), F.BlocksBullet(30, 0));

		TestTrue(TEXT("Async second cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(20, -10, 40, 10)));
		TestTrue(TEXT("The previous Complex collision stays until the async cook completes"), F.BlocksBullet(30, 0));
		TestFalse(TEXT("The earlier opening stays open during the async cook"), F.BlocksBullet(-30, 0));
		const double Deadline = FPlatformTime::Seconds() + 5;
		while (F.BlocksBullet(30, 0) && FPlatformTime::Seconds() < Deadline)
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		TestFalse(TEXT("The async cook opens bullets once it completes"), F.BlocksBullet(30, 0));
		TestFalse(TEXT("The earlier opening survives the swap"), F.BlocksBullet(-30, 0));
		TestTrue(TEXT("The remaining wall still blocks bullets after the swap"), F.BlocksBullet(0, 40));
		TestTrue(TEXT("Async cuts finish"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
	}
	else if (Parameters == TEXT("MirroredSimple"))
	{
		// 음수 스케일 벽은 반전본 한 벌만 굽고, 그 바디가 실제로 만들어져야 한다.
		FUpdateFixture F(FVector(-1, 1, 1));
		TestTrue(TEXT("Mirrored broad cut succeeds"), F.Surface->ApplyCutterPolygon(UpdateBox(-30, -30, 30, 30)));
		TestTrue(TEXT("Mirrored build finishes"), FBreachSimpleAsyncTestAccess::Wait(F.Surface));
		UBreachSimpleProxy* Simple = F.Wall->FindComponentByClass<UBreachSimpleProxy>();
		UBreachSurfaceProxy* Complex = F.Wall->FindComponentByClass<UBreachSurfaceProxy>();
		if (!TestNotNull(TEXT("Mirrored Simple proxy"), Simple) || !TestNotNull(TEXT("Mirrored Complex proxy"), Complex))
		{
			return false;
		}
		UBodySetup* Body = Simple->GetBodySetup();
		check(Body);
		FCookBodySetupInfo Info;
		Body->GetCookInfo(Info, static_cast<EPhysXMeshCookFlags>(0));
		TestTrue(TEXT("Mirrored wall cooks the mirrored convex set"), Info.bCookMirroredConvex);
		TestFalse(TEXT("Mirrored wall does not cook an unused non-mirrored copy"), Info.bCookNonMirroredConvex);
		TestEqual(TEXT("Mirrored wall published its cooked collision"), FBreachSimpleAsyncTestAccess::CookCount(Simple), 1u);
		TestTrue(TEXT("Mirrored Simple body exists"), Simple->BodyInstance.IsValidBodyInstance());
		TestTrue(TEXT("Mirrored Complex body exists"), Complex->BodyInstance.IsValidBodyInstance());
		TestFalse(TEXT("Mirrored wall opens movement through the hole"), F.BlocksMovement(0, 0));
		TestFalse(TEXT("Mirrored wall opens bullets through the hole"), F.BlocksBullet(0, 0));
		TestTrue(TEXT("Mirrored wall still blocks bullets beside the hole"), F.BlocksBullet(40, 40));
	}
	return !HasAnyErrors();
}
#endif
