// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "HitScanWeapon.generated.h"

/** Weapon that immediately damages the first blocking hit along the view. */
UCLASS(abstract)
class BREACHTEST_API AHitScanWeapon : public AShooterWeapon
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category="HitScan", meta = (ClampMin = 0, Units = "cm"))
	float TraceRange = 10000.0f;

	/** Radius of the swept sphere. Zero uses a line trace. */
	UPROPERTY(EditAnywhere, Category="HitScan", meta = (ClampMin = 0, Units = "cm"))
	float TraceRadius = 16.0f;

	virtual bool FireShot(FHitResult& OutHitResult, const FVector& InTraceStart, const FVector& InShotDirection) override;

	/** Impact effects receive the trace result without a projectile actor. */
	UFUNCTION(BlueprintImplementableEvent, Category="HitScan", meta = (DisplayName = "On HitScan Hit"))
	void BP_OnHitScanHit(const FHitResult& Hit);
};
