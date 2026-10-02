// -ArenaMechLab: the combat and movement rules measured on live units.
//   rounds 1-4  every hero shows the indicator of ability 1..4 (screenshot MECH_Slot<N>), the shapes are checked
//               against the data, then the ability is cast at the indicated point and must land there
//   walls       a swing through a wall does not land, the same swing without it does; a dash stops at a wall
//   contact     a melee bot closes to body contact before it swings, bodies never overlap
// -ArenaCamShots=<file>: one screenshot per line "name x y z pitch yaw fov" (map review, CAM_<name>.png).
// Both print LAB PASS/FAIL lines and LAB_SUMMARY like -ArenaAnimLab.
#include "Game/ArenaGameMode.h"
#include "Heroes/ArenaCharacter.h"
#include "AI/ArenaBotController.h"
#include "AI/ArenaBotBrain.h"
#include "Arena/ArenaIndicator.h"
#include "Abilities/ArenaAbility.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"

static const FVector MechRow(-5250.f, 0.f, 400.f);     // plateau A (top z 300)
static const FVector MechSide(5250.f, 0.f, 400.f);     // plateau B: walls and the melee duel

AArenaCharacter* AArenaGameMode::LabSpawn(const FArenaHeroDef& Def, int32 Team, const FVector& At, float Yaw, bool bMinionDef, bool bBot)
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AArenaCharacter* C = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f), P);
	if (!C) { return nullptr; }
	C->InitCharacter(Def, Team, 1, bMinionDef);
	if (!bMinionDef) { C->ForceRanks(1); }   // every ability learnt (rank 1: the data's base numbers)
	C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (bBot)
	{
		AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
		AI->Difficulty = 2;
		AI->Possess(C);
	}
	else
	{
		// no controller: it would sit in MOVE_None; walking like a possessed unit, so dashes and knocks move it
		C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	return C;
}

AActor* AArenaGameMode::LabWall(const FVector& Center, const FVector& Size)
{
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
	if (!A) { return nullptr; }
	UStaticMeshComponent* M = A->GetStaticMeshComponent();
	M->SetMobility(EComponentMobility::Movable);
	M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	M->SetWorldScale3D(Size / 100.f);
	M->SetCollisionProfileName(TEXT("BlockAll"));
	LabActors.Add(A);
	return A;
}

void AArenaGameMode::StartMechLab()
{
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	for (int32 i = 0; i < Defs.Num(); ++i)
	{
		// the line faces away from the camera (-X): lanes and cones point into the picture
		const FVector At = MechRow + FVector(0.f, (i - (Defs.Num() - 1) * 0.5f) * 420.f, 0.f);
		if (AArenaCharacter* C = LabSpawn(Defs[i], i % 2, At, 180.f, false, false))
		{
			C->HeroIndex = i;
			LabUnits.Add(C);
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			LabIndicators.Add(GetWorld()->SpawnActor<AArenaIndicator>(AArenaIndicator::StaticClass(), At, FRotator::ZeroRotator, P));
		}
	}
	// walls on plateau B early, so the dynamic navmesh has cut around them before the tests
	LabWall(MechSide + FVector(85.f, -700.f, 50.f), FVector(20.f, 300.f, 300.f));      // between the swinger and its target
	LabWall(MechSide + FVector(420.f, 700.f, 100.f), FVector(40.f, 600.f, 400.f));     // in the path of a dash
	if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(MechRow + FVector(1500.f, 0.f, 1700.f), FRotator(-47.f, 180.f, 0.f)))
	{
		Cam->GetCameraComponent()->SetFieldOfView(80.f);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(Cam); }
	}
	LabStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=mechlab_start heroes=%d"), LabUnits.Num());
}

void AArenaGameMode::TickMechLab(float Now)
{
	const float T = Now - LabStart;
	auto Check = [this](bool bOk, const FString& What)
	{
		LabFails += bOk ? 0 : 1;
		UE_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	TArray<AArenaCharacter*> U;
	for (const TWeakObjectPtr<AArenaCharacter>& W : LabUnits) { if (W.IsValid()) { U.Add(W.Get()); } }

	// ---- rounds 1..4: indicators, then cast where they point ----------------------------------------------------
	const float RoundLen = 4.5f;
	const int32 Round = FMath::FloorToInt((T - 1.f) / RoundLen) + 1;           // 1..4 while T in [1, 19)
	const float RT = T - 1.f - (Round - 1) * RoundLen;
	if (Round >= 1 && Round <= 4 && U.Num() == LabIndicators.Num())
	{
		const int32 Slot = Round;
		if (LabStep == 0 && Round != MechRound)
		{
			MechRound = Round;
			LabTargets.Reset(); LabBest.Reset();
			for (int32 i = 0; i < U.Num(); ++i)
			{
				AArenaCharacter* C = U[i];
				const FVector Home = MechRow + FVector(0.f, (i - (U.Num() - 1) * 0.5f) * 420.f, 0.f);
				C->SetActorLocationAndRotation(Home, FRotator(0.f, 180.f, 0.f), false, nullptr, ETeleportType::ResetPhysics);
				C->GetCharacterMovement()->StopMovementImmediately();
				C->RefillVitals();
				const FArenaAbilityDef& Ab = C->GetDef().Abilities[Slot];
				const float Reach = Ab.Archetype == EArenaArchetype::GroundAoE ? FMath::Min(Ab.Range * 100.f * 0.6f, 900.f) : 800.f;
				C->AimPoint = Home + FVector(-Reach, 0.f, -100.f);
				if (LabIndicators[i].IsValid()) { LabIndicators[i]->Show(C, Slot); }
				LabTargets.Add(FVector::ZeroVector);
				LabBest.Add(1.e9f);
			}
			++LabStep;
		}
		else if (LabStep == 1 && RT >= 1.3f)
		{
			UIShot(*FString::Printf(TEXT("MECH_Slot%d"), Slot));
			for (int32 i = 0; i < U.Num(); ++i)
			{
				AArenaIndicator* I = LabIndicators[i].Get();
				const FArenaAbilityDef& Ab = U[i]->GetDef().Abilities[Slot];
				bool bOk = I && I->IsShown();
				FString Shape;
				switch (Ab.Archetype)
				{
				case EArenaArchetype::Melee: bOk &= FMath::IsNearlyEqual(I->AreaRadius, Ab.Range * 100.f, 1.f); Shape = FString::Printf(TEXT("cone r=%.0f"), I->AreaRadius); break;
				case EArenaArchetype::Projectile: bOk &= (Ab.Count > 1 ? I->AreaRadius > 0.f : I->LaneLength > 0.f && I->LaneLength <= Ab.Range * 100.f + 1.f); Shape = FString::Printf(TEXT("lane %.0f x %.0f, cone r=%.0f"), I->LaneLength, I->LaneWidth, I->AreaRadius); break;
				case EArenaArchetype::GroundAoE: bOk &= FMath::IsNearlyEqual(I->RingRadius, Ab.Range * 100.f, 1.f) && FMath::IsNearlyEqual(I->AreaRadius, Ab.Radius * 100.f, 1.f)
					&& FVector::Dist2D(I->TargetPoint(), U[i]->GetActorLocation()) <= Ab.Range * 100.f + 1.f; Shape = FString::Printf(TEXT("range ring %.0f, area %.0f"), I->RingRadius, I->AreaRadius); break;
				case EArenaArchetype::Dash: bOk &= I->LaneLength > 0.f && I->LaneLength <= Ab.Distance * 100.f + 5.f; Shape = FString::Printf(TEXT("path %.0f of %.0f"), I->LaneLength, Ab.Distance * 100.f); break;
				case EArenaArchetype::Buff: bOk &= I->AreaRadius > 0.f; Shape = FString::Printf(TEXT("area %.0f, ring %.0f"), I->AreaRadius, I->RingRadius); break;
				}
				Check(bOk, FString::Printf(TEXT("indicator %s slot %d (%s): %s"), *U[i]->GetDef().Id.ToString(), Slot, *Ab.Name, *Shape));
			}
			++LabStep;
		}
		else if (LabStep == 2 && RT >= 1.7f)
		{
			for (int32 i = 0; i < U.Num(); ++i)
			{
				AArenaIndicator* I = LabIndicators[i].Get();
				if (!I) { continue; }
				LabTargets[i] = U[i]->GetDef().Abilities[Slot].Archetype == EArenaArchetype::Dash ? I->LandingPoint() : I->TargetPoint();
				U[i]->AimPoint = I->TargetPoint();                                 // exactly what the controller does on LMB
				I->Hide();
				const bool bCast = U[i]->TryCast(Slot);
				if (!bCast) { Check(false, FString::Printf(TEXT("cast %s slot %d"), *U[i]->GetDef().Id.ToString(), Slot)); }
			}
			++LabStep;
		}
		else if (LabStep == 3)
		{
			// follow what the casts produce: ground areas where they spawn, dashers where they stop
			for (int32 i = 0; i < U.Num(); ++i)
			{
				const FArenaAbilityDef& Ab = U[i]->GetDef().Abilities[Slot];
				if (Ab.Archetype == EArenaArchetype::GroundAoE)
				{
					for (TActorIterator<AArenaAreaEffect> It(GetWorld()); It; ++It)
					{
						LabBest[i] = FMath::Min(LabBest[i], (float)FVector::Dist2D(It->GetActorLocation(), LabTargets[i]));
					}
				}
				// a dash: how close it came (a neighbour's blow may shove the dasher after it landed: not the dash's fault)
				if (Ab.Archetype == EArenaArchetype::Dash) { LabBest[i] = FMath::Min(LabBest[i], (float)FVector::Dist2D(U[i]->GetActorLocation(), LabTargets[i])); }
			}
			if (RT >= 4.2f)
			{
				for (int32 i = 0; i < U.Num(); ++i)
				{
					const FArenaAbilityDef& Ab = U[i]->GetDef().Abilities[Slot];
					if (Ab.Archetype == EArenaArchetype::GroundAoE)
					{
						Check(LabBest[i] < 30.f, FString::Printf(TEXT("%s %s lands where the indicator showed: %.0f cm off"), *U[i]->GetDef().Id.ToString(), *Ab.Name, LabBest[i]));
					}
					else if (Ab.Archetype == EArenaArchetype::Dash)
					{
						const float Off = LabBest[i];
						Check(Off < 90.f, FString::Printf(TEXT("%s %s ends where the indicator showed: %.0f cm off"), *U[i]->GetDef().Id.ToString(), *Ab.Name, Off));
					}
				}
				LabStep = 0;   // next round
			}
		}
		return;
	}

	// ---- walls and the melee duel -------------------------------------------------------------------------------
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	auto HeroDef = [&Defs](const TCHAR* Id) -> const FArenaHeroDef& { const FArenaHeroDef* D = Defs.FindByPredicate([Id](const FArenaHeroDef& H) { return H.Id == Id; }); return D ? *D : Defs[0]; };
	static TWeakObjectPtr<AArenaCharacter> Swinger, Target, Dasher, Duelist, Dummy;
	static float LastCasts = 0.f;
	switch (LabStep)
	{
	case 0:
		if (T > 19.5f)
		{
			for (AArenaCharacter* C : U) { C->Destroy(); }
			LabUnits.Reset();
			for (const TWeakObjectPtr<AArenaIndicator>& I : LabIndicators) { if (I.IsValid()) { I->Destroy(); } }
			LabIndicators.Reset();
			// swinger | wall | target on plateau B, a dasher facing a wall, a melee bot and its target
			Swinger = LabSpawn(HeroDef(TEXT("Greystone")), 0, MechSide + FVector(0.f, -700.f, 0.f), 0.f, false, false);
			Target = LabSpawn(HeroDef(TEXT("Countess")), 1, MechSide + FVector(170.f, -700.f, 0.f), 180.f, false, false);
			Dasher = LabSpawn(HeroDef(TEXT("Countess")), 0, MechSide + FVector(0.f, 700.f, 0.f), 0.f, false, false);
			if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(MechSide + FVector(-900.f, 0.f, 1300.f), FRotator(-50.f, 0.f, 0.f)))
			{
				Cam->GetCameraComponent()->SetFieldOfView(85.f);
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(Cam); }
			}
			++LabStep;
		}
		break;
	case 1:
		if (T > 20.5f && Swinger.IsValid() && Target.IsValid())
		{
			Swinger->AimPoint = Target->GetActorLocation();
			Swinger->TryCast(0);
			++LabStep;
		}
		break;
	case 2:
		if (T > 21.6f && Swinger.IsValid() && Target.IsValid())
		{
			Check(Target->GetHealth() >= Target->GetMaxHealth() - 0.5f, FString::Printf(TEXT("a swing through a wall does not land: target health %.0f/%.0f"), Target->GetHealth(), Target->GetMaxHealth()));
			for (const TWeakObjectPtr<AActor>& A : LabActors) { if (A.IsValid() && FMath::Abs(A->GetActorLocation().Y - (MechSide.Y - 700.f)) < 1.f) { A->Destroy(); } }
			++LabStep;
		}
		break;
	case 3:
		if (T > 22.8f && Swinger.IsValid() && Target.IsValid())
		{
			Swinger->RefillVitals();
			Swinger->AimPoint = Target->GetActorLocation();
			Swinger->TryCast(0);
			++LabStep;
		}
		break;
	case 4:
		if (T > 23.8f && Target.IsValid() && Dasher.IsValid())
		{
			Check(Target->GetHealth() < Target->GetMaxHealth() - 1.f, FString::Printf(TEXT("the same swing without the wall lands: target health %.0f/%.0f"), Target->GetHealth(), Target->GetMaxHealth()));
			UIShot(TEXT("MECH_Walls"));
			Dasher->AimPoint = Dasher->GetActorLocation() + FVector(1500.f, 0.f, 0.f);
			Dasher->TryCast(1);                                                        // Mroczny skok, 9 m, at a wall 4 m ahead
			++LabStep;
		}
		break;
	case 5:
		if (T > 25.3f && Dasher.IsValid())
		{
			const float Face = MechSide.X + 420.f - 20.f;
			const float Front = Dasher->GetActorLocation().X + Dasher->GetCapsuleComponent()->GetScaledCapsuleRadius();
			FCollisionQueryParams Q(SCENE_QUERY_STAT(LabOverlap), false, Dasher.Get());
			const bool bInside = GetWorld()->OverlapAnyTestByChannel(Dasher->GetActorLocation(), FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Dasher->GetCapsuleComponent()->GetScaledCapsuleRadius() - 2.f, Dasher->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 2.f), Q);
			Check(Front <= Face + 3.f && !bInside, FString::Printf(TEXT("a dash stops at the wall: body front %.0f, wall face %.0f, overlapping=%d"), Front, Face, bInside ? 1 : 0));
			UIShot(TEXT("MECH_DashWall"));
			for (TWeakObjectPtr<AArenaCharacter>* W : { &Swinger, &Target, &Dasher }) { if (W->IsValid()) { (*W)->Destroy(); } }
			// the duel: a melee bot and a target that stands still
			Duelist = LabSpawn(HeroDef(TEXT("Kwang")), 0, MechSide + FVector(-300.f, 1600.f, 0.f), 0.f, false, true);
			Dummy = LabSpawn(HeroDef(TEXT("Sparrow")), 1, MechSide + FVector(600.f, 1600.f, 0.f), 180.f, false, false);
			if (Dummy.IsValid()) { Dummy->ApplyStun(30.f); }
			Phase = EArenaPhase::Playing;                                              // bots think only while playing
			LabBest.Reset();
			LastCasts = 0.f;
			++LabStep;
		}
		break;
	case 6:
		if (Duelist.IsValid() && Dummy.IsValid())
		{
			// every basic swing: how far apart the two bodies are at that moment
			Dummy->RefillVitals();                                                     // a target that lasts the whole test
			if (FMath::Fmod(T, 0.5f) < 0.02f)
			{
				const AArenaBotController* AI = Cast<AArenaBotController>(Duelist->GetController());
				UE_LOG(LogArena, Display, TEXT("LAB duel t=%.2f dist=%.0f ready0=%d cd0=%.2f stunned=%d busy=%d dil=%.2f mode=%d move=%d dummyZ=%.0f alive=%d"), T, FVector::Dist(Duelist->GetActorLocation(), Dummy->GetActorLocation()),
					Duelist->CanCastSlot(0) ? 1 : 0, Duelist->CooldownRemaining(0), Duelist->IsStunned() ? 1 : 0, Duelist->IsBusy() ? 1 : 0, Duelist->CustomTimeDilation, (int32)Duelist->GetCharacterMovement()->MovementMode, AI ? (int32)AI->GetMoveStatus() : -1, Dummy->GetActorLocation().Z, Dummy->IsAlive() ? 1 : 0);
			}
			if (Duelist->SlotCasts[0] > LastCasts)
			{
				LastCasts = Duelist->SlotCasts[0];
				LabBest.Add(FVector::Dist2D(Duelist->GetActorLocation(), Dummy->GetActorLocation()));
			}
			if (T > 31.5f)
			{
				const float Contact = Duelist->GetCapsuleComponent()->GetScaledCapsuleRadius() + Dummy->GetCapsuleComponent()->GetScaledCapsuleRadius();
				float Max = 0.f, Min = 1.e9f;
				for (float D : LabBest) { Max = FMath::Max(Max, D); Min = FMath::Min(Min, D); }
				// (v18: since v17 a bot's swing starts within its blade plus the swing's lunge — the blade reaches the
				// target's body at every swing; the old bound of contact + 40 cm held only when no knock-up moved the target)
				const float Blade = Duelist->Ability(0).Range * 100.f + Dummy->GetCapsuleComponent()->GetScaledCapsuleRadius();
				Check(LabBest.Num() >= 3 && Max <= Blade && Min >= Contact - 3.f,
					FString::Printf(TEXT("a melee bot swings with the target in its blade's reach: %d swings at %.0f..%.0f cm (bodies touch at %.0f, the blade reaches %.0f)"), LabBest.Num(), Min, Max, Contact, Blade));
				UIShot(TEXT("MECH_Contact"));
				++LabStep;
			}
		}
		break;
	case 7:
		if (T > 32.5f)
		{
			UE_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			++LabStep;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}

void AArenaGameMode::StartCamShots(const FString& File)
{
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *File);
	for (const FString& L : Lines)
	{
		TArray<FString> P;
		L.ParseIntoArrayWS(P);
		if (P.Num() < 6 || P[0].StartsWith(TEXT("#"))) { continue; }
		CamNames.Add(P[0]);
		CamPoses.Add(FTransform(FRotator(FCString::Atof(*P[4]), FCString::Atof(*P[5]), 0.f), FVector(FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]))));
		CamFovs.Add(P.Num() > 6 ? FCString::Atof(*P[6]) : 80.f);
	}
	LabStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=camshots file=%s cams=%d"), *File, CamNames.Num());
}

void AArenaGameMode::TickCamShots(float Now)
{
	// camera i is set at 3 + 2i s and shot 1.5 s later
	const float T = Now - LabStart - 3.f;
	const int32 I = LabStep / 2;
	if (I >= CamNames.Num())
	{
		if (T > CamNames.Num() * 2.f + 1.f) { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); }
		return;
	}
	if (LabStep % 2 == 0 && T >= I * 2.f)
	{
		if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamPoses[I].GetLocation(), CamPoses[I].Rotator()))
		{
			Cam->GetCameraComponent()->SetFieldOfView(CamFovs[I]);
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetViewTarget(Cam); }
		}
		++LabStep;
	}
	else if (LabStep % 2 == 1 && T >= I * 2.f + 1.5f)
	{
		UIShot(*FString::Printf(TEXT("CAM_%s"), *CamNames[I]));
		++LabStep;
	}
}
