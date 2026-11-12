#pragma once

#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"
#include "Surface/BreachSimpleCollision.h"

class FBreachPreparedBoundary;

/** 한 개구부의 변경 번호에 대응하는 불변 전처리 결과. 작업 스냅샷끼리 공유한다. */
struct FBreachPreparedOpening
{
	TArray<UE::Geometry::FGeneralPolygon2d> Damage;
	TArray<UE::Geometry::FGeneralPolygon2d> Available;
	TArray<UE::Geometry::FPolygon2d> Candidates;
};

struct FBreachPassageBody
{
	UE::Geometry::FGeneralPolygon2d Domain;
	TSharedPtr<const FBreachPreparedBoundary, ESPMode::ThreadSafe> Boundary;
	TArray<UE::Geometry::FPolygon2d> Pieces;
};

struct FBreachPassageState
{
	int32 MaxCount = 0;
	int32 PieceCount = 0;
	TArray<TSharedPtr<const FBreachPassageBody, ESPMode::ThreadSafe>> Bodies;
	TArray<UE::Geometry::FGeneralPolygon2d> Passages;
};

/** UObject에 접근하지 않는 월드 평면 좌표의 통과 영역 계산. */
namespace BreachPassageSolver
{
	bool Prepare(const UE::Geometry::FGeneralPolygon2d& Damage, const UE::Geometry::FPolygon2d& Wall, double Radius, FBreachPreparedOpening& Out, FBreachSimpleBuildStats& Stats, FString& Error);
	bool Solve(TConstArrayView<TSharedPtr<const FBreachPreparedOpening, ESPMode::ThreadSafe>> Openings, const FBreachPassageState& Previous, int32 MaxCount, double Radius, FBreachPassageState& Out, FBreachSimpleBuildStats& Stats, FString& Error);
	UE::Geometry::FGeneralPolygon2d Transform(const UE::Geometry::FGeneralPolygon2d& Polygon, const FVector2d& Scale);
	UE::Geometry::FPolygon2d Transform(const UE::Geometry::FPolygon2d& Polygon, const FVector2d& Scale);
}
