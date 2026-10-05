// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshComponent.h"

#include "BreachSourceComponent.h"
#include "BreachableDynamicMeshComponent.h"
#include "BreachSystemLog.h"
#include "Components/DynamicMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Actor.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Operations/MeshBoolean.h"
#include "DynamicMesh/MeshTransforms.h"
#include "StaticMeshDescription.h"

void UBreachableMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	OnComponentHit.RemoveAll(this);
	OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnBreachableMeshHit);
}

void UBreachableMeshComponent::OnBreachableMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	ApplyBreachHit(HitComponent, OtherActor, Hit);
}

void UBreachableMeshComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	HandleBreachDamage(this, DamageEvent, DamageCauser);
}

void UBreachableMeshComponent::HandleBreachDamage(UPrimitiveComponent* HitComponent, const FDamageEvent& DamageEvent, AActor* DamageCauser)
{
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);
		// Trace의 표면 바깥 법선을 벽이 받는 이동 충돌의 법선 방향에 맞춘다.
		FHitResult BreachHit = PointDamage.HitInfo;
		BreachHit.Normal = -BreachHit.Normal;
		BreachHit.ImpactNormal = -BreachHit.ImpactNormal;
		ApplyBreachHit(HitComponent, DamageCauser, BreachHit);
	}
}

void UBreachableMeshComponent::ApplyBreachHit(UPrimitiveComponent* HitComponent, AActor* SourceActor, const FHitResult& Hit)
{
	// 동일 액터에 여러 브리칭 메시가 있어도 실제로 맞은 원본/프록시만 처리한다.
	if (!HitComponent || (DynamicMeshProxy ? HitComponent != DynamicMeshProxy.Get() : HitComponent != this))
	{
		return;
	}

	// 충돌한 액터에서 브리칭에 사용할 커터를 가져온다.
	UBreachSourceComponent* BreachSourceComponent = SourceActor ? SourceActor->GetComponentByClass<UBreachSourceComponent>() : nullptr;
	if (!BreachSourceComponent)
	{
		return;
	}

	using namespace UE::Geometry;

	// 원본 충돌은 메시를 변환하고, 프록시 충돌은 현재 메시를 그대로 사용한다.
	FDynamicMesh3 ConvertedMesh;
	const FDynamicMesh3* SourceMesh = nullptr;
	if (this == HitComponent)
	{
		if (!ConvertStaticMeshToDynamicMesh(ConvertedMesh))
		{
			return;
		}
		SourceMesh = &ConvertedMesh;
	}
	else if (DynamicMeshProxy == HitComponent)
	{
		SourceMesh = DynamicMeshProxy->GetMesh();
	}

	if (!SourceMesh || SourceMesh->TriangleCount() == 0)
	{
		return;
	}

	// SourceMesh는 로컬 정점이다. 벽의 Transform으로 월드 범위를 구해 관통 길이를 계산한다.
	const FTransformSRT3d MeshTransform(HitComponent->GetComponentTransform());
	const FAxisAlignedBox3d TargetWorldBounds(SourceMesh->GetBounds(), MeshTransform);
	FTransformSRT3d CutterMeshTransform;
	const FDynamicMesh3* CutterMesh = BreachSourceComponent->ResolveCutterMesh(TargetWorldBounds, Hit.ImpactPoint, Hit.ImpactNormal, CutterMeshTransform);
	if (!CutterMesh)
	{
		return;
	}

	DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.f, FColor::Red, false, 5.f);

	FDynamicMesh3 ResultMesh;

	// 벽과 커터를 각각 월드 공간에 배치해 계산한다. Boolean은 커터를 내부 복사하므로 캐시는 유지된다.
	FMeshBoolean MeshBoolean(SourceMesh, MeshTransform, CutterMesh, CutterMeshTransform, &ResultMesh, FMeshBoolean::EBooleanOp::Difference);

	if (!MeshBoolean.Compute())
	{
		SWARN(TEXT("Mesh Boolean Failed: UnweldedEdges=%d"), MeshBoolean.CreatedBoundaryEdges.Num());
		return;
	}

	// Boolean 결과는 월드 공간이므로 프록시에 저장하기 전에 로컬 공간으로 되돌린다.
	MeshTransforms::ApplyTransformInverse(ResultMesh, MeshTransform, true);

	// 연산 성공 시 처음에는 프록시로 전환하고, 이후에는 메시와 충돌만 갱신한다.
	if (this == HitComponent)
	{
		CreateDynamicMeshProxy(MoveTemp(ResultMesh));
	}
	else if (DynamicMeshProxy == HitComponent)
	{
		DynamicMeshProxy->SetMesh(MoveTemp(ResultMesh));
		DynamicMeshProxy->UpdateCollision();
	}
}

bool UBreachableMeshComponent::ConvertStaticMeshToDynamicMesh(UE::Geometry::FDynamicMesh3& OutMesh) const
{
	UStaticMesh* SourceStaticMesh = GetStaticMesh();
	if (!SourceStaticMesh)
	{
		SERROR(TEXT("Invalid Static Mesh"));
		return false;
	}
	UStaticMeshDescription* StaticMeshDescription = SourceStaticMesh->GetStaticMeshDescription(0);
	if (!StaticMeshDescription)
	{
		SERROR(TEXT("Invalid Static Mesh Description"));
		return false;
	}
	const FMeshDescription& MeshDescription = StaticMeshDescription->GetMeshDescription();

	// 복잡한 메시 Convert과정을 간편하게 하도록 도와준다.
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(&MeshDescription, OutMesh, true);
	return true;
}

void UBreachableMeshComponent::CreateDynamicMeshProxy(UE::Geometry::FDynamicMesh3&& Mesh)
{
	// 액터 소유의 프록시를 루트에 부착하고 메시 표시와 충돌에 필요한 설정을 전달한다.
	UBreachableDynamicMeshComponent* BreachableProxy = NewObject<UBreachableDynamicMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	BreachableProxy->BreachableMesh = this;
	DynamicMeshProxy = BreachableProxy;
	DynamicMeshProxy->SetMobility(GetMobility());
	DynamicMeshProxy->SetupAttachment(GetOwner()->GetRootComponent());
	DynamicMeshProxy->SetWorldTransform(GetComponentTransform());
	DynamicMeshProxy->ConfigureMaterialSet(GetMaterials()); // 머티리얼 셋 바인딩
	DynamicMeshProxy->SetCollisionProfileName(GetCollisionProfileName());
	DynamicMeshProxy->SetNotifyRigidBodyCollision(BodyInstance.bNotifyRigidBodyCollision);
	DynamicMeshProxy->SetDeferredCollisionUpdatesEnabled(true, false); // 자동 충돌 갱신을 미루고 UpdateCollision()에서 갱신
	DynamicMeshProxy->SetComplexAsSimpleCollisionEnabled(true, false); // 메시 삼각형을 충돌 형상으로 사용
	DynamicMeshProxy->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnBreachableMeshHit);

	// 완성된 메시로 충돌을 한 번 생성한 뒤 프록시를 등록한다.
	DynamicMeshProxy->SetMesh(MoveTemp(Mesh));
	DynamicMeshProxy->UpdateCollision();
	DynamicMeshProxy->RegisterComponent();

	// 프록시가 준비되면 원본의 표시와 충돌을 끈다.
	OnComponentHit.RemoveDynamic(this, &ThisClass::OnBreachableMeshHit);
	SetVisibility(false);
	SetHiddenInGame(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetComponentTickEnabled(false);
	Deactivate();
}
