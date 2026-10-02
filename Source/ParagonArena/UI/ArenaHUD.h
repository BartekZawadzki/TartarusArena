#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ArenaHUD.generated.h"

class AArenaCharacter;
class AArenaGameMode;
class AArenaPlayerController;
class AArenaIconStudio;
class UTexture;
class UTexture2D;
struct FArenaItemDef;

struct FArenaDamageNumber { FVector World; float Amount = 0.f; float Time = 0.f; bool bCrit = false; bool bByPlayer = false; bool bGold = false; };

/** Canvas HUD: no widget assets. Art comes from AArenaIconStudio (portraits, ability icons, minimap captured from
 *  the library assets at runtime) and a small vector glyph set (items, classes, status). */
UCLASS()
class PARAGONARENA_API AArenaHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	static void AddDamageNumber(const AActor* Context, const FVector& World, float Amount, bool bCrit, bool bByPlayer);
	/** Why a blow on a structure did little or nothing (Conquest), under the crosshair for a moment. */
	static void StructureHint(const AActor* Context, const FString& Text);
	FString HintText;
	float HintAt = -100.f;
	/** Where a blow on the player's hero came from (the red arc around the crosshair, LoL / Smite). */
	static void AddDamageDir(const AActor* Context, const FVector& From);
	/** "+N g" over a unit the player's hero finished (shown whatever the damage-number setting). */
	static void AddGoldNumber(const AActor* Context, const FVector& World, float Amount);
	/** The player's attack landed on Victim: crosshair hit marker (white / yellow crit / red kill), target bar pop. */
	static void NotifyPlayerHit(const AActor* Victim, float Damage, bool bCrit, bool bKill);
	/** Hit markers shown since start (read by the aim lab: one per landed player hit). */
	static int32 HitMarkersShown;

	/** Shop mouse targets (rects from the last drawn frame). */
	enum class EShopHit : uint8 { None, Close, Item, Buy, Sell, Inventory, Potion };
	struct FShopHit { EShopHit Kind = EShopHit::None; int32 Index = INDEX_NONE; };
	FShopHit ShopHitAt(const FVector2D& Screen) const;
	void ShopSelect(int32 Item) { ShopSelected = Item; ShopSelectedInv = INDEX_NONE; }
	void ShopSelectInventory(int32 Pos) { ShopSelectedInv = Pos; }
	int32 ShopSelection() const { return ShopSelected; }
	/** The shop row under the mouse (RMB buys it). */
	int32 ShopHoveredItem() const { return ShopHovered; }
	/** Menu buttons (rects from the last drawn frame): an action name and its argument for the player controller. */
	struct FMenuHit { FName Action; int32 Arg = 0; };
	FMenuHit MenuHitAt(const FVector2D& Screen) const;
	/** The centre of the drawn button with this action (the UI demo clicks it). */
	bool FindMenuButton(FName Action, FVector2D& OutCenter) const;

protected:
	// primitives
	void Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale = 1.f, bool bCenter = false, UFont* Font = nullptr);
	FVector2D Measure(const FString& S, float Scale, UFont* Font = nullptr) const;
	void Rect(float X, float Y, float W, float H, const FLinearColor& C);
	void Frame(float X, float Y, float W, float H, const FLinearColor& C, float T = 1.5f);
	/** v19 style: a quad with a colour per corner; a filled polygon with a vertical gradient; the chamfered outline
	 *  (top-left and bottom-right corners cut) every panel and button uses; its outline; a square icon's frame. */
	void QuadGrad(float X, float Y, float W, float H, const FLinearColor& TL, const FLinearColor& TR, const FLinearColor& BL, const FLinearColor& BR);
	void PolyFill(const TArray<FVector2D>& P, float Y0, float Y1, const FLinearColor& Top, const FLinearColor& Bottom);
	static TArray<FVector2D> ChamferPts(float X, float Y, float W, float H, float C);
	void Outline(const TArray<FVector2D>& P, const FLinearColor& C, float T);
	void ChamferIcon(float X, float Y, float Size, const FLinearColor& Edge, float T, const FLinearColor& Mask);
	UFont* TitleFont() const;
	void Panel(float X, float Y, float W, float H, const FLinearColor& Accent = FLinearColor(0.78f, 0.62f, 0.32f, 0.9f));
	void Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill, const FLinearColor& Back = FLinearColor(0, 0, 0, 0.6f));
	void Tex(UTexture* T, float X, float Y, float W, float H, const FLinearColor& C = FLinearColor::White);
	/** A library icon (white glyph with alpha), tinted. */
	void IconTex(UTexture* T, float X, float Y, float W, float H, const FLinearColor& C);
	UTexture2D* IconTexture(const FString& Name);
	float WrapText(const FString& Str, float X, float Y, float Width, const FLinearColor& C, float Scale);
	TArray<FString> ItemStatLines(const FArenaItemDef& D) const;
	void DrawAbilityTip(AArenaCharacter* Me, int32 Slot, float X, float Bottom);
	void Line(float X1, float Y1, float X2, float Y2, const FLinearColor& C, float T = 2.f);
	void Poly(const TArray<FVector2D>& Pts, float CX, float CY, float Size, const FLinearColor& C, float T, bool bClosed);
	void Disc(float CX, float CY, float R, const FLinearColor& C, int32 Segments = 18);
	void SquareSweep(float X, float Y, float S, float Pct, const FLinearColor& C);   // clockwise pie from 12 o'clock clipped to a square
	void RingSweep(float CX, float CY, float R, float Width, float Pct, const FLinearColor& C);
	void Glyph(const FString& Name, float CX, float CY, float Size, const FLinearColor& C, float T = 2.f);
	FString ClassGlyph(const FString& Class) const;

	// art
	void Portrait(int32 HeroIndex, float X, float Y, float S, const FLinearColor& Border, bool bDim = false);
	void AbilityIcon(int32 HeroIndex, int32 Slot, float X, float Y, float S, bool bLocked = false);
	void ItemIcon(int32 ItemIndex, float X, float Y, float S, bool bDim = false);

	// menus (ArenaMenus.cpp)
	bool Hovered(float X, float Y, float W, float H) const;
	void HitArea(float X, float Y, float W, float H, FName Action, int32 Arg);
	void Button(const FString& Label, float X, float Y, float W, float H, FName Action, int32 Arg, bool bSelected = false, bool bEnabled = true, float TextScale = 0.f);
	void MenuTitle(const FString& Title, const FString& Sub);
	void DrawMainMenu(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawPlayMenu(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawHeroBrowser(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawSettings(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawPauseMenu(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawTrainingPanel(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawEndButtons(AArenaGameMode* GM, AArenaPlayerController* PC);
	TArray<TPair<FBox2D, FMenuHit>> MenuHits;

	// screens
	void DrawHeroSelect(AArenaGameMode* GM);
	void DrawMatch(AArenaGameMode* GM);
	void DrawTopBar(AArenaGameMode* GM);
	void DrawWorldBars();
	void DrawPlayerPanel(AArenaGameMode* GM, AArenaCharacter* Me);
	void DrawDeathPanel(AArenaGameMode* GM);
	void DrawMinimap(AArenaGameMode* GM, AArenaCharacter* Me);
	void DrawKillFeed(AArenaGameMode* GM);
	void DrawAnnouncements(AArenaGameMode* GM);
	void DrawShop(AArenaGameMode* GM, AArenaPlayerController* PC);
	void DrawScoreboard(AArenaGameMode* GM, float Top);
	void DrawNumbers();
	void DrawCrosshair(class AArenaCharacter* Me, float CX, float CY);
	void DrawAimTarget(class AArenaCharacter* Me);
	float HitMarkerTime = -100.f;
	int32 HitMarkerKind = 0;                 // 0 hit, 1 crit, 2 kill
	TWeakObjectPtr<const AActor> LastHitVictim;
	float LastHitTime = -100.f;

	float S = 1.f;                         // scale vs 1080p
	AArenaIconStudio* Studio = nullptr;
	TArray<FArenaDamageNumber> Numbers;
	struct FDamageDir { FVector From = FVector::ZeroVector; float Time = 0.f; };
	TArray<FDamageDir> DamageDirs;
	void DrawDamageDirs(class AArenaCharacter* Me);
	/** Conquest: the range of an enemy tower on the ground when the player's hero comes near it (red: it shoots you). */
	void DrawTowerRanges(class AArenaCharacter* Me);
	void DrawStructureBars();
	void DrawPings(bool bMinimapOnly, float MX = 0.f, float MY = 0.f, float MW = 0.f, float MH = 0.f);
	void DrawTutorial(AArenaGameMode* GM);
	void DrawMatchHighlights(float Top);
	void DrawLoading(class AArenaPlayerController* PC);
	/** This machine's address in the local network (shown to a LAN host). */
	static FString LocalAddress();
	void DrawOffscreenEnemies(class AArenaCharacter* Me);
	// an ability coming off cooldown flashes its icon (LoL / Smite); per slot of the shown hero
	float ReadyAt[5] = { -100.f, -100.f, -100.f, -100.f, -100.f };
	bool bWasCooling[5] = { false, false, false, false, false };
	int32 BarHero = -1;
	TArray<TPair<FBox2D, FShopHit>> ShopHits;
	int32 ShopSelected = INDEX_NONE, ShopSelectedInv = INDEX_NONE, ShopHovered = INDEX_NONE;
	FVector2D LastMouse = FVector2D(-1.f, -1.f);
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> IconCache;
	/** A vertical ramp (clear at the top, opaque at the bottom) made once at run time: the panels' shading. */
	UPROPERTY() TObjectPtr<UTexture2D> ShadeRamp;
	UTexture2D* Ramp();
};
