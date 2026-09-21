// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachSourceComponent.h"

#include "BreachSystemLog.h"
#include "Shape/BreachShape.h"

void UBreachSourceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (BreachSourceOption.ShapeClass)
	{
		CacheCutterMesh();
	}
}

bool UBreachSourceComponent::CacheCutterMesh()
{
	CachedCutterMesh.Clear();

	if (!BreachSourceOption.ShapeClass)
	{
		SWARN(TEXT("BreachSourceComponent '%s' has no ShapeClass."), *GetPathName());
		ensureMsgf(false, TEXT("UBreachSourceComponent requires a ShapeClass."));
		return false;
	}

	UBreachShape* BreachShapeCDO = BreachSourceOption.ShapeClass->GetDefaultObject<UBreachShape>();
	if (!BreachShapeCDO)
	{
		SWARN(TEXT("BreachSourceComponent '%s' could not resolve its ShapeClass CDO."), *GetPathName());
		ensureMsgf(false, TEXT("Invalid BreachShape class default object."));
		return false;
	}

	BreachShapeCDO->GenerateMesh(BreachSourceOption, CachedCutterMesh);
	if (CachedCutterMesh.TriangleCount() <= 0)
	{
		SWARN(TEXT("BreachSourceComponent '%s' generated an empty cutter mesh."), *GetPathName());
		ensureMsgf(false, TEXT("Breach cutter generation produced no triangles."));
		return false;
	}

	return true;
}

const UE::Geometry::FDynamicMesh3* UBreachSourceComponent::ResolveCutterMesh()
{
	if (CachedCutterMesh.TriangleCount() > 0)
	{
		return &CachedCutterMesh;
	}

	SWARN(TEXT("Breach cutter cache was empty; rebuilding it before processing the hit."));
	return CacheCutterMesh() ? &CachedCutterMesh : nullptr;
}
