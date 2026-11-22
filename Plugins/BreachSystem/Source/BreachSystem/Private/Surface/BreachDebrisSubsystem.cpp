#include "Surface/BreachDebrisSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Surface/BreachSurfaceMaterial.h"

#include "BreachSystemLog.h"

void UBreachDebrisSubsystem::Spawn(const UBreachSurfaceMaterial& Material, const FVector& ImpactPoint,
	const FVector& Outward, double RemovedAreaCm2)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PlanarBreach | Spawn debris");
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!FMath::IsFinite(RemovedAreaCm2) || RemovedAreaCm2 <= 0 || Outward.IsNearlyZero())
	{
		return;
	}

	const double BreachRadius = FMath::Sqrt(RemovedAreaCm2 / UE_DOUBLE_PI);
	const bool bLargeBreach = BreachRadius >= 9.0;
	const bool bHasEffect = IsValid(Material.DebrisEffect);
	if (bHasEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Material.DebrisEffect, ImpactPoint + Outward,
			Outward.Rotation(), FVector::OneVector, true, true, ENCPoolMethod::AutoRelease);
		if (!bLargeBreach)
		{
			return;
		}
	}

	if (Material.DebrisMeshes.IsEmpty())
	{
		return;
	}

	const int32 BaseCount = FMath::CeilToInt(FMath::Clamp(BreachRadius / 4.0, 1.0, 8.0));
	const float Multiplier = FMath::Clamp(Material.DebrisCountMultiplier, 0.f, 4.f);
	const int32 Requested = FMath::RoundToInt(BaseCount * Multiplier);
	if (Requested <= 0)
	{
		return;
	}

	constexpr int32 MaxActivePieces = 64;
	ActivePieces.RemoveAllSwap([](const TWeakObjectPtr<AStaticMeshActor>& Piece) { return !Piece.IsValid(); });
	const int32 Available = MaxActivePieces - ActivePieces.Num();
	if (Available <= 0)
	{
		return;
	}

	TArray<UStaticMesh*, TInlineAllocator<8>> Meshes;
	for (UStaticMesh* Mesh : Material.DebrisMeshes)
	{
		if (!IsValid(Mesh))
		{
			continue;
		}

		const UBodySetup* Body = Mesh->GetBodySetup();
		if (!Body || Body->AggGeom.GetElementCount() == 0 || Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple)
		{
			UE_LOG(LogBreachDebris, Verbose, TEXT("%s skipped debris mesh %s because it has no usable simple collision."),
				*Material.GetName(), *Mesh->GetName());
			continue;
		}
		Meshes.Add(Mesh);
	}
	if (Meshes.IsEmpty())
	{
		return;
	}

	FVector Tangent;
	FVector Bitangent;
	Outward.FindBestAxisVectors(Tangent, Bitangent);
	const int32 Count = FMath::Min(Requested, Available);
	const double Spread = FMath::Min(BreachRadius, 20.0);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMesh* Mesh = Meshes[FMath::RandHelper(Meshes.Num())];
		const double Scale = bLargeBreach ? FMath::FRandRange(0.65, 1.05) : FMath::FRandRange(0.13, 0.28);
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const FRotator Rotation = FMath::VRand().Rotation();
		const FVector Offset = Tangent * FMath::FRandRange(-Spread, Spread) + Bitangent * FMath::FRandRange(-Spread, Spread);
		// 충돌 박스가 처음부터 벽에 끼지 않도록 메시 중심을 표면 밖에 둔다.
		const FVector Center = ImpactPoint + Offset + Outward * (Bounds.BoxExtent.Size() * Scale + 2.0);
		const FVector Location = Center - Rotation.RotateVector(Bounds.Origin * Scale);

		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags |= RF_Transient;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Piece = World->SpawnActor<AStaticMeshActor>(Location, Rotation, Parameters);
		if (!IsValid(Piece))
		{
			continue;
		}

		UStaticMeshComponent* Component = Piece->GetStaticMeshComponent();
		check(Component);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetStaticMesh(Mesh);
		Piece->SetActorScale3D(FVector(Scale));
		Component->SetGenerateOverlapEvents(false);
		Component->SetCollisionObjectType(ECC_PhysicsBody);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Component->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		Component->SetSimulatePhysics(true);

		const FVector Velocity = Outward * FMath::FRandRange(140.0, 260.0) + FMath::VRand() * 80.0 + FVector(0, 0, 65);
		Component->SetPhysicsLinearVelocity(Velocity);
		Component->SetPhysicsAngularVelocityInRadians(FMath::VRand() * FMath::FRandRange(5.0, 12.0));
		Piece->SetLifeSpan(4.f);
		ActivePieces.Add(Piece);
	}
}
