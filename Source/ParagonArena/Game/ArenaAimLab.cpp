// -ArenaAimLab: the player's aim assistance and hit confirmation, measured through the real player controller.
//   shots     Sparrow, possessed by the player controller, keeps the crosshair on a hero that strafes 12 m away the
//             way a player does (on its body as it is now, no lead, a small hand error) and fires 20 basic attacks
//             with arena.AimAssist 0, then 20 with 1: the hit rates are compared; the shot lane follows the target
//   feedback  one hit marker per landed player hit, none for another unit's hit; the body flashes and stops
//   melee     Greystone: a swing at an enemy off to the side turns the body to it; an enemy 35 cm past the reach is
//             closed with a step (lands only with assistance); a swing at an enemy running across in front lands
// Prints LAB PASS/FAIL lines and LAB_SUMMARY like -ArenaAnimLab; screenshots AIM_*.png.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaPlayerController.h"
#include "Heroes/ArenaCharacter.h"
#include "Arena/ArenaIndicator.h"
#include "Abilities/ArenaAbility.h"
#include "UI/ArenaHUD.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	const FVector AimFrom(-5700.f, -700.f, 400.f);      // plateau A (top z 300): the shooter faces +Y
	const FVector AimSpot(-5700.f, 500.f, 400.f);         // 12 m ahead: the target strafes along X around here
	const int32 ShotsPerPhase = 20;

	struct FShotPhase { int32 Shots = 0, Hits = 0, Markers0 = 0, Picked = 0, Ticks = 0; float NextShot = 0.f, LastShot = 0.f, ErrYaw = 0.f, ErrPitch = 0.f; };

	// the lab's state (one struct: plain globals named Target or Dummy clash with engine template parameters)
	struct FAimLabState
	{
		TWeakObjectPtr<AArenaCharacter> Shooter, Target, Bystander, Swinger, Dummy;
		TWeakObjectPtr<AArenaIndicator> Lane;
		FRandomStream Rng = FRandomStream(7);
		FShotPhase Phases[2];
		float StepT = 0.f, LastHP = 0.f, FlipAt = 0.f, StrafeSign = 1.f, LaneWorst = 0.f, FacingWorst = 0.f;
		int32 LaneSamples = 0, MarkersAtStart = 0, PlayerHits = 0, Hits0 = 0, Markers0 = 0;
		bool bFlashSeen = false, bOffHit = false, bHitShot = false;
		FVector SwingerStart = FVector::ZeroVector;
	};
	FAimLabState GAimLab;

	void SetAssist(int32 On)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("arena.AimAssist"))) { V->Set(On, ECVF_SetByCode); }
	}

	// a landed hit is read off the target's health (independent of the HUD's own count), then the health is refilled
	bool HitLanded(AArenaCharacter* T)
	{
		if (!T) { return false; }
		if (T->GetHealth() < GAimLab.LastHP - 0.5f)
		{
			T->RefillVitals();
			GAimLab.LastHP = T->GetHealth();
			return true;
		}
		GAimLab.LastHP = T->GetHealth();
		return false;
	}

	// the player's hand: the crosshair on the target's body (where it is now) with a small error
	void Track(APlayerController* PC, const AArenaCharacter* T, float ErrYaw, float ErrPitch)
	{
		if (!PC || !PC->PlayerCameraManager || !T) { return; }
		FRotator R = (T->GetActorLocation() + FVector(0, 0, 15.f) - PC->PlayerCameraManager->GetCameraLocation()).Rotation();
		R.Yaw += ErrYaw;
		R.Pitch += ErrPitch;
		PC->SetControlRotation(R);
	}

	// strafing like a player dodging shots: left and right, turning at random moments, inside a 12 m strip
	void Strafe(AArenaCharacter* T, float T0)
	{
		if (!T) { return; }
		const float X = T->GetActorLocation().X - AimSpot.X;
		if (T0 >= GAimLab.FlipAt || (X > 550.f && GAimLab.StrafeSign > 0.f) || (X < -550.f && GAimLab.StrafeSign < 0.f))
		{
			GAimLab.StrafeSign = -GAimLab.StrafeSign;
			GAimLab.FlipAt = T0 + GAimLab.Rng.FRandRange(0.5f, 1.4f);
		}
		T->AddMovementInput(FVector(GAimLab.StrafeSign, 0.f, 0.f), 1.f);
	}

	float YawError(const AArenaCharacter* From, const AArenaCharacter* To)
	{
		const float Want = (To->GetActorLocation() - From->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw;
		return FMath::Abs(FRotator::NormalizeAxis(From->GetActorRotation().Yaw - Want));
	}
}

void AArenaGameMode::StartAimLab()
{
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	auto HeroDef = [&Defs](const TCHAR* Id) -> const FArenaHeroDef& { const FArenaHeroDef* D = Defs.FindByPredicate([Id](const FArenaHeroDef& H) { return H.Id == Id; }); return D ? *D : Defs[0]; };
	GAimLab.Rng.Initialize(Seed + 1);
	GAimLab.Shooter = LabSpawn(HeroDef(TEXT("Sparrow")), 0, AimFrom, 90.f, false, false);
	GAimLab.Target = LabSpawn(HeroDef(TEXT("Kwang")), 1, AimSpot, 270.f, false, false);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (GAimLab.Shooter.IsValid()) { PC->Possess(GAimLab.Shooter.Get()); PC->SetControlRotation(FRotator(-8.f, 90.f, 0.f)); }
	}
	GAimLab.LastHP = GAimLab.Target.IsValid() ? GAimLab.Target->GetHealth() : 0.f;
	GAimLab.MarkersAtStart = AArenaHUD::HitMarkersShown;
	Phase = EArenaPhase::Playing;
	LabStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=aimlab_start shooter=%d target=%d"), GAimLab.Shooter.IsValid() ? 1 : 0, GAimLab.Target.IsValid() ? 1 : 0);
}

void AArenaGameMode::TickAimLab(float Now)
{
	const float T = Now - LabStart;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		UE_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	auto HeroDef = [&Defs](const TCHAR* Id) -> const FArenaHeroDef& { const FArenaHeroDef* D = Defs.FindByPredicate([Id](const FArenaHeroDef& H) { return H.Id == Id; }); return D ? *D : Defs[0]; };

	// ---- shots: assistance off (phase 0), then on (phase 1) -------------------------------------------------------
	if (LabStep == 0)
	{
		if (T > 2.f && GAimLab.Shooter.IsValid() && GAimLab.Target.IsValid())
		{
			SetAssist(0);
			GAimLab.Phases[0] = FShotPhase();
			GAimLab.Phases[0].Markers0 = AArenaHUD::HitMarkersShown;
			GAimLab.Phases[0].NextShot = T + 0.5f;
			GAimLab.FlipAt = T + 0.8f;
			++LabStep;
		}
		return;
	}
	if (LabStep == 1 || LabStep == 2)
	{
		const int32 Idx = LabStep - 1;
		FShotPhase& P = GAimLab.Phases[Idx];
		AArenaCharacter* S = GAimLab.Shooter.Get();
		AArenaCharacter* Tg = GAimLab.Target.Get();
		if (!S || !Tg) { Check(false, TEXT("aim lab units exist")); LabStep = 20; return; }
		Strafe(Tg, T);
		Track(PC, Tg, P.ErrYaw, P.ErrPitch);
		++P.Ticks;
		P.Picked += S->AimTarget.Get() == Tg ? 1 : 0;
		if (HitLanded(Tg))
		{
			++P.Hits; ++GAimLab.PlayerHits;
			// the body flashes on the same frame the hit lands
			GAimLab.bFlashSeen |= Tg->GetMesh()->GetOverlayMaterial() != nullptr;
			UE_LOG(LogArena, Display, TEXT("LAB aim hit phase=%d t=%.2f shots=%d hits=%d"), Idx, T, P.Shots, P.Hits);
			if (Idx == 1 && !GAimLab.bHitShot && P.Shots >= 6) { GAimLab.bHitShot = true; UIShot(TEXT("AIM_Hit")); }
		}
		// the shot lane (the basic attack's indicator, drawn here for the check) points where the shot will go
		// (compared on smooth frames: on a long frame the target moves between the indicator's tick and this one)
		if (AArenaIndicator* I = GAimLab.Lane.Get())
		{
			if (I->IsShown() && S->AimTarget.Get() == Tg && T > P.NextShot - 0.4f && GetWorld()->GetDeltaSeconds() < 0.03f)
			{
				const FVector Want = (ArenaAbilityHelpers::LeadPoint(S, S->GetDef().Abilities[0], Tg) - S->GetActorLocation()).GetSafeNormal2D();
				const FVector Got = (I->TargetPoint() - S->GetActorLocation()).GetSafeNormal2D();
				const float Err = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector::DotProduct(Want, Got), -1.f, 1.f)));
				if (Err > GAimLab.LaneWorst)
				{
					UE_LOG(LogArena, Display, TEXT("LAB lane t=%.2f dt=%.3f err=%.1f want=%.1f got=%.1f vel=%.0f"), T, GetWorld()->GetDeltaSeconds(), Err, Want.Rotation().Yaw, Got.Rotation().Yaw, Tg->GetVelocity().X);
				}
				GAimLab.LaneWorst = FMath::Max(GAimLab.LaneWorst, Err);
				++GAimLab.LaneSamples;
			}
		}
		if (P.Shots < ShotsPerPhase && T >= P.NextShot && S->CanCastSlot(0))
		{
			const int32 Before = S->SlotCasts[0];
			S->TryCast(0);
			if (S->SlotCasts[0] > Before)
			{
				++P.Shots;
				P.LastShot = T;
				P.NextShot = T + GAimLab.Rng.FRandRange(0.6f, 0.9f);
				P.ErrYaw = GAimLab.Rng.FRandRange(-1.f, 1.f);
				P.ErrPitch = GAimLab.Rng.FRandRange(-0.5f, 0.5f);
				if (Idx == 1 && P.Shots == 4) { UIShot(TEXT("AIM_Track")); }
			}
		}
		if (P.Shots >= ShotsPerPhase && T > P.LastShot + 0.8f)
		{
			const int32 Markers = AArenaHUD::HitMarkersShown - P.Markers0;
			const float Rate = (float)P.Hits / FMath::Max(1, P.Shots);
			UE_LOG(LogArena, Display, TEXT("LAB aim phase=%d assist=%d shots=%d hits=%d rate=%.2f markers=%d picked=%.2f"), Idx, Idx, P.Shots, P.Hits, Rate, Markers, (float)P.Picked / FMath::Max(1, P.Ticks));
			Check(Markers == P.Hits, FString::Printf(TEXT("one hit marker per landed shot (assistance %s): %d markers, %d hits"), Idx ? TEXT("on") : TEXT("off"), Markers, P.Hits));
			if (Idx == 0)
			{
				Check(P.Picked == 0, FString::Printf(TEXT("with arena.AimAssist 0 no target is picked: %d of %d frames"), P.Picked, P.Ticks));
				SetAssist(1);
				GAimLab.Phases[1] = FShotPhase();
				GAimLab.Phases[1].Markers0 = AArenaHUD::HitMarkersShown;
				GAimLab.Phases[1].NextShot = T + 0.8f;
				FActorSpawnParameters SP;
				SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				GAimLab.Lane = GetWorld()->SpawnActor<AArenaIndicator>(AArenaIndicator::StaticClass(), S->GetActorLocation(), FRotator::ZeroRotator, SP);
				if (GAimLab.Lane.IsValid()) { GAimLab.Lane->Show(S, 0); }
			}
			else
			{
				const float Off = (float)GAimLab.Phases[0].Hits / FMath::Max(1, GAimLab.Phases[0].Shots);
				Check(Rate >= 0.7f && Rate >= Off + 0.25f, FString::Printf(TEXT("shots at a strafing hero 12 m away land more with assistance: %.0f%% (%d/%d) vs %.0f%% without"), Rate * 100.f, P.Hits, P.Shots, Off * 100.f));
				Check((float)P.Picked / FMath::Max(1, P.Ticks) >= 0.9f, FString::Printf(TEXT("the crosshair on the target picks it (red crosshair, brackets): %d of %d frames"), P.Picked, P.Ticks));
				Check(GAimLab.LaneSamples > 20 && GAimLab.LaneWorst <= 4.f, FString::Printf(TEXT("the shot lane turns to where the shot will go: worst %.1f deg over %d frames"), GAimLab.LaneWorst, GAimLab.LaneSamples));
				Check(GAimLab.bFlashSeen, TEXT("a hit body flashes (overlay material on the hit frame)"));
				if (GAimLab.Lane.IsValid()) { GAimLab.Lane->Destroy(); }
			}
			GAimLab.StepT = T;
			++LabStep;
		}
		return;
	}

	switch (LabStep)
	{
	case 3:
		// the flash is gone a moment after the last hit; another unit's hit shows no marker on the player's HUD
		HitLanded(GAimLab.Target.Get());
		if (T > GAimLab.StepT + 0.6f && GAimLab.Target.IsValid() && GAimLab.Shooter.IsValid())
		{
			Check(GAimLab.Target->GetMesh()->GetOverlayMaterial() == nullptr, TEXT("the hit flash ends (no overlay 0.6 s after the last hit)"));
			GAimLab.Bystander = LabSpawn(HeroDef(TEXT("Sparrow")), 0, AimFrom + FVector(300.f, 0.f, 0.f), 90.f, false, false);
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 4:
		HitLanded(GAimLab.Target.Get());
		if (T > GAimLab.StepT + 0.5f && GAimLab.Bystander.IsValid() && GAimLab.Target.IsValid())
		{
			GAimLab.Hits0 = 0; GAimLab.Markers0 = AArenaHUD::HitMarkersShown;
			GAimLab.Bystander->AimPoint = GAimLab.Target->GetActorLocation() + FVector(0, 0, 15.f);
			GAimLab.Bystander->TryCast(0);
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 5:
		GAimLab.Hits0 += HitLanded(GAimLab.Target.Get()) ? 1 : 0;
		if (T > GAimLab.StepT + 0.8f)
		{
			Check(GAimLab.Hits0 == 1 && AArenaHUD::HitMarkersShown == GAimLab.Markers0, FString::Printf(TEXT("another unit's hit shows no hit marker: %d hit, %d markers"), GAimLab.Hits0, AArenaHUD::HitMarkersShown - GAimLab.Markers0));
			// ---- melee: Greystone taken over by the player controller -------------------------------------------
			if (PC) { PC->UnPossess(); }
			for (TWeakObjectPtr<AArenaCharacter>* W : { &GAimLab.Shooter, &GAimLab.Target, &GAimLab.Bystander }) { if (W->IsValid()) { (*W)->Destroy(); } }
			GAimLab.SwingerStart = AimFrom;
			GAimLab.Swinger = LabSpawn(HeroDef(TEXT("Greystone")), 0, GAimLab.SwingerStart, 90.f, false, false);
			// an enemy 55 degrees to the right, well inside the reach
			const FVector Side = FRotator(0.f, 90.f + 55.f, 0.f).Vector();
			GAimLab.Dummy = LabSpawn(HeroDef(TEXT("Countess")), 1, GAimLab.SwingerStart + Side * 200.f, 270.f, false, false);
			if (GAimLab.Dummy.IsValid()) { GAimLab.Dummy->ApplyStun(60.f); GAimLab.LastHP = GAimLab.Dummy->GetHealth(); }
			if (PC && GAimLab.Swinger.IsValid()) { PC->Possess(GAimLab.Swinger.Get()); PC->SetControlRotation(FRotator(-12.f, 90.f, 0.f)); }
			SetAssist(1);
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 6:
		HitLanded(GAimLab.Dummy.Get());
		if (T > GAimLab.StepT + 1.f && GAimLab.Swinger.IsValid() && GAimLab.Dummy.IsValid())
		{
			Check(GAimLab.Swinger->AimTarget.Get() == GAimLab.Dummy.Get(), TEXT("a melee hero picks the enemy 55 degrees off the crosshair"));
			GAimLab.Swinger->TryCast(0);
			GAimLab.FacingWorst = YawError(GAimLab.Swinger.Get(), GAimLab.Dummy.Get());
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 7:
	{
		const bool bHit = HitLanded(GAimLab.Dummy.Get());
		GAimLab.bOffHit |= bHit;
		if (T > GAimLab.StepT + 0.12f && T < GAimLab.StepT + 0.2f && GAimLab.Swinger.IsValid() && GAimLab.Dummy.IsValid()) { GAimLab.FacingWorst = FMath::Max(GAimLab.FacingWorst, YawError(GAimLab.Swinger.Get(), GAimLab.Dummy.Get())); }
		if (bHit && GAimLab.Swinger.IsValid() && GAimLab.Dummy.IsValid()) { GAimLab.FacingWorst = FMath::Max(GAimLab.FacingWorst, YawError(GAimLab.Swinger.Get(), GAimLab.Dummy.Get())); UIShot(TEXT("AIM_Melee")); }
		if (T > GAimLab.StepT + 0.8f)
		{
			Check(GAimLab.bOffHit && GAimLab.FacingWorst <= 10.f, FString::Printf(TEXT("the swing turns the body to it and lands: hit=%d, facing off by %.1f deg during the swing"), GAimLab.bOffHit ? 1 : 0, GAimLab.FacingWorst));
			// next: straight ahead, 35 cm past the blade's reach (edge to edge)
			if (GAimLab.Swinger.IsValid() && GAimLab.Dummy.IsValid())
			{
				GAimLab.Swinger->SetActorLocation(GAimLab.SwingerStart, false, nullptr, ETeleportType::ResetPhysics);
				const float Reach = GAimLab.Swinger->GetDef().Abilities[0].Range * 100.f;
				GAimLab.Dummy->SetActorLocation(GAimLab.SwingerStart + FVector(0.f, Reach + 35.f + GAimLab.Dummy->GetCapsuleComponent()->GetScaledCapsuleRadius(), 0.f), false, nullptr, ETeleportType::ResetPhysics);
			}
			SetAssist(0);
			GAimLab.bOffHit = false;
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	}
	case 8:
		HitLanded(GAimLab.Dummy.Get());
		if (T > GAimLab.StepT + 1.2f && GAimLab.Swinger.IsValid()) { GAimLab.Swinger->TryCast(0); GAimLab.StepT = T; ++LabStep; }
		break;
	case 9:
		GAimLab.bOffHit |= HitLanded(GAimLab.Dummy.Get());
		if (T > GAimLab.StepT + 0.8f && GAimLab.Swinger.IsValid())
		{
			GAimLab.Swinger->SetActorLocation(GAimLab.SwingerStart, false, nullptr, ETeleportType::ResetPhysics);
			SetAssist(1);
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 10:
		HitLanded(GAimLab.Dummy.Get());
		if (T > GAimLab.StepT + 1.f && GAimLab.Swinger.IsValid() && GAimLab.Dummy.IsValid())
		{
			Check(GAimLab.Swinger->AimTarget.Get() == GAimLab.Dummy.Get(), TEXT("an enemy just past the reach is picked"));
			GAimLab.Swinger->TryCast(0);
			GAimLab.bHitShot = false;
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 11:
		GAimLab.bHitShot |= HitLanded(GAimLab.Dummy.Get());
		if (T > GAimLab.StepT + 0.8f && GAimLab.Swinger.IsValid() && GAimLab.Dummy.IsValid())
		{
			const float Step = FVector::Dist2D(GAimLab.Swinger->GetActorLocation(), GAimLab.SwingerStart);
			const float Gap = FVector::Dist2D(GAimLab.Swinger->GetActorLocation(), GAimLab.Dummy->GetActorLocation()) - GAimLab.Swinger->GetCapsuleComponent()->GetScaledCapsuleRadius() - GAimLab.Dummy->GetCapsuleComponent()->GetScaledCapsuleRadius();
			Check(GAimLab.bHitShot && !GAimLab.bOffHit && Gap >= -2.f, FString::Printf(TEXT("35 cm past the reach: without assistance the swing misses (hit=%d), with it a %.0f cm step lands it (hit=%d), bodies %.0f cm apart"), GAimLab.bOffHit ? 1 : 0, Step, GAimLab.bHitShot ? 1 : 0, Gap));
			// next: an enemy running across in front of the swinger, 1.4 m ahead, from its left to its right
			GAimLab.Dummy->Destroy();
			GAimLab.Swinger->SetActorLocation(GAimLab.SwingerStart, false, nullptr, ETeleportType::ResetPhysics);
			GAimLab.Dummy = LabSpawn(HeroDef(TEXT("Kwang")), 1, GAimLab.SwingerStart + FVector(420.f, 140.f, 0.f), 180.f, false, false);
			GAimLab.LastHP = GAimLab.Dummy.IsValid() ? GAimLab.Dummy->GetHealth() : 0.f;
			SetAssist(0);
			GAimLab.bOffHit = false; GAimLab.bHitShot = false;
			GAimLab.StepT = T;
			++LabStep;
		}
		break;
	case 12: case 14:
	{
		AArenaCharacter* R = GAimLab.Dummy.Get();
		AArenaCharacter* G = GAimLab.Swinger.Get();
		if (!R || !G) { LabStep = 20; break; }
		const FVector To = (R->GetActorLocation() - G->GetActorLocation()) * FVector(1, 1, 0);
		if (GAimLab.StepT < 0.f || T > GAimLab.StepT) { R->AddMovementInput(FVector(-1.f, 0.f, 0.f), 1.f); }   // runs once the swing is ready
		if (LabStep == 12) { GAimLab.bOffHit |= HitLanded(R); } else { GAimLab.bHitShot |= HitLanded(R); }
		// swing when the runner passes 25 degrees right of the crosshair (it keeps running through the wind-up)
		const float Angle = FRotator::NormalizeAxis(To.Rotation().Yaw - 90.f);
		if (T > GAimLab.StepT + 0.3f && Angle >= 25.f && Angle <= 40.f && GAimLab.StepT >= 0.f)
		{
			G->TryCast(0);
			GAimLab.StepT = -T;                  // swung: wait for the result
		}
		if (GAimLab.StepT < 0.f && T > -GAimLab.StepT + 0.8f)
		{
			if (LabStep == 12)
			{
				R->SetActorLocation(GAimLab.SwingerStart + FVector(420.f, 140.f, 0.f), false, nullptr, ETeleportType::ResetPhysics);
				R->GetCharacterMovement()->StopMovementImmediately();
				G->SetActorLocation(GAimLab.SwingerStart, false, nullptr, ETeleportType::ResetPhysics);
				SetAssist(1);
				GAimLab.StepT = T + 0.8f;        // the cooldown
				LabStep = 14;
			}
			else
			{
				Check(GAimLab.bHitShot, FString::Printf(TEXT("a swing at an enemy running across in front lands: with assistance hit=%d (without %d)"), GAimLab.bHitShot ? 1 : 0, GAimLab.bOffHit ? 1 : 0));
				UIShot(TEXT("AIM_Runner"));
				GAimLab.StepT = T;
				LabStep = 16;
			}
		}
		break;
	}
	case 16:
		if (T > GAimLab.StepT + 0.5f)
		{
			const int32 Markers = AArenaHUD::HitMarkersShown - GAimLab.MarkersAtStart;
			UE_LOG(LogArena, Display, TEXT("LAB aim markers=%d player_hits_shots=%d"), Markers, GAimLab.PlayerHits);
			LabStep = 20;
		}
		break;
	case 20:
		UE_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
		SetAssist(1);
		++LabStep;
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		break;
	default: break;
	}
}
