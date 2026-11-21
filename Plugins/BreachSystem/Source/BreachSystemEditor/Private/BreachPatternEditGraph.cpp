#include "BreachPatternEditGraph.h"
#include "Surface/BreachPatternGeometry.h"

namespace
{
	bool SameGraphPeriodicPoint(FVector2D A, FVector2D B)
	{
		const FVector2D D = A - B;
		return FMath::IsNearlyEqual(D.X, FMath::RoundToDouble(D.X), UE_DOUBLE_KINDA_SMALL_NUMBER) && FMath::IsNearlyEqual(D.Y, FMath::RoundToDouble(D.Y), UE_DOUBLE_KINDA_SMALL_NUMBER);
	}
}

int32 FBreachPatternEditGraph::FindVertex(FVector2D Point) const
{
	return Vertices.IndexOfByPredicate([Point](FVector2D P) { return P.Equals(Point, UE_DOUBLE_KINDA_SMALL_NUMBER); });
}

bool FBreachPatternEditGraph::Build(const UBreachPattern& Pattern, FString& OutError)
{
	check(IsValid(&Pattern));
	*this = {};
	if (!BreachPatternGeometry::Validate(Pattern, OutError))
	{
		return false;
	}
	Mode = Pattern.Mode;
	SourceRegions = Pattern.Regions;
	SourceGuides = Pattern.Guides;
	auto AddPath = [&](const TArray<FVector2D>& Points, bool bClosed, FBreachPatternPathSelection Owner)
	{
		FBreachPatternGraphPath& Path = Paths.AddDefaulted_GetRef();
		Path.Owner = Owner;
		Path.bClosed = bClosed;
		for (FVector2D Point : Points)
		{
			int32 Node = FindVertex(Point);
			if (Node == INDEX_NONE)
			{
				Node = Vertices.Add(Point);
			}
			Path.Nodes.Add(Node);
		}
	};
	if (Mode == EBreachPatternMode::Regions)
	{
		for (int32 R = 0; R < Pattern.Regions.Num(); ++R)
		{
			AddPath(Pattern.Regions[R].Outer, true, {R, INDEX_NONE, INDEX_NONE});
			for (int32 H = 0; H < Pattern.Regions[R].Holes.Num(); ++H)
			{
				AddPath(Pattern.Regions[R].Holes[H].Vertices, true, {R, H, INDEX_NONE});
			}
		}
	}
	else if (Mode == EBreachPatternMode::GrainGuides)
	{
		for (int32 G = 0; G < Pattern.Guides.Num(); ++G)
		{
			AddPath(Pattern.Guides[G].Points, false, {G, INDEX_NONE, INDEX_NONE});
		}
	}
	// Normalize existing T junctions once. Dragging only changes these fixed node positions.
	const TArray<FVector2D> OriginalVertices = Vertices;
	for (FBreachPatternGraphPath& Path : Paths)
	{
		TArray<int32> Normalized;
		for (int32 I = 0; I < Path.Nodes.Num(); ++I)
		{
			Normalized.Add(Path.Nodes[I]);
			if (!Path.bClosed)
			{
				continue;
			}
			const FVector2D A = Vertices[Path.Nodes[I]];
			const FVector2D B = Vertices[Path.Nodes[(I + 1) % Path.Nodes.Num()]];
			const FVector2D D = B - A;
			TArray<TPair<double, FVector2D>> Splits;
			for (FVector2D Point : OriginalVertices)
			{
				const int64 X0 = FMath::CeilToInt64(FMath::Min(A.X, B.X) - Point.X - UE_DOUBLE_KINDA_SMALL_NUMBER);
				const int64 X1 = FMath::FloorToInt64(FMath::Max(A.X, B.X) - Point.X + UE_DOUBLE_KINDA_SMALL_NUMBER);
				const int64 Y0 = FMath::CeilToInt64(FMath::Min(A.Y, B.Y) - Point.Y - UE_DOUBLE_KINDA_SMALL_NUMBER);
				const int64 Y1 = FMath::FloorToInt64(FMath::Max(A.Y, B.Y) - Point.Y + UE_DOUBLE_KINDA_SMALL_NUMBER);
				if (double(X1 - X0 + 1) * double(Y1 - Y0 + 1) > 4096)
				{
					OutError = TEXT("편집할 경계가 너무 많은 타일을 가로지릅니다.");
					return false;
				}
				for (int64 Y = Y0; Y <= Y1; ++Y)
				{
					for (int64 X = X0; X <= X1; ++X)
					{
						const FVector2D P = Point + FVector2D(double(X), double(Y));
						const double Alpha = FVector2D::DotProduct(P - A, D) / FMath::Max(D.SizeSquared(), UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER);
						if (Alpha > UE_DOUBLE_KINDA_SMALL_NUMBER && Alpha < 1 - UE_DOUBLE_KINDA_SMALL_NUMBER && P.Equals(A + D * Alpha, UE_DOUBLE_KINDA_SMALL_NUMBER))
						{
							Splits.Emplace(Alpha, P);
						}
					}
				}
			}
			Splits.Sort([](const auto& L, const auto& R) { return L.Key < R.Key; });
			double Previous = -1;
			for (const auto& Split : Splits)
			{
				if (!FMath::IsNearlyEqual(Previous, Split.Key, UE_DOUBLE_KINDA_SMALL_NUMBER))
				{
					int32 Node = FindVertex(Split.Value);
					if (Node == INDEX_NONE)
					{
						Node = Vertices.Add(Split.Value);
					}
					Normalized.Add(Node);
					Previous = Split.Key;
				}
			}
		}
		Path.Nodes = MoveTemp(Normalized);
		const int32 Count = Path.Nodes.Num() - (Path.bClosed ? 0 : 1);
		for (int32 I = 0; I < Count; ++I)
		{
			const int32 A = Path.Nodes[I];
			const int32 B = Path.Nodes[(I + 1) % Path.Nodes.Num()];
			Edges.AddUnique(FIntPoint(FMath::Min(A, B), FMath::Max(A, B)));
		}
	}
	return true;
}

bool FBreachPatternEditGraph::ApplyMove(TConstArrayView<int32> SelectedNodes, FVector2D Delta, UBreachPattern& OutPattern, TArray<FVector2D>& OutVertices, FString& OutError) const
{
	check(IsValid(&OutPattern));
	for (int32 Node : SelectedNodes)
	{
		if (!Vertices.IsValidIndex(Node))
		{
			OutError = TEXT("선택한 점이 더 이상 존재하지 않습니다.");
			return false;
		}
	}
	OutVertices = Vertices;
	for (int32 I = 0; I < Vertices.Num(); ++I)
	{
		for (int32 Node : SelectedNodes)
		{
			if (SameGraphPeriodicPoint(Vertices[I], Vertices[Node]))
			{
				OutVertices[I] += Delta;
				break;
			}
		}
	}
	OutPattern.Mode = Mode;
	OutPattern.Regions = SourceRegions;
	OutPattern.Guides = SourceGuides;
	for (const FBreachPatternGraphPath& Path : Paths)
	{
		TArray<FVector2D>& Points = Mode == EBreachPatternMode::GrainGuides ? OutPattern.Guides[Path.Owner.Path].Points : (Path.Owner.Hole == INDEX_NONE ? OutPattern.Regions[Path.Owner.Path].Outer : OutPattern.Regions[Path.Owner.Path].Holes[Path.Owner.Hole].Vertices);
		Points.Reset();
		for (int32 Node : Path.Nodes)
		{
			Points.Add(OutVertices[Node]);
		}
	}
	return true;
}

bool FBreachPatternEditGraph::HasSameConnections(const FBreachPatternEditGraph& Other) const
{
	if (Mode != Other.Mode || Paths.Num() != Other.Paths.Num() || Vertices.Num() != Other.Vertices.Num() || Edges.Num() != Other.Edges.Num())
	{
		return false;
	}
	TMap<int32, int32> Forward;
	TMap<int32, int32> Reverse;
	for (int32 P = 0; P < Paths.Num(); ++P)
	{
		const auto& A = Paths[P];
		const auto& B = Other.Paths[P];
		if (A.Nodes.Num() != B.Nodes.Num() || A.bClosed != B.bClosed || A.Owner.Path != B.Owner.Path || A.Owner.Hole != B.Owner.Hole)
		{
			return false;
		}
		for (int32 I = 0; I < A.Nodes.Num(); ++I)
		{
			const int32 From = A.Nodes[I];
			const int32 To = B.Nodes[I];
			if ((Forward.Contains(From) && Forward[From] != To) || (Reverse.Contains(To) && Reverse[To] != From))
			{
				return false;
			}
			Forward.Add(From, To);
			Reverse.Add(To, From);
		}
	}
	return true;
}

FBreachPatternPathSelection FBreachPatternEditGraph::OwnerOf(int32 Node, int32 Edge) const
{
	for (const auto& Path : Paths)
	{
		if (Node != INDEX_NONE)
		{
			const int32 I = Path.Nodes.Find(Node);
			if (I != INDEX_NONE)
			{
				return {Path.Owner.Path, Path.Owner.Hole, I};
			}
		}
		else if (Edges.IsValidIndex(Edge))
		{
			for (int32 I = 0; I < Path.Nodes.Num() - (Path.bClosed ? 0 : 1); ++I)
			{
				const int32 A = Path.Nodes[I];
				const int32 B = Path.Nodes[(I + 1) % Path.Nodes.Num()];
				if (Edges[Edge] == FIntPoint(FMath::Min(A, B), FMath::Max(A, B)))
				{
					return Path.Owner;
				}
			}
		}
	}
	return {};
}
