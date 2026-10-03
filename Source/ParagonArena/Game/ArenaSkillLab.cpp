// -ArenaSkillLab: the MOBA rules of v6 measured on live units (VR-20 ranks and levels, VR-21 fixed displacements and
// telegraphs, VR-12 item passives and recipes). Every number is compared with what heroes.json and ArenaCore say.
//   ranks      Greystone's Cleave at rank 1 and rank 5: damage, slow, knockback distance, mana, cooldown
//   ultimate   Annihilation: the telegraph fills from the cast and is half full at half the delay; knock-up height,
//              air time and stun length
//   levels     XP curve, stats per level, skill points, the ultimate opening at level 5 (a bot spends by its order)
//   items      deterministic crits (every 4th with 25 %) at 225 %, execute, thorns, last stand, spell blade
//   shots      a shot hits a body whose edge is inside its width and misses one 8 cm outside
//   shop       the tooltip, the ability bar and the recipe shop with the player controller; a recipe buy uses up the
//              owned part and costs the rest
// Prints LAB PASS/FAIL and LAB_SUMMARY like the other labs; screenshots SKILL_*.png.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaEvidence.h"
#include "Game/ArenaPlayerController.h"
#include "Heroes/ArenaCharacter.h"
#include "Arena/ArenaIndicator.h"
#include "Abilities/ArenaAbility.h"
#include "UI/ArenaHUD.h"
#include "UI/ArenaSettings.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

namespace
{
	const FVector LabA(-5700.f, -700.f, 400.f);   // plateau A (top z 300), inside the shop radius of base A

	struct FSkillLabState
	{
		TWeakObjectPtr<AArenaCharacter> G, D, H, Gi;
		float StepT = 0.f, MinHP = 0.f, MinG = 0.f, HP0 = 0.f, Mana0 = 0.f, Z0 = 0.f, MaxZ = 0.f, AirStart = -1.f, AirEnd = -1.f, Fill50 = -1.f, FirstStun = -1.f, Power0 = 0.f;
		FVector P0 = FVector::ZeroVector;
		TArray<float> Drops;
		TArray<int32> ToCrit;
		int32 Sub = 0;
		TWeakObjectPtr<AArenaIndicator> Tele;
	};
	FSkillLabState GSkillLab;

	const FArenaHeroDef& HeroDefOf(const TCHAR* Id)
	{
		const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
		const FArenaHeroDef* D = Defs.FindByPredicate([Id](const FArenaHeroDef& H) { return H.Id == Id; });
		return D ? *D : Defs[0];
	}
	int32 HeroIndexOf(const TCHAR* Id) { return FArenaDatabase::Get().Heroes.IndexOfByPredicate([Id](const FArenaHeroDef& H) { return H.Id == Id; }); }

	/** The damage the rules say a hit deals (mitigation by the target's armour, the attacker's penetration). */
	float Expected(const AArenaCharacter* Src, const AArenaCharacter* Dst, float Raw, bool bCrit = false, float CritMult = 1.75f)
	{
		ArenaCore::FDamageInput In;
		In.Base = Raw;
		In.TargetArmor = Dst->GetArmor();
		In.ArmorPenFlat = Src ? Src->ItemBonus().ArmorPen : 0.f;
		In.bCrit = bCrit;
		In.CritMultiplier = CritMult;
		return ArenaCore::ComputeDamage(In);
	}

	void Place(AArenaCharacter* C, const FVector& At, float Yaw)
	{
		if (!C) { return; }
		C->SetActorLocationAndRotation(At, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::ResetPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
		C->RefillVitals();
	}

	void SetItemsByIds(AArenaCharacter* C, std::initializer_list<const TCHAR*> Ids)
	{
		TArray<int32> Items;
		for (const TCHAR* Id : Ids) { const int32 I = FArenaDatabase::ItemIndex(Id); if (I != INDEX_NONE) { Items.Add(I); } }
		C->SetItems(Items);
		C->RefillVitals();
	}
}

void AArenaGameMode::StartSkillLab()
{
	GSkillLab = FSkillLabState();
	// load every effect up front: a first-time load hitch would bend the timing measurements
	for (const FArenaHeroDef& Hd : FArenaDatabase::Get().Heroes)
	{
		for (const FArenaAbilityDef& Ab : Hd.Abilities) { for (const FString* P : { &Ab.Fx, &Ab.CastFx, &Ab.TrailFx, &Ab.Sound, &Ab.ImpactSound }) { if (!P->IsEmpty()) { LoadObject<UObject>(nullptr, **P, nullptr, LOAD_NoWarn | LOAD_Quiet); } } }
	}
	LoadObject<UObject>(nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	GSkillLab.G = LabSpawn(HeroDefOf(TEXT("Greystone")), 0, LabA, 90.f, false, false);
	GSkillLab.D = LabSpawn(HeroDefOf(TEXT("Countess")), 1, LabA + FVector(0.f, 200.f, 0.f), 270.f, false, false);
	if (GSkillLab.G.IsValid()) { GSkillLab.G->HeroIndex = HeroIndexOf(TEXT("Greystone")); }
	// a camera on the test ground (the player controller takes over for the HUD shots at the end)
	if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(LabA + FVector(-1100.f, 100.f, 800.f), FRotator(-38.f, 0.f, 0.f)))
	{
		Cam->GetCameraComponent()->SetFieldOfView(70.f);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(Cam); }
	}
	Phase = EArenaPhase::Playing;
	LabStart = GetWorld()->GetTimeSeconds();
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=skilllab_start"));
}

void AArenaGameMode::TickSkillLab(float Now)
{
	const float T = Now - LabStart;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		ARENA_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	AArenaCharacter* G = GSkillLab.G.Get();
	AArenaCharacter* D = GSkillLab.D.Get();
	if ((!G || !D) && LabStep < 90) { Check(false, TEXT("skill lab units exist")); LabStep = 90; }
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FArenaAbilityDef& Cleave = HeroDefOf(TEXT("Greystone")).Abilities[1];
	const FArenaAbilityDef& Ult = HeroDefOf(TEXT("Greystone")).Abilities[4];
	auto Near = [](float A, float B, float Tol) { return FMath::Abs(A - B) <= Tol; };
	if (D) { GSkillLab.MinHP = FMath::Min(GSkillLab.MinHP, D->GetHealth()); }
	if (G) { GSkillLab.MinG = FMath::Min(GSkillLab.MinG, G->GetHealth()); }

	switch (LabStep)
	{
	// ---- ranks: Cleave at rank 1, then rank 5 ------------------------------------------------------------------
	case 0: case 2:
		if (T > (LabStep == 0 ? 2.5f : GSkillLab.StepT + 1.4f))
		{
			const int32 Rank = LabStep == 0 ? 1 : 5;
			G->ForceRanks(Rank);
			G->ResetCooldowns();
			Place(G, LabA, 90.f);
			Place(D, LabA + FVector(0.f, 200.f, 0.f), 270.f);
			GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth(); GSkillLab.Mana0 = G->GetMana(); GSkillLab.P0 = D->GetActorLocation(); GSkillLab.Power0 = G->GetPower();
			G->AimPoint = D->GetActorLocation();
			Check(G->TryCast(1), FString::Printf(TEXT("cast Cleave at rank %d"), Rank));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 1: case 3:
		if (T > GSkillLab.StepT + 1.0f)
		{
			const int32 Rank = LabStep == 1 ? 1 : 5;
			const float Want = Expected(G, D, ArenaCore::Ranked(Cleave.Damage, Cleave.DamagePerRank, Rank) + Cleave.PowerScale * GSkillLab.Power0);
			const float Got = GSkillLab.HP0 - GSkillLab.MinHP;
			Check(Near(Got, Want, Want * 0.02f + 3.f), FString::Printf(TEXT("rank %d damage: %.0f dealt, %.0f by the data (%.0f base + %.0f%% power)"), Rank, Got, Want, ArenaCore::Ranked(Cleave.Damage, Cleave.DamagePerRank, Rank), Cleave.PowerScale * 100.f));
			const float Slow = FMath::Min(0.8f, ArenaCore::Ranked(Cleave.SlowPct, Cleave.SlowPerRank, Rank));
			Check(D->IsSlowed() && Near(D->GetSlowPct(), Slow, 0.001f), FString::Printf(TEXT("rank %d slow: %.0f%% (data %.0f%%)"), Rank, D->GetSlowPct() * 100.f, Slow * 100.f));
			const float Push = FVector::Dist2D(D->GetActorLocation(), GSkillLab.P0);
			Check(Near(Push, ArenaCore::KnockDistanceCm(Cleave.Knockback), 25.f), FString::Printf(TEXT("knockback: pushed %.0f cm, %.0f planned"), Push, ArenaCore::KnockDistanceCm(Cleave.Knockback)));
			const float Mana = ArenaCore::Ranked(Cleave.ManaCost, Cleave.ManaPerRank, Rank);
			Check(Near(GSkillLab.Mana0 - G->GetMana(), Mana, 6.f), FString::Printf(TEXT("rank %d mana: %.0f spent (data %.0f)"), Rank, GSkillLab.Mana0 - G->GetMana(), Mana));
			const float Cd = ArenaCore::Ranked(Cleave.Cooldown, Cleave.CooldownPerRank, Rank);
			Check(Near(G->CooldownTotal(1), Cd, 0.01f), FString::Printf(TEXT("rank %d cooldown: %.1f s (data %.1f)"), Rank, G->CooldownTotal(1), Cd));
			if (Rank == 5) { UIShot(TEXT("SKILL_Knockback")); }
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	// ---- the ultimate: telegraph from the cast, knock-up, stun ---------------------------------------------------
	case 4:
		if (T > GSkillLab.StepT + 1.0f)
		{
			G->ForceRanks(1);
			G->ResetCooldowns();
			Place(G, LabA, 90.f);
			Place(D, LabA + FVector(0.f, 400.f, 0.f), 270.f);
			GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth(); GSkillLab.Z0 = D->GetActorLocation().Z; GSkillLab.MaxZ = 0.f; GSkillLab.AirStart = GSkillLab.AirEnd = GSkillLab.Fill50 = GSkillLab.FirstStun = -1.f; GSkillLab.Power0 = G->GetPower();
			G->AimPoint = D->GetActorLocation() - FVector(0.f, 0.f, 90.f);
			Check(G->TryCast(4), TEXT("cast Annihilation"));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 5:
	{
		if (!GSkillLab.Tele.IsValid())
		{
			for (TActorIterator<AArenaIndicator> It(GetWorld()); It; ++It) { if (It->TelegraphFill() < 1.f || It->GetLifeSpan() > 0.f) { GSkillLab.Tele = *It; } }
		}
		if (GSkillLab.Fill50 < 0.f && T >= GSkillLab.StepT + Ult.Delay * 0.5f && GSkillLab.Tele.IsValid()) { GSkillLab.Fill50 = GSkillLab.Tele->TelegraphFill(); GSkillLab.Z0 = D->GetActorLocation().Z; GSkillLab.MaxZ = 0.f; UIShot(TEXT("SKILL_Telegraph")); }
		GSkillLab.MaxZ = FMath::Max(GSkillLab.MaxZ, D->GetActorLocation().Z - GSkillLab.Z0);
		if (GSkillLab.Fill50 >= 0.f && GSkillLab.AirStart < 0.f && D->GetActorLocation().Z - GSkillLab.Z0 > 4.f) { GSkillLab.AirStart = T - (GetWorld()->GetTimeSeconds() - D->LastDamageTime); }
		if (GSkillLab.AirStart > 0.f && GSkillLab.AirEnd < 0.f && T > GSkillLab.AirStart + 0.1f && D->GetCharacterMovement()->IsMovingOnGround()) { GSkillLab.AirEnd = T; }
		if (D->IsStunned() && GSkillLab.FirstStun < 0.f) { GSkillLab.FirstStun = D->StunRemaining() + (GetWorld()->GetTimeSeconds() - D->LastDamageTime); }   // from the blow, whatever the frame
		if (T > GSkillLab.StepT + 3.0f)
		{
			Check(GSkillLab.Fill50 > 0.f && Near(GSkillLab.Fill50, 0.5f, 0.1f), FString::Printf(TEXT("the telegraph is half full at half the delay: %.2f"), GSkillLab.Fill50));
			const float Hgt = ArenaCore::KnockHeightCm(Ult.KnockUp);
			Check(Near(GSkillLab.MaxZ, Hgt, 25.f), FString::Printf(TEXT("knock-up: %.0f cm up, %.0f planned"), GSkillLab.MaxZ, Hgt));
			const float Air = GSkillLab.AirEnd > 0.f ? GSkillLab.AirEnd - GSkillLab.AirStart : -1.f;
			const float AirPlan = ArenaCore::KnockAirSeconds(Ult.KnockUp) + 0.12f;   // + the ultimate's hit-stop
			Check(Near(Air, AirPlan, 0.12f), FString::Printf(TEXT("air time: %.2f s, %.2f planned (%.2f flight + 0.12 hit-stop)"), Air, AirPlan, ArenaCore::KnockAirSeconds(Ult.KnockUp)));
			// v21: a melee hero shrugs off a share of every stun (rules.meleeTenacity), so the plan is the data's length less that share
			const float StunPlan = Ult.StunSeconds * (D->IsMeleeHero() ? 1.f - FMath::Clamp(FArenaDatabase::Get().Rules.MeleeTenacity, 0.f, 0.8f) : 1.f);
			Check(Near(GSkillLab.FirstStun, StunPlan, 0.06f), FString::Printf(TEXT("stun: %.2f s, %.2f planned (data %.2f, melee tenacity %s)"), GSkillLab.FirstStun, StunPlan, Ult.StunSeconds, D->IsMeleeHero() ? TEXT("on") : TEXT("off")));
			const float Want = Expected(G, D, Ult.Damage + Ult.PowerScale * GSkillLab.Power0);
			Check(Near(GSkillLab.HP0 - GSkillLab.MinHP, Want, Want * 0.02f + 3.f), FString::Printf(TEXT("ultimate damage: %.0f dealt, %.0f by the data"), GSkillLab.HP0 - GSkillLab.MinHP, Want));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	}
	// ---- levels and skill points (a bot spends them by its order) ---------------------------------------------
	case 6:
		if (T > GSkillLab.StepT + 0.5f)
		{
			GSkillLab.H = LabSpawn(HeroDefOf(TEXT("Sparrow")), 0, LabA + FVector(500.f, 0.f, 0.f), 90.f, false, false);
			AArenaCharacter* Hh = GSkillLab.H.Get();
			if (!Hh) { Check(false, TEXT("level test unit")); LabStep = 90; break; }
			const int32 Fresh[5] = { 1, 0, 0, 0, 0 };
			Hh->SetRanks(Fresh);
			const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
			const FArenaHeroDef& Sd = Hh->GetDef();
			const float Hp1 = Hh->GetMaxHealth(), Pw1 = Hh->GetPower();
			Check(Hh->GetHeroLevel() == 1 && Hh->FreeSkillPoints() == 1 && !Hh->CanRankUp(4) && Hh->CanRankUp(1), TEXT("level 1: one point, abilities 1-3 open, the ultimate closed"));
			Hh->AddXp(ArenaCore::XpToNext(1, Ru.XpBase, Ru.XpGrowth) - 0.5f);
			const bool bStill1 = Hh->GetHeroLevel() == 1;
			Hh->AddXp(0.6f);
			Check(bStill1 && Hh->GetHeroLevel() == 2, FString::Printf(TEXT("level 2 exactly at %.0f XP"), ArenaCore::XpToNext(1, Ru.XpBase, Ru.XpGrowth)));
			Check(Near(Hh->GetMaxHealth() - Hp1, Sd.HealthPerLevel, 0.5f) && Near(Hh->GetPower() - Pw1, Sd.PowerPerLevel, 0.05f),
				FString::Printf(TEXT("a level adds %.0f health and %.1f power (data %.0f, %.1f)"), Hh->GetMaxHealth() - Hp1, Hh->GetPower() - Pw1, Sd.HealthPerLevel, Sd.PowerPerLevel));
			Hh->AddXp(ArenaCore::XpForLevel(5, Ru.XpBase, Ru.XpGrowth) - Hh->Xp + 0.5f);
			const int32* R = Hh->GetRanks();
			Check(Hh->GetHeroLevel() == 5 && R[4] == 1 && R[1] + R[2] + R[3] + R[4] == 5 && Hh->FreeSkillPoints() == 0,
				FString::Printf(TEXT("level 5: the bot took the ultimate and spent all 5 points (%d/%d/%d/%d)"), R[1], R[2], R[3], R[4]));
			Hh->Destroy();
			GSkillLab.StepT = T;
			GSkillLab.Sub = 0;
			GSkillLab.Drops.Reset(); GSkillLab.ToCrit.Reset();
			SetItemsByIds(G, { TEXT("infinity") });
			G->ForceRanks(1);
			++LabStep;
		}
		break;
	// ---- items: crits every 4th hit at 225 % ---------------------------------------------------------------------
	case 7:
		if (T > GSkillLab.StepT + 1.2f)
		{
			if (GSkillLab.Sub > 0) { GSkillLab.Drops.Add(GSkillLab.HP0 - GSkillLab.MinHP); }
			if (GSkillLab.Sub == 4)
			{
				FString Pattern;
				for (int32 C : GSkillLab.ToCrit) { Pattern += FString::FromInt(C) + TEXT(" "); }
				const float Ratio = GSkillLab.Drops.Num() == 4 && GSkillLab.Drops[0] > 0.f ? GSkillLab.Drops[3] / GSkillLab.Drops[0] : 0.f;
				Check(GSkillLab.Drops.Num() == 4 && Near(GSkillLab.Drops[1], GSkillLab.Drops[0], 2.f) && Near(GSkillLab.Drops[2], GSkillLab.Drops[0], 2.f) && Near(Ratio, 2.25f, 0.04f),
					FString::Printf(TEXT("25%% crit = every 4th basic attack at 225%%: hits %.0f %.0f %.0f %.0f (x%.2f), hits-to-crit shown %s"), GSkillLab.Drops.Num() > 0 ? GSkillLab.Drops[0] : 0.f,
						GSkillLab.Drops.Num() > 1 ? GSkillLab.Drops[1] : 0.f, GSkillLab.Drops.Num() > 2 ? GSkillLab.Drops[2] : 0.f, GSkillLab.Drops.Num() > 3 ? GSkillLab.Drops[3] : 0.f, Ratio, *Pattern));
				Check(GSkillLab.ToCrit.Num() == 4 && GSkillLab.ToCrit[0] == 4 && GSkillLab.ToCrit[3] == 1, TEXT("the HUD's hits-to-crit counts down 4, 3, 2, 1"));
				SetItemsByIds(G, { TEXT("reaper") });
				GSkillLab.StepT = T;
				++LabStep;
				break;
			}
			Place(D, LabA + FVector(0.f, 200.f, 0.f), 270.f);
			GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth();
			GSkillLab.ToCrit.Add(G->HitsToCrit());
			G->ResetCooldowns();
			G->AimPoint = D->GetActorLocation();
			G->TryCast(0);
			++GSkillLab.Sub;
			GSkillLab.StepT = T;
		}
		break;
	// ---- execute: +25 % below 35 % health -------------------------------------------------------------------------
	case 8:
		if (T > GSkillLab.StepT + 0.6f)
		{
			Place(D, LabA + FVector(0.f, 200.f, 0.f), 270.f);
			for (int32 Guard = 0; Guard < 60 && D->IsAlive() && D->HealthPct() > 0.34f; ++Guard) { D->ReceiveDamage(D->GetMaxHealth() * 0.05f, nullptr, false); }   // just under 35 %
			GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth(); GSkillLab.Power0 = G->GetPower();
			G->ResetCooldowns();
			G->AimPoint = D->GetActorLocation();
			G->TryCast(0);
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 9:
		if (T > GSkillLab.StepT + 0.7f)
		{
			const FArenaAbilityDef B = G->Ability(0);
			const float Want = Expected(G, D, (B.Damage + B.PowerScale * GSkillLab.Power0) * 1.25f);
			Check(Near(GSkillLab.HP0 - GSkillLab.MinHP, Want, Want * 0.02f + 2.f), FString::Printf(TEXT("execute below 35%%: %.0f dealt, %.0f by the rules (+25%%, 12 armour pen)"), GSkillLab.HP0 - GSkillLab.MinHP, Want));
			SetItemsByIds(G, {});
			SetItemsByIds(D, { TEXT("titan") });
			Place(D, LabA + FVector(0.f, 200.f, 0.f), 270.f);
			Place(G, LabA, 90.f);
			GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth(); GSkillLab.Mana0 = GSkillLab.MinG = G->GetHealth();
			G->ResetCooldowns();
			G->AimPoint = D->GetActorLocation();
			G->TryCast(0);
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	// ---- thorns: 20 % of a basic attack comes back ----------------------------------------------------------------
	case 10:
		if (T > GSkillLab.StepT + 0.7f)
		{
			const float Dealt = GSkillLab.HP0 - GSkillLab.MinHP;
			const float Back = GSkillLab.Mana0 - GSkillLab.MinG;
			const float Want = Expected(D, G, Dealt * 0.2f);
			Check(Dealt > 0.f && Near(Back, Want, 2.f), FString::Printf(TEXT("thorns: %.0f dealt, %.0f reflected (rules %.0f)"), Dealt, Back, Want));
			SetItemsByIds(D, { TEXT("aegis") });
			Place(D, LabA + FVector(0.f, 200.f, 0.f), 270.f);
			for (int32 Guard = 0; Guard < 60 && D->IsAlive() && D->HealthPct() > 0.33f; ++Guard) { D->ReceiveDamage(D->GetMaxHealth() * 0.05f, nullptr, false); }
			const float Before = D->GetShield();
			GSkillLab.HP0 = Before;
			G->ResetCooldowns();
			G->AimPoint = D->GetActorLocation();
			G->TryCast(0);
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	// ---- last stand: a shield of 25 % max health under 30 % -------------------------------------------------------
	case 11:
		if (T > GSkillLab.StepT + 0.5f)
		{
			const float Want = D->GetMaxHealth() * 0.25f;
			Check(D->HealthPct() < 0.3f ? Near(D->GetShield(), Want, 12.f) : false, FString::Printf(TEXT("last stand: at %.0f%% health a %.0f shield (25%% of %.0f)"), D->HealthPct() * 100.f, D->GetShield(), D->GetMaxHealth()));
			SetItemsByIds(D, {});
			SetItemsByIds(G, { TEXT("triforce") });
			Place(D, LabA + FVector(0.f, 200.f, 0.f), 270.f);
			G->ForceRanks(1);
			G->ResetCooldowns();
			G->TryCast(3);                                        // Iron Will arms the Sorcerer's Blade
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 12:
		if (T > GSkillLab.StepT + 0.6f)
		{
			Check(G->IsSpellbladeArmed(), TEXT("spell blade armed by an ability"));
			GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth(); GSkillLab.Power0 = G->GetPower();
			G->ResetCooldowns();
			G->AimPoint = D->GetActorLocation();
			G->TryCast(0);
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 13:
		if (T > GSkillLab.StepT + 0.7f)
		{
			const FArenaAbilityDef B = G->Ability(0);
			const float Want = Expected(G, D, B.Damage + B.PowerScale * GSkillLab.Power0 + 1.f * GSkillLab.Power0);
			Check(!G->IsSpellbladeArmed() && Near(GSkillLab.HP0 - GSkillLab.MinHP, Want, Want * 0.02f + 2.f), FString::Printf(TEXT("spell blade: the basic attack deals %.0f (rules %.0f with +100%% power) and uses it up"), GSkillLab.HP0 - GSkillLab.MinHP, Want));
			SetItemsByIds(G, {});
			// shots: Gideon's basic (0.5 m wide) at a body 8 m away, the line passing 8 cm inside and 8 cm outside its edge
			GSkillLab.Gi = LabSpawn(HeroDefOf(TEXT("Gideon")), 0, LabA + FVector(-500.f, 0.f, 0.f), 90.f, false, false);
			Place(D, LabA + FVector(-500.f, 800.f, 0.f), 270.f);
			D->ApplyStun(10.f);
			GSkillLab.Sub = 0;
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 14:
		if (AArenaCharacter* Gi = GSkillLab.Gi.Get())
		{
			if (T > GSkillLab.StepT + 1.2f)
			{
				const float Edge = Gi->Ability(0).Width * 50.f + D->GetCapsuleComponent()->GetScaledCapsuleRadius();
				if (GSkillLab.Sub == 1 || GSkillLab.Sub == 2)
				{
					const bool bHit = GSkillLab.MinHP < GSkillLab.HP0 - 0.5f;
					Check(GSkillLab.Sub == 1 ? bHit : !bHit, FString::Printf(TEXT("a shot %s the body edge %s (edge at %.0f cm from the line)"), GSkillLab.Sub == 1 ? TEXT("8 cm inside") : TEXT("8 cm outside"), bHit ? TEXT("hits") : TEXT("misses"), Edge));
				}
				if (GSkillLab.Sub >= 2) { Gi->Destroy(); GSkillLab.StepT = T; ++LabStep; break; }
				D->RefillVitals();
				GSkillLab.HP0 = GSkillLab.MinHP = D->GetHealth();
				Gi->ResetCooldowns();
				Gi->AimPoint = D->GetActorLocation() + FVector(GSkillLab.Sub == 0 ? Edge - 8.f : Edge + 8.f, 0.f, 0.f);
				Gi->TryCast(0);
				++GSkillLab.Sub;
				GSkillLab.StepT = T;
			}
		}
		break;
	// ---- the player's view: tooltip, ability bar, recipe shop ------------------------------------------------------
	case 15:
		if (T > GSkillLab.StepT + 0.5f && PC)
		{
			Heroes.Add(G);
			PlayerHeroIndex = G->HeroIndex;
			Place(G, LabA, 90.f);
			Place(D, LabA + FVector(0.f, 600.f, 0.f), 270.f);
			PC->Possess(G);
			PC->SetControlRotation(FRotator(-10.f, 90.f, 0.f));
			const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
			G->AddXp(ArenaCore::XpForLevel(7, Ru.XpBase, Ru.XpGrowth) - G->Xp + 1.f);
			const int32 Ranks[5] = { 1, 3, 2, 0, 1 };                // level 7: one point left, ability 3 not learnt
			G->SetRanks(Ranks);
			if (AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC)) { APC->BeginAim(1); }
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 16:
		if (T > GSkillLab.StepT + 1.0f)
		{
			Check(G->FreeSkillPoints() == 1 && G->CanRankUp(1) && G->CanRankUp(3) && !G->CanRankUp(4), TEXT("level 7 with ranks 3/2/0/1: one free point; ability 1 may go to rank 4, ability 3 can be learnt, the ultimate waits for level 9"));
			UIShot(TEXT("SKILL_Tooltip"));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 17:
		if (T > GSkillLab.StepT + 0.6f)
		{
			if (AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC))
			{
				APC->CancelAim();
				SetItemsByIds(G, { TEXT("warhammer"), TEXT("boots") });
				G->Gold = 2000.f;
				APC->SetShopOpen(true);
				if (AArenaHUD* HUD = Cast<AArenaHUD>(APC->GetHUD())) { HUD->ShopSelect(FArenaDatabase::ItemIndex(TEXT("reaper"))); }
			}
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 18:
		if (T > GSkillLab.StepT + 1.0f)
		{
			UIShot(TEXT("SKILL_Shop"));
			const int32 Reaper = FArenaDatabase::ItemIndex(TEXT("reaper"));
			const int32 Full = FArenaDatabase::ItemTotalCost(Reaper), Hammer = FArenaDatabase::ItemTotalCost(FArenaDatabase::ItemIndex(TEXT("warhammer")));
			const int32 Price = PriceFor(0, G->HeroIndex, Reaper);
			GSkillLab.Power0 = G->GetPower();
			const ArenaCore::EBuyResult R = TryBuy(0, G->HeroIndex, Reaper);
			const bool bItems = G->Items.Num() == 2 && G->Items.Contains(Reaper) && !G->Items.Contains(FArenaDatabase::ItemIndex(TEXT("warhammer")));
			Check(R == ArenaCore::EBuyResult::Ok && Price == Full - Hammer && Near(G->Gold, 2000.f - Price, 0.5f) && bItems,
				FString::Printf(TEXT("recipe: Executioner's Blade costs %d with the owned Warhammer (full %d - %d), the hammer is used up, gold %.0f"), Price, Full, Hammer, G->Gold));
			const float Gain = G->GetPower() - GSkillLab.Power0;
			const float WantGain = FArenaDatabase::Get().Items[Reaper].Power - FArenaDatabase::Get().Items[FArenaDatabase::ItemIndex(TEXT("warhammer"))].Power;
			Check(FMath::Abs(Gain - WantGain) < 0.1f, FString::Printf(TEXT("the finished item replaces its part: power +%.0f (data +%.0f)"), Gain, WantGain));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 19:
		if (T > GSkillLab.StepT + 0.6f)
		{
			if (AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC)) { APC->SetShopOpen(false); }
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 20:
		if (T > GSkillLab.StepT + 0.8f) { UIShot(TEXT("SKILL_Bar")); GSkillLab.StepT = T; ++LabStep; }
		break;
	// ---- casting the way the keyboard does it: quick with a preview, instant, with confirmation, the input buffer,
	// a key on cooldown (it used to open the aim and its description, and LMB then did not attack) --------------
	case 21:
	case 24:
	case 26:
	case 28:
		// between the casts: wait until the last one has finished its swing
		if (T > GSkillLab.StepT + 0.4f && !G->IsBusy())
		{
			AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC);
			if (!APC) { LabStep = 90; break; }
			G->ResetCooldowns();
			G->RefillVitals();
			FArenaSettings& St = FArenaSettings::Get();
			if (LabStep == 21)
			{
				GSkillLab.Sub = St.CastMode;   // restored at the end (in memory only, never saved)
				St.CastMode = 0;
				APC->PressAbility(1);
				Check(APC->AimingSlot() == 1 && G->CooldownRemaining(1) <= 0.f, TEXT("quick cast: pressing 1 shows the aim and does not cast yet"));
			}
			else if (LabStep == 24)
			{
				St.CastMode = 1;
				APC->PressAbility(2);
				Check(APC->AimingSlot() < 0 && G->CooldownRemaining(2) > 0.f, FString::Printf(TEXT("instant cast: pressing 2 casts at once (cooldown %.1f s, no aim)"), G->CooldownRemaining(2)));
			}
			else if (LabStep == 26)
			{
				St.CastMode = 2;
				APC->PressAbility(1);
				Check(APC->AimingSlot() == 1 && G->CooldownRemaining(1) <= 0.f, TEXT("cast with confirmation: the first press aims"));
			}
			else
			{
				St.CastMode = 0;
				G->SetBusyFor(0.35f);         // a moment the hero cannot act (the input buffer's case)
				APC->PressAbility(2);
				APC->ReleaseAbility(2);
				Check(APC->BufferedSlot() == 2 && G->CooldownRemaining(2) <= 0.f, TEXT("input buffer: a cast released while the hero cannot act waits instead of being lost"));
			}
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 22:
		if (T > GSkillLab.StepT + 0.3f)
		{
			AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC);
			APC->ReleaseAbility(1);
			Check(APC->AimingSlot() < 0 && G->CooldownRemaining(1) > 0.f, FString::Printf(TEXT("quick cast: releasing 1 casts it (cooldown %.1f s)"), G->CooldownRemaining(1)));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 23:
		if (T > GSkillLab.StepT + 0.3f)
		{
			AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC);
			APC->PressAbility(1);   // still on cooldown
			const bool bFlash = APC->DeniedSlot == 1 && GetWorld()->GetTimeSeconds() - APC->DeniedAt < 0.1f;
			Check(APC->AimingSlot() < 0 && bFlash, TEXT("a key on cooldown neither aims nor opens anything: the slot flashes and the reason is said, LMB still attacks"));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 25:
		++LabStep;
		break;
	case 27:
		if (T > GSkillLab.StepT + 0.3f)
		{
			AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC);
			APC->PressAbility(1);   // the same key again
			Check(APC->AimingSlot() < 0 && G->CooldownRemaining(1) > 0.f, TEXT("cast with confirmation: the same key again casts"));
			GSkillLab.StepT = T;
			++LabStep;
		}
		break;
	case 29:
		if (T > GSkillLab.StepT + 0.8f)
		{
			AArenaPlayerController* APC = Cast<AArenaPlayerController>(PC);
			Check(APC->BufferedSlot() < 0 && G->CooldownRemaining(2) > 0.f, FString::Printf(TEXT("input buffer: the waiting cast went off when the hero could act (cooldown %.1f s)"), G->CooldownRemaining(2)));
			FArenaSettings::Get().CastMode = GSkillLab.Sub;
			GSkillLab.StepT = T;
			LabStep = 90;
		}
		break;
	case 90:
		if (T > GSkillLab.StepT + 0.5f)
		{
			ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			++LabStep;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
