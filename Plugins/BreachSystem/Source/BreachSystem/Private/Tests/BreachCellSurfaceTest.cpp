#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Settings/BreachSystemSettings.h"
#include "Surface/BreachCellGrid.h"
#include "Surface/BreachSurfaceMaterial.h"

// 테스트 전용: Editor 자동화 테스트 빌드에서만 컴파일한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
namespace
{
	struct FCellSurfaceFixture
	{
		UWorld* World = nullptr;
		AActor* Wall = nullptr;
		AActor* Weapon = nullptr;
		UBreachableSurfaceComponent* Surface = nullptr;
		UBreachSourceComponent* Source = nullptr;
		UBreachSurfaceMaterial* Material = nullptr;

		~FCellSurfaceFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool Create(FAutomationTestBase& Test, const FVector& Scale)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Cell test world"), World)) return false;
			Wall = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Cell test wall"), Wall)) return false;
			Surface = NewObject<UBreachableSurfaceComponent>(Wall);
			if (!Test.TestNotNull(TEXT("Cell test surface"), Surface)) return false;
			Wall->SetRootComponent(Surface);
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Test.TestNotNull(TEXT("Cell test cube"), Cube)) return false;
			Surface->SetStaticMesh(Cube);
			Surface->SetWorldTransform(FTransform(FRotator(17, 35, -11), FVector(100, 200, 300), Scale));
			Surface->RegisterComponent();
			Material = NewObject<UBreachSurfaceMaterial>(World);
			if (!Test.TestNotNull(TEXT("Cell test material"), Material)) return false;
			Material->Accumulation.bEnabled = true;
			Material->Accumulation.WindowSize = FVector2D(40, 40);
			// All direct holes lie well inside the final footprint, so only the accumulated contour remains.
			Material->AccumulationShape.Padding = 20;
			Material->AccumulationShape.Contour.bEnabled = true;
			Material->AccumulationShape.Contour.Seed = 37;
			Material->AccumulationShape.Contour.VertexCount = 32;
			Material->AccumulationShape.Contour.MacroDepthRatio = 0.05f;
			Material->AccumulationShape.Contour.MediumDepthRatio = 0.01f;
			Surface->SetSurfaceMaterial(Material);
			Weapon = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Cell test weapon"), Weapon)) return false;
			Source = NewObject<UBreachSourceComponent>(Weapon);
			if (!Test.TestNotNull(TEXT("Cell test source"), Source)) return false;
			Source->RegisterComponent();
			Source->BindCircle(0.2f, 16);
			Source->BreachDamage = 0.17f;
			return true;
		}

		bool Trace(const FVector2d& PhysicalXZ, FHitResult& Hit) const
		{
			check(World);
			check(Surface);
			const FTransform Transform = Surface->GetComponentTransform();
			const FVector Scale = Transform.GetScale3D().GetAbs();
			const FVector Point(PhysicalXZ.X / Scale.X, 0, PhysicalXZ.Y / Scale.Z);
			return World->LineTraceSingleByChannel(Hit,
				Transform.TransformPosition(Point + FVector(0, -100, 0)),
				Transform.TransformPosition(Point + FVector(0, 100, 0)), ECC_Visibility);
		}

		bool Shoot(FAutomationTestBase& Test, const FVector2d& Point) const
		{
			check(Wall);
			check(Weapon);
			FHitResult Hit;
			if (!Test.TestTrue(TEXT("Each distinct shot hits remaining material"), Trace(Point, Hit))) return false;
			UGameplayStatics::ApplyPointDamage(Wall, 1, -Hit.ImpactNormal, Hit, nullptr, Weapon, nullptr);
			return true;
		}

		bool Reset(FAutomationTestBase& Test) const
		{
			check(Surface);
			struct { bool ReturnValue = false; } Params;
			Surface->ProcessEvent(Surface->FindFunctionChecked(TEXT("ResetSurface")), &Params);
			return Test.TestTrue(TEXT("Reset succeeds"), Params.ReturnValue);
		}

		bool Initialize(FAutomationTestBase& Test) const
		{
			check(Surface);
			struct { bool ReturnValue = false; } Params;
			Surface->ProcessEvent(Surface->FindFunctionChecked(TEXT("InitializeSurface")), &Params);
			return Test.TestTrue(TEXT("Cell surface initialization succeeds"), Params.ReturnValue);
		}

		bool Snapshot(FAutomationTestBase& Test, TArray<FVector3d>& Vertices, TArray<bool>& Collision) const
		{
			check(Wall);
			TArray<UDynamicMeshComponent*> Proxies;
			Wall->GetComponents(Proxies);
			if (!Test.TestEqual(TEXT("One accumulated surface proxy"), Proxies.Num(), 1)) return false;
			if (!Test.TestNotNull(TEXT("Accumulated proxy"), Proxies[0])) return false;
			const UE::Geometry::FDynamicMesh3* Mesh = Proxies[0]->GetMesh();
			if (!Test.TestNotNull(TEXT("Accumulated mesh"), Mesh)) return false;
			Vertices.Reset();
			for (int32 ID : Mesh->VertexIndicesItr()) Vertices.Add(Mesh->GetVertex(ID));
			Vertices.Sort([](const FVector3d& A, const FVector3d& B)
			{
				if (A.X != B.X) return A.X < B.X;
				if (A.Y != B.Y) return A.Y < B.Y;
				return A.Z < B.Z;
			});
			Collision.Reset();
			for (int32 X = -25; X <= 35; X += 2)
			{
				for (int32 Z = -25; Z <= 35; Z += 2)
				{
					FHitResult Hit;
					Collision.Add(Trace(FVector2d(X, Z), Hit));
				}
			}
			return true;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachCellSurfaceTest, "BreachSystem.Cells.Surface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachCellSurfaceTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Name : {TEXT("UnitScale"), TEXT("MirroredNonuniformScale"),
		TEXT("UnitScaleMultipleCells"), TEXT("MirroredNonuniformScaleMultipleCells")})
	{
		OutNames.Add(Name);
		OutCommands.Add(Name);
	}
}

bool FBreachCellSurfaceTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Global cell settings"), Settings)) return false;
	const float OriginalCellSize = Settings->CellSizeCm;
	Settings->CellSizeCm = 10;
	ON_SCOPE_EXIT { check(Settings); Settings->CellSizeCm = OriginalCellSize; };
	const FVector Scale = Parameters.StartsWith(TEXT("UnitScale")) ? FVector::OneVector : FVector(-2, 0.1, 3);
	FCellSurfaceFixture Fixture;
	if (!Fixture.Create(*this, Scale)) return false;
	check(Fixture.Surface);
	check(Fixture.Material);
	// Keep the same-cell contribution case separate from ordering distinct cell representatives.
	const TArray<FVector2d> Points = Parameters.EndsWith(TEXT("MultipleCells"))
		? TArray<FVector2d>{{2, 2}, {12, 2}, {22, 2}, {2, 12}, {12, 12}, {22, 12}}
		: TArray<FVector2d>{{2, 2}, {5, 2}, {8, 2}, {2, 7}, {5, 7}, {8, 7}};
	const TArray<int32> Forward = {0, 1, 2, 3, 4, 5};
	const TArray<int32> Permuted = {5, 2, 4, 1, 3, 0};
	auto FireGroup = [this, &Points](FCellSurfaceFixture& Target, const TArray<int32>& Order, const FVector2d& Jitter)
	{
		check(Target.Surface);
		for (int32 Index = 0; Index < Order.Num(); ++Index)
		{
			if (!Target.Shoot(*this, Points[Order[Index]] + Jitter)) return false;
			if (Index == 4)
			{
				TestEqual(TEXT("Five contributions stay below threshold"), Target.Surface->GetAccumulatedHitCount(), 5);
				FHitResult Hit;
				TestTrue(TEXT("Small holes leave surrounding material before threshold"), Target.Trace(FVector2d(5, 12), Hit));
			}
		}
		TestEqual(TEXT("Six contributions trigger and retire damage"), Target.Surface->GetAccumulatedHitCount(), 0);
		FHitResult Hit;
		TestFalse(TEXT("Accumulated footprint opens material away from direct holes"), Target.Trace(FVector2d(5, 12), Hit));
		return true;
	};
	TArray<FVector3d> ExpectedVertices;
	TArray<bool> ExpectedCollision;
	if (!FireGroup(Fixture, Forward, FVector2d::ZeroVector)) return false;
	if (!Fixture.Snapshot(*this, ExpectedVertices, ExpectedCollision)) return false;
	auto Compare = [this, &ExpectedVertices, &ExpectedCollision](const FCellSurfaceFixture& Target, const FString& Label)
	{
		TArray<FVector3d> ActualVertices;
		TArray<bool> ActualCollision;
		if (!Target.Snapshot(*this, ActualVertices, ActualCollision)) return;
		bool bSameGeometry = ActualVertices.Num() == ExpectedVertices.Num();
		if (bSameGeometry)
		{
			for (int32 Index = 0; Index < ActualVertices.Num(); ++Index)
			{
				if (!ActualVertices[Index].Equals(ExpectedVertices[Index], 0.001)) { bSameGeometry = false; break; }
			}
		}
		TestTrue(Label + TEXT(" preserves vertex positions independent of vertex IDs"), bSameGeometry);
		TestTrue(Label + TEXT(" preserves collision footprint"), ActualCollision == ExpectedCollision);
	};
	if (!Fixture.Reset(*this)) return false;
	if (!FireGroup(Fixture, Permuted, FVector2d::ZeroVector)) return false;
	Compare(Fixture, TEXT("Impact permutation"));
	if (!Fixture.Reset(*this)) return false;
	if (!FireGroup(Fixture, Forward, FVector2d(0.4, 0.3))) return false;
	Compare(Fixture, TEXT("Within-cell impact jitter"));
	FCellSurfaceFixture IdenticalWall;
	if (!IdenticalWall.Create(*this, Scale)) return false;
	if (!FireGroup(IdenticalWall, Permuted, FVector2d::ZeroVector)) return false;
	Compare(IdenticalWall, TEXT("New identical wall"));
	if (!Fixture.Reset(*this)) return false;
	Fixture.Material->AccumulationShape.Contour.Seed = 1;
	if (!FireGroup(Fixture, Forward, FVector2d::ZeroVector)) return false;
	if (!Fixture.Snapshot(*this, ExpectedVertices, ExpectedCollision)) return false;
	if (!Fixture.Reset(*this)) return false;
	Fixture.Material->AccumulationShape.Contour.Seed = 0;
	if (!FireGroup(Fixture, Permuted, FVector2d::ZeroVector)) return false;
	Compare(Fixture, TEXT("Accumulated seed zero uses stable seed one"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachCellSurfaceLifetimeTest, "BreachSystem.Cells.Surface.LifetimeAndDirectHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachCellSurfaceLifetimeTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Global cell settings"), Settings)) return false;
	const float OriginalCellSize = Settings->CellSizeCm;
	Settings->CellSizeCm = 10;
	ON_SCOPE_EXIT { check(Settings); Settings->CellSizeCm = OriginalCellSize; };
	FCellSurfaceFixture Fixture;
	if (!Fixture.Create(*this, FVector(-2, 0.1, 3))) return false;
	check(Fixture.Surface);
	check(Fixture.Wall);
	check(Fixture.Weapon);
	FBreachCellGrid Grid;
	UE::Geometry::FAxisAlignedBox2d Bounds;
	FVector2d Depth;
	if (!TestTrue(TEXT("Uninitialized surface provides its UV grid"), Fixture.Surface->GetCellGridForDebug(Grid, Bounds, Depth))) return false;
	TArray<UDynamicMeshComponent*> Proxies;
	Fixture.Wall->GetComponents(Proxies);
	TestEqual(TEXT("Reading grid does not initialize proxy"), Proxies.Num(), 0);
	TestTrue(TEXT("Reading grid preserves original visibility"), Fixture.Surface->IsVisible());
	TestEqual(TEXT("Preview uses current global cell size"), Grid.GetCellSize(), 10.0);
	const FVector2d Origin = Grid.ToLocal(FVector2d::ZeroVector);
	const FVector2d AdjacentU = Grid.ToLocal(FVector2d(10, 0));
	const FVector2d AdjacentV = Grid.ToLocal(FVector2d(0, 10));
	const FTransform Transform = Fixture.Surface->GetComponentTransform();
	const FVector WorldOrigin = Transform.TransformPosition(FVector(Origin.X, Depth.X, Origin.Y));
	const FVector WorldU = Transform.TransformPosition(FVector(AdjacentU.X, Depth.X, AdjacentU.Y)) - WorldOrigin;
	const FVector WorldV = Transform.TransformPosition(FVector(AdjacentV.X, Depth.X, AdjacentV.Y)) - WorldOrigin;
	TestTrue(TEXT("Rotated mirrored nonuniform surface keeps 10 cm U cells"), FMath::IsNearlyEqual(WorldU.Length(), 10.0, 0.0001));
	TestTrue(TEXT("Rotated mirrored nonuniform surface keeps 10 cm V cells"), FMath::IsNearlyEqual(WorldV.Length(), 10.0, 0.0001));
	TestTrue(TEXT("Physical cell edges stay perpendicular"), FMath::Abs(WorldU.Dot(WorldV)) < 0.0001);
	if (!Fixture.Initialize(*this)) return false;
	Settings->CellSizeCm = 20;
	if (!TestTrue(TEXT("Initialized grid remains readable"), Fixture.Surface->GetCellGridForDebug(Grid, Bounds, Depth))) return false;
	TestEqual(TEXT("Initialization snapshots global size until reset"), Grid.GetCellSize(), 10.0);
	FHitResult FirstHit;
	if (!TestTrue(TEXT("Initial direct hit exists"), Fixture.Trace(FVector2d(2, 2), FirstHit))) return false;
	if (!Fixture.Shoot(*this, FVector2d(2, 2))) return false;
	TestEqual(TEXT("First distinct hit contributes once"), Fixture.Surface->GetAccumulatedHitCount(), 1);
	FHitResult RepeatHit;
	TestFalse(TEXT("Direct bullet hole stays open before accumulation"), Fixture.Trace(FVector2d(2, 2), RepeatHit));
	Proxies.Reset();
	Fixture.Wall->GetComponents(Proxies);
	if (!TestEqual(TEXT("Direct hole owns one proxy"), Proxies.Num(), 1)) return false;
	if (!TestNotNull(TEXT("Direct hole proxy"), Proxies[0])) return false;
	FirstHit.Component = Proxies[0];
	const FPointDamageEvent StaleDamage(1, FirstHit, -FirstHit.ImpactNormal, nullptr);
	Fixture.Surface->HandleBreachDamage(Proxies[0], StaleDamage, Fixture.Weapon, 1);
	TestEqual(TEXT("Queued hit in an existing hole adds no cell damage"), Fixture.Surface->GetAccumulatedHitCount(), 1);
	if (!Fixture.Shoot(*this, FVector2d(5, 2))) return false;
	TestEqual(TEXT("Another material point in the same cell contributes again"), Fixture.Surface->GetAccumulatedHitCount(), 2);
	Fixture.Surface->SetWorldScale3D(FVector(-3, 0.1, 2));
	if (!Fixture.Shoot(*this, FVector2d(8, 7))) return false;
	TestEqual(TEXT("Scale change discards records from the old physical mapping"), Fixture.Surface->GetAccumulatedHitCount(), 1);
	if (!TestTrue(TEXT("Rescaled grid remains readable"), Fixture.Surface->GetCellGridForDebug(Grid, Bounds, Depth))) return false;
	TestEqual(TEXT("Scale change retains initialized cell size"), Grid.GetCellSize(), 10.0);
	if (!Fixture.Reset(*this)) return false;
	TestEqual(TEXT("Reset clears accumulated records"), Fixture.Surface->GetAccumulatedHitCount(), 0);
	if (!Fixture.Initialize(*this)) return false;
	if (!TestTrue(TEXT("Reinitialized grid remains readable"), Fixture.Surface->GetCellGridForDebug(Grid, Bounds, Depth))) return false;
	TestEqual(TEXT("Reset permits current global size on next initialization"), Grid.GetCellSize(), 20.0);
	return !HasAnyErrors();
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachCellUpdateTest, "BreachSystem.Cells.Surface.SingleUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachCellUpdateTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Name : {TEXT("Accumulated"), TEXT("Direct")})
	{
		OutNames.Add(Name);
		OutCommands.Add(Name);
	}
}

bool FBreachCellUpdateTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Global cell settings"), Settings)) return false;
	TGuardValue<float> CellSizeGuard(Settings->CellSizeCm, 10.f);
	FCellSurfaceFixture Fixture;
	if (!Fixture.Create(*this, FVector::OneVector)) return false;
	check(Fixture.Material);
	check(Fixture.Source);
	const bool bAccumulated = Parameters == TEXT("Accumulated");
	Fixture.Material->AccumulationShape.Contour.bEnabled = false;
	Fixture.Material->AccumulationShape.Padding = 0.5f;
	Fixture.Source->BreachDamage = bAccumulated ? 1.f : 0.17f;
	if (!Fixture.Initialize(*this)) return false;

	TArray<UDynamicMeshComponent*> Proxies;
	Fixture.Wall->GetComponents(Proxies);
	if (!TestEqual(TEXT("One surface proxy"), Proxies.Num(), 1)) return false;
	UDynamicMeshComponent* Proxy = Proxies[0];
	if (!TestNotNull(TEXT("Surface proxy"), Proxy)) return false;
	int32 MeshUpdates = 0;
	const FDelegateHandle Handle = Proxy->OnMeshChanged.AddLambda([&MeshUpdates]() { ++MeshUpdates; });
	ON_SCOPE_EXIT { Proxy->OnMeshChanged.Remove(Handle); };

	// The shot at (2,2) is outside the small accumulated footprint centered at (5,5).
	if (!Fixture.Shoot(*this, {2,2})) return false;
	TestEqual(TEXT("One hit publishes the final mesh once"), MeshUpdates, 1);
	FHitResult Hit;
	if (bAccumulated)
	{
		TestTrue(TEXT("Accumulated breach skips the current direct bullet hole"), Fixture.Trace({2,2}, Hit));
		TestFalse(TEXT("Accumulated breach opens its cell center"), Fixture.Trace({5,5}, Hit));
		TestEqual(TEXT("Successful accumulated breach consumes its impacts"), Fixture.Surface->GetAccumulatedHitCount(), 0);
	}
	else
	{
		TestFalse(TEXT("Below threshold the direct bullet hole is created"), Fixture.Trace({2,2}, Hit));
		TestTrue(TEXT("Below threshold the cell center stays solid"), Fixture.Trace({5,5}, Hit));
		TestEqual(TEXT("Below threshold the impact stays accumulated"), Fixture.Surface->GetAccumulatedHitCount(), 1);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachCellInvalidUVTest, "BreachSystem.Cells.Surface.InvalidUVPreservesDirectCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachCellInvalidUVTest::RunTest(const FString& Parameters)
{
	FCellSurfaceFixture Fixture;
	if (!Fixture.Create(*this, FVector::OneVector)) return false;
	check(Fixture.Surface);
	UStaticMesh* Cube = Fixture.Surface->GetStaticMesh();
	if (!TestNotNull(TEXT("Cube mesh"), Cube)) return false;
	const FMeshDescription* Original = Cube->GetMeshDescription(0);
	if (!TestNotNull(TEXT("Cube source description"), Original)) return false;
	FMeshDescription Description = *Original;
	FStaticMeshAttributes Attributes(Description);
	auto UVs = Attributes.GetVertexInstanceUVs();
	const auto Normals = Attributes.GetVertexInstanceNormals();
	for (FVertexInstanceID Instance : Description.VertexInstances().GetElementIDs())
	{
		if (Normals[Instance].Y < -0.99f)
		{
			UVs.Set(Instance, 0, FVector2f(0.5f, 0.5f));
		}
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
	if (!TestNotNull(TEXT("Transient singular-UV mesh"), Mesh)) return false;
	Mesh->GetStaticMaterials() = Cube->GetStaticMaterials();
	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bFastBuild = true;
	BuildParams.bAllowCpuAccess = true;
	BuildParams.bMarkPackageDirty = false;
	if (!TestTrue(TEXT("Singular UV fixture builds"), Mesh->BuildFromMeshDescriptions({&Description}, BuildParams))) return false;
	Fixture.Surface->SetStaticMesh(Mesh);
	if (!Fixture.Initialize(*this)) return false;
	if (!Fixture.Shoot(*this, {2,2})) return false;
	FHitResult Hit;
	TestFalse(TEXT("Invalid grid leaves direct bullet cutting available"), Fixture.Trace({2,2}, Hit));
	TestEqual(TEXT("Invalid grid records no accumulation"), Fixture.Surface->GetAccumulatedHitCount(), 0);
	return !HasAnyErrors();
}
#endif
