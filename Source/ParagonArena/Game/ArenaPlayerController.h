#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "ArenaPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AArenaCharacter;
class AArenaIndicator;

/** The front end's screens (drawn by AArenaHUD, clicked through AArenaHUD::MenuHitAt). */
enum class EArenaMenu : uint8 { None, Main, Play, HeroPick, Heroes, Settings, Pause };

/** Smite controls (01 §4), built at runtime — no input assets. */
UCLASS()
class PARAGONARENA_API AArenaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- LAN (v16): a guest's actions go through the server; the server's answers come back as notices ------------
	/** The team this player plays on (a LAN guest's is set by the server when it joins). */
	UPROPERTY(ReplicatedUsing = OnRep_NetTeam) int32 NetTeam = 0;
	UFUNCTION() void OnRep_NetTeam();
	bool IsNetClient() const { return GetNetMode() == NM_Client; }
	int32 MyTeam() const;
	int32 MyHero() const;
	UFUNCTION(Server, Reliable) void ServerPickHero(int32 HeroIdx, int32 Skin);
	UFUNCTION(Server, Reliable) void ServerCast(int32 Slot, FVector_NetQuantize Aim, AArenaCharacter* Target);
	UFUNCTION(Server, Reliable) void ServerRankUp(int32 Slot);
	UFUNCTION(Server, Reliable) void ServerBuy(int32 Item);
	UFUNCTION(Server, Reliable) void ServerSell(int32 Pos);
	UFUNCTION(Server, Reliable) void ServerBuyPotion(int32 Kind);
	UFUNCTION(Server, Reliable) void ServerDrinkPotion(int32 Kind);
	UFUNCTION(Server, Reliable) void ServerRecall(bool bStart);
	UFUNCTION(Server, Reliable) void ServerRevive();
	UFUNCTION(Server, Reliable) void ServerPing(FVector_NetQuantize At);
	UFUNCTION(Client, Reliable) void ClientSay(const FString& Text, FLinearColor Color);
	/** A cast: straight on the host / alone, through the server for a LAN guest. */
	bool CastSlot(int32 Slot);
	float CastRequestAt[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	// the LAN menu: host a match, or join one by its address
	FString JoinAddress = TEXT("127.0.0.1");
	bool bTypingAddress = false;
	FString PendingJoin;
	int32 PickedHero = -1;         // a LAN guest's pick, shown while the host starts the match
	// -ArenaNetGuest: joins 127.0.0.1, picks, fights the nearest enemies and checks what the server sent back
	bool bNetGuest = false;
	float NetGuestStart = -1.f, NetGuestNextAct = 0.f;
	int32 NetGuestFails = 0, NetGuestStep = 0;
	bool bNetSawCooldown = false, bNetSawEnemyHurt = false;
	TWeakObjectPtr<class AArenaCharacter> NetGuestGoal;   // the test's own target (the crosshair's aim is recomputed every frame)
	void TickNetGuest(float Now);
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	/** Hold-to-attack from code (the UI demo). */
	void SetAutoAttack(bool bOn) { bBasicHeld = bOn; }

	// ---- menus ---------------------------------------------------------------------------------------------------
	EArenaMenu Menu = EArenaMenu::None;
	EArenaMenu SettingsBack = EArenaMenu::Main;    // where Esc / Back leads from the settings
	int32 BrowserHero = 0;                         // the hero shown in the browser
	int32 BrowserSlot = -1;                        // its ability being previewed (-1: idle)
	bool bPickForTraining = false;                 // the hero pick leads to the training centre
	void OpenMenu(EArenaMenu M);
	void MenuAction(FName Action, int32 Arg);
	/** Mouse cursor and input mode for the current screen (a menu, the shop, the end of a match: the cursor). */
	void UpdateCursor();
	bool bMenuInit = false;
	/** Rebuilds the key map from the settings (rebinding) with the fixed keys, the mouse and the gamepad. */
	void RebuildKeyMap();
	/** The settings screen waits for a key for this action (NAME_None: not waiting). */
	FName CapturingAction;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	float QuitArmedUntil = -100.f;   // the quit asks twice
	bool IsQuitArmed() const { return GetWorld() && GetWorld()->GetRealTimeSeconds() < QuitArmedUntil; }
	// ---- the UI demo clicks the paused menu through the real input path (a simulated key into this controller's
	// input, a simulated cursor position: the operator's own mouse is never moved) --------------------------------
	/** Pauses the match and queues clicks on these buttons (by action name), one every 0.4 s of real time. */
	void DemoPauseClicks(const TArray<FName>& Buttons);
	/** Called by the HUD every drawn frame (it draws while the game is paused; the game mode does not tick). */
	void TickDemoClicks(class AArenaHUD* HUD);
	bool IsDemoClicking() const { return DemoClicks.Num() > 0 || DemoClickStep > 0; }
	TArray<FName> DemoClicks;
	int32 DemoClickStep = 0;
	float DemoClickAt = 0.f, DemoClickStart = 0.f;
	TOptional<FVector2D> SimMouse;   // the cursor the click handler reads instead of the OS one (demo only)

	bool bScoreboard = false;
	bool bPaused = false;
	bool bShopOpen = false;
	void SetShopOpen(bool bOpen);
	/** Last shop / revive message for the HUD. */
	FString Notice;
	float NoticeTime = -100.f;
	FLinearColor NoticeColor = FLinearColor::White;
	/** Ability being aimed (its indicator is on the ground), -1 when none. */
	int32 AimingSlot() const { return AimSlot; }
	void BeginAim(int32 Slot);
	void CancelAim();
	/** An ability key pressed / released (1-4), as the keyboard does it; also driven by the labs. */
	void PressAbility(int32 N);
	void ReleaseAbility(int32 N);
	/** The key of an ability that could not go off: the HUD flashes its slot red for a moment. */
	int32 DeniedSlot = -1;
	float DeniedAt = -100.f;
	/** A cast pressed during another action (an attack's swing): it goes off as soon as it can (input buffer). */
	int32 BufferedSlot() const { return PendingSlot; }
	/** The ability whose description Alt shows (the one aimed, else the last key pressed). */
	int32 TipSlot() const { return AimSlot >= 0 ? AimSlot : LastAbilityKey; }

private:
	UInputAction* MakeAction(EInputActionValueType Type);
	AArenaCharacter* Hero() const;
	void OnMove(const FInputActionValue& V);
	void OnLook(const FInputActionValue& V);
	void OnJump();
	void OnSlot(int32 Slot);
	void OnNumber(int32 N);
	void OnScoreboard(const FInputActionValue& V);
	void OnPause();
	void OnQuit();
	void OnRestart();
	void OnDifficulty(int32 D);
	void OnShop();
	void OnShopAnywhere();
	bool bShopFromBase = false;         // opened with B in the base: closes when the hero walks out
	void OnRevive();
	void OnMatchMinutes(int32 Minutes);
	void OnQuality();
	void OnClick();
	void BuyItem(int32 Item);
	void Say(const FString& Text, const FLinearColor& Color);
	void DoPotion(int32 Kind);
	void DoBuyPotion(int32 Kind);
	void DoSell(int32 Pos);
	void DoRecall(bool bStart);
	// targeting (Smite / LoL): 1-4 shows the ability's range and area, LMB or the same key again casts, RMB cancels
	void CastAimed();
	/** Why Slot cannot go off now, or empty; bBufferable: only busy for a moment (the cast is kept and fired). */
	FString NotReadyReason(int32 Slot, bool& bBufferable) const;
	void Deny(int32 Slot, const FString& Why);
	/** Casts Slot at the aim the indicator would show (instant mode), buffering it through a short busy moment. */
	void CastAt(int32 Slot, const FVector& Aim);
	UPROPERTY() TObjectPtr<AArenaIndicator> Indicator;
	int32 AimSlot = -1;
	bool bAimByHold = false;             // quick cast: the key is held, its release casts
	int32 LastAbilityKey = 1;
	int32 PendingSlot = -1;              // the input buffer: a cast waiting for the current swing to end
	FVector PendingAim = FVector::ZeroVector;
	float PendingUntil = 0.f;

	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	// the actions (held by Actions): mapped in RebuildKeyMap
	UInputAction* ActMove = nullptr; UInputAction* ActLook = nullptr; UInputAction* ActLookPad = nullptr; UInputAction* ActJump = nullptr;
	UInputAction* ActBasic = nullptr; UInputAction* ActCancel = nullptr; UInputAction* ActShopAny = nullptr; UInputAction* ActScore = nullptr;
	UInputAction* ActPause = nullptr; UInputAction* ActQuit = nullptr; UInputAction* ActRestart = nullptr; UInputAction* ActQuality = nullptr;
	UInputAction* ActShop = nullptr; UInputAction* ActRevive = nullptr; UInputAction* ActPing = nullptr;
	UInputAction* ActNum[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	UInputAction* ActF[3] = { nullptr, nullptr, nullptr };
	UInputAction* ActMin[3] = { nullptr, nullptr, nullptr };
	void OnLookPad(const FInputActionValue& V);
	void OnPing();
public:
	/** Dead: the camera follows an ally (LMB: the next one). */
	TWeakObjectPtr<AArenaCharacter> Spectating;
	void SpectateNext();
	/** A level to open once the loading screen has been drawn (the map loads synchronously: the last frame stays up). */
	FString PendingMap, PendingOptions;
	int32 LoadingFrames = -1;
	void TravelWithLoading(const FString& Map, const FString& Options);
	FString LoadingTip;
private:
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
	bool bBasicHeld = false;
};
