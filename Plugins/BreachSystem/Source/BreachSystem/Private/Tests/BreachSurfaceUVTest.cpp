#include "Geometry/BreachSurfaceUV.h"
#include "Data/BreachSurfaceMaterial.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: shader와 런타임 패턴이 공유하는 물리 타일의 축·원점·배율을 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachWorldSizeUVTest, "BreachSystem.Pattern.WorldSizeUV", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachWorldSizeUVTest::RunTest(const FString& Parameters)
{
	FString Error;
	FPlanarUVMapping Mapping;
	if (!TestTrue(TEXT("World-size mapping accepts nonuniform scale"), BreachSurfaceUV::MakeWorldSizeMapping(FVector(4, 0.2, 2), FVector2D(100, 50), Mapping, Error)))
	{
		return false;
	}
	TestTrue(TEXT("Local origin anchors texture center"), Mapping.Offset.Equals(FVector2d(0.5, 0.5), 1.e-12));
	TestTrue(TEXT("100 physical cm advance one U tile"), (Mapping.PerX * 25).Equals(FVector2d(1, 0), 1.e-12));
	TestTrue(TEXT("50 physical cm upward advance negative V"), (Mapping.PerZ * 25).Equals(FVector2d(0, -1), 1.e-12));
	FPlanarUVMapping Mirrored;
	TestTrue(TEXT("Mirrored scale is supported"), BreachSurfaceUV::MakeWorldSizeMapping(FVector(-4, -0.2, -2), FVector2D(100, 50), Mirrored, Error));
	TestTrue(TEXT("Mirroring keeps object-local U and V"), Mirrored.PerX.Equals(Mapping.PerX, 1.e-12) && Mirrored.PerZ.Equals(Mapping.PerZ, 1.e-12));
	const FTransform Transform(FRotator(23, 71, -12), FVector(1000, -300, 52), FVector(-4, 0.2, 2));
	for (double Depth : {-50.0, 50.0})
	{
		const FVector Local = Transform.InverseTransformPosition(Transform.TransformPosition(FVector(25, Depth, 25)));
		const FVector2d UV = Mirrored.Offset + Mirrored.PerX * Local.X + Mirrored.PerZ * Local.Z;
		TestTrue(TEXT("Rotated and translated front/back share projected coordinates"), UV.Equals(FVector2d(1.5, -0.5), 1.e-10));
	}
	const FPlanarUVMapping Before = Mapping;
	TestFalse(TEXT("Zero tile dimension is rejected"), BreachSurfaceUV::MakeWorldSizeMapping(FVector::OneVector, FVector2D(0, 50), Mapping, Error));
	TestFalse(TEXT("Zero X scale is rejected"), BreachSurfaceUV::MakeWorldSizeMapping(FVector(0, 1, 1), FVector2D(100, 50), Mapping, Error));
	TestTrue(TEXT("Rejected input preserves previous mapping"), Mapping.Offset.Equals(Before.Offset, 1.e-12) && Mapping.PerX.Equals(Before.PerX, 1.e-12) && Mapping.PerZ.Equals(Before.PerZ, 1.e-12));
	TestFalse(TEXT("Rejected input explains failure"), Error.IsEmpty());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachWorldSizeUVContractTest, "BreachSystem.Pattern.WorldSizeUVContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachWorldSizeUVContractTest::RunTest(const FString& Parameters)
{
	const UBreachSurfaceMaterial* Material = GetDefault<UBreachSurfaceMaterial>();
	const FBoolProperty* UseWorldSize = FindFProperty<FBoolProperty>(Material->GetClass(), TEXT("bUseWorldSizeUV"));
	const FStructProperty* TileSize = FindFProperty<FStructProperty>(Material->GetClass(), TEXT("TileSizeCm"));
	const FObjectProperty* Display = FindFProperty<FObjectProperty>(Material->GetClass(), TEXT("DisplayMaterial"));
	if (!TestNotNull(TEXT("Surface material exposes world-size opt-in"), UseWorldSize) || !TestNotNull(TEXT("Surface material exposes physical tile size"), TileSize) || !TestNotNull(TEXT("Surface material exposes display material"), Display))
	{
		return false;
	}
	TestFalse(TEXT("Existing surface assets keep source UV by default"), UseWorldSize->GetPropertyValue_InContainer(Material));
	TestTrue(TEXT("Default tile is 100 cm in each axis"), TileSize->ContainerPtrToValuePtr<FVector2D>(Material)->Equals(FVector2D(100, 100)));
	TestNull(TEXT("Display material is optional by default"), Display->GetObjectPropertyValue_InContainer(Material));
	return !HasAnyErrors();
}
#endif
