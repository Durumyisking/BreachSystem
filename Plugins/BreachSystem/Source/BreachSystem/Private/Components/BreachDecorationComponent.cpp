#include "Components/BreachDecorationComponent.h"
#include "Components/BreachableSurfaceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Surface/BreachSimpleCollision.h"
#include "GameFramework/Actor.h"

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
	if (!IsValid(Material))
	{
		Clear();
		AppliedMaterial = nullptr;
		return;
	}
	if (AppliedMaterial != Material || !SameSettings(AppliedCut, Material->CutDecoration, AppliedFeature, Material->FeatureDecoration))
	{
		Clear();
		AppliedMaterial = Material;
		AppliedCut = Material->CutDecoration;
		AppliedFeature = Material->FeatureDecoration;
	}
	// Feature 재질만 교체하면 기존 조각의 부착 경로·시드·UV를 보존한다.
	AppliedFeature.Material = Material->FeatureDecoration.Material;
	FString Error;
	const FVector Scale = Surface->GetComponentScale();
	if (!AppliedScale.Equals(Scale))
	{
		// 여백·표면 오프셋·면적 기준이 스케일에 묶여 있다. 표시만 버리고 이번 갱신에서 다시 만든다.
		ResetFeatures();
		ResetCutDisplays();
		AppliedScale = Scale;
	}
	bool bValid = true;
	if (IsValid(AppliedCut.Material))
	{
		bValid = BreachDecoration::ClipPatches(Cuts, Holes, Removed, InCuts, Side, Scale, AppliedCut.Padding, Error);
	}
	if (bValid)
	{
		if (IsValid(AppliedFeature.Material))
		{
			bValid = BreachDecoration::UpdateBindings(Features, Holes, Removed, Scale, AppliedFeature, Error);
		}
		else
		{
			ResetFeatures();
		}
	}
	if (bValid)
	{
		bValid = BuildDisplays(Surface, Depth, bVisible, bHidden, Error);
	}
	if (!bValid)
	{
		UE_LOG(LogBreachDecoration, Warning, TEXT("%s kept its wall cut but removed unsafe Deco: %s"), *Surface->GetName(), *Error);
		Clear();
	}
}

bool UBreachDecorationComponent::BuildDisplays(UBreachableSurfaceComponent* Surface, const FVector2d& Depth, bool bVisible, bool bHidden, FString& Error)
{
	if (!ensureMsgf(IsValid(Surface), TEXT("%s requires a surface to build Deco displays."), *GetName()))
	{
		Error = TEXT("Missing surface.");
		return false;
	}
	AActor* Owner = GetOwner();
	if (!ensureMsgf(IsValid(Owner), TEXT("%s requires an actor to own Deco displays."), *GetName()))
	{
		Error = TEXT("Missing owner.");
		return false;
	}
	const FVector Scale = Surface->GetComponentScale();
	if (Scale.ContainsNaN() || FMath::Abs(Scale.Y) <= UE_DOUBLE_KINDA_SMALL_NUMBER || !FMath::IsFinite(AppliedCut.SurfaceOffset) || AppliedCut.SurfaceOffset <= 0)
	{
		Error = TEXT("Deco surface offset and scale must be finite and nonzero.");
		return false;
	}

	// Cut 패치: 면마다 표시 메시 하나를 두고, 바뀐 패치의 정점만 교체한다.
	CutDisplays.SetNum(2);
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const EBreachSurfaceSide Side = Face == 0 ? EBreachSurfaceSide::Front : EBreachSurfaceSide::Back;
		const double Offset = AppliedCut.SurfaceOffset / FMath::Abs(Scale.Y);
		const double Y = Face == 0 ? Depth.X - Offset : Depth.Y + Offset;
		const bool bHasPatches = Cuts.Patches.ContainsByPredicate([Side](const BreachDecoration::FPatch& Patch) { return Patch.Side == Side; });
		if (!bHasPatches)
		{
			if (IsValid(CutDisplays[Face]))
			{
				CutDisplays[Face]->DestroyComponent();
				CutDisplays[Face] = nullptr;
			}
			Cuts.StaleVertices[Face].Reset();
			continue;
		}
		if (!IsValid(CutDisplays[Face]))
		{
			UDynamicMeshComponent* Display = NewObject<UDynamicMeshComponent>(Owner);
			ConfigureDisplay(Display, Surface, bVisible, bHidden);
			Display->SetCastShadow(false);
			Display->bEnableComplexCollision = false;
			Display->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
			Display->ConfigureMaterialSet({AppliedCut.Material.Get()});
			CutDisplays[Face] = Display;

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

		UE::Geometry::FDynamicMesh3* Mesh = CutDisplays[Face]->GetMesh();
		if (!ensureMsgf(Mesh, TEXT("%s has a cut Deco display without a mesh."), *GetName()))
		{
			Error = TEXT("Missing cut decoration mesh.");
			return false;
		}
		bool bMeshChanged = false;
		if (!BreachDecoration::UpdatePatchMesh(Cuts, Side, Y, *Mesh, bMeshChanged, Error))
		{
			return false;
		}
		if (Mesh->TriangleCount() == 0)
		{
			CutDisplays[Face]->DestroyComponent();
			CutDisplays[Face] = nullptr;
			continue;
		}
		if (bMeshChanged)
		{
			CutDisplays[Face]->NotifyMeshUpdated();
		}
		if (!CutDisplays[Face]->IsRegistered())
		{
			CutDisplays[Face]->RegisterComponent();
		}
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
		if (IsValid(FeatureDisplay))
		{
			FeatureDisplay->DestroyComponent();
			FeatureDisplay = nullptr;
		}
		return true;
	}
	if (!IsValid(FeatureDisplay))
	{
		FeatureDisplay = NewObject<UDynamicMeshComponent>(Owner);
		ConfigureDisplay(FeatureDisplay, Surface, bVisible, bHidden);
		FeatureDisplay->SetCastShadow(Surface->CastShadow);
		FeatureDisplay->bEnableComplexCollision = false;
		FeatureDisplay->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);

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
	FeatureDisplay->ConfigureMaterialSet({AppliedFeature.Material.Get()});

	UE::Geometry::FDynamicMesh3* FeatureMesh = FeatureDisplay->GetMesh();
	if (!ensureMsgf(FeatureMesh, TEXT("%s has a feature Deco display without a mesh."), *GetName()))
	{
		Error = TEXT("Missing feature decoration mesh.");
		return false;
	}
	bool bFeatureChanged = false;
	if (!BreachDecoration::UpdateFeatureMesh(Features, AppliedFeature, Scale, Depth, *FeatureMesh, bFeatureChanged, Error))
	{
		return false;
	}
	if (FeatureMesh->TriangleCount() == 0)
	{
		FeatureDisplay->DestroyComponent();
		FeatureDisplay = nullptr;
		return true;
	}
	if (bFeatureChanged)
	{
		FeatureDisplay->NotifyMeshUpdated();
	}
	if (!FeatureDisplay->IsRegistered())
	{
		FeatureDisplay->RegisterComponent();
	}
	return true;
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

void UBreachDecorationComponent::ResetCutDisplays()
{
	for (UDynamicMeshComponent* Display : CutDisplays)
	{
		if (IsValid(Display))
		{
			Display->DestroyComponent();
		}
	}
	CutDisplays.Reset();
	Cuts.StaleVertices[0].Reset();
	Cuts.StaleVertices[1].Reset();
	for (BreachDecoration::FPatch& Patch : Cuts.Patches)
	{
		Patch.MeshVertices.Reset();
		Patch.bInMesh = false;
	}
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
