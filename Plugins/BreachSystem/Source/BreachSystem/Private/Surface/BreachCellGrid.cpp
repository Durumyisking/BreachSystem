#include "Surface/BreachCellGrid.h"

// UVPerX, UVPerZ는 로컬 XZ로 1cm만큼 움직일때 변하는 양
// 메시의 UV가 Box Oriented가 아닐 수 있기 때문에 Vector2d를 갖는다.
bool FBreachCellGrid::Configure(const FVector2d& UVOffset, const FVector2d& UVPerX, const FVector2d& UVPerZ,
	const FVector2d& PhysicalScale, double InCellSize, FString& OutError)
{
	*this = FBreachCellGrid();
	OutError.Reset();
	auto Finite = [](const FVector2d& V) 
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y); 
	};

	const FVector2d AbsScale(FMath::Abs(PhysicalScale.X), FMath::Abs(PhysicalScale.Y));

	if (!Finite(UVOffset) || !Finite(UVPerX) || !Finite(UVPerZ) || !Finite(AbsScale)
		|| AbsScale.X <= UE_DOUBLE_SMALL_NUMBER || AbsScale.Y <= UE_DOUBLE_SMALL_NUMBER
		|| !FMath::IsFinite(InCellSize) || InCellSize < 0.1)
	{
		OutError = TEXT("Cell grid requires finite UVs, non-zero scale and cell size >= 0.1 cm.");
		return false;
	}

	// Determinant : UV를 XZ로 되돌리는 역변환에 필요한 숫자 (행렬식 이라는 뜻)
	// X로 움직인 결과와 Z로 움직인 결과를 UV에서 구별할 수 있는가?”를 확인
	const double Determinant = UVPerX.X * UVPerZ.Y - UVPerZ.X * UVPerX.Y;
	if (!FMath::IsFinite(Determinant) || Determinant == 0)
	{
		OutError = TEXT("Cell grid requires an invertible planar UV mapping.");
		return false;
	}

	// UVPerX, UVPerZ가 ‘로컬 이동 → UV 변화’라면, 
	// InverseUV는 UV가 1움직일때 XZ가 변하는 양 InversePerU/V라고 보면 된다.
	const FVector2d InverseU(UVPerZ.Y / Determinant, -UVPerX.Y / Determinant);
	const FVector2d InverseV(-UVPerZ.X / Determinant, UVPerX.X / Determinant);

	// UV가 0,0이 되는 XZ좌표
	const FVector2d NewOrigin = -InverseU * UVOffset.X - InverseV * UVOffset.Y;

	// PhysicalU, PhysicalV: 스케일을 반영한 InversePerU/V
	const FVector2d PhysicalU = InverseU * AbsScale;
	const FVector2d PhysicalV = InverseV * AbsScale;
	const double ULength = PhysicalU.Length();
	if (!Finite(NewOrigin) || !Finite(PhysicalV) || !FMath::IsFinite(ULength) || ULength <= 0)
	{
		OutError = TEXT("Cell grid UV basis exceeds finite physical coordinates.");
		return false;
	}
	const FVector2d NewU = PhysicalU / ULength;
	FVector2d NewV(-NewU.Y, NewU.X); // U를 회전시켜 U에 직각인 V를 만든다. 원본 UV가 사각형이 아니어도, 그리드는  사각형으로 만들기 위함이다.

	// V축을 U축과 직각으로 만들기위해, 비스듬한 원본V를 Y축으로 붙이는 작업을 한다.
	const double VProjection = NewV.Dot(PhysicalV);
	if (!FMath::IsFinite(VProjection) || VProjection == 0)
	{
		OutError = TEXT("Cell grid UV axes collapse in physical space.");
		return false;
	}
	// 음수면 뒤집어준다.
	if (VProjection < 0)
	{
		NewV = -NewV;
	}
	Origin = NewOrigin;
	Scale = AbsScale;
	AxisU = NewU;
	AxisV = NewV;
	CellSize = InCellSize;
	return true;
}

FVector2d FBreachCellGrid::ToMetric(const FVector2d& LocalXZ) const
{
	check(IsValid());
	const FVector2d Physical = (LocalXZ - Origin) * Scale;
	return FVector2d(Physical.Dot(AxisU), Physical.Dot(AxisV));
}

FVector2d FBreachCellGrid::ToLocal(const FVector2d& MetricUV) const
{
	check(IsValid());
	return Origin + (AxisU * MetricUV.X + AxisV * MetricUV.Y) / Scale;
}

bool FBreachCellGrid::Quantize(const FVector2d& LocalXZ, FIntPoint& OutCell, FVector2d& OutMetric, FString& OutError) const
{
	OutError.Reset();
	OutCell = FIntPoint::ZeroValue;
	OutMetric = FVector2d::ZeroVector;
	if (!IsValid())
	{
		OutError = TEXT("Configure the cell grid before recording impacts.");
		return false;
	}
	FVector2d Indices = ToMetric(LocalXZ) / CellSize;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		if (!FMath::IsFinite(Indices[Axis]) || Indices[Axis] < MIN_int32 || Indices[Axis] >= MAX_int32)
		{
			OutError = TEXT("Impact exceeds finite integer cell coordinates.");
			return false;
		}
		// Stabilize only transform roundoff at a boundary, not a visible sub-cell displacement.
		const double Nearest = FMath::RoundToDouble(Indices[Axis]);
		if (FMath::Abs(Indices[Axis] - Nearest) <= 1.e-9)
		{
			Indices[Axis] = Nearest;
		}
		OutCell[Axis] = FMath::FloorToInt(Indices[Axis]);
		OutMetric[Axis] = (static_cast<double>(OutCell[Axis]) + 0.5) * CellSize;
		if (!FMath::IsFinite(OutMetric[Axis]))
		{
			OutError = TEXT("Cell center exceeds finite physical coordinates.");
			return false;
		}
	}
	return true;
}
