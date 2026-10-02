#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ArenaMinionAnimInstance.generated.h"

class UAnimSequenceBase;

/** Worker-thread side of UArenaMinionAnimInstance: idle / jog by speed, the full-body slot over it. */
USTRUCT()
struct FArenaMinionAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FArenaMinionAnimProxy() = default;
	explicit FArenaMinionAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	UAnimSequenceBase* Idle = nullptr;   // kept alive by the instance's UPROPERTYs
	UAnimSequenceBase* Run = nullptr;
	float RunSpeed = 350.f;              // cm/s the jog covers at play rate 1 (mesh scale included)
	float Speed = 0.f;                   // the owner's ground speed, copied on the game thread
	float IdleTime = 0.f, RunTime = 0.f, RunAlpha = 0.f;
	float SlotWeight = 0.f, SourceWeight = 1.f, TotalWeight = 0.f;

protected:
	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
};

/**
 * The lane minions' animation (Paragon: Minions ships its clips without an anim blueprint): an idle and a jog blended
 * by the ground speed, the jog's play rate following the speed (no foot sliding), and a full-body "DefaultSlot" over
 * both, so attacks, hit reactions, stuns and deaths play as montages exactly like on the heroes' blueprints.
 */
UCLASS(Transient, NotBlueprintable)
class PARAGONARENA_API UArenaMinionAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	static const FName SlotName;
	void Setup(UAnimSequenceBase* InIdle, UAnimSequenceBase* InRun, float InRunSpeed);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FArenaMinionAnimProxy(this); }

	UPROPERTY() TObjectPtr<UAnimSequenceBase> Idle;
	UPROPERTY() TObjectPtr<UAnimSequenceBase> Run;
};
