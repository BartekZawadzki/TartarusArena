#include "Proto/ProtoPlayerController.h"
#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoGameMode.h"
#include "Proto/ProtoHUD.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "UI/ArenaSettings.h"

AProtoPlayerController::AProtoPlayerController()
{
	bShowMouseCursor = true;
}

AProtoCharacter* AProtoPlayerController::Char() const { return Cast<AProtoCharacter>(GetPawn()); }

UInputAction* AProtoPlayerController::MakeAction(EInputActionValueType Type, bool bWhenPaused)
{
	UInputAction* A = NewObject<UInputAction>(this);
	A->ValueType = Type;
	A->bTriggerWhenPaused = bWhenPaused;
	Actions.Add(A);
	return A;
}

void AProtoPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetMenu(EProtoMenu::Main);
}

void AProtoPlayerController::SetMenu(EProtoMenu M)
{
	Menu = M;
	const bool bMenu = M != EProtoMenu::None;
	bShowMouseCursor = bMenu;
	if (bMenu) { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
	else { SetInputMode(FInputModeGameOnly()); }
	SetPause(M == EProtoMenu::Pause || M == EProtoMenu::Intro || (M == EProtoMenu::Controls && MenuBack == EProtoMenu::Pause));
}

void AProtoPlayerController::OnMatchStarted()
{
	MenuBack = EProtoMenu::Main;
	// the controls card first (the game waits for a click), unless the lab drives the match
	SetMenu(bSkipIntro ? EProtoMenu::None : EProtoMenu::Intro);
}

void AProtoPlayerController::Dismiss()
{
	if (Menu == EProtoMenu::Intro) { SetMenu(EProtoMenu::None); }
}

void AProtoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	Mapping = NewObject<UInputMappingContext>(this);
	ActMove = MakeAction(EInputActionValueType::Axis2D);
	ActLook = MakeAction(EInputActionValueType::Axis2D);
	ActLookPad = MakeAction(EInputActionValueType::Axis2D);
	ActJump = MakeAction(EInputActionValueType::Boolean, true);    // also closes the controls card
	ActSprint = MakeAction(EInputActionValueType::Boolean);
	ActSprintHold = MakeAction(EInputActionValueType::Boolean);
	ActSneak = MakeAction(EInputActionValueType::Boolean);
	ActSneakToggle = MakeAction(EInputActionValueType::Boolean);
	ActLight = MakeAction(EInputActionValueType::Boolean, true);   // also the menus' click (paused ones too)
	ActHeavy = MakeAction(EInputActionValueType::Boolean);
	ActDodge = MakeAction(EInputActionValueType::Boolean);
	ActLock = MakeAction(EInputActionValueType::Boolean);
	ActWheel = MakeAction(EInputActionValueType::Axis1D);
	ActMenu = MakeAction(EInputActionValueType::Boolean, true);
	ActHelp = MakeAction(EInputActionValueType::Boolean, true);
	// the mouse: LMB light, RMB heavy, the wheel zoom / switch, the middle button lock-on, thumb buttons dodge / sprint;
	// the keys and a pad (A jump, B sprint / dodge, X light, Y heavy, LB dodge, RB lock, L3 sneak)
	auto Swz = [this]() { return NewObject<UInputModifierSwizzleAxis>(this); };
	auto Neg = [this]() { return NewObject<UInputModifierNegate>(this); };
	FEnhancedActionKeyMapping& W = Mapping->MapKey(ActMove, EKeys::W); W.Modifiers.Add(Swz());
	FEnhancedActionKeyMapping& Sk = Mapping->MapKey(ActMove, EKeys::S); Sk.Modifiers.Add(Swz()); Sk.Modifiers.Add(Neg());
	FEnhancedActionKeyMapping& Ak = Mapping->MapKey(ActMove, EKeys::A); Ak.Modifiers.Add(Neg());
	Mapping->MapKey(ActMove, EKeys::D);
	FEnhancedActionKeyMapping& Stick = Mapping->MapKey(ActMove, EKeys::Gamepad_Left2D); Stick.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	Mapping->MapKey(ActLook, EKeys::Mouse2D);
	FEnhancedActionKeyMapping& Pad = Mapping->MapKey(ActLookPad, EKeys::Gamepad_Right2D); Pad.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	Mapping->MapKey(ActJump, EKeys::SpaceBar); Mapping->MapKey(ActJump, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(ActSprint, EKeys::LeftShift); Mapping->MapKey(ActSprint, EKeys::Gamepad_FaceButton_Right);
	Mapping->MapKey(ActSprintHold, EKeys::ThumbMouseButton2);
	Mapping->MapKey(ActSneak, EKeys::LeftControl);
	Mapping->MapKey(ActSneakToggle, EKeys::C); Mapping->MapKey(ActSneakToggle, EKeys::Gamepad_LeftThumbstick);
	Mapping->MapKey(ActLight, EKeys::LeftMouseButton); Mapping->MapKey(ActLight, EKeys::Gamepad_FaceButton_Left);
	Mapping->MapKey(ActHeavy, EKeys::RightMouseButton); Mapping->MapKey(ActHeavy, EKeys::Gamepad_FaceButton_Top);
	Mapping->MapKey(ActDodge, EKeys::ThumbMouseButton); Mapping->MapKey(ActDodge, EKeys::Gamepad_LeftShoulder);
	Mapping->MapKey(ActLock, EKeys::MiddleMouseButton); Mapping->MapKey(ActLock, EKeys::Tab); Mapping->MapKey(ActLock, EKeys::Gamepad_RightShoulder);
	Mapping->MapKey(ActWheel, EKeys::MouseWheelAxis);
	Mapping->MapKey(ActMenu, EKeys::Escape); Mapping->MapKey(ActMenu, EKeys::Gamepad_Special_Right);
	Mapping->MapKey(ActHelp, EKeys::F1);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindActionValueLambda(ActMove, ETriggerEvent::Triggered, [this](const FInputActionValue& V)
		{
			LastMove = V.Get<FVector2D>();
			if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->Move(LastMove, GetControlRotation()); }
		});
		EIC->BindActionValueLambda(ActMove, ETriggerEvent::Completed, [this](const FInputActionValue&) { LastMove = FVector2D::ZeroVector; });
		EIC->BindActionValueLambda(ActLook, ETriggerEvent::Triggered, [this](const FInputActionValue& V)
		{
			if (Menu != EProtoMenu::None) { return; }
			const FVector2D In = V.Get<FVector2D>() * FArenaSettings::Get().Sensitivity;
			AddYawInput(In.X);
			AddPitchInput(FArenaSettings::Get().bInvertY ? In.Y : -In.Y);
		});
		EIC->BindActionValueLambda(ActLookPad, ETriggerEvent::Triggered, [this](const FInputActionValue& V)
		{
			if (Menu != EProtoMenu::None) { return; }
			// a stick is a rate, not a distance (as the arena): degrees per second, the same at any frame rate
			const FVector2D In = V.Get<FVector2D>() * FArenaSettings::Get().Sensitivity * GetWorld()->GetDeltaSeconds();
			AddYawInput(In.X * 72.f);
			AddPitchInput((FArenaSettings::Get().bInvertY ? In.Y : -In.Y) * 48.f);
		});
		EIC->BindActionValueLambda(ActJump, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			if (Menu == EProtoMenu::Intro) { Dismiss(); return; }
			if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->PressJump(); }
		});
		EIC->BindActionValueLambda(ActJump, ETriggerEvent::Completed, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->ReleaseJump(); } });
		// Shift: a tap dodges (towards the keys held, else backwards), a hold sprints
		EIC->BindActionValueLambda(ActSprint, ETriggerEvent::Started, [this](const FInputActionValue&) { SprintPressedAt = GetWorld()->GetRealTimeSeconds(); });
		EIC->BindActionValueLambda(ActSprint, ETriggerEvent::Triggered, [this](const FInputActionValue&)
		{
			if (AProtoCharacter* C = Char(); C && SprintPressedAt > 0.f && GetWorld()->GetRealTimeSeconds() - SprintPressedAt > 0.2f && !C->bSprinting) { C->SetSprint(true); }
		});
		EIC->BindActionValueLambda(ActSprint, ETriggerEvent::Completed, [this](const FInputActionValue&)
		{
			AProtoCharacter* C = Char();
			if (C && Menu == EProtoMenu::None && SprintPressedAt > 0.f && GetWorld()->GetRealTimeSeconds() - SprintPressedAt <= 0.2f) { DodgeByKeys(); }
			if (C) { C->SetSprint(false); }
			SprintPressedAt = -1.f;
		});
		EIC->BindActionValueLambda(ActSprintHold, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->SetSprint(true); } });
		EIC->BindActionValueLambda(ActSprintHold, ETriggerEvent::Completed, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->SetSprint(false); } });
		EIC->BindActionValueLambda(ActDodge, ETriggerEvent::Started, [this](const FInputActionValue&) { if (Menu == EProtoMenu::None) { DodgeByKeys(); } });
		EIC->BindActionValueLambda(ActSneak, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->SetSneak(true); } });
		EIC->BindActionValueLambda(ActSneak, ETriggerEvent::Completed, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->SetSneak(false); } });
		EIC->BindActionValueLambda(ActSneakToggle, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->ToggleSneak(); } });
		// LMB: a click in a menu, else the light attack; held, the combo keeps going
		EIC->BindActionValueLambda(ActLight, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			if (Menu == EProtoMenu::Intro) { Dismiss(); return; }
			if (Menu != EProtoMenu::None)
			{
				float MX = 0.f, MY = 0.f;
				if (AProtoHUD* HUD = Cast<AProtoHUD>(GetHUD()); HUD && GetMousePosition(MX, MY))
				{
					const AArenaHUD::FMenuHit M = HUD->MenuHitAt(FVector2D(MX, MY));
					if (!M.Action.IsNone()) { MenuAction(M.Action, M.Arg); }
				}
				return;
			}
			LightDownAt = LastLightPress = GetWorld()->GetRealTimeSeconds();
			if (AProtoCharacter* C = Char()) { C->LightAttack(); }
		});
		EIC->BindActionValueLambda(ActLight, ETriggerEvent::Triggered, [this](const FInputActionValue&)
		{
			const float Real = GetWorld()->GetRealTimeSeconds();
			if (Menu != EProtoMenu::None || LightDownAt < 0.f || Real - LightDownAt < 0.3f || Real - LastLightPress < 0.18f) { return; }
			LastLightPress = Real;
			if (AProtoCharacter* C = Char()) { C->LightAttack(); }
		});
		EIC->BindActionValueLambda(ActLight, ETriggerEvent::Completed, [this](const FInputActionValue&) { LightDownAt = -1.f; });
		EIC->BindActionValueLambda(ActHeavy, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->StartHeavy(); } });
		EIC->BindActionValueLambda(ActHeavy, ETriggerEvent::Completed, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->ReleaseHeavy(); } });
		EIC->BindActionValueLambda(ActLock, ETriggerEvent::Started, [this](const FInputActionValue&) { if (Menu == EProtoMenu::None) { ToggleLock(); } });
		EIC->BindActionValueLambda(ActWheel, ETriggerEvent::Started, [this](const FInputActionValue& V)
		{
			if (Menu != EProtoMenu::None) { return; }
			const float Dir = V.Get<float>();
			AProtoCharacter* C = Char();
			if (!C || FMath::IsNearlyZero(Dir)) { return; }
			// locked on: the wheel moves the lock along the enemies; else it zooms the camera
			if (C->IsLocked()) { SwitchLock(Dir); }
			else { C->ArmLength = FMath::Clamp(C->ArmLength - Dir * 45.f, 260.f, 700.f); }
		});
		EIC->BindActionValueLambda(ActMenu, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			const AProtoGameMode* GM = AProtoGameMode::Get(this);
			if (Menu == EProtoMenu::Intro) { Dismiss(); return; }
			if (!GM || GM->Phase != EProtoPhase::Playing) { if (Menu == EProtoMenu::Controls) { SetMenu(MenuBack); } return; }
			if (Menu == EProtoMenu::None) { MenuBack = EProtoMenu::Pause; SetMenu(EProtoMenu::Pause); }
			else if (Menu == EProtoMenu::Controls) { SetMenu(MenuBack); }
			else { SetMenu(EProtoMenu::None); }
		});
		EIC->BindActionValueLambda(ActHelp, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			// F1: the controls card (the game waits while it is open)
			const AProtoGameMode* GM = AProtoGameMode::Get(this);
			if (Menu == EProtoMenu::Intro) { Dismiss(); }
			else if (Menu == EProtoMenu::None && GM && GM->Phase == EProtoPhase::Playing) { SetMenu(EProtoMenu::Intro); }
		});
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Sub->AddMappingContext(Mapping, 0);
	}
}

void AProtoPlayerController::DodgeByKeys()
{
	AProtoCharacter* C = Char();
	if (!C) { return; }
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Dir = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * LastMove.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * LastMove.X;
	C->Dodge(Dir);
}

void AProtoPlayerController::ToggleLock()
{
	AProtoCharacter* C = Char();
	if (!C) { return; }
	if (C->LockTarget.IsValid()) { C->SetLockTarget(nullptr); return; }
	// the enemy nearest to the middle of the view, within 30 m
	AProtoCharacter* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const FVector Eye = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : C->GetActorLocation();
	const FVector View = GetControlRotation().Vector();
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		AProtoCharacter* E = *It;
		if (E == C || !E->IsAlive() || E->Team == C->Team) { continue; }
		const FVector To = E->GetActorLocation() - Eye;
		const float D = To.Size();
		const float Dot = FVector::DotProduct(View, To / FMath::Max(1.f, D));
		if (D > 3000.f || Dot < 0.5f) { continue; }
		const float Score = D * (2.f - Dot);
		if (Score < BestScore) { BestScore = Score; Best = E; }
	}
	C->SetLockTarget(Best);
}

void AProtoPlayerController::SwitchLock(float Dir)
{
	AProtoCharacter* C = Char();
	AProtoCharacter* Cur = C ? C->LockTarget.Get() : nullptr;
	if (!C || !Cur) { return; }
	// the next enemy around the player, by the angle from the current one (the wheel's way: right or left)
	const FVector Me = C->GetActorLocation();
	const float CurYaw = (Cur->GetActorLocation() - Me).Rotation().Yaw;
	AProtoCharacter* Best = nullptr;
	float BestAngle = 999.f;
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		AProtoCharacter* E = *It;
		if (E == C || E == Cur || !E->IsAlive() || E->Team == C->Team || FVector::Dist(E->GetActorLocation(), Me) > 3000.f) { continue; }
		float Delta = FMath::FindDeltaAngleDegrees(CurYaw, (E->GetActorLocation() - Me).Rotation().Yaw);
		if (Dir < 0.f) { Delta = -Delta; }
		if (Delta <= 0.f) { Delta += 360.f; }
		if (Delta < BestAngle) { BestAngle = Delta; Best = E; }
	}
	if (Best) { C->SetLockTarget(Best); }
}

void AProtoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	AProtoCharacter* C = Char();
	if (!C) { return; }
	// the attacks go where the camera looks
	C->SetAimYaw(GetControlRotation().Yaw);
	// the lock-on: the camera turns to the target (the body circles it: the 8-way jog)
	if (AProtoCharacter* T = C->LockTarget.Get())
	{
		if (!T->IsAlive() || FVector::Dist(T->GetActorLocation(), C->GetActorLocation()) > 3500.f) { C->SetLockTarget(nullptr); return; }
		const FVector Eye = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : C->GetActorLocation();
		FRotator Want = (T->GetActorLocation() - FVector(0.f, 0.f, 40.f) - Eye).Rotation();
		Want.Pitch = FMath::Clamp(Want.Pitch, -35.f, 10.f);
		SetControlRotation(FMath::RInterpTo(GetControlRotation(), Want, DeltaTime, 9.f));
	}
}

void AProtoPlayerController::MenuAction(FName Action, int32 Arg)
{
	AProtoGameMode* GM = AProtoGameMode::Get(this);
	const FString A = Action.ToString();
	if (!GM) { return; }
	if (A == TEXT("ProtoDuel")) { GM->StartMatch(EProtoMode::Duel); }
	else if (A == TEXT("ProtoTwo")) { GM->StartMatch(EProtoMode::TwoBots); }
	else if (A == TEXT("ProtoSandbox")) { GM->StartMatch(EProtoMode::Sandbox); }
	else if (A == TEXT("ProtoDiff")) { GM->Difficulty = FMath::Clamp(Arg, 0, 2); }
	else if (A == TEXT("ProtoControls")) { MenuBack = Menu; SetMenu(EProtoMenu::Controls); }
	else if (A == TEXT("ProtoBack")) { SetMenu(MenuBack); }
	else if (A == TEXT("ProtoResume")) { SetMenu(EProtoMenu::None); }
	else if (A == TEXT("ProtoStart")) { SetMenu(EProtoMenu::None); }
	else if (A == TEXT("ProtoRestart")) { SetPause(false); GM->StartMatch(GM->Mode); }
	else if (A == TEXT("ProtoMenu")) { SetPause(false); GM->BackToMenu(); MenuBack = EProtoMenu::Main; SetMenu(EProtoMenu::Main); }
	else if (A == TEXT("ProtoExit")) { SetPause(false); UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Arena"), true); }
}
