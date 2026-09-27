#include "BreachSourceComponent.h"
#include "Shape/BreachShape.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSourceBindingTest, "BreachSystem.Source.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachSourceBindingTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>();
	const FAxisAlignedBox3d CubeBounds(FVector3d(-50.0, -50.0, -50.0), FVector3d(50.0, 50.0, 50.0));
	const FTransform ThinWall(FQuat::Identity, FVector::ZeroVector, FVector(5.0, 0.1, 5.0));
	const FTransform ThickWall(FQuat::Identity, FVector::ZeroVector, FVector(5.0, 10.0, 5.0));

	auto CheckCutter = [this, Source, &CubeBounds](const TCHAR* What, const FTransform& Wall,
		const FVector& Position, const FVector& Direction, double Width, double Height)
	{
		const FAxisAlignedBox3d WorldBounds(CubeBounds, FTransformSRT3d(Wall));
		FTransformSRT3d Transform;
		const FDynamicMesh3* Mesh = Source->ResolveCutterMesh(WorldBounds, Position, Direction, Transform);
		if (!TestNotNull(What, Mesh))
		{
			return false;
		}

		const FAxisAlignedBox3d Bounds = Mesh->GetBounds();
		bool bPassed = TestTrue(FString(What) + TEXT(": closed cutter"), Mesh->IsClosed());
		bPassed &= TestTrue(FString(What) + TEXT(": cached cutter has unit length"),
			FMath::IsNearlyEqual(Bounds.Min.Z, -0.5, 0.001)
			&& FMath::IsNearlyEqual(Bounds.Max.Z, 0.5, 0.001));
		bPassed &= TestTrue(FString(What) + TEXT(": world size preserved"),
			FMath::IsNearlyEqual(Bounds.Width(), Width, 0.001)
			&& FMath::IsNearlyEqual(Bounds.Height(), Height, 0.001)
			&& FMath::IsNearlyEqual(Transform.GetScale().X, 1.0, 0.001)
			&& FMath::IsNearlyEqual(Transform.GetScale().Y, 1.0, 0.001));
		const FVector3d LocalHit = Transform.InverseTransformPosition(FVector3d(Position));
		bPassed &= TestTrue(FString(What) + TEXT(": cross section stays on hit axis"),
			FMath::IsNearlyZero(LocalHit.X, 0.001) && FMath::IsNearlyZero(LocalHit.Y, 0.001));
		bool bCapsOutside = true;
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const double Z = Transform.InverseTransformPosition(WorldBounds.GetCorner(CornerIndex)).Z;
			bCapsOutside &= Bounds.Min.Z < Z && Z < Bounds.Max.Z;
		}
		bPassed &= TestTrue(FString(What) + TEXT(": both caps outside target"), bCapsOutside);
		return bPassed;
	};

	Source->BindCylinder(0.25f);
	bool bPassed = CheckCutter(TEXT("Thin wall front"), ThinWall, FVector(17.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 0.5, 0.5);
	bPassed &= CheckCutter(TEXT("Thin wall back"), ThinWall, FVector(-17.0, 5.0, 0.0), FVector(0.0, -1.0, 0.0), 0.5, 0.5);
	bPassed &= CheckCutter(TEXT("Thick wall"), ThickWall, FVector(0.0, -500.0, 0.0), FVector(0.0, 1.0, 0.0), 0.5, 0.5);
	FTransformSRT3d ThinTransform;
	FTransformSRT3d ThickTransform;
	const FDynamicMesh3* ThinCutter = Source->ResolveCutterMesh(FAxisAlignedBox3d(CubeBounds, FTransformSRT3d(ThinWall)),
		FVector(0.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), ThinTransform);
	const FDynamicMesh3* ThickCutter = Source->ResolveCutterMesh(FAxisAlignedBox3d(CubeBounds, FTransformSRT3d(ThickWall)),
		FVector(0.0, -500.0, 0.0), FVector(0.0, 1.0, 0.0), ThickTransform);
	bPassed &= TestTrue(TEXT("Different walls use the same cached cutter"), ThinCutter && ThinCutter == ThickCutter);
	bPassed &= TestTrue(TEXT("Only axial scale changes with wall thickness"),
		FMath::IsNearlyEqual(ThinTransform.GetScale().Z, 12.0, 0.001)
		&& FMath::IsNearlyEqual(ThickTransform.GetScale().Z, 1020.0, 0.001));
	Source->BindCylinder(1.f);
	bPassed &= CheckCutter(TEXT("Left edge"), ThinWall, FVector(-249.5, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 2.0, 2.0);
	bPassed &= CheckCutter(TEXT("Right edge"), ThinWall, FVector(249.5, 5.0, 0.0), FVector(0.0, -1.0, 0.0), 2.0, 2.0);
	bPassed &= CheckCutter(TEXT("Side face"), ThinWall, FVector(250.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), 2.0, 2.0);
	const FTransform RotatedWall(FRotator(0.0, 45.0, 0.0), FVector(100.0, 200.0, 300.0), FVector(5.0, 0.1, 5.0));
	bPassed &= CheckCutter(TEXT("Rotated wall"), RotatedWall, RotatedWall.TransformPosition(FVector(0.0, -50.0, 0.0)),
		RotatedWall.TransformVectorNoScale(FVector(0.0, 1.0, 0.0)), 2.0, 2.0);

	Source->BindBox(FVector2D(300.0, 400.0));
	bPassed &= CheckCutter(TEXT("Hammer box"), ThinWall, FVector(0.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 300.0, 400.0);
	Source->BindBox(FVector2D(0.25, 0.5));
	bPassed &= CheckCutter(TEXT("Small box"), ThinWall, FVector(0.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 0.25, 0.5);

	Source->BindSphere(0.25f);
	bPassed &= CheckCutter(TEXT("Sphere circular projection"), ThinWall, FVector(0.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 0.5, 0.5);
	Source->BindCapsule(0.25f);
	bPassed &= CheckCutter(TEXT("Capsule axial projection"), ThinWall, FVector(0.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 0.5, 0.5);

	FBreachSourceOption LegacyOption;
	LegacyOption.ShapeClass = UBreachBoxShape::StaticClass();
	FBreachBoxShapeOption& Box = LegacyOption.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	Box.Size = FVector2D(30.0, 40.0);
	Source->UpdateBreachSourceOption(LegacyOption);
	bPassed &= CheckCutter(TEXT("Existing shape options"), ThinWall, FVector(0.0, -5.0, 0.0), FVector(0.0, 1.0, 0.0), 30.0, 40.0);
	return bPassed;
}

#endif
