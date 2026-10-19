// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#include "Net/UnrealNetwork.h"

AShooterWeapon::AShooterWeapon()
{
	bReplicates = true;

	PrimaryActorTick.bCanEverTick = true;
	HitDamageType = UDamageType::StaticClass();

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// subscribe to the owner's destroyed delegate
	GetOwner()->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// attach the meshes to the owner
	WeaponOwner->AttachWeaponMeshes(this);
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	// raise the firing flag
	bIsFiring = true;

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		// fire the weapon right away
		Fire();

	} 
	else 
	{

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);
		}

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	if (!bIsFiring)
	{
		return;
	}
	if (!PawnOwner)
	{
		return;
	}
	
	// 사격 방향 (우선 플레이어만 신경써보자)
	FVector TraceStart;
	FVector ShotDirection;
	if (APlayerController* PlayerController = Cast<APlayerController>(PawnOwner->GetController()))
	{
		int32 Width = 0;
		int32 Height = 0;
		TraceStart = PlayerController->PlayerCameraManager->GetCameraLocation();
		ShotDirection = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
		ShotDirection.Normalize();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AShooterWeapon::Fire No PC: %s, Authority=%d"), *GetName(), HasAuthority());
	}

	// 실 사격
	FHitResult HitResult;
	if (!FireShot(HitResult, TraceStart, ShotDirection))
	{
		return;
	}
	ServerNotifyHit(HitResult, ShotDirection);

	//UE_LOG(LogTemp, Log, TEXT("FireShot Success"));
	MulticastPlayFiringMontage();
	ClientApplyRecoil();

	--CurrentBullets;
	if (CurrentBullets <= 0)
	{
		CurrentBullets = MagazineSize;
	}
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	if (bFullAuto)
	{
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RefireRate, false);
	} 
	else 
	{
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, RefireRate, false);
	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}


void AShooterWeapon::ClientApplyRecoil_Implementation()
{
	if (WeaponOwner)
	{
		WeaponOwner->AddWeaponRecoil(FiringRecoil);
	}
}

void AShooterWeapon::MulticastPlayFiringMontage_Implementation()
{
	// 서버에서는 애니메이션 재생이 필요가 없다. // 멀티캐스트니까 분기분 걸어준다.
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (WeaponOwner)
		{
			WeaponOwner->PlayFiringMontage(FiringMontage);
		}
	}
}

void AShooterWeapon::ServerNotifyHit_Implementation(const FHitResult& InHitResult, const FVector& InTraceStart)
{
	//UE_LOG(LogTemp, Log, TEXT("ServerNotifyHit"));
	if (InHitResult.bBlockingHit)
	{
		UGameplayStatics::ApplyPointDamage(InHitResult.GetActor(), HitDamage, InTraceStart,
			InHitResult, PawnOwner->GetController(), this, HitDamageType);
	}
}

void AShooterWeapon::OnRep_CurrentBullet()
{
	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AShooterWeapon, CurrentBullets, COND_OwnerOnly);
}
