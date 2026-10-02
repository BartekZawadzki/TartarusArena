#include "Heroes/ArenaCharacter.h"
#include "AnimationRuntime.h"
#include "AbilitySystemComponent.h"
#include "GAS/ArenaAttributeSet.h"
#include "Abilities/ArenaAbility.h"
#include "Core/ArenaCore.h"
#include "Game/ArenaGameMode.h"
#include "UI/ArenaHUD.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Heroes/ArenaSpringArm.h"
#include "Components/DecalComponent.h"
#include "Heroes/ArenaPoseBlendInstance.h"
#include "Heroes/ArenaMinionAnimInstance.h"
#include "GameFramework/PlayerController.h"
#include "Animation/PoseSnapshot.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Arena/ArenaFx.h"
#include "Arena/ArenaPhysics.h"
#include "Heroes/ArenaMovementComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/RootMotionSource.h"
#include "NavigationSystem.h"
#include "Arena/ArenaHitSound.h"
#include "Core/ArenaConquestRules.h"
#include "UI/ArenaSettings.h"
#include "Net/UnrealNetwork.h"
#include "Game/ArenaPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AArenaCharacter::AArenaCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UArenaMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	ASC = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ASC"));
	Attributes = CreateDefaultSubobject<UArenaAttributeSet>(TEXT("Attributes"));

	SpringArm = CreateDefaultSubobject<UArenaSpringArm>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 420.f;
	// over the right shoulder (Smite). The lift is the pivot's (TargetOffset), not the arm end's: with it in the
	// socket offset a wall that shortened the arm slid the camera down the slanted arm to the hero's hips
	SpringArm->TargetOffset = FVector(0.f, 0.f, 90.f);
	SpringArm->SocketOffset = FVector(0.f, 70.f, 0.f);
	SpringArm->bUsePawnControlRotation = true;
	// the camera trails the hero softly while running, but never more than 1.3 m (a 7 m dash does not leave the
	// view behind); substepped so the trail is the same at 40 and 140 fps; no rotation lag: the crosshair aims
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 14.f;
	SpringArm->CameraLagMaxDistance = 130.f;
	SpringArm->bUseCameraLagSubstepping = true;
	SpringArm->CameraLagMaxTimeStep = 1.f / 120.f;
	SpringArm->bEnableCameraRotationLag = false;
	SpringArm->ProbeSize = 14.f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->FieldOfView = 95.f;

	bUseControllerRotationYaw = true;                     // hero faces where the camera aims
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->JumpZVelocity = 620.f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AArenaCharacter::BeginPlay()
{
	Super::BeginPlay();
	ASC->InitAbilityActorInfo(this, this);
	BaseSocketOffset = SpringArm->SocketOffset;
	BaseFov = Camera->FieldOfView;
}

void AArenaCharacter::InitCharacter(const FArenaHeroDef& InDef, int32 InTeam, int32 InLevel, bool bInMinion)
{
	Def = InDef;
	Team = InTeam;
	bMinion = bInMinion;
	Level = FMath::Clamp(InLevel, 1, MaxLevel());

	// a team's own model where the pack has one (Paragon minions: the Dawn army and the Dusk army)
	const FString& MeshPath = Team == 1 && !Def.MeshAlt.IsEmpty() ? Def.MeshAlt : Def.Mesh;
	if (USkeletalMesh* SkMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath)) { GetMesh()->SetSkeletalMesh(SkMesh); }
	if (UClass* Anim = LoadClass<UAnimInstance>(nullptr, *Def.AnimClass)) { AbpClass = Anim; GetMesh()->SetAnimInstanceClass(Anim); SetupNativeAnim(); }
	SetActorScale3D(FVector(Def.Scale));
	// the capsule follows each body's width (Greystone's pauldrons, Sparrow's slim frame): bodies meet at their
	// surfaces in a melee instead of overlapping or stopping short
	GetCapsuleComponent()->SetCapsuleRadius(Def.CapsuleRadius > 0.f ? Def.CapsuleRadius : (bMinion ? 34.f : 40.f));
	if (Def.CapsuleHalfHeight > 0.f) { GetCapsuleComponent()->SetCapsuleHalfHeight(FMath::Max(Def.CapsuleHalfHeight, GetCapsuleComponent()->GetUnscaledCapsuleRadius())); }
	if (!Def.StaticMesh.IsEmpty())
	{
		if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *Def.StaticMesh, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			if (!StaticBody)
			{
				StaticBody = NewObject<UStaticMeshComponent>(this);
				StaticBody->SetupAttachment(GetCapsuleComponent());
				StaticBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				StaticBody->SetCanEverAffectNavigation(false);
				StaticBody->RegisterComponent();
			}
			StaticBody->SetStaticMesh(SM);
			StaticBody->SetRelativeScale3D(FVector(Def.StaticScale));
			StaticBody->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + Def.StaticZ));
		}
	}
	GetMesh()->SetReceivesDecals(false);   // ability indicators and telegraphs paint the ground, not the units
	// the minion punch carries 1.5 m of root motion: it would shove the minion into its target every swing
	if (bMinion) { SetAnimRootMotionTranslationScale(0.f); }
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, Def.MeshZ));
	GetMesh()->SetRelativeRotation(FRotator(0.f, Def.MeshYaw, 0.f));
	// the follow camera framed to the body (v18): the arm, the pivot's lift and the shoulder offset grow with the
	// model's height over a 1.9 m hero (Sevarog, Crunch and Greystone covered the crosshair at the fixed 4.2 m)
	if (!bMinion && !IsStructure() && SpringArm && GetMesh()->GetSkeletalMeshAsset())
	{
		// the head bone's height in the reference pose (the bounds counted Sparrow's bow and Revenant's coat: 2.6-3 m)
		const FReferenceSkeleton& Ref = GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
		float Height = GetMesh()->GetSkeletalMeshAsset()->GetImportedBounds().BoxExtent.Z * 2.f;
		for (const TCHAR* Bone : { TEXT("head"), TEXT("Head"), TEXT("neck_01") })
		{
			const int32 Idx = Ref.FindBoneIndex(FName(Bone));
			if (Idx != INDEX_NONE) { Height = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Idx).GetLocation().Z + 25.f; break; }
		}
		Height *= GetMesh()->GetRelativeScale3D().Z * GetActorScale3D().Z;
		const float K = FMath::Clamp(Height / 190.f, 1.f, 1.45f);
		SpringArm->TargetArmLength = 420.f * (1.f + (K - 1.f) * 0.9f);
		SpringArm->TargetOffset = FVector(0.f, 0.f, 90.f * K);
		SpringArm->SocketOffset = FVector(0.f, 70.f * (1.f + (K - 1.f) * 1.4f), 0.f);
		BaseSocketOffset = SpringArm->SocketOffset;
		UE_LOG(LogArena, Display, TEXT("ARENA evt=camera_frame id=%s height=%.0f k=%.2f arm=%.0f"), *Def.Id.ToString(), Height, K, SpringArm->TargetArmLength);
	}

	// Team-readable tint: hero colour mixed with the team colour (blue / red).
	const FLinearColor TeamColor = Team == 2 ? FLinearColor(1.f, 0.7f, 0.2f) : (Team == LocalTeam ? FLinearColor(0.1f, 0.35f, 1.f) : (FArenaSettings::Get().bColorblind ? FLinearColor(1.f, 0.5f, 0.02f) : FLinearColor(1.f, 0.15f, 0.1f)));   // relative to the player; the camps neutral
	const float TintAmount = Def.TeamTint >= 0.f ? Def.TeamTint : (bMinion ? 0.7f : 0.25f);
	const FLinearColor Tint = FMath::Lerp(FArenaDatabase::Hex(Def.Tint), TeamColor, TintAmount);
	// the pack colours untouched when nothing tints (a team model of its own): its "Color" may mean anything
	const bool bTint = TintAmount > 0.f || !Def.Tint.Equals(TEXT("#FFFFFF"), ESearchCase::IgnoreCase);
	GetMesh()->EmptyOverrideMaterials();
	static const bool bNoFade = FParse::Param(FCommandLine::Get(), TEXT("ArenaNoFadeMats"));   // A/B: the pack materials, no fade
	if (!bNoFade) { ApplyFadeMaterials(); } else { FadeSlots = 0; HideOnFade.Reset(); }
	TintMaterials.Reset();
	for (int32 i = 0; i < GetMesh()->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* MID = GetMesh()->CreateDynamicMaterialInstance(i))
		{
			if (bTint)
			{
				MID->SetVectorParameterValue(TEXT("Paint Tint"), Tint);
				MID->SetVectorParameterValue(TEXT("Global BaseColor"), Tint);
				MID->SetVectorParameterValue(TEXT("Base Color"), Tint);
				MID->SetVectorParameterValue(TEXT("Tint"), Tint);
				MID->SetVectorParameterValue(TEXT("Color"), Tint);
				MID->SetVectorParameterValue(TEXT("BaseColor"), Tint);
				MID->SetVectorParameterValue(TEXT("EmissiveColor"), FLinearColor(1.f, 0.9f, 0.8f));
			}
			TintMaterials.Add(MID);
		}
	}

	ASC->InitAbilityActorInfo(this, this);
	for (int32 Slot = 0; Slot < Def.Abilities.Num() && Slot < 5; ++Slot) { CooldownLen[Slot] = FMath::Max(0.1f, Def.Abilities[Slot].Cooldown); }
	if (HasAuthority())
	{
		// the abilities run on the server only (a LAN client sends its casts there)
		ASC->ClearAllAbilities();
		AbilityHandles.Reset();
		for (int32 Slot = 0; Slot < Def.Abilities.Num() && Slot < 5; ++Slot) { AbilityHandles.Add(ASC->GiveAbility(FGameplayAbilitySpec(UArenaAbility::StaticClass(), 1, Slot, this))); }
	}
	ApplyLevelStats(true);
	UpdateTeamRing();
}

void AArenaCharacter::SetupNativeAnim()
{
	UArenaMinionAnimInstance* M = Cast<UArenaMinionAnimInstance>(GetMesh()->GetAnimInstance());
	if (!M) { return; }
	UAnimSequenceBase* Idle = Def.IdleAnim.IsEmpty() ? nullptr : LoadObject<UAnimSequenceBase>(nullptr, *Def.IdleAnim, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UAnimSequenceBase* Run = Def.RunAnim.IsEmpty() ? nullptr : LoadObject<UAnimSequenceBase>(nullptr, *Def.RunAnim, nullptr, LOAD_NoWarn | LOAD_Quiet);
	M->Setup(Idle, Run, Def.RunAnimSpeed * FMath::Max(0.1f, Def.Scale));
}

int32 AArenaCharacter::LocalTeam = 0;

USoundBase* AArenaCharacter::VoiceCue(const FArenaHeroDef& D, const TCHAR* Event)
{
	// the pack's cues sit together: <dir>/<Hero>_Effort_Pain  ->  <dir>/<Hero>_<Event>
	static TMap<FString, TWeakObjectPtr<USoundBase>> Cache;
	FString Dir, Name, Prefix;
	if (!D.PainSound.Split(TEXT("/"), &Dir, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) { return nullptr; }
	if (!Name.Split(TEXT("_Effort_Pain"), &Prefix, nullptr)) { return nullptr; }
	const FString Path = FString::Printf(TEXT("%s/%s_%s.%s_%s"), *Dir, *Prefix, Event, *Prefix, Event);
	if (const TWeakObjectPtr<USoundBase>* Hit = Cache.Find(Path)) { return Hit->Get(); }
	USoundBase* S = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	Cache.Add(Path, S);
	return S;
}

void AArenaCharacter::Voice(const TCHAR* Event, float MinGap)
{
	if (bMinion || PlayerIndex < 0 || !GetWorld()) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextVoiceAt) { return; }
	if (GetNetMode() != NM_Standalone && !IsLocallyControlled())
	{
		// a LAN player's hero: its line plays on its player's machine, not the host's
		if (Cast<APlayerController>(GetController())) { NextVoiceAt = Now + MinGap; ClientVoice(FName(Event)); }
		return;
	}
	if (USoundBase* S = VoiceCue(Def, Event))
	{
		UGameplayStatics::PlaySound2D(this, S, 0.9f * FArenaSettings::Get().VoiceVolume);
		NextVoiceAt = Now + MinGap;
		UE_LOG(LogArena, Verbose, TEXT("ARENA evt=voice hero=%s event=%s"), *Def.Id.ToString(), Event);
	}
}

void AArenaCharacter::UpdateTeamRing()
{
	if (bMinion || bDead) { if (TeamRing) { TeamRing->SetVisibility(false); } return; }
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_ArenaIndicator.M_ArenaIndicator"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Base) { return; }
	if (!TeamRing)
	{
		TeamRing = NewObject<UDecalComponent>(this);
		TeamRing->SetupAttachment(GetCapsuleComponent());
		TeamRing->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));   // projects straight down
		TeamRing->SortOrder = 0;                                     // under the ability indicators
		TeamRing->SetFadeScreenSize(0.f);
		TeamRing->RegisterComponent();
		TeamRing->SetDecalMaterial(UMaterialInstanceDynamic::Create(Base, this));
	}
	const float R = GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.9f + 30.f;
	TeamRing->DecalSize = FVector(GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 60.f, R, R);
	TeamRing->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	const bool bSelf = PlayerIndex >= 0 && Cast<APlayerController>(GetController()) != nullptr;
	const FLinearColor C = bSelf ? FLinearColor(0.3f, 1.f, 0.35f) : (Team == LocalTeam ? FLinearColor(0.2f, 0.55f, 1.f) : (FArenaSettings::Get().bColorblind ? FLinearColor(1.f, 0.5f, 0.02f) : FLinearColor(1.f, 0.14f, 0.08f)));
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(TeamRing->GetDecalMaterial()))
	{
		MID->SetScalarParameterValue(TEXT("Shape"), 1.f);   // a ring
		MID->SetScalarParameterValue(TEXT("Fill"), 1.f);
		MID->SetScalarParameterValue(TEXT("Edge"), FMath::Clamp(16.f / R, 0.03f, 0.3f));
		MID->SetScalarParameterValue(TEXT("Aspect"), 1.f);
		MID->SetScalarParameterValue(TEXT("Opacity"), bSelf ? 0.5f : 0.7f);
		MID->SetScalarParameterValue(TEXT("Glow"), 2.5f);
		MID->SetVectorParameterValue(TEXT("Color"), C);
	}
	TeamRing->SetVisibility(true);
}

void AArenaCharacter::ShowAura(const FLinearColor& Color, float Seconds)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_ArenaIndicator.M_ArenaIndicator"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Base || bDead) { return; }
	if (!AuraDecal)
	{
		AuraDecal = NewObject<UDecalComponent>(this);
		AuraDecal->SetupAttachment(GetCapsuleComponent());
		AuraDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));   // projects straight down
		AuraDecal->RegisterComponent();
		AuraDecal->SetDecalMaterial(UMaterialInstanceDynamic::Create(Base, this));
	}
	const float R = GetCapsuleComponent()->GetScaledCapsuleRadius() * 2.4f + 40.f;
	AuraDecal->DecalSize = FVector(GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 60.f, R, R);
	AuraDecal->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(AuraDecal->GetDecalMaterial()))
	{
		MID->SetScalarParameterValue(TEXT("Shape"), 0.f);
		MID->SetScalarParameterValue(TEXT("Fill"), 1.f);
		MID->SetScalarParameterValue(TEXT("Edge"), FMath::Clamp(24.f / R, 0.05f, 0.45f));
		MID->SetScalarParameterValue(TEXT("Opacity"), 0.55f);
		MID->SetScalarParameterValue(TEXT("Glow"), 4.f);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
	AuraDecal->SetVisibility(true);
	AuraUntil = FMath::Max(AuraUntil, GetWorld()->GetTimeSeconds() + Seconds);
}

void AArenaCharacter::StartRagdoll()
{
	// limp: the bodies of the physics asset take over from the pose of the moment (no pop). They collide with the
	// ground and walls only: a body never pushes a unit, a prop or a projectile, and traces pass through it; a slow
	// depenetration so a limb starting inside a ramp is eased out instead of flinging the body
	USkeletalMeshComponent* M = GetMesh();
	if (!M->GetPhysicsAsset()) { return; }
	M->SetCollisionObjectType(ECC_PhysicsBody);
	M->SetCollisionResponseToAllChannels(ECR_Ignore);
	M->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	M->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	M->SetAllBodiesSimulatePhysics(true);
	for (FBodyInstance* B : M->Bodies) { if (B) { B->SetMaxDepenetrationVelocity(60.f); } }
	M->WakeAllRigidBodies();
	M->AddImpulse(RagdollPush, NAME_None, true);
	bRagdoll = true;
}

int32 AArenaCharacter::YawSnaps = 0;
bool AArenaCharacter::bShakeEnabled = true;
int32 AArenaCharacter::YawFrames = 0;

UMaterialInterface* AArenaCharacter::FadeCopyOf(const UMaterialInterface* Material)
{
	if (!Material) { return nullptr; }
	// F_<pack>_<name> (the name make_fade_materials.py writes); misses are remembered: a translucent eye layer
	// has no copy and must not cost a package lookup at every spawn
	static TMap<FString, TWeakObjectPtr<UMaterialInterface>> Found;
	static TSet<FString> Missing;
	const FString Key = Material->GetPathName();
	if (Key.StartsWith(TEXT("/Game/Arena/Generated/Fade/"))) { return const_cast<UMaterialInterface*>(Material); }
	if (Missing.Contains(Key)) { return nullptr; }
	if (const TWeakObjectPtr<UMaterialInterface>* Hit = Found.Find(Key)) { if (Hit->IsValid()) { return Hit->Get(); } }
	TArray<FString> Parts;
	Key.ParseIntoArray(Parts, TEXT("/"));   // Game, <pack>, ..., <name>.<name>
	const FString Name = FString::Printf(TEXT("F_%s_%s"), Parts.Num() > 1 ? *Parts[1] : TEXT(""), *Material->GetName());
	UMaterialInterface* Copy = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Arena/Generated/Fade/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Copy) { Found.Add(Key, Copy); } else { Missing.Add(Key); }
	return Copy;
}

void AArenaCharacter::ApplyFadeMaterials()
{
	// the death fade: each slot drawn by its masked copy, identical to the pack material while FadeOut is 0
	FadeSlots = 0;
	HideOnFade.Reset();
	for (int32 i = 0; i < GetMesh()->GetNumMaterials(); ++i)
	{
		UMaterialInterface* Copy = FadeCopyOf(GetMesh()->GetMaterial(i));
		if (Copy) { GetMesh()->SetMaterial(i, Copy); ++FadeSlots; }
		else { HideOnFade.Add(i); }
	}
	for (int32 L = 0; L < GetMesh()->GetNumLODs(); ++L) { GetMesh()->ShowAllMaterialSections(L); }
}

void AArenaCharacter::UseSmoothTurning(bool bSmooth)
{
	bUseControllerRotationYaw = !bSmooth;
	GetCharacterMovement()->bUseControllerDesiredRotation = bSmooth;
	GetCharacterMovement()->RotationRate = FRotator(0.f, bMinion ? 540.f : 720.f, 0.f);
}

bool AArenaCharacter::IsOnAnimBlueprint() const
{
	return GetMesh()->GetAnimationMode() == EAnimationMode::AnimationBlueprint && !Cast<UArenaPoseBlendInstance>(GetMesh()->GetAnimInstance());
}

int32 AArenaCharacter::MaxLevel() const { return FMath::Max(2, FArenaDatabase::Get().Rules.MaxLevel); }

void AArenaCharacter::ApplyLevelStats(bool bRefill)
{
	// heroes grow by their per-level numbers (MOBA); minions keep the flat +5 % per wave level
	const float L1 = static_cast<float>(Level - 1);
	const float S = bMinion ? ArenaCore::LevelScale(Level) : 1.f;
	const float OldMax = FMath::Max(1.f, GetMaxHealth());
	const float HpPct = bRefill ? 1.f : GetHealth() / OldMax;
	const float MaxHp = ((Def.MaxHealth + Def.HealthPerLevel * L1) * S + ItemStats.Health) * (1.f + EdgeHealth), MaxMp = Def.MaxMana + Def.ManaPerLevel * L1 + ItemStats.Mana;
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetMaxHealthAttribute(), MaxHp);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetMaxManaAttribute(), MaxMp);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), MaxHp * HpPct);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), bRefill ? MaxMp : FMath::Min(GetMana(), MaxMp));
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetPowerAttribute(), (Def.Power + Def.PowerPerLevel * L1) * S + ItemStats.Power);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetArmorAttribute(), (Def.Armor + Def.ArmorPerLevel * L1) * S + ItemStats.Armor);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetMoveSpeedAttribute(), Def.MoveSpeed * (1.f + ItemStats.MoveSpeedPct));
	UpdateMoveSpeed();
}

void AArenaCharacter::SetItems(const TArray<int32>& InItems)
{
	Items = InItems;
	ArenaCore::FItemStats Sum;
	const TArray<FArenaItemDef>& Db = FArenaDatabase::Get().Items;
	for (int32 I : Items)
	{
		if (!Db.IsValidIndex(I)) { continue; }
		const FArenaItemDef& D = Db[I];
		ArenaCore::FItemStats One;
		One.Power = D.Power; One.Armor = D.Armor; One.Health = D.Health; One.Mana = D.Mana; One.HealthRegen = D.HealthRegen; One.ManaRegen = D.ManaRegen;
		One.MoveSpeedPct = D.MoveSpeedPct; One.CooldownPct = D.CooldownPct; One.AttackSpeedPct = D.AttackSpeedPct; One.LifestealPct = D.LifestealPct;
		One.CritChance = D.CritChance; One.ArmorPen = D.ArmorPen;
		Sum += One;
	}
	ItemStats = Sum.Capped();
	// unique passives: the first copy counts, a second copy of the same item adds only its stats
	ItemPassives = ArenaCore::FItemPassives();
	TSet<FString> Seen;
	for (int32 I : Items)
	{
		if (!Db.IsValidIndex(I) || Db[I].Passive.IsEmpty() || Seen.Contains(Db[I].Passive)) { continue; }
		const FArenaItemDef& D = Db[I];
		Seen.Add(D.Passive);
		ArenaCore::FItemPassives& P = ItemPassives;
		if (D.Passive == TEXT("execute")) { P.ExecuteBonus = D.PassiveA; P.ExecuteBelow = D.PassiveB; }
		else if (D.Passive == TEXT("infinity")) { P.CritMult = FMath::Max(P.CritMult, D.PassiveA); }
		else if (D.Passive == TEXT("overheal")) { P.OverhealShieldMax = D.PassiveA; }
		else if (D.Passive == TEXT("onhit")) { P.OnHit = D.PassiveA; }
		else if (D.Passive == TEXT("spellblade")) { P.SpellbladePower = D.PassiveA; P.SpellbladeWindow = D.PassiveB; P.SpellbladeCd = D.PassiveCd; }
		else if (D.Passive == TEXT("archon")) { P.AbilityDamagePct = D.PassiveA; }
		else if (D.Passive == TEXT("echo")) { P.EchoSeconds = D.PassiveA; }
		else if (D.Passive == TEXT("thorns")) { P.ThornsPct = D.PassiveA; }
		else if (D.Passive == TEXT("laststand")) { P.LastStandShieldPct = D.PassiveA; P.LastStandBelow = D.PassiveB; P.LastStandCd = D.PassiveCd; }
		else if (D.Passive == TEXT("regen")) { P.RegenPct = D.PassiveA; P.RegenAfter = D.PassiveB; }
	}
	ApplyLevelStats(false);
}

float AArenaCharacter::GetHealth() const { return Attributes ? Attributes->GetHealth() : 0.f; }
float AArenaCharacter::GetMaxHealth() const { return Attributes ? Attributes->GetMaxHealth() : 0.f; }
float AArenaCharacter::GetMana() const { return Attributes ? Attributes->GetMana() : 0.f; }
float AArenaCharacter::GetMaxMana() const { return Attributes ? Attributes->GetMaxMana() : 0.f; }
float AArenaCharacter::GetShield() const { return Attributes ? Attributes->GetShield() : 0.f; }
float AArenaCharacter::GetPower() const { return Attributes ? Attributes->GetPower() : 0.f; }
void AArenaCharacter::ClearShield() { if (ASC) { ASC->SetNumericAttributeBase(UArenaAttributeSet::GetShieldAttribute(), 0.f); } }

float AArenaCharacter::GetArmor() const { return Attributes ? Attributes->GetArmor() : 0.f; }
bool AArenaCharacter::IsStunned() const { return GetWorld() && GetWorld()->GetTimeSeconds() < StunUntil; }

float AArenaCharacter::CooldownRemaining(int32 Slot) const
{
	return (Slot >= 0 && Slot < 5 && GetWorld()) ? FMath::Max(0.f, CooldownEnd[Slot] - GetWorld()->GetTimeSeconds()) : 0.f;
}
float AArenaCharacter::CooldownTotal(int32 Slot) const { return (Slot >= 0 && Slot < 5) ? CooldownLen[Slot] : 1.f; }

bool AArenaCharacter::CanCastSlot(int32 Slot) const
{
	if (bDead || IsStunned() || IsAirborne() || IsBusy() || !Def.Abilities.IsValidIndex(Slot)) { return false; }
	if (const AArenaGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AArenaGameMode>() : nullptr)
	{
		if (GM->Phase == EArenaPhase::Countdown || GM->Phase == EArenaPhase::Ended) { return false; }   // GS-07
	}
	if (!bMinion && Slot > 0 && Ranks[Slot] <= 0) { return false; }                     // not learnt yet (VR-20)
	return ArenaCore::CanCast(CooldownRemaining(Slot), GetMana(), Ability(Slot).ManaCost); // VR-02
}

FArenaAbilityDef AArenaCharacter::Ability(int32 Slot) const
{
	if (!Def.Abilities.IsValidIndex(Slot)) { return FArenaAbilityDef(); }
	FArenaAbilityDef A = Def.Abilities[Slot];
	A.Slot = Slot;
	if (Slot == 0 || bMinion)
	{
		A.Rank = 1;
		if (Slot == 0) { A.Damage += Def.BasicPerLevel * static_cast<float>(Level - 1); }
		return A;
	}
	A.Rank = Ranks[Slot];
	const int32 R = FMath::Max(1, Ranks[Slot]);          // an unlearnt ability shows its rank-1 numbers
	A.Damage = FMath::Max(0.f, ArenaCore::Ranked(A.Damage, A.DamagePerRank, R));
	A.EndDamage = FMath::Max(0.f, ArenaCore::Ranked(A.EndDamage, A.EndDamagePerRank, R));
	A.Heal = FMath::Max(0.f, ArenaCore::Ranked(A.Heal, A.HealPerRank, R));
	A.Shield = FMath::Max(0.f, ArenaCore::Ranked(A.Shield, A.ShieldPerRank, R));
	if (A.StunSeconds > 0.f) { A.StunSeconds = ArenaCore::Ranked(A.StunSeconds, A.StunPerRank, R); }
	if (A.SlowPct > 0.f) { A.SlowPct = FMath::Min(0.8f, ArenaCore::Ranked(A.SlowPct, A.SlowPerRank, R)); }
	if (A.BuffSeconds > 0.f) { A.BuffSeconds = ArenaCore::Ranked(A.BuffSeconds, A.BuffPerRank, R); }
	A.Cooldown = FMath::Max(0.5f, ArenaCore::Ranked(A.Cooldown, A.CooldownPerRank, R));
	A.ManaCost = FMath::Max(0.f, ArenaCore::Ranked(A.ManaCost, A.ManaPerRank, R));
	return A;
}

int32 AArenaCharacter::FreeSkillPoints() const { return bMinion ? 0 : ArenaCore::FreePoints(Level, Ranks); }
bool AArenaCharacter::CanRankUp(int32 Slot) const { return !bMinion && !bDead && ArenaCore::CanRankUp(Slot, Level, Ranks); }

bool AArenaCharacter::RankUp(int32 Slot)
{
	if (!CanRankUp(Slot)) { return false; }
	++Ranks[Slot];
	if (bAssistedAim) { Voice(TEXT("Level_AbilityLevelConfirm"), 1.5f); }
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=rank hero=%s team=%d slot=%d rank=%d level=%d points=%d"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), Team, Slot, Ranks[Slot], Level, FreeSkillPoints());
	return true;
}

void AArenaCharacter::AutoRank()
{
	for (int32 Guard = 0; Guard < 25 && FreeSkillPoints() > 0; ++Guard)
	{
		if (RankUp(4)) { continue; }
		bool bDone = false;
		int32 Seen[5] = { 0, 0, 0, 0, 0 };
		for (int32 S : Def.SkillOrder)
		{
			if (S < 1 || S > 3) { continue; }
			++Seen[S];
			if (Ranks[S] < Seen[S] && RankUp(S)) { bDone = true; break; }
		}
		for (int32 S = 1; S <= 3 && !bDone; ++S) { bDone = RankUp(S); }
		if (!bDone) { break; }
	}
}

void AArenaCharacter::RefillMana()
{
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), GetMaxMana());
}

void AArenaCharacter::ForceRanks(int32 Rank)
{
	for (int32 S = 1; S < 5; ++S) { Ranks[S] = FMath::Clamp(Rank, 0, ArenaCore::MaxRank); }
}

void AArenaCharacter::SetRanks(const int32 In[5])
{
	for (int32 S = 1; S < 5; ++S) { Ranks[S] = FMath::Clamp(In[S], 0, ArenaCore::MaxRank); }
}

float AArenaCharacter::XpIntoLevel() const
{
	const FArenaRulesDef& R = FArenaDatabase::Get().Rules;
	return FMath::Max(0.f, Xp - ArenaCore::XpForLevel(Level, R.XpBase, R.XpGrowth));
}
float AArenaCharacter::XpLevelSpan() const
{
	const FArenaRulesDef& R = FArenaDatabase::Get().Rules;
	return Level >= MaxLevel() ? 0.f : ArenaCore::XpToNext(Level, R.XpBase, R.XpGrowth);
}

bool AArenaCharacter::RollCrit() { return ArenaCore::NextCrit(CritAccum, ItemStats.CritChance); }

int32 AArenaCharacter::HitsToCrit() const
{
	if (ItemStats.CritChance <= 0.f) { return 0; }
	return FMath::Max(1, FMath::CeilToInt((1.f - 1e-4f - CritAccum) / ItemStats.CritChance));
}

void AArenaCharacter::ArmSpellblade()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (ItemPassives.SpellbladePower > 0.f && Now >= SpellbladeReadyAt) { SpellbladeUntil = Now + ItemPassives.SpellbladeWindow; }
}

bool AArenaCharacter::ConsumeSpellblade()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= SpellbladeUntil) { return false; }
	SpellbladeUntil = 0.f;
	SpellbladeReadyAt = Now + ItemPassives.SpellbladeCd;
	return true;
}

void AArenaCharacter::ReduceAbilityCooldowns(float Seconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 S = 1; S < 5; ++S) { CooldownEnd[S] = FMath::Max(Now, CooldownEnd[S] - Seconds); }
}

bool AArenaCharacter::IsInOwnBase() const
{
	const AArenaGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AArenaGameMode>() : nullptr;
	return GM && !bMinion && FVector::Dist2D(GetActorLocation(), GM->TeamBase(Team)) < FArenaDatabase::Get().Rules.BaseRadius * 100.f;
}

bool AArenaCharacter::IsInEnemyBase() const
{
	const AArenaGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AArenaGameMode>() : nullptr;
	return GM && !bMinion && Team >= 0 && Team <= 1 && FVector::Dist2D(GetActorLocation(), GM->TeamBase(1 - Team)) < GM->FountainRadiusCm();
}

bool AArenaCharacter::StartRecall()
{
	if (bDead || bMinion || IsRecalling() || IsStunned() || IsAirborne() || IsBusy() || IsDashing() || IsInOwnBase()) { return false; }
	RecallStart = GetWorld()->GetTimeSeconds();
	RecallFrom = GetActorLocation();
	GetCharacterMovement()->StopMovementImmediately();
	RecallFxComp = ArenaFx::AttachAt(FArenaDatabase::Get().Rules.RecallFx, GetMesh(), NAME_None, FArenaDatabase::Get().Rules.RecallSeconds + 0.2f);
	NetStatus(5, FArenaDatabase::Get().Rules.RecallSeconds);
	ArenaFx::Spawn(this, FArenaDatabase::Get().Rules.RespawnFx, GetActorLocation() - FVector(0.f, 0.f, 90.f), FLinearColor(0.5f, 0.8f, 1.f), 0.6f);
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=recall hero=%s team=%d"), RecallStart, *Def.Id.ToString(), Team);
	return true;
}

void AArenaCharacter::CancelRecall(const TCHAR* Why)
{
	if (!IsRecalling()) { return; }
	RecallStart = -1.f;
	if (RecallFxComp.IsValid()) { RecallFxComp->Deactivate(); }
	NetStatus(6, 0.f);
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=recall_cancel hero=%s team=%d why=%s"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), Team, Why);
}

float AArenaCharacter::RecallProgress() const
{
	return IsRecalling() && GetWorld() ? FMath::Clamp((GetWorld()->GetTimeSeconds() - RecallStart) / FMath::Max(0.5f, FArenaDatabase::Get().Rules.RecallSeconds), 0.f, 1.f) : 0.f;
}

bool AArenaCharacter::DrinkPotion(int32 Kind)
{
	if (bDead || Kind < 0 || Kind > 1 || Potions[Kind] <= 0 || PotionLeft(Kind) > 0.f) { return false; }
	const FArenaRulesDef& R = FArenaDatabase::Get().Rules;
	--Potions[Kind];
	PotionUntil[Kind] = GetWorld()->GetTimeSeconds() + FMath::Max(0.5f, R.PotionSeconds);
	PotionRate[Kind] = (Kind == 0 ? R.PotionHeal : R.PotionMana) / FMath::Max(0.5f, R.PotionSeconds);
	if (Kind == 1) { ArenaFx::AttachAt(R.ManaFx, GetMesh(), NAME_None, 1.5f); NetStatus(7, 1.5f); }
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=potion hero=%s team=%d kind=%d left=%d"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), Team, Kind, Potions[Kind]);
	return true;
}

void AArenaCharacter::TickSustain(float Now, float Dt)
{
	if (bMinion) { return; }
	const FArenaRulesDef& R = FArenaDatabase::Get().Rules;
	// recall: moving (walking, a knock) breaks it on the server, whatever the client says; home when it completes
	if (IsRecalling() && FVector::Dist2D(GetActorLocation(), RecallFrom) > 80.f) { CancelRecall(TEXT("moved")); }
	if (IsRecalling() && Now - RecallStart >= R.RecallSeconds)
	{
		RecallStart = -1.f;
		if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
		{
			const FVector Home = GM->BaseSpot(Team);
			SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
			GetCharacterMovement()->StopMovementImmediately();
			ArenaFx::Spawn(this, R.RespawnFx, Home - FVector(0.f, 0.f, 90.f), FLinearColor(0.5f, 0.8f, 1.f));
			UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=recall_done hero=%s team=%d"), Now, *Def.Id.ToString(), Team);
		}
	}
	// potions: health / mana over time
	if (Now < PotionUntil[0]) { ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), FMath::Min(GetMaxHealth(), GetHealth() + PotionRate[0] * Dt)); }
	if (Now < PotionUntil[1]) { ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), FMath::Min(GetMaxMana(), GetMana() + PotionRate[1] * Dt)); }
	// the fountain: your own heals fast, an enemy's burns (nobody camps a base)
	const AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
	if (!GM || !GM->bFountainsOn || GM->Phase != EArenaPhase::Playing) { return; }
	if (IsInOwnBase())
	{
		ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), FMath::Min(GetMaxHealth(), GetHealth() + GetMaxHealth() * R.FountainHealPct * Dt));
		ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), FMath::Min(GetMaxMana(), GetMana() + GetMaxMana() * R.FountainHealPct * Dt));
	}
	else if (IsInEnemyBase() && Now >= NextFountainHit)
	{
		NextFountainHit = Now + 0.5f;
		FArenaHit H;
		H.ArmorPen = 1.e5f;                         // true damage
		H.Ability = TEXT("Fountain");
		ReceiveHit(GetMaxHealth() * R.FountainDamagePct * 0.5f, nullptr, H);
	}
}

void AArenaCharacter::MakeStructure(int32 Kind, int32 Lane, int32 Tier)
{
	StructureKind = Kind;
	StructureLane = Lane;
	StructureTier = Tier;
	UCharacterMovementComponent* M = GetCharacterMovement();
	M->StopMovementImmediately();
	M->DisableMovement();
	M->GravityScale = 0.f;
	bUseControllerRotationYaw = false;
	SetAnimRootMotionTranslationScale(0.f);
	if (TeamRing) { TeamRing->SetVisibility(false); }
	// a dynamic obstacle: minions and bots path round it instead of pushing into it
	if (!NavBlock)
	{
		NavBlock = NewObject<UBoxComponent>(this);
		NavBlock->SetupAttachment(GetCapsuleComponent());
		NavBlock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NavBlock->bDynamicObstacle = true;
		NavBlock->SetCanEverAffectNavigation(true);
		NavBlock->RegisterComponent();
	}
	const float R = GetCapsuleComponent()->GetUnscaledCapsuleRadius() + 20.f / FMath::Max(0.1f, GetActorScale3D().X);
	NavBlock->SetBoxExtent(FVector(R, R, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
	UE_LOG(LogArena, Display, TEXT("ARENA evt=structure kind=%d team=%d lane=%d tier=%d at=%s radius=%.0f"), Kind, Team, Lane, Tier, *GetActorLocation().ToCompactString(), GetCapsuleComponent()->GetScaledCapsuleRadius());
}

void AArenaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaCharacter, NetSetup);
	DOREPLIFETIME(AArenaCharacter, NetVitals);
	DOREPLIFETIME_CONDITION(AArenaCharacter, NetOwner, COND_OwnerOnly);
	DOREPLIFETIME(AArenaCharacter, NetTarget);
}

void AArenaCharacter::SetNetSetup(uint8 Kind, int32 Index, int32 Skin, float Boost)
{
	NetSetup.Kind = Kind;
	NetSetup.Index = (int16)Index;
	NetSetup.Team = (int8)Team;
	NetSetup.Level = (uint8)FMath::Clamp(Level, 1, 255);
	NetSetup.Skin = (int8)Skin;
	NetSetup.Boost = Boost;
	NetSetup.Lane = (int8)StructureLane;
	NetSetup.Tier = (uint8)StructureTier;
}

bool AArenaCharacter::IsLocalHero() const
{
	if (GetNetMode() == NM_Standalone) { return PlayerIndex >= 0; }
	return IsLocallyControlled() && Cast<APlayerController>(GetController()) != nullptr;
}

void AArenaCharacter::OnRep_Setup()
{
	// a LAN client: the body, the team and the numbers from the definition the server used
	if (HasAuthority() || bNetInit || NetSetup.Kind == 0) { return; }
	const FArenaDatabaseFile& Db = FArenaDatabase::Get();
	const FArenaConquestDef& Cq = Db.Rules.Conquest;
	FArenaHeroDef D;
	bool bMin = true;
	switch (NetSetup.Kind)
	{
	case 1:
		if (!Db.Heroes.IsValidIndex(NetSetup.Index)) { return; }
		D = Db.Heroes[NetSetup.Index];
		bMin = false;
		if (D.Skins.IsValidIndex(NetSetup.Skin)) { D.Mesh = D.Skins[NetSetup.Skin]; D.MeshAlt.Reset(); }
		break;
	case 2: D = Db.Rules.MeleeMinion; break;
	case 3: D = Db.Rules.RangedMinion; break;
	case 4: D = Cq.SiegeMinion; break;
	case 5: D = Cq.SuperMinion; break;
	case 6: D = Cq.Tower; break;
	case 7: D = Cq.Inhibitor; break;
	case 8: D = Cq.Core; break;
	case 9: if (!Cq.Camps.IsValidIndex(NetSetup.Index)) { return; } D = Cq.Camps[NetSetup.Index].Unit; break;
	default: return;
	}
	if (NetSetup.Boost > 0.f) { D.MaxHealth *= 1.f + NetSetup.Boost; D.Scale *= 1.08f; }
	bNetInit = true;
	InitCharacter(D, NetSetup.Team, NetSetup.Level, bMin);
	HeroIndex = NetSetup.Kind == 1 ? NetSetup.Index : -1;
	if (NetSetup.Kind >= 6 && NetSetup.Kind <= 8) { MakeStructure(NetSetup.Kind - 5, NetSetup.Lane, NetSetup.Tier); }
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	OnRep_Vitals();
}

void AArenaCharacter::OnRep_Vitals()
{
	if (HasAuthority() || !bNetInit) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (NetVitals.bDead)
	{
		// dead before this machine ever saw it alive (a late join): down at once, a body hidden; a death seen live
		// comes with its animation from MulticastDie
		if (!bDead && !bNetSeenAlive)
		{
			Die(nullptr, INDEX_NONE);
			if (!IsStructure()) { SetActorHiddenInGame(true); }
		}
		return;
	}
	bNetSeenAlive = true;
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetMaxHealthAttribute(), NetVitals.MaxHealth);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), NetVitals.Health);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetMaxManaAttribute(), NetVitals.MaxMana);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), NetVitals.Mana);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetShieldAttribute(), NetVitals.Shield);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetPowerAttribute(), NetVitals.Power);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetArmorAttribute(), NetVitals.Armor);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetMoveSpeedAttribute(), NetVitals.MoveSpeed);
	Level = NetVitals.Level;
	bInvulnerable = NetVitals.bInvulnerable;
	StunUntil = Now + NetVitals.StunLeft;
	SlowUntil = Now + NetVitals.SlowLeft; SlowPct = NetVitals.SlowPct;
	SpeedUntil = Now + NetVitals.SpeedLeft; SpeedPct = NetVitals.SpeedPct;
	for (int32 i = 0; i < 3; ++i) { CampBuffUntil[i] = Now + NetVitals.CampBuff[i]; }
	UpdateMoveSpeed();
}

void AArenaCharacter::OnRep_NetOwner()
{
	if (HasAuthority()) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	Gold = NetOwner.Gold;
	Xp = NetOwner.Xp;
	for (int32 i = 0; i < 5; ++i)
	{
		Ranks[i] = NetOwner.Ranks[i];
		CooldownEnd[i] = Now + NetOwner.CdLeft[i];
		CooldownLen[i] = FMath::Max(0.1f, NetOwner.CdLen[i]);
	}
	Potions[0] = NetOwner.Potions[0]; Potions[1] = NetOwner.Potions[1];
	PotionUntil[0] = Now + NetOwner.PotionLeft[0]; PotionUntil[1] = Now + NetOwner.PotionLeft[1];
	Items = NetOwner.Items;
	AtkSpeedUntil = Now + NetOwner.AtkSpeedLeft;
	RecallStart = NetOwner.RecallLeft >= 0.f ? Now - FMath::Max(0.f, FArenaDatabase::Get().Rules.RecallSeconds - NetOwner.RecallLeft) : -1.f;
}

void AArenaCharacter::WriteNetState()
{
	NetVitals.Health = GetHealth(); NetVitals.MaxHealth = GetMaxHealth(); NetVitals.Mana = GetMana(); NetVitals.MaxMana = GetMaxMana();
	NetVitals.Shield = GetShield(); NetVitals.Power = GetPower(); NetVitals.Armor = GetArmor();
	NetVitals.MoveSpeed = Attributes ? Attributes->GetMoveSpeed() : 0.f;
	NetVitals.Level = (uint8)FMath::Clamp(Level, 1, 255);
	NetVitals.bInvulnerable = bInvulnerable;
	NetVitals.StunLeft = StunRemaining(); NetVitals.SlowLeft = SlowRemaining(); NetVitals.SlowPct = SlowPct;
	NetVitals.SpeedLeft = SpeedBuffRemaining(); NetVitals.SpeedPct = SpeedPct;
	for (int32 i = 0; i < 3; ++i) { NetVitals.CampBuff[i] = CampBuffLeft(i); }
	NetTarget = IsStructure() ? AimTarget.Get() : nullptr;
	if (Cast<APlayerController>(GetController()))
	{
		NetOwner.Gold = Gold; NetOwner.Xp = Xp;
		for (int32 i = 0; i < 5; ++i) { NetOwner.Ranks[i] = Ranks[i]; NetOwner.CdLeft[i] = CooldownRemaining(i); NetOwner.CdLen[i] = CooldownLen[i]; }
		NetOwner.Potions[0] = Potions[0]; NetOwner.Potions[1] = Potions[1];
		NetOwner.PotionLeft[0] = PotionLeft(0); NetOwner.PotionLeft[1] = PotionLeft(1);
		NetOwner.Items = Items;
		NetOwner.AtkSpeedLeft = AttackSpeedBuffRemaining();
		NetOwner.RecallLeft = IsRecalling() ? FArenaDatabase::Get().Rules.RecallSeconds * (1.f - RecallProgress()) : -1.f;
	}
}

void AArenaCharacter::MulticastHit_Implementation(float Dmg, bool bCrit, AArenaCharacter* Source)
{
	// a blow as a LAN client sees it (the server applied it): the flash, the numbers of its own fights, the reaction
	if (HasAuthority() || bDead) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	HitFlashUntil = Now + 0.12f;
	const bool bMine = IsLocalHero();
	const bool bByMe = Source && Source->IsLocalHero();
	if (bMine || bByMe) { AArenaHUD::AddDamageNumber(this, GetActorLocation() + FVector(0, 0, 110.f), Dmg, bCrit, bByMe); }
	if (bByMe)
	{
		LastHitFlash = Now;
		TickHitFlash(LastHitFlash);
		AArenaHUD::NotifyPlayerHit(this, Dmg, bCrit, GetHealth() - Dmg <= 0.f);
		ArenaHitSound::Play(this, bCrit ? 1 : 0);
	}
	if (bMine && Source) { AArenaHUD::AddDamageDir(this, Source->GetActorLocation()); CameraShake(0.12f, 6.f); }
	if (Now >= NextNetPain) { ArenaFx::Sound(this, Def.PainSound, GetActorLocation(), 0.7f); NextNetPain = Now + 1.5f; }
	PlayHitReact(Source);
}

void AArenaCharacter::MulticastCast_Implementation(uint8 Slot, uint8 Variant, FVector_NetQuantize Aim, AArenaCharacter* Target)
{
	if (HasAuthority() || bDead) { return; }
	// the guest's copy turns to the cast's aim like the host's
	if (!bMinion && Def.Abilities.IsValidIndex(Slot) && Def.Abilities[Slot].Archetype != EArenaArchetype::Buff)
	{
		const FVector To = (FVector(Aim) - GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero()) { FaceAim(To.Rotation().Yaw, FMath::Max(Def.Abilities[Slot].Delay + 0.3f, 0.45f)); }
	}
	ArenaAbilityHelpers::PlayRemoteCast(this, Slot, Variant, Aim, Target);
}

void AArenaCharacter::MulticastDie_Implementation(AArenaCharacter* Killer, int32 Pick)
{
	if (HasAuthority() || bDead) { return; }
	Die(Killer, Pick);
}

void AArenaCharacter::MulticastStatus_Implementation(uint8 Kind, float Seconds)
{
	if (HasAuthority() || bDead) { return; }
	const FArenaRulesDef& R = FArenaDatabase::Get().Rules;
	switch (Kind)
	{
	case 0:
		ArenaFx::AttachAt(R.StunStartFx, GetMesh(), TEXT("head"), 0.8f);
		ArenaFx::AttachAt(R.StunFx, GetMesh(), TEXT("head"), Seconds);
		if (Def.StunAnims.Num() > 0 && Seconds >= 0.3f) { StunMontage = PlaySlotAnim(Def.StunAnims[0], false, 0.1f, 0.25f); }
		break;
	case 1: ArenaFx::AttachAt(R.SlowFx, GetMesh(), NAME_None, Seconds); break;
	case 2: ArenaFx::AttachAt(R.SpeedFx, GetMesh(), NAME_None, Seconds); break;
	case 3: ArenaFx::AttachAt(R.ShieldFx, GetMesh(), TEXT("spine_03"), 1.2f); break;
	case 4: ArenaFx::AttachAt(R.LevelUpFx, GetMesh(), NAME_None, 2.f); break;
	case 5: RecallFxComp = ArenaFx::AttachAt(R.RecallFx, GetMesh(), NAME_None, R.RecallSeconds + 0.2f); break;
	case 6: if (RecallFxComp.IsValid()) { RecallFxComp->Deactivate(); } break;
	case 7: ArenaFx::AttachAt(R.ManaFx, GetMesh(), NAME_None, 1.5f); break;
	default: break;
	}
}

void AArenaCharacter::MulticastSpawnIn_Implementation()
{
	if (HasAuthority()) { return; }
	PlaySpawnIn();
}

void AArenaCharacter::ClientVoice_Implementation(FName Event)
{
	if (USoundBase* S = VoiceCue(Def, *Event.ToString())) { UGameplayStatics::PlaySound2D(this, S, 0.9f * FArenaSettings::Get().VoiceVolume); }
}

void AArenaCharacter::ShowRangeRing(bool bShow, const FLinearColor& Color, float Opacity)
{
	if (!bShow || bDead || Def.Abilities.Num() == 0) { if (RangeRing) { RangeRing->SetVisibility(false); } return; }
	if (!RangeRing)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_ArenaIndicator.M_ArenaIndicator"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Base) { return; }
		RangeRing = NewObject<UDecalComponent>(this);
		RangeRing->SetupAttachment(GetCapsuleComponent());
		RangeRing->SetUsingAbsoluteRotation(true);
		RangeRing->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));   // straight down
		RangeRing->SortOrder = 0;
		RangeRing->SetFadeScreenSize(0.f);
		RangeRing->RegisterComponent();
		RangeRing->SetDecalMaterial(UMaterialInstanceDynamic::Create(Base, this));
		const float R = Ability(0).Range * 100.f + GetCapsuleComponent()->GetScaledCapsuleRadius();
		const FVector S = GetActorScale3D();
		RangeRing->DecalSize = FVector(450.f, R, R) / FVector(S.Z, S.X, S.Y);   // the component inherits the actor's scale
		RangeRing->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(RangeRing->GetDecalMaterial()))
		{
			MID->SetScalarParameterValue(TEXT("Shape"), 1.f);
			MID->SetScalarParameterValue(TEXT("Fill"), 1.f);
			MID->SetScalarParameterValue(TEXT("Edge"), FMath::Clamp(45.f / R, 0.01f, 0.2f));   // a band you can read, not a hairline (a thin one glowed white)
			MID->SetScalarParameterValue(TEXT("Aspect"), 1.f);
			MID->SetScalarParameterValue(TEXT("Glow"), 1.2f);
		}
	}
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(RangeRing->GetDecalMaterial()))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	}
	RangeRing->SetVisibility(true);
}

int32 AArenaCharacter::MinionKind() const
{
	const FString Id = Def.Id.ToString();
	return Id.Contains(TEXT("Ranged")) ? 1 : (Id.Contains(TEXT("Siege")) ? 2 : (Id.Contains(TEXT("Super")) ? 3 : 0));
}

void AArenaCharacter::ApplyCampBuff(int32 Kind, float Seconds)
{
	if (bDead || Kind < 0 || Kind > 2 || Seconds <= 0.f) { return; }
	CampBuffUntil[Kind] = GetWorld()->GetTimeSeconds() + Seconds;
	const TArray<FString>& Fx = FArenaDatabase::Get().Rules.Conquest.BuffFx;
	if (CampBuffFx[Kind].IsValid()) { CampBuffFx[Kind]->Deactivate(); }
	if (Fx.IsValidIndex(Kind)) { CampBuffFx[Kind] = ArenaFx::AttachAt(Fx[Kind], GetMesh(), NAME_None, Seconds); }
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=camp_buff hero=%s team=%d kind=%d seconds=%.0f"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), Team, Kind, Seconds);
}

void AArenaCharacter::Displace(const FVector& Direction, float DistanceCm, float HeightCm, float Seconds)
{
	if (bDead || IsStructure() || IsBoss()) { return; }
	if (IsRecalling()) { CancelRecall(TEXT("knock")); }
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (DashRemaining > 0.f) { M->RemoveRootMotionSourceByID(DashRootMotion); DashRemaining = 0.f; }   // a knock stops a dash
	if (DisplaceRootMotion != 0) { M->RemoveRootMotionSourceByID(DisplaceRootMotion); DisplaceRootMotion = 0; }
	const FVector D = Direction.GetSafeNormal2D();
	const float Dur = FMath::Max(0.15f, Seconds);
	const float Now = GetWorld()->GetTimeSeconds();
	if (HeightCm > 1.f)
	{
		TSharedPtr<FRootMotionSource_JumpForce> J = MakeShared<FRootMotionSource_JumpForce>();
		J->InstanceName = TEXT("ArenaKnockUp");
		J->AccumulateMode = ERootMotionAccumulateMode::Override;
		J->Priority = 600;
		J->Duration = Dur;
		J->Rotation = D.IsNearlyZero() ? GetActorRotation() : D.Rotation();
		J->Distance = D.IsNearlyZero() ? 0.f : DistanceCm;
		J->Height = HeightCm;
		J->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		J->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
		M->SetMovementMode(MOVE_Falling);
		DisplaceRootMotion = M->ApplyRootMotionSource(J);
	}
	else if (DistanceCm > 1.f && !D.IsNearlyZero())
	{
		TSharedPtr<FRootMotionSource_MoveToForce> Move = MakeShared<FRootMotionSource_MoveToForce>();
		Move->InstanceName = TEXT("ArenaKnockBack");
		Move->AccumulateMode = ERootMotionAccumulateMode::Override;
		Move->Priority = 600;
		Move->StartLocation = GetActorLocation();
		Move->TargetLocation = DashDestination(D, DistanceCm);   // stopped by walls and ledges
		Move->Duration = Dur;
		Move->bRestrictSpeedToExpected = true;
		Move->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		Move->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
		DisplaceRootMotion = M->ApplyRootMotionSource(Move);
	}
	// a hit-stop freezing the body right now stretches the flight by its length
	DisplacedUntil = FMath::Max(DisplacedUntil, Now + Dur + (CustomTimeDilation < 0.5f ? 0.12f : 0.f));
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=displace id=%s from=%s dist=%.0f height=%.0f dur=%.2f"), Now, *Def.Id.ToString(), *GetActorLocation().ToCompactString(), DistanceCm, HeightCm, Dur);
}

bool AArenaCharacter::TryCast(int32 Slot)
{
	if (!AbilityHandles.IsValidIndex(Slot) || !CanCastSlot(Slot)) { return false; }
	if (IsRecalling()) { CancelRecall(TEXT("cast")); }
	return ASC->TryActivateAbility(AbilityHandles[Slot]);
}

void AArenaCharacter::SpendManaAndStartCooldown(int32 Slot, float Cost, float Cooldown)
{
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), FMath::Max(0.f, GetMana() - FMath::Max(0.f, Cost)));
	float Cd = Cooldown;
	if (Slot == 0)
	{
		const float Pct = ItemStats.AttackSpeedPct + (GetWorld()->GetTimeSeconds() < AtkSpeedUntil ? AtkSpeedPct : 0.f);
		Cd *= FMath::Max(0.2f, 1.f - Pct);
	}
	else { Cd = ArenaCore::ReducedCooldown(Cd, ItemStats.CooldownPct + (HasCampBuff(1) ? FArenaDatabase::Get().Rules.Conquest.BlackCooldown : 0.f)); }   // items (VR-12) and the black camp buff
	CooldownLen[Slot] = FMath::Max(0.1f, Cd);
	CooldownEnd[Slot] = GetWorld()->GetTimeSeconds() + Cd;
	if (Slot == 0 && !bMinion && IsRangedKit()) { FireSlowUntil = GetWorld()->GetTimeSeconds() + FArenaDatabase::Get().Rules.RangedFireSlowSeconds; }
	++CastCount;
	if (Slot >= 0 && Slot < 5) { ++SlotCasts[Slot]; }
}

bool AArenaCharacter::IsHostileTo(const AArenaCharacter* Other) const
{
	return Other && Other != this && Other->IsAlive() && ArenaCore::IsHostile(Team, Other->Team);
}

float AArenaCharacter::ReceiveDamage(float Raw, AArenaCharacter* Source, bool bUltimate)
{
	FArenaHit H;
	H.bUltimate = bUltimate;
	return ReceiveHit(Raw, Source, H);
}

float AArenaCharacter::ReceiveHit(float Raw, AArenaCharacter* Source, const FArenaHit& Hit)
{
	const bool bUltimate = Hit.bUltimate;
	if (!HasAuthority()) { return 0.f; }   // a LAN client draws blows, the server deals them
	if (bDead || Raw <= 0.f || (Source && !Source->IsHostileTo(this))) { return 0.f; }  // VR-01, VR-04
	if (bResetting) { return 0.f; }   // a monster walking home after its leash broke
	float Base = Raw;
	const FArenaConquestDef& Cq = FArenaDatabase::Get().Rules.Conquest;
	if (IsStructure())
	{
		// Conquest: the chain protects it; a hero's blow counts only as a basic attack from close, and far less with
		// none of the hero's minions near (LoL / Smite)
		AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>();
		const bool bHero = Source && !Source->IsMinion();
		float Scale = 1.f;
		bool bInReach = true;
		if (bHero)
		{
			bInReach = FVector::Dist2D(Source->GetActorLocation(), GetActorLocation()) - GetCapsuleComponent()->GetScaledCapsuleRadius() <= Cq.Reach * 100.f;
			Scale = ArenaConquest::HeroOnStructure(Hit.bBasic, bInReach, GM && GM->MinionsNear(Source->GetTeam(), GetActorLocation(), Cq.BackdoorRadius * 100.f), Cq.BackdoorCut);
		}
		if (bInvulnerable) { Scale = 0.f; }
		// the player learns why a blow did little or nothing (at most every 2.5 s)
		if (bHero && Source->PlayerIndex >= 0 && Scale < 1.f && GetWorld()->GetTimeSeconds() >= StructureHintAt)
		{
			StructureHintAt = GetWorld()->GetTimeSeconds() + 2.5f;
			const TCHAR* Why = bInvulnerable ? TEXT("Indestructible — destroy the structure before it on this lane first")
				: (!Hit.bBasic ? TEXT("Structures only take damage from basic attacks")
				: (!bInReach ? TEXT("Too far — the structure deflects shots from outside its range") : TEXT("Without your own minions nearby you deal 1/3 damage to structures")));
			// the hint goes to the hitter's own screen (a LAN guest's hit: to the guest, not the host)
			if (Source->IsLocalHero()) { AArenaHUD::StructureHint(this, Why); }
			else if (AArenaPlayerController* RPC = Cast<AArenaPlayerController>(Source->GetController())) { RPC->ClientSay(Why, FLinearColor(1.f, 0.75f, 0.35f)); }
		}
		if (Scale <= 0.f) { return 0.f; }
		Base *= Scale;
		if (Source && Source->IsLaneMinion()) { Base *= Cq.MinionOnStructure; }
	}
	if (Source && Source->IsStructure())
	{
		Base = ArenaConquest::TowerShot(Raw, !bMinion, Source->TowerRamp, Cq.TowerRamp, Cq.TowerRampMax, MinionKind(), Cq.MinionShotPct, GetMaxHealth());
	}
	if (Source && !Source->IsMinion() && !bMinion) { Source->LastHitHeroAt = GetWorld()->GetTimeSeconds(); }
	// v21 the melee trait: a melee hero takes less from a ranged hero (it has to walk through the shots to fight)
	if (Source && Source->IsRangedHero() && IsMeleeHero()) { Base *= 1.f - FMath::Clamp(FArenaDatabase::Get().Rules.MeleeRangedResist, 0.f, 0.6f); }
	ArenaCore::FDamageInput In;
	In.Base = Base;
	In.TargetArmor = Attributes->GetArmor();
	In.ArmorPenFlat = Hit.ArmorPen;
	In.AttackerLevel = 1; // level already applied to the source's Power/damage
	In.bCrit = Hit.bCrit;   // crits come from items only, and they are deterministic (ArenaCore::NextCrit)
	In.CritMultiplier = Hit.CritMult;
	const float Dmg = ArenaCore::ComputeDamage(In);
	// the death recap keeps who hit and with what for a few seconds; any blow breaks a recall
	if (Dmg > 0.f)
	{
		const float T = GetWorld()->GetTimeSeconds();
		if (Source && !Source->IsMinion() && !bMinion) { Source->MinionAggroUntil = T + 2.5f; }   // hero on hero: nearby minions answer
		FArenaDamageEvent E;
		E.Source = Source ? (Source->IsLaneMinion() ? FString(TEXT("Minion")) : Source->GetDef().DisplayName) : FString(TEXT("Fountain"));
		E.Ability = Hit.Ability;
		E.Amount = Dmg;
		E.Time = T;
		if (Source && !Source->IsMinion()) { E.SourceTeam = Source->GetTeam(); E.SourceHero = Source->HeroIndex; }
		if (!bMinion) { DamageTaken += Dmg; if (Source && !Source->IsMinion()) { Source->DamageToHeroes += Dmg; } }
		RecentDamage.Add(E);
		RecentDamage.RemoveAll([T](const FArenaDamageEvent& X) { return T - X.Time > 12.f; });
		if (RecentDamage.Num() > 40) { RecentDamage.RemoveAt(0, RecentDamage.Num() - 40); }
		if (IsRecalling()) { CancelRecall(TEXT("damage")); }
	}
	if (IsStructure() && Dmg > 0.f && Team >= 0 && Team <= 1)
	{
		if (AArenaGameMode* SGM = GetWorld()->GetAuthGameMode<AArenaGameMode>()) { SGM->StructureDamage[Team][Source && !Source->IsMinion() ? 0 : 1] += Dmg; }
	}
	const TPair<float, float> After = ArenaCore::ApplyDamage(GetShield(), GetHealth(), GetMaxHealth(), Dmg);
	const float HpBefore = GetHealth(), ShieldBefore = GetShield();
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetShieldAttribute(), After.Key);
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), After.Value);
	if (!bMinion && HpBefore > GetMaxHealth() * 0.25f && After.Value <= GetMaxHealth() * 0.25f && After.Value > 0.f) { Voice(TEXT("Health_Low"), 20.f); }
	if (ShieldBefore > After.Key && GetWorld()->GetTimeSeconds() >= ShieldHitFxAt)
	{
		ShieldHitFxAt = GetWorld()->GetTimeSeconds() + 0.35f;
		ArenaFx::AttachAt(FArenaDatabase::Get().Rules.ShieldHitFx, GetMesh(), TEXT("spine_03"), 0.6f);
	}
	LastDamageTime = GetWorld()->GetTimeSeconds();
	if (Source) { LastAttacker = Source; }
	if (Source && bAssistedAim && PlayerIndex >= 0 && (GetNetMode() == NM_Standalone || IsLocalHero())) { AArenaHUD::AddDamageDir(this, Source->GetActorLocation()); }
	HitFlashUntil = LastDamageTime + 0.12f;

	const bool bLan = GetNetMode() != NM_Standalone;
	const bool bByPlayer = Source && Source->bAssistedAim && (!bLan || Source->IsLocalHero());
	const bool bPlayersFight = bLan ? (bByPlayer || IsLocalHero()) : (bByPlayer || bAssistedAim || PlayerIndex >= 0 || (Source && Source->PlayerIndex >= 0));
	if (bPlayersFight) { AArenaHUD::AddDamageNumber(this, GetActorLocation() + FVector(0, 0, 110.f), Dmg, In.bCrit, bByPlayer); }
	// the body flashes white (the overlay material) when the player lands a blow on it, or when the player's own hero
	// is hit: a second draw of the mesh, so kept to the hits the player needs to see (bot-on-bot hits keep the tint flash)
	if (bByPlayer || (bAssistedAim && (!bLan || IsLocalHero())))
	{
		LastHitFlash = GetWorld()->GetTimeSeconds();
		TickHitFlash(LastHitFlash);
	}
	if (bLan && Dmg > 0.f) { MulticastHit(Dmg, In.bCrit, Source); }
	// the player's own hits: a marker on the crosshair and a tick sound (crit: higher, kill: a second, lower tick)
	if (bByPlayer)
	{
		const bool bKill = After.Value <= 0.f;
		AArenaHUD::NotifyPlayerHit(this, Dmg, In.bCrit, bKill);
		if (LastDamageTime >= Source->NextHitTick || bKill)
		{
			Source->NextHitTick = LastDamageTime + 0.06f;
			ArenaHitSound::Play(this, bKill ? 2 : (In.bCrit ? 1 : 0));
		}
	}
	const float Stop = bUltimate ? 0.12f : (Dmg > 80.f ? 0.08f : 0.05f);                       // 01 §5
	// hit-stop sells heavy blows between heroes; a minion's poke or a stream of hits must not keep anyone in slow motion
	if (Source && !Source->IsMinion() && !bMinion && (bUltimate || Dmg > 60.f || In.bCrit)) { HitStop(Stop); Source->HitStop(Stop); }
	if (bUltimate) { CameraShake(0.35f, 14.f); if (Source) { Source->CameraShake(0.3f, 10.f); } }
	else if (Source && Source->PlayerIndex >= 0) { Source->CameraShake(0.08f, 3.f); }
	if (PlayerIndex >= 0) { CameraShake(0.12f, 6.f); }
	ArenaFx::Spawn(this, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"), GetActorLocation(), FLinearColor::White);

	if (After.Value > 0.f && LastDamageTime >= NextPainSound) { ArenaFx::Sound(this, Def.PainSound, GetActorLocation(), 0.7f); NextPainSound = LastDamageTime + 1.5f; }
	if (After.Value <= 0.f) { Die(Source); }
	else
	{
		PlayHitReact(Source);
		// items: Guardian's Aegis — a shield when health falls low
		if (ItemPassives.LastStandShieldPct > 0.f && After.Value < GetMaxHealth() * ItemPassives.LastStandBelow && LastDamageTime >= LastStandReadyAt)
		{
			LastStandReadyAt = LastDamageTime + ItemPassives.LastStandCd;
			ReceiveShield(GetMaxHealth() * ItemPassives.LastStandShieldPct);
			ArenaFx::Spawn(this, TEXT("/Game/ParagonProps/FX/Particles/Core/P_Core_CharacterRecall.P_Core_CharacterRecall"), GetActorLocation(), FLinearColor(0.5f, 0.9f, 1.f));
			UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=passive name=laststand hero=%s shield=%.0f"), LastDamageTime, *Def.Id.ToString(), GetShield());
		}
	}
	// items: Pancerz tytana — a basic attack reflects part of the blow to the attacker
	if (Hit.bBasic && !Hit.bReflect && Source && Source != this && Source->IsAlive() && ItemPassives.ThornsPct > 0.f && Dmg > 0.f)
	{
		FArenaHit Back;
		Back.bReflect = true;
		Back.Ability = TEXT("Thorns (Titan's Armor)");
		Source->ReceiveHit(Dmg * ItemPassives.ThornsPct, this, Back);
	}
	return Dmg;
}

void AArenaCharacter::ReceiveHeal(float Amount, AArenaCharacter* Source, bool bShowNumber)
{
	if (!HasAuthority() || bDead || Amount <= 0.f || (Source && Source->Team != Team)) { return; }
	const float Missing = FMath::Max(0.f, GetMaxHealth() - GetHealth());
	if (Source && !Source->IsMinion()) { Source->HealingDone += FMath::Min(Amount, Missing); }   // effective healing only
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), FMath::Min(GetMaxHealth(), GetHealth() + Amount));
	// items: Krwiopijca — healing past full health turns into a shield (up to the item's cap)
	if (Source == this && ItemPassives.OverhealShieldMax > 0.f && Amount > Missing)
	{
		const float Room = ItemPassives.OverhealShieldMax - GetShield();
		if (Room > 0.f) { ReceiveShield(FMath::Min(Room, Amount - Missing)); }
	}
	// heal numbers: the player's own fights only, like the damage numbers
	if (bShowNumber && (PlayerIndex >= 0 || bAssistedAim || (Source && (Source->PlayerIndex >= 0 || Source->bAssistedAim)))) { AArenaHUD::AddDamageNumber(this, GetActorLocation() + FVector(0, 0, 120.f), -Amount, false, false); }
}

void AArenaCharacter::ReceiveShield(float Amount)
{
	if (bDead || Amount <= 0.f) { return; }
	if (GetShield() <= 0.f) { ArenaFx::AttachAt(FArenaDatabase::Get().Rules.ShieldFx, GetMesh(), TEXT("spine_03"), 1.2f); NetStatus(3, 1.2f); }
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetShieldAttribute(), GetShield() + Amount);
}

void AArenaCharacter::GrantDashShield()
{
	const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
	if (bDead || !IsMeleeHero() || Ru.MeleeDashShieldPct <= 0.f) { return; }
	// a top-up, not a stack: the shield is at least this big for a few seconds, then what is left of it goes
	const float Want = GetMaxHealth() * Ru.MeleeDashShieldPct;
	const float Add = FMath::Max(0.f, Want - GetShield());
	if (Add <= 0.f) { return; }
	ReceiveShield(Add);
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Add]()
	{
		if (!bDead && ASC) { ASC->SetNumericAttributeBase(UArenaAttributeSet::GetShieldAttribute(), FMath::Max(0.f, GetShield() - Add)); }
	}), FMath::Max(0.5f, Ru.MeleeDashShieldSeconds), false);
}

void AArenaCharacter::ApplyStun(float Seconds)
{
	if (bDead || Seconds <= 0.f || IsStructure() || IsBoss()) { return; }
	if (IsMeleeHero()) { Seconds *= 1.f - FMath::Clamp(FArenaDatabase::Get().Rules.MeleeTenacity, 0.f, 0.8f); }   // v21 tenacity
	if (IsRecalling()) { CancelRecall(TEXT("stun")); }
	StunUntil = FMath::Max(StunUntil, GetWorld()->GetTimeSeconds() + Seconds);
	ASC->AddLooseGameplayTag(ArenaTags::State_Stunned);
	if (GetWorld()->GetTimeSeconds() >= StunFxUntil)
	{
		const FArenaRulesDef& Ru = FArenaDatabase::Get().Rules;
		ArenaFx::AttachAt(Ru.StunStartFx, GetMesh(), TEXT("head"), 0.8f);
		ArenaFx::AttachAt(Ru.StunFx, GetMesh(), TEXT("head"), Seconds);
		StunFxUntil = GetWorld()->GetTimeSeconds() + Seconds;
		NetStatus(0, Seconds);
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (IsStunned()) { return; }
		ASC->RemoveLooseGameplayTag(ArenaTags::State_Stunned, 1000);
		UAnimInstance* Anim = GetMesh()->GetAnimInstance();
		if (Anim && StunMontage.IsValid()) { Anim->Montage_Stop(0.25f, StunMontage.Get()); }
		GetWorldTimerManager().ClearTimer(StunLoopTimer);
		StunMontage = nullptr;
	}), Seconds + 0.01f, false);
	UpdateMoveSpeed();
	// dazed: the pack's stun start, then its loop while the stun lasts (a knock in flight keeps its own pose)
	if (Def.StunAnims.Num() == 0 || Seconds < 0.3f || KnockMontage.IsValid()) { return; }
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (StunMontage.IsValid() && Anim && Anim->Montage_IsPlaying(StunMontage.Get())) { return; }
	StunMontage = PlaySlotAnim(Def.StunAnims[0], false, 0.1f, 0.25f);
	const float StartLen = StunMontage.IsValid() ? StunMontage->GetPlayLength() : 0.f;
	if (Def.StunAnims.IsValidIndex(1) && Seconds > StartLen + 0.2f)
	{
		GetWorldTimerManager().SetTimer(StunLoopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (IsStunned() && !bDead && !KnockMontage.IsValid()) { StunMontage = PlaySlotAnim(Def.StunAnims[1], false, 0.2f, 0.25f, 20); }
		}), FMath::Max(0.05f, StartLen - 0.2f), false);
	}
}

void AArenaCharacter::ApplySlow(float Pct, float Seconds)
{
	if (bDead || Seconds <= 0.f || IsStructure()) { return; }
	if (IsMeleeHero()) { Seconds *= 1.f - FMath::Clamp(FArenaDatabase::Get().Rules.MeleeTenacity, 0.f, 0.8f); }   // v21 tenacity
	SlowPct = FMath::Clamp(FMath::Max(SlowPct, Pct), 0.f, 0.8f);
	SlowUntil = FMath::Max(SlowUntil, GetWorld()->GetTimeSeconds() + Seconds);
	if (GetWorld()->GetTimeSeconds() >= SlowFxUntil) { ArenaFx::AttachAt(FArenaDatabase::Get().Rules.SlowFx, GetMesh(), NAME_None, Seconds); SlowFxUntil = SlowUntil; NetStatus(1, Seconds); }
}

void AArenaCharacter::ApplySpeedBuff(float Pct, float Seconds)
{
	SpeedPct = FMath::Max(SpeedPct, Pct);
	SpeedUntil = FMath::Max(SpeedUntil, GetWorld()->GetTimeSeconds() + Seconds);
	if (!bMinion && GetWorld()->GetTimeSeconds() >= SpeedFxUntil) { ArenaFx::AttachAt(FArenaDatabase::Get().Rules.SpeedFx, GetMesh(), NAME_None, Seconds); SpeedFxUntil = SpeedUntil; NetStatus(2, Seconds); }
}

void AArenaCharacter::ApplyAttackSpeedBuff(float Pct, float Seconds)
{
	AtkSpeedPct = FMath::Max(AtkSpeedPct, Pct);
	AtkSpeedUntil = FMath::Max(AtkSpeedUntil, GetWorld()->GetTimeSeconds() + Seconds);
}

void AArenaCharacter::ApplyPowerBuff(float DamageMult, float BonusSpeedPct, float Seconds)
{
	PowerMult = FMath::Max(1.f, DamageMult);
	PowerUntil = GetWorld()->GetTimeSeconds() + Seconds;
	ApplySpeedBuff(BonusSpeedPct, Seconds);
}

bool AArenaCharacter::HasPowerBuff() const { return GetWorld() && GetWorld()->GetTimeSeconds() < PowerUntil; }
float AArenaCharacter::DamageMultiplier() const
{
	const FArenaConquestDef& Cq = FArenaDatabase::Get().Rules.Conquest;
	return (HasPowerBuff() ? PowerMult : 1.f) * (HasCampBuff(0) ? 1.f + Cq.RedDamage : 1.f) * (HasCampBuff(2) ? 1.f + Cq.BossDamage : 1.f) * (1.f + EdgePower);
}

void AArenaCharacter::UpdateMoveSpeed()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	float Mult = 1.f;
	if (Now < SlowUntil) { Mult *= 1.f - SlowPct; } else { SlowPct = 0.f; }
	if (Now < FireSlowUntil) { Mult *= 1.f - FArenaDatabase::Get().Rules.RangedFireSlow; }
	if (Now < SpeedUntil) { Mult *= 1.f + SpeedPct; } else { SpeedPct = 0.f; }
	if (HasCampBuff(2)) { Mult *= 1.f + FArenaDatabase::Get().Rules.Conquest.BossSpeed; }
	if (Now < StunUntil || Now < BusyUntil || bDead) { Mult = 0.f; }
	// held in place: 1 cm/s, not 0 (the Paragon anim blueprints divide their speed by it; 0 logged a divide by zero
	// every stun and fed their blend spaces garbage)
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(1.f, (Attributes ? Attributes->GetMoveSpeed() : 6.f) * 100.f * Mult);
}

FVector AArenaCharacter::DashDestination(const FVector& Direction, float DistanceCm) const
{
	const FVector Dir = Direction.GetSafeNormal2D();
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Start = GetActorLocation();
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation From;
		if (Nav->ProjectPointToNavigation(Start - FVector(0.f, 0.f, Half), From, FVector(100.f, 100.f, 250.f)))
		{
			const FVector To = From.Location + Dir * DistanceCm;
			FVector Hit;
			FVector End = To;
			if (UNavigationSystemV1::NavigationRaycast(const_cast<AArenaCharacter*>(this), From.Location, To, Hit)) { End = Hit - Dir * 10.f; }
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(End, OnNav, FVector(60.f, 60.f, 320.f))) { End = OnNav.Location; }
			return End + FVector(0.f, 0.f, Half);
		}
	}
	// no navmesh around: stop in front of the first wall
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ArenaDashEnd), false, this);
	const FVector End = Start + Dir * DistanceCm;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius(), FMath::Max(10.f, Half - 40.f));
	if (GetWorld()->SweepSingleByObjectType(H, Start + FVector(0, 0, 40.f), End + FVector(0, 0, 40.f), FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Q))
	{
		return H.Location - FVector(0, 0, 40.f);
	}
	return End;
}

void AArenaCharacter::StartDash(const FVector& Direction, float DistanceCm, const FArenaAbilityDef& Ability, float FinishSpeed)
{
	DashDir = Direction.GetSafeNormal2D();
	DashAbility = Ability;
	DashHits.Reset();
	// the movement component carries the dash (root-motion move-to): full collision, it follows ramps and slides
	// along what it grazes; the end point is on walkable ground, never inside a wall or past a ledge
	const FVector Start = GetActorLocation();
	const FVector End = DashDestination(DashDir, DistanceCm);
	const float Len = FVector::Dist(Start, End);
	const float Duration = FMath::Max(0.08f, Len / 2600.f);   // 26 m/s
	DashRemaining = FMath::Max(1.f, Len);
	DashEndTime = GetWorld()->GetTimeSeconds() + Duration;
	TSharedPtr<FRootMotionSource_MoveToForce> Move = MakeShared<FRootMotionSource_MoveToForce>();
	Move->InstanceName = TEXT("ArenaDash");
	Move->AccumulateMode = ERootMotionAccumulateMode::Override;
	Move->Priority = 500;
	Move->StartLocation = Start;
	Move->TargetLocation = End;
	Move->Duration = Duration;
	Move->bRestrictSpeedToExpected = true;
	Move->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Move->FinishVelocityParams.ClampVelocity = FinishSpeed;
	DashRootMotion = GetCharacterMovement()->ApplyRootMotionSource(Move);
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=dash id=%s from=%s to=%s len=%.0f dur=%.2f rm=%d mode=%d ctrl=%d"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), *Start.ToCompactString(), *End.ToCompactString(), Len, Duration, (int32)DashRootMotion, (int32)GetCharacterMovement()->MovementMode, GetController() ? 1 : 0);
}

void AArenaCharacter::HoldFacing(float Yaw, float Seconds)
{
	HeldYaw = Yaw;
	HoldFacingUntil = GetWorld()->GetTimeSeconds() + Seconds;
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

static TAutoConsoleVariable<int32> CVarArenaFaceMovement(TEXT("arena.FaceMovement"), 1,
	TEXT("1: heroes face where they run and turn to their aim for a cast (the Paragon anim blueprints have a forward jog only). 0: the old camera-facing strafe."));

void AArenaCharacter::TickRelax(float Now)
{
	if (bMinion || IsStructure() || !IsAlive() || Def.IdleRelaxed.IsEmpty() || !IsOnAnimBlueprint()) { return; }
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim) { return; }
	// combat: a cast, a blow taken, or any other montage on the body (a cast, a hit reaction) in the last 3 s
	int32 Casts = 0;
	for (int32 i = 0; i < 5; ++i) { Casts += SlotCasts[i]; }
	if (Casts != RelaxCastsSeen) { RelaxCastsSeen = Casts; LastCombatAt = Now; }
	LastCombatAt = FMath::Max(LastCombatAt, LastDamageTime);
	UAnimMontage* Relax = RelaxMontage.Get();
	const bool bRelaxOn = Relax && Anim->Montage_IsPlaying(Relax);
	const UAnimMontage* Active = Anim->GetCurrentActiveMontage();
	if (Active && Active != Relax) { LastCombatAt = Now; }
	const UCharacterMovementComponent* M = GetCharacterMovement();
	const bool bStanding = GetVelocity().Size2D() < 25.f && M->GetCurrentAcceleration().IsNearlyZero() && M->IsMovingOnGround();
	if (!bStanding) { StandingSince = -1.f; }
	else if (StandingSince < 0.f) { StandingSince = Now; }
	const bool bWant = bStanding && Now - StandingSince > 0.35f && Now - LastCombatAt > 3.f && !IsStunned() && !IsBusy() && !IsDashing() && !IsAirborne();
	if (bWant && !bRelaxOn && (!Active || Active == Relax))
	{
		UAnimSequenceBase* Seq = RelaxAnim.Get();
		if (!Seq) { Seq = LoadObject<UAnimSequenceBase>(nullptr, *Def.IdleRelaxed, nullptr, LOAD_NoWarn | LOAD_Quiet); RelaxAnim = Seq; }
		if (Seq) { RelaxMontage = Anim->PlaySlotAnimationAsDynamicMontage(Seq, AnimSlot(true), 0.6f, 0.25f, 1.f, 1000); }
	}
	else if (!bWant && bRelaxOn) { Anim->Montage_Stop(0.25f, Relax); }
}

void AArenaCharacter::FaceAim(float Yaw, float Seconds)
{
	FaceAimYaw = Yaw;
	FaceAimUntil = GetWorld()->GetTimeSeconds() + Seconds;
}

void AArenaCharacter::UpdateFacing(float Now, float Dt)
{
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (CVarArenaFaceMovement.GetValueOnGameThread() == 0)
	{
		M->bOrientRotationToMovement = false;
		return;   // UseSmoothTurning's flags as before
	}
	bUseControllerRotationYaw = false;
	M->bUseControllerDesiredRotation = false;
	if (Now < HoldFacingUntil)
	{
		// a melee swing's hold (already snapped by HoldFacing)
		M->bOrientRotationToMovement = false;
		SetActorRotation(FRotator(0.f, HeldYaw, 0.f));
	}
	else if (Now < FaceAimUntil && !IsDashing())
	{
		// a cast: a fast turn to the aim (180 degrees in ~0.13 s; the hit itself goes along the aim either way)
		M->bOrientRotationToMovement = false;
		const FRotator Cur = GetActorRotation();
		SetActorRotation(FMath::RInterpConstantTo(FRotator(0.f, Cur.Yaw, 0.f), FRotator(0.f, FaceAimYaw, 0.f), Dt, 1080.f));
	}
	else
	{
		// where it runs: the pack's jog with its lean into the turn (YawDelta)
		M->bOrientRotationToMovement = true;
		M->RotationRate = FRotator(0.f, 720.f, 0.f);
	}
}

void AArenaCharacter::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	if (bDead) { return; }
	if (GetWorld() && GetWorld()->GetTimeSeconds() < HoldFacingUntil) { NewControlRotation.Yaw = HeldYaw; }
	Super::FaceRotation(NewControlRotation, DeltaTime);
}

void AArenaCharacter::Lunge(const FVector& Direction, float DistanceCm)
{
	FArenaAbilityDef Step;           // no damage, no end blast: the move only
	StartDash(Direction, DistanceCm, Step, 0.f);   // stops where the step ends (no slide past the swing)
}

void AArenaCharacter::TickDash(float DeltaSeconds)
{
	if (DashRemaining <= 0.f) { return; }
	if (DashAbility.Damage > 0.f && DashAbility.Radius > 0.f)
	{
		ArenaAbilityHelpers::DamageInRadius(this, GetActorLocation(), DashAbility.Radius * 100.f, DashAbility, DashAbility.Damage, &DashHits);
	}
	// a new root-motion source waits in the movement component's pending list until its next move: the clock, not a
	// lookup, says when the dash is over (the source ends itself at the same moment)
	const bool bRunning = GetWorld()->GetTimeSeconds() < DashEndTime;
	if (!bRunning)                              // runs exactly once: the tick the dash ends
	{
		DashRemaining = 0.f;
		GetCharacterMovement()->RemoveRootMotionSourceByID(DashRootMotion);
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=dash_end id=%s at=%s"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), *GetActorLocation().ToCompactString());
		if (DashAbility.EndRadius > 0.f)
		{
			TSet<uint64> EndHits;
			ArenaAbilityHelpers::DamageInRadius(this, GetActorLocation(), DashAbility.EndRadius * 100.f, DashAbility, DashAbility.EndDamage, &EndHits);
			ArenaPhysics::RadialImpulse(GetWorld(), GetActorLocation(), DashAbility.EndRadius * 150.f, 1200.f);
			if (DashAbility.bUltimate) { CameraShake(0.3f, 12.f); }
		}
		ArenaFx::Spawn(this, DashAbility.Fx, GetActorLocation(), FArenaDatabase::Hex(DashAbility.Color));
	}
}

void AArenaCharacter::SlideOffCharacters()
{
	// UArenaMovementComponent never takes a unit or a loose prop as a floor, so whoever lands on one slides down its
	// round top. Dead centre there is nothing to slide along: ease sideways, horizontally only (no hop, no launch).
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (DashRemaining > 0.f || !Move->IsFalling()) { PerchedSince = -1.f; return; }
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const FVector Start = GetActorLocation();
	const FVector End = Start - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight() + 12.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaSlideOff), false, this);
	FHitResult Under;
	if (!GetWorld()->SweepSingleByChannel(Under, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius() * 0.8f), Params)) { return; }
	if (!UArenaMovementComponent::IsUnstandable(Under)) { PerchedSince = -1.f; return; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (PerchedSince < 0.f) { PerchedSince = Now; }
	const FVector Below = Under.GetComponent() ? Under.GetComponent()->GetComponentLocation() : Under.ImpactPoint;
	FVector Away = (Start - Below).GetSafeNormal2D();
	if (Away.IsNearlyZero()) { Away = GetActorRightVector(); }
	if (Move->Velocity.Size2D() < 60.f) { Move->Velocity += Away * 220.f; }
	// wedged on two shoulders (each capsule holds the other side): after half a second step off decisively, still sideways
	else if (Now - PerchedSince > 0.5f) { Move->Velocity = FVector(Away.X * 320.f, Away.Y * 320.f, FMath::Min(0.f, (float)Move->Velocity.Z)); }
}

// ---- reaction animations ------------------------------------------------------------------------------------
static TArray<FName> AnimSlotsOf(const UAnimInstance* Anim)
{
	// slot nodes of the anim blueprint (FAnimNode_Slot lives in AnimGraphRuntime; read by reflection)
	TArray<FName> Out;
	for (TFieldIterator<FStructProperty> It(Anim->GetClass()); It; ++It)
	{
		if (It->Struct->GetFName() != TEXT("AnimNode_Slot")) { continue; }
		if (const FNameProperty* SlotName = CastField<FNameProperty>(It->Struct->FindPropertyByName(TEXT("SlotName"))))
		{
			Out.AddUnique(SlotName->GetPropertyValue_InContainer(It->ContainerPtrToValuePtr<void>(Anim)));
		}
	}
	return Out;
}

FName AArenaCharacter::AnimSlot(bool bUpperBody) const
{
	static TMap<const UClass*, TPair<FName, FName>> Cache;   // class -> (full body, upper body)
	const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) { return TEXT("DefaultSlot"); }
	if (const TPair<FName, FName>* Hit = Cache.Find(Anim->GetClass())) { return bUpperBody ? Hit->Value : Hit->Key; }
	const TArray<FName> Slots = AnimSlotsOf(Anim);
	FName Full = NAME_None, Upper = NAME_None;
	for (const FName& N : Slots)
	{
		const FString S = N.ToString();
		if (Full.IsNone() && (N == TEXT("DefaultSlot") || S.Contains(TEXT("Full")))) { Full = N; }
		if (Upper.IsNone() && S.Contains(TEXT("Upper"))) { Upper = N; }
	}
	if (Full.IsNone()) { Full = Slots.Num() > 0 ? Slots[0] : FName(TEXT("DefaultSlot")); }
	if (Upper.IsNone()) { Upper = Full; }
	Cache.Add(Anim->GetClass(), TPair<FName, FName>(Full, Upper));
	UE_LOG(LogArena, Display, TEXT("ARENA evt=anim_slots class=%s slots=%s full=%s upper=%s"), *Anim->GetClass()->GetName(),
		*FString::JoinBy(Slots, TEXT(","), [](const FName& N) { return N.ToString(); }), *Full.ToString(), *Upper.ToString());
	return bUpperBody ? Upper : Full;
}

UAnimMontage* AArenaCharacter::PlaySlotAnim(const FString& Path, bool bUpperBody, float BlendIn, float BlendOut, int32 Loops, float Rate)
{
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim || Path.IsEmpty() || !IsOnAnimBlueprint()) { return nullptr; }
	UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UAnimMontage* M = Cast<UAnimMontage>(Obj);
	if (!M)
	{
		if (UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(Obj)) { M = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Seq, AnimSlot(bUpperBody), BlendIn, BlendOut, 1.f, FMath::Max(1, Loops)); }
	}
	if (!M) { return nullptr; }
	return Anim->Montage_Play(M, Rate) > 0.f ? M : nullptr;
}

float AArenaCharacter::PlaySingleNode(const FString& Path, bool bLoop, float BlendTime)
{
	UAnimSequenceBase* Seq = Path.IsEmpty() ? nullptr : Cast<UAnimSequenceBase>(StaticLoadObject(UAnimSequenceBase::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	if (!Seq) { return 0.f; }
	GetWorldTimerManager().ClearTimer(RestoreAbpTimer);
	// the pose on screen now: the new animation starts from it and takes over within BlendTime (the engine's
	// PlayAnimation cut to the first frame, a visible pose pop at every death)
	FPoseSnapshot From;
	if (BlendTime > 0.f && GetMesh()->GetAnimInstance() && GetMesh()->GetBoneSpaceTransforms().Num() > 0) { GetMesh()->SnapshotPose(From); }
	if (UArenaPoseBlendInstance* Cur = Cast<UArenaPoseBlendInstance>(GetMesh()->GetAnimInstance()))
	{
		Cur->Play(Seq, bLoop, From, BlendTime);
		return Seq->GetPlayLength();
	}
	UArenaPoseBlendInstance::SetPending(Seq, bLoop, From, BlendTime);
	GetMesh()->SetAnimInstanceClass(UArenaPoseBlendInstance::StaticClass());
	if (UArenaPoseBlendInstance* Now = Cast<UArenaPoseBlendInstance>(GetMesh()->GetAnimInstance())) { Now->PlayPending(); }
	else { UArenaPoseBlendInstance::ClearPending(); GetMesh()->PlayAnimation(Seq, bLoop); }
	return Seq->GetPlayLength();
}

void AArenaCharacter::RestoreAnimBlueprint()
{
	if (bDead || IsOnAnimBlueprint()) { return; }
	if (AbpClass) { GetMesh()->SetAnimInstanceClass(AbpClass); SetupNativeAnim(); }
	else { GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint); }
}

void AArenaCharacter::PlayHitReact(const AArenaCharacter* Source)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Def.HitReacts.Num() != 4 || !Source || Now < NextHitReact || IsStunned() || IsBusy()) { return; }
	// a minion flinches at a hero's blow while it stands (a full-body flinch mid-run would skate its feet)
	if (bMinion && (Source->IsMinion() || GetVelocity().Size2D() > 150.f)) { return; }
	const UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || Anim->IsAnyMontagePlaying()) { return; }                       // never cut an attack, a stun or a knock
	NextHitReact = Now + (bMinion ? 1.6f : 0.9f);
	const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Source->GetActorLocation() - GetActorLocation());
	const int32 Dir = FMath::Abs(Local.X) >= FMath::Abs(Local.Y) ? (Local.X >= 0.f ? 0 : 1) : (Local.Y < 0.f ? 2 : 3);   // front back left right
	PlaySlotAnim(Def.HitReacts[Dir], true, 0.06f, 0.2f);
}

void AArenaCharacter::OnKnocked(const FVector& Direction, bool bUp)
{
	if (bDead || Def.KnockAnims.Num() == 0) { return; }
	const int32 I = bUp && Def.KnockAnims.IsValidIndex(2) ? 2 : (FVector::DotProduct(Direction.GetSafeNormal2D(), GetActorForwardVector()) < 0.f ? 0 : FMath::Min(1, Def.KnockAnims.Num() - 1));
	if (UAnimMontage* M = PlaySlotAnim(Def.KnockAnims[I], false, 0.08f, 0.25f))
	{
		KnockMontage = M;
		KnockStart = GetWorld()->GetTimeSeconds();
	}
}

void AArenaCharacter::PlaySpawnIn()
{
	if (HasAuthority() && GetNetMode() != NM_Standalone) { MulticastSpawnIn(); }
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	ArenaFx::Spawn(this, FArenaDatabase::Get().Rules.RespawnFx, Feet, FLinearColor::White, 0.55f);
	ArenaFx::Spawn(this, Def.SpawnFx, GetActorLocation(), FLinearColor::White);
	if (bMinion) { return; }
	const float Len = PlaySingleNode(Def.RespawnAnim, false);
	if (Len <= 0.f) { return; }
	BusyUntil = GetWorld()->GetTimeSeconds() + Len;
	UpdateMoveSpeed();
	GetWorldTimerManager().SetTimer(RestoreAbpTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { RestoreAnimBlueprint(); }), Len, false);
}

void AArenaCharacter::PlayIntro()
{
	if (!bMinion) { IntroMontage = PlaySlotAnim(Def.IntroAnim, true, 0.2f, 0.4f); }
}

void AArenaCharacter::StopIntro()
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && IntroMontage.IsValid()) { Anim->Montage_Stop(0.4f, IntroMontage.Get()); }
	IntroMontage = nullptr;
}

void AArenaCharacter::PlayVictory()
{
	if (bDead || bMinion) { return; }
	GetCharacterMovement()->StopMovementImmediately();
	BusyUntil = GetWorld()->GetTimeSeconds() + 3600.f;
	UpdateMoveSpeed();
	PlaySingleNode(Def.VictoryAnim, true);
}

int32 AArenaCharacter::ChooseDeathAnim(const AArenaCharacter* Killer)
{
	const int32 N = Def.DeathAnims.Num();
	if (N == 0) { return INDEX_NONE; }
	// face the killer (the pack's "hit from the front" falls read right), then pick a fall that has room: a body
	// must not end up inside a wall, a tree or a rock
	if (Killer)
	{
		const FVector To = (Killer->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero()) { SetActorRotation(To.Rotation()); }
	}
	const FVector From = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.4f);
	auto HasRoom = [&](int32 i, float Yaw) -> bool
	{
		if (!Def.DeathFall.IsValidIndex(i)) { return true; }
		const FVector2D Fall = Def.DeathFall[i] * GetActorScale3D().X;
		if (Fall.Size() < 40.f) { return true; }
		const FVector Dir = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(Fall.X, Fall.Y, 0.f)).GetSafeNormal();
		FHitResult Hit;
		FCollisionQueryParams P(SCENE_QUERY_STAT(ArenaDeathRoom), false, this);
		return !GetWorld()->SweepSingleByChannel(Hit, From, From + Dir * (Fall.Size() + 60.f), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(30.f), P);
	};
	const int32 First = FMath::RandRange(0, N - 1);
	const float Yaw0 = GetActorRotation().Yaw;
	for (int32 k = 0; k < N; ++k) { if (HasRoom((First + k) % N, Yaw0)) { return (First + k) % N; } }
	for (int32 Step = 1; Step < 8; ++Step)
	{
		const float Yaw = Yaw0 + Step * 45.f;
		if (HasRoom(First, Yaw)) { SetActorRotation(FRotator(0.f, Yaw, 0.f)); return First; }
	}
	return First;
}

void AArenaCharacter::TickReactions(float Now)
{
	// a knock pose lasts while airborne; back on the ground the unit recovers (into its stun if one still runs)
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (KnockMontage.IsValid() && Anim && Now - KnockStart > 0.25f && GetCharacterMovement()->IsMovingOnGround())
	{
		Anim->Montage_Stop(0.25f, KnockMontage.Get());
		KnockMontage = nullptr;
		if (IsStunned() && Def.StunAnims.Num() > 0) { StunMontage = PlaySlotAnim(Def.StunAnims.Last(), false, 0.2f, 0.25f, 20); }
	}
	else if (KnockMontage.IsValid() && Anim && !Anim->Montage_IsPlaying(KnockMontage.Get())) { KnockMontage = nullptr; }
}

void AArenaCharacter::DebugDumpAnim() const
{
	const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) { return; }
	FString Vars;
	for (TFieldIterator<FProperty> It(Anim->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const FProperty* P = *It;
		if (P->GetName().StartsWith(TEXT("AnimGraphNode_")) || P->GetName().StartsWith(TEXT("__"))) { continue; }
		if (const FFloatProperty* F = CastField<FFloatProperty>(P)) { Vars += FString::Printf(TEXT("%s=%.1f "), *P->GetName(), F->GetPropertyValue_InContainer(Anim)); }
		else if (const FDoubleProperty* D = CastField<FDoubleProperty>(P)) { Vars += FString::Printf(TEXT("%s=%.1f "), *P->GetName(), D->GetPropertyValue_InContainer(Anim)); }
		else if (const FBoolProperty* B = CastField<FBoolProperty>(P)) { Vars += FString::Printf(TEXT("%s=%d "), *P->GetName(), B->GetPropertyValue_InContainer(Anim) ? 1 : 0); }
		else if (const FObjectPropertyBase* O = CastField<FObjectPropertyBase>(P)) { const UObject* V = O->GetObjectPropertyValue_InContainer(Anim); Vars += FString::Printf(TEXT("%s=%s "), *P->GetName(), V ? *V->GetClass()->GetName() : TEXT("null")); }
		else if (const FStructProperty* S = CastField<FStructProperty>(P)) { Vars += FString::Printf(TEXT("%s:%s "), *P->GetName(), *S->Struct->GetName()); }
	}
	UE_LOG(LogArena, Display, TEXT("ARENA animdbg id=%s minion=%d cls=%s vel=%.0f accel=%.0f mode=%d montage=%d vars: %s"), *Def.Id.ToString(), bMinion ? 1 : 0, *Anim->GetClass()->GetName(),
		GetVelocity().Size2D(), GetCharacterMovement()->GetCurrentAcceleration().Size(), (int32)GetCharacterMovement()->MovementMode, Anim->IsAnyMontagePlaying() ? 1 : 0, *Vars);
}

void AArenaCharacter::TickHitFlash(float Now)
{
	const float Age = Now - LastHitFlash;
	const float Alpha = Age < 0.18f ? 1.f - Age / 0.18f : 0.f;
	if (Alpha <= 0.f)
	{
		if (GetMesh()->GetOverlayMaterial()) { GetMesh()->SetOverlayMaterial(nullptr); }   // only drawn while flashing
		return;
	}
	if (!FlashMaterial)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_ArenaHitFlash.M_ArenaHitFlash")))
		{
			FlashMaterial = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	if (!FlashMaterial) { return; }
	FlashMaterial->SetScalarParameterValue(TEXT("Flash"), Alpha);
	if (GetMesh()->GetOverlayMaterial() != FlashMaterial) { GetMesh()->SetOverlayMaterial(FlashMaterial); }
}

void AArenaCharacter::HitStop(float Seconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextHitStop) { return; }
	NextHitStop = Now + Seconds + 0.4f;
	CustomTimeDilation = 0.05f;
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() { CustomTimeDilation = 1.f; }), Seconds, false);
}

void AArenaCharacter::CameraShake(float Seconds, float Strength)
{
	if (PlayerIndex < 0 || !bShakeEnabled) { return; }
	ShakeUntil = FMath::Max(ShakeUntil, GetWorld()->GetTimeSeconds() + Seconds);
	ShakeStrength = FMath::Max(ShakeStrength, Strength);
}

void AArenaCharacter::AddXp(float Amount)
{
	if (const AArenaGameMode* XGM = GetWorld() ? GetWorld()->GetAuthGameMode<AArenaGameMode>() : nullptr) { if (XGM->bConquest) { Amount *= FArenaDatabase::Get().Rules.Conquest.XpScale; } }
	if (bMinion || bDead || Amount <= 0.f) { return; }
	const FArenaRulesDef& R = FArenaDatabase::Get().Rules;
	Xp += Amount;
	const int32 NewLevel = ArenaCore::LevelForXp(Xp, R.XpBase, R.XpGrowth, MaxLevel());
	if (NewLevel <= Level) { return; }
	const int32 Old = Level;
	Level = NewLevel;
	ApplyLevelStats(false);
	ArenaFx::AttachAt(R.LevelUpFx, GetMesh(), NAME_None, 2.f);
	NetStatus(4, 2.f);
	Voice(Level >= 5 && Old < 5 ? TEXT("Level_Five") : TEXT("Level_Up"), 2.f);
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.2f evt=levelup hero=%s team=%d level=%d points=%d hp=%.0f power=%.0f"), GetWorld()->GetTimeSeconds(), *Def.Id.ToString(), Team, Level, FreeSkillPoints(), GetMaxHealth(), GetPower());
	if (!bAssistedAim) { AutoRank(); return; }
	// the player chooses: a line in the feed, the HUD shows the free points on the ability bar
	if (GetNetMode() != NM_Standalone && !IsLocallyControlled())
	{
		if (AArenaPlayerController* RPC = Cast<AArenaPlayerController>(GetController())) { RPC->ClientSay(FString::Printf(TEXT("Level %d  ·  skill points: %d  (Ctrl + 1-4)"), Level, FreeSkillPoints()), FLinearColor(1.f, 0.85f, 0.3f)); }
	}
	else if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->Announce(this, FString::Printf(TEXT("Level %d  ·  skill points: %d  (Ctrl + 1-4)"), Level, FreeSkillPoints()), false, FLinearColor(1.f, 0.85f, 0.3f));
		if (ArenaCore::MaxRankAt(4, Level) > ArenaCore::MaxRankAt(4, Old)) { GM->Announce(this, TEXT("You can upgrade your ultimate (Ctrl + 4)"), false, FLinearColor(1.f, 0.7f, 0.2f)); }
	}
}

void AArenaCharacter::Die(AArenaCharacter* Killer, int32 ForcedPick)
{
	if (bDead) { return; }
	bDead = true;
	if (HasAuthority() && GetNetMode() != NM_Standalone)
	{
		// the death as state too (a multicast reaches only who is connected now: a guest joining later saw fallen
		// towers standing and dead bodies alive)
		WriteNetState();
		NetVitals.Health = 0.f;
		NetVitals.bDead = true;
	}
	if (HasAuthority())
	{
		++Deaths;
		ASC->AddLooseGameplayTag(ArenaTags::State_Dead);
		ASC->CancelAllAbilities();
	}
	if (DashRemaining > 0.f) { GetCharacterMovement()->RemoveRootMotionSourceByID(DashRootMotion); }
	DashRemaining = 0.f;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);           // a body pushes nothing and nobody
	GetWorldTimerManager().ClearTimer(RestoreAbpTimer);
	GetWorldTimerManager().ClearTimer(StunLoopTimer);
	// the pack's own death animation, blended in from the pose of the moment; the body turns to its killer over a
	// few frames
	const float YawBefore = GetActorRotation().Yaw;
	const int32 Pick = ForcedPick > -2 ? ForcedPick : ChooseDeathAnim(Killer);
	DeathYawOffset = FRotator::NormalizeAxis(YawBefore - GetActorRotation().Yaw);
	GetMesh()->SetRelativeRotation(FRotator(0.f, Def.MeshYaw + DeathYawOffset, 0.f));
	float Len = 0.f;
	if (Pick != INDEX_NONE)
	{
		// a minion's anim blueprint has a full-body slot: its stagger is a montage there, blended in by the engine;
		// the mannequin's death clips end on their feet (the template ragdolls after them), so a minion goes limp
		// a moment into it (StartRagdoll). The Paragon falls end lying; the Paragon blueprints only have an
		// upper-body slot, so a hero's fall is played outside the blueprint (UArenaPoseBlendInstance)
		UAnimInstance* Anim = GetMesh()->GetAnimInstance();
		UAnimSequenceBase* Seq = bMinion && Anim && IsOnAnimBlueprint() ? LoadObject<UAnimSequenceBase>(nullptr, *Def.DeathAnims[Pick], nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		UAnimMontage* M = Seq && !Seq->IsA<UAnimMontage>() ? UAnimMontage::CreateSlotAnimationAsDynamicMontage(Seq, AnimSlot(false), 0.18f, 0.f, 1.f, 1) : nullptr;
		if (M)
		{
			M->bEnableAutoBlendOut = false;
			Anim->StopAllMontages(0.1f);
			Len = Anim->Montage_Play(M) > 0.f ? M->GetPlayLength() : 0.f;
		}
		if (Len <= 0.f) { Len = PlaySingleNode(Def.DeathAnims[Pick], false, 0.18f); }
	}
	if (NavBlock) { NavBlock->DestroyComponent(); NavBlock = nullptr; }
	if (RangeRing) { RangeRing->SetVisibility(false); }
	if (IsStructure())
	{
		ArenaFx::Spawn(this, FArenaDatabase::Get().Rules.Conquest.StructureDeathFx, GetActorLocation(), FLinearColor::White, 1.4f);
		if (StaticBody) { StaticBody->SetVisibility(false); }
	}
	if (bMinion && !IsStructure() && GetMesh()->GetPhysicsAsset())
	{
		RagdollAt = GetWorld()->GetTimeSeconds() + FMath::Min(0.35f, Len * 0.4f);
		const FVector Away = Killer ? (GetActorLocation() - Killer->GetActorLocation()).GetSafeNormal2D() : -GetActorForwardVector();
		RagdollPush = Away * 160.f + FVector(0.f, 0.f, 40.f);   // a slump away from the blow, not a launch
		Len = FMath::Min(Len, 1.0f);                          // lying still by then: the fade timing below
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// then the body lies still and fades away (LoL / Smite): a minion soon after it falls, a hero a little later;
	// without generated fade materials it sinks into the ground as before
	if (CanFade()) { FadeAt = Now + Len + (bMinion ? 1.2f : 2.5f); FadeLen = bMinion ? 1.0f : 1.5f; SinkAt = -1.f; }
	else { SinkAt = Now + Len + (bMinion ? 1.0f : 3.0f); }
	if (!Def.DeathVanishFx.IsEmpty()) { VanishAt = Now + Len * 0.9f; SinkAt = -1.f; FadeAt = -1.f; }
	if (IsStructure()) { FadeAt = -1.f; SinkAt = -1.f; VanishAt = -1.f; }   // the ruin stays
	UE_LOG(LogArena, Verbose, TEXT("ARENA t=%.2f evt=body id=%s anim=%.2f fade_at=%.2f turn=%.0f"), Now, *Def.Id.ToString(), Len, FadeAt, DeathYawOffset);
	for (UMaterialInstanceDynamic* MID : TintMaterials) { if (MID) { MID->SetScalarParameterValue(TEXT("EmissivePower"), 0.f); } }   // no hit flash frozen on the body
	GetMesh()->SetOverlayMaterial(nullptr);
	if (AuraDecal) { AuraDecal->SetVisibility(false); }
	if (TeamRing) { TeamRing->SetVisibility(false); }
	ArenaFx::Spawn(this, Def.DeathFx, GetActorLocation(), FLinearColor::White);
	ArenaFx::Sound(this, Def.DeathSound, GetActorLocation());
	if (Killer && !Killer->IsMinion() && !bMinion) { ArenaFx::Sound(Killer, Killer->GetDef().KillSound, Killer->GetActorLocation(), 0.9f); }
	if (!HasAuthority()) { return; }   // a LAN client: the fall only; the server destroys the body
	if (GetNetMode() != NM_Standalone) { MulticastDie(Killer, Pick); }
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>()) { GM->OnCharacterDied(this, Killer); }
	// heroes: the (hidden) body keeps the match stats until the respawn
	if (IsStructure()) { return; }
	SetLifeSpan(bMinion ? FMath::Max(4.f, Len + 1.2f + 1.0f + 0.5f) : FMath::Max(17.f, ArenaCore::RespawnSeconds(Level) + 4.f));
}

void AArenaCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDead)
	{
		// the body turns the rest of the way to its killer, lies still, then fades (or sinks, or vanishes in a burst)
		if (RagdollAt > 0.f && GetWorld()->GetTimeSeconds() >= RagdollAt) { RagdollAt = -1.f; StartRagdoll(); }
		if (DeathYawOffset != 0.f && !bRagdoll)
		{
			DeathYawOffset = FMath::Abs(DeathYawOffset) < 0.5f ? 0.f : FMath::FInterpTo(DeathYawOffset, 0.f, DeltaSeconds, 14.f);
			GetMesh()->SetRelativeRotation(FRotator(0.f, Def.MeshYaw + DeathYawOffset, 0.f));
		}
		const float DeadNow = GetWorld()->GetTimeSeconds();
		if (FadeAt > 0.f && DeadNow >= FadeAt)
		{
			if (FadeProgress <= 0.f)
			{
				for (const int32 Slot : HideOnFade)
				{
					for (int32 L = 0; L < GetMesh()->GetNumLODs(); ++L) { GetMesh()->ShowMaterialSection(Slot, INDEX_NONE, false, L); }
				}
			}
			FadeProgress = FMath::Clamp((DeadNow - FadeAt) / FMath::Max(0.05f, FadeLen), 0.001f, 1.f);
			// eased at both ends: no visible start or snap at the end
			const float Shown = FMath::SmoothStep(0.f, 1.f, FadeProgress);
			for (UMaterialInstanceDynamic* MID : TintMaterials) { if (MID) { MID->SetScalarParameterValue(TEXT("ArenaFade"), Shown); } }
			if (FadeProgress >= 1.f) { SetActorHiddenInGame(true); FadeAt = -1.f; }
		}
		if (VanishAt > 0.f && GetWorld()->GetTimeSeconds() >= VanishAt)
		{
			ArenaFx::Spawn(this, Def.DeathVanishFx, GetMesh()->GetBoneLocation(TEXT("pelvis")), FLinearColor::White);
			SetActorHiddenInGame(true);
			VanishAt = -1.f;
		}
		const float T = SinkAt > 0.f ? (GetWorld()->GetTimeSeconds() - SinkAt) / 1.5f : -1.f;
		if (T >= 1.f) { SetActorHiddenInGame(true); SinkAt = -1.f; }
		else if (T > 0.f && bRagdoll)
		{
			// the physics bodies let go of the ground and drift down (~1.3 m in the 1.5 s)
			if (!bRagdollSinking)
			{
				bRagdollSinking = true;
				GetMesh()->SetCollisionResponseToAllChannels(ECR_Ignore);
				GetMesh()->SetEnableGravity(false);
				GetMesh()->SetAllPhysicsLinearVelocity(FVector(0.f, 0.f, -90.f));
			}
		}
		else if (T > 0.f) { GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, Def.MeshZ - 130.f * T)); }
		return;
	}
	// turning smoothness of the AI-driven units (the player's hero follows the mouse and may flick)
	if (!bAssistedAim && GetController())
	{
		const float Yaw = GetActorRotation().Yaw;
		if (bYawSeen && !IsDashing() && GetWorld()->GetTimeSeconds() >= HoldFacingUntil)
		{
			++YawFrames;
			YawSnaps += FMath::Abs(FRotator::NormalizeAxis(Yaw - LastYaw)) > FMath::Max(25.f, 1150.f * DeltaSeconds) ? 1 : 0;   // a cast's fast turn (1080 deg/s) is not a snap at a low frame rate
		}
		LastYaw = Yaw;
		bYawSeen = true;
	}
	if (AuraDecal && AuraDecal->IsVisible() && GetWorld()->GetTimeSeconds() >= AuraUntil) { AuraDecal->SetVisibility(false); }
	const bool bServer = HasAuthority();
	if (bServer)
	{
		TickDash(DeltaSeconds);
		TickSustain(GetWorld()->GetTimeSeconds(), DeltaSeconds);
		SlideOffCharacters();
		if (GetNetMode() != NM_Standalone && GetWorld()->GetTimeSeconds() >= NextNetWrite) { NextNetWrite = GetWorld()->GetTimeSeconds() + 0.1f; WriteNetState(); }
	}
	TickReactions(GetWorld()->GetTimeSeconds());
	if (!bMinion && !IsStructure()) { UpdateFacing(GetWorld()->GetTimeSeconds(), DeltaSeconds); TickRelax(GetWorld()->GetTimeSeconds()); }
	UpdateMoveSpeed();
	static const bool bAnimDebug = FParse::Param(FCommandLine::Get(), TEXT("ArenaAnimDebug"));
	if (bAnimDebug && GetWorld()->GetTimeSeconds() >= NextAnimDebug) { NextAnimDebug = GetWorld()->GetTimeSeconds() + 4.f; DebugDumpAnim(); }
	const float Now = GetWorld()->GetTimeSeconds();
	// regen (out of combat health regen is doubled)
	if (!bServer)
	{
		// a LAN client: the hit flash and the own camera, nothing that changes the unit
		const float CFlash = GetWorld()->GetTimeSeconds() < HitFlashUntil ? 1.f : 0.f;
		if (CFlash != AppliedFlash) { AppliedFlash = CFlash; for (UMaterialInstanceDynamic* MID : TintMaterials) { if (MID) { MID->SetScalarParameterValue(TEXT("EmissivePower"), CFlash * 25.f); } } }
		TickHitFlash(GetWorld()->GetTimeSeconds());
		return;
	}
	const float L1 = static_cast<float>(Level - 1);
	const float ManaBoost = HasCampBuff(1) ? 1.f + FArenaDatabase::Get().Rules.Conquest.BlackManaRegen : 1.f;
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetManaAttribute(), FMath::Min(GetMaxMana(), GetMana() + (Def.ManaRegen + Def.ManaRegenPerLevel * L1 + ItemStats.ManaRegen) * ManaBoost * DeltaSeconds));
	float HpRegen = (Def.HealthRegen + Def.HealthRegenPerLevel * L1 + ItemStats.HealthRegen) * (Now - LastDamageTime > 5.f ? 2.f : 1.f);
	// items: Serce olbrzyma — out of combat the body mends a share of its maximum health every second
	if (ItemPassives.RegenPct > 0.f && Now - LastDamageTime > ItemPassives.RegenAfter) { HpRegen += GetMaxHealth() * ItemPassives.RegenPct; }
	ASC->SetNumericAttributeBase(UArenaAttributeSet::GetHealthAttribute(), FMath::Min(GetMaxHealth(), GetHealth() + HpRegen * DeltaSeconds));
	// shields decay slowly so fights stay decisive
	if (GetShield() > 0.f) { ASC->SetNumericAttributeBase(UArenaAttributeSet::GetShieldAttribute(), FMath::Max(0.f, GetShield() - 8.f * DeltaSeconds)); }
	// hit flash on the tint (materials that have the parameter) and on the overlay (all of them)
	if (PlayerIndex >= 0 && !bMinion && Ranks[4] > 0)
	{
		const bool bUltCooling = CooldownRemaining(4) > 0.f;
		if (bWasUltCooling && !bUltCooling) { Voice(TEXT("Ability_Ultimate_Ready"), 0.5f); }
		bWasUltCooling = bUltCooling;
	}
	const float Flash = Now < HitFlashUntil ? 1.f : 0.f;
	if (Flash != AppliedFlash)
	{
		AppliedFlash = Flash;
		for (UMaterialInstanceDynamic* MID : TintMaterials) { if (MID) { MID->SetScalarParameterValue(TEXT("EmissivePower"), Flash * 25.f); } }
	}
	TickHitFlash(Now);
	// camera shake (player only)
	if (PlayerIndex >= 0)
	{
		// shake: smooth noise (a random jump every frame read as jitter, and at 140 fps as a blur), fading out
		const float Remaining = ShakeUntil - Now;
		if (Remaining > 0.f)
		{
			const float Tn = Now * 22.f;
			const FVector Noise(FMath::PerlinNoise1D(Tn + 11.3f), FMath::PerlinNoise1D(Tn + 47.9f), FMath::PerlinNoise1D(Tn + 83.1f));
			SpringArm->SocketOffset = BaseSocketOffset + Noise * 1.6f * ShakeStrength * FMath::Clamp(Remaining * 4.f, 0.f, 1.f);
		}
		else { SpringArm->SocketOffset = BaseSocketOffset; ShakeStrength = 0.f; }
		// a dash widens the view a little (speed), eased in and out
		const float WantFov = BaseFov + (IsDashing() ? 6.f : 0.f);
		if (!FMath::IsNearlyEqual(Camera->FieldOfView, WantFov, 0.01f)) { Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, WantFov, DeltaSeconds, IsDashing() ? 9.f : 4.f)); }
	}
	// kill-Z guard (VR-08)
	if (GetActorLocation().Z < -3000.f) { ReceiveDamage(99999.f, nullptr, false); Die(nullptr); }
}
