#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class UWorld;

/**
 * The player's settings (menu → Settings). Graphics live in UGameUserSettings (the engine's own file); the rest in
 * the [ArenaSettings] section of GameUserSettings.ini next to them. Everything applies at once and is saved on change.
 */
struct PARAGONARENA_API FArenaSettings
{
	// graphics
	int32 Quality = 3;              // 0 low .. 3 epic (scalability); -1 mixed
	int32 WindowMode = 1;           // 0 fullscreen, 1 windowed fullscreen, 2 window
	int32 Resolution = -1;          // index into Resolutions(); -1 = the desktop's
	bool bVSync = false;
	int32 FpsLimit = 0;             // 0 = none, else 60 / 120 / 144
	int32 RenderScale = 0;          // % of the output resolution rendered (TSR upscales the rest): 0 = auto (the engine's
	                                // default for the display), else 50 .. 100; kept apart from the quality level
	// sound
	float Volume = 1.f;             // 0..1
	// controls
	float Sensitivity = 1.f;        // 0.2 .. 3
	bool bInvertY = false;
	/** How 1-4 cast (LoL / Smite): 0 quick with a preview (the indicator while the key is held, the release casts),
	 *  1 instant (the press casts at the crosshair), 2 with confirmation (the key aims, LMB or the key again casts). */
	int32 CastMode = 0;
	// gameplay
	bool bAimAssist = true;
	bool bCameraShake = true;
	bool bDamageNumbers = true;
	float Fov = 95.f;               // 80 .. 110
	// accessibility (v16)
	float UiScale = 1.f;            // 0.8 .. 1.3: the match HUD's size (the menus keep theirs)
	bool bColorblind = false;       // enemies orange instead of red (red / green-blind players tell them from allies)
	float VoiceVolume = 1.f;        // the heroes' voice lines, 0..1 (on top of the volume)
	bool bTutorial = true;          // the beginner's hints in the first matches (switched off after a finished match)
	/** The skin chosen for each hero (-1 or missing: the default body). */
	TMap<FName, int32> SkinChoice;
	int32 SkinFor(FName Hero) const { const int32* S = SkinChoice.Find(Hero); return S ? *S : -1; }
	/** The rebindable keys (keyboard); the mouse buttons and the gamepad are fixed. */
	struct FKeyDef { FName Action; FKey Default; const TCHAR* Label; };
	static const TArray<FKeyDef>& KeyDefs();
	TMap<FName, FKey> Keys;
	FKey KeyFor(FName Action) const;
	/** Binds Key to Action; the action that had it takes Action's old key (never two actions on one key). */
	void Rebind(FName Action, const FKey& Key);
	void ResetKeys() { Keys.Reset(); }
	/** The enemy colour for the HUD and the rings (orange in the colour-blind mode). */
	FLinearColor EnemyColor() const { return bColorblind ? FLinearColor(1.f, 0.55f, 0.05f) : FLinearColor(1.f, 0.3f, 0.25f); }

	static FArenaSettings& Get();
	/** The resolutions offered (the desktop's first, then the usual 16:9 ones that fit). */
	static TArray<FIntPoint> Resolutions();
	void Load();
	void Save() const;
	/** Pushes the settings into the engine (scalability, window, audio, CVars) and onto the player's hero. */
	void Apply(UWorld* World) const;
	/** The shader model for the next start: SM6 (Lumen) for Epic, SM5 (faster) otherwise. */
	void SyncShaderModel() const;
	/** The running shader model differs from what the quality wants (the change needs a restart). */
	bool NeedsRestart() const;
};
