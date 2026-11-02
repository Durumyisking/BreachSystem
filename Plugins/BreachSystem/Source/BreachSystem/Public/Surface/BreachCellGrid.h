#pragma once

#include "CoreMinimal.h"

/** UV0 원점과 U 방향에 고정된 cm 정사각 격자. 셀 배열을 할당하지 않는다. */
class BREACHSYSTEM_API FBreachCellGrid
{
public:
	bool Configure(const FVector2d& UVOffset, const FVector2d& UVPerX, const FVector2d& UVPerZ,
		const FVector2d& PhysicalScale, double InCellSize, FString& OutError);
	FVector2d ToMetric(const FVector2d& LocalXZ) const;
	FVector2d ToLocal(const FVector2d& MetricUV) const;
	bool Quantize(const FVector2d& LocalXZ, FIntPoint& OutCell, FVector2d& OutMetric, FString& OutError) const;

private:
	// 격자의 기준점
	FVector2d Origin = FVector2d::ZeroVector;
	// 벽의 확대 배율
	FVector2d Scale = FVector2d::ZeroVector;
	
	// 격자의 UV 방향
	FVector2d AxisU = FVector2d::ZeroVector;
	FVector2d AxisV = FVector2d::ZeroVector;

	double CellSize = 0;

public:
	bool IsValid() const { return CellSize > 0; }
	double GetCellSize() const { return CellSize; }
};
