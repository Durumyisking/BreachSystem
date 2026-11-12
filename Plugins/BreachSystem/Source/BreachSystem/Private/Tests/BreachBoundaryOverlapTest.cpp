#include "Surface/BreachSurfaceGeometry.h"
#include "Surface/BreachSimpleCollision.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// 테스트 전용: 역방향 겹침 복원, 기존 좌표와 작은 면 보존, 실제 실패 입력의 삼각분할을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	FPolygon2d BoundaryPolygon(std::initializer_list<FVector2d> Points)
	{
		FPolygon2d Polygon;
		for (const FVector2d& Point : Points)
		{
			Polygon.AppendVertex(Point);
		}
		return Polygon;
	}

	TArray<FGeneralPolygon2d> ReadSnapshotRegions(const FString& Path, const FString& StageName)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			return {};
		}
		TArray<FGeneralPolygon2d> Regions;
		TArray<FPolygon2d> Loops;
		bool bReading = false;
		auto FinishRegion = [&]()
		{
			if (Loops.IsEmpty())
			{
				return;
			}
			FGeneralPolygon2d Region(Loops[0]);
			for (int32 Index = 1; Index < Loops.Num(); ++Index)
			{
				Region.AddHole(Loops[Index], false, false);
			}
			Regions.Add(MoveTemp(Region));
			Loops.Reset();
		};
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("STAGE ")))
			{
				FinishRegion();
				bReading = Line == TEXT("STAGE ") + StageName;
				continue;
			}
			if (!bReading)
			{
				continue;
			}
			if (Line.StartsWith(TEXT("REGION ")))
			{
				FinishRegion();
			}
			else if (Line.StartsWith(TEXT("LOOP ")))
			{
				Loops.Emplace();
			}
			else if (!Loops.IsEmpty())
			{
				TArray<FString> Fields;
				Line.ParseIntoArrayWS(Fields);
				if (Fields.Num() == 2)
				{
					double X, Y;
					const bool bXParsed = LexTryParseString(X, *Fields[0]);
					const bool bYParsed = LexTryParseString(Y, *Fields[1]);
					if (bXParsed && bYParsed)
					{
						Loops.Last().AppendVertex({X, Y});
					}
				}
			}
		}
		FinishRegion();
		return Regions;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FBreachBoundaryOverlapTest, "BreachSystem.Surface.BoundaryOverlap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FBreachBoundaryOverlapTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const FString& Name : {TEXT("ReverseOverlap"), TEXT("Clockwise"), TEXT("OuterHole"), TEXT("PointContact"), TEXT("ValidHole"), TEXT("NearCollinear"), TEXT("SlopedOverlap"), TEXT("OverlapWithHoleAndIsland"), TEXT("RejectMultiplicity"), TEXT("Snapshots"), TEXT("IntegratedSnapshots")})
	{
		OutNames.Add(Name);
		OutCommands.Add(Name);
	}
}

bool FBreachBoundaryOverlapTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	auto Verify = [&](const FString& Label, TArray<FGeneralPolygon2d> Regions, int32 ExpectedCount, double ExpectedArea, bool bRequireTinyTriangle, int32 ExpectedHoleCount = INDEX_NONE)
	{
		TSet<FVector2d> OriginalPoints;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			auto Remember = [&](const FPolygon2d& Loop)
			{
				for (const FVector2d& Point : Loop.GetVertices())
				{
					OriginalPoints.Add(Point);
				}
			};
			Remember(Region.GetOuter());
			for (const FPolygon2d& Hole : Region.GetHoles())
			{
				Remember(Hole);
			}
		}
		FString Error;
		const bool bResolved = BreachSurfaceGeometry::ResolveBoundaryOverlaps(Regions, Error);
		if (!TestTrue(Label + TEXT(" boundary repair succeeds: ") + Error, bResolved))
		{
			return;
		}
		TestEqual(Label + TEXT(" region count"), Regions.Num(), ExpectedCount);
		double Area = 0;
		int32 HoleCount = 0;
		bool bTinyTriangleFound = false;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			if (Label == TEXT("Clockwise"))
			{
				TestTrue(TEXT("Repaired clockwise outer keeps its orientation"), Region.OuterIsClockwise());
			}
			Area += Region.SignedArea();
			HoleCount += Region.GetHoles().Num();
			bTinyTriangleFound |= Region.GetHoles().IsEmpty() && Region.GetOuter().VertexCount() == 3 && FMath::Abs(Region.GetOuter().SignedArea()) == 1.0;
			auto CheckCoordinates = [&](const FPolygon2d& Loop)
			{
				for (const FVector2d& Point : Loop.GetVertices())
				{
					TestTrue(Label + TEXT(" uses an unchanged original coordinate"), OriginalPoints.Contains(Point));
				}
			};
			CheckCoordinates(Region.GetOuter());
			for (const FPolygon2d& Hole : Region.GetHoles())
			{
				CheckCoordinates(Hole);
			}
			TArray<FVector2d> Vertices;
			TArray<FIndex3i> Triangles;
			const bool bTriangulated = BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, Error);
			TestTrue(Label + TEXT(" triangulates: ") + Error, bTriangulated);
		}
		TestTrue(Label + TEXT(" filled area is preserved"), FMath::Abs(Area - ExpectedArea) <= 1.e-8);
		if (ExpectedHoleCount != INDEX_NONE)
		{
			TestEqual(Label + TEXT(" retains its independent interior hole"), HoleCount, ExpectedHoleCount);
		}
		if (bRequireTinyTriangle)
		{
			TestTrue(Label + TEXT(" preserves the area-one triangle"), bTinyTriangleFound);
		}
	};

	if (Parameters == TEXT("Snapshots") || Parameters == TEXT("IntegratedSnapshots"))
	{
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Diagnostics/SimpleCollisionFailures");
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("snapshot.txt"), true, false);
		if (Files.IsEmpty())
		{
			AddWarning(TEXT("No local failure snapshots; deterministic overlap fixtures remain available."));
		}
		int32 ReplayedCount = 0;
		for (const FString& File : Files)
		{
			TArray<FGeneralPolygon2d> Regions = ReadSnapshotRegions(File, TEXT("D_CollisionRegions"));
			if (!TestTrue(File + TEXT(" contains captured collision regions"), !Regions.IsEmpty()))
			{
				continue;
			}
			double Area = 0;
			bool bPreviouslyFailed = false;
			for (const FGeneralPolygon2d& Region : Regions)
			{
				Area += Region.SignedArea();
				TArray<FVector2d> Vertices;
				TArray<FIndex3i> Triangles;
				FString Error;
				bPreviouslyFailed |= !BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, Error);
			}
			TestTrue(File + TEXT(" reproduces its original triangulation failure"), bPreviouslyFailed);
			const TMap<FString, int32> ExpectedCounts = {
				{TEXT("1BB3B167-42A6-12C7-A9F0-A0A7239D2CD6"), 2},
				{TEXT("6D48FE16-4CC5-4827-2CD3-8AB01CF49230"), 1},
				{TEXT("75F8FF5D-4F7E-3760-F6BA-B99B7252642C"), 1},
				{TEXT("7F62052D-49D4-18F3-EBAC-DC9356807A8C"), 3},
				{TEXT("91696015-4E58-A10F-C49B-AD9C696F0621"), 2},
				{TEXT("B0087280-4A58-82DE-6384-FFBF88E9F4B3"), 5},
				{TEXT("C1760848-4CBD-2400-CB03-EB8CB81D6AE1"), 7},
				{TEXT("C44B8B7A-41A2-EC38-1705-F184DB470B07"), 2}
			};
			const FString CaptureId = FPaths::GetCleanFilename(FPaths::GetPath(File));
			const int32 ExpectedCount = ExpectedCounts.FindRef(CaptureId);
			if (ExpectedCount == 0)
			{
				AddWarning(TEXT("No hand-checked region count for capture ") + CaptureId);
				continue;
			}
			++ReplayedCount;
			if (Parameters == TEXT("Snapshots"))
			{
				Verify(File, MoveTemp(Regions), ExpectedCount, Area, false);
			}
			else
			{
				const TArray<FGeneralPolygon2d> Original = ReadSnapshotRegions(File, TEXT("Original"));
				const TArray<FGeneralPolygon2d> Remaining = ReadSnapshotRegions(File, TEXT("A_RemainingRegions"));
				if (!TestEqual(File + TEXT(" contains the original wall"), Original.Num(), 1))
				{
					continue;
				}
				UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
				if (!TestNotNull(TEXT("Breach settings exist"), Settings))
				{
					return false;
				}
				TGuardValue<double> Threshold(Settings->ComplexHoleSizeThreshold, 1000.0);
				FBreachSimpleCollision Collision;
				Collision.Initialize(Original[0].GetOuter(), {-5, 5});
				FKAggregateGeom Shapes;
				FString Error;
				const bool bBuilt = Collision.BuildMesh(Remaining, 1.0, Shapes, Error);
				TestTrue(File + TEXT(" actual Simple BuildMesh succeeds: ") + Error, bBuilt);
				double CollisionArea = 0;
				TArray<FGeneralPolygon2d> Footprints;
				for (const FKConvexElem& Convex : Shapes.ConvexElems)
				{
					FPolygon2d Front;
					for (int32 Index = 0; Index < Convex.VertexData.Num() / 2; ++Index)
					{
						const FVector& Point = Convex.VertexData[Index];
						Front.AppendVertex({Point.X, Point.Z});
					}
					CollisionArea += FMath::Abs(Front.SignedArea());
					Footprints.Add(FGeneralPolygon2d(Front));
				}
				// Convex 통과 영역은 원본 개구부보다 작을 수 있다. 남아 있는 벽을 제거하지 않는지 공간으로 검사한다.
				TArray<FGeneralPolygon2d> MissingSolid;
				TestTrue(File + TEXT(" retained-wall coverage can be measured"), PolygonsDifference(Remaining, Footprints, MissingSolid));
				double MissingArea = 0;
				for (const FGeneralPolygon2d& Part : MissingSolid)
				{
					MissingArea += FMath::Abs(Part.SignedArea());
				}
				TestTrue(File + TEXT(" collision covers every retained wall region"), MissingArea <= 1.e-6);
				TestTrue(File + TEXT(" collision stays within the original wall area"), CollisionArea <= FMath::Abs(Original[0].SignedArea()) + 1.e-6);
			}
		}
		AddInfo(FString::Printf(TEXT("Replayed %d captured failure snapshots."), ReplayedCount));
		return !HasAnyErrors();
	}

	FPolygon2d Square = BoundaryPolygon({{0, 0}, {100, 0}, {100, 100}, {0, 100}});
	if (Parameters == TEXT("ReverseOverlap") || Parameters == TEXT("Clockwise"))
	{
		FPolygon2d Overlap = BoundaryPolygon({{0, 50}, {94, 0}, {92, 0}, {91, 1}, {90, 0}, {100, 0}, {100, 100}, {0, 100}});
		if (Parameters == TEXT("Clockwise"))
		{
			Overlap.Reverse();
		}
		Verify(Parameters, {FGeneralPolygon2d(Overlap)}, 2, 7651.0, true);
	}
	else if (Parameters == TEXT("OuterHole"))
	{
		FGeneralPolygon2d Region(Square);
		Region.AddHole(BoundaryPolygon({{100, 20}, {50, 50}, {100, 80}}), false, false);
		Verify(Parameters, {Region}, 1, 8500.0, false);
	}
	else if (Parameters == TEXT("PointContact"))
	{
		TArray<FGeneralPolygon2d> Regions;
		const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(Square)};
		const TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(BoundaryPolygon({{100, 80}, {100, 90}, {90, 100}}))};
		TestTrue(TEXT("Reduced boundary-contact Difference executes"), PolygonsDifference(Original, Cutters, Regions));
		Verify(Parameters, MoveTemp(Regions), 2, 9950.0, false);
	}
	else if (Parameters == TEXT("ValidHole"))
	{
		FGeneralPolygon2d Region(Square);
		Region.AddHole(BoundaryPolygon({{20, 20}, {20, 80}, {80, 80}, {80, 20}}));
		TArray<FGeneralPolygon2d> Regions = {Region};
		FString Error;
		TestTrue(TEXT("Valid input is accepted"), BreachSurfaceGeometry::ResolveBoundaryOverlaps(Regions, Error));
		TestTrue(TEXT("Valid outer ordering stays unchanged"), Regions[0].GetOuter().GetVertices() == Region.GetOuter().GetVertices());
		TestEqual(TEXT("Valid hole remains a hole"), Regions[0].GetHoles().Num(), 1);
		if (Regions[0].GetHoles().Num() == 1)
		{
			TestTrue(TEXT("Valid hole ordering stays unchanged"), Regions[0].GetHoles()[0].GetVertices() == Region.GetHoles()[0].GetVertices());
		}
		Verify(Parameters, Regions, 1, 6400.0, false);
	}
	else if (Parameters == TEXT("NearCollinear"))
	{
		FPolygon2d Polygon = BoundaryPolygon({{0, 0}, {4, 0}, {4, 4}, {0, 4}, {2, 1.e-8}});
		TArray<FGeneralPolygon2d> Regions = {FGeneralPolygon2d(Polygon)};
		FString Error;
		TestTrue(TEXT("Near edge is accepted without snapping"), BreachSurfaceGeometry::ResolveBoundaryOverlaps(Regions, Error));
		TestTrue(TEXT("Near edge coordinates and ordering stay unchanged"), Regions[0].GetOuter().GetVertices() == Polygon.GetVertices());
	}
	else if (Parameters == TEXT("SlopedOverlap"))
	{
		FGeneralPolygon2d Region(BoundaryPolygon({{0, 0}, {100, 100}, {0, 100}}));
		Region.AddHole(BoundaryPolygon({{20, 20}, {20, 60}, {60, 60}}), false, false);
		Verify(Parameters, {Region}, 1, 4200.0, false);
	}
	else if (Parameters == TEXT("RejectMultiplicity"))
	{
		FPolygon2d Polygon = BoundaryPolygon({{0, 50}, {94, 0}, {92, 0}, {91, 1}, {90, 0}, {100, 0}, {100, 100}, {0, 100}});
		const TArray<FVector2d> Original = Polygon.GetVertices();
		const FGeneralPolygon2d Repairable(Polygon);
		for (const FVector2d& Point : Original)
		{
			Polygon.AppendVertex(Point);
		}
		TArray<FGeneralPolygon2d> Regions = {Repairable, FGeneralPolygon2d(Polygon)};
		FString Error;
		TestFalse(TEXT("Repeated directed boundary is rejected rather than losing material"), BreachSurfaceGeometry::ResolveBoundaryOverlaps(Regions, Error));
		TestFalse(TEXT("Unsupported boundary reports why"), Error.IsEmpty());
		TestEqual(TEXT("Failed repair keeps the original region count"), Regions.Num(), 2);
		TestTrue(TEXT("Failure rolls back the earlier repairable region"), Regions[0].GetOuter().GetVertices() == Original);
		TestTrue(TEXT("Failed repair keeps the invalid region unchanged"), Regions[1].GetOuter().GetVertices() == Polygon.GetVertices());
	}
	else if (Parameters == TEXT("OverlapWithHoleAndIsland"))
	{
		FGeneralPolygon2d Region(BoundaryPolygon({{0, 50}, {94, 0}, {92, 0}, {91, 1}, {90, 0}, {100, 0}, {100, 100}, {0, 100}}));
		Region.AddHole(BoundaryPolygon({{10, 70}, {10, 80}, {20, 80}, {20, 70}}));
		const FGeneralPolygon2d Island(BoundaryPolygon({{12, 72}, {18, 72}, {18, 78}, {12, 78}}));
		Verify(Parameters, {Region, Island}, 3, 7587.0, true, 1);
	}
	return !HasAnyErrors();
}
#endif
