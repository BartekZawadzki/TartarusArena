#include "Heroes/ArenaMinionAnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "GameFramework/Pawn.h"

const FName UArenaMinionAnimInstance::SlotName(TEXT("DefaultSlot"));

void FArenaMinionAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	// what an anim graph's slot node does when it initializes: montages on this slot now have somewhere to play
	RegisterSlotNodeWithAnimInstance(UArenaMinionAnimInstance::SlotName);
}

void FArenaMinionAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const APawn* Pawn = InAnimInstance ? InAnimInstance->TryGetPawnOwner() : nullptr;
	Speed = Pawn ? Pawn->GetVelocity().Size2D() : 0.f;
}

void FArenaMinionAnimProxy::Update(float DeltaSeconds)
{
	// the jog fades in over the first metre per second and plays as fast as the body moves (clamped: past it the
	// feet would visibly skate or flail)
	const float Target = FMath::Clamp(Speed / 120.f, 0.f, 1.f);
	RunAlpha = FMath::FInterpTo(RunAlpha, Target, DeltaSeconds, 9.f);
	IdleTime += DeltaSeconds;
	RunTime += DeltaSeconds * FMath::Clamp(Speed / FMath::Max(1.f, RunSpeed), 0.6f, 1.45f);
	if (Idle) { IdleTime = FMath::Fmod(IdleTime, FMath::Max(0.01f, Idle->GetPlayLength())); }
	if (Run) { RunTime = FMath::Fmod(RunTime, FMath::Max(0.01f, Run->GetPlayLength())); }
	GetSlotWeight(UArenaMinionAnimInstance::SlotName, SlotWeight, SourceWeight, TotalWeight);
	UpdateSlotNodeWeight(UArenaMinionAnimInstance::SlotName, SlotWeight, 1.f);
}

bool FArenaMinionAnimProxy::Evaluate(FPoseContext& Output)
{
	auto Locomotion = [this](FPoseContext& Out)
	{
		FAnimationPoseData Data(Out);
		const bool bRun = Run && RunAlpha > 0.02f;
		const bool bIdle = Idle && (RunAlpha < 0.98f || !bRun);
		if (!bRun && !bIdle) { Out.ResetToRefPose(); return; }
		if (bRun && !bIdle) { Run->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(RunTime), false, {}, true)); return; }
		Idle->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(IdleTime), false, {}, true));
		if (!bRun) { return; }
		FPoseContext RunPose(Out);
		FAnimationPoseData RunData(RunPose);
		Run->GetAnimationPose(RunData, FAnimExtractContext(static_cast<double>(RunTime), false, {}, true));
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data, RunData, 1.f - RunAlpha);
	};
	if (SlotWeight <= ZERO_ANIMWEIGHT_THRESH)
	{
		Locomotion(Output);
		return true;
	}
	// a montage plays: the slot blends it over the locomotion (none at all under a full-weight montage)
	FPoseContext Source(Output);
	if (SourceWeight > ZERO_ANIMWEIGHT_THRESH) { Locomotion(Source); } else { Source.ResetToRefPose(); }
	const FAnimationPoseData SourceData(Source);
	FAnimationPoseData OutData(Output);
	SlotEvaluatePose(UArenaMinionAnimInstance::SlotName, SourceData, SourceWeight, OutData, SlotWeight, TotalWeight);
	return true;
}

void UArenaMinionAnimInstance::Setup(UAnimSequenceBase* InIdle, UAnimSequenceBase* InRun, float InRunSpeed)
{
	Idle = InIdle;
	Run = InRun;
	FArenaMinionAnimProxy& P = GetProxyOnGameThread<FArenaMinionAnimProxy>();
	P.Idle = InIdle;
	P.Run = InRun;
	P.RunSpeed = FMath::Max(50.f, InRunSpeed);
	P.IdleTime = FMath::FRandRange(0.f, InIdle ? InIdle->GetPlayLength() : 1.f);   // a wave does not breathe in step
	P.RunTime = FMath::FRandRange(0.f, InRun ? InRun->GetPlayLength() : 1.f);
}
