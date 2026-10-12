// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BreachableSurfaceComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Materials/Material.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "Engine/DamageEvents.h"

#include "BreachSystemLog.h"

#include "Components/LineBatchComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableSurface, Log, All);

UBreachableSurfaceComponent::UBreachableSurfaceComponent()
{
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

void UBreachableSurfaceComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeSurface();
}

void UBreachableSurfaceComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	HandleBreachDamage(this, DamageEvent, DamageCauser);
}

bool UBreachableSurfaceComponent::InitializeSurface()
{
	// 1. 원본과 수정 가능 여부를 검사한다. 실패하면 현재 표면은 그대로 유지한다.
	if (!IsValid(SourceStaticMesh) || !IsEditable())
	{
		return false;
	}

	// 2. 원본의 LOD0 렌더 데이터를 가져온다. 이 버퍼를 읽어 DynamicMesh로 복사한다.
	const FStaticMeshRenderData* RenderData = SourceStaticMesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
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
	UE::Geometry::FDynamicMesh3 Mesh;
	const UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::ConversionOptions Options;
	if (!UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::Convert(&LOD, Options, Mesh)
		|| Mesh.TriangleCount() == 0)
	{
		return false;
	}

	// 4. 로컬 경계에서 치수를 구한다. 컴포넌트의 월드 스케일은 이 치수에 포함하지 않는다.
	const UE::Geometry::FAxisAlignedBox3d MeshBounds = Mesh.GetBounds();
	FBreachSurfaceState InitialState;
	InitialState.Size.Width = MeshBounds.Max.X - MeshBounds.Min.X;
	InitialState.Size.Height = MeshBounds.Max.Y - MeshBounds.Min.Y;
	InitialState.Size.Thickness = MeshBounds.Max.Z - MeshBounds.Min.Z;
	if (InitialState.Size.Width <= UE_SMALL_NUMBER || InitialState.Size.Height <= UE_SMALL_NUMBER)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s must have non-zero dimensions in local XY."), *SourceStaticMesh->GetName());
		return false;
	}

	// 5. 아직 구멍이 없으므로 남은 영역은 사각형 하나다. 피벗이 중앙이 아니어도 원래 좌표를 쓴다.
	// 사각형 판이라는 전제에서 XY 경계를 사용하며, 임의 메시의 윤곽을 추출하는 과정은 아니다.
	UE::Geometry::FPolygon2d Outer;
	Outer.AppendVertex(FVector2d(MeshBounds.Min.X, MeshBounds.Min.Y));
	Outer.AppendVertex(FVector2d(MeshBounds.Max.X, MeshBounds.Min.Y));
	Outer.AppendVertex(FVector2d(MeshBounds.Max.X, MeshBounds.Max.Y));
	Outer.AppendVertex(FVector2d(MeshBounds.Min.X, MeshBounds.Max.Y));
	InitialState.RemainingRegions.Add(UE::Geometry::FGeneralPolygon2d(Outer));

	// 6. 복사한 삼각형의 머티리얼 ID와 대응하도록 원본의 슬롯 순서를 유지한다.
	TArray<UMaterialInterface*> Materials;
	for (int32 Index = 0; Index < SourceStaticMesh->GetStaticMaterials().Num(); ++Index)
	{
		Materials.Add(SourceStaticMesh->GetMaterial(Index));
	}

	// 7. 준비가 끝난 뒤에 원본과 파괴 상태를 교체한다. Reset은 이 원본으로 다시 초기화한다.
	//SourceStaticMesh = SourceStaticMesh;
	bInitializedFromSize = false;
	SurfaceState = MoveTemp(InitialState);
	ConfigureMaterialSet(Materials);
	SetTangentsType(EDynamicMeshComponentTangentsMode::ExternallyProvided);

	// 8. 이전 단순 충돌을 지우고 현재 삼각형을 충돌로 사용한다. SetMesh가 렌더 갱신도 요청한다.
	ClearSimpleCollisionShapes(false);
	SetComplexAsSimpleCollisionEnabled(true, false);
	SetMesh(MoveTemp(Mesh));
	UpdateCollision();
	return true;
}

bool UBreachableSurfaceComponent::InitializeSurface(double Width, double Height, double Thickness)
{
	// 1. 치수는 cm 단위이며 모두 양수여야 한다. 잘못된 입력으로 기존 표면을 덮어쓰지 않는다.
	if (!IsEditable() || !FMath::IsFinite(Width) || !FMath::IsFinite(Height) || !FMath::IsFinite(Thickness)
		|| Width <= UE_SMALL_NUMBER || Height <= UE_SMALL_NUMBER || Thickness <= UE_SMALL_NUMBER)
	{
		return false;
	}

	// 2. 원점 중심의 사각판을 만든다. Extents는 전체 치수가 아닌 반쪽 길이다.
	// 생성기는 앞뒤와 옆면을 포함한 삼각형 12개와 면별 노멀, UV를 준비한다.
	UE::Geometry::FMinimalBoxMeshGenerator Generator;
	Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::ZeroVector, FVector3d(Width, Height, Thickness) * 0.5);
	UE::Geometry::FDynamicMesh3 Mesh(&Generator.Generate());

	// 3. 3D 메시와 별도로 파괴 계산에 사용할 XY 사각형을 만든다. 시작 상태에는 구멍이 없다.
	FBreachSurfaceState InitialState;
	InitialState.Size.Width = Width;
	InitialState.Size.Height = Height;
	InitialState.Size.Thickness = Thickness;
	UE::Geometry::FPolygon2d Outer;
	Outer.AppendVertex(FVector2d(-Width * 0.5, -Height * 0.5));
	Outer.AppendVertex(FVector2d(Width * 0.5, -Height * 0.5));
	Outer.AppendVertex(FVector2d(Width * 0.5, Height * 0.5));
	Outer.AppendVertex(FVector2d(-Width * 0.5, Height * 0.5));
	InitialState.RemainingRegions.Add(UE::Geometry::FGeneralPolygon2d(Outer));

	// 4. 이 방식에는 원본 에셋이 없으므로 입력 치수를 저장해 Reset 때 같은 판을 다시 만든다.
	SourceStaticMesh = nullptr;
	bInitializedFromSize = true;
	InitialSize = InitialState.Size;
	SurfaceState = MoveTemp(InitialState);

	// 5. 기본 머티리얼을 적용하고 UV와 노멀로 탄젠트를 계산한 뒤 렌더 메시와 충돌을 갱신한다.
	const TArray<UMaterialInterface*> Materials = { UMaterial::GetDefaultMaterial(MD_Surface) };
	ConfigureMaterialSet(Materials);
	SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
	ClearSimpleCollisionShapes(false);
	SetComplexAsSimpleCollisionEnabled(true, false);
	SetMesh(MoveTemp(Mesh));
	UpdateCollision();
	return true;
}

bool UBreachableSurfaceComponent::InitializeSurfaceFromSize(double Width, double Height, double Thickness)
{
	return InitializeSurface(Width, Height, Thickness);
}

bool UBreachableSurfaceComponent::ResetSurface()
{
	// 치수로 만들었으면 저장한 치수로 재생성하고, 에셋으로 만들었으면 원본 에셋을 다시 복사한다.
	if (bInitializedFromSize)
	{
		return InitializeSurface(InitialSize.Width, InitialSize.Height, InitialSize.Thickness);
	}

	return InitializeSurface();
}

void UBreachableSurfaceComponent::HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser)
{
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		DebugDrawSurface();
		
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);

		FVector2D SurfacePoint = WorldToSurface(PointDamage.HitInfo.ImpactPoint);
		SLOG(TEXT("SurfacePoint : %s"), *SurfacePoint.ToString());
		UWorld* World = GetWorld();
		if (World)
		{
			for (auto& RemainingRegion : SurfaceState.RemainingRegions)
			{
				const TArray<FVector2D>& OuterVertices = RemainingRegion.GetOuter().GetVertices();
				for (const FVector2D& Vertex : OuterVertices)
				{
					const FVector WorldVertex = SurfaceToWorld(Vertex);
					DrawDebugLine(World, PointDamage.HitInfo.ImpactPoint, WorldVertex, FColor::Green, false, 2.f, SDPG_Foreground, 2.0f);

				}
			}
		}

		// Trace의 표면 바깥 법선을 벽이 받는 이동 충돌의 법선 방향에 맞춘다.
		//FHitResult BreachHit = PointDamage.HitInfo;
		//BreachHit.Normal = -BreachHit.Normal;
		//BreachHit.ImpactNormal = -BreachHit.ImpactNormal;
	}
}

void UBreachableSurfaceComponent::DebugDrawSurface()
{
	UWorld* World = GetWorld();
	if (World)
	{
		for (auto& RemainingRegion : SurfaceState.RemainingRegions)
		{
			const TArray<FVector2D>& OuterVertices = RemainingRegion.GetOuter().GetVertices();
			for (int32 i = 0; i < OuterVertices.Num(); ++i)
			{
				const FVector Start = SurfaceToWorld(OuterVertices[i]);
				const FVector End = SurfaceToWorld(OuterVertices[(i + 1) % OuterVertices.Num()]);
				DrawDebugLine(World, Start, End, FColor::Green, false, 2.f, SDPG_Foreground, 2.0f);
			}

			const TArray <UE::Geometry::FPolygon2d> Holes = RemainingRegion.GetHoles();
			for (auto& Hole : Holes)
			{
				const TArray<FVector2D>& HoleVertices = Hole.GetVertices();
				for (int32 i = 0; i < HoleVertices.Num(); ++i)
				{
					const FVector Start = SurfaceToWorld(HoleVertices[i]);
					const FVector End = SurfaceToWorld(HoleVertices[(i + 1) % HoleVertices.Num()]);
					DrawDebugLine(World, Start, End, FColor::Green, false, 2.f, SDPG_Foreground, 2.0f);
				}
			}
		}

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

FVector2D UBreachableSurfaceComponent::WorldToSurface(const FVector& InWorldPoint)
{
	const FVector LocalPoint = GetComponentTransform().InverseTransformPosition(InWorldPoint);
	return FVector2D(LocalPoint.X, LocalPoint.Z);
}

FVector UBreachableSurfaceComponent::SurfaceToWorld(const FVector2D& InSurfacePoint)
{
	const FVector LocalPoint(InSurfacePoint.X, 0.0, InSurfacePoint.Y);
	return GetComponentTransform().TransformPosition(LocalPoint);
}

