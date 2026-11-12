#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Settings/BreachSystemSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "BreachSimpleAsyncTestUtils.h"

// 테스트 전용: 실제 Game 월드의 비동기 반영, 연사 병합과 수명 전환을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;
	FPolygon2d AsyncBox(double Right)
	{
		FPolygon2d Polygon;
		for (const FVector2d Point : {FVector2d(-30, -30), FVector2d(Right, -30), FVector2d(Right, 30), FVector2d(-30, 30)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}
	double AsyncCollisionArea(const UBreachSurfaceProxy* Proxy)
	{
		check(Proxy);
		double Area = 0;
		for (const FKConvexElem& Convex : Proxy->GetSimpleCollisionShapes().ConvexElems)
		{
			FPolygon2d Polygon;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector Point = Convex.VertexData[Index];
				Polygon.AppendVertex({Point.X, Point.Z});
			}
			Area += FMath::Abs(Polygon.SignedArea());
		}
		return Area;
	}
	bool AsyncBlocksPoint(const UBreachSurfaceProxy* Proxy, const FVector2d Point)
	{
		check(Proxy);
		for (const FKConvexElem& Convex : Proxy->GetSimpleCollisionShapes().ConvexElems)
		{
			FPolygon2d Polygon;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector P = Convex.VertexData[Index];
				Polygon.AppendVertex({P.X, P.Z});
			}
			if (Polygon.Contains(Point))
			{
				return true;
			}
		}
		return false;
	}

	class FWaitForSimple final : public IAutomationLatentCommand
	{
		UWorld* World;
		TWeakObjectPtr<UBreachableSurfaceComponent> Surface;
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		int32 Phase = 0;
	public:
		FWaitForSimple(UWorld* InWorld, UBreachableSurfaceComponent* InSurface, FAutomationTestBase* InTest) : World(InWorld), Surface(InSurface), Test(InTest) {}
		virtual bool Update() override
		{
			check(World);
			check(Test);
			UBreachableSurfaceComponent* Current = Surface.Get();
			if (!Current)
			{
				Test->AddError(TEXT("Surface disappeared before async verification."));
				World->DestroyWorld(false);
				return true;
			}
			AActor* Owner = Current->GetOwner();
			check(Owner);
			UBreachSurfaceProxy* Proxy = Owner->FindComponentByClass<UBreachSurfaceProxy>();
			if (!Proxy || FPlatformTime::Seconds() - Started > 5)
			{
				Test->AddError(TEXT("Simple collision did not finish within the test timeout."));
				World->DestroyWorld(false);
				return true;
			}
			if (Phase == 0)
			{
				if (AsyncBlocksPoint(Proxy, {35, 2.123}))
				{
					return false;
				}
				Test->AddInfo(FString::Printf(TEXT("Latest burst passage became visible in %.3f ms."), (FPlatformTime::Seconds() - Started) * 1000));
				Test->TestTrue(TEXT("Old and new passage remain open after the pending batch"), !AsyncBlocksPoint(Proxy, {0, 2.123}));
				Test->TestTrue(TEXT("Work is queued before reset"), Current->ApplyCutterPolygon(AsyncBox(45)));
				UFunction* Reset = Current->FindFunction(TEXT("ResetSurface"));
				check(Reset);
				struct { bool ReturnValue; } Result = {};
				Current->ProcessEvent(Reset, &Result);
				Test->TestTrue(TEXT("Reset succeeds with a worker in flight"), Result.ReturnValue);
				Test->TestTrue(TEXT("Retired work is held until completion instead of overlapping new work"), FBreachSimpleAsyncTestAccess::IsPending(Current));
				FPolygon2d Tiny;
				Tiny.AppendVertex({-2, -2}); Tiny.AppendVertex({2, -2}); Tiny.AppendVertex({2, 2}); Tiny.AppendVertex({-2, 2});
				Test->TestTrue(TEXT("New surface accepts a subthreshold cut"), Current->ApplyCutterPolygon(Tiny));
				Current->UnregisterComponent();
				Current->RegisterComponent();
				Test->TestTrue(TEXT("Reregistering the live surface queues its required collision build"), FBreachSimpleAsyncTestAccess::IsPending(Current));
				Started = FPlatformTime::Seconds();
				Phase = 1;
				return false;
			}
			if (FPlatformTime::Seconds() - Started < 0.2)
			{
				return false;
			}
			Test->TestTrue(TEXT("Old lifetime cannot replace the new surface collision"), FMath::IsNearlyEqual(AsyncCollisionArea(Proxy), 10000.0, 1.e-5));
			Test->TestTrue(TEXT("Queue work before component destruction"), Current->ApplyCutterPolygon(AsyncBox(45)));
			Current->DestroyComponent();
			World->DestroyWorld(false);
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleAsyncTest, "BreachSystem.Surface.SimpleAsync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachSimpleAsyncTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Settings exist"), Settings))
	{
		return false;
	}
	// 설정은 작업이 끝날 때까지 유지해야 하므로 프로젝트 기본값을 읽어 검증한다.
	TestTrue(TEXT("Default threshold fits the fixture"), Settings->ComplexHoleSizeThreshold < 3000);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Game world exists"), World))
	{
		return false;
	}
	AActor* Wall = World->SpawnActor<AActor>();
	check(Wall);
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Surface->RegisterComponent();
	TestTrue(TEXT("First cut succeeds"), Surface->ApplyCutterPolygon(AsyncBox(30)));
	UBreachSurfaceProxy* Proxy = Wall->FindComponentByClass<UBreachSurfaceProxy>();
	if (!TestNotNull(TEXT("Proxy is ready before asynchronous completion"), Proxy))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("First frame keeps a complete blocking wall while the passage is calculated"), FMath::IsNearlyEqual(AsyncCollisionArea(Proxy), 10000.0, 1.e-5));
	for (int32 Index = 1; Index <= 10; ++Index)
	{
		TestTrue(TEXT("Burst cut succeeds while the worker is running"), Surface->ApplyCutterPolygon(AsyncBox(30 + Index)));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FWaitForSimple(World, Surface, this));
	return true;
}
#endif
