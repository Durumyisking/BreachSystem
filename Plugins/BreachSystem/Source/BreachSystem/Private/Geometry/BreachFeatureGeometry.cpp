#include "Geometry/BreachDecorationGeometry.h"
#include "Geometry/BreachSimpleCollision.h"
#include "Algo/BinarySearch.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

using namespace UE::Geometry;
using BreachDecoration::FFeatureEdge;

#include "BreachSystemLog.h"

namespace
{
	constexpr int32 Samples = 7;
	constexpr double Tolerance = UE_DOUBLE_KINDA_SMALL_NUMBER;

	struct FSupportPath
	{
		TArray<FVector2d> Points;
		TArray<double> Arc;
		double Length = 0;
	};

	bool ValidOptions(const FBreachFeatureDecorationOptions& Options, const FVector& Scale, FString& Error)
	{
		if (Scale.ContainsNaN() || FMath::Abs(Scale.X) <= UE_DOUBLE_KINDA_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_DOUBLE_KINDA_SMALL_NUMBER || FMath::Abs(Scale.Z) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			Error = TEXT("Feature Deco requires a finite nonzero surface scale.");
			return false;
		}
		if (!FMath::IsFinite(Options.MinHoleArea) || Options.MinHoleArea < 0 || !FMath::IsFinite(Options.Spacing) || Options.Spacing < 0.1 || !FMath::IsFinite(Options.Width) || Options.Width < 0.1 || !FMath::IsFinite(Options.Length) || Options.Length < 0.1 || !FMath::IsFinite(Options.Thickness) || Options.Thickness < 0.01 || !FMath::IsFinite(Options.Roughness) || Options.Roughness < 0 || Options.Roughness > 1 || Options.AtlasSize.X < 1 || Options.AtlasSize.Y < 1)
		{
			Error = TEXT("Feature Deco requires finite positive dimensions/spacing, nonnegative area, roughness 0..1 and a positive atlas size.");
			return false;
		}
		if (Options.TextureWorldSize.ContainsNaN() || Options.TextureWorldSize.X < 0.1 || Options.TextureWorldSize.Y < 0.1)
		{
			Error = TEXT("Feature Deco requires a finite texture world size of at least 0.1cm per axis.");
			return false;
		}
		return true;
	}

	FSupportPath MakePath(TConstArrayView<FVector2d> Points, const FVector2d& Metric)
	{
		FSupportPath Path;
		for (const FVector2d& Point : Points)
		{
			if (!Path.Points.IsEmpty())
			{
				const double Step = ((Point - Path.Points.Last()) * Metric).Length();
				if (Step <= UE_DOUBLE_KINDA_SMALL_NUMBER)
				{
					continue;
				}
				Path.Length += Step;
			}
			Path.Points.Add(Point);
			Path.Arc.Add(Path.Length);
		}
		return Path;
	}

	FVector2d PointAt(const FSupportPath& Path, double Distance, FVector2d* OutTangent = nullptr)
	{
		check(Path.Points.Num() >= 2);
		const double Target = FMath::Clamp(Distance, 0.0, Path.Length);
		// 누적 길이는 단조 증가하므로 이분 탐색한다. 둘레가 긴 구멍에서 후보마다 처음부터 훑지 않는다.
		const int32 Index = FMath::Clamp(Algo::LowerBound(Path.Arc, Target), 1, Path.Arc.Num() - 1);
		const FVector2d Delta = Path.Points[Index] - Path.Points[Index - 1];
		if (OutTangent)
		{
			*OutTangent = Delta;
		}
		const double Alpha = (Target - Path.Arc[Index - 1]) / (Path.Arc[Index] - Path.Arc[Index - 1]);
		return Path.Points[Index - 1] + Alpha * Delta;
	}

	TArray<FVector2d> Subpath(const FSupportPath& Path, double Start, double End)
	{
		TArray<FVector2d> Points;
		Points.Add(PointAt(Path, Start));
		for (int32 Index = 1; Index < Path.Arc.Num() - 1; ++Index)
		{
			if (Path.Arc[Index] > Start + UE_DOUBLE_KINDA_SMALL_NUMBER && Path.Arc[Index] < End - UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				Points.Add(Path.Points[Index]);
			}
		}
		Points.Add(PointAt(Path, End));
		return Points;
	}

	bool Covers(const FVector2d& A, const FVector2d& B, TConstArrayView<FFeatureEdge> Edges)
	{
		const FVector2d Delta = B - A;
		const double Length = Delta.Length();
		if (Length <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FVector2d Axis = Delta / Length;
		TArray<FVector2d> Intervals;
		for (const FFeatureEdge& Edge : Edges)
		{
			const FVector2d FromA = Edge.A - A;
			const FVector2d FromB = Edge.B - A;
			if (FMath::Abs(Axis.X * FromA.Y - Axis.Y * FromA.X) <= Tolerance && FMath::Abs(Axis.X * FromB.Y - Axis.Y * FromB.X) <= Tolerance)
			{
				const double T0 = Axis.Dot(FromA);
				const double T1 = Axis.Dot(FromB);
				Intervals.Add(FVector2d(FMath::Min(T0, T1), FMath::Max(T0, T1)));
			}
		}
		Intervals.Sort([](const FVector2d& L, const FVector2d& R) { return L.X < R.X; });
		double Covered = 0;
		for (const FVector2d& Interval : Intervals)
		{
			if (Interval.X > Covered + Tolerance)
			{
				return false;
			}
			Covered = FMath::Max(Covered, Interval.Y);
			if (Covered >= Length - Tolerance)
			{
				return true;
			}
		}
		return false;
	}

	bool OnSupport(const FVector2d& Point, TConstArrayView<BreachDecoration::FBinding> Existing, const FVector2d& Metric)
	{
		const FVector2d P = Point * Metric;
		for (const BreachDecoration::FBinding& Binding : Existing)
		{
			for (int32 Index = 1; Index < Binding.Support.Num(); ++Index)
			{
				const FVector2d A = Binding.Support[Index - 1] * Metric;
				const FVector2d Delta = (Binding.Support[Index] - Binding.Support[Index - 1]) * Metric;
				const double Alpha = FMath::Clamp((P - A).Dot(Delta) / Delta.SquaredLength(), 0.0, 1.0);
				if ((P - A - Alpha * Delta).Length() < 0.05)
				{
					return true;
				}
			}
		}
		return false;
	}

	void SampleRoots(const FSupportPath& Path, const FVector2d& Metric, TArray<FVector2d>& Roots, TArray<FVector2d>& Normals)
	{
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			FVector2d Tangent;
			const FVector2d Point = PointAt(Path, Path.Length * Index / (Samples - 1), &Tangent);
			Tangent = Tangent * Metric;
			Tangent.Normalize();
			Roots.Add(Point * Metric);
			Normals.Add(FVector2d(-Tangent.Y, Tangent.X));
		}
	}
}

bool BreachDecoration::UpdateBindings(FFeatureState& State, const FBreachSimpleCollision& Holes, TConstArrayView<FPolygon2d> Removed, const FVector& Scale, const FBreachFeatureDecorationOptions& Options, FString& Error)
{
	if (!ValidOptions(Options, Scale, Error))
	{
		return false;
	}
	if (!Holes.HasHoleCache())
	{
		Error = TEXT("Feature Deco requires the tracked openings of its surface.");
		return false;
	}
	const FVector2d Metric(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	const FPolygon2d& Original = Holes.GetOuter();

	// 준비: 변경 번호가 새로 생긴 구멍만 다시 계산한다. 번호가 그대로인 구멍은 조각과 변을 그대로 둔다.
	struct FFreshHole
	{
		uint64 Revision = 0;
		const FGeneralPolygon2d* Polygon = nullptr;
	};
	TArray<FFreshHole> Fresh;
	TSet<uint64> Live;
	Holes.ForEachHole([&](uint64 Revision, const FGeneralPolygon2d& Polygon, const FAxisAlignedBox2d& Bounds)
	{
		Live.Add(Revision);
		if (!State.Holes.Contains(Revision))
		{
			Fresh.Add({Revision, &Polygon});
		}
	});

	// 준비: 번호가 사라진 구멍은 다른 구멍에 합쳐졌거나 형상이 바뀌었다. 그 조각과 변을 모아 새 구멍에 다시 맞춘다.
	TArray<FBinding> Pool;
	TArray<FFeatureEdge> PreviousEdges;
	TArray<FAxisAlignedBox2d> Undecorated;
	for (auto It = State.Holes.CreateIterator(); It; ++It)
	{
		if (Live.Contains(It.Key()))
		{
			continue;
		}
		FFeatureHole& Old = It.Value();
		Pool.Append(MoveTemp(Old.Bindings));
		PreviousEdges.Append(Old.Edges);
		if (!Old.bDecorated)
		{
			Undecorated.Add(Old.Bounds);
		}
		It.RemoveCurrent();
	}

	// 준비: 이번 브리칭으로 경계가 바뀔 수 있는 범위. 허용 오차만큼 넓힌다.
	TArray<FAxisAlignedBox2d> Changed;
	for (const FPolygon2d& Polygon : Removed)
	{
		if (Polygon.VertexCount() < 3)
		{
			continue;
		}
		FAxisAlignedBox2d Bounds = Polygon.Bounds();
		Bounds.Expand(Tolerance);
		Changed.Add(Bounds);
	}
	auto NearChange = [&](const FAxisAlignedBox2d& Bounds)
	{
		for (const FAxisAlignedBox2d& Box : Changed)
		{
			if (Box.Intersects(Bounds))
			{
				return true;
			}
		}
		return false;
	};

	TArray<FFeatureEdge> OriginalEdges;
	for (int32 Index = 0; Index < Original.VertexCount(); ++Index)
	{
		OriginalEdges.Add({Original[Index], Original[(Index + 1) % Original.VertexCount()]});
	}
	const TArray<FGeneralPolygon2d> Wall = {FGeneralPolygon2d(Original)};

	for (const FFreshHole& Hole : Fresh)
	{
		check(Hole.Polygon);

		// 처리: 구멍을 벽 안쪽 형상으로 자르고 경계 변과 둘레 경로를 읽는다.
		TArray<FGeneralPolygon2d> Pieces;
		if (!PolygonsIntersection(MakeArrayView(Hole.Polygon, 1), Wall, Pieces))
		{
			Error = TEXT("Feature Deco could not confine an opening to the original wall.");
			return false;
		}

		FFeatureHole Entry;
		Entry.bDecorated = !Pieces.IsEmpty();
		TArray<FSupportPath> Loops;
		auto AddLoop = [&](FPolygon2d Loop, bool bInnerHole)
		{
			if (Loop.IsClockwise() != bInnerHole)
			{
				Loop.Reverse();
			}
			int32 First = 0;
			for (int32 Index = 1; Index < Loop.VertexCount(); ++Index)
			{
				if (Loop[Index].X < Loop[First].X || (Loop[Index].X == Loop[First].X && Loop[Index].Y < Loop[First].Y))
				{
					First = Index;
				}
			}
			TArray<FVector2d> Points;
			for (int32 Index = 0; Index <= Loop.VertexCount(); ++Index)
			{
				Points.Add(Loop[(First + Index) % Loop.VertexCount()]);
			}
			FSupportPath Path = MakePath(Points, Metric);
			for (int32 Index = 1; Index < Path.Points.Num(); ++Index)
			{
				if (!Covers(Path.Points[Index - 1], Path.Points[Index], OriginalEdges))
				{
					Entry.Edges.Add({Path.Points[Index - 1], Path.Points[Index]});
				}
			}
			Loops.Add(MoveTemp(Path));
		};
		for (const FGeneralPolygon2d& Piece : Pieces)
		{
			// Boolean 연산이 남긴 면적 없는 띠는 구멍이 아니다.
			if (FMath::Abs(Piece.SignedArea()) <= UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Entry.Bounds.Contain(Piece.GetOuter().Bounds());
			if (FMath::Abs(Piece.SignedArea()) * Metric.X * Metric.Y + UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER < Options.MinHoleArea)
			{
				Entry.bDecorated = false;
				continue;
			}
			AddLoop(Piece.GetOuter(), false);
			for (const FPolygon2d& Inner : Piece.GetHoles())
			{
				AddLoop(Inner, true);
			}
		}

		// 이 구멍으로 합쳐진 이전 구멍이 모두 둘레를 기록해 두었으면, 바뀐 범위 밖의 경계는 전과 같다.
		FAxisAlignedBox2d Reach = Entry.Bounds;
		Reach.Expand(Tolerance);
		bool bKnownBoundary = State.bSeeded;
		for (const FAxisAlignedBox2d& Bounds : Undecorated)
		{
			if (Bounds.Intersects(Reach))
			{
				bKnownBoundary = false;
			}
		}

		// 처리: 기존 조각 중 이 구멍의 경계에 여전히 붙어 있는 것만 가져온다.
		// 새 구멍이 하나뿐이면 바뀐 범위 밖의 조각은 부착 변이 그대로이므로 검사 없이 유지한다.
		const bool bTrustFarBindings = bKnownBoundary && Fresh.Num() == 1 && !Entry.Edges.IsEmpty();
		TArray<FBinding> Rest;
		for (FBinding& Binding : Pool)
		{
			FAxisAlignedBox2d SupportBounds = FAxisAlignedBox2d::Empty();
			for (const FVector2d& Point : Binding.Support)
			{
				SupportBounds.Contain(Point);
			}

			bool bAttached = Binding.Support.Num() >= 2 && Reach.Intersects(SupportBounds);
			if (bAttached)
			{
				if (!bTrustFarBindings || NearChange(SupportBounds))
				{
					for (int32 Index = 1; Index < Binding.Support.Num(); ++Index)
					{
						if (!Covers(Binding.Support[Index - 1], Binding.Support[Index], Entry.Edges))
						{
							bAttached = false;
							break;
						}
					}
				}
			}

			if (bAttached)
			{
				Entry.Bindings.Add(MoveTemp(Binding));
			}
			else
			{
				Rest.Add(MoveTemp(Binding));
			}
		}
		Pool = MoveTemp(Rest);

		// 처리: 둘레를 따라 같은 난수열로 후보를 만들고, 새 경계를 포함한 후보만 추가한다.
		const TArray<FBinding> Existing = Entry.Bindings;
		for (const FSupportPath& Loop : Loops)
		{
			if (Loop.Points.Num() < 2 || Loop.Length < 0.1)
			{
				continue;
			}
			const uint32 LoopSeed = HashCombine(GetTypeHash(Options.Seed), HashCombine(GetTypeHash(Loop.Points[0].X), GetTypeHash(Loop.Points[0].Y)));
			FRandomStream Random(static_cast<int32>(LoopSeed));
			double Distance = Options.Spacing * Random.FRandRange(0.15f, 1.35f);
			int32 GroupRemaining = Random.RandRange(2, 5);
			double GroupSize = Random.FRandRange(0.5f, 1.5f);
			while (Distance < Loop.Length - 0.05)
			{
				double Width = Options.Width * GroupSize * Random.FRandRange(0.45f, 1.45f);
				Width = FMath::Min(Width, FMath::Min(2 * Distance - 0.02, 2 * (Loop.Length - Distance) - 0.02));
				FBinding Binding;
				Binding.Length = Options.Length * Random.FRandRange(0.3f, 1.45f);
				Binding.Seed = Random.RandHelper(MAX_int32);
				const FVector2d Center = PointAt(Loop, Distance);

				// 이전 둘레가 모두 기록된 구멍에서 바뀐 범위 밖의 후보는 새 경계를 가질 수 없다. 난수열만 진행한다.
				bool bCandidate = Width > 0.1;
				if (bCandidate && bKnownBoundary)
				{
					const FVector2d Extent(Width * 0.5 / Metric.X, Width * 0.5 / Metric.Y);
					bCandidate = NearChange(FAxisAlignedBox2d(Center - Extent, Center + Extent));
				}
				if (bCandidate)
				{
					bCandidate = !OnSupport(Center, Existing, Metric);
				}

				if (bCandidate)
				{
					for (int32 Attempt = 0; Attempt < 5; ++Attempt)
					{
						Binding.Support = Subpath(Loop, Distance - Width * 0.5, Distance + Width * 0.5);
						const FSupportPath Support = MakePath(Binding.Support, Metric);
						TArray<FVector2d> Roots;
						TArray<FVector2d> Normals;
						SampleRoots(Support, Metric, Roots, Normals);
						bool bSuitable = true;
						for (const FVector2d& Normal : Normals)
						{
							if (Normal.Dot(Normals[Samples / 2]) < 0.5)
							{
								bSuitable = false;
							}
						}
						if (bSuitable)
						{
							bool bHasNewBoundary = false;
							for (int32 Index = 1; Index < Binding.Support.Num(); ++Index)
							{
								if (!Covers(Binding.Support[Index - 1], Binding.Support[Index], Entry.Edges))
								{
									bSuitable = false;
								}
								if (!Covers(Binding.Support[Index - 1], Binding.Support[Index], PreviousEdges))
								{
									bHasNewBoundary = true;
								}
							}
							// 부착 폭을 줄여도 기존 경계에만 놓인 후보는 신규 배치 대상이 되지 않는다.
							if (!bHasNewBoundary)
							{
								break;
							}
							if (bSuitable)
							{
								Entry.Bindings.Add(MoveTemp(Binding));
								break;
							}
						}
						Width *= 0.5;
					}
				}
				--GroupRemaining;
				if (GroupRemaining <= 0)
				{
					Distance += Options.Spacing * Random.FRandRange(1.6f, 3.8f);
					GroupRemaining = Random.RandRange(2, 6);
					GroupSize = Random.FRandRange(0.5f, 1.5f);
				}
				else
				{
					Distance += Options.Spacing * Random.FRandRange(0.2f, 0.75f);
				}
			}
		}

		// 면적 기준을 처음 넘은 구멍은 이전 변 기록이 없어 기존 둘레에도 최초 배치된다.
		State.Holes.Add(Hole.Revision, MoveTemp(Entry));
	}

	// 상태 반영: 어느 구멍에도 붙지 못한 조각은 부착 변을 잃었다. 표시 메시에서 그 정점만 지운다.
	for (const FBinding& Binding : Pool)
	{
		State.StaleVertices.Append(Binding.MeshVertices);
	}
	State.bSeeded = true;
	return true;
}

bool BreachDecoration::UpdateFeatureMesh(FFeatureState& State, const FBreachFeatureDecorationOptions& Options, const FVector& Scale, const FVector2d& Depth, FDynamicMesh3& Mesh, bool& bOutChanged, FString& Error)
{
	bOutChanged = false;
	if (!ValidOptions(Options, Scale, Error))
	{
		return false;
	}
	if (!FMath::IsFinite(Depth.X) || !FMath::IsFinite(Depth.Y) || Depth.Y <= Depth.X)
	{
		Error = TEXT("Feature Deco requires an ordered finite surface depth.");
		return false;
	}
	PrepareDisplayMesh(Mesh);
	auto* UV = Mesh.Attributes()->PrimaryUV();

	// 처리: 부착 변을 잃은 조각의 정점만 지운다. 정점에 붙은 삼각형과 UV·노멀도 함께 지워진다.
	for (int32 VertexID : State.StaleVertices)
	{
		if (Mesh.IsVertex(VertexID))
		{
			Mesh.RemoveVertex(VertexID);
			bOutChanged = true;
		}
	}
	State.StaleVertices.Reset();

	// 처리: 아직 메시에 없는 조각만 만들어 추가한다.
	TArray<FBinding*> Pending;
	for (TPair<uint64, FFeatureHole>& Hole : State.Holes)
	{
		for (FBinding& Binding : Hole.Value.Bindings)
		{
			if (!Binding.bInMesh)
			{
				Pending.Add(&Binding);
			}
		}
	}
	const FVector2d Metric(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	int32 Skipped = 0;
	const double DepthCenter = (Depth.X + Depth.Y) * 0.5;
	for (FBinding* PendingBinding : Pending)
	{
		FBinding& Binding = *PendingBinding;
		const FSupportPath Support = MakePath(Binding.Support, Metric);
		if (Support.Points.Num() < 2 || Support.Length < 0.01 || !FMath::IsFinite(Binding.Length) || Binding.Length <= 0)
		{
			Error = TEXT("Feature Deco has an invalid attachment path or length.");
			return false;
		}
		TArray<FVector2d> Roots;
		TArray<FVector2d> Normals;
		SampleRoots(Support, Metric, Roots, Normals);
		const FVector2d TextureU(Normals[0].Y, -Normals[0].X);
		const FVector2d TextureV = Normals[0];
		FRandomStream Random(Binding.Seed);
		// 깊이 변형 제거 후에도 기존 외곽·아틀라스의 난수 순서는 보존한다.
		Random.FRand();
		Random.FRand();
		const int32 Column = Random.RandHelper(Options.AtlasSize.X);
		const int32 Row = Random.RandHelper(Options.AtlasSize.Y);
		Random.FRand();
		const double Embed = FMath::Min(1.0, Support.Length * 0.18);
		const FVector2d TextureOrigin = Roots[0] - TextureV * Embed;
		double Profile[Samples];
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double T = static_cast<double>(Index) / (Samples - 1);
			const double Shoulder = 0.3 + 0.7 * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * T), 0.45);
			Profile[Index] = Shoulder * (1 - Options.Roughness * Random.FRandRange(0.15f, 0.85f));
			Random.FRand();
		}
		TArray<FVector3d> Positions;
		TArray<FVector2f> UVs;
		double Length = Binding.Length;
		bool bValid = false;
		for (int32 Attempt = 0; Attempt < 7; ++Attempt)
		{
			Positions.Reset();
			UVs.Reset();
			for (int32 Layer = 0; Layer < 4; ++Layer)
			{
				for (int32 Index = 0; Index < Samples; ++Index)
				{
					const bool bTip = Layer % 2 == 1;
					const bool bBack = Layer >= 2;
					const double Distance = bTip ? Length * Profile[Index] : -Embed;
					const FVector2d P = Roots[Index] + Normals[Index] * Distance;
					const double Y = DepthCenter + (bBack ? 0.5 : -0.5) * Options.Thickness / FMath::Abs(Scale.Y);
					Positions.Add(FVector3d(P.X / Metric.X, Y, P.Y / Metric.Y));
					if (Options.AtlasSize == FIntPoint(1, 1))
					{
						const FVector2d TextureDelta = P - TextureOrigin;
						UVs.Add(FVector2f(TextureDelta.Dot(TextureU) / Options.TextureWorldSize.X, TextureDelta.Dot(TextureV) / Options.TextureWorldSize.Y));
					}
					else
					{
						const double T = static_cast<double>(Index) / (Samples - 1);
						UVs.Add(FVector2f((Column + 0.01 + T * 0.98) / Options.AtlasSize.X, (Row + 0.01 + (Distance + Embed) / (Length + Embed) * 0.98) / Options.AtlasSize.Y));
					}
				}
			}
			bValid = true;
			for (int32 Index = 0; Index < Samples - 1; ++Index)
			{
				auto Area = [&](int32 A, int32 B, int32 C)
				{
					const FVector3d AB = Positions[B] - Positions[A];
					const FVector3d AC = Positions[C] - Positions[A];
					return AB.X * AC.Z - AB.Z * AC.X;
				};
				if (Area(Index, Index + 1, Samples + Index + 1) <= UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER || Area(Index, Samples + Index + 1, Samples + Index) <= UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER)
				{
					bValid = false;
				}
			}
			if (bValid)
			{
				break;
			}
			Length *= 0.5;
		}
		Binding.bInMesh = true;
		if (!bValid)
		{
			++Skipped;
			continue;
		}
		bOutChanged = true;
		TArray<int32> IDs;
		TArray<int32> UVIds;
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			IDs.Add(Mesh.AppendVertex(Positions[Index]));
			UVIds.Add(UV->AppendElement(UVs[Index]));
		}
		Binding.MeshVertices = IDs;
		bool bTrianglesValid = true;
		auto Triangle = [&](int32 A, int32 B, int32 C)
		{
			// Unreal triangle normals use the left-handed winding (the HTML preview uses right-handed winding).
			const int32 ID = Mesh.AppendTriangle(IDs[A], IDs[C], IDs[B]);
			if (ID < 0)
			{
				bTrianglesValid = false;
				return;
			}
			UV->SetTriangle(ID, FIndex3i(UVIds[A], UVIds[C], UVIds[B]));
			SetFlatNormal(Mesh, ID);
		};
		auto Quad = [&](int32 A, int32 B, int32 C, int32 D)
		{
			Triangle(A, B, C);
			Triangle(A, C, D);
		};
		for (int32 Index = 0; Index < Samples - 1; ++Index)
		{
			Quad(Index, Index + 1, Samples + Index + 1, Samples + Index);
			Quad(2 * Samples + Index, 3 * Samples + Index, 3 * Samples + Index + 1, 2 * Samples + Index + 1);
			Quad(Index, 2 * Samples + Index, 2 * Samples + Index + 1, Index + 1);
			Quad(Samples + Index, Samples + Index + 1, 3 * Samples + Index + 1, 3 * Samples + Index);
		}
		Quad(0, Samples, 3 * Samples, 2 * Samples);
		Quad(Samples - 1, 3 * Samples - 1, 4 * Samples - 1, 2 * Samples - 1);
		if (!bTrianglesValid)
		{
			Error = TEXT("Feature Deco rejected a generated plate triangle.");
			return false;
		}
	}
	if (Skipped > 0)
	{
		UE_LOG(LogBreachFeatureGeometry, Warning, TEXT("Feature Deco skipped %d folded plates at incompatible boundary bends."), Skipped);
	}
	return true;
}
