#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Materials/Material.h"
#include "Surface/BreachPattern.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 실제 표면의 후보 상태를 조회해 여러 영역의 원자적 절단과 누적 기록을 검증한다.
struct FBreachPatternSurfaceTestAccess
{
	static UMaterialInterface* ProxyMaterial(const UBreachableSurfaceComponent& Surface)
	{
		return Surface.DynamicMeshProxy ? Surface.DynamicMeshProxy->GetMaterial(0) : nullptr;
	}
	static bool Prepare(UBreachableSurfaceComponent& Surface)
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		return Surface.CreateDynamicMeshProxy(Mesh);
	}

	static bool Cut(UBreachableSurfaceComponent& Surface, const TArray<UE::Geometry::FGeneralPolygon2d>& Regions)
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		bool bChanged = false;
		return Surface.CutRegions(Regions, Mesh, bChanged) && bChanged;
	}

	static bool Solid(const UBreachableSurfaceComponent& Surface, const FVector2d& Point)
	{
		for (const UE::Geometry::FGeneralPolygon2d& Region : Surface.SurfaceState.RemainingRegions)
		{
			if (Region.Contains(Point))
			{
				return true;
			}
		}
		return false;
	}

	static FVector2D UV(const UBreachableSurfaceComponent& Surface, const FVector2d& Point)
	{
		const FPlanarUVMapping& Mapping = Surface.SurfaceUVs[0];
		return FVector2D(Mapping.Offset + Mapping.PerX * Point.X + Mapping.PerZ * Point.Y);
	}

	static bool ReportedRemoved(const UBreachableSurfaceComponent& Surface, const FVector2d& Point)
	{
		for (const UE::Geometry::FPolygon2d& Region : Surface.PendingDecorationRemoved)
		{
			if (Region.Contains(Point) || Region.DistanceSquared(Point) <= 1.e-9)
			{
				return true;
			}
		}
		return false;
	}

	static bool Accumulate(UBreachableSurfaceComponent& Surface, const UBreachSurfaceMaterial& Material)
	{
		FVector2d Point;
		if (!Surface.PrepareAccumulation(Surface.GetComponentTransform().TransformPosition(FVector(0, -50, 0)), 1, Material.Accumulation, Point))
		{
			return false;
		}
		UE::Geometry::FDynamicMesh3 Mesh;
		bool bChanged = false;
		return Surface.AccumulateHit(Point, 1, Material.AccumulationShape, Mesh, bChanged) && bChanged;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternDisplayRestoreTest, "BreachSystem.Pattern.DisplayRestore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternDisplayRestoreTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Fixture world"), World))
	{
		return false;
	}
	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	UMaterial* Original = NewObject<UMaterial>(World);
	Surface->SetMaterial(0, Original);
	UBreachSurfaceMaterial* Rules = NewObject<UBreachSurfaceMaterial>(World);
	Rules->DisplayMaterial = NewObject<UMaterial>(World);
	Surface->SetSurfaceMaterial(Rules);
	TestTrue(TEXT("Display rules override original slot"), Surface->GetMaterial(0) != Original);
	TestTrue(TEXT("Proxy prepares"), FBreachPatternSurfaceTestAccess::Prepare(*Surface));
	Surface->SetSurfaceMaterial(nullptr);
	TestEqual(TEXT("Clearing rules restores original source slot"), Surface->GetMaterial(0), static_cast<UMaterialInterface*>(Original));
	TestEqual(TEXT("Clearing rules restores proxy too"), FBreachPatternSurfaceTestAccess::ProxyMaterial(*Surface), static_cast<UMaterialInterface*>(Original));
	World->DestroyWorld(false);
	return true;
}

namespace
{
	UE::Geometry::FPolygon2d PatternSurfaceBox(double X0, double Y0, double X1, double Y1)
	{
		UE::Geometry::FPolygon2d Polygon;
		for (const FVector2d& Point : {FVector2d(X0, Y0), FVector2d(X1, Y0), FVector2d(X1, Y1), FVector2d(X0, Y1)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternCleanupTest, "BreachSystem.Pattern.DamageCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternCleanupTest::RunTest(const FString& Parameters)
{
	FBreachDamageAccumulator Accumulator;
	FString Error;
	TestTrue(TEXT("Configure accumulator"), Accumulator.Configure(FVector2d(40, 40), 10, Error));
	Accumulator.AddHit(FVector2d(0, 0), 1, Error);
	Accumulator.AddHit(FVector2d(8, 0), 1, Error);
	Accumulator.AddHit(FVector2d(30, 0), 1, Error);
	UE::Geometry::FGeneralPolygon2d Region(PatternSurfaceBox(-12, -12, 12, 12));
	Region.AddHole(PatternSurfaceBox(-3, -3, 3, 3), false, false);
	Accumulator.RemoveInside(Region);
	TestEqual(TEXT("Only removed material consumes damage; inner hole and outside records remain"), Accumulator.Num(), 2);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternSurfaceTest, "BreachSystem.Pattern.Surface", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternSurfaceTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Fixture world"), World))
	{
		return false;
	}
	AActor* Wall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
	Wall->SetRootComponent(Surface);
	Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Surface->RegisterComponent();
	TestTrue(TEXT("Initialize source UV and surface"), FBreachPatternSurfaceTestAccess::Prepare(*Surface));

	using namespace UE::Geometry;
	FGeneralPolygon2d Ring(PatternSurfaceBox(-20, -20, 20, 20));
	Ring.AddHole(PatternSurfaceBox(-4, -4, 4, 4), false, false);
	TArray<FGeneralPolygon2d> Cuts{Ring, FGeneralPolygon2d(PatternSurfaceBox(25, -8, 35, 8))};
	TestTrue(TEXT("Disjoint regions cut atomically"), FBreachPatternSurfaceTestAccess::Cut(*Surface, Cuts));
	TestFalse(TEXT("Ring removed"), FBreachPatternSurfaceTestAccess::Solid(*Surface, FVector2d(10, 0)));
	// 고립 영역 제거 규칙에 따라 고리 안쪽 섬도 떨어진다. 직접 연결된 내부 구멍 보존은 별도 기하 테스트가 검증한다.
	TestFalse(TEXT("Separate region removed"), FBreachPatternSurfaceTestAccess::Solid(*Surface, FVector2d(30, 0)));
	TestTrue(TEXT("Space between disjoint cuts stays solid"), FBreachPatternSurfaceTestAccess::Solid(*Surface, FVector2d(23, 0)));
	FPolygon2d Invalid;
	for (const FVector2d& Point : {FVector2d(-30, -5), FVector2d(-20, 5), FVector2d(-30, 5), FVector2d(-20, -5)})
	{
		Invalid.AppendVertex(Point);
	}
	AddExpectedError(TEXT("rejected cutters"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid batch rejected"), FBreachPatternSurfaceTestAccess::Cut(*Surface, {FGeneralPolygon2d(PatternSurfaceBox(-35, -10, -25, 10)), FGeneralPolygon2d(Invalid)}));
	TestTrue(TEXT("Valid member of rejected batch is not applied"), FBreachPatternSurfaceTestAccess::Solid(*Surface, FVector2d(-30, 0)));

	AActor* PatternWall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* PatternSurface = NewObject<UBreachableSurfaceComponent>(PatternWall);
	PatternWall->SetRootComponent(PatternSurface);
	PatternSurface->SetStaticMesh(Surface->GetStaticMesh());
	PatternSurface->RegisterComponent();
	TestTrue(TEXT("Initialize accumulation fixture"), FBreachPatternSurfaceTestAccess::Prepare(*PatternSurface));
	UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>(World);
	UBreachPattern* Pattern = NewObject<UBreachPattern>(Material);
	Material->Pattern = Pattern;
	Material->Accumulation.bEnabled = true;
	Material->Accumulation.BreachHP = 1;
	Material->AccumulationShape.Padding = 2;
	Material->AccumulationShape.Contour.bEnabled = false;
	Pattern->Mode = EBreachPatternMode::Regions;
	PatternSurface->SetSurfaceMaterial(Material);
	FBreachPatternRegion Away;
	const FPolygon2d AwayBlock = PatternSurfaceBox(25, 25, 35, 35);
	for (const FVector2d& Point : AwayBlock.GetVertices())
	{
		Away.Outer.Add(FBreachPatternSurfaceTestAccess::UV(*PatternSurface, Point));
	}
	Pattern->Regions.Add(Away);
	TestFalse(TEXT("Empty pattern selection does not count as a large breach"), FBreachPatternSurfaceTestAccess::Accumulate(*PatternSurface, *Material));
	TestEqual(TEXT("Empty selection preserves damage"), PatternSurface->GetAccumulatedHitCount(), 1);
	Pattern->Regions.Reset();
	AddExpectedError(TEXT("retained damage after its configured pattern failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid configured pattern never substitutes a generic large cut"), FBreachPatternSurfaceTestAccess::Accumulate(*PatternSurface, *Material));
	TestTrue(TEXT("Invalid pattern keeps material outside bullet scale"), FBreachPatternSurfaceTestAccess::Solid(*PatternSurface, FVector2d(0, 10)));
	TestEqual(TEXT("Invalid pattern preserves damage"), PatternSurface->GetAccumulatedHitCount(), 2);
	FBreachPatternRegion Region;
	const FPolygon2d Block = PatternSurfaceBox(-8, -12, 8, 12);
	for (const FVector2d& Point : Block.GetVertices())
	{
		Region.Outer.Add(FBreachPatternSurfaceTestAccess::UV(*PatternSurface, Point));
	}
	Pattern->Regions.Add(Region);
	PatternSurface->SetSurfaceMaterial(Material);
	TestTrue(TEXT("Accumulated cut applies configured pattern"), FBreachPatternSurfaceTestAccess::Accumulate(*PatternSurface, *Material));
	TestFalse(TEXT("Entire block removed outside base padding"), FBreachPatternSurfaceTestAccess::Solid(*PatternSurface, FVector2d(0, 10)));
	TestEqual(TEXT("Successful final region consumes hit"), PatternSurface->GetAccumulatedHitCount(), 0);
	AActor* HoleWall = World->SpawnActor<AActor>();
	UBreachableSurfaceComponent* HoleSurface = NewObject<UBreachableSurfaceComponent>(HoleWall);
	HoleWall->SetRootComponent(HoleSurface);
	HoleSurface->SetStaticMesh(Surface->GetStaticMesh());
	HoleSurface->RegisterComponent();
	TestTrue(TEXT("Initialize boundary-crossing hole fixture"), FBreachPatternSurfaceTestAccess::Prepare(*HoleSurface));
	FGeneralPolygon2d HoleCut(PatternSurfaceBox(-80, -80, 80, 80));
	HoleCut.AddHole(PatternSurfaceBox(-60, -60, 0, 60), false, false);
	TestTrue(TEXT("Hole-aware batch removes right half"), FBreachPatternSurfaceTestAccess::Cut(*HoleSurface, {HoleCut}));
	TestTrue(TEXT("Hole connected to original boundary remains solid"), FBreachPatternSurfaceTestAccess::Solid(*HoleSurface, FVector2d(-20, 0)));
	TestFalse(TEXT("Retained hole material is not reported as removed to decorations"), FBreachPatternSurfaceTestAccess::ReportedRemoved(*HoleSurface, FVector2d(-20, 0)));
	TestTrue(TEXT("Actually removed material is reported"), FBreachPatternSurfaceTestAccess::ReportedRemoved(*HoleSurface, FVector2d(20, 0)));
	World->DestroyWorld(false);
	return !HasAnyErrors();
}
#endif
