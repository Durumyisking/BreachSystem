// UBreachableSurfaceComponent의 Editor 전용 디버그 조회와 표시. 런타임 파손 상태를 만들거나 바꾸지 않는다.

#include "Components/BreachableSurfaceComponent.h"

#if WITH_EDITOR
#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachSurfaceSource.h"
#include "Components/LineBatchComponent.h"
#include "Data/BreachSystemSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/BreachSurfaceGeometry.h"

bool UBreachableSurfaceComponent::GetCellGridForDebug(FBreachCellGrid& OutGrid, UE::Geometry::FAxisAlignedBox2d& OutLocalBounds, FVector2d& OutDepthRange) const
{
	OutGrid = FBreachCellGrid();
	const FVector Scale = GetComponentScale();
	const FVector2d MetricScale(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	FString Error;
	if (IsValid(SurfaceProxy))
	{
		OutLocalBounds = Layout.OriginalBounds;
		OutDepthRange = Layout.OriginalDepthRange;
		return OutGrid.Configure(Layout.SurfaceUVs[0].Offset, Layout.SurfaceUVs[0].PerX, Layout.SurfaceUVs[0].PerZ,
			MetricScale, Layout.CapturedCellSize, Error);
	}

	UStaticMesh* MeshAsset = GetStaticMesh();
	if (!IsValid(MeshAsset))
	{
		return false;
	}
	const void* RenderData = MeshAsset->GetRenderData();
	if (DebugSourceMesh.Get() != MeshAsset || DebugRenderData != RenderData)
	{
		DebugSourceMesh = MeshAsset;
		DebugRenderData = RenderData;
		UE::Geometry::FDynamicMesh3 Mesh;
		bDebugSourceValid = BreachSurfaceSource::ReadSurface(MeshAsset, Mesh, DebugUVs);
		if (bDebugSourceValid)
		{
			const UE::Geometry::FAxisAlignedBox3d MeshBounds = Mesh.GetBounds();
			DebugBounds = UE::Geometry::FAxisAlignedBox2d(FVector2d(MeshBounds.Min.X, MeshBounds.Min.Z), FVector2d(MeshBounds.Max.X, MeshBounds.Max.Z));
			DebugDepth = FVector2d(MeshBounds.Min.Y, MeshBounds.Max.Y);
		}
	}
	if (!bDebugSourceValid)
	{
		return false;
	}
	OutLocalBounds = DebugBounds;
	OutDepthRange = DebugDepth;
	return OutGrid.Configure(DebugUVs[0].Offset, DebugUVs[0].PerX, DebugUVs[0].PerZ,
		MetricScale, UBreachSystemSettings::GetCellSizeCm(), Error);
}

void UBreachableSurfaceComponent::DebugDrawSurface()
{
	// 상태 확인: 월드가 있을 때만 남은 표면 영역을 그린다.
	UWorld* World = GetWorld();
	if (World)
	{
		const FTransform& ComponentTransform = GetComponentTransform();
		BreachSurfaceGeometry::DebugDrawPolygon2ds(this, ComponentTransform, Model.SurfaceState.RemainingRegions);

#pragma region LineBatch방식
		/*for (auto& RemainingRegion : SurfaceState.RemainingRegions)
		{
			TArray<FBatchedLine> Lines;

			const TArray<FVector2D>& OuterVertices = RemainingRegion.GetOuter().GetVertices();
			for (int32 i = 0; i < OuterVertices.Num(); ++i)
			{
				const FVector Start = SurfaceToWorld(OuterVertices[i]);
				const FVector End = SurfaceToWorld(OuterVertices[(i + 1) % OuterVertices.Num()]);
				Lines.Emplace(Start, End, FLinearColor::Green, 2.f, 2.f, SDPG_Foreground, 0);
			}

			const TArray<UE::Geometry::FPolygon2d> Holes = RemainingRegion.GetHoles();
			for (auto& Hole : Holes)
			{
				const TArray<FVector2D>& HoleVertices = Hole.GetVertices();
				for (int32 i = 0; i < HoleVertices.Num(); ++i)
				{
					const FVector Start = SurfaceToWorld(HoleVertices[i]);
					const FVector End = SurfaceToWorld(HoleVertices[(i + 1) % HoleVertices.Num()]);
					Lines.Emplace(Start, End, FLinearColor::Green, 2.f, 2.f, SDPG_Foreground, 0);
				}
			}

			if (!Lines.IsEmpty())
			{
				if (ULineBatchComponent* LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::ForegroundPersistent))
				{
					LineBatcher->DrawLines(Lines);
				}
			}
		}*/
#pragma endregion


	}
}
#endif
