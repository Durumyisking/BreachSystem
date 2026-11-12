#include "BreachPassageSolver.h"
#include "BreachSurfaceGeometry.h"
#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "CompGeom/ConvexHull2.h"
#include "CompGeom/ExactPredicates.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

using namespace UE::Geometry;

namespace
{
	constexpr double AreaTolerance = 1.e-6;
	constexpr double PositionTolerance = 1.e-7;

	double Area(TConstArrayView<FGeneralPolygon2d> Polygons)
	{
		double Result = 0;
		for (const FGeneralPolygon2d& Polygon : Polygons)
		{
			Result += FMath::Abs(Polygon.SignedArea());
		}
		return Result;
	}

	TArray<FGeneralPolygon2d> General(TConstArrayView<FPolygon2d> Polygons)
	{
		TArray<FGeneralPolygon2d> Result;
		for (const FPolygon2d& Polygon : Polygons)
		{
			Result.Emplace(Polygon);
		}
		return Result;
	}

	bool Union(TConstArrayView<FGeneralPolygon2d> Input, TArray<FGeneralPolygon2d>& Out, FBreachSimpleBuildStats& Stats)
	{
		if (Input.IsEmpty())
		{
			Out.Reset();
			return true;
		}
		++Stats.BooleanOperations;
		return PolygonsUnion(Input, Out, false);
	}

	bool Difference(TConstArrayView<FGeneralPolygon2d> A, TConstArrayView<FGeneralPolygon2d> B, TArray<FGeneralPolygon2d>& Out, FBreachSimpleBuildStats& Stats)
	{
		if (A.IsEmpty() || B.IsEmpty())
		{
			Out = TArray<FGeneralPolygon2d>(A);
			return true;
		}
		++Stats.BooleanOperations;
		return PolygonsDifference(A, B, Out);
	}

	FPolygon2d Hull(TConstArrayView<FVector2d> Points)
	{
		TConvexHull2<double> Builder;
		FPolygon2d Result;
		if (Builder.Solve(Points))
		{
			for (int32 Index : Builder.GetPolygonIndices())
			{
				Result.AppendVertex(Points[Index]);
			}
			if (Result.IsClockwise())
			{
				Result.Reverse();
			}
		}
		return Result;
	}

	// 반시계 볼록 다각형에서 새 점이 바라보는 변들의 연속 구간만 교체한다.
	// 매 후보마다 정점을 정렬하고 전체 hull을 다시 만드는 대신 O(V)로 갱신한다.
	bool AddHullPoint(const FPolygon2d& Polygon, const FVector2d& Point, FPolygon2d& Out)
	{
		const int32 Count = Polygon.VertexCount();
		TArray<int8, TInlineAllocator<128>> Sides;
		Sides.Reserve(Count);
		bool bOutside = false;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Side = ExactPredicates::Orient2(Polygon[Index], Polygon[(Index + 1) % Count], Point);
			Sides.Add(Side < 0 ? -1 : Side > 0 ? 1 : 0);
			bOutside |= Side < 0;
		}
		if (!bOutside)
		{
			return false;
		}
		int32 Start = INDEX_NONE;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Sides[Index] <= 0 && Sides[(Index + Count - 1) % Count] > 0)
			{
				Start = Index;
				break;
			}
		}
		if (Start == INDEX_NONE)
		{
			return false;
		}
		int32 End = (Start + 1) % Count;
		while (Sides[End] <= 0)
		{
			End = (End + 1) % Count;
		}
		TArray<FVector2d> Points;
		Points.Reserve(Count + 1);
		for (int32 Index = End;; Index = (Index + 1) % Count)
		{
			Points.Add(Polygon[Index]);
			if (Index == Start)
			{
				break;
			}
		}
		Points.Add(Point);
		Out = FPolygon2d(MoveTemp(Points));
		return true;
	}

	FPolygon2d CombinedHull(const FPolygon2d& A, const FPolygon2d& B)
	{
		TArray<FVector2d> Points(A.GetVertices());
		Points.Append(B.GetVertices());
		return Hull(Points);
	}

	FVector2d Center(const FPolygon2d& Polygon)
	{
		FVector2d Result = FVector2d::ZeroVector;
		for (const FVector2d Point : Polygon.GetVertices())
		{
			Result += Point;
		}
		return Result / Polygon.VertexCount();
	}

	bool SameRing(const FPolygon2d& A, const FPolygon2d& B)
	{
		if (A.VertexCount() != B.VertexCount())
		{
			return false;
		}
		const int32 Count = A.VertexCount();
		for (int32 Start = 0; Start < Count; ++Start)
		{
			if (!A[0].Equals(B[Start], PositionTolerance))
			{
				continue;
			}
			bool bSame = true;
			for (int32 Index = 1; Index < Count; ++Index)
			{
				if (!A[Index].Equals(B[(Start + Index) % Count], PositionTolerance))
				{
					bSame = false;
					break;
				}
			}
			if (bSame)
			{
				return true;
			}
		}
		return false;
	}

	bool SameDomain(const FGeneralPolygon2d& A, const FGeneralPolygon2d& B)
	{
		if (!SameRing(A.GetOuter(), B.GetOuter()) || A.GetHoles().Num() != B.GetHoles().Num())
		{
			return false;
		}
		for (const FPolygon2d& Hole : A.GetHoles())
		{
			if (!B.GetHoles().ContainsByPredicate([&](const FPolygon2d& Other) { return SameRing(Hole, Other); }))
			{
				return false;
			}
		}
		return true;
	}

	bool Offset(double Distance, TConstArrayView<FGeneralPolygon2d> Input, TArray<FGeneralPolygon2d>& Out, FBreachSimpleBuildStats& Stats)
	{
		++Stats.BooleanOperations;
		// 마지막 인자는 오차가 아닌 분할 배율이다. 기본 배율에 상한만 적용한다.
		return PolygonsOffset(Distance, Input, Out, false, 2.0, EPolygonOffsetJoinType::Round, EPolygonOffsetEndType::Polygon, 12.0, 1.0);
	}
}

/** 본체별 경계를 전처리하여 국소 교차·포함 검사에 재사용한다. */
class FBreachPreparedBoundary
{
	struct FEdge
	{
		FVector2d A, B;
		FAxisAlignedBox2d Bounds;
	};
	struct FNode
	{
		FAxisAlignedBox2d Bounds = FAxisAlignedBox2d::Empty();
		int32 Begin = 0, Count = 0, Left = INDEX_NONE, Right = INDEX_NONE;
	};
	FGeneralPolygon2d Domain;
	TArray<FEdge> Edges;
	TArray<FNode> Nodes;

	int32 Build(int32 Begin, int32 Count)
	{
		const int32 Index = Nodes.AddDefaulted();
		FAxisAlignedBox2d Bounds = FAxisAlignedBox2d::Empty();
		for (int32 Edge = Begin; Edge < Begin + Count; ++Edge)
		{
			Bounds.Contain(Edges[Edge].Bounds);
		}
		Nodes[Index].Bounds = Bounds;
		Nodes[Index].Begin = Begin;
		Nodes[Index].Count = Count;
		if (Count > 8)
		{
			const FVector2d Size = Bounds.Max - Bounds.Min;
			const int32 Axis = Size.X >= Size.Y ? 0 : 1;
			Algo::Sort(MakeArrayView(Edges.GetData() + Begin, Count), [Axis](const FEdge& A, const FEdge& B) { return A.A[Axis] + A.B[Axis] < B.A[Axis] + B.B[Axis]; });
			const int32 Left = Build(Begin, Count / 2);
			const int32 Right = Build(Begin + Count / 2, Count - Count / 2);
			Nodes[Index].Left = Left;
			Nodes[Index].Right = Right;
		}
		return Index;
	}

	template<typename Visitor>
	bool Visit(int32 Index, const FAxisAlignedBox2d& Bounds, Visitor&& Callback, FBreachSimpleBuildStats& Stats) const
	{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		// 테스트 전용: AABB 트리 없이 같은 경계 질의를 실행하는 비교 기준.
		if (Index == 0 && Stats.bLinearBoundaryQueries)
		{
			for (const FEdge& Edge : Edges)
			{
				++Stats.BoundaryNodeVisits;
				if (Edge.Bounds.Intersects(Bounds))
				{
					++Stats.BoundaryTests;
					if (!Callback(Edge))
					{
						return false;
					}
				}
			}
			return true;
		}
#endif
		++Stats.BoundaryNodeVisits;
		const FNode& Node = Nodes[Index];
		if (!Node.Bounds.Intersects(Bounds))
		{
			return true;
		}
		if (Node.Left != INDEX_NONE)
		{
			if (!Visit(Node.Left, Bounds, Callback, Stats))
			{
				return false;
			}
			return Visit(Node.Right, Bounds, Callback, Stats);
		}
		for (int32 Edge = Node.Begin; Edge < Node.Begin + Node.Count; ++Edge)
		{
			++Stats.BoundaryNodeVisits;
			if (Edges[Edge].Bounds.Intersects(Bounds))
			{
				++Stats.BoundaryTests;
				if (!Callback(Edges[Edge]))
				{
					return false;
				}
			}
		}
		return true;
	}

	static bool OnEdge(const FVector2d& P, const FEdge& Edge)
	{
		const FVector2d AB = Edge.B - Edge.A;
		const FVector2d AP = P - Edge.A;
		const double Cross = AB.X * AP.Y - AB.Y * AP.X;
		return Cross * Cross <= FMath::Square(PositionTolerance) * AB.SquaredLength() && Edge.Bounds.Contains(P);
	}

	bool ContainsPoint(const FVector2d& P, FBreachSimpleBuildStats& Stats) const
	{
		FAxisAlignedBox2d Ray(P, P);
		Ray.Contain(FVector2d(Nodes[0].Bounds.Max.X + 1, P.Y));
		bool bInside = false;
		bool bBoundary = false;
		Visit(0, Ray, [&](const FEdge& Edge)
		{
			if (OnEdge(P, Edge))
			{
				bBoundary = true;
				return false;
			}
			if ((Edge.A.Y > P.Y) != (Edge.B.Y > P.Y))
			{
				const double X = Edge.A.X + (P.Y - Edge.A.Y) * (Edge.B.X - Edge.A.X) / (Edge.B.Y - Edge.A.Y);
				if (X > P.X)
				{
					bInside = !bInside;
				}
			}
			return true;
		}, Stats);
		return bBoundary || bInside;
	}

	bool MayContainEdge(const FVector2d& A, const FVector2d& B, FBreachSimpleBuildStats& Stats) const
	{
		if (!ContainsPoint((A + B) * 0.5, Stats))
		{
			return false;
		}
		FAxisAlignedBox2d Bounds(A, B);
		return Visit(0, Bounds, [&](const FEdge& Edge)
		{
			const double C = ExactPredicates::Orient2(A, B, Edge.A), D = ExactPredicates::Orient2(A, B, Edge.B);
			const double U = ExactPredicates::Orient2(Edge.A, Edge.B, A), V = ExactPredicates::Orient2(Edge.A, Edge.B, B);
			return !(((C < 0 && D > 0) || (C > 0 && D < 0)) && ((U < 0 && V > 0) || (U > 0 && V < 0)));
		}, Stats);
	}

public:
	bool ContainsExact(const FPolygon2d& Polygon, FBreachSimpleBuildStats& Stats) const
	{
		TArray<FGeneralPolygon2d> Outside;
		if (!Difference(TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Polygon)}, TArray<FGeneralPolygon2d>{Domain}, Outside, Stats))
		{
			return false;
		}
		return Area(Outside) <= AreaTolerance;
	}

	explicit FBreachPreparedBoundary(const FGeneralPolygon2d& InDomain) : Domain(InDomain)
	{
		auto Append = [&](const FPolygon2d& Ring)
		{
			for (int32 Index = 0; Index < Ring.VertexCount(); ++Index)
			{
				const FVector2d A = Ring[Index], B = Ring[(Index + 1) % Ring.VertexCount()];
				FAxisAlignedBox2d Bounds(A, A);
				Bounds.Contain(B);
				Edges.Add({A, B, Bounds});
			}
		};
		Append(Domain.GetOuter());
		for (const FPolygon2d& Hole : Domain.GetHoles())
		{
			Append(Hole);
		}
		Build(0, Edges.Num());
	}

	bool MayContainExtension(const FPolygon2d& Polygon, const FVector2d& Added, FBreachSimpleBuildStats& Stats) const
	{
		if (!ContainsPoint(Added, Stats))
		{
			return false;
		}
		for (int32 Index = 0; Index < Polygon.VertexCount(); ++Index)
		{
			if (Polygon[Index] == Added)
			{
				if (!MayContainEdge(Polygon[(Index + Polygon.VertexCount() - 1) % Polygon.VertexCount()], Added, Stats))
				{
					return false;
				}
				return MayContainEdge(Added, Polygon[(Index + 1) % Polygon.VertexCount()], Stats);
			}
		}
		return false;
	}

	bool MayContain(const FPolygon2d& Polygon, FBreachSimpleBuildStats& Stats) const
	{
		if (Polygon.VertexCount() < 3)
		{
			return false;
		}
		bool bAmbiguous = false;
		for (int32 Index = 0; Index < Polygon.VertexCount(); ++Index)
		{
			const FVector2d A = Polygon[Index], B = Polygon[(Index + 1) % Polygon.VertexCount()];
			if (!ContainsPoint(A, Stats))
			{
				return false;
			}
			if (!ContainsPoint((A + B) * 0.5, Stats))
			{
				return false;
			}
			FAxisAlignedBox2d Bounds(A, A);
			Bounds.Contain(B);
			const bool bNoCrossing = Visit(0, Bounds, [&](const FEdge& Edge)
			{
				const double C = ExactPredicates::Orient2(A, B, Edge.A), D = ExactPredicates::Orient2(A, B, Edge.B);
				const double U = ExactPredicates::Orient2(Edge.A, Edge.B, A), V = ExactPredicates::Orient2(Edge.A, Edge.B, B);
				if (((C < 0 && D > 0) || (C > 0 && D < 0)) && ((U < 0 && V > 0) || (U > 0 && V < 0)))
				{
					return false;
				}
				const bool bSharedEnd = A.Equals(Edge.A, PositionTolerance) || A.Equals(Edge.B, PositionTolerance) || B.Equals(Edge.A, PositionTolerance) || B.Equals(Edge.B, PositionTolerance);
				if (!bSharedEnd && (C == 0 || D == 0 || U == 0 || V == 0))
				{
					bAmbiguous = true;
				}
				return true;
			}, Stats);
			if (!bNoCrossing)
			{
				return false;
			}
		}
		for (const FPolygon2d& Hole : Domain.GetHoles())
		{
			if (Polygon.Bounds().Intersects(Hole.Bounds()) && Polygon.Contains(Hole[0]))
			{
				bAmbiguous = true;
			}
		}
		// 경계 접촉을 포함한 잠정 결과다. 채택할 확장·병합은 ContainsExact로 확정한다.
		(void)bAmbiguous;
		return true;
	}
};

namespace
{
	bool Partition(const FGeneralPolygon2d& Region, TArray<FPolygon2d>& Out, FString& Error)
	{
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles;
		if (!BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, Error))
		{
			return false;
		}
		TArray<TArray<int32>> Parts;
		TMap<uint64, TArray<int32>> Owners;
		TArray<uint64> Queue;
		auto EdgeKey = [](int32 A, int32 B) { return (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B)); };
		auto AddEdges = [&](int32 ID)
		{
			const TArray<int32>& Part = Parts[ID];
			for (int32 Index = 0; Index < Part.Num(); ++Index)
			{
				const uint64 Key = EdgeKey(Part[Index], Part[(Index + 1) % Part.Num()]);
				TArray<int32>& EdgeOwners = Owners.FindOrAdd(Key);
				EdgeOwners.Add(ID);
				if (EdgeOwners.Num() == 2)
				{
					Queue.Add(Key);
				}
			}
		};
		auto RemoveEdges = [&](int32 ID)
		{
			const TArray<int32>& Part = Parts[ID];
			for (int32 Index = 0; Index < Part.Num(); ++Index)
			{
				Owners.FindChecked(EdgeKey(Part[Index], Part[(Index + 1) % Part.Num()])).Remove(ID);
			}
		};
		for (const FIndex3i Triangle : Triangles)
		{
			Parts.Add({Triangle.A, Triangle.B, Triangle.C});
			AddEdges(Parts.Num() - 1);
		}
		TMap<FVector2d, int32> PointIndices;
		for (int32 Index = 0; Index < Vertices.Num(); ++Index)
		{
			PointIndices.Add(Vertices[Index], Index);
		}
		auto Polygon = [&](const TArray<int32>& Indices)
		{
			FPolygon2d Result;
			for (int32 Index : Indices)
			{
				Result.AppendVertex(Vertices[Index]);
			}
			return Result;
		};
		for (int32 Q = 0; Q < Queue.Num(); ++Q)
		{
			const TArray<int32> IDs = Owners.FindChecked(Queue[Q]);
			if (IDs.Num() != 2)
			{
				continue;
			}
			const FPolygon2d A = Polygon(Parts[IDs[0]]), B = Polygon(Parts[IDs[1]]);
			const FPolygon2d Merged = CombinedHull(A, B);
			if (FMath::Abs(FMath::Abs(Merged.SignedArea()) - FMath::Abs(A.SignedArea()) - FMath::Abs(B.SignedArea())) > AreaTolerance)
			{
				continue;
			}
			RemoveEdges(IDs[0]);
			RemoveEdges(IDs[1]);
			Parts[IDs[0]].Reset();
			Parts[IDs[1]].Reset();
			for (const FVector2d Point : Merged.GetVertices())
			{
				Parts[IDs[0]].Add(PointIndices.FindChecked(Point));
			}
			AddEdges(IDs[0]);
		}
		for (const TArray<int32>& Part : Parts)
		{
			if (Part.Num() >= 3)
			{
				Out.Add(Polygon(Part));
			}
		}
		Out.Sort([](const FPolygon2d& A, const FPolygon2d& B) { return FMath::Abs(A.SignedArea()) > FMath::Abs(B.SignedArea()); });
		return true;
	}

	FPolygon2d Expand(FPolygon2d Polygon, const TArray<FVector2d>& Targets, const FBreachPreparedBoundary& Boundary, FBreachSimpleBuildStats& Stats)
	{
		TArray<FVector2d> Remaining = Targets;
		int32 AcceptedSteps = 0;
		while (AcceptedSteps < 24 && !Remaining.IsEmpty())
		{
			FPolygon2d Best;
			FVector2d BestPoint = FVector2d::ZeroVector;
			double BestGain = 0.05;
			TArray<FVector2d> Next;
			const double CurrentArea = FMath::Abs(Polygon.SignedArea());
			for (const FVector2d Point : Remaining)
			{
				FPolygon2d Trial;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
				if (Stats.bRebuildTrialHulls)
				{
					TArray<FVector2d> Points(Polygon.GetVertices());
					Points.Add(Point);
					Trial = Hull(Points);
				}
				else
#endif
				{
					if (!AddHullPoint(Polygon, Point, Trial))
					{
						continue;
					}
				}
				const double Gain = FMath::Abs(Trial.SignedArea()) - CurrentArea;
				if (Gain <= 0.05)
				{
					continue;
				}
				if (!Boundary.MayContainExtension(Trial, Point, Stats))
				{
					continue;
				}
				Next.Add(Point);
				if (Gain > BestGain)
				{
					Best = MoveTemp(Trial);
					BestPoint = Point;
					BestGain = Gain;
				}
			}
			if (Best.VertexCount() < 3)
			{
				break;
			}
			Remaining = MoveTemp(Next);
			Remaining.RemoveSingleSwap(BestPoint);
			// 불변식은 각 채택 단계에 확인한다. 실패한 점은 다시 시도하지 않는다.
			if (!Boundary.ContainsExact(Best, Stats))
			{
				continue;
			}
			Polygon = MoveTemp(Best);
			++AcceptedSteps;
		}
		return Polygon;
	}

	void Merge(TArray<FPolygon2d>& Pieces, const FBreachPreparedBoundary& Boundary, FBreachSimpleBuildStats& Stats)
	{
		for (int32 A = 0; A < Pieces.Num(); ++A)
		{
			for (int32 B = Pieces.Num() - 1; B > A; --B)
			{
				FPolygon2d Combined = CombinedHull(Pieces[A], Pieces[B]);
				if (!Boundary.MayContain(Combined, Stats))
				{
					continue;
				}
				if (Boundary.ContainsExact(Combined, Stats))
				{
					Pieces[A] = MoveTemp(Combined);
					Pieces.RemoveAt(B);
				}
			}
		}
	}
}

FPolygon2d BreachPassageSolver::Transform(const FPolygon2d& Polygon, const FVector2d& Scale)
{
	FPolygon2d Out;
	for (const FVector2d P : Polygon.GetVertices())
	{
		Out.AppendVertex({P.X * Scale.X, P.Y * Scale.Y});
	}
	return Out;
}

FGeneralPolygon2d BreachPassageSolver::Transform(const FGeneralPolygon2d& Polygon, const FVector2d& Scale)
{
	FGeneralPolygon2d Out(Transform(Polygon.GetOuter(), Scale));
	for (const FPolygon2d& Hole : Polygon.GetHoles())
	{
		Out.AddHole(Transform(Hole, Scale), false, false);
	}
	return Out;
}

bool BreachPassageSolver::Prepare(const FGeneralPolygon2d& Damage, const FPolygon2d& Wall, double Radius, FBreachPreparedOpening& Out, FBreachSimpleBuildStats& Stats, FString& Error)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | PrepareOpening");
	++Stats.PreparedOpenings;
	TArray<FGeneralPolygon2d> Clipped, Core, Restored;
	++Stats.BooleanOperations;
	if (!PolygonsIntersection(TArray<FGeneralPolygon2d>{Damage}, TArray<FGeneralPolygon2d>{FGeneralPolygon2d(Wall)}, Clipped))
	{
		Error = TEXT("Opening could not be confined to the original wall.");
		return false;
	}
	const double Margin = FMath::Max(0.001, Radius * 0.004);
	Out.Damage = Clipped;
	if (!Offset(-Radius - Margin, Clipped, Core, Stats))
	{
		Error = TEXT("Opening erosion failed.");
		return false;
	}
	if (Core.IsEmpty())
	{
		return true;
	}
	if (!Offset(Radius, Core, Restored, Stats))
	{
		Error = TEXT("Opening restoration failed.");
		return false;
	}
	++Stats.BooleanOperations;
	if (!PolygonsIntersection(Restored, Clipped, Out.Available))
	{
		Error = TEXT("Restored opening could not be confined to actual damage.");
		return false;
	}
	for (const FGeneralPolygon2d& Region : Core)
	{
		TArray<FPolygon2d> ConvexCores;
		if (!Partition(Region, ConvexCores, Error))
		{
			return false;
		}
		if (ConvexCores.IsEmpty())
		{
			continue;
		}
		const FGeneralPolygon2d* Owner = Clipped.FindByPredicate([&](const FGeneralPolygon2d& P) { return P.Contains(Center(ConvexCores[0])); });
		if (!Owner)
		{
			Error = TEXT("Opening core has no containing damage region.");
			return false;
		}
		const FBreachPreparedBoundary DamageBoundary(*Owner);
		for (int32 Index = 0; Index < FMath::Min(8, ConvexCores.Num()); ++Index)
		{
			TArray<FGeneralPolygon2d> Expanded;
			if (!Offset(Radius, General(MakeArrayView(&ConvexCores[Index], 1)), Expanded, Stats))
			{
				Error = TEXT("Convex opening candidate restoration failed.");
				return false;
			}
			TArray<FVector2d> Points;
			for (const FGeneralPolygon2d& Part : Expanded)
			{
				Points.Append(Part.GetOuter().GetVertices());
			}
			FPolygon2d Candidate = Hull(Points);
			if (DamageBoundary.ContainsExact(Candidate, Stats))
			{
				Out.Candidates.Add(MoveTemp(Candidate));
			}
			FPolygon2d Circle;
			const FVector2d C = Center(ConvexCores[Index]);
			for (int32 Vertex = 0; Vertex < 24; ++Vertex)
			{
				const double Angle = UE_DOUBLE_TWO_PI * Vertex / 24;
				Circle.AppendVertex(C + Radius * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)));
			}
			if (DamageBoundary.ContainsExact(Circle, Stats))
			{
				Out.Candidates.Add(MoveTemp(Circle));
			}
		}
	}
	// 독립적인 offset 호출의 원호 분할이 달라도 실제 손상 안의 유효 후보가
	// 공통 복원 윤곽 밖으로 몇 ulp 나갔다는 이유로 누락되지 않게 한다.
	TArray<FGeneralPolygon2d> DomainInput(Out.Available);
	DomainInput.Append(General(Out.Candidates));
	if (!Union(DomainInput, Out.Available, Stats))
	{
		Error = TEXT("Restored opening candidates could not be assembled.");
		return false;
	}
	return true;
}

bool BreachPassageSolver::Solve(TConstArrayView<TSharedPtr<const FBreachPreparedOpening, ESPMode::ThreadSafe>> Openings, const FBreachPassageState& Previous, int32 MaxCount, double Radius, FBreachPassageState& Out, FBreachSimpleBuildStats& Stats, FString& Error)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | SolvePassages");
	Out.MaxCount = MaxCount;
	TArray<FGeneralPolygon2d> Available(Previous.Passages);
	TArray<FGeneralPolygon2d> ActualDamage;
	TArray<FPolygon2d> Seeds;
	for (const auto& Opening : Openings)
	{
		check(Opening.IsValid());
		Available.Append(Opening->Available);
		ActualDamage.Append(Opening->Damage);
		Seeds.Append(Opening->Candidates);
	}
	TArray<FGeneralPolygon2d> Domains;
	if (!Union(Available, Domains, Stats))
	{
		Error = TEXT("Opening bodies could not be assembled.");
		return false;
	}
	for (const FGeneralPolygon2d& Domain : Domains)
	{
		const auto* Reused = Previous.Bodies.FindByPredicate([&](const auto& Body)
		{
			if (SameDomain(Body->Domain, Domain))
			{
				return true;
			}
			const FAxisAlignedBox2d A = Body->Domain.GetOuter().Bounds(), B = Domain.GetOuter().Bounds();
			if (!A.Min.Equals(B.Min, PositionTolerance) || !A.Max.Equals(B.Max, PositionTolerance) || FMath::Abs(Body->Domain.SignedArea() - Domain.SignedArea()) > AreaTolerance)
			{
				return false;
			}
			TArray<FGeneralPolygon2d> Added, Lost;
			if (!Difference(TArray<FGeneralPolygon2d>{Domain}, TArray<FGeneralPolygon2d>{Body->Domain}, Added, Stats))
			{
				return false;
			}
			if (!Difference(TArray<FGeneralPolygon2d>{Body->Domain}, TArray<FGeneralPolygon2d>{Domain}, Lost, Stats))
			{
				return false;
			}
			return Area(Added) + Area(Lost) <= AreaTolerance;
		});
		if (Reused && Previous.MaxCount == MaxCount)
		{
			++Stats.ReusedBodies;
			Out.Bodies.Add(*Reused);
			continue;
		}
		auto Body = MakeShared<FBreachPassageBody, ESPMode::ThreadSafe>();
		Body->Domain = Domain;
		Body->Boundary = MakeShared<FBreachPreparedBoundary, ESPMode::ThreadSafe>(Domain);
		for (const auto& OldBody : Previous.Bodies)
		{
			for (const FPolygon2d& Piece : OldBody->Pieces)
			{
				if (Domain.Contains(Center(Piece)))
				{
					Body->Pieces.Add(Piece);
				}
			}
		}
		const TArray<FGeneralPolygon2d> Required = General(Body->Pieces);
		if (Domain.GetHoles().IsEmpty())
		{
			FPolygon2d Whole = Hull(Domain.GetOuter().GetVertices());
			if (Body->Boundary->ContainsExact(Whole, Stats))
			{
				// 이미 볼록한 본체는 시드별로 같은 영역을 다시 탐색하지 않는다.
				Body->Pieces = {MoveTemp(Whole)};
				Out.Bodies.Add(Body);
				continue;
			}
		}
		TArray<FVector2d> BoundaryPoints(Domain.GetOuter().GetVertices());
		for (const FPolygon2d& Hole : Domain.GetHoles())
		{
			BoundaryPoints.Append(Hole.GetVertices());
		}
		TArray<FVector2d> Targets;
		for (int32 Index = 0; Index < FMath::Min(128, BoundaryPoints.Num()); ++Index)
		{
			Targets.Add(BoundaryPoints[Index * BoundaryPoints.Num() / FMath::Min(128, BoundaryPoints.Num())]);
		}
		Merge(Body->Pieces, *Body->Boundary, Stats);
		for (FPolygon2d& Piece : Body->Pieces)
		{
			Piece = Expand(MoveTemp(Piece), Targets, *Body->Boundary, Stats);
		}
		Merge(Body->Pieces, *Body->Boundary, Stats);
		const double MinAddition = FMath::Max(UE_DOUBLE_PI * Radius * Radius * 0.05, FMath::Abs(Domain.SignedArea()) * 0.10);
		TArray<FGeneralPolygon2d> CurrentCovered;
		if (!Union(General(Body->Pieces), CurrentCovered, Stats))
		{
			return false;
		}
		if (CurrentCovered.Num() == 1 && FMath::Abs(Domain.SignedArea()) - Area(CurrentCovered) <= MinAddition)
		{
			// 이미 연결됐고 남은 면적도 추가 기준 이하라면 후보 전수 확장이 불필요하다.
			Out.Bodies.Add(Body);
			continue;
		}
		TArray<FPolygon2d> Candidates;
		TArray<FPolygon2d> BodySeeds;
		bool bBodySolved = false;
		for (const FPolygon2d& Seed : Seeds)
		{
			if (Domain.Contains(Center(Seed)))
			{
				BodySeeds.Add(Seed);
			}
		}
		struct FSeedResult
		{
			FPolygon2d Candidate;
			FBreachSimpleBuildStats Stats;
		};
		TArray<FSeedResult> SeedResults;
		SeedResults.SetNum(BodySeeds.Num());
		int32 Groups = FMath::Min(4, BodySeeds.Num());
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		// 테스트 전용: 기준 경로는 전체 hull을 원래 순서로 단일 그룹에서 계산한다.
		if (Stats.bRebuildTrialHulls)
		{
			Groups = FMath::Min(1, BodySeeds.Num());
		}
#endif
		if (Groups > 0)
		{
			ParallelFor(Groups, [&](int32 Group)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_STR("BreachSimpleCollision | FitCandidateGroup");
				const int32 Begin = Group * BodySeeds.Num() / Groups;
				const int32 End = (Group + 1) * BodySeeds.Num() / Groups;
				for (int32 Index = Begin; Index < End; ++Index)
				{
					FSeedResult& Result = SeedResults[Index];
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
					Result.Stats.bLinearBoundaryQueries = Stats.bLinearBoundaryQueries;
					Result.Stats.bRebuildTrialHulls = Stats.bRebuildTrialHulls;
#endif
					const FPolygon2d& Seed = BodySeeds[Index];
					if (Body->Boundary->ContainsExact(Seed, Result.Stats))
					{
						Result.Candidate = Expand(Seed, Targets, *Body->Boundary, Result.Stats);
					}
				}
			}, EParallelForFlags::Unbalanced | EParallelForFlags::BackgroundPriority);
		}
		for (const FSeedResult& Result : SeedResults)
		{
			Stats.BooleanOperations += Result.Stats.BooleanOperations;
			Stats.BoundaryTests += Result.Stats.BoundaryTests;
			Stats.BoundaryNodeVisits += Result.Stats.BoundaryNodeVisits;
		}
		for (FSeedResult& Result : SeedResults)
		{
			FPolygon2d& Candidate = Result.Candidate;
			if (Candidate.VertexCount() < 3)
			{
				continue;
			}
			const bool bGainBoundReached = FMath::Abs(Domain.SignedArea()) - FMath::Abs(Candidate.SignedArea()) <= AreaTolerance;
			Candidates.Add(Candidate);
			if (bGainBoundReached)
			{
				TArray<FGeneralPolygon2d> Missing;
				if (!Difference(CurrentCovered, General(MakeArrayView(&Candidate, 1)), Missing, Stats))
				{
					return false;
				}
				if (Area(Missing) <= AreaTolerance)
				{
					Body->Pieces = {MoveTemp(Candidate)};
					bBodySolved = true;
					break;
				}
			}
		}

		if (bBodySolved)
		{
			Out.Bodies.Add(Body);
			continue;
		}
		Candidates.Sort([](const FPolygon2d& A, const FPolygon2d& B) { return FMath::Abs(A.SignedArea()) > FMath::Abs(B.SignedArea()); });
		while (Body->Pieces.Num() < MaxCount)
		{
			TArray<FGeneralPolygon2d> Covered;
			if (!Union(General(Body->Pieces), Covered, Stats))
			{
				return false;
			}
			int32 Best = INDEX_NONE;
			double BestGain = MinAddition;
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				const FPolygon2d& Candidate = Candidates[Index];
				TArray<FGeneralPolygon2d> Additional;
				if (!Difference(General(MakeArrayView(&Candidate, 1)), Covered, Additional, Stats))
				{
					return false;
				}
				int32 Touched = 0;
				for (const FGeneralPolygon2d& Part : Covered)
				{
					TArray<FGeneralPolygon2d> Overlap;
					++Stats.BooleanOperations;
					if (!PolygonsIntersection(General(MakeArrayView(&Candidate, 1)), MakeArrayView(&Part, 1), Overlap))
					{
						return false;
					}
					Touched += Area(Overlap) > AreaTolerance ? 1 : 0;
				}
				const double Gain = Area(Additional) + (Touched > 1 ? FMath::Abs(Domain.SignedArea()) : 0);
				if (Gain > BestGain)
				{
					Best = Index;
					BestGain = Gain;
				}
			}
			if (Best == INDEX_NONE)
			{
				break;
			}
			Body->Pieces.Add(Candidates[Best]);
			Merge(Body->Pieces, *Body->Boundary, Stats);
		}
		// 조각 단위의 고정 대신 기존 통로 전체를 보존하는 교체를 허용한다.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			bool bChanged = false;
			for (int32 Index = 0; Index < Body->Pieces.Num(); ++Index)
			{
				TArray<FPolygon2d> Others(Body->Pieces);
				Others.RemoveAt(Index);
				TArray<FGeneralPolygon2d> OtherUnion, Essential, Current;
				if (!Union(General(Others), OtherUnion, Stats))
				{
					return false;
				}
				if (!Difference(Required, OtherUnion, Essential, Stats))
				{
					return false;
				}
				if (!Union(General(Body->Pieces), Current, Stats))
				{
					return false;
				}
				double BestScore = Area(Current);
				for (int32 CandidateIndex = 0; CandidateIndex < FMath::Min(12, Candidates.Num()); ++CandidateIndex)
				{
					TArray<FVector2d> Points(Candidates[CandidateIndex].GetVertices());
					for (const FGeneralPolygon2d& Part : Essential)
					{
						Points.Append(Part.GetOuter().GetVertices());
					}
					FPolygon2d Replacement = Hull(Points);
					if (!Body->Boundary->MayContain(Replacement, Stats))
					{
						continue;
					}
					if (!Body->Boundary->ContainsExact(Replacement, Stats))
					{
						continue;
					}
					TArray<FGeneralPolygon2d> TrialInput(OtherUnion), Trial, Lost;
					TrialInput.Emplace(Replacement);
					if (!Union(TrialInput, Trial, Stats))
					{
						return false;
					}
					if (!Difference(Required, Trial, Lost, Stats))
					{
						return false;
					}
					const double Score = Area(Trial) + (Current.Num() - Trial.Num()) * FMath::Abs(Domain.SignedArea());
					if (Area(Lost) <= AreaTolerance && Score > BestScore + MinAddition)
					{
						Body->Pieces[Index] = MoveTemp(Replacement);
						BestScore = Score;
						bChanged = true;
					}
				}
			}
			Merge(Body->Pieces, *Body->Boundary, Stats);
			if (!bChanged)
			{
				break;
			}
		}
		Out.Bodies.Add(Body);
	}
	TArray<FGeneralPolygon2d> Pieces;
	for (const auto& Body : Out.Bodies)
	{
		Pieces.Append(General(Body->Pieces));
		Stats.OverLimitBodies += Body->Pieces.Num() > MaxCount ? 1 : 0;
		Out.PieceCount += Body->Pieces.Num();
	}
	Stats.PassagePieceCount = Out.PieceCount;
	TArray<FGeneralPolygon2d> Lost, Outside;
	if (!Union(Pieces, Out.Passages, Stats))
	{
		Error = TEXT("Passage union failed.");
		return false;
	}
	if (!Difference(Previous.Passages, Out.Passages, Lost, Stats))
	{
		Error = TEXT("Passage preservation could not be checked.");
		return false;
	}
	if (!Difference(Out.Passages, ActualDamage, Outside, Stats))
	{
		Error = TEXT("Passage containment and preservation could not be checked.");
		return false;
	}
	if (Area(Lost) > AreaTolerance || Area(Outside) > AreaTolerance)
	{
		Error = FString::Printf(TEXT("Passage invariant failed: lost %.9g, outside %.9g."), Area(Lost), Area(Outside));
		return false;
	}
	return true;
}
