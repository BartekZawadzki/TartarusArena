// Data contracts for heroes, abilities and match rules. Authority: Content/Data/heroes.json and
// Content/Data/arena_rules.json (copies of 01-game-design.md §2-§3). Parsed with FJsonObjectConverter,
// so the JSON keys are the property names below (camelCase keys match case-insensitively).
#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Core/ArenaCore.h"
#include "ArenaTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogArena, Log, All);

namespace ArenaTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Stunned);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Slowed);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Casting);
}

UENUM()
enum class EArenaArchetype : uint8 { Melee, Projectile, GroundAoE, Dash, Buff, Targeted };   // Targeted: a homing shot at the picked unit (towers, v14)

UENUM()
enum class EArenaBuffTarget : uint8 { Self, AlliesInRadius, LowestAlly };

USTRUCT()
struct FArenaAbilityDef
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() EArenaArchetype Archetype = EArenaArchetype::Melee;
	UPROPERTY() FString Anim;            // AnimMontage or AnimSequence soft path
	UPROPERTY() FString Fx;              // Niagara (or Cascade) system soft path, spawned on impact
	UPROPERTY() FString CastFx;          // spawned on the caster when the ability fires
	UPROPERTY() FString TrailFx;         // projectile trail (replaces the sphere) / dash trail / ground-AoE telegraph
	UPROPERTY() float FxScale = 0.f;     // size of the impact / blast effect; 0 = from the radius, capped (a pack effect scaled x3 blinded the screen)
	UPROPERTY() FString Sound;           // SoundBase played on cast
	UPROPERTY() FString ImpactSound;     // SoundBase played on impact
	UPROPERTY() TArray<FString> AnimVariants; // cycled instead of Anim when present (combo strings)
	UPROPERTY() float Cooldown = 1.f;
	UPROPERTY() float ManaCost = 0.f;
	UPROPERTY() float Damage = 0.f;
	UPROPERTY() float PowerScale = 0.f;
	UPROPERTY() float Range = 3.f;       // metres
	UPROPERTY() float Radius = 0.f;      // metres (AoE / explosion / dash hit radius)
	UPROPERTY() float Angle = 90.f;      // melee cone, degrees
	UPROPERTY() float Delay = 0.25f;     // seconds from cast to effect
	UPROPERTY() int32 Count = 1;         // projectiles
	UPROPERTY() float Spread = 0.f;      // degrees between projectiles
	UPROPERTY() float Speed = 30.f;      // projectile m/s
	UPROPERTY() float Gravity = 0.f;     // projectile gravity scale (grenades)
	UPROPERTY() bool bPierce = false;
	UPROPERTY() float Distance = 0.f;    // dash metres
	UPROPERTY() bool bBackwards = false;
	UPROPERTY() float EndRadius = 0.f;   // dash landing AoE
	UPROPERTY() float EndDamage = 0.f;
	UPROPERTY() float StunSeconds = 0.f;
	UPROPERTY() float Knockback = 0.f;   // m/s horizontal launch on hit
	UPROPERTY() float KnockUp = 0.f;     // m/s vertical launch on hit
	UPROPERTY() float SlowPct = 0.f;
	UPROPERTY() float SlowSeconds = 0.f;
	UPROPERTY() float Heal = 0.f;
	UPROPERTY() float Shield = 0.f;
	UPROPERTY() float SpeedBuffPct = 0.f;
	UPROPERTY() float AttackSpeedBuffPct = 0.f;
	UPROPERTY() float BuffSeconds = 0.f;
	UPROPERTY() EArenaBuffTarget BuffTarget = EArenaBuffTarget::Self;
	UPROPERTY() bool bUltimate = false;
	UPROPERTY() FString Color = TEXT("#FFFFFF");
	UPROPERTY() FString Icon;            // game-icons.net name: /Game/Arena/Icons/T_Icon_<name> (CREDITS.md)
	UPROPERTY() FString Desc;            // one line: what it does (the numbers come from the fields below)
	UPROPERTY() float Width = 0.f;       // projectile width, metres (0 = 0.52); the shot lane shows exactly this width
	// ranks 1..5 (MOBA): value at rank r = base + perRank * (r - 1)
	UPROPERTY() float DamagePerRank = 0.f;
	UPROPERTY() float EndDamagePerRank = 0.f;
	UPROPERTY() float HealPerRank = 0.f;
	UPROPERTY() float ShieldPerRank = 0.f;
	UPROPERTY() float StunPerRank = 0.f;
	UPROPERTY() float SlowPerRank = 0.f;
	UPROPERTY() float BuffPerRank = 0.f;       // seconds
	UPROPERTY() float CooldownPerRank = 0.f;   // negative: shorter
	UPROPERTY() float ManaPerRank = 0.f;
	// runtime only (not in the JSON): the slot and rank this copy was made for, see AArenaCharacter::Ability
	int32 Slot = -1;
	int32 Rank = 1;
};

USTRUCT()
struct FArenaHeroDef
{
	GENERATED_BODY()

	UPROPERTY() FName Id;
	UPROPERTY() FString DisplayName;
	UPROPERTY() FString Class;
	UPROPERTY() FString Mesh;
	UPROPERTY() FString AnimClass;       // AnimBlueprint generated class path (..._C)
	UPROPERTY() FString MeshAlt;         // the second team's mesh (Paragon minions: Dawn for team 0, Dusk for team 1)
	UPROPERTY() FString IdleAnim;        // native locomotion (UArenaMinionAnimInstance): the idle and the jog
	UPROPERTY() FString RunAnim;
	UPROPERTY() float RunAnimSpeed = 350.f; // cm/s the jog covers at play rate 1, before Scale
	UPROPERTY() float TeamTint = -1.f;   // how much of the team colour tints the body (-1: 0.25 heroes, 0.7 minions; 0: the pack colours)
	UPROPERTY() FString Tint = TEXT("#FFFFFF");
	UPROPERTY() float Scale = 1.f;
	UPROPERTY() float MeshZ = -90.f;     // mesh offset inside the capsule
	UPROPERTY() float MeshYaw = -90.f;
	UPROPERTY() float CapsuleRadius = 0.f; // body width for collision (0 = 40 cm heroes / 34 cm minions, before Scale)
	UPROPERTY() FString PainSound;
	UPROPERTY() FString DeathSound;
	UPROPERTY() FString KillSound;
	UPROPERTY() FString PortraitAnim;    // pose for the HUD portrait (hero-select pose of the Paragon pack)
	// the portrait's framing, for bodies the default misses (a hunched giant whose fists hide the face, a rider on a
	// mount): the bone framed, the camera's offset from it and the point it looks at (cm, x the scale), the key light
	UPROPERTY() FString IdleRelaxed;           // v19: the pack's relaxed idle, played out of combat while standing
	UPROPERTY() FString PortraitPose;          // the portrait's own pose (empty: PortraitAnim, which the browser shows)
	UPROPERTY() FString PortraitBone;          // empty: "head"
	UPROPERTY() TArray<float> PortraitCam;     // empty: 145, -40, 6
	UPROPERTY() TArray<float> PortraitLook;    // empty: 0, 0, -14
	UPROPERTY() float PortraitLight = 1.f;
	UPROPERTY() FString Role;            // one line for the hero browser ("Wojownik pierwszej linii")
	UPROPERTY() FString About;           // two or three sentences: how the hero fights
	UPROPERTY() int32 Difficulty = 1;    // 1 easy .. 3 hard (the browser's dots)
	UPROPERTY() TArray<FString> Tips;    // how to play, one short line each
	UPROPERTY() float MaxHealth = 500.f;
	UPROPERTY() float MaxMana = 250.f;
	UPROPERTY() float Power = 30.f;
	UPROPERTY() float Armor = 20.f;
	UPROPERTY() float MoveSpeed = 6.f;   // m/s
	UPROPERTY() float ManaRegen = 4.f;   // per second
	UPROPERTY() float HealthRegen = 3.f; // per second
	// growth per level above 1 (MOBA): the basic attack grows by BasicPerLevel damage, abilities grow with their ranks
	UPROPERTY() float HealthPerLevel = 0.f;
	UPROPERTY() float ManaPerLevel = 0.f;
	UPROPERTY() float PowerPerLevel = 0.f;
	UPROPERTY() float ArmorPerLevel = 0.f;
	UPROPERTY() float HealthRegenPerLevel = 0.f;
	UPROPERTY() float ManaRegenPerLevel = 0.f;
	UPROPERTY() float BasicPerLevel = 0.f;
	UPROPERTY() TArray<int32> SkillOrder;           // bots spend their points in this order (the ultimate whenever it can rank)
	UPROPERTY() TArray<FArenaAbilityDef> Abilities; // [0]=basic, [1..4]=abilities, 4 = ultimate
	UPROPERTY() TArray<FName> Build;                // item ids the bots buy for this hero, in order

	// ---- reaction animations and effects (all optional; paths into the library packs) ---------------------
	UPROPERTY() TArray<FString> DeathAnims;          // one is picked at death; the body holds its last frame
	UPROPERTY() TArray<FVector2D> DeathFall;         // per death anim: where the pelvis ends, actor space cm (x fwd, y right)
	UPROPERTY() TArray<FString> HitReacts;           // front, back, left, right (non-additive copies, Tools/make_hitreacts.py)
	UPROPERTY() TArray<FString> StunAnims;           // start, loop
	UPROPERTY() TArray<FString> KnockAnims;          // airborne pose: pushed back, pushed forward, launched up
	UPROPERTY() FString RespawnAnim;                 // drop-in landing played at every (re)spawn
	UPROPERTY() FString IntroAnim;                   // upper-body montage during the pre-match countdown
	UPROPERTY() FString VictoryAnim;                 // emote of the winning team on the end screen
	UPROPERTY() FString DeathFx;
	UPROPERTY() FString DeathVanishFx;              // set: the body vanishes in this burst when its death animation ends
	UPROPERTY() FString SpawnFx;
	// skins (v15): the pack's alternative bodies on the same skeleton, with their names
	UPROPERTY() TArray<FString> Skins;
	UPROPERTY() TArray<FString> SkinNames;
	// a structure drawn as a static mesh (the core, v14): shown in place of the skeletal body
	UPROPERTY() FString StaticMesh;
	UPROPERTY() float StaticScale = 1.f;
	UPROPERTY() float StaticZ = 0.f;                // cm, from the capsule's bottom
	UPROPERTY() float CapsuleHalfHeight = 0.f;      // 0 = the default (88 cm before Scale)
	UPROPERTY() float MuzzleZ = 0.f;                // a tower's shots leave at this height above its base (cm)
};

/** A neutral camp of Conquest (v14): monsters of team 2 at a spot; killing them pays, some leave a buff. */
USTRUCT()
struct FArenaCampDef
{
	GENERATED_BODY()

	UPROPERTY() FString Name;
	UPROPERTY() FArenaHeroDef Unit;
	UPROPERTY() int32 Count = 1;
	UPROPERTY() FVector2D Spot = FVector2D::ZeroVector;   // cm, on team A's half; the copy on team B's half is mirrored (x -> -x)
	UPROPERTY() bool bMirror = true;
	UPROPERTY() float Yaw = 0.f;                          // where the monsters face at home
	UPROPERTY() int32 Buff = 0;          // 0 gold and XP only, 1 red (damage), 2 black (mana, cooldowns), 3 the boss (the whole team)
	UPROPERTY() float FirstSpawn = 60.f; // seconds into the match
	UPROPERTY() float Respawn = 120.f;   // after the last monster of the camp fell
	UPROPERTY() int32 Gold = 50;         // to the killer (the boss: to every hero of the team)
	UPROPERTY() float Xp = 60.f;         // to the killer's team within the XP radius
	UPROPERTY() float Leash = 9.f;       // metres from home before a monster gives up, walks back and heals
	UPROPERTY() float LevelPerMinute = 0.34f;
};

/** Conquest (v14): three lanes of towers and inhibitors in front of each team's core, the camps and the boss. */
USTRUCT()
struct FArenaConquestDef
{
	GENERATED_BODY()

	UPROPERTY() FArenaHeroDef Tower;
	UPROPERTY() FArenaHeroDef Inhibitor;
	UPROPERTY() FArenaHeroDef Core;
	UPROPERTY() FArenaHeroDef SiegeMinion;
	UPROPERTY() FArenaHeroDef SuperMinion;
	UPROPERTY() TArray<FArenaCampDef> Camps;
	// where the structures stand: cm along each lane from the team's own end (inhibitor, inner tower, outer tower),
	// TowerSide metres beside it; the core at CoreSpot (team A; team B mirrored, x -> -x)
	UPROPERTY() TArray<float> MidAlong;
	UPROPERTY() TArray<float> SideAlong;
	UPROPERTY() float TowerSide = 3.8f;
	UPROPERTY() FVector2D CoreSpot = FVector2D(-4750.f, 0.f);
	UPROPERTY() float TowerRamp = 0.4f;          // each consecutive shot on the same hero +40 %
	UPROPERTY() float TowerRampMax = 1.2f;
	UPROPERTY() float TowerDamagePerMinute = 6.f;
	UPROPERTY() TArray<float> MinionShotPct;     // a tower shot takes this share of a minion's health: melee, ranged, siege, super
	UPROPERTY() float Reach = 12.f;              // metres: a hero hurts a structure only from this close (a tower's range is 11)
	UPROPERTY() float BackdoorCut = 0.66f;       // damage cut on a structure with none of the attacker's minions near
	UPROPERTY() float MinionOnStructure = 2.5f;  // lane minions hit structures this much harder (a wave alone did ~2k damage in 25 min)
	UPROPERTY() float BackdoorRadius = 14.f;
	UPROPERTY() float InhibitorRespawn = 240.f;
	UPROPERTY() int32 GoldTower = 125;           // each hero of the team
	UPROPERTY() int32 GoldTowerKiller = 100;
	UPROPERTY() int32 GoldInhibitor = 75;
	UPROPERTY() float XpTower = 90.f;
	UPROPERTY() int32 SiegeEvery = 3;
	UPROPERTY() int32 MeleePerLane = 3;
	UPROPERTY() int32 RangedPerLane = 1;
	UPROPERTY() float RedDamage = 0.15f;         // red camp buff: +15 % damage
	UPROPERTY() float BlackManaRegen = 1.f;      // black camp buff: +100 % mana regeneration
	UPROPERTY() float BlackCooldown = 0.15f;     //   and 15 % shorter cooldowns
	UPROPERTY() float BuffSeconds = 90.f;
	UPROPERTY() float BossDamage = 0.2f;         // the boss's team: +20 % damage, +10 % speed
	UPROPERTY() float BossSpeed = 0.1f;
	UPROPERTY() float BossSeconds = 150.f;
	UPROPERTY() float BossMinions = 0.5f;        //   and its waves +50 % health and damage while it lasts
	UPROPERTY() float FountainRadius = 5.5f;     // metres: the fountain burns enemies only close to the spawn
	UPROPERTY() int32 MatchMinutes = 35;         // a safety clock; the core decides
	UPROPERTY() float XpScale = 0.65f;           // a longer match: levels come slower (level 20 by ~25 min, not 15)
	UPROPERTY() TArray<FString> BuffFx;          // carried on a hero: red, black, the boss
	UPROPERTY() FString StructureDeathFx;
	UPROPERTY() FString InvulnerableFx;
};

USTRUCT()
struct FArenaItemDef
{
	GENERATED_BODY()

	UPROPERTY() FName Id;
	UPROPERTY() FString Name;
	UPROPERTY() FString Icon;           // game-icons.net name: /Game/Arena/Icons/T_Icon_<name> (CREDITS.md)
	UPROPERTY() int32 Tier = 1;         // 1 component, 2 upgrade, 3 finished item with a unique passive
	UPROPERTY() TArray<FName> From;     // components; Cost is the recipe gold on top of them (the full price adds them up)
	UPROPERTY() FString Color = TEXT("#E8C060");
	UPROPERTY() int32 Cost = 0;
	UPROPERTY() float Power = 0.f;
	UPROPERTY() float Armor = 0.f;
	UPROPERTY() float Health = 0.f;
	UPROPERTY() float Mana = 0.f;
	UPROPERTY() float HealthRegen = 0.f;
	UPROPERTY() float ManaRegen = 0.f;
	UPROPERTY() float MoveSpeedPct = 0.f;
	UPROPERTY() float CooldownPct = 0.f;
	UPROPERTY() float AttackSpeedPct = 0.f;
	UPROPERTY() float LifestealPct = 0.f;
	UPROPERTY() float CritChance = 0.f;   // basic attacks; deterministic: 25 % = every 4th basic attack
	UPROPERTY() float ArmorPen = 0.f;     // flat armour ignored
	UPROPERTY() FString Passive;          // execute, infinity, overheal, onhit, spellblade, archon, echo, thorns, laststand, regen
	UPROPERTY() float PassiveA = 0.f;
	UPROPERTY() float PassiveB = 0.f;
	UPROPERTY() float PassiveCd = 0.f;
	UPROPERTY() FString PassiveText;
};

/** One blow taken, for the death recap (who, with what, how much). */
struct FArenaDamageEvent
{
	FString Source;
	FString Ability;
	float Amount = 0.f;
	float Time = 0.f;
	int32 SourceTeam = -1;   // the attacker's team and hero index (-1: a minion, a tower, the fountain): assists by contribution
	int32 SourceHero = -1;
};

USTRUCT()
struct FArenaRulesDef
{
	GENERATED_BODY()

	UPROPERTY() int32 ScoreHeroKill = 5;        // team points (01 §3, VR-05)
	UPROPERTY() int32 ScoreMinionKill = 1;
	UPROPERTY() int32 ScoreMinionBase = 2;      // a minion that walks into the enemy base
	UPROPERTY() int32 ScorePerMinute = 30;      // score limit = minutes on the clock x this
	UPROPERTY() int32 MatchMinutes = 10;        // default length; 5 / 10 / 15 on the hero-select screen
	UPROPERTY() int32 StartGold = 600;         // VR-12
	UPROPERTY() float GoldPerSecond = 3.f;
	UPROPERTY() int32 GoldHeroKill = 300;
	UPROPERTY() int32 GoldAssist = 150;
	UPROPERTY() int32 GoldMinion = 20;
	UPROPERTY() int32 ReviveBaseCost = 150;
	UPROPERTY() int32 RevivePerLevel = 50;
	UPROPERTY() float ReviveCooldown = 120.f;
	UPROPERTY() float WaveInterval = 30.f;
	UPROPERTY() int32 MeleeMinionsPerWave = 3;
	UPROPERTY() int32 RangedMinionsPerWave = 1;
	UPROPERTY() int32 MaxLevel = 20;
	UPROPERTY() float XpBase = 110.f;           // XP from level L to L+1 = XpBase + XpGrowth * (L - 1)
	UPROPERTY() float XpGrowth = 35.f;
	UPROPERTY() float XpPerSecond = 4.f;        // every living hero
	UPROPERTY() float XpMinion = 28.f;          // to every hero of the killing team within XpShareRadius
	UPROPERTY() float XpShareRadius = 15.f;     // metres
	UPROPERTY() float XpHeroKill = 110.f;       // + XpHeroKillPerLevel x the victim's level
	UPROPERTY() float XpHeroKillPerLevel = 18.f;
	UPROPERTY() float XpAssist = 0.6f;          // share of the kill XP for each assisting hero
	// MOBA sustain: recall, the fountain, potions, shutdown gold
	// v17 (operator: "ranged heroes are far too strong against melee"): a ranged hero walks this much slower for a
	// moment after each basic shot (Smite's basic attack movement penalty) — it can no longer shoot and back away at
	// full speed forever
	UPROPERTY() float RangedFireSlow = 0.35f;
	UPROPERTY() float RangedFireSlowSeconds = 0.6f;
	// a melee hero's basic attack heals it for this share of the damage (it fights in the enemy's face: 1v1 duels
	// showed melee heroes timing out at a third of their health against a healthy ranged one)
	UPROPERTY() float MeleeLifesteal = 0.05f;
	// v21 (operator: "the melee heroes must be much stronger — tougher — to stand level with the ranged ones, and have
	// more fitting abilities: the dissonance is too big"; 5v5 bot matches: melee heroes dealt 31 % less damage and scored
	// a third fewer kills): the melee trait, every hero whose basic attack reaches under 5 m
	UPROPERTY() float MeleeRangedResist = 0.f;     // less damage taken from ranged heroes (share)
	UPROPERTY() float MeleeTenacity = 0.f;         // shorter stuns and slows (share of their duration)
	UPROPERTY() float MeleeDashShieldPct = 0.f;    // a dash ability gives a shield of this share of max health...
	UPROPERTY() float MeleeDashShieldSeconds = 3.f; // ...for this long
	// hard bots (operator: "far too weak even on hard"): an edge in damage and health on top of better play
	UPROPERTY() float HardBotPower = 0.15f;
	UPROPERTY() float HardBotHealth = 0.12f;
	UPROPERTY() float RecallSeconds = 6.f;      // B away from the base: channel, broken by moving, casting or damage
	UPROPERTY() float BaseRadius = 11.f;        // metres: shop and fountain area around the base centre
	UPROPERTY() float FountainHealPct = 0.12f;  // of max health and mana per second at your own fountain
	UPROPERTY() float FountainDamagePct = 0.25f;// of max health per second to an enemy hero at your fountain (true damage)
	UPROPERTY() int32 PotionCost = 50;
	UPROPERTY() float PotionHeal = 220.f;       // health over PotionSeconds
	UPROPERTY() float PotionMana = 160.f;       // mana over PotionSeconds
	UPROPERTY() float PotionSeconds = 8.f;
	UPROPERTY() int32 PotionMax = 5;            // of each kind
	UPROPERTY() int32 ShutdownPerKill = 100;    // bounty on a hero with 3+ kills since its last death
	UPROPERTY() int32 ShutdownMax = 400;
	UPROPERTY() FString RespawnFx;              // light column at the base when any hero (re)spawns
	UPROPERTY() FString LevelUpFx;
	// states on a unit, from Paragon: Minions SharedGameplay (v13): above the head while stunned, at the feet while
	// slowed or hasted, a flash when a shield comes up or takes a blow, the recall column, a mana potion
	UPROPERTY() FString StunFx;
	UPROPERTY() FString StunStartFx;
	UPROPERTY() FString SlowFx;
	UPROPERTY() FString SpeedFx;
	UPROPERTY() FString ShieldFx;
	UPROPERTY() FString ShieldHitFx;
	UPROPERTY() FString RecallFx;
	UPROPERTY() FString ManaFx;
	/** Voice events of the player's own hero (the pack's cues <Hero>_<Event>, next to its Effort_Pain cue). */
	UPROPERTY() TArray<FString> VoiceEvents;
	UPROPERTY() FArenaHeroDef MeleeMinion;
	UPROPERTY() FArenaHeroDef RangedMinion;
	UPROPERTY() FArenaConquestDef Conquest;
};

USTRUCT()
struct FArenaDatabaseFile
{
	GENERATED_BODY()

	UPROPERTY() TArray<FArenaHeroDef> Heroes;
	UPROPERTY() TArray<FArenaItemDef> Items;
	UPROPERTY() FArenaRulesDef Rules;
};

/** Loads and validates Content/Data/heroes.json once. */
struct PARAGONARENA_API FArenaDatabase
{
	static const FArenaDatabaseFile& Get();
	/** Parses JSON text; returns problems (empty = valid). Pure, used by the Arena.Data spec. */
	static TArray<FString> Parse(const FString& Json, FArenaDatabaseFile& Out);
	static FLinearColor Hex(const FString& Hex);
	/** Index of an item id in Items, INDEX_NONE if unknown. */
	static int32 ItemIndex(FName Id);
	/** The item recipes as indices (ArenaCore::BuyPrice / TotalCost), built once. */
	static const TArray<ArenaCore::FRecipe>& Recipes();
	/** Full price of an item (recipe gold plus every component). */
	static int32 ItemTotalCost(int32 ItemIndex);
};
