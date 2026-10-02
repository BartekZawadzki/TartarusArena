#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ArenaBotController.generated.h"

class AArenaCharacter;

/** Actuates ArenaBot::Decide for heroes and minions; logs stuck episodes (VR-08). */
UCLASS()
class PARAGONARENA_API AArenaBotController : public AAIController
{
	GENERATED_BODY()

public:
	AArenaBotController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;

	int32 Difficulty = 1;
	int32 StuckEpisodes = 0;
	TArray<FVector> LanePath;   // minions: waypoints to walk before heading for the enemy portal
	int32 LaneStep = 0;
	// ---- Conquest (v14) ----
	int32 Lane = -1;            // a lane minion's lane
	int32 HomeLane = -1;        // the lane a hero pushes
	bool bMonster = false;      // a neutral camp monster or the boss: guards its home, gives up past its leash
	FVector Home = FVector::ZeroVector;
	float HomeYaw = 0.f;
	float LeashCm = 900.f;
	TWeakObjectPtr<AArenaCharacter> Foe;

private:
	void TickMonster(AArenaCharacter* Me, class AArenaGameMode* GM, float Now, float Dt);
	/** Conquest: where a hero goes when nothing is in reach (defend, the boss, its camp, its lane) and which camp's
	 *  monsters it may fight on the way (-1 none). */
	FVector ConquestGoal(AArenaCharacter* Me, class AArenaGameMode* GM, float Now, int32& OutCamp);
	int32 GoalCamp = -1;
	FVector GoalAt = FVector::ZeroVector;
	float NextGoal = 0.f;
	float NextThink = 0.f;
	FVector LastPos = FVector::ZeroVector;
	float StillSince = -1.f;
	bool bStuckLogged = false;
	float NextUnstick = 0.f;
	int32 UnstickTries = 0;
	FVector LastMoveTo = FVector::ZeroVector;
	bool bLastRetreat = false;
public:
	bool IsRetreating() const { return bLastRetreat; }
private:
	bool bWantsTravel = false;
	float StrafeSign = 1.f;
	float NextStrafeFlip = 0.f;
	float NextJump = 0.f;
	/** v20: a jump with a cooldown (and never while casting, stunned, dashing or already in the air). */
	bool BotJump(class AArenaCharacter* Me, float Now, const TCHAR* Why);
public:
	static int32 Jumps;
private:
};
