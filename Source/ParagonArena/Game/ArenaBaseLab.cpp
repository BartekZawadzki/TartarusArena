// -ArenaBaseLab: the MOBA sustain rules measured on live units (VR-23): recall home and what breaks it, the fountain
// (heals its team, burns an enemy hero), potions (bought only at the base, healing their amount over their time).
// Prints LAB PASS/FAIL and LAB_SUMMARY like the other labs; screenshot BASE_Recall.png (the recall bar).
#include "Game/ArenaGameMode.h"
#include "Game/ArenaPlayerController.h"
#include "Heroes/ArenaCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Heroes/ArenaSpringArm.h"
#include "Camera/CameraComponent.h"

namespace
{
	const FVector LaneSpot(-2000.f, -3500.f, 60.f);
	struct FBaseLabState
	{
		TWeakObjectPtr<AArenaCharacter> H, E;
		float StepT = 0.f, Hp0 = 0.f, HpA = -1.f, RegenRate = 0.f, ArmIn = 0.f, ArmMid = 0.f;
		int32 HeroIdx = 0;
	};
	FBaseLabState GBaseLab;

	void Teleport(AArenaCharacter* C, const FVector& At)
	{
		if (!C) { return; }
		C->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
	}
}

void AArenaGameMode::StartBaseLab()
{
	GBaseLab = FBaseLabState();
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	GBaseLab.HeroIdx = FMath::Max(0, Defs.IndexOfByPredicate([](const FArenaHeroDef& D) { return D.Id == TEXT("Sparrow"); }));
	const int32 Ei = FMath::Max(0, Defs.IndexOfByPredicate([](const FArenaHeroDef& D) { return D.Id == TEXT("Countess"); }));
	GBaseLab.H = LabSpawn(Defs[GBaseLab.HeroIdx], 0, LaneSpot, 0.f, false, false);
	GBaseLab.E = LabSpawn(Defs[Ei], 1, FVector(2500.f, 3500.f, 60.f), 180.f, false, false);
	if (AArenaCharacter* H = GBaseLab.H.Get())
	{
		H->HeroIndex = GBaseLab.HeroIdx;
		Heroes.Add(H);
		PlayerHeroIndex = GBaseLab.HeroIdx;
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->Possess(H); PC->SetControlRotation(FRotator(-12.f, 180.f, 0.f)); }
	}
	if (GBaseLab.E.IsValid()) { GBaseLab.E->HeroIndex = Ei; GBaseLab.E->ApplyStun(600.f); }
	bFountainsOn = true;
	Phase = EArenaPhase::Playing;
	LabStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=baselab_start"));
}

void AArenaGameMode::TickBaseLab(float Now)
{
	const float T = Now - LabStart;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		UE_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	AArenaCharacter* H = GBaseLab.H.Get();
	AArenaCharacter* E = GBaseLab.E.Get();
	if ((!H || !E) && LabStep < 90) { Check(false, TEXT("base lab units exist")); LabStep = 90; GBaseLab.StepT = T; }
	const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
	const float BaseR = Ru.BaseRadius * 100.f;
	auto Dist = [this](const AArenaCharacter* C) { return FVector::Dist2D(C->GetActorLocation(), TeamBase(C->GetTeam())); };

	switch (LabStep)
	{
	// ---- recall: home after RecallSeconds -----------------------------------------------------------------------
	case 0:
		if (T > 2.f)
		{
			Check(H->StartRecall() && H->IsRecalling(), FString::Printf(TEXT("recall starts on the lane (%.0f m from the base)"), Dist(H) / 100.f));
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 1:
		if (T > GBaseLab.StepT + 3.f) { UIShot(TEXT("BASE_Recall")); ++LabStep; }
		break;
	case 2:
		if (T > GBaseLab.StepT + Ru.RecallSeconds + 0.3f)
		{
			Check(!H->IsRecalling() && Dist(H) < BaseR && H->IsInOwnBase(), FString::Printf(TEXT("after %.0f s the hero is home: %.0f m from the base centre (base %.0f m)"), Ru.RecallSeconds, Dist(H) / 100.f, Ru.BaseRadius));
			Teleport(H, LaneSpot);
			Check(H->StartRecall(), TEXT("a second recall starts"));
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	// ---- a blow breaks a recall ------------------------------------------------------------------------------------
	case 3:
		if (T > GBaseLab.StepT + 2.f)
		{
			H->ReceiveDamage(20.f, E, false);
			Check(!H->IsRecalling(), TEXT("damage breaks the recall"));
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 4:
		if (T > GBaseLab.StepT + Ru.RecallSeconds)
		{
			Check(Dist(H) > 2500.f, FString::Printf(TEXT("the broken recall leaves the hero on the lane (%.0f m out)"), Dist(H) / 100.f));
			// the fountain heals: at 30 % health in the base
			Teleport(H, BaseSpot(0));
			for (int32 Guard = 0; Guard < 40 && H->HealthPct() > 0.32f; ++Guard) { H->ReceiveDamage(H->GetMaxHealth() * 0.05f, nullptr, false); }
			GBaseLab.Hp0 = H->HealthPct();
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 5:
		if (T > GBaseLab.StepT + 2.f)
		{
			const float Gain = H->HealthPct() - GBaseLab.Hp0;
			Check(Gain >= Ru.FountainHealPct * 2.f - 0.03f, FString::Printf(TEXT("the fountain heals %.0f%% in 2 s (%.0f%%/s by the rules)"), Gain * 100.f, Ru.FountainHealPct * 100.f));
			// an enemy hero at the fountain burns
			E->RefillVitals();
			Teleport(E, BaseSpot(0) + FVector(0.f, 300.f, 0.f));
			GBaseLab.Hp0 = E->HealthPct();
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 6:
		if (T > GBaseLab.StepT + 2.f)
		{
			const float Loss = GBaseLab.Hp0 - E->HealthPct();
			const bool bRecap = E->RecentDamage.Num() > 0 && E->RecentDamage.Last().Ability == TEXT("Fountain");
			Check(Loss > Ru.FountainDamagePct * 2.f - 0.14f && Loss < Ru.FountainDamagePct * 2.f + 0.14f && bRecap,
				FString::Printf(TEXT("the enemy fountain burns an intruder: %.0f%% in 2 s (%.0f%%/s, true damage), the recap names it"), Loss * 100.f, Ru.FountainDamagePct * 100.f));
			Teleport(E, FVector(2500.f, 3500.f, 60.f));
			E->RefillVitals();
			// potions: two in the base for 2 x cost, none away from it
			Teleport(H, BaseSpot(0));
			H->RefillVitals();
			H->Gold = 200.f;
			const bool bA = TryBuyPotion(0, GBaseLab.HeroIdx, 0) == ArenaCore::EBuyResult::Ok;
			const bool bB = TryBuyPotion(0, GBaseLab.HeroIdx, 0) == ArenaCore::EBuyResult::Ok;
			Check(bA && bB && H->Potions[0] == 2 && FMath::IsNearlyEqual(H->Gold, 200.f - 2.f * Ru.PotionCost), FString::Printf(TEXT("two health potions in the base: %d, gold %.0f"), H->Potions[0], H->Gold));
			Teleport(H, LaneSpot);
			Check(TryBuyPotion(0, GBaseLab.HeroIdx, 0) == ArenaCore::EBuyResult::NotInShop, TEXT("no potion bought away from the base"));
			for (int32 Guard = 0; Guard < 40 && H->HealthPct() > 0.2f; ++Guard)   // low enough that the potion never meets full health
			{
				H->ReceiveDamage(H->GetMaxHealth() * 0.05f, nullptr, false);
			}
			UE_LOG(LogArena, Display, TEXT("LAB potion setup hp=%.0f/%.0f alive=%d"), H->GetHealth(), H->GetMaxHealth(), H->IsAlive() ? 1 : 0);
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 7:
		// wait out the 5 s after the last blow (regeneration doubles), then drink
		if (GBaseLab.HpA < 0.f && T > GBaseLab.StepT + 5.2f) { GBaseLab.HpA = H->GetHealth(); GBaseLab.RegenRate = T; }   // regeneration measured, not assumed
		if (T > GBaseLab.StepT + 7.2f)
		{
			GBaseLab.RegenRate = (H->GetHealth() - GBaseLab.HpA) / FMath::Max(0.1f, T - GBaseLab.RegenRate);
			GBaseLab.Hp0 = H->GetHealth();
			UE_LOG(LogArena, Display, TEXT("LAB potion drink hp=%.0f hpA=%.0f regen=%.1f"), H->GetHealth(), GBaseLab.HpA, GBaseLab.RegenRate);
			Check(H->DrinkPotion(0) && H->Potions[0] == 1 && !H->DrinkPotion(0), TEXT("a potion is drunk (one at a time)"));
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 8:
		if (T > GBaseLab.StepT + Ru.PotionSeconds + 0.3f)
		{
			const float Regen = GBaseLab.RegenRate * (T - GBaseLab.StepT);
			const float Gain = H->GetHealth() - GBaseLab.Hp0;
			Check(FMath::Abs(Gain - Regen - Ru.PotionHeal) < 12.f, FString::Printf(TEXT("the potion heals %.0f over %.0f s (data %.0f; %.0f of measured regeneration taken off)"), Gain - Regen, Ru.PotionSeconds, Ru.PotionHeal, Regen));
			// ---- camera (VR-26): the hero with its back 1.5 m from the east base's front wall, looking away from it
			Teleport(H, FVector(4150.f, 900.f, 120.f));
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetControlRotation(FRotator(-10.f, 180.f, 0.f)); }
			GBaseLab.StepT = T;
			LabStep = 9;
		}
		break;
	case 9:
		if (T > GBaseLab.StepT + 0.8f)
		{
			const UArenaSpringArm* Arm = Cast<UArenaSpringArm>(H->SpringArm);
			const FVector Eye = H->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			const FVector Cam = H->Camera->GetComponentLocation();
			FHitResult Hit;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(LabCam), false, H);
			const bool bThrough = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Cam, ECC_Camera, Q);
			GBaseLab.ArmIn = Arm ? Arm->CurrentLength : -1.f;
			Check(Arm && Arm->CurrentLength < H->SpringArm->TargetArmLength - 100.f && !bThrough,
				FString::Printf(TEXT("a wall behind the hero pulls the camera in (arm %.0f of %.0f cm) and the view is not through it (blocked=%d)"), GBaseLab.ArmIn, H->SpringArm->TargetArmLength, bThrough ? 1 : 0));
			// pulled in, it stays at the shoulders' height (it used to slide down the arm to the hips)
			const float Lift = Cam.Z - H->GetActorLocation().Z;
			Check(Lift > 60.f, FString::Printf(TEXT("the camera pulled in by the wall stays above the shoulders (%.0f cm above the hero's centre)"), Lift));
			UIShot(TEXT("BASE_CamWall"));
			Teleport(H, FVector(3300.f, 1000.f, 120.f));   // open ground, same facing
			GBaseLab.StepT = T;
			++LabStep;
		}
		break;
	case 10:
		if (GBaseLab.ArmMid <= 0.f && T > GBaseLab.StepT + 0.12f) { if (const UArenaSpringArm* Arm = Cast<UArenaSpringArm>(H->SpringArm)) { GBaseLab.ArmMid = Arm->CurrentLength; } }
		if (T > GBaseLab.StepT + 1.8f)
		{
			const UArenaSpringArm* Arm = Cast<UArenaSpringArm>(H->SpringArm);
			const float Full = H->SpringArm->TargetArmLength;
			Check(Arm && GBaseLab.ArmMid > GBaseLab.ArmIn + 5.f && GBaseLab.ArmMid < Full - 30.f && Arm->CurrentLength > Full - 10.f,
				FString::Printf(TEXT("clear of the wall the camera glides back out: %.0f -> %.0f after 0.12 s -> %.0f after 1.8 s (of %.0f)"), GBaseLab.ArmIn, GBaseLab.ArmMid, Arm ? Arm->CurrentLength : -1.f, Full));
			GBaseLab.StepT = T;
			LabStep = 90;
		}
		break;
	case 90:
		if (T > GBaseLab.StepT + 0.5f)
		{
			UE_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			++LabStep;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
