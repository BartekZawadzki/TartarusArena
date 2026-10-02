// The prototype's HUD and menus, drawn with the arena HUD's primitives (the same fonts, panels and buttons). v21: the
// controls card with a drawn mouse (it opens with every match and on F1, the game waits), no guard in the HUD, the
// counter window and the air slashes shown.
#pragma once

#include "CoreMinimal.h"
#include "UI/ArenaHUD.h"
#include "ProtoHUD.generated.h"

class AProtoCharacter;
class AProtoGameMode;
class AProtoPlayerController;

UCLASS()
class PARAGONARENA_API AProtoHUD : public AArenaHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawProtoMenu(AProtoGameMode* GM, AProtoPlayerController* PC);
	void DrawControlsCard(bool bIntro);
	void DrawHadesCard(bool bIntro);   // prototype 2: Hades' layout
	void DrawMouse(float X, float Y, float W, float H);
	void DrawPause(AProtoGameMode* GM);
	void DrawPlay(AProtoGameMode* GM, AProtoPlayerController* PC, AProtoCharacter* Me);
	void DrawEnemies(AProtoCharacter* Me);
	void DrawProtoNumbers();
	void Backdrop();
	void Key(const FString& Cap, float X, float Y, float& OutW);
};
