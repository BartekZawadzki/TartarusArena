#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ArenaMovementComponent.generated.h"

/** Character movement for heroes and minions: another character's head or a loose physics prop is never a floor
 *  (a unit that lands there slides off along the capsule instead of standing or being launched off), and walking
 *  into props nudges them at walking pace instead of flinging them. */
UCLASS()
class PARAGONARENA_API UArenaMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UArenaMovementComponent();
	virtual bool IsWalkable(const FHitResult& Hit) const override;
	virtual void ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity) override;

	/** True when the hit is something a character must not stand on (a pawn or a simulating body). */
	static bool IsUnstandable(const FHitResult& Hit);
};
