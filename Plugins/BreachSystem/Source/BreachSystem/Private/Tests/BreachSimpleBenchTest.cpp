#include "Misc/AutomationTest.h"

// 테스트 전용: 같은 탄착 기록으로 Simple 충돌 경로의 시간·할당·메모리를 계측한다.
// 통과/실패 판정이 아닌 변경 전후 비교가 목적이며, -BreachSimpleBench 인자를 준 전용 프로세스에서만 실행한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Surface/BreachSimpleCollision.h"
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Settings/BreachSystemSettings.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/MemoryBase.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTime.h"
#include "HAL/UnrealMemory.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "BreachSimpleAsyncTestUtils.h"
#include <atomic>
#include <intrin.h>

namespace
{
	using namespace UE::Geometry;

	enum class EBenchMode : int32
	{
		Off,
		Count,
		Time
	};

	// 할당 분류: 플러그인 코드가 직접 요청했는지, 플러그인이 부른 엔진 코드가 요청했는지, 플러그인 프레임을 찾지 못했는지.
	enum EBenchClass : int32
	{
		BenchDirect,
		BenchViaEngine,
		BenchUnattributed,
		BenchClassCount
	};

	struct FBenchAllocSample
	{
		uint64 Calls[BenchClassCount] = {};
		uint64 Bytes[BenchClassCount] = {};
		uint64 Frees = 0;
		int64 Net = 0;
		int64 Peak = 0;
		uint64 Cycles = 0;
		uint64 TimedCalls = 0;
	};

	struct FBenchSite
	{
		std::atomic<uint64> Key{0};
		std::atomic<uint64> Calls{0};
		std::atomic<uint64> Bytes{0};
	};

	constexpr int32 BenchSiteSlots = 4096;

	struct FBenchState
	{
		std::atomic<int32> Mode{0};
		std::atomic<bool> bRecordSites{false};
		std::atomic<uint64> Calls[BenchClassCount];
		std::atomic<uint64> Bytes[BenchClassCount];
		std::atomic<uint64> Frees{0};
		std::atomic<int64> Net{0};
		std::atomic<int64> Peak{0};
		std::atomic<uint64> Cycles{0};
		std::atomic<uint64> TimedCalls{0};
		uint64 BreachBegin = 0, BreachEnd = 0, CoreBegin = 0, CoreEnd = 0;
		FBenchSite Sites[2][BenchSiteSlots];

		void Begin(EBenchMode InMode)
		{
			for (int32 Index = 0; Index < BenchClassCount; ++Index)
			{
				Calls[Index] = 0;
				Bytes[Index] = 0;
			}
			Frees = 0;
			Net = 0;
			Peak = 0;
			Cycles = 0;
			TimedCalls = 0;
			Mode = static_cast<int32>(InMode);
		}

		FBenchAllocSample End()
		{
			Mode = static_cast<int32>(EBenchMode::Off);
			FBenchAllocSample Sample;
			for (int32 Index = 0; Index < BenchClassCount; ++Index)
			{
				Sample.Calls[Index] = Calls[Index];
				Sample.Bytes[Index] = Bytes[Index];
			}
			Sample.Frees = Frees;
			Sample.Net = Net;
			Sample.Peak = Peak;
			Sample.Cycles = Cycles;
			Sample.TimedCalls = TimedCalls;
			return Sample;
		}

		void AddNet(int64 Delta)
		{
			const int64 Now = Net.fetch_add(Delta) + Delta;
			int64 Old = Peak.load();
			while (Now > Old)
			{
				if (Peak.compare_exchange_weak(Old, Now))
				{
					break;
				}
			}
		}

		void AddSite(int32 Table, uint64 Key, uint64 Size)
		{
			uint32 Slot = static_cast<uint32>((Key * 0x9E3779B97F4A7C15ull) >> 52);
			for (int32 Probe = 0; Probe < BenchSiteSlots; ++Probe)
			{
				FBenchSite& Site = Sites[Table][Slot];
				uint64 Existing = Site.Key.load();
				if (Existing == 0)
				{
					if (Site.Key.compare_exchange_strong(Existing, Key))
					{
						Existing = Key;
					}
				}
				if (Existing == Key)
				{
					++Site.Calls;
					Site.Bytes += Size;
					return;
				}
				Slot = (Slot + 1) % BenchSiteSlots;
			}
		}

		void RecordAlloc(uint64 Size)
		{
			uint64 Stack[48];
			const uint32 Depth = FPlatformStackWalk::CaptureStackBackTrace(Stack, 48);
			uint32 Frame = 0;
			// 캡처 함수(Core)와 이 프록시(플러그인 모듈)의 프레임을 먼저 건너뛴다.
			while (Frame < Depth && Stack[Frame] >= CoreBegin && Stack[Frame] < CoreEnd)
			{
				++Frame;
			}
			while (Frame < Depth && Stack[Frame] >= BreachBegin && Stack[Frame] < BreachEnd)
			{
				++Frame;
			}

			bool bEngine = false;
			uint64 Site = 0;
			for (; Frame < Depth; ++Frame)
			{
				const uint64 Counter = Stack[Frame];
				if (Counter >= BreachBegin && Counter < BreachEnd)
				{
					Site = Counter;
					break;
				}
				if (Counter < CoreBegin || Counter >= CoreEnd)
				{
					bEngine = true;
				}
			}

			int32 Class = BenchUnattributed;
			if (Site != 0)
			{
				Class = bEngine ? BenchViaEngine : BenchDirect;
				if (bRecordSites.load())
				{
					AddSite(bEngine ? 1 : 0, Site, Size);
				}
			}
			++Calls[Class];
			Bytes[Class] += Size;
		}
	};

	FBenchState& BenchState()
	{
		static FBenchState State;
		return State;
	}

	// 테스트 전용: 실제 할당기 앞에서 호출만 세고 그대로 전달한다. 계측 구간 밖에서는 설치하지 않는다.
	class FBenchCountingMalloc final : public FMalloc
	{
	public:
		FMalloc* Inner = nullptr;

		virtual void* Malloc(SIZE_T Count, uint32 Alignment) override
		{
			FBenchState& State = BenchState();
			const int32 Mode = State.Mode.load(std::memory_order_relaxed);
			if (Mode == static_cast<int32>(EBenchMode::Off))
			{
				return Inner->Malloc(Count, Alignment);
			}
			if (Mode == static_cast<int32>(EBenchMode::Time))
			{
				const uint64 Start = __rdtsc();
				void* Result = Inner->Malloc(Count, Alignment);
				State.Cycles += __rdtsc() - Start;
				++State.TimedCalls;
				return Result;
			}
			State.RecordAlloc(Count);
			void* Result = Inner->Malloc(Count, Alignment);
			SIZE_T Actual = 0;
			if (Result != nullptr && Inner->GetAllocationSize(Result, Actual))
			{
				State.AddNet(static_cast<int64>(Actual));
			}
			return Result;
		}

		virtual void* Realloc(void* Original, SIZE_T Count, uint32 Alignment) override
		{
			FBenchState& State = BenchState();
			const int32 Mode = State.Mode.load(std::memory_order_relaxed);
			if (Mode == static_cast<int32>(EBenchMode::Off))
			{
				return Inner->Realloc(Original, Count, Alignment);
			}
			if (Mode == static_cast<int32>(EBenchMode::Time))
			{
				const uint64 Start = __rdtsc();
				void* Result = Inner->Realloc(Original, Count, Alignment);
				State.Cycles += __rdtsc() - Start;
				++State.TimedCalls;
				return Result;
			}
			SIZE_T Old = 0;
			if (Original != nullptr && Inner->GetAllocationSize(Original, Old))
			{
				State.AddNet(-static_cast<int64>(Old));
			}
			if (Count > 0)
			{
				State.RecordAlloc(Count);
			}
			else if (Original != nullptr)
			{
				++State.Frees;
			}
			void* Result = Inner->Realloc(Original, Count, Alignment);
			SIZE_T Actual = 0;
			if (Result != nullptr && Inner->GetAllocationSize(Result, Actual))
			{
				State.AddNet(static_cast<int64>(Actual));
			}
			return Result;
		}

		virtual void Free(void* Original) override
		{
			FBenchState& State = BenchState();
			const int32 Mode = State.Mode.load(std::memory_order_relaxed);
			if (Mode == static_cast<int32>(EBenchMode::Off) || Original == nullptr)
			{
				Inner->Free(Original);
				return;
			}
			if (Mode == static_cast<int32>(EBenchMode::Time))
			{
				const uint64 Start = __rdtsc();
				Inner->Free(Original);
				State.Cycles += __rdtsc() - Start;
				++State.TimedCalls;
				return;
			}
			SIZE_T Old = 0;
			if (Inner->GetAllocationSize(Original, Old))
			{
				State.AddNet(-static_cast<int64>(Old));
			}
			++State.Frees;
			Inner->Free(Original);
		}

		virtual SIZE_T QuantizeSize(SIZE_T Count, uint32 Alignment) override { return Inner->QuantizeSize(Count, Alignment); }
		virtual bool GetAllocationSize(void* Original, SIZE_T& SizeOut) override { return Inner->GetAllocationSize(Original, SizeOut); }
		virtual void Trim(bool bTrimThreadCaches) override { Inner->Trim(bTrimThreadCaches); }
		virtual void SetupTLSCachesOnCurrentThread() override { Inner->SetupTLSCachesOnCurrentThread(); }
		virtual void MarkTLSCachesAsUsedOnCurrentThread() override { Inner->MarkTLSCachesAsUsedOnCurrentThread(); }
		virtual void MarkTLSCachesAsUnusedOnCurrentThread() override { Inner->MarkTLSCachesAsUnusedOnCurrentThread(); }
		virtual void ClearAndDisableTLSCachesOnCurrentThread() override { Inner->ClearAndDisableTLSCachesOnCurrentThread(); }
		virtual void UpdateStats() override { Inner->UpdateStats(); }
		virtual void GetAllocatorStats(FGenericMemoryStats& OutStats) override { Inner->GetAllocatorStats(OutStats); }
		virtual void DumpAllocatorStats(FOutputDevice& Ar) override { Inner->DumpAllocatorStats(Ar); }
		virtual bool IsInternallyThreadSafe() const override { return true; }
		virtual bool ValidateHeap() override { return Inner->ValidateHeap(); }
		virtual const TCHAR* GetDescriptiveName() override { return Inner->GetDescriptiveName(); }
		virtual uint64 GetImmediatelyFreeableCachedMemorySize() const override { return Inner->GetImmediatelyFreeableCachedMemorySize(); }
		virtual uint64 GetTotalFreeCachedMemorySize() const override { return Inner->GetTotalFreeCachedMemorySize(); }
	};

	// 다른 스레드가 교체 순간에 프록시 안에 있을 수 있으므로 프록시는 프로세스 수명 동안 유지한다.
	FBenchCountingMalloc& BenchMalloc()
	{
		static FBenchCountingMalloc Proxy;
		return Proxy;
	}

	struct FBenchMallocScope
	{
		FBenchMallocScope()
		{
			FBenchCountingMalloc& Proxy = BenchMalloc();
			Proxy.Inner = UE::Private::GMalloc;
			UE::Private::GMalloc = &Proxy;
		}
		~FBenchMallocScope()
		{
			BenchState().Mode = static_cast<int32>(EBenchMode::Off);
			UE::Private::GMalloc = BenchMalloc().Inner;
		}
	};

	bool BenchFindModule(uint64 Address, uint64& OutBegin, uint64& OutEnd)
	{
		const int32 Count = FPlatformStackWalk::GetProcessModuleCount();
		TArray<FStackWalkModuleInfo> Modules;
		Modules.SetNumZeroed(Count);
		const int32 Found = FPlatformStackWalk::GetProcessModuleSignatures(Modules.GetData(), Count);
		for (int32 Index = 0; Index < Found; ++Index)
		{
			const uint64 Begin = Modules[Index].BaseOfImage;
			const uint64 End = Begin + Modules[Index].ImageSize;
			if (Address >= Begin && Address < End)
			{
				OutBegin = Begin;
				OutEnd = End;
				return true;
			}
		}
		return false;
	}

	struct FBenchShot
	{
		FPolygon2d Cutter;
		const TCHAR* Phase = TEXT("");
	};

	// 월드 cm로 만든 울퉁불퉁한 원을 표면 로컬 좌표로 바꾼다. 중심에서 별 모양이므로 자기 교차가 없다.
	FPolygon2d BenchRoughCircle(FRandomStream& Random, const FVector2d& Center, double Radius, int32 Segments, const FVector2d& WorldScale)
	{
		FPolygon2d Polygon;
		const double Rotation = Random.FRand() * UE_DOUBLE_TWO_PI;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const double Angle = Rotation + UE_DOUBLE_TWO_PI * Index / Segments;
			const double Distance = Radius * Random.FRandRange(0.9f, 1.1f);
			const FVector2d World = Center + Distance * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle));
			Polygon.AppendVertex({World.X / WorldScale.X, World.Y / WorldScale.Y});
		}
		return Polygon;
	}

	// 800x540cm 벽: 산발 총알, 폭발 2회, 가장자리 확장, 두 구멍을 잇는 통로, 병합하는 세 번째 폭발.
	TArray<FBenchShot> BenchShots(const FVector2d& WorldScale)
	{
		TArray<FBenchShot> Shots;
		FRandomStream Random(20261113);
		auto Add = [&](const TCHAR* Phase, const FVector2d& Center, double Radius, int32 Segments)
		{
			Shots.Add({BenchRoughCircle(Random, Center, Radius, Segments, WorldScale), Phase});
		};
		auto Rim = [&](const TCHAR* Phase, const FVector2d& Center, double Radius)
		{
			const double Angle = Random.FRand() * UE_DOUBLE_TWO_PI;
			const double Distance = Radius * Random.FRandRange(0.9f, 1.1f);
			Add(Phase, Center + Distance * FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)), Random.FRandRange(5.f, 8.f), 12);
		};
		const FVector2d A(-150, 0), B(150, 20), C(0, -70);

		for (int32 Index = 0; Index < 30; ++Index)
		{
			Add(TEXT("1_scatter"), {Random.FRandRange(-380.f, 380.f), Random.FRandRange(-250.f, 250.f)}, Random.FRandRange(4.f, 6.f), 12);
		}
		Add(TEXT("2_blastA"), A, 70, 32);
		for (int32 Index = 0; Index < 30; ++Index)
		{
			Rim(TEXT("3_rimA"), A, 70);
		}
		Add(TEXT("4_blastB"), B, 60, 32);
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Rim(TEXT("5_rimB"), B, 60);
		}
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Add(TEXT("6_bridge"), FMath::Lerp(FVector2d(-85, 0), FVector2d(95, 15), Index / 9.0), 11, 12);
		}
		Add(TEXT("7_blastC"), C, 80, 32);
		for (int32 Index = 0; Index < 20; ++Index)
		{
			const FVector2d Centers[3] = {A, B, C};
			const double Radii[3] = {70, 60, 80};
			Rim(TEXT("8_rimAll"), Centers[Index % 3], Radii[Index % 3]);
		}
		return Shots;
	}

	// Surface 컴포넌트의 절단 순서를 따른다: 남은 영역에서 커터를 빼고, 원본 경계에 닿지 않는 영역은 고립 조각으로 넘긴다.
	struct FBenchSurface
	{
		FPolygon2d Wall;
		FAxisAlignedBox2d WallBounds;
		TArray<FGeneralPolygon2d> Regions;
		FBreachSimpleCollision Live;

		FBenchSurface()
		{
			for (const FVector2d Point : {FVector2d(-50, -50), FVector2d(50, -50), FVector2d(50, 50), FVector2d(-50, 50)})
			{
				Wall.AppendVertex(Point);
			}
			WallBounds = Wall.Bounds();
			Regions.Add(FGeneralPolygon2d(Wall));
			Live.Initialize(Wall, {-50, 50});
		}

		bool Cut(const FPolygon2d& Cutter, TArray<FPolygon2d>& OutDetached)
		{
			const TArray<FGeneralPolygon2d> Cutters = {FGeneralPolygon2d(Cutter)};
			TArray<FGeneralPolygon2d> Candidate;
			if (!PolygonsDifference(Regions, Cutters, Candidate))
			{
				return false;
			}
			Regions.Reset();
			for (FGeneralPolygon2d& Region : Candidate)
			{
				const FAxisAlignedBox2d Bounds = Region.GetOuter().Bounds();
				constexpr double Epsilon = 0.001;
				const bool bTouchesX = Bounds.Min.X <= WallBounds.Min.X + Epsilon || Bounds.Max.X >= WallBounds.Max.X - Epsilon;
				const bool bTouchesY = Bounds.Min.Y <= WallBounds.Min.Y + Epsilon || Bounds.Max.Y >= WallBounds.Max.Y - Epsilon;
				if (bTouchesX || bTouchesY)
				{
					Regions.Add(MoveTemp(Region));
				}
				else
				{
					OutDetached.Add(Region.GetOuter());
				}
			}
			return true;
		}
	};

	enum EBenchSection : int32
	{
		BenchApply,
		BenchCopy,
		BenchBuild,
		BenchCommit,
		BenchSectionCount
	};

	struct FBenchRow
	{
		double Ms[BenchSectionCount] = {MAX_dbl, MAX_dbl, MAX_dbl, MAX_dbl};
		double AllocMs[BenchSectionCount] = {MAX_dbl, MAX_dbl, MAX_dbl, MAX_dbl};
		FBenchAllocSample Allocs[BenchSectionCount];
		FBreachSimpleBuildStats Stats;
		int32 ConvexCount = 0;
		// 실제 컴포넌트 경로(게임 월드)에서 잰 값.
		double CutMs = MAX_dbl, GeometryMs = MAX_dbl, AdoptMs = MAX_dbl, CookMs = MAX_dbl, LatencyMs = MAX_dbl;
		int32 CookChanged = 0;
		bool bCutAccepted = false;
	};

	template<typename BodyType>
	void BenchMeasure(EBenchMode Mode, BodyType&& Body, double& OutMs, FBenchAllocSample& OutSample)
	{
		FBenchState& State = BenchState();
		if (Mode != EBenchMode::Off)
		{
			State.Begin(Mode);
		}
		const double Started = FPlatformTime::Seconds();
		Body();
		OutMs = (FPlatformTime::Seconds() - Started) * 1000;
		if (Mode != EBenchMode::Off)
		{
			OutSample = State.End();
		}
	}

	// 컴포넌트가 절단마다 수행하는 Simple 단계(누적, 스냅샷 복사, 계산, 채택)를 스레드 전환 없이 같은 순서로 재현한다.
	bool BenchReplay(FAutomationTestBase& Test, EBenchMode Mode, const TArray<FBenchShot>& Shots, const FBreachSimpleBuildPolicy& Policy, double CyclesPerMs, double TimerCycles, TArray<FBenchRow>& Rows, int64* OutResidentBytes = nullptr)
	{
		FBenchSurface Surface;
		FString Error;
		for (int32 Step = 0; Step < Shots.Num(); ++Step)
		{
			TArray<FPolygon2d> Detached;
			if (!Surface.Cut(Shots[Step].Cutter, Detached))
			{
				Test.AddError(FString::Printf(TEXT("Bench step %d could not derive remaining regions."), Step));
				return false;
			}

			double Ms[BenchSectionCount] = {};
			FBenchAllocSample Samples[BenchSectionCount];
			bool bApplied = false, bBuilt = false;
			TOptional<FBreachSimpleCollision> Snapshot;
			FKAggregateGeom Shapes;
			BenchState().bRecordSites = false;
			BenchMeasure(Mode, [&] { bApplied = Surface.Live.ApplyCut(Shots[Step].Cutter, Detached, Error); }, Ms[BenchApply], Samples[BenchApply]);
			if (!bApplied)
			{
				Test.AddError(FString::Printf(TEXT("Bench step %d ApplyCut failed: %s"), Step, *Error));
				return false;
			}
			BenchMeasure(Mode, [&] { Snapshot.Emplace(Surface.Live); }, Ms[BenchCopy], Samples[BenchCopy]);
			BenchState().bRecordSites = true;
			BenchMeasure(Mode, [&] { bBuilt = Snapshot->BuildMeshFromCuts(Policy, Shapes, Error); }, Ms[BenchBuild], Samples[BenchBuild]);
			BenchState().bRecordSites = false;
			if (!bBuilt)
			{
				Test.AddError(FString::Printf(TEXT("Bench step %d build failed: %s"), Step, *Error));
				return false;
			}
			FBenchRow& Row = Rows[Step];
			Row.Stats = Snapshot->GetLastBuildStats();
			Row.ConvexCount = Shapes.ConvexElems.Num();
			// 채택 비용에는 작업 스냅샷의 해제도 포함한다.
			BenchMeasure(Mode, [&] { Surface.Live.CommitBuild(*Snapshot); Snapshot.Reset(); }, Ms[BenchCommit], Samples[BenchCommit]);

			for (int32 Section = 0; Section < BenchSectionCount; ++Section)
			{
				if (Mode == EBenchMode::Off)
				{
					Row.Ms[Section] = FMath::Min(Row.Ms[Section], Ms[Section]);
				}
				else if (Mode == EBenchMode::Count)
				{
					Row.Allocs[Section] = Samples[Section];
				}
				else
				{
					const double Raw = static_cast<double>(Samples[Section].Cycles) - Samples[Section].TimedCalls * TimerCycles;
					Row.AllocMs[Section] = FMath::Min(Row.AllocMs[Section], FMath::Max(0.0, Raw) / CyclesPerMs);
				}
			}
		}

		// 상주 메모리: 모든 절단 뒤 Simple 상태(구멍, 전처리, 통로, 마지막 충돌)를 비울 때 반환되는 크기.
		if (Mode == EBenchMode::Count && OutResidentBytes != nullptr)
		{
			BenchState().Begin(EBenchMode::Count);
			Surface.Live = FBreachSimpleCollision();
			*OutResidentBytes = -BenchState().End().Net;
		}
		return true;
	}

	// 테스트 전용: 컴포넌트가 Verbose로 남기는 비동기 완료 통계를 읽는다.
	class FBenchLogCapture final : public FOutputDevice
	{
	public:
		float GeometryMs = 0, AdoptMs = 0, CookMs = 0, LatencyMs = 0;
		int32 Changed = 0;
		bool bSeen = false;

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (FCString::Strstr(Message, TEXT("[BreachSimpleAsync]")) == nullptr)
			{
				return;
			}
			bSeen = true;
			FParse::Value(Message, TEXT("geometry="), GeometryMs);
			FParse::Value(Message, TEXT("adopt="), AdoptMs);
			FParse::Value(Message, TEXT("cook="), CookMs);
			FParse::Value(Message, TEXT("latency="), LatencyMs);
			FParse::Value(Message, TEXT("changed="), Changed);
		}
	};

	struct FBenchCookRow
	{
		FString Phase;
		int32 Step = 0, ConvexCount = 0, Triangles = 0;
		double BothMs = MAX_dbl, ComplexOnlyMs = MAX_dbl;
	};

	// 같은 탄착 기록을 실제 게임 월드의 컴포넌트에 적용한다. 절단마다 비동기 완료를 기다리므로 연사 병합은 일어나지 않는다.
	bool BenchComponent(FAutomationTestBase& Test, const TArray<FBenchShot>& Shots, const FVector2d& WorldScale, TArray<FBenchRow>& Rows, TArray<FBenchCookRow>* CookRows)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Test.TestNotNull(TEXT("Bench game world exists"), World))
		{
			return false;
		}
		AActor* Wall = World->SpawnActor<AActor>();
		check(Wall);
		UBreachableSurfaceComponent* Surface = NewObject<UBreachableSurfaceComponent>(Wall);
		Wall->SetRootComponent(Surface);
		Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Surface->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(0, 0, 500), FVector(WorldScale.X, 0.1, WorldScale.Y)));
		Surface->RegisterComponent();

		FBenchLogCapture Capture;
		check(GLog);
		GLog->AddOutputDevice(&Capture);
		bool bOk = true;
		for (int32 Step = 0; Step < Shots.Num(); ++Step)
		{
			FBenchRow& Row = Rows[Step];
			Capture.bSeen = false;
			const double Started = FPlatformTime::Seconds();
			const bool bAccepted = Surface->ApplyCutterPolygon(Shots[Step].Cutter);
			Row.CutMs = FMath::Min(Row.CutMs, (FPlatformTime::Seconds() - Started) * 1000);
			Row.bCutAccepted = bAccepted;
			if (!FBreachSimpleAsyncTestAccess::Wait(Surface))
			{
				Test.AddError(FString::Printf(TEXT("Bench step %d async Simple build did not finish."), Step));
				bOk = false;
				break;
			}
			if (Capture.bSeen)
			{
				Row.GeometryMs = FMath::Min(Row.GeometryMs, static_cast<double>(Capture.GeometryMs));
				Row.AdoptMs = FMath::Min(Row.AdoptMs, static_cast<double>(Capture.AdoptMs));
				Row.CookMs = FMath::Min(Row.CookMs, static_cast<double>(Capture.CookMs));
				Row.LatencyMs = FMath::Min(Row.LatencyMs, static_cast<double>(Capture.LatencyMs));
				Row.CookChanged = Capture.Changed;
			}

			const bool bPhaseEnd = Step + 1 == Shots.Num() || FCString::Strcmp(Shots[Step].Phase, Shots[Step + 1].Phase) != 0;
			UBreachSurfaceProxy* Proxy = Wall->FindComponentByClass<UBreachSurfaceProxy>();
			if (CookRows != nullptr && bPhaseEnd && Proxy != nullptr)
			{
				// 쿠킹 분리: 같은 메시에서 Simple 도형을 비운 재쿠킹과 둘 다 있는 재쿠킹을 비교한 뒤 원래 도형으로 되돌린다.
				FBenchCookRow& Cook = CookRows->AddDefaulted_GetRef();
				Cook.Phase = Shots[Step].Phase;
				Cook.Step = Step;
				const FKAggregateGeom Saved = Proxy->GetSimpleCollisionShapes();
				Cook.ConvexCount = Saved.ConvexElems.Num();
				Cook.Triangles = Proxy->GetMesh() ? Proxy->GetMesh()->TriangleCount() : 0;
				for (int32 Repeat = 0; Repeat < 5; ++Repeat)
				{
					const double BothStarted = FPlatformTime::Seconds();
					Proxy->UpdateCollision(false);
					Cook.BothMs = FMath::Min(Cook.BothMs, (FPlatformTime::Seconds() - BothStarted) * 1000);
				}
				Proxy->SetSimpleCollisionShapes(FKAggregateGeom(), false);
				for (int32 Repeat = 0; Repeat < 5; ++Repeat)
				{
					const double ComplexStarted = FPlatformTime::Seconds();
					Proxy->UpdateCollision(false);
					Cook.ComplexOnlyMs = FMath::Min(Cook.ComplexOnlyMs, (FPlatformTime::Seconds() - ComplexStarted) * 1000);
				}
				Proxy->SetSimpleCollisionShapes(Saved, false);
				Proxy->UpdateCollision(false);
			}
		}
		GLog->RemoveOutputDevice(&Capture);
		World->DestroyWorld(false);
		return bOk;
	}

	double BenchAllocPairNs(SIZE_T Size, int32 Burst)
	{
		constexpr int32 Rounds = 200000;
		TArray<void*, TInlineAllocator<64>> Blocks;
		Blocks.SetNumZeroed(Burst);
		const double Started = FPlatformTime::Seconds();
		for (int32 Round = 0; Round < Rounds / Burst; ++Round)
		{
			for (int32 Index = 0; Index < Burst; ++Index)
			{
				Blocks[Index] = FMemory::Malloc(Size);
			}
			for (int32 Index = Burst - 1; Index >= 0; --Index)
			{
				FMemory::Free(Blocks[Index]);
			}
		}
		return (FPlatformTime::Seconds() - Started) * 1.e9 / (Rounds / Burst * Burst);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachSimpleBenchTest, "BreachSystem.Bench.Simple", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachSimpleBenchTest::RunTest(const FString& Parameters)
{
	// 전역 할당기를 잠시 교체하므로 일반 테스트 묶음에서는 실행하지 않는다.
	if (!FParse::Param(FCommandLine::Get(), TEXT("BreachSimpleBench")))
	{
		AddInfo(TEXT("Bench.Simple requires a dedicated process started with -BreachSimpleBench; skipped."));
		return true;
	}
	FString Label = TEXT("run");
	FParse::Value(FCommandLine::Get(), TEXT("BreachBenchLabel="), Label);

	FBreachSimpleBuildPolicy Policy;
	Policy.WorldScale = FVector2d(8, 5.4);
	Policy.AreaThreshold = UBreachSystemSettings::GetComplexHoleSizeThreshold();
	Policy.MaxCount = UBreachSystemSettings::GetMaxSimpleConvexCount();
	const TArray<FBenchShot> Shots = BenchShots(Policy.WorldScale);
	TArray<FBenchRow> Rows;
	Rows.SetNum(Shots.Num());

	// 준비: 시간 계측용 TSC 주기와 타이머 자체의 비용을 잰다.
	const uint64 CycleStart = __rdtsc();
	const double SecondStart = FPlatformTime::Seconds();
	FPlatformProcess::Sleep(0.05f);
	const double CyclesPerMs = static_cast<double>(__rdtsc() - CycleStart) / ((FPlatformTime::Seconds() - SecondStart) * 1000);
	uint64 TimerSum = 0;
	for (int32 Index = 0; Index < 100000; ++Index)
	{
		const uint64 Start = __rdtsc();
		TimerSum += __rdtsc() - Start;
	}
	const double TimerCycles = static_cast<double>(TimerSum) / 100000;

	FBenchState& State = BenchState();
	static const int32 Anchor = 0;
	const bool bBreachFound = BenchFindModule(reinterpret_cast<uint64>(&Anchor), State.BreachBegin, State.BreachEnd);
	const bool bCoreFound = BenchFindModule(reinterpret_cast<uint64>(&UE::Private::GMalloc), State.CoreBegin, State.CoreEnd);
	if (!TestTrue(TEXT("Bench resolves the plugin and Core module ranges"), bBreachFound && bCoreFound))
	{
		return false;
	}

	// 처리 1: 프록시 없이 구간별 시간을 5회 재고 최솟값을 쓴다.
	for (int32 Repeat = 0; Repeat < 5; ++Repeat)
	{
		if (!BenchReplay(*this, EBenchMode::Off, Shots, Policy, CyclesPerMs, TimerCycles, Rows))
		{
			return false;
		}
	}

	// 처리 2: 할당 횟수·바이트·호출 위치, 그리고 할당기 안에서 보낸 시간.
	// -BreachBenchPlainOnly는 CPU 트레이스에 계측 왜곡이 섞이지 않도록 이 단계를 건너뛴다.
	int64 ResidentBytes = -1;
	if (!FParse::Param(FCommandLine::Get(), TEXT("BreachBenchPlainOnly")))
	{
		FBenchMallocScope MallocScope;
		if (!BenchReplay(*this, EBenchMode::Count, Shots, Policy, CyclesPerMs, TimerCycles, Rows, &ResidentBytes))
		{
			return false;
		}
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			if (!BenchReplay(*this, EBenchMode::Time, Shots, Policy, CyclesPerMs, TimerCycles, Rows))
			{
				return false;
			}
		}
	}

	// 처리 3: 실제 컴포넌트 경로. 완료 통계 로그를 켜고 3회 재서 최솟값을 쓴다.
	TArray<FBenchCookRow> CookRows;
	check(GEngine);
	GEngine->Exec(nullptr, TEXT("Log LogBreachableSurface Verbose"));
	for (int32 Repeat = 0; Repeat < 3; ++Repeat)
	{
		if (!BenchComponent(*this, Shots, Policy.WorldScale, Rows, Repeat == 0 ? &CookRows : nullptr))
		{
			break;
		}
	}
	GEngine->Exec(nullptr, TEXT("Log LogBreachableSurface Log"));

	// 기록
	FString Csv = TEXT("step,phase,cutterVerts,applyMs,copyMs,buildMs,commitMs,booleans,prepared,reusedBodies,changed,pieces,convex,");
	for (const TCHAR* Section : {TEXT("apply"), TEXT("copy"), TEXT("build"), TEXT("commit")})
	{
		Csv += FString::Printf(TEXT("%sDirectCalls,%sDirectBytes,%sEngineCalls,%sEngineBytes,%sOtherCalls,%sFrees,%sAllocMs,"), Section, Section, Section, Section, Section, Section, Section);
	}
	Csv += TEXT("buildPeakBytes,stepRetainedBytes,cutAccepted,cutMs,asyncGeometryMs,asyncAdoptMs,asyncCookMs,asyncLatencyMs,asyncChanged\n");
	auto Finite = [](double Value) { return Value == MAX_dbl ? -1.0 : Value; };
	for (int32 Step = 0; Step < Shots.Num(); ++Step)
	{
		const FBenchRow& Row = Rows[Step];
		Csv += FString::Printf(TEXT("%d,%s,%d,%.5f,%.5f,%.5f,%.5f,%llu,%d,%d,%d,%d,%d,"), Step, Shots[Step].Phase, Shots[Step].Cutter.VertexCount(),
			Row.Ms[BenchApply], Row.Ms[BenchCopy], Row.Ms[BenchBuild], Row.Ms[BenchCommit], Row.Stats.BooleanOperations, Row.Stats.PreparedOpenings, Row.Stats.ReusedBodies, Row.Stats.bCollisionChanged ? 1 : 0, Row.Stats.PassagePieceCount, Row.ConvexCount);
		int64 Retained = 0;
		for (int32 Section = 0; Section < BenchSectionCount; ++Section)
		{
			const FBenchAllocSample& Sample = Row.Allocs[Section];
			Retained += Sample.Net;
			Csv += FString::Printf(TEXT("%llu,%llu,%llu,%llu,%llu,%llu,%.5f,"), Sample.Calls[BenchDirect], Sample.Bytes[BenchDirect], Sample.Calls[BenchViaEngine], Sample.Bytes[BenchViaEngine], Sample.Calls[BenchUnattributed], Sample.Frees, Finite(Row.AllocMs[Section]));
		}
		Csv += FString::Printf(TEXT("%lld,%lld,%d,%.5f,%.5f,%.5f,%.5f,%.5f,%d\n"), Row.Allocs[BenchBuild].Peak, Retained, Row.bCutAccepted ? 1 : 0, Finite(Row.CutMs), Finite(Row.GeometryMs), Finite(Row.AdoptMs), Finite(Row.CookMs), Finite(Row.LatencyMs), Row.CookChanged);
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Benchmarks");
	FFileHelper::SaveStringToFile(Csv, *(Directory / FString::Printf(TEXT("SimpleBench-%s.csv"), *Label)));

	FString CookCsv = TEXT("step,phase,convex,triangles,bothMs,complexOnlyMs\n");
	for (const FBenchCookRow& Cook : CookRows)
	{
		CookCsv += FString::Printf(TEXT("%d,%s,%d,%d,%.5f,%.5f\n"), Cook.Step, *Cook.Phase, Cook.ConvexCount, Cook.Triangles, Cook.BothMs, Cook.ComplexOnlyMs);
	}
	FFileHelper::SaveStringToFile(CookCsv, *(Directory / FString::Printf(TEXT("SimpleBench-%s-cook.csv"), *Label)));

	FString SiteCsv = TEXT("class,calls,bytes,function,file,line\n");
	for (int32 Table = 0; Table < 2; ++Table)
	{
		TArray<const FBenchSite*> Used;
		for (const FBenchSite& Site : State.Sites[Table])
		{
			if (Site.Key.load() != 0)
			{
				Used.Add(&Site);
			}
		}
		Used.Sort([](const FBenchSite& A, const FBenchSite& B) { return A.Calls.load() > B.Calls.load(); });
		for (int32 Index = 0; Index < FMath::Min(40, Used.Num()); ++Index)
		{
			FProgramCounterSymbolInfo Info;
			FPlatformStackWalk::ProgramCounterToSymbolInfo(Used[Index]->Key.load() - 1, Info);
			SiteCsv += FString::Printf(TEXT("%s,%llu,%llu,\"%s\",\"%s\",%d\n"), Table == 0 ? TEXT("direct") : TEXT("viaEngine"), Used[Index]->Calls.load(), Used[Index]->Bytes.load(), ANSI_TO_TCHAR(Info.FunctionName), ANSI_TO_TCHAR(Info.Filename), Info.LineNumber);
		}
	}
	FFileHelper::SaveStringToFile(SiteCsv, *(Directory / FString::Printf(TEXT("SimpleBench-%s-sites.csv"), *Label)));

	FString Meta = FString::Printf(TEXT("label=%s\nshots=%d\nthreshold=%.3f\nmaxCount=%d\nworldScale=%.3f,%.3f\ncyclesPerMs=%.1f\ntimerCycles=%.2f\nresidentBytes=%lld\n"), *Label, Shots.Num(), Policy.AreaThreshold, Policy.MaxCount, Policy.WorldScale.X, Policy.WorldScale.Y, CyclesPerMs, TimerCycles, ResidentBytes);
	for (const SIZE_T Size : {SIZE_T(16), SIZE_T(64), SIZE_T(256), SIZE_T(1024), SIZE_T(4096)})
	{
		Meta += FString::Printf(TEXT("allocPairNs_%llu=%.2f\nallocBurst32Ns_%llu=%.2f\n"), static_cast<uint64>(Size), BenchAllocPairNs(Size, 1), static_cast<uint64>(Size), BenchAllocPairNs(Size, 32));
	}
	FFileHelper::SaveStringToFile(Meta, *(Directory / FString::Printf(TEXT("SimpleBench-%s-meta.txt"), *Label)));
	AddInfo(FString::Printf(TEXT("Simple bench '%s' wrote %d steps to %s."), *Label, Shots.Num(), *Directory));
	return !HasAnyErrors();
}
#endif
