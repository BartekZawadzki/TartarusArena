// The front end on the canvas HUD: the main menu, the play (mode) screen, the hero browser with a live preview of
// each ability, the settings, the pause menu, the training centre's panel and the end-of-match buttons. Every
// button registers its rectangle for the frame (MenuHitAt); AArenaPlayerController::MenuAction does the work.
#include "UI/ArenaHUD.h"
#include "PipelineStateCache.h"
#include "UI/ArenaIconStudio.h"
#include "UI/ArenaSettings.h"
#include "Game/ArenaGameMode.h"
#include "Game/ArenaPlayerController.h"
#include "Heroes/ArenaCharacter.h"
#include "Data/ArenaTypes.h"
#include "Core/ArenaCore.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "Engine/Texture2D.h"

namespace
{
	const FLinearColor MGold(1.f, 0.8f, 0.35f), MGrey(0.66f, 0.67f, 0.72f), MWhite(1.f, 1.f, 1.f), MInk(0.03f, 0.035f, 0.05f, 0.88f);
	const FLinearColor MSel(0.36f, 0.27f, 0.09f, 0.95f), MHover(0.14f, 0.15f, 0.19f, 0.95f), MIdle(0.06f, 0.065f, 0.085f, 0.9f);
}

bool AArenaHUD::FindMenuButton(FName Action, FVector2D& OutCenter) const
{
	for (const TPair<FBox2D, FMenuHit>& H : MenuHits)
	{
		if (H.Value.Action == Action) { OutCenter = H.Key.GetCenter(); return true; }
	}
	return false;
}

AArenaHUD::FMenuHit AArenaHUD::MenuHitAt(const FVector2D& Screen) const
{
	for (int32 i = MenuHits.Num() - 1; i >= 0; --i)   // the last drawn is on top
	{
		if (MenuHits[i].Key.IsInside(Screen)) { return MenuHits[i].Value; }
	}
	return FMenuHit();
}

bool AArenaHUD::Hovered(float X, float Y, float W, float H) const
{
	float MX = -1.f, MY = -1.f;
	return PlayerOwner && PlayerOwner->bShowMouseCursor && PlayerOwner->GetMousePosition(MX, MY) && MX >= X && MX <= X + W && MY >= Y && MY <= Y + H;
}

void AArenaHUD::HitArea(float X, float Y, float W, float H, FName Action, int32 Arg)
{
	MenuHits.Add(TPair<FBox2D, FMenuHit>(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), FMenuHit{ Action, Arg }));
}

void AArenaHUD::Button(const FString& Label, float X, float Y, float W, float H, FName Action, int32 Arg, bool bSelected, bool bEnabled, float TextScale)
{
	// v19: the chamfered button — steel when idle, lit with a gold edge and a gold bar on the left under the mouse, a
	// warm gold body when selected; the label in the HUD's face, shrunk to fit
	{
		const bool bHover = bEnabled && Hovered(X, Y, W, H);
		const float C = FMath::Min(12.f * S, H * 0.3f);
		const TArray<FVector2D> P = ChamferPts(X, Y, W, H, C);
		const FLinearColor Top = bSelected ? FLinearColor(0.56f, 0.4f, 0.14f, 0.96f) : (bHover ? FLinearColor(0.17f, 0.2f, 0.27f, 0.95f) : FLinearColor(0.1f, 0.12f, 0.17f, 0.92f));
		const FLinearColor Bot = bSelected ? FLinearColor(0.24f, 0.15f, 0.04f, 0.96f) : (bHover ? FLinearColor(0.05f, 0.06f, 0.085f, 0.95f) : FLinearColor(0.025f, 0.03f, 0.045f, 0.93f));
		PolyFill(ChamferPts(X + 2.f * S, Y + 3.f * S, W, H, C), Y, Y + H, FLinearColor(0.f, 0.f, 0.f, 0.25f), FLinearColor(0.f, 0.f, 0.f, 0.3f));
		PolyFill(P, Y, Y + H, Top, Bot);
		QuadGrad(X + C, Y, W - C, H * 0.45f, FLinearColor(1.f, 1.f, 1.f, bSelected ? 0.14f : 0.06f), FLinearColor(1.f, 1.f, 1.f, bSelected ? 0.14f : 0.06f), FLinearColor(1.f, 1.f, 1.f, 0.f), FLinearColor(1.f, 1.f, 1.f, 0.f));
		const FLinearColor Edge = bSelected ? MGold : (bHover ? FLinearColor(1.f, 0.85f, 0.5f, 0.95f) : FLinearColor(0.78f, 0.66f, 0.42f, 0.35f));
		Outline(P, Edge, (bSelected || bHover) ? FMath::Max(1.5f, 1.8f * S) : 1.f);
		if (bSelected || bHover) { Rect(X, Y + C, FMath::Max(2.f, 4.f * S), H - C, Edge); }
		float Sc = (TextScale > 0.f ? TextScale : 0.7f) * S;
		FVector2D Sz = Measure(Label, Sc, GEngine->GetMediumFont());
		if (Sz.X > W - 20.f * S && Sz.X > 1.f) { Sc *= (W - 20.f * S) / Sz.X; Sz = Measure(Label, Sc, GEngine->GetMediumFont()); }
		Text(Label, X + W * 0.5f, Y + (H - Sz.Y) * 0.5f, !bEnabled ? FLinearColor(0.4f, 0.4f, 0.44f) : (bSelected ? FLinearColor(1.f, 0.96f, 0.86f) : (bHover ? MGold : MWhite)), Sc, true, GEngine->GetMediumFont());
		if (bEnabled) { HitArea(X, Y, W, H, Action, Arg); }
	}
}

void AArenaHUD::MenuTitle(const FString& Title, const FString& Sub)
{
	// v19: a dark band fading down, the title in carved capitals, a gold rule fading out to both sides with a
	// diamond clasp in its middle
	const float W = Canvas->SizeX;
	QuadGrad(0, 0, W, 150 * S, FLinearColor(0.f, 0.f, 0.02f, 0.8f), FLinearColor(0.f, 0.f, 0.02f, 0.8f), FLinearColor(0.f, 0.f, 0.02f, 0.f), FLinearColor(0.f, 0.f, 0.02f, 0.f));
	Text(Title, W * 0.5f, 16 * S, MGold, 1.7f * S, true, TitleFont());
	const float RY = 124 * S, RW = 420 * S;
	QuadGrad(W * 0.5f - RW, RY, RW, FMath::Max(1.f, 2.f * S), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.9f), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.9f));
	QuadGrad(W * 0.5f, RY, RW, FMath::Max(1.f, 2.f * S), FLinearColor(1.f, 0.8f, 0.35f, 0.9f), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.9f), FLinearColor(1.f, 0.8f, 0.35f, 0.f));
	PolyFill({ FVector2D(W * 0.5f, RY - 9 * S), FVector2D(W * 0.5f + 9 * S, RY + 1 * S), FVector2D(W * 0.5f, RY + 11 * S), FVector2D(W * 0.5f - 9 * S, RY + 1 * S) }, RY - 9 * S, RY + 11 * S, FLinearColor(1.f, 0.9f, 0.55f), FLinearColor(0.7f, 0.5f, 0.18f));
	if (!Sub.IsEmpty()) { Text(Sub, W * 0.5f, 88 * S, FLinearColor(0.85f, 0.86f, 0.9f), 0.62f * S, true, GEngine->GetMediumFont()); }
}

// ---- main menu --------------------------------------------------------------------------------------------------
void AArenaHUD::DrawMainMenu(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	// the arena behind, darkened on the left where the menu stands
	// v19: one smooth gradient (the 48 strips showed as vertical stripes), a gold rule down the menu's left edge
	QuadGrad(0, 0, 820 * S, H, FLinearColor(0.f, 0.f, 0.02f, 0.9f), FLinearColor(0.f, 0.f, 0.02f, 0.f), FLinearColor(0.f, 0.f, 0.02f, 0.9f), FLinearColor(0.f, 0.f, 0.02f, 0.f));
	QuadGrad(0, H * 0.55f, W, H * 0.45f, FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.55f), FLinearColor(0.f, 0.f, 0.f, 0.55f));
	QuadGrad(62 * S, 110 * S, FMath::Max(1.f, 2.f * S), H - 200 * S, FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, 0.7f), FLinearColor(1.f, 0.8f, 0.35f, 0.7f));
	Text(TEXT("TARTARUS ARENA"), 88 * S, 104 * S, MGold, 2.5f * S, false, TitleFont());
	Text(FString::Printf(TEXT("A MOBA  ·  %d heroes  ·  Conquest and Arena  ·  bots and LAN play"), FArenaDatabase::Get().Heroes.Num()), 94 * S, 214 * S, FLinearColor(0.82f, 0.84f, 0.9f), 0.6f * S, false, GEngine->GetMediumFont());
	// a first start on a new shader model compiles the pipeline states: say so instead of looking frozen
	const uint32 Compiling = PipelineStateCache::NumActivePrecacheRequests();
	if (Compiling > 0) { Text(FString::Printf(TEXT("Preparing graphics…  (%u)"), Compiling), 94 * S, 262 * S, FLinearColor(1.f, 0.8f, 0.4f), 0.5f * S, false, GEngine->GetMediumFont()); }
	const float BX = 90 * S, BW = 430 * S, BH = 60 * S, Gap = 12 * S;
	float Y = 320 * S;
	const TPair<const TCHAR*, const TCHAR*> Items[7] = { { TEXT("PLAY"), TEXT("Play") }, { TEXT("HEROES"), TEXT("Heroes") }, { TEXT("TRAINING CENTER"), TEXT("Training") },
		{ TEXT("NEW GAME  ·  PROTOTYPE"), TEXT("Proto") }, { TEXT("NEW GAME 2  ·  HADES CONTROLS"), TEXT("ProtoHades") }, { TEXT("SETTINGS"), TEXT("Settings") }, { PC && PC->IsQuitArmed() ? TEXT("CLICK AGAIN TO QUIT") : TEXT("QUIT"), TEXT("Quit") } };
	for (const TPair<const TCHAR*, const TCHAR*>& It : Items)
	{
		Button(It.Key, BX, Y, BW, BH, FName(It.Value), 0, false, true, 0.85f);
		Y += BH + Gap;
	}
	// hints for the button under the mouse
	const TCHAR* Hints[7] = { TEXT("Match vs bots: pick the mode, length, difficulty and hero."), TEXT("Get to know the heroes: stats, descriptions and a preview of every ability."),
		TEXT("A grey hall with dummies: try a hero with no pressure, no cooldowns, at level 20."),
		TEXT("A separate mode for the new game: a grey mock-up of a small town with three lanes, full movement (run, sprint, jump, climb, sneak) and combat with a bot."),
		TEXT("The same world, characters and combat, but controls like in Hades: top-down camera, cursor aims, LMB attack, RMB cast, Q special, F Wrath, Shift dodge — and jump on Space."), TEXT("Graphics, audio, controls and gameplay."), TEXT("Quit the game.") };
	float HY = 320 * S;
	for (int32 i = 0; i < 7; ++i)
	{
		if (Hovered(BX, HY, BW, BH)) { WrapText(Hints[i], BX, 320 * S + 7 * (BH + Gap) + 10 * S, BW + 80 * S, FLinearColor(0.8f, 0.82f, 0.88f), 0.55f * S); }
		HY += BH + Gap;
	}
	Text(TEXT("Icons: game-icons.net (CC BY 3.0) · characters and environments: Epic Games' free Paragon assets (unofficial, not affiliated with Epic Games) · CREDITS.md"), 90 * S, H - 40 * S, FLinearColor(0.5f, 0.5f, 0.56f), 0.42f * S, false, GEngine->GetMediumFont());
}

// ---- play: mode, length, difficulty ------------------------------------------------------------------------------
void AArenaHUD::DrawPlayMenu(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.72f));
	MenuTitle(TEXT("PLAY"), TEXT("Choose a match mode vs bots"));
	const float PW = 1240 * S, PX = (W - PW) * 0.5f;
	float Y = 170 * S;
	Text(TEXT("MODE"), PX, Y, MGrey, 0.62f * S, false, GEngine->GetMediumFont());
	Y += 36 * S;
	struct FMode { int32 Size; const TCHAR* Name; const TCHAR* Desc; };
	const FMode Modes[4] = {
		{ 105, TEXT("CONQUEST 5V5"), TEXT("Three lanes of towers and inhibitors guarding the core, a jungle with camps and buffs, the Prime Helix boss. Whoever destroys the enemy's core wins.") },
		{ 5, TEXT("ARENA 5V5"), TEXT("Full MOBA match: two teams of five heroes, minion waves, the Power of Tartarus altar, a shop and levels up to 20.") },
		{ 3, TEXT("SKIRMISH 3V3"), TEXT("Smaller teams, more fights per hero: faster decisions, every mistake shows right away.") },
		{ 1, TEXT("DUEL 1V1"), TEXT("You vs one bot and minion waves: learn to trade hits, farm and recall to base.") } };
	const float CW = (PW - 3 * 24 * S) / 4.f, CH = 230 * S;
	for (int32 m = 0; m < 4; ++m)
	{
		const float CX = PX + m * (CW + 24 * S);
		const bool bSel = Modes[m].Size == 105 ? GM->bConquest : (!GM->bConquest && GM->TeamSize == Modes[m].Size);
		const bool bHover = Hovered(CX, Y, CW, CH);
		Rect(CX, Y, CW, CH, bSel ? FLinearColor(0.2f, 0.15f, 0.06f, 0.95f) : (bHover ? MHover : MIdle));
		Frame(CX, Y, CW, CH, bSel ? MGold : FLinearColor(1.f, 1.f, 1.f, bHover ? 0.5f : 0.18f), bSel ? 2.5f * S : 1.f);
		Text(Modes[m].Name, CX + 20 * S, Y + 20 * S, bSel ? MGold : MWhite, 0.8f * S, false);
		WrapText(Modes[m].Desc, CX + 20 * S, Y + 70 * S, CW - 40 * S, FLinearColor(0.8f, 0.82f, 0.88f), 0.5f * S);
		HitArea(CX, Y, CW, CH, TEXT("Mode"), Modes[m].Size);
	}
	Y += CH + 40 * S;
	Text(TEXT("MATCH LENGTH"), PX, Y, MGrey, 0.62f * S, false, GEngine->GetMediumFont());
	Text(TEXT("BOT DIFFICULTY"), PX + PW * 0.5f + 12 * S, Y, MGrey, 0.62f * S, false, GEngine->GetMediumFont());
	Y += 36 * S;
	const int32 Minutes[3] = { 5, 10, 15 };
	const float OW = (PW * 0.5f - 12 * S - 2 * 14 * S) / 3.f;
	for (int32 i = 0; i < 3; ++i)
	{
		Button(FString::Printf(TEXT("%d min"), Minutes[i]), PX + i * (OW + 14 * S), Y, OW, 60 * S, TEXT("Minutes"), Minutes[i], GM->MatchMinutes == Minutes[i]);
		const TCHAR* Diff[3] = { TEXT("Easy"), TEXT("Normal"), TEXT("Hard") };
		Button(Diff[i], PX + PW * 0.5f + 12 * S + i * (OW + 14 * S), Y, OW, 60 * S, TEXT("Difficulty"), i, GM->Difficulty == i);
	}
	Y += 90 * S;
	const int32 Limit = ArenaCore::ScoreLimitFor(GM->MatchMinutes, FArenaDatabase::Get().Rules.ScorePerMinute);
	if (GM->bConquest)
	{
		Text(FString::Printf(TEXT("The team that destroys the enemy's core wins (safety limit %d min)  ·  structures take damage only from basic attacks at melee range"), FArenaDatabase::Get().Rules.Conquest.MatchMinutes),
			W * 0.5f, Y, FLinearColor(0.85f, 0.86f, 0.9f), 0.56f * S, true, GEngine->GetMediumFont());
	}
	else Text(FString::Printf(TEXT("The team that first reaches %d pts, or leads after %d min, wins  ·  hero kill +%d, minion +%d, minion in enemy base +%d"),
		Limit, GM->MatchMinutes, FArenaDatabase::Get().Rules.ScoreHeroKill, FArenaDatabase::Get().Rules.ScoreMinionKill, FArenaDatabase::Get().Rules.ScoreMinionBase),
		W * 0.5f, Y, FLinearColor(0.85f, 0.86f, 0.9f), 0.56f * S, true, GEngine->GetMediumFont());
	// LAN: host this match for a player on another computer, or join one by its address
	Y += 56 * S;
	Text(TEXT("LAN PLAY"), PX, Y, MGrey, 0.62f * S, false, GEngine->GetMediumFont());
	Text(TEXT("a second player on another computer on the same network · bots fill empty slots"), PX + 260 * S, Y + 4 * S, FLinearColor(0.7f, 0.72f, 0.78f), 0.48f * S, false, GEngine->GetMediumFont());
	Y += 36 * S;
	Button(TEXT("HOST LAN MATCH"), PX, Y, 330 * S, 56 * S, TEXT("LanHost"), 0, false, true, 0.62f);
	const bool bTyping = PC && PC->bTypingAddress;
	Button(FString::Printf(TEXT("Address: %s%s"), PC ? *PC->JoinAddress : TEXT(""), bTyping ? TEXT("_") : TEXT("")), PX + 350 * S, Y, 420 * S, 56 * S, TEXT("LanTypeIp"), 0, bTyping, true, 0.58f);
	Button(TEXT("JOIN"), PX + 790 * S, Y, 220 * S, 56 * S, TEXT("LanJoin"), 0, false, true, 0.62f);
	Text(bTyping ? FString(TEXT("type the host's address · Enter: join · Esc: cancel")) : FString::Printf(TEXT("your address on the network: %s (allow the game through Windows Firewall)"), *AArenaHUD::LocalAddress()),
		PX, Y + 64 * S, FLinearColor(0.7f, 0.72f, 0.78f), 0.46f * S, false, GEngine->GetMediumFont());
	const float BY = H - 130 * S;
	Button(TEXT("BACK"), PX, BY, 260 * S, 66 * S, TEXT("Back"), 0);
	Button(TEXT("NEXT: HERO SELECT"), PX + PW - 460 * S, BY, 460 * S, 66 * S, TEXT("Next"), 0, true, true, 0.8f);
}

// ---- hero browser ----------------------------------------------------------------------------------------------
void AArenaHUD::DrawHeroBrowser(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	if (!PC || !Heroes.IsValidIndex(PC->BrowserHero)) { return; }
	const FArenaHeroDef& D = Heroes[PC->BrowserHero];
	const FLinearColor Tint = FMath::Lerp(FArenaDatabase::Hex(D.Tint), MWhite, 0.35f);
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.9f));
	MenuTitle(TEXT("HEROES"), TEXT("Click an ability to see it in action"));
	// the roster
	// one column for a small roster, two for a bigger one
	const bool bTwo = Heroes.Num() > 6;
	const float LS = bTwo ? 70 * S : 108 * S;
	for (int32 i = 0; i < Heroes.Num(); ++i)
	{
		const float LX = 50 * S + (bTwo ? (i % 2) * (LS + 8 * S) : 0.f);
		const float Y = 165 * S + (bTwo ? i / 2 : i) * (LS + (bTwo ? 10 : 16) * S);
		const bool bSel = i == PC->BrowserHero;
		Portrait(i, LX, Y, LS, bSel ? MGold : FLinearColor(1.f, 1.f, 1.f, Hovered(LX, Y, LS, LS) ? 0.7f : 0.25f), !bSel && !Hovered(LX, Y, LS, LS));
		HitArea(LX, Y, LS, LS, TEXT("BrowseHero"), i);
	}
	// the live stage
	const float SX = 200 * S, SY = 160 * S, SW = 600 * S, SH = 750 * S;
	Rect(SX, SY, SW, SH, FLinearColor(0.02f, 0.02f, 0.03f, 1.f));
	if (UTextureRenderTarget2D* RT = Studio ? Studio->PreviewTarget() : nullptr) { Tex(RT, SX, SY, SW, SH); }
	Frame(SX, SY, SW, SH, Tint, 2.f * S);
	if (D.Abilities.IsValidIndex(PC->BrowserSlot))
	{
		Rect(SX, SY + SH - 56 * S, SW, 56 * S, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Text(D.Abilities[PC->BrowserSlot].Name, SX + SW * 0.5f, SY + SH - 46 * S, MGold, 0.72f * S, true, GEngine->GetMediumFont());
	}
	// the card: name, role, difficulty, stats, how to play
	const float RX = 840 * S, RW = W - RX - 50 * S;
	float Y = 160 * S;
	Text(D.DisplayName, RX, Y, MWhite, 1.6f * S, false);
	const FVector2D NameSz = Measure(D.DisplayName, 1.6f * S);
	Glyph(ClassGlyph(D.Class), RX + NameSz.X + 40 * S, Y + 30 * S, 30 * S, Tint);
	Text(FString::Printf(TEXT("%s  ·  %s"), *D.Class, D.Role.IsEmpty() ? TEXT("") : *D.Role), RX + NameSz.X + 66 * S, Y + 16 * S, Tint, 0.62f * S, false, GEngine->GetMediumFont());
	Y += 78 * S;
	Text(TEXT("Difficulty"), RX, Y, MGrey, 0.55f * S, false, GEngine->GetMediumFont());
	for (int32 d = 0; d < 3; ++d) { Rect(RX + 120 * S + d * 28 * S, Y + 6 * S, 20 * S, 12 * S, d < D.Difficulty ? MGold : FLinearColor(1.f, 1.f, 1.f, 0.15f)); }
	Y += 36 * S;
	if (!D.About.IsEmpty()) { Y += WrapText(D.About, RX, Y, RW, FLinearColor(0.86f, 0.88f, 0.93f), 0.56f * S) + 14 * S; }
	// v21: the melee trait, spelled out (the numbers come from the rules)
	if (D.Abilities.IsValidIndex(0) && D.Abilities[0].Range < 5.f)
	{
		const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
		Y += WrapText(FString::Printf(TEXT("Melee: −%.0f%% damage taken from ranged heroes · stuns and slows %.0f%% shorter · dashing grants a shield of %.0f%% health · basic attacks heal %.0f%% of damage dealt"),
			Ru.MeleeRangedResist * 100.f, Ru.MeleeTenacity * 100.f, Ru.MeleeDashShieldPct * 100.f, Ru.MeleeLifesteal * 100.f), RX, Y, RW, FLinearColor(1.f, 0.82f, 0.45f), 0.52f * S) + 12 * S;
	}
	// stats against the roster's best
	float MaxHp = 1.f, MaxPow = 1.f, MaxArm = 1.f, MaxSpd = 1.f, MaxMana = 1.f;
	for (const FArenaHeroDef& O : Heroes) { MaxHp = FMath::Max(MaxHp, O.MaxHealth); MaxPow = FMath::Max(MaxPow, O.Power); MaxArm = FMath::Max(MaxArm, O.Armor); MaxSpd = FMath::Max(MaxSpd, O.MoveSpeed); MaxMana = FMath::Max(MaxMana, O.MaxMana); }
	struct FStat { const TCHAR* Name; float Value; float Max; const TCHAR* Fmt; };
	const FStat Stats[5] = { { TEXT("Health"), D.MaxHealth, MaxHp, TEXT("%.0f") }, { TEXT("Mana"), D.MaxMana, MaxMana, TEXT("%.0f") }, { TEXT("Power"), D.Power, MaxPow, TEXT("%.0f") },
		{ TEXT("Armor"), D.Armor, MaxArm, TEXT("%.0f") }, { TEXT("Speed"), D.MoveSpeed, MaxSpd, TEXT("%.1f m/s") } };
	const float StW = (RW - 30 * S) * 0.5f;
	for (int32 s = 0; s < 5; ++s)
	{
		const float CX = RX + (s % 2) * (StW + 30 * S), CY = Y + (s / 2) * 34 * S;
		Text(Stats[s].Name, CX, CY, MGrey, 0.52f * S, false, GEngine->GetMediumFont());
		Bar(CX + 110 * S, CY + 6 * S, StW - 200 * S, 10 * S, Stats[s].Value / Stats[s].Max, FMath::Lerp(Tint, MGold, 0.3f));
		const FString Val = s == 4 ? FString::Printf(TEXT("%.1f m/s"), Stats[s].Value) : FString::Printf(TEXT("%.0f"), Stats[s].Value);
		Text(Val, CX + StW - 80 * S, CY, MWhite, 0.52f * S, false, GEngine->GetMediumFont());
	}
	Y += 3 * 34 * S + 12 * S;
	// the abilities: click one to see it on the stage
	const TCHAR* Keys[5] = { TEXT("LMB"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") };
	const float RowH = 78 * S;
	for (int32 a = 0; a < 5 && a < D.Abilities.Num(); ++a)
	{
		const FArenaAbilityDef& Ab = D.Abilities[a];
		const bool bSel = a == PC->BrowserSlot;
		const bool bHover = Hovered(RX, Y, RW, RowH - 6 * S);
		Rect(RX, Y, RW, RowH - 6 * S, bSel ? FLinearColor(0.2f, 0.15f, 0.06f, 0.95f) : (bHover ? MHover : MIdle));
		if (bSel) { Frame(RX, Y, RW, RowH - 6 * S, MGold, 2.f * S); }
		AbilityIcon(PC->BrowserHero, a, RX + 8 * S, Y + 6 * S, RowH - 18 * S);
		Text(FString::Printf(TEXT("%s   %s"), Keys[a], *Ab.Name), RX + RowH + 6 * S, Y + 6 * S, a == 4 ? MGold : MWhite, 0.62f * S, false, GEngine->GetMediumFont());
		const FString Meta = FString::Printf(TEXT("cooldown %.0f s%s%s"), Ab.Cooldown, Ab.ManaCost > 0.f ? *FString::Printf(TEXT("  ·  mana %.0f"), Ab.ManaCost) : TEXT(""),
			Ab.Damage > 0.f ? *FString::Printf(TEXT("  ·  damage %.0f"), Ab.Damage) : TEXT(""));
		Text(Meta, RX + RW - 16 * S - Measure(Meta, 0.5f * S, GEngine->GetMediumFont()).X, Y + 8 * S, MGrey, 0.5f * S, false, GEngine->GetMediumFont());
		const FString Desc = Ab.Desc.Len() > 150 ? Ab.Desc.Left(147) + TEXT("...") : Ab.Desc;
		Text(Desc, RX + RowH + 6 * S, Y + 38 * S, FLinearColor(0.8f, 0.82f, 0.88f), 0.48f * S, false, GEngine->GetMediumFont());
		HitArea(RX, Y, RW, RowH - 6 * S, TEXT("BrowseSlot"), a);
		Y += RowH;
	}
	// tips
	if (D.Tips.Num() > 0 && Y < H - 260 * S)
	{
		Y += 8 * S;
		Text(TEXT("HOW TO PLAY"), RX, Y, MGold, 0.58f * S, false, GEngine->GetMediumFont());
		Y += 30 * S;
		for (const FString& T : D.Tips) { if (Y > H - 190 * S) { break; } Y += WrapText(FString::Printf(TEXT("•  %s"), *T), RX, Y, RW, FLinearColor(0.84f, 0.86f, 0.9f), 0.52f * S) + 4 * S; }
	}
	// skins: the pack's alternative bodies (your hero wears the chosen one in your matches)
	if (D.Skins.Num() > 0)
	{
		const int32 Cur = FArenaSettings::Get().SkinFor(D.Id);
		const int32 N = D.Skins.Num() + 1;
		const float KW = FMath::Min(150 * S, (SW - (N - 1) * 6 * S) / N), KY = SY + SH + 10 * S;
		for (int32 k = -1; k < D.Skins.Num(); ++k)
		{
			const FString Name = k < 0 ? FString(TEXT("Default")) : (D.SkinNames.IsValidIndex(k) ? D.SkinNames[k] : FString::Printf(TEXT("Skin %d"), k + 1));
			Button(Name, SX + (k + 1) * (KW + 6 * S), KY, KW, 34 * S, TEXT("PickSkin"), k, Cur == k || (k < 0 && !D.Skins.IsValidIndex(Cur)), true, 0.46f);
		}
	}
	const float BY = H - 110 * S;
	Button(TEXT("BACK"), 200 * S, BY, 260 * S, 66 * S, TEXT("Back"), 0);
	Button(TEXT("TRY IN THE TRAINING CENTER"), W - 50 * S - 620 * S, BY, 620 * S, 66 * S, TEXT("TryHero"), 0, true, true, 0.78f);
}

// ---- settings ----------------------------------------------------------------------------------------------------
void AArenaHUD::DrawSettings(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	const FArenaSettings& St = FArenaSettings::Get();
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.86f));
	MenuTitle(TEXT("SETTINGS"), TEXT("Changes take effect immediately and are saved"));
	const float PW = 1180 * S, PX = FMath::Max(60.f * S, (W - PW - 480.f * S) * 0.5f), LW = 380 * S, VX = PX + LW, VW = PW - LW;   // the controls list at the right
	float Y = 165 * S;
	auto Section = [&](const TCHAR* Name) { Text(Name, PX, Y, MGold, 0.62f * S, false, GEngine->GetMediumFont()); Y += 32 * S; };
	auto Label = [&](const TCHAR* Name) { Text(Name, PX + 12 * S, Y + 8 * S, MWhite, 0.56f * S, false, GEngine->GetMediumFont()); };
	auto Options = [&](const TCHAR* Name, FName Action, const TArray<FString>& Opts, int32 Current)
	{
		Label(Name);
		const float OW = FMath::Min(220 * S, (VW - (Opts.Num() - 1) * 10 * S) / Opts.Num());
		for (int32 i = 0; i < Opts.Num(); ++i) { Button(Opts[i], VX + i * (OW + 10 * S), Y, OW, 36 * S, Action, i, i == Current, true, 0.54f); }
		Y += 41 * S;
	};
	auto Stepper = [&](const TCHAR* Name, FName Action, const FString& Value)
	{
		Label(Name);
		Button(TEXT("–"), VX, Y, 60 * S, 36 * S, Action, -1);
		Rect(VX + 70 * S, Y, 240 * S, 36 * S, MIdle);
		Text(Value, VX + 190 * S, Y + 5 * S, MGold, 0.58f * S, true, GEngine->GetMediumFont());
		Button(TEXT("+"), VX + 320 * S, Y, 60 * S, 36 * S, Action, 1);
		Y += 41 * S;
	};
	auto Toggle = [&](const TCHAR* Name, FName Action, bool bOn)
	{
		Label(Name);
		Button(TEXT("On"), VX, Y, 120 * S, 36 * S, Action, 1, bOn, true, 0.54f);
		Button(TEXT("Off"), VX + 130 * S, Y, 120 * S, 36 * S, Action, 0, !bOn, true, 0.54f);
		Y += 41 * S;
	};
	Section(TEXT("GRAPHICS"));
	Options(TEXT("Quality"), TEXT("SetQuality"), { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic · Lumen") }, St.Quality);
	if (St.NeedsRestart())
	{
		// the shader model is chosen when the game starts: Lumen needs SM6, the other levels run faster on SM5
		Text(St.Quality == 3 ? TEXT("Lumen will turn on after restarting the game") : TEXT("Faster mode (without Lumen) after restarting the game"),
			VX, Y - 4 * S, FLinearColor(1.f, 0.8f, 0.35f), 0.46f * S, false, GEngine->GetMediumFont());
		Y += 16 * S;
	}
	Options(TEXT("Window mode"), TEXT("SetWindow"), { TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") }, St.WindowMode);
	const TArray<FIntPoint> Res = FArenaSettings::Resolutions();
	Stepper(TEXT("Resolution"), TEXT("SetRes"), Res.IsValidIndex(St.Resolution) ? FString::Printf(TEXT("%d × %d"), Res[St.Resolution].X, Res[St.Resolution].Y) : FString(TEXT("Desktop")));
	Stepper(TEXT("Render scale"), TEXT("SetScale"), St.RenderScale <= 0 ? FString(TEXT("auto")) : FString::Printf(TEXT("%d %%%s"), St.RenderScale, St.RenderScale < 100 ? TEXT("  (TSR)") : TEXT("")));
	Options(TEXT("Frame limit"), TEXT("SetFpsIdx"), { TEXT("None"), TEXT("60"), TEXT("120"), TEXT("144") }, St.FpsLimit == 60 ? 1 : (St.FpsLimit == 120 ? 2 : (St.FpsLimit == 144 ? 3 : 0)));
	Toggle(TEXT("Vertical sync"), TEXT("SetVSyncTo"), St.bVSync);
	Y += 2 * S;
	Section(TEXT("AUDIO AND CONTROLS"));
	Stepper(TEXT("Volume"), TEXT("SetVolume"), FString::Printf(TEXT("%.0f %%"), St.Volume * 100.f));
	Stepper(TEXT("Hero voice volume"), TEXT("SetVoice"), FString::Printf(TEXT("%.0f %%"), St.VoiceVolume * 100.f));
	Stepper(TEXT("Mouse sensitivity"), TEXT("SetSens"), FString::Printf(TEXT("%.1f"), St.Sensitivity));
	Toggle(TEXT("Invert Y axis"), TEXT("SetInvertTo"), St.bInvertY);
	Options(TEXT("Ability casting"), TEXT("SetCastMode"), { TEXT("Quick"), TEXT("Instant"), TEXT("With confirmation") }, St.CastMode);
	Y += 6 * S;
	Section(TEXT("GAMEPLAY"));
	Toggle(TEXT("Aim assist"), TEXT("SetAssistTo"), St.bAimAssist);
	Toggle(TEXT("Camera shake"), TEXT("SetShakeTo"), St.bCameraShake);
	Toggle(TEXT("Damage numbers"), TEXT("SetNumbersTo"), St.bDamageNumbers);
	Stepper(TEXT("Field of view (FOV)"), TEXT("SetFov"), FString::Printf(TEXT("%.0f°"), St.Fov));
	Stepper(TEXT("UI scale (match)"), TEXT("SetUiScale"), FString::Printf(TEXT("%.0f %%"), St.UiScale * 100.f));
	Toggle(TEXT("Colorblind mode"), TEXT("SetColorblindTo"), St.bColorblind);
	Toggle(TEXT("Tutorial in match"), TEXT("SetTutorialTo"), St.bTutorial);
	// the controls, for reference (right of the options on a wide screen)
	if (W > PX + PW + 420 * S)
	{
		const float KX = PX + PW + 40 * S;
		float KY = 165 * S;
		Text(TEXT("KEYS  ·  click and press a new one"), KX, KY, MGold, 0.6f * S, false, GEngine->GetMediumFont());
		KY += 34 * S;
		const TArray<FArenaSettings::FKeyDef>& Defs = FArenaSettings::KeyDefs();
		for (int32 i = 0; i < Defs.Num(); ++i)
		{
			const bool bWaiting = PC && PC->CapturingAction == Defs[i].Action;
			Button(bWaiting ? FString(TEXT("…")) : St.KeyFor(Defs[i].Action).GetDisplayName().ToString(), KX, KY, 130 * S, 30 * S, TEXT("SetKey"), i, bWaiting, true, 0.5f);
			Text(Defs[i].Label, KX + 142 * S, KY + 4 * S, FLinearColor(0.85f, 0.87f, 0.92f), 0.48f * S, false, GEngine->GetMediumFont());
			KY += 34 * S;
		}
		Button(TEXT("DEFAULT"), KX, KY + 4 * S, 200 * S, 32 * S, TEXT("SetKeysDefault"), 0, false, true, 0.5f);
		Text(PC && !PC->CapturingAction.IsNone() ? TEXT("press a key  ·  Esc: cancel") : TEXT("Fixed: LMB attack, RMB cancel, Alt description, Ctrl+key rank up, Esc pause"),
			KX, KY + 44 * S, FLinearColor(0.7f, 0.72f, 0.78f), 0.44f * S, false, GEngine->GetMediumFont());
		WrapText(TEXT("Pad: A jump · RT attack · RB/LB/X/Y abilities · LT+button rank up · B shop/recall · Start pause"), KX, KY + 66 * S, W - KX - 40 * S, FLinearColor(0.7f, 0.72f, 0.78f), 0.44f * S);
	}
	Button(TEXT("BACK"), W - 60 * S - 260 * S, H - 86 * S, 260 * S, 56 * S, TEXT("Back"), 0);
}

// ---- pause, training, end ----------------------------------------------------------------------------------------
void AArenaHUD::DrawPauseMenu(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.02f, 0.62f));
	const float PW = 520 * S, PH = 470 * S, PX = (W - PW) * 0.5f, PY = (H - PH) * 0.5f;
	Panel(PX, PY, PW, PH);
	Text(TEXT("PAUSED"), W * 0.5f, PY + 24 * S, MGold, 1.4f * S, true);
	const float BX = PX + 50 * S, BW = PW - 100 * S, BH = 62 * S;
	float Y = PY + 110 * S;
	Button(TEXT("RESUME"), BX, Y, BW, BH, TEXT("Resume"), 0, true); Y += BH + 14 * S;
	Button(TEXT("SETTINGS"), BX, Y, BW, BH, TEXT("Settings"), 0); Y += BH + 14 * S;
	Button(GM && GM->bTraining ? TEXT("EXIT TRAINING") : TEXT("MAIN MENU"), BX, Y, BW, BH, TEXT("ToMenu"), 0); Y += BH + 14 * S;
	Button(PC && PC->IsQuitArmed() ? TEXT("CLICK AGAIN TO QUIT") : TEXT("QUIT GAME"), BX, Y, BW, BH, TEXT("Quit"), 0, PC && PC->IsQuitArmed());
}

void AArenaHUD::DrawTrainingPanel(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float PX = 24 * S, PY = 24 * S, PW = 430 * S, PH = 250 * S;
	Panel(PX, PY, PW, PH, FLinearColor(0.45f, 0.7f, 1.f, 0.9f));
	Text(TEXT("TRAINING CENTER"), PX + 18 * S, PY + 12 * S, FLinearColor(0.6f, 0.85f, 1.f), 0.72f * S, false, GEngine->GetMediumFont());
	Text(TEXT("Damage per second (5 s)"), PX + 18 * S, PY + 52 * S, MGrey, 0.5f * S, false, GEngine->GetMediumFont());
	Text(FString::Printf(TEXT("%.0f"), GM->TrainingDps), PX + PW - 22 * S - Measure(FString::Printf(TEXT("%.0f"), GM->TrainingDps), 1.2f * S).X, PY + 38 * S, MGold, 1.2f * S, false);
	const struct { const TCHAR* Key; const TCHAR* What; bool bOn; } Rows[4] = {
		{ TEXT("F1"), TEXT("No cooldowns or mana"), GM->bTrainNoCooldowns }, { TEXT("F2"), TEXT("Level 20, all ranks"), false },
		{ TEXT("F3"), TEXT("Reset the dummies"), false }, { TEXT("Esc"), TEXT("Menu (exit training)"), false } };
	float Y = PY + 96 * S;
	for (const auto& R : Rows)
	{
		Rect(PX + 18 * S, Y, 52 * S, 30 * S, R.bOn ? MSel : FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Text(R.Key, PX + 44 * S, Y + 4 * S, R.bOn ? MGold : MWhite, 0.5f * S, true, GEngine->GetMediumFont());
		Text(R.What, PX + 84 * S, Y + 4 * S, R.bOn ? MGold : FLinearColor(0.86f, 0.88f, 0.92f), 0.52f * S, false, GEngine->GetMediumFont());
		Y += 36 * S;
	}
}

void AArenaHUD::DrawEndButtons(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	const float BW = 380 * S, BH = 66 * S, Y = H * 0.86f - 10 * S;
	Button(TEXT("PLAY AGAIN"), W * 0.5f - BW - 12 * S, Y, BW, BH, TEXT("Rematch"), 0, true);
	Button(TEXT("MAIN MENU"), W * 0.5f + 12 * S, Y, BW, BH, TEXT("ToMenu"), 0);
}
