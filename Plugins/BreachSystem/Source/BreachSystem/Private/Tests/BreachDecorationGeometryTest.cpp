#include "Surface/BreachDecorationGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"

// 테스트 전용: 클리핑·UV·월드 면적과 부착 구간 보존을 독립된 수치로 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
	using namespace UE::Geometry;
	using namespace BreachDecoration;

	FPolygon2d Box(double MinX, double MinZ, double MaxX, double MaxZ)
	{
		FPolygon2d Polygon;
		Polygon.AppendVertex(FVector2d(MinX, MinZ));
		Polygon.AppendVertex(FVector2d(MaxX, MinZ));
		Polygon.AppendVertex(FVector2d(MaxX, MaxZ));
		Polygon.AppendVertex(FVector2d(MinX, MaxZ));
		return Polygon;
	}

	TArray<FGeneralPolygon2d> WallWithHole(const FPolygon2d& Hole)
	{
		TArray<FGeneralPolygon2d> Result;
		PolygonsDifference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Box(-50, -50, 50, 50))}, TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Hole)}, Result);
		return Result;
	}

	double PatchArea(const TArray<FPatch>& Patches)
	{
		double Area = 0;
		for (const FPatch& Patch : Patches)
		{
			for (const FGeneralPolygon2d& Region : Patch.Regions)
			{
				Area += FMath::Abs(Region.SignedArea());
			}
		}
		return Area;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationPatchTest, "BreachSystem.Decoration.PatchGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationPatchTest::RunTest(const FString& Parameters)
{
	TArray<FPatch> Patches;
	FString Error;
	const FPolygon2d Small = Box(-5, -5, 5, 5);
	const TArray<FAxisAlignedBox2d> Cuts = {Small.Bounds()};
	TestTrue(TEXT("Initial patch clips"), ClipPatches(Patches, WallWithHole(Small), Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestTrue(TEXT("20x20 footprint minus 10x10 hole is 300"), FMath::IsNearlyEqual(PatchArea(Patches), 300.0, 1.e-6));
	if (!TestEqual(TEXT("One patch"), Patches.Num(), 1))
	{
		return false;
	}
	const FAxisAlignedBox2d OriginalUV = Patches[0].UVBounds;
	TestTrue(TEXT("Re-cut clips old patch"), ClipPatches(Patches, WallWithHole(Box(-10, -5, 10, 5)), {}, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestTrue(TEXT("Remaining old patch area is 200"), FMath::IsNearlyEqual(PatchArea(Patches), 200.0, 1.e-6));
	TestTrue(TEXT("UV origin survives clipping"), Patches[0].UVBounds.Min == OriginalUV.Min);
	TestTrue(TEXT("UV extent survives clipping"), Patches[0].UVBounds.Max == OriginalUV.Max);
	FDynamicMesh3 Mesh;
	TestTrue(TEXT("Clipped mesh triangulates"), BuildPatchMesh(Patches, EBreachSurfaceSide::Front, -50.05, Mesh, Error));
	const auto* UVs = Mesh.Attributes()->PrimaryUV();
	for (int32 ID : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Triangle = Mesh.GetTriangle(ID);
		const FIndex3i UVTriangle = UVs->GetTriangle(ID);
		TestTrue(TEXT("Front faces point outward"), Mesh.GetTriNormal(ID).Y < -0.99);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d P = Mesh.GetVertex(Triangle[Corner]);
			const FVector2f UV = UVs->GetElement(UVTriangle[Corner]);
			TestTrue(TEXT("Original UV mapping remains unchanged"), UV.Equals(FVector2f((P.X + 10) / 20, (P.Z + 10) / 20), 1.e-5f));
		}
	}
	const double Before = PatchArea(Patches);
	TestTrue(TEXT("Duplicate footprint is accepted"), ClipPatches(Patches, WallWithHole(Box(-10, -5, 10, 5)), Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestTrue(TEXT("Same-face patches never overlap"), FMath::IsNearlyEqual(PatchArea(Patches), Before, 1.e-6));
	TestTrue(TEXT("Opposite face is independent"), ClipPatches(Patches, WallWithHole(Box(-10, -5, 10, 5)), Cuts, EBreachSurfaceSide::Back, FVector::OneVector, 5, Error));
	TestTrue(TEXT("Opposite face adds its own patch"), FMath::IsNearlyEqual(PatchArea(Patches), Before * 2, 1.e-6));
	TestTrue(TEXT("Full removal clears patches"), ClipPatches(Patches, {}, {}, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestEqual(TEXT("No floating traces remain"), Patches.Num(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationPatchFailureTest, "BreachSystem.Decoration.PatchFailureIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationPatchFailureTest::RunTest(const FString& Parameters)
{
	// 테스트 전용: 한 영역의 삼각분할 실패가 정상 패치 표시와 Feature 갱신을 중단하지 않는지 검증한다.
	FPatch Patch;
	Patch.UVBounds = Box(-20, -20, 20, 20).Bounds();
	Patch.Regions.Add(FGeneralPolygon2d(Box(-10, -10, 0, 0)));
	FPolygon2d Collapsed;
	Collapsed.AppendVertex(FVector2d(5, 0));
	Collapsed.AppendVertex(FVector2d(6, 0));
	Collapsed.AppendVertex(FVector2d(7, 0));
	Patch.Regions.Add(FGeneralPolygon2d(Collapsed));
	Patch.Regions.Add(FGeneralPolygon2d(Box(10, 10, 15, 15)));
	const TArray<FPatch> Patches = {Patch};
	FDynamicMesh3 Mesh;
	FString Error;
	AddExpectedMessage(TEXT("Skipped unsafe cut decoration region"), EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("A failed region does not reject the face or trigger the owner's global clear"), BuildPatchMesh(Patches, EBreachSurfaceSide::Front, -50.05, Mesh, Error));
	double Area = 0;
	for (int32 ID : Mesh.TriangleIndicesItr())
	{
		Area += Mesh.GetTriArea(ID);
	}
	TestTrue(TEXT("Safe regions before and after the failure retain their full 125cm2"), FMath::IsNearlyEqual(Area, 125.0, 1.e-6));
	TestTrue(TEXT("Skipped region leaves no fatal display error"), Error.IsEmpty());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationLocalCutObservationTest, "BreachSystem.Decoration.LocalCutObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationLocalCutObservationTest::RunTest(const FString& Parameters)
{
	// 테스트 전용: Cut 패치·표시·Clear 경로 없이 국소 절삭이 반대편 Feature 배치에 미치는 영향을 기록한다.
	const FPolygon2d Original = Box(-300, -300, 300, 300);
	const TArray<FGeneralPolygon2d> Source = {FGeneralPolygon2d(Original)};
	TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Box(-100, -100, 100, 100))};
	TArray<FGeneralPolygon2d> Remaining;
	TArray<FBinding> Bindings;
	TArray<FFeatureEdge> PreviousEdges;
	FBreachFeatureDecorationOptions Options;
	FString Error;
	TestTrue(TEXT("Initial opening difference succeeds"), PolygonsDifference(Source, Cutters, Remaining));
	TestTrue(TEXT("Initial feature placement succeeds"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	const TArray<FBinding> Before = Bindings;
	auto IsRemote = [](const FBinding& Binding)
	{
		for (const FVector2d& Point : Binding.Support)
		{
			if (Point.X < 90)
			{
				return false;
			}
		}
		return !Binding.Support.IsEmpty();
	};
	Cutters.Add(FGeneralPolygon2d(Box(-120, -15, -90, 15)));
	TestTrue(TEXT("Local left-edge notch difference succeeds"), PolygonsDifference(Source, Cutters, Remaining));
	TestTrue(TEXT("Feature-only update succeeds without a display failure"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	int32 RemoteBefore = 0;
	int32 RemoteRetained = 0;
	int32 RemoteAdded = 0;
	for (const FBinding& Old : Before)
	{
		if (IsRemote(Old))
		{
			++RemoteBefore;
			if (Bindings.ContainsByPredicate([&](const FBinding& Current) { return Current.Support == Old.Support && Current.Seed == Old.Seed && Current.Length == Old.Length; }))
			{
				++RemoteRetained;
			}
		}
	}
	for (const FBinding& Current : Bindings)
	{
		if (IsRemote(Current) && !Before.ContainsByPredicate([&](const FBinding& Old) { return Current.Support == Old.Support && Current.Seed == Old.Seed; }))
		{
			++RemoteAdded;
		}
	}
	TestTrue(TEXT("Probe has existing plates on the untouched right edge"), RemoteBefore > 0);
	TestEqual(TEXT("Existing remote attachments retain their positions and shape seeds"), RemoteRetained, RemoteBefore);
	TestEqual(TEXT("A local notch must not add plates to the untouched right edge"), RemoteAdded, 0);
	AddInfo(FString::Printf(TEXT("LocalCutObservation: left edge extended from X=-100 to -120; untouched right-edge plates before=%d, retained=%d, newly added=%d; Cut decoration, display rebuild and Clear were never called."), RemoteBefore, RemoteRetained, RemoteAdded));
	const int32 AfterNotch = Bindings.Num();
	TestTrue(TEXT("Repeated post-notch geometry evaluates"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	TestEqual(TEXT("Repeated post-notch geometry does not refill old gaps"), Bindings.Num(), AfterNotch);
	const TArray<FBinding> BeforeExtension = Bindings;
	Cutters.Add(FGeneralPolygon2d(Box(-140, -100, -90, 100)));
	TestTrue(TEXT("Collinear extension difference succeeds"), PolygonsDifference(Source, Cutters, Remaining));
	TestTrue(TEXT("Collinear extension evaluates"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	int32 NewOnOldBoundary = 0;
	int32 NewOnExtension = 0;
	for (const FBinding& Current : Bindings)
	{
		if (BeforeExtension.ContainsByPredicate([&](const FBinding& Old) { return Current.Support == Old.Support && Current.Seed == Old.Seed; }))
		{
			continue;
		}
		bool bReachesExtension = false;
		for (const FVector2d& Point : Current.Support)
		{
			bReachesExtension |= Point.X < -100 - 1.e-6;
		}
		if (bReachesExtension)
		{
			++NewOnExtension;
		}
		else
		{
			++NewOnOldBoundary;
		}
	}
	TestTrue(TEXT("Newly extended boundary still receives plates"), NewOnExtension > 0);
	TestEqual(TEXT("Extending a collinear edge does not repopulate its unchanged part"), NewOnOldBoundary, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationFeatureTest, "BreachSystem.Decoration.FeatureGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationFeatureTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Original = Box(-50, -50, 50, 50);
	FBreachFeatureDecorationOptions Options;
	TArray<FBinding> Bindings;
	TArray<FFeatureEdge> PreviousEdges;
	FString Error;
	TestTrue(TEXT("Small hole evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, WallWithHole(Box(-5, -5, 5, 5)), FVector::OneVector, Options, Error));
	TestEqual(TEXT("100cm2 does not get chunks"), Bindings.Num(), 0);
	TestTrue(TEXT("Threshold hole evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, WallWithHole(Box(-10, -10, 10, 10)), FVector::OneVector, Options, Error));
	TestTrue(TEXT("Exactly 400cm2 gets chunks"), Bindings.Num() > 0);
	const TArray<FBinding> Previous = Bindings;
	TestTrue(TEXT("Unchanged boundary evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, WallWithHole(Box(-10, -10, 10, 10)), FVector::OneVector, Options, Error));
	TestEqual(TEXT("No duplicated chunks"), Bindings.Num(), Previous.Num());
	for (int32 I = 0; I < Previous.Num(); ++I)
	{
		TestTrue(TEXT("Unchanged support keeps original anchor"), Bindings[I].Support == Previous[I].Support && Bindings[I].Seed == Previous[I].Seed);
		const FVector2d Center = (Bindings[I].Support[0] + Bindings[I].Support.Last()) * 0.5;
		const FVector2d Tangent = Bindings[I].Support.Last() - Bindings[I].Support[0];
		const FVector2d IntoHole(-Tangent.Y, Tangent.X);
		TestTrue(TEXT("Chunk points into the opening"), Center.Dot(IntoHole) < 0);
	}
	Bindings.Reset();
	PreviousEdges.Reset();
	TestTrue(TEXT("Nonuniform scale evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, WallWithHole(Box(-5, -5, 5, 5)), FVector(2, 1, 3), Options, Error));
	TestTrue(TEXT("100 local cm2 at 2x3 is 600 world cm2"), Bindings.Num() > 0);
	Bindings.Reset();
	PreviousEdges.Reset();
	TestTrue(TEXT("Below scaled threshold evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, WallWithHole(Box(-5, -5, 5, 5)), FVector(2, 1, 1), Options, Error));
	TestEqual(TEXT("200 world cm2 remains below threshold"), Bindings.Num(), 0);
	const TArray<FGeneralPolygon2d> Source = {FGeneralPolygon2d(Original)};
	TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Box(-25, -5, -10, 10)), FGeneralPolygon2d(Box(0, -5, 15, 10))};
	TArray<FGeneralPolygon2d> Remaining;
	PolygonsDifference(Source, Cutters, Remaining);
	TestTrue(TEXT("Separate holes evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	TestEqual(TEXT("Two independent 225cm2 holes do not qualify by their sum"), Bindings.Num(), 0);
	Cutters.Add(FGeneralPolygon2d(Box(-10, 0, 0, 5)));
	PolygonsDifference(Source, Cutters, Remaining);
	TestTrue(TEXT("Connected holes evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	TestTrue(TEXT("Connected 500cm2 opening creates chunks on old boundaries too"), Bindings.Num() > 0);
	TestTrue(TEXT("First threshold crossing decorates the preexisting hole edges"), Bindings.ContainsByPredicate([](const FBinding& Binding)
	{
		for (const FVector2d& Point : Binding.Support)
		{
			if (Point.X < -20 || Point.X > 10)
			{
				return true;
			}
		}
		return false;
	}));
	if (!Bindings.IsEmpty())
	{
		const FBinding Old = Bindings[0];
		const FVector2d End = Old.Support.Last();
		Cutters.Add(FGeneralPolygon2d(Box(End.X - 0.1, End.Y - 0.1, End.X + 0.1, End.Y + 0.1)));
		PolygonsDifference(Source, Cutters, Remaining);
		TestTrue(TEXT("Partial attachment cut evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
		TestFalse(TEXT("Losing part of support removes the original chunk"), Bindings.ContainsByPredicate([&](const FBinding& B) { return B.Support == Old.Support && B.Seed == Old.Seed; }));
	}
	TestTrue(TEXT("Full wall removal evaluated"), UpdateBindings(Bindings, PreviousEdges, Original, {}, FVector::OneVector, Options, Error));
	TestEqual(TEXT("Original outside border gets no chunks"), Bindings.Num(), 0);
	TestFalse(TEXT("Zero scale rejected without invalid geometry"), UpdateBindings(Bindings, PreviousEdges, Original, {}, FVector(0, 1, 1), Options, Error));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationPlateTest, "BreachSystem.Decoration.ProceduralPlate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationPlateTest::RunTest(const FString& Parameters)
{
	FBreachFeatureDecorationOptions Options;
	Options.AtlasSize = FIntPoint(4, 4);
	FBinding Binding;
	Binding.Support = {FVector2d(-11, 0), FVector2d(11, 0)};
	Binding.Length = 10;
	Binding.Seed = 42;
	const TArray<FBinding> Bindings = {Binding};
	FString Error;
	FDynamicMesh3 Unit;
	FDynamicMesh3 Scaled;
	FDynamicMesh3 Repeated;
	TestTrue(TEXT("Plate generates"), BuildFeatureMesh(Bindings, Options, FVector::OneVector, FVector2d(-5, 5), Unit, Error));
	TestTrue(TEXT("Nonuniform plate generates"), BuildFeatureMesh(Bindings, Options, FVector(2, 3, 4), FVector2d(-5, 5), Scaled, Error));
	TestTrue(TEXT("Seed repeats"), BuildFeatureMesh(Bindings, Options, FVector::OneVector, FVector2d(-5, 5), Repeated, Error));
	TestTrue(TEXT("Plate has closed solid thickness"), Unit.IsClosed());
	TestEqual(TEXT("One plate has seven samples in four rows"), Unit.VertexCount(), 28);
	TestEqual(TEXT("All six surfaces are present"), Unit.TriangleCount(), 52);
	for (int32 ID : Unit.VertexIndicesItr())
	{
		TestTrue(TEXT("Seed preserves every vertex"), Unit.GetVertex(ID) == Repeated.GetVertex(ID));
		TestFalse(TEXT("Vertices stay finite"), Unit.GetVertex(ID).ContainsNaN());
	}
	for (int32 ID : Unit.TriangleIndicesItr())
	{
		TestTrue(TEXT("No degenerate triangles"), Unit.GetTriArea(ID) > 1.e-9);
		const auto* UVs = Unit.Attributes()->PrimaryUV();
		const FIndex3i UVTriangle = UVs->GetTriangle(ID);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector2f UV = UVs->GetElement(UVTriangle[Corner]);
			TestTrue(TEXT("Atlas coordinates stay inside texture"), UV.X > 0 && UV.X < 1 && UV.Y > 0 && UV.Y < 1);
		}
	}
	TestTrue(TEXT("Root thickness is world cm at unit scale"), FMath::IsNearlyEqual(FMath::Abs(Unit.GetVertex(14).Y - Unit.GetVertex(0).Y), 1.4, 1.e-6));
	TestTrue(TEXT("Root thickness survives nonuniform scale"), FMath::IsNearlyEqual(FMath::Abs(Scaled.GetVertex(14).Y - Scaled.GetVertex(0).Y) * 3, 1.4, 1.e-6));
	TestTrue(TEXT("Root overlaps remaining wall"), Unit.GetVertex(0).Z < 0);
	TestTrue(TEXT("Free end extends into hole"), Unit.GetVertex(10).Z > 0);
	TestTrue(TEXT("Front faces outward"), Unit.GetTriNormal(0).Y < 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationClusterTest, "BreachSystem.Decoration.ClusteredBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationClusterTest::RunTest(const FString& Parameters)
{
	const FPolygon2d Original = Box(-600, -600, 600, 600);
	FPolygon2d Hole;
	// Many small edges must still share one arc-length placement stream.
	for (int32 I = 0; I < 200; ++I)
	{
		Hole.AppendVertex(FVector2d(-500 + I * 5, -100));
	}
	Hole.AppendVertex(FVector2d(500, -100));
	Hole.AppendVertex(FVector2d(500, 100));
	Hole.AppendVertex(FVector2d(-500, 100));
	TArray<FGeneralPolygon2d> Remaining;
	PolygonsDifference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Original)}, TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Hole)}, Remaining);
	TArray<FBinding> Bindings;
	TArray<FFeatureEdge> PreviousEdges;
	FBreachFeatureDecorationOptions Options;
	FString Error;
	TestTrue(TEXT("Subdivided boundary evaluates"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	TArray<double> Centers;
	for (const FBinding& B : Bindings)
	{
		if (B.Support[0].Y == -100 && B.Support.Last().Y == -100)
		{
			Centers.Add((B.Support[0].X + B.Support.Last().X) * 0.5);
		}
	}
	Centers.Sort();
	double Minimum = TNumericLimits<double>::Max();
	double Maximum = 0;
	for (int32 I = 1; I < Centers.Num(); ++I)
	{
		Minimum = FMath::Min(Minimum, Centers[I] - Centers[I - 1]);
		Maximum = FMath::Max(Maximum, Centers[I] - Centers[I - 1]);
	}
	TestTrue(TEXT("Long boundary has many plates"), Centers.Num() > 20);
	TestTrue(TEXT("Clusters contain close neighbors"), Minimum < Options.Spacing * 0.6);
	TestTrue(TEXT("Clusters leave large empty gaps"), Maximum > Options.Spacing * 1.8);
	const int32 Count = Bindings.Num();
	TestTrue(TEXT("Repeat evaluates"), UpdateBindings(Bindings, PreviousEdges, Original, Remaining, FVector::OneVector, Options, Error));
	TestEqual(TEXT("Repeat adds no duplicate plates"), Bindings.Num(), Count);
	FDynamicMesh3 Mesh;
	TestTrue(TEXT("Subdivided supports build real plates"), BuildFeatureMesh(Bindings, Options, FVector::OneVector, FVector2d(-5, 5), Mesh, Error));
	TestTrue(TEXT("All accepted supports produce closed plates"), Mesh.IsClosed() && Mesh.VertexCount() == Bindings.Num() * 28);
	return !HasAnyErrors();
}
#endif
