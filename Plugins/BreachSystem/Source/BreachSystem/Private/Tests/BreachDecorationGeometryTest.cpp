#include "Geometry/BreachDecorationGeometry.h"
#include "Geometry/BreachSimpleCollision.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"

// 테스트 전용: 클리핑·UV·월드 면적과 부착 구간 보존, 그리고 변경 영역만 다시 계산하는지를 독립된 수치로 검증한다.
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

	// 표면 컴포넌트와 같은 순서로 구멍 캐시에 절단을 누적한다. Deco는 이 캐시에서 바뀐 구멍만 읽는다.
	struct FDecoWall
	{
		FBreachSimpleCollision Holes;

		explicit FDecoWall(const FPolygon2d& Outer)
		{
			Holes.Initialize(Outer, FVector2d(-5, 5));
		}

		bool Cut(const FPolygon2d& Cutter)
		{
			FString Error;
			return Holes.ApplyCut(Cutter, TConstArrayView<FPolygon2d>(), Error);
		}

		// 가장 최근 절단이 만든 구멍의 변경 번호.
		uint64 LastRevision() const
		{
			return Holes.GetRevision() - 1;
		}
	};

	double PatchArea(const FCutState& State)
	{
		double Area = 0;
		for (const FPatch& Patch : State.Patches)
		{
			for (const FGeneralPolygon2d& Region : Patch.Regions)
			{
				Area += FMath::Abs(Region.SignedArea());
			}
		}
		return Area;
	}

	TArray<FBinding> AllBindings(const FFeatureState& State)
	{
		TArray<FBinding> Result;
		for (const TPair<uint64, FFeatureHole>& Hole : State.Holes)
		{
			Result.Append(Hole.Value.Bindings);
		}
		return Result;
	}

	int32 MeshVertexTotal(const FFeatureState& State)
	{
		int32 Total = 0;
		for (const TPair<uint64, FFeatureHole>& Hole : State.Holes)
		{
			for (const FBinding& Binding : Hole.Value.Bindings)
			{
				Total += Binding.MeshVertices.Num();
			}
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationPatchTest, "BreachSystem.Decoration.PatchGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationPatchTest::RunTest(const FString& Parameters)
{
	FDecoWall Wall(Box(-50, -50, 50, 50));
	FCutState State;
	FString Error;
	const FPolygon2d Small = Box(-5, -5, 5, 5);
	const TArray<FPolygon2d> SmallRemoved = {Small};
	const TArray<FAxisAlignedBox2d> Cuts = {Small.Bounds()};
	const TArray<FPolygon2d> NothingRemoved;
	const TArray<FAxisAlignedBox2d> NoCuts;
	TestTrue(TEXT("Small cut recorded"), Wall.Cut(Small));
	TestTrue(TEXT("Initial patch clips"), ClipPatches(State, Wall.Holes, SmallRemoved, Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestTrue(TEXT("20x20 footprint minus 10x10 hole is 300"), FMath::IsNearlyEqual(PatchArea(State), 300.0, 1.e-6));
	if (!TestEqual(TEXT("One patch"), State.Patches.Num(), 1))
	{
		return false;
	}
	const FAxisAlignedBox2d OriginalUV = State.Patches[0].UVBounds;
	const FPolygon2d Wide = Box(-10, -5, 10, 5);
	const TArray<FPolygon2d> WideRemoved = {Wide};
	TestTrue(TEXT("Wider cut recorded"), Wall.Cut(Wide));
	TestTrue(TEXT("Re-cut clips old patch"), ClipPatches(State, Wall.Holes, WideRemoved, NoCuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestTrue(TEXT("Remaining old patch area is 200"), FMath::IsNearlyEqual(PatchArea(State), 200.0, 1.e-6));
	TestTrue(TEXT("UV origin survives clipping"), State.Patches[0].UVBounds.Min == OriginalUV.Min);
	TestTrue(TEXT("UV extent survives clipping"), State.Patches[0].UVBounds.Max == OriginalUV.Max);
	FDynamicMesh3 Mesh;
	bool bChanged = false;
	TestTrue(TEXT("Clipped mesh triangulates"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
	TestTrue(TEXT("First mesh update reports a change"), bChanged);
	const auto* UVs = Mesh.Attributes()->PrimaryUV();
	const auto* Normals = Mesh.Attributes()->PrimaryNormals();
	for (int32 ID : Mesh.TriangleIndicesItr())
	{
		const FIndex3i Triangle = Mesh.GetTriangle(ID);
		const FIndex3i UVTriangle = UVs->GetTriangle(ID);
		TestTrue(TEXT("Front faces point outward"), Mesh.GetTriNormal(ID).Y < -0.99);
		TestTrue(TEXT("Each added triangle carries its own normal"), Normals->IsSetTriangle(ID));
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d P = Mesh.GetVertex(Triangle[Corner]);
			const FVector2f UV = UVs->GetElement(UVTriangle[Corner]);
			TestTrue(TEXT("Original UV mapping remains unchanged"), UV.Equals(FVector2f((P.X + 10) / 20, (P.Z + 10) / 20), 1.e-5f));
		}
	}
	const double Before = PatchArea(State);
	TestTrue(TEXT("Duplicate footprint is accepted"), ClipPatches(State, Wall.Holes, NothingRemoved, Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestTrue(TEXT("Same-face patches never overlap"), FMath::IsNearlyEqual(PatchArea(State), Before, 1.e-6));
	TestTrue(TEXT("Opposite face is independent"), ClipPatches(State, Wall.Holes, NothingRemoved, Cuts, EBreachSurfaceSide::Back, FVector::OneVector, 5, Error));
	TestTrue(TEXT("Opposite face adds its own patch"), FMath::IsNearlyEqual(PatchArea(State), Before * 2, 1.e-6));
	const FPolygon2d Everything = Box(-60, -60, 60, 60);
	const TArray<FPolygon2d> EverythingRemoved = {Everything};
	TestTrue(TEXT("Full removal recorded"), Wall.Cut(Everything));
	TestTrue(TEXT("Full removal clears patches"), ClipPatches(State, Wall.Holes, EverythingRemoved, NoCuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
	TestEqual(TEXT("No floating traces remain"), State.Patches.Num(), 0);
	TestTrue(TEXT("Mesh drops the removed patch"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
	TestTrue(TEXT("Removing the last patch reports a change"), bChanged);
	TestEqual(TEXT("No floating triangles remain"), Mesh.TriangleCount(), 0);
	TestEqual(TEXT("No orphaned vertices remain"), Mesh.VertexCount(), 0);

	FCutState Unavailable;
	FBreachSimpleCollision Uninitialized;
	TestFalse(TEXT("Missing hole cache is rejected instead of guessing the wall"), ClipPatches(Unavailable, Uninitialized, SmallRemoved, Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
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
	FCutState State;
	State.Patches.Add(Patch);
	FDynamicMesh3 Mesh;
	FString Error;
	bool bChanged = false;
	AddExpectedMessage(TEXT("Skipped unsafe cut decoration region"), EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("A failed region does not reject the face or trigger the owner's global clear"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
	double Area = 0;
	for (int32 ID : Mesh.TriangleIndicesItr())
	{
		Area += Mesh.GetTriArea(ID);
	}
	TestTrue(TEXT("Safe regions before and after the failure retain their full 125cm2"), FMath::IsNearlyEqual(Area, 125.0, 1.e-6));
	TestTrue(TEXT("Skipped region leaves no fatal display error"), Error.IsEmpty());
	TestTrue(TEXT("The failed region is not retried on the next update"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
	TestFalse(TEXT("An already processed patch leaves the mesh untouched"), bChanged);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationLocalUpdateTest, "BreachSystem.Decoration.LocalUpdate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationLocalUpdateTest::RunTest(const FString& Parameters)
{
	// 테스트 전용: 한 브리칭이 멀리 떨어진 패치·구멍·조각의 기록과 표시 정점을 건드리지 않는지 검증한다.
	const TArray<FPolygon2d> NothingRemoved;
	const TArray<FAxisAlignedBox2d> NoCuts;
	FString Error;
	bool bChanged = false;

	// Cut 패치
	{
		FDecoWall Wall(Box(-50, -50, 50, 50));
		FCutState State;
		FDynamicMesh3 Mesh;
		const FPolygon2d Left = Box(-32, -2, -28, 2);
		const FPolygon2d Right = Box(28, -2, 32, 2);
		for (const FPolygon2d& Cutter : {Left, Right})
		{
			const TArray<FPolygon2d> Removed = {Cutter};
			const TArray<FAxisAlignedBox2d> Cuts = {Cutter.Bounds()};
			TestTrue(TEXT("Independent cut recorded"), Wall.Cut(Cutter));
			TestTrue(TEXT("Independent cut clips"), ClipPatches(State, Wall.Holes, Removed, Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
			TestTrue(TEXT("Independent cut meshes"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
		}
		if (!TestEqual(TEXT("Two separate patches"), State.Patches.Num(), 2))
		{
			return false;
		}
		const TArray<int32> LeftBefore = State.Patches[0].MeshVertices;
		const TArray<int32> RightBefore = State.Patches[1].MeshVertices;
		const TArray<FGeneralPolygon2d> RightRegions = State.Patches[1].Regions;

		const FPolygon2d Widen = Box(-28, -2, -24, 2);
		const TArray<FPolygon2d> Removed = {Widen};
		const TArray<FAxisAlignedBox2d> Cuts = {Widen.Bounds()};
		TestTrue(TEXT("Local widening recorded"), Wall.Cut(Widen));
		TestTrue(TEXT("Local widening clips"), ClipPatches(State, Wall.Holes, Removed, Cuts, EBreachSurfaceSide::Front, FVector::OneVector, 5, Error));
		TestTrue(TEXT("The distant patch keeps its display vertices through clipping"), State.Patches[1].bInMesh && State.Patches[1].MeshVertices == RightBefore);
		TestFalse(TEXT("The clipped patch is queued for the display"), State.Patches[0].bInMesh);
		TestTrue(TEXT("Local widening meshes"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
		TestTrue(TEXT("Local widening changes the display"), bChanged);
		TestTrue(TEXT("The distant patch regions are untouched"), State.Patches[1].Regions.Num() == RightRegions.Num() && FMath::IsNearlyEqual(FMath::Abs(State.Patches[1].Regions[0].SignedArea()), FMath::Abs(RightRegions[0].SignedArea()), 1.e-12));
		TestTrue(TEXT("The distant patch still owns the same display vertices"), State.Patches[1].MeshVertices == RightBefore);
		for (int32 VertexID : RightBefore)
		{
			TestTrue(TEXT("Distant display vertices survive the local update"), Mesh.IsVertex(VertexID));
		}
		int32 Owned = 0;
		for (const FPatch& Patch : State.Patches)
		{
			Owned += Patch.MeshVertices.Num();
		}
		TestEqual(TEXT("Replaced vertices are removed instead of leaking"), Mesh.VertexCount(), Owned);
		TestTrue(TEXT("A breach elsewhere on the other face clips nothing here"), ClipPatches(State, Wall.Holes, NothingRemoved, NoCuts, EBreachSurfaceSide::Back, FVector::OneVector, 5, Error));
		TestTrue(TEXT("No-change update succeeds"), UpdatePatchMesh(State, EBreachSurfaceSide::Front, -50.05, Mesh, bChanged, Error));
		TestFalse(TEXT("No-change update leaves the display mesh untouched"), bChanged);
	}

	// Feature 조각
	{
		FDecoWall Wall(Box(-300, -300, 300, 300));
		FFeatureState State;
		FBreachFeatureDecorationOptions Options;
		FDynamicMesh3 Mesh;
		const FPolygon2d Left = Box(-250, -100, -50, 100);
		const FPolygon2d Right = Box(50, -100, 250, 100);
		const TArray<FPolygon2d> LeftRemoved = {Left};
		const TArray<FPolygon2d> RightRemoved = {Right};
		TestTrue(TEXT("Left opening recorded"), Wall.Cut(Left));
		const uint64 LeftRevision = Wall.LastRevision();
		TestTrue(TEXT("Left opening evaluates"), UpdateBindings(State, Wall.Holes, LeftRemoved, FVector::OneVector, Options, Error));
		TestTrue(TEXT("Right opening recorded"), Wall.Cut(Right));
		TestTrue(TEXT("Right opening evaluates"), UpdateBindings(State, Wall.Holes, RightRemoved, FVector::OneVector, Options, Error));
		TestTrue(TEXT("Both openings mesh"), UpdateFeatureMesh(State, Options, FVector::OneVector, FVector2d(-5, 5), Mesh, bChanged, Error));
		const FFeatureHole* LeftHole = State.Holes.Find(LeftRevision);
		if (!TestNotNull(TEXT("Left opening keeps its own record"), LeftHole))
		{
			return false;
		}
		TestTrue(TEXT("Left opening has plates"), LeftHole->Bindings.Num() > 0);
		TArray<TArray<int32>> LeftVertices;
		for (const FBinding& Binding : LeftHole->Bindings)
		{
			LeftVertices.Add(Binding.MeshVertices);
		}
		const int32 TotalBefore = AllBindings(State).Num();

		const FPolygon2d Notch = Box(240, -10, 270, 10);
		const TArray<FPolygon2d> NotchRemoved = {Notch};
		TestTrue(TEXT("Notch on the right opening recorded"), Wall.Cut(Notch));
		TestTrue(TEXT("Notch evaluates"), UpdateBindings(State, Wall.Holes, NotchRemoved, FVector::OneVector, Options, Error));
		TestTrue(TEXT("Notch meshes"), UpdateFeatureMesh(State, Options, FVector::OneVector, FVector2d(-5, 5), Mesh, bChanged, Error));
		TestTrue(TEXT("Notch changes the display"), bChanged);
		LeftHole = State.Holes.Find(LeftRevision);
		if (!TestNotNull(TEXT("The untouched opening keeps its revision record"), LeftHole))
		{
			return false;
		}
		if (TestEqual(TEXT("The untouched opening keeps every plate"), LeftHole->Bindings.Num(), LeftVertices.Num()))
		{
			for (int32 Index = 0; Index < LeftVertices.Num(); ++Index)
			{
				TestTrue(TEXT("Plates on the untouched opening keep their display vertices"), LeftHole->Bindings[Index].MeshVertices == LeftVertices[Index]);
			}
		}
		TestTrue(TEXT("The notch adds plates on its new boundary"), AllBindings(State).Num() > TotalBefore - 4);
		TestEqual(TEXT("Removed plates leave no orphaned vertices"), Mesh.VertexCount(), MeshVertexTotal(State));
		TestTrue(TEXT("The display stays a set of closed plates"), Mesh.IsClosed());
		for (int32 ID : Mesh.TriangleIndicesItr())
		{
			TestTrue(TEXT("Every remaining plate triangle keeps a normal"), Mesh.Attributes()->PrimaryNormals()->IsSetTriangle(ID));
		}
		const TArray<FPolygon2d> NothingChanged;
		TestTrue(TEXT("Repeat evaluates"), UpdateBindings(State, Wall.Holes, NothingChanged, FVector::OneVector, Options, Error));
		TestTrue(TEXT("Repeat meshes"), UpdateFeatureMesh(State, Options, FVector::OneVector, FVector2d(-5, 5), Mesh, bChanged, Error));
		TestFalse(TEXT("An update without a breach leaves the feature display untouched"), bChanged);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationLocalCutObservationTest, "BreachSystem.Decoration.LocalCutObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationLocalCutObservationTest::RunTest(const FString& Parameters)
{
	// 테스트 전용: Cut 패치·표시·Clear 경로 없이 국소 절삭이 반대편 Feature 배치에 미치는 영향을 기록한다.
	FDecoWall Wall(Box(-300, -300, 300, 300));
	FFeatureState State;
	FBreachFeatureDecorationOptions Options;
	FString Error;
	const FPolygon2d Opening = Box(-100, -100, 100, 100);
	const TArray<FPolygon2d> OpeningRemoved = {Opening};
	TestTrue(TEXT("Initial opening recorded"), Wall.Cut(Opening));
	TestTrue(TEXT("Initial feature placement succeeds"), UpdateBindings(State, Wall.Holes, OpeningRemoved, FVector::OneVector, Options, Error));
	const TArray<FBinding> Before = AllBindings(State);
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
	const FPolygon2d Notch = Box(-120, -15, -90, 15);
	const TArray<FPolygon2d> NotchRemoved = {Notch};
	TestTrue(TEXT("Local left-edge notch recorded"), Wall.Cut(Notch));
	TestTrue(TEXT("Feature-only update succeeds without a display failure"), UpdateBindings(State, Wall.Holes, NotchRemoved, FVector::OneVector, Options, Error));
	const TArray<FBinding> AfterNotch = AllBindings(State);
	int32 RemoteBefore = 0;
	int32 RemoteRetained = 0;
	int32 RemoteAdded = 0;
	for (const FBinding& Old : Before)
	{
		if (IsRemote(Old))
		{
			++RemoteBefore;
			if (AfterNotch.ContainsByPredicate([&](const FBinding& Current) { return Current.Support == Old.Support && Current.Seed == Old.Seed && Current.Length == Old.Length; }))
			{
				++RemoteRetained;
			}
		}
	}
	for (const FBinding& Current : AfterNotch)
	{
		if (IsRemote(Current) && !Before.ContainsByPredicate([&](const FBinding& Old) { return Current.Support == Old.Support && Current.Seed == Old.Seed; }))
		{
			++RemoteAdded;
		}
	}
	TestTrue(TEXT("Probe has existing plates on the untouched right edge"), RemoteBefore > 0);
	TestEqual(TEXT("Existing remote attachments retain their positions and shape seeds"), RemoteRetained, RemoteBefore);
	TestEqual(TEXT("A local notch must not add plates to the untouched right edge"), RemoteAdded, 0);
	TestTrue(TEXT("The notch boundary itself receives plates"), AfterNotch.ContainsByPredicate([](const FBinding& Binding)
	{
		for (const FVector2d& Point : Binding.Support)
		{
			if (Point.X < -100 - 1.e-6)
			{
				return true;
			}
		}
		return false;
	}));
	AddInfo(FString::Printf(TEXT("LocalCutObservation: left edge extended from X=-100 to -120; untouched right-edge plates before=%d, retained=%d, newly added=%d; Cut decoration, display rebuild and Clear were never called."), RemoteBefore, RemoteRetained, RemoteAdded));
	const TArray<FPolygon2d> NothingRemoved;
	TestTrue(TEXT("Repeated post-notch geometry evaluates"), UpdateBindings(State, Wall.Holes, NothingRemoved, FVector::OneVector, Options, Error));
	TestEqual(TEXT("Repeated post-notch geometry does not refill old gaps"), AllBindings(State).Num(), AfterNotch.Num());
	const TArray<FBinding> BeforeExtension = AllBindings(State);
	const FPolygon2d Extension = Box(-140, -100, -90, 100);
	const TArray<FPolygon2d> ExtensionRemoved = {Extension};
	TestTrue(TEXT("Collinear extension recorded"), Wall.Cut(Extension));
	TestTrue(TEXT("Collinear extension evaluates"), UpdateBindings(State, Wall.Holes, ExtensionRemoved, FVector::OneVector, Options, Error));
	int32 NewOnOldBoundary = 0;
	int32 NewOnExtension = 0;
	for (const FBinding& Current : AllBindings(State))
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
	FString Error;
	const TArray<FPolygon2d> NothingRemoved;
	const FPolygon2d Small = Box(-5, -5, 5, 5);
	const TArray<FPolygon2d> SmallRemoved = {Small};
	{
		FDecoWall Wall(Original);
		FFeatureState State;
		TestTrue(TEXT("Small hole recorded"), Wall.Cut(Small));
		TestTrue(TEXT("Small hole evaluated"), UpdateBindings(State, Wall.Holes, SmallRemoved, FVector::OneVector, Options, Error));
		TestEqual(TEXT("100cm2 does not get chunks"), AllBindings(State).Num(), 0);
		const FPolygon2d Threshold = Box(-10, -10, 10, 10);
		const TArray<FPolygon2d> ThresholdRemoved = {Threshold};
		TestTrue(TEXT("Threshold hole recorded"), Wall.Cut(Threshold));
		TestTrue(TEXT("Threshold hole evaluated"), UpdateBindings(State, Wall.Holes, ThresholdRemoved, FVector::OneVector, Options, Error));
		TestTrue(TEXT("Exactly 400cm2 gets chunks"), AllBindings(State).Num() > 0);
		const TArray<FBinding> Previous = AllBindings(State);
		TestTrue(TEXT("Unchanged boundary evaluated"), UpdateBindings(State, Wall.Holes, NothingRemoved, FVector::OneVector, Options, Error));
		const TArray<FBinding> Bindings = AllBindings(State);
		if (TestEqual(TEXT("No duplicated chunks"), Bindings.Num(), Previous.Num()))
		{
			for (int32 I = 0; I < Previous.Num(); ++I)
			{
				TestTrue(TEXT("Unchanged support keeps original anchor"), Bindings[I].Support == Previous[I].Support && Bindings[I].Seed == Previous[I].Seed);
				const FVector2d Center = (Bindings[I].Support[0] + Bindings[I].Support.Last()) * 0.5;
				const FVector2d Tangent = Bindings[I].Support.Last() - Bindings[I].Support[0];
				const FVector2d IntoHole(-Tangent.Y, Tangent.X);
				TestTrue(TEXT("Chunk points into the opening"), Center.Dot(IntoHole) < 0);
			}
		}
		const FPolygon2d Everything = Box(-60, -60, 60, 60);
		const TArray<FPolygon2d> EverythingRemoved = {Everything};
		TestTrue(TEXT("Full wall removal recorded"), Wall.Cut(Everything));
		TestTrue(TEXT("Full wall removal evaluated"), UpdateBindings(State, Wall.Holes, EverythingRemoved, FVector::OneVector, Options, Error));
		TestEqual(TEXT("Original outside border gets no chunks"), AllBindings(State).Num(), 0);
		TestFalse(TEXT("Zero scale rejected without invalid geometry"), UpdateBindings(State, Wall.Holes, NothingRemoved, FVector(0, 1, 1), Options, Error));
	}
	{
		FDecoWall Wall(Original);
		FFeatureState State;
		TestTrue(TEXT("Scaled hole recorded"), Wall.Cut(Small));
		TestTrue(TEXT("Nonuniform scale evaluated"), UpdateBindings(State, Wall.Holes, SmallRemoved, FVector(2, 1, 3), Options, Error));
		TestTrue(TEXT("100 local cm2 at 2x3 is 600 world cm2"), AllBindings(State).Num() > 0);
		FFeatureState Below;
		TestTrue(TEXT("Below scaled threshold evaluated"), UpdateBindings(Below, Wall.Holes, SmallRemoved, FVector(2, 1, 1), Options, Error));
		TestEqual(TEXT("200 world cm2 remains below threshold"), AllBindings(Below).Num(), 0);
	}
	{
		FDecoWall Wall(Original);
		FFeatureState State;
		const FPolygon2d Left = Box(-25, -5, -10, 10);
		const FPolygon2d Right = Box(0, -5, 15, 10);
		const TArray<FPolygon2d> BothRemoved = {Left, Right};
		TestTrue(TEXT("Left hole recorded"), Wall.Cut(Left));
		TestTrue(TEXT("Right hole recorded"), Wall.Cut(Right));
		TestTrue(TEXT("Separate holes evaluated"), UpdateBindings(State, Wall.Holes, BothRemoved, FVector::OneVector, Options, Error));
		TestEqual(TEXT("Two independent 225cm2 holes do not qualify by their sum"), AllBindings(State).Num(), 0);
		const FPolygon2d Bridge = Box(-10, 0, 0, 5);
		const TArray<FPolygon2d> BridgeRemoved = {Bridge};
		TestTrue(TEXT("Bridge recorded"), Wall.Cut(Bridge));
		TestTrue(TEXT("Connected holes evaluated"), UpdateBindings(State, Wall.Holes, BridgeRemoved, FVector::OneVector, Options, Error));
		TArray<FBinding> Bindings = AllBindings(State);
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
			const FPolygon2d Nick = Box(End.X - 0.1, End.Y - 0.1, End.X + 0.1, End.Y + 0.1);
			const TArray<FPolygon2d> NickRemoved = {Nick};
			TestTrue(TEXT("Partial attachment cut recorded"), Wall.Cut(Nick));
			TestTrue(TEXT("Partial attachment cut evaluated"), UpdateBindings(State, Wall.Holes, NickRemoved, FVector::OneVector, Options, Error));
			TestFalse(TEXT("Losing part of support removes the original chunk"), AllBindings(State).ContainsByPredicate([&](const FBinding& B) { return B.Support == Old.Support && B.Seed == Old.Seed; }));
		}
	}
	FFeatureState Unavailable;
	FBreachSimpleCollision Uninitialized;
	TestFalse(TEXT("Missing hole cache is rejected instead of keeping stale plates"), UpdateBindings(Unavailable, Uninitialized, NothingRemoved, FVector::OneVector, Options, Error));
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
	FString Error;
	auto Build = [&](const FVector& Scale, FDynamicMesh3& Mesh)
	{
		FFeatureState State;
		State.Holes.Add(1).Bindings.Add(Binding);
		bool bChanged = false;
		return UpdateFeatureMesh(State, Options, Scale, FVector2d(-5, 5), Mesh, bChanged, Error) && bChanged;
	};
	FDynamicMesh3 Unit;
	FDynamicMesh3 Scaled;
	FDynamicMesh3 Repeated;
	TestTrue(TEXT("Plate generates"), Build(FVector::OneVector, Unit));
	TestTrue(TEXT("Nonuniform plate generates"), Build(FVector(2, 3, 4), Scaled));
	TestTrue(TEXT("Seed repeats"), Build(FVector::OneVector, Repeated));
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

// 테스트 전용: 조각 크기·비균일 스케일에 독립적인 무늬 밀도와 깊이 중앙의 평평한 일정 두께를 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationPlateMappingTest, "BreachSystem.Decoration.PlateWorldMapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationPlateMappingTest::RunTest(const FString& Parameters)
{
	FBreachFeatureDecorationOptions Options;
	const FVector Scales[] = {FVector::OneVector, FVector(2, 3, 4)};
	for (const FVector& Scale : Scales)
	{
		for (double Width : {12.0, 44.0})
		{
			Options.TextureWorldSize = Width < 20 ? FVector2D(20, 20) : FVector2D(10, 40);
			FFeatureState State;
			FBinding Binding;
			Binding.Support = {FVector2d(-Width * 0.5, 0), FVector2d(Width * 0.5, 0)};
			Binding.Length = Width;
			Binding.Seed = 42;
			State.Holes.Add(1).Bindings.Add(Binding);
			FDynamicMesh3 Mesh;
			bool bChanged = false;
			FString Error;
			if (!TestTrue(TEXT("Mapping fixture generates"), UpdateFeatureMesh(State, Options, Scale, FVector2d(8, 28), Mesh, bChanged, Error)))
			{
				return false;
			}
			const auto* UVs = Mesh.Attributes()->PrimaryUV();
			const FVector3d Origin = Mesh.GetVertex(0);
			const FVector2f OriginUV = UVs->GetElement(0);
			bool bFixedDensity = true;
			bool bAlignedFaces = true;
			bool bCenteredRoots = true;
			for (int32 Index = 0; Index < 14; ++Index)
			{
				const FVector3d P = Mesh.GetVertex(Index);
				const FVector2f UV = UVs->GetElement(Index);
				bFixedDensity &= FMath::IsNearlyEqual(static_cast<double>(UV.X - OriginUV.X), (P.X - Origin.X) * Scale.X / Options.TextureWorldSize.X, 1.e-5);
				bFixedDensity &= FMath::IsNearlyEqual(static_cast<double>(UV.Y - OriginUV.Y), (P.Z - Origin.Z) * Scale.Z / Options.TextureWorldSize.Y, 1.e-5);
				bAlignedFaces &= UV == UVs->GetElement(Index + 14);
				if (Index < 7)
				{
					bCenteredRoots &= FMath::IsNearlyEqual((P.Y + Mesh.GetVertex(Index + 14).Y) * 0.5, 18.0, 1.e-6);
				}
			}
			TestTrue(TEXT("Texture world size sets density independently of plate width and length"), bFixedDensity);
			TestTrue(TEXT("Front and back masks share the same coordinates"), bAlignedFaces);
			TestTrue(TEXT("Root midplane stays at the middle of asymmetric wall depth"), bCenteredRoots);
			bool bFlatFaces = true;
			for (int32 Index = 0; Index < 28; ++Index)
			{
				const double ExpectedDepth = 18.0 + (Index < 14 ? -0.5 : 0.5) * Options.Thickness / FMath::Abs(Scale.Y);
				bFlatFaces &= FMath::IsNearlyEqual(Mesh.GetVertex(Index).Y, ExpectedDepth, 1.e-6);
			}
			TestTrue(TEXT("Every front and back vertex lies on its constant-depth plane with uniform world thickness"), bFlatFaces);
		}
	}
	FFeatureState InvalidState;
	FDynamicMesh3 InvalidMesh;
	bool bChanged = false;
	FString Error;
	Options.TextureWorldSize.X = 0;
	TestFalse(TEXT("Zero texture world size is rejected"), UpdateFeatureMesh(InvalidState, Options, FVector::OneVector, FVector2d(8, 28), InvalidMesh, bChanged, Error));
	TestFalse(TEXT("Invalid texture dimensions do not mutate the display"), bChanged);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachDecorationClusterTest, "BreachSystem.Decoration.ClusteredBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachDecorationClusterTest::RunTest(const FString& Parameters)
{
	FDecoWall Wall(Box(-600, -600, 600, 600));
	FPolygon2d Hole;
	// Many small edges must still share one arc-length placement stream.
	for (int32 I = 0; I < 200; ++I)
	{
		Hole.AppendVertex(FVector2d(-500 + I * 5, -100));
	}
	Hole.AppendVertex(FVector2d(500, -100));
	Hole.AppendVertex(FVector2d(500, 100));
	Hole.AppendVertex(FVector2d(-500, 100));
	const TArray<FPolygon2d> HoleRemoved = {Hole};
	const TArray<FPolygon2d> NothingRemoved;
	FFeatureState State;
	FBreachFeatureDecorationOptions Options;
	FString Error;
	TestTrue(TEXT("Subdivided opening recorded"), Wall.Cut(Hole));
	TestTrue(TEXT("Subdivided boundary evaluates"), UpdateBindings(State, Wall.Holes, HoleRemoved, FVector::OneVector, Options, Error));
	TArray<double> Centers;
	for (const FBinding& B : AllBindings(State))
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
	const int32 Count = AllBindings(State).Num();
	TestTrue(TEXT("Repeat evaluates"), UpdateBindings(State, Wall.Holes, NothingRemoved, FVector::OneVector, Options, Error));
	TestEqual(TEXT("Repeat adds no duplicate plates"), AllBindings(State).Num(), Count);
	FDynamicMesh3 Mesh;
	bool bChanged = false;
	TestTrue(TEXT("Subdivided supports build real plates"), UpdateFeatureMesh(State, Options, FVector::OneVector, FVector2d(-5, 5), Mesh, bChanged, Error));
	TestTrue(TEXT("All accepted supports produce closed plates"), Mesh.IsClosed() && Mesh.VertexCount() == Count * 28);
	return !HasAnyErrors();
}
#endif
