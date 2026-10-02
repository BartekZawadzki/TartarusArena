#include "Game/ArenaGameState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AArenaGameState::AArenaGameState()
{
	bReplicates = true;
}

void AArenaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaGameState, Phase);
	DOREPLIFETIME(AArenaGameState, PhaseStart);
	DOREPLIFETIME(AArenaGameState, MatchStart);
	DOREPLIFETIME(AArenaGameState, MatchLength);
	DOREPLIFETIME(AArenaGameState, bOvertime);
	DOREPLIFETIME(AArenaGameState, ScoreA);
	DOREPLIFETIME(AArenaGameState, ScoreB);
	DOREPLIFETIME(AArenaGameState, ScoreLimit);
	DOREPLIFETIME(AArenaGameState, WinnerTeam);
	DOREPLIFETIME(AArenaGameState, HeroStats);
	DOREPLIFETIME(AArenaGameState, KillFeed);
	DOREPLIFETIME(AArenaGameState, bConquest);
	DOREPLIFETIME(AArenaGameState, Structures);
	DOREPLIFETIME(AArenaGameState, Camps);
	DOREPLIFETIME(AArenaGameState, BossTeam);
	DOREPLIFETIME(AArenaGameState, BossUntil);
	DOREPLIFETIME(AArenaGameState, Pings);
	DOREPLIFETIME(AArenaGameState, Feed);
	DOREPLIFETIME(AArenaGameState, bOrb);
	DOREPLIFETIME(AArenaGameState, OrbPos);
	DOREPLIFETIME(AArenaGameState, TeamSize);
	DOREPLIFETIME(AArenaGameState, MatchMinutes);
	DOREPLIFETIME(AArenaGameState, Difficulty);
	DOREPLIFETIME(AArenaGameState, Humans);
}

float AArenaGameState::TimeLeft() const
{
	if (Phase == EArenaPhase::HeroSelect || Phase == EArenaPhase::Countdown) { return MatchLength; }
	const float End = Phase == EArenaPhase::Ended ? PhaseStart : GetServerWorldTimeSeconds();
	return FMath::Max(0.f, MatchLength - (End - MatchStart));
}

int32 AArenaGameState::StructuresLeft(int32 Team) const
{
	int32 N = 0;
	for (const FArenaStructureRep& S : Structures) { N += S.Team == Team && S.bAlive ? 1 : 0; }
	return N;
}

const FArenaHeroStat* AArenaGameState::Stat(int32 Team, int32 Hero) const
{
	return HeroStats.FindByPredicate([&](const FArenaHeroStat& S) { return S.Team == Team && S.Hero == Hero; });
}

AArenaGameState* AArenaGameState::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetGameState<AArenaGameState>() : nullptr;
}
