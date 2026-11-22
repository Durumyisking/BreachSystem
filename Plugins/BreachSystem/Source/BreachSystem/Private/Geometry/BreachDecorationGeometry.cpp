#include "Geometry/BreachDecorationGeometry.h"
#include "Geometry/BreachSimpleCollision.h"
#include "Geometry/BreachSurfaceGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

using namespace UE::Geometry;

#include "BreachSystemLog.h"

namespace
{
	FPolygon2d Rectangle(const FAxisAlignedBox2d& Bounds)
	{
		FPolygon2d Polygon;
		Polygon.AppendVertex(Bounds.Min);
		Polygon.AppendVertex(FVector2d(Bounds.Max.X, Bounds.Min.Y));
		Polygon.AppendVertex(Bounds.Max);
		Polygon.AppendVertex(FVector2d(Bounds.Min.X, Bounds.Max.Y));
		return Polygon;
	}

	bool ValidScale(const FVector& Scale)
	{
		return !Scale.ContainsNaN() && FMath::Abs(Scale.X) > UE_DOUBLE_KINDA_SMALL_NUMBER && FMath::Abs(Scale.Y) > UE_DOUBLE_KINDA_SMALL_NUMBER && FMath::Abs(Scale.Z) > UE_DOUBLE_KINDA_SMALL_NUMBER;
	}

	double RegionArea(TConstArrayView<FGeneralPolygon2d> Regions)
	{
		double Area = 0;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			Area += FMath::Abs(Region.SignedArea());
		}
		return Area;
	}

	int32 FaceIndex(EBreachSurfaceSide Side)
	{
		return Side == EBreachSurfaceSide::Front ? 0 : 1;
	}

	// Boolean 연산은 호출마다 좌표 격자가 달라, 완전히 덮인 영역도 면적 없는 띠로 남을 수 있다. 그런 조각은 패치가 아니다.
	void RemoveEmptyRegions(TArray<FGeneralPolygon2d>& Regions)
	{
		Regions.RemoveAll([](const FGeneralPolygon2d& Region)
		{
			return FMath::Abs(Region.SignedArea()) <= UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER;
		});
	}
}

void BreachDecoration::PrepareDisplayMesh(FDynamicMesh3& Mesh)
{
	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}
	if (Mesh.Attributes()->NumUVLayers() < 1)
	{
		Mesh.Attributes()->SetNumUVLayers(1);
	}
	if (Mesh.Attributes()->NumNormalLayers() < 1)
	{
		Mesh.Attributes()->SetNumNormalLayers(1);
	}
}

void BreachDecoration::SetFlatNormal(FDynamicMesh3& Mesh, int32 TriangleID)
{
	check(Mesh.HasAttributes());
	FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	check(Normals);
	const FVector3f Normal(Mesh.GetTriNormal(TriangleID));
	FIndex3i Elements;
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		Elements[Corner] = Normals->AppendElement(Normal);
	}
	Normals->SetTriangle(TriangleID, Elements);
}

bool BreachDecoration::ClipPatches(FCutState& State, const FBreachSimpleCollision& Holes, TConstArrayView<FPolygon2d> Removed, TConstArrayView<FAxisAlignedBox2d> Cuts, EBreachSurfaceSide Side, const FVector& Scale, double Padding, FString& Error)
{
	if (!ValidScale(Scale) || !FMath::IsFinite(Padding) || Padding < 0)
	{
		Error = TEXT("Deco requires a finite nonzero surface scale and nonnegative padding.");
		return false;
	}
	if (!Holes.HasHoleCache())
	{
		Error = TEXT("Cut decoration requires the tracked openings of its surface.");
		return false;
	}

	// 준비: 이번 브리칭으로 사라진 도형과 그 경계.
	TArray<FGeneralPolygon2d> RemovedRegions;
	TArray<FAxisAlignedBox2d> RemovedBounds;
	for (const FPolygon2d& Polygon : Removed)
	{
		if (Polygon.VertexCount() < 3)
		{
			continue;
		}
		FPolygon2d Oriented = Polygon;
		if (Oriented.IsClockwise())
		{
			Oriented.Reverse();
		}
		RemovedBounds.Add(Oriented.Bounds());
		RemovedRegions.Emplace(MoveTemp(Oriented));
	}

	// 처리: 사라진 도형과 경계가 겹치는 기존 패치만 다시 자른다. 나머지는 도형 연산 없이 그대로 둔다.
	for (int32 Index = 0; Index < State.Patches.Num(); ++Index)
	{
		FPatch& Patch = State.Patches[Index];
		bool bTouched = false;
		for (const FAxisAlignedBox2d& Bounds : RemovedBounds)
		{
			if (Bounds.Intersects(Patch.UVBounds))
			{
				bTouched = true;
				break;
			}
		}
		if (!bTouched)
		{
			continue;
		}

		TArray<FGeneralPolygon2d> Clipped;
		if (!PolygonsDifference(Patch.Regions, RemovedRegions, Clipped))
		{
			Error = TEXT("Existing cut decoration clipping failed.");
			return false;
		}
		RemoveEmptyRegions(Clipped);

		// 경계만 겹치고 실제로 잘리지 않은 패치는 표시 메시도 그대로 둔다.
		const double Before = RegionArea(Patch.Regions);
		if (FMath::Abs(Before - RegionArea(Clipped)) <= FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER, Before * UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER))
		{
			continue;
		}

		State.StaleVertices[FaceIndex(Patch.Side)].Append(Patch.MeshVertices);
		Patch.MeshVertices.Reset();
		Patch.bInMesh = false;
		Patch.Regions = MoveTemp(Clipped);
	}
	State.Patches.RemoveAll([](const FPatch& Patch) { return Patch.Regions.IsEmpty(); });

	// 처리: 새 패치는 발자국에서 벽 밖, 발자국에 걸친 구멍, 같은 면의 주변 패치를 뺀다.
	const TArray<FGeneralPolygon2d> Wall = {FGeneralPolygon2d(Holes.GetOuter())};
	for (const FAxisAlignedBox2d& Cut : Cuts)
	{
		FPatch Patch;
		Patch.Side = Side;
		const FVector2d Margin(Padding / FMath::Abs(Scale.X), Padding / FMath::Abs(Scale.Z));
		Patch.UVBounds = FAxisAlignedBox2d(Cut.Min - Margin, Cut.Max + Margin);
		const TArray<FGeneralPolygon2d> Footprint = {FGeneralPolygon2d(Rectangle(Patch.UVBounds))};
		if (!PolygonsIntersection(Footprint, Wall, Patch.Regions))
		{
			Error = TEXT("New cut decoration clipping failed.");
			return false;
		}

		TArray<FGeneralPolygon2d> Blocked;
		Holes.ForEachHole([&](uint64 Revision, const FGeneralPolygon2d& Polygon, const FAxisAlignedBox2d& Bounds)
		{
			if (Bounds.Intersects(Patch.UVBounds))
			{
				Blocked.Add(Polygon);
			}
		});
		for (const FPatch& Existing : State.Patches)
		{
			if (Existing.Side == Side && Existing.UVBounds.Intersects(Patch.UVBounds))
			{
				Blocked.Append(Existing.Regions);
			}
		}
		if (!Blocked.IsEmpty() && !Patch.Regions.IsEmpty())
		{
			TArray<FGeneralPolygon2d> Uncovered;
			if (!PolygonsDifference(Patch.Regions, Blocked, Uncovered))
			{
				Error = TEXT("Cut decoration overlap removal failed.");
				return false;
			}
			Patch.Regions = MoveTemp(Uncovered);
		}
		RemoveEmptyRegions(Patch.Regions);
		if (!Patch.Regions.IsEmpty())
		{
			State.Patches.Add(MoveTemp(Patch));
		}
	}
	return true;
}

bool BreachDecoration::UpdatePatchMesh(FCutState& State, EBreachSurfaceSide Side, double Depth, FDynamicMesh3& Mesh, bool& bOutChanged, FString& Error)
{
	bOutChanged = false;
	PrepareDisplayMesh(Mesh);
	FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
	check(UVs);

	// 처리: 바뀌었거나 사라진 패치의 정점만 지운다. 정점에 붙은 삼각형과 UV·노멀도 함께 지워진다.
	TArray<int32>& Stale = State.StaleVertices[FaceIndex(Side)];
	for (int32 VertexID : Stale)
	{
		if (Mesh.IsVertex(VertexID))
		{
			Mesh.RemoveVertex(VertexID);
			bOutChanged = true;
		}
	}
	Stale.Reset();

	// 처리: 아직 메시에 없는 패치만 삼각분할해 추가한다.
	for (FPatch& Patch : State.Patches)
	{
		if (Patch.Side != Side || Patch.bInMesh)
		{
			continue;
		}
		const FVector2d Size = Patch.UVBounds.Max - Patch.UVBounds.Min;
		if (Size.X <= 0 || Size.Y <= 0)
		{
			Error = TEXT("Cut decoration has an empty UV footprint.");
			return false;
		}

		Patch.bInMesh = true;
		bOutChanged = true;
		for (const FGeneralPolygon2d& Region : Patch.Regions)
		{
			TArray<FVector2d> Vertices;
			TArray<FIndex3i> Triangles;
			if (!BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, Error))
			{
				// 잘못된 영역만 숨겨 다른 패치와 Feature 기록의 전체 초기화를 막는다.
				UE_LOG(LogBreachDecorationGeometry, Warning, TEXT("Skipped unsafe cut decoration region on face %d; other Deco is preserved: %s"), static_cast<int32>(Side), *Error);
				Error.Reset();
				continue;
			}
			TArray<int32> IDs;
			for (const FVector2d& P : Vertices)
			{
				const int32 VertexID = Mesh.AppendVertex(FVector3d(P.X, Depth, P.Y));
				IDs.Add(VertexID);
				Patch.MeshVertices.Add(VertexID);
			}
			for (FIndex3i Triangle : Triangles)
			{
				if (Side == EBreachSurfaceSide::Front)
				{
					Swap(Triangle.B, Triangle.C);
				}
				const int32 ID = Mesh.AppendTriangle(IDs[Triangle.A], IDs[Triangle.B], IDs[Triangle.C]);
				if (ID < 0)
				{
					Error = TEXT("Cut decoration triangle construction failed.");
					return false;
				}
				FIndex3i UVIndices;
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const FVector2d P = Vertices[Triangle[Corner]];
					UVIndices[Corner] = UVs->AppendElement(FVector2f((P.X - Patch.UVBounds.Min.X) / Size.X, (P.Y - Patch.UVBounds.Min.Y) / Size.Y));
				}
				UVs->SetTriangle(ID, UVIndices);
				SetFlatNormal(Mesh, ID);
			}
		}
	}
	return true;
}
