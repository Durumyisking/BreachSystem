// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/BreachSourceComponent.h"
#include "BreachSystemLog.h"
#include "Shape/BreachShape.h"

void UBreachSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	// Blueprint 기본값도 캐시 정책에 따라 준비한다.
	if (CachedCutterPolygon.VertexCount() == 0)
	{
		CacheCutterPolygon();
	}
}

void UBreachSourceComponent::BindCircle(float Radius, int32 Segments, float Roughness, int32 Seed)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachCircleShape::StaticClass();
	FBreachCircleShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
	ShapeOption.Radius = Radius;
	ShapeOption.Segments = Segments;
	ShapeOption.Roughness = Roughness;
	ShapeOption.Seed = Seed;
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::BindBox(FVector2D Size, float CornerRadius, int32 CornerSegments, float EdgeSpacing, float Roughness, int32 Seed)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachBoxShape::StaticClass();
	FBreachBoxShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	ShapeOption.Size = Size;
	ShapeOption.CornerRadius = CornerRadius;
	ShapeOption.CornerSegments = CornerSegments;
	ShapeOption.EdgeSpacing = EdgeSpacing;
	ShapeOption.Roughness = Roughness;
	ShapeOption.Seed = Seed;
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::UpdateBreachSourceOption(const FBreachSourceOption& InBreachSourceOption)
{
	BreachSourceOption = InBreachSourceOption;
	CacheCutterPolygon();
}

bool UBreachSourceComponent::CacheCutterPolygon()
{
	// 바인딩이 바뀌면 이전 형상을 버려서 잘못된 커터가 재사용되지 않도록 한다.
	CachedCutterPolygon = UE::Geometry::FPolygon2d();
	if (!ShouldCacheCutterPolygon())
	{
		return true;
	}
	return GenerateCutterPolygon(CachedCutterPolygon);
}

bool UBreachSourceComponent::ShouldCacheCutterPolygon() const
{
	if (const auto* Circle = BreachSourceOption.ShapeOption.GetPtr<FBreachCircleShapeOption>())
	{
		return Circle->Roughness == 0.f || Circle->Seed != 0;
	}
	if (const auto* Box = BreachSourceOption.ShapeOption.GetPtr<FBreachBoxShapeOption>())
	{
		return Box->Roughness == 0.f || Box->Seed != 0;
	}
	return true;
}

bool UBreachSourceComponent::GenerateCutterPolygon(UE::Geometry::FPolygon2d& OutPolygon) const
{
	OutPolygon = UE::Geometry::FPolygon2d();
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

	if (!BreachShapeCDO->GeneratePolygon(BreachSourceOption, OutPolygon))
	{
		SWARN(TEXT("BreachSourceComponent '%s' could not generate its cutter polygon. Check shape options."), *GetPathName());
		return false;
	}
	if (OutPolygon.VertexCount() < 3)
	{
		SWARN(TEXT("BreachSourceComponent '%s' generated an empty cutter polygon."), *GetPathName());
		ensureMsgf(false, TEXT("Breach cutter generation requires at least three vertices."));
		return false;
	}
	return true;
}

bool UBreachSourceComponent::ResolveCutterPolygon(const FTransform& SurfaceTransform, const FVector& HitPosition, UE::Geometry::FGeneralPolygon2d& OutPolygon)
{
	using namespace UE::Geometry;
	OutPolygon = FGeneralPolygon2d();
	const FVector Scale = SurfaceTransform.GetScale3D();
	if (FMath::IsNearlyZero(Scale.X) || FMath::IsNearlyZero(Scale.Z))
	{
		SWARN(TEXT("BreachSourceComponent '%s' cannot resolve a cutter on a zero-scale XZ surface."), *GetPathName());
		return false;
	}
	FPolygon2d GeneratedPolygon;
	const FPolygon2d* CutterPolygon = &CachedCutterPolygon;
	if (ShouldCacheCutterPolygon())
	{
		if (CachedCutterPolygon.VertexCount() == 0)
		{
			if (!CacheCutterPolygon())
			{
				return false;
			}
		}
	}
	else
	{
		if (!GenerateCutterPolygon(GeneratedPolygon))
		{
			return false;
		}
		CutterPolygon = &GeneratedPolygon;
	}

	// 월드 크기로 만든 단면을 벽의 로컬 XZ로 옮긴다. 원본 캐시는 유지한다.
	const FVector LocalHit = SurfaceTransform.InverseTransformPosition(HitPosition);
	FPolygon2d LocalPolygon;
	for (const FVector2d& Point : CutterPolygon->GetVertices())
	{
		LocalPolygon.AppendVertex(FVector2d(LocalHit.X + Point.X / Scale.X, LocalHit.Z + Point.Y / Scale.Z));
	}
	if (LocalPolygon.IsClockwise())
	{
		LocalPolygon.Reverse();
	}
	OutPolygon = FGeneralPolygon2d(LocalPolygon);
	return true;
}
