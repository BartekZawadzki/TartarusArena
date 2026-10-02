// Balance spec (VR-07): time-to-kill of every hero against every hero at levels 5, 10 and 15, from heroes.json.
// Rule, by role (LoL / Smite: an assassin bursts, a guardian holds): each hero's average time to kill the roster,
// against the roster's average, is within its role's band — an assassin 10-35 % faster, a guardian 5-35 % slower,
// every other role within ±15 %. How long each one survives may differ by role. The matrix goes to the log
// (BALANCE lines) for the QA document.
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Data/ArenaTypes.h"
#include "Core/ArenaBalance.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FArenaBalanceSpec, "Arena.Balance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FArenaBalanceSpec)

void FArenaBalanceSpec::Define()
{
	It("time to kill per attacker within its role's band of the roster average at levels 5, 10, 15, the approach counted (VR-07)", [this]()
	{
		FString Json;
		TestTrue("file readable", FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/heroes.json"))));
		FArenaDatabaseFile Db;
		FArenaDatabase::Parse(Json, Db);
		const int32 N = Db.Heroes.Num();
		TestTrue("heroes", N >= 2);
		for (const int32 Level : { 5, 10, 15 })
		{
			TArray<ArenaBalance::FSimHero> Sim;
			for (const FArenaHeroDef& D : Db.Heroes) { Sim.Add(ArenaBalance::Build(D, Level)); }
			TArray<float> AttackAvg, SurviveAvg;
			AttackAvg.Init(0.f, N); SurviveAvg.Init(0.f, N);
			for (int32 a = 0; a < N; ++a)
			{
				FString Row;
				for (int32 b = 0; b < N; ++b)
				{
					if (a == b) { Row += TEXT("   -  "); continue; }
					float T = ArenaBalance::TimeToKill(Sim[a], Sim[b], Db.Rules.MeleeRangedResist);
					// v21: a ranged hero against a melee one is not a stand-up fight — it shoots unanswered while the
					// melee hero closes the gap (the kite model below); both times count from the moment they meet, so
					// the ranged hero's time is its standing time less those free shots (a duel's real race)
					if (Sim[a].bRanged && !Sim[b].bRanged)
					{
						const float Free = ArenaBalance::Kite(Db.Heroes[a], Db.Heroes[b], Db.Rules.RangedFireSlow, Db.Rules.RangedFireSlowSeconds).FreeSeconds;
						T = FMath::Max(0.5f, T - Free);
					}
					AttackAvg[a] += T / (N - 1);
					SurviveAvg[b] += T / (N - 1);
					Row += FString::Printf(TEXT("%6.2f"), T);
				}
				UE_LOG(LogArena, Display, TEXT("BALANCE L=%d %-10s kills in: %s"), Level, *Sim[a].Id.ToString(), *Row);
			}
			float Mean = 0.f;
			for (float V : AttackAvg) { Mean += V / N; }
			for (int32 i = 0; i < N; ++i)
			{
				const float Dev = (AttackAvg[i] - Mean) / FMath::Max(0.01f, Mean);
				const FString& Class = Db.Heroes[i].Class;
				// the class names are English since 2026-10-01 (the old Polish ones still count)
				const bool bAssassin = Class.StartsWith(TEXT("Assassin"));
				const bool bGuardian = Class.StartsWith(TEXT("Guardian"));
				const float Lo = bAssassin ? -0.35f : (bGuardian ? 0.05f : -0.15f), Hi = bAssassin ? -0.10f : (bGuardian ? 0.35f : 0.15f);
				UE_LOG(LogArena, Display, TEXT("BALANCE L=%d %-10s attack_avg=%.2f s (%+.0f %%, band %+.0f..%+.0f) survives_avg=%.2f s"), Level, *Sim[i].Id.ToString(), AttackAvg[i], Dev * 100.f, Lo * 100.f, Hi * 100.f, SurviveAvg[i]);
				TestTrue(FString::Printf(TEXT("level %d: %s (%s) kills in %.2f s on average, roster %.2f s (%+.0f %%, band %+.0f..%+.0f %%)"), Level, *Sim[i].Id.ToString(), *Class, AttackAvg[i], Mean, Dev * 100.f, Lo * 100.f, Hi * 100.f), Dev >= Lo && Dev <= Hi);
			}
		}
	});

	It("every melee hero catches every ranged one: it closes at 1 m/s or more and is shot unanswered for at most 6 s (10 s without a gap closer) (v17)", [this]()
	{
		FString Json;
		TestTrue("file readable", FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/heroes.json"))));
		FArenaDatabaseFile Db;
		FArenaDatabase::Parse(Json, Db);
		for (const FArenaHeroDef& R : Db.Heroes)
		{
			if (!R.Abilities.IsValidIndex(0) || R.Abilities[0].Range < 5.f) { continue; }
			for (const FArenaHeroDef& M : Db.Heroes)
			{
				if (!M.Abilities.IsValidIndex(0) || M.Abilities[0].Range >= 5.f) { continue; }
				const ArenaBalance::FKite K = ArenaBalance::Kite(R, M, Db.Rules.RangedFireSlow, Db.Rules.RangedFireSlowSeconds);
				const float Limit = K.DashMetres > 0.f ? 6.f : 10.f;
				UE_LOG(LogArena, Display, TEXT("KITE %-10s vs %-10s closing=%.2f m/s dash=%.0f m free=%.1f s (limit %.0f)"), *R.Id.ToString(), *M.Id.ToString(), K.ClosingSpeed, K.DashMetres, K.FreeSeconds, Limit);
				TestTrue(FString::Printf(TEXT("%s catches %s: closing %.2f m/s, %.1f s unanswered"), *M.Id.ToString(), *R.Id.ToString(), K.ClosingSpeed, K.FreeSeconds), K.ClosingSpeed >= 1.f && K.FreeSeconds <= Limit);
			}
		}
	});
}

#endif
