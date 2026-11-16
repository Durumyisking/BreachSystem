#pragma once
#include "CoreMinimal.h"
#include "BreachPatternEditing.h"

struct FBreachPatternGraphPath
{
	FBreachPatternPathSelection Owner;
	TArray<int32> Nodes;
	bool bClosed = false;
};

/** Editing graph: one vertex per shared UV junction; path order is preserved, edges are unique. */
struct FBreachPatternEditGraph
{
	TArray<FVector2D> Vertices;
	TArray<FIntPoint> Edges;
	TArray<FBreachPatternGraphPath> Paths;
	TArray<FBreachPatternRegion> SourceRegions;
	TArray<FBreachPatternGuide> SourceGuides;
	EBreachPatternMode Mode = EBreachPatternMode::None;
	bool Build(const UBreachPattern& Pattern, FString& OutError);
	bool ApplyMove(TConstArrayView<int32> SelectedNodes, FVector2D Delta, UBreachPattern& OutPattern, TArray<FVector2D>& OutVertices, FString& OutError) const;
	bool HasSameConnections(const FBreachPatternEditGraph& Other) const;
	int32 FindVertex(FVector2D Point) const;
	FBreachPatternPathSelection OwnerOf(int32 Node, int32 Edge) const;
};
