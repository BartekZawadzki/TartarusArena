// The prototype's test bot: the same character and verbs as the player. It sees (sight, hearing, the sneak), searches,
// hunts, circles the player locked on (the 8-way jog), reads the player's swings and dodges them (a perfect dodge on the
// harder levels), interrupts a charge, mixes combos, heavies, launches and air slashes. Difficulty changes its reaction
// time and how often it reads the player right, never its aim. v21: no blocking (the prototype has no guard).
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ProtoBotController.generated.h"

class AProtoCharacter;

UCLASS()
class PARAGONARENA_API AProtoBotController : public AAIController
{
	GENERATED_BODY()

public:
	AProtoBotController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;

	int32 Difficulty = 1;
	bool bPassive = false;                 // the sandbox: it patrols and dodges, never attacks
	bool bHades = false;                   // prototype 2: the Hades moves (the cast, the dash-strike, the call), no lock-on
	// what it did (the ProtoLab reads them)
	int32 Lights = 0, Heavies = 0, Launchers = 0, SprintAttacks = 0, Dodges = 0, ReadDodges = 0, AirCombos = 0, Jumps = 0, Plunges = 0, Interrupts = 0;
	int32 Casts = 0, DashStrikes = 0, Smashes = 0, Calls = 0;

	/** Does the bot see Target now (distance, line of sight, the sneak, its facing). */
	bool Sees(const AProtoCharacter* Me, const AProtoCharacter* Target) const;
	/** Not seeing the enemy but looking for it: where it was last seen, else around where it roughly is (the HUD's "?"). */
	bool bSearching = false;

private:
	AProtoCharacter* Me() const;
	AProtoCharacter* FindTarget(AProtoCharacter* Self) const;
	void Engage(AProtoCharacter* Self, AProtoCharacter* T, float Now, float Dist);
	void ReadThreat(AProtoCharacter* Self, AProtoCharacter* T, float Now, float Dist);
	float Skill() const { return Difficulty <= 0 ? 0.35f : (Difficulty == 1 ? 0.6f : 0.85f); }
	float Reaction() const { return Difficulty <= 0 ? 0.45f : (Difficulty == 1 ? 0.28f : 0.18f); }
	float NextThink = 0.f, NextAction = 0.f, NextJump = 0.f, NextPatrol = 0.f, NextSearch = 0.f, HeavyReleaseAt = -1.f, LastSeen = -100.f;
	float DodgeAt = -1.f, LastReadSwing = -100.f, LastReadCharge = -100.f, StrafeUntil = -1.f, AirSlashAt = -1.f, DashStrikeAt = -1.f;
	FVector DodgeDir = FVector::ZeroVector;
	int32 ComboLeft = 0, AirLeft = 0;
	float NextComboPress = 0.f;
	int32 PatrolIndex = 0;
	float StrafeSign = 1.f, StrafeFwd = 0.f;
	FVector LastKnown = FVector::ZeroVector;
	TWeakObjectPtr<AProtoCharacter> Foe;
};
