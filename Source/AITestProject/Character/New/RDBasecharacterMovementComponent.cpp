#include "Character/New/RDBasecharacterMovementComponent.h"

#include "Character/New/RDBasecharacter.h"
#include "Character/New/WallRunSurfaceComponent.h"
#include "Engine/World.h"

void URDBasecharacterMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	Super::PhysFalling(DeltaTime, Iterations);

	if (MovementMode != MOVE_Falling || !HasValidData() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (AirLateralDragDeceleration <= 0.0f || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity())
	{
		return;
	}

	const FVector LateralVelocity = ProjectToGravityFloor(Velocity);
	const float LateralSpeed = LateralVelocity.Size();
	if (LateralSpeed <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float NewLateralSpeed = FMath::Max(0.0f, LateralSpeed - (AirLateralDragDeceleration * DeltaTime));
	const FVector NewLateralVelocity = (NewLateralSpeed > 0.0f) ? (LateralVelocity * (NewLateralSpeed / LateralSpeed)) : FVector::ZeroVector;
	const FVector VerticalVelocity = Velocity - LateralVelocity;
	Velocity = VerticalVelocity + NewLateralVelocity;
}

void URDBasecharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode == static_cast<uint8>(ERDBasecharacterCustomMovementMode::CMOVE_WallRun))
	{
		PhysWallRun(DeltaTime, Iterations);
		return;
	}

	Super::PhysCustom(DeltaTime, Iterations);
}

void URDBasecharacterMovementComponent::PhysWallRun(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	ARDBasecharacter* BaseCharacter = Cast<ARDBasecharacter>(PawnOwner);
	if (!BaseCharacter || !UpdatedComponent || !BaseCharacter->bIsWallRunning)
	{
		SetMovementMode(MOVE_Falling);
		return;
	}

	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	UWallRunSurfaceComponent* Surface = BaseCharacter->CurrentWallRunSurface.Get();
	const UWallRunSurfaceComponent* PreviousSurface = Surface;
	if ((!Surface || !BaseCharacter->HasOverlappingWallRunSurface(Surface)) && BaseCharacter->FindBestWallRunSurface())
	{
		Surface = BaseCharacter->FindBestWallRunSurface();
		BaseCharacter->CurrentWallRunSurface = Surface;
	}

	if (Surface && BaseCharacter->HasOverlappingWallRunSurface(Surface) && Surface->IsWallRunEnabled())
	{
		BaseCharacter->CurrentWallNormal = Surface->GetWallNormal();
		BaseCharacter->CurrentWallRunDirection = Surface->GetWallRunDirectionFromVelocity(
			BaseCharacter->CurrentWallRunDirection.IsNearlyZero() ? Velocity : BaseCharacter->CurrentWallRunDirection);
		if (Surface != PreviousSurface || BaseCharacter->CurrentWallRunSide == EWallRunSide::None)
		{
			BaseCharacter->CurrentWallRunSide = BaseCharacter->DetermineWallRunSide(Surface, BaseCharacter->CurrentWallRunDirection);
		}
		BaseCharacter->WallRunLastWallContactTime = CurrentTime;
	}
	else if ((CurrentTime - BaseCharacter->WallRunLastWallContactTime) > BaseCharacter->LostWallForgivenessTime)
	{
		BaseCharacter->StopWallRun(false);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	if ((CurrentTime - BaseCharacter->WallRunStartTime) > BaseCharacter->GetWallRunMaxDurationForCurrentSurface())
	{
		BaseCharacter->StopWallRun(false);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	if (!Acceleration.IsNearlyZero())
	{
		const FVector DesiredFromInput = BaseCharacter->ComputeWallRunDirection(
			BaseCharacter->CurrentWallNormal,
			FVector(Acceleration.X, Acceleration.Y, 0.f));

		BaseCharacter->CurrentWallRunDirection = FMath::VInterpTo(
			BaseCharacter->CurrentWallRunDirection,
			DesiredFromInput,
			DeltaTime,
			6.f).GetSafeNormal();
	}

	const FVector WallRunDirection = BaseCharacter->CurrentWallRunDirection.GetSafeNormal();
	float AlongWallSpeed = FMath::Abs(FVector::DotProduct(Velocity, WallRunDirection));

	const float TargetWallRunSpeed = BaseCharacter->GetWallRunTargetSpeed();
	const float AccelerationInterpSpeed = BaseCharacter->WallRunAcceleration / FMath::Max(TargetWallRunSpeed, 1.f);
	AlongWallSpeed = FMath::FInterpTo(AlongWallSpeed, TargetWallRunSpeed, DeltaTime, AccelerationInterpSpeed);

	if (AlongWallSpeed < BaseCharacter->MinSpeedToMaintainWallRun)
	{
		BaseCharacter->StopWallRun(false);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	const float NewVerticalVelocity = Velocity.Z + GetGravityZ() * BaseCharacter->WallRunGravityScale * DeltaTime;
	const FVector AttractionVelocity = -BaseCharacter->CurrentWallNormal.GetSafeNormal() * BaseCharacter->WallAttractionForce;
	Velocity = (WallRunDirection * AlongWallSpeed) + AttractionVelocity + (FVector::UpVector * NewVerticalVelocity);

	FRotator DesiredRotation = WallRunDirection.Rotation();
	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll = 0.f;

	const FQuat TargetQuat = FMath::QInterpTo(
		UpdatedComponent->GetComponentQuat(),
		DesiredRotation.Quaternion(),
		DeltaTime,
		BaseCharacter->WallRunRotationInterpSpeed);

	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Velocity * DeltaTime, TargetQuat, true, Hit);

	if (Hit.IsValidBlockingHit())
	{
		if (IsWalkable(Hit))
		{
			BaseCharacter->StopWallRun(false);
			StartNewPhysics(DeltaTime, Iterations);
			return;
		}

		HandleImpact(Hit, DeltaTime, Velocity * DeltaTime);
		SlideAlongSurface(Velocity * DeltaTime, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
}
