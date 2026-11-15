#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Surface/BreachDecorationOptions.h"

class FBreachSimpleCollision;

/**
 * Deco 계산은 이번 브리칭으로 바뀐 부분만 다룬다.
 * 바뀌지 않은 패치·구멍·조각은 도형 연산과 삼각분할을 반복하지 않고, 표시 메시에서도 해당 정점만 교체한다.
 */
namespace BreachDecoration
{
	struct FPatch
	{
		EBreachSurfaceSide Side = EBreachSurfaceSide::Front;
		UE::Geometry::FAxisAlignedBox2d UVBounds;
		TArray<UE::Geometry::FGeneralPolygon2d> Regions;
		// 표시 메시에 넣은 정점. 영역이 바뀌면 이 정점만 지우고 다시 넣는다.
		TArray<int32> MeshVertices;
		bool bInMesh = false;
	};

	struct FCutState
	{
		TArray<FPatch> Patches;
		// 영역이 바뀌었거나 사라진 패치가 표시 메시에 남긴 정점. Front, Back 순서다.
		TArray<int32> StaleVertices[2];
	};

	struct FBinding
	{
		// 실제 부착 경로를 보관해 재삼각분할·변 분할에도 같은 조각을 유지한다.
		TArray<FVector2d> Support;
		double Length = 0;
		int32 Seed = 0;
		// 표시 메시에 넣은 정점. 조각이 사라지면 이 정점만 지운다.
		TArray<int32> MeshVertices;
		bool bInMesh = false;
	};

	struct FFeatureEdge
	{
		FVector2d A;
		FVector2d B;
	};

	/** 구멍 캐시의 변경 번호 하나에 대응하는 Feature 기록. */
	struct FFeatureHole
	{
		// 원본 벽 안쪽으로 자른 구멍의 경계.
		UE::Geometry::FAxisAlignedBox2d Bounds = UE::Geometry::FAxisAlignedBox2d::Empty();
		// 구멍 전체가 면적 기준을 넘어 둘레가 모두 Edges에 들어 있다.
		bool bDecorated = false;
		// 조각을 붙일 수 있는 구멍 경계. 원본 외곽선 위의 변은 제외한다.
		TArray<FFeatureEdge> Edges;
		TArray<FBinding> Bindings;
	};

	struct FFeatureState
	{
		// 변경 번호가 그대로인 구멍은 다시 계산하지 않는다.
		TMap<uint64, FFeatureHole> Holes;
		// 부착 변을 잃은 조각이 표시 메시에 남긴 정점.
		TArray<int32> StaleVertices;
		// 한 번이라도 현재 구멍 전체를 반영했다. false면 모든 구멍의 둘레를 새 경계로 취급한다.
		bool bSeeded = false;
	};

	/**
	 * Removed와 경계가 겹치는 기존 패치만 다시 자르고, Cuts마다 새 패치를 만든다.
	 * Removed는 이번 브리칭으로 사라진 도형(커터와 고립 조각의 외곽)이고, Holes는 그 절단까지 반영한 구멍 캐시다.
	 */
	bool ClipPatches(FCutState& State, const FBreachSimpleCollision& Holes, TConstArrayView<UE::Geometry::FPolygon2d> Removed, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> Cuts, EBreachSurfaceSide Side, const FVector& Scale, double Padding, FString& Error);

	/** 한 면의 표시 메시에서 바뀐 패치의 정점만 교체한다. 메시를 건드렸으면 bOutChanged가 true다. */
	bool UpdatePatchMesh(FCutState& State, EBreachSurfaceSide Side, double Depth, UE::Geometry::FDynamicMesh3& Mesh, bool& bOutChanged, FString& Error);

	/**
	 * 변경 번호가 새로 생긴 구멍만 경계를 다시 읽고 조각을 배치한다.
	 * 이전 둘레가 모두 기록된 구멍은 Removed 근처만 검사한다.
	 */
	bool UpdateBindings(FFeatureState& State, const FBreachSimpleCollision& Holes, TConstArrayView<UE::Geometry::FPolygon2d> Removed, const FVector& Scale, const FBreachFeatureDecorationOptions& Options, FString& Error);

	/** 표시 메시에서 사라진 조각을 지우고 새 조각만 추가한다. 메시를 건드렸으면 bOutChanged가 true다. */
	bool UpdateFeatureMesh(FFeatureState& State, const FBreachFeatureDecorationOptions& Options, const FVector& Scale, const FVector2d& Depth, UE::Geometry::FDynamicMesh3& Mesh, bool& bOutChanged, FString& Error);

	/** 부분 갱신에 필요한 UV·노멀 채널을 준비한다. 이미 있으면 그대로 둔다. */
	void PrepareDisplayMesh(UE::Geometry::FDynamicMesh3& Mesh);

	/** 추가한 삼각형 하나에만 면 노멀을 기록한다. 메시 전체 노멀을 다시 만들지 않는다. */
	void SetFlatNormal(UE::Geometry::FDynamicMesh3& Mesh, int32 TriangleID);
}
