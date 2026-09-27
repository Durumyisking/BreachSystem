#include "Shape/BreachShape.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Generators/SweepGenerator.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Generators/MinimalBoxMeshGenerator.h"

namespace
{
	void GenerateRoundCutter(const FVector2D& Radius, int32 Segments, double Length,
		UE::Geometry::FDynamicMesh3& OutMesh)
	{
		using namespace UE::Geometry;
		FCylinderGenerator Generator;
		Generator.Radius[0] = FMath::Max(Radius.X, UE_DOUBLE_KINDA_SMALL_NUMBER);
		Generator.Radius[1] = FMath::Max(Radius.Y, UE_DOUBLE_KINDA_SMALL_NUMBER);
		Generator.Height = static_cast<float>(Length);
		Generator.AngleSamples = FMath::Max(Segments, 3);
		Generator.LengthSamples = 1;
		Generator.bCapped = true;
		Generator.Generate();
		OutMesh = FDynamicMesh3(&Generator);
		// 생성기의 Z=0~Length를 중심 기준으로 옮겨, Z Scale을 늘려도 양 끝이 대칭으로 뻗게 한다.
		MeshTransforms::Translate(OutMesh, FVector3d(0.0, 0.0, -Length * 0.5));
	}
}

void UBreachCylinderShape::GenerateMesh(const FBreachSourceOption& Option, double Length,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	const FBreachCylinderShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachCylinderShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}
	GenerateRoundCutter(ShapeOption->Radius, ShapeOption->Segments, Length, OutMesh);
}


void UBreachSphereShape::GenerateMesh(
	const FBreachSourceOption& Option,
	double Length,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	const FBreachSphereShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachSphereShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}

	GenerateRoundCutter(FVector2D(ShapeOption->Radius, ShapeOption->Radius), ShapeOption->ThetaSegments, Length, OutMesh);
}


void UBreachBoxShape::GenerateMesh(
	const FBreachSourceOption& Option,
	double Length,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	using namespace UE::Geometry;

	const FBreachBoxShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachBoxShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}

	const FVector3d HalfExtent(
		FMath::Max(ShapeOption->Size.X * 0.5, UE_DOUBLE_KINDA_SMALL_NUMBER),
		FMath::Max(ShapeOption->Size.Y * 0.5, UE_DOUBLE_KINDA_SMALL_NUMBER),
		Length * 0.5
	);

	FMinimalBoxMeshGenerator Generator;

	Generator.Box = FOrientedBox3d(
		FVector3d::Zero(),
		HalfExtent
	);

	Generator.Generate();

	OutMesh = FDynamicMesh3(&Generator);
}


void UBreachCapsuleShape::GenerateMesh(
	const FBreachSourceOption& Option,
	double Length,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	const FBreachCapsuleShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachCapsuleShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}

	GenerateRoundCutter(FVector2D(ShapeOption->Radius, ShapeOption->Radius), ShapeOption->CircleSegments, Length, OutMesh);
}
