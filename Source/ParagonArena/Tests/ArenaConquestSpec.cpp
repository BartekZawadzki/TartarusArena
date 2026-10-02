// Conquest rules (v14, 01 §3b): the protection chain, tower targeting and damage, hero damage on structures, the
// lane layout, the timeout winner, and the data (every structure, heavy minion, camp and boss is in heroes.json
// and its assets exist).
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Data/ArenaTypes.h"
#include "Core/ArenaConquestRules.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace ArenaConquest;

BEGIN_DEFINE_SPEC(FArenaConquestSpec, "Arena.Conquest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TArray<FStructureState> Lane(int32 Team, int32 L, bool bOuter, bool bInner, bool bInhib)
	{
		return { { Tower, Team, L, 1, bOuter }, { Tower, Team, L, 2, bInner }, { Inhibitor, Team, L, 3, bInhib } };
	}
END_DEFINE_SPEC(FArenaConquestSpec)

void FArenaConquestSpec::Define()
{
	Describe("the protection chain (LoL)", [this]()
	{
		It("opens the outer tower first, then the inner one, then the inhibitor, then the core", [this]()
		{
			TArray<FStructureState> All = Lane(0, 0, true, true, true);
			All.Append(Lane(0, 1, true, true, true));
			All.Add({ Core, 0, -1, 4, true });
			TestTrue("outer open", IsVulnerable(All[0], All));
			TestFalse("inner shut while the outer stands", IsVulnerable(All[1], All));
			TestFalse("inhibitor shut", IsVulnerable(All[2], All));
			TestFalse("core shut", IsVulnerable(All.Last(), All));
			All[0].bAlive = false;
			TestTrue("inner open once the outer fell", IsVulnerable(All[1], All));
			TestFalse("the other lane's inner stays shut", IsVulnerable(All[4], All));
			All[1].bAlive = false;
			TestTrue("inhibitor open once the inner fell", IsVulnerable(All[2], All));
			TestFalse("core still shut", IsVulnerable(All.Last(), All));
			All[2].bAlive = false;
			TestTrue("core open with an inhibitor down", IsVulnerable(All.Last(), All));
			TestFalse("a dead structure is not a target", IsVulnerable(All[2], All));
		});
		It("works with one tower per lane: the inhibitor opens when it fell", [this]()
		{
			TArray<FStructureState> All = { { Tower, 0, 0, 1, true }, { Inhibitor, 0, 0, 3, true }, { Core, 0, -1, 4, true } };
			TestTrue("tower open", IsVulnerable(All[0], All));
			TestFalse("inhibitor shut", IsVulnerable(All[1], All));
			All[0].bAlive = false;
			TestTrue("inhibitor open", IsVulnerable(All[1], All));
			TestFalse("core shut", IsVulnerable(All[2], All));
		});
		It("never lets one team's chain open another's", [this]()
		{
			TArray<FStructureState> All = Lane(0, 0, false, false, false);
			All.Append(Lane(1, 0, true, true, true));
			All.Add({ Core, 1, -1, 4, true });
			TestFalse("team B's inner shut", IsVulnerable(All[4], All));
			TestFalse("team B's core shut", IsVulnerable(All.Last(), All));
		});
	});
	Describe("tower targeting", [this]()
	{
		It("takes minions before heroes, keeps its target, and answers a hero who hits an ally hero", [this]()
		{
			TArray<FTowerCandidate> C = { { 1, 500.f, true, false }, { 2, 700.f, false, false }, { 3, 300.f, false, false } };
			TestEqual("the nearest minion first", PickTowerTarget(0, C), 3);
			TestEqual("keeps the current target in range", PickTowerTarget(2, C), 2);
			C[0].bHitAllyHero = true;
			TestEqual("call for help overrides", PickTowerTarget(2, C), 1);
			TArray<FTowerCandidate> Heroes = { { 5, 800.f, true, false }, { 6, 400.f, true, false } };
			TestEqual("only heroes: the nearest", PickTowerTarget(0, Heroes), 6);
			TestEqual("nobody in range", PickTowerTarget(4, {}), 0);
		});
		It("ramps on a hero and takes a share of a minion's health", [this]()
		{
			const TArray<float> Pct = { 0.45f, 0.7f, 0.14f, 0.07f };
			TestEqual("first shot", TowerShot(100.f, true, 0, 0.4f, 1.2f, 0, Pct, 500.f), 100.f);
			TestEqual("third shot +80 %", TowerShot(100.f, true, 2, 0.4f, 1.2f, 0, Pct, 500.f), 180.f);
			TestEqual("capped at +120 %", TowerShot(100.f, true, 9, 0.4f, 1.2f, 0, Pct, 500.f), 220.f);
			TestEqual("a melee minion loses 45 %", TowerShot(100.f, false, 0, 0.4f, 1.2f, 0, Pct, 400.f), 180.f);
			TestEqual("a super minion loses 7 % (the tower's own damage does not matter)", TowerShot(100.f, false, 0, 0.4f, 1.2f, 3, Pct, 400.f), 28.f);
		});
	});
	Describe("heroes on structures", [this]()
	{
		It("count basic attacks from close only, a third without their minions", [this]()
		{
			TestEqual("ability", HeroOnStructure(false, true, true, 0.66f), 0.f);
			TestEqual("out of reach", HeroOnStructure(true, false, true, 0.66f), 0.f);
			TestEqual("with minions", HeroOnStructure(true, true, true, 0.66f), 1.f);
			TestTrue("backdoor", FMath::IsNearlyEqual(HeroOnStructure(true, true, false, 0.66f), 0.34f, 0.001f));
		});
	});
	Describe("layout and the end", [this]()
	{
		It("walks a lane polyline by distance", [this]()
		{
			const TArray<FVector> Path = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 1000.f, 0.f) };
			TestTrue("on the first leg", AlongPath(Path, 400.f).Equals(FVector(400.f, 0.f, 0.f), 0.1f));
			TestTrue("on the second leg", AlongPath(Path, 1500.f).Equals(FVector(1000.f, 500.f, 0.f), 0.1f));
			TestTrue("past the end", AlongPath(Path, 9000.f).Equals(FVector(1000.f, 1000.f, 0.f), 0.1f));
		});
		It("gives the timeout to fewer structures lost, then the score", [this]()
		{
			TestEqual("more standing", TimeoutWinner(8, 5, 10, 90), 0);
			TestEqual("the score breaks a tie", TimeoutWinner(6, 6, 30, 45), 1);
			TestEqual("a draw", TimeoutWinner(6, 6, 30, 30), -1);
			TestEqual("monster level grows", MonsterLevel(600.f, 0.4f), 5);
		});
	});
	It("has every structure, heavy minion, camp and the boss in heroes.json, with assets that load", [this]()
	{
		FString Json;
		TestTrue("file readable", FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/heroes.json"))));
		FArenaDatabaseFile Db;
		FArenaDatabase::Parse(Json, Db);
		const FArenaConquestDef& Cq = Db.Rules.Conquest;
		TArray<const FArenaHeroDef*> Units = { &Cq.Tower, &Cq.Inhibitor, &Cq.Core, &Cq.SiegeMinion, &Cq.SuperMinion };
		int32 Bosses = 0;
		for (const FArenaCampDef& C : Cq.Camps) { Units.Add(&C.Unit); Bosses += C.Buff == 3 ? 1 : 0; }
		TestEqual("one boss", Bosses, 1);
		TestTrue("camps", Cq.Camps.Num() >= 4);
		TestTrue("mid layout: an inhibitor and one or two towers", Cq.MidAlong.Num() >= 2 && Cq.MidAlong.Num() <= 3);
		TestEqual("side layout like the mid", Cq.SideAlong.Num(), Cq.MidAlong.Num());
		TestEqual("shot shares", Cq.MinionShotPct.Num(), 4);
		TestEqual("tower archetype", Cq.Tower.Abilities.Num() > 0 ? (int32)Cq.Tower.Abilities[0].Archetype : -1, (int32)EArenaArchetype::Targeted);
		for (const FArenaHeroDef* D : Units)
		{
			TestFalse(FString::Printf(TEXT("%s has a mesh"), *D->Id.ToString()), D->Mesh.IsEmpty());
			TestNotNull(FString::Printf(TEXT("%s mesh loads"), *D->Id.ToString()), LoadObject<UObject>(nullptr, *D->Mesh, nullptr, LOAD_NoWarn | LOAD_Quiet));
			if (!D->IdleAnim.IsEmpty()) { TestNotNull(FString::Printf(TEXT("%s idle loads"), *D->Id.ToString()), LoadObject<UObject>(nullptr, *D->IdleAnim, nullptr, LOAD_NoWarn | LOAD_Quiet)); }
			TestTrue(FString::Printf(TEXT("%s can act"), *D->Id.ToString()), D->Abilities.Num() > 0);
			for (const FArenaAbilityDef& A : D->Abilities)
			{
				for (const FString* P : { &A.Anim, &A.Fx, &A.TrailFx, &A.CastFx })
				{
					if (!P->IsEmpty()) { TestNotNull(FString::Printf(TEXT("%s %s"), *D->Id.ToString(), **P), LoadObject<UObject>(nullptr, **P, nullptr, LOAD_NoWarn | LOAD_Quiet)); }
				}
			}
			for (const FString& P : D->DeathAnims) { TestNotNull(FString::Printf(TEXT("%s death %s"), *D->Id.ToString(), *P), LoadObject<UObject>(nullptr, *P, nullptr, LOAD_NoWarn | LOAD_Quiet)); }
		}
		for (const FString& F : Cq.BuffFx) { TestNotNull(F, LoadObject<UObject>(nullptr, *F, nullptr, LOAD_NoWarn | LOAD_Quiet)); }
	});
}

#endif
