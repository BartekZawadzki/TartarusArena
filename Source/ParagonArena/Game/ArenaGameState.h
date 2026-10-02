#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Game/ArenaGameMode.h"
#include "ArenaGameState.generated.h"

/** One hero's match record, alive or waiting to respawn: what the scoreboard, the top bar and the end screen show. */
USTRUCT()
struct FArenaHeroStat
{
	GENERATED_BODY()

	UPROPERTY() int32 Team = 0;
	UPROPERTY() int32 Hero = 0;
	UPROPERTY() int32 Level = 1;
	UPROPERTY() int32 Kills = 0;
	UPROPERTY() int32 Deaths = 0;
	UPROPERTY() int32 Assists = 0;
	UPROPERTY() int32 MinionKills = 0;
	UPROPERTY() float Gold = 0.f;
	UPROPERTY() float DamageToHeroes = 0.f;
	UPROPERTY() float DamageTaken = 0.f;
	UPROPERTY() float Healing = 0.f;
	UPROPERTY() TArray<int32> Items;
	UPROPERTY() bool bAlive = true;
	UPROPERTY() bool bPlayer = false;
	UPROPERTY() float RespawnAt = 0.f;   // server time
	UPROPERTY() float ReviveReadyAt = 0.f;   // server time the next paid revive is allowed
	UPROPERTY() FString KilledBy;
};

/** An announcement line (the big centre text and the feed), for the players on other machines (LAN, v16). */
USTRUCT()
struct FArenaFeedRep
{
	GENERATED_BODY()

	UPROPERTY() FString Text;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	UPROPERTY() bool bBig = false;
	UPROPERTY() int8 Team = -1;          // team-relative news: Text for Team, Other for the rest
	UPROPERTY() FString Other;
	UPROPERTY() FLinearColor OtherColor = FLinearColor::White;
};

/** A hero death for the kill feed (portraits). */
USTRUCT()
struct FArenaKillRep
{
	GENERATED_BODY()

	UPROPERTY() float Time = 0.f;        // server time
	UPROPERTY() int32 KillerHero = -1;
	UPROPERTY() int32 KillerTeam = -1;
	UPROPERTY() bool bKillerMinion = false;
	UPROPERTY() int32 VictimHero = -1;
	UPROPERTY() int32 VictimTeam = -1;
};

/** A structure of Conquest as every player sees it (the top bar, the minimap, the world bars). */
USTRUCT()
struct FArenaStructureRep
{
	GENERATED_BODY()

	UPROPERTY() uint8 Kind = 0;          // 1 tower, 2 inhibitor, 3 core
	UPROPERTY() uint8 Team = 0;
	UPROPERTY() int8 Lane = -1;
	UPROPERTY() uint8 Tier = 0;
	UPROPERTY() FVector_NetQuantize Pos = FVector::ZeroVector;
	UPROPERTY() float HpPct = 1.f;
	UPROPERTY() bool bAlive = true;
	UPROPERTY() bool bInvulnerable = false;
	UPROPERTY() bool bUnderAttack = false;
	UPROPERTY() float RespawnAt = 0.f;   // server time (a fallen inhibitor comes back)
};

/** A team's ping ("here!"): its allies see it on the ground and on the minimap for a few seconds. */
USTRUCT()
struct FArenaPingRep
{
	GENERATED_BODY()

	UPROPERTY() uint8 Team = 0;
	UPROPERTY() int32 Hero = -1;
	UPROPERTY() FVector_NetQuantize Pos = FVector::ZeroVector;
	UPROPERTY() float Time = 0.f;         // server time
};

/** A neutral camp: where, which buff, up or back at. */
USTRUCT()
struct FArenaCampRep
{
	GENERATED_BODY()

	UPROPERTY() uint8 Buff = 0;          // 0 gold, 1 red, 2 black, 3 the boss
	UPROPERTY() FVector_NetQuantize Pos = FVector::ZeroVector;
	UPROPERTY() bool bUp = false;
	UPROPERTY() float RespawnAt = 0.f;
	UPROPERTY() FString Name;
};

/**
 * The match as every player sees it, replicated from the server (the network foundation, ADR-11): the phase, the
 * clock, the score, one record per hero and the kill feed. The game mode stays the authority (it exists on the
 * server only) and writes this each frame; the HUD reads only this for these, so a client draws the same screens.
 */
UCLASS()
class PARAGONARENA_API AArenaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AArenaGameState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated) EArenaPhase Phase = EArenaPhase::HeroSelect;
	UPROPERTY(Replicated) float PhaseStart = 0.f;
	UPROPERTY(Replicated) float MatchStart = 0.f;
	UPROPERTY(Replicated) float MatchLength = 600.f;
	UPROPERTY(Replicated) bool bOvertime = false;
	UPROPERTY(Replicated) int32 ScoreA = 0;
	UPROPERTY(Replicated) int32 ScoreB = 0;
	UPROPERTY(Replicated) int32 ScoreLimit = 300;
	UPROPERTY(Replicated) int32 WinnerTeam = -1;
	UPROPERTY(Replicated) TArray<FArenaHeroStat> HeroStats;
	UPROPERTY(Replicated) TArray<FArenaKillRep> KillFeed;
	// Conquest (v14)
	UPROPERTY(Replicated) bool bConquest = false;
	UPROPERTY(Replicated) TArray<FArenaStructureRep> Structures;
	UPROPERTY(Replicated) TArray<FArenaCampRep> Camps;
	UPROPERTY(Replicated) int32 BossTeam = -1;
	UPROPERTY(Replicated) float BossUntil = 0.f;
	UPROPERTY(Replicated) TArray<FArenaPingRep> Pings;
	// LAN (v16): what the clients' HUD needs beyond the above
	UPROPERTY(Replicated) TArray<FArenaFeedRep> Feed;
	UPROPERTY(Replicated) bool bOrb = false;
	UPROPERTY(Replicated) FVector_NetQuantize OrbPos = FVector::ZeroVector;
	UPROPERTY(Replicated) int32 TeamSize = 5;
	UPROPERTY(Replicated) int32 MatchMinutes = 10;
	UPROPERTY(Replicated) int32 Difficulty = 1;
	UPROPERTY(Replicated) int32 Humans = 1;          // players connected (host included)
	/** Structures of a team still standing. */
	int32 StructuresLeft(int32 Team) const;

	int32 Score(int32 Team) const { return Team == 0 ? ScoreA : ScoreB; }
	/** Seconds left on the clock (the full length before the start). */
	float TimeLeft() const;
	/** This hero's record, or null. */
	const FArenaHeroStat* Stat(int32 Team, int32 Hero) const;
	static AArenaGameState* Get(const UObject* WorldContext);
};
