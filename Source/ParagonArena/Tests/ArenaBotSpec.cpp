// Bot decisions added in v16 (ArenaBot::Decide, pure): a straight shot leads a moving target by the difficulty's
// share, an area ultimate goes where it catches the most heroes, the team focuses a weak hero an ally is already on.
#include "Misc/AutomationTest.h"
#include "AI/ArenaBotBrain.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FArenaBotSmartsSpec, "Arena.BotSmarts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	ArenaBot::FSnapshot Ranged()
	{
		ArenaBot::FSnapshot S;
		S.Self = FVector(0.f, 0.f, 0.f);
		S.Base = FVector(-5000.f, 0.f, 0.f);
		S.EnemyPortal = FVector(5000.f, 0.f, 0.f);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 2500.f; S.Slots[0].SpeedCm = 5000.f;
		return S;
	}
END_DEFINE_SPEC(FArenaBotSmartsSpec)

void FArenaBotSmartsSpec::Define()
{
	It("leads a moving target with a straight shot, by the difficulty's share", [this]()
	{
		ArenaBot::FSnapshot S = Ranged();
		ArenaBot::FUnit E; E.Id = 3; E.Pos = FVector(1000.f, 0.f, 0.f); E.Vel = FVector(0.f, 500.f, 0.f); E.bHero = true;
		S.Enemies.Add(E);
		S.Lead = 1.f;
		const ArenaBot::FIntent Hard = ArenaBot::Decide(S);
		TestEqual("casts the basic attack", Hard.CastSlot, 0);
		TestTrue("hard: aims 1 m ahead (0.2 s of flight at 5 m/s)", Hard.Aim.Equals(FVector(1000.f, 100.f, 0.f), 1.f));
		S.Lead = 0.f;
		TestTrue("easy: at the body", ArenaBot::Decide(S).Aim.Equals(E.Pos, 1.f));
	});
	It("puts an area ultimate where it catches the most heroes", [this]()
	{
		ArenaBot::FSnapshot S = Ranged();
		S.Slots[4].bReady = true; S.Slots[4].bUltimate = true; S.Slots[4].RangeCm = 2000.f; S.Slots[4].AreaCm = 400.f;
		ArenaBot::FUnit Lone; Lone.Id = 1; Lone.Pos = FVector(600.f, 0.f, 0.f); Lone.bHero = true;
		ArenaBot::FUnit A; A.Id = 2; A.Pos = FVector(1500.f, 300.f, 0.f); A.bHero = true;
		ArenaBot::FUnit B; B.Id = 3; B.Pos = FVector(1600.f, 500.f, 0.f); B.bHero = true;
		S.Enemies = { Lone, A, B };
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestEqual("the ultimate", I.CastSlot, 4);
		TestTrue("into the pair, not the nearer lone hero", I.TargetId == 2 || I.TargetId == 3);
	});
	It("focuses a weak hero an ally is on", [this]()
	{
		ArenaBot::FSnapshot S = Ranged();
		ArenaBot::FUnit Near; Near.Id = 1; Near.Pos = FVector(900.f, 0.f, 0.f); Near.HpPct = 0.45f; Near.bHero = true;
		ArenaBot::FUnit Weak; Weak.Id = 2; Weak.Pos = FVector(1200.f, 300.f, 0.f); Weak.HpPct = 0.4f; Weak.bHero = true; Weak.bFocus = true;
		S.Enemies = { Near, Weak };
		TestEqual("the focused one", ArenaBot::Decide(S).TargetId, 2);
		S.Enemies[1].bFocus = false;
		TestEqual("without the focus: the nearer", ArenaBot::Decide(S).TargetId, 1);
	});
}

#endif
