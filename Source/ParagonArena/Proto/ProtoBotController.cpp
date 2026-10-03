#include "Proto/ProtoBotController.h"
#include "Game/ArenaEvidence.h"
#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogProtoBot, Log, All);

AProtoBotController::AProtoBotController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AProtoBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (AProtoCharacter* C = Cast<AProtoCharacter>(InPawn))
	{
		C->bIsBot = true;
		C->GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths = true;   // the anim blueprint moves with acceleration
	}
}

AProtoCharacter* AProtoBotController::Me() const { return Cast<AProtoCharacter>(GetPawn()); }

AProtoCharacter* AProtoBotController::FindTarget(AProtoCharacter* Self) const
{
	AProtoCharacter* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		AProtoCharacter* C = *It;
		if (C == Self || !C->IsAlive() || C->Team == Self->Team) { continue; }
		const float D = FVector::Dist(C->GetActorLocation(), Self->GetActorLocation());
		if (D < BestD) { BestD = D; Best = C; }
	}
	return Best;
}

bool AProtoBotController::Sees(const AProtoCharacter* Self, const AProtoCharacter* T) const
{
	if (!Self || !T || !T->IsAlive()) { return false; }
	const FVector To = T->GetActorLocation() - Self->GetActorLocation();
	const float D = To.Size();
	if (D > 2800.f) { return false; }
	const float Front = FVector::DotProduct(Self->GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D());
	// the sneak: heard only within 3.5 m, seen in front within 12 m; running: seen in front within 28 m, heard within 9 m
	if (T->bSneaking) { if (D > 350.f && !(Front > 0.55f && D < 1200.f)) { return false; } }
	else if (Front < 0.2f && D > 900.f) { return false; }
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ProtoSight), false, Self);
	Q.AddIgnoredActor(T);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Self->GetActorLocation() + FVector(0.f, 0.f, 60.f), T->GetActorLocation() + FVector(0.f, 0.f, 60.f), ECC_Visibility, Q);
}

void AProtoBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AProtoCharacter* Self = Me();
	if (!Self || !Self->IsAlive()) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	AProtoCharacter* F = Foe.Get();
	// between the thinks: the timed moves — a read dodge, the heavy's release, the combo's presses, the air slashes, the
	// circling steps (a move needs its input every frame)
	if (DodgeAt > 0.f && Now >= DodgeAt)
	{
		DodgeAt = -1.f;
		if (Self->Dodge(DodgeDir)) { ++Dodges; ++ReadDodges; }
	}
	if (HeavyReleaseAt > 0.f && Now >= HeavyReleaseAt) { HeavyReleaseAt = -1.f; Self->ReleaseHeavy(); }
	if (DashStrikeAt > 0.f && Now >= DashStrikeAt) { DashStrikeAt = -1.f; if (F) { Self->SetAimAt(F->GetActorLocation()); } Self->LightAttack(); }
	if (ComboLeft > 0 && Now >= NextComboPress)
	{
		--ComboLeft;
		NextComboPress = Now + 0.2f;
		if (F) { Self->SetAimAt(F->GetActorLocation()); }
		Self->LightAttack();
	}
	if (AirLeft > 0 && Now >= AirSlashAt)
	{
		--AirLeft;
		AirSlashAt = Now + 0.26f;
		if (F) { Self->SetAimAt(F->GetActorLocation()); }
		Self->LightAttack();
	}
	if (F && F->IsAlive()) { ReadThreat(Self, F, Now, FVector::Dist2D(F->GetActorLocation(), Self->GetActorLocation())); }
	if (F && Now < StrafeUntil && Self->CanAct())
	{
		const FRotator Frame(0.f, (F->GetActorLocation() - Self->GetActorLocation()).Rotation().Yaw, 0.f);
		Self->Move(FVector2D(StrafeSign * 0.85f, StrafeFwd), Frame);
	}
	if (Now < NextThink) { return; }
	NextThink = Now + Reaction() * FMath::FRandRange(0.8f, 1.2f);

	AProtoCharacter* T = FindTarget(Self);
	const bool bSees = T && Sees(Self, T);
	if (bSees || (T && Now - Self->LastHitAt < 1.f && Self->LastAttacker.Get() == T))
	{
		if (!Self->bAlerted) { ARENA_LOG(LogProtoBot, Display, TEXT("PROTO t=%.1f evt=spotted bot=%s dist=%.0f sneaking=%d"), Now, *Self->GetName(), FVector::Dist(Self->GetActorLocation(), T->GetActorLocation()), T->bSneaking ? 1 : 0); }
		LastSeen = Now;
		LastKnown = T->GetActorLocation();
		Self->bAlerted = true;
		Self->AlertedAt = Now;
	}
	const bool bHunting = T && Self->bAlerted && Now - LastSeen < 6.f;
	bSearching = false;
	if (!bHunting)
	{
		Foe = nullptr;
		Self->SetLockTarget(nullptr);
		Self->SetSprint(false);
		StrafeUntil = -1.f;
		// lost from sight: go and look where it was last seen
		if (T && LastSeen > 0.f && Now - LastSeen < 16.f && FVector::Dist2D(Self->GetActorLocation(), LastKnown) > 200.f)
		{
			bSearching = true;
			if (Now >= NextSearch) { NextSearch = Now + 2.f; MoveToLocation(LastKnown, 120.f, true, true, false, true); }
			return;
		}
		// the fight modes: a hunt, not a stroll — it heads for where the enemy roughly is (a guess within 12 m, no sight
		// through walls: it still has to see the enemy, and a sneaking enemy can slip past it)
		if (!bPassive && T)
		{
			bSearching = true;
			if (Now >= NextSearch)
			{
				NextSearch = Now + FMath::FRandRange(5.f, 8.f);
				FVector Goal = T->GetActorLocation();
				FNavLocation Nav;
				if (UNavigationSystemV1* NS = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); NS && NS->GetRandomReachablePointInRadius(Goal, 1200.f, Nav)) { Goal = Nav.Location; }
				MoveToLocation(Goal, 150.f, true, true, false, true);
			}
			return;
		}
		// the sandbox: walk the patrol at a stroll
		const AProtoGameMode* GM = AProtoGameMode::Get(this);
		if (GM && GM->PatrolPoints.Num() > 0 && Now >= NextPatrol)
		{
			NextPatrol = Now + FMath::FRandRange(5.f, 9.f);
			PatrolIndex = FMath::RandRange(0, GM->PatrolPoints.Num() - 1);
			MoveToLocation(GM->PatrolPoints[PatrolIndex], 120.f, true, true, false, true);
		}
		return;
	}
	Foe = T;
	SetFocus(T);
	const FVector To = T->GetActorLocation() - Self->GetActorLocation();
	const float Dist = To.Size2D();
	// a target on a roof or a crate above: a jump (the mantle climbs the rest); a launched target: the air combo
	if (To.Z > 110.f && To.Z < 420.f && Dist < 420.f && Now >= NextJump && Self->CanAct() && !T->bLaunched)
	{
		NextJump = Now + 1.6f;
		++Jumps;
		Self->PressJump();
		FTimerHandle H;
		FTimerDelegate D = FTimerDelegate::CreateWeakLambda(this, [this]() { if (AProtoCharacter* S = Me()) { S->PressJump(); } });
		GetWorldTimerManager().SetTimer(H, D, 0.42f, false);   // the second jump at the apex
	}
	const bool bFree = Self->CanAct() || (Self->Attacking == EProtoAttack::Heavy && Now >= Self->CancelAt && !Self->bLaunched && Now >= Self->StaggerUntil);
	if (!bPassive && T->bLaunched && Dist < 420.f && bFree && !Self->GetCharacterMovement()->IsFalling() && Difficulty >= 1 && FMath::FRand() < Skill())
	{
		// the juggle: after it into the air, slash while it hangs
		++AirCombos;
		++Jumps;
		Self->PressJump();
		AirLeft = Difficulty >= 2 ? 3 : 2;
		AirSlashAt = Now + 0.22f;
		NextAction = Now + 1.2f;
		return;
	}
	// a target below while the bot is in the air over it: the slam
	if (!bPassive && Self->GetCharacterMovement()->IsFalling() && To.Z < -150.f && Dist < 220.f && Self->Stamina > 10.f && AirLeft <= 0)
	{
		++Plunges;
		Self->StartHeavy();
	}
	if (bHades && !bPassive && Self->CanAct())
	{
		// prototype 2: the stone at range (a lead on the target for the harder bot), the dash-strike to close in, the
		// call when the gauge is full and the player near
		if (Self->Wrath >= AProtoCharacter::Tuning().WrathMax && Dist < 420.f) { ++Calls; Self->CallWrath(); return; }
		if (Self->CastAmmo > 0 && Dist > 450.f && Dist < 1300.f && Sees(Self, T) && FMath::FRand() < 0.4f + 0.5f * Skill())
		{
			const float Lead = Difficulty >= 2 ? Dist / AProtoCharacter::Tuning().CastSpeed : 0.f;
			Self->SetAimAt(T->GetActorLocation() + T->GetVelocity() * Lead);
			++Casts;
			Self->CastStone();
			return;
		}
		if (Dist > 330.f && Dist < 650.f && Self->Stamina > 40.f && FMath::FRand() < 0.3f + 0.3f * Skill())
		{
			if (Self->Dodge(To)) { ++Dodges; ++DashStrikes; DashStrikeAt = Now + 0.12f; NextAction = Now + 0.8f; return; }
		}
	}
	if (Dist > 420.f)
	{
		// closing in, locked off: a sprint from far, a running strike when it arrives
		Self->SetLockTarget(nullptr);
		StrafeUntil = -1.f;
		Self->SetSprint(!bHades && Dist > 900.f && Self->Stamina > 35.f);   // Hades has no sprint (the same keys as the player)
		if (!bPassive && Self->bSprinting && Dist < 600.f && Self->Stamina > 20.f && Self->CanAct())
		{
			++SprintAttacks;
			Self->SetAimAt(T->GetActorLocation());
			Self->LightAttack();
			return;
		}
		MoveToActor(T, 150.f, true, true, true, nullptr, true);
		return;
	}
	// in the fight: locked on (the 8-way jog), circling
	StopMovement();
	Self->SetSprint(false);
	if (!bHades) { Self->SetLockTarget(T); }
	Engage(Self, T, Now, Dist);
}

void AProtoBotController::ReadThreat(AProtoCharacter* Self, AProtoCharacter* T, float Now, float Dist)
{
	if (Dist > 420.f || DodgeAt > 0.f || Self->bLaunched) { return; }
	const FVector Away = (Self->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(Away, FVector::UpVector) * StrafeSign;
	// how early before the blow it dodges: the harder bot aims inside the perfect window
	const float Lead = Difficulty >= 2 ? FMath::FRandRange(0.03f, 0.12f) : FMath::FRandRange(0.06f, 0.24f);
	auto Plan = [&](float HitAt, float Ready)
	{
		const float At = FMath::Max(Ready, HitAt - Lead);
		if (At > HitAt + 0.01f) { return; }   // too late: the blow lands first
		DodgeAt = At;
		DodgeDir = FMath::FRand() < 0.5f ? Side : Away + Side * 0.6f;
		StrafeSign = -StrafeSign;
	};
	// a swing started: seen at once, but the body moves only after the reaction time — a quick jab lands before it,
	// a heavy or a running strike does not
	if (T->IsAttacking() && !T->bCharging && T->AttackStart > LastReadSwing && T->HitAt > Now)
	{
		LastReadSwing = T->AttackStart;
		const bool bBig = T->Attacking == EProtoAttack::Heavy || T->Attacking == EProtoAttack::Sprint;
		const float Ready = T->AttackStart + Reaction();
		// a blow it can still react to (a heavy, a running strike; a jab only for the quickest bot)
		if (Ready <= T->HitAt && FMath::FRand() < Skill() * (bBig ? 0.9f : 0.5f)) { Plan(T->HitAt, Ready); }
		// a jab it cannot: a combo is foreseen instead — out before the next swing lands
		else if (T->Attacking == EProtoAttack::Light && T->ComboStep < 3 && FMath::FRand() < Skill() * 0.7f) { Plan(T->CancelAt + 0.16f, Ready); }
		return;
	}
	// a charge growing (seen after the reaction time): step out of it, or, close, hit first — a blow breaks it
	if (T->bCharging && T->ChargeStart > LastReadCharge && Now - T->ChargeStart > Reaction())
	{
		LastReadCharge = T->ChargeStart;
		if (FMath::FRand() >= Skill()) { return; }
		if (!bPassive && Dist < 250.f && Difficulty >= 1 && FMath::FRand() < 0.5f && (Self->CanAct() || Self->Attacking == EProtoAttack::Light))
		{
			++Interrupts;
			Self->SetAimAt(T->GetActorLocation());
			Self->LightAttack();
		}
		else
		{
			DodgeAt = Now + 0.02f;
			DodgeDir = Side + Away * 0.5f;
		}
	}
}

void AProtoBotController::Engage(AProtoCharacter* Self, AProtoCharacter* T, float Now, float Dist)
{
	const float Sk = Skill();
	if (Now < NextAction || !Self->CanAct() || DodgeAt > 0.f) { return; }
	Self->SetAimAt(T->GetActorLocation());
	// low on health or stamina: give ground, circle, wait for the stamina
	if (Self->Health < 28.f || Self->Stamina < 18.f)
	{
		if (FMath::FRand() < 0.4f && Self->Dodge((Self->GetActorLocation() - T->GetActorLocation()))) { ++Dodges; }
		StrafeFwd = -0.6f;
		StrafeUntil = Now + 0.9f;
		NextAction = Now + 1.f;
		return;
	}
	if (bPassive)
	{
		StrafeFwd = Dist > 260.f ? 0.4f : -0.2f;
		StrafeUntil = Now + 1.2f;
		NextAction = Now + 1.2f;
		return;
	}
	// out of a swing's reach (with its lunge): step in while circling
	if (Dist > 380.f)
	{
		StrafeFwd = 0.9f;
		StrafeUntil = Now + 0.4f;
		NextAction = Now + 0.25f;
		return;
	}
	// the attack: a combo of 1-3, a quick heavy, a charge (a full one launches: then the juggle), or circling
	const float R = FMath::FRand();
	StrafeUntil = -1.f;
	if (R < 0.48f)
	{
		ComboLeft = FMath::RandRange(Difficulty >= 1 ? 2 : 1, 3);
		NextComboPress = Now;
		Lights += ComboLeft;
		NextAction = Now + 0.45f + 0.32f * ComboLeft;
	}
	else if (R < 0.62f)
	{
		Self->StartHeavy();
		++Heavies;
		if (bHades) { ++Smashes; }
		HeavyReleaseAt = Now + 0.1f;          // a tap: the quick heavy (from above: the smash around)
		NextAction = Now + 0.9f;
	}
	else if (R < 0.62f + 0.14f * (0.5f + Sk))
	{
		Self->StartHeavy();
		++Heavies;
		const bool bFull = Difficulty >= 1 && FMath::FRand() < Sk;
		if (bFull) { ++Launchers; }
		HeavyReleaseAt = Now + (bFull ? 1.05f : FMath::FRandRange(0.4f, 0.8f));
		NextAction = HeavyReleaseAt + 0.8f;
	}
	else
	{
		// circle the player (a feint), in or out a little
		StrafeSign = FMath::FRand() < 0.5f ? 1.f : -1.f;
		StrafeFwd = Dist > 220.f ? 0.3f : -0.3f;
		StrafeUntil = Now + FMath::FRandRange(0.6f, 1.3f);
		NextAction = StrafeUntil;
	}
}
