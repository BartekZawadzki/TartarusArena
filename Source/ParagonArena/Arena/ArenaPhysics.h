// Physics helpers: blasts push every simulating body (props) in range. Presentation-plus-physics, never game
// rules — damage and CC are decided in ArenaAbilityHelpers.
#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Components/PrimitiveComponent.h"

namespace ArenaPhysics
{
	/** A shove, not a launch: mostly outward along the ground, a little lift, capped at MaxSpeed cm/s — a
	 *  40 kg crate tumbles a few metres instead of flying over the arena. Strength is the speed at the centre. */
	inline int32 RadialImpulse(UWorld* World, const FVector& Center, float RadiusCm, float Strength, float MaxSpeed = 650.f)
	{
		if (!World || RadiusCm <= 0.f) { return 0; }
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Q;
		Q.AddObjectTypesToQuery(ECC_PhysicsBody);
		Q.AddObjectTypesToQuery(ECC_WorldDynamic);
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Q, FCollisionShape::MakeSphere(RadiusCm));
		int32 N = 0;
		for (const FOverlapResult& O : Overlaps)
		{
			UPrimitiveComponent* P = O.GetComponent();
			if (!P || !P->IsSimulatingPhysics()) { continue; }
			const FVector To = P->GetComponentLocation() - Center;
			const float Falloff = FMath::Clamp(1.f - To.Size() / RadiusCm, 0.f, 1.f);
			FVector Dir = To.GetSafeNormal2D();
			if (Dir.IsNearlyZero()) { Dir = FVector::ForwardVector; }
			const float Speed = FMath::Min(MaxSpeed, Strength * 0.3f * Falloff);
			// the resulting speed is capped, not only the push: a prop already moving is not sped past MaxSpeed
			const FVector After = P->GetPhysicsLinearVelocity() + (Dir + FVector(0.f, 0.f, 0.3f)).GetSafeNormal() * Speed;
			P->SetPhysicsLinearVelocity(After.GetClampedToMaxSize(MaxSpeed));
			++N;
		}
		return N;
	}

	/** Props settle instead of skating or spinning forever, and never pop out of an overlap at high speed. */
	inline void CalmProp(UPrimitiveComponent* P)
	{
		if (!P || !P->IsSimulatingPhysics()) { return; }
		P->SetLinearDamping(0.6f);
		P->SetAngularDamping(1.2f);
		P->SetPhysicsMaxAngularVelocityInDegrees(540.f);
		if (FBodyInstance* B = P->GetBodyInstance())
		{
			B->SetMaxDepenetrationVelocity(60.f);
		}
	}
}
