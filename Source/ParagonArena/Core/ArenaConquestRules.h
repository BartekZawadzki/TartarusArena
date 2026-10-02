// Pure rules of Conquest, the lane-and-structures mode (v14, 01-game-design.md §3b). No UObjects: the Arena.Conquest
// spec covers them; the game mode (the authority) applies them to the world.
#pragma once

#include "CoreMinimal.h"

namespace ArenaConquest
{
	enum EKind : int32 { None = 0, Tower = 1, Inhibitor = 2, Core = 3 };

	/** One structure as the rules see it. Tier: towers 1 outer, 2 inner; inhibitor 3; core 4. */
	struct FStructureState { int32 Kind = None; int32 Team = 0; int32 Lane = -1; int32 Tier = 0; bool bAlive = true; };

	/** LoL's protection chain: on a lane the outermost tower (tier 1) is open; each next structure of the lane (an
	 *  inner tower, then the inhibitor) opens once every one before it fell; the core once any inhibitor of its team
	 *  is down. Works for one or two towers per lane (v14b: one, the map is small). */
	inline bool IsVulnerable(const FStructureState& S, const TArray<FStructureState>& All)
	{
		if (!S.bAlive) { return false; }
		switch (S.Kind)
		{
		case Tower:
		case Inhibitor:
			for (const FStructureState& O : All)
			{
				if (O.Team == S.Team && O.Lane == S.Lane && O.Kind == Tower && O.Tier < S.Tier && O.bAlive) { return false; }
			}
			return true;
		case Core:
			for (const FStructureState& O : All) { if (O.Team == S.Team && O.Kind == Inhibitor && !O.bAlive) { return true; } }
			return false;
		default: return true;
		}
	}

	/** A tower's candidate target: an enemy in its range. */
	struct FTowerCandidate { int32 Id = 0; float Dist = 0.f; bool bHero = false; bool bHitAllyHero = false; };

	/** LoL tower targeting: an enemy hero that just hurt an allied hero in range is taken at once ("call for help");
	 *  else the current target is kept while it stays in range; else the nearest minion; else the nearest hero.
	 *  Returns the id, 0 for none. */
	inline int32 PickTowerTarget(int32 Current, const TArray<FTowerCandidate>& InRange)
	{
		const FTowerCandidate* Help = nullptr;
		for (const FTowerCandidate& C : InRange) { if (C.bHero && C.bHitAllyHero && (!Help || C.Dist < Help->Dist)) { Help = &C; } }
		if (Help) { return Help->Id; }
		for (const FTowerCandidate& C : InRange) { if (C.Id == Current) { return Current; } }
		const FTowerCandidate* Best = nullptr;
		for (const FTowerCandidate& C : InRange)
		{
			if (!Best || (!C.bHero && Best->bHero) || (C.bHero == Best->bHero && C.Dist < Best->Dist)) { Best = &C; }
		}
		return Best ? Best->Id : 0;
	}

	/** The base damage of a tower shot: a hero takes Raw growing with each consecutive shot on it (RampPer each, at
	 *  most RampMax more); a minion loses a fixed share of its maximum health (melee, ranged, siege, super). */
	inline float TowerShot(float Raw, bool bVictimHero, int32 Ramp, float RampPer, float RampMax, int32 MinionKind, const TArray<float>& MinionPct, float VictimMaxHp)
	{
		if (bVictimHero) { return Raw * (1.f + FMath::Min(FMath::Max(0, Ramp) * RampPer, RampMax)); }
		const float Pct = MinionPct.IsValidIndex(MinionKind) ? MinionPct[MinionKind] : 0.f;
		return Pct > 0.f ? VictimMaxHp * Pct : Raw;
	}

	/** Damage a hero deals to a structure: basic attacks only, from within the structure's reach, and cut when none of
	 *  the hero's minions are near it (backdoor protection). 0 = the blow does not count. */
	inline float HeroOnStructure(bool bBasic, bool bInReach, bool bOwnMinionsNear, float BackdoorCut)
	{
		if (!bBasic || !bInReach) { return 0.f; }
		return bOwnMinionsNear ? 1.f : FMath::Clamp(1.f - BackdoorCut, 0.f, 1.f);
	}

	/** Point of a lane polyline Along cm from its start (the last point past its end). */
	inline FVector AlongPath(const TArray<FVector>& Path, float Along)
	{
		if (Path.Num() == 0) { return FVector::ZeroVector; }
		float Left = FMath::Max(0.f, Along);
		for (int32 i = 0; i + 1 < Path.Num(); ++i)
		{
			const float Seg = FVector::Dist2D(Path[i], Path[i + 1]);
			if (Left <= Seg && Seg > 0.f) { return FMath::Lerp(Path[i], Path[i + 1], Left / Seg); }
			Left -= Seg;
		}
		return Path.Last();
	}

	/** Monster level: grows with the match (a camp at 12 min is a real fight). */
	inline int32 MonsterLevel(float MatchSeconds, float PerMinute) { return FMath::Clamp(1 + FMath::FloorToInt(MatchSeconds / 60.f * PerMinute), 1, 18); }

	/** The winner when the clock runs out: fewer own structures lost wins, then the score; -1 a draw. */
	inline int32 TimeoutWinner(int32 LeftA, int32 LeftB, int32 ScoreA, int32 ScoreB)
	{
		if (LeftA != LeftB) { return LeftA > LeftB ? 0 : 1; }
		if (ScoreA != ScoreB) { return ScoreA > ScoreB ? 0 : 1; }
		return -1;
	}
}
