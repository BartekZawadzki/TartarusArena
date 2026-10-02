// Automation specs for the pure rules (01 §7 VR-01..VR-06). Run headless:
// UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Arena.Core;Quit" -unattended -nullrhi -nosplash -NoSound -ReportExportPath=<dir> -log
#include "Misc/AutomationTest.h"
#include "Core/ArenaCore.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace ArenaCore;

BEGIN_DEFINE_SPEC(FArenaCoreSpec, "Arena.Core", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FArenaCoreSpec)

void FArenaCoreSpec::Define()
{
	Describe("Damage (VR-01)", [this]()
	{
		It("mitigates by K/(K+armor)", [this]()
		{
			FDamageInput In; In.Base = 100.f; In.TargetArmor = 100.f;
			TestEqual("100 dmg vs 100 armor", ComputeDamage(In), 50.f);
		});
		It("never goes negative or heals", [this]()
		{
			FDamageInput In; In.Base = -40.f; In.AttackerPower = -10.f; In.PowerScaling = 1.f;
			TestEqual("negative input clamps to 0", ComputeDamage(In), 0.f);
		});
		It("scales +5% per level and crits", [this]()
		{
			FDamageInput In; In.Base = 100.f; In.AttackerLevel = 3; In.bCrit = true; In.CritMultiplier = 2.f;
			TestEqual("lvl3 crit x2", ComputeDamage(In), 220.f, 0.01f);
		});
		It("shield absorbs first and health clamps to [0,max] (GS-11)", [this]()
		{
			const TPair<float, float> A = ApplyDamage(30.f, 100.f, 100.f, 50.f);
			TestEqual("shield gone", A.Key, 0.f); TestEqual("health -20", A.Value, 80.f);
			const TPair<float, float> B = ApplyDamage(0.f, 10.f, 100.f, 999.f);
			TestEqual("health floor 0", B.Value, 0.f);
		});
	});

	Describe("Friendly fire (VR-04)", [this]()
	{
		It("only different real teams are hostile", [this]()
		{
			TestTrue("0 vs 1", IsHostile(0, 1));
			TestFalse("0 vs 0", IsHostile(0, 0));
			TestFalse("no team", IsHostile(NoTeam, 1));
		});
	});

	Describe("Hit registry (VR-03)", [this]()
	{
		It("registers a target once per activation", [this]()
		{
			FHitRegistry R;
			TestTrue("first", R.TryRegister(42)); TestFalse("second", R.TryRegister(42));
			R.Reset(); TestTrue("after reset", R.TryRegister(42));
		});
	});

	Describe("Team score (VR-05)", [this]()
	{
		It("awards +5/+1/+2 to the scoring team and freezes at the limit", [this]()
		{
			FScoreRules Rules; Rules.Limit = 8;
			FScoreLedger L(Rules);
			TestEqual("hero kill", L.Award(EScoreEvent::HeroKill, 0), 5);
			TestEqual("minion", L.Award(EScoreEvent::MinionKill, 0), 6);
			TestEqual("other team untouched", L.Points[1], 0);
			TestEqual("no winner yet", L.Winner(false), (int32)NoTeam);
			TestEqual("minion at the base, clamped to the limit", L.Award(EScoreEvent::MinionReachedBase, 0), 8);
			TestTrue("limit", L.LimitReached()); TestEqual("team 0 wins", L.Winner(false), 0);
			TestEqual("frozen after the limit", L.Award(EScoreEvent::HeroKill, 1), 0);
		});
		It("decides on time by the lead, a tie plays on", [this]()
		{
			FScoreLedger L;
			L.Award(EScoreEvent::MinionKill, 1);
			TestEqual("leader wins at time", L.Winner(true), 1);
			L.Award(EScoreEvent::MinionKill, 0);
			TestEqual("tie: overtime", L.Winner(true), (int32)NoTeam);
			TestEqual("ignores bad teams", L.Award(EScoreEvent::HeroKill, 7), 0);
		});
		It("plays each match length to 30 points a minute", [this]()
		{
			TestEqual("5 min", ScoreLimitFor(5, 30), 150); TestEqual("15 min", ScoreLimitFor(15, 30), 450); TestEqual("floor", ScoreLimitFor(0, 30), 30);
		});
	});

	Describe("Gold, items and revive (VR-12)", [this]()
	{
		It("sums item stats and caps the percentages", [this]()
		{
			FItemStats A; A.Power = 20.f; A.CooldownPct = 0.3f; A.LifestealPct = 0.2f;
			FItemStats B; B.Power = 25.f; B.CooldownPct = 0.3f; B.LifestealPct = 0.2f; B.MoveSpeedPct = 0.9f;
			A += B;
			const FItemStats C = A.Capped();
			TestEqual("power adds", C.Power, 45.f); TestEqual("cdr cap", C.CooldownPct, 0.4f);
			TestEqual("lifesteal cap", C.LifestealPct, 0.3f); TestEqual("speed cap", C.MoveSpeedPct, 0.4f);
			TestEqual("cooldown shortened", ReducedCooldown(10.f, 0.25f), 7.5f); TestEqual("never below 0.1", ReducedCooldown(0.1f, 0.4f), 0.1f);
		});
		It("buys only in the shop, with gold and a free slot", [this]()
		{
			TestTrue("ok", CanBuy(900, 800, 2, true) == EBuyResult::Ok);
			TestTrue("away from the shop", CanBuy(900, 800, 2, false) == EBuyResult::NotInShop);
			TestTrue("full", CanBuy(9000, 800, InventorySlots, true) == EBuyResult::InventoryFull);
			TestTrue("poor", CanBuy(799, 800, 0, true) == EBuyResult::NotEnoughGold);
		});
		It("revives only dead heroes, off cooldown, with the gold", [this]()
		{
			const int32 Cost = ReviveCost(4, 150, 50);   // levels 1..20
			TestEqual("cost by level", Cost, 350);
			TestTrue("ok", CanRevive(true, 100.f, 90.f, 400, Cost));
			TestFalse("alive", CanRevive(false, 100.f, 90.f, 400, Cost));
			TestFalse("cooldown", CanRevive(true, 80.f, 90.f, 400, Cost));
			TestFalse("gold", CanRevive(true, 100.f, 90.f, 349, Cost));
		});
	});

	Describe("Levels, ranks and crits (VR-20)", [this]()
	{
		It("levels on a growing XP curve up to the cap", [this]()
		{
			TestEqual("1 -> 2", XpToNext(1, 110.f, 35.f), 110.f);
			TestEqual("5 -> 6", XpToNext(5, 110.f, 35.f), 250.f);
			TestEqual("total to 3", XpForLevel(3, 110.f, 35.f), 255.f);
			TestEqual("xp 109 is level 1", LevelForXp(109.f, 110.f, 35.f, 20), 1);
			TestEqual("xp 110 is level 2", LevelForXp(110.f, 110.f, 35.f, 20), 2);
			TestEqual("capped", LevelForXp(1e7f, 110.f, 35.f, 20), 20);
		});
		It("opens ranks by level: abilities at 1/3/5/7/9, the ultimate at 5/9/13/17/20", [this]()
		{
			TestEqual("ability lvl1", MaxRankAt(1, 1), 1); TestEqual("ability lvl2", MaxRankAt(1, 2), 1); TestEqual("ability lvl3", MaxRankAt(2, 3), 2);
			TestEqual("ability lvl9", MaxRankAt(3, 9), 5); TestEqual("ability lvl20", MaxRankAt(3, 20), 5);
			TestEqual("ult lvl4", MaxRankAt(4, 4), 0); TestEqual("ult lvl5", MaxRankAt(4, 5), 1); TestEqual("ult lvl17", MaxRankAt(4, 17), 4); TestEqual("ult lvl20", MaxRankAt(4, 20), 5);
			int32 R[5] = { 1, 0, 0, 0, 0 };
			TestTrue("level 1 learns one ability", CanRankUp(1, 1, R));
			R[1] = 1;
			TestFalse("no second point at level 1", CanRankUp(2, 1, R));
			TestFalse("no rank 2 at level 2", CanRankUp(1, 2, R));
			TestTrue("another ability at level 2", CanRankUp(2, 2, R));
			TestFalse("no ultimate before 5", CanRankUp(4, 4, R));
			TestEqual("rank value", Ranked(90.f, 35.f, 3), 160.f);
		});
		It("crits are deterministic: 25 % is every 4th hit", [this]()
		{
			float Acc = 0.f; int32 Crits = 0; FString Pattern;
			for (int32 i = 0; i < 12; ++i) { const bool bC = NextCrit(Acc, 0.25f); Crits += bC ? 1 : 0; Pattern += bC ? TEXT("X") : TEXT("."); }
			TestEqual("pattern", Pattern, FString(TEXT("...X...X...X")));
			float Z = 0.f; TestFalse("no chance, no crit", NextCrit(Z, 0.f));
		});
	});

	Describe("Shutdown gold (VR-23)", [this]()
	{
		It("pays for ending a streak of 3 or more, capped", [this]()
		{
			TestEqual("no streak", ShutdownGold(2, 100, 400), 0);
			TestEqual("3 kills", ShutdownGold(3, 100, 400), 100);
			TestEqual("5 kills", ShutdownGold(5, 100, 400), 300);
			TestEqual("cap", ShutdownGold(12, 100, 400), 400);
		});
	});

	Describe("Item recipes (VR-12)", [this]()
	{
		It("prices a recipe with the parts already owned and uses them up", [this]()
		{
			// 0 sword 350, 1 tome 400, 2 warhammer = sword + tome + 300, 3 reaper = warhammer + sword + 900
			TArray<FRecipe> Db;
			Db.Add({ 350, {} }); Db.Add({ 400, {} }); Db.Add({ 300, { 0, 1 } }); Db.Add({ 900, { 2, 0 } });
			TestEqual("full price", TotalCost(3, Db), 2300);
			TArray<int32> Used;
			TestEqual("nothing owned", BuyPrice(3, Db, {}, Used), 2300);
			TestEqual("nothing consumed", Used.Num(), 0);
			TestEqual("warhammer owned", BuyPrice(3, Db, { 2 }, Used), 1250);
			TestEqual("warhammer consumed", Used.Num(), 1);
			TestEqual("parts of a part owned", BuyPrice(3, Db, { 1, 0, 0 }, Used), 1200);
			TestEqual("tome + both swords consumed", Used.Num(), 3);
			TestEqual("an unrelated item stays", BuyPrice(2, Db, { 3 }, Used), 1050);
			TestEqual("sell 60 %", SellValue(2, Db), 630);
			TestTrue("a full inventory buys when a part frees a slot", CanBuy(2000, 1250, InventorySlots, true, 1) == EBuyResult::Ok);
			TestTrue("but not without one", CanBuy(2000, 1250, InventorySlots, true, 0) == EBuyResult::InventoryFull);
		});
	});

	Describe("Respawn and cast gate (VR-06, VR-02)", [this]()
	{
		It("respawn = 5 s + 0.75 s per level", [this]()
		{
			TestEqual("lvl1", RespawnSeconds(1), 5.75f); TestEqual("lvl10", RespawnSeconds(10), 12.5f); TestEqual("clamp", RespawnSeconds(99), 20.f);
		});
		It("rejects casts on cooldown or without mana", [this]()
		{
			TestTrue("ready", CanCast(0.f, 50.f, 40.f));
			TestFalse("cooldown", CanCast(0.5f, 50.f, 40.f));
			TestFalse("mana", CanCast(0.f, 39.f, 40.f));
		});
	});
}

#endif
