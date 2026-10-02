#include "Proto/ProtoHUD.h"
#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoGameMode.h"
#include "Proto/ProtoPlayerController.h"
#include "Proto/ProtoBotController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

namespace
{
	const FLinearColor PGold(1.f, 0.8f, 0.35f), PWhite(1.f, 1.f, 1.f), PGrey(0.66f, 0.67f, 0.72f), PRed(1.f, 0.32f, 0.26f), PGreen(0.3f, 0.85f, 0.4f), PStam(1.f, 0.78f, 0.25f), PCyan(0.4f, 0.85f, 1.f);
	// the keys (the mouse has its own drawing)
	const TCHAR* ProtoKeys[][2] = {
		{ TEXT("W A S D"), TEXT("run relative to the camera") },
		{ TEXT("Shift"), TEXT("hold: sprint · tap: dodge (also in the air)") },
		{ TEXT("Space"), TEXT("jump · double jump · wall jump · ledge climb") },
		{ TEXT("Ctrl · C"), TEXT("sneak: quiet and low") },
		{ TEXT("Tab"), TEXT("lock-on (circle around the target)") },
		{ TEXT("F1 · Esc"), TEXT("this card · menu") } };
	const TCHAR* ProtoTips[] = {
		TEXT("Dodge just before the hit = PERFECT DODGE: time slows, and the next blow is a COUNTER ×1.5."),
		TEXT("A full RMB charge launches the enemy up — jump after it and slash with LMB in the air."),
		TEXT("You can cancel the tail of any blow with movement, a dodge, or a jump; early presses wait in the queue."),
		TEXT("From behind, unnoticed (sneaking): a sneak attack ×2.5. The bot can't see through walls.") };
}

void AProtoHUD::Backdrop()
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	QuadGrad(0, 0, 900 * S, H, FLinearColor(0.f, 0.f, 0.02f, 0.9f), FLinearColor(0.f, 0.f, 0.02f, 0.f), FLinearColor(0.f, 0.f, 0.02f, 0.9f), FLinearColor(0.f, 0.f, 0.02f, 0.f));
	QuadGrad(0, H * 0.55f, W, H * 0.45f, FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.5f), FLinearColor(0.f, 0.f, 0.f, 0.5f));
}

void AProtoHUD::DrawHUD()
{
	AHUD::DrawHUD();
	if (!Canvas) { return; }
	S = Canvas->SizeY / 1080.f;
	MenuHits.Reset();
	AProtoGameMode* GM = AProtoGameMode::Get(this);
	AProtoPlayerController* PC = Cast<AProtoPlayerController>(PlayerOwner);
	if (!GM || !PC) { return; }
	if (GM->bLab) { if (AProtoCharacter* Me = PC->Char()) { DrawPlay(GM, PC, Me); } return; }
	if (GM->Phase == EProtoPhase::Menu)
	{
		if (PC->Menu == EProtoMenu::Controls) { DrawControlsCard(false); }
		else { DrawProtoMenu(GM, PC); }
		return;
	}
	if (AProtoCharacter* Me = PC->Char(); Me && PC->Menu == EProtoMenu::None) { DrawPlay(GM, PC, Me); }
	if (PC->Menu == EProtoMenu::Pause) { DrawPause(GM); }
	else if (PC->Menu == EProtoMenu::Controls) { DrawControlsCard(false); }
	else if (PC->Menu == EProtoMenu::Intro) { DrawControlsCard(true); }
}

void AProtoHUD::DrawProtoMenu(AProtoGameMode* GM, AProtoPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Backdrop();
	QuadGrad(62 * S, 110 * S, FMath::Max(1.f, 2.f * S), H - 200 * S, FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.7f), FLinearColor(1.f, 0.8f, 0.35f, 0.7f));
	Text(GM->bHades ? TEXT("NEW GAME · 2") : TEXT("NEW GAME"), 88 * S, 96 * S, PGold, 2.5f * S, false, TitleFont());
	Text(GM->bHades ? TEXT("PROTOTYPE 2 · controls like Hades, camera from above · the same world, characters and combat") : TEXT("PROTOTYPE · a grey mock-up of a small town with three lanes · movement and combat for testing"), 92 * S, 196 * S, FLinearColor(0.82f, 0.84f, 0.9f), 0.62f * S, false, GEngine->GetMediumFont());
	const float BX = 90 * S, BW = 470 * S, BH = 62 * S, Gap = 14 * S;
	float Y = 270 * S;
	Button(TEXT("DUEL VS BOT  ·  1v1"), BX, Y, BW, BH, TEXT("ProtoDuel"), 0, false, true, 0.8f); Y += BH + Gap;
	Button(TEXT("TWO BOTS  ·  1v2"), BX, Y, BW, BH, TEXT("ProtoTwo"), 0, false, true, 0.8f); Y += BH + Gap;
	Button(TEXT("SANDBOX  ·  the bot only patrols"), BX, Y, BW, BH, TEXT("ProtoSandbox"), 0, false, true, 0.8f); Y += BH + Gap + 10 * S;
	Text(TEXT("BOT DIFFICULTY  —  decisions and reflexes, not aim"), BX, Y, PGrey, 0.5f * S, false, GEngine->GetMediumFont()); Y += 30 * S;
	const TCHAR* Diff[3] = { TEXT("Easy"), TEXT("Normal"), TEXT("Hard") };
	const float DW = (BW - 2 * 10 * S) / 3.f;
	for (int32 d = 0; d < 3; ++d) { Button(Diff[d], BX + d * (DW + 10 * S), Y, DW, 48 * S, TEXT("ProtoDiff"), d, GM->Difficulty == d, true, 0.62f); }
	Y += 48 * S + Gap + 16 * S;
	Button(TEXT("CONTROLS"), BX, Y, BW, BH, TEXT("ProtoControls"), 0, false, true, 0.8f); Y += BH + Gap;
	Button(TEXT("BACK TO TARTARUS ARENA"), BX, Y, BW, BH, TEXT("ProtoExit"), 0, false, true, 0.8f);
	// the mouse at a glance, beside the buttons (the full card opens with the match)
	const float PX = W - 560 * S, PY = 250 * S;
	Panel(PX, PY, 480 * S, 470 * S);
	Text(TEXT("MOUSE COMBAT"), PX + 30 * S, PY + 22 * S, PGold, 0.8f * S, false);
	DrawMouse(PX + 60 * S, PY + 110 * S, 110 * S, 170 * S);
	float LY = PY + 104 * S;
	const TCHAR* Short1[][2] = { { TEXT("LMB"), TEXT("combo · air slashes") }, { TEXT("RMB"), TEXT("heavy attack · hold: charge") }, { TEXT("Wheel"), TEXT("zoom · switch target") },
		{ TEXT("Middle"), TEXT("lock-on") }, { TEXT("Side"), TEXT("dodge · sprint") } };
	const TCHAR* Short2[][2] = { { TEXT("Cursor"), TEXT("aiming") }, { TEXT("LMB"), TEXT("attack · after a dash: dash strike") }, { TEXT("RMB"), TEXT("Cast (stone)") },
		{ TEXT("Q · F"), TEXT("special attack · Wrath") }, { TEXT("Shift · Space"), TEXT("dash · jump") } };
	const TCHAR* (*Rows)[2] = GM->bHades ? Short2 : Short1;
	for (int32 r = 0; r < 5; ++r)
	{
		const TCHAR* const* R = Rows[r];
		Text(R[0], PX + 200 * S, LY, PGold, 0.5f * S, false);
		Text(R[1], PX + 200 * S, LY + 22 * S, PWhite, 0.46f * S, false, GEngine->GetMediumFont());
		LY += 56 * S;
	}
	Text(GM->bHades ? TEXT("Hades' key layout + jump on Space.") : TEXT("Blows go where the camera looks."), PX + 30 * S, PY + 420 * S, PGrey, 0.46f * S, false, GEngine->GetMediumFont());
	Text(TEXT("Grey engine shapes and the UE template's mannequin — no art yet, for testing the new game's mechanics and feel."), 90 * S, H - 40 * S, FLinearColor(0.55f, 0.56f, 0.62f), 0.46f * S, false, GEngine->GetMediumFont());
}

void AProtoHUD::DrawMouse(float X, float Y, float W, float H)
{
	const AProtoGameMode* MGM = AProtoGameMode::Get(this);
	const bool bExtras = !(MGM && MGM->bHades);   // Hades' layout: no wheel, no thumb buttons
	// a mouse, drawn: the body (a rounded shape), the two buttons, the wheel, the thumb buttons on the left side
	TArray<FVector2D> Body;
	const float R = W * 0.5f;
	for (int32 i = 0; i <= 16; ++i) { const float A = PI + PI * i / 16.f; Body.Add(FVector2D(X + R + R * FMath::Cos(A), Y + R + R * FMath::Sin(A) * 0.9f)); }
	for (int32 i = 0; i <= 16; ++i) { const float A = PI * i / 16.f; Body.Add(FVector2D(X + R + R * FMath::Cos(A), Y + H - R + R * FMath::Sin(A) * 1.05f)); }
	PolyFill(Body, Y, Y + H, FLinearColor(0.2f, 0.21f, 0.26f, 0.95f), FLinearColor(0.1f, 0.1f, 0.13f, 0.95f));
	// the buttons: LMB gold, RMB cyan (the top two fifths)
	const float Split = Y + H * 0.4f;
	TArray<FVector2D> Left, Right;
	for (const FVector2D& P : Body) { if (P.Y <= Split + 0.5f) { (P.X <= X + R ? Left : Right).Add(P); } }
	Left.Add(FVector2D(X + R, Split)); Left.Add(FVector2D(X, Split));
	Right.Insert(FVector2D(X + R, Split), 0); Right.Add(FVector2D(X + W, Split));
	PolyFill(Left, Y, Split, FLinearColor(1.f, 0.8f, 0.35f, 0.55f), FLinearColor(1.f, 0.8f, 0.35f, 0.3f));
	PolyFill(Right, Y, Split, FLinearColor(0.4f, 0.85f, 1.f, 0.5f), FLinearColor(0.4f, 0.85f, 1.f, 0.25f));
	Outline(Body, FLinearColor(0.85f, 0.87f, 0.95f, 0.9f), 2.f * S);
	Line(X + R, Y, X + R, Split, FLinearColor(0.85f, 0.87f, 0.95f, 0.9f), 2.f * S);
	Line(X, Split, X + W, Split, FLinearColor(0.85f, 0.87f, 0.95f, 0.9f), 2.f * S);
	// the wheel and the thumb buttons
	if (bExtras)
	{
		Rect(X + R - 7 * S, Y + H * 0.12f, 14 * S, H * 0.18f, FLinearColor(0.85f, 0.87f, 0.95f, 0.95f));
		Rect(X - 7 * S, Y + H * 0.5f, 9 * S, H * 0.1f, FLinearColor(0.55f, 1.f, 0.65f, 0.9f));
		Rect(X - 7 * S, Y + H * 0.63f, 9 * S, H * 0.1f, FLinearColor(0.55f, 1.f, 0.65f, 0.9f));
	}
	Text(TEXT("L"), X + R * 0.5f, Y + H * 0.2f, FLinearColor(0.1f, 0.08f, 0.02f), 0.7f * S, true);
	Text(TEXT("R"), X + R * 1.5f, Y + H * 0.2f, FLinearColor(0.02f, 0.08f, 0.12f), 0.7f * S, true);
}

void AProtoHUD::Key(const FString& Cap, float X, float Y, float& OutW)
{
	const FVector2D Sz = Measure(Cap, 0.5f * S);
	OutW = FMath::Max(44.f * S, Sz.X + 20.f * S);
	QuadGrad(X, Y, OutW, 34 * S, FLinearColor(0.28f, 0.29f, 0.35f, 0.95f), FLinearColor(0.28f, 0.29f, 0.35f, 0.95f), FLinearColor(0.14f, 0.15f, 0.19f, 0.95f), FLinearColor(0.14f, 0.15f, 0.19f, 0.95f));
	Frame(X, Y, OutW, 34 * S, FLinearColor(0.8f, 0.82f, 0.9f, 0.6f), 1.5f * S);
	Text(Cap, X + OutW * 0.5f, Y + 4 * S, PWhite, 0.5f * S, true);
}

void AProtoHUD::DrawHadesCard(bool bIntro)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.72f));
	MenuTitle(TEXT("CONTROLS · LIKE HADES"), bIntro ? TEXT("Click the left mouse button or press Space to start the fight") : TEXT("New game · prototype 2"));
	const float PW = 1560 * S, PH = 760 * S, PX = (W - PW) * 0.5f, PY = 150 * S;
	Panel(PX, PY, PW, PH);
	const float MX = PX + 300 * S, MY = PY + 70 * S, MW = 150 * S, MH = 235 * S;
	Text(TEXT("MOUSE — YOU AIM WITH THE CURSOR"), PX + 40 * S, PY + 26 * S, PGold, 0.8f * S, false);
	DrawMouse(MX, MY, MW, MH);
	struct FLabel { float TX, TY; const TCHAR* Head; const TCHAR* Body; float AX, AY; FLinearColor C; };
	const FLabel Labels[] = {
		{ PX + 40 * S, MY + 0 * S, TEXT("LMB — attack"), TEXT("a 3-hit combo toward the cursor · hold: continuous · right after a dash: dash strike · in the air: slashes"), MX + MW * 0.25f, MY + MH * 0.18f, PGold },
		{ MX + MW + 60 * S, MY + 0 * S, TEXT("RMB — Cast"), TEXT("the stone flies toward the cursor: it hurts and slows; it returns after 3 s"), MX + MW * 0.75f, MY + MH * 0.18f, PCyan } };
	for (const FLabel& L : Labels)
	{
		const bool bLeft = L.TX < MX;
		Text(L.Head, L.TX, L.TY, L.C, 0.56f * S, false);
		WrapText(L.Body, L.TX, L.TY + 26 * S, bLeft ? MX - L.TX - 40 * S : 520 * S, FLinearColor(0.88f, 0.9f, 0.95f), 0.46f * S);
		const float LX = bLeft ? MX - 30 * S : L.TX - 10 * S;
		Line(LX, L.TY + 12 * S, L.AX, L.AY, FLinearColor(L.C.R, L.C.G, L.C.B, 0.6f), 1.5f * S);
		Disc(L.AX, L.AY, 3.5f * S, L.C, 10);
	}
	Text(TEXT("The camera sits over the town like in Hades — the character always strikes and casts toward the cursor."), MX + MW + 60 * S, MY + 130 * S, PWhite, 0.48f * S, false, GEngine->GetMediumFont());
	Text(TEXT("A building blocking the character disappears while you stand behind it."), MX + MW + 60 * S, MY + 170 * S, PGrey, 0.46f * S, false, GEngine->GetMediumFont());
	float KY = MY + MH + 50 * S;
	Text(TEXT("KEYBOARD — HADES LAYOUT"), PX + 40 * S, KY, PGold, 0.8f * S, false);
	KY += 46 * S;
	const TCHAR* HKeys[][2] = {
		{ TEXT("W A S D"), TEXT("move on the screen (W: up the screen)") },
		{ TEXT("Shift"), TEXT("dash — on Space in Hades (Space moved to jump)") },
		{ TEXT("Space"), TEXT("jump · double jump · climb · wall jump — the only addition to Hades") },
		{ TEXT("Q"), TEXT("special attack: tap — smash around · hold — charge, a full one launches up") },
		{ TEXT("F"), TEXT("Wrath (Call): when the gauge is full — a blast that launches enemies") },
		{ TEXT("Esc · F1"), TEXT("menu · this card") } };
	for (int32 i = 0; i < 6; ++i)
	{
		const float CX = PX + 40 * S + (i % 2) * 760 * S, CY = KY + (i / 2) * 48 * S;
		float KX = CX, KW = 0.f;
		TArray<FString> Caps;
		FString(HKeys[i][0]).ParseIntoArray(Caps, TEXT(" "), true);
		for (const FString& Cap : Caps) { if (Cap == TEXT("·")) { KX += 8 * S; continue; } Key(Cap, KX, CY, KW); KX += KW + 6 * S; }
		Text(HKeys[i][1], FMath::Max(KX + 14 * S, CX + (i % 2 ? 150.f : 200.f) * S), CY + 5 * S, PWhite, 0.46f * S, false, GEngine->GetMediumFont());
	}
	float TY = KY + 3 * 48 * S + 16 * S;
	const TCHAR* HTips[] = {
		TEXT("Dash just before the hit = PERFECT DASH: time slows, and the next blow is a COUNTER ×1.5."),
		TEXT("A full Q charge launches the enemy up — jump (Space) and slash with LMB in the air; Q in the air: ground slam."),
		TEXT("The Wrath gauge grows from blows dealt and taken. The bot has the same keys and the same moves.") };
	for (const TCHAR* Tip : HTips)
	{
		if (TY > PY + PH - 34 * S) { break; }
		Text(FString::Printf(TEXT("•  %s"), Tip), PX + 40 * S, TY, FLinearColor(0.84f, 0.86f, 0.92f), 0.46f * S, false, GEngine->GetMediumFont());
		TY += 30 * S;
	}
	if (bIntro) { Button(TEXT("START FIGHT"), (W - 360 * S) * 0.5f, PY + PH + 20 * S, 360 * S, 60 * S, TEXT("ProtoStart"), 0, true, true, 0.8f); }
	else { Button(TEXT("BACK"), (W - 300 * S) * 0.5f, PY + PH + 20 * S, 300 * S, 60 * S, TEXT("ProtoBack"), 0, false, true, 0.8f); }
}

void AProtoHUD::DrawControlsCard(bool bIntro)
{
	if (const AProtoGameMode* HGM = AProtoGameMode::Get(this); HGM && HGM->bHades) { DrawHadesCard(bIntro); return; }
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.72f));
	MenuTitle(TEXT("CONTROLS"), bIntro ? TEXT("Click the left mouse button or press Space to start the fight") : TEXT("New game · prototype"));
	const float PW = 1560 * S, PH = 760 * S, PX = (W - PW) * 0.5f, PY = 150 * S;
	Panel(PX, PY, PW, PH);
	// the mouse: the drawing and what each part does
	const float MX = PX + 300 * S, MY = PY + 70 * S, MW = 150 * S, MH = 235 * S;
	Text(TEXT("MOUSE — COMBAT"), PX + 40 * S, PY + 26 * S, PGold, 0.8f * S, false);
	DrawMouse(MX, MY, MW, MH);
	struct FLabel { float TX, TY; const TCHAR* Head; const TCHAR* Body; float AX, AY; FLinearColor C; };
	const FLabel Labels[] = {
		{ PX + 40 * S, MY + 0 * S, TEXT("LMB — light attack"), TEXT("a 3-hit combo · hold: continuous · while sprinting: running strike · in the air: slashes"), MX + MW * 0.25f, MY + MH * 0.18f, PGold },
		{ MX + MW + 60 * S, MY + 0 * S, TEXT("RMB — heavy attack"), TEXT("tap: quick · hold: charge, a full one launches up · in the air: ground slam"), MX + MW * 0.75f, MY + MH * 0.18f, PCyan },
		{ MX + MW + 60 * S, MY + 110 * S, TEXT("Wheel"), TEXT("camera zoom · while locked on: next target"), MX + MW * 0.5f, MY + MH * 0.2f, PWhite },
		{ MX + MW + 60 * S, MY + 190 * S, TEXT("Middle button"), TEXT("lock-on (also Tab)"), MX + MW * 0.5f, MY + MH * 0.28f, PWhite },
		{ PX + 40 * S, MY + 150 * S, TEXT("Side buttons"), TEXT("front: dodge · back: sprint"), MX - 8 * S, MY + MH * 0.6f, FLinearColor(0.55f, 1.f, 0.65f) } };
	for (const FLabel& L : Labels)
	{
		const bool bLeft = L.TX < MX;
		Text(L.Head, L.TX, L.TY, L.C, 0.56f * S, false);
		WrapText(L.Body, L.TX, L.TY + 26 * S, bLeft ? MX - L.TX - 40 * S : 520 * S, FLinearColor(0.88f, 0.9f, 0.95f), 0.46f * S);
		const float LX = bLeft ? MX - 30 * S : L.TX - 10 * S;
		Line(LX, L.TY + 12 * S, L.AX, L.AY, FLinearColor(L.C.R, L.C.G, L.C.B, 0.6f), 1.5f * S);
		Disc(L.AX, L.AY, 3.5f * S, L.C, 10);
	}
	Text(TEXT("Blows go where the camera looks — and home in on the enemy nearest the crosshair."), PX + 40 * S, MY + MH + 30 * S, PWhite, 0.5f * S, false, GEngine->GetMediumFont());
	// the keys
	float KY = MY + MH + 80 * S;
	Text(TEXT("KEYBOARD — MOVEMENT"), PX + 40 * S, KY, PGold, 0.8f * S, false);
	KY += 46 * S;
	const int32 NumKeys = UE_ARRAY_COUNT(ProtoKeys);
	for (int32 i = 0; i < NumKeys; ++i)
	{
		const float CX = PX + 40 * S + (i % 2) * 760 * S, CY = KY + (i / 2) * 48 * S;
		float KX = CX, KW = 0.f;
		TArray<FString> Caps;
		FString(ProtoKeys[i][0]).ParseIntoArray(Caps, TEXT(" "), true);
		for (const FString& Cap : Caps) { if (Cap == TEXT("·")) { KX += 8 * S; continue; } Key(Cap, KX, CY, KW); KX += KW + 6 * S; }
		Text(ProtoKeys[i][1], FMath::Max(KX + 14 * S, CX + (i % 2 ? 150.f : 200.f) * S), CY + 5 * S, PWhite, 0.48f * S, false, GEngine->GetMediumFont());
	}
	// the tips
	float TY = KY + 3 * 48 * S + 16 * S;
	for (const TCHAR* Tip : ProtoTips)
	{
		if (TY > PY + PH - 34 * S) { break; }
		Text(FString::Printf(TEXT("•  %s"), Tip), PX + 40 * S, TY, FLinearColor(0.84f, 0.86f, 0.92f), 0.46f * S, false, GEngine->GetMediumFont());
		TY += 30 * S;
	}
	if (bIntro)
	{
		Button(TEXT("START FIGHT"), (W - 360 * S) * 0.5f, PY + PH + 20 * S, 360 * S, 60 * S, TEXT("ProtoStart"), 0, true, true, 0.8f);
	}
	else
	{
		Button(TEXT("BACK"), (W - 300 * S) * 0.5f, PY + PH + 20 * S, 300 * S, 60 * S, TEXT("ProtoBack"), 0, false, true, 0.8f);
	}
}

void AProtoHUD::DrawPause(AProtoGameMode* GM)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.6f));
	MenuTitle(TEXT("PAUSED"), GM && GM->bHades ? TEXT("New game · prototype 2 (Hades controls)") : TEXT("New game · prototype"));
	const float BW = 440 * S, BH = 60 * S, Gap = 14 * S, BX = (W - BW) * 0.5f;
	float Y = 260 * S;
	Button(TEXT("RESUME"), BX, Y, BW, BH, TEXT("ProtoResume"), 0, true, true, 0.8f); Y += BH + Gap;
	Button(TEXT("RESTART"), BX, Y, BW, BH, TEXT("ProtoRestart"), 0, false, true, 0.8f); Y += BH + Gap;
	Button(TEXT("CONTROLS"), BX, Y, BW, BH, TEXT("ProtoControls"), 0, false, true, 0.8f); Y += BH + Gap;
	Button(TEXT("PROTOTYPE MENU"), BX, Y, BW, BH, TEXT("ProtoMenu"), 0, false, true, 0.8f); Y += BH + Gap;
	Button(TEXT("BACK TO TARTARUS ARENA"), BX, Y, BW, BH, TEXT("ProtoExit"), 0, false, true, 0.8f);
}

void AProtoHUD::DrawEnemies(AProtoCharacter* Me)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		AProtoCharacter* C = *It;
		if (C == Me || !C->IsAlive()) { continue; }
		FVector2D P;
		if (!PlayerOwner->ProjectWorldLocationToScreen(C->GetActorLocation() + FVector(0.f, 0.f, 125.f), P)) { continue; }
		const float Dist = FVector::Dist(C->GetActorLocation(), Me->GetActorLocation());
		if (Dist > 4000.f) { continue; }
		const float BW = 110 * S, BH = 9 * S;
		Bar(P.X - BW * 0.5f, P.Y, BW, BH, C->Health / AProtoCharacter::Tuning().MaxHealth, C->Team == Me->Team ? PGreen : PRed);
		Bar(P.X - BW * 0.5f, P.Y + BH + 2 * S, BW, 4 * S, C->Stamina / AProtoCharacter::Tuning().MaxStamina, PStam);
		// the bot's awareness: ! hunting, ? searching, a grey dot unaware (the sneak attack's chance)
		if (C->bIsBot)
		{
			const bool bHunting = C->bAlerted;
			const AProtoBotController* AI = Cast<AProtoBotController>(C->GetController());
			const bool bSearching = !bHunting && AI && AI->bSearching;
			const FString Mark = bHunting ? TEXT("!") : (bSearching ? TEXT("?") : TEXT("·"));
			Text(Mark, P.X, P.Y - 36 * S, bHunting ? PRed : (bSearching ? PStam : PGrey), 0.9f * S, true);
			if (!bHunting && Me->bSneaking) { Text(TEXT("doesn't see you"), P.X, P.Y - 58 * S, PCyan, 0.42f * S, true, GEngine->GetMediumFont()); }
		}
		// what it is doing: a charge (dodge it), a launch (after it!), a stagger
		if (C->bCharging) { Bar(P.X - BW * 0.5f, P.Y + 18 * S, BW, 5 * S, C->ChargePct(), C->ChargePct() >= 0.95f ? PRed : PGold); }
		else if (C->bLaunched) { Text(TEXT("AIRBORNE — SLASH!"), P.X, P.Y + 18 * S, PGold, 0.46f * S, true, GEngine->GetMediumFont()); }
		else if (Now < C->StaggerUntil) { Text(TEXT("STAGGERED"), P.X, P.Y + 18 * S, PCyan, 0.42f * S, true, GEngine->GetMediumFont()); }
		// the lock-on: a gold diamond around the target
		if (Me->LockTarget.Get() == C)
		{
			FVector2D L;
			if (PlayerOwner->ProjectWorldLocationToScreen(C->GetActorLocation(), L))
			{
				const float R = 26 * S;
				Line(L.X, L.Y - R, L.X + R, L.Y, PGold, 2.5f * S); Line(L.X + R, L.Y, L.X, L.Y + R, PGold, 2.5f * S);
				Line(L.X, L.Y + R, L.X - R, L.Y, PGold, 2.5f * S); Line(L.X - R, L.Y, L.X, L.Y - R, PGold, 2.5f * S);
			}
		}
	}
}

void AProtoHUD::DrawProtoNumbers()
{
	const float Now = GetWorld()->GetTimeSeconds();
	const AProtoGameMode* NGM = AProtoGameMode::Get(this);
	const bool bDash = NGM && NGM->bHades;   // prototype 2 says "dash", as Hades
	for (const AProtoCharacter::FNumber& N : AProtoCharacter::Numbers)
	{
		const float Age = Now - N.Time;
		if (Age > 1.1f || Age < 0.f) { continue; }
		FVector2D P;
		if (!PlayerOwner->ProjectWorldLocationToScreen(N.At + FVector(0.f, 0.f, Age * 70.f), P)) { continue; }
		const float A = FMath::Clamp(1.2f - Age, 0.f, 1.f);
		FString Str = FString::Printf(TEXT("%.0f"), N.Amount);
		FLinearColor C = PWhite;
		float Sc = 0.8f;
		switch (N.Kind)
		{
		case 2: Str = FString::Printf(TEXT("SNEAK ATTACK! %.0f"), N.Amount); C = PGold; Sc = 1.f; break;
		case 4: Str = bDash ? TEXT("DASH") : TEXT("DODGE"); C = PCyan; break;
		case 5: Str = bDash ? TEXT("PERFECT DASH!") : TEXT("PERFECT DODGE!"); C = PCyan; Sc = 1.f; break;
		case 6: Str = FString::Printf(TEXT("COUNTER! %.0f"), N.Amount); C = PGold; Sc = 1.05f; break;
		case 7: Str = FString::Printf(TEXT("LAUNCH %.0f"), N.Amount); C = PStam; Sc = 0.95f; break;
		case 8: Str = TEXT("WRATH!"); C = PRed; Sc = 1.3f; break;
		default: break;
		}
		Text(Str, P.X, P.Y, C * FLinearColor(1.f, 1.f, 1.f, A), Sc * S, true);
	}
}

void AProtoHUD::DrawPlay(AProtoGameMode* GM, AProtoPlayerController* PC, AProtoCharacter* Me)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY, Now = GetWorld()->GetTimeSeconds();
	const FProtoTuning& T = AProtoCharacter::Tuning();
	DrawEnemies(Me);
	DrawProtoNumbers();
	// the crosshair: a dot (the attacks go there), gold while a counter is ready
	const bool bCounter = Now < Me->CounterUntil;
	if (!Me->bTopDown) { Disc(W * 0.5f, H * 0.5f, (bCounter ? 5.f : 3.f) * S, bCounter ? PGold : FLinearColor(1.f, 1.f, 1.f, 0.7f), 12); }
	else
	{
		// the aim on the ground (a ring under the cursor, a line from the hero), gold while a counter is ready
		FVector2D AP, HP;
		if (PlayerOwner->ProjectWorldLocationToScreen(Me->AimPoint, AP) && PlayerOwner->ProjectWorldLocationToScreen(Me->GetActorLocation() - FVector(0.f, 0.f, 60.f), HP))
		{
			const FLinearColor RC = bCounter ? PGold : FLinearColor(1.f, 1.f, 1.f, 0.55f);
			TArray<FVector2D> Ring;
			for (int32 i = 0; i < 20; ++i) { const float A = 2.f * PI * i / 20.f; Ring.Add(FVector2D(AP.X + 16.f * S * FMath::Cos(A), AP.Y + 9.f * S * FMath::Sin(A))); }
			Outline(Ring, RC, 2.f * S);
			const FVector2D D = (AP - HP).GetSafeNormal();
			Line(HP.X + D.X * 40 * S, HP.Y + D.Y * 40 * S, HP.X + D.X * 90 * S, HP.Y + D.Y * 90 * S, RC, 2.f * S);
		}
		// the stone and the wrath, over the player's panel
		const float BX = 30 * S, BY = H - 128 * S - 24 * S - 64 * S;
		Panel(BX, BY, 520 * S, 54 * S);
		const bool bStone = Me->CastAmmo > 0;
		Text(bStone ? TEXT("STONE (RMB): READY") : FString::Printf(TEXT("STONE RETURNS: %.1f s"), FMath::Max(0.f, Me->CastBackAt - Now)), BX + 20 * S, BY + 8 * S, bStone ? PCyan : PGrey, 0.44f * S, false, GEngine->GetMediumFont());
		const bool bFull = Me->Wrath >= T.WrathMax;
		Text(bFull ? TEXT("WRATH (F): READY!") : TEXT("WRATH"), BX + 280 * S, BY + 8 * S, bFull ? PRed : PGrey, 0.44f * S, false, GEngine->GetMediumFont());
		Bar(BX + 280 * S, BY + 32 * S, 220 * S, 8 * S, Me->Wrath / T.WrathMax, bFull ? PRed : FLinearColor(0.85f, 0.3f, 0.2f));
	}
	// the score
	const float CW = 360 * S, CX = (W - CW) * 0.5f;
	Panel(CX, 12 * S, CW, 70 * S);
	Text(FString::Printf(TEXT("YOU  %d  :  %d  BOT"), GM->PlayerKOs, GM->BotKOs), W * 0.5f, 18 * S, PWhite, 1.f * S, true);
	static const TCHAR* Diff[3] = { TEXT("easy"), TEXT("normal"), TEXT("hard") };
	static const TCHAR* Modes[3] = { TEXT("duel"), TEXT("two bots"), TEXT("sandbox") };
	Text(FString::Printf(TEXT("%s · bot %s"), Modes[FMath::Clamp((int32)GM->Mode, 0, 2)], Diff[FMath::Clamp(GM->Difficulty, 0, 2)]), W * 0.5f, 58 * S, PGrey, 0.42f * S, true, GEngine->GetMediumFont());
	// the player's panel: health, stamina, the state and the combo
	const float PX = 30 * S, PW = 520 * S, PH = 128 * S, PY = H - PH - 24 * S;
	Panel(PX, PY, PW, PH);
	Text(TEXT("HEALTH"), PX + 22 * S, PY + 14 * S, PGrey, 0.44f * S, false, GEngine->GetMediumFont());
	Bar(PX + 22 * S, PY + 36 * S, PW - 44 * S, 20 * S, Me->Health / T.MaxHealth, PGreen);
	Text(FString::Printf(TEXT("%.0f / %.0f"), Me->Health, T.MaxHealth), PX + PW * 0.5f, PY + 35 * S, PWhite, 0.46f * S, true, GEngine->GetMediumFont());
	Text(TEXT("STAMINA"), PX + 22 * S, PY + 62 * S, PGrey, 0.44f * S, false, GEngine->GetMediumFont());
	Bar(PX + 22 * S, PY + 84 * S, PW - 44 * S, 12 * S, Me->Stamina / T.MaxStamina, PStam);
	FString State = TEXT("RUN");
	FLinearColor SC = PWhite;
	UCharacterMovementComponent* M = Me->GetCharacterMovement();
	if (!Me->IsAlive()) { State = TEXT("DEFEATED"); SC = PRed; }
	else if (Me->bLaunched) { State = TEXT("LAUNCHED UP"); SC = PRed; }
	else if (Now < Me->StaggerUntil) { State = TEXT("STAGGERED"); SC = PCyan; }
	else if (Me->bCharging) { State = FString::Printf(TEXT("CHARGING %.0f%%"), Me->ChargePct() * 100.f); SC = PGold; }
	else if (Now < Me->DodgeUntil) { State = Me->bTopDown ? TEXT("DASH") : TEXT("DODGE"); SC = PCyan; }
	else if (Now < Me->MantleUntil) { State = TEXT("CLIMB"); }
	else if (M->IsFalling()) { State = Me->bPlunging ? TEXT("GROUND SLAM") : FString::Printf(TEXT("AIRBORNE · slashes %d"), Me->AirSlashesLeft); }
	else if (Me->bSneaking) { State = TEXT("SNEAKING"); SC = PCyan; }
	else if (Me->bSprinting) { State = TEXT("SPRINT"); SC = PStam; }
	else if (Me->IsLocked()) { State = TEXT("LOCK-ON"); SC = PGold; }
	Text(State, PX + 22 * S, PY + 102 * S, SC, 0.5f * S, false, GEngine->GetMediumFont());
	if (Me->Attacking == EProtoAttack::Light && Now < Me->ComboUntil && Me->ComboStep > 0)
	{
		Text(FString::Printf(TEXT("COMBO %d/3"), Me->ComboStep), PX + PW - 22 * S - Measure(TEXT("COMBO 3/3"), 0.8f * S).X, PY + 96 * S, PGold, 0.8f * S, false);
	}
	// the charge under the crosshair (red: full, it launches); the counter window
	if (Me->bCharging) { Bar(W * 0.5f - 120 * S, H * 0.5f + 60 * S, 240 * S, 10 * S, Me->ChargePct(), Me->ChargePct() >= 0.95f ? PRed : PGold); }
	if (bCounter)
	{
		Text(TEXT("COUNTER READY — STRIKE!"), W * 0.5f, H * 0.5f + 78 * S, PGold, 0.6f * S, true);
		Bar(W * 0.5f - 100 * S, H * 0.5f + 106 * S, 200 * S, 5 * S, (Me->CounterUntil - Now) / T.CounterTime, PGold);
	}
	else if (Me->IsLocked() && !Me->bTopDown) { Text(TEXT("wheel: next target"), W * 0.5f, H * 0.5f + 80 * S, PGrey, 0.42f * S, true, GEngine->GetMediumFont()); }
	// the respawn
	if (!Me->IsAlive())
	{
		for (const AProtoGameMode::FRespawn& R : GM->Respawns)
		{
			if (R.Who.Get() == Me) { Text(FString::Printf(TEXT("respawn in %.0f s"), FMath::Max(0.f, R.At - Now)), W * 0.5f, H * 0.4f, PWhite, 1.1f * S, true); }
		}
	}
	Text(TEXT("F1 controls  ·  Esc menu"), W - 24 * S - Measure(TEXT("F1 controls  ·  Esc menu"), 0.46f * S, GEngine->GetMediumFont()).X, H - 36 * S, PGrey, 0.46f * S, false, GEngine->GetMediumFont());
}
