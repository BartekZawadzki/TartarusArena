// Pure bot decision logic (df-npc-ai: Decide(snapshot) -> intent). No UObjects: unit-testable.
#pragma once

#include "CoreMinimal.h"

namespace ArenaBot
{
	// bSafe: at its own fountain; bAggro: a hero that has just hit a hero of the other team (minions switch to it)
	// bStructure: a tower, inhibitor or core (Conquest): a wide body (Radius), hurt by basic attacks only
	// Vel: its velocity (lead aim); bFocus: a weak enemy hero an ally of ours is already on (the team focuses it)
	struct FUnit { int32 Id = 0; FVector Pos = FVector::ZeroVector; float HpPct = 1.f; bool bHero = true; bool bSafe = false; bool bAggro = false; bool bStructure = false; float Radius = 40.f; FVector Vel = FVector::ZeroVector; bool bFocus = false; bool bMelee = false; };

	// SpeedCm: a straight shot's speed (lead aim; 0 other archetypes); AreaCm: the blow's radius (an ultimate goes where it catches most)
	struct FSlot { bool bReady = false; float RangeCm = 300.f; bool bSupport = false; bool bDash = false; bool bUltimate = false; bool bOffCooldown = false; float ManaCost = 0.f; float SpeedCm = 0.f; float AreaCm = 0.f; bool bBackwards = false; bool bCC = false; };   // bCC: stuns or slows hard (v19: kept for heroes)

	// an enemy structure that shoots (Conquest): its reach (centre to the edge a unit is hit from), whom it shoots now
	// bTargetsMe: this bot; bBusy: one of our minions (it tanks the tower: a dive window); bOnAllyHero: an ally hero
	struct FTower { FVector Pos = FVector::ZeroVector; float RangeCm = 0.f; bool bTargetsMe = false; bool bBusy = false; bool bOnAllyHero = false; };

	struct FSnapshot
	{
		FVector Self = FVector::ZeroVector;
		float HpPct = 1.f;
		float ManaPct = 1.f;
		float Mana = 1000.f;
		FVector Base = FVector::ZeroVector;
		FVector EnemyPortal = FVector::ZeroVector;
		TArray<FUnit> Enemies;
		TArray<FUnit> Allies;
		FSlot Slots[5];
		bool bMinion = false;
		int32 Difficulty = 1;      // 0 easy, 1 normal, 2 hard
		float StrafeSign = 1.f;    // flipped by the controller every 1.5-2.5 s (dodging, Smite-style)
		float RetreatHp = 0.25f;
		bool bOrb = false;           // the centre power orb is up
		FVector Orb = FVector::ZeroVector;
		int32 BaseSlot = -1;          // >= 0: this hero's own spot at the fountain (a ring, so five retreating heroes never pile up)
		bool bInEnemyBase = false;     // standing at the enemy fountain (it burns heroes)
		bool bSiege = false;           // Conquest: no enemy hero near: an open structure under our minions is the job
		float Lead = 0.f;              // how much of a target's motion a shot leads (difficulty: 0 / 0.6 / 1)
		TArray<FTower> EnemyTowers;    // v18: Conquest's armed enemy structures
		TArray<FVector> OwnTowers;     // v18: our armed structures (a place to fall back to)
		int32 AlliesAlive = 5, EnemiesAlive = 5;   // v19: heroes alive on each side (this bot counted)
		int32 EnemiesMissing = 0;      // v19: enemy heroes alive but seen by none of our units
		bool bPastMiddle = false;      // v19: this bot stands on the enemy's half of the map
		FVector Fallback = FVector::ZeroVector;   // v19: where to fall back to (our wave's front, else our tower)
	};

	// ---- v18: reading the situation ----------------------------------------------------------------------------
	/** The enemy tower whose reach covers the point (with a margin), or null. */
	inline const FTower* TowerAt(const FSnapshot& S, const FVector& P, float Margin = 0.f)
	{
		for (const FTower& T : S.EnemyTowers) { if (FVector::Dist2D(P, T.Pos) <= T.RangeCm + Margin) { return &T; } }
		return nullptr;
	}
	/** Heroes of a side within R of a point (structures and minions left out). */
	inline int32 HeroesNear(const TArray<FUnit>& Units, const FVector& P, float R)
	{
		int32 N = 0;
		for (const FUnit& U : Units) { N += U.bHero && !U.bStructure && FVector::Dist2D(U.Pos, P) <= R ? 1 : 0; }
		return N;
	}
	/** Enemy heroes near us minus our heroes near us (this bot counted): > 0 means outnumbered. */
	inline int32 Outnumbered(const FSnapshot& S, float R = 1600.f)
	{
		return HeroesNear(S.Enemies, S.Self, R) - (HeroesNear(S.Allies, S.Self, R) + 1);
	}
	/** The health below which the bot goes home: higher when outnumbered or under a tower's fire (hard bots keep a
	 *  wider margin: they die less). */
	inline float RetreatLine(const FSnapshot& S)
	{
		float Line = S.RetreatHp + (S.Difficulty >= 2 ? 0.05f : 0.f);
		const int32 Odds = Outnumbered(S);
		if (Odds >= 1) { Line += 0.1f + 0.05f * FMath::Min(Odds - 1, 2); }
		const FTower* T = TowerAt(S, S.Self, 60.f);
		if (T && T->bTargetsMe) { Line += 0.2f; }
		// (v19) a caster with no mana left fights with its basic attack only; the team two heroes down plays safe
		if (S.ManaPct < 0.12f && S.Slots[1].ManaCost > 0.f) { Line += 0.12f; }
		if (S.AlliesAlive + 2 <= S.EnemiesAlive) { Line += 0.1f; }
		return FMath::Min(Line, 0.6f);
	}
	/** v19: out on the enemy's half with two enemy heroes unseen, or the team two heroes down: back to our wave. */
	inline bool ShouldPullBack(const FSnapshot& S)
	{
		if (S.bMinion || S.Fallback.IsNearlyZero()) { return false; }
		if (FVector::Dist2D(S.Self, S.Fallback) < 900.f) { return false; }
		// (the first measure, two missing, kept the bots on their half: 1-3 towers in 12 min, 119 trips home for mana)
		return S.bPastMiddle && (S.EnemiesMissing >= 3 || S.AlliesAlive + 2 <= S.EnemiesAlive);
	}
	/** v19: a healthy enemy hero running away out of our reach is not worth the chase (it drags the bot off its
	 *  lane, often into the enemy's jungle or towers). */
	inline bool IsFleeingOutOfReach(const FSnapshot& S, const FUnit& E)
	{
		if (!E.bHero || E.bStructure || E.HpPct < 0.3f) { return false; }
		const FVector To = E.Pos - S.Self;
		const float D = To.Size2D();
		const float Reach = S.Slots[0].RangeCm + 250.f;
		bool bGapCloser = false;
		for (int32 i = 1; i < 4; ++i) { bGapCloser |= S.Slots[i].bReady && S.Slots[i].bDash && D <= S.Slots[i].RangeCm * 1.2f; }
		return D > Reach && !bGapCloser && FVector::DotProduct(E.Vel.GetSafeNormal2D(), To.GetSafeNormal2D()) > 0.5f && E.Vel.Size2D() > 250.f;
	}
	/** May the bot fight this enemy where it stands? A hero (or a minion) inside an enemy tower's reach is a dive: only
	 *  while the tower is busy with our minions, the bot is healthy and the hero nearly dead, or we outnumber them there.
	 *  A ranged bot may shoot into the reach from outside it (it never walks in: the controller keeps it out). */
	inline bool CanFightAt(const FSnapshot& S, const FUnit& E, float BasicRangeCm, bool bMelee)
	{
		if (E.bStructure) { return true; }                 // sieges have their own rules (bSiege, the controller)
		const FTower* T = TowerAt(S, E.Pos, 40.f);
		if (!T) { return true; }
		if (!bMelee && !TowerAt(S, S.Self, 60.f) && FVector::Dist2D(S.Self, E.Pos) <= BasicRangeCm) { return true; }
		if (!E.bHero) { return T->bBusy && S.HpPct > 0.4f; }   // minions under a tower: only while it shoots our wave
		const int32 Them = HeroesNear(S.Enemies, E.Pos, 1600.f), Us = HeroesNear(S.Allies, E.Pos, 1600.f) + 1;
		return T->bBusy && !T->bTargetsMe && S.HpPct > 0.6f && (E.HpPct < 0.3f || Us >= Them + 2);
	}

	struct FIntent
	{
		FVector MoveTo = FVector::ZeroVector; int32 TargetId = 0; int32 CastSlot = -1; FVector Aim = FVector::ZeroVector; bool bRetreat = false;
		float StopDistance = 150.f;   // the controller stops when MoveTo is this close (melee closes to body contact)
	};

	/** Melee kits fight at body contact; this is the centre distance a melee bot closes to (two capsules ~80 cm). */
	constexpr float MeleeContactCm = 115.f;
	inline bool IsMeleeKit(const FSnapshot& S) { return S.Slots[0].RangeCm <= 400.f; }

	inline float Score(const FSnapshot& S, const FUnit& E)
	{
		const float Dist = FVector::Dist(S.Self, E.Pos);
		// a structure after the units around it (minions push it once the enemy wave is gone; heroes siege last)
		if (E.bStructure) { return (S.bMinion ? 0.45f : (S.bSiege ? 3.f : 0.6f)) * 1000.f / (Dist + 300.f); }
		if (S.bMinion)
		{
			// minions (LoL / Smite): the nearest enemy minion; a hero only when it attacks one of their heroes
			// ("call for help"), else far behind (they used to prefer heroes x2: the player was always their target)
			const float W = E.bHero ? (E.bAggro ? 4.f : 0.35f) : 1.f;
			return W * 1000.f / (Dist + 300.f);
		}
		// close, weak heroes first; with the ultimate ready a hero is worth much more (a bot commits to use it, MOBA-style)
		const bool bUltReady = S.Slots[4].bReady && S.Slots[4].bUltimate && !S.Slots[4].bSupport && S.HpPct > 0.45f;
		return (E.bHero ? (bUltReady ? 5.f : 2.f) * (E.bFocus ? 1.6f : 1.f) : 1.f) * (1.5f - E.HpPct) * 1000.f / (Dist + 300.f);
	}

	inline FIntent Decide(const FSnapshot& S)
	{
		FIntent Out;
		Out.MoveTo = S.bMinion ? S.EnemyPortal : S.Self;
		// low on health: back to base, unless an enemy hero within reach is lower still and nearly gone (finish it)
		// (v18: not into an enemy tower's reach, not outnumbered, not under a tower's fire — a bot at 10 % chasing the
		// player's last 15 % under the player's tower was the operator's complaint)
		bool bFinish = false;
		const bool bUnderFire = TowerAt(S, S.Self, 60.f) && TowerAt(S, S.Self, 60.f)->bTargetsMe;
		if (!S.bMinion && !bUnderFire && Outnumbered(S) <= 0)
		{
			for (const FUnit& E : S.Enemies)
			{
				bFinish |= E.bHero && !E.bStructure && E.HpPct < 0.3f && E.HpPct <= S.HpPct + 0.1f && FVector::Dist(E.Pos, S.Self) < 900.f && !TowerAt(S, E.Pos, 40.f);
			}
		}
		if (!S.bMinion && S.HpPct < (S.bMinion ? S.RetreatHp : RetreatLine(S)) && !bFinish)
		{
			Out.bRetreat = true;
			Out.MoveTo = S.BaseSlot < 0 ? S.Base : S.Base + FVector(320.f, 0.f, 0.f).RotateAngleAxis(S.BaseSlot * 72.f, FVector::UpVector);
			// support/self buffs while retreating
			for (int32 i = 1; i < 5; ++i) { if (S.Slots[i].bReady && S.Slots[i].bSupport) { Out.CastSlot = i; Out.Aim = S.Self; break; } }
			// (v18) caught from close: the leap takes it away (towards home; a backwards leap is aimed at the chaser)
			if (Out.CastSlot < 0)
			{
				const FUnit* Chaser = nullptr;
				for (const FUnit& E : S.Enemies) { if (E.bHero && !E.bStructure && FVector::Dist2D(E.Pos, S.Self) < 700.f) { Chaser = &E; break; } }
				for (int32 i = 1; Chaser && i < 5; ++i)
				{
					const FSlot& Sl = S.Slots[i];
					if (!Sl.bReady || !Sl.bDash || (Sl.bUltimate && S.HpPct > 0.15f)) { continue; }
					const FVector Home = (Out.MoveTo - S.Self).GetSafeNormal2D();
					Out.CastSlot = i;
					Out.Aim = Sl.bBackwards ? S.Self - Home * 500.f : S.Self + Home * Sl.RangeCm;
					Out.TargetId = 0;
					return Out;
				}
			}
			// (v17) and it hits back at the enemy hero on it while it runs, as MOBA players do: a bot that only ran
			// died without a shot (a faster melee hero catches a runner anyway)
			if (Out.CastSlot < 0)
			{
				const FUnit* Near = nullptr;
				float NearD = TNumericLimits<float>::Max();
				for (const FUnit& E : S.Enemies)
				{
					if (!E.bHero || E.bStructure) { continue; }
					const float D = FVector::Dist(E.Pos, S.Self);
					if (D < NearD) { NearD = D; Near = &E; }
				}
				for (int32 i = 3; Near && i >= 0; --i)
				{
					const FSlot& Sl = S.Slots[i];
					if (!Sl.bReady || Sl.bSupport || Sl.bDash) { continue; }
					const float Reach = (i == 0 && IsMeleeKit(S)) ? FMath::Max(MeleeContactCm + 35.f, Sl.RangeCm + 55.f) : Sl.RangeCm;
					if (NearD > Reach) { continue; }
					Out.CastSlot = i;
					Out.TargetId = Near->Id;
					Out.Aim = Near->Pos + (Sl.SpeedCm > 0.f ? Near->Vel * (NearD / Sl.SpeedCm) * S.Lead : FVector::ZeroVector);
					break;
				}
			}
			return Out;
		}
		// never stand at the enemy fountain: step back out of it
		if (!S.bMinion && S.bInEnemyBase)
		{
			Out.bRetreat = true;
			Out.MoveTo = S.Self + (S.Self - S.EnemyPortal).GetSafeNormal2D() * 900.f;
			return Out;
		}
		const FUnit* Best = nullptr;
		float BestScore = -1.f;
		const float Engage = S.bMinion ? 900.f : 3500.f;
		// v18: a hero that sees two more enemy heroes than friends coming (30 m), or one more near it (16 m) while below
		// 80 %, takes no fight with heroes: it falls back to our nearest tower (or towards home) and shoots what reaches
		// it on the way (the audit: ganked bots died retreating, too late, from ~73 % health)
		const bool bFallBack = !S.bMinion && (Outnumbered(S, 3000.f) >= 2 || (Outnumbered(S) >= 1 && S.HpPct < 0.8f));
		for (const FUnit& E : S.Enemies)
		{
			if (FVector::Dist(S.Self, E.Pos) > Engage) { continue; }
			if (!S.bMinion && E.bSafe) { continue; }        // nobody chases into the enemy fountain
			if (!S.bMinion && !CanFightAt(S, E, S.Slots[0].RangeCm, IsMeleeKit(S))) { continue; }   // no dives
			if (bFallBack && E.bHero && FVector::Dist(S.Self, E.Pos) > S.Slots[0].RangeCm + 100.f) { continue; }
			if (!S.bMinion && IsFleeingOutOfReach(S, E)) { continue; }                           // v19: no chase
			const float Sc = Score(S, E);
			if (Sc > BestScore) { BestScore = Sc; Best = &E; }
		}
		// v19: overextended (missing enemies, a lost team fight): walk back to our wave, fight only what reaches us
		if (!bFallBack && ShouldPullBack(S))
		{
			Out.bRetreat = false;
			Out.MoveTo = S.Fallback;
			Out.StopDistance = 250.f;
			if (Best && FVector::Dist(S.Self, Best->Pos) <= S.Slots[0].RangeCm + (IsMeleeKit(S) ? 55.f : 0.f))
			{
				Out.TargetId = Best->Id;
				Out.Aim = Best->Pos;
				if (S.Slots[0].bReady) { Out.CastSlot = 0; }
			}
			return Out;
		}
		if (bFallBack)
		{
			FVector Safe = S.Base;
			float SafeD = TNumericLimits<float>::Max();
			for (const FVector& T : S.OwnTowers) { const float D = FVector::Dist2D(T, S.Self); if (D < SafeD) { SafeD = D; Safe = T; } }
			Out.bRetreat = SafeD < 900.f ? false : true;   // at our tower: hold there (the tower fights with it)
			Out.MoveTo = SafeD < 900.f ? S.Self : Safe + (S.Base - Safe).GetSafeNormal2D() * 300.f;
			// a ready area ultimate that catches two heroes or more is still the play (then it backs off)
			if (S.Slots[4].bReady && S.Slots[4].bUltimate && !S.Slots[4].bSupport && !S.Slots[4].bDash && S.Slots[4].AreaCm > 0.f)
			{
				for (const FUnit& E : S.Enemies)
				{
					if (!E.bHero || E.bStructure || FVector::Dist(S.Self, E.Pos) > S.Slots[4].RangeCm) { continue; }
					int32 Caught = 1;
					for (const FUnit& O : S.Enemies) { Caught += (&O != &E && O.bHero && !O.bStructure && FVector::Dist2D(O.Pos, E.Pos) <= S.Slots[4].AreaCm) ? 1 : 0; }
					if (Caught >= 2) { Out.CastSlot = 4; Out.Aim = E.Pos; Out.TargetId = E.Id; return Out; }
				}
			}
			if (Best && FVector::Dist(S.Self, Best->Pos) <= S.Slots[0].RangeCm + (IsMeleeKit(S) ? 55.f : 0.f))
			{
				Out.TargetId = Best->Id;
				Out.Aim = Best->Pos;
				for (int32 i = 3; i >= 0; --i)
				{
					const FSlot& Sl = S.Slots[i];
					if (Sl.bReady && !Sl.bSupport && !Sl.bDash && FVector::Dist(S.Self, Best->Pos) <= Sl.RangeCm + (i == 0 && IsMeleeKit(S) ? 55.f : 0.f)) { Out.CastSlot = i; break; }
				}
			}
			return Out;
		}
		// contest the power orb when it is up, close enough and nobody is on top of us
		if (!S.bMinion && S.bOrb && FVector::Dist(S.Self, S.Orb) < 3000.f && (!Best || FVector::Dist(S.Self, Best->Pos) > 700.f))
		{
			Out.MoveTo = S.Orb;
			if (Best) { Out.TargetId = Best->Id; Out.Aim = Best->Pos; }
			return Out;
		}
		if (!Best) { Out.MoveTo = S.bMinion ? S.EnemyPortal : S.EnemyPortal * 0.55f + S.Base * 0.45f; return Out; }
		Out.TargetId = Best->Id;
		Out.Aim = Best->Pos;
		// distance to its body's surface, as for a unit of normal width (a tower is three times as wide)
		const float Wide = FMath::Max(0.f, Best->Radius - 40.f);
		const float Dist = FVector::Dist(S.Self, Best->Pos) - Wide;
		// a ready ultimate goes to the nearest enemy hero in its reach, whoever the current target is (it opens only at
		// level 5, so a bot must not sit on it waiting for its minion target to become a hero)
		if (S.Slots[4].bReady && S.Slots[4].bUltimate && !S.Slots[4].bSupport)
		{
			const FUnit* Hero = nullptr;
			float HeroDist = TNumericLimits<float>::Max();
			int32 BestCaught = 0;
			for (const FUnit& E : S.Enemies)
			{
				const float D = FVector::Dist(S.Self, E.Pos);
				if (!E.bHero || D > S.Slots[4].RangeCm * (S.Slots[4].bDash ? 1.2f : 1.f)) { continue; }
				// a leaping ultimate is an execution (Countess, Khaimera: a big blow at the landing): kept for a hurt
				// target, unless the bot itself is about to fall (it opened every duel with it at full health)
				if (S.Slots[4].bDash && E.HpPct > 0.6f && S.HpPct > 0.35f) { continue; }
				// an area ultimate goes where it catches the most heroes, then the nearest
				int32 Caught = 1;
				if (S.Slots[4].AreaCm > 0.f) { for (const FUnit& O : S.Enemies) { Caught += (&O != &E && O.bHero && FVector::Dist2D(O.Pos, E.Pos) <= S.Slots[4].AreaCm) ? 1 : 0; } }
				if (Caught > BestCaught || (Caught == BestCaught && D < HeroDist)) { Hero = &E; HeroDist = D; BestCaught = Caught; }
			}
			if (Hero) { Out.CastSlot = 4; Out.Aim = Hero->Pos; Out.TargetId = Hero->Id; }
		}
		// pick the highest slot that is ready and in range (ultimate only on heroes or below half HP)
		for (int32 i = 4; i >= 0 && Out.CastSlot < 0; --i)
		{
			const FSlot& Sl = S.Slots[i];
			if (!Sl.bReady) { continue; }
			if (Best->bStructure && i != 0) { continue; }   // abilities do not hurt structures
			if (Sl.bSupport)
			{
				bool bAllyHurt = S.HpPct < 0.7f;
				for (const FUnit& A : S.Allies) { bAllyHurt |= A.bHero && A.HpPct < 0.6f && FVector::Dist(A.Pos, S.Self) < 1500.f; }   // heroes, not minions
				if (bAllyHurt) { Out.CastSlot = i; Out.Aim = S.Self; break; }
				continue;
			}
			if (Sl.bUltimate && !Best->bHero) { continue; }
			// v19: a stun or a hard slow is kept for a hero when one is near (a bot used to spend it on the wave)
			if (Sl.bCC && !Best->bHero && HeroesNear(S.Enemies, S.Self, 2200.f) > 0) { continue; }
			// v19: below 45 % mana the wave is farmed with the basic attack (the mana is for the heroes)
			if (i >= 1 && i <= 3 && !Best->bHero && S.ManaPct < 0.45f) { continue; }
			// the leaping execution waits here too (the block above kept it, this loop still opened every duel with it)
			if (Sl.bUltimate && Sl.bDash && Best->HpPct > 0.6f && S.HpPct > 0.35f) { continue; }
			// mana for the ultimate: with it off cooldown, abilities 1-3 never spend the mana it needs (MOBA players keep it)
			if (i >= 1 && i <= 3 && S.Slots[4].bOffCooldown && S.Mana - Sl.ManaCost < S.Slots[4].ManaCost) { continue; }
			// a melee swing waits for body contact: the weapon visibly reaches the target
			// (v17: within the weapon's reach; with the swing's short lunge a blade of 2.3 m connects from there — at
			// 1.5 m an assassin landing from its leap 2.5 m away never swung before the ranged hero was gone)
			// (a target backing off at the same pace kept an assassin 2.5-2.9 m away, just past its 2.3 m blade: the swing's
			// lunge of up to 70 cm closes that, so the swing starts up to 55 cm past the blade)
			if (i == 0 && IsMeleeKit(S) && Dist > FMath::Max(MeleeContactCm + 35.f, S.Slots[0].RangeCm + 55.f)) { continue; }
			if (Dist <= Sl.RangeCm * (Sl.bDash ? 1.2f : 1.f)) { Out.CastSlot = i; break; }
		}
		// a straight shot leads a moving target (more on the harder levels)
		if (Out.CastSlot >= 0 && S.Lead > 0.f)
		{
			const FSlot& Sl = S.Slots[Out.CastSlot];
			const FUnit* T = Best;
			for (const FUnit& E : S.Enemies) { if (E.Id == Out.TargetId) { T = &E; } }
			if (Sl.SpeedCm > 0.f && T) { Out.Aim = T->Pos + T->Vel * (FVector::Dist(S.Self, T->Pos) / Sl.SpeedCm) * S.Lead; }
		}
		const FVector Away = (S.Self - Best->Pos).GetSafeNormal2D();
		if (IsMeleeKit(S))
		{
			// melee: close to body contact and stay there, circling the target a little (Smite-style footwork)
			Out.StopDistance = 15.f;
			if (Dist > MeleeContactCm + 40.f) { Out.MoveTo = Best->Pos + Away * (MeleeContactCm + Wide); }
			else { Out.MoveTo = Best->Pos + Away.RotateAngleAxis(35.f * S.StrafeSign, FVector::UpVector) * (MeleeContactCm + Wide); }
			return Out;
		}
		// ranged kits keep off an enemy melee hero on top of them (they back away and keep shooting: it is what a
		// player does, and with the fire slow it costs them speed)
		for (const FUnit& E : S.Enemies)
		{
			if (!E.bHero || !E.bMelee || E.bStructure) { continue; }
			const float D = FVector::Dist2D(E.Pos, S.Self);
			if (D < 550.f)
			{
				const FVector Off = (S.Self - E.Pos).GetSafeNormal2D();
				Out.MoveTo = S.Self + Off * 450.f + FVector(-Off.Y, Off.X, 0.f) * S.StrafeSign * 150.f;
				Out.StopDistance = 40.f;
				return Out;
			}
		}
		// ranged kits stay at ~70 % of basic range and strafe
		// a structure is hurt only from close (Conquest: 12 m); a unit at ~70 % of the basic range
		const float Want = Best->bStructure ? FMath::Min(900.f, FMath::Max(150.f, S.Slots[0].RangeCm * 0.7f)) : FMath::Max(150.f, S.Slots[0].RangeCm * 0.7f);
		if (Dist > Want) { Out.MoveTo = Best->Pos; }
		else
		{
			const FVector Side = FVector(-Away.Y, Away.X, 0.f) * S.StrafeSign;
			Out.MoveTo = S.Self + Side * 350.f + (Dist < Want * 0.6f ? Away * 250.f : FVector::ZeroVector);
		}
		return Out;
	}

	/** Aim error per difficulty (01 §6): 3.0 / 1.5 / 0.6 m. */
	// v17: normal and hard sharper (operator: "far too weak even on hard")
	// v19 (operator: "not more accurate in the hitting, better decisions"): a human hand on every level — hard aims
	// within ~0.7 m and reacts in ~0.2 s; its edge is in the decisions above
	inline float AimErrorCm(int32 Difficulty) { return Difficulty <= 0 ? 300.f : (Difficulty == 1 ? 140.f : 70.f); }
	inline float ReactionSeconds(int32 Difficulty) { return Difficulty <= 0 ? 0.6f : (Difficulty == 1 ? 0.32f : 0.2f); }
	inline float LeadFor(int32 Difficulty) { return Difficulty <= 0 ? 0.f : (Difficulty == 1 ? 0.5f : 0.75f); }
}
