#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RDBasecharacterMovementComponent.generated.h"

UCLASS()
class AITESTPROJECT_API URDBasecharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

protected:
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Movement: Jumping / Falling", meta = (ClampMin = "0.0", UIMin = "0.0", ForceUnits = "cm/s^2"))
	float AirLateralDragDeceleration = 0.0f;

	void PhysWallRun(float DeltaTime, int32 Iterations);
};
