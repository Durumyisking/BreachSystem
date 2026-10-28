#include "Surface/BreachAccumulationShape.h"
#include "CompGeom/ConvexHull2.h"

bool BreachAccumulationShape::GenerateBase(TConstArrayView<FVector2d> ImpactPoints, double Padding,
	UE::Geometry::FPolygon2d& OutPolygon, FString& OutError)
{
	using namespace UE::Geometry;
	OutPolygon = FPolygon2d();
	OutError.Reset();
	if (ImpactPoints.IsEmpty() || !FMath::IsFinite(Padding) || Padding <= 0)
	{
		OutError = TEXT("Accumulation footprint requires impact points and positive finite padding.");
		return false;
	}
	// Shared boundaries must not retain trace/transform roundoff as separate Boolean vertices.
	auto SnapPoint = [](const FVector2d& Point)
	{
		constexpr double Precision = 1.e-6;
		return FVector2d(FMath::GridSnap(Point.X, Precision), FMath::GridSnap(Point.Y, Precision));
	};
	const FVector2d Origin = SnapPoint(ImpactPoints[0]);
	TArray<FVector2d> LocalPoints;
	LocalPoints.Reserve(ImpactPoints.Num());
	FAxisAlignedBox2d Bounds = FAxisAlignedBox2d::Empty();
	for (const FVector2d& Point : ImpactPoints)
	{
		const FVector2d Local = SnapPoint(Point) - Origin;
		if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y)
			|| !FMath::IsFinite(Local.X) || !FMath::IsFinite(Local.Y))
		{
			OutError = TEXT("Accumulation footprint contains a non-finite coordinate or extent.");
			return false;
		}
		LocalPoints.Add(Local);
		Bounds.Contain(Local);
	}

	FConvexHull2d Hull;
	const bool bHasArea = Hull.Solve(LocalPoints);
	TArray<FVector2d> Boundary;
	if (bHasArea)
	{
		for (int32 Index : Hull.GetPolygonIndices())
		{
			Boundary.Add(LocalPoints[Index]);
		}
	}
	else
	{
		// One point and collinear groups acquire area from the same padded boundary.
		const int32 Axis = Bounds.Width() >= Bounds.Height() ? 0 : 1;
		int32 First = 0;
		int32 Last = 0;
		for (int32 Index = 1; Index < LocalPoints.Num(); ++Index)
		{
			if (LocalPoints[Index][Axis] < LocalPoints[First][Axis])
			{
				First = Index;
			}
			if (LocalPoints[Index][Axis] > LocalPoints[Last][Axis])
			{
				Last = Index;
			}
		}
		Boundary.Add(LocalPoints[First]);
		if (LocalPoints[First] != LocalPoints[Last])
		{
			Boundary.Add(LocalPoints[Last]);
		}
	}

	// Only hull vertices contribute to the rounded expansion; interior hits add no work here.
	constexpr int32 RoundSegments = 16;
	TArray<FVector2d> Expanded;
	Expanded.Reserve(Boundary.Num() * RoundSegments);
	for (const FVector2d& Point : Boundary)
	{
		for (int32 Index = 0; Index < RoundSegments; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / RoundSegments;
			const FVector2d Offset = Padding * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2d ExpandedPoint = Point + Offset;
			if (!FMath::IsFinite(ExpandedPoint.X) || !FMath::IsFinite(ExpandedPoint.Y))
			{
				OutError = TEXT("Accumulation footprint expansion exceeds finite coordinates.");
				return false;
			}
			Expanded.Add(ExpandedPoint);
		}
	}
	FConvexHull2d PaddedHull;
	const bool bExpanded = PaddedHull.Solve(Expanded);
	if (!bExpanded)
	{
		OutError = TEXT("Accumulation footprint expansion could not produce a polygon.");
		return false;
	}
	FPolygon2d Polygon;
	for (int32 Index : PaddedHull.GetPolygonIndices())
	{
		Polygon.AppendVertex(SnapPoint(Expanded[Index] + Origin));
	}
	const bool bValid = BreachContour::Validate(Polygon, OutError);
	if (!bValid)
	{
		return false;
	}
	if (Polygon.IsClockwise())
	{
		Polygon.Reverse();
	}
	OutPolygon = MoveTemp(Polygon);
	return true;
}
