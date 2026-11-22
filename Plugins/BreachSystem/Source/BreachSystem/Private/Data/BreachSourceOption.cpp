#include "Data/BreachSourceOption.h"

namespace
{
	using namespace UE::Geometry;

	bool MakeCircle(double Radius, int32 Segments, FPolygon2d& OutPolygon)
	{
		OutPolygon = FPolygon2d();
		if (!FMath::IsFinite(Radius) || Radius <= UE_DOUBLE_KINDA_SMALL_NUMBER)
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

bool FBreachShapeOption::GeneratePolygon(UE::Geometry::FPolygon2d& OutPolygon) const
{
	// 에디터에서 기본 구조체 자체를 고를 수 있으므로, 형상 종류가 없는 옵션은 거부한다.
	OutPolygon = UE::Geometry::FPolygon2d();
	return false;
}

bool FBreachCircleShapeOption::GeneratePolygon(UE::Geometry::FPolygon2d& OutPolygon) const
{
	return MakeCircle(Radius, Segments, OutPolygon);
}

bool FBreachBoxShapeOption::GeneratePolygon(UE::Geometry::FPolygon2d& OutPolygon) const
{
	using namespace UE::Geometry;
	OutPolygon = FPolygon2d();
	if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y)
		|| Size.X <= UE_DOUBLE_KINDA_SMALL_NUMBER || Size.Y <= UE_DOUBLE_KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(CornerRadius))
	{
		return false;
	}
	const FVector2d Half = FVector2d(Size) * 0.5;
	const double Radius = FMath::Clamp(double(CornerRadius), 0.0, FMath::Min(Half.X, Half.Y));
	TArray<FVector2d> Boundary;
	if (Radius <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		Boundary = { FVector2d(-Half.X, -Half.Y), FVector2d(Half.X, -Half.Y),
			FVector2d(Half.X, Half.Y), FVector2d(-Half.X, Half.Y) };
	}
	else
	{
		const FVector2d Centers[] = { {Half.X - Radius, -Half.Y + Radius}, {Half.X - Radius, Half.Y - Radius},
			{-Half.X + Radius, Half.Y - Radius}, {-Half.X + Radius, -Half.Y + Radius} };
		const int32 Segments = FMath::Clamp(CornerSegments, 1, 128);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			for (int32 Step = 0; Step <= Segments; ++Step)
			{
				const double Angle = (Corner - 1 + double(Step) / Segments) * UE_DOUBLE_PI * 0.5;
				const FVector2d Point = Centers[Corner] + Radius * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle));
				if (!Boundary.IsEmpty())
				{
					if (Boundary.Last().Equals(Point, UE_DOUBLE_KINDA_SMALL_NUMBER))
					{
						continue;
					}
				}
				Boundary.Add(Point);
			}
		}
		if (Boundary.Num() > 1)
		{
			if (Boundary[0].Equals(Boundary.Last(), UE_DOUBLE_KINDA_SMALL_NUMBER))
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
