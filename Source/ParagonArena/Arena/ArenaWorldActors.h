#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaWorldActors.generated.h"

class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UNavModifierComponent;

/** Launches a hero that steps on it (mobility across the arena). Minions walk past it and bots path around it
 *  (an obstacle area on the navmesh), so nobody is catapulted by accident. Placed by Tools/build_arena.py. */
UCLASS()
class PARAGONARENA_API AArenaJumpPad : public AActor
{
	GENERATED_BODY()

public:
	AArenaJumpPad();

	/** Launch velocity in cm/s, in the pad's local space (X = forward). */
	UPROPERTY(EditAnywhere, Category = "Arena") FVector LaunchVelocity = FVector(900.f, 0.f, 1300.f);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Trigger;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Pad;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UNavModifierComponent> NavModifier;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	TMap<TWeakObjectPtr<AActor>, float> LastLaunch;
};

/** "Moc Tartaru" — a contested centre objective: +25 % damage and +20 % speed for 30 s. */
UCLASS()
class PARAGONARENA_API AArenaPowerOrb : public AActor
{
	GENERATED_BODY()

public:
	AArenaPowerOrb();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Orb;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	FVector Base = FVector::ZeroVector;
};
