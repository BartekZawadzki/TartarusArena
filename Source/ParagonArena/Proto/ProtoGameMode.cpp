#include "Proto/ProtoGameMode.h"
#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoBotController.h"
#include "Proto/ProtoPlayerController.h"
#include "Proto/ProtoHadesPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Proto/ProtoHUD.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Game/ArenaTeaser.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Animation/AnimInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProtoMode, Log, All);

AProtoGameMode::AProtoGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = AProtoPlayerController::StaticClass();
	HUDClass = AProtoHUD::StaticClass();
	DefaultPawnClass = nullptr;
}

void AProtoGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// prototype 2: Hades' controls — its own player controller (the players log in after this)
	bHades = UGameplayStatics::HasOption(Options, TEXT("Hades")) || FParse::Param(FCommandLine::Get(), TEXT("ProtoHades"));
	if (bHades) { PlayerControllerClass = AProtoHadesPlayerController::StaticClass(); }
}

AProtoGameMode* AProtoGameMode::Get(const UObject* Context)
{
	const UWorld* W = Context ? Context->GetWorld() : nullptr;
	return W ? W->GetAuthGameMode<AProtoGameMode>() : nullptr;
}

void AProtoGameMode::BeginPlay()
{
	Super::BeginPlay();
	AProtoCharacter::Numbers.Reset();
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It) { if (It->Tags.Contains(TEXT("ProtoPatrol"))) { PatrolPoints.Add(It->GetActorLocation()); } }
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It) { if (It->Tags.Contains(TEXT("ProtoCam"))) { OverviewCam = *It; } }
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { if (OverviewCam) { PC->SetViewTarget(OverviewCam); } }
	bLab = FParse::Param(FCommandLine::Get(), TEXT("ProtoLab"));
	bUIShots = FParse::Param(FCommandLine::Get(), TEXT("ProtoUIShots"));
	if (bUIShots) { SetTickableWhenPaused(true); UIT0 = GetWorld()->GetRealTimeSeconds(); }
	if (FParse::Value(FCommandLine::Get(), TEXT("ProtoDiff="), Difficulty)) { Difficulty = FMath::Clamp(Difficulty, 0, 2); }
	LabT0 = GetWorld()->GetTimeSeconds();
	UE_LOG(LogProtoMode, Display, TEXT("PROTO evt=boot patrol=%d cam=%d lab=%d hades=%d"), PatrolPoints.Num(), OverviewCam ? 1 : 0, bLab ? 1 : 0, bHades ? 1 : 0);
	if (bLab) { LabFile(FString::Printf(TEXT("LAB_START %s patrol=%d cam=%d hades=%d"), *FDateTime::Now().ToString(), PatrolPoints.Num(), OverviewCam ? 1 : 0, bHades ? 1 : 0)); }
	AArenaTeaserDirector::MaybeStart(GetWorld());   // -ArenaTeaser=duel|hades: the teaser's film camera
	if (FParse::Param(FCommandLine::Get(), TEXT("ProtoDuel")))
	{
		if (AProtoPlayerController* PPC = Cast<AProtoPlayerController>(GetWorld()->GetFirstPlayerController())) { PPC->bSkipIntro = true; }
		StartMatch(EProtoMode::Duel);
	}
}

FTransform AProtoGameMode::SpawnPoint(int32 Team, int32 Index) const
{
	const FName Tag = Team == 0 ? TEXT("ProtoA") : TEXT("ProtoB");
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerStartTag == Tag)
		{
			FTransform T = It->GetActorTransform();
			T.AddToTranslation(FVector(0.f, Index * 350.f, 0.f));
			return T;
		}
	}
	return FTransform(FRotator(0.f, Team == 0 ? 0.f : 180.f, 0.f), FVector(Team == 0 ? -7600.f : 7600.f, 0.f, 150.f));
}

AProtoCharacter* AProtoGameMode::SpawnChar(int32 Team, const FVector& At, float Yaw, bool bBot, bool bPossessByPlayer)
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FTransform Xf(FRotator(0.f, Yaw, 0.f), At);
	AProtoCharacter* C = GetWorld()->SpawnActorDeferred<AProtoCharacter>(AProtoCharacter::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!C) { return nullptr; }
	C->Team = Team;
	C->bIsBot = bBot;
	C->bTopDown = bHades;
	C->FinishSpawning(Xf);
	if (bBot)
	{
		AProtoBotController* AI = GetWorld()->SpawnActor<AProtoBotController>();
		AI->Difficulty = Difficulty;
		AI->bPassive = Mode == EProtoMode::Sandbox;
		AI->bHades = bHades;
		AI->Possess(C);
	}
	else if (bPossessByPlayer)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->Possess(C); }
	}
	else
	{
		C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	return C;
}

void AProtoGameMode::StartMatch(EProtoMode InMode)
{
	Mode = InMode;
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		if (AController* C = It->GetController()) { if (!C->IsA<APlayerController>()) { C->Destroy(); } }
		It->Destroy();
	}
	Bots.Reset();
	Respawns.Reset();
	PlayerKOs = BotKOs = 0;
	const FTransform A = SpawnPoint(0, 0);
	// -TeaserBots: the player's fighter is a bot too (the teaser films two bots; the player's mannequin kept)
	const bool bTeaserBots = FParse::Param(FCommandLine::Get(), TEXT("TeaserBots"));
	Player = SpawnChar(0, A.GetLocation(), A.Rotator().Yaw, bTeaserBots, !bTeaserBots);
	if (bTeaserBots && Player.IsValid())
	{
		if (USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"))) { Player->GetMesh()->SetSkeletalMesh(Manny); }
	}
	const int32 NumBots = Mode == EProtoMode::TwoBots ? 2 : 1;
	for (int32 i = 0; i < NumBots; ++i)
	{
		const FTransform B = SpawnPoint(1, i);
		Bots.Add(SpawnChar(1, B.GetLocation() + FVector(0.f, i * 400.f - (NumBots - 1) * 200.f, 0.f), B.Rotator().Yaw, true, false));
	}
	Phase = EProtoPhase::Playing;
	MatchStart = GetWorld()->GetTimeSeconds();
	if (AProtoPlayerController* PC = Cast<AProtoPlayerController>(GetWorld()->GetFirstPlayerController())) { PC->OnMatchStarted(); }
	UE_LOG(LogProtoMode, Display, TEXT("PROTO evt=start mode=%d bots=%d diff=%d"), (int32)Mode, NumBots, Difficulty);
}

void AProtoGameMode::BackToMenu()
{
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		if (AController* C = It->GetController()) { if (!C->IsA<APlayerController>()) { C->Destroy(); } }
		It->Destroy();
	}
	Bots.Reset();
	Respawns.Reset();
	Phase = EProtoPhase::Menu;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->UnPossess(); if (OverviewCam) { PC->SetViewTarget(OverviewCam); } }
}

void AProtoGameMode::OnKnockout(AProtoCharacter* Victim, AProtoCharacter* Killer)
{
	if (!Victim) { return; }
	if (Victim->bIsBot) { ++PlayerKOs; } else { ++BotKOs; }
	UE_LOG(LogProtoMode, Display, TEXT("PROTO t=%.1f evt=ko victim=%s killer=%s score=%d:%d"), GetWorld()->GetTimeSeconds(), *Victim->GetName(), Killer ? *Killer->GetName() : TEXT("-"), PlayerKOs, BotKOs);
	if (!bLab) { Respawns.Add({ Victim, float(GetWorld()->GetTimeSeconds()) + 3.5f }); }
}

void AProtoGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = Respawns.Num() - 1; i >= 0; --i)
	{
		if (Now < Respawns[i].At) { continue; }
		if (AProtoCharacter* C = Respawns[i].Who.Get())
		{
			const int32 Index = C->bIsBot ? FMath::Max(0, Bots.IndexOfByPredicate([C](const TWeakObjectPtr<AProtoCharacter>& W) { return W.Get() == C; })) : 0;
			const FTransform T = SpawnPoint(C->Team, Index);
			C->bDead = false;
			C->Health = AProtoCharacter::Tuning().MaxHealth;
			C->Stamina = AProtoCharacter::Tuning().MaxStamina;
			C->bAlerted = false;
			C->StaggerUntil = C->StunUntil = C->AttackUntil = -100.f;
			C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			if (UAnimInstance* Anim = C->GetMesh()->GetAnimInstance()) { Anim->StopAllMontages(0.1f); }
			C->SetActorLocationAndRotation(T.GetLocation() + FVector(0.f, 0.f, 20.f), T.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		Respawns.RemoveAt(i);
	}
	if (bLab) { if (bHades) { TickHadesLab(Now); } else { TickLab(Now); } }
	if (bUIShots) { TickUIShots(); }
}

// ---- the ProtoLab ---------------------------------------------------------------------------------------------------
// the Shipping exe writes no log: the lab's lines also go to Saved/ProtoLab.txt (as the arena's ArenaPerf.txt)
void AProtoGameMode::LabFile(const FString& Line)
{
	FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *(FPaths::ProjectSavedDir() / TEXT("ProtoLab.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void AProtoGameMode::Check(bool bOk, const FString& What)
{
	(bOk ? LabPasses : LabFails) += 1;
	UE_LOG(LogProtoMode, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	LabFile(FString::Printf(TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What));
}

void AProtoGameMode::TickUIShots()
{
	const float T = GetWorld()->GetRealTimeSeconds() - UIT0;
	AProtoPlayerController* PC = Cast<AProtoPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC) { return; }
	switch (UIStep)
	{
	case 0: if (T > 3.f) { Shot(TEXT("UI_Menu")); ++UIStep; } break;
	case 1: if (T > 3.6f) { PC->MenuBack = EProtoMenu::Main; PC->SetMenu(EProtoMenu::Controls); ++UIStep; } break;
	case 2: if (T > 4.8f) { Shot(TEXT("UI_Controls")); ++UIStep; } break;
	case 3: if (T > 5.4f) { PC->SetMenu(EProtoMenu::Main); StartMatch(EProtoMode::Duel); ++UIStep; } break;
	case 4: if (T > 6.8f) { Shot(TEXT("UI_Intro")); ++UIStep; } break;
	case 5: if (T > 7.4f) { PC->SetMenu(EProtoMenu::None); ++UIStep; } break;
	case 6:
		if (T > 7.8f)
		{
			// into the town: an alley between the north-west blocks (buildings all around; from above, the cutaway)
			if (AProtoCharacter* P = Player.Get()) { P->SetActorLocationAndRotation(FVector(-4480.f, 1500.f, 110.f), FRotator(0.f, 90.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics); PC->SetControlRotation(FRotator(-10.f, 90.f, 0.f)); }
			++UIStep;
		}
		break;
	case 7: if (T > 10.f) { Shot(TEXT("UI_Town")); ++UIStep; } break;
	case 8: if (T > 11.f) { PC->MenuBack = EProtoMenu::Pause; PC->SetMenu(EProtoMenu::Pause); ++UIStep; } break;
	case 9: if (T > 12.5f) { Shot(TEXT("UI_Pause")); ++UIStep; } break;
	case 10: if (T > 14.f) { bUIShots = false; UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); } break;
	default: break;
	}
}

void AProtoGameMode::Shot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("PROTO_%02d_%s.png"), ShotIndex++, Name), false, false);
}

AActor* AProtoGameMode::LabBox(const FVector& Center, const FVector& Size)
{
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
	if (!A) { return nullptr; }
	UStaticMeshComponent* M = A->GetStaticMeshComponent();
	M->SetMobility(EComponentMobility::Movable);
	M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	M->SetWorldScale3D(Size / 100.f);
	M->SetCollisionProfileName(TEXT("BlockAll"));
	LabProps.Add(A);
	return A;
}

void AProtoGameMode::TickLab(float Now)
{
	const float T = Now - LabStepT;
	// the lab yard: base A's plaza, facing +Y
	const FVector Yard(-7800.f, -700.f, 110.f);
	AProtoCharacter* A = LabA.Get();
	AProtoCharacter* B = LabB.Get();
	auto Put = [](AProtoCharacter* C, const FVector& At, float Yaw)
	{
		if (!C) { return; }
		C->GetCharacterMovement()->StopMovementImmediately();
		C->SetActorLocationAndRotation(At, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	};
	auto Next = [&]() { ++LabStep; LabStepT = Now; for (float& V : LabValue) { V = 0.f; } };
	auto Refill = [](AProtoCharacter* C) { if (C) { C->Health = AProtoCharacter::Tuning().MaxHealth; C->Stamina = AProtoCharacter::Tuning().MaxStamina; C->StunUntil = C->StaggerUntil = C->CounterUntil = -100.f; C->bLaunched = false; } };
	// the player's aim is the camera: the lab turns the camera (the controller's rotation) as a player would the mouse
	auto Aim = [&](float Yaw) { if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetControlRotation(FRotator(0.f, Yaw, 0.f)); } if (A) { A->SetAimYaw(Yaw); } };
	const FProtoTuning& Tu = AProtoCharacter::Tuning();
	switch (LabStep)
	{
	case 0:
		if (Now - LabT0 > 2.f)
		{
			Phase = EProtoPhase::Playing;
			PatrolPoints.Reset();   // the lab bot stays in the yard
			FMath::RandInit(20260929);   // the lab's dice are the same every run (its bot fight is repeatable)
			LabA = SpawnChar(0, Yard, 90.f, false, true);
			if (AProtoPlayerController* PPC = Cast<AProtoPlayerController>(GetWorld()->GetFirstPlayerController())) { PPC->bSkipIntro = true; PPC->SetMenu(EProtoMenu::None); }
			Aim(90.f);
			Next();
		}
		break;
	case 1:   // run, sprint, sneak: the speeds
		if (A)
		{
			A->SetSprint(T > 1.4f && T < 2.8f);
			A->SetSneak(T >= 2.8f);
			A->Move(FVector2D(0.f, 1.f), FRotator(0.f, 90.f, 0.f));
			if (T > 1.2f && T < 1.3f) { LabValue[0] = A->GetVelocity().Size2D(); }
			if (T > 2.6f && T < 2.7f) { LabValue[1] = A->GetVelocity().Size2D(); LabValue[5] = A->Stamina; Shot(TEXT("Sprint")); }
			if (T > 3.9f && T < 4.0f) { LabValue[2] = A->GetVelocity().Size2D(); }
			if (T > 4.1f)
			{
				A->SetSneak(false);
				Check(FMath::Abs(LabValue[0] - Tu.RunSpeed) < 40.f, FString::Printf(TEXT("run %.0f cm/s (tuning %.0f)"), LabValue[0], Tu.RunSpeed));
				Check(FMath::Abs(LabValue[1] - Tu.SprintSpeed) < 50.f && LabValue[5] < Tu.MaxStamina - 10.f, FString::Printf(TEXT("sprint %.0f cm/s (tuning %.0f), stamina drained to %.0f"), LabValue[1], Tu.SprintSpeed, LabValue[5]));
				Check(FMath::Abs(LabValue[2] - Tu.SneakSpeed) < 30.f, FString::Printf(TEXT("sneak %.0f cm/s (tuning %.0f)"), LabValue[2], Tu.SneakSpeed));
				Put(A, Yard, 90.f);
				Next();
			}
		}
		break;
	case 2:   // a jump, then a double jump: the heights
		if (A)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = A->GetActorLocation().Z; A->PressJump(); }
			if (LabValue[3] > 0.f) { LabValue[0] = FMath::Max(LabValue[0], A->GetActorLocation().Z - LabValue[4]); }
			if (T > 1.6f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->ReleaseJump(); A->PressJump(); }
			if (T > 1.65f && LabValue[6] == 0.f) { LabValue[6] = 1.f; LabValue[1] = LabValue[0]; LabValue[0] = 0.f; }
			if (T > 2.05f && LabValue[7] == 0.f) { LabValue[7] = 1.f; A->ReleaseJump(); A->PressJump(); Shot(TEXT("DoubleJump")); }
			if (T > 3.4f)
			{
				Check(LabValue[1] > 130.f && LabValue[1] < 210.f, FString::Printf(TEXT("a jump rises %.0f cm"), LabValue[1]));
				Check(LabValue[0] > LabValue[1] + 90.f, FString::Printf(TEXT("a double jump rises %.0f cm"), LabValue[0]));
				Put(A, Yard, 90.f);
				LabBox(Yard + FVector(0.f, 330.f, 130.f - 110.f + 1.f), FVector(400.f, 400.f, 260.f));   // a 2.6 m ledge 1.3 m ahead: over a jump, in a climb's reach
				Next();
			}
		}
		break;
	case 3:   // the mantle: run at a 2.6 m ledge and jump (a jump alone lands on 1.8 m)
		if (A)
		{
			if (T < 0.9f) { A->Move(FVector2D(0.f, 1.f), FRotator(0.f, 90.f, 0.f)); }
			A->bDebugMantle = T < 2.f;
			if (T > 0.15f && LabValue[3] == 0.f) { LabValue[3] = 1.f; A->PressJump(); }
			if (T > 0.6f && T < 0.65f) { Shot(TEXT("Mantle")); }
			if (T > 2.f)
			{
				const float Feet = A->GetActorLocation().Z - A->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Check(A->Mantles >= 1 && Feet > 230.f, FString::Printf(TEXT("the 2.6 m ledge is climbed: mantles=%d, feet at %.0f cm"), A->Mantles, Feet));
				A->bDebugMantle = false;
				for (TWeakObjectPtr<AActor>& P : LabProps) { if (P.IsValid()) { P->Destroy(); } }
				LabProps.Reset();
				Put(A, Yard, 90.f);
				LabBox(Yard + FVector(0.f, 170.f, 400.f - 110.f), FVector(600.f, 60.f, 800.f));   // a tall wall 1.2 m ahead
				Next();
			}
		}
		break;
	case 4:   // the wall jump: jump, double jump, then the jump key against the wall
		if (A)
		{
			if (T < 1.0f) { A->Move(FVector2D(0.f, 1.f), FRotator(0.f, 90.f, 0.f)); }
			if (T > 0.2f && LabValue[3] == 0.f) { LabValue[3] = 1.f; A->PressJump(); }
			if (T > 0.6f && LabValue[4] == 0.f) { LabValue[4] = 1.f; A->ReleaseJump(); A->PressJump(); }
			if (T > 1.0f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->ReleaseJump(); LabValue[1] = A->GetVelocity().Z; A->PressJump(); LabValue[2] = A->GetVelocity().Z; }
			if (T > 1.1f && T < 1.15f) { Shot(TEXT("WallJump")); }
			if (T > 2.5f)
			{
				Check(A->WallJumps == 1, FString::Printf(TEXT("a wall jump off the wall: %d"), A->WallJumps));
				for (TWeakObjectPtr<AActor>& P : LabProps) { if (P.IsValid()) { P->Destroy(); } }
				LabProps.Reset();
				Put(A, Yard, 0.f);
				LabB = SpawnChar(1, Yard + FVector(-160.f, 0.f, 0.f), 0.f, false, false);   // a dummy behind, for the invulnerability
				Next();
			}
		}
		break;
	case 5:   // the dodge: distance and the invulnerable frames (a blow after the perfect window: dodged, no counter)
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = A->GetActorLocation().X; A->Dodge(FVector(1.f, 0.f, 0.f)); }
			if (T > 0.3f + Tu.PerfectWindow + 0.05f && LabValue[6] == 0.f) { LabValue[6] = 1.f; LabValue[5] = A->TakeBlow(10.f, B, 0.f, EProtoBlow::Light, TEXT("lab")) + 1.f; }
			if (T > 1.2f)
			{
				const float D = A->GetActorLocation().X - LabValue[4];
				Check(D > 380.f && D < 750.f, FString::Printf(TEXT("a dodge covers %.0f cm"), D));
				Check(LabValue[5] == 1.f && A->PerfectDodges == 0, FString::Printf(TEXT("a blow during the dodge deals nothing (%.0f), not a perfect dodge (late)"), LabValue[5] - 1.f));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f);
				Aim(90.f);
				Next();
			}
		}
		break;
	case 6:   // the light combo: three presses, three hits (a press past the chain point starts the next swing at once)
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = B->Health; A->HitsLanded = 0; A->LightAttack(); }
			if (T > 0.5f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->LightAttack(); }
			if (T > 0.95f && LabValue[6] == 0.f) { LabValue[6] = 1.f; A->LightAttack(); Shot(TEXT("Combo")); }
			if (T > 2.4f)
			{
				const float Dmg = LabValue[4] - B->Health;
				const float Want = Tu.LightDamage[0] + Tu.LightDamage[1] + Tu.LightDamage[2];
				Check(A->HitsLanded == 3 && FMath::Abs(Dmg - Want) < 1.f, FString::Printf(TEXT("a three-hit combo: %d hits, %.0f damage (%.0f)"), A->HitsLanded, Dmg, Want));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f);
				Next();
			}
		}
		break;
	case 7:   // RMB tapped: the quick heavy
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = B->Health; A->StartHeavy(); }
			if (T > 0.4f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->ReleaseHeavy(); }
			if (T > 1.6f)
			{
				const float Dmg = LabValue[4] - B->Health;
				Check(FMath::Abs(Dmg - Tu.QuickHeavyDamage) < 0.6f && B->LastAnim.Contains(TEXT("Hvy")), FString::Printf(TEXT("a tapped heavy: %.0f damage (%.0f), reaction %s"), Dmg, Tu.QuickHeavyDamage, *B->LastAnim));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f);
				Next();
			}
		}
		break;
	case 8:   // RMB held to the full: the launch; then jump after it and slash in the air (the juggle)
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = B->Health; LabValue[6] = B->GetActorLocation().Z; A->StartHeavy(); }
			if (T > 1.0f && T < 1.05f) { Shot(TEXT("Charge")); }
			if (T > 1.5f && LabValue[5] == 0.f) { LabValue[5] = 1.f; A->ReleaseHeavy(); }
			if (LabValue[5] > 0.f) { LabValue[7] = FMath::Max(LabValue[7], B->GetActorLocation().Z - LabValue[6]); if (B->bLaunched) { LabValue[2] = 1.f; } }
			if (LabValue[5] > 0.f && LabValue[0] == 0.f && Now >= A->CancelAt) { LabValue[0] = 1.f; LabValue[1] = B->Health; A->PressJump(); }
			if (LabValue[0] == 1.f && A->GetCharacterMovement()->IsFalling() && A->GetVelocity().Z < 520.f) { LabValue[0] = 2.f; A->ReleaseJump(); Aim((B->GetActorLocation() - A->GetActorLocation()).Rotation().Yaw); A->LightAttack(); Shot(TEXT("AirSlash")); }
			if (LabValue[0] == 2.f && Now >= A->CancelAt) { LabValue[0] = 3.f; Aim((B->GetActorLocation() - A->GetActorLocation()).Rotation().Yaw); A->LightAttack(); }
			if (T > 4.f)
			{
				const float Full = Tu.HeavyBase + Tu.HeavyPerCharge;
				Check(FMath::Abs(LabValue[4] - LabValue[1] - Full) < 1.f && LabValue[2] > 0.f && LabValue[7] > 150.f, FString::Printf(TEXT("a full charge launches: %.0f damage (%.0f), thrown %.0f cm up"), LabValue[4] - LabValue[1], Full, LabValue[7]));
				Check(A->AirHits >= 1 && LabValue[1] - B->Health >= Tu.AirDamage - 0.5f, FString::Printf(TEXT("air slashes hit the launched target: %d hits, %.0f damage"), A->AirHits, LabValue[1] - B->Health));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f);
				Next();
			}
		}
		break;
	case 9:   // the perfect dodge: out of a blow at its last moment — the attacker loses its balance, a counter x1.5
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; B->SetAimAt(A->GetActorLocation()); B->LightAttack(); }
			if (LabValue[3] > 0.f && LabValue[4] == 0.f && B->HitAt - Now < 0.07f) { LabValue[4] = 1.f; A->Dodge(FVector(1.f, 0.f, 0.f)); }
			if (T > 1.0f && LabValue[5] == 0.f) { LabValue[5] = 1.f; LabValue[6] = A->CounterUntil > Now ? 1.f : 0.f; Put(A, Yard, 90.f); Aim(90.f); LabValue[7] = B->Health; }
			if (T > 1.1f && LabValue[0] == 0.f) { LabValue[0] = 1.f; A->LightAttack(); Shot(TEXT("Counter")); }
			if (T > 2.2f)
			{
				const float Want = Tu.LightDamage[0] * Tu.CounterMult;
				Check(A->PerfectDodges == 1 && LabValue[6] > 0.f && A->Health >= Tu.MaxHealth, FString::Printf(TEXT("a perfect dodge: %d, counter open %d, health %.0f"), A->PerfectDodges, (int32)LabValue[6], A->Health));
				Check(A->Counters == 1 && FMath::Abs(LabValue[7] - B->Health - Want) < 0.6f, FString::Printf(TEXT("the counter hits x%.1f: %.0f damage (%.0f)"), Tu.CounterMult, LabValue[7] - B->Health, Want));
				Refill(A); Refill(B);
				// the aim: a dummy 3 m away at 45 degrees off the body's facing
				Put(A, Yard, 90.f);
				Put(B, Yard + FRotator(0.f, 135.f, 0.f).Vector() * 300.f, -45.f);
				Next();
			}
		}
		break;
	case 10:   // the camera aims: looking away the swing misses; looking at the dummy it homes in and lands
		if (A && B)
		{
			if (T > 0.2f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = B->Health; Aim(45.f); A->LightAttack(); }
			if (T > 1.0f && LabValue[5] == 0.f) { LabValue[5] = 1.f; LabValue[6] = B->Health; Aim(135.f); A->LightAttack(); }
			if (T > 1.8f)
			{
				Check(FMath::Abs(LabValue[6] - LabValue[4]) < 0.1f, FString::Printf(TEXT("a swing where the camera looks away misses (%.0f)"), LabValue[4] - LabValue[6]));
				const float YawOff = FMath::Abs(FMath::FindDeltaAngleDegrees(A->GetActorRotation().Yaw, (B->GetActorLocation() - A->GetActorLocation()).Rotation().Yaw));
				Check(LabValue[6] - B->Health > 0.f && YawOff < 20.f, FString::Printf(TEXT("a swing where the camera looks homes in: %.0f damage, facing off by %.0f deg"), LabValue[6] - B->Health, YawOff));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), 90.f);   // its back to the player
				Aim(90.f);
				Next();
			}
		}
		break;
	case 11:   // the reactions: a blow in the back plays the back reaction, one in the face a front one
		if (A && B)
		{
			if (T > 0.2f && LabValue[3] == 0.f) { LabValue[3] = 1.f; A->LightAttack(); }
			if (T > 0.9f && LabValue[4] == 0.f) { LabValue[4] = 1.f; LabValue[5] = B->LastAnim.Contains(TEXT("Back")) ? 1.f : 0.f; Refill(B); Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f); }
			if (T > 1.4f && LabValue[6] == 0.f) { LabValue[6] = 1.f; A->LightAttack(); }
			if (T > 2.1f)
			{
				Check(LabValue[5] > 0.f && B->LastAnim.Contains(TEXT("Front")), FString::Printf(TEXT("hit reactions by side: back %d, front %s"), (int32)LabValue[5], *B->LastAnim));
				// the sneak attack: a bot-flagged dummy, unaware, facing away
				B->Destroy();
				LabB = SpawnChar(1, Yard + FVector(0.f, 150.f, 0.f), 90.f, false, false);
				if (AProtoCharacter* NB = LabB.Get()) { NB->bIsBot = true; NB->bAlerted = false; }
				Refill(A);
				Put(A, Yard, 90.f);
				Next();
			}
		}
		break;
	case 12:
		if (A && B)
		{
			if (T > 0.3f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = B->Health; A->LightAttack(); Shot(TEXT("Backstab")); }
			if (T > 1.5f)
			{
				const float Dmg = LabValue[4] - B->Health;
				Check(A->Backstabs == 1 && FMath::Abs(Dmg - Tu.LightDamage[0] * Tu.BackstabMult) < 0.6f, FString::Printf(TEXT("a sneak attack from behind: %.0f damage (%.0f), backstabs=%d"), Dmg, Tu.LightDamage[0] * Tu.BackstabMult, A->Backstabs));
				Refill(A); Refill(B);
				B->bAlerted = true;
				Put(B, Yard + FVector(0.f, 150.f, 0.f), -90.f);
				Put(A, Yard + FVector(0.f, 60.f, 380.f), 90.f);   // in the air over the dummy
				A->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
				Next();
			}
		}
		break;
	case 13:   // RMB in the air: the slam from 3.8 m
		if (A && B)
		{
			if (T > 0.1f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = B->Health; A->StartHeavy(); }
			if (T > 2.f)
			{
				const float Dmg = LabValue[4] - B->Health;
				Check(A->Plunges == 1 && FMath::Abs(Dmg - Tu.PlungeDamage) < 0.6f, FString::Printf(TEXT("a slam lands its blast: plunges=%d, damage %.0f"), A->Plunges, Dmg));
				Refill(A); Refill(B);
				Put(A, Yard, 90.f); Put(B, Yard + FVector(0.f, 400.f, 0.f), -90.f);
				A->SetLockTarget(B);
				Next();
			}
		}
		break;
	case 14:   // locked on: the body keeps facing the target while it circles (the 8-way jog)
		if (A && B)
		{
			const FVector To = B->GetActorLocation() - A->GetActorLocation();
			if (T < 1.4f) { A->Move(FVector2D(1.f, 0.f), FRotator(0.f, To.Rotation().Yaw, 0.f)); }
			if (T > 1.0f && T < 1.05f)
			{
				LabValue[3] = FMath::Abs(FMath::FindDeltaAngleDegrees(A->GetActorRotation().Yaw, To.Rotation().Yaw));
				LabValue[4] = FMath::Abs(FVector::DotProduct(A->GetVelocity().GetSafeNormal2D(), A->GetActorForwardVector().GetSafeNormal2D()));
				LabValue[5] = A->GetVelocity().Size2D();
				Shot(TEXT("Strafe"));
			}
			if (T > 1.8f)
			{
				Check(LabValue[3] < 20.f && LabValue[4] < 0.5f && LabValue[5] > 200.f, FString::Printf(TEXT("locked on, circling: facing the target within %.0f deg, moving sideways (cos %.2f) at %.0f cm/s"), LabValue[3], LabValue[4], LabValue[5]));
				A->SetLockTarget(nullptr);
				B->Destroy();
				Refill(A);
				Put(A, Yard, 90.f);
				// the bot: 12 m away, normal
				Difficulty = 1;
				LabB = SpawnChar(1, Yard + FVector(0.f, 1200.f, 0.f), -90.f, true, false);
				Next();
			}
		}
		break;
	case 15:   // the bot fights: it reaches the player and uses its moves (the player swings back now and then)
		if (A && B)
		{
			Refill(A);   // the player lasts the whole test
			if (T > 4.f && T > LabValue[3] && B->IsAlive() && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) < 320.f && A->CanAct())
			{
				LabValue[3] = T + 1.5f;
				Aim((B->GetActorLocation() - A->GetActorLocation()).Rotation().Yaw);
				if (FMath::FRand() < 0.45f) { A->StartHeavy(); LabValue[4] = T + 0.7f; } else { A->LightAttack(); }
			}
			if (LabValue[4] > 0.f && T > LabValue[4]) { LabValue[4] = 0.f; A->ReleaseHeavy(); }
			if (B->Health < 30.f) { Refill(B); }   // the bot lasts too
			if (T > 6.f && T < 6.05f) { Shot(TEXT("BotFight")); }
			if (T > 14.f && T < 14.05f) { Shot(TEXT("BotFight2")); }
			if (T > 22.f)
			{
				const AProtoBotController* AI = Cast<AProtoBotController>(B->GetController());
				const int32 Kinds = AI ? (AI->Lights > 0) + (AI->Heavies > 0) + (AI->Dodges > 0) + (AI->SprintAttacks > 0) + (AI->AirCombos > 0) : 0;
				Check(B->HitsLanded > 3, FString::Printf(TEXT("the bot reaches the player and lands hits: %d"), B->HitsLanded));
				Check(Kinds >= 2, FString::Printf(TEXT("the bot mixes its moves: light %d, heavy %d (launch %d), sprint %d, dodge %d, air combo %d"), AI ? AI->Lights : 0, AI ? AI->Heavies : 0, AI ? AI->Launchers : 0, AI ? AI->SprintAttacks : 0, AI ? AI->Dodges : 0, AI ? AI->AirCombos : 0));
				Check(AI && AI->ReadDodges + AI->Interrupts >= 1, FString::Printf(TEXT("the bot reads the player's swings: dodges on a read %d (perfect %d), charges interrupted %d"), AI ? AI->ReadDodges : 0, B->PerfectDodges, AI ? AI->Interrupts : 0));
				if (AController* C = B->GetController()) { C->Destroy(); }
				B->Destroy();
				// the sneak test: a fresh bot facing +Y, the player 9 m behind it, sneaking
				LabB = SpawnChar(1, Yard + FVector(0.f, 900.f, 0.f), 90.f, true, false);
				if (AProtoCharacter* NB = LabB.Get()) { if (AProtoBotController* NAI = Cast<AProtoBotController>(NB->GetController())) { NAI->bPassive = true; } }
				Put(A, Yard, 90.f);
				A->SetSneak(true);
				Next();
			}
		}
		break;
	case 16:
		if (A && B)
		{
			const AProtoBotController* AI = Cast<AProtoBotController>(B->GetController());
			if (T > 1.5f && LabValue[3] == 0.f) { LabValue[3] = 1.f; LabValue[4] = AI && AI->Sees(B, A) ? 1.f : 0.f; A->SetSneak(false); Put(A, B->GetActorLocation() + FVector(0.f, 900.f, 0.f), -90.f); }
			if (T > 2.5f && LabValue[5] == 0.f) { LabValue[5] = 1.f; LabValue[6] = AI && AI->Sees(B, A) ? 1.f : 0.f; }
			if (T > 2.6f)
			{
				Check(LabValue[4] == 0.f, TEXT("a sneaking player 9 m behind the bot is not seen"));
				Check(LabValue[6] > 0.f, TEXT("a player 9 m in front of the bot is seen"));
				Next();
			}
		}
		break;
	case 17:
		if (T > 0.5f)
		{
			if (OverviewCam) { if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(OverviewCam); } }
			Next();
		}
		break;
	case 18:
		if (T > 1.5f) { Shot(TEXT("Overview")); Next(); }
		break;
	case 19:
		if (T > 1.f)
		{
			UE_LOG(LogProtoMode, Display, TEXT("LAB_SUMMARY fails=%d passes=%d"), LabFails, LabPasses);
			LabFile(FString::Printf(TEXT("LAB_SUMMARY fails=%d passes=%d"), LabFails, LabPasses));
			bLab = false;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
