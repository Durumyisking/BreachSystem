// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshComponent.h"

#include "BreachSourceComponent.h"
#include "BreachSystemLog.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "Generators/SweepGenerator.h"
#include "Operations/MeshBoolean.h"


void UBreachableMeshComponent::OnRegister()
{
	Super::OnRegister();

	ConvertStaticMeshToDynamicMesh(SourceStaticMesh);
}

void UBreachableMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	OnComponentHit.RemoveAll(this);
	OnComponentHit.AddDynamic(this, &ThisClass::OnBreachableMeshHit);
}

void UBreachableMeshComponent::OnBreachableMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                                   UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	UBreachSourceComponent* BreachSourceComponent = OtherActor->GetComponentByClass<UBreachSourceComponent>();
	if (!BreachSourceComponent)
	{
		return;
	}

	using namespace UE::Geometry;

	// Mesh상에서의 로컬 좌표를 구한다.
	const FTransform& ComponentTransform = GetComponentTransform();
	const FVector HitLocalPosition = ComponentTransform.InverseTransformPosition(Hit.ImpactPoint);
	const FVector HitLocalNormal = ComponentTransform.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();

	DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.f, FColor::Red, false, 5.f);

	const FQuaterniond CutterRotation(FVector3d::UnitZ(),FVector3d(HitLocalNormal)); // 정방향 Cutter을 Normal방향으로 돌리기 위한 회전값

	const FDynamicMesh3& CutterMesh = BreachSourceComponent->GetCutterMesh();
	FTransformSRT3d CutterMeshTransform(CutterRotation, HitLocalPosition);

	FDynamicMesh3 SourceMesh(*GetMesh());
	FDynamicMesh3 ResultMesh;

	FMeshBoolean MeshBoolean(&SourceMesh, FTransformSRT3d::Identity(), &CutterMesh, CutterMeshTransform, &ResultMesh, FMeshBoolean::EBooleanOp::Difference);

	if (!MeshBoolean.Compute())
	{
		SWARN(TEXT("Mesh Boolean Failed"));
		return;
	}

	// 결과 적용
	SetMesh(MoveTemp(ResultMesh));

	// 충돌도 새 메시 기준으로 갱신
	UpdateCollision(false);
}

void UBreachableMeshComponent::ConvertStaticMeshToDynamicMesh(UStaticMesh* InSourceStaticMesh)
{
	if (!InSourceStaticMesh)
	{
		SERROR(TEXT("Invalid Static Mesh"));
		return;
	}

	// DynamicMesh로 변환하기 위한 기존 메시의 Desc를 가져온다.
	FMeshDescription* StaticMeshDescription = InSourceStaticMesh->GetMeshDescription(0);
	if (!StaticMeshDescription)
	{
		SERROR(TEXT("Invalid Static Mesh Description"));
		return;
	}

	UE::Geometry::FDynamicMesh3 DynamicMesh;

	// 이런 멋진 구조체가 존재한다.
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(StaticMeshDescription, DynamicMesh, true);

	// UE식 Rvalue Ref
	SetMesh(MoveTemp(DynamicMesh));

	SetComplexAsSimpleCollisionEnabled(true, true);
}
