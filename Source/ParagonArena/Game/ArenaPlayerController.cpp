#include "Game/ArenaPlayerController.h"
#include "Game/ArenaEvidence.h"
#include "Game/ArenaGameMode.h"
#include "InputKeyEventArgs.h"
#include "Heroes/ArenaCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ArenaHUD.h"
#include "Core/ArenaCore.h"
#include "GameFramework/GameUserSettings.h"
#include "Arena/ArenaIndicator.h"
#include "Abilities/ArenaAbility.h"
#include "UI/ArenaSettings.h"
#include "UI/ArenaIconStudio.h"
#include "Arena/ArenaHitSound.h"
#include "Misc/ConfigCacheIni.h"
#include "Game/ArenaGameState.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "GameFramework/CharacterMovementComponent.h"

static bool GNetGuestDone = false;   // the guest test wrote its summary: a return to the menu (the host left) ends the process

UInputAction* AArenaPlayerController::MakeAction(EInputActionValueType Type)
{
	UInputAction* A = NewObject<UInputAction>(this);
	A->ValueType = Type;
	Actions.Add(A);
	return A;
}

AArenaCharacter* AArenaPlayerController::Hero() const { return Cast<AArenaCharacter>(GetPawn()); }

void AArenaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	Mapping = NewObject<UInputMappingContext>(this);
	// the actions; their keys come from the settings (rebinding) and the gamepad, see RebuildKeyMap
	UInputAction* Move = ActMove = MakeAction(EInputActionValueType::Axis2D);
	UInputAction* Look = ActLook = MakeAction(EInputActionValueType::Axis2D);
	ActLookPad = MakeAction(EInputActionValueType::Axis2D);
	UInputAction* Jump = ActJump = MakeAction(EInputActionValueType::Boolean);
	UInputAction* Basic = ActBasic = MakeAction(EInputActionValueType::Boolean);
	Basic->bTriggerWhenPaused = true;    // the pause menu's buttons are clicked while the game is paused
	UInputAction* Cancel = ActCancel = MakeAction(EInputActionValueType::Boolean);
	Cancel->bTriggerWhenPaused = true;
	UInputAction* NumActions[6];
	for (int32 i = 0; i < 6; ++i) { NumActions[i] = ActNum[i] = MakeAction(EInputActionValueType::Boolean); }
	UInputAction* ShopAny = ActShopAny = MakeAction(EInputActionValueType::Boolean);
	UInputAction* Score = ActScore = MakeAction(EInputActionValueType::Boolean);
	UInputAction* Pause = ActPause = MakeAction(EInputActionValueType::Boolean);
	Pause->bTriggerWhenPaused = true;
	UInputAction* Quit = ActQuit = MakeAction(EInputActionValueType::Boolean);
	Quit->bTriggerWhenPaused = true;
	UInputAction* Restart = ActRestart = MakeAction(EInputActionValueType::Boolean);
	UInputAction* FActions[3];
	for (int32 i = 0; i < 3; ++i) { FActions[i] = ActF[i] = MakeAction(EInputActionValueType::Boolean); }
	UInputAction* Quality = ActQuality = MakeAction(EInputActionValueType::Boolean);
	Quality->bTriggerWhenPaused = true;
	UInputAction* Shop = ActShop = MakeAction(EInputActionValueType::Boolean);
	UInputAction* Revive = ActRevive = MakeAction(EInputActionValueType::Boolean);
	UInputAction* MActions[3];
	for (int32 i = 0; i < 3; ++i) { MActions[i] = ActMin[i] = MakeAction(EInputActionValueType::Boolean); }
	ActPing = MakeAction(EInputActionValueType::Boolean);
	RebuildKeyMap();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(Move, ETriggerEvent::Triggered, this, &AArenaPlayerController::OnMove);
		EIC->BindAction(Look, ETriggerEvent::Triggered, this, &AArenaPlayerController::OnLook);
		EIC->BindAction(ActLookPad, ETriggerEvent::Triggered, this, &AArenaPlayerController::OnLookPad);
		EIC->BindAction(ActPing, ETriggerEvent::Started, this, &AArenaPlayerController::OnPing);
		EIC->BindAction(Jump, ETriggerEvent::Started, this, &AArenaPlayerController::OnJump);
		EIC->BindActionValueLambda(Basic, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			if (AArenaHUD* HUD = Cast<AArenaHUD>(GetHUD()))
			{
				float MX = 0.f, MY = 0.f;
				if (SimMouse.IsSet()) { MX = SimMouse->X; MY = SimMouse->Y; }
				if (bShowMouseCursor && (SimMouse.IsSet() || GetMousePosition(MX, MY)))
				{
					const AArenaHUD::FMenuHit M = HUD->MenuHitAt(FVector2D(MX, MY));
					if (!M.Action.IsNone()) { MenuAction(M.Action, M.Arg); return; }
				}
			}
			if (Menu != EArenaMenu::None || IsPaused()) { return; }   // a click on a menu's empty space does nothing
			if (Spectating.IsValid()) { SpectateNext(); return; }
			if (bShopOpen) { OnClick(); return; }
			if (AimSlot >= 0) { bAimByHold = false; CastAimed(); return; }   // LMB casts the aimed ability (any mode)
			bBasicHeld = true; OnSlot(0);
		});
		EIC->BindActionValueLambda(Cancel, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			if (Menu != EArenaMenu::None || IsPaused()) { return; }
			if (bShopOpen) { if (AArenaHUD* HUD = Cast<AArenaHUD>(GetHUD())) { BuyItem(HUD->ShopHoveredItem()); } return; }   // RMB in the shop buys the row under the mouse
			CancelAim();
		});
		EIC->BindActionValueLambda(Basic, ETriggerEvent::Completed, [this](const FInputActionValue&) { bBasicHeld = false; });
		for (int32 i = 0; i < 6; ++i) { EIC->BindActionValueLambda(NumActions[i], ETriggerEvent::Started, [this, i](const FInputActionValue&) { OnNumber(i + 1); }); }
		for (int32 i = 0; i < 4; ++i) { EIC->BindActionValueLambda(NumActions[i], ETriggerEvent::Completed, [this, i](const FInputActionValue&) { ReleaseAbility(i + 1); }); }
		EIC->BindAction(ShopAny, ETriggerEvent::Started, this, &AArenaPlayerController::OnShopAnywhere);
		EIC->BindAction(Score, ETriggerEvent::Triggered, this, &AArenaPlayerController::OnScoreboard);
		EIC->BindActionValueLambda(Score, ETriggerEvent::Completed, [this](const FInputActionValue&) { bScoreboard = false; });
		EIC->BindAction(Pause, ETriggerEvent::Started, this, &AArenaPlayerController::OnPause);
		EIC->BindAction(Quit, ETriggerEvent::Started, this, &AArenaPlayerController::OnQuit);
		EIC->BindAction(Restart, ETriggerEvent::Started, this, &AArenaPlayerController::OnRestart);
		for (int32 i = 0; i < 3; ++i) { EIC->BindActionValueLambda(FActions[i], ETriggerEvent::Started, [this, i](const FInputActionValue&) { OnDifficulty(i); }); }
		EIC->BindAction(Quality, ETriggerEvent::Started, this, &AArenaPlayerController::OnQuality);
		EIC->BindAction(Shop, ETriggerEvent::Started, this, &AArenaPlayerController::OnShop);
		EIC->BindAction(Revive, ETriggerEvent::Started, this, &AArenaPlayerController::OnRevive);
		const int32 Minutes[3] = { 5, 10, 15 };
		for (int32 i = 0; i < 3; ++i) { const int32 M = Minutes[i]; EIC->BindActionValueLambda(MActions[i], ETriggerEvent::Started, [this, M](const FInputActionValue&) { OnMatchMinutes(M); }); }
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Sub->AddMappingContext(Mapping, 0);
	}
}

void AArenaPlayerController::RebuildKeyMap()
{
	if (!Mapping || !ActMove) { return; }
	Mapping->UnmapAll();
	const FArenaSettings& St = FArenaSettings::Get();
	// movement: the four keys make one 2D value (W/S on Y), the left stick is one already
	FEnhancedActionKeyMapping& W = Mapping->MapKey(ActMove, St.KeyFor(TEXT("Forward"))); W.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	FEnhancedActionKeyMapping& Sb = Mapping->MapKey(ActMove, St.KeyFor(TEXT("Back"))); Sb.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this)); Sb.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	FEnhancedActionKeyMapping& A = Mapping->MapKey(ActMove, St.KeyFor(TEXT("Left"))); A.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	Mapping->MapKey(ActMove, St.KeyFor(TEXT("Right")));
	FEnhancedActionKeyMapping& Stick = Mapping->MapKey(ActMove, EKeys::Gamepad_Left2D); Stick.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	Mapping->MapKey(ActLook, EKeys::Mouse2D);
	FEnhancedActionKeyMapping& Pad = Mapping->MapKey(ActLookPad, EKeys::Gamepad_Right2D); Pad.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	// the gamepad (Smite on a pad): A jump, RT attack, LT cancel, RB / LB / X / Y abilities, the d-pad potions, ping
	// and revive, B shop / recall, Start pause, Back scores
	Mapping->MapKey(ActJump, St.KeyFor(TEXT("Jump"))); Mapping->MapKey(ActJump, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(ActBasic, EKeys::LeftMouseButton); Mapping->MapKey(ActBasic, EKeys::Gamepad_RightTrigger);
	Mapping->MapKey(ActCancel, EKeys::RightMouseButton); Mapping->MapKey(ActCancel, EKeys::Gamepad_LeftTrigger);
	const FName Nums[6] = { TEXT("Ability1"), TEXT("Ability2"), TEXT("Ability3"), TEXT("Ability4"), TEXT("PotionHp"), TEXT("PotionMana") };
	const FKey PadNums[6] = { EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down };
	for (int32 i = 0; i < 6; ++i) { Mapping->MapKey(ActNum[i], St.KeyFor(Nums[i])); Mapping->MapKey(ActNum[i], PadNums[i]); }
	Mapping->MapKey(ActShopAny, St.KeyFor(TEXT("ShopAny")));
	Mapping->MapKey(ActScore, St.KeyFor(TEXT("Scoreboard"))); Mapping->MapKey(ActScore, EKeys::Gamepad_Special_Left);
	Mapping->MapKey(ActPause, EKeys::Escape); Mapping->MapKey(ActPause, EKeys::Gamepad_Special_Right);
	Mapping->MapKey(ActQuit, EKeys::Q);
	Mapping->MapKey(ActRestart, EKeys::R);
	const FKey Fs[3] = { EKeys::F1, EKeys::F2, EKeys::F3 };
	for (int32 i = 0; i < 3; ++i) { Mapping->MapKey(ActF[i], Fs[i]); }
	Mapping->MapKey(ActQuality, EKeys::F8);
	Mapping->MapKey(ActShop, St.KeyFor(TEXT("Shop"))); Mapping->MapKey(ActShop, EKeys::Gamepad_FaceButton_Right);
	Mapping->MapKey(ActRevive, St.KeyFor(TEXT("Revive"))); Mapping->MapKey(ActRevive, EKeys::Gamepad_DPad_Right);
	const FKey Ms[3] = { EKeys::F5, EKeys::F6, EKeys::F7 };
	for (int32 i = 0; i < 3; ++i) { Mapping->MapKey(ActMin[i], Ms[i]); }
	Mapping->MapKey(ActPing, St.KeyFor(TEXT("Ping"))); Mapping->MapKey(ActPing, EKeys::MiddleMouseButton); Mapping->MapKey(ActPing, EKeys::Gamepad_DPad_Left);
	if (UEnhancedInputLocalPlayerSubsystem* Sub = GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()) : nullptr)
	{
		FModifyContextOptions Opt;
		Opt.bForceImmediately = true;
		Sub->RequestRebuildControlMappings(Opt);
	}
}

bool AArenaPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (bTypingAddress && Params.Event == IE_Pressed && !Params.Key.IsMouseButton())
	{
		const FString Name = Params.Key.GetFName().ToString();
		if (Params.Key == EKeys::Enter) { bTypingAddress = false; MenuAction(TEXT("LanJoin"), 0); }
		else if (Params.Key == EKeys::Escape) { bTypingAddress = false; }
		else if (Params.Key == EKeys::BackSpace) { JoinAddress.LeftChopInline(1); }
		else if (Params.Key == EKeys::Period || Params.Key == EKeys::Decimal) { JoinAddress += TEXT("."); }
		else if (Name.StartsWith(TEXT("NumPad")) && Name.Len() == 7) { JoinAddress += Name.Right(1); }
		else if (Params.Key == EKeys::Zero) { JoinAddress += TEXT("0"); } else if (Params.Key == EKeys::One) { JoinAddress += TEXT("1"); } else if (Params.Key == EKeys::Two) { JoinAddress += TEXT("2"); }
		else if (Params.Key == EKeys::Three) { JoinAddress += TEXT("3"); } else if (Params.Key == EKeys::Four) { JoinAddress += TEXT("4"); } else if (Params.Key == EKeys::Five) { JoinAddress += TEXT("5"); }
		else if (Params.Key == EKeys::Six) { JoinAddress += TEXT("6"); } else if (Params.Key == EKeys::Seven) { JoinAddress += TEXT("7"); } else if (Params.Key == EKeys::Eight) { JoinAddress += TEXT("8"); }
		else if (Params.Key == EKeys::Nine) { JoinAddress += TEXT("9"); }
		else if (Name.Len() == 1 && FChar::IsAlpha(Name[0])) { JoinAddress += Name.ToLower(); }
		JoinAddress = JoinAddress.Left(40);
		return true;
	}
	// the settings screen waits for a key: the next keyboard key goes to the action (Esc cancels)
	if (!CapturingAction.IsNone() && Params.Event == IE_Pressed && !Params.Key.IsMouseButton() && !Params.Key.IsGamepadKey() && !Params.Key.IsAxis1D() && !Params.Key.IsAxis2D())
	{
		if (Params.Key != EKeys::Escape)
		{
			FArenaSettings& S = FArenaSettings::Get();
			S.Rebind(CapturingAction, Params.Key);
			S.Save();
			RebuildKeyMap();
			Say(FString::Printf(TEXT("Key: %s"), *Params.Key.GetDisplayName().ToString()), FLinearColor(0.5f, 1.f, 0.5f));
		}
		CapturingAction = NAME_None;
		return true;
	}
	return Super::InputKey(Params);
}

void AArenaPlayerController::OnPing()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	AArenaCharacter* H = Hero();
	if (!GM || !H || !H->IsAlive() || bPaused || Menu != EArenaMenu::None || GM->Phase != EArenaPhase::Playing) { return; }
	if (IsNetClient()) { ServerPing(H->AimPoint); } else { GM->AddPing(H->GetTeam(), H->HeroIndex, H->AimPoint); }
	ArenaHitSound::Play(this, 4);
	Say(TEXT("Ping: here!  (allies will head to this spot)"), FLinearColor(0.5f, 0.85f, 1.f));
}

void AArenaPlayerController::SpectateNext()
{
	// the living allies in turn, after the one watched now
	TArray<AArenaCharacter*> Allies;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { if (It->IsAlive() && !It->IsMinion() && It->GetTeam() == AArenaCharacter::LocalTeam) { Allies.Add(*It); } }
	if (Allies.Num() == 0) { return; }
	const int32 Cur = Allies.IndexOfByKey(Spectating.Get());
	Spectating = Allies[(Cur + 1) % Allies.Num()];
	SetViewTargetWithBlend(Spectating.Get(), 0.35f);
}

void AArenaPlayerController::TravelWithLoading(const FString& Map, const FString& Options)
{
	static const TCHAR* Tips[] = {
		TEXT("Hold the ability key to see its range; release to use it."),
		TEXT("Ctrl + ability key ranks it up, when you have a free point."),
		TEXT("In Conquest, walk under the enemy tower only behind your minions — the tower hits them first."),
		TEXT("The red camp gives +15% damage, the black one: more mana and shorter cooldowns."),
		TEXT("G or the middle mouse button: ping — your bots will head to that spot."),
		TEXT("B outside the base: recall. Any hit interrupts the recall."),
		TEXT("Tab shows the scoreboard; Alt — full ability description.") };
	// the full package path (/Game/Maps/<name>): the packaged game's IoStore does not reliably resolve a short name
	PendingMap = Map.StartsWith(TEXT("/")) ? Map : TEXT("/Game/Maps/") + Map;
	PendingOptions = Options;
	LoadingFrames = 0;
	LoadingTip = Tips[FMath::RandRange(0, UE_ARRAY_COUNT(Tips) - 1)];
	OpenMenu(EArenaMenu::None);
}

void AArenaPlayerController::OnLookPad(const FInputActionValue& V)
{
	// a stick is a rate, not a distance: degrees per second (180 across, 120 up and down at full tilt)
	if (bPaused || bShopOpen || Menu != EArenaMenu::None) { return; }
	const FVector2D In = V.Get<FVector2D>() * FArenaSettings::Get().Sensitivity * GetWorld()->GetDeltaSeconds();
	AddYawInput(In.X * 72.f);
	AddPitchInput((FArenaSettings::Get().bInvertY ? In.Y : -In.Y) * 48.f);
}

void AArenaPlayerController::OnUnPossess()
{
	if (AArenaCharacter* H = Hero()) { H->bAssistedAim = false; H->AimTarget = nullptr; }
	CancelAim();
	Super::OnUnPossess();
}

void AArenaPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (AArenaCharacter* H = Cast<AArenaCharacter>(InPawn)) { H->bAssistedAim = true; H->UseSmoothTurning(false); H->SetBaseFov(FArenaSettings::Get().Fov); H->UpdateTeamRing(); }
	UpdateCursor();
}

void AArenaPlayerController::OnMove(const FInputActionValue& V)
{
	AArenaCharacter* H = Hero();
	if (!H || !H->IsAlive() || bPaused) { return; }
	if (const AArenaGameMode* PhaseGM = AArenaGameMode::Get(this))
	{
		if (PhaseGM->Phase == EArenaPhase::Countdown || PhaseGM->Phase == EArenaPhase::Ended) { return; }   // GS-07: no head start
	}
	const FVector2D In = V.Get<FVector2D>();
	if (H->IsRecalling() && In.SizeSquared() > 0.01f)
	{
		if (IsNetClient()) { if (GetWorld()->GetTimeSeconds() >= CastRequestAt[0]) { CastRequestAt[0] = GetWorld()->GetTimeSeconds() + 0.2f; ServerRecall(false); } }
		else { H->CancelRecall(TEXT("move")); Say(TEXT("Recall interrupted"), FLinearColor(1.f, 0.6f, 0.3f)); }
	}
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	H->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), In.Y);
	H->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), In.X);
}

void AArenaPlayerController::OnLook(const FInputActionValue& V)
{
	if (bPaused || bShopOpen || Menu != EArenaMenu::None) { return; }   // the mouse points at the shop or a menu
	const FVector2D In = V.Get<FVector2D>() * FArenaSettings::Get().Sensitivity;
	AddYawInput(In.X);
	AddPitchInput(FArenaSettings::Get().bInvertY ? In.Y : -In.Y);
}

void AArenaPlayerController::OnJump()
{
	const AArenaGameMode* PhaseGM = AArenaGameMode::Get(this);
	if (PhaseGM && (PhaseGM->Phase == EArenaPhase::Countdown || PhaseGM->Phase == EArenaPhase::Ended)) { return; }
	if (AArenaCharacter* H = Hero()) { if (H->IsAlive() && !H->IsStunned()) { H->Jump(); } }
}

void AArenaPlayerController::OnSlot(int32 Slot)
{
	if (bPaused) { return; }
	CastSlot(Slot);
}

void AArenaPlayerController::OnNumber(int32 N)
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (GM && GM->Phase == EArenaPhase::HeroSelect)
	{
		// 1-5 pick a hero on the pick screen (in the other menus the keys do nothing)
		if (N <= 5 && (Menu == EArenaMenu::HeroPick || Menu == EArenaMenu::None)) { MenuAction(TEXT("PickHero"), N - 1); }
		return;
	}
	if ((N == 5 || N == 6) && !bPaused)
	{
		// 5 / 6: a health / mana potion
		if (IsNetClient()) { ServerDrinkPotion(N - 5); } else { DoPotion(N - 5); }
		return;
	}
	if (N < 1 || N > 4 || bPaused || bShopOpen) { return; }                                    // 1-4 abilities, 4 = ultimate
	AArenaCharacter* H = Hero();
	if (!H) { return; }
	LastAbilityKey = N;
	// Ctrl + key spends a skill point on it; the key of an ability not learnt yet learns it (VR-20)
	const bool bCtrl = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl) || IsInputKeyDown(EKeys::Gamepad_LeftTrigger);   // on a pad: LT + the ability
	if ((bCtrl || H->GetRank(N) <= 0) && IsNetClient()) { ServerRankUp(N); return; }
	if (bCtrl || H->GetRank(N) <= 0)
	{
		const FString Name = H->GetDef().Abilities.IsValidIndex(N) ? H->GetDef().Abilities[N].Name : FString();
		if (H->RankUp(N)) { Say(FString::Printf(TEXT("%s: rank %d"), *Name, H->GetRank(N)), FLinearColor(1.f, 0.85f, 0.3f)); }
		else if (H->FreeSkillPoints() <= 0) { Say(FString::Printf(TEXT("%s: no free points (next level)"), *Name), FLinearColor(1.f, 0.6f, 0.3f)); }
		else if (N == 4) { Say(TEXT("Ultimate: ranks at levels 5 / 9 / 13 / 17 / 20"), FLinearColor(1.f, 0.6f, 0.3f)); }
		else { Say(FString::Printf(TEXT("%s: rank %d from level %d"), *Name, H->GetRank(N) + 1, 2 * (H->GetRank(N) + 1) - 1), FLinearColor(1.f, 0.6f, 0.3f)); }
		return;
	}
	PressAbility(N);
}

FString AArenaPlayerController::NotReadyReason(int32 Slot, bool& bBufferable) const
{
	bBufferable = false;
	const AArenaCharacter* H = Hero();
	if (!H || !H->IsAlive()) { return TEXT("You are dead"); }
	if (!H->GetDef().Abilities.IsValidIndex(Slot)) { return TEXT("?"); }
	const FString& Name = H->GetDef().Abilities[Slot].Name;
	if (Slot > 0 && H->GetRank(Slot) <= 0) { return FString::Printf(TEXT("%s: not learned yet (Ctrl+%d)"), *Name, Slot); }
	if (H->IsStunned()) { return TEXT("You are stunned"); }
	const float Cd = H->CooldownRemaining(Slot);
	if (Cd > 0.35f) { return FString::Printf(TEXT("%s: cooldown %.1f s"), *Name, Cd); }
	if (H->GetMana() < H->Ability(Slot).ManaCost) { return FString::Printf(TEXT("%s: not enough mana (%.0f / %.0f)"), *Name, H->GetMana(), H->Ability(Slot).ManaCost); }
	if (H->CanCastSlot(Slot)) { return FString(); }
	bBufferable = true;   // mid-swing, in the air for a moment, the last tenths of a cooldown: kept and fired
	return FString::Printf(TEXT("%s: in a moment"), *Name);
}

void AArenaPlayerController::Deny(int32 Slot, const FString& Why)
{
	if (GetWorld()->GetTimeSeconds() - DeniedAt > 0.25f) { ArenaHitSound::Play(this, 3, 0.35f); }   // not on every key repeat
	DeniedSlot = Slot;
	DeniedAt = GetWorld()->GetTimeSeconds();
	if (AArenaCharacter* H = Hero())
	{
		if (Why.Contains(TEXT("cooldown"))) { H->Voice(TEXT("Ability_OnCooldown"), 6.f); }
		else if (Why.Contains(TEXT("mana"))) { H->Voice(TEXT("Ability_LowMana"), 6.f); }
	}
	Say(Why, FLinearColor(1.f, 0.6f, 0.3f));
}

void AArenaPlayerController::PressAbility(int32 N)
{
	AArenaCharacter* H = Hero();
	if (!H || N < 1 || N > 4) { return; }
	// not ready (a cooldown, no mana, stunned): nothing to aim, say why and flash the slot. Before, the key still
	// entered the aim with the ability's description, and LMB then tried the ability instead of attacking.
	bool bBufferable = false;
	const FString Why = NotReadyReason(N, bBufferable);
	if (!Why.IsEmpty() && !bBufferable) { if (AimSlot == N) { CancelAim(); } Deny(N, Why); return; }
	switch (FArenaSettings::Get().CastMode)
	{
	case 1:   // instant: at the crosshair, now
	{
		BeginAim(N);
		const FVector Aim = Indicator ? Indicator->TargetPoint() : H->AimPoint;
		CancelAim();
		CastAt(N, Aim);
		break;
	}
	case 2:   // with confirmation: the key aims, the same key again (or LMB) casts
		if (AimSlot == N) { CastAimed(); } else { BeginAim(N); bAimByHold = false; }
		break;
	default:  // quick with a preview: aims while held, the release casts
		BeginAim(N);
		bAimByHold = true;
		break;
	}
}

void AArenaPlayerController::ReleaseAbility(int32 N)
{
	if (FArenaSettings::Get().CastMode != 0 || !bAimByHold || AimSlot != N) { return; }
	bAimByHold = false;
	CastAimed();
}

void AArenaPlayerController::CastAt(int32 Slot, const FVector& Aim)
{
	AArenaCharacter* H = Hero();
	if (!H) { return; }
	bool bBufferable = false;
	const FString Why = NotReadyReason(Slot, bBufferable);
	if (Why.IsEmpty())
	{
		H->AimPoint = Aim;
		if (CastSlot(Slot)) { return; }
		bBufferable = true;   // refused by the ability system this frame (its last use still finishing): kept
	}
	if (!bBufferable) { Deny(Slot, Why); return; }
	// the input buffer (LoL, Smite): pressed during a swing, it goes off the moment the swing ends
	PendingSlot = Slot;
	PendingAim = Aim;
	PendingUntil = GetWorld()->GetTimeSeconds() + 0.6f;
}

void AArenaPlayerController::BeginAim(int32 Slot)
{
	AArenaCharacter* H = Hero();
	if (!H || !H->IsAlive() || !H->GetDef().Abilities.IsValidIndex(Slot)) { return; }
	PendingSlot = -1;   // a new aim replaces a buffered cast
	if (!Indicator)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Indicator = GetWorld()->SpawnActor<AArenaIndicator>(AArenaIndicator::StaticClass(), H->GetActorLocation(), FRotator::ZeroRotator, P);
	}
	AimSlot = Slot;
	bBasicHeld = false;
	if (Indicator) { Indicator->Show(H, Slot); }
}

void AArenaPlayerController::CancelAim()
{
	AimSlot = -1;
	bAimByHold = false;
	if (Indicator) { Indicator->Hide(); }
}

void AArenaPlayerController::CastAimed()
{
	AArenaCharacter* H = Hero();
	const int32 Slot = AimSlot;
	if (!H || Slot < 0) { CancelAim(); return; }
	// exactly where the indicator showed; not ready yet (mid-swing) it is buffered, not ready at all it is dropped
	// with the reason, and the aim ends either way: LMB attacks again at once
	const FVector Aim = Indicator ? Indicator->TargetPoint() : H->AimPoint;
	CancelAim();
	CastAt(Slot, Aim);
}

void AArenaPlayerController::OnDifficulty(int32 D)
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!GM) { return; }
	if (GM->bTraining && Menu == EArenaMenu::None) { GM->TrainingKey(D); return; }   // F1-F3 in the training centre
	if (GM->Phase == EArenaPhase::HeroSelect) { GM->Difficulty = D; }
}

void AArenaPlayerController::OpenMenu(EArenaMenu M)
{
	const EArenaMenu Was = Menu;
	Menu = M;
	if (AArenaIconStudio* Studio = AArenaIconStudio::Get(this))
	{
		if (M == EArenaMenu::Heroes) { Studio->SetPreview(BrowserHero, BrowserSlot); }
		else if (Was == EArenaMenu::Heroes) { Studio->StopPreview(); }
	}
	UpdateCursor();
}

void AArenaPlayerController::UpdateCursor()
{
	AArenaGameMode* GM = GetWorld() ? AArenaGameMode::Get(this) : nullptr;
	const bool bCursor = Menu != EArenaMenu::None || bShopOpen || (GM && GM->Phase == EArenaPhase::Ended && !GM->bBotMatch);
	if (bCursor == bShowMouseCursor) { return; }
	bShowMouseCursor = bCursor;
	bBasicHeld = false;
	if (bCursor) { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
	else { SetInputMode(FInputModeGameOnly()); }
}

void AArenaPlayerController::MenuAction(FName Action, int32 Arg)
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	FArenaSettings& S = FArenaSettings::Get();
	auto Travel = [this](const FString& Map, const FString& Options) { TravelWithLoading(Map, Options); };
	const FString A = Action.ToString();
	if (A == TEXT("Play")) { OpenMenu(EArenaMenu::Play); }
	else if (A == TEXT("Heroes")) { BrowserSlot = -1; OpenMenu(EArenaMenu::Heroes); }
	else if (A == TEXT("Training")) { bPickForTraining = true; OpenMenu(EArenaMenu::HeroPick); }
	else if (A == TEXT("Proto")) { Travel(TEXT("Proto"), FString()); }
	else if (A == TEXT("ProtoHades")) { Travel(TEXT("Proto"), TEXT("Hades")); }   // prototype 2: the same map, Hades' controls   // v20: the new game's prototype (its own map, mode and menu)
	else if (A == TEXT("Settings")) { SettingsBack = Menu == EArenaMenu::Pause ? EArenaMenu::Pause : EArenaMenu::Main; OpenMenu(EArenaMenu::Settings); }
	else if (A == TEXT("Quit"))
	{
		// once more to close: a stray Q (or a click) no longer ends a session
		const float QNow = GetWorld()->GetRealTimeSeconds();
		if (QNow < QuitArmedUntil) { UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false); return; }
		QuitArmedUntil = QNow + 3.f;   // the button itself now reads "CLICK AGAIN TO QUIT"
		Say(TEXT("Quit game: press again (Q or Quit)"), FLinearColor(1.f, 0.8f, 0.4f));
	}
	else if (A == TEXT("Back")) { OpenMenu(Menu == EArenaMenu::Settings ? SettingsBack : EArenaMenu::Main); }
	else if (A == TEXT("Mode") && GM) { GM->SetConquest(Arg == 105); if (Arg != 105) { GM->SetTeamSize(Arg); } }
	else if (A == TEXT("Minutes") && GM) { GM->SetMatchMinutes(Arg); }
	else if (A == TEXT("Difficulty") && GM) { GM->Difficulty = FMath::Clamp(Arg, 0, 2); }
	else if (A == TEXT("Next")) { bPickForTraining = false; OpenMenu(EArenaMenu::HeroPick); }
	else if (A == TEXT("PickHero"))
	{
		if (bPickForTraining) { Travel(TEXT("Training"), FString::Printf(TEXT("Hero=%d"), Arg)); return; }
		OpenMenu(EArenaMenu::None);
		if (FArenaDatabase::Get().Heroes.IsValidIndex(Arg))
		{
			if (USoundBase* Pick = AArenaCharacter::VoiceCue(FArenaDatabase::Get().Heroes[Arg], TEXT("DraftLock"))) { UGameplayStatics::PlaySound2D(this, Pick, 0.9f); }
		}
		if (IsNetClient())
		{
			PickedHero = Arg;
			const TArray<FArenaHeroDef>& Hs = FArenaDatabase::Get().Heroes;
			ServerPickHero(Arg, Hs.IsValidIndex(Arg) ? FArenaSettings::Get().SkinFor(Hs[Arg].Id) : -1);
			OpenMenu(EArenaMenu::HeroPick);
			Say(TEXT("Picked — the host will start the match"), FLinearColor(0.5f, 0.85f, 1.f));
		}
		else if (GM) { GM->PlayerPickHero(Arg); }
	}
	else if (A == TEXT("BrowseHero"))
	{
		BrowserHero = Arg; BrowserSlot = -1;
		if (AArenaIconStudio* St = AArenaIconStudio::Get(this))
		{
			St->SetPreview(BrowserHero, -1);
			const TArray<FArenaHeroDef>& Hs = FArenaDatabase::Get().Heroes;
			if (Hs.IsValidIndex(BrowserHero)) { const int32 Sk = S.SkinFor(Hs[BrowserHero].Id); St->SetPreviewMesh(Hs[BrowserHero].Skins.IsValidIndex(Sk) ? Hs[BrowserHero].Skins[Sk] : FString()); }
		}
	}
	else if (A == TEXT("PickSkin"))
	{
		const TArray<FArenaHeroDef>& Hs = FArenaDatabase::Get().Heroes;
		if (Hs.IsValidIndex(BrowserHero))
		{
			const FArenaHeroDef& D = Hs[BrowserHero];
			S.SkinChoice.Add(D.Id, D.Skins.IsValidIndex(Arg) ? Arg : -1);
			S.Save();
			if (AArenaIconStudio* St = AArenaIconStudio::Get(this)) { St->SetPreviewMesh(D.Skins.IsValidIndex(Arg) ? D.Skins[Arg] : FString()); }
			Say(FString::Printf(TEXT("%s: %s"), *D.DisplayName, D.SkinNames.IsValidIndex(Arg) ? *D.SkinNames[Arg] : TEXT("default skin")), FLinearColor(1.f, 0.85f, 0.4f));
		}
	}
	else if (A == TEXT("BrowseSlot")) { BrowserSlot = Arg; if (AArenaIconStudio* St = AArenaIconStudio::Get(this)) { St->SetPreview(BrowserHero, Arg); } }
	else if (A == TEXT("LanHost"))
	{
		// a listen server on the arena with this screen's mode, length and difficulty
		const FString Opt = FString::Printf(TEXT("listen?Conquest=%d?Size=%d?Minutes=%d?Diff=%d"), GM && GM->bConquest ? 1 : 0, GM ? GM->TeamSize : 5, GM ? GM->MatchMinutes : 10, GM ? GM->Difficulty : 1);
		TravelWithLoading(GM && GM->bConquest ? TEXT("Conquest") : TEXT("Arena"), Opt);   // Conquest has its own map
	}
	else if (A == TEXT("LanJoin")) { if (!JoinAddress.IsEmpty()) { PendingJoin = JoinAddress; LoadingFrames = 0; LoadingTip = FString::Printf(TEXT("Connecting to %s…"), *JoinAddress); OpenMenu(EArenaMenu::None); } }
	else if (A == TEXT("LanTypeIp")) { bTypingAddress = !bTypingAddress; }
	else if (A == TEXT("TryHero")) { Travel(TEXT("Training"), FString::Printf(TEXT("Hero=%d"), BrowserHero)); }
	else if (A == TEXT("Resume")) { bPaused = false; SetPause(false); OpenMenu(EArenaMenu::None); }
	else if (A == TEXT("ToMenu")) { SetPause(false); Travel(TEXT("Arena"), FString()); }
	else if (A == TEXT("Rematch")) { Travel(UGameplayStatics::GetCurrentLevelName(this), FString()); }
	else if (A == TEXT("TrainingKey") && GM) { GM->TrainingKey(Arg); }
	else if (A.StartsWith(TEXT("Set")))
	{
		// settings: saved at once; the graphics also applied to the window
		bool bGraphics = false;
		if (A == TEXT("SetQuality")) { S.Quality = FMath::Clamp(Arg, 0, 3); bGraphics = true; }
		else if (A == TEXT("SetWindow")) { S.WindowMode = FMath::Clamp(Arg, 0, 2); bGraphics = true; }
		else if (A == TEXT("SetRes")) { const int32 N = FArenaSettings::Resolutions().Num(); S.Resolution = N > 0 ? ((FMath::Max(0, S.Resolution) + Arg) % N + N) % N : -1; bGraphics = true; }
		else if (A == TEXT("SetVSyncTo")) { S.bVSync = Arg != 0; bGraphics = true; }
		else if (A == TEXT("SetScale"))
		{
			// auto -> 100 -> 95 ... 50 with "-", back up to 100 and then auto with "+"
			if (S.RenderScale <= 0) { S.RenderScale = Arg < 0 ? 95 : 0; }
			else { S.RenderScale += Arg * 5; S.RenderScale = S.RenderScale > 100 ? 0 : FMath::Max(50, S.RenderScale); }
			bGraphics = true;
		}
		else if (A == TEXT("SetFpsIdx")) { const int32 Fps[4] = { 0, 60, 120, 144 }; S.FpsLimit = Fps[FMath::Clamp(Arg, 0, 3)]; bGraphics = true; }
		else if (A == TEXT("SetVolume")) { S.Volume = FMath::Clamp(S.Volume + Arg * 0.1f, 0.f, 1.f); }
		else if (A == TEXT("SetSens")) { S.Sensitivity = FMath::Clamp(S.Sensitivity + Arg * 0.1f, 0.2f, 3.f); }
		else if (A == TEXT("SetInvertTo")) { S.bInvertY = Arg != 0; }
		else if (A == TEXT("SetCastMode")) { S.CastMode = FMath::Clamp(Arg, 0, 2); CancelAim(); }
		else if (A == TEXT("SetAssistTo")) { S.bAimAssist = Arg != 0; }
		else if (A == TEXT("SetShakeTo")) { S.bCameraShake = Arg != 0; }
		else if (A == TEXT("SetNumbersTo")) { S.bDamageNumbers = Arg != 0; }
		else if (A == TEXT("SetFov")) { S.Fov = FMath::Clamp(S.Fov + Arg * 5.f, 80.f, 110.f); }
		else if (A == TEXT("SetUiScale")) { S.UiScale = FMath::Clamp(FMath::RoundToFloat((S.UiScale + Arg * 0.1f) * 10.f) / 10.f, 0.8f, 1.3f); }
		else if (A == TEXT("SetColorblindTo")) { S.bColorblind = Arg != 0; }
		else if (A == TEXT("SetTutorialTo")) { S.bTutorial = Arg != 0; }
		else if (A == TEXT("SetVoice")) { S.VoiceVolume = FMath::Clamp(S.VoiceVolume + Arg * 0.1f, 0.f, 1.f); }
		else if (A == TEXT("SetKey")) { const TArray<FArenaSettings::FKeyDef>& Defs = FArenaSettings::KeyDefs(); if (Defs.IsValidIndex(Arg)) { CapturingAction = Defs[Arg].Action; } return; }
		else if (A == TEXT("SetKeysDefault")) { S.ResetKeys(); S.Save(); RebuildKeyMap(); CapturingAction = NAME_None; return; }
		S.Save();
		if (bGraphics) { if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr) { GS->ApplySettings(false); } }
		S.Apply(GetWorld());
	}
}

void AArenaPlayerController::OnScoreboard(const FInputActionValue&) { bScoreboard = true; }

void AArenaPlayerController::Say(const FString& Text, const FLinearColor& Color)
{
	// on the server for a LAN guest: the notice goes to the guest's screen
	if (!IsLocalController()) { ClientSay(Text, Color); return; }
	Notice = Text; NoticeColor = Color; NoticeTime = GetWorld()->GetTimeSeconds();
}

void AArenaPlayerController::ClientSay_Implementation(const FString& Text, FLinearColor Color) { Notice = Text; NoticeColor = Color; NoticeTime = GetWorld()->GetTimeSeconds(); }

void AArenaPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaPlayerController, NetTeam);
}

void AArenaPlayerController::OnRep_NetTeam()
{
	AArenaCharacter::LocalTeam = NetTeam;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { It->UpdateTeamRing(); }
}

void AArenaPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bNetGuest = IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("ArenaNetGuest"));
	if (bNetGuest && GetNetMode() == NM_Standalone && GNetGuestDone)
	{
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
	else if (bNetGuest && GetNetMode() == NM_Standalone)
	{
		FTimerHandle Gh;
		GetWorldTimerManager().SetTimer(Gh, FTimerDelegate::CreateWeakLambda(this, [this]() { JoinAddress = TEXT("127.0.0.1"); MenuAction(TEXT("LanJoin"), 0); }), 6.f, false);
	}
	// a LAN guest: the local mirror of the game mode the HUD and the menus read
	if (IsLocalController() && GetNetMode() == NM_Client && !AArenaGameMode::Get(this))
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AArenaClientMirror>(AArenaClientMirror::StaticClass(), FTransform::Identity, P);
	}
}

void AArenaPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
	// a LAN guest's own hero: aim assistance, the camera, its team's colours
	if (AArenaCharacter* H = Cast<AArenaCharacter>(P); H && IsNetClient())
	{
		H->bAssistedAim = true;
		H->PlayerIndex = 0;
		H->UseSmoothTurning(false);
		H->SetBaseFov(FArenaSettings::Get().Fov);
		OnRep_NetTeam();
	}
	UpdateCursor();
}

int32 AArenaPlayerController::MyTeam() const
{
	if (IsNetClient()) { return NetTeam; }
	int32 T = 0, HIdx = 0;
	if (const AArenaGameMode* GM = AArenaGameMode::Get(this)) { if (GM->HumanSlot(this, T, HIdx)) { return T; } }
	return 0;
}

int32 AArenaPlayerController::MyHero() const
{
	if (const AArenaCharacter* H = Cast<AArenaCharacter>(GetPawn())) { if (H->HeroIndex >= 0) { return H->HeroIndex; } }
	int32 T = 0, HIdx = 0;
	if (const AArenaGameMode* GM = AArenaGameMode::Get(this)) { if (!IsNetClient() && GM->HumanSlot(this, T, HIdx)) { return HIdx; } return GM->PlayerHeroIndex; }
	return 0;
}

bool AArenaPlayerController::CastSlot(int32 Slot)
{
	AArenaCharacter* H = Hero();
	if (!H) { return false; }
	if (!IsNetClient()) { return H->TryCast(Slot); }
	if (Slot < 0 || Slot > 4 || !H->CanCastSlot(Slot)) { return false; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < CastRequestAt[Slot]) { return false; }   // the cooldown comes back with the next state
	CastRequestAt[Slot] = Now + 0.15f;
	if (Slot == 0 && H->IsRangedKit()) { H->FireSlowUntil = Now + FArenaDatabase::Get().Rules.RangedFireSlowSeconds; }   // predicted: the server applies the same
	ServerCast(Slot, H->AimPoint, H->AimTarget.Get());
	return true;
}

void AArenaPlayerController::ServerCast_Implementation(int32 Slot, FVector_NetQuantize Aim, AArenaCharacter* Target)
{
	AArenaCharacter* H = Hero();
	if (!H || !H->IsAlive() || Slot < 0 || Slot > 4) { return; }
	// the target a guest names is checked here: alive, within the ability's reach (with the aim assist's slack) and
	// in sight; else the ability goes at the aim point alone
	if (Target)
	{
		const float Reach = (H->GetDef().Abilities.IsValidIndex(Slot) ? H->Ability(Slot).Range * 100.f : 0.f) + 600.f;
		FHitResult Block;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaCastCheck), false, H);
		Q.AddIgnoredActor(Target);
		const bool bSeen = !GetWorld()->LineTraceSingleByChannel(Block, H->GetActorLocation() + FVector(0.f, 0.f, 60.f), Target->GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, Q);
		if (!Target->IsAlive() || FVector::Dist2D(Target->GetActorLocation(), H->GetActorLocation()) > Reach || !bSeen) { Target = nullptr; }
	}
	H->AimPoint = Aim;
	H->AimTarget = Target;
	H->TryCast(Slot);
}

void AArenaPlayerController::ServerPickHero_Implementation(int32 HeroIdx, int32 Skin)
{
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>()) { GM->RemotePickHero(this, HeroIdx, Skin); }
}

void AArenaPlayerController::ServerRankUp_Implementation(int32 Slot)
{
	AArenaCharacter* H = Hero();
	if (!H || Slot < 1 || Slot > 4) { return; }
	const FString Name = H->GetDef().Abilities.IsValidIndex(Slot) ? H->GetDef().Abilities[Slot].Name : FString();
	if (H->RankUp(Slot)) { Say(FString::Printf(TEXT("%s: rank %d"), *Name, H->GetRank(Slot)), FLinearColor(1.f, 0.85f, 0.3f)); }
	else { Say(FString::Printf(TEXT("%s: no free points (next level)"), *Name), FLinearColor(1.f, 0.6f, 0.3f)); }
}

void AArenaPlayerController::ServerBuy_Implementation(int32 Item) { BuyItem(Item); }
void AArenaPlayerController::ServerSell_Implementation(int32 Pos) { DoSell(Pos); }
void AArenaPlayerController::ServerBuyPotion_Implementation(int32 Kind) { if (Kind == 0 || Kind == 1) { DoBuyPotion(Kind); } }
void AArenaPlayerController::ServerDrinkPotion_Implementation(int32 Kind) { if (Kind == 0 || Kind == 1) { DoPotion(Kind); } }
void AArenaPlayerController::ServerRecall_Implementation(bool bStart) { DoRecall(bStart); }
void AArenaPlayerController::ServerRevive_Implementation() { OnRevive(); }
void AArenaPlayerController::ServerPing_Implementation(FVector_NetQuantize At)
{
	AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	if (AArenaCharacter* H = Hero(); GM && H && H->IsAlive()) { GM->AddPing(H->GetTeam(), H->HeroIndex, At); }
}

void AArenaPlayerController::TickNetGuest(float Now)
{
	auto Check = [this](bool bOk, const FString& What) { NetGuestFails += bOk ? 0 : 1; ARENA_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What); };
	const AArenaGameState* GS = AArenaGameState::Get(this);
	if (!GS) { return; }
	if (GS->Phase == EArenaPhase::HeroSelect && PickedHero < 0 && Now > 3.f) { MenuAction(TEXT("PickHero"), 3); }
	if (GS->Phase != EArenaPhase::Playing) { return; }
	if (NetGuestStart < 0.f) { NetGuestStart = Now; }
	AArenaCharacter* H = Hero();
	// walking every frame (a move input lasts one frame)
	if (H && H->IsAlive() && NetGuestGoal.IsValid() && NetGuestGoal->IsAlive() && FVector::Dist2D(NetGuestGoal->GetActorLocation(), H->GetActorLocation()) > 700.f)
	{
		H->AddMovementInput((NetGuestGoal->GetActorLocation() - H->GetActorLocation()).GetSafeNormal2D(), 1.f);
	}
	if (H && H->IsAlive() && Now >= NetGuestNextAct)
	{
		NetGuestNextAct = Now + 0.25f;
		// the nearest enemy: walk to it and attack (the ranks on the first free points)
		AArenaCharacter* Best = nullptr;
		float BestD = 1.e9f;
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsAlive() || It->GetTeam() == H->GetTeam() || It->IsMonster() || It->IsStructure() || !It->GetMesh()->GetSkeletalMeshAsset()) { continue; }
			const float D = FVector::Dist2D(It->GetActorLocation(), H->GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
			if (It->GetHealth() < It->GetMaxHealth() - 1.f) { bNetSawEnemyHurt = true; }
		}
		if (H->FreeSkillPoints() > 0) { ServerRankUp(1 + (int32)(Now) % 3); }
		if (Best)
		{
			const FVector To = (Best->GetActorLocation() - H->GetActorLocation()).GetSafeNormal2D();
			SetControlRotation(To.Rotation());
			NetGuestGoal = Best;
			H->AimPoint = Best->GetActorLocation();
			H->AimTarget = Best;
			if (!CastSlot(0)) { for (int32 s = 1; s <= 3; ++s) { if (H->GetRank(s) > 0 && CastSlot(s)) { break; } } }
		}
		if (H->CooldownRemaining(0) > 0.f) { bNetSawCooldown = true; }
	}
	if (H && FMath::FloorToInt(Now / 5.f) != FMath::FloorToInt((Now - GetWorld()->GetDeltaSeconds()) / 5.f))
	{
		ARENA_LOG(LogArena, Display, TEXT("ARENA evt=netguest_pos t=%.0f at=%s target=%s dist=%.0f speed=%.0f max=%.0f"), Now - NetGuestStart, *H->GetActorLocation().ToCompactString(),
			NetGuestGoal.IsValid() ? *NetGuestGoal->GetDef().Id.ToString() : TEXT("none"), NetGuestGoal.IsValid() ? FVector::Dist2D(NetGuestGoal->GetActorLocation(), H->GetActorLocation()) : -1.f,
			H->GetVelocity().Size2D(), H->GetCharacterMovement()->MaxWalkSpeed);
	}
	if (NetGuestStep == 0 && Now - NetGuestStart > 38.f)
	{
		NetGuestStep = 1;
		FScreenshotRequest::RequestScreenshot(TEXT("NET_Guest.png"), true, false);
		int32 Heroes = 0, Minions = 0;
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->GetMesh()->GetSkeletalMeshAsset()) { continue; }
			Heroes += It->IsMinion() ? 0 : 1;
			Minions += It->IsLaneMinion() ? 1 : 0;
		}
		Check(GS->Phase == EArenaPhase::Playing && GS->HeroStats.Num() == 10, FString::Printf(TEXT("the match state reached the guest (%d heroes in the records)"), GS->HeroStats.Num()));
		Check(H && H->GetMesh()->GetSkeletalMeshAsset() && H->HeroIndex == PickedHero && H->GetTeam() == NetTeam && AArenaCharacter::LocalTeam == NetTeam,
			FString::Printf(TEXT("the guest's own hero: its body, the picked hero %d, team %d"), H ? H->HeroIndex : -1, NetTeam));
		Check(Heroes >= 8, FString::Printf(TEXT("the guest sees the heroes' bodies (%d)"), Heroes));
		Check(Minions >= 4, FString::Printf(TEXT("the guest sees the minion waves (%d)"), Minions));
		Check(bNetSawCooldown, TEXT("the guest's casts came back from the server (cooldowns replicated)"));
		Check(bNetSawEnemyHurt, TEXT("the guest sees the enemies' health change"));
		const AArenaGameMode* Mirror = AArenaGameMode::Get(this);
		Check(Mirror && Mirror->bClientMirror && Mirror->Phase == EArenaPhase::Playing, TEXT("the guest's HUD mirror follows the match"));
		// the pick screen closed with the start (it held the mouse: no camera, no basic attack, no ping)
		Check(Menu == EArenaMenu::None && !bShowMouseCursor, FString::Printf(TEXT("the guest's pick screen closed when the match started (menu=%d cursor=%d)"), (int32)Menu, bShowMouseCursor ? 1 : 0));
	}
	if (NetGuestStep == 1 && Now - NetGuestStart > 42.f)
	{
		NetGuestStep = 2;
		ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), NetGuestFails);
		GNetGuestDone = true;
	}
	if (NetGuestStep == 2 && Now - NetGuestStart > 62.f)
	{
		NetGuestStep = 3;
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

void AArenaPlayerController::DoPotion(int32 Kind)
{
	AArenaCharacter* H = Hero();
	if (!H) { return; }
	if (H->DrinkPotion(Kind)) { Say(Kind == 0 ? TEXT("Health potion") : TEXT("Mana potion"), Kind == 0 ? FLinearColor(0.5f, 1.f, 0.5f) : FLinearColor(0.5f, 0.7f, 1.f)); }
	else if (H->Potions[Kind] <= 0) { Say(TEXT("No potions: buy them in the shop (B at base)"), FLinearColor(1.f, 0.6f, 0.3f)); }
	else { Say(TEXT("Potion already active"), FLinearColor(1.f, 0.6f, 0.3f)); }
}

void AArenaPlayerController::DoRecall(bool bStart)
{
	AArenaCharacter* H = Hero();
	if (!H || !H->IsAlive()) { return; }
	if (!bStart || H->IsRecalling()) { if (H->IsRecalling()) { H->CancelRecall(TEXT("key")); Say(TEXT("Recall interrupted"), FLinearColor(1.f, 0.6f, 0.3f)); } return; }
	if (H->StartRecall()) { Say(FString::Printf(TEXT("Recall: %.0f s  ·  moving, attacking, or taking damage interrupts it"), FArenaDatabase::Get().Rules.RecallSeconds), FLinearColor(0.6f, 0.85f, 1.f)); }
	else { Say(TEXT("You cannot recall right now"), FLinearColor(1.f, 0.6f, 0.3f)); }
}

void AArenaPlayerController::DoBuyPotion(int32 Kind)
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!GM) { return; }
	switch (GM->TryBuyPotion(MyTeam(), MyHero(), Kind))
	{
	case ArenaCore::EBuyResult::Ok: Say(Kind == 0 ? TEXT("Bought a health potion (5)") : TEXT("Bought a mana potion (6)"), FLinearColor(0.5f, 1.f, 0.5f)); break;
	case ArenaCore::EBuyResult::NotEnoughGold: Say(TEXT("Not enough gold"), FLinearColor(1.f, 0.4f, 0.3f)); break;
	case ArenaCore::EBuyResult::InventoryFull: Say(TEXT("You already have the max of this potion"), FLinearColor(1.f, 0.6f, 0.3f)); break;
	default: Say(TEXT("Return to base to buy"), FLinearColor(1.f, 0.6f, 0.3f)); break;
	}
}

void AArenaPlayerController::DoSell(int32 Pos)
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!GM) { return; }
	const TArray<int32> Inv = GM->InventoryOf(MyTeam(), MyHero());
	const FString Name = Inv.IsValidIndex(Pos) && FArenaDatabase::Get().Items.IsValidIndex(Inv[Pos]) ? FArenaDatabase::Get().Items[Inv[Pos]].Name : FString();
	if (GM->TrySell(MyTeam(), MyHero(), Pos))
	{
		Say(FString::Printf(TEXT("Sold: %s"), *Name), FLinearColor(1.f, 0.85f, 0.4f));
		if (AArenaHUD* HUD = Cast<AArenaHUD>(GetHUD())) { HUD->ShopSelectInventory(INDEX_NONE); }
	}
}

void AArenaPlayerController::SetShopOpen(bool bOpen)
{
	bShopOpen = bOpen;
	UpdateCursor();
}

void AArenaPlayerController::OnShop()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!GM || GM->Phase == EArenaPhase::HeroSelect || GM->Phase == EArenaPhase::Ended) { return; }
	if (bShopOpen) { SetShopOpen(false); return; }
	AArenaCharacter* H = Hero();
	const bool bDead = !H || !H->IsAlive();
	if (!bDead && !GM->InShop(H))
	{
		// B away from the base: recall (B again cancels); P opens the shop anywhere to look
		if (IsNetClient()) { ServerRecall(!H->IsRecalling()); } else { DoRecall(!H->IsRecalling()); }
		return;
	}
	bShopFromBase = !bDead;
	SetShopOpen(true);
}

void AArenaPlayerController::OnShopAnywhere()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!GM || GM->Phase == EArenaPhase::HeroSelect || GM->Phase == EArenaPhase::Ended) { return; }
	if (bShopOpen) { SetShopOpen(false); return; }
	bShopFromBase = false;       // look and plan anywhere; buying still needs the base (or death)
	SetShopOpen(true);
}

void AArenaPlayerController::OnClick()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	AArenaHUD* HUD = Cast<AArenaHUD>(GetHUD());
	float X = 0.f, Y = 0.f;
	if (!GM || !HUD || !GetMousePosition(X, Y)) { return; }
	const AArenaHUD::FShopHit Hit = HUD->ShopHitAt(FVector2D(X, Y));
	switch (Hit.Kind)
	{
	case AArenaHUD::EShopHit::Close: SetShopOpen(false); break;
	case AArenaHUD::EShopHit::Item: HUD->ShopSelect(Hit.Index); break;
	case AArenaHUD::EShopHit::Inventory: HUD->ShopSelectInventory(Hit.Index); break;
	case AArenaHUD::EShopHit::Buy: BuyItem(Hit.Index); break;
	case AArenaHUD::EShopHit::Potion: if (IsNetClient()) { ServerBuyPotion(Hit.Index); } else { DoBuyPotion(Hit.Index); } break;
	case AArenaHUD::EShopHit::Sell: if (IsNetClient()) { ServerSell(Hit.Index); HUD->ShopSelectInventory(INDEX_NONE); } else { DoSell(Hit.Index); } break;
	default: break;
	}
}

void AArenaPlayerController::BuyItem(int32 Item)
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!GM || !FArenaDatabase::Get().Items.IsValidIndex(Item)) { return; }
	if (IsNetClient()) { ServerBuy(Item); return; }
	const FArenaItemDef& D = FArenaDatabase::Get().Items[Item];
	const int32 Price = GM->PriceFor(MyTeam(), MyHero(), Item);
	switch (GM->TryBuy(MyTeam(), MyHero(), Item))
	{
	case ArenaCore::EBuyResult::Ok:
		Say(FString::Printf(TEXT("Bought: %s (%d g)"), *D.Name, Price), FLinearColor(0.5f, 1.f, 0.5f));
		if (AArenaCharacter* Buyer = Hero()) { Buyer->Voice(TEXT("Level_BoughtItem"), 4.f); }
		break;
	case ArenaCore::EBuyResult::NotEnoughGold: Say(FString::Printf(TEXT("Not enough gold for %s (%d)"), *D.Name, Price), FLinearColor(1.f, 0.4f, 0.3f)); break;
	case ArenaCore::EBuyResult::InventoryFull: Say(TEXT("Inventory full (6/6): sell something or upgrade a piece you own"), FLinearColor(1.f, 0.6f, 0.3f)); break;
	default: Say(TEXT("Return to base to buy"), FLinearColor(1.f, 0.6f, 0.3f)); break;
	}
}

void AArenaPlayerController::OnRevive()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (IsNetClient()) { ServerRevive(); return; }
	const FArenaRespawn* R = GM ? GM->PendingRespawn(MyTeam(), MyHero()) : nullptr;
	if (!R) { return; }
	const float Wait = GM->ReviveReadyIn(MyTeam(), MyHero());
	const int32 Cost = GM->ReviveCostFor(R->Level);
	if (Wait > 0.f) { Say(FString::Printf(TEXT("Revive ready in %.0f s"), Wait), FLinearColor(1.f, 0.6f, 0.3f)); return; }
	if (R->Gold < Cost) { Say(FString::Printf(TEXT("Revive costs %d gold"), Cost), FLinearColor(1.f, 0.4f, 0.3f)); return; }
	if (GM->TryRevive(MyTeam(), MyHero())) { SetShopOpen(false); Say(TEXT("Back in the fight!"), FLinearColor(0.5f, 1.f, 0.5f)); }
}

void AArenaPlayerController::OnQuality()
{
	// F8: Low -> Medium -> High -> Epic, saved for the next launch
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings) { return; }
	const int32 Next = (FMath::Clamp(Settings->GetOverallScalabilityLevel(), -1, 3) + 1) % 4;
	Settings->SetOverallScalabilityLevel(Next);
	Settings->ApplySettings(false);
	Settings->SaveSettings();
	FArenaSettings& St = FArenaSettings::Get();
	St.Quality = Next;
	St.SyncShaderModel();   // Epic = Lumen = SM6, from the next start
	if (GConfig) { GConfig->Flush(false, GGameUserSettingsIni); }
	const TCHAR* Names[4] = { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("epic · Lumen") };
	Say(FString::Printf(TEXT("Graphics quality: %s  (F8)%s"), Names[Next], St.NeedsRestart() ? (Next == 3 ? TEXT("  ·  Lumen after restart") : TEXT("  ·  faster mode after restart")) : TEXT("")), FLinearColor(0.8f, 0.9f, 1.f));
}

void AArenaPlayerController::OnMatchMinutes(int32 Minutes)
{
	if (AArenaGameMode* GM = AArenaGameMode::Get(this)) { GM->SetMatchMinutes(Minutes); }
}

void AArenaPlayerController::OnPause()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	// Esc walks back: a sub-menu to its parent, the pause menu to the game
	if (Menu == EArenaMenu::Settings) { OpenMenu(SettingsBack); return; }
	if (Menu == EArenaMenu::Play || Menu == EArenaMenu::HeroPick || Menu == EArenaMenu::Heroes) { OpenMenu(EArenaMenu::Main); return; }
	if (Menu == EArenaMenu::Main) { return; }
	if (Menu == EArenaMenu::Pause) { MenuAction(TEXT("Resume"), 0); return; }
	if (bShopOpen) { SetShopOpen(false); return; }   // Esc closes the shop first
	if (AimSlot >= 0) { CancelAim(); return; }        // then drops the aimed ability
	if (GM && GM->Phase == EArenaPhase::Ended) { return; }
	bPaused = true;
	// a LAN match goes on for everyone: the menu opens over the running game (a listen server's pause froze the
	// guests with no way to resume)
	if (GetNetMode() == NM_Standalone) { SetPause(true); }
	OpenMenu(EArenaMenu::Pause);
}

void AArenaPlayerController::DemoPauseClicks(const TArray<FName>& Buttons)
{
	bPaused = true;
	SetPause(true);
	OpenMenu(EArenaMenu::Pause);
	DemoClicks = Buttons;
	DemoClickStep = 0;
	DemoClickStart = DemoClickAt = GetWorld()->GetRealTimeSeconds() + 0.5f;
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=ui_pause_clicks buttons=%d paused=%d"), Buttons.Num(), IsPaused() ? 1 : 0);
}

void AArenaPlayerController::TickDemoClicks(AArenaHUD* HUD)
{
	if (!IsDemoClicking() || !HUD) { return; }
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (Now < DemoClickAt) { return; }
	if (DemoClickStep == 1)
	{
		// the button released a frame after the press, the simulated cursor gone
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
		SimMouse.Reset();
		DemoClickStep = 0;
		DemoClickAt = Now + 0.4f;
		if (DemoClicks.Num() == 0)
		{
			// the last click (RESUME) must have closed the menu and lifted the pause
			const bool bOk = Menu == EArenaMenu::None && !IsPaused();
			ARENA_LOG(LogArena, Display, TEXT("LAB %s UI-PAUSE: clicks in the paused menu reach their buttons (menu=%d paused=%d)"), bOk ? TEXT("PASS") : TEXT("FAIL"), (int32)Menu, IsPaused() ? 1 : 0);
			if (!bOk) { MenuAction(TEXT("Resume"), 0); }
		}
		return;
	}
	if (Now - DemoClickStart > 8.f)
	{
		ARENA_LOG(LogArena, Display, TEXT("LAB FAIL UI-PAUSE: the paused menu stopped answering (menu=%d paused=%d left=%d)"), (int32)Menu, IsPaused() ? 1 : 0, DemoClicks.Num());
		DemoClicks.Reset();
		MenuAction(TEXT("Resume"), 0);
		return;
	}
	const FName Want = DemoClicks[0];
	FVector2D At;
	if (!HUD->FindMenuButton(Want, At)) { return; }   // not drawn yet (the menu opens next frame)
	const EArenaMenu Before = Menu;
	SimMouse = At;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
	ARENA_LOG(LogArena, Display, TEXT("ARENA evt=ui_click button=%s at=%.0f,%.0f menu_before=%d"), *Want.ToString(), At.X, At.Y, (int32)Before);
	DemoClicks.RemoveAt(0);
	DemoClickStep = 1;
	DemoClickAt = Now + 0.15f;
}

void AArenaPlayerController::OnQuit()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (bPaused || (GM && GM->Phase == EArenaPhase::Ended)) { MenuAction(TEXT("Quit"), 0); }
}

void AArenaPlayerController::OnRestart()
{
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (GM && GM->Phase == EArenaPhase::Ended) { TravelWithLoading(UGameplayStatics::GetCurrentLevelName(this), FString()); }
}

void AArenaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (bNetGuest && IsNetClient()) { TickNetGuest(GetWorld()->GetTimeSeconds()); }
	AArenaGameMode* MenuGM = AArenaGameMode::Get(this);
	if (!bMenuInit && MenuGM)
	{
		// the game opens on the main menu (not in a lab, a bot match or the training centre)
		bMenuInit = true;
		FArenaSettings::Get().Apply(GetWorld());
		if (IsNetClient() || GetNetMode() == NM_ListenServer) { bPickForTraining = false; OpenMenu(EArenaMenu::HeroPick); }
		else if (MenuGM->Phase == EArenaPhase::HeroSelect && !MenuGM->bBotMatch && !MenuGM->bAnimLab && !AArenaGameMode::IsTrainingMap(GetWorld()) && !AArenaGameMode::IsConquestMap(GetWorld())) { OpenMenu(EArenaMenu::Main); }
	}
	if (MenuGM && MenuGM->Phase == EArenaPhase::Ended) { UpdateCursor(); }
	// a LAN guest: the pick screen closes when the host starts the match (its mouse and keys were held by the menu)
	if (IsNetClient() && MenuGM && MenuGM->Phase != EArenaPhase::HeroSelect && Menu == EArenaMenu::HeroPick) { OpenMenu(EArenaMenu::None); }
	if (LoadingFrames >= 2 && !PendingJoin.IsEmpty())
	{
		const FString Addr = PendingJoin;
		PendingJoin.Reset();
		LoadingFrames = -1;
		ARENA_LOG(LogArena, Display, TEXT("ARENA evt=net_connect address=%s"), *Addr);
		ClientTravel(Addr, TRAVEL_Absolute);
		return;
	}
	if (LoadingFrames >= 2 && !PendingMap.IsEmpty())
	{
		const FString Map = PendingMap;
		PendingMap.Reset();
		UGameplayStatics::OpenLevel(this, FName(*Map), true, PendingOptions);
		return;
	}
	// dead in a match: watch an ally until the respawn (the respawn takes the camera back)
	if (MenuGM && !MenuGM->bBotMatch && MenuGM->Phase == EArenaPhase::Playing && MenuGM->PendingRespawn(AArenaCharacter::LocalTeam, MenuGM->PlayerHeroIndex))
	{
		if (!Spectating.IsValid() || !Spectating->IsAlive()) { SpectateNext(); }
	}
	else { Spectating = nullptr; }
	AArenaCharacter* H = Hero();
	if (bShopOpen && H && H->IsAlive())
	{
		AArenaGameMode* GM = AArenaGameMode::Get(this);
		if (GM && !GM->InShop(H) && bShopFromBase) { SetShopOpen(false); }   // walked out of the base (P keeps it open to browse)
	}
	if (!H || !PlayerCameraManager) { return; }
	// Aim: trace from the camera through the crosshair; the hero's abilities fire at this point.
	const FVector Start = PlayerCameraManager->GetCameraLocation();
	const FVector End = Start + PlayerCameraManager->GetCameraRotation().Vector() * 6000.f;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaAim), false, H);
	H->AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Q) ? Hit.ImpactPoint : End;
	// the enemy the next attack goes for: the aimed ability's, else the basic attack's (red crosshair, brackets)
	const int32 PickSlot = AimSlot >= 0 ? AimSlot : 0;
	H->AimTarget = H->GetDef().Abilities.IsValidIndex(PickSlot)
		? ArenaAbilityHelpers::FindAimTarget(H, H->Ability(PickSlot), Start, PlayerCameraManager->GetCameraRotation().Vector()) : nullptr;
	if (AimSlot >= 0 && (!H->IsAlive() || (Indicator && !Indicator->IsShown()))) { CancelAim(); }
	if (PendingSlot >= 0)
	{
		if (!H->IsAlive() || GetWorld()->GetTimeSeconds() > PendingUntil) { PendingSlot = -1; }
		else if (H->CanCastSlot(PendingSlot)) { H->AimPoint = PendingAim; CastSlot(PendingSlot); PendingSlot = -1; return; }
	}
	if (bBasicHeld && AimSlot < 0 && PendingSlot < 0) { CastSlot(0); }   // hold LMB = auto basic attacks
}

