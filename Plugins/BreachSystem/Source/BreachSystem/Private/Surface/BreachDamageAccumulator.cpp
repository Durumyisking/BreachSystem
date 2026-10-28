#include "Surface/BreachDamageAccumulator.h"
#include "Algo/BinarySearch.h"
#include "Algo/Unique.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

using namespace UE::Geometry;

bool FBreachDamageAccumulator::Configure(const FVector2d& InWindowSize, double InThreshold, FString& OutError)
{
	OutError.Reset();
	if (!FMath::IsFinite(InWindowSize.X) || !FMath::IsFinite(InWindowSize.Y)
		|| InWindowSize.X < 0.1 || InWindowSize.Y < 0.1 || !FMath::IsFinite(InThreshold) || InThreshold <= 0)
	{
		OutError = TEXT("Accumulation requires finite window dimensions >= 0.1 cm and a positive finite threshold.");
		return false;
	}
	if (WindowSize != InWindowSize || Threshold != InThreshold)
	{
		Reset();
		WindowSize = InWindowSize;
		Threshold = InThreshold;
	}
	return true;
}

int32 FBreachDamageAccumulator::AddHit(const FVector2d& Point, double Contribution, FString& OutError)
{
	OutError.Reset();
	if (WindowSize.X <= 0 || WindowSize.Y <= 0)
	{
		OutError = TEXT("Configure accumulation before adding impacts.");
		return INDEX_NONE;
	}
	const FVector2d CellPoint = Point / WindowSize;
	if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y)
		|| !FMath::IsFinite(Contribution) || Contribution <= 0
		|| !FMath::IsFinite(Point.X + WindowSize.X) || !FMath::IsFinite(Point.Y + WindowSize.Y)
		|| !FMath::IsFinite(Point.X - WindowSize.X) || !FMath::IsFinite(Point.Y - WindowSize.Y)
		|| !FMath::IsFinite(CellPoint.X) || !FMath::IsFinite(CellPoint.Y)
		|| FMath::Abs(CellPoint.X) > MAX_int32 - 4.0 || FMath::Abs(CellPoint.Y) > MAX_int32 - 4.0
		|| NextRecordID == MAX_int32)
	{
		OutError = TEXT("Accumulation impact has invalid coordinates/contribution or exceeds spatial index limits.");
		return INDEX_NONE;
	}
	const int32 ID = NextRecordID++;
	Records.Add(ID, {ID, Point, Contribution});
	const FIntPoint Cell = CellFor(Point);
	FColumn* Column = Columns.Find(Cell.X);
	if (!Column)
	{
		OccupiedColumns.Insert(Cell.X, Algo::LowerBound(OccupiedColumns, Cell.X));
		Column = &Columns.Add(Cell.X);
	}
	TArray<int32>& IDs = Column->Cells.FindOrAdd(Cell.Y);
	if (IDs.IsEmpty())
	{
		Column->Rows.Insert(Cell.Y, Algo::LowerBound(Column->Rows, Cell.Y));
	}
	IDs.Add(ID);
	return ID;
}

FIntPoint FBreachDamageAccumulator::CellFor(const FVector2d& Point) const
{
	// Cleanup cutters may extend beyond the coordinates accepted for stored impacts.
	return FIntPoint(FMath::FloorToInt(FMath::Clamp(Point.X / WindowSize.X, -double(MAX_int32), double(MAX_int32 - 1))),
		FMath::FloorToInt(FMath::Clamp(Point.Y / WindowSize.Y, -double(MAX_int32), double(MAX_int32 - 1))));
}

void FBreachDamageAccumulator::Query(const FAxisAlignedBox2d& Bounds, TArray<FBreachDamageRecord>& OutRecords,
	FBreachAccumulationQueryStats& OutStats) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Breach Accumulation | Spatial query");
	OutRecords.Reset();
	OutStats = {};
	const FIntPoint Min = CellFor(Bounds.Min);
	const FIntPoint Max = CellFor(Bounds.Max);
	for (int32 XIndex = Algo::LowerBound(OccupiedColumns, Min.X);
		XIndex < OccupiedColumns.Num() && OccupiedColumns[XIndex] <= Max.X; ++XIndex)
	{
		const FColumn& Column = Columns.FindChecked(OccupiedColumns[XIndex]);
		for (int32 YIndex = Algo::LowerBound(Column.Rows, Min.Y);
			YIndex < Column.Rows.Num() && Column.Rows[YIndex] <= Max.Y; ++YIndex)
		{
			++OutStats.VisitedCells;
			for (int32 ID : Column.Cells.FindChecked(Column.Rows[YIndex]))
			{
				++OutStats.ExaminedRecords;
				const FBreachDamageRecord& Record = Records.FindChecked(ID);
				if (Bounds.Contains(Record.Point))
				{
					OutRecords.Add(Record);
				}
			}
		}
	}
}

void FBreachDamageAccumulator::FindCandidates(int32 NewRecordID, TArray<FBreachAccumulationCandidate>& OutCandidates,
	FBreachAccumulationQueryStats& OutStats) const
{
	OutCandidates.Reset();
	OutStats = {};
	const FBreachDamageRecord* NewRecord = Records.Find(NewRecordID);
	if (!NewRecord)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Breach Accumulation | Local windows");
	const FVector2d Point = NewRecord->Point;
	TArray<FBreachDamageRecord> Nearby;
	Query(FAxisAlignedBox2d(Point - WindowSize, Point + WindowSize), Nearby, OutStats);
	double Total = 0;
	for (const FBreachDamageRecord& Record : Nearby)
	{
		Total += Record.Contribution;
	}
	if (Total < Threshold)
	{
		return;
	}
	Nearby.Sort([](const FBreachDamageRecord& A, const FBreachDamageRecord& B)
	{
		return A.Point.Y < B.Point.Y || (A.Point.Y == B.Point.Y && A.ID < B.ID);
	});
	TArray<double> StartsX, StartsY;
	for (const FBreachDamageRecord& Record : Nearby)
	{
		for (double X : {Record.Point.X, Record.Point.X - WindowSize.X})
		{
			if (X <= Point.X && X + WindowSize.X >= Point.X)
			{
				StartsX.Add(X);
			}
		}
		for (double Y : {Record.Point.Y, Record.Point.Y - WindowSize.Y})
		{
			if (Y <= Point.Y && Y + WindowSize.Y >= Point.Y)
			{
				StartsY.Add(Y);
			}
		}
	}
	StartsX.Sort();
	StartsX.SetNum(Algo::Unique(StartsX), EAllowShrinking::No);
	StartsY.Sort();
	StartsY.SetNum(Algo::Unique(StartsY), EAllowShrinking::No);
	TSet<int32> Covered;
	// Each selected window must cover a new record; bound work if numeric drift violates that invariant.
	while (Covered.Num() < Nearby.Num() && OutCandidates.Num() < Nearby.Num())
	{
		double BestFresh = 0;
		double BestTotal = 0;
		FAxisAlignedBox2d BestBounds;
		for (double X : StartsX)
		{
			TArray<FBreachDamageRecord> Active;
			for (const FBreachDamageRecord& Record : Nearby)
			{
				++OutStats.MembershipEvaluations;
				if (Record.Point.X >= X && Record.Point.X <= X + WindowSize.X)
				{
					Active.Add(Record);
				}
			}
			int32 First = 0, Last = 0, MinHead = 0, MaxHead = 0;
			int32 FreshCount = 0;
			TArray<int32> MinX, MaxX;
			double Sum = 0, Fresh = 0;
			// Sweep sorted Y boundaries once per X; queues retain the member X extrema.
			for (double Y : StartsY)
			{
				while (Last < Active.Num() && Active[Last].Point.Y <= Y + WindowSize.Y)
				{
					const FBreachDamageRecord& Record = Active[Last];
					Sum += Record.Contribution;
					if (!Covered.Contains(Record.ID))
					{
						Fresh += Record.Contribution;
						++FreshCount;
					}
					while (MinX.Num() > MinHead && Active[MinX.Last()].Point.X >= Record.Point.X)
					{
						MinX.Pop(EAllowShrinking::No);
					}
					while (MaxX.Num() > MaxHead && Active[MaxX.Last()].Point.X <= Record.Point.X)
					{
						MaxX.Pop(EAllowShrinking::No);
					}
					MinX.Add(Last);
					MaxX.Add(Last++);
				}
				while (First < Last && Active[First].Point.Y < Y)
				{
					Sum -= Active[First].Contribution;
					if (!Covered.Contains(Active[First].ID))
					{
						Fresh -= Active[First].Contribution;
						--FreshCount;
					}
					++First;
				}
				while (MinHead < MinX.Num() && MinX[MinHead] < First)
				{
					++MinHead;
				}
				while (MaxHead < MaxX.Num() && MaxX[MaxHead] < First)
				{
					++MaxHead;
				}
				if (First == Last || Sum < Threshold || FreshCount == 0 || Fresh <= 0)
				{
					continue;
				}
				const FVector2d Center(Active[MinX[MinHead]].Point.X * 0.5 + Active[MaxX[MaxHead]].Point.X * 0.5,
					Active[First].Point.Y * 0.5 + Active[Last - 1].Point.Y * 0.5);
				FAxisAlignedBox2d Bounds(Center - WindowSize * 0.5, Center + WindowSize * 0.5);
				// Recentering must not round a qualifying boundary impact out of its own window.
				Bounds.Contain(FVector2d(Active[MinX[MinHead]].Point.X, Active[First].Point.Y));
				Bounds.Contain(FVector2d(Active[MaxX[MaxHead]].Point.X, Active[Last - 1].Point.Y));
				const bool bBetter = Fresh > BestFresh || (Fresh == BestFresh && (Sum > BestTotal
					|| (Sum == BestTotal && (Bounds.Min.X < BestBounds.Min.X
						|| (Bounds.Min.X == BestBounds.Min.X && Bounds.Min.Y < BestBounds.Min.Y)))));
				if (bBetter)
				{
					BestFresh = Fresh;
					BestTotal = Sum;
					BestBounds = Bounds;
				}
			}
		}
		if (BestFresh <= 0)
		{
			break;
		}
		FBreachAccumulationCandidate Candidate;
		Candidate.Bounds = BestBounds;
		// Only the selected window materializes its member list, including recentering additions.
		for (const FBreachDamageRecord& Record : Nearby)
		{
			++OutStats.MembershipEvaluations;
			if (BestBounds.Contains(Record.Point))
			{
				Candidate.RecordIDs.Add(Record.ID);
				Candidate.ImpactPoints.Add(Record.Point);
				Candidate.Contribution += Record.Contribution;
				Covered.Add(Record.ID);
			}
		}
		OutCandidates.Add(MoveTemp(Candidate));
	}
}

void FBreachDamageAccumulator::RemoveInside(const UE::Geometry::FPolygon2d& Polygon, FBreachAccumulationQueryStats* OutStats)
{
	if (OutStats)
	{
		*OutStats = {};
	}
	if (Records.IsEmpty() || Polygon.VertexCount() < 3)
	{
		return;
	}
	TArray<FBreachDamageRecord> Nearby;
	FBreachAccumulationQueryStats Stats;
	Query(Polygon.Bounds(), Nearby, Stats);
	if (OutStats)
	{
		*OutStats = Stats;
	}
	for (const FBreachDamageRecord& Record : Nearby)
	{
		if (Polygon.Contains(Record.Point) || Polygon.DistanceSquared(Record.Point) <= UE_DOUBLE_SMALL_NUMBER)
		{
			const FIntPoint Cell = CellFor(Record.Point);
			FColumn& Column = Columns.FindChecked(Cell.X);
			TArray<int32>& IDs = Column.Cells.FindChecked(Cell.Y);
			IDs.RemoveSwap(Record.ID, EAllowShrinking::No);
			if (IDs.IsEmpty())
			{
				Column.Cells.Remove(Cell.Y);
				Column.Rows.RemoveAt(Algo::LowerBound(Column.Rows, Cell.Y), 1, EAllowShrinking::No);
				if (Column.Rows.IsEmpty())
				{
					Columns.Remove(Cell.X);
					OccupiedColumns.RemoveAt(Algo::LowerBound(OccupiedColumns, Cell.X), 1, EAllowShrinking::No);
				}
			}
			Records.Remove(Record.ID);
		}
	}
}

void FBreachDamageAccumulator::Reset()
{
	Records.Reset();
	Columns.Reset();
	OccupiedColumns.Reset();
	NextRecordID = 0;
}

int32 FBreachDamageAccumulator::Num() const
{
	return Records.Num();
}
