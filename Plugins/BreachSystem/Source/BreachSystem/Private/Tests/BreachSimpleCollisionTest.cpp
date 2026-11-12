#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "BreachSimpleAsyncTestUtils.h"

// 테스트 전용: 총알과 이동 충돌의 면적 정책, 연결, 고립 조각, 누적 파손을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d Rectangle(double MinX, double MinZ, double MaxX, double MaxZ)
	{
		FPolygon2d Polygon;
		for (FVector2d Point : {FVector2d(MinX, MinZ), FVector2d(MaxX, MinZ), FVector2d(MaxX, MaxZ), FVector2d(MinX, MaxZ)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	bool CutFixtureRegions(TArray<FGeneralPolygon2d>& Regions, const FPolygon2d& Cutter, TConstArrayView<FPolygon2d> Detached)
	{
		TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Cutter)};
		for (const FPolygon2d& Region : Detached)
		{
			Cutters.Add(FGeneralPolygon2d(Region));
		}
		TArray<FGeneralPolygon2d> Candidate;
		if (!PolygonsDifference(Regions, Cutters, Candidate))
		{
			return false;
		}
		Regions = MoveTemp(Candidate);
		return true;
	}

	struct FSimpleCollisionFixture
	{
		UWorld* World = nullptr;
		AActor* Wall = nullptr;
		UBreachableSurfaceComponent* Surface = nullptr;
		AActor* Weapon = nullptr;
		UBreachSourceComponent* Source = nullptr;

		~FSimpleCollisionFixture()
		{
			if (World) World->DestroyWorld(false);
		}

		bool Create(FAutomationTestBase& Test, FVector Scale = FVector::OneVector)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Movement test world"), World)) return false;
			Wall = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Movement test wall"), Wall)) return false;
			Surface = NewObject<UBreachableSurfaceComponent>(Wall);
			Wall->SetRootComponent(Surface);
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Test.TestNotNull(TEXT("Movement test cube"), Cube)) return false;
			Surface->SetStaticMesh(Cube);
			Surface->SetCollisionProfileName(TEXT("BlockAll"));
			Surface->SetWorldTransform(FTransform(FRotator(0, 35, 0), FVector(100, 200, 300), Scale));
			Surface->RegisterComponent();
			Weapon = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Movement test weapon"), Weapon)) return false;
			Source = NewObject<UBreachSourceComponent>(Weapon);
			Source->RegisterComponent();
			return true;
		}

		bool Hits(FVector2d Point, bool bComplex, bool bCapsule = false) const
		{
			check(World);
			check(Surface);
			if (!bComplex)
			{
				checkf(FBreachSimpleAsyncTestAccess::Wait(Surface), TEXT("Simple collision did not finish before the movement query."));
			}
			const FTransform Transform = Surface->GetComponentTransform();
			const FVector Start = Transform.TransformPosition(FVector(Point.X, -100, Point.Y));
			const FVector End = Transform.TransformPosition(FVector(Point.X, 100, Point.Y));
			FCollisionQueryParams Params(SCENE_QUERY_STAT(BreachSimpleCollisionTest), bComplex);
			Params.AddIgnoredActor(Weapon);
			FHitResult Hit;
			if (bCapsule)
			{
				return World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(2.f, 4.f), Params);
			}
			return World->LineTraceSingleByChannel(Hit, Start, End, bComplex ? ECC_Visibility : ECC_Pawn, Params);
		}

		bool Cut(const FPolygon2d& Polygon) const
		{
			check(Surface);
			return Surface->ApplyCutterPolygon(Polygon);
		}

		void ShootBox(FVector2d Center, FVector2D Size) const
		{
			check(Surface);
			check(Source);
			Source->BindBox(Size);
			FHitResult Hit;
			TArray<UDynamicMeshComponent*> Proxies;
			Wall->GetComponents(Proxies);
			Hit.Component = Proxies.IsEmpty() ? static_cast<UPrimitiveComponent*>(Surface) : Proxies[0];
			Hit.ImpactPoint = Surface->GetComponentTransform().TransformPosition(FVector(Center.X, -50, Center.Y));
			Hit.ImpactNormal = Surface->GetComponentTransform().TransformVectorNoScale(FVector(0, -1, 0));
			UGameplayStatics::ApplyPointDamage(Wall, 1.f, -Hit.ImpactNormal, Hit, nullptr, Weapon, nullptr);
		}

		void Reset() const
		{
			check(Surface);
			struct { bool ReturnValue = false; } Params;
			Surface->ProcessEvent(Surface->FindFunctionChecked(TEXT("ResetSurface")), &Params);
			check(Params.ReturnValue);
		}

		UDynamicMeshComponent* Proxy() const
		{
			check(Wall);
			TArray<UDynamicMeshComponent*> Proxies;
			Wall->GetComponents(Proxies);
			return Proxies.Num() == 1 ? Proxies[0] : nullptr;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachSimpleCollisionTest, "BreachSystem.Surface.SimpleCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachSimpleCollisionTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("Direct"), TEXT("Connected"), TEXT("Detached"), TEXT("Accumulated"), TEXT("WorldArea"),
		TEXT("Concave"), TEXT("Simplified"), TEXT("EdgeOpening"), TEXT("FullRemoval"), TEXT("Lifecycle")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FBreachSimpleCollisionTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Movement collision settings"), Settings)) return false;
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 500.0);
	FSimpleCollisionFixture Fixture;
	const FVector Scale = Parameters == TEXT("WorldArea") ? FVector(-2, 0.1, 3) : FVector::OneVector;
	if (!Fixture.Create(*this, Scale)) return false;

	if (Parameters == TEXT("Direct"))
	{
		Fixture.ShootBox({-30, 0}, {10, 10});
		TestFalse(TEXT("Small Source hole is open to bullets"), Fixture.Hits({-30, 0}, true));
		TestTrue(TEXT("Small Source hole stays closed to movement"), Fixture.Hits({-30, 0}, false));
		Fixture.ShootBox({10, 0}, {30, 40});
		TestFalse(TEXT("Large Source opening clears Simple collision"), Fixture.Hits({10, 0}, false));
		TestFalse(TEXT("Capsule passes through a large Source opening"), Fixture.Hits({10, 0}, false, true));
		TestTrue(TEXT("Untouched wall still blocks a capsule"), Fixture.Hits({40, 0}, false, true));
		UDynamicMeshComponent* Proxy = Fixture.Proxy();
		if (TestNotNull(TEXT("Movement proxy"), Proxy))
		{
			TestEqual(TEXT("Simple output does not use whole-wall boxes"), Proxy->GetSimpleCollisionShapes().BoxElems.Num(), 0);
			TestTrue(TEXT("Simple output contains convex pieces"), Proxy->GetSimpleCollisionShapes().ConvexElems.Num() > 1);
		}
	}
	else if (Parameters == TEXT("Connected"))
	{
		TestTrue(TEXT("First small opening"), Fixture.Cut(Rectangle(-25, -5, -5, 5)));
		TestTrue(TEXT("Second small opening"), Fixture.Cut(Rectangle(5, -5, 25, 5)));
		TestTrue(TEXT("Separate small openings remain blocked"), Fixture.Hits({-15, 0}, false));
		TestTrue(TEXT("Bridge cut"), Fixture.Cut(Rectangle(-6, -5, 6, 5)));
		TestTrue(TEXT("Combined area at the threshold stays blocked"), Fixture.Hits({-15, 0}, false));
		TestTrue(TEXT("Extend the connected opening above the threshold"), Fixture.Cut(Rectangle(-25, -5, 26, 5)));
		TestTrue(TEXT("A long opening narrower than the probe keeps the first hole blocked"), Fixture.Hits({-15, 0}, false));
		TestTrue(TEXT("The narrow bridge remains blocked"), Fixture.Hits({0, 0}, false));
		TestTrue(TEXT("The narrow second hole remains blocked"), Fixture.Hits({15, 0}, false));
	}
	else if (Parameters == TEXT("Detached"))
	{
		Settings->ComplexHoleSizeThreshold = 10000;
		for (const FPolygon2d& Cut : {Rectangle(-25, -25, 25, -20), Rectangle(-25, 20, 25, 25),
			Rectangle(-25, -21, -20, 21), Rectangle(20, -21, 25, 21)})
		{
			TestTrue(TEXT("Island boundary cut succeeds"), Fixture.Cut(Cut));
		}
		TestFalse(TEXT("Detached wall island disappears from Complex"), Fixture.Hits({0, 0}, true));
		TestTrue(TEXT("Detached opening below the threshold keeps Simple collision"), Fixture.Hits({0, 0}, false));
		TestTrue(TEXT("Attached wall remains solid"), Fixture.Hits({40, 0}, false));
	}
	else if (Parameters == TEXT("Accumulated"))
	{
		UBreachSurfaceMaterial* Material = NewObject<UBreachSurfaceMaterial>(Fixture.World);
		Material->Accumulation.bEnabled = true;
		Material->AccumulationShape.Padding = 20.f;
		Material->AccumulationShape.Contour.bEnabled = false;
		Fixture.Surface->SetSurfaceMaterial(Material);
		Fixture.Source->BreachDamage = 1.f;
		Fixture.ShootBox({2, 2}, {1, 1});
		TestFalse(TEXT("Accumulated opening clears Complex"), Fixture.Hits({5, 5}, true));
		TestFalse(TEXT("Accumulated opening clears Simple"), Fixture.Hits({5, 5}, false));
		TestFalse(TEXT("Capsule passes through accumulated opening"), Fixture.Hits({5, 5}, false, true));
		TestTrue(TEXT("Accumulation preserves untouched Simple collision"), Fixture.Hits({40, 0}, false));
	}
	else if (Parameters == TEXT("WorldArea"))
	{
		TestTrue(TEXT("Scaled opening succeeds"), Fixture.Cut(Rectangle(-5, -5, 5, 5)));
		TestTrue(TEXT("Sufficient world area with insufficient world width remains blocked"), Fixture.Hits({0, 0}, false));
		TestTrue(TEXT("Scaled untouched wall remains solid"), Fixture.Hits({30, 0}, false));
	}
	else if (Parameters == TEXT("Concave"))
	{
		FPolygon2d L;
		for (FVector2d Point : {FVector2d(-30, -30), FVector2d(30, -30), FVector2d(30, -10),
			FVector2d(-10, -10), FVector2d(-10, 30), FVector2d(-30, 30)}) L.AppendVertex(Point);
		TestTrue(TEXT("Concave opening succeeds"), Fixture.Cut(L));
		TestTrue(TEXT("First narrow concave arm stays blocked"), Fixture.Hits({-20, 15}, false));
		TestTrue(TEXT("Second narrow concave arm stays blocked"), Fixture.Hits({15, -20}, false));
		TestTrue(TEXT("Solid corner inside the opening hull remains blocked"), Fixture.Hits({15, 15}, false));
		FPolygon2d Wide;
		for (const FVector2d Point : {FVector2d(-40,-40), FVector2d(40,-40), FVector2d(40,0), FVector2d(0,0), FVector2d(0,40), FVector2d(-40,40)})
		{
			Wide.AppendVertex(Point);
		}
		TestTrue(TEXT("Widen the two concave arms"), Fixture.Cut(Wide));
		TestFalse(TEXT("First broad arm opens"), Fixture.Hits({-20,15}, false));
		TestFalse(TEXT("Second broad arm opens"), Fixture.Hits({15,-20}, false));
		TestTrue(TEXT("Broad arms preserve the solid corner"), Fixture.Hits({15,15}, false));
	}
	else if (Parameters == TEXT("Simplified"))
	{
		FPolygon2d Opening;
		for (int32 Index = 0; Index < 96; ++Index)
		{
			const double Angle = 2 * UE_PI * Index / 96;
			const double Radius = Index % 2 == 0 ? 25.0 : 25.4;
			Opening.AppendVertex({Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle)});
		}
		TestTrue(TEXT("Detailed opening succeeds"), Fixture.Cut(Opening));
		TestFalse(TEXT("Simplification retains the opening center"), Fixture.Hits({0, 0}, false));
		for (int32 X = -30; X <= 30; X += 3)
		{
			for (int32 Z = -30; Z <= 30; Z += 3)
			{
				if (!Opening.Contains(FVector2d(X, Z)))
				{
					TestTrue(TEXT("Simple never opens visible solid material"), Fixture.Hits({static_cast<double>(X), static_cast<double>(Z)}, false));
				}
			}
		}
		UDynamicMeshComponent* Proxy = Fixture.Proxy();
		if (TestNotNull(TEXT("Simplified proxy"), Proxy))
		{
			const FKAggregateGeom& Shapes = Proxy->GetSimpleCollisionShapes();
			const int32 PassageCount = FBreachSimpleAsyncTestAccess::PassageCount(Fixture.Surface);
			TestTrue(TEXT("Detailed opening uses a bounded number of convex passages"), PassageCount > 0 && PassageCount <= Settings->MaxSimpleConvexCount);
			FString Geometry = TEXT("{\"opening\":[");
			for (int32 Index = 0; Index < Opening.VertexCount(); ++Index)
			{
				if (Index) Geometry += TEXT(",");
				Geometry += FString::Printf(TEXT("[%.9g,%.9g]"), Opening[Index].X, Opening[Index].Y);
			}
			Geometry += TEXT("],\"pieces\":[");
			for (int32 Index = 0; Index < Shapes.ConvexElems.Num(); ++Index)
			{
				if (Index) Geometry += TEXT(",");
				Geometry += TEXT("[");
				const TArray<FVector>& Points = Shapes.ConvexElems[Index].VertexData;
				FPolygon2d Footprint;
				for (int32 Point = 0; Point < Points.Num() / 2; ++Point)
				{
					if (Point) Geometry += TEXT(",");
					Geometry += FString::Printf(TEXT("[%.9g,%.9g]"), Points[Point].X, Points[Point].Z);
					Footprint.AppendVertex({Points[Point].X, Points[Point].Z});
				}
				TestTrue(TEXT("Every supplied collision footprint is convex"), Footprint.IsConvex());
				Geometry += TEXT("]");
			}
			Geometry += TEXT("]}");
			TestTrue(TEXT("Export actual collision geometry for visual inspection"), FFileHelper::SaveStringToFile(Geometry,
				*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimpleCollisionGeometry.json"))));
		}
	}
	else if (Parameters == TEXT("EdgeOpening"))
	{
		TestTrue(TEXT("Opening clipped by original border succeeds"), Fixture.Cut(Rectangle(-60, -20, -20, 20)));
		TestFalse(TEXT("Open wall edge clears movement"), Fixture.Hits({-40, 0}, false, true));
		TestTrue(TEXT("Remaining wall beside edge opening blocks"), Fixture.Hits({20, 0}, false));
	}
	else if (Parameters == TEXT("FullRemoval"))
	{
		Settings->ComplexHoleSizeThreshold = 1.e8;
		TestTrue(TEXT("Full visual removal succeeds"), Fixture.Cut(Rectangle(-100, -100, 100, 100)));
		TestTrue(TEXT("Full opening below the threshold keeps Simple collision"), Fixture.Hits({0, 0}, false));
	}
	else if (Parameters == TEXT("Lifecycle"))
	{
		TestTrue(TEXT("Initial large opening"), Fixture.Cut(Rectangle(-20, -20, 20, 20)));
		TestFalse(TEXT("Large opening is open"), Fixture.Hits({0, 0}, false));
		FPolygon2d Invalid;
		Invalid.AppendVertex({0, 0});
		TestFalse(TEXT("Invalid cut is rejected"), Fixture.Cut(Invalid));
		TestFalse(TEXT("Invalid cut preserves Simple opening"), Fixture.Hits({0, 0}, false));
		Fixture.Reset();
		TestTrue(TEXT("Reset restores original wall"), Fixture.Hits({0, 0}, false));
		TestTrue(TEXT("Small hole after reset"), Fixture.Cut(Rectangle(-5, -5, 5, 5)));
		TestTrue(TEXT("Reset clears previous movement openings"), Fixture.Hits({0, 0}, false));
		TestTrue(TEXT("Full removal succeeds"), Fixture.Cut(Rectangle(-100, -100, 100, 100)));
		TestFalse(TEXT("Full removal clears Simple"), Fixture.Hits({0, 0}, false));
		TestFalse(TEXT("Full removal clears Complex"), Fixture.Hits({0, 0}, true));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleCollisionCacheTest, "BreachSystem.Surface.SimpleCollisionCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachSimpleCollisionCacheTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Wall = Rectangle(-50, -50, 50, 50);
	TArray<FGeneralPolygon2d> Regions = {FGeneralPolygon2d(Wall)};
	FBreachSimpleCollision Collision;
	Collision.Initialize(Wall, {-5, 5});
	FKAggregateGeom Shapes;
	FString Error;
	TestTrue(TEXT("Unbroken movement collision builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestEqual(TEXT("Unbroken rectangle merges into one convex piece"), Shapes.ConvexElems.Num(), 1);
	const TArray<FPolygon2d> NoDetached;
	for (double Center : {-30.0, 0.0, 30.0})
	{
		TestTrue(TEXT("A separate small opening is recorded in the supplied regions"), CutFixtureRegions(Regions, Rectangle(Center - 2, -2, Center + 2, 2), NoDetached));
	}
	TestTrue(TEXT("Large cut updates supplied regions"), CutFixtureRegions(Regions, Rectangle(-15, -15, 15, 15), NoDetached));
	const double BuildStart = FPlatformTime::Seconds();
	TestTrue(TEXT("Changed supplied regions build"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	AddInfo(FString::Printf(TEXT("Movement convex build: %d pieces, %.3f ms (automation fixture)."),
		Shapes.ConvexElems.Num(), (FPlatformTime::Seconds() - BuildStart) * 1000));
	const int32 PreviousPieceCount = Shapes.ConvexElems.Num();
	TestTrue(TEXT("An already open cut is accepted"), CutFixtureRegions(Regions, Rectangle(-10, -10, 10, 10), NoDetached));
	TestTrue(TEXT("Unchanged supplied regions build"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	TestEqual(TEXT("An unchanged opening preserves collision output"), Shapes.ConvexElems.Num(), PreviousPieceCount);
	return !HasAnyErrors();
}
// 테스트 전용: 모든 구멍에 동일한 월드 면적 임계값을 적용하는지 검증한다.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachSimpleCollisionThresholdTest, "BreachSystem.Surface.SimpleCollisionThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachSimpleCollisionThresholdTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("Below"), TEXT("Equal"), TEXT("Above"), TEXT("Detached"), TEXT("FullRemoval")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FBreachSimpleCollisionThresholdTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Threshold settings"), Settings))
	{
		return false;
	}
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 500.0);
	FBreachSimpleCollision Collision;
	const FPolygon2d Wall = Rectangle(-50, -50, 50, 50);
	Collision.Initialize(Wall, {-5, 5});
	TArray<FGeneralPolygon2d> Regions = {FGeneralPolygon2d(Wall)};
	FPolygon2d Cutter = Rectangle(-5, -24, 5, 24);
	TArray<FPolygon2d> Detached;
	if (Parameters == TEXT("Equal"))
	{
		Cutter = Rectangle(-5, -25, 5, 25);
	}
	else if (Parameters == TEXT("Above"))
	{
		Cutter = Rectangle(-5, -26, 5, 26);
	}
	else if (Parameters == TEXT("Detached"))
	{
		Cutter = Rectangle(-30, -30, -29, -29);
		Detached.Add(Rectangle(-5, -5, 5, 5));
	}
	else if (Parameters == TEXT("FullRemoval"))
	{
		Settings->ComplexHoleSizeThreshold = 10000.0;
		Cutter = Rectangle(-100, -100, 100, 100);
	}

	FString Error;
	TestTrue(TEXT("Cut succeeds"), CutFixtureRegions(Regions, Cutter, Detached));
	const bool bShouldOpen = Parameters == TEXT("Above");
	FKAggregateGeom Shapes;
	TestTrue(TEXT("Simple collision builds"), Collision.BuildMesh(Regions, 1.0, Shapes, Error));
	if (bShouldOpen)
	{
		TestTrue(TEXT("Large opening leaves multiple wall pieces"), Shapes.ConvexElems.Num() > 1);
	}
	else
	{
		TestEqual(TEXT("Small or equal opening keeps the solid wall"), Shapes.ConvexElems.Num(), 1);
	}
	return !HasAnyErrors();
}
#endif
