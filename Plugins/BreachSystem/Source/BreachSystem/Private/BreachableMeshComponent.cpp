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
	// 충돌한 액터에서 브리칭에 사용할 커터를 가져온다.
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

	// 충돌 위치와 법선을 메시 로컬 좌표로 옮겨 커터를 배치한다.
	const FTransform& ComponentTransform = HitComponent->GetComponentTransform();
	const FVector HitLocalPosition = ComponentTransform.InverseTransformPosition(Hit.ImpactPoint);
	const FVector HitLocalNormal = ComponentTransform.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();

	DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.f, FColor::Red, false, 5.f);

	const FQuaterniond CutterRotation(FVector3d::UnitZ(),FVector3d(HitLocalNormal));

	FTransformSRT3d CutterMeshTransform(CutterRotation, HitLocalPosition);

	FDynamicMesh3 ResultMesh;

	FMeshBoolean MeshBoolean(SourceMesh, FTransformSRT3d::Identity(), CutterMesh, CutterMeshTransform, &ResultMesh, FMeshBoolean::EBooleanOp::Difference);

	if (!MeshBoolean.Compute())
	{
		SWARN(TEXT("Mesh Boolean Failed"));
		return;
	}

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

	const FMeshDescription* StaticMeshDescription = SourceStaticMesh->GetMeshDescription(0);
	if (!StaticMeshDescription)
	{
		SERROR(TEXT("Invalid Static Mesh Description"));
		return false;
	}

	// 복잡한 메시 Convert과정을 간편하게 하도록 도와준다.
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(StaticMeshDescription, OutMesh, true);
	return true;
}

void UBreachableMeshComponent::CreateDynamicMeshProxy(UE::Geometry::FDynamicMesh3&& Mesh)
{
	// 액터 소유의 프록시를 루트에 부착하고 메시 표시와 충돌에 필요한 설정을 전달한다.
	DynamicMeshProxy = NewObject<UDynamicMeshComponent>(GetOwner(), NAME_None, RF_Transient);
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
