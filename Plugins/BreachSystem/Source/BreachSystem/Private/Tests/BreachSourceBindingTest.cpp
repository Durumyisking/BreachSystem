#include "BreachSourceComponent.h"
#include "BreachSystemSettings.h"
#include "Shape/BreachShape.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSourceBindingTest, "BreachSystem.Source.Binding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FBreachSourceBindingTest::RunTest(const FString& Parameters)
{
	UBreachSystemSettings* Settings = GetMutableDefault<UBreachSystemSettings>();
	TGuardValue<float> SharedDepthGuard(Settings->SharedDepth, 120.f);
	UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>();

	auto CheckSize = [this, Source](const TCHAR* What, const FVector3d& Expected)
	{
		const UE::Geometry::FDynamicMesh3* Mesh = Source->ResolveCutterMesh();
		if (!TestNotNull(What, Mesh))
		{
			return false;
		}

		const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh->GetBounds();
		return TestTrue(What, Mesh->TriangleCount() > 0
			&& FMath::IsNearlyEqual(Bounds.Width(), Expected.X, 0.001)
			&& FMath::IsNearlyEqual(Bounds.Height(), Expected.Y, 0.001)
			&& FMath::IsNearlyEqual(Bounds.Depth(), Expected.Z, 0.001));
	};

	Source->BindCylinder(0.25f);
	bool bPassed = CheckSize(TEXT("Bullet radius 0.25 with shared depth"), FVector3d(0.5, 0.5, 120.0));
	const UE::Geometry::FDynamicMesh3* CachedMesh = Source->ResolveCutterMesh();
	Settings->SharedDepth = 240.f;
	bPassed &= TestTrue(TEXT("Shared setting changes keep the cached mesh"), Source->ResolveCutterMesh() == CachedMesh);
	bPassed &= CheckSize(TEXT("Bound depth remains fixed"), FVector3d(0.5, 0.5, 120.0));

	Source->BindCylinder(0.25f, EBreachDepthMode::Individual, 60.f);
	bPassed &= CheckSize(TEXT("Cylinder individual depth"), FVector3d(0.5, 0.5, 60.0));

	Source->BindBox(FVector2D(300.0, 400.0));
	bPassed &= CheckSize(TEXT("Hammer size with shared depth"), FVector3d(300.0, 400.0, 240.0));
	Source->BindBox(FVector2D(0.25, 0.5), EBreachDepthMode::Individual, 0.75f);
	bPassed &= CheckSize(TEXT("Box sub-unit size and individual depth"), FVector3d(0.25, 0.5, 0.75));

	Source->BindSphere(0.25f);
	const UE::Geometry::FDynamicMesh3* SphereMesh = Source->ResolveCutterMesh();
	if (TestNotNull(TEXT("Sphere mesh"), SphereMesh))
	{
		bool bRadiusMatches = SphereMesh->TriangleCount() > 0;
		for (const FVector3d& Vertex : SphereMesh->VerticesItr())
		{
			bRadiusMatches &= FMath::IsNearlyEqual(Vertex.Length(), 0.25, 0.001);
		}
		bPassed &= TestTrue(TEXT("Sphere uses only its radius"), bRadiusMatches);
	}
	else
	{
		bPassed = false;
	}

	Source->BindCapsule(0.25f);
	bPassed &= CheckSize(TEXT("Capsule shared total depth"), FVector3d(0.5, 0.5, 240.0));
	Source->BindCapsule(0.25f, EBreachDepthMode::Individual, 2.f);
	bPassed &= CheckSize(TEXT("Capsule individual total depth"), FVector3d(0.5, 0.5, 2.0));

	FBreachSourceOption LegacyOption;
	LegacyOption.ShapeClass = UBreachBoxShape::StaticClass();
	FBreachBoxShapeOption& Box = LegacyOption.ShapeOption.InitializeAs<FBreachBoxShapeOption>();
	Box.Size = FVector2D(30.0, 40.0);
	Box.Depth = 55.f;
	Source->UpdateBreachSourceOption(LegacyOption);
	bPassed &= CheckSize(TEXT("Existing explicit options retain their depth"), FVector3d(30.0, 40.0, 55.0));
	return bPassed;
}

#endif
