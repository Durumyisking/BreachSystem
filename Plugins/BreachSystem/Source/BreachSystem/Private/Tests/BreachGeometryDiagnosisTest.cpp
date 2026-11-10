#include "Surface/BreachSimpleCollision.h"
#include "Surface/BreachSurfaceGeometry.h"
#include "Settings/BreachSystemSettings.h"
#include "CompGeom/ConvexHull3.h"
#include "CompGeom/Delaunay2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/AggregateGeom.h"

// 테스트 전용: 실제 절단 경로를 관찰하며 오류 입력을 보존한다. 형상이나 실패 정책은 변경하지 않는다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;

	struct FGeometryStage
	{
		FString Name;
		TArray<FGeneralPolygon2d> Regions;
	};

	TArray<FVector2d> Flatten(const FGeneralPolygon2d& Region)
	{
		TArray<FVector2d> Vertices = Region.GetOuter().GetVertices();
		for (const FPolygon2d& Hole : Region.GetHoles())
		{
			Vertices.Append(Hole.GetVertices());
		}
		return Vertices;
	}

	void AppendRegions(FString& Text, TConstArrayView<FGeneralPolygon2d> Regions)
	{
		for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
		{
			const FGeneralPolygon2d& Region = Regions[RegionIndex];
			Text += FString::Printf(TEXT("REGION %d AREA %.17g\n"), RegionIndex, Region.SignedArea());
			auto AppendLoop = [&](const FPolygon2d& Loop, int32 LoopIndex)
			{
				Text += FString::Printf(TEXT("LOOP %d COUNT %d\n"), LoopIndex, Loop.VertexCount());
				for (const FVector2d& Point : Loop.GetVertices())
				{
					Text += FString::Printf(TEXT("%.17g %.17g\n"), Point.X, Point.Y);
				}
			};
			AppendLoop(Region.GetOuter(), -1);
			for (int32 HoleIndex = 0; HoleIndex < Region.GetHoles().Num(); ++HoleIndex)
			{
				AppendLoop(Region.GetHoles()[HoleIndex], HoleIndex);
			}
		}
	}

	FString Summarize(TConstArrayView<FGeneralPolygon2d> Regions)
	{
		int32 ExactDuplicates = 0;
		int32 FloatCollapses = 0;
		double MinDistinctDistance = TNumericLimits<double>::Max();
		for (const FGeneralPolygon2d& Region : Regions)
		{
			const TArray<FVector2d> Vertices = Flatten(Region);
			for (int32 A = 0; A < Vertices.Num(); ++A)
			{
				for (int32 B = A + 1; B < Vertices.Num(); ++B)
				{
					if (Vertices[A] == Vertices[B])
					{
						++ExactDuplicates;
					}
					else
					{
						MinDistinctDistance = FMath::Min(MinDistinctDistance, (Vertices[B] - Vertices[A]).Length());
						if (FVector2f(Vertices[A]) == FVector2f(Vertices[B]))
						{
							++FloatCollapses;
						}
					}
				}
			}
		}
		return FString::Printf(TEXT("regions=%d exact_duplicate_pairs=%d float_collapsed_pairs=%d min_distinct_distance=%.17g"),
			Regions.Num(), ExactDuplicates, FloatCollapses, MinDistinctDistance);
	}

	FString DescribeMissingEdges(FDelaunay2& Delaunay, const FGeneralPolygon2d& Region)
	{
		FString Text;
		const TArray<FVector2d> Vertices = Flatten(Region);
		int32 Offset = 0;
		auto InspectLoop = [&](const FPolygon2d& Loop, int32 LoopIndex)
		{
			for (int32 Current = 0, Last = Loop.VertexCount() - 1; Current < Loop.VertexCount(); Last = Current++)
			{
				const FIndex2i Edge(Offset + Last, Offset + Current);
				if (Delaunay.HasEdge(Edge, false))
				{
					continue;
				}
				const FVector2d A = Vertices[Edge.A];
				const FVector2d B = Vertices[Edge.B];
				Text += FString::Printf(TEXT("MISSING loop=%d edge=(%d,%d) A=(%.17g,%.17g) B=(%.17g,%.17g)\n"),
					LoopIndex, Edge.A, Edge.B, A.X, A.Y, B.X, B.Y);
				for (int32 Index = 0; Index < Vertices.Num(); ++Index)
				{
					if (Index == Edge.A || Index == Edge.B)
					{
						continue;
					}
					const FVector2d AP = Vertices[Index] - A;
					const FVector2d AB = B - A;
					const double Cross = AB.X * AP.Y - AB.Y * AP.X;
					const double Dot = AP.Dot(AB);
					if (Vertices[Index] == A || Vertices[Index] == B || (Cross == 0 && Dot >= 0 && Dot <= AB.SquaredLength()))
					{
						Text += FString::Printf(TEXT("  ON_EDGE_OR_DUPLICATE vertex=%d P=(%.17g,%.17g) cross=%.17g\n"),
							Index, Vertices[Index].X, Vertices[Index].Y, Cross);
					}
				}
			}
			Offset += Loop.VertexCount();
		};
		InspectLoop(Region.GetOuter(), -1);
		for (int32 Index = 0; Index < Region.GetHoles().Num(); ++Index)
		{
			InspectLoop(Region.GetHoles()[Index], Index);
		}
		return Text;
	}

	bool TouchesWall(const FPolygon2d& Polygon)
	{
		const TArray<FVector2d>& Vertices = Polygon.GetVertices();
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			const FVector2d A = Vertices[Index];
			const FVector2d B = Vertices[(Index + 1) % Vertices.Num()];
			if ((B - A).SquaredLength() <= 1.e-6)
			{
				continue;
			}
			for (double Limit : {-50.0, 50.0})
			{
				if ((FMath::Abs(A.X - Limit) <= .001 && FMath::Abs(B.X - Limit) <= .001)
					|| (FMath::Abs(A.Y - Limit) <= .001 && FMath::Abs(B.Y - Limit) <= .001))
				{
					return true;
				}
			}
		}
		return false;
	}

	FString DifferenceControl(const TCHAR* Name, TConstArrayView<FGeneralPolygon2d> Before, TConstArrayView<FGeneralPolygon2d> Cuts)
	{
		TArray<FGeneralPolygon2d> Candidate;
		const bool bBoolean = PolygonsDifference(Before, Cuts, Candidate);
		int32 Failures = 0;
		int32 Collapsed = 0;
		double MinArea = TNumericLimits<double>::Max();
		for (const FGeneralPolygon2d& Region : Candidate)
		{
			FDelaunay2 Delaunay;
			TArray<FVector2d> Vertices;
			TArray<FIndex3i> Triangles;
			if (!Delaunay.Triangulate(Region, &Triangles, &Vertices, false))
			{
				++Failures;
				continue;
			}
			for (const FIndex3i& Triangle : Triangles)
			{
				const FVector2d A = Vertices[Triangle.A], B = Vertices[Triangle.B], C = Vertices[Triangle.C];
				const FVector2f AF(A), BF(B), CF(C);
				const double Area = FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * .5;
				const double FloatCross = double(BF.X - AF.X) * double(CF.Y - AF.Y) - double(BF.Y - AF.Y) * double(CF.X - AF.X);
				MinArea = FMath::Min(MinArea, Area);
				Collapsed += int32(FloatCross == 0);
			}
		}
		return FString::Printf(TEXT("CONTROL %s boolean=%d triangulation_failures=%d collapsed_triangles=%d min_area=%.17g %s\n"),
			Name, bBoolean, Failures, Collapsed, MinArea, *Summarize(Candidate));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachGeometryDiagnosisTest, "BreachSystem.Diagnostics.GeometryCauses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachGeometryDiagnosisTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	if (!TestNotNull(TEXT("Diagnosis settings"), Settings))
	{
		return false;
	}
	TGuardValue<double> ThresholdGuard(Settings->ComplexHoleSizeThreshold, 1000.0);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Diagnostics/SurfaceShared20261110");
	IFileManager::Get().MakeDirectory(*Directory, true);
	FString Report = TEXT("Surface regions passed directly to Simple; inspecting derived CollisionRegions; Simplify disabled; threshold=1000 cm2.\n");
	Report += TEXT("Surface detachment is modelled with the same rectangle-border predicate, not a full gameplay/PIE replay.\n");
	FPolygon2d Outer;
	for (const FVector2d& Point : {FVector2d(-50, -50), FVector2d(50, -50), FVector2d(50, 50), FVector2d(-50, 50)})
	{
		Outer.AppendVertex(Point);
	}
	int32 MissingCases = 0;
	int32 CollapsedCases = 0;
	int32 ThinHullCases = 0;
	int32 BuildFailureCases = 0;
	for (int32 Scenario = 0; Scenario < 10; ++Scenario)
	{
		FBreachSimpleCollision Simple;
		Simple.Initialize(Outer, FVector2d(-50, 50));
		TArray<FGeometryStage> Stages;
		Simple.GeometryObserver = [&](const TCHAR* Name, TConstArrayView<FGeneralPolygon2d> Regions)
		{
			Stages.Add({FString(Name), TArray<FGeneralPolygon2d>(Regions)});
		};
		TArray<FGeneralPolygon2d> Surface = {FGeneralPolygon2d(Outer)};
		FRandomStream Random(71 + FMath::Max(0, Scenario - 2) * 97);
		bool bSavedMissing = false;
		bool bSavedCollapsed = false;
		bool bSavedThin = false;
		bool bSavedNoDuplicateFailure = false;
		bool bSavedBuildFailure = false;
		const int32 Steps = Scenario >= 2 ? 128 : 48;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double Angle = 2 * PI * Step / 24;
			const FVector2d Center = Scenario >= 2
				? FVector2d(Random.FRandRange(-40.f, 40.f), Random.FRandRange(-40.f, 40.f))
				: FVector2d(22 * FMath::Cos(Angle), 22 * FMath::Sin(Angle));
			FPolygon2d Cutter;
			for (int32 Vertex = 0; Vertex < 32; ++Vertex)
			{
				const double Theta = 2 * PI * Vertex / 32;
				const double Radius = (Scenario >= 2 ? 7.0 : 4.5) + (Scenario == 0 ? 0 : Random.FRandRange(-.6f, .6f));
				Cutter.AppendVertex(Center + Radius * FVector2d(FMath::Cos(Theta), FMath::Sin(Theta)));
			}
			const TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Cutter)};
			const TArray<FGeneralPolygon2d> Before = Surface;
			Stages.Reset();
			TArray<FGeneralPolygon2d> SurfaceCandidate;
			if (!PolygonsDifference(Surface, Cutters, SurfaceCandidate))
			{
				AddError(TEXT("Surface-model Boolean failed before diagnosis"));
				return false;
			}
			TArray<FPolygon2d> Detached;
			SurfaceCandidate.RemoveAll([&](const FGeneralPolygon2d& Region)
			{
				if (!TouchesWall(Region.GetOuter()))
				{
					Detached.Add(Region.GetOuter());
					return true;
				}
				return false;
			});
			Stages.Add({TEXT("SurfaceRegions"), SurfaceCandidate});
			Surface = MoveTemp(SurfaceCandidate);
			FString Error;
			FKAggregateGeom BuiltCollision;
			const bool bBuilt = Simple.BuildMesh(Surface, 1.0, BuiltCollision, Error);
			const FGeometryStage* CollisionStage = Stages.FindByPredicate([](const FGeometryStage& Stage)
			{
				return Stage.Name == TEXT("CollisionRegions");
			});
			if (!TestNotNull(TEXT("Actual collision regions were observed before triangulation"), CollisionStage))
			{
				AddError(Error);
				return false;
			}
			if (!bBuilt)
			{
				++BuildFailureCases;
				Report += FString::Printf(TEXT("BUILD_FAILURE scenario=%d step=%d error=%s\n"), Scenario, Step, *Error);
			}
			FString Details;
			bool bMissing = false;
			bool bCollapsed = false;
			bool bThin = false;
			bool bNoDuplicateFailure = false;
			for (int32 RegionIndex = 0; RegionIndex < CollisionStage->Regions.Num(); ++RegionIndex)
			{
				const FGeneralPolygon2d& Region = CollisionStage->Regions[RegionIndex];
				FDelaunay2 Delaunay;
				TArray<FVector2d> Vertices;
				TArray<FIndex3i> Triangles;
				const bool bTriangulated = Delaunay.Triangulate(Region, &Triangles, &Vertices, false);
				if (!bTriangulated)
				{
					bMissing |= Delaunay.GetResult() == FDelaunay2::EResult::MissingEdges;
					bNoDuplicateFailure |= Delaunay.GetResult() == FDelaunay2::EResult::MissingEdges && !Delaunay.HasDuplicates();
					Details += FString::Printf(TEXT("DELAUNAY region=%d result=%d duplicates=%d %s\n"),
						RegionIndex, int32(Delaunay.GetResult()), Delaunay.HasDuplicates(), *Summarize(MakeArrayView(&Region, 1)));
					Details += DescribeMissingEdges(Delaunay, Region);
					FDelaunay2 Remapped;
					Remapped.bAutomaticallyFixEdgesToDuplicateVertices = true;
					const bool bRemapped = Remapped.Triangulate(Region, &Triangles, &Vertices, false);
					Details += FString::Printf(TEXT("DUPLICATE_REMAP_EXPERIMENT success=%d result=%d\n"), bRemapped, int32(Remapped.GetResult()));
					continue;
				}
				if (Triangles.IsEmpty())
				{
					Details += FString::Printf(TEXT("NO_FACES region=%d %s\n"), RegionIndex, *Summarize(MakeArrayView(&Region, 1)));
				}
				for (const FIndex3i& Triangle : Triangles)
				{
					const FVector2d A = Vertices[Triangle.A];
					const FVector2d B = Vertices[Triangle.B];
					const FVector2d C = Vertices[Triangle.C];
					const FVector2f AF(A), BF(B), CF(C);
					const double DoubleCross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
					const double FloatCross = double(BF.X - AF.X) * double(CF.Y - AF.Y) - double(BF.Y - AF.Y) * double(CF.X - AF.X);
					if (FMath::Abs(DoubleCross) < 1.e-5 || FloatCross == 0)
					{
						bCollapsed |= DoubleCross != 0 && FloatCross == 0;
						TArray<FVector3f> Prism;
						for (float Depth : {-50.f, 50.f})
						{
							for (const FVector2f& Point : {AF, BF, CF})
							{
								Prism.Add(FVector3f(Point.X, Depth, Point.Y));
							}
						}
						TConvexHull3<float> Hull;
						const bool bHull = Hull.Solve<FVector3f>(Prism);
						bThin |= !bHull;
						const FPlane4f Plane(Prism[0], Prism[1], Prism[2]);
						Details += FString::Printf(TEXT("THIN_PRISM region=%d triangle=(%d,%d,%d) double_area=%.17g float_area=%.17g hull_success=%d dimension=%d plane_normal=(%.9g,%.9g,%.9g)\n"),
							RegionIndex, Triangle.A, Triangle.B, Triangle.C, FMath::Abs(DoubleCross) * .5, FMath::Abs(FloatCross) * .5, bHull, Hull.GetDimension(), Plane.X, Plane.Y, Plane.Z);
						for (const FVector2d& Point : {A, B, C})
						{
							Details += FString::Printf(TEXT("  DOUBLE %.17g %.17g FLOAT %.9g %.9g\n"), Point.X, Point.Y, float(Point.X), float(Point.Y));
						}
					}
				}
			}
			MissingCases += int32(bMissing);
			CollapsedCases += int32(bCollapsed);
			ThinHullCases += int32(bThin);
			if (bMissing)
			{
				Report += FString::Printf(TEXT("MISSING_CASE scenario=%d step=%d without_duplicates=%d\n"), Scenario, Step, bNoDuplicateFailure);
			}
			if ((bMissing && !bSavedMissing) || (bCollapsed && !bSavedCollapsed) || (bThin && !bSavedThin)
				|| (bNoDuplicateFailure && !bSavedNoDuplicateFailure) || (!bBuilt && !bSavedBuildFailure))
			{
				Details += DifferenceControl(TEXT("NewCutterOnly"), Before, Cutters);
				for (const FGeometryStage& Stage : Stages)
				{
					if (Stage.Name == TEXT("IncomingWithCachedHoles") || Stage.Name == TEXT("Union") || Stage.Name == TEXT("SafeIntersection"))
					{
						Details += DifferenceControl(*Stage.Name, Before, Stage.Regions);
					}
					if (Stage.Name == TEXT("SafeIntersection"))
					{
						const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(Outer)};
						Details += DifferenceControl(TEXT("FromOriginalWall"), Original, Stage.Regions);
					}
				}
				FString Snapshot = FString::Printf(TEXT("SCENARIO %d STEP %d DETACHED %d\n"), Scenario, Step, Detached.Num());
				Snapshot += Details;
				Snapshot += TEXT("STAGE Cutter\n");
				AppendRegions(Snapshot, Cutters);
				Snapshot += TEXT("STAGE RemainingBefore\n");
				AppendRegions(Snapshot, Before);
				for (const FGeometryStage& Stage : Stages)
				{
					Snapshot += FString::Printf(TEXT("STAGE %s %s\n"), *Stage.Name, *Summarize(Stage.Regions));
					AppendRegions(Snapshot, Stage.Regions);
				}
				const FString Filename = FString::Printf(TEXT("scenario%d_step%d.txt"), Scenario, Step);
				TestTrue(TEXT("Failure inputs saved at double precision"), FFileHelper::SaveStringToFile(Snapshot, *(Directory / Filename)));
				Report += FString::Printf(TEXT("%s missing=%d collapsed=%d thin_hull=%d\n%s"), *Filename, bMissing, bCollapsed, bThin, *Details);
				bSavedMissing |= bMissing;
				bSavedCollapsed |= bCollapsed;
				bSavedThin |= bThin;
				bSavedNoDuplicateFailure |= bNoDuplicateFailure;
				bSavedBuildFailure |= !bBuilt;
			}
		}
	}
	Report += FString::Printf(TEXT("TOTAL missing_steps=%d float_collapsed_steps=%d thin_hull_steps=%d build_failure_steps=%d\n"), MissingCases, CollapsedCases, ThinHullCases, BuildFailureCases);
	TestTrue(TEXT("Diagnosis report saved"), FFileHelper::SaveStringToFile(Report, *(Directory / TEXT("report.txt"))));
	AddInfo(FString::Printf(TEXT("Diagnosis saved to %s; missing_steps=%d, collapsed_steps=%d, thin_hull_steps=%d"),
		*Directory, MissingCases, CollapsedCases, ThinHullCases));
	if (BuildFailureCases > 0 || CollapsedCases > 0 || ThinHullCases > 0)
	{
		AddWarning(FString::Printf(TEXT("Residual geometry failures: build_steps=%d, collapsed_steps=%d, thin_hull_steps=%d. See diagnosis report."), BuildFailureCases, CollapsedCases, ThinHullCases));
	}
	return true;
}
#endif
