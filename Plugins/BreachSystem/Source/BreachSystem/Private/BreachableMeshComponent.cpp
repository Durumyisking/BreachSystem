// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshComponent.h"

#include "BreachSourceComponent.h"
#include "BreachSystemLog.h"
#include "Components/DynamicMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Operations/MeshBoolean.h"


void UBreachableMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	OnComponentHit.RemoveAll(this);
	OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnBreachableMeshHit);
}

void UBreachableMeshComponent::OnBreachableMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	UBreachSourceComponent* BreachSourceComponent = OtherActor ? OtherActor->GetComponentByClass<UBreachSourceComponent>() : nullptr;
	if (!BreachSourceComponent)
	{
		return;
	}

	using namespace UE::Geometry;

	const FDynamicMesh3* CutterMesh = BreachSourceComponent->ResolveCutterMesh();
	if (!CutterMesh)
	{
		return;
	}

	FDynamicMesh3 ConvertedMesh;
	const FDynamicMesh3* SourceMesh = nullptr;
	const bool bCreateProxy = !DynamicMeshProxy;
	if (bCreateProxy) // 
	{
		UStaticMesh* SourceStaticMesh = GetStaticMesh();
		if (!SourceStaticMesh)
		{
			SERROR(TEXT("Invalid Static Mesh"));
			return;
		}

		const FMeshDescription* StaticMeshDescription = SourceStaticMesh->GetMeshDescription(0);
		if (!StaticMeshDescription)
		{
			SERROR(TEXT("Invalid Static Mesh Description"));
			return;
		}

		// Converter을 통해 복잡한 메시 변환 과정을 한번에 처리한다.
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(StaticMeshDescription, ConvertedMesh, true);
		SourceMesh = &ConvertedMesh;
	}
	else
	{
		SourceMesh = DynamicMeshProxy->GetMesh();
	}

	// Mesh상에서의 로컬 좌표를 구한다.
	const FTransform& ComponentTransform = bCreateProxy ? GetComponentTransform() : DynamicMeshProxy->GetComponentTransform();
	const FVector HitLocalPosition = ComponentTransform.InverseTransformPosition(Hit.ImpactPoint);
	const FVector HitLocalNormal = ComponentTransform.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();

	DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.f, FColor::Red, false, 5.f);

	const FQuaterniond CutterRotation(FVector3d::UnitZ(),FVector3d(HitLocalNormal)); // 정방향 Cutter을 Normal방향으로 돌리기 위한 회전값

	FTransformSRT3d CutterMeshTransform(CutterRotation, HitLocalPosition);

	FDynamicMesh3 ResultMesh;

	FMeshBoolean MeshBoolean(SourceMesh, FTransformSRT3d::Identity(), CutterMesh, CutterMeshTransform, &ResultMesh, FMeshBoolean::EBooleanOp::Difference);

	if (!MeshBoolean.Compute())
	{
		SWARN(TEXT("Mesh Boolean Failed"));
		return;
	}

	if (bCreateProxy)
	{
		DynamicMeshProxy = NewObject<UDynamicMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		DynamicMeshProxy->SetMobility(GetMobility());
		DynamicMeshProxy->SetupAttachment(GetOwner()->GetRootComponent());
		DynamicMeshProxy->SetWorldTransform(GetComponentTransform());
		DynamicMeshProxy->ConfigureMaterialSet(GetMaterials());
		DynamicMeshProxy->SetVisibility(IsVisible());
		DynamicMeshProxy->SetHiddenInGame(bHiddenInGame);
		DynamicMeshProxy->SetCastShadow(CastShadow);
		DynamicMeshProxy->SetReceivesDecals(bReceivesDecals);
		DynamicMeshProxy->SetRenderCustomDepth(bRenderCustomDepth);
		DynamicMeshProxy->SetCustomDepthStencilValue(CustomDepthStencilValue);
		DynamicMeshProxy->SetCustomDepthStencilWriteMask(CustomDepthStencilWriteMask);
		DynamicMeshProxy->SetCollisionProfileName(GetCollisionProfileName());
		DynamicMeshProxy->SetCollisionObjectType(GetCollisionObjectType());
		DynamicMeshProxy->SetCollisionResponseToChannels(GetCollisionResponseToChannels());
		DynamicMeshProxy->SetCollisionEnabled(GetCollisionEnabled());
		DynamicMeshProxy->SetGenerateOverlapEvents(GetGenerateOverlapEvents());
		DynamicMeshProxy->SetNotifyRigidBodyCollision(BodyInstance.bNotifyRigidBodyCollision);
		DynamicMeshProxy->SetPhysMaterialOverride(BodyInstance.GetPhysMaterialOverride());
		DynamicMeshProxy->SetDeferredCollisionUpdatesEnabled(true, false);
		DynamicMeshProxy->SetComplexAsSimpleCollisionEnabled(true, false);
		DynamicMeshProxy->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnBreachableMeshHit);
	}

	// 결과 적용 후 충돌을 한 번만 갱신한다.
	DynamicMeshProxy->SetMesh(MoveTemp(ResultMesh));
	DynamicMeshProxy->UpdateCollision();

	if (bCreateProxy)
	{
		DynamicMeshProxy->RegisterComponent();
		OnComponentHit.RemoveDynamic(this, &ThisClass::OnBreachableMeshHit);
		SetVisibility(false);
		SetHiddenInGame(true);
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetComponentTickEnabled(false);
		Deactivate();
	}
}
