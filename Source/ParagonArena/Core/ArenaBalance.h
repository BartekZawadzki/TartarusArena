// Time-to-kill simulation (VR-07): hero A's rotation against hero B at a level, from heroes.json alone, with the same
// damage formula, ranks and cooldowns the game uses. Pure (no UObjects): used by the balance spec and by tuning.
#pragma once

#include "CoreMinimal.h"
#include "Core/ArenaCore.h"
#include "Data/ArenaTypes.h"

namespace ArenaBalance
{
	struct FSimAbility
	{
		int32 Slot = 0;
		float Damage = 0.f;       // per hit, before armor (ranked)
		float PowerScale = 0.f;
		float Hits = 1.f;         // a fan of shots: not every shot lands
		float Cooldown = 1.f;
		float Delay = 0.25f;      // cast to damage
		float Lock = 0.3f;        // the caster is busy for this long (no other cast, no basic)
		float AttackSpeedPct = 0.f, BuffSeconds = 0.f;   // a self buff that speeds up the basic attack
	};

	struct FSimHero
	{
		FName Id;
		int32 Level = 1;
		float MaxHealth = 1.f, Armor = 0.f, Power = 0.f, SelfShield = 0.f;
		bool bRanged = false;             // the basic attack reaches 5 m or more
		FSimAbility Basic;
		TArray<FSimAbility> Abilities;   // learnt abilities 1-4 at their ranks
	};

	/** The ranks a bot would have at this level (AArenaCharacter::AutoRank: the ultimate whenever it can rank, the
	 *  rest in the hero's SkillOrder). */
	inline void RanksAt(const FArenaHeroDef& D, int32 Level, int32 OutRanks[5])
	{
		OutRanks[0] = 1; OutRanks[1] = OutRanks[2] = OutRanks[3] = OutRanks[4] = 0;
		for (int32 L = 1; L <= Level; ++L)
		{
			if (ArenaCore::CanRankUp(4, L, OutRanks)) { ++OutRanks[4]; continue; }
			bool bDone = false;
			int32 Seen[5] = { 0, 0, 0, 0, 0 };
			for (int32 S : D.SkillOrder)
			{
				if (S < 1 || S > 3) { continue; }
				++Seen[S];
				if (OutRanks[S] < Seen[S] && ArenaCore::CanRankUp(S, L, OutRanks)) { ++OutRanks[S]; bDone = true; break; }
			}
			for (int32 S = 1; S <= 3 && !bDone; ++S) { if (ArenaCore::CanRankUp(S, L, OutRanks)) { ++OutRanks[S]; bDone = true; } }
		}
	}

	inline FSimHero Build(const FArenaHeroDef& D, int32 Level)
	{
		FSimHero H;
		H.Id = D.Id;
		H.Level = Level;
		const float L1 = static_cast<float>(Level - 1);
		H.MaxHealth = D.MaxHealth + D.HealthPerLevel * L1;
		H.Armor = D.Armor + D.ArmorPerLevel * L1;
		H.Power = D.Power + D.PowerPerLevel * L1;
		H.bRanged = D.Abilities.IsValidIndex(0) && D.Abilities[0].Range >= 5.f;
		int32 Ranks[5];
		RanksAt(D, Level, Ranks);
		for (int32 Slot = 0; Slot < D.Abilities.Num() && Slot < 5; ++Slot)
		{
			const FArenaAbilityDef& A = D.Abilities[Slot];
			if (Slot > 0 && Ranks[Slot] <= 0) { continue; }
			const int32 R = FMath::Max(1, Ranks[Slot]);
			FSimAbility S;
			S.Slot = Slot;
			S.Damage = Slot == 0 ? A.Damage + D.BasicPerLevel * L1 : ArenaCore::Ranked(A.Damage, A.DamagePerRank, R) + ArenaCore::Ranked(A.EndDamage, A.EndDamagePerRank, R);
			S.PowerScale = A.PowerScale;
			S.Hits = A.Archetype == EArenaArchetype::Projectile && A.Count > 1 ? 1.f + (A.Count - 1) * 0.5f : 1.f;
			S.Cooldown = Slot == 0 ? FMath::Max(0.2f, A.Cooldown) : FMath::Max(0.5f, ArenaCore::Ranked(A.Cooldown, A.CooldownPerRank, R));
			S.Delay = A.Archetype == EArenaArchetype::GroundAoE ? FMath::Max(0.05f, A.Delay) : FMath::Max(0.01f, A.Delay);
			S.Lock = Slot == 0 ? FMath::Max(0.1f, A.Delay) : FMath::Clamp(A.Delay + 0.15f, 0.3f, 1.2f);
			if (A.Archetype == EArenaArchetype::Buff)
			{
				S.AttackSpeedPct = A.AttackSpeedBuffPct;
				S.BuffSeconds = ArenaCore::Ranked(A.BuffSeconds, A.BuffPerRank, R);
				const float Shield = ArenaCore::Ranked(A.Shield, A.ShieldPerRank, R);
				if (Shield > 0.f) { H.SelfShield += Shield + A.PowerScale * H.Power; }
			}
			if (Slot == 0) { H.Basic = S; } else { H.Abilities.Add(S); }
		}
		// the ultimate first when it is learnt, then 1-3 (a bot's order)
		H.Abilities.Sort([](const FSimAbility& X, const FSimAbility& Y) { return (X.Slot == 4 ? 0 : X.Slot) < (Y.Slot == 4 ? 0 : Y.Slot); });
		return H;
	}

	/** The gap a melee hero must close on a ranged one who backs away while shooting (v17: the stand-and-fight time to
	 *  kill never saw the ranged heroes' edge). The ranged hero walks at its speed less the fire slow for the share of
	 *  time it is on (slow seconds / basic cooldown); the melee hero runs at full speed and leaps with its gap closer
	 *  (a dash of 15 s cooldown or less). Out: closing speed (m/s) and the seconds the ranged hero shoots unanswered. */
	struct FKite { float ClosingSpeed = 0.f; float FreeSeconds = 0.f; float DashMetres = 0.f; };
	inline FKite Kite(const FArenaHeroDef& Ranged, const FArenaHeroDef& Melee, float FireSlow, float FireSlowSeconds)
	{
		FKite K;
		if (!Ranged.Abilities.IsValidIndex(0) || !Melee.Abilities.IsValidIndex(0)) { return K; }
		const FArenaAbilityDef& RB = Ranged.Abilities[0];
		const float Uptime = FMath::Clamp(FireSlowSeconds / FMath::Max(0.2f, RB.Cooldown), 0.f, 1.f);
		const float KiteSpeed = Ranged.MoveSpeed * (1.f - FireSlow * Uptime);
		K.ClosingSpeed = Melee.MoveSpeed - KiteSpeed;
		for (const FArenaAbilityDef& A : Melee.Abilities)
		{
			if (A.Archetype == EArenaArchetype::Dash && A.Cooldown <= 15.f) { K.DashMetres = FMath::Max(K.DashMetres, A.Distance); }
		}
		const float Gap = RB.Range - Melee.Abilities[0].Range - K.DashMetres;
		K.FreeSeconds = Gap <= 0.f ? 0.f : (K.ClosingSpeed <= 0.05f ? 999.f : Gap / K.ClosingSpeed);
		return K;
	}

	/** Seconds for A to take B's health and self shield (30 = not within 30 s). RangedResist: the v21 melee trait (a
	 *  ranged attacker's damage on a melee target is cut by this share). */
	inline float TimeToKill(const FSimHero& A, const FSimHero& B, float RangedResist = 0.f)
	{
		const float Ehp = (B.MaxHealth + B.SelfShield) / ((A.bRanged && !B.bRanged) ? FMath::Max(0.2f, 1.f - RangedResist) : 1.f);
		auto Hit = [&](const FSimAbility& S)
		{
			ArenaCore::FDamageInput In;
			In.Base = S.Damage;
			In.PowerScaling = S.PowerScale;
			In.AttackerPower = A.Power;
			In.TargetArmor = B.Armor;
			return ArenaCore::ComputeDamage(In) * S.Hits;
		};
		TArray<TPair<float, float>> Pending;   // time, damage
		TArray<float> Ready;
		Ready.Init(0.f, A.Abilities.Num());
		float BasicReady = 0.f, BusyUntil = 0.f, SpeedUntil = -1.f, SpeedPct = 0.f, Done = 0.f;
		constexpr float Dt = 0.02f;
		for (float T = 0.f; T < 30.f; T += Dt)
		{
			if (T >= BusyUntil)
			{
				bool bCast = false;
				for (int32 i = 0; i < A.Abilities.Num() && !bCast; ++i)
				{
					const FSimAbility& S = A.Abilities[i];
					if (T < Ready[i]) { continue; }
					Ready[i] = T + S.Cooldown;
					BusyUntil = T + S.Lock;
					if (S.AttackSpeedPct > 0.f) { SpeedPct = S.AttackSpeedPct; SpeedUntil = T + S.BuffSeconds; }
					if (S.Damage > 0.f) { Pending.Add(TPair<float, float>(T + S.Delay, Hit(S))); }
					bCast = true;
				}
				if (!bCast && T >= BasicReady && A.Basic.Damage > 0.f)
				{
					const float Cd = A.Basic.Cooldown * (T < SpeedUntil ? FMath::Max(0.2f, 1.f - SpeedPct) : 1.f);
					BasicReady = T + Cd;
					BusyUntil = T + FMath::Min(A.Basic.Lock, Cd);
					Pending.Add(TPair<float, float>(T + A.Basic.Delay, Hit(A.Basic)));
				}
			}
			for (int32 i = Pending.Num() - 1; i >= 0; --i)
			{
				if (Pending[i].Key > T) { continue; }
				Done += Pending[i].Value;
				Pending.RemoveAtSwap(i);
			}
			if (Done >= Ehp) { return T; }
		}
		return 30.f;
	}
}
