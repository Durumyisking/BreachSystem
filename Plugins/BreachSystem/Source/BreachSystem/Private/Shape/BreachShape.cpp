#include "Shape/BreachShape.h"

namespace
{
	using namespace UE::Geometry;

	bool MakeCircle(double Radius, int32 Segments, FPolygon2d& OutPolygon)
	{
		OutPolygon = FPolygon2d();
		if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER)
		{
			return false;
		}
		OutPolygon = FPolygon2d::MakeCircle(Radius, FMath::Clamp(Segments, 3, 512));
		if (OutPolygon.IsClockwise())
		{
			OutPolygon.Reverse();
		}
		return true;
	}
}

bool UBreachCircleShape::GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const
{
	const auto* Shape = Option.ShapeOption.GetPtr<FBreachCircleShapeOption>();
	OutPolygon = UE::Geometry::FPolygon2d();
	if (!Shape)
	{
		return false;
	}
	return MakeCircle(Shape->Radius, Shape->Segments, OutPolygon);
}

bool UBreachBoxShape::GeneratePolygon(const FBreachSourceOption& Option, UE::Geometry::FPolygon2d& OutPolygon) const
{
	using namespace UE::Geometry;
	OutPolygon = FPolygon2d();
	const auto* Shape = Option.ShapeOption.GetPtr<FBreachBoxShapeOption>();
	if (!Shape)
	{
		return false;
	}
	if (!FMath::IsFinite(Shape->Size.X) || !FMath::IsFinite(Shape->Size.Y)
		|| Shape->Size.X <= UE_SMALL_NUMBER || Shape->Size.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Shape->CornerRadius))
	{
		return false;
	}
	const FVector2d Half = FVector2d(Shape->Size) * 0.5;
	const double Radius = FMath::Clamp(double(Shape->CornerRadius), 0.0, FMath::Min(Half.X, Half.Y));
	TArray<FVector2d> Boundary;
	if (Radius <= UE_SMALL_NUMBER)
	{
		Boundary = { FVector2d(-Half.X, -Half.Y), FVector2d(Half.X, -Half.Y),
			FVector2d(Half.X, Half.Y), FVector2d(-Half.X, Half.Y) };
	}
	else
	{
		const FVector2d Centers[] = { {Half.X - Radius, -Half.Y + Radius}, {Half.X - Radius, Half.Y - Radius},
			{-Half.X + Radius, Half.Y - Radius}, {-Half.X + Radius, -Half.Y + Radius} };
		const int32 Segments = FMath::Clamp(Shape->CornerSegments, 1, 128);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			for (int32 Step = 0; Step <= Segments; ++Step)
			{
				const double Angle = (Corner - 1 + double(Step) / Segments) * UE_DOUBLE_PI * 0.5;
				const FVector2d Point = Centers[Corner] + Radius * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle));
				if (!Boundary.IsEmpty())
				{
					if (Boundary.Last().Equals(Point, UE_SMALL_NUMBER))
					{
						continue;
					}
				}
				Boundary.Add(Point);
			}
		}
		if (Boundary.Num() > 1)
		{
			if (Boundary[0].Equals(Boundary.Last(), UE_SMALL_NUMBER))
			{
				Boundary.Pop();
			}
		}
	}
	for (const FVector2d& Point : Boundary)
	{
		OutPolygon.AppendVertex(Point);
	}
	return true;
}
