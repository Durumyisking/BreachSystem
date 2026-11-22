#pragma once

#include "CoreMinimal.h"
#include "Geometry/BreachSurfaceUV.h"

class UStaticMesh;
namespace UE::Geometry { class FDynamicMesh3; }

/** 원본 StaticMesh에서 파괴 표면을 읽는다. 표면 초기화와 Editor 표시 조회가 같은 LOD0/UV 검증을 쓴다. */
namespace BreachSurfaceSource
{
	/**
	 * LOD0을 Mesh로 복사하고 앞·뒷면의 선형 UV 변환을 구한다. 원본 에셋은 수정하지 않는다.
	 * 원본이 없으면 그대로 false다. 렌더 데이터·CPU 접근·치수·UV 문제는 원본 이름과 함께 경고를 남기고 false다.
	 */
	bool ReadSurface(const UStaticMesh* SourceStaticMesh, UE::Geometry::FDynamicMesh3& Mesh, FPlanarUVMapping (&OutUVs)[2]);
}
