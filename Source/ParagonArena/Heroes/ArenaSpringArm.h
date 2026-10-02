#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "ArenaSpringArm.generated.h"

/**
 * The player's camera boom. A wall, a pillar or a tree between the hero and the camera pulls the camera in at once
 * (never a view through geometry), and once the view clears it glides back out instead of snapping to full length
 * (the stock spring arm jumps back the frame the obstacle is passed).
 */
UCLASS()
class PARAGONARENA_API UArenaSpringArm : public USpringArmComponent
{
	GENERATED_BODY()

public:
	/** How fast the arm grows back after an obstacle (1/s, exponential). */
	float ReturnSpeed = 5.f;
	/** Current allowed arm length (cm); read by the labs. */
	float CurrentLength = -1.f;
	/** The last frame's trace hit something. */
	bool bBlocked = false;

protected:
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime) override;
};
