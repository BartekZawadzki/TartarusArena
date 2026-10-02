// Hit confirmation sound for the player: a short synthesized tick (the Paragon packs ship voice lines only, no
// impact sounds). Presentation only. Kind: 0 hit, 1 critical hit, 2 kill, 3 "not ready" (a low double buzz),
// 4 the ultimate is ready (a soft two-tone chime).
#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "Kismet/GameplayStatics.h"

namespace ArenaHitSound
{
	inline void Play(const UObject* WorldContext, int32 Kind, float Volume = 0.55f)
	{
		if (!WorldContext || !WorldContext->GetWorld()) { return; }
		const int32 Rate = 44100;
		const float Len = Kind == 2 ? 0.16f : (Kind == 3 ? 0.14f : (Kind == 4 ? 0.32f : 0.06f));
		const int32 N = FMath::CeilToInt(Rate * Len);
		TArray<int16> Pcm;
		Pcm.SetNumUninitialized(N);
		const float Pitch = Kind == 1 ? 2100.f : 1500.f;
		for (int32 i = 0; i < N; ++i)
		{
			const float T = (float)i / Rate;
			// a bright click: sine with a fast decay plus a very short noise transient; a kill adds a lower second tick
			float S = FMath::Sin(2.f * PI * Pitch * T) * FMath::Exp(-T * 70.f);
			if (Kind == 3)
			{
				// two short soft-square pulses at 230 Hz: "no"
				const bool bOn = T < 0.05f || (T > 0.08f && T < 0.13f);
				S = bOn ? 0.45f * FMath::Clamp(3.f * FMath::Sin(2.f * PI * 230.f * T), -1.f, 1.f) : 0.f;
			}
			else if (Kind == 4)
			{
				// E6 then A6, each with a gentle decay
				S = 0.5f * FMath::Sin(2.f * PI * 1318.5f * T) * FMath::Exp(-T * 14.f);
				if (T > 0.09f) { const float U = T - 0.09f; S += 0.5f * FMath::Sin(2.f * PI * 1760.f * U) * FMath::Exp(-U * 11.f); }
			}
			if (T < 0.006f && Kind <= 2) { S += FMath::FRandRange(-0.6f, 0.6f) * (1.f - T / 0.006f); }
			if (Kind == 2 && T > 0.07f) { const float U = T - 0.07f; S += 0.9f * FMath::Sin(2.f * PI * 900.f * U) * FMath::Exp(-U * 45.f); }
			Pcm[i] = (int16)FMath::Clamp(S * 0.8f * 32767.f, -32767.f, 32767.f);
		}
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
		Wave->SetSampleRate(Rate);
		Wave->NumChannels = 1;
		Wave->Duration = Len;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = false;
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
		UGameplayStatics::PlaySound2D(WorldContext, Wave, Volume);
	}
}
