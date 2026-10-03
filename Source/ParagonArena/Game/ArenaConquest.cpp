// Conquest (v14): the lane-and-structures mode (01-game-design.md §3b). Three lanes; on each, per team, an outer and an
// inner tower and an inhibitor in front of the team's core (LoL); neutral camps with buffs in the jungle and the
// Prime Helix boss in the centre. The game mode is the authority; AArenaGameState carries what the players see.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaEvidence.h"
#include "Game/ArenaGameState.h"
#include "Heroes/ArenaCharacter.h"
#include "AI/ArenaBotController.h"
#include "UI/ArenaHUD.h"
#include "Core/ArenaConquestRules.h"
#include "Arena/ArenaFx.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	const FLinearColor ConquestGold(1.f, 0.8f, 0.25f), ConquestRed(1.f, 0.3f, 0.2f), ConquestPurple(0.8f, 0.45f, 1.f), ConquestOrange(1.f, 0.6f, 0.2f);

	/** "mid" / "top" / "bottom" from where the lane runs (the map's lanes run along X). */
	FString LaneWord(const TArray<FVector>& Path)
	{
		if (Path.Num() == 0) { return TEXT("?"); }
		const float Y = Path[Path.Num() / 2].Y;
		return FMath::Abs(Y) < 1500.f ? TEXT("mid") : (Y > 0.f ? TEXT("top") : TEXT("bottom"));
	}
}

int32 AArenaGameMode::LaneForSlot(int32 SlotInTeam) const
{
	// lanes sorted by name (Mid, North, South; Tools/build_arena.py): one hero in the middle, two on each side
	static const int32 Order[5] = { 0, 1, 2, 1, 2 };
	return Lanes.Num() == 0 ? -1 : Order[FMath::Clamp(SlotInTeam, 0, 4)] % Lanes.Num();
}

int32 AArenaGameMode::LaneOf(int32 Team, int32 HeroIndex) const
{
	const int32* L = HeroLanes.Find(Team * 100 + HeroIndex);
	return bConquest && L ? *L : -1;
}

float AArenaGameMode::FountainRadiusCm() const
{
	return (bConquest ? Rules.Conquest.FountainRadius : Rules.BaseRadius) * 100.f;
}

float AArenaGameMode::ConquestMatchSeconds() const
{
	return Phase == EArenaPhase::Playing || Phase == EArenaPhase::Ended ? GetWorld()->GetTimeSeconds() - MatchStart : 0.f;
}

void AArenaGameMode::PayHero(AArenaCharacter* Hero, float Amount)
{
	if (!Hero) { return; }
	if (Hero->IsAlive()) { Hero->Gold += Amount; return; }
	for (FArenaRespawn& R : Respawns) { if (R.Team == Hero->GetTeam() && R.Hero == Hero->HeroIndex) { R.Gold += Amount; return; } }
}

FVector AArenaGameMode::CorePos(int32 Team) const
{
	for (const FStructure& S : Structures) { if (S.Kind == ArenaConquest::Core && S.Team == Team) { return S.Spot; } }
	return TeamBase(Team);
}

bool AArenaGameMode::MinionsNear(int32 Team, const FVector& At, float Radius) const
{
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->IsLaneMinion() && It->GetTeam() == Team && FVector::DistSquared2D(It->GetActorLocation(), At) <= Radius * Radius) { return true; }
	}
	return false;
}

int32 AArenaGameMode::StructuresLeft(int32 Team) const
{
	int32 N = 0;
	for (const FStructure& S : Structures) { N += S.Team == Team && S.bAlive ? 1 : 0; }
	return N;
}

const AArenaGameMode::FStructure* AArenaGameMode::FindStructure(const AArenaCharacter* Unit) const
{
	return Structures.FindByPredicate([Unit](const FStructure& S) { return S.Unit.Get() == Unit; });
}

FVector AArenaGameMode::GroundSpot(const FVector& Desired, bool* bOk) const
{
	if (bOk) { *bOk = false; }
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation On;
		if (Nav->ProjectPointToNavigation(Desired, On, FVector(250.f, 250.f, 900.f))) { if (bOk) { *bOk = true; } return On.Location; }
	}
	return Desired;
}

FVector AArenaGameMode::LaneFront(int32 Team, int32 Lane) const
{
	const TArray<FVector> Path = LanePath(Lane, Team);
	if (Path.Num() < 2) { return TeamBase(1 - Team) * 0.55f + TeamBase(Team) * 0.45f; }
	// progress along the lane: the lanes run along X, a projection on the lane's overall direction is enough
	const FVector Dir = (Path.Last() - Path[0]).GetSafeNormal2D();
	auto Progress = [&](const FVector& P) { return FVector::DotProduct(P - Path[0], Dir); };
	float Front = -1.f;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive() || !It->IsLaneMinion() || It->GetTeam() != Team) { continue; }
		const AArenaBotController* AI = Cast<AArenaBotController>(It->GetController());
		if (AI && AI->Lane == Lane) { Front = FMath::Max(Front, Progress(It->GetActorLocation())); }
	}
	if (Front > 0.f) { return ArenaConquest::AlongPath(Path, FMath::Max(0.f, Front - 250.f)); }   // just behind the wave
	// no wave out: wait by the outermost tower still standing
	float Own = 0.f;
	for (const FStructure& S : Structures)
	{
		if (S.bAlive && S.Team == Team && S.Lane == Lane && S.Kind == ArenaConquest::Tower) { Own = FMath::Max(Own, Progress(S.Spot)); }
	}
	return ArenaConquest::AlongPath(Path, FMath::Max(0.f, Own - 200.f));
}

AArenaCharacter* AArenaGameMode::SpawnStructure(int32 Kind, int32 Team, int32 Lane, int32 Tier, const FVector& Spot, float Yaw)
{
	const FArenaConquestDef& Cq = Rules.Conquest;
	const FArenaHeroDef& D = Kind == ArenaConquest::Tower ? Cq.Tower : (Kind == ArenaConquest::Inhibitor ? Cq.Inhibitor : Cq.Core);
	if (D.Mesh.IsEmpty() && D.StaticMesh.IsEmpty()) { return nullptr; }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArenaCharacter* C = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Spot + FVector(0.f, 0.f, 200.f), FRotator(0.f, Yaw, 0.f), P);
	if (!C) { return nullptr; }
	C->InitCharacter(D, Team, 1, true);
	C->SetNetSetup(5 + Kind, -1);   // 6 tower, 7 inhibitor, 8 core
	// the capsule's bottom on the ground
	C->SetActorLocation(Spot + FVector(0.f, 0.f, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	C->MakeStructure(Kind, Lane, Tier);
	return C;
}

void AArenaGameMode::StartConquest()
{
	const FArenaConquestDef& Cq = Rules.Conquest;
	Structures.Reset();
	Camps.Reset();
	BossTeam = -1;
	BossUntil = 0.f;
	NextOrb = -1.f;   // no power orb: the boss holds the centre
	if (Lanes.Num() == 0) { ARENA_LOG(LogArena, Warning, TEXT("ARENA evt=conquest_no_lanes")); return; }
	// the structures are laid out on team A's half and mirrored (x -> -x) for team B: the map is symmetric
	for (int32 L = 0; L < Lanes.Num(); ++L)
	{
		const TArray<FVector> Path = LanePath(L, 0);
		if (Path.Num() < 2) { continue; }
		const bool bMid = FMath::Abs(Path[0].Y) < 1000.f;
		const TArray<float>& Along = bMid ? Cq.MidAlong : Cq.SideAlong;
		const float SideSign = bMid ? 1.f : -FMath::Sign(Path[0].Y);   // the mid on its +Y side, the side lanes toward the centre
		for (int32 k = 0; k < Along.Num() && k < 3; ++k)
		{
			// beside the lane on walkable ground: the preferred side first, then the other, then a little up or down
			// the lane (the side lanes' inhibitors met rocks by the base's corners)
			bool bOk = false;
			FVector G = FVector::ZeroVector;
			for (const float Shift : { 0.f, 300.f, -300.f, 600.f, -600.f })
			{
				const FVector OnLane = ArenaConquest::AlongPath(Path, FMath::Max(0.f, Along[k] + Shift));
				for (const float Sign : { SideSign, -SideSign })
				{
					const FVector Want = OnLane + FVector(0.f, Sign * Cq.TowerSide * 100.f, 0.f);
					const FVector Got = GroundSpot(Want, &bOk);
					if (bOk && FVector::Dist2D(Got, Want) < 150.f) { G = Got; break; }
					bOk = false;
				}
				if (bOk) { break; }
			}
			if (!bOk) { G = ArenaConquest::AlongPath(Path, Along[k]) + FVector(0.f, SideSign * Cq.TowerSide * 100.f, 0.f); }
			const int32 Kind = k == 0 ? ArenaConquest::Inhibitor : ArenaConquest::Tower;
			const int32 Tier = k == 0 ? 3 : FMath::Min(Along.Num(), 3) - k;   // the last one is the outer tower (tier 1)
			for (int32 Team = 0; Team < 2; ++Team)
			{
				FVector Spot = Team == 0 ? G : GroundSpot(FVector(-G.X, G.Y, G.Z));
				FStructure& S = Structures.AddDefaulted_GetRef();
				S.Kind = Kind; S.Team = Team; S.Lane = L; S.Tier = Tier; S.Spot = Spot; S.Yaw = Team == 0 ? 0.f : 180.f;
				S.Unit = SpawnStructure(Kind, Team, L, Tier, Spot, S.Yaw);
				S.bAlive = S.Unit.IsValid();
				S.bOnNav = bOk;
				S.LastHp = S.bAlive ? S.Unit->GetHealth() : 0.f;
				ARENA_LOG(LogArena, Display, TEXT("ARENA evt=conquest_place kind=%d team=%d lane=%d tier=%d at=%s on_nav=%d spawned=%d"), Kind, Team, L, Tier, *Spot.ToCompactString(), bOk ? 1 : 0, S.bAlive ? 1 : 0);
			}
		}
	}
	for (int32 Team = 0; Team < 2; ++Team)
	{
		bool bOk = false;
		const FVector Spot = GroundSpot(FVector(Cq.CoreSpot.X * (Team == 0 ? 1.f : -1.f), Cq.CoreSpot.Y, 300.f), &bOk);
		FStructure& S = Structures.AddDefaulted_GetRef();
		S.Kind = ArenaConquest::Core; S.Team = Team; S.Tier = 4; S.Spot = Spot; S.Yaw = Team == 0 ? 0.f : 180.f;
		S.Unit = SpawnStructure(ArenaConquest::Core, Team, -1, 4, Spot, S.Yaw);
		S.bAlive = S.Unit.IsValid();
		S.bOnNav = bOk;
		S.LastHp = S.bAlive ? S.Unit->GetHealth() : 0.f;
		ARENA_LOG(LogArena, Display, TEXT("ARENA evt=conquest_place kind=3 team=%d at=%s on_nav=%d spawned=%d"), Team, *Spot.ToCompactString(), bOk ? 1 : 0, S.bAlive ? 1 : 0);
	}
	UpdateProtection();
	// the camps: team A's half as written, team B's mirrored
	for (int32 i = 0; i < Cq.Camps.Num(); ++i)
	{
		const FArenaCampDef& D = Cq.Camps[i];
		for (int32 Side = 0; Side < (D.bMirror ? 2 : 1); ++Side)
		{
			bool bOk = false;
			const FVector Spot = GroundSpot(FVector(D.Spot.X * (Side == 0 ? 1.f : -1.f), D.Spot.Y, 100.f), &bOk);
			if (!bOk) { ARENA_LOG(LogArena, Warning, TEXT("ARENA evt=camp_off_nav name=%s side=%d"), *D.Name, Side); continue; }
			FCamp& C = Camps.AddDefaulted_GetRef();
			C.Def = i; C.Side = Side; C.Spot = Spot; C.Yaw = Side == 0 ? D.Yaw : 180.f - D.Yaw;
			C.RespawnAt = D.FirstSpawn;   // seconds into the match
			ARENA_LOG(LogArena, Display, TEXT("ARENA evt=conquest_camp name=%s side=%d at=%s buff=%d first=%.0f"), *D.Name, Side, *Spot.ToCompactString(), D.Buff, D.FirstSpawn);
		}
	}
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=conquest_start structures=%d camps=%d lanes=%d"), Structures.Num(), Camps.Num(), Lanes.Num());
}

void AArenaGameMode::UpdateProtection()
{
	TArray<ArenaConquest::FStructureState> All;
	for (const FStructure& S : Structures) { All.Add({ S.Kind, S.Team, S.Lane, S.Tier, S.bAlive }); }
	for (int32 i = 0; i < Structures.Num(); ++i)
	{
		if (AArenaCharacter* U = Structures[i].Unit.Get()) { U->bInvulnerable = !ArenaConquest::IsVulnerable(All[i], All); }
	}
}

void AArenaGameMode::TickConquest(float Now, float Dt)
{
	TickStructures(Now, Dt);
	TickCamps(Now);
	if (BossTeam >= 0 && Now >= BossUntil) { BossTeam = -1; }
	// bot matches: once a minute the cores (open or shut, health) and the attackers at them: the pace's evidence
	if (bBotMatch && Phase == EArenaPhase::Playing && FMath::FloorToInt(Now / 60.f) != FMath::FloorToInt((Now - Dt) / 60.f))
	{
		FString Line;
		for (const FStructure& S : Structures)
		{
			const AArenaCharacter* U = S.Unit.Get();
			if (S.Kind != ArenaConquest::Core || !U) { continue; }
			int32 AtHeroes = 0, AtMinions = 0;
			for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
			{
				if (!It->IsAlive() || It->GetTeam() != 1 - S.Team || It->IsStructure()) { continue; }
				const float D = FVector::Dist2D(It->GetActorLocation(), S.Spot);
				if (It->IsLaneMinion()) { AtMinions += D < 1500.f ? 1 : 0; }
				else if (!It->IsMinion()) { AtHeroes += D < 2500.f ? 1 : 0; }
			}
			Line += FString::Printf(TEXT(" core%d=%.0f%%%s attackers=%d+%dm"), S.Team, U->HealthPct() * 100.f, U->bInvulnerable ? TEXT("_shut") : TEXT("_open"), AtHeroes, AtMinions);
		}
		ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.0f evt=cq_snapshot%s"), Now - MatchStart, *Line);
	}
}

void AArenaGameMode::TickStructures(float Now, float Dt)
{
	for (FStructure& S : Structures)
	{
		AArenaCharacter* T = S.Unit.Get();
		if (!S.bAlive)
		{
			// a fallen inhibitor comes back (LoL), the protection chain with it
			if (S.Kind == ArenaConquest::Inhibitor && S.RespawnAt > 0.f && Now >= S.RespawnAt)
			{
				if (T) { T->Destroy(); }
				S.Unit = SpawnStructure(S.Kind, S.Team, S.Lane, S.Tier, S.Spot, S.Yaw);
				S.bAlive = S.Unit.IsValid();
				S.RespawnAt = -1.f;
				S.Target = nullptr;
				S.LastHp = S.bAlive ? S.Unit->GetHealth() : 0.f;
				UpdateProtection();
				AnnounceFor(S.Team, TEXT("Our inhibitor is back up"), ConquestOrange, TEXT("Enemy inhibitor is back up"), ConquestOrange, false);
				ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=inhibitor_back team=%d lane=%d"), Now, S.Team, S.Lane);
			}
			continue;
		}
		if (!T || !T->IsAlive()) { continue; }
		// under attack: the player's team hears of it (each structure at most every 20 s)
		if (T->GetHealth() < S.LastHp - 1.f)
		{
			S.HurtAt = Now;
			if (Now - S.AlertAt > 20.f)
			{
				// the defenders hear of it (every structure at most every 20 s); the attackers do not
				S.AlertAt = Now;
				const TCHAR* What = S.Kind == ArenaConquest::Core ? TEXT("Our core is under attack!") : (S.Kind == ArenaConquest::Inhibitor ? TEXT("Our inhibitor is under attack!") : TEXT("Our tower is under attack!"));
				AnnounceFor(S.Team, What, ConquestOrange, FString(), ConquestOrange, false);
			}
		}
		S.LastHp = T->GetHealth();
		// shooting: towers and inhibitors (the core does not shoot)
		if (T->GetDef().Abilities.Num() == 0 || T->GetDef().Abilities[0].Damage <= 0.f) { continue; }
		const float Range = T->Ability(0).Range * 100.f;
		if (Now >= S.NextThink)
		{
			S.NextThink = Now + 0.2f;
			// "call for help": the enemy heroes that just hurt one of our heroes standing in range
			TSet<uint32> Attackers;
			for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
			{
				AArenaCharacter* H = *It;
				if (!H->IsAlive() || H->IsMinion() || H->GetTeam() != S.Team || Now - H->LastDamageTime > 0.8f || !H->LastAttacker.IsValid()) { continue; }
				if (FVector::Dist2D(H->GetActorLocation(), T->GetActorLocation()) <= Range + 200.f) { Attackers.Add(H->LastAttacker->GetUniqueID()); }
			}
			TArray<ArenaConquest::FTowerCandidate> Cands;
			TMap<int32, AArenaCharacter*> ById;
			for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
			{
				AArenaCharacter* C = *It;
				if (!C->IsAlive() || C->IsStructure() || C->IsMonster() || C->GetTeam() == S.Team) { continue; }
				const float D = FVector::Dist2D(C->GetActorLocation(), T->GetActorLocation()) - C->GetCapsuleComponent()->GetScaledCapsuleRadius();
				if (D > Range) { continue; }
				ArenaConquest::FTowerCandidate K;
				K.Id = (int32)C->GetUniqueID(); K.Dist = D; K.bHero = !C->IsMinion(); K.bHitAllyHero = K.bHero && Attackers.Contains(C->GetUniqueID());
				Cands.Add(K);
				ById.Add(K.Id, C);
			}
			const int32 Id = ArenaConquest::PickTowerTarget(S.Target.IsValid() ? (int32)S.Target->GetUniqueID() : 0, Cands);
			AArenaCharacter* NewTarget = Id != 0 ? ById.FindRef(Id) : nullptr;
			if (NewTarget != S.Target.Get())
			{
				T->TowerRamp = -1;   // the next shot on a hero is the first of a new streak
				S.Target = NewTarget;
				UE_LOG(LogArena, Verbose, TEXT("ARENA t=%.1f evt=tower_target team=%d lane=%d tier=%d target=%s"), Now, S.Team, S.Lane, S.Tier, NewTarget ? *NewTarget->GetDef().Id.ToString() : TEXT("none"));
			}
			T->AimTarget = S.Target;
		}
		AArenaCharacter* Tgt = S.Target.Get();
		if (!Tgt || !Tgt->IsAlive()) { S.Target = nullptr; continue; }
		// the turret turns to its target, then fires (a homing shot: it does not miss)
		const float WantYaw = (Tgt->GetActorLocation() - T->GetActorLocation()).Rotation().Yaw;
		T->SetActorRotation(FMath::RInterpConstantTo(T->GetActorRotation(), FRotator(0.f, WantYaw, 0.f), Dt, 260.f));
		if (T->CanCastSlot(0) && FMath::Abs(FRotator::NormalizeAxis(T->GetActorRotation().Yaw - WantYaw)) < 25.f)
		{
			T->AimTarget = Tgt;
			T->AimPoint = Tgt->GetActorLocation();
			if (T->TryCast(0)) { T->TowerRamp = Tgt->IsMinion() ? 0 : T->TowerRamp + 1; }
		}
	}
}

void AArenaGameMode::SpawnCamp(FCamp& Camp, float Now)
{
	const FArenaCampDef& D = Rules.Conquest.Camps[Camp.Def];
	const int32 Level = ArenaConquest::MonsterLevel(Now - MatchStart, D.LevelPerMinute);
	Camp.Units.Reset();
	const int32 N = FMath::Max(1, D.Count);
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Off = N > 1 ? FVector(170.f, 0.f, 0.f).RotateAngleAxis(i * 360.f / N + Camp.Yaw, FVector::UpVector) : FVector::ZeroVector;
		const FVector At = FreeSpot(GroundSpot(Camp.Spot + Off) + FVector(0.f, 0.f, 100.f * FMath::Max(1.f, D.Unit.Scale)), 40.f);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AArenaCharacter* M = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), At, FRotator(0.f, Camp.Yaw, 0.f), P);
		if (!M) { continue; }
		M->InitCharacter(D.Unit, 2, Level, true);
		M->SetNetSetup(9, Camp.Def);
		AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
		AI->Difficulty = 1;
		AI->bMonster = true;
		AI->Home = M->GetActorLocation();
		AI->HomeYaw = Camp.Yaw;
		AI->LeashCm = D.Leash * 100.f;
		AI->Possess(M);
		ArenaFx::Spawn(this, D.Unit.SpawnFx, M->GetActorLocation() - FVector(0.f, 0.f, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), FLinearColor::White);
		Camp.Units.Add(M);
	}
	Camp.bUp = Camp.Units.Num() > 0;
	if (D.Buff == 3 && Camp.bUp) { Announce(this, FString::Printf(TEXT("%s has appeared in the center of the map!"), *D.Name), true, ConquestPurple); }
	ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=camp_spawn name=%s side=%d units=%d level=%d"), Now, *D.Name, Camp.Side, Camp.Units.Num(), Level);
}

void AArenaGameMode::TickCamps(float Now)
{
	const float MatchT = Now - MatchStart;
	for (FCamp& C : Camps)
	{
		if (!C.bUp && MatchT >= C.RespawnAt) { SpawnCamp(C, Now); }
	}
}

void AArenaGameMode::OnMonsterKilled(AArenaCharacter* M, AArenaCharacter* Killer)
{
	const float Now = GetWorld()->GetTimeSeconds();
	FCamp* Camp = Camps.FindByPredicate([M](const FCamp& C) { return C.Units.Contains(M); });
	if (!Camp) { return; }
	const FArenaCampDef& D = Rules.Conquest.Camps[Camp->Def];
	// the credit: the hero who landed the blow, else the last hero who hurt it
	AArenaCharacter* Hero = Killer && !Killer->IsMinion() ? Killer : nullptr;
	for (int32 i = M->RecentDamage.Num() - 1; i >= 0 && !Hero; --i)
	{
		const FArenaDamageEvent& E = M->RecentDamage[i];
		if (Now - E.Time <= 10.f && E.SourceHero >= 0) { Hero = FindHero(E.SourceTeam, E.SourceHero); }
	}
	const int32 KTeam = Hero ? Hero->GetTeam() : -1;
	const float Share = 1.f / FMath::Max(1, D.Count);
	if (Hero)
	{
		if (D.Buff != 3)
		{
			PayHero(Hero, D.Gold * Share);
			if (Hero->IsLocalHero()) { AArenaHUD::AddGoldNumber(this, M->GetActorLocation(), D.Gold * Share); }
		}
		for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
		{
			AArenaCharacter* H = W.Get();
			if (H && H->IsAlive() && H->GetTeam() == KTeam && FVector::Dist2D(H->GetActorLocation(), M->GetActorLocation()) <= Rules.XpShareRadius * 100.f) { H->AddXp(D.Xp * Share); }
		}
	}
	int32 Left = 0;
	for (const TWeakObjectPtr<AArenaCharacter>& U : Camp->Units) { Left += U.IsValid() && U.Get() != M && U->IsAlive() ? 1 : 0; }
	ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=monster_kill name=%s killer=%s team=%d left=%d"), Now, *D.Name, Hero ? *Hero->GetDef().Id.ToString() : TEXT("none"), KTeam, Left);
	if (Left > 0) { return; }
	Camp->bUp = false;
	Camp->Units.Reset();
	Camp->RespawnAt = (Now - MatchStart) + D.Respawn;
	if (!Hero) { return; }
	const FArenaConquestDef& Cq = Rules.Conquest;
	if (D.Buff == 1 || D.Buff == 2)
	{
		if (Hero->IsAlive()) { Hero->ApplyCampBuff(D.Buff - 1, Cq.BuffSeconds); }
		const TCHAR* BuffName = D.Buff == 1 ? TEXT("red buff (+damage)") : TEXT("black buff (mana, cooldowns)");
		AnnounceFor(KTeam, FString::Printf(TEXT("%s takes the %s"), *Hero->GetDef().DisplayName, BuffName), ConquestGold,
			FString::Printf(TEXT("Enemy: %s takes the %s"), *Hero->GetDef().DisplayName, BuffName), ConquestRed, false);
	}
	else if (D.Buff == 3)
	{
		BossTeam = KTeam;
		BossUntil = Now + Cq.BossSeconds;
		for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
		{
			AArenaCharacter* H = W.Get();
			if (H && H->IsAlive() && H->GetTeam() == KTeam) { H->ApplyCampBuff(2, Cq.BossSeconds); }
		}
		const TArray<TWeakObjectPtr<AArenaCharacter>> Team = Heroes;
		for (const TWeakObjectPtr<AArenaCharacter>& W : Team) { if (W.IsValid() && W->GetTeam() == KTeam) { W->Gold += D.Gold; } }
		for (FArenaRespawn& R : Respawns) { if (R.Team == KTeam) { R.Gold += D.Gold; } }
		AnnounceFor(KTeam, FString::Printf(TEXT("We defeated %s! Helix Power: +damage, +speed, stronger waves"), *D.Name), ConquestPurple, FString::Printf(TEXT("Enemies defeated %s!"), *D.Name), ConquestRed, true);
		ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=boss_kill team=%d until=%.0f"), Now, KTeam, BossUntil);
	}
}

void AArenaGameMode::OnStructureDestroyed(AArenaCharacter* Unit, AArenaCharacter* Killer)
{
	FStructure* St = Structures.FindByPredicate([Unit](const FStructure& S) { return S.Unit.Get() == Unit; });
	if (!St || !St->bAlive) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	const FArenaConquestDef& Cq = Rules.Conquest;
	St->bAlive = false;
	St->Target = nullptr;
	const int32 Winner = 1 - St->Team;
	// the credit: the hero who landed the blow, else the last hero who hurt it
	AArenaCharacter* Hero = Killer && !Killer->IsMinion() ? Killer : nullptr;
	for (int32 i = Unit->RecentDamage.Num() - 1; i >= 0 && !Hero; --i)
	{
		const FArenaDamageEvent& E = Unit->RecentDamage[i];
		if (Now - E.Time <= 10.f && E.SourceHero >= 0 && E.SourceTeam == Winner) { Hero = FindHero(E.SourceTeam, E.SourceHero); }
	}
	// the whole team is paid (LoL): the living and the dead (on their respawn records); XP to those near
	const int32 Gold = St->Kind == ArenaConquest::Tower ? Cq.GoldTower : (St->Kind == ArenaConquest::Inhibitor ? Cq.GoldInhibitor : 0);
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
	{
		AArenaCharacter* H = W.Get();
		if (!H || !H->IsAlive() || H->GetTeam() != Winner) { continue; }
		H->Gold += Gold;
		if (FVector::Dist2D(H->GetActorLocation(), Unit->GetActorLocation()) <= 2500.f) { H->AddXp(Cq.XpTower); }
	}
	for (FArenaRespawn& R : Respawns) { if (R.Team == Winner) { R.Gold += Gold; } }
	if (Hero && St->Kind == ArenaConquest::Tower)
	{
		PayHero(Hero, Cq.GoldTowerKiller);
		if (Hero->IsLocalHero()) { AArenaHUD::AddGoldNumber(this, Unit->GetActorLocation(), Cq.GoldTowerKiller + Gold); }
	}
	if (St->Kind == ArenaConquest::Inhibitor) { St->RespawnAt = Now + Cq.InhibitorRespawn; }
	UpdateProtection();
	// the news: whose, which, where
	const FString Where = St->Lane >= 0 ? FString::Printf(TEXT(" on the %s lane"), *LaneWord(LanePath(St->Lane, 0))) : FString();
	const TCHAR* What = St->Kind == ArenaConquest::Core ? TEXT("core") : (St->Kind == ArenaConquest::Inhibitor ? TEXT("inhibitor") : (St->Tier == 1 ? TEXT("outer tower") : TEXT("inner tower")));
	if (St->Kind != ArenaConquest::Core)
	{
		// the owners lost it, the others destroyed it (each team's view)
		AnnounceFor(St->Team, FString::Printf(TEXT("We lost our %s%s"), What, *Where), ConquestRed, FString::Printf(TEXT("Destroyed the enemy's %s%s"), What, *Where), ConquestGold, St->Kind == ArenaConquest::Inhibitor);
		if (St->Kind == ArenaConquest::Inhibitor) { AnnounceFor(St->Team, TEXT("The enemy is fielding super minions on this lane!"), ConquestRed, TEXT("Our waves on this lane are led by a super minion!"), ConquestGold, false); }
	}
	ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=structure_down kind=%d team=%d lane=%d tier=%d killer=%s credit=%s left=%d/%d"), Now, St->Kind, St->Team, St->Lane, St->Tier,
		Killer ? *Killer->GetDef().Id.ToString() : TEXT("none"), Hero ? *Hero->GetDef().Id.ToString() : TEXT("none"), StructuresLeft(0), StructuresLeft(1));
}

void AArenaGameMode::SpawnConquestWave()
{
	const FArenaConquestDef& Cq = Rules.Conquest;
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 WaveLevel = 1 + FMath::Clamp(FMath::FloorToInt((Now - MatchStart) / 120.f), 0, 9);
	const bool bSiegeWave = Cq.SiegeEvery > 0 && WaveIndex % Cq.SiegeEvery == Cq.SiegeEvery - 1;
	int32 Spawned = 0;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float Boost = BossTeam == Team && Now < BossUntil ? Cq.BossMinions : 0.f;
		for (int32 L = 0; L < Lanes.Num(); ++L)
		{
			const TArray<FVector> Path = LanePath(L, Team);
			if (Path.Num() == 0) { continue; }
			// the enemy's inhibitor on this lane down: a super minion leads every wave there (LoL)
			bool bEnemyInhibDown = false;
			for (const FStructure& S : Structures) { bEnemyInhibDown |= S.Kind == ArenaConquest::Inhibitor && S.Team == 1 - Team && S.Lane == L && !S.bAlive; }
			TArray<const FArenaHeroDef*> Units;
			for (int32 i = 0; i < Cq.MeleePerLane; ++i) { Units.Add(&Rules.MeleeMinion); }
			for (int32 i = 0; i < Cq.RangedPerLane; ++i) { Units.Add(&Rules.RangedMinion); }
			if (bEnemyInhibDown && !Cq.SuperMinion.Mesh.IsEmpty()) { Units.Insert(&Cq.SuperMinion, 0); }
			else if (bSiegeWave && !Cq.SiegeMinion.Mesh.IsEmpty()) { Units.Add(&Cq.SiegeMinion); }
			const FVector Base = TeamBase(Team);
			const FVector Fwd = (Path[0] - Base).GetSafeNormal2D();
			const FVector Side = Fwd.RotateAngleAxis(90.f, FVector::UpVector);
			for (int32 i = 0; i < Units.Num(); ++i)
			{
				const FVector Loc = FreeSpot(Base + Fwd * 520.f + Side * ((i - Units.Num() * 0.5f) * 150.f) + FVector(0.f, 0.f, 60.f), 30.f);
				FActorSpawnParameters P;
				P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				AArenaCharacter* M = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Loc, Fwd.Rotation(), P);
				if (!M) { continue; }
				FArenaHeroDef Def = *Units[i];
				if (Boost > 0.f)
				{
					// the boss's team: stronger, a little bigger waves while its buff lasts
					Def.MaxHealth *= 1.f + Boost;
					for (FArenaAbilityDef& A : Def.Abilities) { A.Damage *= 1.f + Boost; }
					Def.Scale *= 1.08f;
				}
				M->InitCharacter(Def, Team, WaveLevel, true);
				M->SetNetSetup(Units[i] == &Rules.RangedMinion ? 3 : (Units[i] == &Cq.SiegeMinion ? 4 : (Units[i] == &Cq.SuperMinion ? 5 : 2)), -1, -1, Boost);
				AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
				AI->Difficulty = 1;
				AI->Lane = L;
				AI->Possess(M);
				AI->LanePath = Path;
				++Spawned;
			}
		}
	}
	ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=wave mode=conquest index=%d minions=%d siege=%d level=%d"), Now, WaveIndex, Spawned, bSiegeWave ? 1 : 0, WaveLevel);
	++WaveIndex;
}

void AArenaGameMode::WriteConquestState(AArenaGameState* GS)
{
	const float Now = GetWorld()->GetTimeSeconds();
	GS->Structures.SetNum(Structures.Num());
	for (int32 i = 0; i < Structures.Num(); ++i)
	{
		const FStructure& S = Structures[i];
		FArenaStructureRep& R = GS->Structures[i];
		const AArenaCharacter* U = S.Unit.Get();
		R.Kind = (uint8)S.Kind; R.Team = (uint8)S.Team; R.Lane = (int8)S.Lane; R.Tier = (uint8)S.Tier; R.Pos = S.Spot;
		R.bAlive = S.bAlive && U && U->IsAlive();
		R.HpPct = R.bAlive ? U->HealthPct() : 0.f;
		R.bInvulnerable = R.bAlive && U->bInvulnerable;
		R.bUnderAttack = R.bAlive && Now - S.HurtAt < 3.f;
		R.RespawnAt = S.RespawnAt;
	}
	GS->Camps.SetNum(Camps.Num());
	for (int32 i = 0; i < Camps.Num(); ++i)
	{
		const FCamp& C = Camps[i];
		FArenaCampRep& R = GS->Camps[i];
		const FArenaCampDef& D = Rules.Conquest.Camps[C.Def];
		R.Buff = (uint8)D.Buff; R.Pos = C.Spot; R.bUp = C.bUp; R.RespawnAt = MatchStart + C.RespawnAt; R.Name = D.Name;
	}
	GS->BossTeam = BossTeam;
	GS->BossUntil = BossUntil;
}
