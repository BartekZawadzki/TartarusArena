// -ArenaFxLab: every ability of every hero cast at a dummy, one at a time, in front of a fixed side camera.
// A screenshot at the moment of impact (FX_<hero>_<slot>.png: does it look like the ability, is its effect there, is
// it the right size) and the measured result (FXLAB lines: damage dealt, knockback distance); a damaging ability that
// misses a dummy standing in its aim is a LAB FAIL. Then terrain physics: a shot up and a shot down the base ramp at
// a dummy on the slope must land; a knockback towards a wall must stop the body in front of it. LAB_SUMMARY at the end.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaEvidence.h"
#include "Heroes/ArenaCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	const FVector FxCaster(-5700.f, -350.f, 400.f);   // plateau A (top z 300), the caster faces +X
	const FVector FxDummy(-4950.f, -350.f, 400.f);    // 7.5 m in front of the caster (shots, areas)

	/** Where the dummy stands for an ability: inside a swing's reach, at a dash's landing, else 7.5 m ahead. */
	FVector DummySpot(const FArenaAbilityDef& Ab)
	{
		float Dist = FVector::Dist2D(FxCaster, FxDummy);
		if (Ab.Archetype == EArenaArchetype::Melee) { Dist = FMath::Max(150.f, Ab.Range * 100.f * 0.75f); }
		else if (Ab.Archetype == EArenaArchetype::Dash && Ab.Distance > 0.f) { Dist = FMath::Min(Dist, Ab.Distance * 100.f * 0.9f); }
		return FxCaster + FVector(Dist, 0.f, 0.f);
	}
	struct FFxLabState
	{
		TWeakObjectPtr<AArenaCharacter> Caster, Dummy;
		int32 Hero = 0, Slot = 0, Phase = 0, Test = 0;
		TWeakObjectPtr<AActor> Wall;
		float StepT = 0.f, Hp0 = 0.f, ShotAt = 0.f;
		FVector DummyAt0 = FVector::ZeroVector;
		bool bCast = false;
	};
	FFxLabState GFxLab;

	void FxPlace(AArenaCharacter* C, const FVector& At, float Yaw)
	{
		if (!C) { return; }
		C->SetActorLocationAndRotation(At, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
	}
}

void AArenaGameMode::StartFxLab()
{
	GFxLab = FFxLabState();
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	const int32 Di = FMath::Max(0, Defs.IndexOfByPredicate([](const FArenaHeroDef& D) { return D.Id == TEXT("Greystone"); }));
	GFxLab.Dummy = LabSpawn(Defs[Di], 1, FxDummy, 180.f, false, false);
	if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>((FxCaster + FxDummy) * 0.5f + FVector(0.f, -1350.f, 650.f), FRotator(-24.f, 90.f, 0.f)))
	{
		Cam->GetCameraComponent()->SetFieldOfView(75.f);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(Cam); }
	}
	Phase = EArenaPhase::Playing;
	LabStart = GetWorld()->GetTimeSeconds();
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=fxlab_start"));
}

void AArenaGameMode::TickFxLab(float Now)
{
	const float T = Now - LabStart;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		ARENA_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	if (T < 2.f) { return; }   // the level, the dummy and the camera settle
	if (GFxLab.Hero >= Defs.Num())
	{
		// ---- terrain physics -------------------------------------------------------------------------------
		auto HeroDef = [&Defs](const TCHAR* Id) -> const FArenaHeroDef& { const int32 I = Defs.IndexOfByPredicate([Id](const FArenaHeroDef& D) { return D.Id == FName(Id); }); return Defs[FMath::Max(0, I)]; };
		AArenaCharacter* Dm = GFxLab.Dummy.Get();
		// base A's mid ramp runs from its top (-4400, 0, 300) down to (-3200, 0, 0)
		// both on the y = -250 line: the other half of the ramp has a physics block (cover) that rightly stops a shot
		const FVector RampFoot(-2950.f, -250.f, 100.f), RampMid(-4000.f, -250.f, 300.f), RampTop(-4250.f, -250.f, 400.f), FootFar(-3000.f, -250.f, 100.f);
		switch (GFxLab.Test)
		{
		case 0: case 2:
		{
			// a basic shot up the ramp (test 0) and down it (test 2), aimed at the dummy's body
			if (GFxLab.Caster.IsValid()) { GFxLab.Caster->Destroy(); }
			const bool bUp = GFxLab.Test == 0;
			GFxLab.Caster = LabSpawn(HeroDef(TEXT("Sparrow")), 0, bUp ? RampFoot : RampTop, bUp ? 180.f : 0.f, false, false);
			FxPlace(Dm, bUp ? RampMid : FootFar, 0.f);
			GFxLab.StepT = T;
			++GFxLab.Test;
			break;
		}
		case 1: case 3:
		{
			AArenaCharacter* C = GFxLab.Caster.Get();
			if (!C || !Dm) { ++GFxLab.Test; break; }
			if (GFxLab.Phase != 20 && T > GFxLab.StepT + 1.0f)
			{
				Dm->RefillVitals();
				Dm->ApplyStun(1.f);
				GFxLab.Hp0 = Dm->GetHealth();
				C->ResetCooldowns();
				C->AimPoint = Dm->GetActorLocation();
				GFxLab.bCast = C->TryCast(0);
				GFxLab.Phase = 20;
				GFxLab.StepT = T;
			}
			else if (GFxLab.Phase == 20 && T > GFxLab.StepT + 1.2f)
			{
				const bool bUp = GFxLab.Test == 1;
				if (bUp) { UIShot(TEXT("FX_RampShot")); }
				Check(GFxLab.bCast && Dm->GetHealth() < GFxLab.Hp0, FString::Printf(TEXT("a shot %s the ramp at a dummy on the slope lands (%.0f damage, height difference %.0f cm)"), bUp ? TEXT("up") : TEXT("down"),
					GFxLab.Hp0 - Dm->GetHealth(), Dm->GetActorLocation().Z - C->GetActorLocation().Z));
				GFxLab.Phase = 0;
				++GFxLab.Test;
			}
			break;
		}
		case 4:
		{
			// a knockback towards a wall: the dummy stands 1.2 m in front of it, the blow throws 4 m
			if (GFxLab.Caster.IsValid()) { GFxLab.Caster->Destroy(); }
			GFxLab.Caster = LabSpawn(HeroDef(TEXT("Greystone")), 0, FxCaster, 0.f, false, false);
			GFxLab.Wall = LabWall(FxCaster + FVector(200.f + 120.f + 40.f + 30.f, 0.f, 50.f), FVector(60.f, 600.f, 300.f));   // face at +360 cm
			FxPlace(Dm, FxCaster + FVector(200.f, 0.f, 0.f), 180.f);
			GFxLab.StepT = T;
			++GFxLab.Test;
			break;
		}
		case 5:
		{
			AArenaCharacter* C = GFxLab.Caster.Get();
			if (!C || !Dm) { ++GFxLab.Test; break; }
			if (GFxLab.Phase != 30 && T > GFxLab.StepT + 1.0f)
			{
				Dm->RefillVitals();
				C->ResetCooldowns();
				C->AimPoint = Dm->GetActorLocation();
				GFxLab.DummyAt0 = Dm->GetActorLocation();
				GFxLab.bCast = C->TryCast(1);   // Cleave: a 4 m knockback
				GFxLab.Phase = 30;
				GFxLab.StepT = T;
			}
			else if (GFxLab.Phase == 30 && T > GFxLab.StepT + 1.6f)
			{
				UIShot(TEXT("FX_KnockWall"));
				const float Face = FxCaster.X + 360.f;
				const float Front = Dm->GetActorLocation().X + Dm->GetCapsuleComponent()->GetScaledCapsuleRadius();
				Check(GFxLab.bCast && Dm->GetActorLocation().X > GFxLab.DummyAt0.X + 40.f && Front <= Face + 3.f && Dm->GetActorLocation().Z < GFxLab.DummyAt0.Z + 60.f,
					FString::Printf(TEXT("a knockback into a wall stops the body at the wall: moved %.0f cm, body front %.0f cm from the wall face (<= 0 = not inside)"),
						Dm->GetActorLocation().X - GFxLab.DummyAt0.X, Front - Face));
				GFxLab.Phase = 0;
				++GFxLab.Test;
			}
			break;
		}
		default:
			if (GFxLab.Phase != 99)
			{
				GFxLab.Phase = 99;
				ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
				UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
			}
			break;
		}
		return;
	}
	AArenaCharacter* D = GFxLab.Dummy.Get();
	if (!D) { Check(false, TEXT("fx lab dummy exists")); GFxLab.Hero = Defs.Num(); return; }
	switch (GFxLab.Phase)
	{
	case 0:
	{
		// a fresh caster for this hero (the previous one is removed)
		if (GFxLab.Caster.IsValid()) { GFxLab.Caster->Destroy(); }
		GFxLab.Caster = LabSpawn(Defs[GFxLab.Hero], 0, FxCaster, 0.f, false, false);
		GFxLab.Slot = 0;
		GFxLab.StepT = T;
		GFxLab.Phase = 1;
		break;
	}
	case 1:
	{
		// set up one cast: both in place, full vitals, no cooldowns, the dummy stunned in place
		AArenaCharacter* C = GFxLab.Caster.Get();
		if (!C || T < GFxLab.StepT + 1.2f) { break; }
		FxPlace(C, FxCaster, 0.f);
		FxPlace(D, DummySpot(C->Ability(GFxLab.Slot)), 180.f);
		C->RefillVitals();
		C->ResetCooldowns();
		D->RefillVitals();
		D->ApplyStun(0.6f);
		GFxLab.DummyAt0 = D->GetActorLocation();
		GFxLab.Hp0 = D->GetHealth();
		C->AimPoint = D->GetActorLocation();
		const FArenaAbilityDef Ab = C->Ability(GFxLab.Slot);
		GFxLab.bCast = C->TryCast(GFxLab.Slot);
		// the moment of impact: the ability's delay plus the flight of a shot
		const float Flight = Ab.Archetype == EArenaArchetype::Projectile && Ab.Speed > 0.f ? FVector::Dist(FxCaster, D->GetActorLocation()) / (Ab.Speed * 100.f) : 0.f;
		GFxLab.ShotAt = T + FMath::Max(0.3f, Ab.Delay + Flight + 0.22f);   // the effect a moment after it starts
		GFxLab.StepT = T;
		GFxLab.Phase = 2;
		break;
	}
	case 2:
	{
		if (T < GFxLab.ShotAt) { break; }
		UIShot(*FString::Printf(TEXT("FX_%s_%d"), *Defs[GFxLab.Hero].Id.ToString(), GFxLab.Slot));
		GFxLab.Phase = 3;
		break;
	}
	case 3:
	{
		AArenaCharacter* C = GFxLab.Caster.Get();
		if (!C || T < GFxLab.StepT + 1.6f) { break; }
		const FArenaAbilityDef Ab = C->Ability(GFxLab.Slot);
		const float Dmg = GFxLab.Hp0 - D->GetHealth();
		const float Moved = FVector::Dist2D(D->GetActorLocation(), GFxLab.DummyAt0);
		ARENA_LOG(LogArena, Display, TEXT("FXLAB hero=%s slot=%d ability=%s archetype=%d cast=%d dmg=%.0f moved=%.0f fx=%d castfx=%d trail=%d"), *Defs[GFxLab.Hero].Id.ToString(), GFxLab.Slot, *Ab.Name,
			(int32)Ab.Archetype, GFxLab.bCast ? 1 : 0, Dmg, Moved, Ab.Fx.IsEmpty() ? 0 : 1, Ab.CastFx.IsEmpty() ? 0 : 1, Ab.TrailFx.IsEmpty() ? 0 : 1);
		Check(GFxLab.bCast, FString::Printf(TEXT("%s casts %s"), *Defs[GFxLab.Hero].Id.ToString(), *Ab.Name));
		// a damaging ability aimed at a dummy standing where it reaches hits it
		if (Ab.Damage > 0.f && Ab.Archetype != EArenaArchetype::Buff)
		{
			Check(Dmg > 0.f, FString::Printf(TEXT("%s %s hits the dummy in its aim (%.0f damage)"), *Defs[GFxLab.Hero].Id.ToString(), *Ab.Name, Dmg));
		}
		++GFxLab.Slot;
		GFxLab.StepT = T;
		if (GFxLab.Slot >= 5 || GFxLab.Slot >= Defs[GFxLab.Hero].Abilities.Num()) { ++GFxLab.Hero; GFxLab.Phase = 0; }
		else { GFxLab.Phase = 1; }
		break;
	}
	default: break;
	}
}
