#include "Misc/AutomationTest.h"

// 테스트 전용: LV_Test를 저장하지 않고 연 PIE에서 나무 벽을 실제 권총 커터 설정으로 연사해, 피격 처리 시간과 그 뒤 프레임 시간을 기록한다.
// -BreachWoodBench가 있을 때만 실행한다. -trace=cpu와 함께 돌리면 단계별 시간은 Insights로 내보낸다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Components/BreachableSurfaceComponent.h"
#include "Components/BreachSourceComponent.h"
#include "Components/BreachSurfaceProxy.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DamageEvents.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Settings/BreachSystemSettings.h"
#include "Surface/BreachPattern.h"
#include "Surface/BreachSurfaceMaterial.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"

namespace
{
	FString WoodBenchProperties(const UObject& Object, const UClass& Declared, TConstArrayView<FName> Skip = {})
	{
		FString Text;
		for (TFieldIterator<FProperty> It(Object.GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit) || It->GetOwnerClass() != &Declared || Skip.Contains(It->GetFName()))
			{
				continue;
			}
			FString Value;
			It->ExportTextItem_InContainer(Value, &Object, nullptr, nullptr, PPF_None);
			Text += FString::Printf(TEXT(" %s=%s"), *It->GetName(), *Value);
		}
		return Text;
	}

	// 누적 피해(BreachDamage)가 있는 실제 무기는 HitScanPistol뿐이다. 총알 투사체는 BreachDamage가 0이라 누적에 들어가지 않는다.
	const UBreachSourceComponent* WoodBenchSourceTemplate()
	{
		const UBlueprintGeneratedClass* Class = LoadObject<UBlueprintGeneratedClass>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_HitScanPistol.BP_ShooterWeapon_HitScanPistol_C"));
		for (const UBlueprintGeneratedClass* It = Class; It; It = Cast<UBlueprintGeneratedClass>(It->GetSuperClass()))
		{
			if (!It->SimpleConstructionScript)
			{
				continue;
			}
			for (const USCS_Node* Node : It->SimpleConstructionScript->GetAllNodes())
			{
				if (const UBreachSourceComponent* Source = Cast<UBreachSourceComponent>(Node->ComponentTemplate))
				{
					return Source;
				}
			}
		}
		return nullptr;
	}

	class FWoodAccumulateBench final : public IAutomationLatentCommand
	{
	public:
		explicit FWoodAccumulateBench(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("BreachWoodShots="), ShotCount);
			FParse::Value(FCommandLine::Get(), TEXT("BreachWoodInterval="), Interval);
			FParse::Value(FCommandLine::Get(), TEXT("BreachWoodSpread="), Spread);
			FParse::Value(FCommandLine::Get(), TEXT("BreachWoodAimX="), AimX);
			FParse::Value(FCommandLine::Get(), TEXT("BreachWoodAimZ="), AimZ);
		}

		virtual bool Update() override
		{
			check(Test);
			if (!GEditor || !GEditor->PlayWorld)
			{
				Test->AddError(TEXT("Wood accumulate bench requires its disposable PIE world."));
				return true;
			}
			UWorld& World = *GEditor->PlayWorld;
			const double Now = FPlatformTime::Seconds();
			// 피격은 Update 안에서 일어나므로, 그 프레임 길이는 다음 Update에서 잰다.
			if (LastFrame > 0 && bReady)
			{
				const double FrameMs = (Now - LastFrame) * 1000;
				if (Shots.IsEmpty())
				{
					IdleFrames.Add(FrameMs);
				}
				else
				{
					Shots.Last().MaxFrameMs = FMath::Max(Shots.Last().MaxFrameMs, FrameMs);
					++Shots.Last().Frames;
				}
			}
			LastFrame = Now;
			if (++WaitedFrames < 30)
			{
				return false;
			}
			if (!bReady)
			{
				if (!Setup(World))
				{
					return true;
				}
				bReady = true;
				NextShot = Now + 1;
				return false;
			}
			if (Shots.Num() < ShotCount)
			{
				if (Now >= NextShot)
				{
					Fire();
					NextShot = Now + Interval;
				}
				return false;
			}
			if (FinishAt == 0)
			{
				FinishAt = Now + 2;
			}
			if (Now < FinishAt)
			{
				return false;
			}
			Finish();
			return true;
		}

	private:
		struct FShot
		{
			int32 Index = 0;
			FVector2d Offset = FVector2d::ZeroVector;
			bool bHit = false;
			double HandleMs = 0;
			int32 Before = 0;
			int32 After = 0;
			int32 Triangles = 0;
			double MaxFrameMs = 0;
			int32 Frames = 0;

			bool Consumed() const
			{
				return bHit && After <= Before;
			}
		};

		bool Setup(UWorld& World)
		{
			const APlayerStart* Start = nullptr;
			for (TActorIterator<APlayerStart> It(&World); It; ++It)
			{
				Start = *It;
				break;
			}
			const FVector StartLocation = Start ? Start->GetActorLocation() : FVector::ZeroVector;
			const FVector Right = Start ? Start->GetActorRotation().RotateVector(FVector::RightVector) : FVector::RightVector;
			Test->AddInfo(FString::Printf(TEXT("PlayerStart location=%s rotation=%s"), *StartLocation.ToCompactString(), Start ? *Start->GetActorRotation().ToCompactString() : TEXT("none")));

			FString Wanted;
			FParse::Value(FCommandLine::Get(), TEXT("BreachWoodWall="), Wanted);
			UBreachableSurfaceComponent* Chosen = nullptr;
			double ChosenRight = TNumericLimits<double>::Max();
			for (TActorIterator<AActor> It(&World); It; ++It)
			{
				TInlineComponentArray<UBreachableSurfaceComponent*> Surfaces(*It);
				for (UBreachableSurfaceComponent* Candidate : Surfaces)
				{
					const UBreachSurfaceMaterial* Material = Candidate->GetSurfaceMaterial();
					const FVector Location = Candidate->Bounds.Origin;
					// 플레이어 시작 시점의 오른쪽 방향 거리다. 작을수록 왼쪽이다.
					const double RightDistance = (Location - StartLocation).Dot(Right);
					Test->AddInfo(FString::Printf(TEXT("Wall %s material=%s center=%s rotation=%s scale=%s mesh=%s right=%.1f"), *It->GetActorLabel(),
						Material ? *Material->GetName() : TEXT("None"), *Location.ToCompactString(), *Candidate->GetComponentRotation().ToCompactString(),
						*Candidate->GetComponentScale().ToCompactString(), Candidate->GetStaticMesh() ? *Candidate->GetStaticMesh()->GetName() : TEXT("None"), RightDistance));
					const bool bWood = Material && Material->GetName().Contains(TEXT("Wood"));
					if (Wanted.IsEmpty() ? (bWood && RightDistance < ChosenRight) : It->GetActorLabel() == Wanted)
					{
						Chosen = Candidate;
						ChosenRight = RightDistance;
					}
				}
			}
			if (!Test->TestNotNull(TEXT("LV_Test has a wood breachable wall"), Chosen) || !Test->TestNotNull(TEXT("Chosen wall mesh"), static_cast<const UStaticMesh*>(Chosen->GetStaticMesh())))
			{
				return false;
			}
			Surface = Chosen;
			Test->AddInfo(FString::Printf(TEXT("Target wall %s"), *Chosen->GetOwner()->GetActorLabel()));

			// 비교용: 에셋은 그대로 두고 이 PIE 벽에만 복제 재질로 누적 여유 폭을 바꾼다.
			double Padding = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("BreachWoodPadding="), Padding))
			{
				UBreachSurfaceMaterial* Copy = DuplicateObject<UBreachSurfaceMaterial>(Chosen->GetSurfaceMaterial(), Chosen->GetOwner());
				Copy->AccumulationShape.Padding = Padding;
				Chosen->SetSurfaceMaterial(Copy);
			}
			const UBreachSurfaceMaterial* Material = Chosen->GetSurfaceMaterial();
			Test->AddInfo(FString::Printf(TEXT("Material %s%s"), *Material->GetName(), *WoodBenchProperties(*Material, *UBreachSurfaceMaterial::StaticClass())));
			if (const UBreachPattern* Pattern = Material->Pattern.Get())
			{
				int32 Points = 0;
				for (const FBreachPatternGuide& Guide : Pattern->Guides)
				{
					Points += Guide.Points.Num();
				}
				Test->AddInfo(FString::Printf(TEXT("Pattern %s guides=%d guidePoints=%d regions=%d%s"), *Pattern->GetName(), Pattern->Guides.Num(), Points, Pattern->Regions.Num(),
					*WoodBenchProperties(*Pattern, *UBreachPattern::StaticClass(), {TEXT("Guides"), TEXT("Regions")})));
			}
			Test->AddInfo(FString::Printf(TEXT("Settings%s"), *WoodBenchProperties(*GetDefault<UBreachSystemSettings>(), *UBreachSystemSettings::StaticClass())));

			const UBreachSourceComponent* Template = WoodBenchSourceTemplate();
			if (!Test->TestNotNull(TEXT("HitScanPistol BreachSource template"), Template))
			{
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("Source%s"), *WoodBenchProperties(*Template, *UBreachSourceComponent::StaticClass())));
			AActor* Shooter = World.SpawnActor<AActor>();
			check(Shooter);
			UBreachSourceComponent* Source = NewObject<UBreachSourceComponent>(Shooter, NAME_None, RF_NoFlags, const_cast<UBreachSourceComponent*>(Template));
			Shooter->AddInstanceComponent(Source);
			Source->RegisterComponent();
			Gun = Shooter;

			// 플레이어가 선 쪽 면의 중앙을 겨눈다.
			const FTransform& Transform = Chosen->GetComponentTransform();
			const FBox Box = Chosen->GetStaticMesh()->GetBoundingBox();
			const bool bFront = Transform.InverseTransformPosition(StartLocation).Y < Box.GetCenter().Y;
			AxisX = Transform.GetUnitAxis(EAxis::X);
			AxisZ = Transform.GetUnitAxis(EAxis::Z);
			Normal = Transform.TransformVectorNoScale(FVector(0, bFront ? -1 : 1, 0)).GetSafeNormal();
			Center = Transform.TransformPosition(FVector(Box.GetCenter().X, bFront ? Box.Min.Y : Box.Max.Y, Box.GetCenter().Z)) + AxisX * AimX + AxisZ * AimZ;
			Test->AddInfo(FString::Printf(TEXT("Aim center=%s normal=%s side=%s shots=%d interval=%.3fs spread=%.1fcm"), *Center.ToCompactString(), *Normal.ToCompactString(),
				bFront ? TEXT("front") : TEXT("back"), ShotCount, Interval, Spread));
			return true;
		}

		void Fire()
		{
			UBreachableSurfaceComponent* Wall = Surface.Get();
			check(Wall);
			FShot& Shot = Shots.AddDefaulted_GetRef();
			Shot.Index = Shots.Num() - 1;
			const double Angle = Random.FRand() * 2 * UE_DOUBLE_PI;
			Shot.Offset = FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)) * Spread * FMath::Sqrt(Random.FRand());
			const FVector Aim = Center + AxisX * Shot.Offset.X + AxisZ * Shot.Offset.Y;

			// 실제 총알처럼 이미 뚫린 자리는 지나간다.
			UBreachSurfaceProxy* Proxy = Wall->GetOwner()->FindComponentByClass<UBreachSurfaceProxy>();
			UPrimitiveComponent* Target = IsValid(Proxy) && Proxy->IsRegistered() ? static_cast<UPrimitiveComponent*>(Proxy) : Wall;
			FHitResult Hit;
			Shot.bHit = Target->LineTraceComponent(Hit, Aim + Normal * 20, Aim - Normal * 20, FCollisionQueryParams(SCENE_QUERY_STAT(WoodAccumulateBench), true));
			Shot.Before = Wall->GetAccumulatedHitCount();
			TRACE_BOOKMARK(TEXT("WoodShot %d"), Shot.Index);
			if (Shot.bHit)
			{
				FPointDamageEvent Damage;
				Damage.Damage = 1;
				Damage.HitInfo = Hit;
				Damage.HitInfo.Component = Target;
				Damage.ShotDirection = -Normal;
				const double Started = FPlatformTime::Seconds();
				Wall->HandleBreachDamage(Target, Damage, Gun.Get(), 1);
				Shot.HandleMs = (FPlatformTime::Seconds() - Started) * 1000;
			}
			Shot.After = Wall->GetAccumulatedHitCount();
			Proxy = Wall->GetOwner()->FindComponentByClass<UBreachSurfaceProxy>();
			Shot.Triangles = IsValid(Proxy) && Proxy->GetMesh() ? Proxy->GetMesh()->TriangleCount() : 0;
		}

		void Finish()
		{
			FString Csv = TEXT("Shot,OffsetX,OffsetZ,Hit,Consumed,HandleMs,RecordsBefore,RecordsAfter,Triangles,MaxFrameMs,Frames\n");
			double ConsumedMax = 0, ConsumedSum = 0, DirectMax = 0, DirectSum = 0, ConsumedFrameMax = 0, DirectFrameMax = 0;
			int32 ConsumedCount = 0, DirectCount = 0;
			for (const FShot& Shot : Shots)
			{
				Csv += FString::Printf(TEXT("%d,%.2f,%.2f,%d,%d,%.3f,%d,%d,%d,%.3f,%d\n"), Shot.Index, Shot.Offset.X, Shot.Offset.Y, Shot.bHit, Shot.Consumed(), Shot.HandleMs,
					Shot.Before, Shot.After, Shot.Triangles, Shot.MaxFrameMs, Shot.Frames);
				if (!Shot.bHit)
				{
					continue;
				}
				if (Shot.Consumed())
				{
					++ConsumedCount;
					ConsumedSum += Shot.HandleMs;
					ConsumedMax = FMath::Max(ConsumedMax, Shot.HandleMs);
					ConsumedFrameMax = FMath::Max(ConsumedFrameMax, Shot.MaxFrameMs);
					Test->AddInfo(FString::Printf(TEXT("Accumulated shot %d handle=%.3fms maxFrame=%.3fms triangles=%d records %d->%d"), Shot.Index, Shot.HandleMs, Shot.MaxFrameMs, Shot.Triangles, Shot.Before, Shot.After));
				}
				else
				{
					++DirectCount;
					DirectSum += Shot.HandleMs;
					DirectMax = FMath::Max(DirectMax, Shot.HandleMs);
					DirectFrameMax = FMath::Max(DirectFrameMax, Shot.MaxFrameMs);
				}
			}
			TArray<double> Sorted = IdleFrames;
			Sorted.Sort();
			const double IdleMedian = Sorted.IsEmpty() ? 0 : Sorted[Sorted.Num() / 2];
			Test->AddInfo(FString::Printf(TEXT("Idle frame median=%.3fms over %d frames"), IdleMedian, Sorted.Num()));
			Test->AddInfo(FString::Printf(TEXT("Accumulated hits=%d handle avg=%.3fms max=%.3fms maxFrame=%.3fms"), ConsumedCount, ConsumedCount ? ConsumedSum / ConsumedCount : 0, ConsumedMax, ConsumedFrameMax));
			Test->AddInfo(FString::Printf(TEXT("Other hits=%d handle avg=%.3fms max=%.3fms maxFrame=%.3fms"), DirectCount, DirectCount ? DirectSum / DirectCount : 0, DirectMax, DirectFrameMax));
			Test->TestTrue(TEXT("At least one accumulated breach occurred"), ConsumedCount > 0);

			FString Label = TEXT("run");
			FParse::Value(FCommandLine::Get(), TEXT("BreachBenchLabel="), Label);
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Benchmarks");
			IFileManager::Get().MakeDirectory(*Directory, true);
			const FString Path = Directory / FString::Printf(TEXT("WoodAccumulate-%s.csv"), *Label);
			Test->TestTrue(TEXT("Save wood accumulate bench rows outside Content"), FFileHelper::SaveStringToFile(Csv, *Path));
			Test->AddInfo(FString::Printf(TEXT("Wood accumulate rows: %s"), *Path));
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<UBreachableSurfaceComponent> Surface;
		TWeakObjectPtr<AActor> Gun;
		FVector Center = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		FVector AxisX = FVector::ZeroVector;
		FVector AxisZ = FVector::ZeroVector;
		FRandomStream Random{7};
		TArray<FShot> Shots;
		TArray<double> IdleFrames;
		int32 ShotCount = 40;
		double Interval = .15;
		double Spread = 6;
		double AimX = 0;
		double AimZ = 0;
		int32 WaitedFrames = 0;
		double LastFrame = 0;
		double NextShot = 0;
		double FinishAt = 0;
		bool bReady = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachWoodAccumulateBenchTest, "BreachSystem.Bench.WoodAccumulate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachWoodAccumulateBenchTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("BreachWoodBench")))
	{
		AddInfo(TEXT("Wood accumulate bench runs only with -BreachWoodBench."));
		return true;
	}
	if (!GEditor || !FApp::IsUnattended() || GEditor->PlayWorld)
	{
		AddWarning(TEXT("Wood accumulate bench requires a separate unattended editor without an active PIE; no map was loaded."));
		return true;
	}
	// 맵은 이 테스트 프로세스의 메모리에서만 연다. 저장하지 않는다.
	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/Level/LV_Test"));
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::SimulateInEditor;
	Params.bAllowOnlineSubsystem = false;
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIEForAutomationCommand(Params));
	ADD_LATENT_AUTOMATION_COMMAND(FWoodAccumulateBench(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
