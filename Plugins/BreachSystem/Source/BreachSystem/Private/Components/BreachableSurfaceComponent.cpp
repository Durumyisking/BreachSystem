// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Components/BreachSourceComponent.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Settings/BreachSystemSettings.h"
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

bool UBreachableSurfaceComponent::GetCellGridForDebug(FBreachCellGrid& OutGrid, UE::Geometry::FAxisAlignedBox2d& OutLocalBounds, FVector2d& OutDepthRange) const
{
	OutGrid = FBreachCellGrid();
	const FVector Scale = GetComponentScale();
	const FVector2d MetricScale(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	FString Error;
	if (IsValid(DynamicMeshProxy))
	{
		OutLocalBounds = OriginBounds;
		OutDepthRange = OriginalDepthRange;
		return OutGrid.Configure(SurfaceUVs[0].Offset, SurfaceUVs[0].PerX, SurfaceUVs[0].PerZ,
			MetricScale, CapturedCellSize, Error);
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
		bDebugSourceValid = ReadSourceSurface(Mesh, DebugUVs);
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
	DebugSourceMesh.Reset();
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

bool UBreachableSurfaceComponent::ReadSourceSurface(UE::Geometry::FDynamicMesh3& Mesh, FPlanarUVMapping (&InitialUVs)[2]) const
{
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

	// 검증: XZ 평면에서 유효한 면적을 가져야 한다.
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

		// 검증: 한 면 안에 UV 이음선이나 서로 다른 배율이 있으면 단일 변환으로 보존할 수 없다.
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

	// 검증: 원본의 앞·뒷면 UV 변환을 모두 확보한 뒤 저장한다.
	if (!bFoundUVs[0] || !bFoundUVs[1])
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s needs UV0 on both XZ faces; keeping the static mesh."), *SourceStaticMesh->GetName());
		return false;
	}

	return true;
}

bool UBreachableSurfaceComponent::PrepareSurface(UE::Geometry::FDynamicMesh3& OutMesh)
{
	FPlanarUVMapping InitialUVs[2];
	if (!ReadSourceSurface(OutMesh, InitialUVs))
	{
		return false;
	}
	const UE::Geometry::FAxisAlignedBox3d MeshBounds = OutMesh.GetBounds();
	FBreachSurfaceState InitialState;
	InitialState.Size.Width = MeshBounds.Max.X - MeshBounds.Min.X;
	InitialState.Size.Height = MeshBounds.Max.Z - MeshBounds.Min.Z;
	InitialState.Size.Thickness = MeshBounds.Max.Y - MeshBounds.Min.Y;

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

	// 7. DynamicMeshProxy를 생성하고 원본과 같은 위치에 부착한다.
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
	SurfaceState = MoveTemp(InitialState);

	// 한 표면의 수명 동안 같은 전역 규격과 -Y 면 UV 기준을 사용한다.
	CapturedCellSize = UBreachSystemSettings::GetCellSizeCm();
	const FVector Scale = GetComponentScale();
	CachedMeshScaleXZ = FVector2d(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	FString GridError;
	if (!CellGrid.Configure(SurfaceUVs[0].Offset, SurfaceUVs[0].PerX, SurfaceUVs[0].PerZ,
		CachedMeshScaleXZ, CapturedCellSize, GridError))
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s cannot accumulate cell damage: %s"), *GetName(), *GridError);
	}


	// 실제 표시와 충돌 전환은 최종 메시가 준비된 뒤 한다.
	OriginalCollisionEnabled = GetCollisionEnabled();
	bOriginalVisible = IsVisible();
	bOriginalHiddenInGame = bHiddenInGame;

	return true;
}

bool UBreachableSurfaceComponent::UpdateMesh(UE::Geometry::FDynamicMesh3&& Mesh)
{
	if (!ensureMsgf(IsValid(DynamicMeshProxy), TEXT("%s requires a surface proxy to update its mesh."), *GetName()))
	{
		return false;
	}

	const bool bFirstUpdate = !DynamicMeshProxy->IsRegistered();
	DynamicMeshProxy->SetMesh(MoveTemp(Mesh));
	if (bFirstUpdate)
	{
		DynamicMeshProxy->RegisterComponent();
	}
	DynamicMeshProxy->UpdateCollision();

	if (bFirstUpdate)
	{
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetVisibility(false, false);
		DynamicMeshProxy->SetCollisionEnabled(OriginalCollisionEnabled);
		DynamicMeshProxy->SetVisibility(bOriginalVisible, false);
		DynamicMeshProxy->SetHiddenInGame(bOriginalHiddenInGame, false);
	}
	return true;
}

bool UBreachableSurfaceComponent::ResetSurface()
{
	// 처리: 누적 기록을 비우고 Proxy가 대신하던 원본 표시·충돌을 복구한다.
	DamageAccumulator.Reset();
	CachedMeshScaleXZ = FVector2d::ZeroVector;
	CellGrid = FBreachCellGrid();
	CapturedCellSize = 0;
	DebugSourceMesh.Reset();

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
	// 검증: 포인트 피해만 처리하며, 커터를 제공하는 BreachSource가 필요하다.
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Total");

		// 검증: 이 표면의 피격이며 피해량이 유효해야 한다.
		if (!IsValid(HitComponent) || (HitComponent != this && HitComponent != DynamicMeshProxy))
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s ignored accumulation damage for an unrelated component."), *GetName());
			return;
		}

		if (!FMath::IsFinite(DamageAmount) || DamageAmount <= 0)
		{
			return;
		}

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

		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);

		// 설정 선택: 지정된 Material을 우선하고, 없으면 기존 컴포넌트 설정을 사용한다.
		const FBreachAccumulationOptions* AccumulationOptions = &Accumulation;
		const FBreachAccumulationShapeOptions* ShapeOptions = &AccumulationShape;
		if (IsValid(SurfaceMaterial))
		{
			AccumulationOptions = &SurfaceMaterial->Accumulation;
			ShapeOptions = &SurfaceMaterial->AccumulationShape;
		}
		else
		{
			UE_LOG(LogBreachableSurface, Verbose, TEXT("%s has no SurfaceMaterial; using its legacy accumulation settings. Assign a SurfaceMaterial asset to share settings."), *GetName());
		}

		// 앞의 ValidCheck이 통과하면 프록시를 생성한다.
		const bool bHadProxy = IsValid(DynamicMeshProxy);
		FDynamicMesh3 Mesh;
		if (!bHadProxy)
		{
			const bool bInitialized = PrepareSurface(Mesh);
			if (!bInitialized)
			{
				return;
			}
		}

		// 누적 파손처리
		const bool bAccumulate = AccumulationOptions->bEnabled && Source->BreachDamage != 0;
		bool bPrepared = false;
		bool bBreached = false;
		if (bAccumulate)
		{
			FVector2d GridHitPoint;
			bPrepared = PrepareAccumulation(PointDamage.HitInfo.ImpactPoint, Source->BreachDamage, *AccumulationOptions, GridHitPoint);
			if (bPrepared)
			{
				bBreached = AccumulateHit(GridHitPoint, Source->BreachDamage, *ShapeOptions, Mesh);
			}
		}

		// 누적파손이 없으면 Source의 컨투어 옵션대로 브리치가 발생한다.
		if (!bBreached)
		{
			FGeneralPolygon2d Cutter;
			const bool bCut = ApplyDirectCut(Source, PointDamage.HitInfo.ImpactPoint, Cutter, Mesh);
			if (!bCut)
			{
				if (!bHadProxy)
				{
					ResetSurface();
				}
				return;
			}

			// 유효한 누적 기록은 총알 구멍을 만든 뒤에도 유지한다.
			if (!bPrepared)
			{
				RemoveHitsInside(Cutter.GetOuter());
			}
		}

		const bool bUpdated = UpdateMesh(MoveTemp(Mesh));
		if (!bUpdated)
		{
			return;
		}

		// 최종 메시와 충돌이 반영된 뒤 피격을 알린다.
		OnBreachedDelegate.Broadcast(PointDamage.HitInfo, DynamicMeshProxy);
	}
}

bool UBreachableSurfaceComponent::PrepareAccumulation(const FVector& ImpactPoint, double BreachDamage,
	const FBreachAccumulationOptions& Options, FVector2d& OutGridHitPoint)
{
	// 검증
	OutGridHitPoint = FVector2d::ZeroVector;
	if (!ensureMsgf(IsValid(DynamicMeshProxy), TEXT("%s requires an initialized surface to prepare accumulation."), *GetName()))
	{
		return false;
	}

	if (!FMath::IsFinite(BreachDamage) || BreachDamage <= 0)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s rejected non-positive/non-finite accumulation BreachDamage."), *GetName());
		return false;
	}

	// 검증: 누적 규칙을 적용한다. 설정 실패 시 이전 누적 상태를 유지한다.
	FString Error;
	const bool bConfigured = DamageAccumulator.Configure(Options.WindowSize, Options.BreachHP, Error);
	if (!bConfigured)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous accumulation state: %s"), *GetName(), *Error);
		return false;
	}

	// 처리: 초기 Hit/메시 스케일의 변경시 아래 로직을 처리한다.
	const FVector Scale = GetComponentScale();
	const FVector2d MetricScale(FMath::Abs(Scale.X), FMath::Abs(Scale.Z)); // Scale을 안전한 XZ 추출 (언리얼이니까 XZ다)
	if (CachedMeshScaleXZ != MetricScale)
	{
		if (DamageAccumulator.Num() > 0)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s cleared accumulated impacts after its surface scale changed."), *GetName());
		}

		DamageAccumulator.Reset();
		CachedMeshScaleXZ = MetricScale;

		// ★원본 UV를 Grid화 한다.★
		CellGrid.Configure(SurfaceUVs[0].Offset, SurfaceUVs[0].PerX, SurfaceUVs[0].PerZ,
			CachedMeshScaleXZ, CapturedCellSize, Error);
	}

	// Region에 Hit 했는지 확인한다.
	const FVector LocalHit = GetComponentTransform().InverseTransformPosition(ImpactPoint);
	const FVector2d LocalPoint(LocalHit.X, LocalHit.Z);
	bool bHitSurface = false;
	for (const FGeneralPolygon2d& Region : SurfaceState.RemainingRegions)
	{
		if (Region.Contains(LocalPoint))
		{
			bHitSurface = true;
			break;
		}
	}
	if (!bHitSurface)
	{
		return false;
	}


	// 피격점이 어느 셀(Cell)에 속하는지 찾고, 그 셀의 중심 좌표(OutGridHitPoint)를 반환한다.
	FIntPoint Cell;
	const bool bQuantized = CellGrid.Quantize(LocalPoint, Cell, OutGridHitPoint, Error);
	if (!bQuantized)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s skipped accumulation and kept direct cutting: %s"), *GetName(), *Error);
		return false;
	}

	return true;
}


bool UBreachableSurfaceComponent::ApplyDirectCut(UBreachSourceComponent* Source, const FVector& ImpactPoint,
	FGeneralPolygon2d& InOutCutter, FDynamicMesh3& OutMesh)
{
	if (!ensureMsgf(IsValid(Source), TEXT("%s requires a valid BreachSourceComponent to apply a direct cut."), *GetName()))
	{
		return false;
	}

	// Cutter Polygon가져온다.
	const bool bResolved = Source->ResolveCutterPolygon(GetComponentTransform(), ImpactPoint, InOutCutter);
	if (!bResolved)
	{
		return false;
	}

	// PolygonCut!
	const bool bCut = CutPolygon(InOutCutter.GetOuter(), OutMesh);
	if (bCut)
	{
		return true;
	}
	// 기본로직은 여기서 종료
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 컨투어 적용한 Cut에 실패하면 Base로 우회한다.
	UE_LOG(LogBreachableSurface, Warning, TEXT("%s is retrying the breach with its base cutter."), *GetName());
	FGeneralPolygon2d BaseCutter;
	const bool bResolvedBase = Source->ResolveCutterPolygon(GetComponentTransform(), ImpactPoint, BaseCutter, false);
	if (!bResolvedBase)
	{
		return false;
	}
	const bool bBaseCut = CutPolygon(BaseCutter.GetOuter(), OutMesh);
	if (!bBaseCut)
	{
		UE_LOG(LogBreachableSurface, Error, TEXT("%s could not apply even the base cutter."), *GetName());
		return false;
	}

	InOutCutter = MoveTemp(BaseCutter);
	return true;
}

bool UBreachableSurfaceComponent::AccumulateHit(const FVector2d& GridHitPoint, double BreachDamage,
	const FBreachAccumulationShapeOptions& ShapeOptions, FDynamicMesh3& OutMesh)
{
	// 기록: 유효한 탄착을 추가한 뒤 임계값을 만족하는 후보를 한 번에 조회한다.
	FString Error;
	const int32 RecordID = DamageAccumulator.AddHit(GridHitPoint, BreachDamage, Error);
	if (RecordID == INDEX_NONE)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s could not record accumulation impact: %s"), *GetName(), *Error);
		return false;
	}

	TArray<FBreachAccumulationCandidate> Candidates;
	DamageAccumulator.FindCandidates(RecordID, Candidates);

	bool bBreached = false;
	// 모든 후보는 공유 탄착을 포함한 절단 전 기록을 기준으로 처리한다.
	for (const FBreachAccumulationCandidate& Candidate : Candidates)
	{
		// 생성: 탄착군과 여백으로 기본 윤곽을 만들고 선택적으로 변형한다.
		FPolygon2d BaseMetric;
		const bool bGeneratedBase = BreachAccumulationShape::GenerateBase(Candidate.ImpactPoints, ShapeOptions.Padding, BaseMetric, Error);
		if (!bGeneratedBase)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage after its accumulation footprint failed: %s"), *GetName(), *Error);
			continue;
		}

		FBreachContourOptions ContourOptions = ShapeOptions.Contour;
		// 직접 총알의 무작위 시드 정책은 유지하고, 누적 파손만 안정적인 기본값을 쓴다.
		if (ContourOptions.Seed == 0)
		{
			ContourOptions.Seed = 1;
		}
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

		// 좌표 변환: 누적에 사용하는 cm 좌표를 표면 로컬 XZ로 되돌린다.
		auto ToLocal = [this](const FPolygon2d& Polygon)
		{
			FPolygon2d Local;
			for (const FVector2d& Point : Polygon.GetVertices())
			{
				Local.AppendVertex(CellGrid.ToLocal(Point));
			}
			return Local;
		};

		// 처리: 변형 윤곽 적용에 실패하면 기본 윤곽으로 재시도한다.
		bool bApplied = CutPolygon(ToLocal(MetricCutter), OutMesh);
		if (!bApplied && ContourOptions.bEnabled)
		{
			MetricCutter = BaseMetric;
			bApplied = CutPolygon(ToLocal(BaseMetric), OutMesh);
		}

		// 결과 반영: 성공한 커터 내부 기록만 제거하고, 실패한 후보의 기록은 남긴다.
		if (bApplied)
		{
			bBreached = true;
			DamageAccumulator.RemoveInside(MetricCutter);
		}
		else
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage records after its accumulation cutter failed."), *GetName());
		}
	}
	return bBreached;
}

void UBreachableSurfaceComponent::RemoveHitsInside(const FPolygon2d& LocalPolygon)
{
	// 상태 확인: 기록이 없으면 좌표 변환도 생략한다.
	if (DamageAccumulator.Num() == 0)
	{
		return;
	}

	// 처리: 로컬 절단 영역을 cm 좌표로 변환해 내부·경계 기록을 제거한다.
	FPolygon2d MetricPolygon;
	for (const FVector2d& Point : LocalPolygon.GetVertices())
	{
		MetricPolygon.AppendVertex(CellGrid.ToMetric(Point));
	}

	DamageAccumulator.RemoveInside(MetricPolygon);
}

bool UBreachableSurfaceComponent::ApplyCutterPolygon(const FPolygon2d& InCutter)
{
	FDynamicMesh3 Mesh;
	const bool bCut = CutPolygon(InCutter, Mesh);
	if (!bCut)
	{
		return false;
	}

	const bool bUpdated = UpdateMesh(MoveTemp(Mesh));
	return bUpdated;
}

bool UBreachableSurfaceComponent::CutPolygon(const FPolygon2d& InCutter, FDynamicMesh3& OutMesh)
{
	// 검증: 잘못된 커터는 표면을 초기화하거나 변경하기 전에 거부한다.
	FString Error;
	if (!BreachContour::Validate(InCutter, Error))
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s rejected cutter: %s"), *GetName(), *Error);
		return false;
	}

	// 준비: 커터 방향을 통일하고 필요할 때 원본에서 Proxy를 초기화한다.
	FPolygon2d Cutter = InCutter;
	if (Cutter.IsClockwise()) 
	{
		Cutter.Reverse(); 
	}

	// Ensure DynamicMeshProxy
	const bool bHadProxy = IsValid(DynamicMeshProxy);
	if (!bHadProxy)
	{
		const bool bInitialized = PrepareSurface(OutMesh);
		if (!bInitialized) 
		{ 
			return false; 
		}
	}

	// 실패 처리: 이번 호출에서 만든 Proxy만 되돌리고 기존 표면은 유지한다.
	auto Reject = [&](const TCHAR* Reason)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous surface: %s"), *GetName(), Reason);
		if (!bHadProxy) 
		{
			ResetSurface(); 
		}
		return false;
	};

	// 처리: 현재 영역에서 커터를 빼되 결과는 아직 후보 상태로 보관한다.
	TArray<FGeneralPolygon2d> Cutters;
	Cutters.Add(FGeneralPolygon2d(Cutter));

	TArray<FGeneralPolygon2d> CandidateRegions;

	const bool bDifference = PolygonsDifference(SurfaceState.RemainingRegions, Cutters, CandidateRegions);
	if (!bDifference)
	{
		return Reject(TEXT("Polygon difference failed."));
	}

	// 처리: 원본 경계에 고정되지 않은 고립 영역을 후보에서 제거한다.
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

	// 메시 생성: 후보 영역으로 새 메시를 만든다. 검증이 끝날 때까지 SurfaceState는 유지한다.
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

		// 검증: 유효한 삼각형들이 남은 영역의 면적을 빠짐없이 덮어야 한다.
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

		// 메시 생성: 검증된 삼각분할을 원본 두께의 앞·뒷면으로 확장한다.
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

	// 검증: 삼각형 추가 실패나 열린 메시가 있으면 후보 결과 전체를 거부한다.
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

	// 처리: 검증된 메시의 노멀과 UV를 구성한다.
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

	// 계산 결과만 보관한다. 실제 메시와 충돌은 모든 절단이 끝난 뒤 반영한다.
	SurfaceState.RemainingRegions = MoveTemp(CandidateRegions);
	OutMesh = MoveTemp(RenewedMesh);

	for (const FPolygon2d& Detached : DetachedRegions)
	{
		RemoveHitsInside(Detached);
	}

	return true;
}

void UBreachableSurfaceComponent::DebugDrawSurface()
{
	// 상태 확인: 월드가 있을 때만 남은 표면 영역을 그린다.
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

UBreachSurfaceMaterial* UBreachableSurfaceComponent::GetSurfaceMaterial() const
{
	return SurfaceMaterial.Get();
}

void UBreachableSurfaceComponent::SetSurfaceMaterial(UBreachSurfaceMaterial* InSurfaceMaterial)
{
	SurfaceMaterial = InSurfaceMaterial;
}

