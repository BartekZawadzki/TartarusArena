// -ArenaAnimLab: every hero and both minions in a line on base plateau A, a camera in front of them, and a scripted
// run through each reaction (intro, hit, stun, knock, death, drop-in, victory) plus the physics rules (nobody stands
// on a unit's head, a blast or a walking hero does not fling a prop). Prints LAB PASS/FAIL lines, LAB_SUMMARY, and
// takes LAB_*.png screenshots (Saved/Screenshots/<platform>) as visual evidence. Needs a rendering run (no -nullrhi)
// for the pictures; the measured checks also work headless.
#include "Game/ArenaGameMode.h"
#include "Heroes/ArenaCharacter.h"
#include "Heroes/ArenaPoseBlendInstance.h"
#include "AI/ArenaBotController.h"
#include "Arena/ArenaPhysics.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"

static const FVector LabRow(-5250.f, 0.f, 400.f);   // plateau A top is z 300

void AArenaGameMode::LabSpawnLine()
{
	for (const TWeakObjectPtr<AArenaCharacter>& W : LabUnits) { if (W.IsValid()) { W->Destroy(); } }
	LabUnits.Reset();
	const FArenaDatabaseFile& Db = FArenaDatabase::Get();
	TArray<const FArenaHeroDef*> Defs;
	for (const FArenaHeroDef& D : Db.Heroes) { Defs.Add(&D); }
	Defs.Add(&Db.Rules.MeleeMinion);
	Defs.Add(&Db.Rules.RangedMinion);
	for (int32 i = 0; i < Defs.Num(); ++i)
	{
		const FVector Loc = LabRow + FVector(0.f, (i - (Defs.Num() - 1) * 0.5f) * 330.f, 0.f);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AArenaCharacter* C = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Loc, FRotator::ZeroRotator, P);
		if (!C) { continue; }
		const bool bMinionDef = i >= Db.Heroes.Num();
		C->InitCharacter(*Defs[i], i % 2, 1, bMinionDef);
		C->HeroIndex = bMinionDef ? -1 : i;
		C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;   // bone checks work headless too
		AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
		AI->Possess(C);                                   // idle outside the Playing phase: the lab drives them
		LabUnits.Add(C);
	}
}

void AArenaGameMode::StartAnimLab()
{
	LabSpawnLine();
	for (int32 T = 0; T < 2; ++T)
	{
		// invisible attackers (no controller, no collision: they never move) used as the damage source
		AArenaCharacter* D = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), LabRow + FVector(2500.f, 0.f, 0.f), FRotator::ZeroRotator);
		if (!D) { continue; }
		D->InitCharacter(FArenaDatabase::Get().Heroes[0], T, 1, false);
		D->SetActorHiddenInGame(true);
		D->SetActorEnableCollision(false);
		LabDummy[T] = D;
	}
	if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(LabRow + FVector(1450.f, 0.f, 140.f), FRotator(-6.f, 180.f, 0.f)))
	{
		Cam->GetCameraComponent()->SetFieldOfView(78.f);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(Cam); }
	}
	LabStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=lab_start units=%d"), LabUnits.Num());
}

void AArenaGameMode::TickAnimLab(float Now)
{
	const float T = Now - LabStart;
	TArray<AArenaCharacter*> U;
	for (const TWeakObjectPtr<AArenaCharacter>& W : LabUnits) { if (W.IsValid()) { U.Add(W.Get()); } }
	// the heroes mid-fade (their falls differ in length, so the shot sits between the checks of steps 13 and 14)
	static bool bFadeShot = false;
	if (LabStep == 14 && !bFadeShot && T > 17.2f) { bFadeShot = true; UIShot(TEXT("LAB_07_Fading")); }
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		UE_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto Hit = [this](AArenaCharacter* C, float Amount)
	{
		AArenaCharacter* D = LabDummy[1 - C->GetTeam()].Get();
		if (!D) { return; }
		D->SetActorLocation(C->GetActorLocation() + C->GetActorForwardVector() * 300.f);   // the blow comes from the front
		C->ReceiveDamage(Amount, D, false);
	};
	auto Montage = [](const AArenaCharacter* C)
	{
		const UAnimInstance* A = C->GetMesh()->GetAnimInstance();
		return A && A->IsAnyMontagePlaying();
	};
	// continuous measurements
	if (LabStep >= 9 && LabStep <= 10)
	{
		for (int32 i = 0; i < U.Num() && i < LabZ0.Num(); ++i) { LabKnockMaxZ = FMath::Max(LabKnockMaxZ, U[i]->GetActorLocation().Z - LabZ0[i]); }
	}
	if ((LabStep == 18 || LabStep == 19) && U.Num() > 1) { LabStackMaxUp = FMath::Max(LabStackMaxUp, (float)U[1]->GetVelocity().Z); }
	if (LabStep == 24 && U.Num() > 0 && LabProp.IsValid())
	{
		// walk like a player holding forward, straight at the crate and on past it
		U[0]->AddMovementInput(FVector::ForwardVector, 1.f);
	}
	if (LabStep >= 22 && LabStep <= 24 && LabProp.IsValid())
	{
		LabPropMaxRise = FMath::Max(LabPropMaxRise, (float)(LabProp->GetComponentLocation().Z - LabPropZ0));
		LabPropMaxSpeed = FMath::Max(LabPropMaxSpeed, (float)LabProp->GetPhysicsLinearVelocity().Size());
	}

	switch (LabStep)
	{
	case 0: if (T > 0.6f) { for (AArenaCharacter* C : U) { C->PlayIntro(); } ++LabStep; } break;
	case 1: if (T > 1.8f) { UIShot(TEXT("LAB_00_Intro")); ++LabStep; } break;
	case 2: if (T > 2.6f) { for (AArenaCharacter* C : U) { C->StopIntro(); } ++LabStep; } break;
	case 3:
		if (T > 3.6f)
		{
			LabZ0.Reset();
			for (AArenaCharacter* C : U)
			{
				Hit(C, 5.f);
				if (!C->IsMinion()) { Check(Montage(C), FString::Printf(TEXT("hitreact id=%s plays=%d slot=%s"), *C->GetDef().Id.ToString(), Montage(C) ? 1 : 0, *C->AnimSlot(true).ToString())); }
				else { LabZ0.Add(C->GetActorLocation().X); LabZ0.Add(C->GetActorLocation().Y); C->TryCast(0); }   // minion swing: its root motion must not carry it
			}
			++LabStep;
		}
		break;
	case 4: if (T > 3.85f) { UIShot(TEXT("LAB_01_HitReact")); ++LabStep; } break;
	case 5:
		if (T > 5.0f)
		{
			int32 k = 0;
			for (AArenaCharacter* C : U)
			{
				if (C->IsMinion() && LabZ0.IsValidIndex(k + 1))
				{
					const float Moved = FVector2D::Distance(FVector2D(C->GetActorLocation()), FVector2D(LabZ0[k], LabZ0[k + 1]));
					k += 2;
					Check(Moved < 30.f, FString::Printf(TEXT("minion %s swing keeps its place: moved %.0f cm (slot %s)"), *C->GetDef().Id.ToString(), Moved, *C->AnimSlot(true).ToString()));
				}
				C->ApplyStun(3.2f);
			}
			LabZ0.Reset();
			++LabStep;
		}
		break;
	case 6:
		if (T > 5.7f)
		{
			UIShot(TEXT("LAB_02_Stun"));
			for (AArenaCharacter* C : U) { if (!C->IsMinion()) { Check(Montage(C), FString::Printf(TEXT("stun id=%s plays=%d slot=%s"), *C->GetDef().Id.ToString(), Montage(C) ? 1 : 0, *C->AnimSlot(false).ToString())); } }
			++LabStep;
		}
		break;
	case 7: if (T > 7.6f) { UIShot(TEXT("LAB_03_StunLoop")); ++LabStep; } break;
	case 8:
		if (T > 8.6f)
		{
			LabZ0.Reset();
			for (int32 i = 0; i < U.Num(); ++i)
			{
				LabZ0.Add(U[i]->GetActorLocation().Z);
				const FVector Dir = -U[i]->GetActorForwardVector();
				U[i]->Displace(Dir, ArenaCore::KnockDistanceCm(4.f), ArenaCore::KnockHeightCm(4.5f), ArenaCore::KnockAirSeconds(4.5f));   // the strongest knock in the data, the way abilities apply it
				U[i]->OnKnocked(Dir, i % 2 == 0);
			}
			LabKnockMaxZ = 0.f;
			++LabStep;
		}
		break;
	case 9: if (T > 8.95f) { UIShot(TEXT("LAB_04_Knock")); ++LabStep; } break;
	case 10:
		if (T > 11.0f)
		{
			int32 Grounded = 0;
			for (AArenaCharacter* C : U) { Grounded += C->GetCharacterMovement()->IsMovingOnGround() ? 1 : 0; }
			Check(LabKnockMaxZ < 140.f && LabKnockMaxZ > 60.f, FString::Printf(TEXT("knock-up max_rise=%.0f cm (4.5 m/s: %.0f cm planned)"), LabKnockMaxZ, ArenaCore::KnockHeightCm(4.5f)));
			Check(Grounded == U.Num(), FString::Printf(TEXT("knocked units back on the ground %d/%d"), Grounded, U.Num()));
			++LabStep;
		}
		break;
	case 11: if (T > 11.5f) { for (AArenaCharacter* C : U) { Hit(C, 1.e6f); } ++LabStep; } break;
	case 12: if (T > 12.1f) { UIShot(TEXT("LAB_05_Dying")); ++LabStep; } break;
	case 13:
		if (T > 14.2f)
		{
			UIShot(TEXT("LAB_06_Dead"));
			for (AArenaCharacter* C : U)
			{
				const float Feet = C->GetActorLocation().Z - C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				// a ragdoll lies where it fell: from the ground under its pelvis, not from the capsule's old feet
				FHitResult Floor;
				const FVector Pv = C->GetMesh()->GetBoneLocation(TEXT("pelvis"));
				FCollisionQueryParams Fq(SCENE_QUERY_STAT(LabFloor), false, C);
				const float Ground = GetWorld()->LineTraceSingleByChannel(Floor, Pv + FVector(0.f, 0.f, 50.f), Pv - FVector(0.f, 0.f, 400.f), ECC_WorldStatic, Fq) ? Floor.ImpactPoint.Z : Feet;
				const float Pelvis = Pv.Z - Ground;
				const bool bFloats = !C->GetDef().DeathVanishFx.IsEmpty();   // a death that ends in a vanish (Countess's bats, Morigesh's swarm, ...) never lies down
				const UArenaPoseBlendInstance* PB = Cast<UArenaPoseBlendInstance>(C->GetMesh()->GetAnimInstance());
				Check(!C->IsAlive() && (bFloats || Pelvis < 50.f), FString::Printf(TEXT("death id=%s pelvis_above_floor=%.0f outside_abp=%d anim_time=%.2f blend=%.2f"), *C->GetDef().Id.ToString(), Pelvis,
					C->IsOnAnimBlueprint() ? 0 : 1, PB ? PB->GetTime() : -1.f, PB ? PB->GetAlpha() : -1.f));
				// a body lies on the ground before it fades (a hero for at least 2 s after its fall)
				if (!C->IsMinion() && !bFloats) { Check(C->GetFadeProgress() <= 0.f && !C->IsHidden(), FString::Printf(TEXT("the body of %s lies fully drawn 2.7 s after its death (fade=%.2f)"), *C->GetDef().Id.ToString(), C->GetFadeProgress())); }
			}
			++LabStep;
		}
		break;
	case 14:
		if (T > 17.5f)
		{
			// the death fade: minions are gone, heroes are fading; a hero whose death ends in its own burst (Countess's
			// bats, Morigesh's swarm, Khaimera's and Sevarog's bursts) is gone instead
			for (AArenaCharacter* C : U)
			{
				if (!C->GetDef().DeathVanishFx.IsEmpty())
				{
					Check(C->IsHidden(), FString::Printf(TEXT("hero %s vanished in its own burst (hidden=%d)"), *C->GetDef().Id.ToString(), C->IsHidden() ? 1 : 0));
					continue;
				}
				Check(C->CanFade(), FString::Printf(TEXT("%s has fade materials (Tools/make_fade_materials.py)"), *C->GetDef().Id.ToString()));
				if (C->IsMinion()) { Check(C->IsHidden() && C->GetFadeProgress() >= 1.f, FString::Printf(TEXT("minion %s faded away 6 s after its death (fade=%.2f hidden=%d)"), *C->GetDef().Id.ToString(), C->GetFadeProgress(), C->IsHidden() ? 1 : 0)); }
				else { Check(C->GetFadeProgress() > 0.f, FString::Printf(TEXT("hero %s is fading 6 s after its death (fade=%.2f hidden=%d)"), *C->GetDef().Id.ToString(), C->GetFadeProgress(), C->IsHidden() ? 1 : 0)); }
			}
			++LabStep;
		}
		break;
	case 15: if (T > 19.0f) { LabSpawnLine(); for (const TWeakObjectPtr<AArenaCharacter>& W : LabUnits) { if (W.IsValid()) { W->PlaySpawnIn(); } } ++LabStep; } break;
	case 16: if (T > 19.35f) { UIShot(TEXT("LAB_08_SpawnIn")); ++LabStep; } break;
	case 17:
		if (T > 21.0f && U.Num() > 1)
		{
			int32 Abp = 0;
			for (AArenaCharacter* C : U) { Abp += C->IsOnAnimBlueprint() ? 1 : 0; }
			Check(Abp == U.Num(), FString::Printf(TEXT("after the drop-in every unit is back on its anim blueprint %d/%d"), Abp, U.Num()));
			int32 Visible = 0;
			for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
			{
				Visible += It->IsHidden() ? 0 : 1;
				UE_LOG(LogArena, Display, TEXT("LAB unit %s alive=%d hidden=%d at=%s"), *It->GetDef().Id.ToString(), It->IsAlive() ? 1 : 0, It->IsHidden() ? 1 : 0, *It->GetActorLocation().ToCompactString());
			}
			Check(Visible == U.Num(), FString::Printf(TEXT("only the new line-up is visible: %d characters shown for %d units"), Visible, U.Num()));
			// stack: unit 1 dropped dead-centre onto unit 0's head
			U[1]->SetActorLocation(U[0]->GetActorLocation() + FVector(0.f, 0.f, 235.f), false, nullptr, ETeleportType::TeleportPhysics);
			U[1]->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			U[1]->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			LabStackMaxUp = 0.f;
			++LabStep;
		}
		break;
	case 18: if (T > 21.45f) { UIShot(TEXT("LAB_09_Stack")); ++LabStep; } break;
	case 19:
		if (T > 24.0f && U.Num() > 1)
		{
			const UCharacterMovementComponent* M = U[1]->GetCharacterMovement();
			const AActor* Floor = M->CurrentFloor.HitResult.GetActor();
			const float Sep = FVector::Dist2D(U[0]->GetActorLocation(), U[1]->GetActorLocation());
			const float Need = U[0]->GetCapsuleComponent()->GetScaledCapsuleRadius() + U[1]->GetCapsuleComponent()->GetScaledCapsuleRadius() - 5.f;
			Check(M->IsMovingOnGround() && !Cast<APawn>(Floor) && Sep >= Need, FString::Printf(TEXT("unit dropped on a head slid off: grounded=%d floor=%s separation=%.0f (need %.0f)"),
				M->IsMovingOnGround() ? 1 : 0, Floor ? *Floor->GetName() : TEXT("none"), Sep, Need));
			Check(LabStackMaxUp < 150.f, FString::Printf(TEXT("no launch while sliding off: max_up_speed=%.0f cm/s"), LabStackMaxUp));
			++LabStep;
		}
		break;
	case 20:
		if (T > 24.5f && U.Num() > 0)
		{
			const FVector At(LabRow.X + 260.f, U[0]->GetActorLocation().Y, 345.f);
			if (AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator))
			{
				UStaticMeshComponent* C = A->GetStaticMeshComponent();
				C->SetMobility(EComponentMobility::Movable);
				C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				C->SetWorldScale3D(FVector(0.8f));
				C->SetCollisionProfileName(TEXT("PhysicsActor"));
				C->SetSimulatePhysics(true);
				C->SetMassOverrideInKg(NAME_None, 40.f, true);
				C->CanCharacterStepUpOn = ECB_No;
				ArenaPhysics::CalmProp(C);
				LabProp = C;
			}
			++LabStep;
		}
		break;
	case 21:
		if (T > 25.5f && LabProp.IsValid())
		{
			LabPropZ0 = LabProp->GetComponentLocation().Z;
			LabPropMaxRise = LabPropMaxSpeed = 0.f;
			ArenaPhysics::RadialImpulse(GetWorld(), LabProp->GetComponentLocation() - FVector(120.f, 0.f, 40.f), 720.f, 3000.f);   // an ultimate's blast
			++LabStep;
		}
		break;
	case 22: if (T > 25.8f) { UIShot(TEXT("LAB_10_PropBlast")); ++LabStep; } break;
	case 23:
		if (T > 28.5f && LabProp.IsValid() && U.Num() > 0)
		{
			Check(LabPropMaxRise < 120.f, FString::Printf(TEXT("ultimate blast lifts a 40 kg crate %.0f cm, top speed %.0f cm/s"), LabPropMaxRise, LabPropMaxSpeed));
			LabPropMaxRise = LabPropMaxSpeed = 0.f;
			// the crate goes right in front of the hero, who then walks straight ahead (+X) like a player holding W
			LabPushFrom = U[0]->GetActorLocation();
			LabProp->SetWorldLocationAndRotation(FVector(LabPushFrom.X + 220.f, LabPushFrom.Y, 342.f), FRotator::ZeroRotator, false, nullptr, ETeleportType::ResetPhysics);
			LabProp->SetPhysicsLinearVelocity(FVector::ZeroVector);
			LabProp->WakeAllRigidBodies();
			LabPropZ0 = 342.f;
			++LabStep;
		}
		break;
	case 24:
		if (T > 32.0f)
		{
			const float Walked = U.Num() > 0 ? FVector::Dist2D(U[0]->GetActorLocation(), LabPushFrom) : 0.f;
			Check(Walked > 180.f && LabPropMaxSpeed > 20.f && LabPropMaxRise < 60.f && LabPropMaxSpeed < 900.f, FString::Printf(TEXT("a walking hero shoves the crate: walked %.0f cm, crate rise %.0f cm, top speed %.0f cm/s"), Walked, LabPropMaxRise, LabPropMaxSpeed));
			++LabStep;
		}
		break;
	case 25:
		if (T > 32.5f && U.Num() > 2)
		{
			// regression: bots standing on the rim of the orb altar did not walk off it (seeds 7, 12, 21)
			U[2]->SetActorLocation(FVector(-430.f, 0.f, -31.f), false, nullptr, ETeleportType::TeleportPhysics);
			LabPushFrom = U[2]->GetActorLocation();
			if (AAIController* AI = Cast<AAIController>(U[2]->GetController()))
			{
				const EPathFollowingRequestResult::Type R = AI->MoveToLocation(FVector(-3174.f, -309.f, 22.f), 50.f, true, true, false, true);
				UE_LOG(LogArena, Display, TEXT("LAB altar move request=%d"), (int32)R);
			}
			++LabStep;
		}
		break;
	case 26:
		if (U.Num() > 2 && FMath::Fmod(T, 0.25f) < 0.02f)
		{
			const UCharacterMovementComponent* M = U[2]->GetCharacterMovement();
			const AAIController* AI = Cast<AAIController>(U[2]->GetController());
			const UPathFollowingComponent* PF = AI ? AI->GetPathFollowingComponent() : nullptr;
			UE_LOG(LogArena, Display, TEXT("LAB altar t=%.2f pos=%s accel=%.0f vel=%.0f mode=%d floor=%s walkable=%d status=%d idx=%d pts=%d"), T, *U[2]->GetActorLocation().ToCompactString(),
				M->GetCurrentAcceleration().Size(), U[2]->GetVelocity().Size2D(), (int32)M->MovementMode, M->CurrentFloor.HitResult.GetActor() ? *M->CurrentFloor.HitResult.GetActor()->GetName() : TEXT("none"),
				M->CurrentFloor.bWalkableFloor ? 1 : 0, PF ? (int32)PF->GetStatus() : -1, PF ? (int32)PF->GetCurrentPathIndex() : -1, PF && PF->GetPath().IsValid() ? PF->GetPath()->GetPathPoints().Num() : -1);
		}
		if (T > 36.0f && U.Num() > 2)
		{
			const float Moved = FVector::Dist2D(U[2]->GetActorLocation(), LabPushFrom);
			Check(Moved > 600.f, FString::Printf(TEXT("a unit on the orb altar rim walks off towards its goal: moved %.0f cm"), Moved));
			++LabStep;
		}
		break;
	case 27: if (T > 36.5f) { for (AArenaCharacter* C : U) { C->PlayVictory(); } ++LabStep; } break;
	case 28: if (T > 38.0f) { UIShot(TEXT("LAB_11_Victory")); ++LabStep; } break;
	case 29:
		if (T > 39.0f)
		{
			UE_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			++LabStep;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
