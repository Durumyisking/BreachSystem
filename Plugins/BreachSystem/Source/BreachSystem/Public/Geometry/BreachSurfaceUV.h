#pragma once

#include "CoreMinimal.h"

// 벽의 로컬 XZ 위치를 텍스처 UV로 바꾸는 규칙을 저장하는 구조체
// UV = Offset + X * PerX + Z * PerZ;
struct FPlanarUVMapping
{
	FVector2d Offset; // X=0, Z=0에서의 UV
	FVector2d PerX;   // X가 1 증가할 때 UV 변화량
	FVector2d PerZ;   // Z가 1 증가할 때 UV 변화량
};

namespace BreachSurfaceUV
{
	/** 앞·뒷면에 같은 로컬 원점과 축을 사용한다. 실패하면 기존 매핑을 보존한다. */
	BREACHSYSTEM_API bool MakeWorldSizeMapping(const FVector& WorldScale, const FVector2D& TileSizeCm, FPlanarUVMapping& OutMapping, FString& OutError);
}
