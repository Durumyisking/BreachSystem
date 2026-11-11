#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/AggregateGeom.h"

// 테스트 전용: 절단을 누적한 구멍 캐시가 RemainingRegions에서 구멍을 되짚지 않고도 같은 면적 정책의 충돌을 만드는지 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d MakeCacheRectangle(double MinX, double MinY, double MaxX, double MaxY)
	{
		FPolygon2d Polygon;
		for (const FVector2d& Point : {FVector2d(MinX, MinY), FVector2d(MaxX, MinY), FVector2d(MaxX, MaxY), FVector2d(MinX, MaxY)})
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	FPolygon2d MakeCacheCircle(const FVector2d& Center, double Radius, int32 Segments, double Rotation)
	{
		FPolygon2d Polygon;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const double Angle = Rotation + 2.0 * UE_DOUBLE_PI * Index / Segments;
			Polygon.AppendVertex(Center + Radius * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)));
		}
		return Polygon;
	}

	double CacheFootprintArea(const FKAggregateGeom& Collision)
	{
		double Area = 0;
		for (const FKConvexElem& Convex : Collision.ConvexElems)
		{
			FPolygon2d Footprint;
			for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
			{
				const FVector& Point = Convex.VertexData[Index];
				Footprint.AppendVertex(FVector2d(Point.X, Point.Z));
			}
			Area += FMath::Abs(Footprint.SignedArea());
		}
		return Area;
	}

	// Surface 컴포넌트의 절단 순서를 따른다: 남은 영역에서 커터를 빼고, 원본 경계에 닿지 않는 영역을 고립 조각으로 제거한 뒤 캐시에 알린다.
	struct FHoleCacheFixture
	{
		FPolygon2d Wall;
		FAxisAlignedBox2d WallBounds;
		TArray<FGeneralPolygon2d> Regions;
		FBreachSimpleCollision Collision;
		FString Error;

		explicit FHoleCacheFixture(const FPolygon2d& InWall)
			: Wall(InWall)
			, WallBounds(InWall.Bounds())
		{
			Regions.Add(FGeneralPolygon2d(Wall));
			Collision.Initialize(Wall, {-5, 5});
		}

		bool Cut(const FPolygon2d& Cutter)
		{
			const TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Cutter)};
			TArray<FGeneralPolygon2d> Candidate;
			if (!PolygonsDifference(Regions, Cutters, Candidate))
			{
				return false;
			}

			TArray<FPolygon2d> Detached;
			Regions.Reset();
			for (FGeneralPolygon2d& Region : Candidate)
			{
				const FAxisAlignedBox2d Bounds = Region.GetOuter().Bounds();
				constexpr double Epsilon = 0.001;
				const bool bTouchesX = Bounds.Min.X <= WallBounds.Min.X + Epsilon || Bounds.Max.X >= WallBounds.Max.X - Epsilon;
				const bool bTouchesY = Bounds.Min.Y <= WallBounds.Min.Y + Epsilon || Bounds.Max.Y >= WallBounds.Max.Y - Epsilon;
				if (bTouchesX || bTouchesY)
				{
					Regions.Add(MoveTemp(Region));
				}
				else
				{
					Detached.Add(Region.GetOuter());
				}
			}

			return Collision.ApplyCut(Cutter, Detached, Error);
		}

		double RemainingArea() const
		{
			double Area = 0;
			for (const FGeneralPolygon2d& Region : Regions)
			{
				Area += FMath::Abs(Region.SignedArea());
			}
			return Area;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachSimpleHoleCacheTest, "BreachSystem.Surface.SimpleHoleCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachSimpleHoleCacheTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const TCHAR* Name : {TEXT("Policy"), TEXT("Detached"), TEXT("OutsideOverlap"), TEXT("Stress"), TEXT("Fallback")})
	{
		Names.Add(Name);
		Commands.Add(Name);
	}
}

bool FBreachSimpleHoleCacheTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Hole cache settings"), Settings))
	{
		return false;
	}
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 450.0);
	const FPolygon2d Wall = MakeCacheRectangle(-50, -50, 50, 50);
	FKAggregateGeom Shapes;
	FString Error;

	auto ExpectArea = [&](FHoleCacheFixture& Fixture, const TCHAR* What, double Expected)
	{
		const bool bBuilt = Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error);
		if (!TestTrue(FString::Printf(TEXT("%s builds from the hole cache: %s"), What, *Error), bBuilt))
		{
			return;
		}
		const double Area = CacheFootprintArea(Shapes);
		TestTrue(FString::Printf(TEXT("%s footprint %.9g equals %.9g"), What, Area, Expected), FMath::IsNearlyEqual(Area, Expected, 1.e-6));
	};

	if (Parameters == TEXT("Policy"))
	{
		// 상태 확인: 내부 구멍은 이어져 임계를 넘을 때만 열리고, 임계를 바꾸면 출력만 바뀐다.
		FHoleCacheFixture Inner(Wall);
		TestTrue(TEXT("Small cut recorded"), Inner.Cut(MakeCacheRectangle(-25, -5, -5, 5)));
		ExpectArea(Inner, TEXT("Small cut"), 10000.0);
		TestTrue(TEXT("Second separate cut recorded"), Inner.Cut(MakeCacheRectangle(5, -5, 25, 5)));
		ExpectArea(Inner, TEXT("Separate cuts"), 10000.0);
		TestTrue(TEXT("Connecting cut recorded"), Inner.Cut(MakeCacheRectangle(-6, -5, 6, 5)));
		ExpectArea(Inner, TEXT("Connected opening"), 9500.0);

		FKAggregateGeom FromRegions;
		TestTrue(TEXT("Same state builds from the supplied regions"), Inner.Collision.BuildMesh(Inner.Regions, 1.0, FromRegions, Error));
		TestTrue(TEXT("Hole cache output matches the region-derived output"), FMath::IsNearlyEqual(CacheFootprintArea(FromRegions), 9500.0, 1.e-6));

		Settings->ComplexHoleSizeThreshold = 600.0;
		ExpectArea(Inner, TEXT("Higher threshold"), 10000.0);
		Settings->ComplexHoleSizeThreshold = 450.0;
		ExpectArea(Inner, TEXT("Restored threshold"), 9500.0);

		// 처리: 임계 미만인 별도 구멍은 이미 만든 충돌 출력을 바꾸지 않는다.
		const int32 PieceCount = Shapes.ConvexElems.Num();
		TestTrue(TEXT("Unrelated small cut recorded"), Inner.Cut(MakeCacheRectangle(30, 30, 34, 34)));
		ExpectArea(Inner, TEXT("Unrelated small cut"), 9500.0);
		TestEqual(TEXT("Unchanged selected openings keep the previous pieces"), Shapes.ConvexElems.Num(), PieceCount);

		// 상태 확인: 가장자리 파임도 같은 면적 조건을 따른다.
		FHoleCacheFixture Border(Wall);
		TestTrue(TEXT("Small border cut recorded"), Border.Cut(MakeCacheRectangle(-60, -5, -45, 5)));
		ExpectArea(Border, TEXT("Small border notch"), 10000.0);
		TestTrue(TEXT("Large border cut recorded"), Border.Cut(MakeCacheRectangle(-60, -20, -35, 20)));
		ExpectArea(Border, TEXT("Large border opening"), 9400.0);

		FHoleCacheFixture Split(Wall);
		TestTrue(TEXT("Wall-splitting cut recorded"), Split.Cut(MakeCacheRectangle(-5, -60, 5, 60)));
		TestEqual(TEXT("Split wall has two remaining regions"), Split.Regions.Num(), 2);
		ExpectArea(Split, TEXT("Split wall"), 9000.0);

		FHoleCacheFixture Removed(Wall);
		TestTrue(TEXT("Full removal recorded"), Removed.Cut(MakeCacheRectangle(-100, -100, 100, 100)));
		TestTrue(TEXT("Full removal builds"), Removed.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		TestEqual(TEXT("Full removal leaves no collision"), Shapes.ConvexElems.Num(), 0);
	}
	else if (Parameters == TEXT("Detached"))
	{
		// 상태 확인: 고립 조각이 제거되면 그 면적과 조각 안의 작은 구멍까지 둘러싼 구멍에 포함된다.
		Settings->ComplexHoleSizeThreshold = 1000.0;
		FHoleCacheFixture Fixture(Wall);
		TestTrue(TEXT("Small cut inside the future island recorded"), Fixture.Cut(MakeCacheRectangle(-2, -2, 2, 2)));
		for (const FPolygon2d& RingCut : {MakeCacheRectangle(-20, -20, 20, -15), MakeCacheRectangle(-20, 15, 20, 20), MakeCacheRectangle(-20, -15, -15, 15)})
		{
			TestTrue(TEXT("Open ring cut recorded"), Fixture.Cut(RingCut));
		}
		ExpectArea(Fixture, TEXT("Open ring below the threshold"), 10000.0);
		TestTrue(TEXT("Closing ring cut recorded"), Fixture.Cut(MakeCacheRectangle(15, -15, 20, 15)));
		TestTrue(TEXT("Closing the ring detaches the center"), FMath::IsNearlyEqual(Fixture.RemainingArea(), 8400.0, 1.e-6));
		ExpectArea(Fixture, TEXT("Detached center joins the opening"), 8400.0);
		Settings->ComplexHoleSizeThreshold = 2000.0;
		ExpectArea(Fixture, TEXT("Detached opening below a higher threshold"), 10000.0);
	}
	else if (Parameters == TEXT("OutsideOverlap"))
	{
		// 상태 확인: 벽 밖에서만 겹치는 커터는 벽 안의 파임을 하나로 잇지 않는다.
		Settings->ComplexHoleSizeThreshold = 80.0;
		FHoleCacheFixture Fixture(Wall);
		TestTrue(TEXT("Lower notch recorded"), Fixture.Cut(MakeCacheRectangle(-70, -12, -45, -2)));
		TestTrue(TEXT("Upper notch recorded"), Fixture.Cut(MakeCacheRectangle(-70, 2, -45, 12)));
		TestTrue(TEXT("Cutter outside the wall recorded"), Fixture.Cut(MakeCacheRectangle(-70, -12, -55, 12)));
		ExpectArea(Fixture, TEXT("Notches joined only outside the wall"), 10000.0);
		// 다리는 파임의 모서리에서 2만큼 떨어뜨린다. 더 가까우면 모서리가 단순화 허용 거리 안에 들어와 면적이 정확히 맞지 않는다.
		TestTrue(TEXT("Bridge inside the wall recorded"), Fixture.Cut(MakeCacheRectangle(-49, -4, -47, 4)));
		ExpectArea(Fixture, TEXT("Notches joined inside the wall"), 9892.0);
	}
	else if (Parameters == TEXT("Stress"))
	{
		// 회귀: 가장자리에 걸친 절단이 누적돼도 충돌 영역에 역방향 겹침 변이 생기지 않아 매 단계 삼각분할이 성공한다.
		Settings->ComplexHoleSizeThreshold = 600.0;
		const FPolygon2d LargeWall = MakeCacheRectangle(-150, 0, 150, 250);
		const double WallArea = 300.0 * 250.0;
		int32 OpenedSteps = 0;
		for (int32 Seed : {20261111, 7, 4242})
		{
			FRandomStream Random(Seed);
			FHoleCacheFixture Fixture(LargeWall);
			for (int32 Step = 0; Step < 150; ++Step)
			{
				const bool bLarge = Random.FRand() < 0.08f;
				const double Radius = bLarge ? Random.FRandRange(30.f, 50.f) : Random.FRandRange(5.f, 11.f);
				const FVector2d Center(Random.FRandRange(-160.f, 160.f), Random.FRandRange(-10.f, 260.f));
				const FPolygon2d Cutter = MakeCacheCircle(Center, Radius, bLarge ? 24 : 12, Random.FRand());
				if (!TestTrue(FString::Printf(TEXT("Seed %d step %d cut is recorded: %s"), Seed, Step, *Fixture.Error), Fixture.Cut(Cutter)))
				{
					return false;
				}
				if (!TestTrue(FString::Printf(TEXT("Seed %d step %d builds from the hole cache: %s"), Seed, Step, *Error), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error)))
				{
					return false;
				}

				const double Area = CacheFootprintArea(Shapes);
				const double Remaining = Fixture.RemainingArea();
				if (Area < WallArea - 1.e-6)
				{
					++OpenedSteps;
				}
				if (Area < Remaining - 1.e-6 || Area > WallArea + 1.e-6)
				{
					AddError(FString::Printf(TEXT("Seed %d step %d collision area %.9g must stay between remaining %.9g and wall %.9g."), Seed, Step, Area, Remaining, WallArea));
					return false;
				}
			}
			TestTrue(TEXT("Hole cache stays valid through the whole sequence"), Fixture.Collision.HasHoleCache());
		}
		TestTrue(TEXT("Accumulated cuts open the movement collision"), OpenedSteps > 0);
	}
	else if (Parameters == TEXT("Fallback"))
	{
		// 실패 처리: 캐시가 설명할 수 없는 입력은 캐시를 무효화하고, 영역에서 파생하는 경로는 계속 쓸 수 있다.
		FBreachSimpleCollision Uninitialized;
		TestFalse(TEXT("Uninitialized collision has no hole cache"), Uninitialized.HasHoleCache());

		FHoleCacheFixture Fixture(Wall);
		TestTrue(TEXT("Initialized collision has a hole cache"), Fixture.Collision.HasHoleCache());
		const TArray<FPolygon2d> Unmatched = {MakeCacheRectangle(20, 20, 30, 30)};
		TestFalse(TEXT("Detached region without a surrounding opening is rejected"), Fixture.Collision.ApplyCut(MakeCacheRectangle(-5, -5, 5, 5), Unmatched, Error));
		TestFalse(TEXT("Rejected cut reports a reason"), Error.IsEmpty());
		TestFalse(TEXT("Rejected cut invalidates the hole cache"), Fixture.Collision.HasHoleCache());
		TestFalse(TEXT("Invalid hole cache does not build"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		TestTrue(TEXT("Region-derived collision still builds"), Fixture.Collision.BuildMesh(Fixture.Regions, 1.0, Shapes, Error));
		TestTrue(TEXT("Region-derived collision keeps the wall"), FMath::IsNearlyEqual(CacheFootprintArea(Shapes), 10000.0, 1.e-6));
	}
	return !HasAnyErrors();
}
#endif
