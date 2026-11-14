#include "Surface/BreachDecorationGeometry.h"
#include "Surface/BreachSurfaceGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"

using namespace UE::Geometry;

DEFINE_LOG_CATEGORY_STATIC(LogBreachDecorationGeometry, Log, All);

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
		return !Scale.ContainsNaN() && FMath::Abs(Scale.X) > UE_DOUBLE_SMALL_NUMBER && FMath::Abs(Scale.Y) > UE_DOUBLE_SMALL_NUMBER && FMath::Abs(Scale.Z) > UE_DOUBLE_SMALL_NUMBER;
	}

}

bool BreachDecoration::ClipPatches(TArray<FPatch>& Patches, TConstArrayView<FGeneralPolygon2d> Remaining, TConstArrayView<FAxisAlignedBox2d> Cuts, EBreachSurfaceSide Side, const FVector& Scale, double Padding, FString& Error)
{
	if (!ValidScale(Scale) || !FMath::IsFinite(Padding) || Padding < 0)
	{
		Error = TEXT("Deco requires a finite nonzero surface scale and nonnegative padding.");
		return false;
	}
	TArray<FPatch> Updated;
	for (const FPatch& Patch : Patches)
	{
		FPatch Clipped = Patch;
		if (!PolygonsIntersection(Patch.Regions, Remaining, Clipped.Regions))
		{
			Error = TEXT("Existing cut decoration clipping failed.");
			return false;
		}
		if (!Clipped.Regions.IsEmpty())
		{
			Updated.Add(MoveTemp(Clipped));
		}
	}
	for (const FAxisAlignedBox2d& Cut : Cuts)
	{
		FPatch Patch;
		Patch.Side = Side;
		const FVector2d Margin(Padding / FMath::Abs(Scale.X), Padding / FMath::Abs(Scale.Z));
		Patch.UVBounds = FAxisAlignedBox2d(Cut.Min - Margin, Cut.Max + Margin);
		const TArray<FGeneralPolygon2d> Footprint = {FGeneralPolygon2d(Rectangle(Patch.UVBounds))};
		if (!PolygonsIntersection(Footprint, Remaining, Patch.Regions))
		{
			Error = TEXT("New cut decoration clipping failed.");
			return false;
		}
		TArray<FGeneralPolygon2d> Occupied;
		for (const FPatch& Existing : Updated)
		{
			if (Existing.Side == Side)
			{
				Occupied.Append(Existing.Regions);
			}
		}
		if (!Occupied.IsEmpty())
		{
			TArray<FGeneralPolygon2d> Uncovered;
			if (!PolygonsDifference(Patch.Regions, Occupied, Uncovered))
			{
				Error = TEXT("Cut decoration overlap removal failed.");
				return false;
			}
			Patch.Regions = MoveTemp(Uncovered);
		}
		if (!Patch.Regions.IsEmpty())
		{
			Updated.Add(MoveTemp(Patch));
		}
	}
	Patches = MoveTemp(Updated);
	return true;
}

bool BreachDecoration::BuildPatchMesh(TConstArrayView<FPatch> Patches, EBreachSurfaceSide Side, double Depth, FDynamicMesh3& OutMesh, FString& Error)
{
	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(1);
	auto* UVs = Mesh.Attributes()->PrimaryUV();
	for (const FPatch& Patch : Patches)
	{
		if (Patch.Side != Side)
		{
			continue;
		}
		const FVector2d Size = Patch.UVBounds.Max - Patch.UVBounds.Min;
		if (Size.X <= 0 || Size.Y <= 0)
		{
			Error = TEXT("Cut decoration has an empty UV footprint.");
			return false;
		}
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
				IDs.Add(Mesh.AppendVertex(FVector3d(P.X, Depth, P.Y)));
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
			}
		}
	}
	FMeshNormals::InitializeMeshToPerTriangleNormals(&Mesh);
	OutMesh = MoveTemp(Mesh);
	return true;
}
