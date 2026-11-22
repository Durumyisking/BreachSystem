#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Shape/BreachContour.h"

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;
	FPolygon2d MakePolygon(std::initializer_list<FVector2d> Points)
	{
		FPolygon2d Result;
		for (const FVector2d& Point : Points)
		{
			Result.AppendVertex(Point);
		}
		return Result;
	}

	// 테스트 전용: 실제 절단 실패 로그로 동일 커터의 중복 실행을 검출한다.
	class FRejectedCutCounter : public FOutputDevice
	{
	public:
		int32 Count = 0;

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == TEXT("LogBreachableSurface") && FCString::Strstr(Message, TEXT("kept its previous surface:")))
			{
				++Count;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachComplexCutTest, "BreachSystem.Surface.ComplexCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachComplexCutTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube fixture exists"), Cube))
	{
		World->DestroyWorld(false);
		return false;
	}
	Surface->SetStaticMesh(Cube);
	Surface->RegisterComponent();
	auto Reset = [Surface]()
	{
		struct
		{
			bool ReturnValue = false;
		} Params;
		Surface->ProcessEvent(Surface->FindFunctionChecked(TEXT("ResetSurface")), &Params);
	};
	auto Hits = [World](double X, double Z)
	{
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, FVector(X,-100,Z), FVector(X,100,Z), ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(BreachSurfaceTest), true));
	};
	const FPolygon2d Bowtie = MakePolygon({{-20,-20}, {20,20}, {-20,20}, {20,-20}});
	TestFalse(TEXT("Invalid first cut rejected"), Surface->ApplyCutterPolygon(Bowtie));
	TestTrue(TEXT("Invalid first cut preserves original visibility"), Surface->IsVisible());
	TArray<UDynamicMeshComponent*> Proxies;
	Wall->GetComponents(Proxies);
	TestEqual(TEXT("Invalid first cut creates no proxy"), Proxies.Num(), 0);
	TestTrue(TEXT("Invalid first cut leaves collision"), Hits(0,0));
	const TArray<FPolygon2d> Fixtures = {
		MakePolygon({{-30,-30}, {30,-30}, {30,30}, {10,30}, {10,-10}, {-10,-10}, {-10,30}, {-30,30}}),
		MakePolygon({{-20,-20}, {20,-20}, {20,-2}, {45,-2}, {45,2}, {20,2}, {20,20}, {-20,20}}),
		MakePolygon({{-60,-15}, {15,-15}, {15,15}, {-60,15}}),
		MakePolygon({{-60,-5}, {60,-5}, {60,5}, {-60,5}})
	};
	for (const FPolygon2d& Cutter : Fixtures)
	{
		Reset();
		const bool bCut = Surface->ApplyCutterPolygon(Cutter);
		TestTrue(TEXT("Manual complex cutter succeeds"), bCut);
		if (!bCut)
		{
			continue;
		}
		Wall->GetComponents(Proxies);
		if (!TestEqual(TEXT("One proxy per surface"), Proxies.Num(), 1))
		{
			continue;
		}
		const FDynamicMesh3* Mesh = Proxies[0]->GetMesh();
		TestTrue(TEXT("Complex cut remains a closed solid"), Mesh->IsClosed());
		for (int32 X = -45; X <= 45; X += 10)
		{
			for (int32 Z = -45; Z <= 45; Z += 10)
			{
				const FVector2d Point(X,Z);
				if (Cutter.DistanceSquared(Point) < 0.25)
				{
					continue;
				}
				TestEqual(TEXT("Trace matches manually specified cutter"), Hits(X,Z), !Cutter.Contains(Point));
			}
		}
		const int32 BeforeTriangles = Mesh->TriangleCount();
		const int32 BeforeVertices = Mesh->VertexCount();
		TestFalse(TEXT("Invalid later cut rejected"), Surface->ApplyCutterPolygon(Bowtie));
		TestEqual(TEXT("Failure preserves mesh triangles"), Proxies[0]->GetMesh()->TriangleCount(), BeforeTriangles);
		TestEqual(TEXT("Failure preserves mesh vertices"), Proxies[0]->GetMesh()->VertexCount(), BeforeVertices);
		TestTrue(TEXT("Failure preserves untouched collision"), Hits(40,40));
		TestTrue(TEXT("Repeated valid cut succeeds after failure"), Surface->ApplyCutterPolygon(Cutter));
		TestEqual(TEXT("Repeat retains polygon state"), Proxies[0]->GetMesh()->TriangleCount(), BeforeTriangles);
	}
	Reset();
	FPolygon2d Teeth;
	Teeth.AppendVertex(FVector2d(-30,-20));
	Teeth.AppendVertex(FVector2d(30,-20));
	for (int32 X = 30; X >= -30; --X)
	{
		Teeth.AppendVertex(FVector2d(X, (X % 2 == 0) ? 20 : 16));
	}
	TestTrue(TEXT("Many short teeth cut successfully"), Surface->ApplyCutterPolygon(Teeth));
	TestFalse(TEXT("Sawtooth interior is open"), Hits(0,0));
	FBreachContourOptions Options;
	Options.bEnabled = true;
	Options.MacroDepthRatio = 0.16f;
	Options.MediumDepthRatio = 0.04f;
	Options.MicroDepthRatio = 0.01f;
	for (int32 Seed = 1; Seed <= 8; ++Seed)
	{
		Reset();
		FPolygon2d Cutter;
		FString Error;
		Options.Seed = Seed;
		const bool bGenerated = BreachContour::Generate(FPolygon2d::MakeCircle(25,32), Options, Cutter, Error);
		TestTrue(TEXT("Generated cutter available"), bGenerated);
		if (bGenerated)
		{
			TestTrue(TEXT("Generated contour cuts actual wall"), Surface->ApplyCutterPolygon(Cutter));
		}
	}
	Reset();
	TestTrue(TEXT("Whole-wall removal is a success"), Surface->ApplyCutterPolygon(MakePolygon({{-100,-100}, {100,-100}, {100,100}, {-100,100}})));
	TestFalse(TEXT("Whole-wall removal clears collision"), Hits(0,0));
	Reset();
	TestTrue(TEXT("Reset restores collision"), Hits(0,0));

	// 커터가 벽 경계의 변 중간이나 기존 구멍 구석에 정확히 닿거나, 계산 오차만큼 비껴 닿아도 절단은 완료된다.
	// 정확히 닿은 점에서는 옆면 정점이 따로 생겨 구조상 닫힌 메시가 아니므로, 닫힘은 비껴 닿은 경우만 확인한다.
	const FPolygon2d FirstHole = MakePolygon({{-10,-10}, {10,-10}, {10,10}, {-10,10}});
	const double ContactOffsets[] = {0.0, 1.e-7};
	for (const double Offset : ContactOffsets)
	{
		Reset();
		const FPolygon2d TouchesWall = MakePolygon({{20,-10}, {50 - Offset,0}, {20,10}});
		TestTrue(TEXT("Contact with the wall border is cut"), Surface->ApplyCutterPolygon(TouchesWall));
		TestFalse(TEXT("Wall-contact cut opens the wall"), Hits(30,0));
		const FPolygon2d TouchesHole = MakePolygon({{10 + Offset,10 + Offset}, {30,10 + Offset}, {30,30}, {10 + Offset,30}});
		TestTrue(TEXT("Existing hole for the contact cut succeeds"), Surface->ApplyCutterPolygon(FirstHole));
		TestTrue(TEXT("Contact with an existing hole corner is cut"), Surface->ApplyCutterPolygon(TouchesHole));
		TestFalse(TEXT("Existing hole remains open"), Hits(0,0));
		TestFalse(TEXT("Corner-contact cut opens the wall"), Hits(20,20));
		TestTrue(TEXT("Untouched wall stays solid beside the contact cuts"), Hits(-30,30));
		Wall->GetComponents(Proxies);
		if (TestEqual(TEXT("Contact cuts keep one proxy"), Proxies.Num(), 1))
		{
			// 정확히 닿은 경우는 삼각분할이 감싸는 횟수로 채운다. 그 판정이 남은 벽 면적을 그대로 덮는지 앞면 넓이로 확인한다.
			const FDynamicMesh3& Mesh = *Proxies[0]->GetMesh();
			double FrontArea = 0;
			for (int32 ID : Mesh.TriangleIndicesItr())
			{
				const FIndex3i Triangle = Mesh.GetTriangle(ID);
				const FVector3d A = Mesh.GetVertex(Triangle.A), B = Mesh.GetVertex(Triangle.B), C = Mesh.GetVertex(Triangle.C);
				if (A.Y < 0 && B.Y < 0 && C.Y < 0)
				{
					FrontArea += FMath::Abs((B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X)) * 0.5;
				}
			}
			TestTrue(FString::Printf(TEXT("Contact cuts keep the exact remaining front area (%.9f)"), FrontArea), FMath::IsNearlyEqual(FrontArea, 8900.0, 1.e-3));
			if (Offset > 0)
			{
				TestTrue(TEXT("Near-contact cuts keep a closed solid"), Mesh.IsClosed());
			}
		}
	}

	// 기본 커터가 기존 구멍 구석에 닿아도 첫 시도에 절단을 완료한다.
	Reset();
	TestTrue(TEXT("Direct-cut fixture has an existing hole"), Surface->ApplyCutterPolygon(FirstHole));
	AActor* Weapon = World->SpawnActor<AActor>();
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Weapon);
	Source->RegisterComponent();
	Source->BindBox(FVector2D(20, 20));
	Proxies.Reset();
	Wall->GetComponents(Proxies);
	if (TestEqual(TEXT("Direct-cut fixture keeps its existing proxy"), Proxies.Num(), 1))
	{
		FHitResult Hit;
		Hit.Component = Proxies[0];
		Hit.ImpactPoint = FVector(20, -50, 20);
		const FPointDamageEvent Damage(1, Hit, FVector(0, 1, 0), nullptr);
		FRejectedCutCounter RejectedCuts;
		GLog->FlushThreadedLogs();
		GLog->AddOutputDevice(&RejectedCuts);
		Surface->HandleBreachDamage(Proxies[0], Damage, Weapon, 1);
		GLog->FlushThreadedLogs();
		GLog->RemoveOutputDevice(&RejectedCuts);
		TestEqual(TEXT("Corner-contact direct cut is never rejected"), RejectedCuts.Count, 0);
		TestFalse(TEXT("Existing hole remains open after the direct cut"), Hits(0, 0));
		TestFalse(TEXT("Corner-contact direct cut opens the wall"), Hits(20, 20));
	}
	World->DestroyWorld(false);
	return !HasAnyErrors();
}
#endif
