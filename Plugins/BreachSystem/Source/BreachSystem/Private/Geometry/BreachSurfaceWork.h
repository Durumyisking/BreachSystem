#pragma once

#include "CoreMinimal.h"
#include "Components/BreachDecorationComponent.h"
#include "Engine/HitResult.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/BreachCellGrid.h"
#include "Geometry/BreachDamageAccumulator.h"
#include "Data/BreachDecorationOptions.h"
#include "Geometry/BreachSimpleCollision.h"
#include "Geometry/BreachSurfaceTypes.h"
#include "Geometry/BreachSurfaceUV.h"

class UBreachSurfaceMaterial;


/** One hit as the game thread saw it. The work reads nothing else from the surface's components or the damage causer. */
struct FBreachHitInput
{
	FHitResult HitInfo;
	/** Impact in surface local XZ. */
	FVector2d LocalPoint = FVector2d::ZeroVector;
	EBreachSurfaceSide Side = EBreachSurfaceSide::Front;
	FVector ComponentScale = FVector::OneVector;
	/** Accumulation rules at the time of the hit. The surface keeps the asset referenced until the work is applied. */
	const UBreachSurfaceMaterial* Material = nullptr;
	double BreachDamage = 0;
	bool bAccumulate = false;
	/** Bullet cutter with its contour, and the plain cutter retried when the contour cannot be cut. */
	bool bHasCutter = false;
	UE::Geometry::FGeneralPolygon2d Cutter;
	bool bHasBaseCutter = false;
	UE::Geometry::FGeneralPolygon2d BaseCutter;
};

/** What one hit changed. The game thread applies results in hit order. */
struct FBreachHitResult
{
	/** The hit changed the shown surface, so the mesh, collision and Deco must follow. */
	bool bChanged = false;
	/** Even the bullet hole could not be cut; the surface keeps its previous shape. */
	bool bFailed = false;
	double RemovedArea = 0;
	TArray<UE::Geometry::FAxisAlignedBox2d> DecorationCuts;
	TArray<UE::Geometry::FPolygon2d> DecorationRemoved;
};

/**
 * Breach geometry of one surface, kept apart from its components so it can run off the game thread.
 * The surface hands its state over, runs the work, and takes the state back; nothing else changes that state.
 */
class FBreachSurfaceWork
{
public:
	FString SurfaceName;

	// Surface state the cuts read and change.
	FBreachSurfaceModel Model;

	// Fixed for the lifetime of the surface.
	FBreachSurfaceLayout Layout;

	/** The surface already shows its proxy. A cut that removes no area then changes nothing. */
	bool bPublished = false;
	/** The proxy was created for these hits. A hit that cannot cut then resets the surface, as if it had never been hit. */
	bool bCreatedProxy = false;

	/** Deco records and display meshes. Updated after every hit that changed the surface, like a separate update per hit. */
	FBreachDecorationWork Decoration;
	/** The surface has a Deco component, or a hit's material asked for one. */
	bool bDecorationExists = false;

	/** Mesh of the latest cut, without normals or UVs until FinishMesh. */
	UE::Geometry::FDynamicMesh3 Mesh;
	TArray<FBreachHitResult> Results;

	/** Processes hits in order as separate breaches would, then finishes the mesh once. */
	void ProcessHits(TConstArrayView<FBreachHitInput> Hits);

	/** Accumulates the hit, or cuts its bullet hole when accumulation does not breach. Appends one result. */
	void ProcessHit(const FBreachHitInput& Hit);

	/**
	 * Subtracts the cutters and drops pieces no longer attached to the original border. On success the remaining regions,
	 * the hole cache and Mesh hold the result. A cut that removes no area from a published surface succeeds with
	 * bOutChanged false and touches nothing; an unpublished surface always reports a change so it switches to the proxy.
	 */
	bool CutRegions(TConstArrayView<UE::Geometry::FGeneralPolygon2d> Cutters, bool& bOutChanged, FBreachHitResult& Result);

	/** Builds normals and UVs on Mesh once, after every cut of the work. */
	void FinishMesh();

	static void BuildSurfaceAttributes(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FAxisAlignedBox2d& Bounds, const FPlanarUVMapping (&UVs)[2]);

private:
	bool PrepareAccumulation(const FBreachHitInput& Hit, FVector2d& OutGridHitPoint);
	bool AccumulateHit(const FBreachHitInput& Hit, const FVector2d& GridHitPoint, bool& bOutChanged, FBreachHitResult& Result);
	bool ApplyDirectCut(const FBreachHitInput& Hit, UE::Geometry::FGeneralPolygon2d& OutCutter, bool& bOutChanged, FBreachHitResult& Result);
	/** Removes accumulated impacts inside a polygon given in surface local XZ. */
	void RemoveHitsInside(const UE::Geometry::FPolygon2d& LocalPolygon);
	/** True when an edge with length lies on one side of the original rectangle, within UE_DOUBLE_KINDA_SMALL_NUMBER. A touching point is not enough. */
	static bool TouchesOriginalBorder(const UE::Geometry::FPolygon2d& Outer, const UE::Geometry::FAxisAlignedBox2d& Bounds);
};
