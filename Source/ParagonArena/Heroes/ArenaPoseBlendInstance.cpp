#include "Heroes/ArenaPoseBlendInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"

void FArenaPoseBlendProxy::Update(float DeltaSeconds)
{
	if (!Sequence) { return; }
	const float Len = FMath::Max(0.01f, Sequence->GetPlayLength());
	Time += DeltaSeconds;
	Time = bLoop ? FMath::Fmod(Time, Len) : FMath::Min(Time, Len);   // a death holds its last frame
	Alpha = BlendTime > 0.f ? FMath::Min(1.f, Alpha + DeltaSeconds / BlendTime) : 1.f;
}

bool FArenaPoseBlendProxy::Evaluate(FPoseContext& Output)
{
	if (!Sequence) { Output.ResetToRefPose(); return true; }
	FAnimationPoseData PoseData(Output);
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false, {}, bLoop));
	if (Alpha >= 1.f || !From.bIsValid) { return true; }
	// ease out: most of the way in the first half of the blend, no visible settle at the end
	const float A = 1.f - FMath::Square(1.f - Alpha);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
	{
		const int32 MeshIndex = Bones.MakeMeshPoseIndex(I).GetInt();
		if (!From.LocalTransforms.IsValidIndex(MeshIndex)) { continue; }
		FTransform Blended;
		Blended.Blend(From.LocalTransforms[MeshIndex], Output.Pose[I], A);
		Output.Pose[I] = Blended;
	}
	return true;
}

namespace
{
	struct FPendingPlay
	{
		TWeakObjectPtr<UAnimSequenceBase> Sequence;
		FPoseSnapshot From;
		float BlendTime = 0.f;
		bool bLoop = false;
		bool bSet = false;
	};
	FPendingPlay GPendingPlay;   // game thread only: set right before the anim class switch that consumes it
}

void UArenaPoseBlendInstance::SetPending(UAnimSequenceBase* InSequence, bool bInLoop, const FPoseSnapshot& From, float BlendTime)
{
	GPendingPlay.Sequence = InSequence;
	GPendingPlay.From = From;
	GPendingPlay.BlendTime = BlendTime;
	GPendingPlay.bLoop = bInLoop;
	GPendingPlay.bSet = true;
}

bool UArenaPoseBlendInstance::PlayPending()
{
	if (!GPendingPlay.bSet) { return false; }
	GPendingPlay.bSet = false;
	Play(GPendingPlay.Sequence.Get(), GPendingPlay.bLoop, GPendingPlay.From, GPendingPlay.BlendTime);
	GPendingPlay.From = FPoseSnapshot();
	return true;
}

void UArenaPoseBlendInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	PlayPending();
}

void UArenaPoseBlendInstance::Play(UAnimSequenceBase* InSequence, bool bInLoop, const FPoseSnapshot& From, float BlendTime)
{
	Sequence = InSequence;
	RootMotionMode = ERootMotionMode::IgnoreRootMotion;
	FArenaPoseBlendProxy& P = GetProxyOnGameThread<FArenaPoseBlendProxy>();
	P.Sequence = InSequence;
	P.From = From;
	P.Time = 0.f;
	P.Alpha = From.bIsValid ? 0.f : 1.f;
	P.BlendTime = BlendTime;
	P.bLoop = bInLoop;
}

float UArenaPoseBlendInstance::GetTime() const { return GetProxyOnGameThread<FArenaPoseBlendProxy>().Time; }
float UArenaPoseBlendInstance::GetAlpha() const { return GetProxyOnGameThread<FArenaPoseBlendProxy>().Alpha; }

void UArenaPoseBlendInstance::ClearPending() { GPendingPlay = FPendingPlay(); }
