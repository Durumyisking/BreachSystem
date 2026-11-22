#include "Surface/BreachSurfaceWork.h"
#include "Surface/BreachAccumulationShape.h"
#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"
#include "Surface/BreachSurfaceGeometry.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Shape/BreachContour.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

using namespace UE::Geometry;

void FBreachSurfaceWork::ProcessHits(TConstArrayView<FBreachHitInput> Hits)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Breach work");
	bool bAnyChanged = false;
	for (const FBreachHitInput& Hit : Hits)
	{
		ProcessHit(Hit);
		const FBreachHitResult& Result = Results.Last();

		// 처음 만든 Proxy에서 자르지 못한 피격은 표면을 되돌린다. 다음 피격은 새 표면에서 시작하므로 그 기록도 남지 않는다.
		if (Result.bFailed && bCreatedProxy && !bPublished)
		{
			DamageAccumulator.Reset();
		}
		if (!Result.bChanged)
		{
			continue;
		}
		bAnyChanged = true;

		// Deco는 피격마다 그 절단까지 반영한 구멍 캐시로 갱신한다. 설정된 재질이 처음 나오면 Deco를 만든다.
		if (Result.DecorationCuts.IsEmpty())
		{
			continue;
		}
		if (IsValid(Hit.Material))
		{
			if (IsValid(Hit.Material->CutDecoration.Material) || IsValid(Hit.Material->FeatureDecoration.Material))
			{
				bDecorationExists = true;
			}
		}
		if (bDecorationExists)
		{
			Decoration.Update(Hit.Material, SimpleCollision, OriginalDepthRange, Result.DecorationCuts, Result.DecorationRemoved, Hit.Side, Hit.ComponentScale);
		}
	}
	if (bAnyChanged)
	{
		FinishMesh();
	}
}

void FBreachSurfaceWork::ProcessHit(const FBreachHitInput& Hit)
{
	FBreachHitResult& Result = Results.AddDefaulted_GetRef();

	// 누적 파손처리
	bool bPrepared = false;
	bool bBreached = false;
	bool bChanged = false;
	if (Hit.bAccumulate)
	{
		FVector2d GridHitPoint;
		bPrepared = PrepareAccumulation(Hit, GridHitPoint);
		if (bPrepared)
		{
			bBreached = AccumulateHit(Hit, GridHitPoint, bChanged, Result);
		}
	}

	// 누적파손이 없으면 Source의 컨투어 옵션대로 브리치가 발생한다.
	if (!bBreached)
	{
		FGeneralPolygon2d Cutter;
		const bool bCut = ApplyDirectCut(Hit, Cutter, bChanged, Result);
		if (!bCut)
		{
			Result.bFailed = true;
			return;
		}

		// 유효한 누적 기록은 총알 구멍을 만든 뒤에도 유지한다.
		if (!bPrepared)
		{
			RemoveHitsInside(Cutter.GetOuter());
		}
	}

	Result.bChanged = bChanged;
	// 표면을 바꾼 피격은 반영 때 Proxy를 표시로 전환한다. 이어지는 피격은 이미 표시 중인 표면을 자른다.
	if (bChanged)
	{
		bPublished = true;
	}
}

bool FBreachSurfaceWork::PrepareAccumulation(const FBreachHitInput& Hit, FVector2d& OutGridHitPoint)
{
	// 검증
	OutGridHitPoint = FVector2d::ZeroVector;
	if (!FMath::IsFinite(Hit.BreachDamage) || Hit.BreachDamage <= 0)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s rejected non-positive/non-finite accumulation BreachDamage."), *SurfaceName);
		return false;
	}
	if (!ensureMsgf(IsValid(Hit.Material), TEXT("%s requires a surface material to prepare accumulation."), *SurfaceName))
	{
		return false;
	}
	const FBreachAccumulationOptions& Options = Hit.Material->Accumulation;

	// 검증: 누적 규칙을 적용한다. 설정 실패 시 이전 누적 상태를 유지한다.
	FString Error;
	const bool bConfigured = DamageAccumulator.Configure(Options.WindowSize, Options.BreachHP, Error);
	if (!bConfigured)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous accumulation state: %s"), *SurfaceName, *Error);
		return false;
	}

	// 처리: 초기 Hit/메시 스케일의 변경시 아래 로직을 처리한다.
	const FVector2d MetricScale(FMath::Abs(Hit.ComponentScale.X), FMath::Abs(Hit.ComponentScale.Z)); // Scale을 안전한 XZ 추출 (언리얼이니까 XZ다)
	if (CachedMeshScaleXZ != MetricScale)
	{
		if (DamageAccumulator.Num() > 0)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s cleared accumulated impacts after its surface scale changed."), *SurfaceName);
		}

		DamageAccumulator.Reset();
		CachedMeshScaleXZ = MetricScale;

		// ★원본 UV를 Grid화 한다.★
		CellGrid.Configure(SurfaceUVs[0].Offset, SurfaceUVs[0].PerX, SurfaceUVs[0].PerZ,
			CachedMeshScaleXZ, CapturedCellSize, Error);
	}

	// Region에 Hit 했는지 확인한다.
	bool bHitSurface = false;
	for (const FGeneralPolygon2d& Region : SurfaceState.RemainingRegions)
	{
		if (Region.Contains(Hit.LocalPoint))
		{
			bHitSurface = true;
			break;
		}
	}
	if (!bHitSurface)
	{
		return false;
	}

	// 피격점이 어느 셀(Cell)에 속하는지 찾고, 그 셀의 중심 좌표(OutGridHitPoint)를 반환한다.
	FIntPoint Cell;
	const bool bQuantized = CellGrid.Quantize(Hit.LocalPoint, Cell, OutGridHitPoint, Error);
	if (!bQuantized)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s skipped accumulation and kept direct cutting: %s"), *SurfaceName, *Error);
		return false;
	}

	return true;
}

bool FBreachSurfaceWork::ApplyDirectCut(const FBreachHitInput& Hit, FGeneralPolygon2d& OutCutter, bool& bOutChanged, FBreachHitResult& Result)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Direct cut");
	bOutChanged = false;

	// 피격 순간 BreachSource가 만든 커터를 쓴다.
	if (!Hit.bHasCutter)
	{
		return false;
	}
	OutCutter = Hit.Cutter;

	// PolygonCut!
	const TArray<FGeneralPolygon2d> Cutters{FGeneralPolygon2d(OutCutter.GetOuter())};
	const bool bCut = CutRegions(Cutters, bOutChanged, Result);
	if (bCut)
	{
		return true;
	}
	// 기본로직은 여기서 종료
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 컨투어 적용한 Cut에 실패하면 Base로 우회한다.
	if (!Hit.bHasBaseCutter)
	{
		return false;
	}
	if (OutCutter.GetOuter().GetVertices() == Hit.BaseCutter.GetOuter().GetVertices())
	{
		return false;
	}
	UE_LOG(LogBreachableSurface, Warning, TEXT("%s is retrying the breach with its base cutter."), *SurfaceName);
	const TArray<FGeneralPolygon2d> BaseCutters{FGeneralPolygon2d(Hit.BaseCutter.GetOuter())};
	const bool bBaseCut = CutRegions(BaseCutters, bOutChanged, Result);
	if (!bBaseCut)
	{
		UE_LOG(LogBreachableSurface, Error, TEXT("%s could not apply even the base cutter."), *SurfaceName);
		return false;
	}

	OutCutter = Hit.BaseCutter;
	return true;
}

bool FBreachSurfaceWork::AccumulateHit(const FBreachHitInput& Hit, const FVector2d& GridHitPoint, bool& bOutChanged, FBreachHitResult& Result)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Accumulated cut");
	bOutChanged = false;
	const UBreachSurfaceMaterial* Material = Hit.Material;
	if (!ensureMsgf(IsValid(Material), TEXT("%s requires a surface material for accumulation."), *SurfaceName))
	{
		return false;
	}
	const FBreachAccumulationShapeOptions& ShapeOptions = Material->AccumulationShape;

	// 기록: 유효한 탄착을 추가한 뒤 임계값을 만족하는 후보를 한 번에 조회한다.
	FString Error;
	const int32 RecordID = DamageAccumulator.AddHit(GridHitPoint, Hit.BreachDamage, Error);
	if (RecordID == INDEX_NONE)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s could not record accumulation impact: %s"), *SurfaceName, *Error);
		return false;
	}

	TArray<FBreachAccumulationCandidate> Candidates;
	DamageAccumulator.FindCandidates(RecordID, Candidates);

	bool bBreached = false;
	// 모든 후보는 공유 탄착을 포함한 절단 전 기록을 기준으로 처리한다.
	for (const FBreachAccumulationCandidate& Candidate : Candidates)
	{
		// 생성: 탄착군과 여백으로 기본 윤곽을 만들고 선택적으로 변형한다.
		FPolygon2d BaseMetric;
		const bool bGeneratedBase = BreachAccumulationShape::GenerateBase(Candidate.ImpactPoints, ShapeOptions.Padding, BaseMetric, Error);
		if (!bGeneratedBase)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage after its accumulation footprint failed: %s"), *SurfaceName, *Error);
			continue;
		}

		const UBreachPattern* Pattern = Material->Pattern.Get();
		if (IsValid(Pattern) && Pattern->Mode != EBreachPatternMode::None)
		{
			FPlanarUVMapping UV = SurfaceUVs[Hit.Side == EBreachSurfaceSide::Back ? 1 : 0];
			if (Material->bUseWorldSizeUV)
			{
				if (!BreachSurfaceUV::MakeWorldSizeMapping(Hit.ComponentScale, Material->TileSizeCm, UV, Error))
				{
					UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage after invalid world-size pattern mapping: %s"), *SurfaceName, *Error);
					continue;
				}
			}
			auto UVAtMetric = [this, &UV](const FVector2d& Metric)
			{
				const FVector2d Local = CellGrid.ToLocal(Metric);
				return UV.Offset + UV.PerX * Local.X + UV.PerZ * Local.Y;
			};
			FBreachPatternMapping Mapping;
			Mapping.UVOffset = UVAtMetric(FVector2d::ZeroVector);
			Mapping.UVPerX = UVAtMetric(FVector2d(1, 0)) - Mapping.UVOffset;
			Mapping.UVPerY = UVAtMetric(FVector2d(0, 1)) - Mapping.UVOffset;

			TArray<FGeneralPolygon2d> MetricRegions;
			if (!BreachPatternGeometry::Generate(*Pattern, BaseMetric, Mapping, ShapeOptions.Contour, Material->GrainInfluence, MetricRegions, Error))
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage after its configured pattern failed: %s"), *SurfaceName, *Error);
				continue;
			}
			if (MetricRegions.IsEmpty())
			{
				continue;
			}

			TArray<FGeneralPolygon2d> LocalRegions;
			for (const FGeneralPolygon2d& Region : MetricRegions)
			{
				auto ToLocalLoop = [this](const FPolygon2d& Loop)
				{
					FPolygon2d Local;
					for (const FVector2d& Point : Loop.GetVertices())
					{
						Local.AppendVertex(CellGrid.ToLocal(Point));
					}
					return Local;
				};
				FGeneralPolygon2d Local(ToLocalLoop(Region.GetOuter()));
				for (const FPolygon2d& Hole : Region.GetHoles())
				{
					Local.AddHole(ToLocalLoop(Hole), false, false);
				}
				LocalRegions.Add(MoveTemp(Local));
			}

			bool bCutChanged = false;
			if (CutRegions(LocalRegions, bCutChanged, Result))
			{
				bBreached = true;
				bOutChanged |= bCutChanged;
				for (const FGeneralPolygon2d& Region : MetricRegions)
				{
					DamageAccumulator.RemoveInside(Region);
				}
			}
			else
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage after its configured pattern could not be cut."), *SurfaceName);
			}
			continue;
		}

		FBreachContourOptions ContourOptions = ShapeOptions.Contour;
		// 직접 총알의 무작위 시드 정책은 유지하고, 누적 파손만 안정적인 기본값을 쓴다.
		if (ContourOptions.Seed == 0)
		{
			ContourOptions.Seed = 1;
		}
		FPolygon2d MetricCutter = BaseMetric;
		if (ContourOptions.bEnabled)
		{
			FPolygon2d Modified;
			const bool bGenerated = BreachContour::Generate(BaseMetric, ContourOptions, Modified, Error);
			if (bGenerated)
			{
				MetricCutter = MoveTemp(Modified);
			}
			else
			{
				UE_LOG(LogBreachableSurface, Warning, TEXT("%s is using its base accumulation cutter: %s"), *SurfaceName, *Error);
			}
		}

		// 좌표 변환: 누적에 사용하는 cm 좌표를 표면 로컬 XZ로 되돌린다.
		auto ToLocal = [this](const FPolygon2d& Polygon)
		{
			FPolygon2d Local;
			for (const FVector2d& Point : Polygon.GetVertices())
			{
				Local.AppendVertex(CellGrid.ToLocal(Point));
			}
			return Local;
		};

		// 처리: 변형 윤곽 적용에 실패하면 기본 윤곽으로 재시도한다.
		bool bCutChanged = false;
		const TArray<FGeneralPolygon2d> Cutters{FGeneralPolygon2d(ToLocal(MetricCutter))};
		bool bApplied = CutRegions(Cutters, bCutChanged, Result);
		if (!bApplied && ContourOptions.bEnabled)
		{
			MetricCutter = BaseMetric;
			const TArray<FGeneralPolygon2d> BaseCutters{FGeneralPolygon2d(ToLocal(BaseMetric))};
			bApplied = CutRegions(BaseCutters, bCutChanged, Result);
		}

		// 결과 반영: 성공한 커터 내부 기록만 제거하고, 실패한 후보의 기록은 남긴다.
		if (bApplied)
		{
			bBreached = true;
			if (bCutChanged)
			{
				bOutChanged = true;
			}
			DamageAccumulator.RemoveInside(MetricCutter);
		}
		else
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s retained damage records after its accumulation cutter failed."), *SurfaceName);
		}
	}
	return bBreached;
}

void FBreachSurfaceWork::RemoveHitsInside(const FPolygon2d& LocalPolygon)
{
	// 상태 확인: 기록이 없으면 좌표 변환도 생략한다.
	if (DamageAccumulator.Num() == 0)
	{
		return;
	}

	// 처리: 로컬 절단 영역을 cm 좌표로 변환해 내부·경계 기록을 제거한다.
	FPolygon2d MetricPolygon;
	for (const FVector2d& Point : LocalPolygon.GetVertices())
	{
		MetricPolygon.AppendVertex(CellGrid.ToMetric(Point));
	}

	DamageAccumulator.RemoveInside(MetricPolygon);
}

bool FBreachSurfaceWork::CutRegions(TConstArrayView<FGeneralPolygon2d> InCutters, bool& bOutChanged, FBreachHitResult& Result)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Cut regions");
	bOutChanged = false;
	FString Error;
	if (!BreachPatternGeometry::ValidateRegions(InCutters, Error))
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s rejected cutters: %s"), *SurfaceName, *Error);
		return false;
	}

	TArray<FGeneralPolygon2d> Cutters;
	for (const FGeneralPolygon2d& Region : InCutters)
	{
		FPolygon2d Outer = Region.GetOuter();
		if (Outer.IsClockwise())
		{
			Outer.Reverse();
		}
		FGeneralPolygon2d Normalized(Outer);
		for (FPolygon2d Hole : Region.GetHoles())
		{
			if (!Hole.IsClockwise())
			{
				Hole.Reverse();
			}
			Normalized.AddHole(Hole, false, false);
		}
		Cutters.Add(MoveTemp(Normalized));
	}

	// 실패 처리: 표면 상태는 그대로 둔다. 이번 피격이 만든 Proxy를 되돌리는 것은 호출한 쪽이 한다.
	auto Reject = [&](const TCHAR* Reason)
	{
		UE_LOG(LogBreachableSurface, Warning, TEXT("%s kept its previous surface: %s"), *SurfaceName, Reason);
		return false;
	};

	// 주요로직 시작

	// 처리: 현재 영역에서 커터를 빼되 결과는 아직 후보 상태로 보관한다.

	TArray<FGeneralPolygon2d> CandidateRegions;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Subtract cutters");
		const bool bDifference = PolygonsDifference(SurfaceState.RemainingRegions, Cutters, CandidateRegions);
		if (!bDifference)
		{
			return Reject(TEXT("Polygon difference failed."));
		}
	}

	// 처리: 원본 경계에 고정되지 않은 고립 영역을 후보에서 제거한다.
	TArray<FPolygon2d> DetachedRegions;
	CandidateRegions.RemoveAll([&](const FGeneralPolygon2d& Region)
	{
		if (!TouchesOriginalBorder(Region.GetOuter(), OriginBounds))
		{
			DetachedRegions.Add(Region.GetOuter());
			return true;
		}
		return false;
	});

	// 이미 Proxy로 표시 중인 표면에서 면적이 줄지 않은 절단은 아무것도 바꾸지 않는다. 메시·충돌·구멍 캐시·Deco를 다시 만들지 않고 끝낸다.
	// 아직 전환하지 않은 표면은 효과 없는 절단이라도 계속 진행해, 기존처럼 Proxy 표시로 전환한다.
	double PreviousArea = 0;
	for (const FGeneralPolygon2d& Region : SurfaceState.RemainingRegions)
	{
		PreviousArea += FMath::Abs(Region.SignedArea());
	}
	double NewArea = 0;
	for (const FGeneralPolygon2d& Region : CandidateRegions)
	{
		NewArea += FMath::Abs(Region.SignedArea());
	}
	const bool bAreaReduced = PreviousArea - NewArea > UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER;
	if (!bAreaReduced)
	{
		if (bPublished)
		{
			return true;
		}
	}

	// 메시 생성: 후보 영역으로 새 메시를 만든다. 검증이 끝날 때까지 SurfaceState는 유지한다.
	FDynamicMesh3 RenewedMesh;
	for (const auto& RemainingRegion : CandidateRegions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Rebuild wall region mesh");
		TArray<FVector2d> Vertices;
		TArray<FIndex3i> Triangles; // 삼각분할 결과로 만들어진 삼각형들의 정점 인덱스를 저장하는 배열 (그냥 int 3개임)

		// 삼각분할이 실패하는 경우는 면적이 없는 영역뿐이라 만들 면도 없다.
		if (!BreachSurfaceGeometry::TriangulateRegion(RemainingRegion, Vertices, Triangles, Error))
		{
			continue;
		}

		// 메시 생성: 삼각분할을 원본 두께의 앞·뒷면으로 확장한다.
		TArray<int32> FrontIDs;
		TArray<int32> BackIDs;

		for (const FVector2d& Vertex : Vertices)
		{
			FrontIDs.Add(RenewedMesh.AppendVertex(FVector3d(Vertex.X, OriginalDepthRange.X, Vertex.Y)));
			BackIDs.Add(RenewedMesh.AppendVertex(FVector3d(Vertex.X, OriginalDepthRange.Y, Vertex.Y)));
		}

		auto EdgeKey = [](int32 A, int32 B) { return (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B)); };
		TMap<uint64, int32> EdgeUses;
		for (const FIndex3i& Triangle : Triangles)
		{
			RenewedMesh.AppendTriangle(FrontIDs[Triangle.A], FrontIDs[Triangle.C], FrontIDs[Triangle.B]);
			RenewedMesh.AppendTriangle(BackIDs[Triangle.A], BackIDs[Triangle.B], BackIDs[Triangle.C]);
			++EdgeUses.FindOrAdd(EdgeKey(Triangle.A, Triangle.B));
			++EdgeUses.FindOrAdd(EdgeKey(Triangle.B, Triangle.C));
			++EdgeUses.FindOrAdd(EdgeKey(Triangle.C, Triangle.A));
		}

		// 한 점에서 닿은 두 쐐기는 같은 세로 변을 네 옆면이 나눠 써야 해서 추가가 거부된다. 그 옆면만 같은 위치에 정점을 새로 만들어 붙인다.
		auto AppendSide = [&](int32 A, int32 B)
		{
			const int32 First = RenewedMesh.AppendTriangle(FrontIDs[A], FrontIDs[B], BackIDs[B]);
			if (First >= 0)
			{
				if (RenewedMesh.AppendTriangle(FrontIDs[A], BackIDs[B], BackIDs[A]) >= 0)
				{
					return;
				}
				RenewedMesh.RemoveTriangle(First, false);
			}
			const int32 FrontA = RenewedMesh.AppendVertex(RenewedMesh.GetVertex(FrontIDs[A]));
			const int32 FrontB = RenewedMesh.AppendVertex(RenewedMesh.GetVertex(FrontIDs[B]));
			const int32 BackA = RenewedMesh.AppendVertex(RenewedMesh.GetVertex(BackIDs[A]));
			const int32 BackB = RenewedMesh.AppendVertex(RenewedMesh.GetVertex(BackIDs[B]));
			RenewedMesh.AppendTriangle(FrontA, FrontB, BackB);
			RenewedMesh.AppendTriangle(FrontA, BackB, BackA);
		};

		// 옆면은 윤곽 대신 앞면 삼각형의 바깥 변에서 만든다. 대체 판정으로 채운 면과도 항상 맞물린다.
		for (const FIndex3i& Triangle : Triangles)
		{
			const int32 Corners[3] = {Triangle.A, Triangle.B, Triangle.C};
			for (int32 Index = 0; Index < 3; ++Index)
			{
				int32 A = Corners[Index];
				int32 B = Corners[(Index + 1) % 3];
				if (EdgeUses[EdgeKey(A, B)] != 1)
				{
					continue;
				}

				// 기존 윤곽 옆면처럼 벽 안쪽이 A→B의 왼쪽에 오게 맞춘다. 남은 꼭짓점이 벽 안쪽이다.
				const FVector2d AB = Vertices[B] - Vertices[A];
				const FVector2d AC = Vertices[Corners[(Index + 2) % 3]] - Vertices[A];
				if (AB.X * AC.Y - AB.Y * AC.X < 0)
				{
					Swap(A, B);
				}
				AppendSide(A, B);
			}
		}
	}

	// 면적이 줄어든 절삭만 Deco에 알린다. 사라진 도형은 Deco가 다시 계산할 범위를 정한다.
	if (bAreaReduced)
	{
		bool bHasCutterHoles = false;
		for (const FGeneralPolygon2d& Region : Cutters)
		{
			bHasCutterHoles |= !Region.GetHoles().IsEmpty();
		}
		if (bHasCutterHoles)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Resolve removed regions");
			TArray<FGeneralPolygon2d> Removed;
			if (!PolygonsDifference(SurfaceState.RemainingRegions, CandidateRegions, Removed))
			{
				return Reject(TEXT("Could not resolve hole-aware removal boundaries."));
			}
			TArray<FPolygon2d> RemovedPieces;
			for (const FGeneralPolygon2d& Region : Removed)
			{
				TArray<FVector2d> Vertices;
				TArray<FIndex3i> Triangles;
				// 면적이 없는 제거 영역은 Deco에 넘길 조각이 없다.
				if (!BreachSurfaceGeometry::TriangulateRegion(Region, Vertices, Triangles, Error))
				{
					continue;
				}
				for (const FIndex3i& Triangle : Triangles)
				{
					FPolygon2d Piece;
					Piece.AppendVertex(Vertices[Triangle.A]);
					Piece.AppendVertex(Vertices[Triangle.B]);
					Piece.AppendVertex(Vertices[Triangle.C]);
					RemovedPieces.Add(MoveTemp(Piece));
				}
			}
			// 기존 장식 입력은 구멍 없는 다각형이다. 실제 제거 영역만 전달해 남은 구멍 안의 장식을 보존한다.
			for (const FGeneralPolygon2d& Region : Removed)
			{
				Result.DecorationCuts.Add(Region.Bounds());
			}
			Result.DecorationRemoved.Append(RemovedPieces);
		}
		else
		{
			for (const FGeneralPolygon2d& Region : Cutters)
			{
				Result.DecorationCuts.Add(Region.Bounds());
				Result.DecorationRemoved.Add(Region.GetOuter());
			}
			Result.DecorationRemoved.Append(DetachedRegions);
		}
	}

	// 계산 결과만 보관한다. 실제 메시와 충돌은 모든 절단이 끝난 뒤 반영한다.
	SurfaceState.RemainingRegions = MoveTemp(CandidateRegions);
	if (bAreaReduced)
	{
		Result.RemovedArea += PreviousArea - NewArea;
	}
	Mesh = MoveTemp(RenewedMesh);
	bOutChanged = true;

	// 확정된 절단을 이동 충돌용 구멍 캐시에 알린다. 벽을 줄이지 않은 절단은 구멍이 아니므로 알리지 않는다.
	// 추적에 실패했거나 이전 절단에서 캐시를 잃었으면 남은 영역에서 캐시를 다시 만든다. 그대로 두면 이동 충돌이 더는 갱신되지 않는다.
	FString CacheError;
	bool bTracked = !bAreaReduced;
	if (bAreaReduced)
	{
		const bool bSingleSimpleCutter = Cutters.Num() == 1 && Cutters[0].GetHoles().IsEmpty();
		if (!bSingleSimpleCutter)
		{
			bTracked = SimpleCollision.RebuildHoleCache(SurfaceState.RemainingRegions, CacheError);
		}
		else if (SimpleCollision.HasHoleCache())
		{
			bTracked = SimpleCollision.ApplyCut(Cutters[0].GetOuter(), DetachedRegions, CacheError);
		}
		else
		{
			CacheError = TEXT("The hole cache was lost by an earlier cut.");
		}
	}
	if (!bTracked)
	{
		FString RebuildError;
		const bool bRebuilt = SimpleCollision.RebuildHoleCache(SurfaceState.RemainingRegions, RebuildError);
		if (bRebuilt)
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s rebuilt its movement openings from the remaining regions after tracking failed: %s"), *SurfaceName, *CacheError);
		}
		else
		{
			UE_LOG(LogBreachableSurface, Warning, TEXT("%s could not rebuild its movement openings and keeps its previous Simple collision until a later cut succeeds: %s / %s"), *SurfaceName, *CacheError, *RebuildError);
		}
	}

	for (const FPolygon2d& Detached : DetachedRegions)
	{
		RemoveHitsInside(Detached);
	}

	return true;
}

void FBreachSurfaceWork::FinishMesh()
{
	BuildSurfaceAttributes(Mesh, OriginBounds, SurfaceUVs);
}

void FBreachSurfaceWork::BuildSurfaceAttributes(FDynamicMesh3& InOutMesh, const FAxisAlignedBox2d& Bounds, const FPlanarUVMapping (&UVs)[2])
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Build mesh normals and UVs");
	// 처리: 검증된 메시의 노멀과 UV를 구성한다.
	UE::Geometry::FMeshNormals::InitializeMeshToPerTriangleNormals(&InOutMesh);

	// 새 메시의 각 삼각형에 UV 좌표를 만들어 연결
	// Attribute : 노멀, UV 등의 부가 데이터에 접근
	InOutMesh.Attributes()->SetNumUVLayers(1); // UV 채널을 1개로 설정
	auto* UVOverlay = InOutMesh.Attributes()->PrimaryUV();  // 첫 번째 UV 채널에 접근 (기존 UV 포인터)

	const FVector2d Min = Bounds.Min;
	const FVector2d Size = Bounds.Max - Min;

	for (int32 TriangleID : InOutMesh.TriangleIndicesItr())
	{
		const FIndex3i Triangle = InOutMesh.GetTriangle(TriangleID);
		const FVector3d Normal = InOutMesh.GetTriNormal(TriangleID);
		FIndex3i UVElementIDs;

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d Position = InOutMesh.GetVertex(Triangle[Corner]);

			// 벽의 좌표를 0~1 범위로 바꾼다.
			FVector2f UV(static_cast<float>((Position.X - Min.X) / Size.X),
				static_cast<float>((Position.Z - Min.Y) / Size.Y));
			if (FMath::Abs(Normal.Y) > 0.99)
			{
				const FPlanarUVMapping& Mapping = UVs[Normal.Y > 0 ? 1 : 0];
				UV = FVector2f(Mapping.Offset + Mapping.PerX * Position.X + Mapping.PerZ * Position.Z);
			}

			UVElementIDs[Corner] = UVOverlay->AppendElement(UV);
		}

		UVOverlay->SetTriangle(TriangleID, UVElementIDs);
	}
}

bool FBreachSurfaceWork::TouchesOriginalBorder(const FPolygon2d& Outer, const FAxisAlignedBox2d& Bounds)
{
	// 현재 outer의 Edge와 원본 outer의 Bound를 비교해 원본 경계와 닿지 않은 폴리곤들을 제거한다.
	constexpr double Epsilon = UE_DOUBLE_KINDA_SMALL_NUMBER;

	const TArray<FVector2d>& Vertices = Outer.GetVertices();

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2d& A = Vertices[i];
		const FVector2d& B = Vertices[(i + 1) % Vertices.Num()];

		// 길이가 없는 에지는 제외
		if ((B - A).SizeSquared() <= Epsilon * Epsilon)
		{
			continue;
		}

		// 각 정점이 Bound변 위에 있는지 검사한다.
		const bool bLeft =
			FMath::IsNearlyEqual(A.X, Bounds.Min.X, Epsilon) &&
			FMath::IsNearlyEqual(B.X, Bounds.Min.X, Epsilon);

		const bool bRight =
			FMath::IsNearlyEqual(A.X, Bounds.Max.X, Epsilon) &&
			FMath::IsNearlyEqual(B.X, Bounds.Max.X, Epsilon);

		const bool bBottom =
			FMath::IsNearlyEqual(A.Y, Bounds.Min.Y, Epsilon) &&
			FMath::IsNearlyEqual(B.Y, Bounds.Min.Y, Epsilon);

		const bool bTop =
			FMath::IsNearlyEqual(A.Y, Bounds.Max.Y, Epsilon) &&
			FMath::IsNearlyEqual(B.Y, Bounds.Max.Y, Epsilon);

		if (bLeft || bRight || bBottom || bTop)
		{
			return true;
		}
	}

	return false;
}
