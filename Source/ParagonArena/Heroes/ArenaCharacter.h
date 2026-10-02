#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpecHandle.h"
#include "Data/ArenaTypes.h"
#include "Core/ArenaCore.h"
#include "ArenaCharacter.generated.h"

class UAbilitySystemComponent;
class UArenaAttributeSet;
class USpringArmComponent;
class UCameraComponent;
class UAnimMontage;

/** How a blow lands: who threw it and what rides on it (crits and item passives key off this). */
struct FArenaHit
{
	bool bUltimate = false;
	bool bBasic = false;        // a basic attack (thorns, on-hit and crit apply)
	bool bCrit = false;
	float CritMult = 1.75f;
	float ArmorPen = 0.f;
	bool bReflect = false;      // thorns: reflected damage is not reflected again
	FString Ability;            // for the death recap
};

/** LAN (v16): which definition a unit was made from; a client builds the body from it. */
USTRUCT()
struct FArenaNetSetup
{
	GENERATED_BODY()

	UPROPERTY() uint8 Kind = 0;      // 1 hero, 2 melee minion, 3 ranged, 4 siege, 5 super, 6 tower, 7 inhibitor, 8 core, 9 camp monster
	UPROPERTY() int16 Index = -1;    // the hero or the camp
	UPROPERTY() int8 Team = -1;
	UPROPERTY() uint8 Level = 1;
	UPROPERTY() int8 Skin = -1;
	UPROPERTY() float Boost = 0.f;
	UPROPERTY() int8 Lane = -1;
	UPROPERTY() uint8 Tier = 0;
};

/** LAN: what every player sees of a unit (its bars, its states). */
USTRUCT()
struct FArenaNetVitals
{
	GENERATED_BODY()

	UPROPERTY() float Health = 0.f;
	UPROPERTY() float MaxHealth = 0.f;
	UPROPERTY() float Mana = 0.f;
	UPROPERTY() float MaxMana = 0.f;
	UPROPERTY() float Shield = 0.f;
	UPROPERTY() float Power = 0.f;
	UPROPERTY() float Armor = 0.f;
	UPROPERTY() float MoveSpeed = 0.f;
	UPROPERTY() uint8 Level = 1;
	UPROPERTY() bool bInvulnerable = false;
	UPROPERTY() bool bDead = false;
	UPROPERTY() float StunLeft = 0.f;
	UPROPERTY() float SlowLeft = 0.f;
	UPROPERTY() float SlowPct = 0.f;
	UPROPERTY() float SpeedLeft = 0.f;
	UPROPERTY() float SpeedPct = 0.f;
	UPROPERTY() float CampBuff[3] = { 0.f, 0.f, 0.f };
};

/** LAN: what only the hero's own player needs (its gold, ranks, cooldowns, items). */
USTRUCT()
struct FArenaNetOwner
{
	GENERATED_BODY()

	UPROPERTY() float Gold = 0.f;
	UPROPERTY() float Xp = 0.f;
	UPROPERTY() int32 Ranks[5] = { 1, 0, 0, 0, 0 };
	UPROPERTY() float CdLeft[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	UPROPERTY() float CdLen[5] = { 1.f, 1.f, 1.f, 1.f, 1.f };
	UPROPERTY() int32 Potions[2] = { 0, 0 };
	UPROPERTY() TArray<int32> Items;
	UPROPERTY() float RecallLeft = -1.f;
	UPROPERTY() float PotionLeft[2] = { 0.f, 0.f };
	UPROPERTY() float AtkSpeedLeft = 0.f;
};

/** A hero or a minion. All combat state changes go through this class (VR-01..VR-04). */
UCLASS()
class PARAGONARENA_API AArenaCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AArenaCharacter(const FObjectInitializer& ObjectInitializer);

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }
	virtual void Tick(float DeltaSeconds) override;

	void InitCharacter(const FArenaHeroDef& InDef, int32 InTeam, int32 InLevel, bool bInMinion);

	// ---- queries ---------------------------------------------------------------------------
	bool IsAlive() const { return !bDead; }
	bool IsMinion() const { return bMinion; }
	int32 GetTeam() const { return Team; }
	int32 GetHeroLevel() const { return Level; }
	const FArenaHeroDef& GetDef() const { return Def; }
	float GetHealth() const;
	float GetMaxHealth() const;
	float GetMana() const;
	float GetMaxMana() const;
	float GetShield() const;
	float GetPower() const;
	float GetArmor() const;
	/** Labs: every cooldown ready now. */
	void ResetCooldowns() { for (float& E : CooldownEnd) { E = 0.f; } }
	float HealthPct() const { return GetMaxHealth() > 0.f ? GetHealth() / GetMaxHealth() : 0.f; }
	bool IsStunned() const;
	/** Carried by a knockback or knock-up right now. */
	bool IsDisplaced() const { return GetWorld() && GetWorld()->GetTimeSeconds() < DisplacedUntil; }
	void RefillMana();
	float CooldownRemaining(int32 Slot) const;
	float CooldownTotal(int32 Slot) const;
	bool CanCastSlot(int32 Slot) const;

	// ---- levels and ranks (VR-20) -------------------------------------------------------------
	/** The ability in a slot as it is now: every number at its rank (the basic attack at the hero's level). */
	FArenaAbilityDef Ability(int32 Slot) const;
	int32 GetRank(int32 Slot) const { return Slot >= 0 && Slot < 5 ? Ranks[Slot] : 0; }
	int32 FreeSkillPoints() const;
	bool CanRankUp(int32 Slot) const;
	bool RankUp(int32 Slot);
	/** Spends every free point by the hero's skill order (the ultimate first whenever it can rank). Bots. */
	void AutoRank();
	/** Labs: every ability at this rank, whatever the level. */
	void ForceRanks(int32 Rank);
	void SetRanks(const int32 In[5]);
	const int32* GetRanks() const { return Ranks; }
	/** XP gathered in the current level and the XP the level needs (the HUD's XP bar). */
	float XpIntoLevel() const;
	float XpLevelSpan() const;
	int32 MaxLevel() const;
	// ---- actions ---------------------------------------------------------------------------
	bool TryCast(int32 Slot);
	/** The point the character aims at (set by the player camera or the bot brain). */
	FVector AimPoint = FVector::ZeroVector;
	/** Controlled by the human player: its attacks get aim assistance and its hits are confirmed on screen. */
	bool bAssistedAim = false;
	/** The enemy the player's next attack goes for (picked each frame from the crosshair, see FindAimTarget). */
	TWeakObjectPtr<AArenaCharacter> AimTarget;
	/** Time of the last hit taken (drives the white hit flash on the body). */
	float LastHitFlash = -100.f;

	// ---- combat state changes (all clamped) -------------------------------------------------
	float ReceiveDamage(float Raw, AArenaCharacter* Source, bool bUltimate);
	float ReceiveHit(float Raw, AArenaCharacter* Source, const FArenaHit& Hit);
	/** Knockback / knock-up as a fixed displacement (root motion): the same distance and height every time,
	 *  stopped by walls and ledges; the unit cannot act until it lands. */
	void Displace(const FVector& Direction, float DistanceCm, float HeightCm, float Seconds);
	bool IsAirborne() const { return GetWorld() && GetWorld()->GetTimeSeconds() < DisplacedUntil; }
	float StunRemaining() const { return GetWorld() ? FMath::Max(0.f, StunUntil - GetWorld()->GetTimeSeconds()) : 0.f; }
	float SlowRemaining() const { return GetWorld() ? FMath::Max(0.f, SlowUntil - GetWorld()->GetTimeSeconds()) : 0.f; }
	float GetSlowPct() const { return SlowPct; }
	float SpeedBuffRemaining() const { return GetWorld() ? FMath::Max(0.f, SpeedUntil - GetWorld()->GetTimeSeconds()) : 0.f; }
	float AttackSpeedBuffRemaining() const { return GetWorld() ? FMath::Max(0.f, AtkSpeedUntil - GetWorld()->GetTimeSeconds()) : 0.f; }

	// ---- MOBA sustain: recall, fountain, potions, death recap, streak (VR-23) ------------------------------
	/** B away from the base: a channel of RecallSeconds, then home. Moving, casting, damage or a stun break it. */
	bool StartRecall();
	void CancelRecall(const TCHAR* Why);
	bool IsRecalling() const { return RecallStart >= 0.f; }
	float RecallProgress() const;
	/** Potions carried (0 health, 1 mana) and drinking one: the heal or mana comes over PotionSeconds. */
	int32 Potions[2] = { 0, 0 };
	bool DrinkPotion(int32 Kind);
	float PotionLeft(int32 Kind) const { return GetWorld() && Kind >= 0 && Kind < 2 ? FMath::Max(0.f, PotionUntil[Kind] - GetWorld()->GetTimeSeconds()) : 0.f; }
	/** Blows taken in the last seconds (the death recap). */
	TArray<FArenaDamageEvent> RecentDamage;
	/** Hero kills since the last death (a streak of 3+ carries shutdown gold). */
	int32 Streak = 0;
	bool IsInOwnBase() const;
	bool IsInEnemyBase() const;

	// ---- item passives -------------------------------------------------------------------------
	const ArenaCore::FItemPassives& Passives() const { return ItemPassives; }
	/** Deterministic crit of the next basic attack (see ArenaCore::NextCrit). */
	bool RollCrit();
	/** Hits until the next critical basic attack (0 = no crit chance). */
	int32 HitsToCrit() const;
	/** Sorcerer's Blade: an ability arms the next basic attack; the basic attack uses it up. */
	void ArmSpellblade();
	bool ConsumeSpellblade();
	bool IsSpellbladeArmed() const { return GetWorld() && GetWorld()->GetTimeSeconds() < SpellbladeUntil; }
	float LastStandReadyIn() const { return GetWorld() ? FMath::Max(0.f, LastStandReadyAt - GetWorld()->GetTimeSeconds()) : 0.f; }
	void ReduceAbilityCooldowns(float Seconds);
	void ReceiveHeal(float Amount, AArenaCharacter* Source, bool bShowNumber = true);
	void ReceiveShield(float Amount);
	void ApplyStun(float Seconds);
	void ApplySlow(float Pct, float Seconds);
	void ApplySpeedBuff(float Pct, float Seconds);
	void ApplyAttackSpeedBuff(float Pct, float Seconds);
	void ApplyPowerBuff(float DamageMult, float BonusSpeedPct, float Seconds);
	float DamageMultiplier() const;
	bool HasPowerBuff() const;
	float PowerBuffRemaining() const { return GetWorld() ? FMath::Max(0.f, PowerUntil - GetWorld()->GetTimeSeconds()) : 0.f; }
	bool IsSlowed() const { return GetWorld() && GetWorld()->GetTimeSeconds() < SlowUntil; }
	bool IsSpedUp() const { return GetWorld() && GetWorld()->GetTimeSeconds() < SpeedUntil; }
	void StartDash(const FVector& Direction, float DistanceCm, const FArenaAbilityDef& Ability, float FinishSpeed = 250.f);
	/** The body keeps this yaw for a moment instead of the camera's (a swing at a picked target faces it). */
	void HoldFacing(float Yaw, float Seconds);
	/** A cast turns the hero fast to its aim and keeps it there for Seconds (then it faces where it runs again). */
	void FaceAim(float Yaw, float Seconds);
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;
	/** A short step into a melee swing (the player's swing closing on its target), no damage of its own. */
	void Lunge(const FVector& Direction, float DistanceCm);
	/** Where a dash of this length along Direction ends: along the navmesh surface (up and down ramps), stopped at
	 *  walls, rocks and ledges. The indicator shows exactly this point. */
	FVector DashDestination(const FVector& Direction, float DistanceCm) const;
	bool IsDashing() const { return DashRemaining > 0.f; }
	void SpendManaAndStartCooldown(int32 Slot, float Cost, float Cooldown);
	void AddXp(float Amount);
	void HitStop(float Seconds);
	void CameraShake(float Seconds, float Strength);
	/** Settings: camera shake on / off for every hero, the player's field of view. */
	static bool bShakeEnabled;
	/** The local player's team: ally / enemy colours are relative to it (LoL / Smite: blue allies, red enemies). */
	static int32 LocalTeam;
	/** The ring on the ground under a hero: green = you, blue = an ally, red = an enemy (the same hero can be on
	 *  both teams, so the body alone does not tell). Called at init and when a player takes the hero. */
	void UpdateTeamRing();
	void SetBaseFov(float Fov) { BaseFov = Fov; }
	/** Bots and minions turn at a rate toward where they look (no yaw snap); the player's hero faces the camera. */
	void UseSmoothTurning(bool bSmooth);
	/** The unit runs its pack anim blueprint (not a death / drop-in / emote played outside it). */
	bool IsOnAnimBlueprint() const;
	/** 0 = the body is fully drawn, 1 = faded away (the death fade; see Tools/make_fade_materials.py). */
	float GetFadeProgress() const { return FadeProgress; }
	bool CanFade() const { return FadeSlots > 0; }
	/** The masked copy of a pack material that can fade (null when none was generated, e.g. translucent eyes). */
	static UMaterialInterface* FadeCopyOf(const UMaterialInterface* Material);
	/** A glowing circle on the ground under the unit for Seconds (a buff or a shield is visible at a glance). */
	void ShowAura(const FLinearColor& Color, float Seconds);
	/** Frames in which a bot or minion turned more than 25 degrees at once (ARENA_SUMMARY yaw_snaps; 0 = smooth). */
	static int32 YawSnaps;
	static int32 YawFrames;

	// ---- reaction animations (library packs; see FArenaHeroDef) -----------------------------------------------
	/** A sequence or montage through the anim blueprint's slot (upper body when it has one, else full body). */
	UAnimMontage* PlaySlotAnim(const FString& Path, bool bUpperBody, float BlendIn = 0.1f, float BlendOut = 0.2f, int32 Loops = 1, float Rate = 1.f);
	FName AnimSlot(bool bUpperBody) const;
	/** Airborne pose while a knockback / knock-up carries the unit; ends on landing. */
	void OnKnocked(const FVector& Direction, bool bUp);
	/** Drop-in landing + light column at every (re)spawn; the unit waits for it to finish. */
	void PlaySpawnIn();
	/** Pre-match pose during the countdown, cut when the fight starts. */
	void PlayIntro();
	void StopIntro();
	/** End screen: the winners celebrate. */
	void PlayVictory();
	bool IsBusy() const { return GetWorld() && GetWorld()->GetTimeSeconds() < BusyUntil; }
	/** Labs: the unit cannot act for a moment (as during its drop-in). */
	void SetBusyFor(float Seconds) { BusyUntil = GetWorld()->GetTimeSeconds() + Seconds; }
	/** Until when this hero draws the enemy minions (it hit one of their heroes: LoL's "call for help"). */
	float MinionAggroUntil = 0.f;
	float AppliedFlash = -1.f;          // the hit-flash value last written to the materials
	/** The player's own hero speaks a pack voice line (<Hero>_<Event>, e.g. Ability_Ultimate_Ready); MinGap keeps it
	 *  from chattering. Other units stay silent (a 5v5 of voice lines was noise). */
	void Voice(const TCHAR* Event, float MinGap = 3.f);
	/** Any hero's voice cue for an event (hero pick screen); null if the pack has none. */
	static class USoundBase* VoiceCue(const FArenaHeroDef& D, const TCHAR* Event);
	float NextVoiceAt = 0.f;
	bool bWasUltCooling = false;
	float StunFxUntil = 0.f, SlowFxUntil = 0.f, SpeedFxUntil = 0.f, ShieldHitFxAt = 0.f;
	TWeakObjectPtr<class UFXSystemComponent> RecallFxComp;

	// ---- Conquest (v14): structures and neutral monsters ---------------------------------------------------------
	/** 1 tower, 2 inhibitor, 3 core (ArenaConquest::EKind; 0 = a unit that moves). A structure stands: no movement,
	 *  no crowd control; a hero hurts it with basic attacks from close only; while the structure before it stands
	 *  it takes no damage at all (bInvulnerable, set by the game mode). */
	int32 StructureKind = 0;
	int32 StructureLane = -1, StructureTier = 0;
	bool IsStructure() const { return StructureKind > 0; }
	/** The neutral camps and the boss (team 2): hostile to both teams, ignored by lane minions and towers. */
	bool IsMonster() const { return Team == 2; }
	bool IsBoss() const { return Team == 2 && Def.Class == TEXT("Boss"); }
	/** A lane minion (not a structure, not a monster). */
	bool IsLaneMinion() const { return bMinion && StructureKind == 0 && Team != 2; }
	bool bInvulnerable = false;
	/** A tower's consecutive shots on the same hero (each one hits harder, LoL). */
	int32 TowerRamp = 0;
	void MakeStructure(int32 Kind, int32 Lane, int32 Tier);
	/** A shooting structure's range painted on the ground (the local player's view; red: it shoots you). */
	void ShowRangeRing(bool bShow, const FLinearColor& Color, float Opacity);
	/** Camp buffs on a hero: 0 red (damage), 1 black (mana regeneration, cooldowns), 2 the boss (damage, speed). */
	float CampBuffUntil[3] = { 0.f, 0.f, 0.f };
	bool HasCampBuff(int32 Kind) const { return GetWorld() && Kind >= 0 && Kind < 3 && GetWorld()->GetTimeSeconds() < CampBuffUntil[Kind]; }
	float CampBuffLeft(int32 Kind) const { return HasCampBuff(Kind) ? CampBuffUntil[Kind] - GetWorld()->GetTimeSeconds() : 0.f; }
	void ApplyCampBuff(int32 Kind, float Seconds);
	/** A monster walking back to its camp: it takes no damage and heals (LoL leash). */
	bool bResetting = false;
	/** For tower shots: 0 melee, 1 ranged, 2 siege, 3 super. */
	int32 MinionKind() const;
	/** The time a hero last hit an enemy hero (a tower answers it: "call for help"). */
	float LastHitHeroAt = -100.f;
	// ---- LAN (v16): the other machines' view of this unit ---------------------------------------------------------
	/** Set by the spawner on the server (see FArenaNetSetup). */
	void SetNetSetup(uint8 Kind, int32 Index, int32 Skin = -1, float Boost = 0.f);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** This machine's own player's hero (a LAN client's pawn, or the host's). */
	bool IsLocalHero() const;
	/** A cast seen by the other machines: the animation, the cast effect and the flight / area / blow drawn there. */
	UFUNCTION(NetMulticast, Unreliable) void MulticastCast(uint8 Slot, uint8 Variant, FVector_NetQuantize Aim, AArenaCharacter* Target);
	UFUNCTION(NetMulticast, Unreliable) void MulticastHit(float Dmg, bool bCrit, AArenaCharacter* Source);
	UFUNCTION(NetMulticast, Reliable) void MulticastDie(AArenaCharacter* Killer, int32 Pick);
	/** 0 stun, 1 slow, 2 haste, 3 shield, 4 level up, 5 recall, 6 recall over, 7 mana potion. */
	UFUNCTION(NetMulticast, Unreliable) void MulticastStatus(uint8 Kind, float Seconds);
	UFUNCTION(NetMulticast, Unreliable) void MulticastSpawnIn();
	UFUNCTION(Client, Unreliable) void ClientVoice(FName Event);
	void NetStatus(uint8 Kind, float Seconds) { if (HasAuthority() && GetNetMode() != NM_Standalone) { MulticastStatus(Kind, Seconds); } }
	/** A structure's target, for its range ring on the clients. */
	AArenaCharacter* TowerTarget() const { return HasAuthority() ? AimTarget.Get() : NetTarget.Get(); }
	/** Friendly-fire guard used by every damage/CC path (VR-04). */
	bool IsHostileTo(const AArenaCharacter* Other) const;

	float LastDamageTime = -100.f;
	TWeakObjectPtr<AArenaCharacter> LastAttacker;
	int32 Kills = 0, Deaths = 0, Assists = 0, CastCount = 0;
	int32 MinionKills = 0;
	float DamageToHeroes = 0.f, DamageTaken = 0.f, HealingDone = 0.f;   // the post-match numbers
	int32 SlotCasts[5] = { 0, 0, 0, 0, 0 };
	int32 HeroIndex = -1;
	float LastKillTime = -100.f;
	int32 KillStreakWindow = 0;
	float Xp = 0.f;
	int32 PlayerIndex = -1; // >= 0 for the human player's hero

	// ---- gold and items (VR-12) ---------------------------------------------------------------
	float Gold = 0.f;
	TArray<int32> Items;                                   // indices into FArenaDatabase::Get().Items
	/** Replaces the inventory and re-applies the stats (health keeps its percentage). */
	void SetItems(const TArray<int32>& InItems);
	/** Full health and mana (a fresh respawn). */
	void RefillVitals() { ApplyLevelStats(true); ClearShield(); }
	/** Labs: the ordinary death, no killer (a duel over; destroying a hero mid-ability crashed the engine). */
	void LabKill() { if (!bDead) { Die(nullptr, INDEX_NONE); } }
	int32 NetSetupSkin() const { return NetSetup.Skin; }
	void ClearShield();
	const ArenaCore::FItemStats& ItemBonus() const { return ItemStats; }

	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> SpringArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

protected:
	virtual void BeginPlay() override;
	/** ForcedPick: a client plays the death the server chose (-2: choose here). */
	void Die(AArenaCharacter* Killer, int32 ForcedPick = -2);
	UPROPERTY(ReplicatedUsing = OnRep_Setup) FArenaNetSetup NetSetup;
	UPROPERTY(ReplicatedUsing = OnRep_Vitals) FArenaNetVitals NetVitals;
	UPROPERTY(ReplicatedUsing = OnRep_NetOwner) FArenaNetOwner NetOwner;
	UPROPERTY(Replicated) TObjectPtr<AArenaCharacter> NetTarget;
	UFUNCTION() void OnRep_Setup();
	UFUNCTION() void OnRep_Vitals();
	UFUNCTION() void OnRep_NetOwner();
	void WriteNetState();
	float NextNetWrite = 0.f;
	bool bNetInit = false;
	bool bNetSeenAlive = false;   // a LAN client saw this unit alive (its death then comes with MulticastDie)
	float NextNetPain = 0.f;
	void ApplyLevelStats(bool bRefill);
	void UpdateMoveSpeed();
	void TickDash(float DeltaSeconds);
	void SlideOffCharacters();
	void DebugDumpAnim() const;   // -ArenaAnimDebug: the anim blueprint's own variables and their values
	void PlayHitReact(const AArenaCharacter* Source);
	int32 ChooseDeathAnim(const AArenaCharacter* Killer);
	float PlaySingleNode(const FString& Path, bool bLoop, float BlendTime = 0.2f);   // bypasses the anim blueprint (death, drop-in, emote), blending in from the current pose
	void ApplyFadeMaterials();
	void RestoreAnimBlueprint();
	/** A native anim instance (the minions') gets its clips after every anim class switch. */
	void SetupNativeAnim();
	void TickReactions(float Now);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> ASC;
	UPROPERTY() TObjectPtr<UArenaAttributeSet> Attributes;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> TintMaterials;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FlashMaterial;   // overlay: the whole body flashes white when hit
	void TickHitFlash(float Now);
	float NextHitTick = 0.f;
	float HeldYaw = 0.f, HoldFacingUntil = -1.f;
	float FaceAimYaw = 0.f, FaceAimUntil = -1.f;
	/** v19: the relaxed idle out of combat (see TickRelax). */
	float LastCombatAt = -100.f, StandingSince = -1.f;
	int32 RelaxCastsSeen = 0;
	TWeakObjectPtr<class UAnimMontage> RelaxMontage;
	TWeakObjectPtr<class UAnimSequenceBase> RelaxAnim;
	void TickRelax(float Now);
	/** Heroes: which way the body turns this frame (a held swing, a cast's aim, or where it runs). */
	void UpdateFacing(float Now, float Dt);
	int32 Ranks[5] = { 1, 0, 0, 0, 0 };          // slot 0 (basic attack) is always 1
	ArenaCore::FItemPassives ItemPassives;
	float CritAccum = 0.f;
	float SpellbladeUntil = 0.f, SpellbladeReadyAt = 0.f, LastStandReadyAt = 0.f;
	float DisplacedUntil = 0.f;
	float RecallStart = -1.f;
public:
	/** A ranged hero's short slow after a basic shot (the anti-kite rule); also set on a LAN client at its request. */
	float FireSlowUntil = -1.f;
	/** Hard bots' edge (FArenaRulesDef::HardBotPower / HardBotHealth); 0 for players. */
	void SetBotEdge(float InPower, float InHealth) { EdgePower = InPower; EdgeHealth = InHealth; ApplyLevelStats(true); }
	bool IsRangedKit() const { return Def.Abilities.IsValidIndex(0) && Def.Abilities[0].Range >= 5.f; }
	/** A hero (not a minion, monster or structure) with a melee or a ranged kit (the v21 melee trait). */
	bool IsMeleeHero() const { return !bMinion && !IsStructure() && Team != 2 && !IsRangedKit(); }
	bool IsRangedHero() const { return !bMinion && !IsStructure() && Team != 2 && IsRangedKit(); }
	/** v21: a melee hero's dash shield (top-up, taken back when it runs out). */
	void GrantDashShield();
protected:
	float EdgePower = 0.f, EdgeHealth = 0.f;
	FVector RecallFrom = FVector::ZeroVector;
	float PotionUntil[2] = { 0.f, 0.f }, PotionRate[2] = { 0.f, 0.f };
	float NextFountainHit = 0.f;
	void TickSustain(float Now, float Dt);
	uint16 DisplaceRootMotion = 0;

	FArenaHeroDef Def;
	ArenaCore::FItemStats ItemStats;                       // summed and capped item bonuses
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;
	float CooldownEnd[5] = { 0, 0, 0, 0, 0 };
	float CooldownLen[5] = { 1, 1, 1, 1, 1 };
	int32 Team = -1;
	int32 Level = 1;
	bool bMinion = false;
	bool bDead = false;

	float StunUntil = 0.f, SlowUntil = 0.f, SlowPct = 0.f, SpeedUntil = 0.f, SpeedPct = 0.f;
	float AtkSpeedUntil = 0.f, AtkSpeedPct = 0.f, HitFlashUntil = 0.f;
	float PowerUntil = 0.f, PowerMult = 1.f;
	float ShakeUntil = 0.f, ShakeStrength = 0.f, NextPainSound = 0.f;
	FVector BaseSocketOffset = FVector::ZeroVector;
	float NextAnimDebug = 0.f;
	float NextHitStop = 0.f;
	float PerchedSince = -1.f;               // falling onto a unit's head since (see SlideOffCharacters)
	float BusyUntil = 0.f;                   // drop-in / victory: no moving or casting
	float NextHitReact = 0.f, KnockStart = -1.f, SinkAt = -1.f, VanishAt = -1.f;
	float FadeAt = -1.f, FadeLen = 1.2f, FadeProgress = 0.f;   // the body fades from FadeAt over FadeLen seconds
	int32 FadeSlots = 0;                     // material slots drawn by a fade copy
	TArray<int32> HideOnFade;                // slots without one (translucent eyes, tear lines): hidden when the fade starts
	float DeathYawOffset = 0.f;              // the body turns to its killer over a few frames, not at once
	UPROPERTY() TObjectPtr<class UDecalComponent> AuraDecal;
	UPROPERTY() TObjectPtr<class UBoxComponent> NavBlock;           // a structure: the lanes' paths go round it
	UPROPERTY() TObjectPtr<class UDecalComponent> RangeRing;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> StaticBody;  // a structure drawn as a static mesh (the core)
	TWeakObjectPtr<class UFXSystemComponent> CampBuffFx[3];
	float StructureHintAt = -100.f;
	UPROPERTY() TObjectPtr<class UDecalComponent> TeamRing;
	float AuraUntil = 0.f;
	float LastYaw = 0.f;
	bool bYawSeen = false;
	float RagdollAt = -1.f;                  // a minion's stagger turns into a limp fall at this time
	FVector RagdollPush = FVector::ZeroVector;
	bool bRagdoll = false;
	bool bRagdollSinking = false;   // v18: a ragdoll sinks through the ground (a simulated mesh cannot be moved)
	void StartRagdoll();
	float BaseFov = 95.f;
	UPROPERTY() TSubclassOf<UAnimInstance> AbpClass;   // the pack anim blueprint, restored after a single animation
	TWeakObjectPtr<UAnimMontage> StunMontage, KnockMontage, IntroMontage;
	FTimerHandle RestoreAbpTimer, StunLoopTimer;

	// dash (a MoveToForce root-motion source: the movement component carries it with full collision)
	FVector DashDir = FVector::ZeroVector;
	float DashRemaining = 0.f;
	float DashEndTime = 0.f;
	uint16 DashRootMotion = 0;
	FArenaAbilityDef DashAbility;
	TSet<uint64> DashHits;
};
