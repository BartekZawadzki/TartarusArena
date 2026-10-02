#include "Arena/ArenaIndicator.h"
#include "Abilities/ArenaAbility.h"
#include "Heroes/ArenaCharacter.h"
#include "Components/DecalComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

static const TCHAR* IndicatorMaterialPath = TEXT("/Game/Arena/Materials/M_ArenaIndicator.M_ArenaIndicator");

AArenaIndicator::AArenaIndicator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;   // after the controller has updated the aim this frame
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	auto MakeDecal = [this](const TCHAR* Name, int32 Order)
	{
		UDecalComponent* D = CreateDefaultSubobject<UDecalComponent>(Name);
		D->SetupAttachment(RootComponent);
		D->SetUsingAbsoluteLocation(true);
		D->SetUsingAbsoluteRotation(true);
		D->SetUsingAbsoluteScale(true);
		D->SortOrder = Order;
		D->SetFadeScreenSize(0.f);
		D->SetVisibility(false);
		return D;
	};
	Ring = MakeDecal(TEXT("Ring"), 1);
	Area = MakeDecal(TEXT("Area"), 2);
	Lane = MakeDecal(TEXT("Lane"), 3);
	SetActorTickEnabled(false);
}

void AArenaIndicator::Off(UDecalComponent* D) { D->SetVisibility(false); }

void AArenaIndicator::Draw(UDecalComponent* D, EShape Shape, const FVector& Center, float Yaw, float HalfForward, float HalfSide, const FLinearColor& Color, float Opacity, float ConeHalfAngleRad)
{
	if (!BaseMaterial) { BaseMaterial = LoadObject<UMaterialInterface>(nullptr, IndicatorMaterialPath); }
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(D->GetDecalMaterial());
	if (!MID && BaseMaterial)
	{
		MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		D->SetDecalMaterial(MID);
	}
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Shape"), (float)(uint8)Shape);
		MID->SetScalarParameterValue(TEXT("Fill"), 1.f);
		MID->SetScalarParameterValue(TEXT("Angle"), ConeHalfAngleRad);
		MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		// rim thickness about 12 cm whatever the size
		// the rim is ~24 cm wide on the ground whatever the size (the bright band ~8 cm used to vanish at game
		// distance); a lane's far end gets the same width through Aspect
		MID->SetScalarParameterValue(TEXT("Edge"), FMath::Clamp(24.f / FMath::Max(10.f, Shape == EShape::Lane ? HalfSide : HalfForward), 0.01f, 0.45f));
		MID->SetScalarParameterValue(TEXT("Aspect"), Shape == EShape::Lane ? FMath::Clamp(HalfSide / FMath::Max(1.f, HalfForward), 0.01f, 1.f) : 1.f);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
	// the decal projects along its X axis (straight down); Y is sideways and Z the aim direction
	D->SetWorldLocation(Center);
	D->SetWorldRotation(FRotator(-90.f, Yaw, 0.f));
	D->DecalSize = FVector(450.f, HalfSide, HalfForward);
	D->MarkRenderStateDirty();
	D->SetVisibility(true);
}

void AArenaIndicator::Show(AArenaCharacter* InCaster, int32 InSlot)
{
	if (!InCaster || !InCaster->GetDef().Abilities.IsValidIndex(InSlot)) { Hide(); return; }
	Caster = InCaster;
	Slot = InSlot;
	Ab = InCaster->Ability(InSlot);   // at its rank
	SetActorTickEnabled(true);
	UpdateShapes();
}

void AArenaIndicator::Hide()
{
	Slot = -1;
	Caster = nullptr;
	Off(Ring); Off(Area); Off(Lane);
	RingRadius = AreaRadius = LaneLength = LaneWidth = 0.f;
	SetActorTickEnabled(false);
}

void AArenaIndicator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Slot >= 0) { Ab = Caster.IsValid() ? Caster->Ability(Slot) : Ab; UpdateShapes(); }
	if (FillLen > 0.f)
	{
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Area->GetDecalMaterial())) { MID->SetScalarParameterValue(TEXT("Fill"), TelegraphFill()); }
	}
}

void AArenaIndicator::UpdateShapes()
{
	AArenaCharacter* C = Caster.Get();
	if (!C || !C->IsAlive()) { Hide(); return; }
	const float Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = C->GetActorLocation() - FVector(0.f, 0.f, Half);
	// with a picked target (the player's aim assistance) the cone / lane already turns to where the attack will go
	FVector AimAt = C->AimPoint;
	if (C->bAssistedAim && C->AimTarget.IsValid() && (Ab.Archetype == EArenaArchetype::Melee || (Ab.Archetype == EArenaArchetype::Projectile && Ab.Count <= 1)))
	{
		AimAt = ArenaAbilityHelpers::LeadPoint(C, Ab, C->AimTarget.Get());
	}
	const FVector Dir = ArenaAbilityHelpers::AimDirection(C, AimAt);
	const float Yaw = Dir.Rotation().Yaw;
	bReady = C->CanCastSlot(Slot);
	const FLinearColor Base = FArenaDatabase::Hex(Ab.Color);
	const FLinearColor Col = bReady ? FMath::Lerp(Base, FLinearColor::White, 0.25f) : FLinearColor(1.f, 0.18f, 0.12f);
	const FLinearColor RangeCol = bReady ? FLinearColor(0.85f, 0.92f, 1.f) : FLinearColor(1.f, 0.3f, 0.25f);
	RingRadius = AreaRadius = LaneLength = LaneWidth = 0.f;
	Off(Ring); Off(Area); Off(Lane);
	Landing = C->GetActorLocation();

	switch (Ab.Archetype)
	{
	case EArenaArchetype::Melee:
	{
		// cone in front of the caster (a full circle for 360-degree swings)
		AreaRadius = Ab.Range * 100.f;
		Target = C->GetActorLocation() + Dir * AreaRadius;
		if (Ab.Angle >= 300.f) { Draw(Area, EShape::Disc, Feet, Yaw, AreaRadius, AreaRadius, Col, 0.6f); }
		else { Draw(Area, EShape::Cone, Feet, Yaw, AreaRadius, AreaRadius, Col, 0.65f, FMath::DegreesToRadians(Ab.Angle * 0.5f)); }
		break;
	}
	case EArenaArchetype::Projectile:
	{
		const float Range = Ab.Range * 100.f;
		if (Ab.Count > 1)
		{
			// a fan of shots: the cone they cover
			AreaRadius = Range;
			const float HalfAngle = FMath::DegreesToRadians(Ab.Spread * (Ab.Count - 1) * 0.5f + 4.f);
			Draw(Area, EShape::Cone, Feet, Yaw, Range, Range, Col, 0.55f, HalfAngle);
			Target = C->GetActorLocation() + Dir * Range;
			break;
		}
		if (Ab.Gravity > 0.f)
		{
			// a lobbed shot: its path on the ground to the landing point and the blast there (the shot comes down
			// exactly where the circle is)
			const FVector Land = ArenaAbilityHelpers::LobLanding(C, Ab, AimAt);
			LaneLength = FMath::Max(50.f, FVector::Dist2D(Land, C->GetActorLocation()));
			LaneWidth = Ab.Width > 0.f ? Ab.Width * 100.f : 52.f;
			Draw(Lane, EShape::Lane, Feet + Dir * (LaneLength * 0.5f), Yaw, LaneLength * 0.5f, LaneWidth * 0.5f, Col, 0.45f);
			Target = Land;
			AreaRadius = FMath::Max(50.f, Ab.Radius * 100.f);
			Draw(Area, EShape::Disc, Land, Yaw, AreaRadius, AreaRadius, Col, 0.6f);
			break;
		}
		// the lane the shot flies, stopped where a wall stops it; an explosive shot also shows its blast
		LaneLength = ArenaAbilityHelpers::ProjectileReach(C, Dir, Range);
		LaneWidth = Ab.Width > 0.f ? Ab.Width * 100.f : 52.f;   // exactly the shot's width (the projectile's sphere)
		Draw(Lane, EShape::Lane, Feet + Dir * (LaneLength * 0.5f), Yaw, LaneLength * 0.5f, LaneWidth * 0.5f, Col, 0.6f);
		Target = C->GetActorLocation() + Dir * LaneLength;
		if (Ab.Radius > 0.f)
		{
			AreaRadius = Ab.Radius * 100.f;
			Draw(Area, EShape::Disc, Feet + Dir * LaneLength, Yaw, AreaRadius, AreaRadius, Col, 0.45f);
		}
		break;
	}
	case EArenaArchetype::GroundAoE:
	{
		RingRadius = Ab.Range * 100.f;
		Draw(Ring, EShape::Ring, Feet, Yaw, RingRadius, RingRadius, RangeCol, 0.8f);
		Target = ArenaAbilityHelpers::GroundTarget(C, Ab, C->AimPoint);
		AreaRadius = Ab.Radius * 100.f;
		Draw(Area, EShape::Disc, Target, Yaw, AreaRadius, AreaRadius, Col, 0.6f);
		break;
	}
	case EArenaArchetype::Dash:
	{
		// the path to the real landing spot (walls and ledges end the dash, see AArenaCharacter::DashDestination)
		const FVector DashDir = Ab.bBackwards ? -Dir : Dir;
		const FVector End = C->DashDestination(DashDir, Ab.Distance * 100.f);
		LaneLength = FVector::Dist2D(End, C->GetActorLocation());
		LaneWidth = FMath::Max(C->GetCapsuleComponent()->GetScaledCapsuleRadius(), Ab.Radius * 100.f) * 2.f;
		const FVector EndFeet = End - FVector(0.f, 0.f, Half);
		Draw(Lane, EShape::Lane, (Feet + EndFeet) * 0.5f, DashDir.Rotation().Yaw, FMath::Max(20.f, LaneLength * 0.5f), LaneWidth * 0.5f, Col, 0.6f);
		Landing = End;
		Target = C->GetActorLocation() + Dir * FMath::Max(100.f, LaneLength);   // the aim (a backwards leap aims forward)
		if (Ab.EndRadius > 0.f)
		{
			AreaRadius = Ab.EndRadius * 100.f;
			Draw(Area, EShape::Disc, EndFeet, Yaw, AreaRadius, AreaRadius, Col, 0.45f);
		}
		break;
	}
	case EArenaArchetype::Buff:
	{
		if (Ab.BuffTarget == EArenaBuffTarget::LowestAlly)
		{
			RingRadius = Ab.Range * 100.f;
			Draw(Ring, EShape::Ring, Feet, Yaw, RingRadius, RingRadius, RangeCol, 0.8f);
			AArenaCharacter* Ally = ArenaAbilityHelpers::LowestAlly(C, Ab);
			const FVector At = Ally ? Ally->GetActorLocation() - FVector(0.f, 0.f, Ally->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) : Feet;
			AreaRadius = 110.f;
			Draw(Area, EShape::Disc, At, Yaw, AreaRadius, AreaRadius, Col, 0.65f);
			Target = Ally ? Ally->GetActorLocation() : C->GetActorLocation();
		}
		else
		{
			// self: a ring around the caster; allies in a radius: the covered disc
			AreaRadius = Ab.BuffTarget == EArenaBuffTarget::AlliesInRadius ? Ab.Radius * 100.f : 160.f;
			Draw(Area, Ab.BuffTarget == EArenaBuffTarget::AlliesInRadius ? EShape::Disc : EShape::Ring, Feet, Yaw, AreaRadius, AreaRadius, Col, 0.7f);
			Target = C->GetActorLocation();
		}
		break;
	}
	}
}

AArenaIndicator* AArenaIndicator::FlashCone(UWorld* World, const FVector& Apex, float Yaw, float RadiusCm, float HalfAngleRad, const FLinearColor& Color, float Seconds)
{
	if (!World) { return nullptr; }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArenaIndicator* I = World->SpawnActor<AArenaIndicator>(AArenaIndicator::StaticClass(), Apex, FRotator::ZeroRotator, P);
	if (!I) { return nullptr; }
	I->AreaRadius = RadiusCm;
	if (HalfAngleRad >= PI - 0.01f) { I->Draw(I->Area, EShape::Disc, Apex, Yaw, RadiusCm, RadiusCm, Color, 0.5f); }
	else { I->Draw(I->Area, EShape::Cone, Apex, Yaw, RadiusCm, RadiusCm, Color, 0.5f, HalfAngleRad); }
	I->SetLifeSpan(FMath::Max(0.05f, Seconds));
	return I;
}

AArenaIndicator* AArenaIndicator::Telegraph(UWorld* World, const FVector& Center, float RadiusCm, const FLinearColor& Color, float Seconds, float FillSeconds)
{
	if (!World) { return nullptr; }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AArenaIndicator* I = World->SpawnActor<AArenaIndicator>(AArenaIndicator::StaticClass(), Center, FRotator::ZeroRotator, P);
	if (!I) { return nullptr; }
	I->AreaRadius = RadiusCm;
	I->Draw(I->Area, EShape::Disc, Center, 0.f, RadiusCm, RadiusCm, Color, 0.6f);
	if (FillSeconds > 0.f)
	{
		I->FillStart = World->GetTimeSeconds();
		I->FillLen = FillSeconds;
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(I->Area->GetDecalMaterial())) { MID->SetScalarParameterValue(TEXT("Fill"), 0.f); }
		I->SetActorTickEnabled(true);
	}
	I->SetLifeSpan(FMath::Max(0.05f, Seconds));
	return I;
}
