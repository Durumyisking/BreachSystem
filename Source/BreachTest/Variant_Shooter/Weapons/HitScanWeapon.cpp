// Copyright Epic Games, Inc. All Rights Reserved.

#include "HitScanWeapon.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "KismetTraceUtils.h"

bool AHitScanWeapon::FireShot(FHitResult& OutHitResult, const FVector& InTraceStart, const FVector& InShotDirection)
{
	const FVector TraceEnd = InTraceStart + InShotDirection * TraceRange;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HitScanWeapon), true);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());

	// Projectile object channel defined in DefaultEngine.ini; use the target's response to it.
	constexpr ECollisionChannel ShotChannel = ECC_GameTraceChannel1;
	const bool bBlockingHit = TraceRadius > 0.0f
		? GetWorld()->SweepSingleByChannel(OutHitResult, InTraceStart, TraceEnd, FQuat::Identity,
			ShotChannel, FCollisionShape::MakeSphere(TraceRadius), QueryParams)
		: GetWorld()->LineTraceSingleByChannel(OutHitResult, InTraceStart, TraceEnd, ShotChannel, QueryParams);
//#if ENABLE_DRAW_DEBUG
//	if (TraceRadius > 0.0f)
//	{
//		DrawDebugSphereTraceSingle(GetWorld(), InTraceStart, TraceEnd, TraceRadius,
//			EDrawDebugTrace::ForDuration, bBlockingHit, OutHitResult, FLinearColor::Red, FLinearColor::Green, 2.0f);
//	}
//	else
//	{
//		DrawDebugLineTraceSingle(GetWorld(), InTraceStart, TraceEnd,
//			EDrawDebugTrace::ForDuration, bBlockingHit, OutHitResult, FLinearColor::Red, FLinearColor::Green, 2.0f);
//	}
//#endif

	// A miss still consumes a round and plays firing effects.
	return true;
}
