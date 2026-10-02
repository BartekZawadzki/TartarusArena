#include "Heroes/ArenaSpringArm.h"

FVector UArenaSpringArm::BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime)
{
	// PreviousArmOrigin is this frame's (lagged) pivot: USpringArmComponent sets it before the trace
	const FVector Origin = PreviousArmOrigin;
	const float Full = FVector::Dist(Origin, DesiredArmLocation);
	if (Full < 1.f) { return DesiredArmLocation; }
	const float Allowed = bHitSomething ? FVector::Dist(Origin, TraceHitLocation) : Full;
	bBlocked = bHitSomething;
	if (CurrentLength < 0.f || Allowed <= CurrentLength) { CurrentLength = Allowed; }   // in at once
	else { CurrentLength = FMath::Min(Allowed, FMath::FInterpTo(CurrentLength, Allowed, DeltaTime, ReturnSpeed)); }   // out smoothly
	return Origin + (DesiredArmLocation - Origin) * (CurrentLength / Full);
}
