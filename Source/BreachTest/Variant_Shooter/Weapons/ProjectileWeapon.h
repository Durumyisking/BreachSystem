// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ProjectileWeapon.generated.h"

class AShooterProjectile;

/** Weapon that delivers damage through a moving projectile. */
UCLASS(abstract)
class BREACHTEST_API AProjectileWeapon : public AShooterWeapon
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category="Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float AimVariance = 0.0f;

	UPROPERTY(EditAnywhere, Category="Aim")
	FName MuzzleSocketName;

	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float MuzzleOffset = 10.0f;

	virtual bool FireShot(FHitResult& OutHitResult, const FVector& InTraceStart, const FVector& InShotDirection) override;
	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation) const;
};
