// The prototype's character (operator 2026-09-29: "a separate mode for a new game: a grey town map with three lanes, a
// simple grey character with full JRPG-style movement — running, jumping, sneaking — and full combat with different
// attacks, and the same bot to test the mechanics against; no art yet, as it is done in game dev").
// v21 (operator: "remove parrying and blocking completely, add more animations and make everything fluid with what we
// have; the combat itself must be intuitive and dynamic, using the mouse fully"): no guard — the dodge is the defence
// (a perfect dodge slows time and opens a counter); attacks go where the camera looks and home in on the enemy near
// the crosshair; charged heavies launch, air slashes juggle, attacks cancel into moves, presses are buffered.
// One class for the player and the bot: the bot drives the same verbs as the player's keys.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ProtoCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAnimSequenceBase;

/** The prototype's tuning in one place (the ProtoLab measures against it). */
struct FProtoTuning
{
	float MaxHealth = 100.f, MaxStamina = 100.f, StaminaRegen = 30.f, StaminaDelay = 0.7f;
	float RunSpeed = 520.f, SprintSpeed = 760.f, SneakSpeed = 230.f, ChargeSpeed = 220.f, LockSpeed = 460.f;
	float SprintDrain = 14.f;
	float JumpZ = 720.f, Gravity = 1.6f, AirControl = 0.5f;
	float WallJumpUp = 760.f, WallJumpOut = 620.f;
	float MantleReach = 200.f;              // a ledge this high over the feet is climbed
	float DodgeCost = 18.f, DodgeTime = 0.3f, DodgeSpeed = 1750.f, DodgeInvuln = 0.3f;
	float PerfectWindow = 0.18f;            // a blow this soon after the dodge starts: a perfect dodge
	float CounterTime = 1.6f, CounterMult = 1.5f, SlowMoScale = 0.3f, SlowMoSeconds = 0.5f;
	float LightDamage[3] = { 8.f, 10.f, 15.f };
	float LightCost = 4.f, HeavyCost = 16.f, SprintAttackCost = 10.f;
	float QuickHeavyDamage = 16.f, HeavyBase = 20.f, HeavyPerCharge = 26.f, HeavyChargeMax = 1.f, TapTime = 0.22f;
	float LaunchUp = 950.f;                 // a full charge throws the target up
	float SprintAttackDamage = 14.f, PlungeDamage = 18.f, PlungeRadius = 320.f;
	float AirDamage = 7.f, AirHang = 0.35f; int32 AirSlashes = 3;
	float BackstabMult = 2.5f;
	float Reach = 160.f, Magnet = 560.f, MagnetCos = 0.64f, MaxLunge = 320.f;   // the aim: 5.6 m, 50 degrees off the crosshair
	float BufferTime = 0.3f;
	// prototype 2 (the Hades controls): the cast, the dash-strike, the special's smash around, the wrath (call)
	float CastSpeed = 2600.f, CastRange = 1400.f, CastDamage = 10.f, CastSlow = 0.35f, CastSlowTime = 2.f, CastReturn = 3.f;
	float DashStrikeDamage = 12.f, NovaDamage = 16.f, NovaRadius = 240.f;
	float WrathMax = 100.f, WrathDamage = 28.f, WrathRadius = 520.f;
	float TopMagnet = 420.f, TopMagnetCos = 0.85f;   // the mouse aims precisely: a narrow homing
};

UENUM()
enum class EProtoAttack : uint8 { None, Light, Heavy, Sprint, Plunge, Air, Cast, DashStrike, Wrath };

/** How a blow lands (the reaction it causes). */
UENUM()
enum class EProtoBlow : uint8 { Light, Medium, Heavy, Launch, Air, Plunge };

UCLASS()
class PARAGONARENA_API AProtoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AProtoCharacter();
	static const FProtoTuning& Tuning();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;

	// ---- the verbs (keys and the mouse for the player, decisions for the bot) ----
	void Move(const FVector2D& Input, const FRotator& Frame);
	void SetSprint(bool bOn);
	void SetSneak(bool bOn);
	void ToggleSneak() { SetSneak(!bSneaking); }
	void PressJump();
	void ReleaseJump();
	bool Dodge(const FVector& Direction);          // zero: backwards
	void LightAttack();
	void StartHeavy();
	void ReleaseHeavy();
	void SetLockTarget(AProtoCharacter* Target) { LockTarget = Target; }
	/** Where the attacks go: the camera's yaw for the player, the enemy for the bot. */
	void SetAimYaw(float Yaw) { AimYaw = Yaw; bHasAim = true; }
	void SetAimAt(const FVector& Where);
	// prototype 2 (the Hades controls): RMB the cast, F the wrath (the call); LMB right after a dash: the dash-strike
	void CastStone();
	void CallWrath();
	void CastLanded(AProtoCharacter* Victim);
	/** Prototype 2: the camera from above (fixed, as Hades) instead of the free third-person one. Set before BeginPlay. */
	bool bTopDown = false;
	FVector AimPoint = FVector::ZeroVector;   // where the cursor points on the ground (the Hades controls)
	int32 CastAmmo = 1;
	float CastBackAt = -100.f;
	float Wrath = 0.f;
	float SlowUntil = -100.f;
	int32 Casts = 0, CastHits = 0, DashStrikes = 0, Calls = 0;

	// ---- state ----
	int32 Team = 0;
	bool bIsBot = false;
	float Health = 100.f, Stamina = 100.f;
	bool bSprinting = false, bSneaking = false, bCharging = false, bDead = false;
	float ChargeStart = -1.f, LastStaminaUse = -100.f;
	int32 ComboStep = 0;                 // the last light attack of the chain (1-3)
	float ComboUntil = -100.f;
	bool bComboQueued = false;
	EProtoAttack Attacking = EProtoAttack::None;
	float AttackStart = -100.f, AttackUntil = -100.f, HitAt = -100.f, CancelAt = -100.f;
	float StaggerUntil = -100.f, StunUntil = -100.f, InvulnUntil = -100.f, DodgeStart = -100.f, DodgeUntil = -100.f;
	float MantleUntil = -100.f, MantleStart = -1.f;
	FVector MantleFrom = FVector::ZeroVector, MantleTo = FVector::ZeroVector;
	bool bPlunging = false, bWallJumped = false, bLaunched = false;
	int32 AirSlashesLeft = 3;
	float HangUntil = -100.f;            // an air slash keeps the body up a moment
	float CounterUntil = -100.f;         // after a perfect dodge: the next blow is a counter
	TWeakObjectPtr<AProtoCharacter> LockTarget;
	TWeakObjectPtr<AProtoCharacter> LastAttacker;
	float LastHitAt = -100.f;
	int32 Knockouts = 0;
	/** Is this character aware of its enemy (the bot's perception; a sneaking player behind it is not seen). */
	bool bAlerted = false;
	float AlertedAt = -100.f;
	float AimYaw = 0.f;
	bool bHasAim = false;
	/** The camera's kick (the player's own view): set on a landed or taken blow, fades. */
	float ShakeAt = -100.f, ShakeStrength = 0.f;
	FString LastAnim;                    // the lab reads which animation a move played

	bool IsAlive() const { return !bDead; }
	bool CanAct() const;
	bool IsAttacking() const;
	bool IsLocked() const;
	float ChargePct() const;
	float GetNow() const;

	/** A blow from Source; returns the damage dealt (after a dodge, a sneak attack, a counter). */
	float TakeBlow(float Damage, AProtoCharacter* Source, float Knockback, EProtoBlow Kind, const TCHAR* What);

	// counters for the ProtoLab and the bot
	int32 HitsLanded = 0, Backstabs = 0, Dodges = 0, PerfectDodges = 0, Counters = 0, Mantles = 0, WallJumps = 0, Plunges = 0, AirHits = 0, Launches = 0;
	float LastDamageDealt = 0.f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> SpringArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	float ArmLength = 430.f;             // the player's zoom (the mouse wheel)

	/** The HUD's damage numbers (kind 0 hit, 2 sneak attack, 4 dodged, 5 perfect dodge, 6 counter, 7 launch). */
	struct FNumber { FVector At; float Amount = 0.f; float Time = 0.f; int32 Kind = 0; };
	static TArray<FNumber> Numbers;
	bool bDebugMantle = false;   // the lab: log why a mantle was refused

private:
	void UpdateSpeed();
	void PlayAnim(const TCHAR* Path, float Rate = 1.f, float BlendIn = 0.08f, float BlendOut = 0.2f, float StartAt = 0.f);
	void StopAttackAnim(float Blend);
	void StartLight(int32 Step);
	void AirSlash();
	void Plunge();
	/** The attack's direction and the enemy it homes in on (the lock, else the one nearest the crosshair). */
	AProtoCharacter* ResolveAim(FVector& OutDir, float& OutLunge) const;
	void BeginSwing(EProtoAttack Kind, float Duration, float HitTime, float CancelTime);
	void Lunge(const FVector& Dir, float Distance, float Seconds);
	void DoHit(float Damage, float Radius, float Reach, float Knockback, EProtoBlow Kind, const TCHAR* What);
	void Kick(float Strength) { ShakeAt = GetNow(); ShakeStrength = FMath::Max(ShakeStrength * 0.5f, Strength); }
	void RunBuffered();
	bool TryMantle();
	bool TryWallJump();
	void Die(AProtoCharacter* Killer, const FVector& From);
	void React(EProtoBlow Kind, const FVector& From);
	enum class EBuffered : uint8 { None, Light, Heavy, Dodge, Jump };
	EBuffered Buffered = EBuffered::None;
	float BufferedAt = -100.f;
	FVector BufferedDir = FVector::ZeroVector;
	FVector SwingDir = FVector::ForwardVector;
	float TurnYaw = 0.f;
	float TurnUntil = -100.f;
	float HeavyPressAt = -100.f;
	float BaseFov = 90.f;
	float FallSpeed = 0.f;               // the speed of the last fall (a hard landing plays the landing)
	void SlowMo();
	void SetupTopDown();
	void DashStrike();
	void AddWrath(float Amount) { Wrath = FMath::Min(Tuning().WrathMax, Wrath + Amount); }
	FTimerHandle HitTimer, ComboTimer;
	TMap<FString, TWeakObjectPtr<UAnimSequenceBase>> AnimCache;
};
