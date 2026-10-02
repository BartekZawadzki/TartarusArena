// The prototype's controls (their own mapping, made at run time like the arena's), v21 built around the mouse: the
// attacks go where the camera looks (they home in on the enemy nearest the crosshair); LMB the light combo (hold: it
// keeps going; in the air: slashes), RMB a heavy (tap: quick, hold: charge — a full one launches; in the air: the
// slam), the wheel zooms (locked on: switches the target), the middle button locks on, the thumb buttons dodge and
// sprint. Keys: WASD / the left stick, Space jump (double, wall jump, ledge climb), Shift hold sprint / tap dodge,
// Ctrl hold or C sneak, Tab lock-on, F1 the controls card, Esc the menu. The controls card opens with each match.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "ProtoPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AProtoCharacter;

UENUM()
enum class EProtoMenu : uint8 { None, Main, Controls, Pause, Intro };

UCLASS()
class PARAGONARENA_API AProtoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AProtoPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	EProtoMenu Menu = EProtoMenu::Main;
	EProtoMenu MenuBack = EProtoMenu::Main;   // where "back" from the controls goes
	bool bSkipIntro = false;                  // the lab: no controls card
	void OnMatchStarted();
	void MenuAction(FName Action, int32 Arg);
	AProtoCharacter* Char() const;
	virtual void SetMenu(EProtoMenu M);
	/** Locked on: the next enemy to the right (Dir > 0) or the left of the current one. */
	void SwitchLock(float Dir);

protected:
	UInputAction* MakeAction(EInputActionValueType Type, bool bWhenPaused = false);
	UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UInputAction *ActMove = nullptr, *ActLook = nullptr, *ActLookPad = nullptr, *ActJump = nullptr, *ActSprint = nullptr, *ActSprintHold = nullptr, *ActSneak = nullptr, *ActSneakToggle = nullptr;
	UInputAction *ActLight = nullptr, *ActHeavy = nullptr, *ActDodge = nullptr, *ActLock = nullptr, *ActWheel = nullptr, *ActMenu = nullptr, *ActHelp = nullptr;
	float SprintPressedAt = -1.f, LightDownAt = -1.f, LastLightPress = -100.f;
	FVector2D LastMove = FVector2D::ZeroVector;
	void ToggleLock();
	void DodgeByKeys();
	void Dismiss();
};
