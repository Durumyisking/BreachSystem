#include "Components/BreachSurfaceSource.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/BreachSurfaceTypes.h"

#include "BreachSystemLog.h"

bool BreachSurfaceSource::ReadSurface(const UStaticMesh* SourceStaticMesh, UE::Geometry::FDynamicMesh3& Mesh, FPlanarUVMapping (&OutUVs)[2])
{
	// 1. 원본을 검사한다. 실패하면 현재 표면은 그대로 유지한다.
	if (!IsValid(SourceStaticMesh))
	{
		return false;
	}

	// 2. 원본의 LOD0 렌더 데이터를 가져온다. 이 버퍼를 읽어 DynamicMesh로 복사한다.
	const FStaticMeshRenderData* RenderData = SourceStaticMesh->GetRenderData();
	if (!RenderData)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s has no render data."), *SourceStaticMesh->GetName());
		return false;
	}

	if (RenderData->LODResources.Num() == 0)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s has no render LOD to initialize from."), *SourceStaticMesh->GetName());
		return false;
	}

	// GPU에만 남은 데이터는 읽을 수 없으므로 CPU 접근 가능한 버퍼인지 먼저 확인한다.
	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	if (!LOD.VertexBuffers.PositionVertexBuffer.GetAllowCPUAccess()
		|| !LOD.VertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess()
		|| !LOD.IndexBuffer.GetAllowCPUAccess())
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("Enable Allow CPU Access on %s before initializing a breach surface."), *SourceStaticMesh->GetName());
		return false;
	}

	// 3. 정점, 삼각형, 노멀, UV, 머티리얼 ID를 복사한다. 원본 에셋은 수정하지 않는다. (Mesh가 결과물이다)
	const UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::ConversionOptions Options;
	if (!UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::Convert(&LOD, Options, Mesh))
	{
		return false;
	}
	if (Mesh.TriangleCount() == 0)
	{
		return false;
	}

	// 4. 로컬 경계에서 치수를 구한다. 컴포넌트의 월드 스케일은 이 치수에 포함하지 않는다.
	const UE::Geometry::FAxisAlignedBox3d MeshBounds = Mesh.GetBounds();
	FBreachSurfaceState InitialState;
	InitialState.Size.Width = MeshBounds.Max.X - MeshBounds.Min.X;
	InitialState.Size.Height = MeshBounds.Max.Z - MeshBounds.Min.Z;

	// 검증: XZ 평면에서 유효한 면적을 가져야 한다.
	if (InitialState.Size.Width <= UE_DOUBLE_KINDA_SMALL_NUMBER || InitialState.Size.Height <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s must have non-zero dimensions in local XZ."), *SourceStaticMesh->GetName());
		return false;
	}

	// 사각 면의 UV = Offset + X * PerX + Z * PerZ. 원본에서 한 번만 구한다.
	const auto* SourceUV = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryUV() : nullptr;
	if (!SourceUV)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s needs UV0 to initialize the breach surface; keeping the static mesh."), *SourceStaticMesh->GetName());
		return false;
	}

	bool bFoundUVs[2] = { false, false };
	for (int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		const FVector3d Normal = Mesh.GetTriNormal(TriangleID);
		if (FMath::Abs(Normal.Y) < 0.99)
		{
			continue;
		}
		if (!SourceUV->IsSetTriangle(TriangleID))
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s has a surface triangle without UV0; keeping the static mesh."), *SourceStaticMesh->GetName());
			return false;
		}
		FVector3d A, B, C;
		Mesh.GetTriVertices(TriangleID, A, B, C);
		const UE::Geometry::FIndex3i Elements = SourceUV->GetTriangle(TriangleID);
		const FVector2d UV0(SourceUV->GetElement(Elements.A));
		const FVector2d UV1(SourceUV->GetElement(Elements.B));
		const FVector2d UV2(SourceUV->GetElement(Elements.C));
		const int32 Side = Normal.Y > 0 ? 1 : 0;
		FPlanarUVMapping& Mapping = OutUVs[Side];
		if (!bFoundUVs[Side])
		{
			const FVector3d AB = B - A, AC = C - A;
			const double Determinant = AB.X * AC.Z - AC.X * AB.Z;
			if (FMath::Abs(Determinant) <= UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Mapping.PerX = ((UV1 - UV0) * AC.Z - (UV2 - UV0) * AB.Z) / Determinant;
			Mapping.PerZ = ((UV2 - UV0) * AB.X - (UV1 - UV0) * AC.X) / Determinant;
			Mapping.Offset = UV0 - Mapping.PerX * A.X - Mapping.PerZ * A.Z;
			bFoundUVs[Side] = true;
		}

		// 검증: 한 면 안에 UV 이음선이나 서로 다른 배율이 있으면 단일 변환으로 보존할 수 없다.
		const FVector3d Positions[3] = { A, B, C };
		const FVector2d UVs[3] = { UV0, UV1, UV2 };
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector2d Expected = Mapping.Offset + Mapping.PerX * Positions[Corner].X + Mapping.PerZ * Positions[Corner].Z;
			if ((Expected - UVs[Corner]).Length() > UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s requires one linear UV mapping per surface face; keeping the static mesh."), *SourceStaticMesh->GetName());
				return false;
			}
		}
	}

	// 검증: 원본의 앞·뒷면 UV 변환을 모두 확보한 뒤 저장한다.
	if (!bFoundUVs[0] || !bFoundUVs[1])
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s needs UV0 on both XZ faces; keeping the static mesh."), *SourceStaticMesh->GetName());
		return false;
	}

	return true;
}
