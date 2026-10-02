#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/ArenaCore.h"
#include "Data/ArenaTypes.h"
#include "ArenaGameMode.generated.h"

class AArenaCharacter;
class AArenaBotController;

UENUM()
enum class EArenaPhase : uint8 { HeroSelect, Countdown, Playing, Ended };

struct FArenaFeedEntry
{
	FString Text; float Time = 0.f; FLinearColor Color = FLinearColor::White; bool bBig = false;
	// team-relative news ("we lost a tower" / "an enemy tower destroyed"): Text and Color are Team's view, Other and
	// OtherColor the other team's (empty: not shown to them). Each machine picks its own (a LAN guest read the host's)
	int32 Team = -1; FString Other; FLinearColor OtherColor = FLinearColor::White;
	FArenaFeedEntry For(int32 Viewer) const { FArenaFeedEntry E = *this; if (Team >= 0 && Viewer != Team) { E.Text = Other; E.Color = OtherColor; } return E; }
};
struct FArenaKill { float Time = 0.f; int32 KillerHero = -1; int32 KillerTeam = -1; bool bKillerMinion = false; int32 VictimHero = -1; int32 VictimTeam = -1; };
/** A dead hero waiting to come back: everything that survives the death (VR-06, VR-12). */
struct FArenaRespawn
{
	int32 Team = 0; int32 Hero = 0; float At = 0.f;
	int32 Level = 1; float Xp = 0.f; bool bPlayer = false; int32 Ranks[5] = { 1, 0, 0, 0, 0 }; int32 Potions[2] = { 0, 0 };
	FString Killer; TArray<FArenaDamageEvent> Recap;   // the death recap
	TWeakObjectPtr<APlayerController> Owner;          // a human's hero (the host's or a LAN player's) comes back to its controller
	int32 Kills = 0, Deaths = 0, Assists = 0, CastCount = 0; int32 SlotCasts[5] = { 0, 0, 0, 0, 0 };
	int32 MinionKills = 0; float DamageToHeroes = 0.f, DamageTaken = 0.f, HealingDone = 0.f;
	float Gold = 0.f; TArray<int32> Items;
	FTimerHandle Timer;
};

/** Smite-style Arena (01 §3): tickets, minion waves, portals, respawn, XP. The authority for match state. */
UCLASS()
class PARAGONARENA_API AArenaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AArenaGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- LAN (v16) ----------------------------------------------------------------------------------------------
	/** This world's game mode: the server's, or on a LAN client the local mirror (AArenaClientMirror) that the HUD and
	 *  the menus read, filled each frame from the replicated state; the mirror never decides anything. */
	static AArenaGameMode* Get(const UObject* WorldContext);
	bool bClientMirror = false;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	/** A player on another machine: its team and hero (chosen in the lobby, or a bot's taken over when late). */
	struct FHuman { TWeakObjectPtr<APlayerController> PC; int32 Team = 1; int32 Hero = -1; int32 Skin = -1; };
	TArray<FHuman> RemoteHumans;
	void RemotePickHero(APlayerController* PC, int32 Hero, int32 Skin);
	/** The team and hero a player controller plays (the host: team 0 and PlayerHeroIndex). */
	bool HumanSlot(const APlayerController* PC, int32& OutTeam, int32& OutHero) const;
	/** A LAN player's controller that joined mid-match takes over a bot hero of its team. */
	void TakeOverBot(APlayerController* PC, int32 Team);
	/** -ArenaNetHost: hosts a LAN match, waits for a guest (-ArenaNetGuest), plays and checks what came through. */
	bool bNetTestHost = false;
	int32 NetTestStep = 0;
	float NetTestAt = 0.f;
	void TickNetTestHost(float Now);
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override { return nullptr; }

	void PlayerPickHero(int32 Index);
	void OnCharacterDied(AArenaCharacter* Victim, AArenaCharacter* Killer);
	void MinionReachedPortal(AArenaCharacter* Minion, int32 PortalTeam);
	void Announce(AActor* Context, const FString& Text, bool bBig, FLinearColor Color = FLinearColor::White);
	/** News told differently to each team: Text to Team, Other to the rest (empty: only Team hears it). */
	void AnnounceFor(int32 Team, const FString& Text, const FLinearColor& Color, const FString& Other, const FLinearColor& OtherColor, bool bBig);
	FVector TeamBase(int32 Team) const { return Team == 0 ? BaseA : BaseB; }
	void OnOrbTaken(AArenaCharacter* Taker);
	bool OrbLocation(FVector& Out) const;
	/** Waypoints of a minion lane from this team's base towards the enemy (empty if the map has no lanes). */
	TArray<FVector> LanePath(int32 LaneIndex, int32 Team) const;
	int32 NumLanes() const { return Lanes.Num(); }

	EArenaPhase Phase = EArenaPhase::HeroSelect;
	int32 Difficulty = 1;
	int32 PlayerHeroIndex = 0;
	int32 WinnerTeam = -1;
	float PhaseStart = 0.f;
	float MatchStart = 0.f;
	ArenaCore::FScoreLedger Score;
	int32 MatchMinutes = 10;          // chosen on the hero-select screen (5 / 10 / 15)
	int32 TeamSize = 5;               // the mode: 5 (Arena 5v5), 3 (Skirmish), 1 (Duel)
	void SetTeamSize(int32 Size) { TeamSize = FMath::Clamp(Size, 1, 5); }
	// ---- the training centre (ArenaTraining.cpp) ----------------------------------------------------------------
	static bool IsTrainingMap(const UWorld* World);
	/** The Conquest map (Tools/build_conquest.py): Conquest only, the match starts on arrival with the hero picked in the
	 *  menu (?Hero=N). */
	static bool IsConquestMap(const UWorld* World) { return World && World->GetMapName().Contains(TEXT("Conquest")); }
	bool bTraining = false;
	bool bTrainNoCooldowns = false;
	float TrainingDps = 0.f;
	void StartTraining(int32 HeroIndex);
	void TrainingKey(int32 Key);        // 0 cooldowns off/on, 1 level 20, 2 dummies back
	struct FTrainingDummy { TWeakObjectPtr<AArenaCharacter> Unit; FVector Spot = FVector::ZeroVector; float Yaw = 0.f; int32 Def = 0; bool bMover = false; float DeadSince = -1.f; };
	TArray<FTrainingDummy> TrainingDummies;
	float MatchLength = 600.f;        // seconds on the clock
	bool bOvertime = false;
	float TimeLeft() const;
	void SetMatchMinutes(int32 Minutes);

	// ---- revive and shop (VR-12) ----------------------------------------------------------------
	const FArenaRespawn* PendingRespawn(int32 Team, int32 HeroIndex) const;
	int32 ReviveCostFor(int32 Level) const;
	/** Seconds until this hero may revive again (0 = ready). */
	float ReviveReadyIn(int32 Team, int32 HeroIndex) const;
	/** Instant respawn at the base for gold; own cooldown. */
	bool TryRevive(int32 Team, int32 HeroIndex);
	/** Shop: a living hero in its base, or a dead one (buys for the respawn). */
	ArenaCore::EBuyResult TryBuy(int32 Team, int32 HeroIndex, int32 ItemIndex);
	/** What the item costs this hero right now (owned components come off), and how many of its items it uses up. */
	int32 PriceFor(int32 Team, int32 HeroIndex, int32 ItemIndex, int32* OutConsumed = nullptr) const;
	/** Sells the item at this inventory position for 60 % of its full price (in the shop). */
	bool TrySell(int32 Team, int32 HeroIndex, int32 InventoryPos);
	/** The inventory and gold of a hero, alive or waiting to respawn. */
	TArray<int32> InventoryOf(int32 Team, int32 HeroIndex) const;
	/** A potion (0 health, 1 mana) for PotionCost, in the shop, up to PotionMax of a kind. */
	ArenaCore::EBuyResult TryBuyPotion(int32 Team, int32 HeroIndex, int32 Kind);
	/** A free spot at a team's base (recall, respawn). */
	FVector BaseSpot(int32 Team) const;
	bool bFountainsOn = false;         // real matches and the base lab (the other labs stand their units in base A)
	bool InShop(const AArenaCharacter* C) const;
	AArenaCharacter* FindHero(int32 Team, int32 HeroIndex) const;
	TArray<FArenaFeedEntry> Feed;
	TArray<FArenaKill> Kills;          // hero deaths for the kill feed (portraits)
	TArray<FArenaRespawn> Respawns;    // pending hero respawns for the top bar
	TArray<TWeakObjectPtr<AArenaCharacter>> Heroes;
	bool bBotMatch = false;
	bool bUIDemo = false;              // -ArenaUIDemo
	bool bTrainingDemo = false;        // -ArenaTrainingDemo on the training map: screenshots and checks, then quit
	int32 TrainingDemoStep = 0;
	int32 UIFrontStep = 0;             // the UI demo's front-end tour before the hero pick
	float UIFrontDoneAt = 1.e9f;
	void TickTrainingDemo(float Now);
	bool bAnimLab = false;             // -ArenaAnimLab: reaction animations and physics checks on a test line-up
	bool bLabHUD = false;              // a lab that shows the player's HUD (-ArenaAimLab)
	int32 Seed = 1;
	int32 StuckTotal = 0;   // every stuck episode of every bot this match (controllers die with their pawns)

	// ---- Conquest (v14, Game/ArenaConquest.cpp): three lanes of towers and inhibitors before each team's core, the
	// neutral camps and the boss. The game mode is the authority; the players see AArenaGameState (Structures, Camps).
	bool bConquest = false;
	void SetConquest(bool bOn) { if (Phase == EArenaPhase::HeroSelect) { bConquest = bOn; if (bOn) { TeamSize = 5; } } }
	struct FStructure
	{
		TWeakObjectPtr<AArenaCharacter> Unit; int32 Kind = 0; int32 Team = 0; int32 Lane = -1; int32 Tier = 0;
		FVector Spot = FVector::ZeroVector; float Yaw = 0.f; bool bAlive = false; float RespawnAt = -1.f;
		TWeakObjectPtr<AArenaCharacter> Target; float NextThink = 0.f; float LastHp = 0.f; float HurtAt = -100.f; float AlertAt = -100.f; bool bOnNav = false;
	};
	TArray<FStructure> Structures;
	/** v18: a hero's death labelled for the bots' quality (ARENA evt=death_audit; DEATH_AUDIT in the summary). */
	void AuditHeroDeath(class AArenaCharacter* Victim, class AArenaCharacter* Killer, float Now);
	/** Is the point inside the reach of an alive, armed structure of the other team (and does it shoot at Who)? */
	bool InEnemyTowerRange(int32 Team, const FVector& At, const class AArenaCharacter* Who, bool* bTargetsWho = nullptr, float Margin = 0.f) const;
	int32 DeathAudit[4] = { 0, 0, 0, 0 };   // tower_dive, outnumbered, low_engage, fair
	int32 DeathAuditBots[4] = { 0, 0, 0, 0 };
	struct FCamp { int32 Def = 0; int32 Side = 0; FVector Spot = FVector::ZeroVector; float Yaw = 0.f; TArray<TWeakObjectPtr<AArenaCharacter>> Units; float RespawnAt = 0.f; bool bUp = false; };
	TArray<FCamp> Camps;
	int32 BossTeam = -1;
	float BossUntil = 0.f;
	/** Any of this team's lane minions within Radius of the point (backdoor protection, tower safety). */
	bool MinionsNear(int32 Team, const FVector& At, float Radius) const;
	/** The fountain's burn around a base (Conquest: only close to the spawn, the core stands outside it). */
	float FountainRadiusCm() const;
	int32 StructuresLeft(int32 Team) const;
	const FStructure* FindStructure(const AArenaCharacter* Unit) const;
	/** Where a hero of this team goes to push its lane: its minions' front, else just behind its outermost tower. */
	FVector LaneFront(int32 Team, int32 Lane) const;
	/** The lane a hero plays (Conquest: 5 heroes 1 mid, 2 north, 2 south), -1 outside Conquest. */
	int32 LaneOf(int32 Team, int32 HeroIndex) const;
	FVector CorePos(int32 Team) const;
	TMap<int32, int32> HeroLanes;      // team * 100 + hero -> lane
	int32 LaneForSlot(int32 SlotInTeam) const;
	/** A ping of a team at a point (the player's key G): shown to the team, its bots go there (v16). */
	void AddPing(int32 Team, int32 HeroIndex, const FVector& At);
	/** The team's newest ping of the last 10 s. */
	bool PingFor(int32 Team, FVector& Out) const;
	/** Damage dealt to each team's structures this match: [team attacked][0 by heroes, 1 by minions] (CONQUEST_SUMMARY). */
	float StructureDamage[2][2] = { { 0.f, 0.f }, { 0.f, 0.f } };
	float ConquestMatchSeconds() const;

private:
	void MirrorFromState();
	void StartConquest();
	// -ArenaConquestLab (Game/ArenaConquestLab.cpp)
	void StartConquestLab();
	void TickConquestLab(float Now, float Dt);
	/** -ArenaDuelLab: melee against ranged, one on one, every pair (the v17 balance measure). */
	void StartDuelLab();
	void TickDuelLab(float Now);
	/** -ArenaSkinDeathLab: every hero in every skin dies to a blow, then a forced garbage collection (a crash names
	 *  the body that corrupts memory on death). */
	void TickSkinDeathLab(float Now);
	/** -ArenaLocoLab (-LocoHero=Id): each hero's locomotion under a player's input and a bot's path, the anim
	 *  blueprint's states and variables logged, side-camera screenshots. */
	void TickLocoLab(float Now);
	/** Labs: the skin SpawnHero puts on (-2: its own choice). */
	int32 ForcedSkin = -2;
	void TickConquest(float Now, float Dt);
	void TickStructures(float Now, float Dt);
	void TickCamps(float Now);
	void UpdateProtection();
	void OnStructureDestroyed(AArenaCharacter* S, AArenaCharacter* Killer);
	void OnMonsterKilled(AArenaCharacter* M, AArenaCharacter* Killer);
	AArenaCharacter* SpawnStructure(int32 Kind, int32 Team, int32 Lane, int32 Tier, const FVector& Spot, float Yaw);
	void SpawnCamp(FCamp& Camp, float Now);
	/** The ground under a point (the navmesh, else a trace); bOk false when neither found it. */
	FVector GroundSpot(const FVector& Desired, bool* bOk = nullptr) const;
	void SpawnConquestWave();
	void WriteConquestState(class AArenaGameState* GS);
	int32 ConquestLogCount = 0;
	/** Conquest waits for the navmesh (a bot match starts in the first frame, before the navigation data is up). */
	bool bConquestPending = false;
	bool NavReady() const;
	void StartMatch();
	AArenaCharacter* SpawnHero(int32 HeroIndex, int32 Team, int32 SlotInTeam, bool bPlayer, int32 Level, float Xp, APlayerController* OwnerPC = nullptr);
	void SpawnWave();
	void EndMatch(int32 Winner);
	void WriteSummaryAndMaybeQuit();
	void NavCheck();
	void RespawnNow(int32 Team, int32 HeroIndex);
	/** Writes the replicated match state (AArenaGameState) from the authority's own state. */
	void SyncGameState();
	/** Gold, XP, kills and assists to a hero, alive or on its respawn record (a dead killer still gets its kill). */
	void GiveReward(int32 Team, int32 HeroIndex, AArenaCharacter* Live, float GoldAmount, float XpAmount, int32 AddKills, int32 AddAssists);
	/** Gold to a hero, or to its respawn record when it is dead by now (the reward was lost on the body). */
	void PayHero(AArenaCharacter* Hero, float Amount);
	/** Desired spawn point, or the nearest one around it where a capsule overlaps no other unit. */
	FVector FreeSpot(const FVector& Desired, float Radius) const;
	void CheckWinConditions(float Now);
	void BotEconomy(float Now);
	void BuyBuild(int32 Team, int32 HeroIndex);

	FVector BaseA = FVector(-5200.f, 0.f, 100.f);
	FVector BaseB = FVector(5200.f, 0.f, 100.f);
	float NextWave = 0.f;
	float BotMatchSeconds = 180.f;
	FArenaRulesDef Rules;
	int32 FirstBlood = 0;
	bool bShots = false;
	int32 ShotIndex = 0;
	float NextShot = 0.f;
	float NextOrb = 0.f;
	TWeakObjectPtr<AActor> Orb;
	FVector OrbSpot = FVector(0.f, 0.f, 130.f);
	TArray<TArray<FVector>> Lanes;   // TargetPoints tagged Lane_<Name>_<NN>, ordered from base A to base B
	int32 WaveIndex = 0;
	// -ArenaUIDemo: walks the UI (hero select, match, shop, scoreboard, death, end) taking a screenshot of each
	int32 UIDemoStep = 0;
	float UIDemoAimAt = 0.f;
	TWeakObjectPtr<AController> UIDemoBot;   // the bot handed back its hero after the aiming shot
	void TickUIDemo(float Now);
	void UIShot(const TCHAR* Name);
	TMap<int32, float> ReviveReadyAt;   // team * 100 + hero -> time the next revive is allowed
	float NextEconomy = 0.f;
	float OvertimeStart = 0.f;
	TArray<float> FrameMs;             // real frame times while playing (ARENA_PERF)
	/** Loads every effect, sound and animation heroes.json names (a first cast used to stall a frame loading them). */
	void PreloadAssets();
	UPROPERTY() TArray<TObjectPtr<UObject>> Preloaded;
	int32 Hitches = 0;
	float PerfSkipUntil = 0.f;          // frames around a screenshot are not the game's frames (ARENA_PERF)
	int32 TeamDifficulty[2] = { -1, -1 };   // -DiffA= / -DiffB=: a bot match with a difficulty per team (GS-12)
	float PreloadMs = 0.f;
	// -ArenaAnimLab (Game/ArenaAnimLab.cpp)
	void StartAnimLab();
	void TickAnimLab(float Now);
	void LabSpawnLine();
	TArray<TWeakObjectPtr<AArenaCharacter>> LabUnits;
	TWeakObjectPtr<AArenaCharacter> LabDummy[2];
	TWeakObjectPtr<UPrimitiveComponent> LabProp;
	float LabStart = -1.f, LabPropZ0 = 0.f, LabPropMaxRise = 0.f, LabPropMaxSpeed = 0.f, LabStackMaxUp = 0.f, LabKnockMaxZ = 0.f;
	int32 LabStep = 0, LabFails = 0;
	TArray<float> LabZ0;
	FVector LabPushFrom = FVector::ZeroVector;
	// -ArenaMechLab and -ArenaCamShots=<file> (Game/ArenaMechLab.cpp)
	uint8 LabMode = 0;                 // 0 animation lab, 1 mechanics lab, 2 camera shots, 3 aim lab, 4 skill lab
	// -ArenaAimLab (Game/ArenaAimLab.cpp)
	void StartAimLab();
	void TickAimLab(float Now);
	// -ArenaSkillLab (Game/ArenaSkillLab.cpp): ranks, levels, displacements, telegraphs, item passives, recipes
	void StartSkillLab();
	void TickSkillLab(float Now);
	// -ArenaBaseLab (Game/ArenaBaseLab.cpp): recall, fountain, potions
	void StartBaseLab();
	void StartFxLab();
	void TickTraining(float Now, float Dt);
	void StartTrainingPlayerAgain();
	float TrainingPlayerDeadSince = -1.f;
	void TickFxLab(float Now);
	void TickBaseLab(float Now);
	// -ArenaFoliageCheck (Game/ArenaFoliageCheck.cpp): no plant hangs in the air or sticks out of a slope
	void RunFoliageCheck();
	void StartMechLab();
	void TickMechLab(float Now);
	void StartCamShots(const FString& File);
	void TickCamShots(float Now);
	class AArenaCharacter* LabSpawn(const struct FArenaHeroDef& Def, int32 Team, const FVector& At, float Yaw, bool bMinionDef, bool bBot);
	AActor* LabWall(const FVector& Center, const FVector& Size);
	TArray<TWeakObjectPtr<class AArenaIndicator>> LabIndicators;
	TArray<FVector> LabTargets;
	TArray<TWeakObjectPtr<AActor>> LabActors;
	TArray<float> LabBest;
	TArray<FString> CamNames;
	TArray<FTransform> CamPoses;
	TArray<float> CamFovs;
	int32 MechRound = 0, LabCasts = 0;
	TArray<TWeakObjectPtr<AActor>> TourCams;   // -ArenaTour: screenshots cycle through the map's cameras tagged Tour
	int32 TourIndex = 0;
	float TourShotAt = -1.f;
};

/** The LAN client's mirror of the game mode (see AArenaGameMode::Get): spawned by the local player controller on a
 *  client; skips AGameModeBase's set-up (it would replace the replicated game state). */
UCLASS(NotPlaceable, Transient)
class PARAGONARENA_API AArenaClientMirror : public AArenaGameMode
{
	GENERATED_BODY()

public:
	AArenaClientMirror() { bClientMirror = true; }
	virtual void PreInitializeComponents() override { AInfo::PreInitializeComponents(); }
};
