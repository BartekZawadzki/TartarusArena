// The training centre (/Game/Maps/Training, built by Tools/build_training.py): a grey room of engine shapes where the
// player tries a hero on dummies. No clock, score, minions or bots; the shop works anywhere with unlimited gold.
//   F1  cooldowns and mana off / on       F2  level 20, every rank maxed       F3  dummies back to their spots
//   Esc the pause menu (back to the main menu)
// The dummies (enemy heroes, no controller) regenerate when hurt and come back 1.5 s after a death; one of them walks
// side to side (aiming at a moving target). The HUD shows the player's damage per second over the last 5 s.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaEvidence.h"
#include "Heroes/ArenaCharacter.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"

bool AArenaGameMode::IsTrainingMap(const UWorld* World)
{
	return World && World->GetMapName().Contains(TEXT("Training"));
}

void AArenaGameMode::StartTraining(int32 HeroIndex)
{
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	if (Defs.Num() == 0) { return; }
	PlayerHeroIndex = Defs.IsValidIndex(HeroIndex) ? HeroIndex : 0;
	Heroes.Reset();
	// the player at the start, facing the dummies
	FVector Start(-2000.f, 0.f, 120.f);
	FRotator Facing(0.f, 0.f, 0.f);
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) { Start = It->GetActorLocation(); Facing = It->GetActorRotation(); }
	BaseA = Start;
	BaseB = Start + FVector(100000.f, 0.f, 0.f);   // no enemy base in the room
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AArenaCharacter* C = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Start, Facing, P))
	{
		C->InitCharacter(Defs[PlayerHeroIndex], 0, 1, false);
		C->HeroIndex = PlayerHeroIndex;
		C->PlayerIndex = 0;
		C->ForceRanks(1);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->Possess(C);
			PC->SetControlRotation(Facing);
			PC->SetViewTarget(C);
		}
		Heroes.Add(C);
	}
	// dummies at the map's points: tag Dummy (standing) or Mover (walks side to side); the heroes in turn
	int32 Next = 0;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		const bool bMover = It->Tags.Contains(TEXT("Mover"));
		if (!bMover && !It->Tags.Contains(TEXT("Dummy"))) { continue; }
		FTrainingDummy D;
		D.Spot = It->GetActorLocation();
		D.Yaw = It->GetActorRotation().Yaw;
		D.bMover = bMover;
		D.Def = (PlayerHeroIndex + 1 + Next++) % Defs.Num();
		TrainingDummies.Add(D);
	}
	for (FTrainingDummy& D : TrainingDummies) { D.Unit = LabSpawn(Defs[D.Def], 1, D.Spot, D.Yaw, false, false); }
	bTraining = true;
	bFountainsOn = false;
	Phase = EArenaPhase::Playing;
	MatchStart = GetWorld()->GetTimeSeconds();
	Announce(this, TEXT("TRAINING CENTER"), true, FLinearColor(0.6f, 0.85f, 1.f));
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=training_start hero=%s dummies=%d"), *Defs[PlayerHeroIndex].Id.ToString(), TrainingDummies.Num());
}

void AArenaGameMode::TrainingKey(int32 Key)
{
	AArenaCharacter* Me = Heroes.Num() > 0 ? Heroes[0].Get() : nullptr;
	if (!bTraining || !Me) { return; }
	switch (Key)
	{
	case 0:
		bTrainNoCooldowns = !bTrainNoCooldowns;
		Announce(this, bTrainNoCooldowns ? TEXT("No cooldowns/mana: ON.") : TEXT("No cooldowns/mana: OFF."), false, FLinearColor(0.6f, 0.85f, 1.f));
		break;
	case 1:
		// straight to the top: every level's XP, every rank the level allows
		Me->AddXp(1.e7f);
		Me->ForceRanks(5);
		Me->RefillVitals();
		Announce(this, TEXT("Level 20, all ranks"), false, FLinearColor(1.f, 0.85f, 0.3f));
		break;
	case 2:
		for (FTrainingDummy& D : TrainingDummies)
		{
			if (D.Unit.IsValid()) { D.Unit->Destroy(); }
			D.Unit = LabSpawn(FArenaDatabase::Get().Heroes[D.Def], 1, D.Spot, D.Yaw, false, false);
			D.DeadSince = -1.f;
		}
		Announce(this, TEXT("Dummies back in place"), false, FLinearColor(0.6f, 0.85f, 1.f));
		break;
	default: break;
	}
}

void AArenaGameMode::TickTraining(float Now, float Dt)
{
	AArenaCharacter* Me = Heroes.Num() > 0 ? Heroes[0].Get() : nullptr;
	if (Me)
	{
		Me->Gold = 99999.f;
		if (bTrainNoCooldowns) { Me->ResetCooldowns(); Me->RefillMana(); }
		if (!Me->IsAlive() && TrainingPlayerDeadSince < 0.f) { TrainingPlayerDeadSince = Now; }
	}
	// the player's hero comes back at the start 2 s after a death (a dummy cannot kill, a fall can)
	if (TrainingPlayerDeadSince > 0.f && Now - TrainingPlayerDeadSince > 2.f)
	{
		TrainingPlayerDeadSince = -1.f;
		StartTrainingPlayerAgain();
	}
	float Dealt = 0.f;
	const FString MyName = Me ? Me->GetDef().DisplayName : FString();
	for (int32 i = 0; i < TrainingDummies.Num(); ++i)
	{
		FTrainingDummy& D = TrainingDummies[i];
		AArenaCharacter* U = D.Unit.Get();
		if (!U || !U->IsAlive())
		{
			if (D.DeadSince < 0.f) { D.DeadSince = Now; }
			if (Now - D.DeadSince > 1.5f)
			{
				D.Unit = LabSpawn(FArenaDatabase::Get().Heroes[D.Def], 1, D.Spot, D.Yaw, false, false);
				D.DeadSince = -1.f;
			}
			continue;
		}
		for (const FArenaDamageEvent& E : U->RecentDamage) { if (Now - E.Time <= 5.f && E.Source == MyName) { Dealt += E.Amount; } }
		// a dummy mends once it is left alone for 3 s (or at a quarter of its health): endless practice
		if (U->GetHealth() < U->GetMaxHealth() && (Now - U->LastDamageTime > 3.f)) { U->RefillVitals(); }
		// the walker: side to side across its spot, 3.5 m each way
		if (D.bMover && !U->IsStunned() && !U->IsDisplaced())
		{
			const float Side = FMath::Sin(Now * 0.9f + i) >= 0.f ? 1.f : -1.f;
			const FVector Goal = D.Spot + FVector(0.f, Side * 350.f, 0.f);
			const FVector To = (Goal - U->GetActorLocation()).GetSafeNormal2D();
			U->AddMovementInput(To, 1.f);
			U->SetActorRotation(FRotator(0.f, D.Yaw, 0.f));
		}
		// knocked far away: back to the spot after 4 s of rest
		if (FVector::Dist2D(U->GetActorLocation(), D.Spot) > 900.f && Now - U->LastDamageTime > 4.f)
		{
			U->SetActorLocation(D.Spot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	TrainingDps = Dealt / 5.f;
}

void AArenaGameMode::StartTrainingPlayerAgain()
{
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	if (Heroes.Num() > 0 && Heroes[0].IsValid()) { Heroes[0]->Destroy(); }
	Heroes.Reset();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (AArenaCharacter* C = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), BaseA, FRotator::ZeroRotator, P))
	{
		C->InitCharacter(Defs[PlayerHeroIndex], 0, 1, false);
		C->HeroIndex = PlayerHeroIndex;
		C->PlayerIndex = 0;
		C->ForceRanks(1);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->Possess(C); PC->SetViewTarget(C); }
		Heroes.Add(C);
	}
}

// -ArenaTrainingDemo (on the training map): the room, casts at a dummy with the cooldowns off, the damage meter, level
// 20 and the dummies mending; screenshots TRAIN_*.png, LAB PASS/FAIL lines, LAB_SUMMARY, then quit.
void AArenaGameMode::TickTrainingDemo(float Now)
{
	const float T = Now - MatchStart;
	AArenaCharacter* Me = Heroes.Num() > 0 ? Heroes[0].Get() : nullptr;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		ARENA_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	AArenaCharacter* Target = nullptr;
	for (const FTrainingDummy& D : TrainingDummies) { if (D.Unit.IsValid() && D.Unit->IsAlive() && !D.bMover && (!Target || (Me && FVector::Dist(D.Unit->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Target->GetActorLocation(), Me->GetActorLocation())))) { Target = D.Unit.Get(); } }
	switch (TrainingDemoStep)
	{
	case 0:
		if (T > 2.5f)
		{
			UIShot(TEXT("TRAIN_01_Room"));
			Check(Me != nullptr && TrainingDummies.Num() >= 3, FString::Printf(TEXT("the training centre starts with the player's hero and %d dummies"), TrainingDummies.Num()));
			TrainingKey(0);
			++TrainingDemoStep;
		}
		break;
	case 1: case 2: case 3: case 4: case 5: case 6:
		// walk up to the nearest dummy and use the abilities on it (cooldowns off)
		if (Me && Target && T > 3.f + (TrainingDemoStep - 1) * 1.1f)
		{
			const int32 Slot = TrainingDemoStep <= 4 ? TrainingDemoStep : 0;
			const FVector To = Target->GetActorLocation() - Me->GetActorLocation();
			// in reach of the ability: a sword at 1.5 m, a shot or a leap from farther
			const float Reach = FMath::Clamp(Me->Ability(Slot).Range * 100.f * 0.7f, 150.f, 600.f);
			if (FMath::Abs(To.Size2D() - Reach) > 50.f) { Me->SetActorLocation(Target->GetActorLocation() - To.GetSafeNormal2D() * Reach, false, nullptr, ETeleportType::TeleportPhysics); }
			Me->SetActorRotation(To.GetSafeNormal2D().Rotation());
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetControlRotation(FRotator(-12.f, To.Rotation().Yaw, 0.f)); }
			Me->AimPoint = Target->GetActorLocation();
			Me->TryCast(Slot);
			++TrainingDemoStep;
		}
		break;
	case 7:
		if (T > 10.f)
		{
			UIShot(TEXT("TRAIN_02_Casts"));
			Check(TrainingDps > 0.f, FString::Printf(TEXT("the damage meter counts the player's damage on the dummies: %.0f per second"), TrainingDps));
			TrainingKey(1);
			++TrainingDemoStep;
		}
		break;
	case 8:
		if (T > 11.f)
		{
			UIShot(TEXT("TRAIN_03_Level20"));
			Check(Me && Me->GetHeroLevel() >= Me->MaxLevel(), FString::Printf(TEXT("F2 gives level %d (max %d)"), Me ? Me->GetHeroLevel() : 0, Me ? Me->MaxLevel() : 0));
			++TrainingDemoStep;
		}
		break;
	case 9:
		if (T > 16.f)
		{
			int32 Full = 0, Alive = 0;
			for (const FTrainingDummy& D : TrainingDummies) { if (D.Unit.IsValid() && D.Unit->IsAlive()) { ++Alive; Full += D.Unit->GetHealth() >= D.Unit->GetMaxHealth() - 1.f ? 1 : 0; } }
			Check(Alive == TrainingDummies.Num() && Full == Alive, FString::Printf(TEXT("the dummies are all standing and mended after a rest: %d alive, %d at full health of %d"), Alive, Full, TrainingDummies.Num()));
			ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			++TrainingDemoStep;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
