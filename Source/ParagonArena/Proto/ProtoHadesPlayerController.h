// Prototype 2 (operator 2026-09-29: "another prototype for the new game: everything the same — the world, the
// characters, the assumptions — but this time all the controls the same as in Hades, for the bot and the player,
// adding only a jump on Space"): Hades' keyboard and mouse layout — WASD move (on the screen: the camera is fixed
// above), the cursor aims, LMB attack (a dash then LMB: the dash-strike), RMB cast, Q special, F call (the wrath),
// and Space jump; Hades' dash is on Space, here on Shift (the jump took Space). Esc the menu.
#pragma once

#include "CoreMinimal.h"
#include "Proto/ProtoPlayerController.h"
#include "ProtoHadesPlayerController.generated.h"

UCLASS()
class PARAGONARENA_API AProtoHadesPlayerController : public AProtoPlayerController
{
	GENERATED_BODY()

public:
	AProtoHadesPlayerController();
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetMenu(EProtoMenu M) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	/** The buildings hidden between the camera and the hero (Hades cuts the walls away). */
	int32 CutCount() const { return CutAway.Num(); }

private:
	UInputAction *HMove = nullptr, *HAttack = nullptr, *HSpecial = nullptr, *HCast = nullptr, *HDash = nullptr, *HJump = nullptr, *HCall = nullptr, *HAimPad = nullptr;
	TSet<TWeakObjectPtr<AActor>> CutAway;
	FVector2D PadAim = FVector2D::ZeroVector;
	void DashByKeys();
	void UpdateCutaway(AProtoCharacter* C);
	void RestoreCutaway();
	float CameraYaw() const;
};
