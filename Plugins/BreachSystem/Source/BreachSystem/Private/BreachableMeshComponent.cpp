// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshComponent.h"

#include "BreachSystemLog.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "Generators/SweepGenerator.h"
#include "Operations/MeshBoolean.h"


void UBreachableMeshComponent::OnRegister()
{
	Super::OnRegister();

	OnComponentHit.RemoveAll(this);
	OnComponentHit.AddDynamic(this, &ThisClass::OnBreachableMeshHit);
}

void UBreachableMeshComponent::OnBreachableMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	using namespace UE::Geometry;

	const FTransform& ComponentTransform = GetComponentTransform();

	const FVector HitLocalPosition =
		ComponentTransform.InverseTransformPosition(Hit.ImpactPoint);

	const FVector HitLocalNormal =
		ComponentTransform.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();

	DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 10.f, FColor::Red, false, 5.f);

	// 1. 뚫을 원통 생성
	FCylinderGenerator CylinderGenerator;
	CylinderGenerator.Radius[0] = 1.f;
	CylinderGenerator.Radius[1] = 1.f;
	CylinderGenerator.Height = 200.f;
	CylinderGenerator.AngleSamples = 16;
	CylinderGenerator.LengthSamples = 1;
	CylinderGenerator.bCapped = true; // false면 튜브형태

	CylinderGenerator.Generate();

	FDynamicMesh3 CutterMesh(&CylinderGenerator); // 실제 메시정보 생성

	// 2. Cylinder가 기본적으로 Z축 방향이므로
	// Z축을 충돌 Normal 방향으로 회전시킨다.
	const FQuat CutterRotation = FQuat::FindBetweenNormals(FVector::UpVector, HitLocalNormal);

	// 3. Cutter를 Hit 위치로 이동 + 회전
	for (int32 VertexID : CutterMesh.VertexIndicesItr())
	{
		FVector3d Position = CutterMesh.GetVertex(VertexID);

		// 충돌면 방향으로 회전
		Position = CutterRotation.RotateVector(Position);

		// Hit 위치로 이동
		Position += FVector3d(HitLocalPosition);

		CutterMesh.SetVertex(VertexID, Position); // 정점 위치 재설정
	}

	// 4. 원본 벽 복사
	FDynamicMesh3 SourceMesh(*GetMesh());

	FDynamicMesh3 ResultMesh;

	// 5. 벽 - 원통
	FMeshBoolean MeshBoolean(&SourceMesh, &CutterMesh, &ResultMesh, FMeshBoolean::EBooleanOp::Difference);

	if (!MeshBoolean.Compute())
	{
		SWARN(TEXT("Mesh Boolean Failed"));
		return;
	}

	// 6. 결과 적용
	SetMesh(MoveTemp(ResultMesh));

	// 7. 충돌도 새 메시 기준으로 갱신
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
