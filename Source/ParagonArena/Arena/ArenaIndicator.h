#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/ArenaTypes.h"
#include "ArenaIndicator.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class AArenaCharacter;

/** Ability targeting preview, LoL / Smite style: the range around the caster and the area, cone or lane the ability
 *  will cover, following the aim until the cast. Deferred decals (M_ArenaIndicator), so the shapes lie on ramps,
 *  steps and the pit rim. The shapes are computed with the same rules the ability uses (clamped reach, walls, the
 *  dash landing), so what is shown is where it lands. Presentation only. */
UCLASS()
class PARAGONARENA_API AArenaIndicator : public AActor
{
	GENERATED_BODY()

public:
	AArenaIndicator();
	virtual void Tick(float DeltaSeconds) override;

	void Show(AArenaCharacter* InCaster, int32 InSlot);
	void Hide();
	bool IsShown() const { return Slot >= 0; }
	int32 ShownSlot() const { return Slot; }
	/** Where the ability will be aimed: the aim point clamped to its reach (on the ground for area abilities). */
	FVector TargetPoint() const { return Target; }
	/** Where the caster itself ends up (dashes; the caster's position otherwise). */
	FVector LandingPoint() const { return Landing; }

	/** One ground-area telegraph (enemy or own ground AoE while it charges). */
	/** A ground area about to land: FillSeconds > 0 grows the fill from the centre to the rim over that time. */
	static AArenaIndicator* Telegraph(UWorld* World, const FVector& Center, float RadiusCm, const FLinearColor& Color, float Seconds, float FillSeconds = 0.f);
	float TelegraphFill() const { return FillLen > 0.f && GetWorld() ? FMath::Clamp((GetWorld()->GetTimeSeconds() - FillStart) / FillLen, 0.f, 1.f) : 1.f; }
	/** A brief cone on the ground (the reach of a swing that just went off). */
	static AArenaIndicator* FlashCone(UWorld* World, const FVector& Apex, float Yaw, float RadiusCm, float HalfAngleRad, const FLinearColor& Color, float Seconds);

	// the shapes as drawn, in cm (read by the lab checks)
	float RingRadius = 0.f, AreaRadius = 0.f, LaneLength = 0.f, LaneWidth = 0.f;
	bool bReady = true;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UDecalComponent> Ring;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDecalComponent> Area;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDecalComponent> Lane;

private:
	enum class EShape : uint8 { Disc = 0, Ring = 1, Lane = 2, Cone = 3 };
	void Draw(UDecalComponent* D, EShape Shape, const FVector& Center, float Yaw, float HalfForward, float HalfSide, const FLinearColor& Color, float Opacity, float ConeHalfAngleRad = 0.f);
	void Off(UDecalComponent* D);
	void UpdateShapes();

	TWeakObjectPtr<AArenaCharacter> Caster;
	int32 Slot = -1;
	FArenaAbilityDef Ab;
	float FillStart = 0.f, FillLen = 0.f;
	FVector Target = FVector::ZeroVector;
	FVector Landing = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
};
