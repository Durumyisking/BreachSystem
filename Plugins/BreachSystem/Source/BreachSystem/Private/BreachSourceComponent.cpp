// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachSourceComponent.h"

#include "BreachSystemLog.h"
#include "Shape/BreachShape.h"

void UBreachSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	// Blueprint 기본값도 캐시한다. 이미 바인딩한 커터는 다시 생성하지 않는다.
	if (CachedCutterMesh.TriangleCount() == 0)
	{
		CacheCutterMesh();
	}
}

void UBreachSourceComponent::BindCylinder(float Radius)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachCylinderShape::StaticClass();
	FBreachCylinderShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachCylinderShapeOption>();
	ShapeOption.Radius = FVector2D(Radius, Radius);
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::BindBox(FVector2D Size)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachBoxShape::StaticClass();
	FBreachBoxShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	ShapeOption.Size = Size;
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

void UBreachSourceComponent::BindCapsule(float Radius)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachCapsuleShape::StaticClass();
	FBreachCapsuleShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachCapsuleShapeOption>();
	ShapeOption.Radius = Radius;
	UpdateBreachSourceOption(Option);
}

bool UBreachSourceComponent::CacheCutterMesh()
{
	// 바인딩이 바뀌면 이전 형상을 버려서 잘못된 커터가 재사용되지 않도록 한다.
	CachedCutterMesh.Clear();

	if (!BreachSourceOption.ShapeClass)
	{
		SWARN(TEXT("BreachSourceComponent '%s' has no ShapeClass."), *GetPathName());
		ensureMsgf(false, TEXT("UBreachSourceComponent requires a ShapeClass."));
		return false;
	}

	const UBreachShape* BreachShapeCDO = BreachSourceOption.ShapeClass->GetDefaultObject<UBreachShape>();
	if (!BreachShapeCDO)
	{
		SWARN(TEXT("BreachSourceComponent '%s' could not resolve its ShapeClass CDO."), *GetPathName());
		ensureMsgf(false, TEXT("Invalid BreachShape class default object."));
		return false;
	}

	// 단면은 실제 월드 크기로 만들고, 관통 길이만 피격 시 Z Scale로 적용한다.
	BreachShapeCDO->GenerateMesh(BreachSourceOption, 1.0, CachedCutterMesh);
	if (CachedCutterMesh.TriangleCount() <= 0)
	{
		SWARN(TEXT("BreachSourceComponent '%s' generated an empty cutter mesh."), *GetPathName());
		ensureMsgf(false, TEXT("Breach cutter generation produced no triangles."));
		return false;
	}

	return true;
}

const UE::Geometry::FDynamicMesh3* UBreachSourceComponent::ResolveCutterMesh(
	const UE::Geometry::FAxisAlignedBox3d& TargetWorldBounds,
	const FVector& HitPosition, const FVector& HitDirection,
	UE::Geometry::FTransformSRT3d& OutTransform)
{
	using namespace UE::Geometry;

	const FVector3d Direction = HitDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() || TargetWorldBounds.IsEmpty())
	{
		return nullptr;
	}
	if (CachedCutterMesh.TriangleCount() == 0 && !CacheCutterMesh())
	{
		return nullptr;
	}

	// Direction의 길이가 1이므로 내적은 피격점 기준 관통 축의 거리다.
	// 박스의 8개 꼭짓점 중 최소/최대 거리가 대상 전체를 포함하는 구간을 만든다.
	double MinProjection = TNumericLimits<double>::Max();
	double MaxProjection = TNumericLimits<double>::Lowest();
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		// TargetWorldBounds.GetCorner(CornerIndex)은 바운딩박스의 코너 정점을 반환한다.

		// 이 꼭짓점이 피격점보다 뚫는 방향으로 얼마나 앞이나 뒤에 있나?
		const double Projection = (TargetWorldBounds.GetCorner(CornerIndex) - FVector3d(HitPosition)).Dot(Direction);

		/*
		 * // 1. 피격점에서 꼭짓점으로 향하는 벡터
			const FVector3d ToCorner = TargetWorldBounds.GetCorner(CornerIndex) - FVector3d(HitPosition);

			// 2. 그 벡터 중 뚫는 방향의 거리만 추출
			const double Projection = ToCorner.Dot(Direction);
		 */

		MinProjection = FMath::Min(MinProjection, Projection);
		MaxProjection = FMath::Max(MaxProjection, Projection);
	}
	// 커터 뚜껑이 벽 표면과 겹치지 않도록 양 끝을 최소 1cm씩 밖으로 연장한다.
	const double Padding = FMath::Max(1.0, (MaxProjection - MinProjection) * 0.01);
	const double Length = MaxProjection - MinProjection + 2.0 * Padding;
	// 피격점을 지나는 축 위에 커터 중심을 놓는다. 피격점 자체가 중심은 아니다.
	const FVector3d Center = FVector3d(HitPosition) + Direction * ((MinProjection + MaxProjection) * 0.5);
	// Scale→Rotation→Translation 순서다. 로컬 Z만 늘린 후 관통 방향으로 돌리므로
	// X/Y 단면 크기는 벽의 Scale과 관계없이 유지된다.
	OutTransform = FTransformSRT3d(FQuaterniond(FVector3d::UnitZ(), Direction), Center, FVector3d(1.0, 1.0, Length));

	return &CachedCutterMesh;
}
