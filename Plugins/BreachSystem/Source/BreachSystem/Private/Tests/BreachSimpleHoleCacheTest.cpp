#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "CompGeom/ConvexHull2.h"
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
	for (const TCHAR* Name : {TEXT("Policy"), TEXT("Detached"), TEXT("OutsideOverlap"), TEXT("Stress"), TEXT("Fallback"), TEXT("Rebuild"), TEXT("Isolation"), TEXT("Merged"), TEXT("Simplified")})
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
		// 상태 확인: 면적이 커져도 기준 원보다 얇은 구멍은 계속 막는다.
		FHoleCacheFixture Inner(Wall);
		TestTrue(TEXT("Small cut recorded"), Inner.Cut(MakeCacheRectangle(-25, -5, -5, 5)));
		ExpectArea(Inner, TEXT("Small cut"), 10000.0);
		TestTrue(TEXT("Second separate cut recorded"), Inner.Cut(MakeCacheRectangle(5, -5, 25, 5)));
		ExpectArea(Inner, TEXT("Separate cuts"), 10000.0);
		TestTrue(TEXT("Connecting cut recorded"), Inner.Cut(MakeCacheRectangle(-6, -5, 6, 5)));
		ExpectArea(Inner, TEXT("Connected opening"), 10000.0);

		FKAggregateGeom FromRegions;
		TestTrue(TEXT("Same state builds from the supplied regions"), Inner.Collision.BuildMesh(Inner.Regions, 1.0, FromRegions, Error));
		TestTrue(TEXT("Hole cache output matches the region-derived output"), FMath::IsNearlyEqual(CacheFootprintArea(FromRegions), 10000.0, 1.e-6));

		Settings->ComplexHoleSizeThreshold = 600.0;
		ExpectArea(Inner, TEXT("Higher threshold"), 10000.0);
		Settings->ComplexHoleSizeThreshold = 450.0;
		ExpectArea(Inner, TEXT("Restored threshold"), 10000.0);

		// 처리: 임계 미만인 별도 구멍은 이미 만든 충돌 출력을 바꾸지 않는다.
		const int32 PieceCount = Shapes.ConvexElems.Num();
		TestTrue(TEXT("Unrelated small cut recorded"), Inner.Cut(MakeCacheRectangle(30, 30, 34, 34)));
		ExpectArea(Inner, TEXT("Unrelated small cut"), 10000.0);
		TestEqual(TEXT("Unchanged selected openings keep the previous pieces"), Shapes.ConvexElems.Num(), PieceCount);

		// 상태 확인: 가장자리 파임과 벽을 가르는 절단에도 폭 조건을 적용한다.
		FHoleCacheFixture Border(Wall);
		TestTrue(TEXT("Small border cut recorded"), Border.Cut(MakeCacheRectangle(-60, -5, -45, 5)));
		ExpectArea(Border, TEXT("Small border notch"), 10000.0);
		TestTrue(TEXT("Large border cut recorded"), Border.Cut(MakeCacheRectangle(-60, -20, -35, 20)));
		ExpectArea(Border, TEXT("Large border opening"), 10000.0);

		FHoleCacheFixture Split(Wall);
		TestTrue(TEXT("Wall-splitting cut recorded"), Split.Cut(MakeCacheRectangle(-5, -60, 5, 60)));
		TestEqual(TEXT("Split wall has two remaining regions"), Split.Regions.Num(), 2);
		ExpectArea(Split, TEXT("Split wall"), 10000.0);

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
		TestTrue(TEXT("Detached center joins a broad opening"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		AddInfo(FString::Printf(TEXT("Detached broad opening retained area %.9f"), CacheFootprintArea(Shapes)));
		TestTrue(TEXT("The broad detached opening opens its body while retaining rounded corners"), CacheFootprintArea(Shapes) > 8400.0 && CacheFootprintArea(Shapes) < 9000.0);
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
		// 벽 안에서 연결돼도 각 Convex 부분이 80보다 작으므로 이동 통과 영역은 만들지 않는다.
		TestTrue(TEXT("Bridge inside the wall recorded"), Fixture.Cut(MakeCacheRectangle(-49, -4, -47, 4)));
		ExpectArea(Fixture, TEXT("Connected notches have no qualifying convex passage"), 10000.0);
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
	else if (Parameters == TEXT("Rebuild"))
	{
		// 회귀: 캐시를 잃은 표면도 남은 영역에서 캐시를 다시 만들어 이동 충돌을 계속 갱신한다.
		FHoleCacheFixture Fixture(Wall);
		TestTrue(TEXT("Broad opening recorded"), Fixture.Cut(MakeCacheRectangle(-25, -15, 25, 15)));
		TestTrue(TEXT("Broad opening builds"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		const double Opened = CacheFootprintArea(Shapes);
		TestTrue(TEXT("Broad opening removes movement collision"), Opened < 10000.0 - 1.0);
		const uint64 RevisionBefore = Fixture.Collision.GetRevision();

		const TArray<FPolygon2d> Unmatched = {MakeCacheRectangle(30, 30, 40, 40)};
		TestFalse(TEXT("Untrackable cut is rejected"), Fixture.Collision.ApplyCut(MakeCacheRectangle(-45, -45, -40, -40), Unmatched, Error));
		TestFalse(TEXT("Untrackable cut invalidates the hole cache"), Fixture.Collision.HasHoleCache());
		TestFalse(TEXT("Invalid hole cache does not build"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));

		TestTrue(TEXT("Hole cache is rebuilt from the remaining regions"), Fixture.Collision.RebuildHoleCache(Fixture.Regions, Error));
		TestTrue(TEXT("Rebuilt hole cache is valid"), Fixture.Collision.HasHoleCache());
		TestTrue(TEXT("Rebuild issues a new revision so a pending build is requested"), Fixture.Collision.GetRevision() > RevisionBefore);
		TestTrue(TEXT("Rebuilt hole cache builds"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		TestTrue(TEXT("Rebuilt cache reproduces the same opening"), FMath::IsNearlyEqual(CacheFootprintArea(Shapes), Opened, 1.e-3));

		TestTrue(TEXT("Later cuts are tracked on the rebuilt cache"), Fixture.Cut(MakeCacheRectangle(20, -15, 45, 15)));
		TestTrue(TEXT("Later cut builds"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		TestTrue(TEXT("Later cut widens the opening"), CacheFootprintArea(Shapes) < Opened - 1.0);
		TestTrue(TEXT("Collision never falls below the remaining wall"), CacheFootprintArea(Shapes) >= Fixture.RemainingArea() - 1.e-6);

		FHoleCacheFixture Empty(Wall);
		TestTrue(TEXT("Full removal recorded"), Empty.Cut(MakeCacheRectangle(-100, -100, 100, 100)));
		TestTrue(TEXT("Rebuild accepts a fully removed wall"), Empty.Collision.RebuildHoleCache(Empty.Regions, Error));
		TestTrue(TEXT("Rebuilt full removal builds"), Empty.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		TestEqual(TEXT("Rebuilt full removal leaves no collision"), Shapes.ConvexElems.Num(), 0);

		FBreachSimpleCollision Uninitialized;
		TestFalse(TEXT("Rebuild requires an initialized wall"), Uninitialized.RebuildHoleCache(Fixture.Regions, Error));
	}
	else if (Parameters == TEXT("Isolation"))
	{
		// 회귀: 한 개구부의 계산 실패는 그 개구부만 이전 통로로 고정하고, 다른 개구부와 이후 빌드를 막지 않는다.
		FBreachSimpleBuildPolicy Policy;
		Policy.AreaThreshold = 450.0;
		Policy.MaxCount = 3;
		const FPolygon2d Left = MakeCacheRectangle(-40, -20, -10, 20);
		const FPolygon2d Right = MakeCacheRectangle(10, -20, 40, 20);

		for (const bool bFailPrepare : {false, true})
		{
			FHoleCacheFixture Fixture(Wall);
			TestTrue(TEXT("Left opening recorded"), Fixture.Cut(Left));
			TestTrue(TEXT("Left opening builds"), Fixture.Collision.BuildMeshFromCuts(Policy, Shapes, Error));
			const double LeftOnly = CacheFootprintArea(Shapes);
			TestTrue(TEXT("Left opening removes movement collision"), LeftOnly < 10000.0 - 1.0);

			TestTrue(TEXT("Right opening recorded"), Fixture.Cut(Right));
			FBreachSimpleBuildPolicy Failing = Policy;
			if (bFailPrepare)
			{
				Failing.FailPrepareRevision = Fixture.Collision.GetRevision() - 1;
			}
			else
			{
				Failing.FailSolveRevision = Fixture.Collision.GetRevision() - 1;
			}
			TestTrue(TEXT("A failing opening does not fail the whole wall"), Fixture.Collision.BuildMeshFromCuts(Failing, Shapes, Error));
			TestEqual(TEXT("Only the failing opening is frozen"), Fixture.Collision.GetLastBuildStats().FrozenOpenings, 1);
			TestFalse(TEXT("The isolated failure reports its reason"), Error.IsEmpty());
			TestTrue(TEXT("The healthy opening stays open and the failing one stays blocked"), FMath::IsNearlyEqual(CacheFootprintArea(Shapes), LeftOnly, 1.e-6));

			TestTrue(TEXT("The same state builds again"), Fixture.Collision.BuildMeshFromCuts(Policy, Shapes, Error));
			TestEqual(TEXT("A frozen opening is not recalculated until it changes"), Fixture.Collision.GetLastBuildStats().PreparedOpenings, 0);
			TestTrue(TEXT("The frozen result is reused"), FMath::IsNearlyEqual(CacheFootprintArea(Shapes), LeftOnly, 1.e-6));

			TestTrue(TEXT("Third opening recorded"), Fixture.Cut(MakeCacheRectangle(-15, 22, 15, 48)));
			TestTrue(TEXT("Other openings keep updating after an isolated failure"), Fixture.Collision.BuildMeshFromCuts(Policy, Shapes, Error));
			const double WithThird = CacheFootprintArea(Shapes);
			TestTrue(TEXT("The third opening opens while the frozen one stays blocked"), WithThird < LeftOnly - 1.0);

			TestTrue(TEXT("The frozen opening grows"), Fixture.Cut(MakeCacheRectangle(10, -25, 40, 25)));
			TestTrue(TEXT("A changed opening is calculated again"), Fixture.Collision.BuildMeshFromCuts(Policy, Shapes, Error));
			TestEqual(TEXT("The retried opening is no longer frozen"), Fixture.Collision.GetLastBuildStats().FrozenOpenings, 0);
			TestTrue(TEXT("The retried opening finally opens"), CacheFootprintArea(Shapes) < WithThird - 1.0);
		}

		// 처리: 같은 빌드에서 두 개구부가 바뀌어도 원인 개구부 하나만 고정한다.
		FHoleCacheFixture Pair(Wall);
		TestTrue(TEXT("First of two openings recorded"), Pair.Cut(Left));
		TestTrue(TEXT("Second of two openings recorded"), Pair.Cut(Right));
		FBreachSimpleBuildPolicy Failing = Policy;
		Failing.FailSolveRevision = Pair.Collision.GetRevision() - 1;
		TestTrue(TEXT("Two fresh openings with one failure still build"), Pair.Collision.BuildMeshFromCuts(Failing, Shapes, Error));
		TestEqual(TEXT("Only the culprit of the two is frozen"), Pair.Collision.GetLastBuildStats().FrozenOpenings, 1);
		TestTrue(TEXT("The innocent opening of the two opens"), CacheFootprintArea(Shapes) < 10000.0 - 1.0);

		// 처리: 첫 빌드부터 실패한 유일한 개구부는 벽을 그대로 막아 둔다.
		FHoleCacheFixture Single(Wall);
		TestTrue(TEXT("Single opening recorded"), Single.Cut(Left));
		Failing = Policy;
		Failing.FailSolveRevision = Single.Collision.GetRevision() - 1;
		TestTrue(TEXT("A single failing opening keeps a valid wall"), Single.Collision.BuildMeshFromCuts(Failing, Shapes, Error));
		TestTrue(TEXT("The wall stays fully blocked"), FMath::IsNearlyEqual(CacheFootprintArea(Shapes), 10000.0, 1.e-6));
	}
	else if (Parameters == TEXT("Merged"))
	{
		// 회귀: 삼각형마다 기둥을 두지 않고 볼록한 이웃끼리 합친다. 합친 뒤에도 벽을 빈틈없이, 겹침 없이 덮어야 한다.
		auto Footprints = [&]()
		{
			TArray<FGeneralPolygon2d> Result;
			for (const FKConvexElem& Convex : Shapes.ConvexElems)
			{
				FPolygon2d Footprint;
				for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
				{
					const FVector& Point = Convex.VertexData[Index];
					Footprint.AppendVertex(FVector2d(Point.X, Point.Z));
				}
				TestTrue(TEXT("Every merged footprint stays convex"), Footprint.IsConvex());
				TestTrue(TEXT("Both prism caps use the same footprint"), Convex.VertexData.Num() == Footprint.VertexCount() * 2);
				if (Footprint.IsClockwise())
				{
					Footprint.Reverse();
				}
				Result.Emplace(MoveTemp(Footprint));
			}
			return Result;
		};
		auto ExpectExactCover = [&](const TCHAR* What)
		{
			const TArray<FGeneralPolygon2d> Pieces = Footprints();
			TArray<FGeneralPolygon2d> Covered;
			if (!TestTrue(FString::Printf(TEXT("%s footprints can be united"), What), PolygonsUnion(Pieces, Covered, false)))
			{
				return;
			}
			double CoveredArea = 0;
			for (const FGeneralPolygon2d& Part : Covered)
			{
				CoveredArea += FMath::Abs(Part.SignedArea());
			}
			TestTrue(FString::Printf(TEXT("%s merged pieces do not overlap: union %.9g, sum %.9g"), What, CoveredArea, CacheFootprintArea(Shapes)), FMath::IsNearlyEqual(CoveredArea, CacheFootprintArea(Shapes), 1.e-5));

			// 변을 공유하는 두 조각의 합집합이 여전히 볼록이면 병합이 덜 된 것이다.
			int32 Mergeable = 0;
			for (int32 A = 0; A < Pieces.Num(); ++A)
			{
				for (int32 B = A + 1; B < Pieces.Num(); ++B)
				{
					const TArray<FVector2d>& PointsA = Pieces[A].GetOuter().GetVertices();
					const TArray<FVector2d>& PointsB = Pieces[B].GetOuter().GetVertices();
					int32 Shared = 0;
					for (const FVector2d& Point : PointsA)
					{
						Shared += PointsB.Contains(Point) ? 1 : 0;
					}
					if (Shared < 2)
					{
						continue;
					}
					TArray<FVector2d> Combined(PointsA);
					Combined.Append(PointsB);
					TConvexHull2<double> Hull;
					if (!Hull.Solve(Combined))
					{
						continue;
					}
					FPolygon2d HullPolygon;
					for (int32 Index : Hull.GetPolygonIndices())
					{
						HullPolygon.AppendVertex(Combined[Index]);
					}
					const double Sum = FMath::Abs(Pieces[A].SignedArea()) + FMath::Abs(Pieces[B].SignedArea());
					if (FMath::Abs(FMath::Abs(HullPolygon.SignedArea()) - Sum) <= 1.e-7)
					{
						++Mergeable;
					}
				}
			}
			TestEqual(FString::Printf(TEXT("%s leaves no neighbours that could still be merged"), What), Mergeable, 0);
		};

		FHoleCacheFixture Fixture(Wall);
		TestTrue(TEXT("Subthreshold cut recorded"), Fixture.Cut(MakeCacheRectangle(-5, -5, 5, 5)));
		TestTrue(TEXT("Solid wall builds"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		TestEqual(TEXT("A solid rectangular wall is one convex prism, not two triangles"), Shapes.ConvexElems.Num(), 1);
		TestTrue(TEXT("The single prism keeps the wall area"), FMath::IsNearlyEqual(CacheFootprintArea(Shapes), 10000.0, 1.e-6));

		TestTrue(TEXT("Broad opening recorded"), Fixture.Cut(MakeCacheRectangle(-25, -15, 25, 15)));
		TestTrue(TEXT("Broad opening builds"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		const int32 OneOpening = Shapes.ConvexElems.Num();
		const double OneOpeningArea = CacheFootprintArea(Shapes);
		AddInfo(FString::Printf(TEXT("One rounded opening uses %d merged convex pieces."), OneOpening));
		TestTrue(TEXT("A wall with a hole needs several pieces"), OneOpening > 1);
		TestTrue(TEXT("The opening stays open"), OneOpeningArea < 10000.0 - 1.0 && OneOpeningArea >= Fixture.RemainingArea() - 1.e-6);
		ExpectExactCover(TEXT("One opening"));

		TestTrue(TEXT("Second opening recorded"), Fixture.Cut(MakeCacheRectangle(-40, 22, -10, 48)));
		TestTrue(TEXT("Second opening builds"), Fixture.Collision.BuildMeshFromCuts(1.0, Shapes, Error));
		AddInfo(FString::Printf(TEXT("Two rounded openings use %d merged convex pieces."), Shapes.ConvexElems.Num()));
		TestTrue(TEXT("The second opening opens too"), CacheFootprintArea(Shapes) < OneOpeningArea - 1.0 && CacheFootprintArea(Shapes) >= Fixture.RemainingArea() - 1.e-6);
		ExpectExactCover(TEXT("Two openings"));
	}
	else if (Parameters == TEXT("Simplified"))
	{
		// 회귀: 통로 윤곽 단순화는 벽 충돌 조각을 줄이되, 통로를 허용 오차 이내로만 좁히고 벽이 남은 곳은 열지 않는다.
		FBreachSimpleBuildPolicy Exact;
		Exact.AreaThreshold = 450.0;
		Exact.MaxCount = 3;
		Exact.PassageTolerance = 0;
		FBreachSimpleBuildPolicy Loose = Exact;
		Loose.PassageTolerance = 1;
		auto FootprintsOf = [](const FKAggregateGeom& Geometry)
		{
			TArray<FGeneralPolygon2d> Result;
			for (const FKConvexElem& Convex : Geometry.ConvexElems)
			{
				FPolygon2d Footprint;
				for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
				{
					const FVector& Point = Convex.VertexData[Index];
					Footprint.AppendVertex(FVector2d(Point.X, Point.Z));
				}
				if (Footprint.IsClockwise())
				{
					Footprint.Reverse();
				}
				Result.Emplace(MoveTemp(Footprint));
			}
			return Result;
		};

		FHoleCacheFixture Fixture(Wall);
		TestTrue(TEXT("First round opening recorded"), Fixture.Cut(MakeCacheCircle(FVector2d(-15, 0), 22, 24, 0.1)));
		TestTrue(TEXT("Overlapping round opening recorded"), Fixture.Cut(MakeCacheCircle(FVector2d(12, 5), 20, 24, 0.3)));

		FKAggregateGeom ExactShapes;
		TestTrue(TEXT("Exact outline builds"), Fixture.Collision.BuildMeshFromCuts(Exact, ExactShapes, Error));
		const double ExactArea = CacheFootprintArea(ExactShapes);
		TestTrue(TEXT("Exact outline opens the wall"), ExactArea < 10000.0 - 1.0);

		FKAggregateGeom LooseShapes;
		TestTrue(TEXT("Simplified outline builds"), Fixture.Collision.BuildMeshFromCuts(Loose, LooseShapes, Error));
		TestTrue(TEXT("Changing only the tolerance rebuilds the collision shapes"), Fixture.Collision.GetLastBuildStats().bCollisionChanged);
		TestEqual(TEXT("Changing only the tolerance keeps the prepared openings"), Fixture.Collision.GetLastBuildStats().PreparedOpenings, 0);
		const double LooseArea = CacheFootprintArea(LooseShapes);
		AddInfo(FString::Printf(TEXT("Passage tolerance 0 -> 1 cm: %d -> %d convex pieces, wall area %.3f -> %.3f."), ExactShapes.ConvexElems.Num(), LooseShapes.ConvexElems.Num(), ExactArea, LooseArea));
		TestTrue(TEXT("Simplified outline needs fewer collision pieces"), LooseShapes.ConvexElems.Num() < ExactShapes.ConvexElems.Num());
		TestTrue(TEXT("Simplified outline still opens the wall"), LooseArea < 10000.0 - 1.0);
		TestTrue(TEXT("Simplification never shrinks the wall"), LooseArea >= ExactArea - 1.e-6);

		// 벽이 남은 곳은 열지 않는다: 정확한 벽 조각은 모두 단순화한 벽에 덮인다.
		const TArray<FGeneralPolygon2d> ExactPieces = FootprintsOf(ExactShapes);
		const TArray<FGeneralPolygon2d> LoosePieces = FootprintsOf(LooseShapes);
		TArray<FGeneralPolygon2d> Uncovered;
		if (TestTrue(TEXT("Exact and simplified walls can be compared"), PolygonsDifference(ExactPieces, LoosePieces, Uncovered)))
		{
			double UncoveredArea = 0;
			for (const FGeneralPolygon2d& Part : Uncovered)
			{
				UncoveredArea += FMath::Abs(Part.SignedArea());
			}
			TestTrue(FString::Printf(TEXT("Simplified wall covers the exact wall (uncovered %.9g)"), UncoveredArea), UncoveredArea <= 1.e-5);
		}

		// 통로는 허용 오차 이내로만 좁아진다: 늘어난 벽 면적은 허용 오차 × 통로 둘레를 넘지 않는다.
		auto PassagesOf = [&](const TArray<FGeneralPolygon2d>& WallPieces)
		{
			TArray<FGeneralPolygon2d> Passages;
			PolygonsDifference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Wall)}, WallPieces, Passages);
			return Passages;
		};
		double PassagePerimeter = 0;
		for (const FGeneralPolygon2d& Passage : PassagesOf(ExactPieces))
		{
			PassagePerimeter += Passage.GetOuter().Perimeter();
		}
		TestTrue(TEXT("The exact passage outline can be measured"), PassagePerimeter > 0);
		TestTrue(FString::Printf(TEXT("Wall grows by %.6f, bound %.6f"), LooseArea - ExactArea, Loose.PassageTolerance * PassagePerimeter), LooseArea - ExactArea <= Loose.PassageTolerance * PassagePerimeter + 1.e-6);

		FKAggregateGeom Repeated;
		TestTrue(TEXT("Unchanged tolerance builds again"), Fixture.Collision.BuildMeshFromCuts(Loose, Repeated, Error));
		TestFalse(TEXT("Unchanged tolerance reuses the built collision"), Fixture.Collision.GetLastBuildStats().bCollisionChanged);
		TestEqual(TEXT("Reused collision keeps its pieces"), Repeated.ConvexElems.Num(), LooseShapes.ConvexElems.Num());

		FKAggregateGeom Restored;
		TestTrue(TEXT("Zero tolerance builds again"), Fixture.Collision.BuildMeshFromCuts(Exact, Restored, Error));
		TestEqual(TEXT("Zero tolerance restores the exact outline"), Restored.ConvexElems.Num(), ExactShapes.ConvexElems.Num());
		TestTrue(TEXT("Zero tolerance restores the exact wall area"), FMath::IsNearlyEqual(CacheFootprintArea(Restored), ExactArea, 1.e-6));

		FBreachSimpleBuildPolicy Invalid = Exact;
		Invalid.PassageTolerance = -1;
		TestFalse(TEXT("Negative tolerance is rejected"), Fixture.Collision.BuildMeshFromCuts(Invalid, Restored, Error));

		// 회귀: 단순화한 통로도 한 번 열리면 다시 막히지 않는다. 구멍이 계속 커지는 동안 매 단계 직전 통로가 그대로 남아야 한다.
		Loose.AreaThreshold = 600.0;
		const FPolygon2d LargeWall = MakeCacheRectangle(-150, 0, 150, 250);
		auto GrowingPassages = [&](const FKAggregateGeom& Geometry)
		{
			TArray<FGeneralPolygon2d> Passages;
			PolygonsDifference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(LargeWall)}, FootprintsOf(Geometry), Passages);
			return Passages;
		};
		FRandomStream Random(20261115);
		FHoleCacheFixture Growing(LargeWall);
		TArray<FGeneralPolygon2d> PreviousPassages;
		int32 Grown = 0;
		double WorstLoss = 0;
		for (int32 Step = 0; Step < 60; ++Step)
		{
			const bool bLarge = Random.FRand() < 0.25f;
			const double Radius = bLarge ? Random.FRandRange(25.f, 45.f) : Random.FRandRange(6.f, 14.f);
			const FVector2d Center(Random.FRandRange(-130.f, 130.f), Random.FRandRange(20.f, 230.f));
			if (!TestTrue(FString::Printf(TEXT("Growing step %d cut is recorded: %s"), Step, *Growing.Error), Growing.Cut(MakeCacheCircle(Center, Radius, bLarge ? 24 : 12, Random.FRand()))))
			{
				return false;
			}
			FKAggregateGeom StepShapes;
			if (!TestTrue(FString::Printf(TEXT("Growing step %d builds: %s"), Step, *Error), Growing.Collision.BuildMeshFromCuts(Loose, StepShapes, Error)))
			{
				return false;
			}
			const TArray<FGeneralPolygon2d> CurrentPassages = GrowingPassages(StepShapes);
			TArray<FGeneralPolygon2d> Lost;
			if (!PreviousPassages.IsEmpty())
			{
				PolygonsDifference(PreviousPassages, CurrentPassages, Lost);
			}
			double LostArea = 0;
			for (const FGeneralPolygon2d& Part : Lost)
			{
				LostArea += FMath::Abs(Part.SignedArea());
			}
			WorstLoss = FMath::Max(WorstLoss, LostArea);
			if (LostArea >= 0.001)
			{
				AddError(FString::Printf(TEXT("Growing step %d closed %.6f cm2 of a passage that was already open."), Step, LostArea));
				return false;
			}
			if (Growing.Collision.GetLastBuildStats().bCollisionChanged)
			{
				++Grown;
			}
			PreviousPassages = CurrentPassages;
		}
		AddInfo(FString::Printf(TEXT("Simplified passages grew in %d of 60 steps; worst re-closed area %.9f cm2."), Grown, WorstLoss));
		TestTrue(TEXT("The growing sequence actually changed the passages several times"), Grown >= 5);
	}
	return !HasAnyErrors();
}
#endif
