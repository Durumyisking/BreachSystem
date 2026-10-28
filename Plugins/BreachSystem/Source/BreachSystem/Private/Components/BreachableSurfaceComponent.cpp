// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachSourceComponent.h"
#include "Shape/BreachContour.h"
#include "GameFramework/Actor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Materials/Material.h"
#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "StaticMeshResources.h"
#include "Engine/DamageEvents.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "BreachSystemLog.h"

#include "Components/LineBatchComponent.h"

#include "Surface/BreachSurfaceGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/MeshNormals.h"

#include "Math/BreachSystemDelaunay.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableSurface, Log, All);

UBreachableSurfaceComponent::UBreachableSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

#if WITH_EDITOR
void UBreachableSurfaceComponent::PostLoad()
{
	Super::PostLoad();
	AccumulationShape.Contour.CacheJitterInterval();
}

void UBreachableSurfaceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	AccumulationShape.Contour.CacheJitterInterval();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

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

//void UBreachableSurfaceComponent::ReceiveComponentDamage_Implementation(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
//{
//	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
//	HandleBreachDamage(this, DamageEvent, DamageCauser);
//}
void UBreachableSurfaceComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	HandleBreachDamage(this, DamageEvent, DamageCauser, DamageAmount);
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

	// 사각 면의 UV = Offset + X * PerX + Z * PerZ. 원본에서 한 번만 구한다.
	const auto* SourceUV = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryUV() : nullptr;
	if (!SourceUV)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s needs UV0 to initialize the breach surface; keeping the static mesh."), *SourceStaticMesh->GetName());
		return false;
	}
	FPlanarUVMapping InitialUVs[2];
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
		FPlanarUVMapping& Mapping = InitialUVs[Side];
		if (!bFoundUVs[Side])
		{
			const FVector3d AB = B - A, AC = C - A;
			const double Determinant = AB.X * AC.Z - AC.X * AB.Z;
			if (FMath::Abs(Determinant) <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}
			Mapping.PerX = ((UV1 - UV0) * AC.Z - (UV2 - UV0) * AB.Z) / Determinant;
			Mapping.PerZ = ((UV2 - UV0) * AB.X - (UV1 - UV0) * AC.X) / Determinant;
			Mapping.Offset = UV0 - Mapping.PerX * A.X - Mapping.PerZ * A.Z;
			bFoundUVs[Side] = true;
		}
		// 한 면 안에 UV 이음선이나 서로 다른 배율이 있으면 단일 변환으로 보존할 수 없다.
		const FVector3d Positions[3] = { A, B, C };
		const FVector2d UVs[3] = { UV0, UV1, UV2 };
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector2d Expected = Mapping.Offset + Mapping.PerX * Positions[Corner].X + Mapping.PerZ * Positions[Corner].Z;
			if ((Expected - UVs[Corner]).Length() > 0.0001)
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s requires one linear UV mapping per surface face; keeping the static mesh."), *SourceStaticMesh->GetName());
				return false;
			}
		}
	}
	if (!bFoundUVs[0] || !bFoundUVs[1])
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s needs UV0 on both XZ faces; keeping the static mesh."), *SourceStaticMesh->GetName());
		return false;
	}
	SurfaceUVs[0] = InitialUVs[0];
	SurfaceUVs[1] = InitialUVs[1];

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
	OriginalDepthRange = FVector2d(MeshBounds.Min.Y, MeshBounds.Max.Y);

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
	DamageAccumulator.Reset();
	AccumulationScale = FVector2d::ZeroVector;
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
	SurfaceUVs[0] = FPlanarUVMapping();
	SurfaceUVs[1] = FPlanarUVMapping();
	return true;
}

void UBreachableSurfaceComponent::HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser, float DamageAmount)
{
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Total");
		if (!IsValid(DamageCauser))
		{
			return;
		}
		UBreachSourceComponent* Source = DamageCauser->FindComponentByClass<UBreachSourceComponent>();
		if (!IsValid(Source))
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s has no BreachSourceComponent."), *DamageCauser->GetName());
			return;
		}

		// Cutter 가져오기
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
		FGeneralPolygon2d Cutter;
		const bool bResolvedCutter = Source->ResolveCutterPolygon(GetComponentTransform(), PointDamage.HitInfo.ImpactPoint, Cutter);
		if (!bResolvedCutter)
		{
			return;
		}
		const bool bAccumulate = Accumulation.bEnabled && Source->AccumulationContribution != 0;
		FVector2d MetricPoint = FVector2d::ZeroVector;
		if (bAccumulate)
		{
			if (!IsValid(HitComponent) || (HitComponent != this && HitComponent != DynamicMeshProxy))
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s ignored accumulation damage for an unrelated component."), *GetName());
				return;
			}
			if (!FMath::IsFinite(DamageAmount) || DamageAmount <= 0)
			{
				return;
			}
			if (!FMath::IsFinite(Source->AccumulationContribution) || Source->AccumulationContribution <= 0)
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s rejected non-positive/non-finite accumulation contribution."), *GetName());
				return;
			}
			FString Error;
			const bool bConfigured = DamageAccumulator.Configure(Accumulation.WindowSize, Accumulation.DamageThreshold, Error);
			if (!bConfigured)
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous accumulation state: %s"), *GetName(), *Error);
				return;
			}
			const FVector Scale = GetComponentScale();
			const FVector2d MetricScale(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
			if (AccumulationScale != MetricScale)
			{
				if (DamageAccumulator.Num() > 0)
				{
					UE_LOG(LogBreachableSurface, Warning, TEXT("%s cleared accumulated impacts after its surface scale changed."), *GetName());
				}
				DamageAccumulator.Reset();
				AccumulationScale = MetricScale;
			}
			const bool bHadProxy = IsValid(DynamicMeshProxy);
			if (!bHadProxy)
			{
				const bool bInitialized = InitializeSurface();
				if (!bInitialized)
				{
					return;
				}
			}
			const FVector LocalHit = GetComponentTransform().InverseTransformPosition(PointDamage.HitInfo.ImpactPoint);
			const FVector2d LocalPoint(LocalHit.X, LocalHit.Z);
			bool bHitsMaterial = false;
			for (const FGeneralPolygon2d& Region : SurfaceState.RemainingRegions)
			{
				if (Region.Contains(LocalPoint))
				{
					bHitsMaterial = true;
					break;
				}
			}
			if (!bHitsMaterial)
			{
				if (!bHadProxy)
				{
					ResetSurface();
				}
				return;
			}
			MetricPoint = LocalPoint * AccumulationScale;
		}
		bool bCut = ApplyCutterPolygon(Cutter.GetOuter());
		if (!bCut)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s is retrying the breach with its base cutter."), *GetName());
			FGeneralPolygon2d BaseCutter;
			const bool bResolvedBase = Source->ResolveCutterPolygon(GetComponentTransform(), PointDamage.HitInfo.ImpactPoint, BaseCutter, false);
			if (!bResolvedBase)
			{
				return;
			}
			bCut = ApplyCutterPolygon(BaseCutter.GetOuter());
			if (!bCut)
			{
				UE_LOG(LogBreachableSurface, Error, TEXT("%s could not apply even the base cutter."), *GetName());
				return;
			}
			Cutter = MoveTemp(BaseCutter);
		}
		if (bAccumulate)
		{
			ApplyAccumulatedDamage(MetricPoint, Source->AccumulationContribution);
		}
		else
		{
			RetireAccumulationInside(Cutter.GetOuter());
		}
		OnBreachedDelegate.Broadcast(PointDamage.HitInfo, DynamicMeshProxy);
	}
}

void UBreachableSurfaceComponent::ApplyAccumulatedDamage(const FVector2d& MetricPoint, double Contribution)
{
	FString Error;
	const int32 RecordID = DamageAccumulator.AddHit(MetricPoint, Contribution, Error);
	if (RecordID == INDEX_NONE)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s could not record accumulation impact: %s"), *GetName(), *Error);
		return;
	}
	TArray<FBreachAccumulationCandidate> Candidates;
	FBreachAccumulationQueryStats Stats;
	DamageAccumulator.FindCandidates(RecordID, Candidates, Stats);
	// All candidates use the same pre-cut records, including shared impacts.
	for (const FBreachAccumulationCandidate& Candidate : Candidates)
	{
		FPolygon2d BaseMetric;
		const bool bGeneratedBase = BreachAccumulationShape::GenerateBase(Candidate.ImpactPoints, AccumulationShape.Padding, BaseMetric, Error);
		if (!bGeneratedBase)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage after its accumulation footprint failed: %s"), *GetName(), *Error);
			continue;
		}
		const FBreachContourOptions& ContourOptions = AccumulationShape.Contour;
		FPolygon2d MetricCutter = BaseMetric;
		if (ContourOptions.bEnabled)
		{
			FPolygon2d Modified;
			const bool bGenerated = BreachContour::Generate(BaseMetric, ContourOptions, Modified, Error);
			if (bGenerated)
			{
				MetricCutter = MoveTemp(Modified);
			}
			else
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s is using its base accumulation cutter: %s"), *GetName(), *Error);
			}
		}
		auto ToLocal = [this](const FPolygon2d& Polygon)
		{
			FPolygon2d Local;
			for (const FVector2d& Point : Polygon.GetVertices())
			{
				Local.AppendVertex(Point / AccumulationScale);
			}
			return Local;
		};
		bool bApplied = ApplyCutterPolygon(ToLocal(MetricCutter));
		if (!bApplied && ContourOptions.bEnabled)
		{
			MetricCutter = BaseMetric;
			bApplied = ApplyCutterPolygon(ToLocal(BaseMetric));
		}
		if (bApplied)
		{
			DamageAccumulator.RemoveInside(MetricCutter);
		}
		else
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage records after its accumulation cutter failed."), *GetName());
		}
	}
}

void UBreachableSurfaceComponent::RetireAccumulationInside(const FPolygon2d& LocalPolygon)
{
	if (DamageAccumulator.Num() == 0)
	{
		return;
	}
	FPolygon2d MetricPolygon;
	for (const FVector2d& Point : LocalPolygon.GetVertices())
	{
		MetricPolygon.AppendVertex(Point * AccumulationScale);
	}
	DamageAccumulator.RemoveInside(MetricPolygon);
}

bool UBreachableSurfaceComponent::ApplyCutterPolygon(const FPolygon2d& InCutter)
{
	FString Error;
	if (!BreachContour::Validate(InCutter, Error))
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s rejected cutter: %s"), *GetName(), *Error);
		return false;
	}
	FPolygon2d Cutter = InCutter;
	if (Cutter.IsClockwise()) 
	{
		Cutter.Reverse(); 
	}
	const bool bHadProxy = IsValid(DynamicMeshProxy);
	if (!bHadProxy)
	{
		const bool bInitialized = InitializeSurface();
		if (!bInitialized) 
		{ 
			return false; 
		}
	}
	auto Reject = [&](const TCHAR* Reason)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous surface: %s"), *GetName(), Reason);
		if (!bHadProxy) 
		{
			ResetSurface(); 
		}
		return false;
	};
	TArray<FGeneralPolygon2d> Cutters;
	Cutters.Add(FGeneralPolygon2d(Cutter));
	TArray<FGeneralPolygon2d> CandidateRegions;
	const bool bDifference = PolygonsDifference(SurfaceState.RemainingRegions, Cutters, CandidateRegions);
	if (!bDifference)
	{
		return Reject(TEXT("Polygon difference failed."));
	}

	TArray<FPolygon2d> DetachedRegions;
	CandidateRegions.RemoveAll([&](const FGeneralPolygon2d& Region)
	{
		if (!TouchesOriginalBorder(Region.GetOuter(), OriginBounds))
		{
			DetachedRegions.Add(Region.GetOuter());
			return true;
		}
		return false;
	});

	// SurfaceState기반으로 메시를 갱신한다.
	FDelaunay2 Delaunay;
	FDynamicMesh3 RenewedMesh;
	bool bMeshValid = true;
	auto AppendTriangle = [&](int32 A, int32 B, int32 C)
	{
		const int32 TriangleID = RenewedMesh.AppendTriangle(A, B, C);
		if (TriangleID < 0) 
		{
			bMeshValid = false; 
		}
	};
	for (const auto& RemainingRegion : CandidateRegions)
	{
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles; // 삼각분할 결과로 만들어진 삼각형들의 정점 인덱스를 저장하는 배열 (그냥 int 3개임)

		// 삼각분할
		if (!Delaunay.Triangulate(RemainingRegion, &Triangles, &Vertices, false))
		{
			return Reject(TEXT("Region triangulation failed."));
		}
		if (Triangles.IsEmpty())
		{
			return Reject(TEXT("Triangulation produced no faces for a remaining region."));
		}
		double TriangulatedArea = 0;
		for (const FIndex3i& Triangle : Triangles)
		{
			if (!Vertices.IsValidIndex(Triangle.A) || !Vertices.IsValidIndex(Triangle.B) || !Vertices.IsValidIndex(Triangle.C))
			{
				return Reject(TEXT("Triangulation returned an invalid vertex index."));
			}
			const FVector2d AB = Vertices[Triangle.B] - Vertices[Triangle.A];
			const FVector2d AC = Vertices[Triangle.C] - Vertices[Triangle.A];
			const double Area = FMath::Abs(AB.X * AC.Y - AB.Y * AC.X) * 0.5;
			if (!FMath::IsFinite(Area) || Area <= 1.e-12)
			{
				return Reject(TEXT("Triangulation produced a degenerate face."));
			}
			TriangulatedArea += Area;
		}
		const double RegionArea = FMath::Abs(RemainingRegion.SignedArea());
		if (FMath::Abs(TriangulatedArea - RegionArea) > FMath::Max(1.e-6, RegionArea * 1.e-6))
		{
			return Reject(TEXT("Triangulation does not cover the remaining region."));
		}
		TArray<int32> FrontIDs;
		TArray<int32> BackIDs;

		for (const FVector2d& Vertex : Vertices)
		{
			FrontIDs.Add(RenewedMesh.AppendVertex(FVector3d(Vertex.X, OriginalDepthRange.X, Vertex.Y)));
			BackIDs.Add(RenewedMesh.AppendVertex(FVector3d(Vertex.X, OriginalDepthRange.Y, Vertex.Y)));
		}
		for (const FIndex3i& Triangle : Triangles)
		{
			AppendTriangle(FrontIDs[Triangle.A], FrontIDs[Triangle.C], FrontIDs[Triangle.B]);
			AppendTriangle(BackIDs[Triangle.A], BackIDs[Triangle.B], BackIDs[Triangle.C]);
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
				AppendTriangle(FrontIDs[A], FrontIDs[B], BackIDs[B]);
				AppendTriangle(FrontIDs[A], BackIDs[B], BackIDs[A]);
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

	if (!bMeshValid)
	{
		return Reject(TEXT("Mesh reconstruction rejected a triangle."));
	}
	if (RenewedMesh.TriangleCount() > 0)
	{
		if (!RenewedMesh.IsClosed())
		{
			return Reject(TEXT("Reconstructed surface is not closed."));
		}
	}
	UE::Geometry::FMeshNormals::InitializeMeshToPerTriangleNormals(&RenewedMesh);

	// 새 메시의 각 삼각형에 UV 좌표를 만들어 연결
	// Attribute : 노멀, UV 등의 부가 데이터에 접근
	RenewedMesh.Attributes()->SetNumUVLayers(1); // UV 채널을 1개로 설정
	auto* UVOverlay = RenewedMesh.Attributes()->PrimaryUV();  // 첫 번째 UV 채널에 접근 (기존 UV 포인터)

	const FVector2d Min = OriginBounds.Min;
	const FVector2d Size = OriginBounds.Max - Min;

	for (int32 TriangleID : RenewedMesh.TriangleIndicesItr())
	{
		const FIndex3i Triangle = RenewedMesh.GetTriangle(TriangleID);
		const FVector3d Normal = RenewedMesh.GetTriNormal(TriangleID);
		FIndex3i UVElementIDs;

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d Position = RenewedMesh.GetVertex(Triangle[Corner]);

			// 벽의 좌표를 0~1 범위로 바꾼다.
			FVector2f UV(static_cast<float>((Position.X - Min.X) / Size.X),
				static_cast<float>((Position.Z - Min.Y) / Size.Y));
			if (FMath::Abs(Normal.Y) > 0.99)
			{
				const FPlanarUVMapping& Mapping = SurfaceUVs[Normal.Y > 0 ? 1 : 0];
				UV = FVector2f(Mapping.Offset + Mapping.PerX * Position.X + Mapping.PerZ * Position.Z);
			}

			UVElementIDs[Corner] = UVOverlay->AppendElement(UV);
		}

		UVOverlay->SetTriangle(TriangleID, UVElementIDs);
	}

	SurfaceState.RemainingRegions = MoveTemp(CandidateRegions);
	DynamicMeshProxy->SetMesh(MoveTemp(RenewedMesh));
	DynamicMeshProxy->UpdateCollision();
	for (const FPolygon2d& Detached : DetachedRegions)
	{
		RetireAccumulationInside(Detached);
	}

	return true;
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

