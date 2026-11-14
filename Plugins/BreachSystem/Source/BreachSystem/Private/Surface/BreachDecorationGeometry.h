#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Surface/BreachDecorationOptions.h"

namespace BreachDecoration
{
	struct FPatch
	{
		EBreachSurfaceSide Side = EBreachSurfaceSide::Front;
		UE::Geometry::FAxisAlignedBox2d UVBounds;
		TArray<UE::Geometry::FGeneralPolygon2d> Regions;
	};

	struct FBinding
	{
		// 실제 부착 경로를 보관해 재삼각분할·변 분할에도 같은 조각을 유지한다.
		TArray<FVector2d> Support;
		double Length = 0;
		int32 Seed = 0;
	};

	struct FFeatureEdge
	{
		FVector2d A;
		FVector2d B;
	};

	bool ClipPatches(TArray<FPatch>& Patches, TConstArrayView<UE::Geometry::FGeneralPolygon2d> Remaining, TConstArrayView<UE::Geometry::FAxisAlignedBox2d> Cuts, EBreachSurfaceSide Side, const FVector& Scale, double Padding, FString& Error);
	bool BuildPatchMesh(TConstArrayView<FPatch> Patches, EBreachSurfaceSide Side, double Depth, UE::Geometry::FDynamicMesh3& OutMesh, FString& Error);
	bool UpdateBindings(TArray<FBinding>& Bindings, TArray<FFeatureEdge>& PreviousEdges, const UE::Geometry::FPolygon2d& Original, TConstArrayView<UE::Geometry::FGeneralPolygon2d> Remaining, const FVector& Scale, const FBreachFeatureDecorationOptions& Options, FString& Error);
	bool BuildFeatureMesh(TConstArrayView<FBinding> Bindings, const FBreachFeatureDecorationOptions& Options, const FVector& Scale, const FVector2d& Depth, UE::Geometry::FDynamicMesh3& OutMesh, FString& Error);
}
