// Presentation helpers: spawn a Niagara (or legacy Cascade) system by soft path. Presentation only —
// never game state. Paragon packs ship Cascade particles; the engine placeholders ship Niagara.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

namespace ArenaFx
{
	inline void Sound(const UObject* WorldContext, const FString& Path, const FVector& Location, float Volume = 1.f)
	{
		if (!WorldContext || Path.IsEmpty()) { return; }
		if (USoundBase* S = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)))
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, S, Location, Volume);
		}
	}

	/** Attaches a (looping) system to a component; it dies with the owner. Returns false if the path did not load. */
	inline bool Attach(const FString& Path, USceneComponent* Parent, const FLinearColor& Color)
	{
		if (!Parent || Path.IsEmpty()) { return false; }
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UNiagaraSystem* NS = Cast<UNiagaraSystem>(Obj))
		{
			if (UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAttached(NS, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true)) { C->SetVariableLinearColor(TEXT("Color"), Color); return true; }
		}
		else if (UParticleSystem* PS = Cast<UParticleSystem>(Obj))
		{
			return UGameplayStatics::SpawnEmitterAttached(PS, Parent) != nullptr;
		}
		return false;
	}

	/** Attaches a system to a component for Seconds (a buff's aura follows its bearer), then lets it fade out. Returns
	 *  false if the path did not load. */
	inline bool AttachFor(const FString& Path, USceneComponent* Parent, const FLinearColor& Color, float Seconds)
	{
		if (!Parent || Path.IsEmpty() || !Parent->GetWorld()) { return false; }
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UFXSystemComponent* Fx = nullptr;
		if (UNiagaraSystem* NS = Cast<UNiagaraSystem>(Obj))
		{
			UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAttached(NS, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
			if (C) { C->SetVariableLinearColor(TEXT("Color"), Color); }
			Fx = C;
		}
		else if (UParticleSystem* PS = Cast<UParticleSystem>(Obj))
		{
			Fx = UGameplayStatics::SpawnEmitterAttached(PS, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
		}
		if (!Fx) { return false; }
		FTimerHandle H;
		TWeakObjectPtr<UFXSystemComponent> Weak(Fx);
		Parent->GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) { Weak->Deactivate(); } }), FMath::Max(0.2f, Seconds), false);
		return true;
	}

	/** Attached to a socket of a component for Seconds (0 = until stopped), returned so a state that ends early (a
	 *  recall broken, a stun cleansed) can stop it. Null if the path did not load. */
	inline UFXSystemComponent* AttachAt(const FString& Path, USceneComponent* Parent, FName Socket, float Seconds)
	{
		if (!Parent || Path.IsEmpty() || !Parent->GetWorld()) { return nullptr; }
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UFXSystemComponent* Fx = nullptr;
		if (UNiagaraSystem* NS = Cast<UNiagaraSystem>(Obj)) { Fx = UNiagaraFunctionLibrary::SpawnSystemAttached(NS, Parent, Socket, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true); }
		else if (UParticleSystem* PS = Cast<UParticleSystem>(Obj)) { Fx = UGameplayStatics::SpawnEmitterAttached(PS, Parent, Socket, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true); }
		if (Fx && Seconds > 0.f)
		{
			FTimerHandle H;
			TWeakObjectPtr<UFXSystemComponent> Weak(Fx);
			Parent->GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) { Weak->Deactivate(); } }), Seconds, false);
		}
		return Fx;
	}

	/** The size of an impact or blast effect: the ability's own FxScale, else from its radius, never huge. */
	inline float BlastScale(float FxScale, float RadiusM, float PerMetre, float Min, float Max)
	{
		return FxScale > 0.f ? FxScale : FMath::Clamp(RadiusM * PerMetre, Min, Max);
	}

	inline void Spawn(const UObject* WorldContext, const FString& Path, const FVector& Location, const FLinearColor& Color, float Scale = 1.f)
	{
		if (!WorldContext || Path.IsEmpty()) { return; }
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UNiagaraSystem* NS = Cast<UNiagaraSystem>(Obj))
		{
			if (UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, NS, Location, FRotator::ZeroRotator, FVector(Scale)))
			{
				C->SetVariableLinearColor(TEXT("Color"), Color);
			}
		}
		else if (UParticleSystem* PS = Cast<UParticleSystem>(Obj))
		{
			UGameplayStatics::SpawnEmitterAtLocation(WorldContext, PS, Location, FRotator::ZeroRotator, FVector(Scale));
		}
	}
}
