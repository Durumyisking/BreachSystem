#include "Components/BreachDecorationComponent.h"
#include "Components/BreachableSurfaceComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachDecoration, Log, All);

namespace
{
	bool SameSettings(const FBreachCutDecorationOptions& A, const FBreachCutDecorationOptions& B, const FBreachFeatureDecorationOptions& C, const FBreachFeatureDecorationOptions& D)
	{
		return A.Material == B.Material && A.Padding == B.Padding && A.SurfaceOffset == B.SurfaceOffset && C.MinHoleArea == D.MinHoleArea && C.Spacing == D.Spacing && C.Width == D.Width && C.Length == D.Length && C.Thickness == D.Thickness && C.Roughness == D.Roughness && C.Seed == D.Seed && C.AtlasSize == D.AtlasSize;
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

void UBreachDecorationComponent::Update(UBreachableSurfaceComponent* Surface, UBreachSurfaceMaterial* Material, const UE::Geometry::FPolygon2d& Original, TConstArrayView<UE::Geometry::FGeneralPolygon2d> Remaining, const FVector2d& Depth, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> Cuts, EBreachSurfaceSide Side, bool bVisible, bool bHidden)
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
	if (!FeatureScale.Equals(Scale))
	{
		Bindings.Reset();
		FeatureEdges.Reset();
		FeatureScale = Scale;
	}
	bool bValid = true;
	if (IsValid(AppliedCut.Material))
	{
		bValid = BreachDecoration::ClipPatches(Patches, Remaining, Cuts, Side, Scale, AppliedCut.Padding, Error);
	}
	if (bValid)
	{
		if (IsValid(AppliedFeature.Material))
		{
			bValid = BreachDecoration::UpdateBindings(Bindings, FeatureEdges, Original, Remaining, Scale, AppliedFeature, Error);
		}
		else
		{
			Bindings.Reset();
			FeatureEdges.Reset();
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
	if (Scale.ContainsNaN() || FMath::Abs(Scale.Y) <= UE_DOUBLE_SMALL_NUMBER || !FMath::IsFinite(AppliedCut.SurfaceOffset) || AppliedCut.SurfaceOffset <= 0)
	{
		Error = TEXT("Deco surface offset and scale must be finite and nonzero.");
		return false;
	}
	CutDisplays.SetNum(2);
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const EBreachSurfaceSide Side = Face == 0 ? EBreachSurfaceSide::Front : EBreachSurfaceSide::Back;
		const double Offset = AppliedCut.SurfaceOffset / FMath::Abs(Scale.Y);
		const double Y = Face == 0 ? Depth.X - Offset : Depth.Y + Offset;
		UE::Geometry::FDynamicMesh3 Mesh;
		if (!BreachDecoration::BuildPatchMesh(Patches, Side, Y, Mesh, Error))
		{
			return false;
		}
		if (Mesh.TriangleCount() == 0)
		{
			if (IsValid(CutDisplays[Face]))
			{
				CutDisplays[Face]->DestroyComponent();
				CutDisplays[Face] = nullptr;
			}
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
		}
		CutDisplays[Face]->SetMesh(MoveTemp(Mesh));
		CutDisplays[Face]->RegisterComponent();
	}
	UE::Geometry::FDynamicMesh3 FeatureMesh;
	if (!Bindings.IsEmpty())
	{
		if (!BreachDecoration::BuildFeatureMesh(Bindings, AppliedFeature, Scale, Depth, FeatureMesh, Error))
		{
			return false;
		}
	}
	if (FeatureMesh.TriangleCount() == 0)
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
	}
	FeatureDisplay->ConfigureMaterialSet({AppliedFeature.Material.Get()});
	FeatureDisplay->SetMesh(MoveTemp(FeatureMesh));
	FeatureDisplay->RegisterComponent();
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
	if (IsValid(FeatureDisplay))
	{
		FeatureDisplay->DestroyComponent();
		FeatureDisplay = nullptr;
	}
	CutDisplays.Reset();
	Patches.Reset();
	Bindings.Reset();
	FeatureEdges.Reset();
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
