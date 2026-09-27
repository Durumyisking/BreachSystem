// Copyright Epic Games, Inc. All Rights Reserved.

#include "HitScanWeapon.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "KismetTraceUtils.h"

bool AHitScanWeapon::FireShot()
{
	FVector TraceStart;
	FVector ShotDirection;
	if (APlayerController* PlayerController = Cast<APlayerController>(PawnOwner->GetController()))
	{
		int32 Width = 0;
		int32 Height = 0;
		PlayerController->GetViewportSize(Width, Height);
		if (Width <= 0 || Height <= 0 || !PlayerController->DeprojectScreenPositionToWorld(
			Width * 0.5f, Height * 0.5f, TraceStart, ShotDirection))
		{
			return false;
		}
	}
	else
	{
		// AI has no screen; retain the weapon holder's target selection.
		TraceStart = PawnOwner->GetPawnViewLocation();
		ShotDirection = (WeaponOwner->GetWeaponTargetLocation() - TraceStart).GetSafeNormal();
	}

	const FVector TraceEnd = TraceStart + ShotDirection * TraceRange;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HitScanWeapon), true);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	// Projectile object channel defined in DefaultEngine.ini; use the target's response to it.
	constexpr ECollisionChannel ShotChannel = ECC_GameTraceChannel1;
	const bool bBlockingHit = TraceRadius > 0.0f
		? GetWorld()->SweepSingleByChannel(Hit, TraceStart, TraceEnd, FQuat::Identity,
			ShotChannel, FCollisionShape::MakeSphere(TraceRadius), QueryParams)
		: GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ShotChannel, QueryParams);
#if ENABLE_DRAW_DEBUG
	if (TraceRadius > 0.0f)
	{
		DrawDebugSphereTraceSingle(GetWorld(), TraceStart, TraceEnd, TraceRadius,
			EDrawDebugTrace::ForDuration, bBlockingHit, Hit, FLinearColor::Red, FLinearColor::Green, 2.0f);
	}
	else
	{
		DrawDebugLineTraceSingle(GetWorld(), TraceStart, TraceEnd,
			EDrawDebugTrace::ForDuration, bBlockingHit, Hit, FLinearColor::Red, FLinearColor::Green, 2.0f);
	}
#endif

	if (bBlockingHit)
	{
		UGameplayStatics::ApplyPointDamage(Hit.GetActor(), HitDamage, ShotDirection,
			Hit, PawnOwner->GetController(), this, HitDamageType);
		BP_OnHitScanHit(Hit);
	}

	// A miss still consumes a round and plays firing effects.
	return true;
}
