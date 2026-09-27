#include "Shape/BreachShape.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Generators/SweepGenerator.h"
#include "Generators/SphereGenerator.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Generators/CapsuleGenerator.h"


void UBreachCylinderShape::GenerateMesh(
	const FBreachSourceOption& Option,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	using namespace UE::Geometry;

	const FBreachCylinderShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachCylinderShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}

	FCylinderGenerator Generator;

	Generator.Radius[0] = FMath::Max(ShapeOption->Radius.X, 1.f);
	Generator.Radius[1] = FMath::Max(ShapeOption->Radius.Y, 1.f);
	Generator.Height = FMath::Max(ShapeOption->Depth, 1.f);

	Generator.AngleSamples = FMath::Max(ShapeOption->Segments, 3);
	Generator.LengthSamples = 1;
	Generator.bCapped = true;

	Generator.Generate();

	OutMesh = FDynamicMesh3(&Generator);
}


void UBreachSphereShape::GenerateMesh(
	const FBreachSourceOption& Option,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	using namespace UE::Geometry;

	const FBreachSphereShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachSphereShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}

	FSphereGenerator Generator;

	Generator.Radius = FMath::Max(ShapeOption->Radius, 1.f);
	Generator.NumTheta = FMath::Max(ShapeOption->ThetaSegments, 3);
	Generator.NumPhi = FMath::Max(ShapeOption->PhiSegments, 2);

	Generator.Generate();

	OutMesh = FDynamicMesh3(&Generator);
}


void UBreachBoxShape::GenerateMesh(
	const FBreachSourceOption& Option,
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
		FMath::Max(ShapeOption->Size.X * 0.5, 0.5),
		FMath::Max(ShapeOption->Size.Y * 0.5, 0.5),
		FMath::Max(ShapeOption->Depth * 0.5, 0.5)
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
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	using namespace UE::Geometry;

	const FBreachCapsuleShapeOption* ShapeOption = Option.ShapeOption.GetPtr<FBreachCapsuleShapeOption>();
	if (!ShapeOption)
	{
		OutMesh.Clear();
		return;
	}

	const double Radius = FMath::Max(ShapeOption->Radius, 1.f);

	FCapsuleGenerator Generator;

	Generator.Radius = Radius;

	// Capsule 전체 길이를 Depth로 취급
	Generator.SegmentLength =
		FMath::Max(static_cast<double>(ShapeOption->Depth) - Radius * 2.0, 0.0);

	Generator.NumCircleSteps = FMath::Max(ShapeOption->CircleSegments, 3);
	Generator.NumHemisphereArcSteps = FMath::Max(ShapeOption->HemisphereSegments, 2);
	Generator.NumSegmentSteps = 1;

	Generator.Generate();

	OutMesh = FDynamicMesh3(&Generator);
}