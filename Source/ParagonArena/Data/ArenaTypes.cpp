#include "Data/ArenaTypes.h"
#include "Game/ArenaEvidence.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogArena);

namespace ArenaTags
{
	UE_DEFINE_GAMEPLAY_TAG(State_Dead, "Arena.State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(State_Stunned, "Arena.State.Stunned");
	UE_DEFINE_GAMEPLAY_TAG(State_Slowed, "Arena.State.Slowed");
	UE_DEFINE_GAMEPLAY_TAG(State_Casting, "Arena.State.Casting");
}

FLinearColor FArenaDatabase::Hex(const FString& Hex)
{
	return FLinearColor(FColor::FromHex(Hex));
}

TArray<FString> FArenaDatabase::Parse(const FString& Json, FArenaDatabaseFile& Out)
{
	TArray<FString> Problems;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Out, 0, 0))
	{
		Problems.Add(TEXT("heroes.json is not valid JSON for FArenaDatabaseFile"));
		return Problems;
	}
	if (Out.Heroes.Num() == 0) { Problems.Add(TEXT("no heroes")); }
	for (const FArenaHeroDef& H : Out.Heroes)
	{
		if (H.Abilities.Num() != 5) { Problems.Add(FString::Printf(TEXT("%s: %d abilities (need basic + 4)"), *H.Id.ToString(), H.Abilities.Num())); }
		if (H.MaxHealth <= 0.f || H.MaxMana < 0.f || H.MoveSpeed <= 0.f) { Problems.Add(FString::Printf(TEXT("%s: bad base stats"), *H.Id.ToString())); }
		for (int32 i = 0; i < H.Abilities.Num(); ++i)
		{
			const FArenaAbilityDef& A = H.Abilities[i];
			if (A.Damage < 0.f || A.Cooldown < 0.f || A.ManaCost < 0.f || A.Heal < 0.f || A.Shield < 0.f)
			{
				Problems.Add(FString::Printf(TEXT("%s[%d] %s: negative number (VR-01/VR-02)"), *H.Id.ToString(), i, *A.Name));
			}
			if (i >= 1 && i <= 3 && (A.Cooldown < 4.f || A.Cooldown > 20.f)) { Problems.Add(FString::Printf(TEXT("%s[%d] cooldown %.1f outside 4..20 s"), *H.Id.ToString(), i, A.Cooldown)); }
			if (i == 4 && (A.Cooldown < 45.f || A.Cooldown > 120.f)) { Problems.Add(FString::Printf(TEXT("%s ultimate cooldown %.1f outside 45..120 s"), *H.Id.ToString(), A.Cooldown)); }
		}
	}
	if (Out.Rules.ScorePerMinute <= 0 || Out.Rules.MatchMinutes <= 0) { Problems.Add(TEXT("rules: scorePerMinute and matchMinutes must be > 0")); }
	if (Out.Rules.StartGold < 0 || Out.Rules.GoldPerSecond < 0.f || Out.Rules.ReviveCooldown < 0.f) { Problems.Add(TEXT("rules: negative gold or revive numbers (VR-12)")); }
	TSet<FName> ItemIds;
	for (const FArenaItemDef& I : Out.Items)
	{
		if (I.Id.IsNone() || ItemIds.Contains(I.Id)) { Problems.Add(FString::Printf(TEXT("item %s: missing or duplicate id"), *I.Name)); }
		ItemIds.Add(I.Id);
		if (I.Tier < 1 || I.Tier > 3) { Problems.Add(FString::Printf(TEXT("item %s: tier must be 1..3"), *I.Id.ToString())); }
		if (I.Cost <= 0 || I.CritChance < 0.f || I.ArmorPen < 0.f || I.Power < 0.f || I.Armor < 0.f || I.Health < 0.f || I.Mana < 0.f || I.MoveSpeedPct < 0.f || I.CooldownPct < 0.f || I.AttackSpeedPct < 0.f || I.LifestealPct < 0.f)
		{
			Problems.Add(FString::Printf(TEXT("item %s: cost must be > 0 and stats >= 0 (VR-12)"), *I.Id.ToString()));
		}
	}
	// recipes: every component exists and is a lower tier (no loops)
	for (const FArenaItemDef& I : Out.Items)
	{
		for (const FName& C : I.From)
		{
			const FArenaItemDef* Part = Out.Items.FindByPredicate([&C](const FArenaItemDef& X) { return X.Id == C; });
			if (!Part) { Problems.Add(FString::Printf(TEXT("item %s: component %s does not exist"), *I.Id.ToString(), *C.ToString())); }
			else if (Part->Tier >= I.Tier) { Problems.Add(FString::Printf(TEXT("item %s: component %s must be a lower tier"), *I.Id.ToString(), *C.ToString())); }
		}
	}
	if (Out.Rules.MaxLevel < 2 || Out.Rules.XpBase <= 0.f || Out.Rules.XpGrowth < 0.f) { Problems.Add(TEXT("rules: maxLevel >= 2, xpBase > 0, xpGrowth >= 0")); }
	for (const FArenaHeroDef& H : Out.Heroes)
	{
		for (int32 i = 1; i < H.Abilities.Num(); ++i)
		{
			// at rank 5 nothing may turn negative or free
			const FArenaAbilityDef& A = H.Abilities[i];
			if (A.Cooldown + A.CooldownPerRank * 4.f < 0.5f || A.Damage + A.DamagePerRank * 4.f < 0.f || A.ManaCost + A.ManaPerRank * 4.f < 0.f)
			{
				Problems.Add(FString::Printf(TEXT("%s %s: rank 5 values out of range"), *H.Id.ToString(), *A.Name));
			}
		}
		for (int32 S : H.SkillOrder) { if (S < 1 || S > 3) { Problems.Add(FString::Printf(TEXT("%s: skillOrder holds slots 1..3 (the ultimate ranks by itself)"), *H.Id.ToString())); break; } }
		for (const FName& B : H.Build) { if (!ItemIds.Contains(B)) { Problems.Add(FString::Printf(TEXT("%s: build item %s does not exist"), *H.Id.ToString(), *B.ToString())); } }
	}
	TArray<const FArenaHeroDef*> All;
	for (const FArenaHeroDef& H : Out.Heroes) { All.Add(&H); }
	All.Add(&Out.Rules.MeleeMinion);
	All.Add(&Out.Rules.RangedMinion);
	for (const FArenaHeroDef* H : All)
	{
		if (H->DeathFall.Num() > 0 && H->DeathFall.Num() != H->DeathAnims.Num()) { Problems.Add(FString::Printf(TEXT("%s: deathFall needs one entry per death anim"), *H->Id.ToString())); }
		if (H->HitReacts.Num() > 0 && H->HitReacts.Num() != 4) { Problems.Add(FString::Printf(TEXT("%s: hitReacts must be front, back, left, right"), *H->Id.ToString())); }
		for (const FArenaAbilityDef& A : H->Abilities)
		{
			// launches stay readable: nobody is thrown higher than ~1 m or further than a short stumble
			if (A.KnockUp > 4.5f || A.Knockback > 5.f) { Problems.Add(FString::Printf(TEXT("%s %s: knockUp <= 4.5 and knockback <= 5 m/s"), *H->Id.ToString(), *A.Name)); }
		}
	}
	return Problems;
}

const TArray<ArenaCore::FRecipe>& FArenaDatabase::Recipes()
{
	static TArray<ArenaCore::FRecipe> Cache;
	if (Cache.Num() == 0)
	{
		for (const FArenaItemDef& I : Get().Items)
		{
			ArenaCore::FRecipe R;
			R.Cost = I.Cost;
			for (const FName& C : I.From) { const int32 K = ItemIndex(C); if (K != INDEX_NONE) { R.From.Add(K); } }
			Cache.Add(R);
		}
	}
	return Cache;
}

int32 FArenaDatabase::ItemTotalCost(int32 Index) { return ArenaCore::TotalCost(Index, Recipes()); }

int32 FArenaDatabase::ItemIndex(FName Id)
{
	return Get().Items.IndexOfByPredicate([Id](const FArenaItemDef& I) { return I.Id == Id; });
}

const FArenaDatabaseFile& FArenaDatabase::Get()
{
	static FArenaDatabaseFile Db;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Text;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/heroes.json");
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			ARENA_LOG(LogArena, Error, TEXT("ARENA evt=data_missing path=%s"), *Path);
			return Db;
		}
		for (const FString& P : Parse(Text, Db))
		{
			ARENA_LOG(LogArena, Error, TEXT("ARENA evt=data_invalid %s"), *P);
		}
		ARENA_LOG(LogArena, Display, TEXT("ARENA evt=data_loaded heroes=%d items=%d minutes=%d"), Db.Heroes.Num(), Db.Items.Num(), Db.Rules.MatchMinutes);
	}
	return Db;
}
