// Prototype 2's lab (-ProtoLab -ProtoHades, or ?Hades): the Hades controls measured with PASS / FAIL lines (in the
// log and Saved/ProtoLab.txt) — the fixed camera above, moving on the screen, the jump, the dash, the attack at the
// cursor, the dash-strike, the cast, the special's smash around and its charge, the call, the perfect dodge, the
// buildings cut away between the camera and the hero, and the bot with the same moves.
#include "Proto/ProtoGameMode.h"
#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoBotController.h"
#include "Proto/ProtoHadesPlayerController.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetSystemLibrary.h"

void AProtoGameMode::TickHadesLab(float Now)
{
	const float T = Now - LabStepT;
	const FVector Yard(-7800.f, -700.f, 110.f);
	AProtoCharacter* A = LabA.Get();
	AProtoCharacter* B = LabB.Get();
	AProtoCharacter* C = LabC.Get();
	AProtoHadesPlayerController* PC = Cast<AProtoHadesPlayerController>(GetWorld()->GetFirstPlayerController());
	auto Put = [](AProtoCharacter* X, const FVector& At, float Yaw)
	{
		if (!X) { return; }
		X->GetCharacterMovement()->StopMovementImmediately();
		X->SetActorLocationAndRotation(At, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	};
	auto Next = [&]() { ++LabStep; LabStepT = Now; for (float& V : LabValue) { V = 0.f; } };
	auto Refill = [](AProtoCharacter* X) { if (X) { X->Health = AProtoCharacter::Tuning().MaxHealth; X->Stamina = AProtoCharacter::Tuning().MaxStamina; X->StunUntil = X->StaggerUntil = X->CounterUntil = X->SlowUntil = -100.f; X->bLaunched = false; } };
	auto Dummy = [&](const FVector& At, float Yaw) { AProtoCharacter* X = SpawnChar(1, At, Yaw, false, false); return X; };
	auto Hp = [](AProtoCharacter* X) { return X ? X->Health : 0.f; };
	const FProtoTuning& Tu = AProtoCharacter::Tuning();
	const float CamYaw = A ? A->SpringArm->GetComponentRotation().Yaw : 45.f;
	switch (LabStep)
	{
	case 0:
		if (Now - LabT0 > 2.f)
		{
			Phase = EProtoPhase::Playing;
			PatrolPoints.Reset();
			FMath::RandInit(20260929);
			LabA = SpawnChar(0, Yard, 90.f, false, true);
			if (PC) { PC->bSkipIntro = true; PC->SetMenu(EProtoMenu::None); }
			Next();
		}
		break;
	case 1:   // the camera from above, fixed; W moves up the screen, D to its right
		if (A)
		{
			if (T < 1.f) { A->Move(FVector2D(0.f, 1.f), FRotator(0.f, CamYaw, 0.f)); }
			else if (T < 2.f) { A->Move(FVector2D(1.f, 0.f), FRotator(0.f, CamYaw, 0.f)); }
			if (T > 0.85f && T < 0.9f) { LabValue[0] = A->GetVelocity().Rotation().Yaw; LabValue[1] = A->GetVelocity().Size2D(); Shot(TEXT("H_TopDown")); }
			if (T > 1.85f && T < 1.9f) { LabValue[2] = A->GetVelocity().Rotation().Yaw; }
			if (T > 2.3f)
			{
				const FRotator Arm = A->SpringArm->GetComponentRotation();
				Check(A->SpringArm->IsUsingAbsoluteRotation() && FMath::Abs(Arm.Pitch + 55.f) < 1.f && FMath::Abs(FMath::FindDeltaAngleDegrees(Arm.Yaw, 45.f)) < 1.f && A->SpringArm->TargetArmLength > 1500.f,
					FString::Printf(TEXT("the camera is fixed above: pitch %.0f, yaw %.0f, %.0f cm away"), Arm.Pitch, Arm.Yaw, A->SpringArm->TargetArmLength));
				Check(FMath::Abs(FMath::FindDeltaAngleDegrees(LabValue[0], CamYaw)) < 10.f && FMath::Abs(FMath::FindDeltaAngleDegrees(LabValue[2], CamYaw + 90.f)) < 10.f && LabValue[1] > 400.f,
					FString::Printf(TEXT("WASD on the screen: W runs at yaw %.0f (up the screen %.0f), D at %.0f, %.0f cm/s"), LabValue[0], CamYaw, LabValue[2], LabValue[1]));
				Put(A, Yard, 90.f);
				Next();
			}
		}
		break;
	case 2:   // Space: the jump (the one addition to Hades' keys)
		if (A)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = A->GetActorLocation().Z; A->PressJump(); }
			if (LabValue[3] > 0.f) { LabValue[0] = FMath::Max(LabValue[0], A->GetActorLocation().Z - LabValue[4]); }
			if (T > 1.6f)
			{
				A->ReleaseJump();
				Check(LabValue[0] > 130.f, FString::Printf(TEXT("Space jumps: %.0f cm"), LabValue[0]));
				Put(A, Yard, 90.f);
				LabB = Dummy(Yard + FVector(-160.f, 0.f, 0.f), 0.f);
				Next();
			}
		}
		break;
	case 3:   // Shift: the dash (Hades' dash, off Space): distance, invulnerable
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = A->GetActorLocation().X; A->Dodge(FVector(1.f, 0.f, 0.f)); }
			if (T > 0.3f + Tu.PerfectWindow + 0.05f && LabValue[6] == 0.f) { LabValue[6] = 1.f; LabValue[5] = A->TakeBlow(10.f, B, 0.f, EProtoBlow::Light, TEXT("lab")) + 1.f; }
			if (T > 1.2f)
			{
				const float D = A->GetActorLocation().X - LabValue[4];
				Check(D > 380.f && D < 750.f && LabValue[5] == 1.f, FString::Printf(TEXT("the dash covers %.0f cm, a blow in it deals %.0f"), D, LabValue[5] - 1.f));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f);
				Put(B, Yard + FRotator(0.f, 135.f, 0.f).Vector() * 250.f, -45.f);
				Next();
			}
		}
		break;
	case 4:   // LMB at the cursor: aimed away it misses, on the dummy it hits
		if (A && B)
		{
			if (T > 0.2f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = Hp(B); A->SetAimAt(A->GetActorLocation() + FRotator(0.f, 45.f, 0.f).Vector() * 300.f); A->LightAttack(); }
			if (T > 1.0f && LabValue[5] == 0.f) { LabValue[5] = 1.f; LabValue[6] = Hp(B); A->SetAimAt(B->GetActorLocation()); A->LightAttack(); Shot(TEXT("H_Attack")); }
			if (T > 1.8f)
			{
				Check(FMath::Abs(LabValue[6] - LabValue[4]) < 0.1f && LabValue[6] - Hp(B) > 0.f, FString::Printf(TEXT("LMB attacks at the cursor: away %.0f, on the dummy %.0f damage"), LabValue[4] - LabValue[6], LabValue[6] - Hp(B)));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 450.f, 0.f), -90.f);
				Next();
			}
		}
		break;
	case 5:   // the dash-strike: a dash at the dummy, LMB in it
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = Hp(B); A->SetAimAt(B->GetActorLocation()); A->Dodge(FVector(0.f, 1.f, 0.f)); }
			if (T > 0.42f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->LightAttack(); Shot(TEXT("H_DashStrike")); }
			if (T > 1.4f)
			{
				Check(A->DashStrikes == 1 && FMath::Abs(LabValue[4] - Hp(B) - Tu.DashStrikeDamage) < 0.6f, FString::Printf(TEXT("the dash-strike: %d, %.0f damage (%.0f)"), A->DashStrikes, LabValue[4] - Hp(B), Tu.DashStrikeDamage));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 900.f, 0.f), -90.f);
				Next();
			}
		}
		break;
	case 6:   // RMB: the cast at the cursor 9 m away — damage, a slow; the stone comes back
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = Hp(B); A->SetAimAt(B->GetActorLocation()); A->CastStone(); LabValue[7] = A->CastAmmo; Shot(TEXT("H_Cast")); }
			if (T > 1.2f && LabValue[5] == 0.f) { LabValue[5] = 1.f; LabValue[6] = B->SlowUntil > Now ? 1.f : 0.f; LabValue[0] = LabValue[4] - Hp(B); }
			if (T > 0.3f + Tu.CastReturn + 0.4f)
			{
				Check(A->CastHits == 1 && FMath::Abs(LabValue[0] - Tu.CastDamage) < 0.6f && LabValue[6] > 0.f && LabValue[7] == 0.f, FString::Printf(TEXT("the cast hits 9 m away: %d hit, %.0f damage (%.0f), slowed %d, ammo after the throw %.0f"), A->CastHits, LabValue[0], Tu.CastDamage, (int32)LabValue[6], LabValue[7]));
				Check(A->CastAmmo == 1, FString::Printf(TEXT("the stone comes back after %.0f s: ammo %d"), Tu.CastReturn, A->CastAmmo));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 170.f, 0.f), -90.f);
				LabC = Dummy(Yard + FVector(0.f, -170.f, 0.f), 90.f);
				Next();
			}
		}
		break;
	case 7:   // Q tapped: the smash around — the dummy in front and the one behind
		if (A && B && C)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = Hp(B); LabValue[5] = Hp(C); A->SetAimAt(B->GetActorLocation()); A->StartHeavy(); }
			if (T > 0.4f && LabValue[6] == 0.f) { LabValue[6] = 1.f; A->ReleaseHeavy(); Shot(TEXT("H_Smash")); }
			if (T > 1.5f)
			{
				Check(FMath::Abs(LabValue[4] - Hp(B) - Tu.NovaDamage) < 0.6f && FMath::Abs(LabValue[5] - Hp(C) - Tu.NovaDamage) < 0.6f, FString::Printf(TEXT("Q tapped smashes around: front %.0f, behind %.0f (%.0f)"), LabValue[4] - Hp(B), LabValue[5] - Hp(C), Tu.NovaDamage));
				Refill(A); Refill(B); Refill(C);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f); Put(C, Yard + FVector(600.f, -600.f, 0.f), 0.f);
				Next();
			}
		}
		break;
	case 8:   // Q held to the full: the launch
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = Hp(B); LabValue[6] = B->GetActorLocation().Z; A->SetAimAt(B->GetActorLocation()); A->StartHeavy(); }
			if (T > 1.5f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->ReleaseHeavy(); }
			if (LabValue[5] > 0.f) { LabValue[7] = FMath::Max(LabValue[7], B->GetActorLocation().Z - LabValue[6]); }
			if (T > 3.5f)
			{
				const float Full = Tu.HeavyBase + Tu.HeavyPerCharge;
				Check(FMath::Abs(LabValue[4] - Hp(B) - Full) < 1.f && LabValue[7] > 150.f, FString::Printf(TEXT("Q held to the full launches: %.0f damage (%.0f), %.0f cm up"), LabValue[4] - Hp(B), Full, LabValue[7]));
				LabValue[0] = A->Wrath;
				Refill(A); Refill(B); Refill(C);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 300.f, 0.f), -90.f); Put(C, Yard + FVector(0.f, -350.f, 0.f), 90.f);
				Next();
				LabValue[0] = A->Wrath;   // the gauge filled by the blows so far
			}
		}
		break;
	case 9:   // F: the call — the wrath gauge (filled by blows) set full, a blast that throws every enemy near up
		if (A && B && C)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[1] = LabValue[0]; A->Wrath = Tu.WrathMax; LabValue[4] = Hp(B); LabValue[5] = Hp(C); A->CallWrath(); Shot(TEXT("H_Call")); }
			if (LabValue[3] > 0.f && (B->bLaunched || C->bLaunched)) { LabValue[6] = FMath::Max(LabValue[6], (B->bLaunched ? 1.f : 0.f) + (C->bLaunched ? 1.f : 0.f)); }
			if (T > 2.6f)
			{
				Check(LabValue[1] > 0.f, FString::Printf(TEXT("the wrath gauge fills with the blows: %.0f of %.0f"), LabValue[1], Tu.WrathMax));
				Check(A->Calls == 1 && A->Wrath < 1.f && FMath::Abs(LabValue[4] - Hp(B) - Tu.WrathDamage) < 0.6f && FMath::Abs(LabValue[5] - Hp(C) - Tu.WrathDamage) < 0.6f && LabValue[6] >= 2.f,
					FString::Printf(TEXT("F calls the wrath: %.0f and %.0f damage (%.0f), both thrown up %d"), LabValue[4] - Hp(B), LabValue[5] - Hp(C), Tu.WrathDamage, (int32)LabValue[6]));
				if (C) { C->Destroy(); }
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f);
				Next();
			}
		}
		break;
	case 10:   // the perfect dodge (Shift at the last moment) and the counter
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; B->SetAimAt(A->GetActorLocation()); B->LightAttack(); }
			if (LabValue[3] > 0.f && LabValue[4] == 0.f && B->HitAt - Now < 0.07f) { LabValue[4] = 1.f; A->Dodge(FVector(1.f, 0.f, 0.f)); }
			if (T > 1.0f && LabValue[5] == 0.f) { LabValue[5] = 1.f; Put(A, Yard, 90.f); LabValue[7] = Hp(B); }
			if (T > 1.1f && LabValue[0] == 0.f) { LabValue[0] = 1.f; A->SetAimAt(B->GetActorLocation()); A->LightAttack(); }
			if (T > 2.2f)
			{
				Check(A->PerfectDodges == 1 && A->Counters == 1 && FMath::Abs(LabValue[7] - Hp(B) - Tu.LightDamage[0] * Tu.CounterMult) < 0.6f, FString::Printf(TEXT("a perfect dodge and the counter: dodges %d, counters %d, %.0f damage"), A->PerfectDodges, A->Counters, LabValue[7] - Hp(B)));
				B->Destroy();
				Refill(A);
				Put(A, Yard, 90.f);
				// a tall wall between the camera and the hero (the camera sits back along -45 degrees, up 55)
				const FVector Back = -FRotator(0.f, CamYaw, 0.f).Vector();
				LabBox(FVector(Yard.X, Yard.Y, 0.f) + Back * 350.f + FVector(0.f, 0.f, 450.f), FVector(300.f, 300.f, 900.f));
				Next();
			}
		}
		break;
	case 11:   // a building between the camera and the hero is cut away, and comes back when the hero walks off
		if (A)
		{
			AActor* Wall = LabProps.Num() > 0 ? LabProps.Last().Get() : nullptr;
			if (T > 0.4f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = Wall && Wall->IsHidden() ? 1.f : 0.f; LabValue[5] = PC ? PC->CutCount() : 0; Shot(TEXT("H_Cutaway")); Put(A, Yard + FVector(1300.f, 0.f, 0.f), 90.f); }
			if (T > 0.8f)
			{
				Check(LabValue[4] > 0.f && (!Wall || !Wall->IsHidden()), FString::Printf(TEXT("a building in the camera's way is cut away (%.0f hidden), back when the hero moves off (hidden now %d)"), LabValue[5], Wall && Wall->IsHidden() ? 1 : 0));
				for (TWeakObjectPtr<AActor>& P : LabProps) { if (P.IsValid()) { P->Destroy(); } }
				LabProps.Reset();
				Put(A, Yard, 90.f);
				Difficulty = 1;
				LabB = SpawnChar(1, Yard + FVector(0.f, 1100.f, 0.f), -90.f, true, false);
				Next();
			}
		}
		break;
	case 12:   // the bot with the same moves: it throws, dash-strikes, attacks, reads the player's swings
		if (A && B)
		{
			Refill(A);
			if (T > 4.f && T > LabValue[3] && B->IsAlive() && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) < 320.f && A->CanAct())
			{
				LabValue[3] = T + 1.5f;
				A->SetAimAt(B->GetActorLocation());
				if (FMath::FRand() < 0.45f) { A->StartHeavy(); LabValue[4] = T + 0.7f; } else { A->LightAttack(); }
			}
			if (LabValue[4] > 0.f && T > LabValue[4]) { LabValue[4] = 0.f; A->ReleaseHeavy(); }
			if (B->Health < 30.f) { Refill(B); }
			if (T > 6.f && T < 6.05f) { Shot(TEXT("H_BotFight")); }
			if (T > 22.f)
			{
				const AProtoBotController* AI = Cast<AProtoBotController>(B->GetController());
				const int32 Kinds = AI ? (AI->Lights > 0) + (AI->Heavies > 0) + (AI->Casts > 0) + (AI->DashStrikes > 0) + (AI->Dodges > 0) : 0;
				Check(B->HitsLanded > 3, FString::Printf(TEXT("the bot lands hits: %d"), B->HitsLanded));
				Check(AI && AI->Casts + AI->DashStrikes >= 1 && Kinds >= 3, FString::Printf(TEXT("the bot uses the Hades moves: casts %d (hits %d), dash-strikes %d, smashes %d, light %d, heavy %d, dodges %d"), AI ? AI->Casts : 0, B->CastHits, AI ? AI->DashStrikes : 0, AI ? AI->Smashes : 0, AI ? AI->Lights : 0, AI ? AI->Heavies : 0, AI ? AI->Dodges : 0));
				if (AController* Ctl = B->GetController()) { Ctl->Destroy(); }
				B->Destroy();
				Next();
			}
		}
		break;
	case 13:
		if (T > 0.5f)
		{
			if (APlayerController* P = GetWorld()->GetFirstPlayerController()) { if (A) { P->SetViewTarget(A); } }
			Next();
		}
		break;
	case 14:
		if (T > 1.f) { Shot(TEXT("H_View")); Next(); }
		break;
	case 15:
		if (T > 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("LAB_SUMMARY fails=%d passes=%d"), LabFails, LabPasses);
			LabFile(FString::Printf(TEXT("LAB_SUMMARY fails=%d passes=%d"), LabFails, LabPasses));
			bLab = false;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
