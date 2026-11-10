#include "Surface/BreachSimpleCollision.h"
#include "CompGeom/ConvexHull2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Settings/BreachSystemSettings.h"

#include "BreachSurfaceGeometry.h"

#if WITH_EDITOR
#include "Surface/BreachSimpleCollisionDiagnostics.h"
#include "Misc/Paths.h"
#endif

namespace
{
	using namespace UE::Geometry;

	FPolygon2d PolygonFromIndices(TConstArrayView<int32> Indices, TConstArrayView<FVector2d> Vertices)
	{
		FPolygon2d Polygon;
		for (int32 Index : Indices)
		{
			Polygon.AppendVertex(Vertices[Index]);
		}

		return Polygon;
	}

	bool MergeConvex(TConstArrayView<int32> A, TConstArrayView<int32> B,
		TConstArrayView<FVector2d> Vertices, TArray<int32>& OutIndices)
	{
		TArray<int32> Indices(A);
		for (int32 Index : B)
		{
			Indices.AddUnique(Index);
		}

		TArray<FVector2d> Points;
		for (int32 Index : Indices)
		{
			Points.Add(Vertices[Index]);
		}

		TConvexHull2<double> Hull;
		if (!Hull.Solve(Points))
		{
			return false;
		}

		TArray<int32> Merged;
		for (int32 Index : Hull.GetPolygonIndices())
		{
			Merged.Add(Indices[Index]);
		}

		const double CombinedArea = FMath::Abs(PolygonFromIndices(A, Vertices).SignedArea())
			+ FMath::Abs(PolygonFromIndices(B, Vertices).SignedArea());
		const double HullArea = FMath::Abs(PolygonFromIndices(Merged, Vertices).SignedArea());

		// 인접 조각의 합집합이 이미 볼록할 때만 병합한다. 구멍을 가로질러 덮지 않는다.
		if (FMath::Abs(HullArea - CombinedArea) > FMath::Max(1.e-8, CombinedArea * 1.e-10))
		{
			return false;
		}

		OutIndices = MoveTemp(Merged);
		return true;
	}
}

void FBreachSimpleCollision::Initialize(const FPolygon2d& Outer, const FVector2d& Depth)
{
	OriginalOuter = Outer;
	DepthRange = Depth;
#if WITH_EDITOR
	bFailureSnapshotSaved = false;
#endif
}

bool FBreachSimpleCollision::BuildMesh(TConstArrayView<FGeneralPolygon2d> RemainingRegions, const double ComponentArea, FKAggregateGeom& OutCollision, FString& OutError) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | BuildConvex");
	OutError.Reset();

	const double AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	const double Bias = 1.e-7;
	if (OriginalOuter.VertexCount() < 3 || !FMath::IsFinite(AreaThreshold) || AreaThreshold <= 0)
	{
		OutError = TEXT("Simple collision requires an initialized surface and a positive area threshold.");
		return false;
	}

	// 모든 구멍들을 가져온다
	const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(OriginalOuter)};
	TArray<FGeneralPolygon2d> Holes;
	const bool bOpeningsBuilt = PolygonsDifference(Original, RemainingRegions, Holes);
	if (!bOpeningsBuilt)
	{
		OutError = TEXT("Simple openings could not be derived from the damage regions.");
		return false;
	}

	//UE_LOG(LogTemp, Log, TEXT("Openings Count %d"), Holes.Num());
	TArray<FGeneralPolygon2d> SimpleCuts;
	for (FGeneralPolygon2d& Hole : Holes)
	{
		const double WorldArea = FMath::Abs(Hole.SignedArea()) * ComponentArea;
		// 임계보다 작은 구멍은 스킵
		if (WorldArea + Bias < AreaThreshold)
		{
			const double OriginalArea = FMath::Abs(OriginalOuter.SignedArea());
			if (WorldArea < OriginalArea - 1.e-7)
			{
				continue;
			}
		}

		FGeneralPolygon2d Simplified = Hole;
		//Simplified.Simplify(0.0, 1.0);

		// 단순화한 구멍이 원래 구멍 밖으로 나가지 않도록 잘라내는 작업
		TArray<FGeneralPolygon2d> SafeHoles;
		const bool bContained = PolygonsIntersection(MakeArrayView(&Simplified, 1), MakeArrayView(&Hole, 1), SafeHoles);
		if (!bContained)
		{
			OutError = TEXT("Simplified hole could not be confined to the actual Hole.");
			return false;
		}
		SimpleCuts.Append(MoveTemp(SafeHoles));
	}

	// 충돌은 원본 벽에서 파생한다. 전달받은 표면 영역은 변경하지 않는다.
	TArray<FGeneralPolygon2d> CollisionRegions(Original); 
#if WITH_EDITOR
	bool bFinalDifferenceApplied = false;
#endif
	if (!SimpleCuts.IsEmpty())
	{
		const bool bCollisionBuilt = PolygonsDifference(Original, SimpleCuts, CollisionRegions);
#if WITH_EDITOR
		bFinalDifferenceApplied = true;
#endif
		if (!bCollisionBuilt)
		{
			OutError = TEXT("Simple collision regions could not be built from the original wall.");
			return false;
		}

		const bool bBoundariesResolved = BreachSurfaceGeometry::ResolveBoundaryOverlaps(CollisionRegions, OutError);
		if (!bBoundariesResolved)
		{
			return false;
		}
	}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	ObserveGeometry(TEXT("CollisionRegions"), CollisionRegions);
#endif

	FKAggregateGeom Collision;
	for (int32 RegionIndex = 0; RegionIndex < CollisionRegions.Num(); ++RegionIndex)
	{
		const FGeneralPolygon2d& Region = CollisionRegions[RegionIndex];
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles;
		const bool bTriangulated = BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, OutError);
		if (!bTriangulated)
		{
#if WITH_EDITOR
			if (!bFailureSnapshotSaved)
			{
				const BreachSimpleCollisionDiagnostics::FFailureSnapshot Snapshot{OriginalOuter, DepthRange, RemainingRegions, Holes, SimpleCuts, CollisionRegions, AreaThreshold, Bias, bFinalDifferenceApplied, RegionIndex, OutError};
				FString SnapshotPath;
				bFailureSnapshotSaved = BreachSimpleCollisionDiagnostics::SaveSnapshot(Snapshot, FPaths::ProjectSavedDir() / TEXT("Diagnostics/SimpleCollisionFailures"), SnapshotPath);
				if (bFailureSnapshotSaved)
				{
					UE_LOG(LogTemp, Warning, TEXT("[BreachSimpleDiag] Failure snapshot saved: %s"), *SnapshotPath);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[BreachSimpleDiag] Failed to save collision failure snapshot under Saved/Diagnostics/SimpleCollisionFailures."));
				}
			}
#endif
			return false;
		}

		TArray<TArray<int32>> Parts;
		for (const FIndex3i& Triangle : Triangles)
		{
			TArray<int32> Part = {Triangle.A, Triangle.B, Triangle.C};
			Parts.Add(MoveTemp(Part));
		}

	/*	bool bMerged = true;
		while (bMerged)
		{
			bMerged = false;
			TMap<FIntPoint, int32> EdgeOwners;
			for (int32 Index = 0; Index < Parts.Num() && !bMerged; ++Index)
			{
				for (int32 Edge = 0; Edge < Parts[Index].Num(); ++Edge)
				{
					const int32 A = Parts[Index][Edge];
					const int32 B = Parts[Index][(Edge + 1) % Parts[Index].Num()];
					const FIntPoint Key(FMath::Min(A, B), FMath::Max(A, B));
					const int32* Other = EdgeOwners.Find(Key);
					if (Other)
					{
						TArray<int32> Merged;
						if (MergeConvex(Parts[*Other], Parts[Index], Vertices, Merged))
						{
							Parts[*Other] = MoveTemp(Merged);
							Parts[Index].Reset();
							bMerged = true;
							break;
						}
					}
					else
					{
						EdgeOwners.Add(Key, Index);
					}
				}
			}
		}*/

		for (const TArray<int32>& Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}

			FKConvexElem& Convex = Collision.ConvexElems.AddDefaulted_GetRef();
			for (double Depth : {DepthRange.X, DepthRange.Y})
			{
				for (int32 Index : Part)
				{
					Convex.VertexData.Add(FVector(Vertices[Index].X, Depth, Vertices[Index].Y));
				}
			}

			Convex.UpdateElemBox();
		}
	}

	OutCollision = MoveTemp(Collision);
	return true;
}
