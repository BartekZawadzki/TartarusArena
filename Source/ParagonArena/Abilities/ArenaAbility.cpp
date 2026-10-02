#include "Abilities/ArenaAbility.h"
#include "AbilitySystemComponent.h"
#include "Heroes/ArenaCharacter.h"
#include "Arena/ArenaFx.h"
#include "Arena/ArenaPhysics.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Components/CapsuleComponent.h"
#include "Arena/ArenaIndicator.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarArenaAimAssist(TEXT("arena.AimAssist"), 1, TEXT("Aim assistance for the player's attacks (1 on, 0 off)"));

// ---------------------------------------------------------------------------------------------
namespace ArenaAbilityHelpers
{
	float ScaledDamage(const AArenaCharacter* Source, const FArenaAbilityDef& Ab, float BaseDamage)
	{
		return FMath::Max(0.f, (BaseDamage + (Source ? Ab.PowerScale * Source->GetPower() : 0.f)) * (Source ? Source->DamageMultiplier() : 1.f));
	}

	void ApplyHitEffects(AArenaCharacter* Source, AArenaCharacter* Target, const FArenaAbilityDef& Ab, float BaseDamage)
	{
		if (!Target || !Source || !Source->IsHostileTo(Target)) { return; } // VR-04
		const bool bBasic = Ab.Slot == 0;
		const ArenaCore::FItemPassives& P = Source->Passives();
		if (BaseDamage > 0.f || (bBasic && P.OnHit > 0.f))
		{
			float Raw = ScaledDamage(Source, Ab, BaseDamage);
			if (Ab.Slot >= 1 && P.AbilityDamagePct > 0.f) { Raw *= 1.f + P.AbilityDamagePct; }                  // Korona archonta
			if (bBasic)
			{
				Raw += P.OnHit;                                                                                 // Falconer's Bow
				if (P.SpellbladePower > 0.f && Source->ConsumeSpellblade()) { Raw += P.SpellbladePower * Source->GetPower(); }   // Sorcerer's Blade
			}
			if (P.ExecuteBonus > 0.f && Target->HealthPct() < P.ExecuteBelow) { Raw *= 1.f + P.ExecuteBonus; }  // Executioner's Blade
			FArenaHit Hit;
			Hit.bUltimate = Ab.bUltimate;
			Hit.bBasic = bBasic;
			Hit.Ability = Ab.Name;
			Hit.ArmorPen = Source->ItemBonus().ArmorPen;
			if (bBasic) { Hit.bCrit = Source->RollCrit(); Hit.CritMult = P.CritMult; }                      // items only, every n-th hit
			const float Dealt = Target->ReceiveHit(Raw, Source, Hit);
			if (Dealt > 0.f && Source->ItemBonus().LifestealPct > 0.f) { Source->ReceiveHeal(Dealt * Source->ItemBonus().LifestealPct, Source, false); }   // items: lifesteal
			if (Dealt > 0.f && bBasic && !Source->IsMinion() && !Source->IsRangedKit()) { Source->ReceiveHeal(Dealt * FArenaDatabase::Get().Rules.MeleeLifesteal, Source, false); }   // melee sustain (v17)
			if (Dealt > 0.f && bBasic && P.EchoSeconds > 0.f && !Target->IsMinion()) { Source->ReduceAbilityCooldowns(P.EchoSeconds); }                  // Sage's Hourglass
		}
		ArenaFx::Sound(Target, Ab.ImpactSound, Target->GetActorLocation(), 0.8f);
		if (!Target->IsAlive()) { return; }
		if (Ab.StunSeconds > 0.f) { Target->ApplyStun(Ab.StunSeconds); }
		if (Ab.SlowPct > 0.f) { Target->ApplySlow(Ab.SlowPct, Ab.SlowSeconds); }
		if (Ab.Knockback > 0.f || Ab.KnockUp > 0.f)
		{
			// a fixed displacement: the same distance, height and air time every time (VR-21)
			const FVector Dir = (Target->GetActorLocation() - Source->GetActorLocation()).GetSafeNormal2D();
			Target->Displace(Dir, ArenaCore::KnockDistanceCm(Ab.Knockback), ArenaCore::KnockHeightCm(Ab.KnockUp), ArenaCore::KnockAirSeconds(Ab.KnockUp));
			if (Ab.Knockback >= 3.f || Ab.KnockUp >= 2.5f) { Target->OnKnocked(Dir, Ab.KnockUp > Ab.Knockback * 0.8f); }
		}
	}

	bool LineOfSight(const UWorld* World, const FVector& From, const FVector& To, const AActor* IgnoreA, const AActor* IgnoreB)
	{
		if (!World) { return true; }
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaLOS), false);
		if (IgnoreA) { Q.AddIgnoredActor(IgnoreA); }
		if (IgnoreB) { Q.AddIgnoredActor(IgnoreB); }
		FHitResult Hit;
		return !World->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Q);
	}

	FVector AimDirection(const AArenaCharacter* Hero, const FVector& Aim)
	{
		FVector Dir = Aim - Hero->GetActorLocation();
		Dir.Z = 0.f;
		return Dir.SizeSquared() < 100.f || Aim.IsNearlyZero() ? Hero->GetActorForwardVector().GetSafeNormal2D() : Dir.GetSafeNormal();
	}

	FVector GroundTarget(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& Aim)
	{
		const FVector Origin = Hero->GetActorLocation();
		const float MaxRange = Ab.Range * 100.f;
		FVector Target = Aim.IsNearlyZero() ? Origin + Hero->GetActorForwardVector() * MaxRange : Aim;
		if (FVector::Dist2D(Target, Origin) > MaxRange) { Target = Origin + AimDirection(Hero, Aim) * MaxRange; }
		FHitResult Ground;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaGround), false, Hero);
		if (Hero->GetWorld()->LineTraceSingleByObjectType(Ground, Target + FVector(0, 0, 800.f), Target - FVector(0, 0, 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q)) { Target = Ground.ImpactPoint; }
		return Target;
	}

	float ProjectileReach(const AArenaCharacter* Hero, const FVector& Dir, float RangeCm)
	{
		const FVector Start = Hero->GetActorLocation() + FVector(0, 0, 50.f);
		// along the same line the shot flies (up or down a ramp towards the aim), measured on the ground
		FVector D3 = Dir;
		if (!Hero->AimPoint.IsNearlyZero())
		{
			D3 = (Hero->AimPoint - Start).GetSafeNormal();
			D3.Z = FMath::Clamp(D3.Z, -0.25f, 0.25f);
			D3 = (Dir.GetSafeNormal2D() * FMath::Sqrt(FMath::Max(0.f, 1.f - D3.Z * D3.Z)) + FVector(0.f, 0.f, D3.Z)).GetSafeNormal();
		}
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaShotReach), false, Hero);
		if (Hero->GetWorld()->SweepSingleByObjectType(Hit, Start, Start + D3 * RangeCm, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(22.f), Q))
		{
			return FMath::Max(50.f, Hit.Distance * D3.Size2D());
		}
		return RangeCm * D3.Size2D();
	}

	bool AimAssistOn() { return CVarArenaAimAssist.GetValueOnGameThread() != 0; }

	AArenaCharacter* FindAimTarget(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& ViewOrigin, const FVector& ViewDir)
	{
		if (!Hero || !AimAssistOn()) { return nullptr; }
		const bool bMelee = Ab.Archetype == EArenaArchetype::Melee;
		if (!bMelee && !(Ab.Archetype == EArenaArchetype::Projectile && Ab.Count <= 1)) { return nullptr; }
		const float Reach = Ab.Range * 100.f;
		const FVector Me = Hero->GetActorLocation();
		const FVector View = ViewDir.GetSafeNormal();
		const FVector View2D = View.GetSafeNormal2D();
		AArenaCharacter* Best = nullptr;
		float BestScore = TNumericLimits<float>::Max();
		for (TActorIterator<AArenaCharacter> It(Hero->GetWorld()); It; ++It)
		{
			AArenaCharacter* T = *It;
			if (!Hero->IsHostileTo(T)) { continue; }
			const FVector C = T->GetActorLocation();
			const float R = T->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const float Dist2D = FVector::Dist2D(C, Me);
			if (FMath::Abs(C.Z - Me.Z) > 220.f && bMelee) { continue; }
			float Score = 0.f;
			if (bMelee)
			{
				// the swing turns to an enemy within reach (plus a short step) inside 65 degrees of where you look
				if (Dist2D - R > Reach + 40.f) { continue; }
				const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector::DotProduct((C - Me).GetSafeNormal2D(), View2D), -1.f, 1.f)));
				if (Angle > 65.f) { continue; }
				Score = Angle + Dist2D * 0.02f;
			}
			else
			{
				// the shot bends to an enemy whose body is within ~0.6 m + 3.5 % of the distance of the crosshair ray
				if (Dist2D - R > Reach) { continue; }
				const FVector ToC = C - ViewOrigin;
				const float Along = FVector::DotProduct(ToC, View);
				if (Along < 0.f) { continue; }
				const float Perp = FMath::Max(0.f, (float)(ToC - View * Along).Size() - R);
				const float Allowed = 60.f + Along * 0.035f;
				if (Perp > Allowed) { continue; }
				Score = Perp / Allowed + (T->IsMinion() ? 0.15f : 0.f);   // heroes win a tie
			}
			if (!LineOfSight(Hero->GetWorld(), Me + FVector(0, 0, 50.f), C, Hero, T)) { continue; }
			if (Score < BestScore) { BestScore = Score; Best = T; }
		}
		return Best;
	}

	/** Where a lobbed shot comes down: the aimed ground point, pulled in to the ability's range. */
	FVector LobLanding(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& Aim)
	{
		return GroundTarget(Hero, Ab, Aim);
	}

	FVector LeadPoint(const AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const AArenaCharacter* Target)
	{
		FVector P = Target->GetActorLocation() + FVector(0, 0, 15.f);                 // chest
		if (Ab.Archetype != EArenaArchetype::Projectile || Ab.Gravity > 0.f) { return P; }
		const FVector Muzzle = Hero->GetActorLocation() + FVector(0, 0, 50.f);
		const float Speed = FMath::Max(500.f, Ab.Speed * 100.f);
		FVector V = Target->GetVelocity();
		V.Z = 0.f;                                                                    // do not chase a jump
		for (int32 i = 0; i < 2; ++i) { P = Target->GetActorLocation() + FVector(0, 0, 15.f) + V * FMath::Min(1.f, (float)FVector::Dist(Muzzle, P) / Speed); }
		return P;
	}

	void FlashSwing(AArenaCharacter* Hero, const FArenaAbilityDef& Ab, const FVector& Dir, bool bConnected)
	{
		const FVector Feet = Hero->GetActorLocation() - FVector(0, 0, Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FLinearColor C = bConnected ? FMath::Lerp(FArenaDatabase::Hex(Ab.Color), FLinearColor::White, 0.5f) : FLinearColor(0.55f, 0.55f, 0.6f);
		AArenaIndicator::FlashCone(Hero->GetWorld(), Feet, Dir.Rotation().Yaw, Ab.Range * 100.f, Ab.Angle >= 300.f ? PI : FMath::DegreesToRadians(Ab.Angle * 0.5f), C, bConnected ? 0.18f : 0.14f);
	}

	AArenaCharacter* LowestAlly(AArenaCharacter* Hero, const FArenaAbilityDef& Ab)
	{
		AArenaCharacter* Lowest = nullptr;
		for (TActorIterator<AArenaCharacter> It(Hero->GetWorld()); It; ++It)
		{
			AArenaCharacter* A = *It;
			if (!A->IsAlive() || A->GetTeam() != Hero->GetTeam() || A->IsMinion()) { continue; }   // heroes only (v12)
			if (FVector::Dist(A->GetActorLocation(), Hero->GetActorLocation()) > Ab.Range * 100.f) { continue; }
			if (!Lowest || A->HealthPct() < Lowest->HealthPct()) { Lowest = A; }
		}
		return Lowest;
	}

	int32 DamageInRadius(AArenaCharacter* Source, const FVector& Center, float RadiusCm, const FArenaAbilityDef& Ab, float BaseDamage, TSet<uint64>* Registry, float ConeCos, const FVector& Forward)
	{
		if (!Source) { return 0; }
		int32 N = 0;
		const float HalfCone = ConeCos > -1.5f ? FMath::Acos(FMath::Clamp(ConeCos, -1.f, 1.f)) : PI;
		for (TActorIterator<AArenaCharacter> It(Source->GetWorld()); It; ++It)
		{
			AArenaCharacter* T = *It;
			if (!Source->IsHostileTo(T)) { continue; }
			// the target's body counts, not its centre: a blow that reaches its capsule edge lands
			const float R = T->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const float Half = T->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector To = T->GetActorLocation() - Center;
			const float Dist2D = To.Size2D();
			if (Dist2D - R > RadiusCm) { continue; }
			if (FMath::Abs(To.Z) > Half + 160.f) { continue; }                  // another level (the ridge above the pit) is out of reach
			if (HalfCone < PI && Dist2D > 1.f)
			{
				const float Angle = FMath::Acos(FMath::Clamp((float)FVector::DotProduct(To.GetSafeNormal2D(), Forward.GetSafeNormal2D()), -1.f, 1.f));
				if (Angle - FMath::Atan(R / Dist2D) > HalfCone) { continue; }   // cone widened by the target's own width
			}
			// walls, rocks and trees shield (units and props do not)
			if (!LineOfSight(Source->GetWorld(), Center + FVector(0, 0, 60.f), T->GetActorLocation(), Source, T)) { continue; }
			bool bAlready = false;
			if (Registry) { Registry->Add(T->GetUniqueID(), &bAlready); }
			if (bAlready) { continue; }                                              // VR-03
			ApplyHitEffects(Source, T, Ab, BaseDamage);
			++N;
		}
		return N;
	}

	static void ApplyBuff(AArenaCharacter* Hero, const FArenaAbilityDef& Ab)
	{
		TArray<AArenaCharacter*> Targets;
		if (Ab.BuffTarget == EArenaBuffTarget::Self) { Targets.Add(Hero); }
		else if (Ab.BuffTarget == EArenaBuffTarget::LowestAlly) { if (AArenaCharacter* L = LowestAlly(Hero, Ab)) { Targets.Add(L); } }
		else
		{
			for (TActorIterator<AArenaCharacter> It(Hero->GetWorld()); It; ++It)
			{
				AArenaCharacter* A = *It;
				if (A->IsAlive() && A->GetTeam() == Hero->GetTeam() && FVector::Dist(A->GetActorLocation(), Hero->GetActorLocation()) <= Ab.Radius * 100.f) { Targets.Add(A); }
			}
		}
		for (AArenaCharacter* T : Targets)
		{
			if (Ab.Heal > 0.f) { T->ReceiveHeal(Ab.Heal + Ab.PowerScale * Hero->GetPower(), Hero); }
			if (Ab.Shield > 0.f) { T->ReceiveShield(Ab.Shield + Ab.PowerScale * Hero->GetPower()); }
			if (Ab.SpeedBuffPct > 0.f) { T->ApplySpeedBuff(Ab.SpeedBuffPct, Ab.BuffSeconds); }
			if (Ab.AttackSpeedBuffPct > 0.f) { T->ApplyAttackSpeedBuff(Ab.AttackSpeedBuffPct, Ab.BuffSeconds); }
			// the buff's effect rides on its bearer for as long as the buff lasts (a shield: a few seconds): a
			// one-off burst at the spot left no sign of who is buffed
			const float Last = Ab.BuffSeconds > 0.f ? Ab.BuffSeconds : (Ab.Shield > 0.f ? 4.f : 1.5f);
			T->ShowAura(FMath::Lerp(FArenaDatabase::Hex(Ab.Color), FLinearColor::White, 0.2f), Last);
			if (!ArenaFx::AttachFor(Ab.Fx, T->GetMesh(), FArenaDatabase::Hex(Ab.Color), Last)) { ArenaFx::Spawn(Hero, Ab.Fx, T->GetActorLocation(), FArenaDatabase::Hex(Ab.Color)); }
		}
	}
}

// ---------------------------------------------------------------------------------------------
UArenaAbility::UArenaAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// the server is the only authority (ADR-13): a LAN guest asks by ServerCast and sees the result as multicasts; the
	// engine's default (LocalPredicted) made the server tell the guest to run the whole ability on its own machine
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
	ActivationBlockedTags.AddTag(ArenaTags::State_Stunned);
	ActivationBlockedTags.AddTag(ArenaTags::State_Dead);
}

bool UArenaAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) { return false; }
	const AArenaCharacter* Hero = ActorInfo ? Cast<AArenaCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const FGameplayAbilitySpec* Spec = ActorInfo && ActorInfo->AbilitySystemComponent.IsValid() ? ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle) : nullptr;
	return Hero && Spec && Hero->CanCastSlot(Spec->InputID); // VR-02: cooldown + mana
}

float UArenaAbility::PlayAnim(AArenaCharacter* Hero, const FArenaAbilityDef& Ab)
{
	UAnimInstance* AnimInst = Hero->GetMesh() ? Hero->GetMesh()->GetAnimInstance() : nullptr;
	const FString& AnimPath = Ab.AnimVariants.Num() > 0 ? Ab.AnimVariants[VariantIndex++ % Ab.AnimVariants.Num()] : Ab.Anim;
	if (!AnimInst || AnimPath.IsEmpty()) { return 0.3f; }
	UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *AnimPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (UAnimMontage* M = Cast<UAnimMontage>(Obj)) { return FMath::Max(0.2f, AnimInst->Montage_Play(M, 1.2f)); }
	if (UAnimSequenceBase* S = Cast<UAnimSequenceBase>(Obj))
	{
		// the anim blueprint's upper-body slot: the legs keep walking while the arms swing (no sliding idle legs)
		UAnimMontage* Dyn = AnimInst->PlaySlotAnimationAsDynamicMontage(S, Hero->AnimSlot(true), 0.08f, 0.15f, 1.2f);
		return Dyn ? FMath::Max(0.2f, S->GetPlayLength() / 1.2f) : 0.3f;
	}
	return 0.3f;
}

void UArenaAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	AArenaCharacter* Hero = Cast<AArenaCharacter>(ActorInfo->AvatarActor.Get());
	const FGameplayAbilitySpec* Spec = GetCurrentAbilitySpec();
	if (!Hero || !Spec || !Hero->GetDef().Abilities.IsValidIndex(Spec->InputID))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const int32 Slot = Spec->InputID;
	const FArenaAbilityDef Ab = Hero->Ability(Slot);                          // every number at its rank (VR-20)
	Hero->SpendManaAndStartCooldown(Slot, Ab.ManaCost, Ab.Cooldown);          // commit (VR-02)
	if (Slot >= 1) { Hero->ArmSpellblade(); }                                 // items: Sorcerer's Blade
	const float AnimLen = PlayAnim(Hero, Ab);
	ArenaFx::Spawn(Hero, Ab.CastFx, Hero->GetActorLocation(), FArenaDatabase::Hex(Ab.Color));
	ArenaFx::Sound(Hero, Ab.Sound, Hero->GetActorLocation());
	FVector Aim = Hero->AimPoint.IsNearlyZero() ? Hero->GetActorLocation() + Hero->GetActorForwardVector() * 1000.f : Hero->AimPoint;
	// the player's swing or shot goes for the enemy the crosshair picked (see FindAimTarget); bots aim on their own
	TWeakObjectPtr<AArenaCharacter> Target;
	// (v17) a bot hero's melee swing gets the same turn and short lunge into its target: without them the 1v1 duels
	// showed Countess, Khaimera and Crunch swinging at air behind a ranged hero backing away (0-10 % of duels won)
	const bool bBotMelee = !Hero->bAssistedAim && !Hero->IsMinion() && Ab.Archetype == EArenaArchetype::Melee;
	if (Hero->AimTarget.IsValid() && Hero->AimTarget->IsAlive() && (bBotMelee || (Hero->bAssistedAim && ArenaAbilityHelpers::AimAssistOn()
		&& (Ab.Archetype == EArenaArchetype::Melee || (Ab.Archetype == EArenaArchetype::Projectile && Ab.Count <= 1)))))
	{
		Target = Hero->AimTarget;
		Aim = ArenaAbilityHelpers::LeadPoint(Hero, Ab, Target.Get());
		if (Ab.Archetype == EArenaArchetype::Melee)
		{
			// turn into the swing (the anim and the cone face the target)
			const FVector To = (Target->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D();
			if (!To.IsNearlyZero()) { Hero->HoldFacing(To.Rotation().Yaw, FMath::Max(Ab.Delay + 0.15f, 0.3f)); }
			const float Gap = FVector::Dist2D(Target->GetActorLocation(), Hero->GetActorLocation()) - Target->GetCapsuleComponent()->GetScaledCapsuleRadius() - Ab.Range * 100.f + 20.f;
			if (Gap > 0.f && !To.IsNearlyZero() && !Hero->IsDashing()) { Hero->Lunge(To, FMath::Min(Gap, 70.f)); }
		}
	}
	if (Ab.Archetype == EArenaArchetype::Targeted) { Target = Hero->AimTarget; }   // a tower's shot goes for the unit it picked
	// a hero turns to its aim for the cast (it runs facing its way; the pack has a forward jog only)
	if (!Hero->IsMinion() && !Hero->IsStructure() && Ab.Archetype != EArenaArchetype::Buff)
	{
		Hero->FaceAim(ArenaAbilityHelpers::AimDirection(Hero, Aim).Rotation().Yaw, FMath::Max(Ab.Delay + 0.3f, 0.45f));
	}
	if (Hero->GetNetMode() != NM_Standalone)
	{
		const int32 NumVariants = FMath::Max(1, Ab.AnimVariants.Num());
		Hero->MulticastCast((uint8)Slot, (uint8)(((VariantIndex - 1) % NumVariants + NumVariants) % NumVariants), Aim, Target.Get());
	}
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=cast hero=%s team=%d slot=%d ability=%s assist=%d"), Hero->GetWorld()->GetTimeSeconds(), *Hero->GetDef().Id.ToString(), Hero->GetTeam(), Slot, *Ab.Name, Target.IsValid() ? 1 : 0);

	FTimerHandle ExecTimer;
	TWeakObjectPtr<AArenaCharacter> WeakHero(Hero);
	Hero->GetWorldTimerManager().SetTimer(ExecTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakHero, Ab, Aim, Target]()
	{
		if (WeakHero.IsValid() && WeakHero->IsAlive() && !WeakHero->IsStunned()) { Execute(WeakHero.Get(), Ab, Aim, Target); }
	}), Ab.Archetype == EArenaArchetype::GroundAoE ? 0.01f : FMath::Max(0.01f, Ab.Delay), false);   // a ground area telegraphs its own delay from the cast (VR-21)

	FTimerHandle EndTimer;
	Hero->GetWorldTimerManager().SetTimer(EndTimer, FTimerDelegate::CreateWeakLambda(this, [this, Handle, ActorInfo, ActivationInfo]()
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}), Slot == 0 ? Ab.Delay + 0.05f : FMath::Max(Ab.Delay + 0.05f, FMath::Min(AnimLen, 1.2f)), false);   // a basic attack is ready again by its cooldown (attack speed counts), not by its animation
}

void UArenaAbility::Execute(AArenaCharacter* Hero, FArenaAbilityDef Ab, FVector Aim, TWeakObjectPtr<AArenaCharacter> Target)
{
	UWorld* World = Hero->GetWorld();
	const FVector Origin = Hero->GetActorLocation();
	// a swing sticks to its target while the blade comes down (it moved during the wind-up), if still in reach
	if (Ab.Archetype == EArenaArchetype::Melee && Target.IsValid() && Target->IsAlive()
		&& FVector::Dist2D(Target->GetActorLocation(), Origin) - Target->GetCapsuleComponent()->GetScaledCapsuleRadius() <= Ab.Range * 100.f + 40.f)
	{
		Aim = Target->GetActorLocation();
		if (Hero->bAssistedAim) { Hero->HoldFacing((Aim - Origin).GetSafeNormal2D().Rotation().Yaw, 0.15f); }
	}
	if (Ab.Archetype == EArenaArchetype::Projectile && Target.IsValid() && Target->IsAlive()) { Aim = ArenaAbilityHelpers::LeadPoint(Hero, Ab, Target.Get()); }
	const FVector Dir = ArenaAbilityHelpers::AimDirection(Hero, Aim);
	const FLinearColor Color = FArenaDatabase::Hex(Ab.Color);

	switch (Ab.Archetype)
	{
	case EArenaArchetype::Melee:
	{
		TSet<uint64> Hits;
		// the cone starts at the attacker: a target pressed against its side is inside the swing
		const float ConeCos = Ab.Angle >= 300.f ? -2.f : FMath::Cos(FMath::DegreesToRadians(Ab.Angle * 0.5f));
		const int32 Landed = ArenaAbilityHelpers::DamageInRadius(Hero, Origin, Ab.Range * 100.f, Ab, Ab.Damage, &Hits, ConeCos, Dir);
		if (Hero->bAssistedAim) { ArenaAbilityHelpers::FlashSwing(Hero, Ab, Dir, Landed > 0); }
		ArenaFx::Spawn(Hero, Ab.Fx, Origin + Dir * Ab.Range * 60.f, Color, ArenaFx::BlastScale(Ab.FxScale, Ab.bUltimate ? 5.f : 4.f, 0.25f, 1.f, 1.3f));
		if (Ab.bUltimate) { Hero->CameraShake(0.3f, 10.f); }
		break;
	}
	case EArenaArchetype::Projectile:
	{
		const int32 Count = FMath::Max(1, Ab.Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Yaw = (i - (Count - 1) * 0.5f) * Ab.Spread;
			FVector ShotDir = (Aim - (Origin + FVector(0, 0, 50.f))).GetSafeNormal();
			// a shot at a picked target flies straight at it (up to a ridge or down into the pit); a free shot stays
			// near level so a crosshair on the far ground does not bury it in the floor
			if (Ab.Gravity > 0.f) { ShotDir = (Dir + FVector(0, 0, 0.45f)).GetSafeNormal(); }
			else if (!Target.IsValid()) { ShotDir.Z = FMath::Clamp(ShotDir.Z, -0.25f, 0.25f); ShotDir.Normalize(); }
			ShotDir = ShotDir.RotateAngleAxis(Yaw, FVector::UpVector);
			FActorSpawnParameters P; P.Owner = Hero; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// leaves from the caster's body edge; an enemy pressed against the caster is caught by the spawn overlap
			const float Muzzle = Hero->GetCapsuleComponent()->GetScaledCapsuleRadius() + 15.f;
			const FVector Launch = Origin + FVector(0, 0, 50.f) + ShotDir.GetSafeNormal2D() * Muzzle;
			if (AArenaProjectile* Proj = World->SpawnActor<AArenaProjectile>(AArenaProjectile::StaticClass(), Launch, ShotDir.Rotation(), P))
			{
				Proj->Init(Hero, Ab, ShotDir);
				// a lobbed shot lands where it is aimed, within its range: the arc is solved for that point (it used to
				// leave at a fixed 24 degrees, sail over everything closer than ~35 m and vanish in the air)
				if (Ab.Gravity > 0.f) { Proj->LobTo(Launch, ArenaAbilityHelpers::LobLanding(Hero, Ab, Aim)); }
			}
		}
		if (Ab.bUltimate) { Hero->CameraShake(0.25f, 8.f); }
		break;
	}
	case EArenaArchetype::GroundAoE:
	{
		const FVector Spot = ArenaAbilityHelpers::GroundTarget(Hero, Ab, Aim);   // the spot the indicator showed
		FActorSpawnParameters P; P.Owner = Hero; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AArenaAreaEffect* Area = World->SpawnActor<AArenaAreaEffect>(AArenaAreaEffect::StaticClass(), Spot, FRotator::ZeroRotator, P)) { Area->Init(Hero, Ab); }
		break;
	}
	case EArenaArchetype::Dash:
	{
		Hero->StartDash(Ab.bBackwards ? -Dir : Dir, Ab.Distance * 100.f, Ab);
		Hero->GrantDashShield();   // v21: a melee hero leaps in behind a shield (no-op for the ranged)
		ArenaFx::Spawn(Hero, Ab.TrailFx.IsEmpty() ? FString(TEXT("/Game/Variant_Platforming/VFX/NS_Jump_Trail.NS_Jump_Trail")) : Ab.TrailFx, Origin, Color);
		break;
	}
	case EArenaArchetype::Targeted:
	{
		if (!Target.IsValid() || !Target->IsAlive()) { break; }
		FActorSpawnParameters P; P.Owner = Hero; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const float Half = Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Launch = Origin + FVector(0.f, 0.f, Hero->GetDef().MuzzleZ > 0.f ? Hero->GetDef().MuzzleZ * Hero->GetActorScale3D().Z - Half : Half * 0.8f);
		if (AArenaProjectile* Proj = World->SpawnActor<AArenaProjectile>(AArenaProjectile::StaticClass(), Launch, (Target->GetActorLocation() - Launch).Rotation(), P)) { Proj->InitHoming(Hero, Ab, Target.Get()); }
		break;
	}
	case EArenaArchetype::Buff:
		ArenaAbilityHelpers::ApplyBuff(Hero, Ab);
		if (Ab.bUltimate) { Hero->CameraShake(0.25f, 6.f); }
		break;
	}
}

void ArenaAbilityHelpers::PlayRemoteCast(AArenaCharacter* Hero, int32 Slot, int32 Variant, const FVector& Aim, AArenaCharacter* Target)
{
	if (!Hero || !Hero->GetDef().Abilities.IsValidIndex(Slot)) { return; }
	const FArenaAbilityDef Ab = Hero->Ability(Slot);
	const FLinearColor Color = FArenaDatabase::Hex(Ab.Color);
	// the animation the server played (the same variant of a combo)
	if (UAnimInstance* AnimInst = Hero->GetMesh() ? Hero->GetMesh()->GetAnimInstance() : nullptr)
	{
		const FString& AnimPath = Ab.AnimVariants.Num() > 0 ? Ab.AnimVariants[Variant % Ab.AnimVariants.Num()] : Ab.Anim;
		UObject* Obj = AnimPath.IsEmpty() ? nullptr : StaticLoadObject(UObject::StaticClass(), nullptr, *AnimPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UAnimMontage* M = Cast<UAnimMontage>(Obj)) { AnimInst->Montage_Play(M, 1.2f); }
		else if (UAnimSequenceBase* S = Cast<UAnimSequenceBase>(Obj)) { AnimInst->PlaySlotAnimationAsDynamicMontage(S, Hero->AnimSlot(true), 0.08f, 0.15f, 1.2f); }
	}
	ArenaFx::Spawn(Hero, Ab.CastFx, Hero->GetActorLocation(), Color);
	ArenaFx::Sound(Hero, Ab.Sound, Hero->GetActorLocation());
	TWeakObjectPtr<AArenaCharacter> WeakHero(Hero);
	TWeakObjectPtr<AArenaCharacter> WeakTarget(Target);
	FTimerHandle H;
	Hero->GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WeakHero, WeakTarget, Ab, Aim, Color]()
	{
		AArenaCharacter* Hr = WeakHero.Get();
		if (!Hr || !Hr->IsAlive()) { return; }
		UWorld* World = Hr->GetWorld();
		const FVector Origin = Hr->GetActorLocation();
		const FVector Dir = ArenaAbilityHelpers::AimDirection(Hr, Aim);
		FActorSpawnParameters P; P.Owner = Hr; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		switch (Ab.Archetype)
		{
		case EArenaArchetype::Melee:
			ArenaFx::Spawn(Hr, Ab.Fx, Origin + Dir * Ab.Range * 60.f, Color, ArenaFx::BlastScale(Ab.FxScale, Ab.bUltimate ? 5.f : 4.f, 0.25f, 1.f, 1.3f));
			break;
		case EArenaArchetype::Projectile:
		{
			const int32 Count = FMath::Max(1, Ab.Count);
			for (int32 i = 0; i < Count; ++i)
			{
				FVector ShotDir = (Aim - (Origin + FVector(0, 0, 50.f))).GetSafeNormal();
				if (Ab.Gravity > 0.f) { ShotDir = (Dir + FVector(0, 0, 0.45f)).GetSafeNormal(); }
				else { ShotDir.Z = FMath::Clamp(ShotDir.Z, -0.25f, 0.25f); ShotDir.Normalize(); }
				ShotDir = ShotDir.RotateAngleAxis((i - (Count - 1) * 0.5f) * Ab.Spread, FVector::UpVector);
				const FVector Launch = Origin + FVector(0, 0, 50.f) + ShotDir.GetSafeNormal2D() * (Hr->GetCapsuleComponent()->GetScaledCapsuleRadius() + 15.f);
				if (AArenaProjectile* Proj = World->SpawnActor<AArenaProjectile>(AArenaProjectile::StaticClass(), Launch, ShotDir.Rotation(), P))
				{
					Proj->bVisualOnly = true;
					Proj->Init(Hr, Ab, ShotDir);
					if (Ab.Gravity > 0.f) { Proj->LobTo(Launch, ArenaAbilityHelpers::LobLanding(Hr, Ab, Aim)); }
				}
			}
			break;
		}
		case EArenaArchetype::Targeted:
			if (AArenaCharacter* T = WeakTarget.Get())
			{
				const FVector Launch = Origin + FVector(0.f, 0.f, Hr->GetDef().MuzzleZ > 0.f ? Hr->GetDef().MuzzleZ * Hr->GetActorScale3D().Z - Hr->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : Hr->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.8f);
				if (AArenaProjectile* Proj = World->SpawnActor<AArenaProjectile>(AArenaProjectile::StaticClass(), Launch, (T->GetActorLocation() - Launch).Rotation(), P)) { Proj->bVisualOnly = true; Proj->InitHoming(Hr, Ab, T); }
			}
			break;
		case EArenaArchetype::GroundAoE:
			if (AArenaAreaEffect* Area = World->SpawnActor<AArenaAreaEffect>(AArenaAreaEffect::StaticClass(), ArenaAbilityHelpers::GroundTarget(Hr, Ab, Aim), FRotator::ZeroRotator, P)) { Area->bVisualOnly = true; Area->Init(Hr, Ab); }
			break;
		case EArenaArchetype::Dash:
			ArenaFx::Spawn(Hr, Ab.TrailFx.IsEmpty() ? FString(TEXT("/Game/Variant_Platforming/VFX/NS_Jump_Trail.NS_Jump_Trail")) : Ab.TrailFx, Origin, Color);
			break;
		default: break;
		}
	}), Ab.Archetype == EArenaArchetype::GroundAoE ? 0.01f : FMath::Max(0.01f, Ab.Delay), false);
}

// ---------------------------------------------------------------------------------------------
static UStaticMesh* LoadShape(const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name)); }

static void ColorMesh(UStaticMeshComponent* C, const FLinearColor& Color)
{
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, C);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, MID);
	}
}

AArenaProjectile::AArenaProjectile()
{
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(22.f);
	Sphere->SetCollisionObjectType(ECC_WorldDynamic);
	Sphere->SetCollisionResponseToAllChannels(ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Sphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Sphere->SetGenerateOverlapEvents(true);
	Sphere->SetCanEverAffectNavigation(false);   // flying projectiles must not keep dirtying navmesh tiles
	SetRootComponent(Sphere);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Sphere);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetStaticMesh(LoadShape(TEXT("Sphere")));
	Visual->SetRelativeScale3D(FVector(0.35f));
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Sphere);
	Light->SetIntensity(6000.f);
	Light->SetAttenuationRadius(450.f);
	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 0.f;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;   // only a homing shot steers
}

int32 AArenaProjectile::SourceTeam() const { return Source.IsValid() ? Source->GetTeam() : -1; }
FVector AArenaProjectile::GetFlightVelocity() const { return Movement ? Movement->Velocity : FVector::ZeroVector; }

void AArenaProjectile::InitHoming(AArenaCharacter* InSource, const FArenaAbilityDef& InAb, AArenaCharacter* Target)
{
	Init(InSource, InAb, (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal());
	Homing = Target;
	bHoming = true;
	// nothing on the way stops it: no walls, no other units (it only ever meets its target)
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetGenerateOverlapEvents(false);
	SetLifeSpan(6.f);
	SetActorTickEnabled(true);
}

void AArenaProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bHoming || bDone) { return; }
	AArenaCharacter* T = Homing.Get();
	if (!T || !T->IsAlive()) { bDone = true; Destroy(); return; }
	const FVector Aim = T->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	const FVector To = Aim - GetActorLocation();
	const float Speed = FMath::Max(5.f, Ab.Speed) * 100.f;
	if (To.Size() <= Speed * DeltaSeconds + 50.f)
	{
		bDone = true;
		if (Source.IsValid() && !bVisualOnly) { ArenaAbilityHelpers::ApplyHitEffects(Source.Get(), T, Ab, Ab.Damage); }
		ArenaFx::Spawn(this, Ab.Fx, Aim, FArenaDatabase::Hex(Ab.Color));
		ArenaFx::Sound(this, Ab.ImpactSound, Aim);
		Destroy();
		return;
	}
	Movement->Velocity = To.GetSafeNormal() * Speed;
}

void AArenaProjectile::Init(AArenaCharacter* InSource, const FArenaAbilityDef& InAb, const FVector& Direction)
{
	Source = InSource;
	Ab = InAb;
	const FLinearColor C = FArenaDatabase::Hex(Ab.Color);
	ColorMesh(Visual, C);
	Light->SetLightColor(C);
	if (Ab.bUltimate || Ab.Radius > 0.f) { Visual->SetRelativeScale3D(FVector(0.6f)); }
	Movement->ProjectileGravityScale = Ab.Gravity;
	// the shot is as wide as its lane on the ground (the data's width); the player's basic shot is at least 40 cm
	// around the bolt (aim assistance, like the fat hitboxes of Smite)
	const float Radius = Ab.Width > 0.f ? Ab.Width * 50.f : 26.f;
	Sphere->SetSphereRadius(InSource && InSource->bAssistedAim && ArenaAbilityHelpers::AimAssistOn() && Ab.Slot == 0 ? FMath::Max(Radius, 40.f) : Radius);
	if (ArenaFx::Attach(Ab.TrailFx, Sphere, C)) { Visual->SetVisibility(false); }
	Movement->Velocity = Direction * FMath::Max(5.f, Ab.Speed) * 100.f;
	Sphere->IgnoreActorWhenMoving(InSource, true);
	Sphere->OnComponentBeginOverlap.AddDynamic(this, &AArenaProjectile::OnOverlap);
	Sphere->OnComponentHit.AddDynamic(this, &AArenaProjectile::OnHitWorld);
	SetLifeSpan(FMath::Clamp((Ab.Range * 100.f) / FMath::Max(1.f, Ab.Speed * 100.f), 0.2f, 6.f));
}

void AArenaProjectile::LobTo(const FVector& From, const FVector& Land)
{
	// horizontal speed = the ability's speed; the vertical speed that brings it down on the landing point in that
	// time under its gravity; it bursts on the ground there (or on the first enemy it meets on the way)
	const float G = FMath::Max(1.f, -GetWorld()->GetGravityZ() * Ab.Gravity);
	const float Horiz = FMath::Max(1.f, FVector::Dist2D(From, Land));
	const float Speed = FMath::Max(500.f, Ab.Speed * 100.f);
	const float T = Horiz / Speed;
	const float Vz = (Land.Z + 10.f - From.Z) / T + 0.5f * G * T;
	Movement->Velocity = (Land - From).GetSafeNormal2D() * Speed + FVector(0.f, 0.f, Vz);
	SetLifeSpan(T + 1.f);
}

void AArenaProjectile::OnOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	AArenaCharacter* T = Cast<AArenaCharacter>(Other);
	if (bDone || !Source.IsValid() || !Source->IsHostileTo(T)) { return; }         // allies pass through (GS-04)
	if (bVisualOnly)
	{
		// a client's copy: the impact where the server's shot lands (the damage is the server's)
		ArenaFx::Spawn(this, Ab.Fx, GetActorLocation(), FArenaDatabase::Hex(Ab.Color), Ab.Radius > 0.f ? ArenaFx::BlastScale(Ab.FxScale, Ab.Radius, 0.4f, 0.8f, 1.5f) : 1.f);
		if (!Ab.bPierce || Ab.Radius > 0.f) { bDone = true; Destroy(); }
		return;
	}
	// an explosive shot bursts on the first enemy it touches, and that enemy is in the blast (it used to be
	// recorded as already hit first, and the blast skips the already hit: a direct hit did no damage at all)
	if (Ab.Radius > 0.f) { Explode(GetActorLocation()); return; }
	bool bAlready = false;
	Hits.Add(T->GetUniqueID(), &bAlready);
	if (bAlready) { return; }
	ArenaAbilityHelpers::ApplyHitEffects(Source.Get(), T, Ab, Ab.Damage);
	ArenaFx::Spawn(this, Ab.Fx, GetActorLocation(), FArenaDatabase::Hex(Ab.Color));
	if (!Ab.bPierce) { bDone = true; Destroy(); }
}

void AArenaProjectile::OnHitWorld(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
	if (bDone || Cast<AArenaCharacter>(Other)) { return; }
	UE_LOG(LogArena, Verbose, TEXT("ARENA evt=shot_blocked ability=%s by=%s comp=%s at=%s"), *Ab.Name, Other ? *Other->GetActorNameOrLabel() : TEXT("none"), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"), *Hit.ImpactPoint.ToCompactString());
	if (UPrimitiveComponent* P = Hit.GetComponent()) { if (P->IsSimulatingPhysics()) { P->AddImpulseAtLocation(GetVelocity().GetSafeNormal() * 600.f * FMath::Max(1.f, Ab.Damage / 30.f), Hit.ImpactPoint); } }
	if (Ab.Radius > 0.f) { Explode(Hit.ImpactPoint); return; }
	ArenaFx::Spawn(this, Ab.Fx, Hit.ImpactPoint, FArenaDatabase::Hex(Ab.Color), 0.6f);
	bDone = true;
	Destroy();
}

void AArenaProjectile::Explode(const FVector& At)
{
	bDone = true;
	if (Source.IsValid() && !bVisualOnly) { ArenaAbilityHelpers::DamageInRadius(Source.Get(), At, Ab.Radius * 100.f, Ab, Ab.Damage, &Hits); }
	ArenaPhysics::RadialImpulse(GetWorld(), At, Ab.Radius * 150.f, 1500.f);
	ArenaFx::Spawn(this, Ab.Fx, At, FArenaDatabase::Hex(Ab.Color), ArenaFx::BlastScale(Ab.FxScale, Ab.Radius, 0.4f, 0.8f, 1.5f));
	Destroy();
}

// ---------------------------------------------------------------------------------------------
AArenaAreaEffect::AArenaAreaEffect()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(0, 0, 60.f));
	Light->SetIntensity(9000.f);
}

void AArenaAreaEffect::Init(AArenaCharacter* InSource, const FArenaAbilityDef& InAb)
{
	Source = InSource;
	Ab = InAb;
	const float R = FMath::Max(1.f, Ab.Radius);
	const FLinearColor C = FArenaDatabase::Hex(Ab.Color);
	// telegraph on the ground (a decal: it lies on slopes and steps) in the caster's team colour, until it lands
	// the fill grows from the centre and reaches the rim the moment the blow lands: dodge it or not (VR-21)
	const bool bEnemy = !InSource || InSource->GetTeam() != AArenaCharacter::LocalTeam;   // relative to the player
	const FLinearColor TeamTint = bEnemy ? FLinearColor(1.f, 0.2f, 0.12f) : FLinearColor(0.25f, 0.6f, 1.f);
	AArenaIndicator::Telegraph(GetWorld(), GetActorLocation(), R * 100.f, FMath::Lerp(C, TeamTint, bEnemy ? 0.75f : 0.5f), FMath::Max(0.05f, Ab.Delay) + 0.1f, FMath::Max(0.05f, Ab.Delay));
	Light->SetLightColor(C);
	Light->SetAttenuationRadius(R * 160.f);
	// telegraph FX (arrows already falling, a portal opening) on the area itself: it ends with the blow instead of
	// lingering seconds after it
	if (!ArenaFx::AttachFor(Ab.TrailFx, GetRootComponent(), C, FMath::Max(0.05f, Ab.Delay) + 0.3f)) { ArenaFx::Spawn(this, Ab.TrailFx, GetActorLocation(), C); }
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateUObject(this, &AArenaAreaEffect::Detonate), FMath::Max(0.05f, Ab.Delay), false);
}

int32 AArenaAreaEffect::SourceTeam() const { return Source.IsValid() ? Source->GetTeam() : -1; }

void AArenaAreaEffect::Detonate()
{
	bLanded = true;
	if (Source.IsValid() && !bVisualOnly)
	{
		TSet<uint64> Hits;
		ArenaAbilityHelpers::DamageInRadius(Source.Get(), GetActorLocation(), Ab.Radius * 100.f, Ab, Ab.Damage, &Hits);
		if (Ab.bUltimate) { Source->CameraShake(0.4f, 14.f); }
	}
	ArenaPhysics::RadialImpulse(GetWorld(), GetActorLocation(), Ab.Radius * 160.f, Ab.bUltimate ? 3000.f : 1400.f);
	ArenaFx::Spawn(this, Ab.Fx, GetActorLocation(), FArenaDatabase::Hex(Ab.Color), ArenaFx::BlastScale(Ab.FxScale, Ab.Radius, 0.25f, 0.8f, 1.6f));
	Light->SetIntensity(40000.f);
	SetLifeSpan(0.15f);
}
