#include "Surface/BreachDecorationGeometry.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"

using namespace UE::Geometry;
using BreachDecoration::FFeatureEdge;

DEFINE_LOG_CATEGORY_STATIC(LogBreachFeatureGeometry, Log, All);

namespace
{
	constexpr int32 Samples = 7;
	constexpr double Tolerance = 0.001;

	struct FSupportPath
	{
		TArray<FVector2d> Points;
		TArray<double> Arc;
		double Length = 0;
	};

	bool ValidOptions(const FBreachFeatureDecorationOptions& Options, const FVector& Scale, FString& Error)
	{
		if (Scale.ContainsNaN() || FMath::Abs(Scale.X) <= UE_DOUBLE_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_DOUBLE_SMALL_NUMBER || FMath::Abs(Scale.Z) <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Feature Deco requires a finite nonzero surface scale.");
			return false;
		}
		if (!FMath::IsFinite(Options.MinHoleArea) || Options.MinHoleArea < 0 || !FMath::IsFinite(Options.Spacing) || Options.Spacing < 0.1 || !FMath::IsFinite(Options.Width) || Options.Width < 0.1 || !FMath::IsFinite(Options.Length) || Options.Length < 0.1 || !FMath::IsFinite(Options.Thickness) || Options.Thickness < 0.01 || !FMath::IsFinite(Options.Roughness) || Options.Roughness < 0 || Options.Roughness > 1 || Options.AtlasSize.X < 1 || Options.AtlasSize.Y < 1)
		{
			Error = TEXT("Feature Deco requires finite positive dimensions/spacing, nonnegative area, roughness 0..1 and a positive atlas size.");
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
				if (Step <= 1.e-8)
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
		int32 Index = 1;
		while (Index < Path.Arc.Num() - 1 && Path.Arc[Index] < Target)
		{
			++Index;
		}
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
			if (Path.Arc[Index] > Start + 1.e-8 && Path.Arc[Index] < End - 1.e-8)
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
		if (Length <= 1.e-8)
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

bool BreachDecoration::UpdateBindings(TArray<FBinding>& Bindings, TArray<FFeatureEdge>& PreviousEdges, const FPolygon2d& Original, TConstArrayView<FGeneralPolygon2d> Remaining, const FVector& Scale, const FBreachFeatureDecorationOptions& Options, FString& Error)
{
	if (!ValidOptions(Options, Scale, Error))
	{
		return false;
	}
	const FVector2d Metric(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	const TArray<FGeneralPolygon2d> Source = {FGeneralPolygon2d(Original)};
	TArray<FGeneralPolygon2d> Removed;
	if (!PolygonsDifference(Source, Remaining, Removed))
	{
		Error = TEXT("Feature Deco could not derive actual removed regions.");
		return false;
	}
	TArray<FFeatureEdge> OriginalEdges;
	for (int32 Index = 0; Index < Original.VertexCount(); ++Index)
	{
		OriginalEdges.Add({Original[Index], Original[(Index + 1) % Original.VertexCount()]});
	}
	TArray<FFeatureEdge> Edges;
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
				Edges.Add({Path.Points[Index - 1], Path.Points[Index]});
			}
		}
		Loops.Add(MoveTemp(Path));
	};
	for (const FGeneralPolygon2d& Hole : Removed)
	{
		if (FMath::Abs(Hole.SignedArea()) * Metric.X * Metric.Y + 1.e-8 < Options.MinHoleArea)
		{
			continue;
		}
		AddLoop(Hole.GetOuter(), false);
		for (const FPolygon2d& Inner : Hole.GetHoles())
		{
			AddLoop(Inner, true);
		}
	}
	Bindings.RemoveAll([&](const FBinding& Binding)
	{
		if (Binding.Support.Num() < 2)
		{
			return true;
		}
		for (int32 Index = 1; Index < Binding.Support.Num(); ++Index)
		{
			if (!Covers(Binding.Support[Index - 1], Binding.Support[Index], Edges))
			{
				return true;
			}
		}
		return false;
	});
	const TArray<FBinding> Existing = Bindings;
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
			if (Width > 0.1 && !OnSupport(Center, Existing, Metric))
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
							if (!Covers(Binding.Support[Index - 1], Binding.Support[Index], Edges))
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
							Bindings.Add(MoveTemp(Binding));
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
	// 면적 기준을 처음 넘은 구멍은 이전 경계가 없어 기존 둘레에도 최초 배치한다.
	PreviousEdges = MoveTemp(Edges);
	return true;
}

bool BreachDecoration::BuildFeatureMesh(TConstArrayView<FBinding> Bindings, const FBreachFeatureDecorationOptions& Options, const FVector& Scale, const FVector2d& Depth, FDynamicMesh3& OutMesh, FString& Error)
{
	if (!ValidOptions(Options, Scale, Error))
	{
		return false;
	}
	if (!FMath::IsFinite(Depth.X) || !FMath::IsFinite(Depth.Y) || Depth.Y <= Depth.X)
	{
		Error = TEXT("Feature Deco requires an ordered finite surface depth.");
		return false;
	}
	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(1);
	auto* UV = Mesh.Attributes()->PrimaryUV();
	const FVector2d Metric(FMath::Abs(Scale.X), FMath::Abs(Scale.Z));
	int32 Skipped = 0;
	for (const FBinding& Binding : Bindings)
	{
		const FSupportPath Support = MakePath(Binding.Support, Metric);
		if (Support.Points.Num() < 2 || Support.Length < 0.01 || !FMath::IsFinite(Binding.Length) || Binding.Length <= 0)
		{
			Error = TEXT("Feature Deco has an invalid attachment path or length.");
			return false;
		}
		TArray<FVector2d> Roots;
		TArray<FVector2d> Normals;
		SampleRoots(Support, Metric, Roots, Normals);
		FRandomStream Random(Binding.Seed);
		const double Bend = Random.FRandRange(-1.f, 1.f) * Options.Thickness;
		const double Twist = Random.FRandRange(-0.75f, 0.75f) * Options.Thickness;
		const int32 Column = Random.RandHelper(Options.AtlasSize.X);
		const int32 Row = Random.RandHelper(Options.AtlasSize.Y);
		const double Lift = Random.FRandRange(0.04f, 0.14f);
		const double Embed = FMath::Min(1.0, Support.Length * 0.18);
		double Profile[Samples];
		double TipThickness[Samples];
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double T = static_cast<double>(Index) / (Samples - 1);
			const double Shoulder = 0.3 + 0.7 * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * T), 0.45);
			Profile[Index] = Shoulder * (1 - Options.Roughness * Random.FRandRange(0.15f, 0.85f));
			TipThickness[Index] = Options.Thickness * Random.FRandRange(0.18f, 0.48f);
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
					const double T = static_cast<double>(Index) / (Samples - 1);
					const double Distance = bTip ? Length * Profile[Index] : -Embed;
					const double Raised = bTip ? Bend * Profile[Index] + Twist * (T - 0.5) : 0;
					const double Thickness = bTip ? TipThickness[Index] : Options.Thickness;
					const FVector2d P = Roots[Index] + Normals[Index] * Distance;
					const double Y = Depth.X - (Lift + Raised - (bBack ? Thickness : 0)) / FMath::Abs(Scale.Y);
					Positions.Add(FVector3d(P.X / Metric.X, Y, P.Y / Metric.Y));
					UVs.Add(FVector2f((Column + 0.01 + T * 0.98) / Options.AtlasSize.X, (Row + 0.01 + (Distance + Embed) / (Length + Embed) * 0.98) / Options.AtlasSize.Y));
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
				if (Area(Index, Index + 1, Samples + Index + 1) <= 1.e-10 || Area(Index, Samples + Index + 1, Samples + Index) <= 1.e-10)
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
		if (!bValid)
		{
			++Skipped;
			continue;
		}
		TArray<int32> IDs;
		TArray<int32> UVIds;
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			IDs.Add(Mesh.AppendVertex(Positions[Index]));
			UVIds.Add(UV->AppendElement(UVs[Index]));
		}
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
	FMeshNormals::InitializeMeshToPerTriangleNormals(&Mesh);
	OutMesh = MoveTemp(Mesh);
	return true;
}
