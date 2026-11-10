#include "Surface/BreachSurfaceGeometry.h"
#include "CompGeom/Delaunay2.h"
#include "CompGeom/ExactPredicates.h"

using namespace UE::Geometry;

namespace
{
    bool OnBoundarySegment(const FVector2d& A, const FVector2d& B, const FVector2d& Point)
    {
        if (Point.X < FMath::Min(A.X, B.X) || Point.X > FMath::Max(A.X, B.X) || Point.Y < FMath::Min(A.Y, B.Y) || Point.Y > FMath::Max(A.Y, B.Y))
        {
            return false;
        }
        return ExactPredicates::Orient2(A, B, Point) == 0;
    }

    // 경계 위는 0, 내부는 1, 외부는 -1. 접촉을 좌표 오차로 밀어내지 않는다.
    int32 ClassifyBoundaryPoint(const FPolygon2d& Loop, const FVector2d& Point)
    {
        int32 Winding = 0;
        for (int32 Index = 0; Index < Loop.VertexCount(); ++Index)
        {
            const FVector2d& A = Loop[Index];
            const FVector2d& B = Loop[(Index + 1) % Loop.VertexCount()];
            if (OnBoundarySegment(A, B, Point))
            {
                return 0;
            }
            const double Side = ExactPredicates::Orient2(A, B, Point);
            if (A.Y <= Point.Y && Point.Y < B.Y && Side > 0)
            {
                ++Winding;
            }
            else if (B.Y <= Point.Y && Point.Y < A.Y && Side < 0)
            {
                --Winding;
            }
        }
        return Winding == 0 ? -1 : 1;
    }

    double BoundaryOrientation(const FPolygon2d& Loop)
    {
        int32 Corner = 0;
        for (int32 Index = 1; Index < Loop.VertexCount(); ++Index)
        {
            if (Loop[Index].X < Loop[Corner].X || (Loop[Index].X == Loop[Corner].X && Loop[Index].Y < Loop[Corner].Y))
            {
                Corner = Index;
            }
        }
        const int32 Previous = (Corner + Loop.VertexCount() - 1) % Loop.VertexCount();
        for (int32 Offset = 1; Offset < Loop.VertexCount() - 1; ++Offset)
        {
            const int32 Next = (Corner + Offset) % Loop.VertexCount();
            const double Side = ExactPredicates::Orient2(Loop[Previous], Loop[Corner], Loop[Next]);
            if (Side != 0)
            {
                return Side;
            }
        }
        return 0;
    }
}

bool BreachSurfaceGeometry::ResolveBoundaryOverlaps(TArray<FGeneralPolygon2d>& Regions, FString& OutError)
{
    OutError.Reset();
    TArray<FGeneralPolygon2d> Resolved;
    for (const FGeneralPolygon2d& Region : Regions)
    {
        TArray<FVector2d> Vertices;
        TMap<FVector2d, int32> VertexIDs;
        TArray<TArray<int32>> Loops;
        const bool bClockwise = Region.OuterIsClockwise();
        auto AddLoop = [&](const FPolygon2d& Polygon)
        {
            TArray<int32>& Loop = Loops.Emplace_GetRef();
            for (int32 Index = 0; Index < Polygon.VertexCount(); ++Index)
            {
                const FVector2d& Point = Polygon[bClockwise ? Polygon.VertexCount() - Index - 1 : Index];
                if (!VertexIDs.Contains(Point))
                {
                    VertexIDs.Add(Point, Vertices.Add(Point));
                }
                Loop.Add(VertexIDs.FindChecked(Point));
            }
        };
        AddLoop(Region.GetOuter());
        for (const FPolygon2d& Hole : Region.GetHoles())
        {
            AddLoop(Hole);
        }

        TMap<FIndex2i, int32> EdgeCounts;
        for (const TArray<int32>& Loop : Loops)
        {
            for (int32 Index = 0; Index < Loop.Num(); ++Index)
            {
                const FVector2d& A = Vertices[Loop[Index]];
                const FVector2d& B = Vertices[Loop[(Index + 1) % Loop.Num()]];
                if (A == B)
                {
                    OutError = TEXT("경계 복원 입력에 길이가 0인 변이 있습니다.");
                    return false;
                }
                TArray<int32> Chain;
                for (int32 PointIndex = 0; PointIndex < Vertices.Num(); ++PointIndex)
                {
                    if (OnBoundarySegment(A, B, Vertices[PointIndex]))
                    {
                        Chain.Add(PointIndex);
                    }
                }
                const int32 Axis = A.X != B.X ? 0 : 1;
                Chain.Sort([&](int32 Left, int32 Right)
                {
                    return B[Axis] > A[Axis] ? Vertices[Left][Axis] < Vertices[Right][Axis] : Vertices[Left][Axis] > Vertices[Right][Axis];
                });
                for (int32 ChainIndex = 1; ChainIndex < Chain.Num(); ++ChainIndex)
                {
                    ++EdgeCounts.FindOrAdd(FIndex2i(Chain[ChainIndex - 1], Chain[ChainIndex]));
                }
            }
        }

        bool bHasOverlap = false;
        TArray<FIndex2i> Edges;
        for (const TPair<FIndex2i, int32>& Pair : EdgeCounts)
        {
            const int32 ReverseCount = EdgeCounts.FindRef(FIndex2i(Pair.Key.B, Pair.Key.A));
            bHasOverlap |= ReverseCount > 0;
            const int32 Count = Pair.Value - ReverseCount;
            if (Count > 1)
            {
                OutError = TEXT("경계 복원 입력에서 같은 방향의 변이 중복됩니다.");
                return false;
            }
            if (Count == 1)
            {
                Edges.Add(Pair.Key);
            }
        }
        if (!bHasOverlap)
        {
            Resolved.Add(Region);
            continue;
        }

        TArray<TArray<int32>> Outgoing;
        Outgoing.SetNum(Vertices.Num());
        for (int32 Index = 0; Index < Edges.Num(); ++Index)
        {
            Outgoing[Edges[Index].A].Add(Index);
        }
        TArray<int32> NextEdges;
        TSet<int32> UsedNextEdges;
        for (const FIndex2i& Edge : Edges)
        {
            const FVector2d& Pivot = Vertices[Edge.B];
            const FVector2d& Previous = Vertices[Edge.A];
            auto Half = [&](int32 Candidate)
            {
                const FVector2d& Point = Vertices[Edges[Candidate].B];
                const double Side = ExactPredicates::Orient2(Pivot, Previous, Point);
                const int32 Axis = Previous.X != Pivot.X ? 0 : 1;
                const bool bSameDirection = (Previous[Axis] > Pivot[Axis]) == (Point[Axis] > Pivot[Axis]);
                return Side < 0 || (Side == 0 && bSameDirection) ? 0 : 1;
            };
            int32 Next = INDEX_NONE;
            // 들어온 변의 역방향에서 시계방향으로 골라, 벽이 진행 방향의 왼쪽에 남도록 한다.
            for (int32 Candidate : Outgoing[Edge.B])
            {
                if (Next == INDEX_NONE)
                {
                    Next = Candidate;
                    continue;
                }
                if (Half(Candidate) < Half(Next))
                {
                    Next = Candidate;
                }
                else if (Half(Candidate) == Half(Next))
                {
                    if (ExactPredicates::Orient2(Pivot, Vertices[Edges[Candidate].B], Vertices[Edges[Next].B]) < 0)
                    {
                        Next = Candidate;
                    }
                }
            }
            if (Next == INDEX_NONE || UsedNextEdges.Contains(Next))
            {
                OutError = TEXT("겹친 경계를 닫힌 영역으로 연결할 수 없습니다.");
                return false;
            }
            NextEdges.Add(Next);
            UsedNextEdges.Add(Next);
        }

        TArray<bool> Visited;
        Visited.Init(false, Edges.Num());
        TArray<FGeneralPolygon2d> RegionParts;
        TArray<FPolygon2d> Holes;
        for (int32 Start = 0; Start < Edges.Num(); ++Start)
        {
            if (Visited[Start])
            {
                continue;
            }
            FPolygon2d Loop;
            int32 Edge = Start;
            while (!Visited[Edge])
            {
                Visited[Edge] = true;
                Loop.AppendVertex(Vertices[Edges[Edge].A]);
                Edge = NextEdges[Edge];
            }
            if (Edge != Start || Loop.VertexCount() < 3)
            {
                OutError = TEXT("복원된 경계가 유효한 닫힌 영역을 이루지 않습니다.");
                return false;
            }
            const double Orientation = BoundaryOrientation(Loop);
            if (Orientation == 0)
            {
                OutError = TEXT("복원된 경계의 모든 정점이 일직선입니다.");
                return false;
            }
            if (Orientation > 0)
            {
                RegionParts.Add(FGeneralPolygon2d(Loop));
            }
            else
            {
                Holes.Add(MoveTemp(Loop));
            }
        }
        for (const FPolygon2d& Hole : Holes)
        {
            int32 Container = INDEX_NONE;
            double SmallestArea = TNumericLimits<double>::Max();
            for (int32 Index = 0; Index < RegionParts.Num(); ++Index)
            {
                const FPolygon2d& Outer = RegionParts[Index].GetOuter();
                bool bOutside = false;
                bool bInside = false;
                for (const FVector2d& Point : Hole.GetVertices())
                {
                    const int32 Location = ClassifyBoundaryPoint(Outer, Point);
                    bOutside |= Location < 0;
                    bInside |= Location > 0;
                }
                const double Area = FMath::Abs(Outer.SignedArea());
                if (!bOutside && bInside && Area < SmallestArea)
                {
                    Container = Index;
                    SmallestArea = Area;
                }
            }
            if (Container == INDEX_NONE)
            {
                OutError = TEXT("복원된 구멍을 포함하는 외곽 경계를 찾을 수 없습니다.");
                return false;
            }
            RegionParts[Container].AddHole(Hole, false, false);
        }
        for (FGeneralPolygon2d& Part : RegionParts)
        {
            if (bClockwise)
            {
                FPolygon2d Outer = Part.GetOuter();
                Outer.Reverse();
                FGeneralPolygon2d Reversed(Outer);
                for (FPolygon2d Hole : Part.GetHoles())
                {
                    Hole.Reverse();
                    Reversed.AddHole(Hole, false, false);
                }
                Part = MoveTemp(Reversed);
            }
            Resolved.Add(MoveTemp(Part));
        }
    }
    Regions = MoveTemp(Resolved);
    return true;
}

bool BreachSurfaceGeometry::TriangulateRegion(const FGeneralPolygon2d& Region, TArray<FVector2d>& OutVertices,
    TArray<FIndex3i>& OutTriangles, FString& OutError)
{
    OutError.Reset();

    FDelaunay2 Delaunay;
    const bool bTriangulated = Delaunay.Triangulate(Region, &OutTriangles, &OutVertices, false);
    if (!bTriangulated)
    {
        const TCHAR* Reason = TEXT("알 수 없음: 분류되지 않은 실패");
        switch (Delaunay.GetResult())
        {
        case FDelaunay2::EResult::EmptyInput:
            Reason = TEXT("입력 없음: 입력 정점이 없음");
            break;
        case FDelaunay2::EResult::Collinear:
            Reason = TEXT("일직선: 입력 정점들이 2차원 영역을 이루지 않음");
            break;
        case FDelaunay2::EResult::MissingEdges:
            Reason = TEXT("경계 누락: 삼각분할 결과에 일부 경계 변이 포함되지 않음");
            break;
        case FDelaunay2::EResult::NotComputed:
            Reason = TEXT("계산 안 됨: 삼각분할이 수행되지 않음");
            break;
        case FDelaunay2::EResult::Success:
        case FDelaunay2::EResult::Unknown:
            break;
        }

        OutError = FString::Printf(
            TEXT("영역 삼각분할에 실패했습니다. %s. 정점 수=%d, 구멍 수=%d, 중복 정점=%s."),
            Reason, OutVertices.Num(), Region.GetHoles().Num(), Delaunay.HasDuplicates() ? TEXT("있음") : TEXT("없음"));
        return false;
    }

    if (OutTriangles.IsEmpty())
    {
        OutError = TEXT("남은 영역을 삼각분할한 결과에 면이 없습니다.");
        return false;
    }

    double TriangulatedArea = 0;
    for (const FIndex3i& Triangle : OutTriangles)
    {
        if (!OutVertices.IsValidIndex(Triangle.A) || !OutVertices.IsValidIndex(Triangle.B) || !OutVertices.IsValidIndex(Triangle.C))
        {
            OutError = TEXT("삼각분할 결과에 유효하지 않은 정점 인덱스가 있습니다.");
            return false;
        }

        const FVector2d AB = OutVertices[Triangle.B] - OutVertices[Triangle.A];
        const FVector2d AC = OutVertices[Triangle.C] - OutVertices[Triangle.A];
        const double Area = FMath::Abs(AB.X * AC.Y - AB.Y * AC.X) * 0.5;
        if (!FMath::IsFinite(Area))
        {
            OutError = TEXT("삼각분할 결과에 퇴화한 면이 생성되었습니다.");
            return false;
        }

        TriangulatedArea += Area;
    }

    const double RegionArea = FMath::Abs(Region.SignedArea());
    if (FMath::Abs(TriangulatedArea - RegionArea) > FMath::Max(1.e-6, RegionArea * 1.e-6))
    {
        OutError = TEXT("삼각분할 결과가 남은 영역을 완전히 덮지 못합니다.");
        return false;
    }

    return true;
}

UE::Geometry::FGeneralPolygon2d BreachSurfaceGeometry::MakeCircleCutter(const FVector2d& Center, double Radius, int32 Segments)
{
    Radius = 0.f >= Radius ? 1.f : Radius;
    Segments = 8 >= Segments ? 16 : Segments;

    FPolygon2d Polygon2d = FPolygon2d::MakeCircle(Radius, Segments, 0.0);
    Polygon2d.Translate(Center);
    FGeneralPolygon2d GeneralPolygon2d(Polygon2d);
    return GeneralPolygon2d;
}

UE::Geometry::FGeneralPolygon2d BreachSurfaceGeometry::MakeBoxCutter(const FVector2d& Center, FVector2d Size)
{
    Size.X = 0.f >= Size.X ? 1.f : Size.X;
    Size.Y = 0.f >= Size.Y ? 1.f : Size.Y;

    FPolygon2d Polygon2d = FPolygon2d::MakeRectangle(Center, Size.X, Size.Y);
    FGeneralPolygon2d GeneralPolygon2d(Polygon2d);
    return GeneralPolygon2d;
}

UE::Geometry::FGeneralPolygon2d BreachSurfaceGeometry::MakeRoughCircleCutter(const FVector2d& Center, double Radius, int32 Segments, double Roughness, int32 Seed)
{
    Radius = 0.f >= Radius ? 1.f : Radius;
    Segments = 8 >= Segments ? 16 : Segments;
    Roughness = 0.f > Roughness ? 0.f : Roughness;
    Seed = -1 == Seed ? FMath::Rand() : Seed;

    FPolygon2d Polygon2d = FPolygon2d::MakeCircle(Radius, Segments, 0.0);
    Polygon2d.Translate(Center);

    FRandomStream RandomStream(Seed);
 
    for (int32 i = 0; i < Polygon2d.VertexCount(); ++i)
    {
        const FVector2d Direction = (Polygon2d[i] - Center).GetSafeNormal();
        const float Offset = RandomStream.FRandRange(-Roughness, Roughness);
        Polygon2d[i] += Direction * Offset;
    }

    FGeneralPolygon2d GeneralPolygon2d(Polygon2d);
    return GeneralPolygon2d;
}



#if WITH_EDITOR
void BreachSurfaceGeometry::DebugDrawPolygon2ds(UObject* InWorldContext, const FTransform& InTransform, TArray<FGeneralPolygon2d> InGeneralPolygon2ds, const FColor& InColor, const FColor& InHoleColor, const float InTime)
{
    if (!InWorldContext)
    {
        return;
    }
    UWorld* World = InWorldContext->GetWorld();
    if (!World)
    {
        return;
    }
    for (auto& GeneralPolygon2d : InGeneralPolygon2ds)
    {
        DebugDrawPolygon2d(InWorldContext, InTransform, GeneralPolygon2d, InColor, InTime);
        
        const TArray <UE::Geometry::FPolygon2d> Holes = GeneralPolygon2d.GetHoles();
        for (auto& Hole : Holes)
        {
            BreachSurfaceGeometry::DebugDrawPolygon2d(InWorldContext, InTransform, Hole, InHoleColor, InTime);
        }
    }
}

void BreachSurfaceGeometry::DebugDrawPolygon2d(UObject* InWorldContext, const FTransform& InTransform, UE::Geometry::FGeneralPolygon2d InGeneralPolygon2d, const FColor& InColor, const float InTime)
{
    if (!InWorldContext)
    {
        return;
    }
    UWorld* World = InWorldContext->GetWorld();
    if (!World)
    {
        return;
    }

    const TArray<FVector2D>& OuterVertices = InGeneralPolygon2d.GetOuter().GetVertices();
    for (int32 i = 0; i < OuterVertices.Num(); ++i)
    {
        const FVector Start = SurfaceToWorld(InTransform, OuterVertices[i]);
        const FVector End = SurfaceToWorld(InTransform, OuterVertices[(i + 1) % OuterVertices.Num()]);
        DrawDebugLine(World, Start, End, InColor, false, InTime, SDPG_Foreground, 2.0f);
    }
}


#endif

FVector2D BreachSurfaceGeometry::WorldToSurface(const FTransform& InTransform, const FVector& InWorldPoint)
{
    const FVector LocalPoint = InTransform.InverseTransformPosition(InWorldPoint);
    return FVector2D(LocalPoint.X, LocalPoint.Z);
}

FVector BreachSurfaceGeometry::SurfaceToWorld(const FTransform& InTransform, const FVector2D& InSurfacePoint)
{
    const FVector LocalPoint(InSurfacePoint.X, 0.0, InSurfacePoint.Y);
    return InTransform.TransformPosition(LocalPoint);
}

