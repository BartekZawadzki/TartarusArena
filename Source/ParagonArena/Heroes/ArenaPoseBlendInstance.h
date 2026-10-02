#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/PoseSnapshot.h"
#include "ArenaPoseBlendInstance.generated.h"

class UAnimSequenceBase;

/** Worker-thread side of UArenaPoseBlendInstance: samples the sequence and blends the snapshot over it. */
USTRUCT()
struct FArenaPoseBlendProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FArenaPoseBlendProxy() = default;
	explicit FArenaPoseBlendProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	UAnimSequenceBase* Sequence = nullptr;   // kept alive by the instance's UPROPERTY
	FPoseSnapshot From;                      // the pose on screen when the animation started
	float Time = 0.f;
	float Alpha = 1.f;                       // 0 = the snapshot, 1 = the animation
	float BlendTime = 0.2f;
	bool bLoop = false;

protected:
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
};

/**
 * Plays one animation outside the pack's anim blueprint (a death, the drop-in, the victory emote) and blends into it
 * from the pose the body had, instead of the one-frame pose pop of USkeletalMeshComponent::PlayAnimation. A
 * non-looping animation holds its last frame (a body stays lying).
 */
UCLASS(Transient, NotBlueprintable)
class PARAGONARENA_API UArenaPoseBlendInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void Play(UAnimSequenceBase* InSequence, bool bInLoop, const FPoseSnapshot& From, float BlendTime);
	/** What the next instance plays from its first evaluation: switching the anim class evaluates the new instance
	 *  at once, and without it that first pose would be the reference pose (a one-frame T-pose). */
	static void SetPending(UAnimSequenceBase* InSequence, bool bInLoop, const FPoseSnapshot& From, float BlendTime);
	static void ClearPending();
	/** Consumes a pending play the initialization did not pick up; true if there was one. */
	bool PlayPending();
	float GetTime() const;
	float GetAlpha() const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FArenaPoseBlendProxy(this); }
	virtual void NativeInitializeAnimation() override;

	UPROPERTY() TObjectPtr<UAnimSequenceBase> Sequence;
};
