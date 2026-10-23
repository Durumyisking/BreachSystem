#include "Components/BreachableSurfaceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Shape/BreachContour.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
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
		return World->LineTraceSingleByChannel(Hit, FVector(X,-100,Z), FVector(X,100,Z), ECC_Visibility);
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
	Options.MacroDepth = 8;
	Options.MacroWidth = 20;
	Options.MediumDepth = 2;
	Options.MicroDepth = 0.5f;
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

	// Each cutter is simple, but point contact produces a non-manifold remaining boundary.
	const FPolygon2d TouchesWall = MakePolygon({{20,-10}, {50,0}, {20,10}});
	FString Error;
	TestTrue(TEXT("Late-failure fixture passes input validation"), BreachContour::Validate(TouchesWall, Error));
	TestFalse(TEXT("Point contact with wall is rejected after clipping"), Surface->ApplyCutterPolygon(TouchesWall));
	Wall->GetComponents(Proxies);
	TestEqual(TEXT("Late failure removes newly initialized proxy"), Proxies.Num(), 0);
	TestTrue(TEXT("Late first failure restores static visibility"), Surface->IsVisible());
	TestTrue(TEXT("Late first failure restores collision"), Hits(30,0));

	const FPolygon2d FirstHole = MakePolygon({{-10,-10}, {10,-10}, {10,10}, {-10,10}});
	const FPolygon2d TouchesHole = MakePolygon({{10,10}, {30,10}, {30,30}, {10,30}});
	TestTrue(TEXT("Prepare existing hole for late failure"), Surface->ApplyCutterPolygon(FirstHole));
	Wall->GetComponents(Proxies);
	if (TestEqual(TEXT("Existing-hole fixture has one proxy"), Proxies.Num(), 1))
	{
		const FDynamicMesh3 Before = *Proxies[0]->GetMesh();
		TestTrue(TEXT("Touching second cutter is itself simple"), BreachContour::Validate(TouchesHole, Error));
		TestFalse(TEXT("Point contact with existing hole is rejected after clipping"), Surface->ApplyCutterPolygon(TouchesHole));
		const FDynamicMesh3& After = *Proxies[0]->GetMesh();
		TestEqual(TEXT("Late failure preserves triangle count"), After.TriangleCount(), Before.TriangleCount());
		TestEqual(TEXT("Late failure preserves vertex count"), After.VertexCount(), Before.VertexCount());
		for (int32 ID : Before.VertexIndicesItr())
		{
			TestTrue(TEXT("Late failure preserves every vertex"), After.IsVertex(ID) && After.GetVertex(ID) == Before.GetVertex(ID));
		}
		for (int32 ID : Before.TriangleIndicesItr())
		{
			TestTrue(TEXT("Late failure preserves every triangle"), After.IsTriangle(ID) && After.GetTriangle(ID) == Before.GetTriangle(ID));
		}
		TestFalse(TEXT("Existing hole remains open after late failure"), Hits(0,0));
		TestTrue(TEXT("Rejected second hole remains solid"), Hits(20,20));
		TestTrue(TEXT("Valid repeat after late failure"), Surface->ApplyCutterPolygon(FirstHole));
		TestEqual(TEXT("Late failure did not commit candidate polygon state"), Proxies[0]->GetMesh()->TriangleCount(), Before.TriangleCount());
	}
	World->DestroyWorld(false);
	return !HasAnyErrors();
}
#endif
