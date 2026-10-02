// The teaser director (2026-10-01, operator: "run the game and record the best scenes for a teaser"): a film camera
// that finds the action by itself and records it frame by frame (-ArenaTeaser=<scene>, with -benchmark -fps=30 the
// game steps a fixed 1/30 s per frame, so the frames make a smooth 30 fps clip however slowly they render).
// Scenes: aerial (a slow flight over the map), fight (an orbit around the biggest hero fight), follow (behind a hero
// in a fight), tower (Conquest: an orbit around a structure under attack), duel (prototype 1: a side camera on two
// bots fighting), hades (prototype 2: the fixed top-down view on two bots fighting). The HUD is hidden; the frames
// are written as Saved/Screenshots/<platform>/MovieFrameNNNNN.png; the game quits when the clip is done.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaTeaser.generated.h"

class ACameraActor;

UCLASS()
class PARAGONARENA_API AArenaTeaserDirector : public AActor
{
	GENERATED_BODY()

public:
	AArenaTeaserDirector();
	virtual void Tick(float DeltaSeconds) override;

	/** Spawns the director when the command line asks for a scene (-ArenaTeaser=<scene> [-TeaserSeconds=N]
	 *  [-TeaserPreroll=N] [-TeaserWait=N] [-TeaserSlomo=X]). */
	static void MaybeStart(UWorld* World);

	FString Scene;
	float Preroll = 4.f;      // game seconds before it may start recording
	float MaxWait = 300.f;    // ... and at the latest
	float Seconds = 10.f;     // the clip's length (seconds of film: 30 frames each)
	float Slomo = 1.f;
	// the fight orbit (operator 2026-10-01: "the camera was a bit too high"): at the heroes' shoulders, close
	float OrbitR = 760.f;     // -TeaserOrbit=cm
	float OrbitH = 120.f;     // -TeaserHeight=cm above the middle of the fight (the heroes' capsule centres)        // -TeaserSlomo=0.4: the game slowed while filming (a smooth slow motion, the camera at full speed)

private:
	UPROPERTY() TObjectPtr<ACameraActor> Cam;
	float Born = -1.f, RecStart = -1.f;
	bool bRecording = false, bPlaced = false;
	FVector Focus = FVector::ZeroVector, CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	float Angle = 0.f;
	TWeakObjectPtr<AActor> Followed;
	FVector AerialA = FVector::ZeroVector, AerialB = FVector::ZeroVector, AerialLook = FVector::ZeroVector;
	TSet<TWeakObjectPtr<AActor>> CutAway;

	/** The hottest spot: the heroes (or the prototype's fighters) closest together with enemies; Heat = how many. */
	bool HotSpot(FVector& Out, int32& Heat, AActor*& Star) const;
	bool StructureUnderAttack(FVector& Out, int32& Heat) const;
	void Aim(const FVector& Loc, const FVector& LookAt, float Fov, float Dt, float Lag, bool bSnap);
	void UpdateCutaway(const FVector& From, const FVector& To);
	void SetupStage();
	void StartRecording();
};
