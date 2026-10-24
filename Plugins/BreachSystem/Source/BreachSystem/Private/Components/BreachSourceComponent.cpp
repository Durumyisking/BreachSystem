// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/BreachSourceComponent.h"
#include "BreachSystemLog.h"
#include "Shape/BreachShape.h"

void UBreachSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	// Serialized Blueprint defaults must be prepared even if an Editor cache existed.
	CacheCutterPolygon();
}

#if WITH_EDITOR
void UBreachSourceComponent::PostLoad()
{
	Super::PostLoad();
	BreachSourceOption.Contour.CacheJitterInterval();
}

void UBreachSourceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	BreachSourceOption.Contour.CacheJitterInterval();
	CacheCutterPolygon();
	// Super may reconstruct and replace Blueprint-created components.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UBreachSourceComponent::BindCircle(float Radius, int32 Segments, float MicroDepth, int32 Seed)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachCircleShape::StaticClass();
	FBreachCircleShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
	ShapeOption.Radius = Radius;
	ShapeOption.Segments = Segments;
	Option.Contour.MicroDepthRatio = Radius > 0 ? MicroDepth / (2.f * Radius) : -1.f;
	Option.Contour.MacroDepthRatio = 0.f;
	Option.Contour.MediumDepthRatio = 0.f;
	Option.Contour.bEnabled = MicroDepth > 0;
	Option.Contour.Seed = Seed;
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::BindBox(FVector2D Size, float CornerRadius, int32 CornerSegments, float MicroDepth, int32 Seed)
{
	FBreachSourceOption Option;
	Option.ShapeClass = UBreachBoxShape::StaticClass();
	FBreachBoxShapeOption& ShapeOption = Option.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	ShapeOption.Size = Size;
	ShapeOption.CornerRadius = CornerRadius;
	ShapeOption.CornerSegments = CornerSegments;
	Option.Contour.MicroDepthRatio = FMath::Min(Size.X, Size.Y) > 0 ? MicroDepth / FMath::Min(Size.X, Size.Y) : -1.f;
	Option.Contour.MacroDepthRatio = 0.f;
	Option.Contour.MediumDepthRatio = 0.f;
	Option.Contour.bEnabled = MicroDepth > 0;
	Option.Contour.Seed = Seed;
	UpdateBreachSourceOption(Option);
}

void UBreachSourceComponent::UpdateBreachSourceOption(const FBreachSourceOption& InBreachSourceOption)
{
	PrepareCutterPolygon(InBreachSourceOption);
}

bool UBreachSourceComponent::PrepareCutterPolygon(const FBreachSourceOption& InOption)
{
	FBreachSourceOption AcceptedOption = InOption;
	if (!AcceptedOption.ShapeClass)
	{
		SWARN(TEXT("BreachSourceComponent '%s' rejected source without ShapeClass."), *GetPathName());
		return false;
	}
	const UBreachShape* ShapeCDO = AcceptedOption.ShapeClass->GetDefaultObject<UBreachShape>();
	if (!ShapeCDO)
	{
		SWARN(TEXT("BreachSourceComponent '%s' rejected source without shape CDO."), *GetPathName());
		return false;
	}
	UE::Geometry::FPolygon2d BasePolygon;
	const bool bBaseGenerated = ShapeCDO->GeneratePolygon(AcceptedOption, BasePolygon);
	if (!bBaseGenerated)
	{
		SWARN(TEXT("BreachSourceComponent '%s' rejected invalid base shape options."), *GetPathName());
		return false;
	}
	FString Error;
	FBreachContourOptions ContourOptions = AcceptedOption.Contour;
	// Circle vertices approximate a smooth boundary rather than authored corners.
	if (ShapeCDO->IsA<UBreachCircleShape>())
	{
		ContourOptions.bMoveCorners = true;
	}
	BreachContour::FPreparedContour Prepared;
	bool bPrepared = BreachContour::Prepare(BasePolygon, ContourOptions, Prepared, Error);
	if (!bPrepared)
	{
		if (ContourOptions.bEnabled)
		{
			SWARN(TEXT("BreachSourceComponent '%s' is using its base shape because a safe contour could not be prepared: %s"), *GetPathName(), *Error);
			AcceptedOption.Contour.bEnabled = false;
			ContourOptions.bEnabled = false;
			bPrepared = BreachContour::Prepare(BasePolygon, ContourOptions, Prepared, Error);
		}
		if (!bPrepared)
		{
			SWARN(TEXT("BreachSourceComponent '%s' rejected invalid base geometry: %s"), *GetPathName(), *Error);
			return false;
		}
	}
	bool bRandom = false;
	if (ContourOptions.Seed == 0)
	{
		for (const BreachContour::FPreparedSample& Sample : Prepared.Samples)
		{
			if (Sample.MaxDisplacement > 0 || Sample.DistanceBefore != 0 || Sample.DistanceAfter != 0)
			{
				bRandom = true;
				break;
			}
		}
	}
	UE::Geometry::FPolygon2d Cached;
	if (!bRandom)
	{
		BreachContour::Generate(Prepared, Cached);
	}
	// Commit only after the replacement is ready, preserving the previous cutter on invalid rebinds.
	BreachSourceOption = MoveTemp(AcceptedOption);
	PreparedContour = MoveTemp(Prepared);
	CachedCutterPolygon = MoveTemp(Cached);
	bRandomizeCutter = bRandom;
	bCutterPrepared = true;
	return true;
}

bool UBreachSourceComponent::CacheCutterPolygon()
{
	const bool bPrepared = PrepareCutterPolygon(BreachSourceOption);
	if (bPrepared)
	{
		return true;
	}
	if (!bCutterPrepared)
	{
		FBreachSourceOption DefaultOption;
		DefaultOption.ShapeClass = UBreachCircleShape::StaticClass();
		DefaultOption.ShapeOption.InitializeAs<FBreachCircleShapeOption>();
		SWARN(TEXT("BreachSourceComponent '%s' is using the default circle cutter."), *GetPathName());
		return PrepareCutterPolygon(DefaultOption);
	}
	return false;
}

bool UBreachSourceComponent::ResolveCutterPolygon(const FTransform& SurfaceTransform, const FVector& HitPosition, UE::Geometry::FGeneralPolygon2d& OutPolygon, bool bApplyContour)
{
	using namespace UE::Geometry;
	OutPolygon = FGeneralPolygon2d();
	const FVector Scale = SurfaceTransform.GetScale3D();
	if (FMath::IsNearlyZero(Scale.X) || FMath::IsNearlyZero(Scale.Z))
	{
		SWARN(TEXT("BreachSourceComponent '%s' cannot resolve a cutter on a zero-scale XZ surface."), *GetPathName());
		return false;
	}
	if (!bCutterPrepared)
	{
		if (!CacheCutterPolygon())
		{
			return false;
		}
	}
	FPolygon2d GeneratedPolygon;
	const FPolygon2d* CutterPolygon = &PreparedContour.Base;
	if (bApplyContour)
	{
		CutterPolygon = &CachedCutterPolygon;
		if (bRandomizeCutter)
		{
			BreachContour::Generate(PreparedContour, GeneratedPolygon);
			CutterPolygon = &GeneratedPolygon;
		}
	}

	// 월드 크기로 만든 단면을 벽의 로컬 XZ로 옮긴다. 원본 캐시는 유지한다.
	const FVector LocalHit = SurfaceTransform.InverseTransformPosition(HitPosition);
	FPolygon2d LocalPolygon;
	for (const FVector2d& Point : CutterPolygon->GetVertices())
	{
		LocalPolygon.AppendVertex(FVector2d(LocalHit.X + Point.X / Scale.X, LocalHit.Z + Point.Y / Scale.Z));
	}
	if ((Scale.X < 0) != (Scale.Z < 0))
	{
		LocalPolygon.Reverse();
	}
	OutPolygon = FGeneralPolygon2d(LocalPolygon);
	return true;
}
