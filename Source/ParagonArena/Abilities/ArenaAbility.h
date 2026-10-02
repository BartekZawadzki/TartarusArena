#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Actor.h"
#include "Data/ArenaTypes.h"
#include "ArenaAbility.generated.h"

class AArenaCharacter;
class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class UPointLightComponent;

namespace ArenaAbilityHelpers
{
	/** Damage + CC every hostile in a sphere, once per target per registry (VR-03, VR-04). Returns hits. */
	int32 DamageInRadius(AArenaCharacter* Source, const FVector& Center, float RadiusCm, const FArenaAbilityDef& Ab, float BaseDamage, TSet<uint64>* Registry, float ConeCos = -2.f, const FVector& Forward = FVector::ZeroVector);
	void ApplyHitEffects(AArenaCharacter* Source, AArenaCharacter* Target, const FArenaAbilityDef& Ab, float BaseDamage);
	float ScaledDamage(const AArenaCharacter* Source, const FArenaAbilityDef& Ab, float BaseDamage);

	// ---- targeting rules shared by the abilities and their on-screen indicators (what is shown is what lands) ----
	/** Horizontal aim direction from the caster towards the aim point (its facing when the aim is on top of it). */
	FVector AimDirection(const AArenaCharacter* Hero, const FVector& Aim);
	/** Ground AoE landing point: the aim clamped to the ability's range, dropped onto the ground below. */
	FVector GroundTarget(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& Aim);
	/** How far a shot flies along Dir before a wall stops it (cm, at most RangeCm). */
	float ProjectileReach(const AArenaCharacter* Hero, const FVector& Dir, float RangeCm);
	/** The ally a LowestAlly buff picks: lowest health share in range, the caster included. */
	AArenaCharacter* LowestAlly(AArenaCharacter* Hero, const FArenaAbilityDef& Ab);
	/** No wall, rock or tree between the two points (units and loose props never block). */
	bool LineOfSight(const UWorld* World, const FVector& From, const FVector& To, const AActor* IgnoreA, const AActor* IgnoreB);

	// ---- aim assistance for the human player (Smite-style forgiveness; bots aim on their own) ------------------
	/** Whether the player's attacks get assistance (console: arena.AimAssist 0/1). */
	bool AimAssistOn();
	/** The enemy this attack would go for: a melee swing takes the closest enemy in reach inside a wide cone around
	 *  the view direction; a shot takes the enemy on or close to the view ray (a band that widens with distance).
	 *  Only enemies in reach and in line of sight; nullptr for other archetypes. */
	AArenaCharacter* FindAimTarget(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& ViewOrigin, const FVector& ViewDir);
	/** Where to aim at the target: its body now (melee), or where it will be when the shot arrives (projectile). */
	FVector LeadPoint(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const AArenaCharacter* Target);
	/** Where a lobbed shot comes down: the aimed ground point, pulled in to the ability's range. */
	FVector LobLanding(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& Aim);
	/** The swing cone and whether it connected, flashed on the ground for the player (learn the reach, see a miss). */
	void FlashSwing(AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& Dir, bool bConnected);
	/** LAN (v16): a cast the server made, drawn on a client: the animation, the cast effect, then the shot, area or
	 *  blow as the server has it (visual only: the damage is the server's). */
	void PlayRemoteCast(AArenaCharacter* Hero, int32 Slot, int32 Variant, const FVector& Aim, AArenaCharacter* Target);
}

/** One GAS ability class; its behaviour is data (FArenaAbilityDef from heroes.json). InputID = slot. */
UCLASS()
class PARAGONARENA_API UArenaAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UArenaAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	void Execute(AArenaCharacter* Hero, FArenaAbilityDef Ab, FVector Aim, TWeakObjectPtr<AArenaCharacter> Target);
	float PlayAnim(AArenaCharacter* Hero, const FArenaAbilityDef& Ab);
	int32 VariantIndex = 0;
};

/** Skillshot projectile (optionally piercing / exploding / arcing). */
UCLASS()
class PARAGONARENA_API AArenaProjectile : public AActor
{
	GENERATED_BODY()

public:
	AArenaProjectile();
	void Init(AArenaCharacter* InSource, const FArenaAbilityDef& InAb, const FVector& Direction);
	/** LAN: a copy drawn on a client (no damage). */
	bool bVisualOnly = false;
	/** For the bots' dodge: whose shot, its flight, and whether it can be sidestepped at all (a homing one cannot). */
	int32 SourceTeam() const;
	FVector GetFlightVelocity() const;
	bool IsDecorative() const { return bVisualOnly || bHoming || bDone; }
	/** A Targeted shot: follows Target until it reaches it (walls and other units do not stop it). */
	void InitHoming(AArenaCharacter* InSource, const FArenaAbilityDef& InAb, AArenaCharacter* Target);
	virtual void Tick(float DeltaSeconds) override;
	/** A lobbed shot (gravity > 0): the arc that lands on Land. */
	void LobTo(const FVector& From, const FVector& Land);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;

private:
	UFUNCTION() void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
	UFUNCTION() void OnHitWorld(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, FVector Impulse, const FHitResult& Hit);
	void Explode(const FVector& At);

	TWeakObjectPtr<AArenaCharacter> Source;
	FArenaAbilityDef Ab;
	TSet<uint64> Hits;
	bool bDone = false;
	TWeakObjectPtr<AArenaCharacter> Homing;
	bool bHoming = false;
};

/** Delayed ground AoE with a telegraph disc (readability: you see it coming). */
UCLASS()
class PARAGONARENA_API AArenaAreaEffect : public AActor
{
	GENERATED_BODY()

public:
	AArenaAreaEffect();
	void Init(AArenaCharacter* InSource, const FArenaAbilityDef& InAb);
	/** For the bots' dodging: the thrower's team (-1 gone), the radius (cm), landed already. */
	int32 SourceTeam() const;
	float RadiusCm() const { return Ab.Radius * 100.f; }
	bool bLanded = false;
	bool bVisualOnly = false;   // LAN: a copy drawn on a client (no damage)

	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;

private:
	void Detonate();
	TWeakObjectPtr<AArenaCharacter> Source;
	FArenaAbilityDef Ab;
};
