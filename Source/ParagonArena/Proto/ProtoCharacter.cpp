#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoGameMode.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Proto/ProtoCastStone.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogProto, Log, All);

TArray<AProtoCharacter::FNumber> AProtoCharacter::Numbers;

namespace
{
	const TCHAR* AnimRoot = TEXT("/Game/Characters/Mannequins/Anims/");
	FString AnimPath(const TCHAR* Rel)
	{
		const FString Name = FPaths::GetBaseFilename(Rel);
		return FString(AnimRoot) + Rel + TEXT(".") + Name;
	}
	const TCHAR* LightAnims[3] = { TEXT("Unarmed/Attack/MM_Attack_01"), TEXT("Unarmed/Attack/MM_Attack_02"), TEXT("Unarmed/Attack/MM_Attack_03") };
	const TCHAR* ChargedAnim = TEXT("Unarmed/Attack/MM_ChargedAttack");
	const TCHAR* ReactLight[4] = { TEXT("Rifle/HitReact/MM_HitReact_Front_Lgt_01"), TEXT("Rifle/HitReact/MM_HitReact_Front_Lgt_02"), TEXT("Rifle/HitReact/MM_HitReact_Front_Lgt_03"), TEXT("Rifle/HitReact/MM_HitReact_Front_Lgt_04") };
	const TCHAR* ReactMedium[2] = { TEXT("Rifle/HitReact/MM_HitReact_Front_Med_01"), TEXT("Rifle/HitReact/MM_HitReact_Front_Med_02") };
	const TCHAR* ReactHeavy = TEXT("Rifle/HitReact/MM_HitReact_Front_Hvy_01");
	const TCHAR* ReactBack = TEXT("Rifle/HitReact/MM_HitReact_Back_Med_01");
	const TCHAR* DeathFront[3] = { TEXT("Death/MM_Death_Front_01"), TEXT("Death/MM_Death_Front_02"), TEXT("Death/MM_Death_Front_03") };
	const TCHAR* DeathBack = TEXT("Death/MM_Death_Back_01");
	const TCHAR* DeathLeft = TEXT("Death/MM_Death_Left_01");
	const TCHAR* DeathRight = TEXT("Death/MM_Death_Right_01");
	const FVector ArmSocket(0.f, 45.f, 0.f);
}

const FProtoTuning& AProtoCharacter::Tuning()
{
	static FProtoTuning T;
	return T;
}

AProtoCharacter::AProtoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	const FProtoTuning& T = Tuning();
	GetCapsuleComponent()->InitCapsuleSize(40.f, 94.f);
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	JumpMaxCount = 2;                                    // the double jump
	UCharacterMovementComponent* M = GetCharacterMovement();
	M->bOrientRotationToMovement = true;                 // the body turns to where it runs (a JRPG's free camera)
	M->RotationRate = FRotator(0.f, 900.f, 0.f);
	M->JumpZVelocity = T.JumpZ;
	M->GravityScale = T.Gravity;
	M->AirControl = T.AirControl;
	M->MaxWalkSpeed = T.RunSpeed;
	M->BrakingDecelerationWalking = 2400.f;
	M->BrakingDecelerationFalling = 600.f;
	M->MaxAcceleration = 2600.f;
	M->bUseSeparateBrakingFriction = true;
	M->BrakingFriction = 6.f;
	M->GetNavAgentPropertiesRef().bCanJump = true;
	M->SetWalkableFloorAngle(50.f);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetCapsuleComponent());
	SpringArm->TargetArmLength = 430.f;
	SpringArm->TargetOffset = FVector(0.f, 0.f, 70.f);
	SpringArm->SocketOffset = ArmSocket;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 90.f;

	// the grey mannequin with the template's combat anim blueprint (8-way walk and jog, jumps, a DefaultSlot)
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> Abp(TEXT("/Game/Variant_Combat/Anims/ABP_Manny_Combat"));
	if (Manny.Succeeded()) { GetMesh()->SetSkeletalMesh(Manny.Object); }
	if (Abp.Succeeded()) { GetMesh()->SetAnimInstanceClass(Abp.Class); }
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -94.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void AProtoCharacter::BeginPlay()
{
	Super::BeginPlay();
	Health = Tuning().MaxHealth;
	Stamina = Tuning().MaxStamina;
	AirSlashesLeft = Tuning().AirSlashes;
	BaseFov = Camera->FieldOfView;
	ArmLength = SpringArm->TargetArmLength;
	if (bIsBot)
	{
		// the bot is the other mannequin (same skeleton, same animations, same moves)
		if (USkeletalMesh* Quinn = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")))
		{
			GetMesh()->SetSkeletalMesh(Quinn);
		}
	}
	if (bTopDown) { SetupTopDown(); }
	// all movement comes from code (the dash animation's own root motion carried the dodge 40 % further)
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance()) { Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion); }
}

void AProtoCharacter::SetupTopDown()
{
	// the Hades view: a fixed camera high above and behind at 55 degrees, turned 45 degrees (the town's streets run
	// across the screen), no collision pull-in (a building in the way is cut away by the player controller)
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->SetUsingAbsoluteRotation(true);
	SpringArm->SetWorldRotation(FRotator(-55.f, 45.f, 0.f));
	SpringArm->TargetArmLength = ArmLength = 1900.f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->TargetOffset = FVector::ZeroVector;
	SpringArm->SocketOffset = FVector::ZeroVector;
	SpringArm->CameraLagSpeed = 10.f;
	Camera->SetFieldOfView(55.f);
	BaseFov = 55.f;
}

float AProtoCharacter::GetNow() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f; }

bool AProtoCharacter::IsAttacking() const { return GetNow() < AttackUntil || bPlunging || bCharging; }

bool AProtoCharacter::IsLocked() const { return LockTarget.IsValid() && LockTarget->IsAlive(); }

bool AProtoCharacter::CanAct() const
{
	const float Now = GetNow();
	return !bDead && !bLaunched && Now >= StaggerUntil && Now >= StunUntil && Now >= MantleUntil && Now >= DodgeUntil && !IsAttacking();
}

float AProtoCharacter::ChargePct() const
{
	return bCharging ? FMath::Clamp((GetNow() - ChargeStart) / Tuning().HeavyChargeMax, 0.f, 1.f) : 0.f;
}

void AProtoCharacter::PlayAnim(const TCHAR* Path, float Rate, float BlendIn, float BlendOut, float StartAt)
{
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	LastAnim = FPaths::GetBaseFilename(Path);
	if (!Anim) { return; }
	TWeakObjectPtr<UAnimSequenceBase>& Cached = AnimCache.FindOrAdd(Path);
	if (!Cached.IsValid()) { Cached = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet); }
	if (UAnimSequenceBase* Seq = Cached.Get())
	{
		Anim->PlaySlotAnimationAsDynamicMontage(Seq, TEXT("DefaultSlot"), BlendIn, BlendOut, Rate, 1, -1.f, StartAt * Seq->GetPlayLength());
	}
}

void AProtoCharacter::StopAttackAnim(float Blend)
{
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr) { Anim->StopSlotAnimation(Blend, TEXT("DefaultSlot")); }
}

void AProtoCharacter::UpdateSpeed()
{
	const FProtoTuning& T = Tuning();
	float Speed = T.RunSpeed;
	if (bCharging) { Speed = T.ChargeSpeed; }
	else if (bSneaking) { Speed = T.SneakSpeed; }
	else if (bSprinting) { Speed = T.SprintSpeed; }
	else if (IsLocked()) { Speed = T.LockSpeed; }   // circling a locked enemy: a step slower, the 8-way jog
	if (GetNow() < SlowUntil) { Speed *= 1.f - T.CastSlow; }   // hit by a cast stone
	GetCharacterMovement()->MaxWalkSpeed = bDead ? 0.f : Speed;
}

void AProtoCharacter::Move(const FVector2D& Input, const FRotator& Frame)
{
	if (bDead || bLaunched || Input.IsNearlyZero()) { return; }
	const float Now = GetNow();
	if (Now < StunUntil || Now < StaggerUntil || Now < MantleUntil || Now < DodgeUntil) { return; }
	if (Now < AttackUntil && !bCharging)
	{
		if (Now < CancelAt || Attacking == EProtoAttack::Plunge) { return; }   // the swing's commitment
		// the swing's tail: a step cancels it into the run (fluid, no waiting out the follow-through)
		AttackUntil = -100.f;
		Attacking = EProtoAttack::None;
		bComboQueued = false;
		GetWorldTimerManager().ClearTimer(ComboTimer);
		StopAttackAnim(0.2f);
	}
	const FRotator Yaw(0.f, Frame.Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Input.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Input.X);
}

void AProtoCharacter::SetSprint(bool bOn)
{
	bSprinting = bOn && !bDead && Stamina > 5.f;
	if (bSprinting) { bSneaking = false; if (!bTopDown) { SpringArm->TargetOffset = FVector(0.f, 0.f, 70.f); } }
}

void AProtoCharacter::SetSneak(bool bOn)
{
	bSneaking = bOn && !bDead;
	if (bSneaking) { bSprinting = false; }
	// the camera lowers a little while sneaking
	if (!bTopDown) { SpringArm->TargetOffset = FVector(0.f, 0.f, bSneaking ? 40.f : 70.f); }
}

void AProtoCharacter::SetAimAt(const FVector& Where)
{
	AimPoint = Where;
	const FVector To = Where - GetActorLocation();
	if (!To.IsNearlyZero()) { SetAimYaw(To.Rotation().Yaw); }
}

void AProtoCharacter::PressJump()
{
	const float Now = GetNow();
	if (bDead || bLaunched || Now < StunUntil || Now < StaggerUntil || Now < MantleUntil || bCharging) { return; }
	if (Now < AttackUntil)
	{
		if (Now < CancelAt || Attacking == EProtoAttack::Plunge) { Buffered = EBuffered::Jump; BufferedAt = Now; return; }
		AttackUntil = -100.f;                  // the tail of a swing: a jump cancels it
		Attacking = EProtoAttack::None;
		bComboQueued = false;
		StopAttackAnim(0.15f);
	}
	if (Now < DodgeUntil) { Buffered = EBuffered::Jump; BufferedAt = Now; return; }
	bSneaking = false;
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (M->IsFalling())
	{
		// the air: a ledge in reach is climbed, a wall in front is kicked off, else the second jump
		if (TryMantle()) { return; }
		if (JumpCurrentCount >= JumpMaxCount && TryWallJump()) { return; }
		if (JumpCurrentCount < JumpMaxCount) { PlayAnim(TEXT("Unarmed/Jump/MM_Jump"), 1.3f, 0.05f, 0.2f); }
	}
	Jump();
}

void AProtoCharacter::ReleaseJump() { StopJumping(); }

bool AProtoCharacter::Dodge(const FVector& Direction)
{
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	if (bDead || bLaunched || Now < StunUntil || Now < MantleUntil || Now < DodgeUntil) { return false; }
	if (Now < AttackUntil)
	{
		// a light swing, an air slash, and the tail of any other move cancel into the dodge; a heavy's start does not
		const bool bCancel = Attacking == EProtoAttack::Light || Attacking == EProtoAttack::Air || Now >= CancelAt;
		if (!bCancel || Attacking == EProtoAttack::Plunge) { Buffered = EBuffered::Dodge; BufferedAt = Now; BufferedDir = Direction; return false; }
	}
	if (Now < StaggerUntil || Stamina < T.DodgeCost) { return false; }
	FVector Dir = Direction.GetSafeNormal2D();
	if (Dir.IsNearlyZero()) { Dir = -GetActorForwardVector().GetSafeNormal2D(); }
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(ComboTimer);
	bCharging = false;
	bComboQueued = false;
	AttackUntil = -100.f;
	Attacking = EProtoAttack::None;
	Stamina -= T.DodgeCost;
	LastStaminaUse = Now;
	TSharedPtr<FRootMotionSource_ConstantForce> F = MakeShared<FRootMotionSource_ConstantForce>();
	F->InstanceName = TEXT("ProtoDodge");
	F->AccumulateMode = ERootMotionAccumulateMode::Override;
	F->Priority = 5;
	F->Force = Dir * T.DodgeSpeed;
	F->Duration = T.DodgeTime;
	F->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	F->FinishVelocityParams.SetVelocity = Dir * 250.f;
	GetCharacterMovement()->ApplyRootMotionSource(F);
	SetActorRotation(Dir.Rotation());      // the dash faces its way; a lock turns the body back after it
	PlayAnim(TEXT("Unarmed/Jump/MM_Dash"), 1.35f, 0.04f, 0.15f);
	DodgeStart = Now;
	DodgeUntil = Now + T.DodgeTime;
	InvulnUntil = Now + T.DodgeInvuln;
	++Dodges;
	return true;
}

AProtoCharacter* AProtoCharacter::ResolveAim(FVector& OutDir, float& OutLunge) const
{
	const FProtoTuning& T = Tuning();
	const FVector Me = GetActorLocation();
	const FVector Aim = bHasAim ? FRotator(0.f, AimYaw, 0.f).Vector() : GetActorForwardVector().GetSafeNormal2D();
	AProtoCharacter* Best = nullptr;
	const float Magnet = bTopDown ? T.TopMagnet : T.Magnet, MagnetCos = bTopDown ? T.TopMagnetCos : T.MagnetCos;
	if (IsLocked() && FVector::Dist2D(LockTarget->GetActorLocation(), Me) < Magnet + 200.f) { Best = LockTarget.Get(); }
	else
	{
		// the enemy nearest the crosshair within reach of a lunge (or pressed against the body, whatever the aim)
		float BestScore = TNumericLimits<float>::Max();
		for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
		{
			AProtoCharacter* C = *It;
			if (C == this || !C->IsAlive() || C->Team == Team) { continue; }
			const FVector To = C->GetActorLocation() - Me;
			if (FMath::Abs(To.Z) > 240.f) { continue; }
			const float D = To.Size2D();
			if (D > Magnet) { continue; }
			const float Dot = FVector::DotProduct(Aim, To.GetSafeNormal2D());
			if (Dot < MagnetCos && !(D < 170.f && Dot > -0.3f)) { continue; }
			const float Score = D * (1.6f - Dot);
			if (Score < BestScore) { BestScore = Score; Best = C; }
		}
	}
	if (Best)
	{
		const FVector To = Best->GetActorLocation() - Me;
		OutDir = To.GetSafeNormal2D();
		OutLunge = FMath::Clamp(To.Size2D() - 105.f, 0.f, T.MaxLunge);
	}
	else
	{
		OutDir = Aim;
		OutLunge = 60.f;
	}
	return Best;
}

void AProtoCharacter::BeginSwing(EProtoAttack Kind, float Duration, float HitTime, float CancelTime)
{
	const float Now = GetNow();
	Attacking = Kind;
	AttackStart = Now;
	AttackUntil = Now + Duration;
	HitAt = Now + HitTime;
	CancelAt = Now + CancelTime;
	bSprinting = false;
	// a quick turn to the swing's way (0.1 s), not a snap
	TurnYaw = SwingDir.Rotation().Yaw;
	TurnUntil = Now + 0.1f;
}

void AProtoCharacter::Lunge(const FVector& Dir, float Distance, float Seconds)
{
	if (Distance < 5.f || !GetCharacterMovement()->IsMovingOnGround()) { return; }
	TSharedPtr<FRootMotionSource_ConstantForce> F = MakeShared<FRootMotionSource_ConstantForce>();
	F->InstanceName = TEXT("ProtoLunge");
	F->AccumulateMode = ERootMotionAccumulateMode::Override;
	F->Priority = 3;
	F->Force = Dir.GetSafeNormal2D() * (Distance / FMath::Max(0.05f, Seconds));
	F->Duration = Seconds;
	F->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	F->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	GetCharacterMovement()->ApplyRootMotionSource(F);
}

void AProtoCharacter::LightAttack()
{
	const FProtoTuning& T = Tuning();
	if (bDead || bCharging || bLaunched) { return; }
	const float Now = GetNow();
	// prototype 2: the attack during a dash or just after it is the dash-strike (Hades)
	if (bTopDown && Now >= DodgeStart + 0.06f && Now < DodgeUntil + 0.15f && Now >= StaggerUntil && !GetCharacterMovement()->IsFalling() && Stamina >= T.LightCost)
	{
		DashStrike();
		return;
	}
	// in the air: a slash that keeps the body up (up to three per jump; the heavy key slams down)
	if (GetCharacterMovement()->IsFalling() && Now >= MantleUntil && !bPlunging)
	{
		if (Now < AttackUntil && Attacking == EProtoAttack::Air && Now < CancelAt) { Buffered = EBuffered::Light; BufferedAt = Now; return; }
		AirSlash();
		return;
	}
	// a swing in progress: the next one is queued (the combo); anything else waits in the buffer
	if (Now < AttackUntil)
	{
		if (Attacking == EProtoAttack::Light)
		{
			// before the chain point: queued; past it (the swing's follow-through): the next swing at once
			if (Now < CancelAt) { bComboQueued = true; }
			else if (Now >= StaggerUntil && Now >= StunUntil) { GetWorldTimerManager().ClearTimer(ComboTimer); AttackUntil = -100.f; StartLight(ComboStep % 3 + 1); }
		}
		else { Buffered = EBuffered::Light; BufferedAt = Now; }
		return;
	}
	if (!CanAct()) { Buffered = EBuffered::Light; BufferedAt = Now; return; }
	if (Stamina < T.LightCost) { return; }
	// out of a sprint: the running strike (a long lunge)
	if (bSprinting && GetVelocity().Size2D() > T.RunSpeed + 40.f && Stamina >= T.SprintAttackCost)
	{
		Stamina -= T.SprintAttackCost;
		LastStaminaUse = Now;
		float Lng = 0.f;
		ResolveAim(SwingDir, Lng);
		BeginSwing(EProtoAttack::Sprint, 0.55f, 0.2f, 0.4f);
		Lunge(SwingDir, FMath::Max(Lng, 260.f), 0.22f);
		PlayAnim(LightAnims[1], 1.4f, 0.06f, 0.2f);
		GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			DoHit(Tuning().SprintAttackDamage, 95.f, 180.f, 520.f, EProtoBlow::Medium, TEXT("Running strike"));
		}), 0.2f, false);
		return;
	}
	StartLight(Now < ComboUntil ? ComboStep % 3 + 1 : 1);
}

void AProtoCharacter::StartLight(int32 Step)
{
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	if (Stamina < T.LightCost) { return; }
	Stamina -= T.LightCost;
	LastStaminaUse = Now;
	ComboStep = Step;
	bComboQueued = false;
	float Lng = 0.f;
	ResolveAim(SwingDir, Lng);
	// a counter (after a perfect dodge) is quicker
	const float Speed = Now < CounterUntil ? 1.25f : 1.f;
	const float Dur = (Step == 3 ? 0.6f : 0.42f) / Speed, HitT = (Step == 3 ? 0.25f : 0.16f) / Speed, Chain = HitT + 0.14f / Speed;
	BeginSwing(EProtoAttack::Light, Dur, HitT, Chain);
	ComboUntil = AttackUntil + 0.5f;
	PlayAnim(LightAnims[Step - 1], (Step == 3 ? 1.3f : 1.5f) * Speed, 0.06f, 0.22f);
	Lunge(SwingDir, FMath::Max(Lng, Step == 3 ? 70.f : 40.f), 0.14f);
	const float Damage = T.LightDamage[Step - 1];
	GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this, Damage, Step]()
	{
		DoHit(Damage, Step == 3 ? 95.f : 80.f, Tuning().Reach, Step == 3 ? 460.f : 110.f, Step == 3 ? EProtoBlow::Medium : EProtoBlow::Light, Step == 3 ? TEXT("Finisher") : TEXT("Strike"));
	}), HitT, false);
	// the chain: a press during the swing starts the next one right after this one lands (not after its follow-through)
	GetWorldTimerManager().SetTimer(ComboTimer, FTimerDelegate::CreateWeakLambda(this, [this, Step]()
	{
		if (bComboQueued && !bDead && !bLaunched && GetNow() >= StaggerUntil && GetNow() >= StunUntil)
		{
			AttackUntil = -100.f;
			StartLight(Step % 3 + 1);
		}
	}), Chain, false);
}

void AProtoCharacter::DashStrike()
{
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	GetCharacterMovement()->RemoveRootMotionSource(TEXT("ProtoDodge"));
	DodgeUntil = FMath::Min(DodgeUntil, Now);
	Stamina -= T.LightCost;
	LastStaminaUse = Now;
	float Lng = 0.f;
	ResolveAim(SwingDir, Lng);
	BeginSwing(EProtoAttack::DashStrike, 0.4f, 0.12f, 0.28f);
	PlayAnim(LightAnims[1], 1.7f, 0.04f, 0.2f);
	Lunge(SwingDir, FMath::Max(Lng, 150.f), 0.12f);
	++DashStrikes;
	GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		DoHit(Tuning().DashStrikeDamage, 95.f, 180.f, 300.f, EProtoBlow::Medium, TEXT("Dash strike"));
	}), 0.12f, false);
}

void AProtoCharacter::CastStone()
{
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	if (bDead || bLaunched || bCharging || CastAmmo <= 0 || Now < StaggerUntil || Now < StunUntil || Now < MantleUntil) { return; }
	if (Now < AttackUntil && Now < CancelAt) { return; }
	--CastAmmo;
	CastBackAt = Now + T.CastReturn;
	++Casts;
	float Lng = 0.f;
	ResolveAim(SwingDir, Lng);
	// the cast goes straight at the cursor (a homing target only steers it when the cursor is on it)
	if (bHasAim && !bIsBot) { SwingDir = FRotator(0.f, AimYaw, 0.f).Vector(); }
	BeginSwing(EProtoAttack::Cast, 0.3f, 0.1f, 0.16f);
	PlayAnim(LightAnims[0], 2.f, 0.04f, 0.18f, 0.1f);
	FActorSpawnParameters P;
	P.Owner = this;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector From = GetActorLocation() + SwingDir * 60.f + FVector(0.f, 0.f, 30.f);
	if (AProtoCastStone* Stone = GetWorld()->SpawnActor<AProtoCastStone>(AProtoCastStone::StaticClass(), From, SwingDir.Rotation(), P)) { Stone->Launch(this, SwingDir); }
}

void AProtoCharacter::CastLanded(AProtoCharacter* Victim)
{
	const FProtoTuning& T = Tuning();
	if (!Victim) { return; }
	const float Dealt = Victim->TakeBlow(T.CastDamage, this, 150.f, EProtoBlow::Light, TEXT("Stone"));
	if (Dealt > 0.f)
	{
		++CastHits;
		++HitsLanded;
		Victim->SlowUntil = GetNow() + T.CastSlowTime;
		AddWrath(Dealt * 0.8f);
	}
}

void AProtoCharacter::CallWrath()
{
	// the call (Hades' god's aid): the gauge full, a blast around — every enemy near is thrown up
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	if (bDead || bLaunched || Wrath < T.WrathMax || Now < StunUntil) { return; }
	Wrath = 0.f;
	++Calls;
	bCharging = false;
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(ComboTimer);
	SwingDir = GetActorForwardVector().GetSafeNormal2D();
	BeginSwing(EProtoAttack::Wrath, 0.7f, 0.2f, 0.5f);
	TurnUntil = -100.f;
	InvulnUntil = Now + 0.6f;
	PlayAnim(ChargedAnim, 1.4f, 0.05f, 0.25f, 0.35f);
	Numbers.Add({ GetActorLocation() + FVector(0.f, 0.f, 160.f), 0.f, Now, 8 });
	if (!bIsBot) { SlowMo(); }
	GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		DoHit(Tuning().WrathDamage, 99999.f, Tuning().WrathRadius, 250.f, EProtoBlow::Launch, TEXT("Wrath"));
		Kick(1.6f);
	}), 0.2f, false);
}

void AProtoCharacter::AirSlash()
{
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	if (AirSlashesLeft <= 0 || Stamina < T.LightCost) { return; }
	--AirSlashesLeft;
	Stamina -= T.LightCost;
	LastStaminaUse = Now;
	float Lng = 0.f;
	AProtoCharacter* Target = ResolveAim(SwingDir, Lng);
	BeginSwing(EProtoAttack::Air, 0.36f, 0.14f, 0.24f);
	// the body hangs a moment and drifts to the target
	UCharacterMovementComponent* M = GetCharacterMovement();
	M->Velocity.Z = FMath::Max(M->Velocity.Z, 160.f);
	if (Target) { const FVector H = SwingDir * FMath::Min(Lng / 0.25f, 700.f); M->Velocity.X = H.X; M->Velocity.Y = H.Y; }
	HangUntil = Now + T.AirHang;
	PlayAnim(LightAnims[AirSlashesLeft % 2], 1.6f, 0.05f, 0.2f);
	GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		DoHit(Tuning().AirDamage, 95.f, 190.f, 80.f, EProtoBlow::Air, TEXT("Air slash"));
	}), 0.14f, false);
}

void AProtoCharacter::Plunge()
{
	const FProtoTuning& T = Tuning();
	bPlunging = true;
	Attacking = EProtoAttack::Plunge;
	Stamina -= T.LightCost;
	LastStaminaUse = GetNow();
	HangUntil = -100.f;
	LaunchCharacter(FVector(0.f, 0.f, -2600.f), true, true);
	PlayAnim(LightAnims[2], 1.6f);
}

void AProtoCharacter::StartHeavy()
{
	const FProtoTuning& T = Tuning();
	if (bDead || bLaunched) { return; }
	const float Now = GetNow();
	// in the air: the slam straight down (a blast on landing)
	if (GetCharacterMovement()->IsFalling() && !bPlunging && Now >= MantleUntil)
	{
		if (Stamina >= T.LightCost) { Plunge(); }
		return;
	}
	if (Now < AttackUntil)
	{
		// the combo's end: a light swing past its blow flows into the heavy
		if (Attacking != EProtoAttack::Light || Now < CancelAt) { return; }
		GetWorldTimerManager().ClearTimer(ComboTimer);
		AttackUntil = -100.f;
		Attacking = EProtoAttack::None;
	}
	if (!CanAct() || Stamina < T.HeavyCost) { return; }
	bCharging = true;
	ChargeStart = Now;
	HeavyPressAt = Now;
	bSprinting = false;
	float Lng = 0.f;
	ResolveAim(SwingDir, Lng);
	TurnYaw = SwingDir.Rotation().Yaw;
	TurnUntil = Now + 0.1f;
	// the wind-up: the charged strike's own start, slowed (released early it becomes the quick heavy)
	PlayAnim(ChargedAnim, 0.35f, 0.1f, 0.2f);
}

void AProtoCharacter::ReleaseHeavy()
{
	if (!bCharging) { return; }
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	const float Charge = ChargePct();
	const bool bQuick = Now - ChargeStart < T.TapTime;
	bCharging = false;
	if (bDead || bLaunched || Now < StaggerUntil || Now < StunUntil) { return; }
	Stamina -= T.HeavyCost;
	LastStaminaUse = Now;
	float Lng = 0.f;
	ResolveAim(SwingDir, Lng);
	float Damage = 0.f, Knock = 0.f, HitT = 0.f;
	EProtoBlow Kind = EProtoBlow::Heavy;
	const TCHAR* What = TEXT("Heavy strike");
	if (bQuick)
	{
		// a tap: the quick heavy (a shove that staggers)
		Damage = bTopDown ? T.NovaDamage : T.QuickHeavyDamage;
		Knock = bTopDown ? 600.f : 500.f;
		HitT = 0.2f;
		BeginSwing(EProtoAttack::Heavy, 0.55f, HitT, 0.34f);
		PlayAnim(ChargedAnim, 1.6f, 0.05f, 0.22f, 0.35f);
		if (bTopDown)
		{
			// the smash around (Hades' special): every enemy within 2.4 m, whatever the aim
			What = TEXT("Smash");
			GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this, Damage, Knock, What]()
			{
				DoHit(Damage, 99999.f, Tuning().NovaRadius, Knock, EProtoBlow::Heavy, What);
			}), HitT, false);
			return;
		}
		Lunge(SwingDir, FMath::Max(Lng, 50.f), 0.16f);
	}
	else
	{
		// held: the charged strike; a full charge throws the target up (jump after it and slash)
		Damage = T.HeavyBase + T.HeavyPerCharge * Charge;
		Knock = 600.f + 400.f * Charge;
		HitT = 0.28f;
		Kind = Charge >= 0.95f ? EProtoBlow::Launch : EProtoBlow::Heavy;
		What = Charge >= 0.95f ? TEXT("Launch") : TEXT("Charged strike");
		BeginSwing(EProtoAttack::Heavy, 0.72f, HitT, 0.4f);   // cancellable soon after the blow: jump after a launched target
		PlayAnim(ChargedAnim, 1.35f, 0.05f, 0.25f, 0.35f);
		Lunge(SwingDir, Lng * 0.8f, 0.18f);
	}
	GetWorldTimerManager().SetTimer(HitTimer, FTimerDelegate::CreateWeakLambda(this, [this, Damage, Knock, Kind, What]()
	{
		DoHit(Damage, 105.f, 195.f, Knock, Kind, What);
	}), HitT, false);
}

void AProtoCharacter::DoHit(float Damage, float Radius, float Reach, float Knockback, EProtoBlow Kind, const TCHAR* What)
{
	if (bDead || bLaunched) { return; }
	const float Now = GetNow();
	const FVector Fwd = SwingDir.GetSafeNormal2D();
	const FVector Me = GetActorLocation();
	const float MaxZ = Kind == EProtoBlow::Air ? 240.f : 150.f;
	bool bAny = false;
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
	{
		AProtoCharacter* C = *It;
		if (C == this || !C->IsAlive() || C->Team == Team) { continue; }
		const FVector To = C->GetActorLocation() - Me;
		if (FMath::Abs(To.Z) > MaxZ) { continue; }
		const float D = To.Size2D() - C->GetCapsuleComponent()->GetScaledCapsuleRadius();
		if (D > Reach) { continue; }
		// in front (a 120 degree cone), or pressed against the body
		if (D > Radius * 0.6f && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) < 0.5f) { continue; }
		float Dmg = Damage;
		const bool bCounter = Now < CounterUntil;
		if (bCounter) { Dmg *= Tuning().CounterMult; CounterUntil = -100.f; ++Counters; }
		const float Dealt = C->TakeBlow(Dmg, this, Knockback, Kind, bCounter ? TEXT("Counter") : What);
		if (Dealt <= 0.f) { continue; }
		LastDamageDealt = Dealt;
		++HitsLanded;
		if (Attacking != EProtoAttack::Wrath) { AddWrath(Dealt * 0.8f); }   // the call does not refill itself
		if (Kind == EProtoBlow::Air) { ++AirHits; }
		if (Kind == EProtoBlow::Launch) { ++Launches; }
		if (bCounter) { Numbers.Add({ C->GetActorLocation() + FVector(0.f, 0.f, 150.f), Dealt, Now, 6 }); }
		bAny = true;
		// the hit-stop: a few frames of weight, longer for the big blows
		C->CustomTimeDilation = 0.05f;
		FTimerHandle H;
		TWeakObjectPtr<AProtoCharacter> WeakC(C);
		const bool bBig = Kind == EProtoBlow::Heavy || Kind == EProtoBlow::Launch;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [WeakC]() { if (WeakC.IsValid()) { WeakC->CustomTimeDilation = 1.f; } }), bBig ? 0.11f : 0.055f, false);
	}
	if (bAny)
	{
		const bool bBig = Kind == EProtoBlow::Heavy || Kind == EProtoBlow::Launch;
		CustomTimeDilation = 0.05f;
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() { CustomTimeDilation = 1.f; }), bBig ? 0.11f : 0.055f, false);
		Kick(bBig ? 1.f : 0.45f);
	}
}

void AProtoCharacter::SlowMo()
{
	// a perfect dodge: the world slows for half a second (real time), the counter's window stays open
	const FProtoTuning& T = Tuning();
	UGameplayStatics::SetGlobalTimeDilation(this, T.SlowMoScale);
	FTimerHandle H;
	TWeakObjectPtr<UWorld> W = GetWorld();
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([W]() { if (W.IsValid()) { UGameplayStatics::SetGlobalTimeDilation(W.Get(), 1.f); } }), T.SlowMoSeconds * T.SlowMoScale, false);
}

float AProtoCharacter::TakeBlow(float Damage, AProtoCharacter* Source, float Knockback, EProtoBlow Kind, const TCHAR* What)
{
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	if (bDead) { return 0.f; }
	if (Now < InvulnUntil)
	{
		if (Source && Now - DodgeStart <= T.PerfectWindow)
		{
			// the perfect dodge: out of the blow at the last moment — the attacker loses its balance, a counter opens
			++PerfectDodges;
			CounterUntil = Now + T.CounterTime;
			Source->StaggerUntil = FMath::Max(Source->StaggerUntil, Now + 0.35f);
			Numbers.Add({ GetActorLocation() + FVector(0.f, 0.f, 130.f), 0.f, Now, 5 });
			UE_LOG(LogProto, Display, TEXT("PROTO t=%.2f evt=perfect_dodge who=%s from=%s"), Now, *GetName(), *Source->GetName());
			if (!bIsBot) { SlowMo(); }
		}
		else { Numbers.Add({ GetActorLocation() + FVector(0.f, 0.f, 110.f), 0.f, Now, 4 }); }
		return 0.f;
	}
	const FVector From = Source ? Source->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
	const FVector ToSource = (From - GetActorLocation()).GetSafeNormal2D();
	const float Facing = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToSource);
	int32 NumberKind = Kind == EProtoBlow::Launch ? 7 : 0;
	// from behind, unaware: the sneak attack
	if (Source && !bAlerted && Facing < -0.2f)
	{
		Damage *= T.BackstabMult;
		NumberKind = 2;
		++Source->Backstabs;
	}
	Health = FMath::Max(0.f, Health - Damage);
	AddWrath(Damage * 1.2f);
	LastAttacker = Source;
	LastHitAt = Now;
	bAlerted = true;
	AlertedAt = Now;
	Numbers.Add({ GetActorLocation() + FVector(FMath::FRandRange(-30.f, 30.f), 0.f, 110.f), Damage, Now, NumberKind });
	if (Numbers.Num() > 64) { Numbers.RemoveAt(0, Numbers.Num() - 64); }
	UE_LOG(LogProto, Display, TEXT("PROTO t=%.2f evt=hit what=%s from=%s to=%s dmg=%.1f kind=%d hp=%.0f"), Now, What, Source ? *Source->GetName() : TEXT("-"), *GetName(), Damage, (int32)Kind, Health);
	// a blow interrupts: the swing, the charge, the combo
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(ComboTimer);
	bCharging = false;
	bComboQueued = false;
	AttackUntil = -100.f;
	Attacking = EProtoAttack::None;
	Kick(Kind == EProtoBlow::Light || Kind == EProtoBlow::Air ? 0.6f : 1.2f);
	if (Health <= 0.f) { Die(Source, From); return Damage; }
	React(Kind, From);
	// the push: along the blow; a launch throws up, an air slash keeps an airborne body up (the juggle)
	UCharacterMovementComponent* M = GetCharacterMovement();
	switch (Kind)
	{
	case EProtoBlow::Launch:
		bLaunched = true;
		LaunchCharacter(-ToSource * 250.f + FVector(0.f, 0.f, T.LaunchUp), true, true);
		break;
	case EProtoBlow::Air:
		if (M->IsFalling()) { M->Velocity.Z = FMath::Max(M->Velocity.Z, 180.f); HangUntil = Now + 0.3f; }
		else { LaunchCharacter(-ToSource * Knockback, true, false); }
		break;
	case EProtoBlow::Heavy:
	case EProtoBlow::Plunge:
		LaunchCharacter(-ToSource * Knockback + FVector(0.f, 0.f, 220.f), true, false);
		break;
	default:
		if (Knockback > 0.f) { LaunchCharacter(-ToSource * Knockback + FVector(0.f, 0.f, 40.f), true, false); }
		break;
	}
	return Damage;
}

void AProtoCharacter::React(EProtoBlow Kind, const FVector& From)
{
	const float Now = GetNow();
	const FVector ToSource = (From - GetActorLocation()).GetSafeNormal2D();
	const bool bFront = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToSource) > -0.1f;
	// the reaction by the blow's weight and its side (seven of the template's hit reactions)
	switch (Kind)
	{
	case EProtoBlow::Light:
	case EProtoBlow::Air:
		StaggerUntil = FMath::Max(StaggerUntil, Now + 0.26f);
		PlayAnim(bFront ? ReactLight[FMath::RandRange(0, 3)] : ReactBack, 1.35f, 0.04f, 0.2f);
		break;
	case EProtoBlow::Medium:
		StaggerUntil = FMath::Max(StaggerUntil, Now + 0.42f);
		PlayAnim(bFront ? ReactMedium[FMath::RandRange(0, 1)] : ReactBack, 1.25f, 0.04f, 0.2f);
		break;
	case EProtoBlow::Launch:
		StaggerUntil = FMath::Max(StaggerUntil, Now + 0.5f);   // the rest waits for the landing
		PlayAnim(ReactHeavy, 0.9f, 0.04f, 0.3f);
		break;
	default:
		StaggerUntil = FMath::Max(StaggerUntil, Now + 0.62f);
		PlayAnim(bFront ? ReactHeavy : ReactBack, 1.1f, 0.04f, 0.25f);
		break;
	}
}

void AProtoCharacter::Die(AProtoCharacter* Killer, const FVector& From)
{
	if (bDead) { return; }
	bDead = true;
	bCharging = false;
	bLaunched = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// the fall by the blow's side (six of the template's deaths)
	const FVector ToSource = (From - GetActorLocation()).GetSafeNormal2D();
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	const float Front = FVector::DotProduct(Fwd, ToSource);
	const float Side = FVector::DotProduct(GetActorRightVector().GetSafeNormal2D(), ToSource);
	const TCHAR* Anim = Front > 0.4f ? DeathFront[FMath::RandRange(0, 2)] : (Front < -0.4f ? DeathBack : (Side > 0.f ? DeathRight : DeathLeft));
	PlayAnim(Anim, 1.f, 0.1f, 0.f);
	// hold the last frame (no blend out): the montage stays on its end
	if (UAnimInstance* A = GetMesh()->GetAnimInstance()) { if (UAnimMontage* M = A->GetCurrentActiveMontage()) { M->bEnableAutoBlendOut = false; } }
	if (Killer) { ++Killer->Knockouts; }
	if (AProtoGameMode* GM = GetWorld()->GetAuthGameMode<AProtoGameMode>()) { GM->OnKnockout(this, Killer); }
}

bool AProtoCharacter::TryMantle()
{
	const FProtoTuning& T = Tuning();
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (!M->IsFalling() || GetNow() < MantleUntil || bLaunched) { return false; }
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Rad = GetCapsuleComponent()->GetScaledCapsuleRadius();
	FVector Fwd = GetLastMovementInputVector().GetSafeNormal2D();
	if (Fwd.IsNearlyZero()) { Fwd = GetActorForwardVector().GetSafeNormal2D(); }
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, Half);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ProtoMantle), false, this);
	// the wall: at the feet, the knees or the chest (a low ledge is below the capsule's centre at the jump's top)
	FHitResult Wall;
	bool bWall = false;
	for (const float Up : { 12.f, 60.f, Half })
	{
		const FVector From = Feet + FVector(0.f, 0.f, Up);
		if (GetWorld()->LineTraceSingleByChannel(Wall, From, From + Fwd * (Rad + 60.f), ECC_Visibility, Q) && FMath::Abs(Wall.ImpactNormal.Z) < 0.3f) { bWall = true; break; }
	}
	if (!bWall) { if (bDebugMantle) { UE_LOG(LogProto, Display, TEXT("PROTO dbg mantle: no wall feet=%.0f vz=%.0f"), Feet.Z, M->Velocity.Z); } return false; }
	// the top: down from above the reach, just past the wall's face
	const FVector Over = Wall.ImpactPoint + Fwd * (Rad + 15.f);
	FHitResult Top;
	if (!GetWorld()->LineTraceSingleByChannel(Top, FVector(Over.X, Over.Y, Feet.Z + T.MantleReach + 60.f), FVector(Over.X, Over.Y, Feet.Z - 30.f), ECC_Visibility, Q)) { return false; }
	if (!Top.bBlockingHit || Top.ImpactNormal.Z < 0.7f) { return false; }
	const float Rise = Top.ImpactPoint.Z - Feet.Z;
	if (bDebugMantle) { UE_LOG(LogProto, Display, TEXT("PROTO dbg mantle: feet=%.0f top=%.0f rise=%.0f"), Feet.Z, Top.ImpactPoint.Z, Rise); }
	if (Rise < 0.f || Rise > T.MantleReach) { return false; }
	const FVector Dest = FVector(Over.X, Over.Y, Top.ImpactPoint.Z + Half + 3.f);
	FHitResult Block;
	if (GetWorld()->SweepSingleByChannel(Block, Dest, Dest, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Rad * 0.95f, Half * 0.95f), Q)) { return false; }
	// up and over (0.28 s), then walking on top
	M->StopMovementImmediately();
	M->SetMovementMode(MOVE_Flying);
	MantleFrom = GetActorLocation();
	MantleTo = Dest;
	MantleStart = GetNow();
	MantleUntil = MantleStart + 0.28f;
	SetActorRotation(Fwd.Rotation());
	PlayAnim(TEXT("Unarmed/Jump/MM_Jump"), 1.6f, 0.05f, 0.2f);
	++Mantles;
	return true;
}

bool AProtoCharacter::TryWallJump()
{
	const FProtoTuning& T = Tuning();
	if (bWallJumped || bLaunched || !GetCharacterMovement()->IsFalling()) { return false; }
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ProtoWall), false, this);
	FHitResult Wall;
	FVector Fwd = GetLastMovementInputVector().GetSafeNormal2D();
	if (Fwd.IsNearlyZero()) { Fwd = GetActorForwardVector().GetSafeNormal2D(); }
	if (!GetWorld()->LineTraceSingleByChannel(Wall, GetActorLocation(), GetActorLocation() + Fwd * (GetCapsuleComponent()->GetScaledCapsuleRadius() + 75.f), ECC_Visibility, Q)) { return false; }
	if (FMath::Abs(Wall.ImpactNormal.Z) > 0.3f) { return false; }
	const FVector Out = Wall.ImpactNormal.GetSafeNormal2D();
	LaunchCharacter(Out * T.WallJumpOut + FVector(0.f, 0.f, T.WallJumpUp), true, true);
	SetActorRotation(Out.Rotation());
	PlayAnim(TEXT("Unarmed/Jump/MM_WallJump"), 1.2f, 0.05f, 0.2f);
	bWallJumped = true;
	++WallJumps;
	return true;
}

void AProtoCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	bWallJumped = false;
	AirSlashesLeft = T.AirSlashes;
	HangUntil = -100.f;
	GetCharacterMovement()->GravityScale = T.Gravity;
	if (Attacking == EProtoAttack::Air) { AttackUntil = FMath::Min(AttackUntil, Now + 0.05f); }
	if (bLaunched)
	{
		// thrown up and down again: the landing, a moment to get the feet back
		bLaunched = false;
		StaggerUntil = FMath::Max(StaggerUntil, Now + 0.4f);
		PlayAnim(TEXT("Unarmed/Jump/MM_Land"), 1.3f, 0.05f, 0.25f);
	}
	else if (bPlunging)
	{
		bPlunging = false;
		Attacking = EProtoAttack::Plunge;
		AttackUntil = Now + 0.35f;
		CancelAt = Now + 0.25f;
		PlayAnim(TEXT("Unarmed/Jump/MM_Land"), 1.3f, 0.03f, 0.15f);
		++Plunges;
		Kick(1.2f);
		for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It)
		{
			AProtoCharacter* C = *It;
			if (C == this || !C->IsAlive() || C->Team == Team) { continue; }
			const FVector To = C->GetActorLocation() - GetActorLocation();
			if (To.Size2D() > T.PlungeRadius || FMath::Abs(To.Z) > 200.f) { continue; }
			float Dmg = T.PlungeDamage;
			if (Now < CounterUntil) { Dmg *= T.CounterMult; CounterUntil = -100.f; ++Counters; }
			if (C->TakeBlow(Dmg, this, 380.f, EProtoBlow::Plunge, TEXT("Ground slam")) > 0.f) { ++HitsLanded; }
		}
	}
	else if (FallSpeed > 1250.f)
	{
		// a hard landing (a long fall): the landing animation and a short brake
		PlayAnim(TEXT("Unarmed/Jump/MM_Land"), 1.5f, 0.04f, 0.2f);
		StaggerUntil = FMath::Max(StaggerUntil, Now + 0.12f);
	}
	FallSpeed = 0.f;
}

void AProtoCharacter::RunBuffered()
{
	if (Buffered == EBuffered::None) { return; }
	const float Now = GetNow();
	if (Now - BufferedAt > Tuning().BufferTime) { Buffered = EBuffered::None; return; }
	const bool bAir = GetCharacterMovement()->IsFalling();
	const EBuffered B = Buffered;
	if (B == EBuffered::Light && bAir && Now >= CancelAt && Now >= MantleUntil) { Buffered = EBuffered::None; LightAttack(); return; }
	if (!CanAct()) { return; }
	Buffered = EBuffered::None;
	switch (B)
	{
	case EBuffered::Light: LightAttack(); break;
	case EBuffered::Dodge: Dodge(BufferedDir); break;
	case EBuffered::Jump: PressJump(); break;
	default: break;
	}
}

void AProtoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FProtoTuning& T = Tuning();
	const float Now = GetNow();
	UCharacterMovementComponent* M = GetCharacterMovement();
	// the mantle's move
	if (MantleStart > 0.f)
	{
		const float A = FMath::Clamp((Now - MantleStart) / FMath::Max(0.01f, MantleUntil - MantleStart), 0.f, 1.f);
		// up first, then over: the path bends
		const FVector Up(MantleFrom.X, MantleFrom.Y, MantleTo.Z);
		const FVector P = A < 0.6f ? FMath::Lerp(MantleFrom, Up, A / 0.6f) : FMath::Lerp(Up, MantleTo, (A - 0.6f) / 0.4f);
		SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		if (A >= 1.f)
		{
			MantleStart = -1.f;
			M->SetMovementMode(MOVE_Walking);
		}
	}
	// the air: the plunge keeps its speed down, an air slash hangs, the fall speed is kept for the landing
	if (M->IsFalling())
	{
		if (bPlunging) { M->Velocity.Z = FMath::Min(M->Velocity.Z, -2200.f); }
		const bool bHang = Now < HangUntil && !bPlunging;
		M->GravityScale = bHang ? 0.22f : T.Gravity;
		if (bHang) { M->Velocity.Z = FMath::Max(M->Velocity.Z, -60.f); }
		FallSpeed = FMath::Max(FallSpeed, -M->Velocity.Z);
	}
	// a ledge met in the air with the key held forward: climb it (the jump key also does)
	if (!bDead && !bLaunched && M->IsFalling() && !bPlunging && M->Velocity.Z < 150.f && !GetLastMovementInputVector().IsNearlyZero() && Now >= MantleUntil + 0.2f && Attacking != EProtoAttack::Air)
	{
		TryMantle();
	}
	// stamina: sprint drains it, a pause refills it
	if (bSprinting && GetVelocity().Size2D() > 100.f)
	{
		Stamina -= T.SprintDrain * DeltaSeconds;
		LastStaminaUse = Now;
		if (Stamina <= 0.f) { Stamina = 0.f; bSprinting = false; }
	}
	else if (Now - LastStaminaUse > T.StaminaDelay && !bCharging)
	{
		Stamina = FMath::Min(T.MaxStamina, Stamina + T.StaminaRegen * DeltaSeconds);
	}
	if (CastAmmo <= 0 && Now >= CastBackAt) { CastAmmo = 1; }
	UpdateSpeed();
	// the facing: a swing turns quickly to its way; a lock keeps the body on the enemy (the 8-way jog: circling,
	// backing off); otherwise the body turns to where it runs
	const bool bFaceLock = IsLocked() && !bSprinting && !bDead && !bLaunched && Now >= DodgeUntil && Now >= MantleUntil;
	M->bOrientRotationToMovement = !bFaceLock;
	if (!bDead && Now < TurnUntil)
	{
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, TurnYaw, 0.f), DeltaSeconds, 2000.f));
	}
	else if (bFaceLock && !IsAttacking())
	{
		const FVector To = LockTarget->GetActorLocation() - GetActorLocation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, To.Rotation().Yaw, 0.f), DeltaSeconds, 10.f));
	}
	RunBuffered();
	// the player is always aware (the sneak attack is on the bots); the unaware go back to unaware after a while
	if (!bIsBot) { bAlerted = true; }
	else if (bAlerted && Now - AlertedAt > 8.f && Now - LastHitAt > 8.f) { bAlerted = false; }
	// the player's camera: wider in a sprint, closer while charging, a kick on a blow, the zoom (the mouse wheel)
	if (!bIsBot && IsLocallyControlled())
	{
		const float Dt = FMath::Min(DeltaSeconds / FMath::Max(0.01f, CustomTimeDilation), 0.05f);
		const float WantFov = BaseFov + (bSprinting && GetVelocity().Size2D() > T.RunSpeed + 60.f ? 8.f : 0.f) - (bCharging ? 4.f : 0.f);
		Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, WantFov, Dt, 6.f));
		SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, ArmLength - (bCharging ? 60.f : 0.f), Dt, 8.f);
		const float Age = Now - ShakeAt;
		if (Age >= 0.f && Age < 0.22f)
		{
			const float Amp = ShakeStrength * (1.f - Age / 0.22f) * 7.f;
			SpringArm->SocketOffset = (bTopDown ? FVector::ZeroVector : ArmSocket) + FVector(0.f, FMath::FRandRange(-Amp, Amp), FMath::FRandRange(-Amp, Amp));
		}
		else { SpringArm->SocketOffset = bTopDown ? FVector::ZeroVector : ArmSocket; ShakeStrength = 0.f; }
	}
}
