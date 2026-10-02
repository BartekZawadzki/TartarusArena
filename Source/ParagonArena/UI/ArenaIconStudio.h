// Renders the HUD's art at runtime from the library assets, so it always matches heroes.json and the map:
// hero portraits (each pack's hero-select pose), ability icons (the hero frozen at the key frame of that ability's
// own animation, rim-lit in the ability's colour, the way Paragon drew its icons) and the minimap (an orthographic,
// unlit shot of the arena). Shot with show-only scene captures in a booth far above the map; the render targets
// are what ArenaHUD draws. -ArenaIconExport also writes them to Saved/Icons.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaIconStudio.generated.h"

class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UPointLightComponent;
class ASkeletalMeshActor;
class AStaticMeshActor;

UCLASS()
class PARAGONARENA_API AArenaIconStudio : public AActor
{
	GENERATED_BODY()

public:
	AArenaIconStudio();
	/** The studio of this world (spawned by the game mode). */
	static AArenaIconStudio* Get(const UObject* WorldContext);

	UTextureRenderTarget2D* Portrait(int32 HeroIndex) const { return bReady && Portraits.IsValidIndex(HeroIndex) ? Portraits[HeroIndex] : nullptr; }
	UTextureRenderTarget2D* AbilityIcon(int32 HeroIndex, int32 Slot) const { const int32 I = HeroIndex * 5 + Slot; return bReady && Icons.IsValidIndex(I) ? Icons[I] : nullptr; }
	UTextureRenderTarget2D* Minimap() const { return bReady ? MinimapRT : nullptr; }
	/** World position -> 0..1 minimap coordinates (valid once the minimap is captured). */
	FVector2D MinimapUV(const FVector& World) const;
	float MinimapAspect() const { return MiniOrthoH > 0.f ? MiniOrthoW / MiniOrthoH : 1.6f; }
	bool IsReady() const { return bReady; }
	/** The hero browser's live stage: the hero at full height, idle (Slot -1) or performing an ability with its
	 *  cast and impact effects; captured every frame while shown. */
	void SetPreview(int32 HeroIndex, int32 Slot);
	void StopPreview();
	/** The preview's body: a skin (empty: the hero's default). */
	void SetPreviewMesh(const FString& MeshPath);
	UTextureRenderTarget2D* PreviewTarget() const { return PreviewRT; }
	int32 PreviewHero() const { return PreviewHeroIndex; }

protected:
	virtual void BeginPlay() override;

private:
	AStaticMeshActor* SpawnBackdrop(const FVector& At, const FVector& Facing, float Size, const FLinearColor& Tint);
	ASkeletalMeshActor* SpawnBooth(int32 HeroIndex, const FVector& At, const FString& AnimPath, float Frame, const FLinearColor& Rim, bool bPortraitLights);
	USceneCaptureComponent2D* MakeCapture(UTextureRenderTarget2D* Target, float Fov, float ExposureBias);
	void Shoot();
	void Finish();
	/** Waits for the booths' (and the level's) pipeline states before shooting: until they are compiled the engine
	 *  does not draw a mesh at all, and a first start on a new shader model shot empty portraits. */
	void TryShoot();
	FTimerHandle WaitTimer;
	float WaitStart = 0.f;
	bool bShotOnce = false;

	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Portraits;
	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Icons;        // HeroIndex * 5 + Slot
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> MinimapRT;
	UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
	UPROPERTY() TArray<TObjectPtr<ASkeletalMeshActor>> PortraitBooths;  // per hero
	UPROPERTY() TArray<TObjectPtr<ASkeletalMeshActor>> IconBooths;      // per hero * 5 + slot
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> Backdrops;
	UPROPERTY() TObjectPtr<ASkeletalMeshActor> PreviewBooth;
	UPROPERTY() TObjectPtr<AStaticMeshActor> PreviewBack;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> PreviewCapture;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PreviewRT;
	int32 PreviewHeroIndex = -1;
	FTimerHandle PreviewIdle, PreviewImpact;
	void PreviewPlay(const FString& AnimPath, bool bLoop);

	FVector Origin = FVector(0.f, 0.f, 90000.f);
	FVector MiniCam = FVector::ZeroVector, MiniRight = FVector::RightVector, MiniUp = FVector::ForwardVector;
	float MiniOrthoW = 0.f, MiniOrthoH = 0.f;
	bool bReady = false;
};
