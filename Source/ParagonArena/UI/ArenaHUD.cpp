#include "UI/ArenaHUD.h"
#include "Game/ArenaEvidence.h"
#include "UI/ArenaIconStudio.h"
#include "Game/ArenaGameMode.h"
#include "Game/ArenaPlayerController.h"
#include "Heroes/ArenaCharacter.h"
#include "Data/ArenaTypes.h"
#include "Core/ArenaCore.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "CanvasItem.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Engine/Texture2D.h"
#include "UI/ArenaSettings.h"
#include "Game/ArenaGameState.h"
#include "Arena/ArenaHitSound.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Components/CapsuleComponent.h"

namespace
{
	const FLinearColor Blue(0.25f, 0.55f, 1.f), Red(1.f, 0.3f, 0.25f), Gold(1.f, 0.8f, 0.35f), GoldDim(0.78f, 0.62f, 0.32f, 0.9f);
	const FLinearColor Ink(0.03f, 0.035f, 0.05f, 0.84f), White(1.f, 1.f, 1.f), Grey(0.62f, 0.62f, 0.66f), ManaBlue(0.3f, 0.55f, 1.f);
	// relative to you: your team blue, the other red (the teams' own colours would swap for a player on team 1)
	const FLinearColor Neutral(1.f, 0.72f, 0.25f), Purple(0.8f, 0.45f, 1.f);
	FLinearColor TeamColor(int32 Team) { return Team == 2 ? Neutral : (Team == AArenaCharacter::LocalTeam ? Blue : FArenaSettings::Get().EnemyColor()); }   // the camps neutral; orange enemies in the colour-blind mode
	FString Clock(float Seconds) { const int32 T = FMath::Max(0, FMath::CeilToInt(Seconds)); return FString::Printf(TEXT("%d:%02d"), T / 60, T % 60); }

	/** Vector glyphs on a [-1, 1] box, y down. */
	TArray<FVector2D> Pts(std::initializer_list<FVector2D> L) { return TArray<FVector2D>(L); }
	TArray<FVector2D> Arc(float CX, float CY, float R, float A0, float A1, int32 N)
	{
		TArray<FVector2D> P;
		for (int32 i = 0; i <= N; ++i) { const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, float(i) / N)); P.Add(FVector2D(CX + R * FMath::Cos(A), CY + R * FMath::Sin(A))); }
		return P;
	}
}

int32 AArenaHUD::HitMarkersShown = 0;

void AArenaHUD::NotifyPlayerHit(const AActor* Victim, float Damage, bool bCrit, bool bKill)
{
	++HitMarkersShown;
	if (!Victim || !Victim->GetWorld()) { return; }
	APlayerController* PC = Victim->GetWorld()->GetFirstPlayerController();
	if (AArenaHUD* HUD = PC ? Cast<AArenaHUD>(PC->GetHUD()) : nullptr)
	{
		const float Now = Victim->GetWorld()->GetTimeSeconds();
		// a kill marker is not overwritten by the next plain hit for a moment
		if (!(HUD->HitMarkerKind == 2 && Now - HUD->HitMarkerTime < 0.35f)) { HUD->HitMarkerKind = bKill ? 2 : (bCrit ? 1 : 0); }
		HUD->HitMarkerTime = Now;
		HUD->LastHitVictim = Victim;
		HUD->LastHitTime = Now;
	}
}

void AArenaHUD::AddDamageNumber(const AActor* Context, const FVector& World, float Amount, bool bCrit, bool bByPlayer)
{
	if (!Context || !Context->GetWorld()) { return; }
	APlayerController* PC = Context->GetWorld()->GetFirstPlayerController();
	if (AArenaHUD* HUD = PC ? Cast<AArenaHUD>(PC->GetHUD()) : nullptr)
	{
		FArenaDamageNumber N; N.World = World + FVector(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 0.f);
		N.Amount = Amount; N.Time = Context->GetWorld()->GetTimeSeconds(); N.bCrit = bCrit; N.bByPlayer = bByPlayer;
		HUD->Numbers.Add(N);
		if (HUD->Numbers.Num() > 80) { HUD->Numbers.RemoveAt(0); }
	}
}





// ---- primitives ----------------------------------------------------------------------------------------
namespace
{
	/** The HUD's face: the engine's Roboto (a runtime composite font) rasterized at the exact pixel size it is drawn
	 *  at. The canvas used to draw the legacy engine fonts at their own size and stretch the glyphs by the HUD scale
	 *  (x1.7 for the small labels, x2 at 4K): blurry text. */
	const UObject* HudFace()
	{
		static TWeakObjectPtr<UFont> Face;
		if (!Face.IsValid()) { Face = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"), nullptr, LOAD_NoWarn | LOAD_Quiet); }
		return Face.Get();
	}

	/** v17: one type family for the whole UI, like Paragon's condensed sans: Barlow Condensed for titles, numbers and
	 *  the clock (what used to be the large font), Barlow for labels and text (the medium font). Built at run time
	 *  from Content/Data/Fonts (SIL OFL 1.1, CREDITS.md; the folder is staged as plain files, as the engine's own Slate
	 *  fonts are); Roboto stays the fallback. "Regular" / "Bold" are the two weights HudFont picks by size. */
	// (the canvas draws text only for a UFont object: a composite font alone was dropped without a word, so each
	//  family is a transient runtime-cache UFont holding the composite font, kept alive by the root set)
	struct FHudFonts { UFont* Body = nullptr; UFont* Head = nullptr; UFont* Title = nullptr; bool bRajdhani = false; bool bTried = false; };
	const FHudFonts& HudFonts()
	{
		static FHudFonts F;
		if (!F.bTried)
		{
			F.bTried = true;
			const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Fonts");
			auto Make = [&Dir](const TCHAR* Regular, const TCHAR* Bold) -> UFont*
			{
				if (!FPaths::FileExists(Dir / Regular) || !FPaths::FileExists(Dir / Bold)) { return nullptr; }
				UFont* Font = NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient);
				Font->FontCacheType = EFontCacheType::Runtime;
				Font->LegacyFontSize = 24;
				Font->CompositeFont.DefaultTypeface.AppendFont(FName(TEXT("Regular")), Dir / Regular, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
				Font->CompositeFont.DefaultTypeface.AppendFont(FName(TEXT("Bold")), Dir / Bold, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
				Font->AddToRoot();
				return Font;
			};
			// v19: Rajdhani (a squared, technical sans, like Paragon's HUD) for text and numbers, Cinzel's carved
			// capitals for the titles; Barlow stays the fallback (all SIL OFL 1.1, CREDITS.md)
			F.Body = Make(TEXT("Rajdhani-SemiBold.ttf"), TEXT("Rajdhani-Bold.ttf"));
			F.Head = Make(TEXT("Rajdhani-Bold.ttf"), TEXT("Rajdhani-Bold.ttf"));
			F.bRajdhani = F.Body && F.Head;
			if (!F.Body) { F.Body = Make(TEXT("Barlow-Medium.ttf"), TEXT("Barlow-SemiBold.ttf")); }
			if (!F.Head) { F.Head = Make(TEXT("BarlowCondensed-SemiBold.ttf"), TEXT("BarlowCondensed-Bold.ttf")); }
			F.Title = Make(TEXT("Cinzel-Bold.ttf"), TEXT("Cinzel-Black.ttf"));
			ARENA_LOG(LogTemp, Display, TEXT("ARENA evt=hud_fonts body=%d head=%d title=%d rajdhani=%d dir=%s"), F.Body ? 1 : 0, F.Head ? 1 : 0, F.Title ? 1 : 0, F.bRajdhani ? 1 : 0, *Dir);
		}
		return F;
	}

	/** Pixel size = the size the old stretched text had (legacy size x scale): the layout does not move. Labels of
	 *  15 px and more are bold; a thin dark outline instead of the soft drop shadow keeps them sharp on any ground. */
	/** 0 text, 1 heading / number, 2 title. */
	int32 HudRole(const UFont* Font)
	{
		if (!Font) { return 1; }
		const FHudFonts& Fonts = HudFonts();
		return Fonts.Title && Font == Fonts.Title ? 2 : 0;
	}

	bool HudFont(int32 Role, float Sc, FSlateFontInfo& Out)
	{
		const UFont* Legacy = GEngine ? GEngine->GetLargeFont() : nullptr;
		const UObject* Face = HudFace();
		if (!Face || !FSlateApplication::IsInitialized()) { return false; }
		// never below 12 px at 1080p (the smallest labels were 9-10 px), scaled with the screen height
		const float MinPx = GEngine && GEngine->GameViewport ? 12.f * FMath::Max(0.75f, GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY().Y / 1080.f : 1.f) : 12.f;
		const bool bTitle = Role == 2, bHead = Role == 1;
		const float Px = FMath::Max(MinPx, (Legacy && Legacy->LegacyFontSize > 0 ? Legacy->LegacyFontSize : 24) * Sc);
		const FHudFonts& Fonts = HudFonts();
		const UFont* Family = bTitle ? (Fonts.Title ? Fonts.Title : Fonts.Head) : (bHead ? Fonts.Head : Fonts.Body);
		// the families' own sizes against the layout measured with Roboto: Rajdhani draws small (x-height ~0.45 em)
		// (v19, operator: "the font is much too big": the layout was sized for a stretched Roboto; a fifth off the text, a
		// quarter off the titles)
		const float Fit = bTitle ? (Fonts.Title ? 0.7f : 0.85f) : (Fonts.bRajdhani ? (bHead ? 0.92f : 0.94f) : (bHead ? 0.88f : 0.84f));
		const int32 Size = FMath::Clamp(FMath::RoundToInt(Px * (Family ? Fit : 1.f)), 7, 170);
		const FName Weight = (bTitle || Size >= 15) ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
		if (Family)
		{
			Out = FSlateFontInfo(Family, Size, Weight);
			Out.LetterSpacing = bTitle ? 90 : (bHead ? 30 : 12);   // carved titles wide, numbers and labels a touch airy
		}
		else { Out = FSlateFontInfo(Face, Size, Weight); }
		Out.OutlineSettings.OutlineSize = bTitle ? FMath::Max(2, Size / 18) : (Size >= 26 ? 2 : 1);
		Out.OutlineSettings.OutlineColor = bTitle ? FLinearColor(0.08f, 0.04f, 0.f, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.75f);
		return true;
	}
}

void AArenaHUD::Text(const FString& Str, float X, float Y, const FLinearColor& C, float Scale, bool bCenter, UFont* Font)
{
	const int32 TextRole = HudRole(Font);
	UFont* F = GEngine->GetLargeFont();
	const float Sc = Scale * 1.7f;   // every call was laid out with the stretch the (single) engine font got
	FSlateFontInfo Info;
	if (HudFont(TextRole, Sc, Info))
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Str), Info, C);
		Item.bCentreX = bCenter;
		Canvas->DrawItem(Item);
		return;
	}
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Str), F, C);
	Item.Scale = FVector2D(Sc, Sc);
	Item.bCentreX = bCenter;
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Canvas->DrawItem(Item);
}

FVector2D AArenaHUD::Measure(const FString& Str, float Scale, UFont* Font) const
{
	const int32 TextRole = HudRole(Font);
	UFont* F = GEngine->GetLargeFont();
	const float Sc = Scale * 1.7f;
	FSlateFontInfo Info;
	if (HudFont(TextRole, Sc, Info) && FSlateApplication::Get().GetRenderer())
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, Info);
	}
	float W = 0.f, H = 0.f;
	Canvas->TextSize(F, Str, W, H, Sc, Sc);
	return FVector2D(W, H);
}

void AArenaHUD::Rect(float X, float Y, float W, float H, const FLinearColor& C)
{
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), C);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AArenaHUD::Frame(float X, float Y, float W, float H, const FLinearColor& C, float T)
{
	Rect(X, Y, W, T, C); Rect(X, Y + H - T, W, T, C); Rect(X, Y, T, H, C); Rect(X + W - T, Y, T, H, C);
}

UTexture2D* AArenaHUD::Ramp()
{
	if (ShadeRamp) { return ShadeRamp; }
	constexpr int32 RW = 4, RH = 64;
	ShadeRamp = UTexture2D::CreateTransient(RW, RH, PF_B8G8R8A8);
	if (!ShadeRamp) { return nullptr; }
	ShadeRamp->Filter = TF_Bilinear;
	ShadeRamp->AddressX = TA_Clamp;
	ShadeRamp->AddressY = TA_Clamp;
	ShadeRamp->SRGB = false;
	FTexture2DMipMap& Mip = ShadeRamp->GetPlatformData()->Mips[0];
	FColor* Px = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 y = 0; y < RH; ++y)
	{
		const float T = y / float(RH - 1);
		for (int32 x = 0; x < RW; ++x) { Px[y * RW + x] = FColor(255, 255, 255, (uint8)FMath::RoundToInt(255.f * T * T * (3.f - 2.f * T))); }   // smoothstep
	}
	Mip.BulkData.Unlock();
	ShadeRamp->UpdateResource();
	return ShadeRamp;
}

UFont* AArenaHUD::TitleFont() const { return HudFonts().Title; }   // a marker too: Text() draws it in Cinzel

void AArenaHUD::QuadGrad(float X, float Y, float W, float H, const FLinearColor& TL, const FLinearColor& TR, const FLinearColor& BL, const FLinearColor& BR)
{
	TArray<FCanvasUVTri> Tris;
	FCanvasUVTri A; A.V0_Pos = FVector2D(X, Y); A.V1_Pos = FVector2D(X + W, Y); A.V2_Pos = FVector2D(X, Y + H); A.V0_Color = TL; A.V1_Color = TR; A.V2_Color = BL; Tris.Add(A);
	FCanvasUVTri B; B.V0_Pos = FVector2D(X + W, Y); B.V1_Pos = FVector2D(X + W, Y + H); B.V2_Pos = FVector2D(X, Y + H); B.V0_Color = TR; B.V1_Color = BR; B.V2_Color = BL; Tris.Add(B);
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AArenaHUD::PolyFill(const TArray<FVector2D>& P, float Y0, float Y1, const FLinearColor& Top, const FLinearColor& Bottom)
{
	if (P.Num() < 3) { return; }
	FVector2D C(0.f, 0.f);
	for (const FVector2D& V : P) { C += V; }
	C /= P.Num();
	auto At = [&](float Y) { return FMath::Lerp(Top, Bottom, FMath::Clamp((Y - Y0) / FMath::Max(1.f, Y1 - Y0), 0.f, 1.f)); };
	TArray<FCanvasUVTri> Tris;
	for (int32 i = 0; i < P.Num(); ++i)
	{
		const FVector2D& A = P[i]; const FVector2D& B = P[(i + 1) % P.Num()];
		FCanvasUVTri T; T.V0_Pos = C; T.V1_Pos = A; T.V2_Pos = B; T.V0_Color = At(C.Y); T.V1_Color = At(A.Y); T.V2_Color = At(B.Y);
		Tris.Add(T);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

TArray<FVector2D> AArenaHUD::ChamferPts(float X, float Y, float W, float H, float C)
{
	return { FVector2D(X + C, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H - C), FVector2D(X + W - C, Y + H), FVector2D(X, Y + H), FVector2D(X, Y + C) };
}

void AArenaHUD::Outline(const TArray<FVector2D>& P, const FLinearColor& C, float T)
{
	for (int32 i = 0; i < P.Num(); ++i) { Line(P[i].X, P[i].Y, P[(i + 1) % P.Num()].X, P[(i + 1) % P.Num()].Y, C, T); }
}

void AArenaHUD::ChamferIcon(float X, float Y, float Size, const FLinearColor& Edge, float T, const FLinearColor& Mask)
{
	// the icon's cut corners painted over in the panel's colour, then the chamfered edge
	const float C = Size * 0.2f;
	TArray<FCanvasUVTri> Tris;
	FCanvasUVTri A; A.V0_Pos = FVector2D(X, Y); A.V1_Pos = FVector2D(X + C, Y); A.V2_Pos = FVector2D(X, Y + C); A.V0_Color = A.V1_Color = A.V2_Color = Mask; Tris.Add(A);
	FCanvasUVTri B; B.V0_Pos = FVector2D(X + Size, Y + Size); B.V1_Pos = FVector2D(X + Size - C, Y + Size); B.V2_Pos = FVector2D(X + Size, Y + Size - C); B.V0_Color = B.V1_Color = B.V2_Color = Mask; Tris.Add(B);
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
	Outline(ChamferPts(X, Y, Size, Size, C), Edge, T);
}

void AArenaHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Accent)
{
	// v19: the chamfered card — a soft shadow, a slate body lighter at the top, a lit top edge, a thin accent outline,
	// the accent stronger on the two cut corners and along the edges next to them
	const float C = FMath::Min(16.f * S, FMath::Min(W, H) * 0.22f);
	const TArray<FVector2D> P = ChamferPts(X, Y, W, H, C);
	PolyFill(ChamferPts(X + 3.f * S, Y + 5.f * S, W, H, C), Y, Y + H, FLinearColor(0.f, 0.f, 0.f, 0.26f), FLinearColor(0.f, 0.f, 0.f, 0.36f));
	PolyFill(P, Y, Y + H, FLinearColor(0.085f, 0.1f, 0.145f, 0.93f), FLinearColor(0.02f, 0.024f, 0.038f, 0.95f));
	QuadGrad(X + C, Y, W - C, FMath::Min(26.f * S, H * 0.35f), FLinearColor(1.f, 1.f, 1.f, 0.06f), FLinearColor(1.f, 1.f, 1.f, 0.06f), FLinearColor(1.f, 1.f, 1.f, 0.f), FLinearColor(1.f, 1.f, 1.f, 0.f));
	Outline(P, Accent * FLinearColor(1.f, 1.f, 1.f, 0.5f), FMath::Max(1.f, 1.2f * S));
	const float T = FMath::Max(2.f, 2.6f * S), L = FMath::Min(34.f * S, FMath::Min(W, H) * 0.35f);
	Line(X, Y + C, X + C, Y, Accent, T);
	Line(X + C, Y, X + C + L, Y, Accent, T);
	Line(X, Y + C, X, Y + C + L, Accent, T);
	Line(X + W, Y + H - C, X + W - C, Y + H, Accent, T);
	Line(X + W - C, Y + H, X + W - C - L, Y + H, Accent, T);
	Line(X + W, Y + H - C, X + W, Y + H - C - L, Accent, T);
}

void AArenaHUD::Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill, const FLinearColor& Back)
{
	// v19: a sunken trough, the fill bright at the top and deep at the foot, a gloss line, a lit leading edge
	Rect(X, Y, W, H, Back);
	QuadGrad(X, Y, W, H * 0.45f, FLinearColor(0.f, 0.f, 0.f, 0.35f), FLinearColor(0.f, 0.f, 0.f, 0.35f), FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f));
	const float P = FMath::Clamp(Pct, 0.f, 1.f);
	if (P > 0.f)
	{
		const FLinearColor Hi = FLinearColor(FMath::Min(1.f, Fill.R * 1.25f + 0.08f), FMath::Min(1.f, Fill.G * 1.25f + 0.08f), FMath::Min(1.f, Fill.B * 1.25f + 0.08f), Fill.A);
		const FLinearColor Lo = FLinearColor(Fill.R * 0.55f, Fill.G * 0.55f, Fill.B * 0.55f, Fill.A);
		QuadGrad(X, Y, W * P, H, Hi, Hi, Lo, Lo);
		Rect(X, Y + FMath::Max(1.f, H * 0.12f), W * P, FMath::Max(1.f, H * 0.1f), FLinearColor(1.f, 1.f, 1.f, 0.28f));   // gloss
		if (P < 1.f && H >= 6.f) { Rect(X + W * P - FMath::Max(1.f, 1.5f * S), Y, FMath::Max(1.f, 1.5f * S), H, FLinearColor(1.f, 1.f, 1.f, 0.55f)); }   // the lit edge
	}
	Frame(X, Y, W, H, FLinearColor(0.f, 0.f, 0.f, 0.8f), 1.f);
}

void AArenaHUD::Tex(UTexture* T, float X, float Y, float W, float H, const FLinearColor& C)
{
	if (!T || !T->GetResource()) { return; }
	FCanvasTileItem Tile(FVector2D(X, Y), T->GetResource(), FVector2D(W, H), C);
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);
}

void AArenaHUD::IconTex(UTexture* T, float X, float Y, float W, float H, const FLinearColor& C)
{
	if (!T || !T->GetResource()) { return; }
	FCanvasTileItem Tile(FVector2D(X, Y), T->GetResource(), FVector2D(W, H), C);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

UTexture2D* AArenaHUD::IconTexture(const FString& Name)
{
	// game-icons.net (CC BY 3.0, CREDITS.md), imported by Tools/import_icons.py
	if (Name.IsEmpty()) { return nullptr; }
	if (const TObjectPtr<UTexture2D>* Found = IconCache.Find(Name)) { return Found->Get(); }
	const FString Asset = TEXT("T_Icon_") + Name.Replace(TEXT("-"), TEXT("_"));
	UTexture2D* T = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Arena/Icons/%s.%s"), *Asset, *Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
	IconCache.Add(Name, T);
	return T;
}

void AArenaHUD::Line(float X1, float Y1, float X2, float Y2, const FLinearColor& C, float T)
{
	FCanvasLineItem L(FVector2D(X1, Y1), FVector2D(X2, Y2));
	L.SetColor(C);
	L.LineThickness = T;
	Canvas->DrawItem(L);
}

void AArenaHUD::Poly(const TArray<FVector2D>& P, float CX, float CY, float Size, const FLinearColor& C, float T, bool bClosed)
{
	const float H = Size * 0.5f;
	for (int32 i = 0; i + 1 < P.Num(); ++i) { Line(CX + P[i].X * H, CY + P[i].Y * H, CX + P[i + 1].X * H, CY + P[i + 1].Y * H, C, T); }
	if (bClosed && P.Num() > 2) { Line(CX + P.Last().X * H, CY + P.Last().Y * H, CX + P[0].X * H, CY + P[0].Y * H, C, T); }
}

void AArenaHUD::Disc(float CX, float CY, float R, const FLinearColor& C, int32 Segments)
{
	TArray<FCanvasUVTri> Tris;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A0 = 2.f * PI * i / Segments, A1 = 2.f * PI * (i + 1) / Segments;
		FCanvasUVTri T;
		T.V0_Pos = FVector2D(CX, CY); T.V1_Pos = FVector2D(CX + R * FMath::Cos(A0), CY + R * FMath::Sin(A0)); T.V2_Pos = FVector2D(CX + R * FMath::Cos(A1), CY + R * FMath::Sin(A1));
		T.V0_Color = T.V1_Color = T.V2_Color = C;
		Tris.Add(T);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AArenaHUD::SquareSweep(float X, float Y, float Size, float Pct, const FLinearColor& C)
{
	const float P = FMath::Clamp(Pct, 0.f, 1.f);
	if (P <= 0.f) { return; }
	const FVector2D Ctr(X + Size * 0.5f, Y + Size * 0.5f);
	const float H = Size * 0.5f;
	const int32 Steps = FMath::Max(2, FMath::CeilToInt(P * 48.f));
	TArray<FCanvasUVTri> Tris;
	auto Edge = [&](float A) { const FVector2D D(FMath::Sin(A), -FMath::Cos(A)); return Ctr + D * (H / FMath::Max(FMath::Abs(D.X), FMath::Abs(D.Y))); };
	// the remaining cooldown is the part still dark: from the current angle round to 12 o'clock
	const float Start = 2.f * PI * (1.f - P);
	for (int32 i = 0; i < Steps; ++i)
	{
		const float A0 = FMath::Lerp(Start, 2.f * PI, float(i) / Steps), A1 = FMath::Lerp(Start, 2.f * PI, float(i + 1) / Steps);
		FCanvasUVTri T; T.V0_Pos = Ctr; T.V1_Pos = Edge(A0); T.V2_Pos = Edge(A1);
		T.V0_Color = T.V1_Color = T.V2_Color = C;
		Tris.Add(T);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AArenaHUD::RingSweep(float CX, float CY, float R, float Width, float Pct, const FLinearColor& C)
{
	const float P = FMath::Clamp(Pct, 0.f, 1.f);
	const int32 Steps = FMath::Max(2, FMath::CeilToInt(P * 64.f));
	TArray<FCanvasUVTri> Tris;
	for (int32 i = 0; i < Steps; ++i)
	{
		const float A0 = -PI * 0.5f + 2.f * PI * P * i / Steps, A1 = -PI * 0.5f + 2.f * PI * P * (i + 1) / Steps;
		const FVector2D O0(CX + R * FMath::Cos(A0), CY + R * FMath::Sin(A0)), O1(CX + R * FMath::Cos(A1), CY + R * FMath::Sin(A1));
		const FVector2D I0(CX + (R - Width) * FMath::Cos(A0), CY + (R - Width) * FMath::Sin(A0)), I1(CX + (R - Width) * FMath::Cos(A1), CY + (R - Width) * FMath::Sin(A1));
		FCanvasUVTri A; A.V0_Pos = O0; A.V1_Pos = O1; A.V2_Pos = I0; A.V0_Color = A.V1_Color = A.V2_Color = C; Tris.Add(A);
		FCanvasUVTri B; B.V0_Pos = I0; B.V1_Pos = O1; B.V2_Pos = I1; B.V0_Color = B.V1_Color = B.V2_Color = C; Tris.Add(B);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AArenaHUD::Glyph(const FString& Name, float CX, float CY, float Size, const FLinearColor& C, float T)
{
	const float Th = T * S;
	if (Name == TEXT("sword") || Name == TEXT("greatsword") || Name == TEXT("dagger"))
	{
		const float L = Name == TEXT("dagger") ? 0.55f : 0.8f, G = Name == TEXT("greatsword") ? 0.5f : 0.35f;
		Poly(Pts({ { -L + 0.1f, L - 0.1f }, { L, -L } }), CX, CY, Size, C, Th * (Name == TEXT("greatsword") ? 2.2f : 1.6f), false);   // blade
		Poly(Pts({ { -0.45f - G * 0.3f, 0.15f - G * 0.3f }, { -0.15f + G * 0.3f, 0.45f + G * 0.3f } }), CX, CY, Size, C, Th * 1.4f, false);   // guard
		Poly(Pts({ { -0.3f, 0.3f }, { -0.8f, 0.8f } }), CX, CY, Size, C, Th * 1.2f, false);   // grip
	}
	else if (Name == TEXT("shield") || Name == TEXT("aegis"))
	{
		const TArray<FVector2D> P = Pts({ { 0.f, -0.9f }, { 0.75f, -0.6f }, { 0.7f, 0.2f }, { 0.f, 0.9f }, { -0.7f, 0.2f }, { -0.75f, -0.6f } });
		Poly(P, CX, CY, Size, C, Th, true);
		if (Name == TEXT("aegis")) { Poly(P, CX, CY, Size * 0.55f, C, Th, true); }
		else { Poly(Pts({ { 0.f, -0.6f }, { 0.f, 0.6f } }), CX, CY, Size, C, Th, false); }
	}
	else if (Name == TEXT("armor"))
	{
		Poly(Pts({ { -0.8f, -0.7f }, { -0.35f, -0.8f }, { 0.f, -0.5f }, { 0.35f, -0.8f }, { 0.8f, -0.7f }, { 0.62f, 0.85f }, { -0.62f, 0.85f } }), CX, CY, Size, C, Th, true);
		Poly(Pts({ { 0.f, -0.5f }, { 0.f, 0.85f } }), CX, CY, Size, C, Th, false);
		Poly(Pts({ { -0.55f, 0.1f }, { 0.55f, 0.1f } }), CX, CY, Size, C, Th, false);
	}
	else if (Name == TEXT("heart"))
	{
		TArray<FVector2D> P;
		for (int32 i = 0; i < 28; ++i)
		{
			const float A = 2.f * PI * i / 28.f;
			P.Add(FVector2D(16.f * FMath::Pow(FMath::Sin(A), 3) / 17.f, -(13.f * FMath::Cos(A) - 5.f * FMath::Cos(2 * A) - 2.f * FMath::Cos(3 * A) - FMath::Cos(4 * A)) / 17.f + 0.05f));
		}
		Poly(P, CX, CY, Size, C, Th, true);
	}
	else if (Name == TEXT("boots"))
	{
		Poly(Pts({ { -0.3f, -0.9f }, { 0.15f, -0.9f }, { 0.15f, 0.3f }, { 0.85f, 0.5f }, { 0.85f, 0.85f }, { -0.35f, 0.85f } }), CX, CY, Size, C, Th, true);
		Poly(Pts({ { -0.3f, -0.55f }, { 0.15f, -0.55f } }), CX, CY, Size, C, Th, false);
		Poly(Pts({ { -0.75f, -0.2f }, { -0.45f, -0.2f } }), CX, CY, Size, C, Th, false);   // speed lines
		Poly(Pts({ { -0.85f, 0.2f }, { -0.5f, 0.2f } }), CX, CY, Size, C, Th, false);
	}
	else if (Name == TEXT("crystal"))
	{
		Poly(Pts({ { 0.f, -0.95f }, { 0.55f, -0.25f }, { 0.f, 0.95f }, { -0.55f, -0.25f } }), CX, CY, Size, C, Th, true);
		Poly(Pts({ { -0.55f, -0.25f }, { 0.f, -0.05f }, { 0.55f, -0.25f } }), CX, CY, Size, C, Th, false);
		Poly(Pts({ { 0.f, -0.05f }, { 0.f, 0.95f } }), CX, CY, Size, C, Th * 0.7f, false);
	}
	else if (Name == TEXT("bow"))
	{
		const TArray<FVector2D> A = Arc(0.25f, 0.f, 0.9f, 115.f, 245.f, 14);
		Poly(A, CX, CY, Size, C, Th * 1.3f, false);
		Poly(Pts({ A[0], A.Last() }), CX, CY, Size, C, Th * 0.6f, false);                                      // string
		Poly(Pts({ { -0.75f, 0.f }, { 0.9f, 0.f } }), CX, CY, Size, C, Th, false);                               // arrow
		Poly(Pts({ { 0.6f, -0.22f }, { 0.9f, 0.f }, { 0.6f, 0.22f } }), CX, CY, Size, C, Th, false);
	}
	else if (Name == TEXT("hourglass"))
	{
		Poly(Pts({ { -0.6f, -0.85f }, { 0.6f, -0.85f }, { -0.6f, 0.85f }, { 0.6f, 0.85f } }), CX, CY, Size, C, Th, true);
		Poly(Pts({ { -0.75f, -0.85f }, { 0.75f, -0.85f } }), CX, CY, Size, C, Th * 1.4f, false);
		Poly(Pts({ { -0.75f, 0.85f }, { 0.75f, 0.85f } }), CX, CY, Size, C, Th * 1.4f, false);
	}
	else if (Name == TEXT("chalice"))
	{
		Poly(Pts({ { -0.65f, -0.8f }, { 0.65f, -0.8f }, { 0.45f, -0.1f }, { 0.12f, 0.1f }, { 0.12f, 0.6f }, { 0.45f, 0.85f }, { -0.45f, 0.85f }, { -0.12f, 0.6f }, { -0.12f, 0.1f }, { -0.45f, -0.1f } }), CX, CY, Size, C, Th, true);
		Disc(CX, CY - Size * 0.22f, Size * 0.1f, C);
	}
	else if (Name == TEXT("crown"))
	{
		Poly(Pts({ { -0.85f, 0.6f }, { -0.85f, -0.5f }, { -0.4f, 0.f }, { 0.f, -0.75f }, { 0.4f, 0.f }, { 0.85f, -0.5f }, { 0.85f, 0.6f } }), CX, CY, Size, C, Th, true);
		Poly(Pts({ { -0.85f, 0.35f }, { 0.85f, 0.35f } }), CX, CY, Size, C, Th * 0.7f, false);
	}
	else if (Name == TEXT("orb"))
	{
		Poly(Arc(0.f, 0.f, 0.75f, 0.f, 360.f, 24), CX, CY, Size, C, Th, false);
		Poly(Arc(-0.1f, -0.1f, 0.4f, 200.f, 290.f, 6), CX, CY, Size, C, Th, false);
	}
	else if (Name == TEXT("star"))
	{
		TArray<FVector2D> P;
		for (int32 i = 0; i < 10; ++i) { const float A = -PI * 0.5f + PI * i / 5.f; const float R = i % 2 ? 0.4f : 0.95f; P.Add(FVector2D(R * FMath::Cos(A), R * FMath::Sin(A))); }
		Poly(P, CX, CY, Size, C, Th, true);
	}
	else if (Name == TEXT("skull"))
	{
		Poly(Arc(0.f, -0.2f, 0.7f, 150.f, 390.f, 18), CX, CY, Size, C, Th, false);
		Poly(Pts({ { -0.6f, 0.15f }, { -0.35f, 0.45f }, { -0.35f, 0.85f }, { 0.35f, 0.85f }, { 0.35f, 0.45f }, { 0.6f, 0.15f } }), CX, CY, Size, C, Th, false);
		Disc(CX - Size * 0.16f, CY - Size * 0.08f, Size * 0.1f, C);
		Disc(CX + Size * 0.16f, CY - Size * 0.08f, Size * 0.1f, C);
	}
	else if (Name == TEXT("up") || Name == TEXT("down"))
	{
		const float D = Name == TEXT("up") ? 1.f : -1.f;
		Poly(Pts({ { 0.f, -0.9f * D }, { 0.7f, -0.1f * D }, { 0.3f, -0.1f * D }, { 0.3f, 0.9f * D }, { -0.3f, 0.9f * D }, { -0.3f, -0.1f * D }, { -0.7f, -0.1f * D } }), CX, CY, Size, C, Th, true);
	}
	else if (Name == TEXT("coin"))
	{
		Poly(Arc(0.f, 0.f, 0.8f, 0.f, 360.f, 20), CX, CY, Size, C, Th, false);
		Poly(Arc(0.f, 0.f, 0.5f, 0.f, 360.f, 16), CX, CY, Size, C, Th * 0.7f, false);
	}
	else if (Name == TEXT("swords"))
	{
		Poly(Pts({ { -0.8f, 0.8f }, { 0.8f, -0.8f } }), CX, CY, Size, C, Th * 1.4f, false);
		Poly(Pts({ { 0.8f, 0.8f }, { -0.8f, -0.8f } }), CX, CY, Size, C, Th * 1.4f, false);
		Poly(Pts({ { -0.75f, 0.35f }, { -0.35f, 0.75f } }), CX, CY, Size, C, Th, false);
		Poly(Pts({ { 0.75f, 0.35f }, { 0.35f, 0.75f } }), CX, CY, Size, C, Th, false);
	}
}

FString AArenaHUD::ClassGlyph(const FString& Class) const
{
	const FString L = Class.ToLower();
	if (L.Contains(TEXT("warrior"))) { return TEXT("sword"); }
	if (L.Contains(TEXT("assassin"))) { return TEXT("dagger"); }
	if (L.Contains(TEXT("mage"))) { return TEXT("star"); }
	if (L.Contains(TEXT("archer")) || L.Contains(TEXT("marksman"))) { return TEXT("bow"); }
	return TEXT("shield");
}

// ---- art -------------------------------------------------------------------------------------------------
void AArenaHUD::Portrait(int32 HeroIndex, float X, float Y, float Size, const FLinearColor& Border, bool bDim)
{
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	Rect(X, Y, Size, Size, FLinearColor(0.02f, 0.02f, 0.03f, 1.f));
	if (UTextureRenderTarget2D* RT = Studio ? Studio->Portrait(HeroIndex) : nullptr) { Tex(RT, X, Y, Size, Size, bDim ? FLinearColor(0.35f, 0.35f, 0.38f) : White); }
	else if (Heroes.IsValidIndex(HeroIndex))
	{
		Rect(X, Y, Size, Size, FArenaDatabase::Hex(Heroes[HeroIndex].Tint) * FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		Text(Heroes[HeroIndex].DisplayName.Left(1), X + Size * 0.5f, Y + Size * 0.2f, White, Size / 60.f, true);
	}
	if (bDim) { Rect(X, Y, Size, Size, FLinearColor(0.f, 0.f, 0.f, 0.35f)); }
	Frame(X, Y, Size, Size, Border, FMath::Max(1.f, 2.f * S));
}

void AArenaHUD::AbilityIcon(int32 HeroIndex, int32 Slot, float X, float Y, float Size, bool bLocked)
{
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	const FArenaAbilityDef* Ab = Heroes.IsValidIndex(HeroIndex) && Heroes[HeroIndex].Abilities.IsValidIndex(Slot) ? &Heroes[HeroIndex].Abilities[Slot] : nullptr;
	const FLinearColor AC = Ab ? FArenaDatabase::Hex(Ab->Color) : White;
	// the ability's colour as a dark-to-rich vertical wash under its symbol (the hero's pose used to sit behind the
	// symbol: a muddy, low-contrast icon)
	Rect(X, Y, Size, Size, FLinearColor(0.02f, 0.02f, 0.03f, 1.f));
	for (int32 k = 0; k < 6; ++k) { Rect(X, Y + Size * k / 6.f, Size, Size / 6.f + 0.5f, AC * FLinearColor(0.16f + 0.07f * k, 0.16f + 0.07f * k, 0.16f + 0.07f * k, 1.f)); }
	Rect(X, Y, Size, Size * 0.06f, FLinearColor(1.f, 1.f, 1.f, 0.08f));
	if (UTexture2D* T = Ab ? IconTexture(Ab->Icon) : nullptr)
	{
		IconTex(T, X + Size * 0.12f, Y + Size * 0.12f, Size * 0.76f, Size * 0.76f, bLocked ? FLinearColor(0.45f, 0.45f, 0.48f, 0.9f) : FMath::Lerp(AC, White, 0.82f));
	}
	if (bLocked) { Rect(X, Y, Size, Size, FLinearColor(0.f, 0.f, 0.f, 0.5f)); }
}

void AArenaHUD::ItemIcon(int32 ItemIndex, float X, float Y, float Size, bool bDim)
{
	const TArray<FArenaItemDef>& Items = FArenaDatabase::Get().Items;
	Rect(X, Y, Size, Size, FLinearColor(0.05f, 0.05f, 0.07f, 0.95f));
	if (!Items.IsValidIndex(ItemIndex)) { Frame(X, Y, Size, Size, FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f); return; }
	const FLinearColor C = FArenaDatabase::Hex(Items[ItemIndex].Color) * (bDim ? FLinearColor(0.45f, 0.45f, 0.45f, 1.f) : White);
	Rect(X, Y, Size, Size, C * FLinearColor(0.18f, 0.18f, 0.18f, 1.f));
	if (UTexture2D* T = IconTexture(Items[ItemIndex].Icon)) { IconTex(T, X + Size * 0.1f, Y + Size * 0.1f, Size * 0.8f, Size * 0.8f, FMath::Lerp(C, White, 0.45f) * (bDim ? FLinearColor(0.6f, 0.6f, 0.6f, 1.f) : White)); }
	else { Glyph(Items[ItemIndex].Icon, X + Size * 0.5f, Y + Size * 0.5f, Size * 0.68f, C, FMath::Max(1.f, Size / 28.f)); }
	// the frame tells the tier: grey part, green upgrade, gold finished item
	const int32 Tier = Items[ItemIndex].Tier;
	const FLinearColor TC = Tier >= 3 ? Gold : (Tier == 2 ? FLinearColor(0.5f, 0.9f, 0.55f) : FLinearColor(0.62f, 0.68f, 0.8f));
	Frame(X, Y, Size, Size, TC * FLinearColor(1.f, 1.f, 1.f, bDim ? 0.45f : 0.95f), FMath::Max(1.f, (Tier >= 3 ? 2.f : 1.f) * S));
}

// ---- frame -----------------------------------------------------------------------------------------------
void AArenaHUD::DrawHUD()
{
	Super::DrawHUD();
	AArenaGameMode* GM = AArenaGameMode::Get(this);
	if (!Canvas || !GM || (GM->bAnimLab && !GM->bLabHUD)) { return; }
	S = Canvas->SizeY / 1080.f;
	Studio = AArenaIconStudio::Get(this);
	ShopHits.Reset();
	MenuHits.Reset();
	AArenaPlayerController* MPC = Cast<AArenaPlayerController>(PlayerOwner);
	const EArenaMenu Menu = MPC ? MPC->Menu : EArenaMenu::None;
	if (MPC && MPC->LoadingFrames >= 0) { DrawLoading(MPC); ++MPC->LoadingFrames; return; }
	if (GM->Phase == EArenaPhase::HeroSelect)
	{
		switch (Menu)
		{
		case EArenaMenu::Main: DrawMainMenu(GM, MPC); break;
		case EArenaMenu::Play: DrawPlayMenu(GM, MPC); break;
		case EArenaMenu::Heroes: DrawHeroBrowser(GM, MPC); break;
		case EArenaMenu::Settings: DrawSettings(GM, MPC); break;
		default: DrawHeroSelect(GM); break;
		}
		return;
	}
	{
		// the match HUD at the player's scale (settings: Skala interfejsu); the menus over it keep theirs
		const float BaseS = S;
		S = BaseS * FArenaSettings::Get().UiScale;
		DrawMatch(GM);
		S = BaseS;
	}
	if (Menu == EArenaMenu::Pause) { DrawPauseMenu(GM, MPC); }
	else if (Menu == EArenaMenu::Settings) { DrawSettings(GM, MPC); }
	if (MPC) { MPC->TickDemoClicks(this); }   // the UI demo's clicks in the paused menu (the HUD still draws while paused)
}

void AArenaHUD::DrawHeroSelect(AArenaGameMode* GM)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.01f, 0.01f, 0.02f, 0.62f));
	Rect(0, 0, W, 150 * S, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	AArenaPlayerController* PC = Cast<AArenaPlayerController>(PlayerOwner);
	const bool bPickMenu = PC && PC->Menu == EArenaMenu::HeroPick;
	const bool bTrainingPick = bPickMenu && PC->bPickForTraining;
	const TCHAR* ModeName = GM->bConquest ? TEXT("Conquest 5v5") : (GM->TeamSize >= 5 ? TEXT("Arena 5v5") : (GM->TeamSize >= 3 ? TEXT("Skirmish 3v3") : TEXT("Duel 1v1")));
	Text(bTrainingPick ? TEXT("TRAINING CENTER") : TEXT("TARTARUS ARENA"), W * 0.5f, 34 * S, Gold, 2.6f * S, true);
	Text(bTrainingPick ? FString(TEXT("Choose a hero for training  ·  click a card or 1-5")) : FString::Printf(TEXT("%s  ·  choose a hero: click a card or 1-5"), ModeName),
		W * 0.5f, 104 * S, FLinearColor(0.85f, 0.85f, 0.9f), 0.95f * S, true);

	if (GetNetMode() != NM_Standalone)
	{
		// the lobby: this machine's address and the guests' picks
		const bool bHost = GetNetMode() == NM_ListenServer;
		FString Guests;
		if (bHost)
		{
			for (const AArenaGameMode::FHuman& Hm : GM->RemoteHumans)
			{
				Guests += FString::Printf(TEXT(" · player (%s): %s"), Hm.Team == 0 ? TEXT("with you") : TEXT("against"),
					FArenaDatabase::Get().Heroes.IsValidIndex(Hm.Hero) ? *FArenaDatabase::Get().Heroes[Hm.Hero].DisplayName : TEXT("picking…"));
			}
		}
		const FString Line = bHost
			? FString::Printf(TEXT("LAN · your address: %s (port 7777)%s · pick a hero to start the match"), *LocalAddress(), Guests.IsEmpty() ? TEXT(" · waiting for players") : *Guests)
			: FString::Printf(TEXT("LAN · connected to host · %s"), PC && PC->PickedHero >= 0 && FArenaDatabase::Get().Heroes.IsValidIndex(PC->PickedHero)
				? *FString::Printf(TEXT("picked: %s — the host will start the match"), *FArenaDatabase::Get().Heroes[PC->PickedHero].DisplayName) : TEXT("pick a hero"));
		Rect(W * 0.5f - 640 * S, 128 * S, 1280 * S, 30 * S, FLinearColor(0.02f, 0.05f, 0.09f, 0.8f));
		Text(Line, W * 0.5f, 131 * S, FLinearColor(0.5f, 0.85f, 1.f), 0.5f * S, true, GEngine->GetMediumFont());
	}
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	float MaxHp = 1.f, MaxPow = 1.f, MaxArm = 1.f, MaxSpd = 1.f;
	for (const FArenaHeroDef& D : Heroes) { MaxHp = FMath::Max(MaxHp, D.MaxHealth); MaxPow = FMath::Max(MaxPow, D.Power); MaxArm = FMath::Max(MaxArm, D.Armor); MaxSpd = FMath::Max(MaxSpd, D.MoveSpeed); }
	// one row of big cards for a small roster; a grid of compact ones (6 a row) for a bigger one
	const int32 Cols = Heroes.Num() <= 5 ? FMath::Max(1, Heroes.Num()) : 6;
	const int32 Rows = FMath::DivideAndRoundUp(Heroes.Num(), Cols);
	const bool bCompact = Rows > 1;
	const float Gap = (bCompact ? 16 : 24) * S;
	const float CardW = bCompact ? FMath::Min(270 * S, (W - 80 * S - (Cols - 1) * Gap) / Cols) : 300 * S;
	const float CardH = bCompact ? 345 * S : 560 * S;
	const TCHAR* Keys[5] = { TEXT("LMB"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") };
	for (int32 i = 0; i < Heroes.Num(); ++i)
	{
		const FArenaHeroDef& D = Heroes[i];
		const int32 Row = i / Cols, Col = i % Cols;
		const int32 InRow = FMath::Min(Cols, Heroes.Num() - Row * Cols);
		const float RowW = InRow * CardW + (InRow - 1) * Gap;
		const float X = (W - RowW) * 0.5f + Col * (CardW + Gap), Y = (bCompact ? 150 : 190) * S + Row * (CardH + Gap);
		const FLinearColor Tint = FMath::Lerp(FArenaDatabase::Hex(D.Tint), White, 0.35f);
		Panel(X, Y, CardW, CardH);
		if (Hovered(X, Y, CardW, CardH)) { Frame(X - 3 * S, Y - 3 * S, CardW + 6 * S, CardH + 6 * S, Gold, 2.5f * S); }
		HitArea(X, Y, CardW, CardH, TEXT("PickHero"), i);
		const float P = bCompact ? FMath::Min(CardW - 20 * S, 190 * S) : CardW - 20 * S;
		const float PX0 = X + (CardW - P) * 0.5f;
		Portrait(i, PX0, Y + 10 * S, P, Tint);
		Rect(PX0, Y + 10 * S + P - (bCompact ? 44 : 70) * S, P, (bCompact ? 44 : 70) * S, FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Text(D.DisplayName, X + CardW * 0.5f, Y + P - (bCompact ? 36 : 58) * S, White, (bCompact ? 0.85f : 1.25f) * S, true);
		// key badge (the keys 1-5 pick the first five)
		if (i < 5)
		{
			Rect(X + 16 * S, Y + 16 * S, 40 * S, 40 * S, FLinearColor(0.f, 0.f, 0.f, 0.7f));
			Frame(X + 16 * S, Y + 16 * S, 40 * S, 40 * S, Gold, 1.5f * S);
			Text(FString::FromInt(i + 1), X + 36 * S, Y + 19 * S, Gold, 0.9f * S, true);
		}
		if (bCompact)
		{
			// compact: the class and the abilities
			const float CY = Y + P + 18 * S;
			Glyph(ClassGlyph(D.Class), X + 30 * S, CY + 12 * S, 22 * S, Tint);
			Text(D.Class, X + 50 * S, CY, Tint, 0.7f * S, false);
			const float IS = FMath::Min(40 * S, (CardW - 30 * S - 4 * 6 * S) / 5.f), IY = CY + 42 * S, IX = X + (CardW - (5 * IS + 4 * 6 * S)) * 0.5f;
			for (int32 a = 0; a < 5 && a < D.Abilities.Num(); ++a)
			{
				const float AX = IX + a * (IS + 6 * S);
				AbilityIcon(i, a, AX, IY, IS);
				Frame(AX, IY, IS, IS, a == 4 ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.25f), a == 4 ? 2.f * S : 1.f);
			}
			continue;
		}
		// class
		const float CY = Y + P + 22 * S;
		Glyph(ClassGlyph(D.Class), X + 34 * S, CY + 14 * S, 26 * S, Tint);
		Text(D.Class, X + 58 * S, CY, Tint, 0.85f * S, false);
		// stats
		const TPair<const TCHAR*, float> Stats[4] = { { TEXT("Health"), D.MaxHealth / MaxHp }, { TEXT("Power"), D.Power / MaxPow }, { TEXT("Armor"), D.Armor / MaxArm }, { TEXT("Speed"), D.MoveSpeed / MaxSpd } };
		for (int32 s = 0; s < 4; ++s)
		{
			const float SY = CY + 40 * S + s * 24 * S;
			Text(Stats[s].Key, X + 18 * S, SY - 4 * S, Grey, 0.55f * S, false, GEngine->GetMediumFont());
			Bar(X + 110 * S, SY, CardW - 128 * S, 10 * S, Stats[s].Value, FMath::Lerp(Tint, Gold, 0.3f));
		}
		// abilities
		const float IS = 48 * S, IY = CY + 146 * S, IX = X + (CardW - (5 * IS + 4 * 8 * S)) * 0.5f;
		for (int32 a = 0; a < 5 && a < D.Abilities.Num(); ++a)
		{
			const float AX = IX + a * (IS + 8 * S);
			AbilityIcon(i, a, AX, IY, IS);
			Frame(AX, IY, IS, IS, a == 4 ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.25f), a == 4 ? 2.f * S : 1.f);
			Text(Keys[a], AX + IS * 0.5f, IY + IS + 2 * S, a == 4 ? Gold : Grey, 0.5f * S, true, GEngine->GetMediumFont());
		}
	}
	if (bPickMenu) { Button(TEXT("BACK"), 40 * S, H - 185 * S, 220 * S, 66 * S, bTrainingPick ? FName(TEXT("Back")) : FName(TEXT("Play")), 0); }
	if (bTrainingPick) { return; }   // the training centre has no clock, score or bots
	// match options
	const float OY = H - 185 * S;
	Panel(W * 0.5f - 560 * S, OY, 1120 * S, 110 * S);
	const int32 Minutes[3] = { 5, 10, 15 };
	Text(TEXT("MATCH LENGTH"), W * 0.5f - 400 * S, OY + 12 * S, Grey, 0.6f * S, true, GEngine->GetMediumFont());
	for (int32 m = 0; m < 3; ++m)
	{
		const bool bSel = GM->MatchMinutes == Minutes[m];
		const float BX = W * 0.5f - 530 * S + m * 190 * S, BY = OY + 42 * S;
		HitArea(BX, BY, 170 * S, 50 * S, TEXT("Minutes"), Minutes[m]);
		Rect(BX, BY, 170 * S, 50 * S, bSel ? FLinearColor(0.35f, 0.26f, 0.08f, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.5f));
		Frame(BX, BY, 170 * S, 50 * S, bSel ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.2f), bSel ? 2.f * S : 1.f);
		Text(FString::Printf(TEXT("F%d  ·  %d min"), 5 + m, Minutes[m]), BX + 85 * S, BY + 12 * S, bSel ? Gold : Grey, 0.7f * S, true, GEngine->GetMediumFont());
	}
	const TCHAR* Diff[3] = { TEXT("F1  Easy"), TEXT("F2  Normal"), TEXT("F3  Hard") };
	Text(TEXT("BOT DIFFICULTY"), W * 0.5f + 290 * S, OY + 12 * S, Grey, 0.6f * S, true, GEngine->GetMediumFont());
	for (int32 d = 0; d < 3; ++d)
	{
		const bool bSel = d == GM->Difficulty;
		const float BX = W * 0.5f + 60 * S + d * 160 * S, BY = OY + 42 * S;
		HitArea(BX, BY, 145 * S, 50 * S, TEXT("Difficulty"), d);
		Rect(BX, BY, 145 * S, 50 * S, bSel ? FLinearColor(0.35f, 0.26f, 0.08f, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.5f));
		Frame(BX, BY, 145 * S, 50 * S, bSel ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.2f), bSel ? 2.f * S : 1.f);
		Text(Diff[d], BX + 72 * S, BY + 12 * S, bSel ? Gold : Grey, 0.7f * S, true, GEngine->GetMediumFont());
	}
	Text(FString::Printf(TEXT("The team that reaches %d pts first wins, or leads after %d min  ·  hero kill +%d, minion +%d"),
		ArenaCore::ScoreLimitFor(GM->MatchMinutes, FArenaDatabase::Get().Rules.ScorePerMinute), GM->MatchMinutes, FArenaDatabase::Get().Rules.ScoreHeroKill, FArenaDatabase::Get().Rules.ScoreMinionKill),
		W * 0.5f, H - 64 * S, FLinearColor(0.85f, 0.85f, 0.9f), 0.6f * S, true, GEngine->GetMediumFont());
	Text(FString::Printf(TEXT("WASD move · mouse aim · LMB attack · 1-4 abilities (Ctrl+1-4 rank) · 5/6 potions · %s shop / return · %s shop · %s revive · %s ping · Tab scoreboard"),
		*FArenaSettings::Get().KeyFor(TEXT("Shop")).GetDisplayName().ToString(), *FArenaSettings::Get().KeyFor(TEXT("ShopAny")).GetDisplayName().ToString(),
		*FArenaSettings::Get().KeyFor(TEXT("Revive")).GetDisplayName().ToString(), *FArenaSettings::Get().KeyFor(TEXT("Ping")).GetDisplayName().ToString()),
		W * 0.5f, H - 36 * S, Grey, 0.55f * S, true, GEngine->GetMediumFont());
	Text(TEXT("Icons: game-icons.net (CC BY 3.0) · credits in CREDITS.md"), W - 16 * S - Measure(TEXT("Icons: game-icons.net (CC BY 3.0) · credits in CREDITS.md"), 0.42f * S, GEngine->GetMediumFont()).X, H - 22 * S, FLinearColor(0.5f, 0.5f, 0.55f), 0.42f * S, false, GEngine->GetMediumFont());
}

void AArenaHUD::DrawTopBar(AArenaGameMode* GM)
{
	// read from the replicated match state only (a client has no game mode)
	const AArenaGameState* GS = AArenaGameState::Get(this);
	if (!GS) { return; }
	const float W = Canvas->SizeX, Now = GS->GetServerWorldTimeSeconds();
	const int32 Limit = FMath::Max(1, GS->ScoreLimit);
	// clock
	const float CW = 190 * S, CX = W * 0.5f - CW * 0.5f, CY = 10 * S;
	Panel(CX, CY, CW, 82 * S);
	const bool bOver = GS->bOvertime && GS->Phase == EArenaPhase::Playing;
	const float Left = GS->TimeLeft();
	int32 Total[2] = { 0, 0 };
	for (const FArenaStructureRep& St : GS->Structures) { ++Total[FMath::Clamp<int32>(St.Team, 0, 1)]; }
	if (GS->bConquest)
	{
		const float Elapsed = GS->Phase == EArenaPhase::Playing || GS->Phase == EArenaPhase::Ended ? GS->MatchLength - Left : 0.f;
		Text(Clock(Elapsed), W * 0.5f, CY + 8 * S, White, 1.35f * S, true);
		Text(TEXT("destroy the enemy core"), W * 0.5f, CY + 54 * S, FLinearColor(0.85f, 0.85f, 0.9f), 0.5f * S, true, GEngine->GetMediumFont());
	}
	else
	{
		Text(bOver ? TEXT("OVERTIME") : Clock(Left), W * 0.5f, CY + 8 * S, bOver ? Gold : (Left < 60.f && GS->Phase == EArenaPhase::Playing ? Red : White), (bOver ? 0.95f : 1.35f) * S, true);
		Text(FString::Printf(TEXT("goal: %d pts"), Limit), W * 0.5f, CY + 54 * S, FLinearColor(0.85f, 0.85f, 0.9f), 0.5f * S, true, GEngine->GetMediumFont());
	}
	// teams: score, progress to the limit, five portraits with respawn timers
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 T = Side == 0 ? FMath::Clamp(AArenaCharacter::LocalTeam, 0, 1) : 1 - FMath::Clamp(AArenaCharacter::LocalTeam, 0, 1);   // yours on the left
		const bool bL = Side == 0;
		const float PW = 470 * S, PX = bL ? CX - 10 * S - PW : CX + CW + 10 * S;
		Panel(PX, CY, PW, 82 * S, TeamColor(T) * FLinearColor(1.f, 1.f, 1.f, 0.9f));
		if (GS->bConquest)
		{
			// the enemy structures this team destroyed, and the boss's buff while it lasts
			const int32 Down = Total[1 - T] - GS->StructuresLeft(1 - T);
			Text(FString::FromInt(Down), bL ? PX + PW - 60 * S : PX + 60 * S, CY + 6 * S, TeamColor(T), 1.6f * S, true);
			Text(TEXT("destroyed"), bL ? PX + PW - 60 * S : PX + 60 * S, CY + 58 * S, FLinearColor(0.8f, 0.8f, 0.85f), 0.4f * S, true, GEngine->GetMediumFont());
			// the enemy's structures, towers small, inhibitors larger, the core largest: filled = destroyed
			{
				TArray<const FArenaStructureRep*> Theirs;
				for (const FArenaStructureRep& St : GS->Structures) { if (St.Team == 1 - T) { Theirs.Add(&St); } }
				Theirs.Sort([](const FArenaStructureRep& A, const FArenaStructureRep& B) { return A.Kind != B.Kind ? A.Kind < B.Kind : A.Lane < B.Lane; });
				const float Gap = 5 * S;
				float RowW = 0.f;
				for (const FArenaStructureRep* St : Theirs) { RowW += (St->Kind == 3 ? 18.f : (St->Kind == 2 ? 14.f : 11.f)) * S + Gap; }
				float X = bL ? PX + PW - 110 * S - RowW : PX + 110 * S;
				for (const FArenaStructureRep* St : Theirs)
				{
					const float Sz = (St->Kind == 3 ? 18.f : (St->Kind == 2 ? 14.f : 11.f)) * S, Y0 = CY + 64 * S - Sz * 0.5f;
					Rect(X, Y0, Sz, Sz, St->bAlive ? FLinearColor(0.f, 0.f, 0.f, 0.55f) : TeamColor(T));
					Frame(X, Y0, Sz, Sz, St->bAlive ? TeamColor(1 - T) * FLinearColor(1.f, 1.f, 1.f, 0.8f) : White, FMath::Max(1.f, 1.2f * S));
					X += Sz + Gap;
				}
			}
			if (GS->BossTeam == T && GS->BossUntil > Now) { Glyph(TEXT("orb"), bL ? PX + PW - 110 * S : PX + 110 * S, CY + 30 * S, 26 * S, Purple); }
		}
		else
		{
			Text(FString::FromInt(GS->Score(T)), bL ? PX + PW - 60 * S : PX + 60 * S, CY + 6 * S, TeamColor(T), 1.6f * S, true);
			Bar(bL ? PX + 12 * S : PX + 120 * S, CY + 62 * S, PW - 132 * S, 10 * S, GS->Score(T) / float(Limit), TeamColor(T));
		}
		int32 i = 0;
		const float PS = 44 * S;
		for (const FArenaHeroStat& St : GS->HeroStats)
		{
			if (St.Team != T || i >= 5) { continue; }
			const float X = bL ? PX + 12 * S + i * (PS + 6 * S) : PX + PW - 12 * S - (i + 1) * PS - i * 6 * S;
			++i;
			const float Wait = St.bAlive ? 0.f : FMath::Max(0.1f, St.RespawnAt - Now);
			Portrait(St.Hero, X, CY + 10 * S, PS, TeamColor(T), !St.bAlive);
			if (!St.bAlive) { Text(FString::FromInt(FMath::CeilToInt(Wait)), X + PS * 0.5f, CY + 16 * S, White, 0.8f * S, true); }
		}
	}
}

void AArenaHUD::DrawWorldBars()
{
	// LoL / Smite: bars coloured by relation to you (blue allies, red enemies), heroes with a name, a level, health
	// ticks and mana; bars that would overlap are stacked (the nearest hero keeps its place)
	const AArenaCharacter* Me = PlayerOwner ? Cast<AArenaCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const AArenaCharacter* Aimed = Me ? Me->AimTarget.Get() : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();
	UFont* M = GEngine->GetMediumFont();
	struct FHeroBar { AArenaCharacter* C; FVector2D P; float Dist; };
	TArray<FHeroBar> HeroBars;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (!C->IsAlive() || C->PlayerIndex >= 0) { continue; }
		FVector2D P;
		if (C->IsStructure() || C->IsBoss()) { continue; }   // DrawStructureBars
		if (!PlayerOwner || !PlayerOwner->ProjectWorldLocationToScreen(C->GetActorLocation() + FVector(0, 0, C->IsMinion() ? 110.f : 140.f) * C->GetActorScale3D().Z, P)) { continue; }
		const float Dist = FVector::Dist(C->GetActorLocation(), PlayerOwner->PlayerCameraManager->GetCameraLocation());
		if (Dist > 6500.f) { continue; }
		if (!C->IsMinion()) { HeroBars.Add({ C, P, Dist }); continue; }
		const bool bAimed = C == Aimed;
		const float BW = (bAimed ? 100.f : 62.f) * S, BH = (bAimed ? 11.f : 6.f) * S;
		Bar(P.X - BW * 0.5f, P.Y, BW, BH, C->HealthPct(), TeamColor(C->GetTeam()));
		if (bAimed)
		{
			const bool bJustHit = LastHitVictim.Get() == C && Now - LastHitTime < 0.2f;
			Frame(P.X - BW * 0.5f - 1.f, P.Y - 1.f, BW + 2.f, BH + 2.f, bJustHit ? FLinearColor::White : FLinearColor(1.f, 0.35f, 0.25f, 0.9f), 1.5f * S);
		}
		if (C->IsStunned()) { Glyph(TEXT("star"), P.X + BW * 0.5f + 10 * S, P.Y + 3 * S, 14 * S, Gold); }
	}
	HeroBars.Sort([](const FHeroBar& A, const FHeroBar& B) { return A.Dist < B.Dist; });
	TArray<FBox2D> Taken;
	for (FHeroBar& E : HeroBars)
	{
		AArenaCharacter* C = E.C;
		const bool bAimed = C == Aimed;
		// far away: a compact bar without the name
		const bool bFar = E.Dist > 3500.f && !bAimed;
		const float BW = (bAimed ? 150.f : (bFar ? 84.f : 116.f)) * S, BH = (bAimed ? 13.f : (bFar ? 7.f : 10.f)) * S;
		// the block: name, level badge, health, mana, a line of crowd control (a far bar: the badge and the bar)
		auto Block = [&](const FVector2D& P) { return FBox2D(FVector2D(P.X - BW * 0.5f - 24 * S, P.Y - (bFar ? 7.f : 26.f) * S), FVector2D(P.X + BW * 0.5f + 4 * S, P.Y + BH + 6 * S)); };
		// stacked above everything in the way: with three or more heroes in one spot one step up could land on another
		// bar (two steps were not enough: the names of a clump were drawn over each other)
		for (int32 Try = 0; Try < 8; ++Try)
		{
			const FBox2D Me2 = Block(E.P);
			float Top = TNumericLimits<float>::Max();
			for (const FBox2D& T : Taken) { if (T.Intersect(Me2)) { Top = FMath::Min(Top, T.Min.Y); } }
			if (Top == TNumericLimits<float>::Max()) { break; }
			E.P.Y = Top - (Me2.Max.Y - E.P.Y) - 2.f * S;   // just above the highest one in the way
		}
		Taken.Add(Block(E.P));
		const FVector2D P = E.P;
		const FLinearColor TC = TeamColor(C->GetTeam());
		Bar(P.X - BW * 0.5f, P.Y, BW, BH, C->HealthPct(), TC);
		// a tick every 200 health: how tanky at a glance (LoL)
		const float MaxHp = FMath::Max(1.f, C->GetMaxHealth());
		for (float Hp = 200.f; Hp < MaxHp; Hp += 200.f)
		{
			const float TX = P.X - BW * 0.5f + BW * Hp / MaxHp;
			Rect(TX, P.Y, FMath::Max(1.f, S), BH * (FMath::Fmod(Hp, 1000.f) < 1.f ? 1.f : 0.55f), FLinearColor(0.f, 0.f, 0.f, 0.55f));
		}
		if (C->GetMaxMana() > 0.f) { Bar(P.X - BW * 0.5f, P.Y + BH + 1.f * S, BW, 3.f * S, C->GetMana() / C->GetMaxMana(), ManaBlue, FLinearColor(0.f, 0.f, 0.f, 0.6f)); }
		if (bAimed)
		{
			// the enemy the attack goes for: a framed bar with its health (the frame flashes white on a hit)
			const bool bJustHit = LastHitVictim.Get() == C && Now - LastHitTime < 0.2f;
			Frame(P.X - BW * 0.5f - 1.f, P.Y - 1.f, BW + 2.f, BH + 2.f, bJustHit ? FLinearColor::White : FLinearColor(1.f, 0.35f, 0.25f, 0.9f), 1.5f * S);
			Text(FString::Printf(TEXT("%.0f"), C->GetHealth()), P.X, P.Y - 1.f * S, FLinearColor::White, 0.42f * S, true, M);
		}
		if (C->GetShield() > 0.f) { Rect(P.X - BW * 0.5f, P.Y - 4 * S, BW * FMath::Clamp(C->GetShield() / MaxHp, 0.f, 1.f), 3 * S, FLinearColor(0.95f, 0.95f, 1.f, 0.9f)); }
		Rect(P.X - BW * 0.5f - 22 * S, P.Y - 5 * S, 20 * S, 20 * S, FLinearColor(0.f, 0.f, 0.f, 0.75f));
		Frame(P.X - BW * 0.5f - 22 * S, P.Y - 5 * S, 20 * S, 20 * S, TC, 1.5f * S);
		Text(FString::FromInt(C->GetHeroLevel()), P.X - BW * 0.5f - 12 * S, P.Y - 5 * S, White, 0.5f * S, true, M);
		if (!bFar) { Text(C->GetDef().DisplayName, P.X, P.Y - 24 * S, FMath::Lerp(White, TC, 0.3f), 0.55f * S, true, M); }   // light: readable against the sky
		if (C->IsStunned()) { Glyph(TEXT("star"), P.X + BW * 0.5f + 12 * S, P.Y + 4 * S, 16 * S, Gold); }
		// crowd control, readable at a glance: what and for how long
		float SY = P.Y + BH + 5 * S;
		if (C->IsStunned()) { Text(FString::Printf(TEXT("STUN %.1f"), C->StunRemaining()), P.X, SY, Gold, 0.4f * S, true, M); SY += 15 * S; }
		else if (C->IsAirborne()) { Text(TEXT("AIRBORNE"), P.X, SY, FLinearColor(1.f, 0.6f, 0.3f), 0.4f * S, true, M); SY += 15 * S; }
		if (C->IsSlowed()) { Text(FString::Printf(TEXT("SLOW %.0f%%"), C->GetSlowPct() * 100.f), P.X, SY, FLinearColor(0.55f, 0.75f, 1.f), 0.4f * S, true, M); }
	}
}

void AArenaHUD::DrawStructureBars()
{
	// towers, inhibitors, cores and the boss: a wide bar with a name over the top; a padlock while the chain
	// protects it (LoL shows the same)
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager) { return; }
	UFont* M = GEngine->GetMediumFont();
	struct FBarEntry { AArenaCharacter* C; FVector2D P; float Dist; };
	TArray<FBarEntry> Entries;
	const FVector Eye = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (!C->IsAlive() || !(C->IsStructure() || C->IsBoss())) { continue; }
		const float Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		// a structure's bar sits over its mesh (v17: the towers stand at Paragon's own scale, taller than the capsule)
		FVector Top = C->GetActorLocation() + FVector(0.f, 0.f, Half * (C->IsBoss() ? 1.3f : 1.9f) + 60.f);
		if (C->IsStructure()) { Top.Z = FMath::Max(C->GetActorLocation().Z + Half * 1.1f, C->GetMesh()->Bounds.Origin.Z + C->GetMesh()->Bounds.BoxExtent.Z) + 90.f; }
		FVector2D P;
		if (!PlayerOwner->ProjectWorldLocationToScreen(Top, P)) { continue; }
		const float Dist = FVector::Dist(C->GetActorLocation(), Eye);
		if (Dist > 7000.f) { continue; }
		// behind a ridge or a wall: no bar (the side lanes' towers used to show through the terrain)
		FHitResult Block;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaBarSight), false, C);
		FCollisionObjectQueryParams Statics(ECC_WorldStatic);
		if (Dist > 2500.f && GetWorld()->LineTraceSingleByObjectType(Block, Eye, C->GetActorLocation() + FVector(0.f, 0.f, Half * 0.8f), Statics, Q)) { continue; }
		Entries.Add({ C, P, Dist });
	}
	Entries.Sort([](const FBarEntry& A, const FBarEntry& B) { return A.Dist < B.Dist; });
	TArray<FBox2D> Taken;
	for (FBarEntry& E : Entries)
	{
		AArenaCharacter* C = E.C;
		const float Dist = E.Dist;
		const bool bFar = Dist > 4500.f;
		{
			const float BWt = (bFar ? 110.f : 170.f) * S;
			auto Box = [&](const FVector2D& Q) { return FBox2D(FVector2D(Q.X - BWt * 0.5f - 4 * S, Q.Y - 26 * S), FVector2D(Q.X + BWt * 0.5f + 30 * S, Q.Y + 16 * S)); };
			// a farther structure's bar behind a nearer one is left out (stacked, they climbed into the top bar)
			const FBox2D Mine = Box(E.P);
			if (Taken.ContainsByPredicate([&](const FBox2D& X) { return X.Intersect(Mine); })) { continue; }
			Taken.Add(Mine);
		}
		const FVector2D P = E.P;
		const float BW = (bFar ? 110.f : 170.f) * S, BH = (bFar ? 8.f : 13.f) * S;
		const FLinearColor TC = TeamColor(C->GetTeam());
		Bar(P.X - BW * 0.5f, P.Y, BW, BH, C->HealthPct(), C->bInvulnerable ? FLinearColor(0.55f, 0.58f, 0.62f) : TC);
		Frame(P.X - BW * 0.5f - 1.f, P.Y - 1.f, BW + 2.f, BH + 2.f, FLinearColor(0.f, 0.f, 0.f, 0.8f), 1.5f * S);
		if (!bFar)
		{
			const FString Name = C->IsBoss() ? C->GetDef().DisplayName : (C->StructureKind == 3 ? FString(TEXT("Core")) : (C->StructureKind == 2 ? FString(TEXT("Inhibitor")) : FString(TEXT("Tower"))));
			Text(Name, P.X, P.Y - 24 * S, FMath::Lerp(White, TC, 0.35f), 0.55f * S, true, M);
			Text(FString::Printf(TEXT("%.0f"), C->GetHealth()), P.X, P.Y - 1.f * S, White, 0.42f * S, true, M);
		}
		if (C->bInvulnerable) { Glyph(TEXT("shield"), P.X + BW * 0.5f + 14 * S, P.Y + BH * 0.5f, 20 * S, FLinearColor(0.85f, 0.88f, 0.95f)); }
	}
}

FString AArenaHUD::LocalAddress()
{
	static FString Cached;
	if (Cached.IsEmpty())
	{
		bool bCanBind = false;
		if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			TSharedRef<FInternetAddr> Addr = Sockets->GetLocalHostAddr(*GLog, bCanBind);
			Cached = Addr->ToString(false);
		}
	}
	return Cached;
}

void AArenaHUD::DrawLoading(AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	Rect(0, 0, W, H, FLinearColor(0.01f, 0.012f, 0.02f, 1.f));
	Text(TEXT("TARTARUS ARENA"), W * 0.5f, H * 0.36f, Gold, 2.4f * S, true);
	Text(TEXT("Loading…"), W * 0.5f, H * 0.36f + 110 * S, White, 0.9f * S, true, GEngine->GetMediumFont());
	Text(FString::Printf(TEXT("Tip: %s"), *PC->LoadingTip), W * 0.5f, H * 0.78f, FLinearColor(0.8f, 0.82f, 0.88f), 0.6f * S, true, GEngine->GetMediumFont());
}

void AArenaHUD::DrawPings(bool bMinimapOnly, float MX, float MY, float MW, float MH)
{
	const AArenaGameState* GS = AArenaGameState::Get(this);
	if (!GS || !PlayerOwner) { return; }
	const float Now = GS->GetServerWorldTimeSeconds();
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	for (const FArenaPingRep& P : GS->Pings)
	{
		const float Age = Now - P.Time;
		if (P.Team != AArenaCharacter::LocalTeam || Age > 5.f || Age < 0.f) { continue; }
		const float A = FMath::Clamp(5.f - Age, 0.f, 1.f);
		const FLinearColor C(0.45f, 0.85f, 1.f, A);
		if (bMinimapOnly)
		{
			if (!Studio) { continue; }
			const FVector2D UV = Studio->MinimapUV(P.Pos);
			const FVector2D M(MX + FMath::Clamp(UV.X, 0.f, 1.f) * MW, MY + FMath::Clamp(UV.Y, 0.f, 1.f) * MH);
			const float Pulse = 8.f + 10.f * FMath::Fmod(Age, 1.f);
			Poly(Arc(0.f, 0.f, 1.f, 0.f, 360.f, 16), M.X, M.Y, Pulse * S, C, 2.f * S, false);
			continue;
		}
		FVector2D Sc;
		if (!PlayerOwner->ProjectWorldLocationToScreen(P.Pos + FVector(0.f, 0.f, 120.f), Sc)) { continue; }
		const float Bob = FMath::Sin(Age * 6.f) * 6.f * S;
		Poly(Pts({ { 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } }), Sc.X, Sc.Y + Bob, 16 * S, C, 3.f * S, false);
		const float Dist = PlayerOwner->GetPawn() ? FVector::Dist(P.Pos, PlayerOwner->GetPawn()->GetActorLocation()) / 100.f : 0.f;
		const FString Who = Defs.IsValidIndex(P.Hero) ? Defs[P.Hero].DisplayName : FString();
		Text(FString::Printf(TEXT("HERE!  %s · %.0f m"), *Who, Dist), Sc.X, Sc.Y + Bob - 44 * S, C, 0.5f * S, true, GEngine->GetMediumFont());
	}
}

void AArenaHUD::DrawTutorial(AArenaGameMode* GM)
{
	// the beginner's hints: one card at a time for the first two and a half minutes of a player's first matches
	if (!FArenaSettings::Get().bTutorial || GM->bBotMatch || GM->bTraining || GM->Phase != EArenaPhase::Playing) { return; }
	const float T = GetWorld()->GetTimeSeconds() - GM->MatchStart;
	const FArenaSettings& St = FArenaSettings::Get();
	auto K = [&St](const TCHAR* A) { return St.KeyFor(A).GetDisplayName().ToString(); };
	TArray<FString> Tips = {
		FString::Printf(TEXT("%s%s%s%s — move  ·  mouse — aim  ·  LMB — basic attack (hold: combo)"), *K(TEXT("Forward")), *K(TEXT("Left")), *K(TEXT("Back")), *K(TEXT("Right"))),
		FString::Printf(TEXT("%s–%s — abilities: hold to see the range, release to use  ·  Alt — description"), *K(TEXT("Ability1")), *K(TEXT("Ability4"))),
		TEXT("Leveling up gives a point: Ctrl + ability key ranks it up (ultimate from level 5)"),
		FString::Printf(TEXT("%s in base — shop  ·  %s outside base — recall  ·  %s / %s — potions"), *K(TEXT("Shop")), *K(TEXT("Shop")), *K(TEXT("PotionHp")), *K(TEXT("PotionMana"))),
		GM->bConquest ? FString(TEXT("Conquest: destroy the tower and inhibitor on the lane, then the enemy core. Push under a tower behind your minions!"))
			: FString(TEXT("Arena: points for hero kills (+5) and minion kills (+1). The Power of Tartarus in the center empowers you.")),
		FString::Printf(TEXT("%s or the middle mouse button — ping: your bots will come to that spot"), *K(TEXT("Ping"))) };
	const int32 I = FMath::FloorToInt(T / 13.f);
	if (T < 1.f || !Tips.IsValidIndex(I)) { return; }
	const float Local = T - I * 13.f;
	const float A = FMath::Clamp(FMath::Min(Local, 13.f - Local) * 2.f, 0.f, 1.f);
	const float W = Canvas->SizeX, PW = FMath::Min(W - 80 * S, 1100 * S), PX = W * 0.5f - PW * 0.5f, PY = 108 * S;
	Rect(PX, PY, PW, 52 * S, FLinearColor(0.02f, 0.03f, 0.05f, 0.82f * A));
	Frame(PX, PY, PW, 52 * S, FLinearColor(0.45f, 0.85f, 1.f, 0.8f * A), 1.5f * S);
	Text(FString::Printf(TEXT("TUTORIAL %d/%d"), I + 1, Tips.Num()), PX + 14 * S, PY + 14 * S, FLinearColor(0.45f, 0.85f, 1.f, A), 0.46f * S, false, GEngine->GetMediumFont());
	Text(Tips[I], PX + 150 * S, PY + 13 * S, FLinearColor(1.f, 1.f, 1.f, A), 0.55f * S, false, GEngine->GetMediumFont());
}

void AArenaHUD::DrawMatchHighlights(float Top)
{
	// the end of the match: the best player and the records (LoL / Smite end screens)
	const AArenaGameState* GS = AArenaGameState::Get(this);
	if (!GS || GS->HeroStats.Num() == 0) { return; }
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	auto Score = [](const FArenaHeroStat& H) { return H.Kills * 3.f + H.Assists * 1.5f - H.Deaths + H.DamageToHeroes / 500.f + H.Healing / 400.f + H.MinionKills / 10.f; };
	const FArenaHeroStat* Mvp = nullptr; const FArenaHeroStat* Dmg = nullptr; const FArenaHeroStat* Heal = nullptr; const FArenaHeroStat* Farm = nullptr;
	for (const FArenaHeroStat& H : GS->HeroStats)
	{
		if (!Mvp || Score(H) > Score(*Mvp)) { Mvp = &H; }
		if (!Dmg || H.DamageToHeroes > Dmg->DamageToHeroes) { Dmg = &H; }
		if (!Heal || H.Healing > Heal->Healing) { Heal = &H; }
		if (!Farm || H.MinionKills > Farm->MinionKills) { Farm = &H; }
	}
	const float W = Canvas->SizeX, PW = FMath::Min(W - 40.f * S, 1560.f * S), PX = W * 0.5f - PW * 0.5f, CW = PW / 4.f;
	auto Card = [&](int32 i, const TCHAR* Title, const FArenaHeroStat* H, const FString& Line)
	{
		if (!H) { return; }
		const float X = PX + i * CW + 6 * S;
		Panel(X, Top, CW - 12 * S, 86 * S, TeamColor(H->Team));
		Portrait(H->Hero, X + 12 * S, Top + 12 * S, 62 * S, TeamColor(H->Team));
		Text(Title, X + 86 * S, Top + 10 * S, Gold, 0.5f * S, false, GEngine->GetMediumFont());
		Text(Defs.IsValidIndex(H->Hero) ? Defs[H->Hero].DisplayName : FString(), X + 86 * S, Top + 32 * S, White, 0.62f * S, false, GEngine->GetMediumFont());
		Text(Line, X + 86 * S, Top + 58 * S, Grey, 0.46f * S, false, GEngine->GetMediumFont());
	};
	Card(0, TEXT("MATCH MVP"), Mvp, Mvp ? FString::Printf(TEXT("%d / %d / %d  ·  %.0f damage"), Mvp->Kills, Mvp->Deaths, Mvp->Assists, Mvp->DamageToHeroes) : FString());
	Card(1, TEXT("MOST DAMAGE"), Dmg, Dmg ? FString::Printf(TEXT("%.0f to heroes"), Dmg->DamageToHeroes) : FString());
	Card(2, TEXT("MOST HEALING"), Heal, Heal ? FString::Printf(TEXT("%.0f health"), Heal->Healing) : FString());
	Card(3, TEXT("BEST FARM"), Farm, Farm ? FString::Printf(TEXT("%d minions"), Farm->MinionKills) : FString());
	const float Dur = GS->Phase == EArenaPhase::Ended ? GS->PhaseStart - GS->MatchStart : 0.f;
	FString Line = FString::Printf(TEXT("Match time %s"), *Clock(Dur));
	if (GS->bConquest)
	{
		int32 Total[2] = { 0, 0 };
		for (const FArenaStructureRep& St : GS->Structures) { ++Total[FMath::Clamp<int32>(St.Team, 0, 1)]; }
		const int32 Me = FMath::Clamp(AArenaCharacter::LocalTeam, 0, 1);
		Line += FString::Printf(TEXT("  ·  structures destroyed: your team %d, enemies %d"), Total[1 - Me] - GS->StructuresLeft(1 - Me), Total[Me] - GS->StructuresLeft(Me));
	}
	Text(Line, W * 0.5f, Top + 96 * S, FLinearColor(0.85f, 0.86f, 0.9f), 0.52f * S, true, GEngine->GetMediumFont());
}

void AArenaHUD::DrawTowerRanges(AArenaCharacter* Me)
{
	if (!Me || !PlayerOwner) { return; }
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* T = *It;
		if (!T->IsAlive() || !T->IsStructure() || T->GetTeam() == Me->GetTeam() || T->GetDef().Abilities.Num() == 0) { continue; }
		const float R = T->Ability(0).Range * 100.f + T->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float D = FVector::Dist2D(T->GetActorLocation(), Me->GetActorLocation());
		if (D > R + 900.f) { T->ShowRangeRing(false, FLinearColor::White, 0.f); continue; }
		const bool bOnMe = T->AimTarget.Get() == Me;
		const float A = FMath::Clamp((R + 900.f - D) / 900.f, 0.f, 1.f) * (bOnMe ? 0.9f : 0.45f);
		T->ShowRangeRing(true, bOnMe ? FLinearColor(1.f, 0.12f, 0.08f) : FLinearColor(1.f, 0.85f, 0.55f), A);
		if (bOnMe) { Text(TEXT("TOWER IS TARGETING YOU"), Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f + 100 * S, FLinearColor(1.f, 0.3f, 0.2f), 0.6f * S, true, GEngine->GetMediumFont()); }
	}
}

void AArenaHUD::DrawPlayerPanel(AArenaGameMode* GM, AArenaCharacter* Me)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY, Now = GetWorld()->GetTimeSeconds();
	const float PW = 900 * S, PH = 176 * S, PX = W * 0.5f - PW * 0.5f, PY = H - PH - 14 * S;
	Panel(PX, PY, PW, PH);
	// portrait + level badge
	const float PS = 150 * S;
	Portrait(Me->HeroIndex, PX + 13 * S, PY + 13 * S, PS, Gold);
	Rect(PX + 8 * S, PY + PS - 18 * S, 40 * S, 40 * S, FLinearColor(0.05f, 0.04f, 0.02f, 0.95f));
	Frame(PX + 8 * S, PY + PS - 18 * S, 40 * S, 40 * S, Gold, 2.f * S);
	Text(FString::FromInt(Me->GetHeroLevel()), PX + 28 * S, PY + PS - 14 * S, Gold, 0.85f * S, true);
	// XP to the next level
	const float XpSpan = Me->XpLevelSpan();
	Bar(PX + 52 * S, PY + PS + 16 * S - 11 * S, PS - 40 * S, 8 * S, XpSpan > 0.f ? Me->XpIntoLevel() / XpSpan : 1.f, FLinearColor(0.95f, 0.75f, 0.25f));
	// name, bars
	const float BX = PX + PS + 28 * S, BW = 470 * S;
	Text(FString::Printf(TEXT("%s  ·  %s"), *Me->GetDef().DisplayName, *Me->GetDef().Class), BX, PY + 10 * S, Gold, 0.7f * S, false, GEngine->GetMediumFont());
	Bar(BX, PY + 36 * S, BW, 24 * S, Me->HealthPct(), FLinearColor(0.2f, 0.78f, 0.28f));
	// v19: a tick every 100 health (a taller one every 500): how many blows it takes reads at a glance
	for (float Hp = 100.f; Hp < Me->GetMaxHealth(); Hp += 100.f)
	{
		const bool bBig = FMath::Fmod(Hp, 500.f) < 1.f;
		Rect(BX + BW * Hp / Me->GetMaxHealth(), PY + 36 * S, FMath::Max(1.f, 1.2f * S), (bBig ? 24.f : 11.f) * S, FLinearColor(0.f, 0.f, 0.f, bBig ? 0.7f : 0.5f));
	}
	if (Me->GetShield() > 0.f) { Rect(BX, PY + 36 * S, BW * FMath::Clamp(Me->GetShield() / FMath::Max(1.f, Me->GetMaxHealth()), 0.f, 1.f), 7 * S, FLinearColor(0.95f, 0.97f, 1.f, 0.95f)); }
	Text(FString::Printf(TEXT("%.0f / %.0f"), Me->GetHealth(), Me->GetMaxHealth()), BX + BW * 0.5f, PY + 37 * S, White, 0.55f * S, true, GEngine->GetMediumFont());
	Bar(BX, PY + 64 * S, BW, 12 * S, Me->GetMaxMana() > 0 ? Me->GetMana() / Me->GetMaxMana() : 0.f, ManaBlue);
	// abilities: icon, hotkey, mana cost, radial cooldown, rank pips and the "+" of a free skill point (VR-20)
	const TCHAR* Keys[5] = { TEXT("LMB"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") };
	const int32 Points = Me->FreeSkillPoints();
	if (Points > 0)
	{
		const float Pulse = 0.7f + 0.3f * FMath::Sin(Now * 6.f);
		Text(FString::Printf(TEXT("Skill points: %d   ·   Ctrl + 1-4"), Points), BX + 250 * S, PY - 74 * S, Gold * FLinearColor(1.f, 1.f, 1.f, Pulse), 0.62f * S, true, GEngine->GetMediumFont());
	}
	for (int32 i = 0; i < 5 && i < Me->GetDef().Abilities.Num(); ++i)
	{
		const FArenaAbilityDef A = Me->Ability(i);
		const int32 Rank = Me->GetRank(i);
		const bool bLocked = i > 0 && Rank <= 0;
		const float IS = 78 * S, IX = BX + i * (IS + 20 * S), IY = PY + 86 * S;
		AbilityIcon(Me->HeroIndex, i, IX, IY, IS, bLocked);
		const float Cd = bLocked ? 0.f : Me->CooldownRemaining(i);
		const bool bMana = bLocked || Me->GetMana() >= A.ManaCost;
		if (bLocked) { Text(FString::Printf(TEXT("Ctrl+%d"), i), IX + IS * 0.5f, IY + IS * 0.34f, FLinearColor(0.8f, 0.8f, 0.85f), 0.48f * S, true, GEngine->GetMediumFont()); }
		if (Cd > 0.f)
		{
			SquareSweep(IX, IY, IS, Cd / FMath::Max(0.1f, Me->CooldownTotal(i)), FLinearColor(0.f, 0.f, 0.f, 0.72f));
			Text(Cd >= 10.f ? FString::FromInt(FMath::CeilToInt(Cd)) : FString::Printf(TEXT("%.1f"), Cd), IX + IS * 0.5f, IY + IS * 0.28f, White, 0.95f * S, true);
		}
		else if (!bMana) { Rect(IX, IY, IS, IS, FLinearColor(0.05f, 0.1f, 0.4f, 0.55f)); }
		if (const AArenaPlayerController* BarPC = Cast<AArenaPlayerController>(PlayerOwner))
		{
			// a key that could not go off: the slot flashes red; a cast waiting for the swing to end: gold
			if (BarPC->DeniedSlot == i && Now - BarPC->DeniedAt < 0.5f) { Frame(IX - 4 * S, IY - 4 * S, IS + 8 * S, IS + 8 * S, FLinearColor(1.f, 0.2f, 0.12f, 1.f - (Now - BarPC->DeniedAt) * 2.f), 3.f * S); }
			if (BarPC->BufferedSlot() == i) { Frame(IX - 4 * S, IY - 4 * S, IS + 8 * S, IS + 8 * S, Gold, 3.f * S); }
		}
		// off cooldown now: a white flash over the icon (and a soft chime for the ultimate)
		if (BarHero != Me->HeroIndex) { for (int32 k = 0; k < 5; ++k) { bWasCooling[k] = false; } BarHero = Me->HeroIndex; }
		if (i > 0 && bWasCooling[i] && Cd <= 0.f && !bLocked)
		{
			ReadyAt[i] = Now;
			if (A.bUltimate) { ArenaHitSound::Play(this, 4, 0.3f); }
		}
		bWasCooling[i] = Cd > 0.f;
		if (Now - ReadyAt[i] < 0.45f)
		{
			const float Fl = 1.f - (Now - ReadyAt[i]) / 0.45f;
			Rect(IX, IY, IS, IS, FLinearColor(1.f, 1.f, 1.f, 0.4f * Fl));
			Frame(IX - 3 * S, IY - 3 * S, IS + 6 * S, IS + 6 * S, FLinearColor(1.f, 1.f, 1.f, Fl), 2.5f * S);
		}
		const bool bUltReady = A.bUltimate && !bLocked && Cd <= 0.f && bMana;
		const float Pulse = bUltReady ? 0.6f + 0.4f * FMath::Sin(Now * 5.f) : 1.f;
		ChamferIcon(IX, IY, IS, A.bUltimate ? Gold * FLinearColor(1, 1, 1, Pulse) : FArenaDatabase::Hex(A.Color), (A.bUltimate ? 3.f : 2.f) * S, FLinearColor(0.05f, 0.06f, 0.09f, 1.f));
		Rect(IX, IY, 30 * S, 20 * S, FLinearColor(0.f, 0.f, 0.f, 0.75f));
		Text(Keys[i], IX + 15 * S, IY + 1 * S, Gold, 0.5f * S, true, GEngine->GetMediumFont());
		if (A.ManaCost > 0.f && !bLocked)
		{
			const FString Cost = FString::Printf(TEXT("%.0f"), A.ManaCost);
			const FVector2D CS = Measure(Cost, 0.46f * S, GEngine->GetMediumFont());
			const float TW = CS.X + 8 * S, TH = 18 * S;
			Rect(IX + IS - TW - 2 * S, IY + IS - TH - 2 * S, TW, TH, FLinearColor(0.f, 0.02f, 0.08f, 0.8f));
			Text(Cost, IX + IS - 2 * S - TW * 0.5f, IY + IS - TH - 3 * S, bMana ? FLinearColor(0.6f, 0.8f, 1.f) : Red, 0.46f * S, true, GEngine->GetMediumFont());
		}
		if (i > 0)
		{
			// rank pips: gold = learnt, dim gold = open at this level, dark = later
			const int32 Open = ArenaCore::MaxRankAt(i, Me->GetHeroLevel());
			const float PipW = (IS - 8 * S) / 5.f;
			for (int32 r = 0; r < ArenaCore::MaxRank; ++r)
			{
				Rect(IX + r * (PipW + 2 * S), IY + IS + 3 * S, PipW, 6 * S, r < Rank ? Gold : (r < Open ? FLinearColor(1.f, 0.8f, 0.35f, 0.3f) : FLinearColor(0.f, 0.f, 0.f, 0.65f)));
			}
		}
		if (Me->CanRankUp(i))
		{
			const float PlusPulse = 0.55f + 0.45f * FMath::Sin(Now * 7.f);
			const float PB = 28 * S, PBX = IX + IS - PB + 8 * S, PBY = IY - 12 * S;
			Rect(PBX, PBY, PB, PB, FLinearColor(0.32f, 0.24f, 0.04f, 0.96f));
			Frame(PBX, PBY, PB, PB, Gold * FLinearColor(1.f, 1.f, 1.f, PlusPulse), 2.f * S);
			Text(TEXT("+"), PBX + PB * 0.5f, PBY - 4 * S, Gold, 1.f * S, true);
		}
		// the basic attack: which hit crits next (items; deterministic) and an armed Czar ostrza
		if (i == 0 && Me->HitsToCrit() > 0) { Text(Me->HitsToCrit() == 1 ? FString(TEXT("next: CRIT")) : FString::Printf(TEXT("crit in %d"), Me->HitsToCrit()), IX + IS * 0.5f, IY + IS + 1 * S, FLinearColor(1.f, 0.7f, 0.3f), 0.42f * S, true, GEngine->GetMediumFont()); }
		if (i == 0 && Me->IsSpellbladeArmed()) { Frame(IX - 4 * S, IY - 4 * S, IS + 8 * S, IS + 8 * S, FLinearColor(1.f, 0.55f, 1.f, 0.95f), 2.5f * S); }
	}
	// gold + items
	const float GX = PX + PW - 200 * S;
	Glyph(TEXT("coin"), GX + 14 * S, PY + 24 * S, 22 * S, Gold);
	Text(FString::FromInt(FMath::FloorToInt(Me->Gold)), GX + 32 * S, PY + 10 * S, Gold, 0.9f * S, false);
	const float ItS = 52 * S;
	for (int32 k = 0; k < ArenaCore::InventorySlots; ++k)
	{
		const float IX = GX + (k % 3) * (ItS + 6 * S), IY = PY + 46 * S + (k / 3) * (ItS + 6 * S);
		ItemIcon(Me->Items.IsValidIndex(k) ? Me->Items[k] : INDEX_NONE, IX, IY, ItS);
	}
	Text(GM->InShop(Me) ? TEXT("B  shop") : TEXT("B  return  ·  P  shop"), GX + 86 * S, PY + 155 * S, GM->InShop(Me) ? Gold : Grey, 0.42f * S, true, GEngine->GetMediumFont());
	// potions (5 / 6): count, and the time left of the one being drunk
	for (int32 k = 0; k < 2; ++k)
	{
		const float PS2 = 40 * S, QX = GX + k * (PS2 + 8 * S), QY = PY - PS2 - 8 * S;
		Rect(QX, QY, PS2, PS2, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		if (UTexture2D* T = IconTexture(k == 0 ? TEXT("health-potion") : TEXT("magic-potion")))
		{
			IconTex(T, QX + 4 * S, QY + 4 * S, PS2 - 8 * S, PS2 - 8 * S, (k == 0 ? FLinearColor(1.f, 0.45f, 0.45f) : FLinearColor(0.5f, 0.7f, 1.f)) * (Me->Potions[k] > 0 ? White : FLinearColor(0.35f, 0.35f, 0.35f, 1.f)));
		}
		const float Left = Me->PotionLeft(k);
		if (Left > 0.f) { SquareSweep(QX, QY, PS2, Left / FMath::Max(0.5f, FArenaDatabase::Get().Rules.PotionSeconds), FLinearColor(0.f, 0.f, 0.f, 0.55f)); }
		Frame(QX, QY, PS2, PS2, Me->Potions[k] > 0 ? (k == 0 ? FLinearColor(1.f, 0.45f, 0.45f) : FLinearColor(0.5f, 0.7f, 1.f)) : FLinearColor(1.f, 1.f, 1.f, 0.15f), 1.5f * S);
		Text(FString::FromInt(Me->Potions[k]), QX + PS2 - 12 * S, QY + PS2 - 20 * S, White, 0.45f * S, true, GEngine->GetMediumFont());
		Text(FString::FromInt(5 + k), QX + 7 * S, QY - 1 * S, Gold, 0.42f * S, true, GEngine->GetMediumFont());
	}
	if (Me->IsRecalling())
	{
		const float RW = 380 * S, RX = W * 0.5f - RW * 0.5f, RYp = PY - 150 * S;
		Text(FString::Printf(TEXT("RECALL  ·  %.1f s"), (1.f - Me->RecallProgress()) * FArenaDatabase::Get().Rules.RecallSeconds), W * 0.5f, RYp - 28 * S, FLinearColor(0.6f, 0.85f, 1.f), 0.62f * S, true, GEngine->GetMediumFont());
		Bar(RX, RYp, RW, 14 * S, Me->RecallProgress(), FLinearColor(0.35f, 0.7f, 1.f));
	}
	// status row above the panel
	float SX = PX;
	auto Status = [&](const TCHAR* G, const FLinearColor& C, const FString& Label)
	{
		Rect(SX, PY - 40 * S, 34 * S, 34 * S, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		Frame(SX, PY - 40 * S, 34 * S, 34 * S, C, 1.5f * S);
		Glyph(G, SX + 17 * S, PY - 23 * S, 24 * S, C);
		if (!Label.IsEmpty())
		{
			// the time left on a dark tag across the icon's foot (it was drawn over the glyph and the frame)
			Rect(SX + 1.5f * S, PY - 21 * S, 31 * S, 13.5f * S, FLinearColor(0.f, 0.f, 0.f, 0.78f));
			Text(Label, SX + 17 * S, PY - 23 * S, White, 0.4f * S, true, GEngine->GetMediumFont());
		}
		SX += 40 * S;
	};
	if (Me->HasPowerBuff()) { Status(TEXT("orb"), FLinearColor(1.f, 0.45f, 0.1f), FString::FromInt(FMath::CeilToInt(Me->PowerBuffRemaining()))); }
	if (Me->HasCampBuff(0)) { Status(TEXT("orb"), FLinearColor(1.f, 0.25f, 0.15f), FString::FromInt(FMath::CeilToInt(Me->CampBuffLeft(0)))); }
	if (Me->HasCampBuff(1)) { Status(TEXT("orb"), FLinearColor(0.6f, 0.4f, 1.f), FString::FromInt(FMath::CeilToInt(Me->CampBuffLeft(1)))); }
	if (Me->HasCampBuff(2)) { Status(TEXT("orb"), Purple, FString::FromInt(FMath::CeilToInt(Me->CampBuffLeft(2)))); }
	if (Me->GetShield() > 0.f) { Status(TEXT("shield"), FLinearColor(0.9f, 0.95f, 1.f), FString::FromInt(FMath::RoundToInt(Me->GetShield()))); }
	if (Me->IsSpedUp()) { Status(TEXT("up"), FLinearColor(0.5f, 1.f, 0.5f), FString::FromInt(FMath::CeilToInt(Me->SpeedBuffRemaining()))); }
	if (Me->AttackSpeedBuffRemaining() > 0.f) { Status(TEXT("bow"), FLinearColor(1.f, 0.75f, 0.3f), FString::FromInt(FMath::CeilToInt(Me->AttackSpeedBuffRemaining()))); }
	if (Me->IsSlowed()) { Status(TEXT("down"), FLinearColor(0.6f, 0.7f, 1.f), FString::Printf(TEXT("%.0f%%"), Me->GetSlowPct() * 100.f)); }
	if (Me->IsStunned()) { Status(TEXT("star"), Gold, FString::Printf(TEXT("%.1f"), Me->StunRemaining())); }
	if (Me->IsAirborne()) { Status(TEXT("up"), FLinearColor(1.f, 0.6f, 0.3f), FString()); }
	if (Me->Passives().LastStandShieldPct > 0.f && Me->LastStandReadyIn() > 0.f) { Status(TEXT("aegis"), Grey, FString::FromInt(FMath::CeilToInt(Me->LastStandReadyIn()))); }
	// low-HP vignette
	if (Me->HealthPct() < 0.3f)
	{
		const float A = (0.3f - Me->HealthPct()) * 1.3f;
		Rect(0, 0, W, 60 * S, FLinearColor(0.6f, 0, 0, A)); Rect(0, H - 60 * S, W, 60 * S, FLinearColor(0.6f, 0, 0, A));
		Rect(0, 0, 60 * S, H, FLinearColor(0.6f, 0, 0, A)); Rect(W - 60 * S, 0, 60 * S, H, FLinearColor(0.6f, 0, 0, A));
	}
}

void AArenaHUD::DrawDeathPanel(AArenaGameMode* GM)
{
	const FArenaRespawn* R = GM->PendingRespawn(AArenaCharacter::LocalTeam, GM->PlayerHeroIndex);
	if (!R) { return; }
	const float W = Canvas->SizeX, H = Canvas->SizeY, Now = GetWorld()->GetTimeSeconds();
	Rect(0, 0, W, H, FLinearColor(0.08f, 0.f, 0.f, 0.28f));
	const float PW = 560 * S, PH = 330 * S, PX = W * 0.5f - PW * 0.5f, PY = H - PH - 150 * S;   // low: the ally watched stays in view
	Panel(PX, PY, PW, PH, Red);
	if (const AArenaPlayerController* SPC = Cast<AArenaPlayerController>(PlayerOwner); SPC && SPC->Spectating.IsValid())
	{
		Text(FString::Printf(TEXT("Spectating: %s  ·  LMB — next ally"), *SPC->Spectating->GetDef().DisplayName), W * 0.5f, PY - 34 * S, White, 0.6f * S, true, GEngine->GetMediumFont());
	}
	Text(TEXT("YOU DIED"), W * 0.5f, PY + 14 * S, Red, 1.3f * S, true);
	// respawn ring
	const float Total = FMath::Max(1.f, ArenaCore::RespawnSeconds(R->Level)), Left = FMath::Max(0.f, R->At - Now);
	const float RX = W * 0.5f, RY = PY + 150 * S, RR = 62 * S;
	RingSweep(RX, RY, RR, 10 * S, 1.f, FLinearColor(1.f, 1.f, 1.f, 0.08f));
	RingSweep(RX, RY, RR, 10 * S, 1.f - Left / Total, Gold);
	Text(FString::FromInt(FMath::CeilToInt(Left)), RX, RY - 30 * S, White, 1.5f * S, true);
	Text(TEXT("respawning at base"), RX, RY + 72 * S, Grey, 0.55f * S, true, GEngine->GetMediumFont());
	// revive
	const int32 Cost = GM->ReviveCostFor(R->Level);
	const float Wait = GM->ReviveReadyIn(AArenaCharacter::LocalTeam, GM->PlayerHeroIndex);
	const bool bCan = Wait <= 0.f && R->Gold >= Cost;
	const float BY = PY + PH - 72 * S;
	Rect(PX + 30 * S, BY, PW - 60 * S, 54 * S, bCan ? FLinearColor(0.3f, 0.22f, 0.05f, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.5f));
	Frame(PX + 30 * S, BY, PW - 60 * S, 54 * S, bCan ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.2f), (bCan ? 2.f : 1.f) * S);
	const FString Label = Wait > 0.f ? FString::Printf(TEXT("Revive ready in %s"), *Clock(Wait))
		: FString::Printf(TEXT("[F]  Revive now  —  %d g   (you have %d)"), Cost, FMath::FloorToInt(R->Gold));
	Text(Label, W * 0.5f, BY + 14 * S, bCan ? Gold : Grey, 0.7f * S, true, GEngine->GetMediumFont());
	Text(TEXT("[B]  shop  ·  buy before you return"), W * 0.5f, PY + PH + 8 * S, Grey, 0.55f * S, true, GEngine->GetMediumFont());
	// death recap (MOBA): who killed you and what hit you in the last 10 s, the biggest first
	if (R->Recap.Num() > 0)
	{
		struct FLine { FString Who; FString What; float Sum = 0.f; int32 Hits = 0; };
		TArray<FLine> Lines;
		float DmgTotal = 0.f;
		for (const FArenaDamageEvent& E : R->Recap)
		{
			FLine* L = Lines.FindByPredicate([&E](const FLine& X) { return X.Who == E.Source && X.What == E.Ability; });
			if (!L) { L = &Lines.AddDefaulted_GetRef(); L->Who = E.Source; L->What = E.Ability; }
			L->Sum += E.Amount; ++L->Hits; DmgTotal += E.Amount;
		}
		Lines.Sort([](const FLine& A, const FLine& B) { return A.Sum > B.Sum; });
		const float QW = 440 * S, QX = PX - QW - 20 * S, QY = PY;
		const int32 Shown = FMath::Min(7, Lines.Num());
		const float QH = 96 * S + Shown * 30 * S;
		Panel(QX, QY, QW, QH, Red);
		Text(FString::Printf(TEXT("KILLED BY: %s"), *R->Killer), QX + 18 * S, QY + 12 * S, Red, 0.7f * S, false, GEngine->GetMediumFont());
		Text(FString::Printf(TEXT("damage in the last 10 s: %.0f"), DmgTotal), QX + 18 * S, QY + 46 * S, Grey, 0.5f * S, false, GEngine->GetMediumFont());
		for (int32 i = 0; i < Shown; ++i)
		{
			const FLine& L = Lines[i];
			const float Y = QY + 80 * S + i * 30 * S;
			Rect(QX + 18 * S, Y + 22 * S, (QW - 36 * S) * FMath::Clamp(L.Sum / FMath::Max(1.f, DmgTotal), 0.f, 1.f), 3 * S, FLinearColor(1.f, 0.35f, 0.25f, 0.8f));
			Text(FString::Printf(TEXT("%s  ·  %s%s"), *L.Who, L.What.IsEmpty() ? TEXT("attack") : *L.What, L.Hits > 1 ? *FString::Printf(TEXT("  ×%d"), L.Hits) : TEXT("")), QX + 18 * S, Y, White, 0.48f * S, false, GEngine->GetMediumFont());
			Text(FString::Printf(TEXT("%.0f"), L.Sum), QX + QW - 18 * S - Measure(FString::Printf(TEXT("%.0f"), L.Sum), 0.5f * S, GEngine->GetMediumFont()).X, Y, Gold, 0.5f * S, false, GEngine->GetMediumFont());
		}
	}
}

void AArenaHUD::DrawMinimap(AArenaGameMode* GM, AArenaCharacter* Me)
{
	UTextureRenderTarget2D* Map = Studio ? Studio->Minimap() : nullptr;
	if (!Map) { return; }
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	const float MW = 340 * S, MH = MW / Studio->MinimapAspect(), MX = W - MW - 18 * S, MY = H - MH - 18 * S;
	Panel(MX - 6 * S, MY - 6 * S, MW + 12 * S, MH + 12 * S);
	Tex(Map, MX, MY, MW, MH, FLinearColor(0.95f, 0.9f, 0.8f));   // a warm parchment tint: lanes and plateaus apart from the stone
	auto ToScreen = [&](const FVector& P) { const FVector2D UV = Studio->MinimapUV(P); return FVector2D(MX + FMath::Clamp(UV.X, 0.f, 1.f) * MW, MY + FMath::Clamp(UV.Y, 0.f, 1.f) * MH); };
	// the bases in their team colours (yours blue), the objective
	for (int32 T = 0; T < 2; ++T)
	{
		const FVector2D B = ToScreen(GM->TeamBase(T));
		Poly(Arc(0.f, 0.f, 1.f, 0.f, 360.f, 24), B.X, B.Y, 34 * S, TeamColor(T) * FLinearColor(1.f, 1.f, 1.f, 0.85f), 2.f * S, false);
	}
	if (const AArenaGameState* CGS = AArenaGameState::Get(this); CGS && CGS->bConquest)
	{
		const float GNow = CGS->GetServerWorldTimeSeconds();
		for (const FArenaCampRep& C : CGS->Camps)
		{
			const FVector2D P = ToScreen(C.Pos);
			const FLinearColor CC = C.Buff == 1 ? FLinearColor(1.f, 0.3f, 0.15f) : (C.Buff == 2 ? FLinearColor(0.6f, 0.4f, 1.f) : (C.Buff == 3 ? Purple : Neutral));
			const float R = (C.Buff == 3 ? 11.f : (C.Buff ? 8.f : 6.f)) * S;
			Poly(Arc(0.f, 0.f, 1.f, 0.f, 360.f, 16), P.X, P.Y, R, C.bUp ? CC : FLinearColor(0.4f, 0.4f, 0.42f, 0.7f), 2.f * S, C.bUp);
			if (!C.bUp && C.RespawnAt - GNow < 45.f && C.RespawnAt > GNow) { Text(FString::FromInt(FMath::CeilToInt(C.RespawnAt - GNow)), P.X, P.Y - 7 * S, Grey, 0.36f * S, true, GEngine->GetMediumFont()); }
		}
		for (const FArenaStructureRep& St : CGS->Structures)
		{
			const FVector2D P = ToScreen(St.Pos);
			const float Sz = (St.Kind == 3 ? 9.f : (St.Kind == 2 ? 6.5f : 5.5f)) * S;
			const FLinearColor TC = TeamColor(St.Team);
			if (!St.bAlive) { Line(P.X - Sz, P.Y - Sz, P.X + Sz, P.Y + Sz, FLinearColor(0.35f, 0.35f, 0.38f), 2.f * S); Line(P.X - Sz, P.Y + Sz, P.X + Sz, P.Y - Sz, FLinearColor(0.35f, 0.35f, 0.38f), 2.f * S); continue; }
			const bool bBlink = St.bUnderAttack && FMath::Fmod(GNow, 0.6f) < 0.3f;
			Rect(P.X - Sz - 2 * S, P.Y - Sz - 2 * S, 2 * Sz + 4 * S, 2 * Sz + 4 * S, bBlink ? FLinearColor(1.f, 1.f, 1.f, 0.95f) : FLinearColor(0.f, 0.f, 0.f, 0.85f));
			if (St.Kind == 2) { Poly(Pts({ { 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } }), P.X, P.Y, Sz * 1.2f, St.bInvulnerable ? TC * 0.6f : TC, 2.f * S, true); }
			else { Rect(P.X - Sz, P.Y - Sz, 2 * Sz, 2 * Sz, St.bInvulnerable ? TC * FLinearColor(0.6f, 0.6f, 0.6f, 1.f) : TC); }
		}
	}
	DrawPings(true, MX, MY, MW, MH);
	FVector OrbAt;
	if (GM->OrbLocation(OrbAt)) { const FVector2D O = ToScreen(OrbAt); Poly(Pts({ { 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } }), O.X, O.Y, 14 * S, FLinearColor(1.f, 0.5f, 0.1f), 2.f * S, true); }
	// the camera's view on the map (LoL): where you look
	if (Me && PlayerOwner && PlayerOwner->PlayerCameraManager)
	{
		const FVector2D P = ToScreen(Me->GetActorLocation());
		const float Yaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw, Half = PlayerOwner->PlayerCameraManager->GetFOVAngle() * 0.5f;
		const FVector2D L = ToScreen(Me->GetActorLocation() + FRotator(0.f, Yaw - Half, 0.f).Vector() * 2200.f);
		const FVector2D R = ToScreen(Me->GetActorLocation() + FRotator(0.f, Yaw + Half, 0.f).Vector() * 2200.f);
		const FLinearColor ViewC(1.f, 1.f, 1.f, 0.55f);
		Line(P.X, P.Y, L.X, L.Y, ViewC, 1.5f * S);
		Line(P.X, P.Y, R.X, R.Y, ViewC, 1.5f * S);
		Line(L.X, L.Y, R.X, R.Y, FLinearColor(1.f, 1.f, 1.f, 0.3f), 1.f * S);
	}
	// minions under the heroes: small squares with a dark edge
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (!C->IsAlive() || !C->IsMinion() || C->IsStructure() || C->IsMonster()) { continue; }   // the structures and camps are drawn above
		const FVector2D P = ToScreen(C->GetActorLocation());
		Rect(P.X - 3 * S, P.Y - 3 * S, 6 * S, 6 * S, FLinearColor(0.f, 0.f, 0.f, 0.8f));
		Rect(P.X - 2 * S, P.Y - 2 * S, 4 * S, 4 * S, TeamColor(C->GetTeam()));
	}
	// heroes: their portraits framed in the relation colour (LoL / Smite), yours last, on top, in gold
	const float PS = 22 * S;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
		{
			AArenaCharacter* C = *It;
			if (!C->IsAlive() || C->IsMinion() || (C == Me) != (Pass == 1)) { continue; }
			const FVector2D P = ToScreen(C->GetActorLocation());
			const float Sz = C == Me ? PS * 1.2f : PS;
			Rect(P.X - Sz * 0.5f - 2 * S, P.Y - Sz * 0.5f - 2 * S, Sz + 4 * S, Sz + 4 * S, FLinearColor(0.f, 0.f, 0.f, 0.85f));
			Portrait(C->HeroIndex, P.X - Sz * 0.5f, P.Y - Sz * 0.5f, Sz, C == Me ? Gold : TeamColor(C->GetTeam()), false);
			if (C->GetTeam() != AArenaCharacter::LocalTeam) { Rect(P.X - Sz * 0.5f, P.Y + Sz * 0.5f - 3 * S, Sz * C->HealthPct(), 3 * S, Red); }
		}
	}
}

void AArenaHUD::DrawKillFeed(AArenaGameMode* GM)
{
	const AArenaGameState* GS = AArenaGameState::Get(this);
	if (!GS) { return; }
	const float W = Canvas->SizeX, Now = GS->GetServerWorldTimeSeconds();
	int32 Row = 0;
	for (int32 i = GS->KillFeed.Num() - 1; i >= 0 && Row < 5; --i)
	{
		const FArenaKillRep& K = GS->KillFeed[i];
		const float Age = Now - K.Time;
		if (Age > 9.f) { break; }
		const float A = FMath::Clamp(9.f - Age, 0.f, 1.f);
		const float PS = 38 * S, RW = 3 * PS + 40 * S, X = W - RW - 20 * S, Y = 110 * S + Row++ * (PS + 8 * S);
		Rect(X, Y, RW, PS, FLinearColor(0.f, 0.f, 0.f, 0.55f * A));
		if (K.KillerHero >= 0) { Portrait(K.KillerHero, X, Y, PS, TeamColor(K.KillerTeam)); }
		else
		{
			Rect(X, Y, PS, PS, FLinearColor(0.1f, 0.1f, 0.12f, A));
			Frame(X, Y, PS, PS, K.KillerTeam >= 0 ? TeamColor(K.KillerTeam) : Grey, 1.5f * S);
			Glyph(K.bKillerMinion ? TEXT("swords") : TEXT("skull"), X + PS * 0.5f, Y + PS * 0.5f, PS * 0.6f, K.KillerTeam >= 0 ? TeamColor(K.KillerTeam) : Grey);
		}
		Glyph(TEXT("sword"), X + PS + 20 * S + PS * 0.5f, Y + PS * 0.5f, PS * 0.7f, K.KillerTeam >= 0 ? TeamColor(K.KillerTeam) : Grey);
		Portrait(K.VictimHero, X + 2 * PS + 40 * S, Y, PS, TeamColor(K.VictimTeam));
		Rect(X + 2 * PS + 40 * S + PS * 0.55f, Y + PS * 0.55f, PS * 0.45f, PS * 0.45f, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		Glyph(TEXT("skull"), X + 2 * PS + 40 * S + PS * 0.775f, Y + PS * 0.775f, PS * 0.36f, FLinearColor(1.f, 0.85f, 0.8f));
	}
}

void AArenaHUD::DrawAnnouncements(AArenaGameMode* GM)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY, Now = GetWorld()->GetTimeSeconds();
	int32 Row = 0;
	for (int32 i = GM->Feed.Num() - 1; i >= 0 && Row < 4; --i)
	{
		const FArenaFeedEntry E = GM->Feed[i].For(AArenaCharacter::LocalTeam);
		if (E.bBig || E.Text.IsEmpty() || Now - E.Time > 6.f) { continue; }
		const float TW = Measure(E.Text, 0.55f * S, GEngine->GetMediumFont()).X;
		Text(E.Text, W - 24 * S - TW, 350 * S + Row++ * 30 * S, E.Color * FLinearColor(1, 1, 1, FMath::Clamp(6.f - (Now - E.Time), 0.f, 1.f)), 0.55f * S, false, GEngine->GetMediumFont());
	}
	for (int32 i = GM->Feed.Num() - 1; i >= 0; --i)
	{
		const FArenaFeedEntry E = GM->Feed[i].For(AArenaCharacter::LocalTeam);
		if (!E.bBig || E.Text.IsEmpty() || Now - E.Time > 2.6f) { continue; }
		const float Age = Now - E.Time;
		const float Pop = 1.f + FMath::Clamp(0.35f - Age, 0.f, 0.35f) * 1.5f;
		const float Fit = FMath::Min(1.7f * S * Pop, 1.7f * S * Pop * (W * 0.9f) / FMath::Max(1.f, Measure(E.Text, 1.7f * S * Pop, TitleFont()).X));
		const FVector2D Sz = Measure(E.Text, Fit, TitleFont());
		const float BA = 0.55f * FMath::Clamp(2.6f - Age, 0.f, 1.f), BW2 = Sz.X * 0.5f + 160 * S;
		// a band fading out to both sides, a gold rule over and under it
		QuadGrad(W * 0.5f - BW2, H * 0.24f - 10 * S, BW2, Sz.Y + 20 * S, FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, BA), FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, BA));
		QuadGrad(W * 0.5f, H * 0.24f - 10 * S, BW2, Sz.Y + 20 * S, FLinearColor(0, 0, 0, BA), FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, BA), FLinearColor(0, 0, 0, 0));
		for (const float RY : TArray<float>{ H * 0.24f - 10.f * S, H * 0.24f + float(Sz.Y) + 9.f * S })
		{
			QuadGrad(W * 0.5f - BW2, RY, BW2, FMath::Max(1.f, 1.5f * S), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, BA * 1.5f), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, BA * 1.5f));
			QuadGrad(W * 0.5f, RY, BW2, FMath::Max(1.f, 1.5f * S), FLinearColor(1.f, 0.8f, 0.35f, BA * 1.5f), FLinearColor(1.f, 0.8f, 0.35f, 0.f), FLinearColor(1.f, 0.8f, 0.35f, BA * 1.5f), FLinearColor(1.f, 0.8f, 0.35f, 0.f));
		}
		Text(E.Text, W * 0.5f, H * 0.24f, E.Color * FLinearColor(1, 1, 1, FMath::Clamp(2.6f - Age, 0.f, 1.f)), Fit, true, TitleFont());
		break;
	}
}

void AArenaHUD::DrawShop(AArenaGameMode* GM, AArenaPlayerController* PC)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	const TArray<FArenaItemDef>& Items = FArenaDatabase::Get().Items;
	const int32 Hero = GM->PlayerHeroIndex;
	AArenaCharacter* Me = Cast<AArenaCharacter>(PC->GetPawn());
	if (!Me) { Me = Cast<AArenaCharacter>(PC->GetViewTarget()); }
	const FArenaRespawn* R = GM->PendingRespawn(AArenaCharacter::LocalTeam, Hero);
	const bool bAlive = Me && Me->IsAlive() && !R;
	const float Gold_ = bAlive ? Me->Gold : (R ? R->Gold : 0.f);
	const TArray<int32> Owned = GM->InventoryOf(AArenaCharacter::LocalTeam, Hero);
	UFont* M = GEngine->GetMediumFont();
	const float PW = 1600 * S, PH = 880 * S, PX = W * 0.5f - PW * 0.5f, PY = H * 0.5f - PH * 0.5f - 10 * S;
	float MX = -1.f, MY = -1.f;
	PC->GetMousePosition(MX, MY);
	const FVector2D Mouse(MX, MY);
	auto AddHit = [this](float X, float Y, float BW, float BH, EShopHit Kind, int32 Index) -> bool
	{
		const FBox2D B(FVector2D(X, Y), FVector2D(X + BW, Y + BH));
		FShopHit Hit; Hit.Kind = Kind; Hit.Index = Index;
		ShopHits.Add(TPair<FBox2D, FShopHit>(B, Hit));
		return B.IsInside(LastMouse);
	};
	LastMouse = Mouse;
	Rect(0, 0, W, H, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	Panel(PX, PY, PW, PH);
	Text(TEXT("SHOP"), PX + 24 * S, PY + 14 * S, Gold, 1.2f * S, false);
	Glyph(TEXT("coin"), PX + 196 * S, PY + 38 * S, 28 * S, Gold);
	Text(FString::Printf(TEXT("%d g"), FMath::FloorToInt(Gold_)), PX + 218 * S, PY + 20 * S, Gold, 0.95f * S, false);
	Text(FString::Printf(TEXT("Inventory %d/%d"), Owned.Num(), ArenaCore::InventorySlots), PX + 400 * S, PY + 28 * S, Grey, 0.6f * S, false, M);
	// close
	const float XB = PX + PW - 54 * S, YB = PY + 14 * S, BS = 40 * S;
	Rect(XB, YB, BS, BS, FLinearColor(0.3f, 0.05f, 0.05f, 0.85f));
	Frame(XB, YB, BS, BS, Red, 1.5f * S);
	Line(XB + 11 * S, YB + 11 * S, XB + BS - 11 * S, YB + BS - 11 * S, White, 2.5f * S);
	Line(XB + BS - 11 * S, YB + 11 * S, XB + 11 * S, YB + BS - 11 * S, White, 2.5f * S);
	AddHit(XB, YB, BS, BS, EShopHit::Close, INDEX_NONE);

	// the hero's recommended build (what the bots buy too)
	const FArenaHeroDef& HD = FArenaDatabase::Get().Heroes[Hero];
	Text(FString::Printf(TEXT("Recommended for: %s"), *HD.DisplayName), PX + 620 * S, PY + 28 * S, Grey, 0.55f * S, false, M);
	int32 FirstMissing = INDEX_NONE;
	for (int32 k = 0; k < HD.Build.Num(); ++k)
	{
		const int32 I = FArenaDatabase::ItemIndex(HD.Build[k]);
		if (I == INDEX_NONE) { continue; }
		const bool bHas = Owned.Contains(I);
		if (!bHas && FirstMissing == INDEX_NONE) { FirstMissing = I; }
		const float IX = PX + 830 * S + k * 60 * S, IY = PY + 16 * S;
		ItemIcon(I, IX, IY, 50 * S, false);
		if (bHas) { Frame(IX - 2 * S, IY - 2 * S, 54 * S, 54 * S, FLinearColor(0.4f, 1.f, 0.45f), 2.f * S); }
		if (AddHit(IX, IY, 50 * S, 50 * S, EShopHit::Item, I)) { Frame(IX - 3 * S, IY - 3 * S, 56 * S, 56 * S, Gold, 1.5f * S); }
	}
	const int32 Sel = Items.IsValidIndex(ShopSelected) ? ShopSelected : (FirstMissing != INDEX_NONE ? FirstMissing : 0);

	// three columns by tier (MOBA: parts, upgrades, finished items)
	const float ColW = 350 * S, ColGap = 12 * S, ListY = PY + 84 * S, RowH = 60 * S;
	const TCHAR* TierNames[3] = { TEXT("PARTS"), TEXT("UPGRADES"), TEXT("LEGENDARY (passive)") };
	int32 Hovered = INDEX_NONE;
	for (int32 Tier = 1; Tier <= 3; ++Tier)
	{
		const float CX = PX + 24 * S + (Tier - 1) * (ColW + ColGap);
		Text(TierNames[Tier - 1], CX + 4 * S, ListY, Tier == 3 ? Gold : GoldDim, 0.55f * S, false, M);
		int32 Row = 0;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			const FArenaItemDef& D = Items[i];
			if (D.Tier != Tier) { continue; }
			const float X = CX, Y = ListY + 30 * S + Row * RowH;
			++Row;
			int32 Used = 0;
			const int32 Price = GM->PriceFor(AArenaCharacter::LocalTeam, Hero, i, &Used);
			const int32 Full = FArenaDatabase::ItemTotalCost(i);
			const bool bAfford = Gold_ >= Price && Owned.Num() - Used < ArenaCore::InventorySlots;
			const bool bHover = AddHit(X, Y, ColW, RowH - 6 * S, EShopHit::Item, i);
			if (bHover) { Hovered = i; }
			const int32 Count = Owned.FilterByPredicate([i](int32 X2) { return X2 == i; }).Num();
			Rect(X, Y, ColW, RowH - 6 * S, i == Sel ? FLinearColor(0.2f, 0.15f, 0.05f, 0.95f) : (bHover ? FLinearColor(0.12f, 0.1f, 0.06f, 0.95f) : FLinearColor(0.05f, 0.05f, 0.07f, 0.9f)));
			Frame(X, Y, ColW, RowH - 6 * S, i == Sel ? Gold : FLinearColor(1.f, 1.f, 1.f, bHover ? 0.3f : 0.1f), (i == Sel ? 2.f : 1.f) * S);
			ItemIcon(i, X + 5 * S, Y + 4 * S, 46 * S, !bAfford);
			Text(D.Name, X + 60 * S, Y + 4 * S, bAfford ? White : Grey, 0.5f * S, false, M);
			Glyph(TEXT("coin"), X + 67 * S, Y + 38 * S, 13 * S, bAfford ? Gold : Red);
			Text(FString::FromInt(Price), X + 78 * S, Y + 29 * S, bAfford ? (Price < Full ? FLinearColor(0.5f, 1.f, 0.5f) : Gold) : Red, 0.48f * S, false, M);
			if (Price < Full) { Text(FString::Printf(TEXT("(full %d)"), Full), X + 130 * S, Y + 30 * S, Grey, 0.4f * S, false, M); }
			if (Count > 0) { Text(FString::Printf(TEXT("x%d"), Count), X + ColW - 34 * S, Y + 16 * S, FLinearColor(0.5f, 1.f, 0.5f), 0.55f * S, false, M); }
		}
	}

	// potions: under the upgrades (5 / 6 to drink)
	{
		const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
		const float X = PX + 24 * S + ColW + ColGap, Y0 = ListY + 30 * S + 8 * RowH + 12 * S;   // under the upgrades (8 rows), clear of the inventory row
		Text(TEXT("POTIONS"), X + 4 * S, Y0, GoldDim, 0.55f * S, false, M);
		const int32 Have[2] = { bAlive && Me ? Me->Potions[0] : (R ? R->Potions[0] : 0), bAlive && Me ? Me->Potions[1] : (R ? R->Potions[1] : 0) };
		for (int32 k = 0; k < 2; ++k)
		{
			const float Y = Y0 + 26 * S + k * (RowH - 6 * S);
			const bool bHover = AddHit(X, Y, ColW, RowH - 12 * S, EShopHit::Potion, k);
			const bool bAff = Gold_ >= Ru.PotionCost && Have[k] < Ru.PotionMax;
			Rect(X, Y, ColW, RowH - 12 * S, bHover ? FLinearColor(0.12f, 0.1f, 0.06f, 0.95f) : FLinearColor(0.05f, 0.05f, 0.07f, 0.9f));
			Frame(X, Y, ColW, RowH - 12 * S, FLinearColor(1.f, 1.f, 1.f, bHover ? 0.3f : 0.1f), 1.f);
			if (UTexture2D* T = IconTexture(k == 0 ? TEXT("health-potion") : TEXT("magic-potion"))) { IconTex(T, X + 6 * S, Y + 4 * S, 40 * S, 40 * S, k == 0 ? FLinearColor(1.f, 0.45f, 0.45f) : FLinearColor(0.5f, 0.7f, 1.f)); }
			Text(FString::Printf(TEXT("%s  (%d)"), k == 0 ? TEXT("Health potion") : TEXT("Mana potion"), 5 + k), X + 56 * S, Y + 2 * S, bAff ? White : Grey, 0.48f * S, false, M);
			Text(FString::Printf(TEXT("+%.0f %s over %.0f s  ·  %d g  ·  you have %d/%d"), k == 0 ? Ru.PotionHeal : Ru.PotionMana, k == 0 ? TEXT("health") : TEXT("mana"), Ru.PotionSeconds, Ru.PotionCost, Have[k], Ru.PotionMax),
				X + 56 * S, Y + 24 * S, bAff ? FLinearColor(0.75f, 0.9f, 0.75f) : Red, 0.42f * S, false, M);
		}
	}

	// details of the selected item: stats, passive, recipe tree, what it builds into, the buy button
	const float DX = PX + 24 * S + 3 * (ColW + ColGap), DY = ListY, DW = PX + PW - 24 * S - DX, DH = PH - (DY - PY) - 130 * S;
	Rect(DX, DY, DW, DH, FLinearColor(0.f, 0.f, 0.f, 0.35f));
	Frame(DX, DY, DW, DH, FLinearColor(1.f, 1.f, 1.f, 0.1f), 1.f);
	if (Items.IsValidIndex(Sel))
	{
		const FArenaItemDef& D = Items[Sel];
		int32 Used = 0;
		const int32 Price = GM->PriceFor(AArenaCharacter::LocalTeam, Hero, Sel, &Used);
		const int32 Full = FArenaDatabase::ItemTotalCost(Sel);
		const bool bAfford = Gold_ >= Price && Owned.Num() - Used < ArenaCore::InventorySlots;
		ItemIcon(Sel, DX + 16 * S, DY + 16 * S, 84 * S, false);
		Text(D.Name, DX + 116 * S, DY + 14 * S, D.Tier == 3 ? Gold : White, 0.75f * S, false, M);
		const TCHAR* TierLabel[3] = { TEXT("part"), TEXT("upgrade"), TEXT("legendary item") };
		Text(TierLabel[FMath::Clamp(D.Tier, 1, 3) - 1], DX + 116 * S, DY + 50 * S, Grey, 0.48f * S, false, M);
		Text(Price < Full ? FString::Printf(TEXT("Price: %d g  (full %d, owned parts: -%d)"), Price, Full, Full - Price) : FString::Printf(TEXT("Price: %d g"), Price),
			DX + 116 * S, DY + 74 * S, bAfford ? Gold : Red, 0.5f * S, false, M);
		float Y = DY + 116 * S;
		for (const FString& L : ItemStatLines(D)) { Text(L, DX + 20 * S, Y, FLinearColor(0.75f, 0.9f, 0.75f), 0.52f * S, false, M); Y += 24 * S; }
		if (!D.PassiveText.IsEmpty())
		{
			Y += 6 * S;
			Text(TEXT("Passive (unique):"), DX + 20 * S, Y, Gold, 0.5f * S, false, M);
			Y += 24 * S;
			Y += WrapText(D.PassiveText, DX + 20 * S, Y, DW - 40 * S, FLinearColor(1.f, 0.92f, 0.75f), 0.5f * S);
		}
		// recipe tree: the item, its parts, their parts; parts you own are framed green and come off the price
		if (D.From.Num() > 0)
		{
			Y += 14 * S;
			Text(TEXT("Recipe:"), DX + 20 * S, Y, GoldDim, 0.5f * S, false, M);
			Y += 30 * S;
			TArray<int32> Pool = Owned;
			auto Take = [&Pool](int32 I) { const int32 K = Pool.IndexOfByKey(I); if (K != INDEX_NONE) { Pool.RemoveAt(K); return true; } return false; };
			const float Cx = DX + DW * 0.5f, S1 = 54 * S, S2 = 44 * S, S3 = 34 * S;
			ItemIcon(Sel, Cx - S1 * 0.5f, Y, S1, false);
			const int32 N1 = D.From.Num();
			const float Y2 = Y + S1 + 26 * S, Y3 = Y2 + S2 + 22 * S;
			for (int32 a = 0; a < N1; ++a)
			{
				const int32 C1 = FArenaDatabase::ItemIndex(D.From[a]);
				const float X1 = DX + DW * (a + 0.5f) / N1;
				Line(Cx, Y + S1, X1, Y2, FLinearColor(1.f, 1.f, 1.f, 0.35f), 1.5f * S);
				const bool bOwn1 = Take(C1);
				ItemIcon(C1, X1 - S2 * 0.5f, Y2, S2, false);
				if (bOwn1) { Frame(X1 - S2 * 0.5f - 2 * S, Y2 - 2 * S, S2 + 4 * S, S2 + 4 * S, FLinearColor(0.4f, 1.f, 0.45f), 2.f * S); }
				AddHit(X1 - S2 * 0.5f, Y2, S2, S2, EShopHit::Item, C1);
				if (bOwn1 || !Items.IsValidIndex(C1)) { continue; }
				const int32 N2 = Items[C1].From.Num();
				for (int32 b = 0; b < N2; ++b)
				{
					const int32 C2 = FArenaDatabase::ItemIndex(Items[C1].From[b]);
					const float X2 = X1 + (b - (N2 - 1) * 0.5f) * (S3 + 10 * S);
					Line(X1, Y2 + S2, X2, Y3, FLinearColor(1.f, 1.f, 1.f, 0.25f), 1.f);
					const bool bOwn2 = Take(C2);
					ItemIcon(C2, X2 - S3 * 0.5f, Y3, S3, false);
					if (bOwn2) { Frame(X2 - S3 * 0.5f - 2 * S, Y3 - 2 * S, S3 + 4 * S, S3 + 4 * S, FLinearColor(0.4f, 1.f, 0.45f), 2.f * S); }
					AddHit(X2 - S3 * 0.5f, Y3, S3, S3, EShopHit::Item, C2);
				}
			}
			Y = Y3 + S3 + 12 * S;
		}
		// what it builds into
		TArray<int32> Into;
		for (int32 i = 0; i < Items.Num(); ++i) { if (Items[i].From.Contains(D.Id)) { Into.AddUnique(i); } }
		if (Into.Num() > 0)
		{
			Y += 8 * S;
			Text(TEXT("Builds into:"), DX + 20 * S, Y + 8 * S, GoldDim, 0.5f * S, false, M);
			for (int32 k = 0; k < Into.Num() && k < 6; ++k)
			{
				const float IX = DX + 150 * S + k * 48 * S;
				ItemIcon(Into[k], IX, Y, 40 * S, false);
				AddHit(IX, Y, 40 * S, 40 * S, EShopHit::Item, Into[k]);
			}
		}
		// buy button
		const float BW = DW - 40 * S, BH = 54 * S, BX = DX + 20 * S, BY = DY + DH - BH - 16 * S;
		const bool bHoverBuy = AddHit(BX, BY, BW, BH, EShopHit::Buy, Sel);
		Rect(BX, BY, BW, BH, bAfford ? FLinearColor(0.12f, 0.35f, 0.12f, bHoverBuy ? 1.f : 0.9f) : FLinearColor(0.2f, 0.2f, 0.22f, 0.9f));
		Frame(BX, BY, BW, BH, bAfford ? FLinearColor(0.5f, 1.f, 0.5f) : Grey, 2.f * S);
		Text(bAfford ? FString::Printf(TEXT("BUY  ·  %d g"), Price) : (Gold_ < Price ? FString::Printf(TEXT("missing %d g"), Price - FMath::FloorToInt(Gold_)) : FString(TEXT("inventory full"))),
			BX + BW * 0.5f, BY + 12 * S, bAfford ? White : Grey, 0.62f * S, true, M);
	}

	// the inventory: pick an item to sell it (60 % of its full price)
	const float IY = PY + PH - 112 * S;
	Text(TEXT("Your items:"), PX + 24 * S, IY + 20 * S, Grey, 0.55f * S, false, M);
	for (int32 k = 0; k < ArenaCore::InventorySlots; ++k)
	{
		const float IX = PX + 220 * S + k * 74 * S;
		ItemIcon(Owned.IsValidIndex(k) ? Owned[k] : INDEX_NONE, IX, IY, 64 * S, false);
		if (Owned.IsValidIndex(k))
		{
			const bool bHover = AddHit(IX, IY, 64 * S, 64 * S, EShopHit::Inventory, k);
			if (k == ShopSelectedInv) { Frame(IX - 3 * S, IY - 3 * S, 70 * S, 70 * S, Gold, 2.f * S); }
			else if (bHover) { Frame(IX - 2 * S, IY - 2 * S, 68 * S, 68 * S, FLinearColor(1.f, 1.f, 1.f, 0.4f), 1.f); }
		}
	}
	if (Owned.IsValidIndex(ShopSelectedInv))
	{
		const int32 Value = ArenaCore::SellValue(Owned[ShopSelectedInv], FArenaDatabase::Recipes());
		const float SX = PX + 220 * S + 6 * 74 * S + 20 * S, SW = 250 * S;
		const bool bHoverSell = AddHit(SX, IY + 6 * S, SW, 52 * S, EShopHit::Sell, ShopSelectedInv);
		Rect(SX, IY + 6 * S, SW, 52 * S, FLinearColor(0.35f, 0.12f, 0.08f, bHoverSell ? 1.f : 0.85f));
		Frame(SX, IY + 6 * S, SW, 52 * S, Red, 2.f * S);
		Text(FString::Printf(TEXT("SELL  ·  +%d g"), Value), SX + SW * 0.5f, IY + 18 * S, White, 0.58f * S, true, M);
	}
	ShopHovered = Hovered;
	Text(TEXT("LMB selects  ·  RMB buys instantly  ·  parts you own come off the price  ·  B / Esc closes"), W * 0.5f, PY + PH - 30 * S, Grey, 0.5f * S, true, M);
}

TArray<FString> AArenaHUD::ItemStatLines(const FArenaItemDef& D) const
{
	TArray<FString> Lines;
	if (D.Power > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f power"), D.Power)); }
	if (D.Health > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f health"), D.Health)); }
	if (D.Armor > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f armor"), D.Armor)); }
	if (D.Mana > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f mana"), D.Mana)); }
	if (D.AttackSpeedPct > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f%% attack speed"), D.AttackSpeedPct * 100.f)); }
	if (D.CritChance > 0.f)
	{
		// deterministic crits: say exactly which basic attack crits
		const float Every = 1.f / D.CritChance;
		Lines.Add(FMath::Abs(Every - FMath::RoundToFloat(Every)) < 0.05f
			? FString::Printf(TEXT("+%.0f%% crit: every %d basic attacks"), D.CritChance * 100.f, FMath::RoundToInt(Every))
			: FString::Printf(TEXT("+%.0f%% crit: on average every %s attacks"), D.CritChance * 100.f, *FString::Printf(TEXT("%.1f"), Every)));
	}
	if (D.ArmorPen > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f armor penetration"), D.ArmorPen)); }
	if (D.CooldownPct > 0.f) { Lines.Add(FString::Printf(TEXT("-%.0f%% ability cooldown"), D.CooldownPct * 100.f)); }
	if (D.LifestealPct > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f%% lifesteal"), D.LifestealPct * 100.f)); }
	if (D.MoveSpeedPct > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f%% move speed"), D.MoveSpeedPct * 100.f)); }
	if (D.HealthRegen > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f health regen"), D.HealthRegen)); }
	if (D.ManaRegen > 0.f) { Lines.Add(FString::Printf(TEXT("+%.0f mana regen"), D.ManaRegen)); }
	return Lines;
}

float AArenaHUD::WrapText(const FString& Str, float X, float Y, float Width, const FLinearColor& C, float Scale)
{
	UFont* M = GEngine->GetMediumFont();
	TArray<FString> Words;
	Str.ParseIntoArrayWS(Words);
	FString Line;
	float H = 0.f;
	const float LineH = Measure(TEXT("Ag"), Scale, M).Y + 2.f * S;
	for (const FString& Wd : Words)
	{
		const FString Try = Line.IsEmpty() ? Wd : Line + TEXT(" ") + Wd;
		if (!Line.IsEmpty() && Measure(Try, Scale, M).X > Width) { Text(Line, X, Y + H, C, Scale, false, M); H += LineH; Line = Wd; }
		else { Line = Try; }
	}
	if (!Line.IsEmpty()) { Text(Line, X, Y + H, C, Scale, false, M); H += LineH; }
	return H;
}

AArenaHUD::FShopHit AArenaHUD::ShopHitAt(const FVector2D& Screen) const
{
	// the last added target wins (buttons are drawn over the rows under them)
	for (int32 i = ShopHits.Num() - 1; i >= 0; --i) { if (ShopHits[i].Key.IsInside(Screen)) { return ShopHits[i].Value; } }
	return FShopHit();
}

// ---- ability tooltip: every number at every rank, the current one in gold (VR-20) -------------------------
void AArenaHUD::DrawAbilityTip(AArenaCharacter* Me, int32 Slot, float X, float Bottom)
{
	if (!Me || !Me->GetDef().Abilities.IsValidIndex(Slot)) { return; }
	UFont* M = GEngine->GetMediumFont();
	const FArenaAbilityDef& B = Me->GetDef().Abilities[Slot];     // rank 1 numbers + growth
	const FArenaAbilityDef A = Me->Ability(Slot);                  // as it is now
	const int32 Rank = Me->GetRank(Slot);
	const float TW = 690 * S, Sc = 0.56f * S, LH = 28 * S;
	const float Pw = Me->GetPower();
	const FLinearColor AC = FArenaDatabase::Hex(B.Color);
	auto Num = [](float V, int32 Dec) { FString Sx = Dec <= 0 ? FString::Printf(TEXT("%.0f"), V) : FString::Printf(TEXT("%.1f"), V); if (Dec > 0 && Sx.EndsWith(TEXT(".0"))) { Sx.LeftChopInline(2); } return Sx; };
	struct FRow { FString Label; float Base = 0.f; float Per = 0.f; int32 Dec = 0; FString Unit; FString Tail; bool bRanked = true; FString Plain; };
	TArray<FRow> Rows;
	auto Ranked = [&](const TCHAR* L, float Base, float Per, int32 Dec, const TCHAR* Unit, const FString& Tail) { FRow R; R.Label = L; R.Base = Base; R.Per = Per; R.Dec = Dec; R.Unit = Unit; R.Tail = Tail; R.bRanked = Per != 0.f; Rows.Add(R); };
	auto Plain = [&](const TCHAR* L, const FString& Val) { FRow R; R.Label = L; R.bRanked = false; R.Plain = Val; Rows.Add(R); };
	const FString ScaleTail = B.PowerScale > 0.f ? FString::Printf(TEXT("+%.0f%% power  →  now %s"), B.PowerScale * 100.f, *Num(A.Damage + B.PowerScale * Pw, 0)) : FString();
	if (B.Damage > 0.f) { Ranked(TEXT("Damage"), B.Damage, B.DamagePerRank, 0, TEXT(""), ScaleTail); }
	if (B.EndDamage > 0.f) { Ranked(TEXT("Landing"), B.EndDamage, B.EndDamagePerRank, 0, TEXT(""), B.PowerScale > 0.f ? FString::Printf(TEXT("+%.0f%% power"), B.PowerScale * 100.f) : FString()); }
	if (B.Heal > 0.f) { Ranked(TEXT("Healing"), B.Heal, B.HealPerRank, 0, TEXT(""), B.PowerScale > 0.f ? FString::Printf(TEXT("+%.0f%% power  →  now %s"), B.PowerScale * 100.f, *Num(A.Heal + B.PowerScale * Pw, 0)) : FString()); }
	if (B.Shield > 0.f) { Ranked(TEXT("Shield"), B.Shield, B.ShieldPerRank, 0, TEXT(""), B.PowerScale > 0.f ? FString::Printf(TEXT("+%.0f%% power  →  now %s"), B.PowerScale * 100.f, *Num(A.Shield + B.PowerScale * Pw, 0)) : FString()); }
	if (B.StunSeconds > 0.f) { Ranked(TEXT("Stun"), B.StunSeconds, B.StunPerRank, 1, TEXT(" s"), FString()); }
	if (B.SlowPct > 0.f) { Ranked(TEXT("Slow"), B.SlowPct * 100.f, B.SlowPerRank * 100.f, 0, TEXT("%"), FString::Printf(TEXT("for %s s"), *Num(B.SlowSeconds, 1))); }
	if (B.Knockback > 0.f) { Plain(TEXT("Knockback"), FString::Printf(TEXT("%s m"), *Num(ArenaCore::KnockDistanceCm(B.Knockback) / 100.f, 1))); }
	if (B.KnockUp > 0.f) { Plain(TEXT("Knock-up"), FString::Printf(TEXT("%s s airborne (%s m up)"), *Num(ArenaCore::KnockAirSeconds(B.KnockUp), 1), *Num(ArenaCore::KnockHeightCm(B.KnockUp) / 100.f, 1))); }
	if (B.SpeedBuffPct > 0.f) { Plain(TEXT("Move Speed"), FString::Printf(TEXT("+%.0f%%"), B.SpeedBuffPct * 100.f)); }
	if (B.AttackSpeedBuffPct > 0.f) { Plain(TEXT("Attack Speed"), FString::Printf(TEXT("+%.0f%%"), B.AttackSpeedBuffPct * 100.f)); }
	if (B.BuffSeconds > 0.f) { Ranked(TEXT("Duration"), B.BuffSeconds, B.BuffPerRank, 1, TEXT(" s"), FString()); }
	switch (B.Archetype)
	{
	case EArenaArchetype::Melee: Plain(TEXT("Range"), FString::Printf(TEXT("%s m  ·  cone %.0f°"), *Num(B.Range, 1), B.Angle)); break;
	case EArenaArchetype::Projectile:
		Plain(TEXT("Projectile"), FString::Printf(TEXT("%s m  ·  width %s m  ·  %s m/s%s%s"), *Num(B.Range, 0), *Num(B.Width > 0.f ? B.Width : 0.52f, 1), *Num(B.Speed, 0),
			B.Radius > 0.f ? *FString::Printf(TEXT("  ·  blast %s m"), *Num(B.Radius, 1)) : TEXT(""), B.Count > 1 ? *FString::Printf(TEXT("  ·  ×%d"), B.Count) : TEXT("")));
		break;
	case EArenaArchetype::GroundAoE: Plain(TEXT("Area"), FString::Printf(TEXT("range %s m  ·  radius %s m  ·  hits after %s s"), *Num(B.Range, 0), *Num(B.Radius, 1), *Num(B.Delay, 1))); break;
	case EArenaArchetype::Dash:
		Plain(TEXT("Dash"), FString::Printf(TEXT("%s m%s%s"), *Num(B.Distance, 0), B.Radius > 0.f ? *FString::Printf(TEXT("  ·  damages along the path (%s m)"), *Num(B.Radius, 1)) : TEXT(""),
			B.EndRadius > 0.f ? *FString::Printf(TEXT("  ·  landing %s m"), *Num(B.EndRadius, 1)) : TEXT("")));
		break;
	case EArenaArchetype::Buff:
		Plain(TEXT("Target"), B.BuffTarget == EArenaBuffTarget::Self ? FString(TEXT("you")) : (B.BuffTarget == EArenaBuffTarget::LowestAlly ? FString::Printf(TEXT("weakest ally within %s m (or you)"), *Num(B.Range, 0)) : FString::Printf(TEXT("allies within %s m radius"), *Num(B.Radius, 1))));
		break;
	}
	const float CdPct = Me->ItemBonus().CooldownPct;
	Ranked(TEXT("Cooldown"), B.Cooldown, B.CooldownPerRank, 1, TEXT(" s"), CdPct > 0.f ? FString::Printf(TEXT("-%.0f%% from items  →  now %s s"), CdPct * 100.f, *Num(ArenaCore::ReducedCooldown(A.Cooldown, CdPct), 1)) : FString());
	if (B.ManaCost > 0.f) { Ranked(TEXT("Mana"), B.ManaCost, B.ManaPerRank, 0, TEXT(""), FString()); }

	const float DescH = Measure(TEXT("Ag"), Sc, M).Y + 2.f * S;
	const int32 DescLines = FMath::Max(1, FMath::CeilToInt(Measure(B.Desc, Sc, M).X / (TW - 32 * S)));
	const float TH = 58 * S + DescLines * DescH + Rows.Num() * LH + 40 * S;
	const float Y0 = Bottom - TH;
	Panel(X, Y0, TW, TH, AC);
	Text(FString::Printf(TEXT("%d  ·  %s"), Slot, *B.Name), X + 16 * S, Y0 + 10 * S, White, 0.68f * S, false, M);
	Text(B.bUltimate ? FString::Printf(TEXT("ULT  ·  rank %d/%d"), Rank, ArenaCore::MaxRank) : FString::Printf(TEXT("rank %d/%d"), Rank, ArenaCore::MaxRank), X + TW - 16 * S - Measure(TEXT("ULT  ·  rank 0/5"), 0.5f * S, M).X, Y0 + 16 * S, Gold, 0.5f * S, false, M);
	float Y = Y0 + 46 * S;
	Y += WrapText(B.Desc, X + 16 * S, Y, TW - 32 * S, FLinearColor(0.85f, 0.87f, 0.92f), Sc) + 6 * S;
	const int32 Cur = FMath::Max(1, Rank);
	for (const FRow& R : Rows)
	{
		Text(R.Label, X + 16 * S, Y, Grey, Sc, false, M);
		float CX = X + 170 * S;
		if (!R.bRanked)
		{
			const FString V = R.Plain.IsEmpty() ? Num(R.Base, R.Dec) + R.Unit : R.Plain;
			Text(V, CX, Y, White, Sc, false, M);
			CX += Measure(V, Sc, M).X;
		}
		else
		{
			for (int32 r = 1; r <= ArenaCore::MaxRank; ++r)
			{
				const FString V = Num(ArenaCore::Ranked(R.Base, R.Per, r), R.Dec) + R.Unit;
				const FLinearColor C = r == Cur ? (Rank > 0 ? Gold : FLinearColor(1.f, 0.85f, 0.5f, 0.8f)) : (r < Cur ? FLinearColor(0.8f, 0.8f, 0.8f) : FLinearColor(0.55f, 0.55f, 0.58f));
				Text(V, CX, Y, C, Sc, false, M);
				CX += Measure(V, Sc, M).X;
				if (r < ArenaCore::MaxRank) { Text(TEXT(" / "), CX, Y, FLinearColor(0.4f, 0.4f, 0.42f), Sc, false, M); CX += Measure(TEXT(" / "), Sc, M).X; }
			}
		}
		if (!R.Tail.IsEmpty()) { Text(R.Tail, CX + 12 * S, Y, FLinearColor(0.6f, 0.85f, 1.f), Sc, false, M); }
		Y += LH;
	}
	FString Foot;
	if (Rank <= 0) { Foot = Me->CanRankUp(Slot) ? FString::Printf(TEXT("Locked: learn  Ctrl + %d"), Slot) : (Slot == 4 ? FString(TEXT("Ultimate from level 5")) : FString(TEXT("Locked: needs a skill point (next level)"))); }
	else if (Me->CanRankUp(Slot)) { Foot = FString::Printf(TEXT("Ctrl + %d: rank %d"), Slot, Rank + 1); }
	else if (Rank < ArenaCore::MaxRank) { Foot = Slot == 4 ? FString(TEXT("Next rank at level 9 / 13 / 17 / 20")) : FString::Printf(TEXT("Rank %d from level %d"), Rank + 1, 2 * (Rank + 1) - 1); }
	if (!Foot.IsEmpty()) { Text(Foot, X + 16 * S, Y + 4 * S, Me->CanRankUp(Slot) ? Gold : Grey, Sc, false, M); }
}

void AArenaHUD::DrawScoreboard(AArenaGameMode* GM, float Top)
{
	// the replicated records: a hero dead at the end of the match keeps its row (the actors-only version lost it)
	const AArenaGameState* GS = AArenaGameState::Get(this);
	if (!GS) { return; }
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	const float W = Canvas->SizeX;
	const float PW = FMath::Min(W - 40.f * S, 1560.f * S), PX = W * 0.5f - PW * 0.5f, RowH = 58 * S, Half = PW * 0.5f;
	const float PH = 120 * S + 5 * RowH;
	UFont* M = GEngine->GetMediumFont();
	Panel(PX, Top, PW, PH);
	Text(FString::Printf(TEXT("%d  :  %d"), GS->ScoreA, GS->ScoreB), W * 0.5f, Top + 12 * S, White, 1.2f * S, true);
	// columns, as fractions of a team's half (fits 1280 to 4K)
	auto Col = [&](float Frac) { return Half * Frac; };
	for (int32 T = 0; T < 2; ++T)
	{
		const float CX = PX + 20 * S + T * Half;
		Text(T == AArenaCharacter::LocalTeam ? TEXT("YOUR TEAM") : TEXT("ENEMIES"), CX, Top + 52 * S, TeamColor(T), 0.65f * S, false, M);
		const TCHAR* Heads[] = { TEXT("lvl."), TEXT("K / D / A"), TEXT("g"), TEXT("minions"), TEXT("damage"), TEXT("items") };
		const float Xs[] = { 0.30f, 0.36f, 0.47f, 0.55f, 0.64f, 0.75f };
		for (int32 c = 0; c < 6; ++c) { Text(Heads[c], CX + Col(Xs[c]), Top + 84 * S, Grey, 0.42f * S, false, M); }
		int32 Row = 0;
		for (const FArenaHeroStat& St : GS->HeroStats)
		{
			if (St.Team != T) { continue; }
			const float Y = Top + 112 * S + Row++ * RowH;
			if (St.bPlayer) { Rect(CX - 8 * S, Y - 2 * S, Half - 24 * S, RowH - 4 * S, FLinearColor(1.f, 0.8f, 0.3f, 0.08f)); }
			Portrait(St.Hero, CX, Y, 46 * S, TeamColor(T), !St.bAlive);
			const FString Name = Defs.IsValidIndex(St.Hero) ? Defs[St.Hero].DisplayName : FString(TEXT("?"));
			Text(Name + (St.bPlayer ? TEXT(" (You)") : TEXT("")), CX + 56 * S, Y + 10 * S, St.bPlayer ? Gold : White, 0.56f * S, false, M);
			Text(FString::FromInt(St.Level), CX + Col(0.30f), Y + 10 * S, White, 0.56f * S, false, M);
			Text(FString::Printf(TEXT("%d / %d / %d"), St.Kills, St.Deaths, St.Assists), CX + Col(0.36f), Y + 10 * S, White, 0.56f * S, false, M);
			Text(FString::Printf(TEXT("%.0f"), St.Gold), CX + Col(0.47f), Y + 10 * S, Gold, 0.5f * S, false, M);
			Text(FString::FromInt(St.MinionKills), CX + Col(0.55f), Y + 10 * S, White, 0.5f * S, false, M);
			Text(St.DamageToHeroes >= 1000.f ? FString::Printf(TEXT("%.1fK"), St.DamageToHeroes / 1000.f) : FString::Printf(TEXT("%.0f"), St.DamageToHeroes), CX + Col(0.64f), Y + 10 * S, White, 0.5f * S, false, M);
			const float IS = FMath::Min(26 * S, (Half * 0.23f) / ArenaCore::InventorySlots - 2 * S);
			for (int32 k = 0; k < ArenaCore::InventorySlots; ++k) { ItemIcon(St.Items.IsValidIndex(k) ? St.Items[k] : INDEX_NONE, CX + Col(0.75f) + k * (IS + 2 * S), Y + 8 * S, IS); }
		}
	}
}

void AArenaHUD::DrawCrosshair(AArenaCharacter* Me, float CX, float CY)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// red when the basic attack has an enemy to go for (in reach, in sight), white otherwise
	const bool bTarget = Me->AimTarget.IsValid() && Me->AimTarget->IsAlive();
	const FLinearColor C = bTarget ? FLinearColor(1.f, 0.25f, 0.2f, 0.95f) : FLinearColor(1.f, 1.f, 1.f, 0.9f);
	const float Gap = (bTarget ? 5.f : 6.f) * S, Len = (bTarget ? 12.f : 9.f) * S;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector2D D = FVector2D(FMath::Cos(i * PI * 0.5f), FMath::Sin(i * PI * 0.5f));
		Line(CX + D.X * Gap, CY + D.Y * Gap, CX + D.X * (Gap + Len), CY + D.Y * (Gap + Len), C, 2.f * S);
	}
	Disc(CX, CY, 1.8f * S, C, 8);
	// hit marker: an X that pops and fades (white hit, yellow critical, red kill)
	const float Age = Now - HitMarkerTime;
	const float Life = HitMarkerKind == 2 ? 0.45f : 0.28f;
	if (Age >= 0.f && Age < Life)
	{
		const float A = 1.f - Age / Life;
		const float Pop = 1.f + FMath::Max(0.f, 0.08f - Age) * 6.f;
		const FLinearColor M = HitMarkerKind == 2 ? FLinearColor(1.f, 0.15f, 0.1f, A) : (HitMarkerKind == 1 ? FLinearColor(1.f, 0.85f, 0.2f, A) : FLinearColor(1.f, 1.f, 1.f, A));
		const float In = 7.f * S * Pop, Out = (HitMarkerKind == 2 ? 22.f : 16.f) * S * Pop;
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D D = FVector2D(FMath::Cos(PI * 0.25f + i * PI * 0.5f), FMath::Sin(PI * 0.25f + i * PI * 0.5f));
			Line(CX + D.X * In, CY + D.Y * In, CX + D.X * Out, CY + D.Y * Out, M, (HitMarkerKind == 2 ? 3.5f : 2.5f) * S);
		}
	}
}

void AArenaHUD::DrawAimTarget(AArenaCharacter* Me)
{
	// brackets around the enemy the attack goes for, and its health bar enlarged with the number
	AArenaCharacter* T = Me->AimTarget.Get();
	if (!T || !T->IsAlive() || !PlayerOwner) { return; }
	const float Half = T->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector2D Top, Bottom;
	if (!PlayerOwner->ProjectWorldLocationToScreen(T->GetActorLocation() + FVector(0, 0, Half), Top) || !PlayerOwner->ProjectWorldLocationToScreen(T->GetActorLocation() - FVector(0, 0, Half), Bottom)) { return; }
	const float Hgt = FMath::Max(24.f * S, Bottom.Y - Top.Y), Wid = Hgt * 0.55f, CX = (Top.X + Bottom.X) * 0.5f;
	const float X0 = CX - Wid * 0.5f, X1 = CX + Wid * 0.5f, Y0 = Top.Y, Y1 = Top.Y + Hgt, L = FMath::Min(Wid, Hgt) * 0.25f;
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bJustHit = LastHitVictim.Get() == T && Now - LastHitTime < 0.2f;
	const FLinearColor C = bJustHit ? FLinearColor(1.f, 1.f, 1.f, 0.95f) : FLinearColor(1.f, 0.3f, 0.2f, 0.85f);
	const float Th = 2.f * S;
	Line(X0, Y0, X0 + L, Y0, C, Th); Line(X0, Y0, X0, Y0 + L, C, Th);
	Line(X1, Y0, X1 - L, Y0, C, Th); Line(X1, Y0, X1, Y0 + L, C, Th);
	Line(X0, Y1, X0 + L, Y1, C, Th); Line(X0, Y1, X0, Y1 - L, C, Th);
	Line(X1, Y1, X1 - L, Y1, C, Th); Line(X1, Y1, X1, Y1 - L, C, Th);
}

void AArenaHUD::StructureHint(const AActor* Context, const FString& Text)
{
	if (!Context || !Context->GetWorld()) { return; }
	APlayerController* PC = Context->GetWorld()->GetFirstPlayerController();
	if (AArenaHUD* HUD = PC ? Cast<AArenaHUD>(PC->GetHUD()) : nullptr) { HUD->HintText = Text; HUD->HintAt = Context->GetWorld()->GetTimeSeconds(); }
}

void AArenaHUD::AddDamageDir(const AActor* Context, const FVector& From)
{
	if (!Context || !Context->GetWorld()) { return; }
	APlayerController* PC = Context->GetWorld()->GetFirstPlayerController();
	if (AArenaHUD* HUD = PC ? Cast<AArenaHUD>(PC->GetHUD()) : nullptr)
	{
		const float Now = Context->GetWorld()->GetTimeSeconds();
		// one arc per direction: a stream of hits from the same side refreshes it
		for (FDamageDir& D : HUD->DamageDirs) { if (FVector::Dist2D(D.From, From) < 300.f) { D.From = From; D.Time = Now; return; } }
		HUD->DamageDirs.Add({ From, Now });
		if (HUD->DamageDirs.Num() > 8) { HUD->DamageDirs.RemoveAt(0); }
	}
}

void AArenaHUD::DrawDamageDirs(AArenaCharacter* Me)
{
	if (!Me || !PlayerOwner || !PlayerOwner->PlayerCameraManager) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	DamageDirs.RemoveAll([Now](const FDamageDir& D) { return Now - D.Time > 1.2f; });
	const float CamYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
	const float CX = Canvas->SizeX * 0.5f, CY = Canvas->SizeY * 0.5f;
	for (const FDamageDir& D : DamageDirs)
	{
		const float Yaw = (D.From - Me->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw;
		const float Rel = FRotator::NormalizeAxis(Yaw - CamYaw);   // 0 = ahead (the top of the screen)
		const float A = FMath::Clamp(1.2f - (Now - D.Time), 0.f, 1.f);
		const float Mid = -90.f + Rel;
		Poly(Arc(0.f, 0.f, 1.f, Mid - 16.f, Mid + 16.f, 10), CX, CY, 2.f * 190.f * S, FLinearColor(1.f, 0.15f, 0.1f, 0.85f * A), 7.f * S, false);
	}
}

void AArenaHUD::DrawOffscreenEnemies(AArenaCharacter* Me)
{
	// enemy heroes near you but off the screen: an arrow at the edge where they are (Smite)
	if (!Me || !PlayerOwner || !PlayerOwner->PlayerCameraManager) { return; }
	const float W = Canvas->SizeX, H = Canvas->SizeY;
	const float CamYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (!C->IsAlive() || C->IsMinion() || C->GetTeam() == Me->GetTeam()) { continue; }
		const float Dist = FVector::Dist2D(C->GetActorLocation(), Me->GetActorLocation());
		if (Dist > 3500.f) { continue; }
		FVector2D P;
		const bool bOn = PlayerOwner->ProjectWorldLocationToScreen(C->GetActorLocation(), P) && P.X > 0.f && P.Y > 0.f && P.X < W && P.Y < H;
		if (bOn) { continue; }
		const float Rel = FRotator::NormalizeAxis((C->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw - CamYaw);
		const float Ang = FMath::DegreesToRadians(-90.f + Rel);
		const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
		const FVector2D At(W * 0.5f + Dir.X * W * 0.43f, H * 0.5f + Dir.Y * H * 0.40f);
		const FVector2D Side(-Dir.Y, Dir.X);
		const float Sz = 18.f * S * (Dist < 1500.f ? 1.25f : 1.f);
		const FLinearColor C1(1.f, 0.2f, 0.12f, Dist < 1500.f ? 0.95f : 0.7f);
		const FVector2D Tip = At + Dir * Sz, L = At - Dir * Sz * 0.6f + Side * Sz * 0.8f, R = At - Dir * Sz * 0.6f - Side * Sz * 0.8f;
		Line(Tip.X, Tip.Y, L.X, L.Y, C1, 3.f * S); Line(L.X, L.Y, R.X, R.Y, C1, 3.f * S); Line(R.X, R.Y, Tip.X, Tip.Y, C1, 3.f * S);
		Text(FString::Printf(TEXT("%.0f m"), Dist / 100.f), At.X - Dir.X * 26.f * S, At.Y - Dir.Y * 26.f * S - 8.f * S, C1, 0.4f * S, true, GEngine->GetMediumFont());
	}
}

void AArenaHUD::AddGoldNumber(const AActor* Context, const FVector& World, float Amount)
{
	if (!Context || !Context->GetWorld() || Amount <= 0.f) { return; }
	APlayerController* PC = Context->GetWorld()->GetFirstPlayerController();
	if (AArenaHUD* HUD = PC ? Cast<AArenaHUD>(PC->GetHUD()) : nullptr)
	{
		FArenaDamageNumber N; N.World = World + FVector(0.f, 0.f, 60.f);
		N.Amount = Amount; N.Time = Context->GetWorld()->GetTimeSeconds(); N.bGold = true;
		HUD->Numbers.Add(N);
		if (HUD->Numbers.Num() > 80) { HUD->Numbers.RemoveAt(0); }
	}
}

void AArenaHUD::DrawNumbers()
{
	const float Now = GetWorld()->GetTimeSeconds();
	Numbers.RemoveAll([Now](const FArenaDamageNumber& N) { return Now - N.Time > (N.bGold ? 1.4f : 1.1f); });
	const bool bDamage = FArenaSettings::Get().bDamageNumbers;
	for (const FArenaDamageNumber& N : Numbers)
	{
		FVector2D P;
		const float Age = Now - N.Time;
		if (N.bGold)
		{
			if (!PlayerOwner || !PlayerOwner->ProjectWorldLocationToScreen(N.World + FVector(0, 0, Age * 70.f), P)) { continue; }
			const float A = FMath::Clamp(1.4f - Age, 0.f, 1.f);
			Text(FString::Printf(TEXT("+%.0f g"), N.Amount), P.X, P.Y, Gold * FLinearColor(1, 1, 1, A), (1.05f + FMath::Max(0.f, 0.12f - Age) * 4.f) * S, true);
			continue;
		}
		if (!bDamage) { continue; }
		if (!PlayerOwner || !PlayerOwner->ProjectWorldLocationToScreen(N.World + FVector(0, 0, Age * 120.f), P)) { continue; }
		if (N.bByPlayer) { P.X += 46.f * S; P.Y -= 8.f * S; }
		const bool bHeal = N.Amount < 0.f;
		const FLinearColor C = bHeal ? FLinearColor(0.3f, 1.f, 0.4f) : (N.bCrit ? Gold : (N.bByPlayer ? FLinearColor(1.f, 0.95f, 0.9f) : FLinearColor(1.f, 0.4f, 0.3f)));
		const float Scale = (N.bCrit ? 1.5f : 1.f) * (N.bByPlayer ? 1.35f : 0.9f) * (1.f + FMath::Max(0.f, 0.15f - Age) * (N.bByPlayer ? 5.f : 3.f)) * S;
		const FString Label = FString::Printf(TEXT("%.0f"), FMath::Abs(N.Amount));
		Text(bHeal ? TEXT("+") + Label : (N.bCrit ? Label + TEXT("!") : Label), P.X, P.Y, C * FLinearColor(1, 1, 1, 1.1f - Age), Scale, true);
	}
}

void AArenaHUD::DrawMatch(AArenaGameMode* GM)
{
	const float W = Canvas->SizeX, H = Canvas->SizeY, Now = GetWorld()->GetTimeSeconds();
	DrawWorldBars();
	DrawStructureBars();
	DrawNumbers();   // damage numbers per the setting, gold popups always
	if (!GM->bTraining) { DrawTopBar(GM); }
	AArenaPlayerController* PC = Cast<AArenaPlayerController>(PlayerOwner);
	AArenaCharacter* Me = PC ? Cast<AArenaCharacter>(PC->GetPawn()) : nullptr;
	if (!Me && PC) { Me = Cast<AArenaCharacter>(PC->GetViewTarget()); }   // spectating a bot match
	if (GM->Phase == EArenaPhase::Countdown)
	{
		Text(FString::FromInt(FMath::Max(1, 3 - (int32)(Now - GM->PhaseStart))), W * 0.5f, H * 0.33f, Gold, 4.f * S, true);
	}
	const bool bPlayerDead = (!GM->bBotMatch || GM->bUIDemo) && GM->PendingRespawn(AArenaCharacter::LocalTeam, GM->PlayerHeroIndex) != nullptr;
	if (Me && Me->IsAlive() && !bPlayerDead && GM->Phase != EArenaPhase::Ended)
	{
		// crosshair
		const float CX = W * 0.5f, CY = H * 0.5f;
		DrawAimTarget(Me);
		DrawCrosshair(Me, CX, CY);
		DrawDamageDirs(Me);
		DrawOffscreenEnemies(Me);
		DrawTowerRanges(Me);
		DrawPings(false);
		const float HintAge = GetWorld()->GetTimeSeconds() - HintAt;
		if (HintAge < 2.2f && !HintText.IsEmpty())
		{
			const float A = FMath::Clamp(2.2f - HintAge, 0.f, 1.f);
			Text(HintText, CX, CY + 70 * S, FLinearColor(1.f, 0.82f, 0.45f, A), 0.55f * S, true, GEngine->GetMediumFont());
		}
		// aiming an ability: its name and the controls under the crosshair (the range and area are on the ground)
		if (PC && Me->GetDef().Abilities.IsValidIndex(PC->AimingSlot()))
		{
			const FArenaAbilityDef& Ab = Me->GetDef().Abilities[PC->AimingSlot()];
			const bool bReady = Me->CanCastSlot(PC->AimingSlot());
			const FString Head = FString::Printf(TEXT("%d  %s"), PC->AimingSlot(), *Ab.Name);
			// the controls of the chosen cast mode (the full description only on Alt: it covered the screen and
			// read as a menu that blocked the ability)
			const FString How = FArenaSettings::Get().CastMode == 0 ? FString(TEXT("release the key or LMB: use  ·  RMB: cancel  ·  Alt: description"))
				: FString(TEXT("LMB / key again: use  ·  RMB: cancel  ·  Alt: description"));
			const FVector2D Size = Measure(Head, 0.56f * S, GEngine->GetMediumFont());
			const float BoxW = FMath::Max(Size.X, Measure(How, 0.44f * S, GEngine->GetMediumFont()).X) + 24 * S;
			Panel(CX - BoxW * 0.5f, CY + 40 * S, BoxW, 58 * S, bReady ? FArenaDatabase::Hex(Ab.Color) * FLinearColor(1.f, 1.f, 1.f, 0.8f) : FLinearColor(1.f, 0.3f, 0.2f, 0.9f));
			Text(Head, CX, CY + 43 * S, bReady ? FLinearColor::White : FLinearColor(1.f, 0.55f, 0.45f), 0.56f * S, true, GEngine->GetMediumFont());
			Text(How, CX, CY + 70 * S, FLinearColor(0.85f, 0.87f, 0.92f), 0.44f * S, true, GEngine->GetMediumFont());
		}
		// Alt: the full description of the ability aimed (or the last one used), bottom left
		if (PC && PC->IsInputKeyDown(EKeys::LeftAlt) && Me->GetDef().Abilities.IsValidIndex(PC->TipSlot()))
		{
			DrawAbilityTip(Me, PC->TipSlot(), 24 * S, H - 176 * S - 14 * S - 60 * S);
		}
		DrawPlayerPanel(GM, Me);
	}
	else if (bPlayerDead && GM->Phase == EArenaPhase::Playing) { DrawPings(false); DrawDeathPanel(GM); }
	DrawTutorial(GM);
	if (GM->bTraining) { DrawTrainingPanel(GM, PC); }
	else { DrawMinimap(GM, Me); DrawKillFeed(GM); }
	DrawAnnouncements(GM);
	if (PC && Now - PC->NoticeTime < 2.5f)
	{
		Text(PC->Notice, W * 0.5f, H - 260 * S, PC->NoticeColor * FLinearColor(1, 1, 1, FMath::Clamp(2.5f - (Now - PC->NoticeTime), 0.f, 1.f)), 0.8f * S, true, GEngine->GetMediumFont());
	}
	if (PC && PC->bShopOpen && GM->Phase == EArenaPhase::Playing) { DrawShop(GM, PC); }
	if (PC && PC->bScoreboard && GM->Phase != EArenaPhase::Ended) { DrawScoreboard(GM, H * 0.2f); }
	if (GM->Phase == EArenaPhase::Ended)
	{
		Rect(0, 0, W, H, FLinearColor(0, 0, 0, 0.5f));
		const AArenaGameState* EndGS = AArenaGameState::Get(this);
		const int32 Win = EndGS ? EndGS->WinnerTeam : GM->WinnerTeam;
		const bool bWon = Win >= 0 && Win == AArenaCharacter::LocalTeam;   // the viewer's team (a LAN guest plays team 1)
		Text(Win < 0 ? TEXT("DRAW") : (bWon ? TEXT("VICTORY") : TEXT("DEFEAT")), W * 0.5f, H * 0.11f, Win < 0 ? Grey : (bWon ? Gold : Red), 1.9f * S, true, TitleFont());
		DrawScoreboard(GM, H * 0.2f);
		DrawMatchHighlights(H * 0.2f + 120 * S + 5 * 58 * S + 14 * S);
		if (GM->bBotMatch) { Text(TEXT("R — new match     Q — quit"), W * 0.5f, H * 0.86f, White, 1.f * S, true); }
		else { DrawEndButtons(GM, PC); }
	}
}
