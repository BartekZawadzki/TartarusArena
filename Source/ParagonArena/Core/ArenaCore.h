// Pure game rules for Tartarus Arena (01-game-design.md §3, §7). No UObjects, no world access:
// everything here is replayable and covered by the Arena.Core automation specs.
#pragma once

#include "CoreMinimal.h"

namespace ArenaCore
{
	// ---- VR-01: damage and mitigation --------------------------------------------------------
	constexpr float ArmorK = 100.f; // mitigation = K / (K + effective armor), same formula as ttk_sim.py

	struct FDamageInput
	{
		float Base = 0.f;          // ability/basic base damage
		float PowerScaling = 0.f;  // fraction of attacker power added
		float AttackerPower = 0.f;
		float TargetArmor = 0.f;
		float ArmorPenFlat = 0.f;
		bool bCrit = false;
		float CritMultiplier = 1.5f;
		int32 AttackerLevel = 1;
	};

	/** Level scaling from 01 §3: +5 % per level above 1, levels clamped to [1, 10]. */
	inline float LevelScale(int32 Level) { return 1.f + 0.05f * static_cast<float>(FMath::Clamp(Level, 1, 10) - 1); }

	/** Final damage >= 0 (VR-01). Negative inputs are treated as 0, never as healing. */
	inline float ComputeDamage(const FDamageInput& In)
	{
		const float Raw = FMath::Max(0.f, In.Base + In.PowerScaling * FMath::Max(0.f, In.AttackerPower)) * LevelScale(In.AttackerLevel);
		const float Crit = In.bCrit ? FMath::Max(1.f, In.CritMultiplier) : 1.f;
		const float EffArmor = FMath::Max(0.f, In.TargetArmor - FMath::Max(0.f, In.ArmorPenFlat));
		return FMath::Max(0.f, Raw * Crit * (ArmorK / (ArmorK + EffArmor)));
	}

	/** Shield absorbs first, then health (GS-11). Returns the new {Shield, Health}; both clamped (VR-01). */
	inline TPair<float, float> ApplyDamage(float Shield, float Health, float MaxHealth, float Damage)
	{
		const float D = FMath::Max(0.f, Damage);
		const float Absorbed = FMath::Min(FMath::Max(0.f, Shield), D);
		return { FMath::Max(0.f, Shield) - Absorbed, FMath::Clamp(Health - (D - Absorbed), 0.f, MaxHealth) };
	}

	// ---- VR-04: friendly fire ----------------------------------------------------------------
	constexpr int32 NoTeam = -1;
	inline bool IsHostile(int32 TeamA, int32 TeamB) { return TeamA != NoTeam && TeamB != NoTeam && TeamA != TeamB; }

	// ---- VR-03: one hit per target per activation ---------------------------------------------
	struct FHitRegistry
	{
		TSet<uint64> Hit;
		/** true the first time a target id is seen in this activation, false afterwards. */
		bool TryRegister(uint64 TargetId) { bool bAlready = false; Hit.Add(TargetId, &bAlready); return !bAlready; }
		void Reset() { Hit.Reset(); }
	};

	// ---- VR-05: team score and match clock (GameMode is the authority) ------------------------
	enum class EScoreEvent : uint8 { HeroKill, MinionKill, MinionReachedBase };

	struct FScoreRules
	{
		int32 HeroKill = 5;
		int32 MinionKill = 1;
		int32 MinionBase = 2;
		int32 Limit = 300;          // first team to reach it wins before the clock runs out (<= 0: no limit)
	};

	/** Points a match length is played to: PerMinute points for every minute on the clock. */
	inline int32 ScoreLimitFor(int32 Minutes, int32 PerMinute) { return FMath::Max(1, Minutes) * FMath::Max(1, PerMinute); }

	struct FScoreLedger
	{
		FScoreRules Rules;
		int32 Points[2] = { 0, 0 };

		explicit FScoreLedger(const FScoreRules& InRules = FScoreRules()) : Rules(InRules) {}

		/** Awards the points of an event to ScoringTeam. Scores only grow, stop at the limit and freeze once it is reached. */
		int32 Award(EScoreEvent Event, int32 ScoringTeam)
		{
			if (ScoringTeam < 0 || ScoringTeam > 1) { return 0; }
			if (LimitReached()) { return Points[ScoringTeam]; }
			const int32 Gain = Event == EScoreEvent::HeroKill ? Rules.HeroKill : (Event == EScoreEvent::MinionKill ? Rules.MinionKill : Rules.MinionBase);
			Points[ScoringTeam] += FMath::Max(0, Gain);
			if (Rules.Limit > 0) { Points[ScoringTeam] = FMath::Min(Points[ScoringTeam], Rules.Limit); }
			return Points[ScoringTeam];
		}
		bool LimitReached() const { return Rules.Limit > 0 && (Points[0] >= Rules.Limit || Points[1] >= Rules.Limit); }
		/** The team ahead, NoTeam on a tie. */
		int32 Leader() const { return Points[0] > Points[1] ? 0 : (Points[1] > Points[0] ? 1 : NoTeam); }
		/** Winner: the team at the limit; when the clock is up the leader; NoTeam = play on (tie: overtime, next point wins). */
		int32 Winner(bool bTimeUp) const
		{
			if (LimitReached()) { return Points[0] >= Rules.Limit ? 0 : 1; }
			return bTimeUp ? Leader() : NoTeam;
		}
	};

	// ---- VR-20: levels and ability ranks (MOBA) --------------------------------------------------------
	constexpr int32 MaxRank = 5;

	/** XP from Level to Level + 1. */
	inline float XpToNext(int32 Level, float Base, float Growth) { return FMath::Max(1.f, Base + Growth * static_cast<float>(FMath::Max(1, Level) - 1)); }
	/** Total XP to reach Level from level 1. */
	inline float XpForLevel(int32 Level, float Base, float Growth)
	{
		float Sum = 0.f;
		for (int32 L = 1; L < Level; ++L) { Sum += XpToNext(L, Base, Growth); }
		return Sum;
	}
	inline int32 LevelForXp(float Xp, float Base, float Growth, int32 MaxLevel)
	{
		int32 L = 1;
		while (L < MaxLevel && Xp + 1e-3f >= XpForLevel(L + 1, Base, Growth)) { ++L; }
		return L;
	}
	/** Highest rank an ability may have at a hero level: abilities 1-3 open rank r at level 2r - 1 (1, 3, 5, 7, 9),
	 *  the ultimate (slot 4) at levels 5, 9, 13, 17 and 20. The basic attack (slot 0) has no ranks. */
	inline int32 MaxRankAt(int32 Slot, int32 Level)
	{
		if (Slot == 4) { return (Level >= 5) + (Level >= 9) + (Level >= 13) + (Level >= 17) + (Level >= 20); }
		if (Slot >= 1 && Slot <= 3) { return FMath::Min(MaxRank, (FMath::Max(1, Level) + 1) / 2); }
		return 1;
	}
	/** One skill point per level; Ranks[1..4] are what was spent. */
	inline int32 FreePoints(int32 Level, const int32 Ranks[5]) { return FMath::Max(1, Level) - (Ranks[1] + Ranks[2] + Ranks[3] + Ranks[4]); }
	inline bool CanRankUp(int32 Slot, int32 Level, const int32 Ranks[5])
	{
		return Slot >= 1 && Slot <= 4 && FreePoints(Level, Ranks) > 0 && Ranks[Slot] < MaxRankAt(Slot, Level);
	}
	/** A value at a rank: base at rank 1, + PerRank for every rank above. */
	inline float Ranked(float Base, float PerRank, int32 Rank) { return Base + PerRank * static_cast<float>(FMath::Max(0, Rank - 1)); }

	/** Deterministic critical hits: the chance accumulates hit by hit and every time it passes 1 the hit is critical,
	 *  so 25 % is exactly every 4th basic attack (no lucky or unlucky streaks). */
	inline bool NextCrit(float& Accum, float Chance)
	{
		if (Chance <= 0.f) { return false; }
		Accum += FMath::Min(1.f, Chance);
		if (Accum >= 1.f - 1e-4f) { Accum -= 1.f; return true; }
		return false;
	}

	// ---- VR-21: displacements are fixed (MOBA): the data's launch speeds become a distance, a height and a time
	constexpr float KnockCmPerSpeed = 45.f;                      // knockback 4 m/s -> 1.8 m
	inline float KnockDistanceCm(float Knockback) { return FMath::Max(0.f, Knockback) * KnockCmPerSpeed; }
	inline float KnockHeightCm(float KnockUp) { const float V = FMath::Max(0.f, KnockUp); return V * V / (2.f * 9.81f) * 100.f; }
	inline float KnockAirSeconds(float KnockUp) { return KnockUp > 0.f ? 2.f * KnockUp / 9.81f : 0.3f; }

	// ---- VR-23: shutdown gold (a streaking hero carries a bounty) ------------------------------------------
	/** Gold on top of the kill for ending a streak of Streak kills since the victim's last death (3+). */
	inline int32 ShutdownGold(int32 Streak, int32 PerKill, int32 Max) { return Streak >= 3 ? FMath::Min(FMath::Max(0, Max), FMath::Max(0, PerKill) * (Streak - 2)) : 0; }

	// ---- VR-12: gold, items, revive ------------------------------------------------------------------
	constexpr int32 InventorySlots = 6;

	struct FItemStats
	{
		float Power = 0.f, Armor = 0.f, Health = 0.f, Mana = 0.f, HealthRegen = 0.f, ManaRegen = 0.f;
		float MoveSpeedPct = 0.f, CooldownPct = 0.f, AttackSpeedPct = 0.f, LifestealPct = 0.f;
		float CritChance = 0.f, ArmorPen = 0.f;

		FItemStats& operator+=(const FItemStats& O)
		{
			Power += O.Power; Armor += O.Armor; Health += O.Health; Mana += O.Mana; HealthRegen += O.HealthRegen; ManaRegen += O.ManaRegen;
			MoveSpeedPct += O.MoveSpeedPct; CooldownPct += O.CooldownPct; AttackSpeedPct += O.AttackSpeedPct; LifestealPct += O.LifestealPct;
			CritChance += O.CritChance; ArmorPen += O.ArmorPen;
			return *this;
		}
		/** The same stats with the percentage caps applied (no immortal or cooldown-free builds). */
		FItemStats Capped() const
		{
			FItemStats C = *this;
			C.CooldownPct = FMath::Clamp(C.CooldownPct, 0.f, 0.4f);
			C.LifestealPct = FMath::Clamp(C.LifestealPct, 0.f, 0.3f);
			C.MoveSpeedPct = FMath::Clamp(C.MoveSpeedPct, 0.f, 0.4f);
			C.AttackSpeedPct = FMath::Clamp(C.AttackSpeedPct, 0.f, 0.6f);
			C.CritChance = FMath::Clamp(C.CritChance, 0.f, 1.f);
			return C;
		}
	};

	/** Unique item passives (two copies of an item do not stack its passive). */
	struct FItemPassives
	{
		float ExecuteBonus = 0.f, ExecuteBelow = 0.f;          // Executioner's Blade
		float CritMult = 1.75f;                                 // Blade of Infinity: 2.25
		float OverhealShieldMax = 0.f;                          // Krwiopijca
		float OnHit = 0.f;                                      // Falconer's Bow
		float SpellbladePower = 0.f, SpellbladeWindow = 0.f, SpellbladeCd = 0.f;   // Sorcerer's Blade
		float AbilityDamagePct = 0.f;                           // Korona archonta
		float EchoSeconds = 0.f;                                // Sage's Hourglass
		float ThornsPct = 0.f;                                  // Pancerz tytana
		float LastStandShieldPct = 0.f, LastStandBelow = 0.f, LastStandCd = 0.f;   // Guardian's Aegis
		float RegenPct = 0.f, RegenAfter = 0.f;                 // Serce olbrzyma
	};

	/** A recipe in item-index form (a pure mirror of FArenaItemDef.Cost / From). */
	struct FRecipe { int32 Cost = 0; TArray<int32> From; };

	/** The full price of an item: its recipe gold plus the full price of every component. */
	inline int32 TotalCost(int32 Item, const TArray<FRecipe>& Db, int32 Depth = 0)
	{
		if (!Db.IsValidIndex(Item) || Depth > 8) { return 0; }
		int32 P = FMath::Max(0, Db[Item].Cost);
		for (int32 C : Db[Item].From) { P += TotalCost(C, Db, Depth + 1); }
		return P;
	}

	/** What buying Item costs with this inventory (MOBA recipes): every component already owned, or the components of
	 *  a component, is used up and its value comes off the price. Consumed receives the inventory positions used. */
	inline int32 BuyPrice(int32 Item, const TArray<FRecipe>& Db, const TArray<int32>& Inventory, TArray<int32>& Consumed)
	{
		TArray<bool> Used;
		Used.Init(false, Inventory.Num());
		TFunction<int32(int32, bool, int32)> Price = [&](int32 I, bool bTop, int32 Depth) -> int32
		{
			if (!Db.IsValidIndex(I) || Depth > 8) { return 0; }
			if (!bTop)
			{
				for (int32 k = 0; k < Inventory.Num(); ++k) { if (!Used[k] && Inventory[k] == I) { Used[k] = true; return 0; } }
			}
			int32 P = FMath::Max(0, Db[I].Cost);
			for (int32 C : Db[I].From) { P += Price(C, false, Depth + 1); }
			return P;
		};
		const int32 P = Price(Item, true, 0);
		Consumed.Reset();
		for (int32 k = 0; k < Used.Num(); ++k) { if (Used[k]) { Consumed.Add(k); } }
		return P;
	}
	/** Selling gives back 60 % of the full price. */
	inline int32 SellValue(int32 Item, const TArray<FRecipe>& Db) { return FMath::FloorToInt(TotalCost(Item, Db) * 0.6f); }

	enum class EBuyResult : uint8 { Ok, NotInShop, InventoryFull, NotEnoughGold };
	/** ItemsConsumed: components the purchase uses up (they free their slots). */
	inline EBuyResult CanBuy(int32 Gold, int32 Cost, int32 ItemsOwned, bool bInShop, int32 ItemsConsumed = 0)
	{
		if (!bInShop) { return EBuyResult::NotInShop; }
		if (ItemsOwned - ItemsConsumed >= InventorySlots) { return EBuyResult::InventoryFull; }
		return Gold >= FMath::Max(0, Cost) ? EBuyResult::Ok : EBuyResult::NotEnoughGold;
	}

	/** Instant revive at the base: costs more the higher the hero, and has its own cooldown. */
	inline int32 ReviveCost(int32 Level, int32 Base, int32 PerLevel) { return FMath::Max(0, Base) + FMath::Max(0, PerLevel) * FMath::Clamp(Level, 1, 20); }
	inline bool CanRevive(bool bDead, float Now, float ReadyAt, int32 Gold, int32 Cost) { return bDead && Now >= ReadyAt && Gold >= Cost; }
	inline float ReducedCooldown(float Cooldown, float CooldownPct) { return FMath::Max(0.1f, Cooldown * (1.f - FMath::Clamp(CooldownPct, 0.f, 0.4f))); }

	// ---- VR-06: respawn --------------------------------------------------------------------------
	inline float RespawnSeconds(int32 Level) { return 5.f + 0.75f * static_cast<float>(FMath::Clamp(Level, 1, 20)); }

	// ---- VR-02: cooldown + mana gate (pure mirror of the GAS check, used by bots and tests) ------
	inline bool CanCast(float CooldownRemaining, float Mana, float Cost) { return CooldownRemaining <= 0.f && Mana >= Cost && Cost >= 0.f; }
}
