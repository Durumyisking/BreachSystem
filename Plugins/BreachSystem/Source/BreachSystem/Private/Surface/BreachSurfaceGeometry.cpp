#include "Surface/BreachSurfaceGeometry.h"

using namespace UE::Geometry;

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



void BreachSurfaceGeometry::DebugDrawPolygon2ds(UObject* InWorldContext, const FTransform& InTransform, TArray<FGeneralPolygon2d> InGeneralPolygon2ds)
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
        DebugDrawPolygon2d(InWorldContext, InTransform, GeneralPolygon2d);
        
        const TArray <UE::Geometry::FPolygon2d> Holes = GeneralPolygon2d.GetHoles();
        for (auto& Hole : Holes)
        {
            BreachSurfaceGeometry::DebugDrawPolygon2d(InWorldContext, InTransform, Hole, FColor::Red);
        }
    }
}

void BreachSurfaceGeometry::DebugDrawPolygon2d(UObject* InWorldContext, const FTransform& InTransform, UE::Geometry::FGeneralPolygon2d InGeneralPolygon2d, const FColor& InColor)
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
        DrawDebugLine(World, Start, End, InColor, false, 2.f, SDPG_Foreground, 2.0f);
    }
}


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

