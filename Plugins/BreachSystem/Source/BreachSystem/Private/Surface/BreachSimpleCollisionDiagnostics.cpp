#include "Surface/BreachSimpleCollisionDiagnostics.h"

#if WITH_EDITOR
#include "CompGeom/Delaunay2.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	using namespace UE::Geometry;

	void AppendStage(FString& Text, const TCHAR* Name, TConstArrayView<FGeneralPolygon2d> Regions)
	{
		Text += FString::Printf(TEXT("STAGE %s\n"), Name);
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

	void AppendTriangulationChecks(FString& Text, const TCHAR* Name, TConstArrayView<FGeneralPolygon2d> Regions)
	{
		for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
		{
			const FGeneralPolygon2d& Region = Regions[RegionIndex];
			FDelaunay2 Delaunay;
			TArray<FVector2d> Vertices;
			TArray<FIndex3i> Triangles;
			const bool bTriangulated = Delaunay.Triangulate(Region, &Triangles, &Vertices, false);
			Text += FString::Printf(TEXT("TRIANGULATION stage=%s region=%d replay=1 success=%d result=%d duplicates=%d vertices=%d triangles=%d\n"), Name, RegionIndex, bTriangulated, int32(Delaunay.GetResult()), Delaunay.HasDuplicates(), Vertices.Num(), Triangles.Num());
			if (Delaunay.GetResult() != FDelaunay2::EResult::MissingEdges)
			{
				continue;
			}

			int32 Offset = 0;
			auto InspectLoop = [&](const FPolygon2d& Loop, int32 LoopIndex)
			{
				for (int32 Index = 0; Index < Loop.VertexCount(); ++Index)
				{
					const int32 Next = (Index + 1) % Loop.VertexCount();
					const FIndex2i Edge(Offset + Index, Offset + Next);
					if (!Delaunay.HasEdge(Edge, false))
					{
						const FVector2d A = Loop[Index];
						const FVector2d B = Loop[Next];
						Text += FString::Printf(TEXT("MISSING stage=%s region=%d loop=%d edge=(%d,%d) A=(%.17g,%.17g) B=(%.17g,%.17g)\n"), Name, RegionIndex, LoopIndex, Edge.A, Edge.B, A.X, A.Y, B.X, B.Y);
					}
				}
				Offset += Loop.VertexCount();
			};
			InspectLoop(Region.GetOuter(), -1);
			for (int32 HoleIndex = 0; HoleIndex < Region.GetHoles().Num(); ++HoleIndex)
			{
				InspectLoop(Region.GetHoles()[HoleIndex], HoleIndex);
			}
		}
	}
}

FString BreachSimpleCollisionDiagnostics::SerializeSnapshot(const FFailureSnapshot& Snapshot)
{
	FString Text = TEXT("SCHEMA BreachSimpleCollisionFailure 1\nCOORDINATE_SPACE SurfaceLocalXZ\nEDGE_CHECKS ReplayWithFreshFDelaunay2\n");
	Text += FString::Printf(TEXT("ENGINE_VERSION %s\nAUTOMATION_TEST %d\nAREA_THRESHOLD %.17g\nBIAS %.17g\nDEPTH_RANGE %.17g %.17g\nFINAL_DIFFERENCE_APPLIED %d\nFAILED_REGION %d\nREASON %s\n"), *FEngineVersion::Current().ToString(), GIsAutomationTesting, Snapshot.AreaThreshold, Snapshot.Bias, Snapshot.DepthRange.X, Snapshot.DepthRange.Y, Snapshot.bFinalDifferenceApplied, Snapshot.FailedRegionIndex, *Snapshot.Error);
	const TArray<UE::Geometry::FGeneralPolygon2d> Original = {UE::Geometry::FGeneralPolygon2d(Snapshot.OriginalOuter)};
	AppendStage(Text, TEXT("Original"), Original);
	AppendStage(Text, TEXT("A_RemainingRegions"), Snapshot.RemainingRegions);
	AppendStage(Text, TEXT("B_Holes"), Snapshot.Holes);
	AppendStage(Text, TEXT("C_SimpleCuts"), Snapshot.SimpleCuts);
	AppendStage(Text, TEXT("D_CollisionRegions"), Snapshot.CollisionRegions);
	AppendTriangulationChecks(Text, TEXT("A_RemainingRegions"), Snapshot.RemainingRegions);
	AppendTriangulationChecks(Text, TEXT("B_Holes"), Snapshot.Holes);
	AppendTriangulationChecks(Text, TEXT("C_SimpleCuts"), Snapshot.SimpleCuts);
	AppendTriangulationChecks(Text, TEXT("D_CollisionRegions"), Snapshot.CollisionRegions);
	return Text;
}

bool BreachSimpleCollisionDiagnostics::SaveSnapshot(const FFailureSnapshot& Snapshot, const FString& Directory, FString& OutPath)
{
	OutPath.Reset();
	const FString CaptureDirectory = FPaths::ConvertRelativePathToFull(Directory / FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	if (!IFileManager::Get().MakeDirectory(*CaptureDirectory, true))
	{
		return false;
	}
	const FString Path = CaptureDirectory / TEXT("snapshot.txt");
	if (!FFileHelper::SaveStringToFile(SerializeSnapshot(Snapshot), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
	OutPath = Path;
	return true;
}
#endif
