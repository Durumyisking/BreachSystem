
![[Pasted image 20260930233419.png|794]]


---

언리얼 샘플FPS  플러그인의 코드들을 Dedicated서버 환경으로 옮긴다.
![[네트워크 구현 - 사격처리1.mkv]]
![[네트워크 구현 - 템플릿 기본 기능 이전 완료.mkv]]

### 사격 -> 사망 프로세스 이전

기존 사격 프로세스 : 사용자 입력 -> 무기에게 사격 명령 -> 무기가 사격 수행 -> UI갱신/애니메이션 재생/판정(히트스캔일시)
일련의 과정이 전부 클라이언트에서 수행된다.
#### 책임 분리

**입력(Client)**
- 입력은 클라가 하는거니까 당연히 클라에서 받는다.
```cpp
EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);
void AShooterCharacter::DoStartFiring()
{
	ServerStartFiring();
}
void AShooterCharacter::DoStopFiring()
{
	ServerStopFiring();
}
```

**무기에게 사격 명령(Server)**
- 총을 발사하는 로직은 서버에서 제어한다. (공격속도 처리등을 안전하게 하기 위함)
```cpp
UFUNCTION(Server, Reliable) // Server객체에서만 호출된다.
void ServerStartFiring();
UFUNCTION(Server, Reliable)
void ServerStopFiring();

void AShooterCharacter::ServerStartFiring_Implementation()
{
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring();
	}
}
void AShooterCharacter::ServerStopFiring_Implementation()
{
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StopFiring();
	}
}
void AShooterWeapon::StartFiring()
{
	bIsFiring = true;

	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		Fire();
	} 
	else 
	{
		if (bFullAuto)
		{
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);
		}
	}
}

void AShooterWeapon::StopFiring()
{
	bIsFiring = false;

	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}
```

**무기가 사격 수행(Server)**
- 언제 어떻게 사격을 하냐는 서버에서 처리한다.
- 하지만 서버는 애니메이션 재생, 비주얼 리코일, UI갱신등을 처리할 필요가 없다.
- 몽타주는 모든 클라이언트들에게 재생되어야 하니까 NetMulticast로 실행한다.

```cpp
void AShooterWeapon::Fire()
{
	if (!bIsFiring)
	{
		return;
	}

	if (!FireShot()) // 실제 사격 판정
	{
		return;
	}
	ServerNotifyHit(HitResult, ShotDirection); // 위에 분기 통과했으니까 명중 성공임
	/* 
	* 내부에서 데미지
	* UGameplayStatics::ApplyPointDamage(InHitResult.GetActor(), HitDamage, InTraceStart,
			InHitResult, PawnOwner->GetController(), this, HitDamageType);
	*/
	
	MulticastPlayFiringMontage(); // 애니메이션
	ClientApplyRecoil(); // 반동


	--CurrentBullets; // UI갱신 (ReplicatedUsing = OnRep_CurrentBullet)
	TimeOfLastShot = GetWorld()->GetTimeSeconds(); // 공격속도처리
	...
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
	DOREPLIFETIME_CONDITION(AShooterWeapon, CurrentBullets, COND_OwnerOnly); // Authority가 있는 액터만
}

UPROPERTY(ReplicatedUsing = OnRep_CurrentBullet)
int32 CurrentBullets = 0;
UFUNCTION()
void OnRep_CurrentBullet();

	
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

void AShooterWeapon::OnRep_CurrentBullet() // 얘는 어차피 Owner Client만 실행하니까 별다른 처리는 하지 않는다.
{
	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}
UFUNCTION(Client, Reliable) // 소유한 클라이언트만 실행
void ClientApplyRecoil();
UFUNCTION(NetMulticast, Unreliable) // 서버에서 호출하면 모든 클라가, 클라가 호출하면 클라만
void MulticastPlayFiringMontage();

```

#### 데미지 받기

```cpp
// 서버에서만 수행된다 : ApplyDamage가 서버에서만 수행되기 때문
float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// 우리 게임은 서버에서만 호출된다.
	//UE_LOG(LogTemp, Log, TEXT("TakeDamage"));

	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	return Damage;
}
void AShooterCharacter::OnDeath()
{
	// deactivate the weapon on each machine
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// grant the death tag to the character
	Tags.AddUnique(DeathTag);
		
	// stop character movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	if (IsLocallyControlled())
	{
		// disable controls
		DisableInput(nullptr);

		// reset the bullet counter UI
		OnBulletCountUpdated.Broadcast(0, 0);
	}

	// call the BP handler
	BP_OnDeath();
}

UPROPERTY(ReplicatedUsing = OnRep_CurrentHp, EditAnywhere, Category = "Health")
float CurrentHP = 0.0f;
// CurrentHP는 리플리케이션된다. (당연히 서버 규칙이니까)

void AShooterCharacter::OnRep_CurrentHp()
{
	// 캐릭터 주인 빼고는 UI업데이트를 할 필요없다.
	// update the HUD
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
}
void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AShooterCharacter, CurrentHP, COND_OwnerOnly);
}


```

