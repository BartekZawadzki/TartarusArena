#include "UI/ArenaSettings.h"
#include "Heroes/ArenaCharacter.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AudioDevice.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "RHI.h"
#include "EngineUtils.h"

namespace
{
	const TCHAR* Section = TEXT("ArenaSettings");
	const TCHAR* KeySection = TEXT("ArenaKeys");
}

const TArray<FArenaSettings::FKeyDef>& FArenaSettings::KeyDefs()
{
	static const TArray<FKeyDef> Defs = {
		{ TEXT("Forward"), EKeys::W, TEXT("Move: forward") }, { TEXT("Back"), EKeys::S, TEXT("Move: backward") },
		{ TEXT("Left"), EKeys::A, TEXT("Move: left") }, { TEXT("Right"), EKeys::D, TEXT("Move: right") },
		{ TEXT("Jump"), EKeys::SpaceBar, TEXT("Jump") },
		{ TEXT("Ability1"), EKeys::One, TEXT("Ability 1") }, { TEXT("Ability2"), EKeys::Two, TEXT("Ability 2") },
		{ TEXT("Ability3"), EKeys::Three, TEXT("Ability 3") }, { TEXT("Ability4"), EKeys::Four, TEXT("Ultimate") },
		{ TEXT("PotionHp"), EKeys::Five, TEXT("Health potion") }, { TEXT("PotionMana"), EKeys::Six, TEXT("Mana potion") },
		{ TEXT("Shop"), EKeys::B, TEXT("Shop in base / return") }, { TEXT("ShopAny"), EKeys::P, TEXT("Shop anywhere") },
		{ TEXT("Revive"), EKeys::F, TEXT("Revive for gold") }, { TEXT("Scoreboard"), EKeys::Tab, TEXT("Scoreboard") },
		{ TEXT("Ping"), EKeys::G, TEXT("Ping for team") } };
	return Defs;
}

FKey FArenaSettings::KeyFor(FName Action) const
{
	if (const FKey* K = Keys.Find(Action)) { return *K; }
	for (const FKeyDef& D : KeyDefs()) { if (D.Action == Action) { return D.Default; } }
	return EKeys::Invalid;
}

void FArenaSettings::Rebind(FName Action, const FKey& Key)
{
	const FKey Old = KeyFor(Action);
	for (const FKeyDef& D : KeyDefs())
	{
		if (D.Action != Action && KeyFor(D.Action) == Key) { Keys.Add(D.Action, Old); }   // a swap, not a clash
	}
	Keys.Add(Action, Key);
}

FArenaSettings& FArenaSettings::Get()
{
	static FArenaSettings S;
	static bool bLoaded = false;
	if (!bLoaded) { bLoaded = true; S.Load(); }
	return S;
}

TArray<FIntPoint> FArenaSettings::Resolutions()
{
	TArray<FIntPoint> Out;
	const UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	const FIntPoint Desk = GS ? GS->GetDesktopResolution() : FIntPoint(0, 0);
	if (Desk.X > 0 && Desk.Y > 0) { Out.Add(Desk); }
	for (const FIntPoint R : { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) })
	{
		if ((Desk.X <= 0 || (R.X <= Desk.X && R.Y <= Desk.Y)) && !Out.Contains(R)) { Out.Add(R); }
	}
	return Out;
}

void FArenaSettings::Load()
{
	if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Quality = GS->GetOverallScalabilityLevel();   // -1: mixed levels (none of the four is shown selected)
		const EWindowMode::Type M = GS->GetFullscreenMode();
		WindowMode = M == EWindowMode::Fullscreen ? 0 : (M == EWindowMode::WindowedFullscreen ? 1 : 2);
		Resolution = Resolutions().IndexOfByKey(GS->GetScreenResolution());
		bVSync = GS->IsVSyncEnabled();
		FpsLimit = FMath::RoundToInt(GS->GetFrameRateLimit());
		// the render scale is our own key: the engine's value follows the quality level (High = 87 %) and 0 means auto,
		// which the first version read as "50 %" and wrote back (a 1440p desktop rendered at 1280 x 720)
	}
	if (!GConfig) { return; }
	GConfig->GetFloat(Section, TEXT("Volume"), Volume, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("Sensitivity"), Sensitivity, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("InvertY"), bInvertY, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("CastMode"), CastMode, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("AimAssist"), bAimAssist, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("CameraShake"), bCameraShake, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("DamageNumbers"), bDamageNumbers, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("Fov"), Fov, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("RenderScale"), RenderScale, GGameUserSettingsIni);
	RenderScale = RenderScale <= 0 ? 0 : FMath::Clamp(RenderScale, 50, 100);
	GConfig->GetFloat(Section, TEXT("UiScale"), UiScale, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("Colorblind"), bColorblind, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("VoiceVolume"), VoiceVolume, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("Tutorial"), bTutorial, GGameUserSettingsIni);
	SkinChoice.Reset();
	TArray<FString> SkinLines;
	if (GConfig->GetSection(TEXT("ArenaSkins"), SkinLines, GGameUserSettingsIni))
	{
		for (const FString& L : SkinLines) { FString K, V; if (L.Split(TEXT("="), &K, &V)) { SkinChoice.Add(FName(*K), FCString::Atoi(*V)); } }
	}
	UiScale = FMath::Clamp(UiScale, 0.8f, 1.3f);
	VoiceVolume = FMath::Clamp(VoiceVolume, 0.f, 1.f);
	Keys.Reset();
	for (const FKeyDef& D : KeyDefs())
	{
		FString Name;
		if (GConfig->GetString(KeySection, *D.Action.ToString(), Name, GGameUserSettingsIni))
		{
			const FKey K(*Name);
			if (K.IsValid() && !K.IsMouseButton() && !K.IsGamepadKey()) { Keys.Add(D.Action, K); }
		}
	}
	Volume = FMath::Clamp(Volume, 0.f, 1.f);
	Sensitivity = FMath::Clamp(Sensitivity, 0.2f, 3.f);
	Fov = FMath::Clamp(Fov, 80.f, 110.f);
	CastMode = FMath::Clamp(CastMode, 0, 2);
#if !WITH_EDITOR
	// v11: Epic now means Lumen (about 4.7 ms a frame more): a player on Epic (or mixed) moves to High once, so the
	// update does not drop their frame rate unasked; Epic stays one click away in the settings
	int32 GfxVersion = 0;
	GConfig->GetInt(Section, TEXT("GfxVersion"), GfxVersion, GGameUserSettingsIni);
	if (GfxVersion < 11)
	{
		GConfig->SetInt(Section, TEXT("GfxVersion"), 11, GGameUserSettingsIni);
		if (Quality == 3 || Quality < 0)
		{
			Quality = 2;
			Save();
			if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr) { GS->ApplyNonResolutionSettings(); }
		}
		GConfig->Flush(false, GGameUserSettingsIni);
	}
#endif
}

void FArenaSettings::Save() const
{
	if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		if (Quality >= 0) { GS->SetOverallScalabilityLevel(Quality); }
		GS->SetFullscreenMode(WindowMode == 0 ? EWindowMode::Fullscreen : (WindowMode == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed));
		const TArray<FIntPoint> Res = Resolutions();
		if (Res.IsValidIndex(Resolution)) { GS->SetScreenResolution(Res[Resolution]); }
		GS->SetVSyncEnabled(bVSync);
		GS->SetFrameRateLimit(static_cast<float>(FpsLimit));
		GS->SetResolutionScaleValueEx(static_cast<float>(RenderScale));   // after the quality level, which resets it (0 = auto)
		GS->SaveSettings();
	}
	if (!GConfig) { return; }
	GConfig->SetFloat(Section, TEXT("Volume"), Volume, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("Sensitivity"), Sensitivity, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("InvertY"), bInvertY, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("CastMode"), CastMode, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("AimAssist"), bAimAssist, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("CameraShake"), bCameraShake, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("DamageNumbers"), bDamageNumbers, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("Fov"), Fov, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("RenderScale"), RenderScale, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("UiScale"), UiScale, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("Colorblind"), bColorblind, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("VoiceVolume"), VoiceVolume, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("Tutorial"), bTutorial, GGameUserSettingsIni);
	GConfig->EmptySection(TEXT("ArenaSkins"), GGameUserSettingsIni);
	for (const TPair<FName, int32>& Sk : SkinChoice) { GConfig->SetInt(TEXT("ArenaSkins"), *Sk.Key.ToString(), Sk.Value, GGameUserSettingsIni); }
	GConfig->EmptySection(KeySection, GGameUserSettingsIni);
	for (const TPair<FName, FKey>& K : Keys) { GConfig->SetString(KeySection, *K.Key.ToString(), *K.Value.ToString(), GGameUserSettingsIni); }
	SyncShaderModel();
	GConfig->Flush(false, GGameUserSettingsIni);
}

void FArenaSettings::SyncShaderModel() const
{
	if (!GConfig) { return; }
	GConfig->SetString(TEXT("D3DRHIPreference"), TEXT("PreferredFeatureLevel"), Quality == 3 ? TEXT("sm6") : TEXT("sm5"), GGameUserSettingsIni);
}

bool FArenaSettings::NeedsRestart() const
{
	const bool bSM6 = GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6;
	return Quality >= 0 && (Quality == 3) != bSM6;
}

void FArenaSettings::Apply(UWorld* World) const
{
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("arena.AimAssist"))) { V->Set(bAimAssist ? 1 : 0, ECVF_SetByGameSetting); }
	if (GEngine)
	{
		if (FAudioDeviceHandle Dev = GEngine->GetMainAudioDevice()) { Dev->SetTransientPrimaryVolume(Volume); }
	}
	AArenaCharacter::bShakeEnabled = bCameraShake;
	if (World)
	{
		// the rings under the heroes follow the colour mode at once
		for (TActorIterator<AArenaCharacter> It(World); It; ++It) { It->UpdateTeamRing(); }
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (AArenaCharacter* H = Cast<AArenaCharacter>(PC->GetPawn())) { H->SetBaseFov(Fov); }
		}
	}
}
