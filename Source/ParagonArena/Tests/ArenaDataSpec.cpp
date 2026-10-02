// Specs for the data contract (heroes.json) and the pure bot brain (01 §6, §7).
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Data/ArenaTypes.h"
#include "AI/ArenaBotBrain.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FArenaDataSpec, "Arena.Data", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FArenaDataSpec)

void FArenaDataSpec::Define()
{
	It("heroes.json parses with no problems and holds the five classes", [this]()
	{
		FString Json;
		TestTrue("file readable", FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/heroes.json"))));
		FArenaDatabaseFile Db;
		const TArray<FString> Problems = FArenaDatabase::Parse(Json, Db);
		for (const FString& P : Problems) { AddError(P); }
		TestTrue("heroes: the roster (5 in v1, 11 since v15)", Db.Heroes.Num() >= 5);
		TArray<FName> Ids;
		for (const FArenaHeroDef& H : Db.Heroes) { TestFalse(H.Id.ToString() + " id unique", Ids.Contains(H.Id)); Ids.Add(H.Id); }
		TArray<FString> Classes;
		for (const FArenaHeroDef& H : Db.Heroes) { Classes.AddUnique(H.Class); TestTrue(H.Id.ToString() + " has an ultimate", H.Abilities.Num() == 5 && H.Abilities[4].bUltimate); }
		TestTrue("at least five classes", Classes.Num() >= 5);
		TestEqual("score per minute", Db.Rules.ScorePerMinute, 30);
		TestTrue("a shop with at least 10 items", Db.Items.Num() >= 10);
		for (const FArenaHeroDef& H : Db.Heroes) { TestTrue(FString::Printf(TEXT("%s has a bot build"), *H.Id.ToString()), H.Build.Num() >= 4); }
	});
	It("every asset heroes.json names exists (a wrong path leaves an ability without its effect)", [this]()
	{
		// found in v11: Countess's Blade Siphon and Kwang's Light Strike burst named paths without their FX folder,
		// so those abilities played with no effect since v9
		FString Json;
		TestTrue("file readable", FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/heroes.json"))));
		TSharedPtr<FJsonValue> Root;
		TestTrue("valid JSON", FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
		// the Paragon packs' own *PlayerCharacter blueprints (not used by the game) fail to compile in 5.8 when the
		// anim blueprints pull them in: known, not what this test checks
		AddExpectedError(TEXT("PlayerCharacter"), EAutomationExpectedErrorFlags::Contains, 0);
		TArray<FString> Paths;
		TFunction<void(const TSharedPtr<FJsonValue>&)> Walk = [&](const TSharedPtr<FJsonValue>& V)
		{
			if (!V.IsValid()) { return; }
			switch (V->Type)
			{
			case EJson::String: { const FString S = V->AsString(); if (S.StartsWith(TEXT("/Game/")) || S.StartsWith(TEXT("/Engine/"))) { Paths.AddUnique(S); } break; }
			case EJson::Array: for (const TSharedPtr<FJsonValue>& E : V->AsArray()) { Walk(E); } break;
			case EJson::Object: for (const TPair<FString, TSharedPtr<FJsonValue>>& P : V->AsObject()->Values) { Walk(P.Value); } break;
			default: break;
			}
		};
		Walk(Root);
		TestTrue("paths found", Paths.Num() > 100);
		int32 Missing = 0;
		for (const FString& P : Paths)
		{
			// an object first (an asset may itself be named ..._C); a blueprint class path loads as a class
			UObject* O = LoadObject<UObject>(nullptr, *P, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!O && P.EndsWith(TEXT("_C"))) { O = LoadClass<UObject>(nullptr, *P, nullptr, LOAD_NoWarn | LOAD_Quiet); }
			if (!O) { AddError(FString::Printf(TEXT("missing asset: %s"), *P)); ++Missing; }
		}
		TestEqual("missing assets", Missing, 0);
	});
	It("rejects negative numbers (decoy)", [this]()
	{
		FArenaDatabaseFile Db;
		const FString Bad = TEXT("{\"heroes\":[{\"id\":\"X\",\"maxHealth\":100,\"moveSpeed\":5,\"abilities\":[{\"damage\":-5},{},{},{},{}]}],\"rules\":{\"tickets\":10}}");
		TestTrue("problem reported", FArenaDatabase::Parse(Bad, Db).Num() > 0);
	});
}

BEGIN_DEFINE_SPEC(FArenaBotSpec, "Arena.Bot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FArenaBotSpec)

void FArenaBotSpec::Define()
{
	It("retreats to base below the retreat threshold", [this]()
	{
		ArenaBot::FSnapshot S; S.HpPct = 0.1f; S.Base = FVector(-5000, 0, 0);
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestTrue("retreat", I.bRetreat); TestEqual("moves to base", I.MoveTo, S.Base);
		// each hero has its own spot at the fountain: retreating heroes do not stack on one point
		S.BaseSlot = 0; const FVector A = ArenaBot::Decide(S).MoveTo;
		S.BaseSlot = 3; const FVector B = ArenaBot::Decide(S).MoveTo;
		TestTrue("own spots differ", FVector::Dist(A, B) > 200.f);
		TestTrue("spots stay at the base", FVector::Dist(A, S.Base) < 350.f && FVector::Dist(B, S.Base) < 350.f);
	});
	It("does not chase a healthy hero running away out of reach; a hurt one it does (v19)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector; S.HpPct = 0.9f; S.Base = FVector(-8000, 0, 0);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 250.f;
		ArenaBot::FUnit E; E.Id = 3; E.Pos = FVector(900, 0, 0); E.Vel = FVector(600, 0, 0); E.HpPct = 0.8f; S.Enemies.Add(E);
		ArenaBot::FUnit M; M.Id = 4; M.bHero = false; M.Pos = FVector(0, 700, 0); S.Enemies.Add(M);
		TestEqual("the minion, not the runner", ArenaBot::Decide(S).TargetId, 4);
		S.Enemies[0].HpPct = 0.2f;
		TestEqual("a nearly dead runner is chased", ArenaBot::Decide(S).TargetId, 3);
	});
	It("keeps a stun for a hero when one is near instead of spending it on a minion (v19)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector; S.HpPct = 0.9f; S.Base = FVector(-8000, 0, 0);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 1500.f;
		S.Slots[1].bReady = true; S.Slots[1].RangeCm = 1500.f; S.Slots[1].bCC = true;
		ArenaBot::FUnit M; M.Id = 4; M.bHero = false; M.Pos = FVector(300, 0, 0); S.Enemies.Add(M);
		ArenaBot::FUnit E; E.Id = 3; E.Pos = FVector(1700, 900, 0); E.HpPct = 1.f; S.Enemies.Add(E);
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestTrue("not the stun on the minion", I.TargetId != 4 || I.CastSlot != 1);
	});
	It("pulls back to its wave from the enemy's half when three enemies are missing (v19)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector(3000, 0, 0); S.HpPct = 0.9f; S.Base = FVector(-8000, 0, 0);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 1500.f;
		S.bPastMiddle = true; S.EnemiesMissing = 3; S.Fallback = FVector(-1000, 0, 0);
		ArenaBot::FUnit M; M.Id = 4; M.bHero = false; M.Pos = FVector(5000, 0, 0); S.Enemies.Add(M);
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestEqual("back to the wave", I.MoveTo, S.Fallback);
		S.EnemiesMissing = 0;
		TestNotEqual("with everyone in sight it pushes on", ArenaBot::Decide(S).MoveTo, S.Fallback);
	});
	It("does not dive a hero under its tower; dives only while the tower shoots our minions and the hero is nearly dead (v18)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector; S.HpPct = 0.8f; S.Base = FVector(-8000, 0, 0);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 250.f;                       // a melee kit
		ArenaBot::FUnit E; E.Id = 5; E.Pos = FVector(1200, 0, 0); E.HpPct = 0.2f; S.Enemies.Add(E);
		ArenaBot::FTower T; T.Pos = FVector(1700, 0, 0); T.RangeCm = 1100.f; S.EnemyTowers.Add(T);
		TestNotEqual("no chase under a free tower", ArenaBot::Decide(S).TargetId, 5);
		S.EnemyTowers[0].bBusy = true;                                               // it shoots our wave now
		TestEqual("the dive when the tower is busy and the hero nearly dead", ArenaBot::Decide(S).TargetId, 5);
		S.HpPct = 0.35f;
		TestNotEqual("never at low health", ArenaBot::Decide(S).TargetId, 5);
	});
	It("goes home earlier under a tower's fire and never finishes a hero under its tower at low health (v18)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector; S.HpPct = 0.3f; S.Base = FVector(-8000, 0, 0);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 1500.f;
		ArenaBot::FUnit E; E.Id = 6; E.Pos = FVector(800, 0, 0); E.HpPct = 0.15f; S.Enemies.Add(E);
		ArenaBot::FTower T; T.Pos = FVector(900, 0, 0); T.RangeCm = 1100.f; T.bTargetsMe = true; S.EnemyTowers.Add(T);
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestTrue("retreats at 30 % under fire", I.bRetreat);
		TestEqual("towards home", I.MoveTo, S.Base);
	});
	It("outnumbered by two it falls back to its tower instead of fighting (v18)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector; S.HpPct = 0.7f; S.Base = FVector(-8000, 0, 0);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 1000.f;
		for (int32 i = 0; i < 3; ++i) { ArenaBot::FUnit E; E.Id = 10 + i; E.Pos = FVector(1200 + i * 100, 0, 0); S.Enemies.Add(E); }
		S.OwnTowers.Add(FVector(-3000, 0, 0));
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestTrue("backs off", I.MoveTo.X < -2000.f);
		TestEqual("no shot out of reach", I.CastSlot, -1);
	});
	It("hits back at the enemy hero on it while it retreats (v17)", [this]()
	{
		ArenaBot::FSnapshot S; S.HpPct = 0.1f; S.Base = FVector(-5000, 0, 0);
		ArenaBot::FUnit E; E.Id = 9; E.Pos = FVector(400, 0, 0); E.bMelee = true; S.Enemies.Add(E);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 1500.f;
		ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestTrue("still retreats", I.bRetreat); TestEqual("runs home", I.MoveTo, S.Base);
		TestEqual("shoots the hero on it", I.CastSlot, 0); TestEqual("at that hero", I.TargetId, 9);
		S.Enemies[0].Pos = FVector(2500, 0, 0);   // out of its reach: it only runs
		I = ArenaBot::Decide(S);
		TestEqual("no shot out of range", I.CastSlot, -1);
	});
	It("never casts an out-of-range attack (NPC-L: range)", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector;
		ArenaBot::FUnit E; E.Id = 7; E.Pos = FVector(2000, 0, 0); S.Enemies.Add(E);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 300.f;
		const ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestEqual("targets the enemy", I.TargetId, 7);
		TestEqual("no cast at 20 m with a 3 m attack", I.CastSlot, -1);
	});
	It("casts the ultimate on a hero in range", [this]()
	{
		ArenaBot::FSnapshot S;
		ArenaBot::FUnit E; E.Id = 3; E.Pos = FVector(500, 0, 0); S.Enemies.Add(E);
		for (int32 i = 0; i < 5; ++i) { S.Slots[i].bReady = true; S.Slots[i].RangeCm = 800.f; }
		S.Slots[4].bUltimate = true;
		TestEqual("ultimate first", ArenaBot::Decide(S).CastSlot, 4);
	});
	It("contests the power orb when no enemy is on top of it", [this]()
	{
		ArenaBot::FSnapshot S; S.bOrb = true; S.Orb = FVector(1000, 0, 0);
		ArenaBot::FUnit E; E.Id = 9; E.Pos = FVector(-2500, 0, 0); S.Enemies.Add(E);
		TestEqual("goes for the orb", ArenaBot::Decide(S).MoveTo, S.Orb);
		S.Enemies[0].Pos = FVector(300, 0, 0);
		TestNotEqual("fights instead when an enemy is close", ArenaBot::Decide(S).MoveTo, S.Orb);
	});
	It("a melee bot closes to body contact and stays there", [this]()
	{
		ArenaBot::FSnapshot S; S.Self = FVector::ZeroVector;
		ArenaBot::FUnit E; E.Id = 5; E.Pos = FVector(600, 0, 0); S.Enemies.Add(E);
		S.Slots[0].bReady = true; S.Slots[0].RangeCm = 260.f;
		ArenaBot::FIntent I = ArenaBot::Decide(S);
		TestTrue("walks up to contact distance", FMath::Abs(FVector::Dist(I.MoveTo, E.Pos) - ArenaBot::MeleeContactCm) < 1.f);
		TestTrue("stops only at contact", I.StopDistance < 100.f);
		S.Self = FVector(120, 0, 0) + E.Pos - FVector(240, 0, 0);   // already in contact
		I = ArenaBot::Decide(S);
		TestTrue("keeps contact while circling", FMath::Abs(FVector::Dist(I.MoveTo, E.Pos) - ArenaBot::MeleeContactCm) < 1.f);
		S.Slots[0].RangeCm = 2500.f;                                 // a ranged kit keeps its distance instead
		TestTrue("ranged does not close in", FVector::Dist(ArenaBot::Decide(S).MoveTo, E.Pos) > 400.f);
	});
	It("minions fight minions, and switch to a hero that attacks their heroes (call for help)", [this]()
	{
		ArenaBot::FSnapshot S; S.bMinion = true; S.Self = FVector::ZeroVector;
		ArenaBot::FUnit Hero; Hero.Id = 1; Hero.Pos = FVector(300, 0, 0); Hero.bHero = true;
		ArenaBot::FUnit Minion; Minion.Id = 2; Minion.Pos = FVector(0, 500, 0); Minion.bHero = false;
		S.Enemies = { Hero, Minion };
		TestEqual("the minion, not the nearer hero", ArenaBot::Decide(S).TargetId, 2);
		S.Enemies[0].bAggro = true;
		TestEqual("the hero once it hits an allied hero", ArenaBot::Decide(S).TargetId, 1);
		S.Enemies.RemoveAt(1); S.Enemies[0].bAggro = false;
		TestEqual("a hero alone is still fought", ArenaBot::Decide(S).TargetId, 1);
	});
	It("minions march on the enemy portal when nothing is near", [this]()
	{
		ArenaBot::FSnapshot S; S.bMinion = true; S.EnemyPortal = FVector(5000, 0, 0);
		TestEqual("portal", ArenaBot::Decide(S).MoveTo, S.EnemyPortal);
	});
}

#endif
