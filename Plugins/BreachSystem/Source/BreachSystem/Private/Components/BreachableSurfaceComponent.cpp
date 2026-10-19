// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachSourceComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Materials/Material.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "Engine/DamageEvents.h"

#include "BreachSystemLog.h"

#include "Components/LineBatchComponent.h"

#include "Surface/BreachSurfaceGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/MeshNormals.h"

#include "Math/BreachSystemDelaunay.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableSurface, Log, All);

UBreachableSurfaceComponent::UBreachableSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

void UBreachableSurfaceComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (IsValid(DynamicMeshProxy))
	{
		DynamicMeshProxy->SetSurface(nullptr);
		DynamicMeshProxy->DestroyComponent();
		DynamicMeshProxy = nullptr;
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UBreachableSurfaceComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	HandleBreachDamage(this, DamageEvent, DamageCauser);
}

bool UBreachableSurfaceComponent::InitializeSurface()
{
	if (IsValid(DynamicMeshProxy))
	{
		return true;
	}

	// 1. 원본을 검사한다. 실패하면 현재 표면은 그대로 유지한다.
	UStaticMesh* SourceStaticMesh = GetStaticMesh();
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
	UE::Geometry::FDynamicMesh3 Mesh;
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
	InitialState.Size.Thickness = MeshBounds.Max.Y - MeshBounds.Min.Y;
	if (InitialState.Size.Width <= UE_SMALL_NUMBER || InitialState.Size.Height <= UE_SMALL_NUMBER)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s must have non-zero dimensions in local XZ."), *SourceStaticMesh->GetName());
		return false;
	}

	// 5. 아직 구멍이 없으므로 남은 영역은 사각형 하나다. 피벗이 중앙이 아니어도 원래 좌표를 쓴다.
	// 사각형 판이라는 전제에서 XZ 경계를 사용하며, 임의 메시의 윤곽을 추출하는 과정은 아니다.
	UE::Geometry::FPolygon2d Outer;
	Outer.AppendVertex(FVector2d(MeshBounds.Min.X, MeshBounds.Min.Z));
	Outer.AppendVertex(FVector2d(MeshBounds.Max.X, MeshBounds.Min.Z));
	Outer.AppendVertex(FVector2d(MeshBounds.Max.X, MeshBounds.Max.Z));
	Outer.AppendVertex(FVector2d(MeshBounds.Min.X, MeshBounds.Max.Z));
	InitialState.RemainingRegions.Add(UE::Geometry::FGeneralPolygon2d(Outer));
	OriginalOuter = Outer;
	OriginalVertices = Outer.GetVertices();
	OriginBounds = OriginalOuter.Bounds();

	// 6. 복사한 삼각형의 머티리얼 ID와 대응하도록 원본의 슬롯 순서를 유지한다.
	TArray<UMaterialInterface*> Materials;
	for (int32 Index = 0; Index < GetNumMaterials(); ++Index)
	{
		Materials.Add(GetMaterial(Index));
	}

	// 7. 첫 피격에서 DynamicMesh를 만들고 원본과 같은 위치에 부착한다.
	AActor* Owner = GetOwner();
	if (!ensureMsgf(Owner, TEXT("Breach surface requires an owner to create its proxy.")))
	{
		return false;
	}
	DynamicMeshProxy = NewObject<UBreachSurfaceProxy>(Owner);
	DynamicMeshProxy->SetSurface(this);
	DynamicMeshProxy->SetupAttachment(this);
	DynamicMeshProxy->SetRelativeTransform(FTransform::Identity);
	DynamicMeshProxy->SetMobility(GetMobility());
	DynamicMeshProxy->SetCastShadow(CastShadow);
	DynamicMeshProxy->SetCollisionObjectType(GetCollisionObjectType());
	DynamicMeshProxy->SetCollisionResponseToChannels(GetCollisionResponseToChannels());
	DynamicMeshProxy->ConfigureMaterialSet(Materials);
	DynamicMeshProxy->SetTangentsType(EDynamicMeshComponentTangentsMode::ExternallyProvided);
	DynamicMeshProxy->SetComplexAsSimpleCollisionEnabled(true, false);
	DynamicMeshProxy->SetMesh(MoveTemp(Mesh));
	DynamicMeshProxy->RegisterComponent();
	DynamicMeshProxy->UpdateCollision();
	SurfaceState = MoveTemp(InitialState);

	// 8. 원본의 표시와 충돌을 Proxy로 전환한다.
	OriginalCollisionEnabled = GetCollisionEnabled();
	bOriginalVisible = IsVisible();
	bOriginalHiddenInGame = bHiddenInGame;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetVisibility(false, false);
	DynamicMeshProxy->SetCollisionEnabled(OriginalCollisionEnabled);
	DynamicMeshProxy->SetVisibility(bOriginalVisible, false);
	DynamicMeshProxy->SetHiddenInGame(bOriginalHiddenInGame, false);
	return true;
}

bool UBreachableSurfaceComponent::ResetSurface()
{
	if (IsValid(DynamicMeshProxy))
	{
		DynamicMeshProxy->SetSurface(nullptr);
		DynamicMeshProxy->DestroyComponent();
		DynamicMeshProxy = nullptr;
		SetCollisionEnabled(OriginalCollisionEnabled);
		SetVisibility(bOriginalVisible, false);
		SetHiddenInGame(bOriginalHiddenInGame, false);
	}
	SurfaceState = FBreachSurfaceState();
	return true;
}
void UBreachableSurfaceComponent::HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser)
{
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		if (!IsValid(DamageCauser))
		{
			return;
		}
		UBreachSourceComponent* Source = DamageCauser->FindComponentByClass<UBreachSourceComponent>();
		if (!Source)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s has no BreachSourceComponent."), *DamageCauser->GetName());
			return;
		}
		if (!IsValid(DynamicMeshProxy))
		{
			if (!InitializeSurface())
			{
				return;
			}
		}

		//DebugDrawSurface();
		
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);

		const FTransform& ComponentTransform = GetComponentTransform();
		FGeneralPolygon2d Cutter;
		if (!Source->ResolveCutterPolygon(ComponentTransform, PointDamage.HitInfo.ImpactPoint, Cutter))
		{
			return;
		}
		TArray<FGeneralPolygon2d> Cutters;
		Cutters.Add(Cutter);
		PolygonsDifference(SurfaceState.RemainingRegions, Cutters, SurfaceState.RemainingRegions);

		SurfaceState.RemainingRegions.RemoveAll([&](const FGeneralPolygon2d& Region)
		{
			return !TouchesOriginalBorder(Region.GetOuter(), OriginBounds);
		});

		// SurfaceState기반으로 메시를 갱신한다.
		FDelaunay2 Delaunay;
		FDynamicMesh3 RenewedMesh;
		for (auto& RemainingRegion : SurfaceState.RemainingRegions)
		{
			TArray<FVector2d> Vertices;
			TArray<FIndex3i> Triangles; // 삼각분할 결과로 만들어진 삼각형들의 정점 인덱스를 저장하는 배열 (그냥 int 3개임)

			// 삼각분할
			if (!Delaunay.Triangulate(RemainingRegion, &Triangles, &Vertices, false))
			{
				continue;
			}
			TArray<int32> FrontIDs;
			TArray<int32> BackIDs;

			for (const FVector2d& Vertex : Vertices)
			{
				FrontIDs.Add(RenewedMesh.AppendVertex(FVector3d(Vertex.X, 0.0, Vertex.Y)));
				BackIDs.Add(RenewedMesh.AppendVertex(FVector3d(Vertex.X, SurfaceState.Size.Thickness, Vertex.Y)));
			}
			for (const FIndex3i& Triangle : Triangles)
			{
				RenewedMesh.AppendTriangle(FrontIDs[Triangle.A], FrontIDs[Triangle.C], FrontIDs[Triangle.B]);
				RenewedMesh.AppendTriangle(BackIDs[Triangle.A], BackIDs[Triangle.B], BackIDs[Triangle.C]);
			}

			// 사이드 면 채우기
			auto AppendSideFaces = [&](const FPolygon2d& Boundary, bool bIsHole, int32 VertexOffset)
			{
				const int32 Count = Boundary.VertexCount();
				const bool bReverse = Boundary.IsClockwise() != bIsHole;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					int32 A = VertexOffset + Index;
					int32 B = VertexOffset + (Index + 1) % Count;
					if (bReverse)
					{
						Swap(A, B);
					}
					RenewedMesh.AppendTriangle(FrontIDs[A], FrontIDs[B], BackIDs[B]);
					RenewedMesh.AppendTriangle(FrontIDs[A], BackIDs[B], BackIDs[A]);
				}
			};
			AppendSideFaces(RemainingRegion.GetOuter(), false, 0);
			int32 HoleVertexOffset = RemainingRegion.GetOuter().VertexCount();
			for (const FPolygon2d& Hole : RemainingRegion.GetHoles())
			{
				AppendSideFaces(Hole, true, HoleVertexOffset);
				HoleVertexOffset += Hole.VertexCount();
			}
		}

		UE::Geometry::FMeshNormals::InitializeMeshToPerTriangleNormals(&RenewedMesh);
		DynamicMeshProxy->SetMesh(MoveTemp(RenewedMesh));
		DynamicMeshProxy->UpdateCollision();

	}
}

void UBreachableSurfaceComponent::DebugDrawSurface()
{
	UWorld* World = GetWorld();
	if (World)
	{
		const FTransform& ComponentTransform = GetComponentTransform();
		BreachSurfaceGeometry::DebugDrawPolygon2ds(this, ComponentTransform, SurfaceState.RemainingRegions);

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

bool UBreachableSurfaceComponent::TouchesOriginalBorder(const UE::Geometry::FPolygon2d& Outer, const UE::Geometry::FAxisAlignedBox2d& Bounds)
{
	// 현재 outer의 Edge와 원본 outer의 Bound를 비교해 원본 경계와 닿지 않은 폴리곤들을 제거한다.
	constexpr double Epsilon = 0.001;

	const TArray<FVector2d>& Vertices = Outer.GetVertices();

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2d& A = Vertices[i];
		const FVector2d& B = Vertices[(i + 1) % Vertices.Num()];

		// 길이가 없는 에지는 제외
		if ((B - A).SizeSquared() <= Epsilon * Epsilon)
		{
			continue;
		}

		// 각 정점이 Bound변 위에 있는지 검사한다.
		const bool bLeft =
			FMath::IsNearlyEqual(A.X, Bounds.Min.X, Epsilon) &&
			FMath::IsNearlyEqual(B.X, Bounds.Min.X, Epsilon);

		const bool bRight =
			FMath::IsNearlyEqual(A.X, Bounds.Max.X, Epsilon) &&
			FMath::IsNearlyEqual(B.X, Bounds.Max.X, Epsilon);

		const bool bBottom =
			FMath::IsNearlyEqual(A.Y, Bounds.Min.Y, Epsilon) &&
			FMath::IsNearlyEqual(B.Y, Bounds.Min.Y, Epsilon);

		const bool bTop =
			FMath::IsNearlyEqual(A.Y, Bounds.Max.Y, Epsilon) &&
			FMath::IsNearlyEqual(B.Y, Bounds.Max.Y, Epsilon);

		if (bLeft || bRight || bBottom || bTop)
		{
			return true;
		}
	}

	return false;
}

