// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectileWeapon.h"
#include "ShooterProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetMathLibrary.h"

bool AProjectileWeapon::FireShot(FHitResult& OutHitResult, const FVector& InTraceStart, const FVector& InShotDirection)
{
	if (!ProjectileClass)
	{
		return false;
	}

	const FTransform SpawnTransform = CalculateProjectileSpawnTransform(WeaponOwner->GetWeaponTargetLocation());
	AShooterProjectile* Projectile = GetWorld()->SpawnActorDeferred<AShooterProjectile>(
		ProjectileClass, SpawnTransform, GetOwner(), PawnOwner,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
	if (!Projectile)
	{
		return false;
	}

	Projectile->InitializeDamage(HitDamage, HitDamageType);
	Projectile->FinishSpawning(SpawnTransform, false, nullptr, ESpawnActorScaleMethod::OverrideRootScale);
	return true;
}

FTransform AProjectileWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	const FVector MuzzleLoc = GetFirstPersonMesh()->GetSocketLocation(MuzzleSocketName);
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}
