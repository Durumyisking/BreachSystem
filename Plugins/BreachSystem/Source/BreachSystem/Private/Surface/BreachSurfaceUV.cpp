#include "Surface/BreachSurfaceUV.h"

bool BreachSurfaceUV::MakeWorldSizeMapping(const FVector& WorldScale, const FVector2D& TileSizeCm, FPlanarUVMapping& OutMapping, FString& OutError)
{
	if (WorldScale.ContainsNaN() || !FMath::IsFinite(TileSizeCm.X) || !FMath::IsFinite(TileSizeCm.Y) || TileSizeCm.X <= 0 || TileSizeCm.Y <= 0 || FMath::Abs(WorldScale.X) <= UE_DOUBLE_KINDA_SMALL_NUMBER || FMath::Abs(WorldScale.Z) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("World-size UV requires finite nonzero X/Z scale and positive tile dimensions.");
		return false;
	}
	OutMapping.Offset = FVector2d(0.5, 0.5);
	OutMapping.PerX = FVector2d(FMath::Abs(WorldScale.X) / TileSizeCm.X, 0);
	OutMapping.PerZ = FVector2d(0, -FMath::Abs(WorldScale.Z) / TileSizeCm.Y);
	OutError.Reset();
	return true;
}
