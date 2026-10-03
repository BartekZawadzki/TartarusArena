#include "AI/ArenaBotController.h"
#include "Game/ArenaEvidence.h"
#include "AI/ArenaBotBrain.h"
#include "Heroes/ArenaCharacter.h"
#include "Game/ArenaGameMode.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/ArenaGameState.h"
#include "Abilities/ArenaAbility.h"
#include "Core/ArenaConquestRules.h"

AArenaBotController::AArenaBotController()
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = false;
}

void AArenaBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AArenaCharacter* Me = Cast<AArenaCharacter>(GetPawn());
	AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	if (!Me || !Me->IsAlive() || !GM || GM->Phase != EArenaPhase::Playing) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (bMonster) { TickMonster(Me, GM, Now, DeltaSeconds); return; }

	// stuck detector: not moving while it wants to move, 3 s (VR-08)
	// stunned / rooted by an enemy, or channelling a root-motion ultimate, is not "stuck": the clock only runs while the bot is free to move
	// the clock runs only while the bot is actually trying to walk somewhere: standing still on purpose (shooting, at
	// melee contact, waiting at the fountain) is not stuck
	if (!bWantsTravel || GetMoveStatus() != EPathFollowingStatus::Moving || Me->IsStunned() || Me->IsBusy() || Me->IsDashing() || Me->IsPlayingRootMotion() || FVector::DistSquared2D(Me->GetActorLocation(), LastPos) > 25.f * 25.f) { LastPos = Me->GetActorLocation(); StillSince = Now; bStuckLogged = false; UnstickTries = 0; }
	else if (bWantsTravel && StillSince > 0.f && Now - StillSince > 3.f && !bStuckLogged && GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		bStuckLogged = true;
		++StuckEpisodes;
		++GM->StuckTotal;
		const FFindFloorResult& Fl = Me->GetCharacterMovement()->CurrentFloor;
		ARENA_LOG(LogArena, Warning, TEXT("ARENA t=%.1f evt=stuck id=%s pos=%s mode=%d speed=%.0f maxspeed=%.0f accel=%.0f dil=%.2f floor=%s goal=%s goaldist=%.0f retreat=%d busy=%d ignoremove=%d pathpts=%d pathidx=%d anim=%d"), Now, *Me->GetDef().Id.ToString(), *Me->GetActorLocation().ToCompactString(),
			(int32)Me->GetCharacterMovement()->MovementMode, Me->GetVelocity().Size2D(), Me->GetCharacterMovement()->MaxWalkSpeed, Me->GetCharacterMovement()->GetCurrentAcceleration().Size(), Me->CustomTimeDilation,
			Fl.HitResult.GetActor() ? *Fl.HitResult.GetActor()->GetName() : TEXT("none"), *LastMoveTo.ToCompactString(), FVector::Dist2D(LastMoveTo, Me->GetActorLocation()), bLastRetreat ? 1 : 0, Me->IsBusy() ? 1 : 0, IsMoveInputIgnored() ? 1 : 0,
			GetPathFollowingComponent() && GetPathFollowingComponent()->GetPath().IsValid() ? GetPathFollowingComponent()->GetPath()->GetPathPoints().Num() : -1,
			GetPathFollowingComponent() ? (int32)GetPathFollowingComponent()->GetCurrentPathIndex() : -1, (int32)Me->GetMesh()->GetAnimationMode());
	}
	// unstick: wants to travel but has not moved for 1.5 s -> walk to another reachable point nearby (no hop: a
	// unit must never be thrown into the air); still pinned after three tries means it stands off the navmesh
	// (a ledge lip, a prop): it steps back onto the nearest walkable spot, at most a few metres away
	if (bWantsTravel && StillSince > 0.f && Now - StillSince > 1.0f && Now >= NextUnstick && GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		NextUnstick = Now + 1.0f;
		++UnstickTries;
		if (UnstickTries == 1 && BotJump(Me, Now, TEXT("unstick"))) { return; }
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation On;
			const float Half = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (UnstickTries >= 3 && Nav->ProjectPointToNavigation(Me->GetActorLocation() - FVector(0.f, 0.f, Half), On, FVector(300.f, 300.f, 250.f))
				&& FVector::Dist2D(On.Location, Me->GetActorLocation()) > 20.f)
			{
				Me->SetActorLocation(On.Location + FVector(0.f, 0.f, Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
				ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=unstick_step id=%s"), Now, *Me->GetDef().Id.ToString());
				UnstickTries = 0;
			}
			// head-on with another unit (two heroes meeting at the orb): step aside, alternating left and right
			const FVector ToGoal = (LastMoveTo - Me->GetActorLocation()).GetSafeNormal2D();
			const FVector Aside = FVector(-ToGoal.Y, ToGoal.X, 0.f) * ((UnstickTries % 2) ? -1.f : 1.f);
			FNavLocation Side;
			if (Nav->ProjectPointToNavigation(Me->GetActorLocation() + Aside * 260.f + ToGoal * 80.f, Side, FVector(150.f, 150.f, 300.f))
				|| Nav->GetRandomReachablePointInRadius(Me->GetActorLocation(), 500.f, Side)) { MoveToLocation(Side.Location, 50.f, true, true, false, true); }
		}
		NextThink = Now + 0.8f;
		ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=unstick id=%s"), Now, *Me->GetDef().Id.ToString());
		return;
	}

	if (Now < NextThink) { return; }
	NextThink = Now + ArenaBot::ReactionSeconds(Difficulty) * FMath::FRandRange(0.7f, 1.3f);

	ArenaBot::FSnapshot S;
	S.Self = Me->GetActorLocation();
	S.HpPct = Me->HealthPct();
	S.ManaPct = Me->GetMaxMana() > 0.f ? Me->GetMana() / Me->GetMaxMana() : 0.f;
	S.Mana = Me->GetMana();
	S.Base = GM->TeamBase(Me->GetTeam());
	S.EnemyPortal = GM->TeamBase(1 - Me->GetTeam());
	// minion lanes: march waypoint by waypoint, then on the portal
	while (LaneStep < LanePath.Num() && FVector::Dist2D(LanePath[LaneStep], S.Self) < 450.f) { ++LaneStep; }
	if (Me->IsMinion() && LaneStep < LanePath.Num()) { S.EnemyPortal = LanePath[LaneStep]; }
	else if (Me->IsMinion() && GM->bConquest) { S.EnemyPortal = GM->CorePos(1 - Me->GetTeam()); }   // the lane is done: on to the core
	const bool bConquestHero = GM->bConquest && !Me->IsMinion();
	int32 CampOk = -1;
	if (bConquestHero && Now >= NextGoal) { NextGoal = Now + 1.5f; GoalAt = ConquestGoal(Me, GM, Now, GoalCamp); }
	CampOk = bConquestHero ? GoalCamp : -1;
	S.bMinion = Me->IsMinion();
	// an assassin dives: it backs off earlier (it died the most, 42 deaths in 8 games)
	// (v17: an assassin left at 35 %: it never finished a duel, 0-2 of 10 won) — the same line as everyone
	S.BaseSlot = Me->IsMinion() ? -1 : FMath::Max(0, Me->HeroIndex) % 5;
	S.Difficulty = Difficulty;
	if (Now >= NextStrafeFlip) { StrafeSign = -StrafeSign; NextStrafeFlip = Now + FMath::FRandRange(1.5f, 2.5f); }
	S.StrafeSign = StrafeSign;
	S.bOrb = GM->OrbLocation(S.Orb);
	S.bInEnemyBase = Me->IsInEnemyBase();
	TMap<int32, AArenaCharacter*> ById;
	int32 FoesAlive = 0;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { FoesAlive += It->IsAlive() && !It->IsMinion() && It->GetTeam() <= 1 && It->GetTeam() != Me->GetTeam() ? 1 : 0; }
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (!C->IsAlive() || C == Me) { continue; }
		if (C->IsMonster())
		{
			// the camps are nobody's business unless it is the camp we go for, or the monster is on us
			const AArenaBotController* MC = Cast<AArenaBotController>(C->GetController());
			const bool bOnMe = MC && MC->Foe.Get() == Me;
			const bool bOurCamp = CampOk >= 0 && GM->Camps.IsValidIndex(CampOk) && GM->Camps[CampOk].Units.Contains(C);
			if (Me->IsMinion() || !(bOnMe || bOurCamp)) { continue; }
		}
		if (C->IsStructure() && C->GetTeam() != Me->GetTeam())
		{
			// a structure is a target once the chain opened it; a hero sieges it only with its own minions under it
			if (C->bInvulnerable) { continue; }
			// (the open core also when at most one defender is alive: the team finishes the match; the backdoor
			// cut still takes two thirds of the damage)
			const bool bLastStand = C->StructureKind == ArenaConquest::Core && FoesAlive <= 1;
			if (!Me->IsMinion() && !bLastStand && !GM->MinionsNear(Me->GetTeam(), C->GetActorLocation(), 1100.f)) { continue; }
		}
		ArenaBot::FUnit U; U.Id = C->GetUniqueID(); U.Pos = C->GetActorLocation(); U.HpPct = C->HealthPct(); U.bHero = !C->IsMinion();
		U.bStructure = C->IsStructure(); U.Radius = C->GetCapsuleComponent()->GetScaledCapsuleRadius();
		U.bMelee = !C->IsMinion() && !C->IsRangedKit();
		U.Vel = C->GetVelocity();
		U.bAggro = U.bHero && C->MinionAggroUntil > Now;
		// the enemy's fountain: nobody chases into it. Conquest's fountain is the spawn only (5.5 m) and the core stands
		// outside it: with the arena's 11 m base radius the core counted as "safe" and no hero ever attacked it (a
		// 20-minute bot match: all towers and inhibitors down by 11 min, the open core untouched for 9 min)
		const float SafeR = GM->bConquest ? GM->FountainRadiusCm() : FArenaDatabase::Get().Rules.BaseRadius * 100.f;
		U.bSafe = !U.bStructure && FVector::Dist2D(U.Pos, GM->TeamBase(C->GetTeam())) < SafeR + 150.f;
		ById.Add(U.Id, C);
		(Me->IsHostileTo(C) ? S.Enemies : S.Allies).Add(U);
	}
	// focus: a weak enemy hero one of our heroes is already on (the team finishes it)
	for (ArenaBot::FUnit& E : S.Enemies)
	{
		if (!E.bHero || E.HpPct > 0.5f) { continue; }
		for (const ArenaBot::FUnit& A : S.Allies) { if (A.bHero && FVector::Dist2D(A.Pos, E.Pos) < 1200.f) { E.bFocus = true; break; } }
	}
	S.Lead = ArenaBot::LeadFor(Difficulty);
	const TArray<FArenaAbilityDef>& Abs = Me->GetDef().Abilities;
	for (int32 i = 0; i < 5 && i < Abs.Num(); ++i)
	{
		S.Slots[i].bReady = Me->CanCastSlot(i);
		S.Slots[i].bOffCooldown = Me->CooldownRemaining(i) <= 0.f && (i == 0 || Me->GetRank(i) > 0);
		S.Slots[i].ManaCost = Me->Ability(i).ManaCost;
		S.Slots[i].bSupport = Abs[i].Archetype == EArenaArchetype::Buff;
		S.Slots[i].bDash = Abs[i].Archetype == EArenaArchetype::Dash;
		S.Slots[i].bBackwards = Abs[i].bBackwards;
		S.Slots[i].bCC = Abs[i].StunSeconds > 0.f || Abs[i].SlowPct >= 0.3f;
		S.Slots[i].bUltimate = Abs[i].bUltimate;
		S.Slots[i].SpeedCm = Abs[i].Archetype == EArenaArchetype::Projectile && Abs[i].Gravity <= 0.f ? Abs[i].Speed * 100.f : 0.f;
		// a leap is led too (~26 m/s, 0.35-0.46 s): aimed at where the target stood, its landing blow missed a hero backing away
		if (Abs[i].Archetype == EArenaArchetype::Dash) { S.Slots[i].SpeedCm = 2600.f; }
		S.Slots[i].AreaCm = Abs[i].Radius * 100.f;
		// a ground area reaches past its cast range by most of its radius (aimed at the edge, it still catches the hero)
		const float R = Abs[i].Archetype == EArenaArchetype::Dash ? Abs[i].Distance : (Abs[i].Archetype == EArenaArchetype::GroundAoE ? Abs[i].Range + Abs[i].Radius * 0.6f : Abs[i].Range);
		S.Slots[i].RangeCm = FMath::Max(1.5f, R) * 100.f;
	}

	// sustain: a potion when hurt away from the fountain; recall home when retreating far from it with no hero near
	// a recall starts with no enemy hero within 25 m and breaks only when one comes within 10 m (no start-stop loop)
	// (v12: 30 m, and no enemy minion within 12 m either: their hits broke half the recalls, 1/5 and 4/8 completed)
	bool bHeroNear = false, bHeroClose = false, bMinionNear = false;
	for (const ArenaBot::FUnit& E : S.Enemies)
	{
		const float D = FVector::Dist(E.Pos, S.Self);
		if (E.bHero) { bHeroNear |= D < 3000.f; bHeroClose |= D < 1000.f; }
		else { bMinionNear |= D < 1200.f; }
	}
	if (!Me->IsMinion())
	{
		if (S.HpPct < 0.55f && Me->PotionLeft(0) <= 0.f && Me->Potions[0] > 0 && !Me->IsInOwnBase()) { Me->DrinkPotion(0); }
		if (Me->IsRecalling())
		{
			if (bHeroClose) { Me->CancelRecall(TEXT("enemy")); }
			else { StopMovement(); return; }
		}
	}
	if (bConquestHero)
	{
		// a siege: no enemy hero within 15 m, or ours outnumber theirs around us (a 5v5 nearly always has someone
		// near: towers fell only when a wave broke through alone, 3-4 structures in a 20-minute bot match)
		int32 Foes = 0, Friends = 1;
		for (const ArenaBot::FUnit& E : S.Enemies) { Foes += E.bHero && !E.bStructure && FVector::Dist2D(E.Pos, S.Self) < 1500.f ? 1 : 0; }
		for (const ArenaBot::FUnit& A : S.Allies) { Friends += A.bHero && !A.bStructure && FVector::Dist2D(A.Pos, S.Self) < 1500.f ? 1 : 0; }
		S.bSiege = Foes == 0 || (Friends > Foes && S.HpPct > 0.5f);
	}
	// v18: the armed structures (Conquest): the enemy's with their reach and whom they shoot, ours to fall back to
	if (GM->bConquest && !Me->IsMinion())
	{
		for (const AArenaGameMode::FStructure& St : GM->Structures)
		{
			const AArenaCharacter* T = St.Unit.Get();
			if (!St.bAlive || !T || T->GetDef().Abilities.Num() == 0 || T->GetDef().Abilities[0].Damage <= 0.f) { continue; }
			if (St.Team == Me->GetTeam()) { S.OwnTowers.Add(T->GetActorLocation()); continue; }
			ArenaBot::FTower Tw;
			Tw.Pos = T->GetActorLocation();
			Tw.RangeCm = T->Ability(0).Range * 100.f + T->GetCapsuleComponent()->GetScaledCapsuleRadius() + 60.f;
			const AArenaCharacter* Shot = St.Target.Get();
			Tw.bTargetsMe = Shot == Me;
			Tw.bBusy = Shot && Shot->IsAlive() && Shot->IsMinion() && !Shot->IsStructure() && Shot->GetTeam() == Me->GetTeam();
			Tw.bOnAllyHero = Shot && Shot != Me && !Shot->IsMinion() && Shot->GetTeam() == Me->GetTeam();
			S.EnemyTowers.Add(Tw);
		}
	}
	// v19: the team's picture — heroes alive on both sides, enemy heroes nobody of ours sees, our wave's front
	if (!Me->IsMinion())
	{
		int32 Ours = 0, Theirs = 0, Missing = 0;
		TArray<FVector> Eyes;
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsAlive() && It->GetTeam() == Me->GetTeam()) { Eyes.Add(It->GetActorLocation()); }
		}
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			const AArenaCharacter* C = *It;
			if (!C->IsAlive() || C->IsMinion() || C->IsStructure() || C->GetTeam() > 1) { continue; }
			if (C->GetTeam() == Me->GetTeam()) { ++Ours; continue; }
			++Theirs;
			bool bSeen = false;
			for (const FVector& E : Eyes) { if (FVector::DistSquared2D(E, C->GetActorLocation()) < 3500.f * 3500.f) { bSeen = true; break; } }
			Missing += bSeen ? 0 : 1;
		}
		S.AlliesAlive = Ours; S.EnemiesAlive = Theirs; S.EnemiesMissing = Missing;
		const FVector OurBase = GM->TeamBase(Me->GetTeam()), TheirBase = GM->TeamBase(1 - Me->GetTeam());
		S.bPastMiddle = FVector::Dist2D(S.Self, TheirBase) < FVector::Dist2D(S.Self, OurBase) - 1500.f;
		if (GM->bConquest) { S.Fallback = GM->LaneFront(Me->GetTeam(), HomeLane >= 0 ? HomeLane : 0); }
		else { S.Fallback = OurBase * 0.6f + TheirBase * 0.4f; }
	}
	ArenaBot::FIntent I = ArenaBot::Decide(S);
	// the team's ping (the player's G): heroes with nothing in reach go there (v16)
	FVector PingAt = FVector::ZeroVector;
	const bool bPing = !Me->IsMinion() && !I.bRetreat && I.TargetId == 0 && GM->PingFor(Me->GetTeam(), PingAt) && FVector::Dist2D(PingAt, S.Self) < 8000.f;
	if (bConquestHero && !I.bRetreat)
	{
		// nothing in reach: the goal (defend, the boss, a camp, the lane's front)
		if (I.TargetId == 0) { I.MoveTo = GoalAt; I.StopDistance = 250.f; }
		// the enemy's towers: never walk into one's range unless our minions are under it and it is not on us;
		// under its fire, step out (LoL: a tower kills a lone hero in a few shots)
		for (const AArenaGameMode::FStructure& St : GM->Structures)
		{
			const AArenaCharacter* T = St.Unit.Get();
			if (!St.bAlive || !T || St.Team == Me->GetTeam() || T->GetDef().Abilities.Num() == 0 || T->GetDef().Abilities[0].Damage <= 0.f) { continue; }
			const FVector TP = T->GetActorLocation();
			const float R = T->Ability(0).Range * 100.f + T->GetCapsuleComponent()->GetScaledCapsuleRadius() + 120.f;
			const bool bOnMe = St.Target.Get() == Me;
			const bool bCovered = !bOnMe && GM->MinionsNear(Me->GetTeam(), TP, R - 150.f);
			if (bOnMe && FVector::Dist2D(S.Self, TP) < R + 80.f)
			{
				I.MoveTo = TP + (S.Self - TP).GetSafeNormal2D() * (R + 350.f);
				I.StopDistance = 60.f;
			}
			else if (!bCovered && FVector::Dist2D(I.MoveTo, TP) < R)
			{
				I.MoveTo = TP + (I.MoveTo - TP).GetSafeNormal2D() * (R + 60.f);
				I.StopDistance = FMath::Max(I.StopDistance, 120.f);
			}
		}
	}
	// v19: a full purse (1 400 gold) or an empty mana pool is a reason to go home when no enemy is near and nothing
	// of ours is under attack (bots only shopped when they died or fled)
	bool bBaseCalls = false;
	if (GM->bConquest) { for (const AArenaGameMode::FStructure& St : GM->Structures) { bBaseCalls |= St.bAlive && St.Team == Me->GetTeam() && Now - St.HurtAt < 5.f; } }
	const bool bShopRun = !Me->IsMinion() && !I.bRetreat && !bBaseCalls && !bHeroNear && !bMinionNear && Now - Me->LastDamageTime > 6.f
		&& (Me->Gold >= 1400.f || (S.ManaPct < 0.1f && S.Slots[1].ManaCost > 0.f && (S.HpPct < 0.7f || Me->Gold >= 900.f)) || (S.HpPct < 0.45f && Me->Potions[0] == 0));
	if (bShopRun && FVector::Dist2D(S.Self, S.Base) > 2800.f && !S.bInEnemyBase && Me->StartRecall())
	{
		ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=bot_recall id=%s why=%s gold=%.0f mana=%.0f%% hp=%.0f%%"), Now, *Me->GetDef().Id.ToString(),
			Me->Gold >= 1400.f ? TEXT("shop") : (S.ManaPct < 0.1f ? TEXT("mana") : TEXT("health")), Me->Gold, S.ManaPct * 100.f, S.HpPct * 100.f);
		StopMovement();
		return;
	}
	if (!Me->IsMinion() && I.bRetreat && !bHeroNear && !bMinionNear && !S.bInEnemyBase && FVector::Dist2D(S.Self, S.Base) > 2800.f && Me->StartRecall())
	{
		StopMovement();
		return;
	}
	if (bPing) { I.MoveTo = PingAt; I.StopDistance = 300.f; }
	// a telegraphed area of the enemy under the bot: step out of it before it lands (normal: most of the time, hard:
	// always; easy never looks down)
	if (!Me->IsMinion() && Difficulty > 0)
	{
		for (TActorIterator<AArenaAreaEffect> It(GetWorld()); It; ++It)
		{
			const int32 From = It->SourceTeam();
			if (It->bLanded || From < 0 || From == Me->GetTeam()) { continue; }
			const FVector C = It->GetActorLocation();
			const float R = It->RadiusCm() + Me->GetCapsuleComponent()->GetScaledCapsuleRadius() + 40.f;
			if (FVector::Dist2D(C, S.Self) > R) { continue; }
			if ((It->GetUniqueID() + Me->GetUniqueID()) % 10u >= (Difficulty == 1 ? 5u : 8u)) { continue; }   // v19: normal half of them, hard 80 %
			FVector Out = (S.Self - C).GetSafeNormal2D();
			if (Out.IsNearlyZero()) { Out = Me->GetActorRightVector(); }
			I.MoveTo = C + Out * (R + 180.f);
			I.StopDistance = 40.f;
			UE_LOG(LogArena, Verbose, TEXT("ARENA evt=dodge id=%s"), *Me->GetDef().Id.ToString());
			break;
		}
	}
	// a shot flying at the bot: a side step out of its line (hard: always, normal: half the time, easy never looks)
	if (!Me->IsMinion() && Difficulty > 0)
	{
		for (TActorIterator<AArenaProjectile> It(GetWorld()); It; ++It)
		{
			if (It->IsDecorative() || It->SourceTeam() < 0 || It->SourceTeam() == Me->GetTeam()) { continue; }
			const FVector V = It->GetFlightVelocity();
			const float V2 = V.SizeSquared2D();
			if (V2 < 100.f * 100.f) { continue; }
			const FVector Rel = S.Self - It->GetActorLocation();
			const float T = FVector::DotProduct(FVector(Rel.X, Rel.Y, 0.f), FVector(V.X, V.Y, 0.f)) / V2;
			if (T <= 0.f || T > 0.7f) { continue; }
			const FVector Miss = FVector(Rel.X, Rel.Y, 0.f) - FVector(V.X, V.Y, 0.f) * T;
			if (Miss.Size2D() > Me->GetCapsuleComponent()->GetScaledCapsuleRadius() + 90.f) { continue; }
			if ((It->GetUniqueID() + Me->GetUniqueID()) % 20u >= (Difficulty == 1 ? 7u : 14u)) { continue; }   // v19: normal 35 %, hard 70 % of the shots
			FVector Side = FVector(-V.Y, V.X, 0.f).GetSafeNormal2D();
			if (FVector::DotProduct(Side, Miss) < 0.f) { Side = -Side; }   // away from the line, on the side it already leans to
			I.MoveTo = S.Self + Side * 320.f;
			I.StopDistance = 30.f;
			if ((It->GetUniqueID() + Me->GetUniqueID()) % 3u == 0u) { BotJump(Me, Now, TEXT("dodge")); }
			break;
		}
	}
	// v20: a hop while backing off a melee hero on top of it (Paragon's kiting), a jump onto the step its melee target
	// stands on
	if (!Me->IsMinion() && I.TargetId != 0)
	{
		if (AArenaCharacter** T = ById.Find(I.TargetId))
		{
			const FVector To = (*T)->GetActorLocation() - S.Self;
			const bool bKiting = !ArenaBot::IsMeleeKit(S) && I.StopDistance <= 40.f && To.Size2D() < 600.f && !(*T)->IsMinion();
			const bool bStepUp = ArenaBot::IsMeleeKit(S) && To.Z > 70.f && To.Z < 260.f && To.Size2D() < 450.f;
			if (bStepUp) { BotJump(Me, Now, TEXT("step")); }
			else if (bKiting && FMath::FRand() < 0.35f) { BotJump(Me, Now, TEXT("kite")); }
		}
	}
	LastMoveTo = I.MoveTo;
	bLastRetreat = I.bRetreat;
	const float TravelDist = FVector::Dist2D(I.MoveTo, S.Self);
	const bool bToOrb = S.bOrb && !S.bMinion && FVector::Dist2D(I.MoveTo, S.Orb) < 1.f;   // walk right onto the orb, not to its rim
	bWantsTravel = TravelDist > 300.f;
	if (TravelDist < (bToOrb ? 30.f : I.StopDistance)) { StopMovement(); }
	else
	{
		// multi-level map: a goal can hang in the air (roam point between bases, an enemy mid-jump, a unit on a
		// ledge above); snap it onto the navmesh within 12 m vertically, or pathfinding fails and the bot stands still
		FVector Goal = I.MoveTo;
		FNavLocation OnNav;
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			if (Nav->ProjectPointToNavigation(Goal, OnNav, FVector(400.f, 400.f, 1200.f))) { Goal = OnNav.Location; }
		}
		LastMoveTo = Goal;
		// closing to melee contact: a small acceptance and no "stop on overlap" (that adds the capsule radius, and a
		// target knocked 2 m away already counted as reached, leaving the bot idle out of reach)
		const bool bContact = I.StopDistance < 100.f;
		MoveToLocation(Goal, bToOrb ? 5.f : (bContact ? 20.f : (S.bMinion ? 80.f : 120.f)), !bToOrb && !bContact, true, false, true);
	}
	if (I.TargetId != 0)
	{
		if (AArenaCharacter** T = ById.Find(I.TargetId)) { SetFocus(*T); }
	}
	else { SetFocalPoint(I.MoveTo); }
	if (I.CastSlot >= 0)
	{
		const float Err = ArenaBot::AimErrorCm(Difficulty);
		Me->AimPoint = I.Aim + FVector(FMath::FRandRange(-Err, Err), FMath::FRandRange(-Err, Err), 0.f);
		Me->TryCast(I.CastSlot);
	}
}

int32 AArenaBotController::Jumps = 0;

bool AArenaBotController::BotJump(AArenaCharacter* Me, float Now, const TCHAR* Why)
{
	if (!Me || Me->IsMinion() || Now < NextJump || Me->IsBusy() || Me->IsStunned() || Me->IsDashing() || Me->IsAirborne() || !Me->GetCharacterMovement()->IsMovingOnGround()) { return false; }
	NextJump = Now + FMath::FRandRange(2.5f, 4.5f);
	Me->Jump();
	++Jumps;
	UE_LOG(LogArena, Verbose, TEXT("ARENA t=%.1f evt=bot_jump id=%s why=%s"), Now, *Me->GetDef().Id.ToString(), Why);
	return true;
}

FVector AArenaBotController::ConquestGoal(AArenaCharacter* Me, AArenaGameMode* GM, float Now, int32& OutCamp)
{
	OutCamp = -1;
	const int32 Team = Me->GetTeam();
	const FVector Self = Me->GetActorLocation();
	// 1. defend: one of our structures under attack near us
	for (const AArenaGameMode::FStructure& St : GM->Structures)
	{
		// (v18: an inhibitor or the core under attack calls everyone home, from anywhere on the map)
		const float Reach = St.Kind >= ArenaConquest::Inhibitor ? 1.e6f : 6000.f;
		if (St.bAlive && St.Team == Team && Now - St.HurtAt < 3.f && FVector::Dist2D(St.Spot, Self) < Reach && Me->HealthPct() > 0.3f) { return St.Spot + (GM->TeamBase(Team) - St.Spot).GetSafeNormal2D() * 300.f; }
	}
	// dead enemies: a window to take an objective
	const AArenaGameState* GS = AArenaGameState::Get(this);
	int32 EnemyDead = 0, AllyAlive = 0, EnemyAlive = 0;
	if (GS) { for (const FArenaHeroStat& H : GS->HeroStats) { EnemyDead += H.Team != Team && !H.bAlive ? 1 : 0; AllyAlive += H.Team == Team && H.bAlive ? 1 : 0; EnemyAlive += H.Team != Team && H.bAlive ? 1 : 0; } }
	// 1b. the enemy's core is open (an inhibitor down): finish the match when the team is up in numbers or our wave
	// is already at it (the lane's front stops at the lane's end, 10 m short of the core)
	for (const AArenaGameMode::FStructure& St : GM->Structures)
	{
		const AArenaCharacter* Core = St.Unit.Get();
		if (St.Kind != ArenaConquest::Core || St.Team == Team || !St.bAlive || !Core || Core->bInvulnerable) { continue; }
		if (Me->HealthPct() > 0.45f && (AllyAlive > EnemyAlive || GM->MinionsNear(Team, St.Spot, 1800.f)))
		{
			return St.Spot + (GM->TeamBase(Team) - St.Spot).GetSafeNormal2D() * 350.f;
		}
	}
	const float MatchT = GM->ConquestMatchSeconds();
	for (int32 i = 0; i < GM->Camps.Num(); ++i)
	{
		const AArenaGameMode::FCamp& C = GM->Camps[i];
		if (!C.bUp || !FArenaDatabase::Get().Rules.Conquest.Camps.IsValidIndex(C.Def)) { continue; }
		const FArenaCampDef& D = FArenaDatabase::Get().Rules.Conquest.Camps[C.Def];
		const float Dist = FVector::Dist2D(C.Spot, Self);
		// 2. the boss: with two enemies down, or late with the team up, when near enough
		if (D.Buff == 3 && Dist < 7000.f && Me->HealthPct() > 0.5f && (EnemyDead >= 2 || (MatchT > 900.f && AllyAlive >= 4))) { OutCamp = i; return C.Spot; }
		// 3. our own buff camps (on our half), for the side-lane heroes with nothing better to do, healthy, nobody near
		if ((D.Buff == 1 || D.Buff == 2) && C.Side == Team && Dist < 3500.f && Me->HealthPct() > 0.6f && Me->GetHeroLevel() >= 3)
		{
			bool bEnemyNear = false;
			for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { bEnemyNear |= It->IsAlive() && !It->IsMinion() && It->GetTeam() != Team && It->GetTeam() <= 1 && FVector::Dist2D(It->GetActorLocation(), Self) < 2500.f; }
			if (!bEnemyNear && !Me->HasCampBuff(D.Buff - 1)) { OutCamp = i; return C.Spot; }
		}
	}
	// 4. the lane
	return GM->LaneFront(Team, HomeLane >= 0 ? HomeLane : 0);
}

void AArenaBotController::TickMonster(AArenaCharacter* Me, AArenaGameMode* GM, float Now, float Dt)
{
	const float FromHome = FVector::Dist2D(Me->GetActorLocation(), Home);
	if (Me->bResetting)
	{
		// walking home after the leash broke: it takes no damage and heals on the way (LoL)
		Me->ReceiveHeal(Me->GetMaxHealth() * 0.3f * Dt, nullptr, false);
		if (FromHome < 150.f)
		{
			Me->bResetting = false;
			Me->RefillVitals();
			StopMovement();
			ClearFocus(EAIFocusPriority::Gameplay);
			SetFocalPoint(Home + FRotator(0.f, HomeYaw, 0.f).Vector() * 500.f);
		}
		else if (Now >= NextThink) { NextThink = Now + 0.6f; MoveToLocation(Home, 60.f, false, true, false, true); }
		return;
	}
	AArenaCharacter* F = Foe.Get();
	if (F && !F->IsAlive()) { F = nullptr; }
	// aggro: whoever hit it, or hit one of its camp (the camp fights as one)
	if (!F && Now - Me->LastDamageTime < 3.f && Me->LastAttacker.IsValid() && Me->LastAttacker->IsAlive()) { F = Me->LastAttacker.Get(); }
	if (!F)
	{
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			AArenaCharacter* Mate = *It;
			if (Mate != Me && Mate->IsAlive() && Mate->IsMonster() && FVector::Dist2D(Mate->GetActorLocation(), Home) < 900.f
				&& Now - Mate->LastDamageTime < 3.f && Mate->LastAttacker.IsValid() && Mate->LastAttacker->IsAlive()) { F = Mate->LastAttacker.Get(); break; }
		}
	}
	const bool bWasFighting = Foe.IsValid();
	Foe = F;
	// the leash: too far from home, the foe dragged it away, or it has not been hurt for a while and cannot reach
	const bool bLeash = FromHome > LeashCm || (F && FVector::Dist2D(F->GetActorLocation(), Home) > LeashCm + 500.f)
		|| (F && Now - Me->LastDamageTime > 6.f && FVector::Dist2D(F->GetActorLocation(), Me->GetActorLocation()) > Me->Ability(0).Range * 100.f + 300.f);
	if ((F && bLeash) || (!F && bWasFighting) || (!F && FromHome > 250.f))
	{
		Foe = nullptr;
		Me->bResetting = true;
		Me->AimTarget = nullptr;
		ClearFocus(EAIFocusPriority::Gameplay);
		MoveToLocation(Home, 60.f, false, true, false, true);
		NextThink = Now + 0.6f;
		ARENA_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=leash id=%s from_home=%.0f"), Now, *Me->GetDef().Id.ToString(), FromHome);
		return;
	}
	if (!F || Now < NextThink) { return; }
	NextThink = Now + 0.25f;
	SetFocus(F);
	const float Body = F->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float D = FVector::Dist2D(F->GetActorLocation(), Me->GetActorLocation()) - Body;
	const float Reach = Me->Ability(0).Range * 100.f;
	if (D > Reach * 0.85f && Me->GetDef().MoveSpeed > 0.5f) { MoveToLocation(F->GetActorLocation(), FMath::Max(40.f, Reach * 0.5f), false, true, false, true); }
	else { StopMovement(); }
	Me->AimTarget = F;
	Me->AimPoint = F->GetActorLocation();
	// the boss's special: its second ability on whoever is in reach of it, else the basic attack
	if (Me->GetDef().Abilities.Num() > 1 && Me->CanCastSlot(1) && D <= Me->Ability(1).Range * 100.f) { Me->TryCast(1); }
	else if (D <= Reach + 30.f) { Me->TryCast(0); }
}

void AArenaBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// crowd avoidance: ten heroes plus minion waves body-block each other in melee without it
	if (AArenaCharacter* C = Cast<AArenaCharacter>(InPawn))
	{
		UCharacterMovementComponent* Move = C->GetCharacterMovement();
		// turn toward the focus at a rate (a snap to each new target read as a twitch, and the pack anim blueprints
		// lean and turn in place from the yaw rate)
		// (-ArenaSnapTurn: the old snap, to measure the difference; ARENA_SUMMARY yaw_snaps)
		C->UseSmoothTurning(!FParse::Param(FCommandLine::Get(), TEXT("ArenaSnapTurn")));
		Move->bUseRVOAvoidance = true;
		Move->AvoidanceConsiderationRadius = 350.f;
		Move->AvoidanceWeight = 0.5f;
		// allies step around each other; enemies do not dodge each other (a melee fight reaches body contact)
		FNavAvoidanceMask Own, Other;
		Own.SetGroup(C->GetTeam() == 1 ? 1 : 0);
		Other.SetGroup(C->GetTeam() == 1 ? 0 : 1);
		Move->SetAvoidanceGroupMask(Own);
		Move->SetGroupsToAvoidMask(Own);
		Move->SetGroupsToIgnoreMask(Other);
		// path following through acceleration, like a player's input: the Paragon and mannequin anim blueprints
		// only leave idle while acceleration is non-zero (IsAccelerating / Should Move), so direct velocity
		// requests left every bot sliding in its idle pose
		Move->GetNavMovementProperties()->bUseAccelerationForPaths = true;
	}
}
