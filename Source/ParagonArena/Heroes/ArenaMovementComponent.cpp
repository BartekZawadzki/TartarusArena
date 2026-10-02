#include "Heroes/ArenaMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

UArenaMovementComponent::UArenaMovementComponent()
{
	// loose props are shoved by ApplyImpactPhysicsForces below; the engine's force-based push (PushForceFactor
	// 750000) launched a 40 kg crate, and any force-based push depends on the frame rate
	bEnablePhysicsInteraction = true;
	bPushForceScaledToMass = false;
	TouchForceFactor = 0.3f;
	StandingDownwardForceScale = 0.2f;
}

bool UArenaMovementComponent::IsUnstandable(const FHitResult& Hit)
{
	const UPrimitiveComponent* C = Hit.GetComponent();
	return Cast<APawn>(Hit.GetActor()) != nullptr || (C && C->IsSimulatingPhysics());
}

bool UArenaMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	return !IsUnstandable(Hit) && Super::IsWalkable(Hit);
}

void UArenaMovementComponent::ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity)
{
	UPrimitiveComponent* Prop = Impact.GetComponent();
	if (!bEnablePhysicsInteraction || !Prop || !Prop->IsSimulatingPhysics() || Cast<APawn>(Impact.GetActor()))
	{
		Super::ApplyImpactPhysicsForces(Impact, ImpactAcceleration, ImpactVelocity);
		return;
	}
	// a unit walking into a loose prop shoves it along the ground at its own walking pace: the prop slides ahead
	// (never up, never flung), the unit is never pinned behind it, and the result is the same at any frame rate
	FVector Dir = -Impact.ImpactNormal;
	Dir.Z = 0.f;
	if (!Dir.Normalize()) { return; }
	const float Walk = FMath::Min(ImpactVelocity.Size2D() > 10.f ? (float)ImpactVelocity.Size2D() : GetMaxSpeed(), 650.f);
	const FVector Current = Prop->GetPhysicsLinearVelocity();
	const float Along = FVector::DotProduct(Current, Dir);
	// the whole velocity is set, not added to: ahead at the walking pace (a faster prop keeps its speed, never more
	// than it had), its sideways drift halved, never lifted by the walker (a crate still moving from a blast went
	// 900-1100 cm/s when the push was added on top: -ArenaAnimLab)
	const FVector Side = FVector(Current.X, Current.Y, 0.f) - Dir * Along;
	const float Ahead = FMath::Max(Along, Walk);
	Prop->SetPhysicsLinearVelocity(Dir * Ahead + Side * 0.5f + FVector(0.f, 0.f, FMath::Min((float)Current.Z, 0.f)));
}
