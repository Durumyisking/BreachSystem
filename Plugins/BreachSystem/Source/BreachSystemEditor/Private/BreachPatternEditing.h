#pragma once

#include "CoreMinimal.h"
#include "Surface/BreachPattern.h"
#include "Curve/GeneralPolygon2.h"

class UBreachSurfaceMaterial;

struct FBreachPatternPathSelection
{
	int32 Path = INDEX_NONE;
	int32 Hole = INDEX_NONE;
	int32 Vertex = INDEX_NONE;
};
namespace BreachPatternEditing
{
	bool CreatePath(UBreachPattern& Pattern, TConstArrayView<FVector2D> Points, bool bClosed, FString& OutError);
	bool DeletePath(UBreachPattern& Pattern, FBreachPatternPathSelection Selection, FString& OutError);
	bool DeleteVertex(UBreachPattern& Pattern, FBreachPatternPathSelection Selection, FString& OutError);
	bool MovePath(UBreachPattern& Pattern, FBreachPatternPathSelection Selection, FVector2D Delta, FString& OutError);
	bool GeneratePreview(const UBreachPattern& Pattern, const UBreachSurfaceMaterial* Material, FVector2D CenterUV, TArray<UE::Geometry::FGeneralPolygon2d>& OutPreview, FString& OutError);
	void MakeRunningBond(UBreachPattern& Pattern, int32 Columns, int32 Rows);
	void MoveSharedVertex(UBreachPattern& Pattern, FVector2D From, FVector2D To);
	void MoveSharedEdge(UBreachPattern& Pattern, FVector2D FromA, FVector2D FromB, FVector2D Delta);
}
