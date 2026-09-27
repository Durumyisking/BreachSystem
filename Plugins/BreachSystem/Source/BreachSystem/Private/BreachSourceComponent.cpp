// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachSourceComponent.h"

#include "BreachSystemLog.h"
#include "BreachSystemSettings.h"
#include "Shape/BreachShape.h"

namespace
{
	float ResolveBreachDepth(EBreachDepthMode DepthMode, float IndividualDepth)
	{
		return DepthMode == EBreachDepthMode::Shared
			? GetDefault<UBreachSystemSettings>()->SharedDepth
			: IndividualDepth;
	}
}

void UBreachSourceComponent::BindCylinder(float Radius, EBreachDepthMode DepthMode, float IndividualDepth)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachCylinderShape::StaticClass();
	FBreachCylinderShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachCylinderShapeOption>();
	ShapeOption.Radius = FVector2D(Radius, Radius);
	ShapeOption.Depth = ResolveBreachDepth(DepthMode, IndividualDepth);
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::BindBox(FVector2D Size, EBreachDepthMode DepthMode, float IndividualDepth)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachBoxShape::StaticClass();
	FBreachBoxShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	ShapeOption.Size = Size;
	ShapeOption.Depth = ResolveBreachDepth(DepthMode, IndividualDepth);
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::BindSphere(float Radius)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachSphereShape::StaticClass();
	FBreachSphereShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachSphereShapeOption>();
	ShapeOption.Radius = Radius;
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::BindCapsule(float Radius, EBreachDepthMode DepthMode, float IndividualDepth)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachCapsuleShape::StaticClass();
	FBreachCapsuleShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachCapsuleShapeOption>();
	ShapeOption.Radius = Radius;
	ShapeOption.Depth = ResolveBreachDepth(DepthMode, IndividualDepth);
	UpdateBreachSourceOption(Option);
}

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
