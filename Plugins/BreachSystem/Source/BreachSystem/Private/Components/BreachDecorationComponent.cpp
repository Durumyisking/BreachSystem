#include "Components/BreachDecorationComponent.h"
#include "Components/BreachableSurfaceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Surface/BreachSimpleCollision.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachDecoration, Log, All);

namespace
{
	bool SameSettings(const FBreachCutDecorationOptions& A, const FBreachCutDecorationOptions& B, const FBreachFeatureDecorationOptions& C, const FBreachFeatureDecorationOptions& D)
	{
		return A.Material == B.Material && A.Padding == B.Padding && A.SurfaceOffset == B.SurfaceOffset && C.MinHoleArea == D.MinHoleArea && C.Spacing == D.Spacing && C.Width == D.Width && C.Length == D.Length && C.Thickness == D.Thickness && C.Roughness == D.Roughness && C.Seed == D.Seed && C.TextureWorldSize == D.TextureWorldSize && C.AtlasSize == D.AtlasSize;
	}

	void ConfigureDisplay(UPrimitiveComponent* Display, UBreachableSurfaceComponent* Surface, bool bVisible, bool bHidden)
	{
		check(Display);
		check(Surface);
		Display->SetupAttachment(Surface);
		Display->SetRelativeTransform(FTransform::Identity);
		Display->SetMobility(Surface->GetMobility());
		Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Display->SetGenerateOverlapEvents(false);
		Display->SetCanEverAffectNavigation(false);
		Display->SetVisibility(bVisible, false);
		Display->SetHiddenInGame(bHidden, false);
	}

	/** Same layout as a new UDynamicMesh, so a display built here matches one the component would have created. */
	UE::Geometry::FDynamicMesh3 NewDisplayMesh()
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		Mesh.EnableTriangleGroups();
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnableMaterialID();
		return Mesh;
	}
}

void FBreachDecorationWork::Update(const UBreachSurfaceMaterial* InMaterial, const FBreachSimpleCollision& Holes, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> InCuts, TConstArrayView<UE::Geometry::FPolygon2d> Removed, EBreachSurfaceSide Side, const FVector& InScale)
{
	if (!IsValid(InMaterial))
	{
		Clear();
		Material = nullptr;
		return;
	}
	if (Material != InMaterial || !SameSettings(Cut, InMaterial->CutDecoration, Feature, InMaterial->FeatureDecoration))
	{
		Clear();
		Material = InMaterial;
		Cut = InMaterial->CutDecoration;
		Feature = InMaterial->FeatureDecoration;
	}
	// Feature 재질만 교체하면 기존 조각의 부착 경로·시드·UV를 보존한다.
	Feature.Material = InMaterial->FeatureDecoration.Material;
	FString Error;
	if (!Scale.Equals(InScale))
	{
		// 여백·표면 오프셋·면적 기준이 스케일에 묶여 있다. 표시만 버리고 이번 갱신에서 다시 만든다.
		ResetFeatures();
		ResetCutDisplays();
		Scale = InScale;
	}
	bool bValid = true;
	if (IsValid(Cut.Material))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachDecoration | Clip cut patches");
		bValid = BreachDecoration::ClipPatches(Cuts, Holes, Removed, InCuts, Side, Scale, Cut.Padding, Error);
	}
	if (bValid)
	{
		if (IsValid(Feature.Material))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachDecoration | Update feature bindings");
			bValid = BreachDecoration::UpdateBindings(Features, Holes, Removed, Scale, Feature, Error);
		}
		else
		{
			ResetFeatures();
		}
	}
	if (bValid)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachDecoration | Build displays");
		bValid = BuildDisplays(Depth, Error);
	}
	if (!bValid)
	{
		Warnings.Add(Error);
		Clear();
	}
}

bool FBreachDecorationWork::BuildDisplays(const FVector2d& Depth, FString& Error)
{
	if (Scale.ContainsNaN() || FMath::Abs(Scale.Y) <= UE_DOUBLE_KINDA_SMALL_NUMBER || !FMath::IsFinite(Cut.SurfaceOffset) || Cut.SurfaceOffset <= 0)
	{
		Error = TEXT("Deco surface offset and scale must be finite and nonzero.");
		return false;
	}

	// Cut 패치: 면마다 표시 메시 하나를 두고, 바뀐 패치의 정점만 교체한다.
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const EBreachSurfaceSide Side = Face == 0 ? EBreachSurfaceSide::Front : EBreachSurfaceSide::Back;
		const double Offset = Cut.SurfaceOffset / FMath::Abs(Scale.Y);
		const double Y = Face == 0 ? Depth.X - Offset : Depth.Y + Offset;
		const bool bHasPatches = Cuts.Patches.ContainsByPredicate([Side](const BreachDecoration::FPatch& Patch) { return Patch.Side == Side; });
		if (!bHasPatches)
		{
			bHasCutMesh[Face] = false;
			CutMeshes[Face] = UE::Geometry::FDynamicMesh3();
			Cuts.StaleVertices[Face].Reset();
			continue;
		}
		if (!bHasCutMesh[Face])
		{
			bHasCutMesh[Face] = true;
			bCutMeshChanged[Face] = true;
			CutMeshes[Face] = NewDisplayMesh();

			// 새 표시 메시에는 이 면의 패치가 하나도 없다. 이전 메시의 정점 기록을 버리고 모두 다시 넣는다.
			Cuts.StaleVertices[Face].Reset();
			for (BreachDecoration::FPatch& Patch : Cuts.Patches)
			{
				if (Patch.Side == Side)
				{
					Patch.MeshVertices.Reset();
					Patch.bInMesh = false;
				}
			}
		}

		bool bMeshChanged = false;
		if (!BreachDecoration::UpdatePatchMesh(Cuts, Side, Y, CutMeshes[Face], bMeshChanged, Error))
		{
			return false;
		}
		if (CutMeshes[Face].TriangleCount() == 0)
		{
			bHasCutMesh[Face] = false;
			CutMeshes[Face] = UE::Geometry::FDynamicMesh3();
			continue;
		}
		bCutMeshChanged[Face] |= bMeshChanged;
	}

	// Feature 조각: 표시 메시 하나에서 사라진 조각만 지우고 새 조각만 추가한다.
	bool bHasBindings = !Features.StaleVertices.IsEmpty();
	for (const TPair<uint64, BreachDecoration::FFeatureHole>& Hole : Features.Holes)
	{
		if (!Hole.Value.Bindings.IsEmpty())
		{
			bHasBindings = true;
			break;
		}
	}
	if (!bHasBindings)
	{
		bHasFeatureMesh = false;
		FeatureMesh = UE::Geometry::FDynamicMesh3();
		return true;
	}
	if (!bHasFeatureMesh)
	{
		bHasFeatureMesh = true;
		bFeatureMeshChanged = true;
		FeatureMesh = NewDisplayMesh();

		// 새 표시 메시에는 조각이 하나도 없다. 이전 메시의 정점 기록을 버리고 모두 다시 넣는다.
		Features.StaleVertices.Reset();
		for (TPair<uint64, BreachDecoration::FFeatureHole>& Hole : Features.Holes)
		{
			for (BreachDecoration::FBinding& Binding : Hole.Value.Bindings)
			{
				Binding.MeshVertices.Reset();
				Binding.bInMesh = false;
			}
		}
	}

	bool bFeatureChanged = false;
	if (!BreachDecoration::UpdateFeatureMesh(Features, Feature, Scale, Depth, FeatureMesh, bFeatureChanged, Error))
	{
		return false;
	}
	if (FeatureMesh.TriangleCount() == 0)
	{
		bHasFeatureMesh = false;
		FeatureMesh = UE::Geometry::FDynamicMesh3();
		return true;
	}
	bFeatureMeshChanged |= bFeatureChanged;
	return true;
}

void FBreachDecorationWork::Clear()
{
	ResetCutDisplays();
	Cuts = BreachDecoration::FCutState();
	ResetFeatures();
}

void FBreachDecorationWork::ResetFeatures()
{
	bHasFeatureMesh = false;
	bFeatureMeshChanged = false;
	FeatureMesh = UE::Geometry::FDynamicMesh3();
	bFeatureDisplayReset = true;
	Features = BreachDecoration::FFeatureState();
}

void FBreachDecorationWork::ResetCutDisplays()
{
	for (int32 Face = 0; Face < 2; ++Face)
	{
		bHasCutMesh[Face] = false;
		bCutMeshChanged[Face] = false;
		CutMeshes[Face] = UE::Geometry::FDynamicMesh3();
	}
	bCutDisplaysReset = true;
	Cuts.StaleVertices[0].Reset();
	Cuts.StaleVertices[1].Reset();
	for (BreachDecoration::FPatch& Patch : Cuts.Patches)
	{
		Patch.MeshVertices.Reset();
		Patch.bInMesh = false;
	}
}

UBreachDecorationComponent::UBreachDecorationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBreachDecorationComponent::Update(UBreachableSurfaceComponent* Surface, UBreachSurfaceMaterial* Material, const FBreachSimpleCollision& Holes, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> InCuts, TConstArrayView<UE::Geometry::FPolygon2d> Removed, EBreachSurfaceSide Side, bool bVisible, bool bHidden)
{
	if (!ensureMsgf(IsValid(Surface), TEXT("%s cannot update Deco without its surface."), *GetName()))
	{
		return;
	}
	FBreachDecorationWork Work;
	PrepareWork(Work);
	Work.Update(Material, Holes, Depth, InCuts, Removed, Side, Surface->GetComponentScale());
	ApplyWork(Surface, Work, bVisible, bHidden);
}

void UBreachDecorationComponent::PrepareWork(FBreachDecorationWork& Work) const
{
	Work.Material = AppliedMaterial;
	Work.Cut = AppliedCut;
	Work.Feature = AppliedFeature;
	Work.Scale = AppliedScale;
	Work.Cuts = Cuts;
	Work.Features = Features;
	for (int32 Face = 0; Face < 2; ++Face)
	{
		if (CutDisplays.IsValidIndex(Face) && IsValid(CutDisplays[Face]))
		{
			Work.bHasCutMesh[Face] = true;
			Work.CutMeshes[Face] = *CutDisplays[Face]->GetMesh();
		}
	}
	if (IsValid(FeatureDisplay))
	{
		Work.bHasFeatureMesh = true;
		Work.FeatureMesh = *FeatureDisplay->GetMesh();
	}
}

void UBreachDecorationComponent::ApplyWork(UBreachableSurfaceComponent* Surface, FBreachDecorationWork& Work, bool bVisible, bool bHidden)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachDecoration | Apply displays");
	if (!ensureMsgf(IsValid(Surface), TEXT("%s cannot apply Deco without its surface."), *GetName()))
	{
		return;
	}
	for (const FString& Warning : Work.Warnings)
	{
		UE_LOG(LogBreachDecoration, Warning, TEXT("%s kept its wall cut but removed unsafe Deco: %s"), *Surface->GetName(), *Warning);
	}
	AActor* Owner = GetOwner();
	if (!ensureMsgf(IsValid(Owner), TEXT("%s requires an actor to own Deco displays."), *GetName()))
	{
		UE_LOG(LogBreachDecoration, Warning, TEXT("%s kept its wall cut but removed unsafe Deco: Missing owner."), *Surface->GetName());
		Clear();
		return;
	}

	AppliedMaterial = const_cast<UBreachSurfaceMaterial*>(Work.Material);
	AppliedCut = Work.Cut;
	AppliedFeature = Work.Feature;
	AppliedScale = Work.Scale;
	Cuts = MoveTemp(Work.Cuts);
	Features = MoveTemp(Work.Features);

	// Cut 패치: 기록을 비운 갱신은 이전 표시를 버리고 새 재질로 다시 만든다.
	if (Work.bCutDisplaysReset)
	{
		for (UDynamicMeshComponent* Display : CutDisplays)
		{
			if (IsValid(Display))
			{
				Display->DestroyComponent();
			}
		}
		CutDisplays.Reset();
	}
	CutDisplays.SetNum(2);
	for (int32 Face = 0; Face < 2; ++Face)
	{
		if (!Work.bHasCutMesh[Face])
		{
			if (IsValid(CutDisplays[Face]))
			{
				CutDisplays[Face]->DestroyComponent();
			}
			CutDisplays[Face] = nullptr;
			continue;
		}
		bool bReplaceMesh = Work.bCutMeshChanged[Face];
		if (!IsValid(CutDisplays[Face]))
		{
			UDynamicMeshComponent* Display = NewObject<UDynamicMeshComponent>(Owner);
			ConfigureDisplay(Display, Surface, bVisible, bHidden);
			Display->SetCastShadow(false);
			Display->bEnableComplexCollision = false;
			Display->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
			Display->ConfigureMaterialSet({AppliedCut.Material.Get()});
			CutDisplays[Face] = Display;
			bReplaceMesh = true;
		}
		if (bReplaceMesh)
		{
			CutDisplays[Face]->SetMesh(MoveTemp(Work.CutMeshes[Face]));
		}
		if (!CutDisplays[Face]->IsRegistered())
		{
			CutDisplays[Face]->RegisterComponent();
		}
	}

	// Feature 조각: 표시 메시 하나를 같은 방식으로 맞춘다.
	if (Work.bFeatureDisplayReset || !Work.bHasFeatureMesh)
	{
		if (IsValid(FeatureDisplay))
		{
			FeatureDisplay->DestroyComponent();
		}
		FeatureDisplay = nullptr;
	}
	if (!Work.bHasFeatureMesh)
	{
		return;
	}
	bool bReplaceFeature = Work.bFeatureMeshChanged;
	if (!IsValid(FeatureDisplay))
	{
		FeatureDisplay = NewObject<UDynamicMeshComponent>(Owner);
		ConfigureDisplay(FeatureDisplay, Surface, bVisible, bHidden);
		FeatureDisplay->SetCastShadow(Surface->CastShadow);
		FeatureDisplay->bEnableComplexCollision = false;
		FeatureDisplay->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
		bReplaceFeature = true;
	}
	FeatureDisplay->ConfigureMaterialSet({AppliedFeature.Material.Get()});
	if (bReplaceFeature)
	{
		FeatureDisplay->SetMesh(MoveTemp(Work.FeatureMesh));
	}
	if (!FeatureDisplay->IsRegistered())
	{
		FeatureDisplay->RegisterComponent();
	}
}

void UBreachDecorationComponent::Clear()
{
	for (UDynamicMeshComponent* Display : CutDisplays)
	{
		if (IsValid(Display))
		{
			Display->DestroyComponent();
		}
	}
	CutDisplays.Reset();
	Cuts = BreachDecoration::FCutState();
	ResetFeatures();
}

void UBreachDecorationComponent::ResetFeatures()
{
	if (IsValid(FeatureDisplay))
	{
		FeatureDisplay->DestroyComponent();
		FeatureDisplay = nullptr;
	}
	Features = BreachDecoration::FFeatureState();
}

void UBreachDecorationComponent::OnRegister()
{
	Super::OnRegister();
	for (UDynamicMeshComponent* Display : CutDisplays)
	{
		if (IsValid(Display))
		{
			Display->RegisterComponent();
		}
	}
	if (IsValid(FeatureDisplay))
	{
		FeatureDisplay->RegisterComponent();
	}
}

void UBreachDecorationComponent::OnUnregister()
{
	for (UDynamicMeshComponent* Display : CutDisplays)
	{
		if (IsValid(Display))
		{
			Display->UnregisterComponent();
		}
	}
	if (IsValid(FeatureDisplay))
	{
		FeatureDisplay->UnregisterComponent();
	}
	Super::OnUnregister();
}

void UBreachDecorationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Clear();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
