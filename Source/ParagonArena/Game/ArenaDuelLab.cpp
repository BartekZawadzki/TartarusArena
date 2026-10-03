// -ArenaDuelLab (v17): the melee-ranged balance measured with the game's own rules, abilities and AI. Every melee hero
// fights every ranged hero one on one, both bots on hard (no stat edge), level 9 with their ranks, 15 m apart on base
// B's plateau (no minions, no fountain); twice each, sides swapped. A duel ends with a death or, after 45 s, on the
// health left. Logs one DUEL line per fight, the win rate per hero and per group; LAB_SUMMARY fails=0 always (a
// measurement, not a check).
#include "Game/ArenaGameMode.h"
#include "Game/ArenaEvidence.h"
#include "Heroes/ArenaCharacter.h"
#include "AI/ArenaBotController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"

namespace
{
	struct FDuelLabState
	{
		TArray<TPair<int32, int32>> Pairs;   // ranged, melee
		int32 Next = 0, Swap = 0;
		TWeakObjectPtr<AArenaCharacter> A, B;   // A = ranged
		float StartT = -1.f, PauseUntil = 0.f;
		int32 Wins[2] = { 0, 0 }, Draws = 0;     // [0] ranged, [1] melee
		TMap<int32, FIntPoint> PerHero;         // hero -> (wins, fights)
		float TimeSum = 0.f;
		FVector Centre = FVector::ZeroVector;
	};
	FDuelLabState GDuel;

	FVector DuelGround(UWorld* W, const FVector& At)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(DuelGround), false);
		if (W->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 600.f), At - FVector(0.f, 0.f, 800.f), ECC_Visibility, Q)) { return Hit.ImpactPoint; }
		return At;
	}
}

void AArenaGameMode::StartDuelLab()
{
	GDuel = FDuelLabState();
	LabStart = GetWorld()->GetTimeSeconds();
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	for (int32 r = 0; r < Defs.Num(); ++r)
	{
		if (!Defs[r].Abilities.IsValidIndex(0) || Defs[r].Abilities[0].Range < 5.f) { continue; }
		for (int32 m = 0; m < Defs.Num(); ++m)
		{
			if (!Defs[m].Abilities.IsValidIndex(0) || Defs[m].Abilities[0].Range >= 5.f) { continue; }
			GDuel.Pairs.Add(TPair<int32, int32>(r, m));
		}
	}
	// -DuelOnly=Ranged-Melee -DuelRepeat=N: one pair N times (a crash hunt, a single matchup)
	FString Only;
	if (FParse::Value(FCommandLine::Get(), TEXT("DuelOnly="), Only))
	{
		FString Rn, Mn;
		Only.Split(TEXT("-"), &Rn, &Mn);
		const int32 R = Defs.IndexOfByPredicate([&](const FArenaHeroDef& D) { return D.Id.ToString() == Rn; });
		const int32 M = Defs.IndexOfByPredicate([&](const FArenaHeroDef& D) { return D.Id.ToString() == Mn; });
		int32 Repeat = 10;
		FParse::Value(FCommandLine::Get(), TEXT("DuelRepeat="), Repeat);
		GDuel.Pairs.Reset();
		if (R >= 0 && M >= 0) { for (int32 i = 0; i < Repeat; ++i) { GDuel.Pairs.Add(TPair<int32, int32>(R, M)); } }
	}
	// -DuelSeed=N: the bots' random choices (aim error, reaction, strafe) from seed N — one round is deterministic for
	// its seed, and every fight shifts the random stream of the next ones: several seeds pooled give the rates
	int32 DuelSeed = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("DuelSeed="), DuelSeed)) { FMath::RandInit(DuelSeed); FMath::SRandInit(DuelSeed); }
	int32 From = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("DuelStart="), From)) { GDuel.Next = FMath::Clamp(From, 0, GDuel.Pairs.Num() * 2); }
	// the ground: base B's flat plateau. Its base is then moved far off (both ways, symmetric): standing inside it, the
	// ranged hero (team A) took the spot for the enemy fountain and only backed off, and the melee hero (team B) counted
	// as "safe" in its own base and was never shot at — every duel up to r12 was skewed towards melee
	// (moved once the navmesh is up: NavReady looks for it at the bases)
	GDuel.Centre = TeamBase(1) + FVector(-600.f, 0.f, 0.f);
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=duellab_start pairs=%d from=%d"), GDuel.Pairs.Num(), GDuel.Next);
}

void AArenaGameMode::TickDuelLab(float Now)
{
	const float T = Now - LabStart;
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	if (LabStep == 0)
	{
		if (!NavReady() || T < 1.f) { return; }
		Phase = EArenaPhase::Playing;
		MatchStart = Now;
		BaseA = GDuel.Centre + FVector(-60000.f, 0.f, 0.f);
		BaseB = GDuel.Centre + FVector(60000.f, 0.f, 0.f);
		LabStep = 1;
		return;
	}
	if (LabStep == 1)
	{
		AArenaCharacter* A = GDuel.A.Get();
		AArenaCharacter* B = GDuel.B.Get();
		if (GDuel.StartT < 0.f)
		{
			if (Now < GDuel.PauseUntil) { return; }
			static int32 Count = -1;
			if (Count < 0) { Count = 0; FParse::Value(FCommandLine::Get(), TEXT("DuelCount="), Count); Count = Count > 0 ? GDuel.Next + Count : TNumericLimits<int32>::Max(); }
			if (GDuel.Next >= GDuel.Pairs.Num() * 2 || GDuel.Next >= Count) { LabStep = 2; return; }
			const TPair<int32, int32> P = GDuel.Pairs[GDuel.Next / 2];
			GDuel.Swap = GDuel.Next % 2;
			++GDuel.Next;
			// 15 m apart across base B's plateau; the sides swap on the second fight
			const FVector Centre = GDuel.Centre;
			const FVector PosA = DuelGround(GetWorld(), Centre + FVector(0.f, GDuel.Swap ? 750.f : -750.f, 0.f));
			const FVector PosB = DuelGround(GetWorld(), Centre + FVector(0.f, GDuel.Swap ? -750.f : 750.f, 0.f));
			auto Make = [&](int32 Hero, int32 Team, const FVector& Pos, const FVector& FaceTo) -> AArenaCharacter*
			{
				AArenaCharacter* C = SpawnHero(Hero, Team, 2, false, 9, 0.f, nullptr);
				if (!C) { return nullptr; }
				if (AArenaBotController* AI = Cast<AArenaBotController>(C->GetController())) { AI->Difficulty = 2; }
				C->SetBotEdge(0.f, 0.f);
				C->AutoRank();
				C->RefillVitals();
				C->SetActorLocation(Pos + FVector(0.f, 0.f, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
				C->SetActorRotation((FaceTo - Pos).GetSafeNormal2D().Rotation());
				return C;
			};
			GDuel.A = Make(P.Key, 0, PosA, PosB);
			GDuel.B = Make(P.Value, 1, PosB, PosA);
			GDuel.StartT = Now;
			return;
		}
		const float Elapsed = Now - GDuel.StartT;
		static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("DuelTrace"));
		if (bTrace && A && B && FMath::FloorToInt(Elapsed) != FMath::FloorToInt(Elapsed - GetWorld()->GetDeltaSeconds()))
		{
			ARENA_LOG(LogArena, Display, TEXT("DUEL_TRACE t=%.0f dist=%.0f A=%s hp=%.0f%% v=%.0f max=%.0f busy=%d stun=%d | B=%s hp=%.0f%% v=%.0f max=%.0f busy=%d stun=%d dz=%.0f"), Elapsed, FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()),
				*A->GetDef().Id.ToString(), A->HealthPct() * 100.f, A->GetVelocity().Size2D(), A->GetCharacterMovement()->MaxWalkSpeed, A->IsBusy() ? 1 : 0, A->IsStunned() ? 1 : 0,
				*B->GetDef().Id.ToString(), B->HealthPct() * 100.f, B->GetVelocity().Size2D(), B->GetCharacterMovement()->MaxWalkSpeed, B->IsBusy() ? 1 : 0, B->IsStunned() ? 1 : 0, B->GetActorLocation().Z - A->GetActorLocation().Z);
		}
		const bool bADead = !A || !A->IsAlive(), bBDead = !B || !B->IsAlive();
		if (!bADead && !bBDead && Elapsed < 45.f) { return; }
		// the result: a death, or the health left after 45 s (within 5 % a draw)
		int32 Winner = -1;
		if (bADead != bBDead) { Winner = bADead ? 1 : 0; }
		else if (!bADead)
		{
			const float Ha = A->HealthPct(), Hb = B->HealthPct();
			Winner = FMath::Abs(Ha - Hb) < 0.05f ? -1 : (Ha > Hb ? 0 : 1);
		}
		const TPair<int32, int32> P = GDuel.Pairs[(GDuel.Next - 1) / 2];
		if (Winner < 0) { ++GDuel.Draws; } else { ++GDuel.Wins[Winner]; }
		FIntPoint& Ra = GDuel.PerHero.FindOrAdd(P.Key);
		FIntPoint& Mb = GDuel.PerHero.FindOrAdd(P.Value);
		Ra.Y += 1; Mb.Y += 1;
		if (Winner == 0) { Ra.X += 1; } else if (Winner == 1) { Mb.X += 1; }
		GDuel.TimeSum += Elapsed;
		ARENA_LOG(LogArena, Display, TEXT("DUEL ranged=%s melee=%s winner=%s time=%.1f hp_ranged=%.0f%% hp_melee=%.0f%% swap=%d"), *Defs[P.Key].Id.ToString(), *Defs[P.Value].Id.ToString(),
			Winner < 0 ? TEXT("draw") : (Winner == 0 ? TEXT("ranged") : TEXT("melee")), Elapsed, A ? A->HealthPct() * 100.f : 0.f, B ? B->HealthPct() * 100.f : 0.f, GDuel.Swap);
		// both leave by the ordinary death (destroying a hero mid-ability crashed the engine): the bodies fade out, the
		// lab does not respawn them
		for (AArenaCharacter* C : { A, B })
		{
			if (!C) { continue; }
			ARENA_LOG(LogArena, Display, TEXT("DUEL_END kill %s alive=%d dashing=%d rootmotion=%d"), *C->GetDef().Id.ToString(), C->IsAlive() ? 1 : 0, C->IsDashing() ? 1 : 0, C->IsPlayingRootMotion() ? 1 : 0);
			C->LabKill();
			ARENA_LOG(LogArena, Display, TEXT("DUEL_END killed %s"), *C->GetDef().Id.ToString());
			Heroes.Remove(C);
		}
		GDuel.A = nullptr; GDuel.B = nullptr;
		GDuel.StartT = -1.f;
		GDuel.PauseUntil = Now + 1.f;
		return;
	}
	if (LabStep == 2)
	{
		const int32 Fights = GDuel.Wins[0] + GDuel.Wins[1] + GDuel.Draws;
		for (const TPair<int32, FIntPoint>& H : GDuel.PerHero)
		{
			ARENA_LOG(LogArena, Display, TEXT("DUEL_HERO %-10s %s wins=%d/%d (%.0f%%)"), *Defs[H.Key].Id.ToString(), Defs[H.Key].Abilities[0].Range < 5.f ? TEXT("melee ") : TEXT("ranged"), H.Value.X, H.Value.Y, 100.f * H.Value.X / FMath::Max(1, H.Value.Y));
		}
		ARENA_LOG(LogArena, Display, TEXT("DUEL_SUMMARY fights=%d ranged_wins=%d melee_wins=%d draws=%d melee_winrate=%.0f%% avg_time=%.1f s"), Fights, GDuel.Wins[0], GDuel.Wins[1], GDuel.Draws,
			100.f * GDuel.Wins[1] / FMath::Max(1, GDuel.Wins[0] + GDuel.Wins[1]), GDuel.TimeSum / FMath::Max(1, Fights));
		ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=0"));
		LabStep = 3;
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

// ---- -ArenaSkinDeathLab ---------------------------------------------------------------------------------------------
namespace
{
	struct FSkinDeathState { int32 Hero = 0, Skin = -1; int32 Step = 0; float At = 0.f; TWeakObjectPtr<AArenaCharacter> Killer, Victim; int32 Done = 0; };
	FSkinDeathState GSkin;
}

void AArenaGameMode::TickSkinDeathLab(float Now)
{
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	const FVector Centre = TeamBase(1) + FVector(-600.f, 0.f, 0.f);
	switch (GSkin.Step)
	{
	case 0:
		if (!NavReady() || Now - LabStart < 1.f) { return; }
		Phase = EArenaPhase::Playing;
		MatchStart = Now;
		ForcedSkin = -1;
		GSkin.Killer = SpawnHero(0, 0, 2, false, 9, 0.f, nullptr);
		if (AArenaCharacter* K = GSkin.Killer.Get()) { K->SetActorLocation(DuelGround(GetWorld(), Centre + FVector(0.f, -900.f, 0.f)) + FVector(0.f, 0.f, 100.f)); if (AController* C = K->GetController()) { C->UnPossess(); } }
		GSkin.Step = 1;
		return;
	case 1:
	{
		if (!Defs.IsValidIndex(GSkin.Hero)) { ARENA_LOG(LogArena, Display, TEXT("SKIN_DEATH_SUMMARY bodies=%d crashes=0"), GSkin.Done); ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=0")); GSkin.Step = 9; UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); return; }
		ForcedSkin = GSkin.Skin;
		AArenaCharacter* V = SpawnHero(GSkin.Hero, 1, 2, false, 9, 0.f, nullptr);
		ForcedSkin = -2;
		if (V)
		{
			if (AController* C = V->GetController()) { C->UnPossess(); }
			V->SetActorLocation(DuelGround(GetWorld(), Centre) + FVector(0.f, 0.f, 100.f));
		}
		GSkin.Victim = V;
		GSkin.At = Now;
		GSkin.Step = 2;
		return;
	}
	case 2:
		if (Now - GSkin.At < 0.5f) { return; }
		if (AArenaCharacter* V = GSkin.Victim.Get())
		{
			ARENA_LOG(LogArena, Display, TEXT("SKIN_DEATH die hero=%s skin=%d"), *Defs[GSkin.Hero].Id.ToString(), GSkin.Skin);
			FArenaHit Hit; Hit.Ability = TEXT("lab");
			V->ReceiveHit(1.e6f, GSkin.Killer.Get(), Hit);
		}
		GSkin.At = Now;
		GSkin.Step = 3;
		return;
	case 3:
		if (Now - GSkin.At < 2.5f) { return; }
		ARENA_LOG(LogArena, Display, TEXT("SKIN_DEATH gc hero=%s skin=%d"), *Defs[GSkin.Hero].Id.ToString(), GSkin.Skin);
		if (AArenaCharacter* V = GSkin.Victim.Get()) { V->Destroy(); }
		GEngine->ForceGarbageCollection(true);
		GSkin.At = Now;
		GSkin.Step = 4;
		return;
	case 4:
		if (Now - GSkin.At < 0.5f) { return; }
		ARENA_LOG(LogArena, Display, TEXT("SKIN_DEATH ok hero=%s skin=%d"), *Defs[GSkin.Hero].Id.ToString(), GSkin.Skin);
		++GSkin.Done;
		if (++GSkin.Skin >= Defs[GSkin.Hero].Skins.Num()) { GSkin.Skin = -1; ++GSkin.Hero; }
		GSkin.Step = 1;
		return;
	default: return;
	}
}
