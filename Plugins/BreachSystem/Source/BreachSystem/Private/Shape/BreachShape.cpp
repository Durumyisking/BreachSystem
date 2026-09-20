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

	FCylinderGenerator Generator;

	Generator.Radius[0] = FMath::Max(Option.Size.X, 1.f);
	Generator.Radius[1] = FMath::Max(Option.Size.Y, 1.f);
	Generator.Height = FMath::Max(Option.Depth, 1.f);

	Generator.AngleSamples = 16;
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

	FSphereGenerator Generator;

	Generator.Radius = FMath::Max(Option.Size.X, 1.f);
	Generator.NumTheta = 16;
	Generator.NumPhi = 8;

	Generator.Generate();

	OutMesh = FDynamicMesh3(&Generator);
}


void UBreachBoxShape::GenerateMesh(
	const FBreachSourceOption& Option,
	UE::Geometry::FDynamicMesh3& OutMesh) const
{
	using namespace UE::Geometry;

	const FVector3d HalfExtent(
		FMath::Max(Option.Size.X * 0.5, 0.5),
		FMath::Max(Option.Size.Y * 0.5, 0.5),
		FMath::Max(Option.Depth * 0.5, 0.5)
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

	const double Radius = FMath::Max(Option.Size.X, 1.f);

	FCapsuleGenerator Generator;

	Generator.Radius = Radius;

	// Capsule 전체 길이를 Depth로 취급
	Generator.SegmentLength =
		FMath::Max(static_cast<double>(Option.Depth) - Radius * 2.0, 0.0);

	Generator.NumCircleSteps = 16;
	Generator.NumHemisphereArcSteps = 4;
	Generator.NumSegmentSteps = 1;

	Generator.Generate();

	OutMesh = FDynamicMesh3(&Generator);
}