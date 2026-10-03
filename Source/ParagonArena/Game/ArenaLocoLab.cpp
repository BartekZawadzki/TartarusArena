// -ArenaLocoLab (v17): how the heroes move — the operator saw stiff locomotion. Every hero (or -LocoHero=Id) runs a
// player's input on the Conquest map's mid lane: idle, forward, a strafe, backwards, a release to a stop, then a bot's
// path to a point behind it. Logs the Paragon anim blueprint's state machines as they change (LOCO_STATE), the pace
// and the blueprint's own variables once per phase (LOCO_VARS), and takes LOCO_*.png screenshots from a side camera.
// A measurement, not a check: LAB_SUMMARY fails=0.
#include "Game/ArenaGameMode.h"
#include "Game/ArenaEvidence.h"
#include "Heroes/ArenaCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_StateMachine.h"
#include "AI/ArenaBotController.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#endif

namespace
{
	struct FLocoPhase { const TCHAR* Name; FVector Input; float Seconds; bool bShot; float ShotAt = -1.f; };
	// facing +X the whole time (the player's hero faces the camera's yaw): forward, a strafe to its left, backwards
	const FLocoPhase GLocoPhases[] = {
		{ TEXT("idle"), FVector::ZeroVector, 1.0f, true, 0.8f },
		{ TEXT("fwd"), FVector(1.f, 0.f, 0.f), 2.2f, true },
		{ TEXT("strafe"), FVector(0.f, -1.f, 0.f), 1.2f, true },
		{ TEXT("back"), FVector(-1.f, 0.f, 0.f), 1.5f, true },
		{ TEXT("stop"), FVector::ZeroVector, 1.4f, true },
		{ TEXT("relax"), FVector::ZeroVector, 5.0f, true, 4.6f },
		{ TEXT("path"), FVector::ZeroVector, 3.0f, true },
	};
	constexpr int32 GLocoNumPhases = UE_ARRAY_COUNT(GLocoPhases);

	struct FLocoState
	{
		TArray<int32> Heroes;
		int32 Hero = 0, Phase = -1;
		float PhaseAt = 0.f, NextSample = 0.f;
		bool bVars = false, bShotDone = false;
		TWeakObjectPtr<AArenaCharacter> H;
		TWeakObjectPtr<ACameraActor> Cam;
		TArray<FName> LastStates;
		float PeakSpeed = 0.f, SpeedSum = 0.f; int32 Samples = 0; int32 StateChanges = 0;
	};
	FLocoState GLoco;

	FString LocoVars(UAnimInstance* Inst)
	{
		// the blueprint's own variables (the native UAnimInstance ones are left out)
		FString Out;
		for (TFieldIterator<FProperty> It(Inst->GetClass()); It; ++It)
		{
			const FProperty* P = *It;
			if (P->GetOwnerClass() == UAnimInstance::StaticClass() || P->GetOwnerClass()->IsNative()) { continue; }
			const void* V = P->ContainerPtrToValuePtr<void>(Inst);
			FString Val;
			if (const FFloatProperty* F = CastField<FFloatProperty>(P)) { Val = FString::Printf(TEXT("%.2f"), F->GetPropertyValue(V)); }
			else if (const FDoubleProperty* D = CastField<FDoubleProperty>(P)) { Val = FString::Printf(TEXT("%.2f"), D->GetPropertyValue(V)); }
			else if (const FBoolProperty* B = CastField<FBoolProperty>(P)) { Val = B->GetPropertyValue(V) ? TEXT("1") : TEXT("0"); }
			else if (const FIntProperty* I = CastField<FIntProperty>(P)) { Val = FString::FromInt(I->GetPropertyValue(V)); }
			else if (const FStructProperty* S = CastField<FStructProperty>(P))
			{
				if (S->Struct == TBaseStructure<FVector>::Get()) { Val = static_cast<const FVector*>(V)->ToCompactString(); }
				else if (S->Struct == TBaseStructure<FRotator>::Get()) { Val = static_cast<const FRotator*>(V)->ToCompactString(); }
				else { continue; }
			}
			else if (const FObjectPropertyBase* O = CastField<FObjectPropertyBase>(P)) { const UObject* Obj = O->GetObjectPropertyValue(V); Val = Obj ? Obj->GetClass()->GetName() : TEXT("null"); }
			else { continue; }
			Out += FString::Printf(TEXT(" %s=%s"), *P->GetName(), *Val);
		}
		return Out;
	}

	void LocoStates(UAnimInstance* Inst, TArray<FName>& Out, FString& Names)
	{
		Out.Reset();
		const IAnimClassInterface* Cls = IAnimClassInterface::GetFromClass(Inst->GetClass());
		if (!Cls) { return; }
		const TArray<FBakedAnimationStateMachine>& Machines = Cls->GetBakedStateMachines();
		for (int32 m = 0; m < Machines.Num(); ++m)
		{
			const FAnimNode_StateMachine* SM = Inst->GetStateMachineInstance(m);
			const FName State = SM ? SM->GetCurrentStateName() : NAME_None;
			Out.Add(State);
			Names += FString::Printf(TEXT(" %s:%s"), *Machines[m].MachineName.ToString(), *State.ToString());
		}
	}
}

#if WITH_EDITOR
namespace
{
	// -ArenaDumpAnimBP=/Game/...: every graph, node, pin default and link of a Paragon anim blueprint (what its
	// locomotion reads from the pawn: the pack's graphs cast to their own PlayerCharacter blueprints)
	void DumpAnimBlueprint(const FString& Path)
	{
		UAnimBlueprint* BP = LoadObject<UAnimBlueprint>(nullptr, *Path);
		if (!BP) { ARENA_LOG(LogArena, Display, TEXT("ABP_DUMP missing %s"), *Path); return; }
		TArray<UEdGraph*> Graphs;
		BP->GetAllGraphs(Graphs);
		for (UEdGraph* G : Graphs)
		{
			ARENA_LOG(LogArena, Display, TEXT("ABP_GRAPH %s nodes=%d"), *G->GetName(), G->Nodes.Num());
			for (UEdGraphNode* N : G->Nodes)
			{
				if (!N) { continue; }
				FString Pins;
				for (UEdGraphPin* P : N->Pins)
				{
					if (!P || P->bHidden) { continue; }
					FString Links;
					for (UEdGraphPin* L : P->LinkedTo) { if (L && L->GetOwningNode()) { Links += FString::Printf(TEXT("%s%s.%s"), Links.IsEmpty() ? TEXT("") : TEXT(","), *L->GetOwningNode()->GetName(), *L->PinName.ToString()); } }
					if (Links.IsEmpty() && P->DefaultValue.IsEmpty() && !P->DefaultObject) { continue; }
					Pins += FString::Printf(TEXT(" [%s%s=%s%s]"), P->Direction == EGPD_Input ? TEXT("in:") : TEXT("out:"), *P->PinName.ToString(),
						P->DefaultObject ? *P->DefaultObject->GetName() : *P->DefaultValue, Links.IsEmpty() ? TEXT("") : *(TEXT(" ->") + Links));
				}
				ARENA_LOG(LogArena, Display, TEXT("ABP_NODE %s | %s | %s |%s"), *G->GetName(), *N->GetName(), *N->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" ")), *Pins);
			}
		}
	}
}
#endif

void AArenaGameMode::TickLocoLab(float Now)
{
#if WITH_EDITOR
	static bool bDumped = false;
	FString DumpPath;
	if (!bDumped && FParse::Value(FCommandLine::Get(), TEXT("ArenaDumpAnimBP="), DumpPath))
	{
		bDumped = true;
		DumpAnimBlueprint(DumpPath);
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		LabStep = 2;
		return;
	}
#endif
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	const FVector Start(-3200.f, 150.f, 60.f);
	if (LabStep == 0)
	{
		if (!NavReady() || Now - LabStart < 1.f) { return; }
		Phase = EArenaPhase::Playing;
		MatchStart = Now;
		FString Only;
		FParse::Value(FCommandLine::Get(), TEXT("LocoHero="), Only);
		for (int32 i = 0; i < Defs.Num(); ++i) { if (Only.IsEmpty() || Defs[i].Id.ToString() == Only) { GLoco.Heroes.Add(i); } }
		if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Start + FVector(0.f, -700.f, 200.f), FRotator(-8.f, 90.f, 0.f)))
		{
			GLoco.Cam = Cam;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
		}
		LabStep = 1;
		return;
	}
	if (LabStep == 2) { return; }
	AArenaCharacter* H = GLoco.H.Get();
	if (!H)
	{
		if (!GLoco.Heroes.IsValidIndex(GLoco.Hero))
		{
			ARENA_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=0"));
			LabStep = 2;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
			return;
		}
		H = SpawnHero(GLoco.Heroes[GLoco.Hero], 0, 2, false, 9, 0.f, nullptr);
		if (!H) { ++GLoco.Hero; return; }
		if (AController* C = H->GetController()) { C->UnPossess(); C->Destroy(); }
		H->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		H->SetActorLocation(Start + FVector(0.f, 0.f, H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
		H->SetActorRotation(FRotator::ZeroRotator);
		GLoco.H = H;
		GLoco.Phase = 0;
		GLoco.PhaseAt = Now;
		GLoco.bVars = false; GLoco.bShotDone = false;
		GLoco.LastStates.Reset();
		GLoco.PeakSpeed = GLoco.SpeedSum = 0.f; GLoco.Samples = 0; GLoco.StateChanges = 0;
		return;
	}
	const FLocoPhase& P = GLocoPhases[GLoco.Phase];
	const float InPhase = Now - GLoco.PhaseAt;
	const FString Id = H->GetDef().Id.ToString();
	// the input of this phase, every frame (a held key); the yaw stays on +X
	if (!P.Input.IsNearlyZero()) { H->AddMovementInput(P.Input, 1.f); }
	const bool bPath = FCString::Strcmp(P.Name, TEXT("path")) == 0;
	// (v17: the hero turns to where it runs by itself, like the player's)
	// the side camera follows along X
	if (ACameraActor* Cam = GLoco.Cam.Get()) { Cam->SetActorLocation(FVector(H->GetActorLocation().X, H->GetActorLocation().Y - 620.f, H->GetActorLocation().Z + 150.f)); }
	UAnimInstance* Inst = H->GetMesh()->GetAnimInstance();
	if (Inst && Now >= GLoco.NextSample)
	{
		GLoco.NextSample = Now + 0.05f;
		TArray<FName> States;
		FString Names;
		LocoStates(Inst, States, Names);
		const float V = H->GetVelocity().Size2D();
		GLoco.PeakSpeed = FMath::Max(GLoco.PeakSpeed, V);
		GLoco.SpeedSum += V; ++GLoco.Samples;
		if (States != GLoco.LastStates)
		{
			ARENA_LOG(LogArena, Display, TEXT("LOCO_STATE hero=%s phase=%s t=%.2f v=%.0f acc=%.0f anim=%s%s"), *Id, P.Name, InPhase, V, H->GetCharacterMovement()->GetCurrentAcceleration().Size2D(), *Inst->GetClass()->GetName(), *Names);
			GLoco.StateChanges += GLoco.LastStates.Num() > 0 ? 1 : 0;
			GLoco.LastStates = States;
		}
		if (!GLoco.bVars && InPhase > P.Seconds * 0.5f)
		{
			GLoco.bVars = true;
			ARENA_LOG(LogArena, Display, TEXT("LOCO_VARS hero=%s phase=%s v=%.0f%s"), *Id, P.Name, V, *LocoVars(Inst));
		}
	}
	if (P.bShot && !GLoco.bShotDone && InPhase > (P.ShotAt >= 0.f ? P.ShotAt : FMath::Min(0.35f, P.Seconds * 0.3f)))
	{
		GLoco.bShotDone = true;
		UIShot(*FString::Printf(TEXT("LOCO_%s_%s"), *Id, P.Name));
	}
	if (InPhase < P.Seconds) { return; }
	ARENA_LOG(LogArena, Display, TEXT("LOCO_PHASE hero=%s phase=%s avg_v=%.0f peak_v=%.0f state_changes=%d"), *Id, P.Name, GLoco.SpeedSum / FMath::Max(1, GLoco.Samples), GLoco.PeakSpeed, GLoco.StateChanges);
	GLoco.PeakSpeed = GLoco.SpeedSum = 0.f; GLoco.Samples = 0; GLoco.StateChanges = 0;
	++GLoco.Phase;
	GLoco.PhaseAt = Now;
	GLoco.bVars = false; GLoco.bShotDone = false;
	if (GLoco.Phase < GLocoNumPhases && FCString::Strcmp(GLocoPhases[GLoco.Phase].Name, TEXT("path")) == 0)
	{
		// a bot's own move (no enemy near: it walks up the lane like in a match)
		if (AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>())
		{
			AI->Difficulty = 2;
			AI->Possess(H);
			H->UseSmoothTurning(true);
		}
	}
	if (GLoco.Phase >= GLocoNumPhases)
	{
		if (AController* C = H->GetController()) { C->UnPossess(); C->Destroy(); }
		H->Destroy();
		GLoco.H = nullptr;
		++GLoco.Hero;
	}
}
