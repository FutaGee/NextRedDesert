#include "DmcCameraCharacter.h"

#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Character/New/DmcCameraCharacterMovementComponent.h"
#include "Character/New/WallRunSurfaceComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GAS/AITestAttributeSet.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "CollisionShape.h"
#include "WorldCollision.h"

//测试修改

ADMCameraCharacter::ADMCameraCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDmcCameraCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = ExploreArmLength;
	CameraBoom->SocketOffset = ExploreSocketOffset;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 12.f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;
	CameraBoom->bEnableCameraRotationLag = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->FieldOfView = ExploreFOV;

	WallRunDetector = CreateDefaultSubobject<UBoxComponent>(TEXT("WallRunDetector"));
	WallRunDetector->SetupAttachment(GetCapsuleComponent());
	WallRunDetector->SetBoxExtent(FVector(65.f, 75.f, 95.f));
	WallRunDetector->SetRelativeLocation(FVector(0.f, 0.f, 10.f));
	WallRunDetector->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallRunDetector->SetCollisionObjectType(ECC_Pawn);
	WallRunDetector->SetCollisionResponseToAllChannels(ECR_Ignore);
	WallRunDetector->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	WallRunDetector->SetGenerateOverlapEvents(true);
	WallRunDetector->SetCanEverAffectNavigation(false);

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UAITestAttributeSet>(TEXT("AttributeSet"));

	DesiredArmLength = ExploreArmLength;
	DesiredSocketOffset = ExploreSocketOffset;
	DesiredFOV = ExploreFOV;

	DefaultCapsuleHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	DefaultGroundFriction = GetCharacterMovement()->GroundFriction;
	DefaultBrakingDecelerationWalking = GetCharacterMovement()->BrakingDecelerationWalking;
	DefaultMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
}

UAbilitySystemComponent* ADMCameraCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ADMCameraCharacter::Jump()
{
	LastJumpPressedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (CanPerformWallJump())
	{
		bWallJumpRequested = true;
		PerformWallJump();
		return;
	}

	Super::Jump();
}

void ADMCameraCharacter::BeginPlay()
{
	Super::BeginPlay();

	InitializeAbilitySystem();

	DefaultCapsuleHalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	DefaultGroundFriction = GetCharacterMovement()->GroundFriction;
	DefaultBrakingDecelerationWalking = GetCharacterMovement()->BrakingDecelerationWalking;
	DefaultMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	GetCharacterMovement()->SetCrouchedHalfHeight(GetClampedSlideCapsuleHalfHeight());

	if (WallRunDetector)
	{
		WallRunDetector->OnComponentBeginOverlap.AddDynamic(this, &ADMCameraCharacter::OnWallRunDetectorBeginOverlap);
		WallRunDetector->OnComponentEndOverlap.AddDynamic(this, &ADMCameraCharacter::OnWallRunDetectorEndOverlap);
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (DefaultMappingContext)
				{
					Subsystem->AddMappingContext(DefaultMappingContext, 0);
				}
			}
		}

		DesiredControlRotation = PC->GetControlRotation();
	}
}

void ADMCameraCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilitySystem();
}

void ADMCameraCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HorizontalSpeed = FVector(GetVelocity().X, GetVelocity().Y, 0.f).Size();

	if (TargetSwitchCooldown > 0.f)
	{
		TargetSwitchCooldown -= DeltaSeconds;
	}

	if (SlideCooldownRemaining > 0.f)
	{
		SlideCooldownRemaining -= DeltaSeconds;
	}

	UpdateSlide(DeltaSeconds);
	UpdateWallRunState(DeltaSeconds);
	UpdateCameraSystem(DeltaSeconds);
}

void ADMCameraCharacter::Landed(const FHitResult& Hit)
{
	if (bIsWallRunning)
	{
		StopWallRun(false);
	}

	bWallJumpRequested = false;
	LastWallJumpSide = EWallRunSide::None;
	SetWallJumpTriggered(false);
	Super::Landed(Hit);
}

void ADMCameraCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (IsWallRunMovementMode())
	{
		bIsWallRunning = true;
		WallRunLastWallContactTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		GetCharacterMovement()->bOrientRotationToMovement = false;
		return;
	}

	if (PrevMovementMode == MOVE_Custom && PreviousCustomMode == static_cast<uint8>(EDmcCustomMovementMode::CMOVE_WallRun))
	{
		ClearWallRunState();
	}
}

void ADMCameraCharacter::InitializeAbilitySystem()
{
	if (!AbilitySystemComponent || !AttributeSet)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UAITestAttributeSet::GetHealthAttribute()).RemoveAll(this);
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UAITestAttributeSet::GetHealthAttribute()).AddUObject(this, &ADMCameraCharacter::HandleHealthChanged);
	AbilitySystemComponent->SetNumericAttributeBase(UAITestAttributeSet::GetMaxHealthAttribute(), MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UAITestAttributeSet::GetHealthAttribute(), MaxHealth);

	CurrentHealth = AttributeSet->GetHealth();
	GrantStartupAbilities();
}

void ADMCameraCharacter::GrantStartupAbilities()
{
	if (!AbilitySystemComponent || bStartupAbilitiesGranted || !HasAuthority())
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& StartupAbility : StartupAbilities)
	{
		if (StartupAbility)
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(StartupAbility, 1));
		}
	}

	bStartupAbilitiesGranted = true;
}

void ADMCameraCharacter::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	CurrentHealth = ChangeData.NewValue;
}

void ADMCameraCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADMCameraCharacter::Move);
		}

		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADMCameraCharacter::Look);
		}

		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ADMCameraCharacter::StartJump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ADMCameraCharacter::StopJump);
		}

		if (LockOnAction)
		{
			EnhancedInputComponent->BindAction(LockOnAction, ETriggerEvent::Started, this, &ADMCameraCharacter::ToggleLockOn);
		}

		if (SlideAction)
		{
			EnhancedInputComponent->BindAction(SlideAction, ETriggerEvent::Started, this, &ADMCameraCharacter::HandleSlideInput);
		}

		if (SwitchTargetAction)
		{
			EnhancedInputComponent->BindAction(SwitchTargetAction, ETriggerEvent::Triggered, this, &ADMCameraCharacter::SwitchTarget);
		}
	}
}

void ADMCameraCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (!Controller) return;

	const FRotator Rotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	if (MovementVector.Y != 0.f)
	{
		AddMovementInput(ForwardDirection, bIsSliding ? MovementVector.Y * SlideSteeringControl : MovementVector.Y);
	}

	if (MovementVector.X != 0.f)
	{
		AddMovementInput(RightDirection, bIsSliding ? MovementVector.X * SlideSteeringControl : MovementVector.X);
	}

	if (bIsSliding)
	{
		FVector InputDirection = (ForwardDirection * MovementVector.Y) + (RightDirection * MovementVector.X);
		InputDirection.Z = 0.f;
		if (!InputDirection.IsNearlyZero())
		{
			SlideDirection = FMath::Lerp(SlideDirection, InputDirection.GetSafeNormal(), SlideSteeringControl).GetSafeNormal();
		}
	}
}

void ADMCameraCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	if (!Controller) return;

	if (CameraState == ECombatCameraState::LockOn)
	{
		PendingOrbitYawInput += LookAxisVector.X * LockOrbitYawInputScale;
		PendingOrbitPitchInput += LookAxisVector.Y * LockOrbitPitchInputScale;
	}
	else
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);

		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			FRotator ControlRot = PC->GetControlRotation();
			const float MinPitch = (CameraState == ECombatCameraState::Explore) ? ExploreMinPitch : CombatMinPitch;
			const float MaxPitch = (CameraState == ECombatCameraState::Explore) ? ExploreMaxPitch : CombatMaxPitch;
			ControlRot.Pitch = FMath::ClampAngle(ControlRot.Pitch, MinPitch, MaxPitch);
			PC->SetControlRotation(ControlRot);
		}
	}
}

void ADMCameraCharacter::StartJump(const FInputActionValue& Value)
{
	if (bIsSliding)
	{
		return;
	}

	Jump();
}

void ADMCameraCharacter::StopJump(const FInputActionValue& Value)
{
	StopJumping();
}

void ADMCameraCharacter::HandleSlideInput(const FInputActionValue& Value)
{
	bHasSlideInputQueued = true;
	StartSlide();
	bHasSlideInputQueued = false;
}

void ADMCameraCharacter::ToggleLockOn(const FInputActionValue& Value)
{
	if (CurrentLockTarget)
	{
		ExitLockOn();
		return;
	}

	if (AActor* NewTarget = FindBestLockTarget())
	{
		EnterLockOn(NewTarget);
	}
}

void ADMCameraCharacter::SwitchTarget(const FInputActionValue& Value)
{
	if (!CurrentLockTarget || CameraState != ECombatCameraState::LockOn || TargetSwitchCooldown > 0.f)
	{
		return;
	}

	const float AxisValue = Value.Get<float>();
	if (FMath::Abs(AxisValue) < 0.5f)
	{
		return;
	}

	if (AActor* NewTarget = FindSwitchTarget(FMath::Sign(AxisValue)))
	{
		if (NewTarget != CurrentLockTarget)
		{
			PreviousLockTarget = CurrentLockTarget;
			CurrentLockTarget = NewTarget;
			TargetSwitchCooldown = 0.2f;
		}
	}
}

bool ADMCameraCharacter::StartSlide()
{
	if (!CanStartSlide())
	{
		return false;
	}

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent)
	{
		return false;
	}

	const FVector CurrentVelocity = GetVelocity();
	FVector HorizontalVelocity = FVector(CurrentVelocity.X, CurrentVelocity.Y, 0.f);
	const float CurrentSpeed = HorizontalVelocity.Size();
	HorizontalSpeed = CurrentSpeed;

	SlideDirection = CurrentSpeed > KINDA_SMALL_NUMBER ? HorizontalVelocity.GetSafeNormal() : GetActorForwardVector().GetSafeNormal2D();
	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = FVector::ForwardVector;
	}

	bIsSliding = true;
	bPendingSlideUnCrouch = false;
	SlideTimeRemaining = SlideDuration;
	SlideCooldownRemaining = SlideCooldown;

	MovementComponent->SetCrouchedHalfHeight(GetClampedSlideCapsuleHalfHeight());
	Crouch();

	MovementComponent->GroundFriction = SlideGroundFriction;
	MovementComponent->BrakingDecelerationWalking = SlideBrakingDeceleration;
	MovementComponent->MaxWalkSpeed = FMath::Max(DefaultMaxWalkSpeed, SlideMaxSpeed);
	MovementComponent->bOrientRotationToMovement = false;

	const float StartSpeed = FMath::Clamp(FMath::Max(CurrentSpeed, SlideMinStartSpeed) + SlideInitialSpeedBoost, 0.f, SlideMaxSpeed);
	MovementComponent->Velocity = SlideDirection * StartSpeed + FVector(0.f, 0.f, CurrentVelocity.Z);
	HorizontalSpeed = StartSpeed;
	return true;
}

void ADMCameraCharacter::StopSlide()
{
	if (!bIsSliding)
	{
		return;
	}

	bIsSliding = false;
	bPendingSlideUnCrouch = true;
	SlideTimeRemaining = 0.f;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->GroundFriction = DefaultGroundFriction;
		MovementComponent->BrakingDecelerationWalking = DefaultBrakingDecelerationWalking;
		MovementComponent->MaxWalkSpeed = DefaultMaxWalkSpeed;
	}

	UnCrouch();
	if (!bIsCrouched)
	{
		bPendingSlideUnCrouch = false;
	}
}

void ADMCameraCharacter::UpdateWallRunState(float DeltaSeconds)
{
	const bool bShouldClearWallJumpRequestAtEnd = bWallJumpRequested && !bIsWallRunning;

	if (WallJumpAnimFlagRemaining > 0.f)
	{
		WallJumpAnimFlagRemaining -= DeltaSeconds;
		if (WallJumpAnimFlagRemaining <= 0.f)
		{
			SetWallJumpTriggered(false);
		}
	}

	CleanupInvalidWallRunSurfaces();

	if (bIsWallRunning)
	{
		if (!CurrentWallRunSurface.IsValid() || !HasOverlappingWallRunSurface(CurrentWallRunSurface.Get()))
		{
			const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			if ((CurrentTime - WallRunLastWallContactTime) > LostWallForgivenessTime)
			{
				StopWallRun(false);
			}
		}
		return;
	}

	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent || bIsSliding || MovementComponent->MovementMode != MOVE_Falling)
	{
		if (bShouldClearWallJumpRequestAtEnd)
		{
			bWallJumpRequested = false;
		}
		return;
	}

	TryStartWallRun();

	if (bShouldClearWallJumpRequestAtEnd)
	{
		bWallJumpRequested = false;
	}
}

bool ADMCameraCharacter::TryStartWallRun()
{
	if (UWallRunSurfaceComponent* Surface = FindBestWallRunSurface())
	{
		if (CanStartWallRunOnSurface(Surface))
		{
			StartWallRun(Surface);
			return true;
		}
	}
	else
	{
		DebugWallRunMessage(TEXT("WallRun: No valid surface candidate"), FColor::Orange);
	}

	return false;
}

bool ADMCameraCharacter::CanStartWallRunOnSurface(UWallRunSurfaceComponent* Surface) const
{
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent || !Surface || bIsWallRunning || bIsSliding)
	{
		DebugWallRunMessage(TEXT("WallRun Blocked: invalid state or surface"), FColor::Orange);
		return false;
	}

	if (MovementComponent->MovementMode != MOVE_Falling || !Surface->IsWallRunEnabled())
	{
		DebugWallRunMessage(TEXT("WallRun Blocked: not falling or surface disabled"), FColor::Orange);
		return false;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (CurrentTime < WallJumpBlockedUntilTime)
	{
		if (!LastWallJumpSurface.IsValid() || Surface == LastWallJumpSurface.Get())
		{
			DebugWallRunMessage(TEXT("WallRun Blocked: wall jump reattach block active"), FColor::Orange);
			return false;
		}
	}

	if ((CurrentTime - LastJumpPressedTime) > JumpToWallGraceTime)
	{
		DebugWallRunMessage(
			FString::Printf(TEXT("WallRun Blocked: jump grace expired (Current: %.2f, Allowed: %.2f)"), CurrentTime - LastJumpPressedTime, JumpToWallGraceTime),
			FColor::Orange);
		return false;
	}

	const float HorizontalSpeed2D = FVector(GetVelocity().X, GetVelocity().Y, 0.f).Size();
	if (HorizontalSpeed2D < MinSpeedToStartWallRun)
	{
		DebugWallRunMessage(
			FString::Printf(TEXT("WallRun Blocked: speed too low (Current: %.2f, Required: %.2f)"), HorizontalSpeed2D, MinSpeedToStartWallRun),
			FColor::Orange);
		return false;
	}

	const FVector WallNormal = Surface->GetWallNormal();
	const float WallAngle = GetWallAngleFromUpDeg(WallNormal);
	if (WallAngle < MinWallAngleFromUpDeg || WallAngle > MaxWallAngleFromUpDeg)
	{
		DebugWallRunMessage(
			FString::Printf(TEXT("Wall Angle Error! Current: %.2f, Allowed: %.2f - %.2f"), WallAngle, MinWallAngleFromUpDeg, MaxWallAngleFromUpDeg),
			FColor::Red);
		return false;
	}

	const FVector WallRunDirection = Surface->GetWallRunDirectionFromVelocity(GetVelocity());
	if (WallRunDirection.IsNearlyZero() || !Surface->IsDirectionAllowed(WallRunDirection))
	{
		DebugWallRunMessage(TEXT("WallRun Blocked: invalid wall run direction"), FColor::Red);
		return false;
	}

	const float ApproachAngle = GetWallApproachAngleDeg(WallNormal);
	if (ApproachAngle < MinApproachAngleDeg || ApproachAngle > MaxApproachAngleDeg)
	{
		DebugWallRunMessage(
			FString::Printf(TEXT("Angle Error! Current: %.2f, Allowed: %.2f - %.2f"), ApproachAngle, MinApproachAngleDeg, MaxApproachAngleDeg),
			FColor::Red);
		return false;
	}

	DebugWallRunMessage(TEXT("WallRun Ready: all conditions passed"), FColor::Green);
	return true;
}

UWallRunSurfaceComponent* ADMCameraCharacter::FindBestWallRunSurface() const
{
	const FVector HorizontalVelocity = FVector(GetVelocity().X, GetVelocity().Y, 0.f).GetSafeNormal();

	UWallRunSurfaceComponent* BestSurface = nullptr;
	float BestScore = -FLT_MAX;

	for (const TWeakObjectPtr<UWallRunSurfaceComponent>& SurfacePtr : OverlappingWallRunSurfaces)
	{
		UWallRunSurfaceComponent* Surface = SurfacePtr.Get();
		if (!Surface || !Surface->IsWallRunEnabled())
		{
			continue;
		}

		const FVector CandidateDirection = Surface->GetWallRunDirectionFromVelocity(GetVelocity());
		if (CandidateDirection.IsNearlyZero() || !Surface->IsDirectionAllowed(CandidateDirection))
		{
			continue;
		}

		const FVector ClosestPoint = Surface->GetClosestPointToLocation(GetActorLocation());
		const float DistanceScore = -FVector::DistSquared2D(GetActorLocation(), ClosestPoint) * 0.001f;
		const float DirectionScore = HorizontalVelocity.IsNearlyZero() ? 0.f : FVector::DotProduct(HorizontalVelocity, CandidateDirection) * 1000.f;
		const float ApproachScore = -GetWallApproachAngleDeg(Surface->GetWallNormal()) * 2.f;
		const float Score = DirectionScore + DistanceScore + ApproachScore;

		if (Score > BestScore)
		{
			BestScore = Score;
			BestSurface = Surface;
		}
	}

	return BestSurface;
}

void ADMCameraCharacter::StartWallRun(UWallRunSurfaceComponent* Surface)
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent || !Surface)
	{
		return;
	}

	CurrentWallRunSurface = Surface;
	CurrentWallNormal = Surface->GetWallNormal();
	CurrentWallRunDirection = Surface->GetWallRunDirectionFromVelocity(GetVelocity());
	CurrentWallRunSide = DetermineWallRunSide(Surface, CurrentWallRunDirection);

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	WallRunStartTime = CurrentTime;
	WallRunLastWallContactTime = CurrentTime;
	bIsWallRunning = true;
	bWallJumpRequested = false;

	const float StartSpeed = FMath::Max(MinSpeedToMaintainWallRun, FVector(GetVelocity().X, GetVelocity().Y, 0.f).Size());
	MovementComponent->Velocity = CurrentWallRunDirection * StartSpeed + FVector(0.f, 0.f, FMath::Max(GetVelocity().Z, 0.f));
	MovementComponent->SetMovementMode(MOVE_Custom, static_cast<uint8>(EDmcCustomMovementMode::CMOVE_WallRun));
	MovementComponent->bOrientRotationToMovement = false;
}

void ADMCameraCharacter::StopWallRun(bool bLaunchAway)
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	const bool bWasWallRunning = bIsWallRunning || IsWallRunMovementMode();
	if (!bWasWallRunning)
	{
		return;
	}

	const FVector WallNormalToUse = CurrentWallNormal;
	ClearWallRunState();
	MovementComponent->SetMovementMode(MOVE_Falling);

	if (bLaunchAway && !WallNormalToUse.IsNearlyZero())
	{
		MovementComponent->Velocity += WallNormalToUse.GetSafeNormal() * (WallJumpHorizontalStrength * 0.2f);
	}
}

void ADMCameraCharacter::PerformWallJump()
{
	if (!CanPerformWallJump())
	{
		return;
	}

	LastWallJumpSide = CurrentWallRunSide;
	LastWallJumpSurface = CurrentWallRunSurface;
	WallJumpBlockedUntilTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) + WallJumpReattachBlockTime;

	const FVector LaunchVelocity =
		(CurrentWallNormal.GetSafeNormal() * WallJumpHorizontalStrength) +
		(FVector::UpVector * WallJumpVerticalStrength) +
		(GetActorForwardVector().GetSafeNormal2D() * WallJumpForwardBoost);

	StopWallRun(false);
	LaunchCharacter(LaunchVelocity, true, true);
	SetWallJumpTriggered(true);
}

void ADMCameraCharacter::OnWallRunDetectorBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherActor;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	UWallRunSurfaceComponent* Surface = Cast<UWallRunSurfaceComponent>(OtherComp);
	if (!Surface)
	{
		return;
	}

	DebugWallRunMessage(TEXT("Overlapped!"), FColor::Yellow);

	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const float HorizontalSpeed2D = FVector(GetVelocity().X, GetVelocity().Y, 0.f).Size();
	if (MovementComponent && MovementComponent->MovementMode == MOVE_Falling && HorizontalSpeed2D >= MinSpeedToStartWallRun)
	{
		DebugWallRunMessage(TEXT("Character Falling!"), FColor::Cyan);

		const FVector WallNormal = Surface->GetWallNormal();
		const FVector WallRunDirection = Surface->GetWallRunDirectionFromVelocity(GetVelocity());
		const float ApproachAngle = GetWallApproachAngleDeg(WallNormal);
		const bool bAngleValid = ApproachAngle >= MinApproachAngleDeg && ApproachAngle <= MaxApproachAngleDeg;
		if (!bAngleValid)
		{
			DebugWallRunMessage(
				FString::Printf(TEXT("Angle Error! Current: %.2f, Allowed: %.2f - %.2f"), ApproachAngle, MinApproachAngleDeg, MaxApproachAngleDeg),
				FColor::Red);
		}
	}

	AddOverlappingWallRunSurface(Surface);
}

void ADMCameraCharacter::OnWallRunDetectorEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	(void)OverlappedComponent;
	(void)OtherActor;
	(void)OtherBodyIndex;

	UWallRunSurfaceComponent* Surface = Cast<UWallRunSurfaceComponent>(OtherComp);
	if (!Surface)
	{
		return;
	}

	RemoveOverlappingWallRunSurface(Surface);
}

void ADMCameraCharacter::AddOverlappingWallRunSurface(UWallRunSurfaceComponent* Surface)
{
	if (!Surface)
	{
		return;
	}

	for (const TWeakObjectPtr<UWallRunSurfaceComponent>& SurfacePtr : OverlappingWallRunSurfaces)
	{
		if (SurfacePtr.Get() == Surface)
		{
			return;
		}
	}

	OverlappingWallRunSurfaces.Add(Surface);
}

void ADMCameraCharacter::RemoveOverlappingWallRunSurface(UWallRunSurfaceComponent* Surface)
{
	OverlappingWallRunSurfaces.RemoveAll([Surface](const TWeakObjectPtr<UWallRunSurfaceComponent>& SurfacePtr)
	{
		return !SurfacePtr.IsValid() || SurfacePtr.Get() == Surface;
	});
}

bool ADMCameraCharacter::HasOverlappingWallRunSurface(UWallRunSurfaceComponent* Surface) const
{
	if (!Surface)
	{
		return false;
	}

	for (const TWeakObjectPtr<UWallRunSurfaceComponent>& SurfacePtr : OverlappingWallRunSurfaces)
	{
		if (SurfacePtr.Get() == Surface)
		{
			return true;
		}
	}

	return false;
}

void ADMCameraCharacter::CleanupInvalidWallRunSurfaces()
{
	OverlappingWallRunSurfaces.RemoveAll([](const TWeakObjectPtr<UWallRunSurfaceComponent>& SurfacePtr)
	{
		return !SurfacePtr.IsValid() || !SurfacePtr->IsWallRunEnabled();
	});
}

FVector ADMCameraCharacter::ComputeWallRunDirection(const FVector& WallNormal, const FVector& ReferenceDirection) const
{
	const FVector HorizontalWallNormal = FVector(WallNormal.X, WallNormal.Y, 0.f).GetSafeNormal();
	FVector AlongWall = FVector::CrossProduct(FVector::UpVector, HorizontalWallNormal).GetSafeNormal();
	if (AlongWall.IsNearlyZero())
	{
		return GetActorForwardVector().GetSafeNormal2D();
	}

	FVector Reference = FVector(ReferenceDirection.X, ReferenceDirection.Y, 0.f).GetSafeNormal();
	if (Reference.IsNearlyZero())
	{
		Reference = GetActorForwardVector().GetSafeNormal2D();
	}

	if (FVector::DotProduct(AlongWall, Reference) < 0.f)
	{
		AlongWall *= -1.f;
	}

	return AlongWall;
}

EWallRunSide ADMCameraCharacter::DetermineWallRunSide(const UWallRunSurfaceComponent* Surface, const FVector& WallRunDirection) const
{
	if (!Surface)
	{
		return EWallRunSide::None;
	}

	const FVector ToSurface = (Surface->GetClosestPointToLocation(GetActorLocation()) - GetActorLocation()).GetSafeNormal2D();
	const FVector AlongWallDirection = WallRunDirection.GetSafeNormal2D();
	if (ToSurface.IsNearlyZero() || AlongWallDirection.IsNearlyZero())
	{
		return EWallRunSide::None;
	}

	const float CrossZ = FVector::CrossProduct(AlongWallDirection, ToSurface).Z;
	return CrossZ >= 0.f ? EWallRunSide::Right : EWallRunSide::Left;
}

float ADMCameraCharacter::GetWallAngleFromUpDeg(const FVector& WallNormal) const
{
	const float Dot = FVector::DotProduct(WallNormal.GetSafeNormal(), FVector::UpVector);
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
}

float ADMCameraCharacter::GetWallApproachAngleDeg(const FVector& WallNormal) const
{
	const FVector HorizontalForward = GetActorForwardVector().GetSafeNormal2D();
	if (HorizontalForward.IsNearlyZero())
	{
		return 0.f;
	}

	const FVector IntoWallDirection = FVector(-WallNormal.X, -WallNormal.Y, 0.f).GetSafeNormal();
	if (IntoWallDirection.IsNearlyZero())
	{
		return 0.f;
	}

	const float Dot = FVector::DotProduct(HorizontalForward, IntoWallDirection);
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
}

bool ADMCameraCharacter::CanPerformWallJump() const
{
	return bEnableWallJump
		&& bIsWallRunning
		&& IsWallRunMovementMode()
		&& CurrentWallRunSurface.IsValid()
		&& !CurrentWallNormal.IsNearlyZero()
		&& !CurrentWallRunDirection.IsNearlyZero();
}

float ADMCameraCharacter::GetWallRunTargetSpeed() const
{
	return CurrentWallRunSurface.IsValid()
		? CurrentWallRunSurface->ResolveWallRunSpeed(WallRunSpeed)
		: WallRunSpeed;
}

float ADMCameraCharacter::GetWallRunMaxDurationForCurrentSurface() const
{
	return CurrentWallRunSurface.IsValid()
		? CurrentWallRunSurface->ResolveWallRunMaxDuration(WallRunMaxDuration)
		: WallRunMaxDuration;
}

bool ADMCameraCharacter::IsWallRunMovementMode() const
{
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	return MovementComponent
		&& MovementComponent->MovementMode == MOVE_Custom
		&& MovementComponent->CustomMovementMode == static_cast<uint8>(EDmcCustomMovementMode::CMOVE_WallRun);
}

void ADMCameraCharacter::ClearWallRunState()
{
	bIsWallRunning = false;
	CurrentWallNormal = FVector::ZeroVector;
	CurrentWallRunDirection = FVector::ZeroVector;
	CurrentWallRunSide = EWallRunSide::None;
	CurrentWallRunSurface = nullptr;
	WallRunLastWallContactTime = -1000.f;
}

void ADMCameraCharacter::SetWallJumpTriggered(bool bNewTriggered)
{
	bWallJumpTriggered = bNewTriggered;
	WallJumpAnimFlagRemaining = bNewTriggered ? WallJumpAnimFlagDuration : 0.f;
	if (!bNewTriggered)
	{
		LastWallJumpSide = EWallRunSide::None;
	}
}

void ADMCameraCharacter::DebugWallRunMessage(const FString& Message, const FColor& Color) const
{
	if (!bDebugWallRun || !GetWorld())
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if ((CurrentTime - LastWallRunDebugTime) < 0.08f)
	{
		return;
	}

	LastWallRunDebugTime = CurrentTime;

	UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, Color, Message);
	}
}

UDmcCameraCharacterMovementComponent* ADMCameraCharacter::GetDmcMovementComponent() const
{
	return Cast<UDmcCameraCharacterMovementComponent>(GetCharacterMovement());
}

void ADMCameraCharacter::UpdateCameraSystem(float DeltaSeconds)
{
	UpdateCameraState(DeltaSeconds);
	SolveDesiredCamera(DeltaSeconds);
	ApplyCamera(DeltaSeconds);
	UpdateCharacterFacing(DeltaSeconds);
}

void ADMCameraCharacter::UpdateSlide(float DeltaSeconds)
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	MovementComponent->SetCrouchedHalfHeight(GetClampedSlideCapsuleHalfHeight());

	if (!bIsSliding)
	{
		if (bPendingSlideUnCrouch && bIsCrouched)
		{
			UnCrouch();
		}

		if (!bIsCrouched)
		{
			bPendingSlideUnCrouch = false;
		}

		return;
	}

	if (!MovementComponent->IsMovingOnGround())
	{
		StopSlide();
		return;
	}

	SlideTimeRemaining -= DeltaSeconds;

	FVector HorizontalVelocity = FVector(MovementComponent->Velocity.X, MovementComponent->Velocity.Y, 0.f);
	float CurrentHorizontalSpeed = HorizontalVelocity.Size();
	HorizontalSpeed = CurrentHorizontalSpeed;

	if (CurrentHorizontalSpeed > KINDA_SMALL_NUMBER)
	{
		SlideDirection = HorizontalVelocity.GetSafeNormal();
	}

	CurrentHorizontalSpeed = FMath::Clamp(CurrentHorizontalSpeed + SlideAcceleration * DeltaSeconds, 0.f, SlideMaxSpeed);
	MovementComponent->Velocity = SlideDirection * CurrentHorizontalSpeed + FVector(0.f, 0.f, MovementComponent->Velocity.Z);
	HorizontalSpeed = CurrentHorizontalSpeed;

	if (SlideTimeRemaining <= 0.f || CurrentHorizontalSpeed <= SlideMinSpeedToEnd)
	{
		StopSlide();
	}
}

void ADMCameraCharacter::UpdateCameraState(float DeltaSeconds)
{
	if (CurrentLockTarget && !IsValidLockTarget(CurrentLockTarget))
	{
		ExitLockOn();
	}

	if (CurrentLockTarget)
	{
		CameraState = ECombatCameraState::LockOn;
		return;
	}

	if (UnlockRecoveryRemaining > 0.f)
	{
		UnlockRecoveryRemaining -= DeltaSeconds;
		CameraState = ECombatCameraState::Recover;
		return;
	}

	CameraState = HasNearbyCombatTarget() ? ECombatCameraState::CombatFree : ECombatCameraState::Explore;
}

void ADMCameraCharacter::SolveDesiredCamera(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return;

	FRotator CurrentControlRot = PC->GetControlRotation();

	if (CameraState == ECombatCameraState::Explore)
	{
		DesiredArmLength = ExploreArmLength;
		DesiredSocketOffset = ExploreSocketOffset;
		DesiredFOV = ExploreFOV;
		DesiredControlRotation = CurrentControlRot;
		DesiredControlRotation.Pitch = FMath::ClampAngle(DesiredControlRotation.Pitch, ExploreMinPitch, ExploreMaxPitch);
		CurrentZone = ELockOnZone::Center;
		return;
	}

	if (CameraState == ECombatCameraState::CombatFree || CameraState == ECombatCameraState::Recover)
	{
		DesiredArmLength = CombatArmLength;
		DesiredSocketOffset = CombatSocketOffset;
		DesiredFOV = CombatFOV;
		DesiredControlRotation = CurrentControlRot;
		DesiredControlRotation.Pitch = FMath::ClampAngle(DesiredControlRotation.Pitch, CombatMinPitch, CombatMaxPitch);
		CurrentZone = ELockOnZone::Center;
		return;
	}

	if (!CurrentLockTarget)
	{
		return;
	}

	const FVector PlayerPivot = GetPlayerPivot();
	const FVector TargetPivot = GetTargetPivot(CurrentLockTarget);
	const FVector ToTarget = TargetPivot - PlayerPivot;
	const float Distance2D = FVector(ToTarget.X, ToTarget.Y, 0.f).Length();

	if (Distance2D <= NearDistance)
	{
		DesiredArmLength = LockNearArmLength;
		DesiredSocketOffset = LockNearSocketOffset;
		DesiredFOV = LockNearFOV;
	}
	else if (Distance2D <= MidDistance)
	{
		DesiredArmLength = LockMidArmLength;
		DesiredSocketOffset = LockMidSocketOffset;
		DesiredFOV = LockMidFOV;
	}
	else
	{
		DesiredArmLength = LockFarArmLength;
		DesiredSocketOffset = LockFarSocketOffset;
		DesiredFOV = LockFarFOV;
	}

	const float HeightDelta = TargetPivot.Z - PlayerPivot.Z;
	LockOrbitYaw = FMath::Clamp(LockOrbitYaw + PendingOrbitYawInput, -LockOrbitYawMax, LockOrbitYawMax);
	LockOrbitPitch = FMath::Clamp(LockOrbitPitch + PendingOrbitPitchInput, -LockOrbitPitchMax, LockOrbitPitchMax);
	PendingOrbitYawInput = 0.f;
	PendingOrbitPitchInput = 0.f;

	const FRotator BaseLookRot = ToTarget.Rotation();
	float DesiredYaw = BaseLookRot.Yaw + LockOrbitYaw;
	float DesiredPitch = BaseLookRot.Pitch + HeightDelta * HeightPitchFactor + LockOrbitPitch;
	DesiredPitch = FMath::ClampAngle(DesiredPitch, LockMinPitch, LockMaxPitch);

	CurrentZone = ComputeLockZone(BaseLookRot.Yaw);
	const bool bDangerZone = CurrentZone == ELockOnZone::Edge || CurrentZone == ELockOnZone::Offscreen;
	LockOrbitYaw = FMath::FInterpTo(LockOrbitYaw, 0.f, DeltaSeconds, bDangerZone ? LockOrbitDecaySpeed * 2.0f : LockOrbitDecaySpeed);
	LockOrbitPitch = FMath::FInterpTo(LockOrbitPitch, 0.f, DeltaSeconds, LockOrbitDecaySpeed);

	if (CurrentZone == ELockOnZone::Offscreen)
	{
		DesiredArmLength += 30.f;
		DesiredSocketOffset.Z += 10.f;
	}

	DesiredControlRotation = FRotator(DesiredPitch, DesiredYaw, 0.f);
}

void ADMCameraCharacter::ApplyCamera(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return;

	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, DesiredArmLength, DeltaSeconds, ArmInterpSpeed);
	CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, DesiredSocketOffset, DeltaSeconds, OffsetInterpSpeed);

	float RotationInterpSpeed = RotationInterpSpeed_Explore;
	switch (CameraState)
	{
	case ECombatCameraState::Explore:
		RotationInterpSpeed = RotationInterpSpeed_Explore;
		break;
	case ECombatCameraState::CombatFree:
	case ECombatCameraState::Recover:
		RotationInterpSpeed = RotationInterpSpeed_Combat;
		break;
	case ECombatCameraState::LockOn:
		RotationInterpSpeed = GetRotationInterpSpeedForZone(CurrentZone);
		break;
	default:
		break;
	}

	FRotator NewControlRot = FMath::RInterpTo(PC->GetControlRotation(), DesiredControlRotation, DeltaSeconds, RotationInterpSpeed);
	if (CameraState == ECombatCameraState::Explore)
	{
		NewControlRot.Pitch = FMath::ClampAngle(NewControlRot.Pitch, ExploreMinPitch, ExploreMaxPitch);
	}
	else if (CameraState == ECombatCameraState::CombatFree || CameraState == ECombatCameraState::Recover)
	{
		NewControlRot.Pitch = FMath::ClampAngle(NewControlRot.Pitch, CombatMinPitch, CombatMaxPitch);
	}
	else
	{
		NewControlRot.Pitch = FMath::ClampAngle(NewControlRot.Pitch, LockMinPitch, LockMaxPitch);
	}

	PC->SetControlRotation(NewControlRot);

	const float NewFOV = FMath::FInterpTo(FollowCamera->FieldOfView, DesiredFOV, DeltaSeconds, FOVInterpSpeed);
	FollowCamera->SetFieldOfView(NewFOV);
}

void ADMCameraCharacter::UpdateCharacterFacing(float DeltaSeconds)
{
	if (bIsWallRunning)
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;

		if (!CurrentWallRunDirection.IsNearlyZero())
		{
			FRotator DesiredActorRot = CurrentWallRunDirection.Rotation();
			DesiredActorRot.Pitch = 0.f;
			DesiredActorRot.Roll = 0.f;
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredActorRot, DeltaSeconds, WallRunRotationInterpSpeed));
		}

		return;
	}

	if (bIsSliding)
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;

		if (!SlideDirection.IsNearlyZero())
		{
			FRotator DesiredActorRot = SlideDirection.Rotation();
			DesiredActorRot.Pitch = 0.f;
			DesiredActorRot.Roll = 0.f;
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredActorRot, DeltaSeconds, 12.f));
		}

		return;
	}

	if (CameraState == ECombatCameraState::LockOn && CurrentLockTarget)
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;

		const FVector ToTarget = GetTargetPivot(CurrentLockTarget) - GetActorLocation();
		FRotator DesiredActorRot = ToTarget.Rotation();
		DesiredActorRot.Pitch = 0.f;
		DesiredActorRot.Roll = 0.f;
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredActorRot, DeltaSeconds, 10.f));
	}
	else
	{
		GetCharacterMovement()->bOrientRotationToMovement = true;
	}
}

bool ADMCameraCharacter::CanStartSlide() const
{
	if (!bSlideEnabled)
	{
		return false;
	}

	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent || !Controller || bIsSliding || SlideCooldownRemaining > 0.f)
	{
		return false;
	}

	if (!MovementComponent->IsMovingOnGround())
	{
		return false;
	}

	const float CurrentHorizontalSpeed = FVector(GetVelocity().X, GetVelocity().Y, 0.f).Size();
	return bCanSlideFromStandingStill || CurrentHorizontalSpeed >= SlideTriggerMinSpeed;
}

float ADMCameraCharacter::GetClampedSlideCapsuleHalfHeight() const
{
	const float CapsuleRadius = GetCapsuleComponent() ? GetCapsuleComponent()->GetUnscaledCapsuleRadius() : 0.f;
	return FMath::Max(SlideCapsuleHalfHeight, CapsuleRadius + 1.f);
}

float ADMCameraCharacter::GetHorizontalSpeed() const
{
	return HorizontalSpeed;
}

FVector ADMCameraCharacter::GetSlideDirection() const
{
	return SlideDirection;
}

bool ADMCameraCharacter::IsWallRunning() const
{
	return bIsWallRunning;
}

bool ADMCameraCharacter::IsWallJumping() const
{
	return bWallJumpTriggered;
}

bool ADMCameraCharacter::WasWallJumpRequestedThisFrame() const
{
	return bWallJumpRequested;
}

EWallRunSide ADMCameraCharacter::GetWallJumpSide() const
{
	return LastWallJumpSide;
}

EWallRunSide ADMCameraCharacter::GetWallRunSide() const
{
	return CurrentWallRunSide;
}

bool ADMCameraCharacter::IsWallRunningOnLeft() const
{
	return bIsWallRunning && CurrentWallRunSide == EWallRunSide::Left;
}

bool ADMCameraCharacter::IsWallRunningOnRight() const
{
	return bIsWallRunning && CurrentWallRunSide == EWallRunSide::Right;
}

bool ADMCameraCharacter::IsWallJumpFromLeft() const
{
	return bWallJumpTriggered && LastWallJumpSide == EWallRunSide::Left;
}

bool ADMCameraCharacter::IsWallJumpFromRight() const
{
	return bWallJumpTriggered && LastWallJumpSide == EWallRunSide::Right;
}

FVector ADMCameraCharacter::GetWallRunDirection() const
{
	return CurrentWallRunDirection;
}

FVector ADMCameraCharacter::GetWallNormal() const
{
	return CurrentWallNormal;
}

float ADMCameraCharacter::GetWallRunSpeed() const
{
	return FMath::Abs(FVector::DotProduct(GetVelocity(), CurrentWallRunDirection.GetSafeNormal()));
}

bool ADMCameraCharacter::HasNearbyCombatTarget() const
{
	return GatherCandidateTargets(CombatDetectRadius).Num() > 0;
}

bool ADMCameraCharacter::IsValidLockTarget(AActor* Actor) const
{
	if (!Actor || Actor == this || Actor->IsPendingKillPending() || !Actor->ActorHasTag(LockTargetTag))
	{
		return false;
	}

	const FVector PlayerPivot = GetPlayerPivot();
	const FVector TargetPivot = GetTargetPivot(Actor);
	const float Dist2D = FVector(TargetPivot.X - PlayerPivot.X, TargetPivot.Y - PlayerPivot.Y, 0.f).Length();
	if (Dist2D > LockAcquireRadius)
	{
		return false;
	}

	return FMath::Abs(TargetPivot.Z - PlayerPivot.Z) <= MaxLockVerticalDelta;
}

TArray<AActor*> ADMCameraCharacter::GatherCandidateTargets(float Radius) const
{
	TArray<AActor*> Results;
	if (!GetWorld()) return Results;

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const bool bHit = GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		GetActorLocation(),
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(Radius),
		QueryParams);

	if (!bHit) return Results;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AActor* HitActor = Overlap.GetActor())
		{
			if (IsValidLockTarget(HitActor))
			{
				Results.AddUnique(HitActor);
			}
		}
	}

	return Results;
}

AActor* ADMCameraCharacter::FindBestLockTarget() const
{
	TArray<AActor*> Candidates = GatherCandidateTargets(LockAcquireRadius);
	if (Candidates.IsEmpty()) return nullptr;

	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return nullptr;

	const FVector PlayerPivot = GetPlayerPivot();
	const FVector CamForward = FRotationMatrix(PC->GetControlRotation()).GetUnitAxis(EAxis::X);

	AActor* BestTarget = nullptr;
	float BestScore = -FLT_MAX;

	for (AActor* Candidate : Candidates)
	{
		const FVector TargetPivot = GetTargetPivot(Candidate);
		const FVector ToTarget = (TargetPivot - PlayerPivot).GetSafeNormal();
		const float Distance = FVector::Dist2D(PlayerPivot, TargetPivot);
		const float Dot = FVector::DotProduct(CamForward, ToTarget);
		float Score = (Dot * 1000.f) - Distance;

		if (Dot > 0.f)
		{
			Score += 200.f;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	return BestTarget;
}

AActor* ADMCameraCharacter::FindSwitchTarget(float DirectionSign) const
{
	TArray<AActor*> Candidates = GatherCandidateTargets(LockAcquireRadius);
	if (Candidates.IsEmpty() || !CurrentLockTarget) return nullptr;

	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return nullptr;

	const FVector PlayerPivot = GetPlayerPivot();
	const FVector CamForward = FRotationMatrix(PC->GetControlRotation()).GetUnitAxis(EAxis::X);
	const FVector CamRight = FRotationMatrix(PC->GetControlRotation()).GetUnitAxis(EAxis::Y);

	AActor* BestTarget = nullptr;
	float BestScore = -FLT_MAX;

	for (AActor* Candidate : Candidates)
	{
		if (Candidate == CurrentLockTarget)
		{
			continue;
		}

		const FVector ToTarget = (GetTargetPivot(Candidate) - PlayerPivot).GetSafeNormal();
		const float Side = FVector::DotProduct(CamRight, ToTarget);
		if ((DirectionSign > 0.f && Side <= 0.f) || (DirectionSign < 0.f && Side >= 0.f))
		{
			continue;
		}

		const float Front = FVector::DotProduct(CamForward, ToTarget);
		const float Distance = FVector::Dist2D(PlayerPivot, GetTargetPivot(Candidate));
		const float Score = Front * 1000.f - Distance + FMath::Abs(Side) * 100.f;

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	return BestTarget ? BestTarget : CurrentLockTarget.Get();
}

ELockOnZone ADMCameraCharacter::ComputeLockZone(float TargetYawDeg) const
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return ELockOnZone::Center;

	const float CurrentYaw = PC->GetControlRotation().Yaw;
	const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYawDeg));

	if (DeltaYaw <= CenterAngle) return ELockOnZone::Center;
	if (DeltaYaw <= BufferAngle) return ELockOnZone::Buffer;
	if (DeltaYaw <= EdgeAngle) return ELockOnZone::Edge;
	return ELockOnZone::Offscreen;
}

float ADMCameraCharacter::GetRotationInterpSpeedForZone(ELockOnZone Zone) const
{
	switch (Zone)
	{
	case ELockOnZone::Center:
		return RotationInterpSpeed_Lock_Center;
	case ELockOnZone::Buffer:
		return RotationInterpSpeed_Lock_Buffer;
	case ELockOnZone::Edge:
		return RotationInterpSpeed_Lock_Edge;
	case ELockOnZone::Offscreen:
		return RotationInterpSpeed_Lock_Offscreen;
	default:
		return RotationInterpSpeed_Lock_Center;
	}
}

FVector ADMCameraCharacter::GetPlayerPivot() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 70.f);
}

FVector ADMCameraCharacter::GetTargetPivot(AActor* Target) const
{
	return Target ? Target->GetActorLocation() + FVector(0.f, 0.f, 70.f) : FVector::ZeroVector;
}

float ADMCameraCharacter::GetDistanceToTarget2D(AActor* Target) const
{
	if (!Target) return BIG_NUMBER;
	return FVector::Dist2D(GetPlayerPivot(), GetTargetPivot(Target));
}

bool ADMCameraCharacter::IsTargetInFrontHemisphere(AActor* Target) const
{
	if (!Target || !Controller) return false;

	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return false;

	const FVector CamForward = FRotationMatrix(PC->GetControlRotation()).GetUnitAxis(EAxis::X);
	const FVector ToTarget = (GetTargetPivot(Target) - GetPlayerPivot()).GetSafeNormal();
	return FVector::DotProduct(CamForward, ToTarget) > 0.f;
}

void ADMCameraCharacter::EnterLockOn(AActor* NewTarget)
{
	if (!NewTarget) return;

	CurrentLockTarget = NewTarget;
	PreviousLockTarget = nullptr;
	UnlockRecoveryRemaining = 0.f;
	LockOrbitYaw = 0.f;
	LockOrbitPitch = 0.f;
	PendingOrbitYawInput = 0.f;
	PendingOrbitPitchInput = 0.f;
}

void ADMCameraCharacter::ExitLockOn()
{
	PreviousLockTarget = CurrentLockTarget;
	CurrentLockTarget = nullptr;
	LockOrbitYaw = 0.f;
	LockOrbitPitch = 0.f;
	PendingOrbitYawInput = 0.f;
	PendingOrbitPitchInput = 0.f;
	UnlockRecoveryRemaining = UnlockRecoveryDelay;
}

