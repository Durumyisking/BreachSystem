// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshComponent.h"

#include "BreachSystemLog.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"


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
}
