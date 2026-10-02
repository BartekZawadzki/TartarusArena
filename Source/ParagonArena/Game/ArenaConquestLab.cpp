// -ArenaConquestLab: Conquest (v14) measured on the real map: the structures placed on the navmesh, the protection
// chain, what hurts a structure (basic attacks from close; a third without minions), tower targeting (minions first,
// "call for help"), the tower's ramp, a tower's fall (the next opens, the team is paid), a camp (aggro, the leash,
// the buff), the boss (no crowd control, the team buff) and the core's fall ending the match. Prints LAB PASS/FAIL
// and LAB_SUMMARY like the other labs; screenshots CQ_*.png.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaPlayerController.h"
#include "Heroes/ArenaCharacter.h"
#include "AI/ArenaBotController.h"
#include "Core/ArenaConquestRules.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"

namespace
{
	struct FConquestLabState
	{
		TWeakObjectPtr<AArenaCharacter> H, E, Ally, Monster;
		int32 T1 = -1, T2 = -1, Inhib = -1, Core = -1;   // team B's mid structures (indices into Structures)
		float StepT = 0.f, Gold0 = 0.f, Hp0 = 0.f;
		TArray<float> Shots;
		int32 HeroIdx = 0;
		bool bShot = false;
		TWeakObjectPtr<ACameraActor> Cam;
	};
	FConquestLabState GCq;

	void Put(AArenaCharacter* C, const FVector& At)
	{
		if (!C) { return; }
		C->SetActorLocation(At + FVector(0.f, 0.f, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
	}
}

void AArenaGameMode::StartConquestLab()
{
	GCq = FConquestLabState();
	bConquest = true;
	bFountainsOn = true;
	LabStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=conquestlab_start"));
}

void AArenaGameMode::TickConquestLab(float Now, float Dt)
{
	const float T = Now - LabStart;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		UE_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	const FArenaConquestDef& Cq = Rules.Conquest;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	auto Look = [PC](const AArenaCharacter* From, const FVector& At, float Pitch)
	{
		if (PC && From) { PC->SetControlRotation(FRotator(Pitch, (At - From->GetActorLocation()).Rotation().Yaw, 0.f)); }
	};
	auto Unit = [this](int32 i) { return Structures.IsValidIndex(i) ? Structures[i].Unit.Get() : nullptr; };
	auto Basic = []() { FArenaHit B; B.bBasic = true; B.Ability = TEXT("lab"); return B; };
	if (LabStep > 0 && LabStep < 90) { TickConquest(Now, Dt); }
	AArenaCharacter* H = GCq.H.Get();

	switch (LabStep)
	{
	case 0:
		// the navmesh first (the structures and camps stand on it)
		if (NavReady() && T > 1.f)
		{
			Phase = EArenaPhase::Playing;
			MatchStart = Now;
			StartConquest();
			for (FCamp& C : Camps) { C.RespawnAt = Cq.Camps.IsValidIndex(C.Def) && Cq.Camps[C.Def].Buff == 3 ? 1.e6f : 0.f; }   // the camps now, the boss later
			const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
			GCq.HeroIdx = FMath::Max(0, Defs.IndexOfByPredicate([](const FArenaHeroDef& D) { return D.Id == TEXT("Sparrow"); }));
			const int32 Ei = FMath::Max(0, Defs.IndexOfByPredicate([](const FArenaHeroDef& D) { return D.Id == TEXT("Countess"); }));
			for (int32 i = 0; i < Structures.Num(); ++i)
			{
				const FStructure& S = Structures[i];
				if (S.Team != 1) { continue; }
				if (S.Kind == ArenaConquest::Tower && S.Lane == 0 && S.Tier == 1) { GCq.T1 = i; }
				if (S.Kind == ArenaConquest::Tower && S.Lane == 0 && S.Tier == 2) { GCq.T2 = i; }
				if (S.Kind == ArenaConquest::Inhibitor && S.Lane == 0) { GCq.Inhib = i; }
				if (S.Kind == ArenaConquest::Core) { GCq.Core = i; }
			}
			int32 Spawned = 0, OnGround = 0;
			for (const FStructure& S : Structures) { Spawned += S.bAlive ? 1 : 0; OnGround += S.bOnNav ? 1 : 0; }
			if (GCq.T2 < 0) { GCq.T2 = GCq.Inhib; }   // one tower per lane: the inhibitor is next
			const int32 Want = 2 * (Lanes.Num() * (Cq.MidAlong.Num()) + 1);
			Check(Structures.Num() == Want && Spawned == Want, FString::Printf(TEXT("%d structures: per team a tower and an inhibitor on each of 3 lanes and the core (%d spawned of %d)"), Want, Spawned, Structures.Num()));
			Check(OnGround == Want, FString::Printf(TEXT("every structure stands on the navmesh (%d/%d)"), OnGround, Want));
			Check(Camps.Num() == 9, FString::Printf(TEXT("camps placed on the navmesh: 2 buffs + 2 minion camps per half + the boss (%d)"), Camps.Num()));
			AArenaCharacter* T1 = Unit(GCq.T1);
			AArenaCharacter* T2 = Unit(GCq.T2);
			AArenaCharacter* Core = Unit(GCq.Core);
			Check(T1 && T2 && Core && !T1->bInvulnerable && T2->bInvulnerable && Core->bInvulnerable, TEXT("the chain: the lane's tower open, the structure behind it and the core shut"));
			if (!T1 || !T2 || !Core) { LabStep = 90; GCq.StepT = T; break; }
			// the player's hero (Sparrow, ranged) 17 m in front of team B's outer mid tower, a team B hero beside it
			const FVector Toward = (TeamBase(0) - T1->GetActorLocation()).GetSafeNormal2D();
			GCq.H = LabSpawn(Defs[GCq.HeroIdx], 0, T1->GetActorLocation() + Toward * 1700.f, 0.f, false, false);
			GCq.E = LabSpawn(Defs[Ei], 1, T1->GetActorLocation() + Toward * 500.f + FVector(0.f, 350.f, 0.f), 180.f, false, false);
			if ((H = GCq.H.Get()) != nullptr)
			{
				H->HeroIndex = GCq.HeroIdx; Heroes.Add(H); PlayerHeroIndex = GCq.HeroIdx;
				if (PC) { PC->Possess(H); }
				Put(H, GroundSpot(T1->GetActorLocation() + Toward * 1700.f));
				Look(H, T1->GetActorLocation() + FVector(0.f, 0.f, 200.f), -8.f);
			}
			if (AArenaCharacter* E = GCq.E.Get()) { E->HeroIndex = Ei; Heroes.Add(E); Put(E, GroundSpot(T1->GetActorLocation() + Toward * 500.f + FVector(0.f, 350.f, 0.f))); E->SetBusyFor(600.f); }
			GCq.StepT = T;
			++LabStep;
		}
		else if (T > 20.f) { Check(false, TEXT("the navmesh came up")); LabStep = 90; GCq.StepT = T; }
		break;
	case 1:
		if (T > GCq.StepT + 2.5f) { UIShot(TEXT("CQ_Tower")); GCq.StepT = T; ++LabStep; }
		break;
	case 2:
	{
		// what hurts a structure: not an ability, not a basic attack from afar; from close a third without minions
		AArenaCharacter* T1 = Unit(GCq.T1);
		AArenaCharacter* T2 = Unit(GCq.T2);
		if (!H || !T1 || !T2) { LabStep = 90; break; }
		FArenaHit Ab; Ab.Ability = TEXT("lab ability");
		const float DAbility = T1->ReceiveHit(80.f, H, Ab);
		const float DFar = T1->ReceiveHit(80.f, H, Basic());
		Put(H, GroundSpot(T1->GetActorLocation() + (TeamBase(0) - T1->GetActorLocation()).GetSafeNormal2D() * 700.f));
		const float DAlone = T1->ReceiveHit(80.f, H, Basic());
		GCq.Ally = LabSpawn(Rules.MeleeMinion, 0, T1->GetActorLocation() + FVector(0.f, -300.f, 150.f), 0.f, true, false);
		const float DMinions = T1->ReceiveHit(80.f, H, Basic());
		const float DShut = T2->ReceiveHit(80.f, H, Basic());
		Check(DAbility == 0.f && DFar == 0.f, FString::Printf(TEXT("an ability (%.0f) and a basic attack from 17 m (%.0f) do not hurt a tower"), DAbility, DFar));
		Check(DMinions > 0.f && FMath::IsNearlyEqual(DAlone / DMinions, 1.f - Cq.BackdoorCut, 0.02f), FString::Printf(TEXT("from 7 m: %.1f alone, %.1f with a minion of ours near (ratio %.2f, rule %.2f)"), DAlone, DMinions, DMinions > 0.f ? DAlone / DMinions : -1.f, 1.f - Cq.BackdoorCut));
		Check(DShut == 0.f && T2->HealthPct() >= 1.f, TEXT("the structure behind the tower takes nothing while the tower stands"));
		GCq.StepT = T;
		++LabStep;
		break;
	}
	case 3:
		// targeting: our minion and our hero in its range: the first target it takes is the minion
		if (Structures[GCq.T1].Target.IsValid() || T > GCq.StepT + 2.f)
		{
			const FStructure& S = Structures[GCq.T1];
			Check(S.Target.Get() == GCq.Ally.Get() && GCq.Ally.IsValid(), FString::Printf(TEXT("a tower takes the minion before the hero (first target=%s)"), S.Target.IsValid() ? *S.Target->GetDef().Id.ToString() : TEXT("none")));
			// "call for help": our hero hurts the tower's hero next to it
			if (AArenaCharacter* E = GCq.E.Get()) { E->ReceiveHit(10.f, H, Basic()); }
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 4:
		if (T > GCq.StepT + 0.6f)
		{
			const FStructure& S = Structures[GCq.T1];
			Check(S.Target.Get() == H, FString::Printf(TEXT("a hero who hits the tower's hero draws its fire (target=%s)"), S.Target.IsValid() ? *S.Target->GetDef().Id.ToString() : TEXT("none")));
			if (AArenaCharacter* A = GCq.Ally.Get()) { A->ReceiveDamage(99999.f, nullptr, false); }
			GCq.Shots.Reset();
			GCq.Hp0 = H ? H->RecentDamage.Num() : 0;
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 5:
		// the ramp: consecutive shots on the hero hit harder (it is kept alive between them)
		if (H)
		{
			for (int32 i = (int32)GCq.Hp0; i < H->RecentDamage.Num(); ++i) { if (H->RecentDamage[i].Source == TEXT("Tower")) { GCq.Shots.Add(H->RecentDamage[i].Amount); } }
			GCq.Hp0 = H->RecentDamage.Num();
			if (H->HealthPct() < 0.9f) { H->RefillVitals(); }
			if (GCq.Shots.Num() == 2 && !GCq.bShot) { GCq.bShot = true; UIShot(TEXT("CQ_TowerShot")); }
		}
		if (GCq.Shots.Num() >= 3 || T > GCq.StepT + 8.f)
		{
			const bool bRamp = GCq.Shots.Num() >= 3 && GCq.Shots[1] > GCq.Shots[0] * 1.25f && GCq.Shots[2] > GCq.Shots[1] * 1.1f;
			Check(bRamp, FString::Printf(TEXT("tower shots on a hero ramp up: %s"), *FString::JoinBy(GCq.Shots, TEXT(" -> "), [](float V) { return FString::Printf(TEXT("%.0f"), V); })));
			// the outer tower falls to a minion's blow: the inner opens, the team is paid, the hero who hurt it last gets the killer's share
			AArenaCharacter* T1 = Unit(GCq.T1);
			GCq.Gold0 = H ? H->Gold : 0.f;
			AArenaCharacter* M = LabSpawn(Rules.MeleeMinion, 0, T1->GetActorLocation() + FVector(0.f, -350.f, 150.f), 0.f, true, false);
			T1->ReceiveHit(80.f, H, Basic());
			if (M) { T1->ReceiveHit(99999.f, M, FArenaHit()); M->ReceiveDamage(99999.f, nullptr, false); }
			if (H) { Put(H, GroundSpot(T1->GetActorLocation() + (TeamBase(0) - T1->GetActorLocation()).GetSafeNormal2D() * 1600.f)); Look(H, T1->GetActorLocation(), -10.f); }
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 6:
		if (T > GCq.StepT + 1.f)
		{
			AArenaCharacter* T2 = Unit(GCq.T2);
			const float Paid = H ? H->Gold - GCq.Gold0 : 0.f;
			Check(!Structures[GCq.T1].bAlive && T2 && !T2->bInvulnerable, TEXT("the tower fell; the structure behind it opened"));
			Check(Paid >= Cq.GoldTower + Cq.GoldTowerKiller - 1.f, FString::Printf(TEXT("the team is paid %d and the hero who hurt it last %d more (+%.0f)"), Cq.GoldTower, Cq.GoldTowerKiller, Paid));
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 7:
		if (T > GCq.StepT + 3.5f) { UIShot(TEXT("CQ_Ruin")); GCq.StepT = T; ++LabStep; }
		break;
	case 8:
	{
		// a camp: team A's red one
		const FCamp* Red = Camps.FindByPredicate([&](const FCamp& C) { return C.Side == 0 && Cq.Camps.IsValidIndex(C.Def) && Cq.Camps[C.Def].Buff == 1; });
		int32 Up = 0;
		for (const FCamp& C : Camps) { Up += C.bUp ? 1 : 0; }
		Check(Red && Red->bUp && Up == 8, FString::Printf(TEXT("the camps are up (%d of 8 without the boss)"), Up));
		if (!Red || Red->Units.Num() == 0 || !H) { LabStep = 20; GCq.StepT = T; break; }
		GCq.Monster = Red->Units[0];
		Put(H, GroundSpot(Red->Spot + FVector(900.f, 0.f, 0.f)));
		Look(H, Red->Spot + FVector(0.f, 0.f, 120.f), -10.f);
		GCq.StepT = T;
		++LabStep;
		break;
	}
	case 9:
		if (T > GCq.StepT + 2.f)
		{
			UIShot(TEXT("CQ_Camp"));
			// aggro: a blow draws it; then the hero stands far past its leash
			if (AArenaCharacter* M = GCq.Monster.Get()) { M->ReceiveHit(60.f, H, Basic()); }
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 10:
		if (T > GCq.StepT + 1.5f)
		{
			AArenaCharacter* M = GCq.Monster.Get();
			const AArenaBotController* AI = M ? Cast<AArenaBotController>(M->GetController()) : nullptr;
			Check(AI && AI->Foe.Get() == H, TEXT("a monster hit by a hero goes for it"));
			if (M && H) { Put(H, GroundSpot(M->GetActorLocation() + FVector(2600.f, 0.f, 0.f))); }
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 11:
		if (T > GCq.StepT + 9.f)
		{
			AArenaCharacter* M = GCq.Monster.Get();
			const AArenaBotController* AI = M ? Cast<AArenaBotController>(M->GetController()) : nullptr;
			const float Home = AI ? FVector::Dist2D(M->GetActorLocation(), AI->Home) : -1.f;
			Check(M && M->IsAlive() && !M->bResetting && Home < 250.f && M->HealthPct() >= 0.999f, FString::Printf(TEXT("left alone past its leash it walks home and heals (%.0f cm from home, health %.0f%%)"), Home, M ? M->HealthPct() * 100.f : -1.f));
			// the kill: the buff and the gold go to the hero
			GCq.Gold0 = H ? H->Gold : 0.f;
			if (M && H) { Put(H, GroundSpot(M->GetActorLocation() + FVector(500.f, 0.f, 0.f))); M->ReceiveHit(99999.f, H, Basic()); }
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 12:
		if (T > GCq.StepT + 1.f)
		{
			Check(H && H->HasCampBuff(0) && H->Gold - GCq.Gold0 >= 80.f, FString::Printf(TEXT("the red camp's kill: its buff on the hero (%.0f s) and gold (+%.0f)"), H ? H->CampBuffLeft(0) : 0.f, H ? H->Gold - GCq.Gold0 : 0.f));
			// the boss now
			for (FCamp& C : Camps) { if (Cq.Camps.IsValidIndex(C.Def) && Cq.Camps[C.Def].Buff == 3) { C.RespawnAt = 0.f; } }
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 13:
		if (T > GCq.StepT + 1.f)
		{
			const FCamp* Boss = Camps.FindByPredicate([&](const FCamp& C) { return Cq.Camps.IsValidIndex(C.Def) && Cq.Camps[C.Def].Buff == 3; });
			AArenaCharacter* B = Boss && Boss->Units.Num() > 0 ? Boss->Units[0].Get() : nullptr;
			Check(B && B->IsBoss(), TEXT("the boss stands in the centre"));
			if (B)
			{
				B->ApplyStun(2.f);
				Check(!B->IsStunned(), TEXT("the boss shrugs off a stun"));
				GCq.Monster = B;
				// a camera beside the pit (the hero's view there sat right behind a statue)
				const FVector CamAt = B->GetActorLocation() + FVector(1500.f, -1500.f, 450.f);
				if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamAt, (B->GetActorLocation() - CamAt).Rotation()))
				{
					GCq.Cam = Cam;
					if (PC) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
				}
			}
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 14:
		if (T > GCq.StepT + 4.f) { UIShot(TEXT("CQ_Boss")); GCq.StepT = T; LabStep = 30; }
		break;
	case 30:
		if (T > GCq.StepT + 0.4f)
		{
			if (AArenaCharacter* B = GCq.Monster.Get())
			{
				UE_LOG(LogArena, Display, TEXT("LAB boss body: bounds=%s scale=%.2f materials=%d"), *B->GetMesh()->Bounds.BoxExtent.ToCompactString(), B->GetActorScale3D().X, B->GetMesh()->GetNumMaterials());
				B->ReceiveHit(99999.f, H, Basic());
				if (PC && H) { PC->SetViewTarget(H); PC->bAutoManageActiveCameraTarget = true; }
			}
			GCq.StepT = T;
			LabStep = 15;
		}
		break;
	case 15:
		if (T > GCq.StepT + 1.f)
		{
			Check(BossTeam == 0 && H && H->HasCampBuff(2), FString::Printf(TEXT("the boss's kill: the team buff (team %d, %.0f s on the hero)"), BossTeam, H ? H->CampBuffLeft(2) : 0.f));
			// an overview of team A's half: structures, the camps, the lanes
			if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(FVector(-600.f, -5200.f, 5200.f), FRotator(-50.f, 145.f, 0.f)))
			{
				GCq.Cam = Cam;
				if (PC) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
			}
			GCq.StepT = T;
			++LabStep;
		}
		break;
	case 16:
		if (T > GCq.StepT + 2.5f) { UIShot(TEXT("CQ_Overview")); GCq.StepT = T; LabStep = 31; }
		break;
	case 31:
		if (T > GCq.StepT + 0.5f)
		{
			if (PC && H) { PC->SetViewTarget(H); PC->bAutoManageActiveCameraTarget = true; }
			GCq.StepT = T;
			LabStep = 32;
		}
		break;
	case 32:
		if (T > GCq.StepT + 1.5f) { UIShot(TEXT("CQ_HUD")); GCq.StepT = T; LabStep = 17; }
		break;
	case 17:
		if (T > GCq.StepT + 0.5f)
		{
			// the core: shut until an inhibitor falls; its fall ends the match
			AArenaCharacter* Core = Unit(GCq.Core);
			AArenaCharacter* M = LabSpawn(Rules.MeleeMinion, 0, FVector(0.f, 0.f, 200.f), 0.f, true, false);
			if (GCq.T2 != GCq.Inhib) { if (AArenaCharacter* T2 = Unit(GCq.T2)) { T2->ReceiveHit(99999.f, M, FArenaHit()); } }
			const bool bShutBefore = Core && Core->bInvulnerable;
			if (AArenaCharacter* I = Unit(GCq.Inhib)) { I->ReceiveHit(99999.f, M, FArenaHit()); }
			Check(bShutBefore && Core && !Core->bInvulnerable, TEXT("the core opens once an inhibitor falls"));
			Check(Structures[GCq.Inhib].RespawnAt > Now, FString::Printf(TEXT("the inhibitor comes back in %.0f s"), Structures[GCq.Inhib].RespawnAt - Now));
			if (Core && M) { Core->ReceiveHit(99999.f, M, FArenaHit()); }
			Check(Phase == EArenaPhase::Ended && WinnerTeam == 0, FString::Printf(TEXT("the core's fall ends the match (winner %d)"), WinnerTeam));
			if (M) { M->ReceiveDamage(99999.f, nullptr, false); }
			GCq.StepT = T;
			LabStep = 90;
		}
		break;
	case 20:
		LabStep = 90;
		break;
	case 90:
		if (T > GCq.StepT + 1.f)
		{
			UE_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			++LabStep;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
