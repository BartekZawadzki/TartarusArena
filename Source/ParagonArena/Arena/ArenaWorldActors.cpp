#include "Arena/ArenaWorldActors.h"
#include "Arena/ArenaFx.h"
#include "Heroes/ArenaCharacter.h"
#include "Game/ArenaGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Obstacle.h"

static void ArenaColor(UStaticMeshComponent* C, const FLinearColor& Color)
{
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, C);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, MID);
	}
}

// ---- jump pad ---------------------------------------------------------------------------------
AArenaJumpPad::AArenaJumpPad()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));   // children keep their own scale
	Pad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pad"));
	Pad->SetupAttachment(RootComponent);
	Pad->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Pad->SetRelativeScale3D(FVector(2.2f, 2.2f, 0.12f));
	Pad->SetCollisionProfileName(TEXT("BlockAll"));
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetBoxExtent(FVector(110.f, 110.f, 80.f));
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	Light->SetIntensity(9000.f);
	Light->SetAttenuationRadius(600.f);
	Light->SetLightColor(FLinearColor(0.3f, 0.9f, 1.f));
	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->SetAreaClass(UNavArea_Obstacle::StaticClass());
}

void AArenaJumpPad::BeginPlay()
{
	Super::BeginPlay();
	ArenaColor(Pad, FLinearColor(0.1f, 0.7f, 1.f));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AArenaJumpPad::OnOverlap);
}

void AArenaJumpPad::OnOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	AArenaCharacter* C = Cast<AArenaCharacter>(Other);
	if (!C || !C->IsAlive() || C->IsMinion()) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	float& Last = LastLaunch.FindOrAdd(C);
	if (Now - Last < 1.f) { return; }
	Last = Now;
	C->LaunchCharacter(GetActorRotation().RotateVector(LaunchVelocity), true, true);
	ArenaFx::Spawn(this, TEXT("/Game/Variant_Platforming/VFX/NS_Jump_Trail.NS_Jump_Trail"), C->GetActorLocation(), FLinearColor(0.3f, 0.9f, 1.f));
}

// ---- power orb --------------------------------------------------------------------------------
AArenaPowerOrb::AArenaPowerOrb()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;   // LAN: the guests see it too (it bobs on each machine; taking it is the server's)
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(160.f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetCanEverAffectNavigation(false);   // an overlap sphere must not carve a hole in the navmesh under the altar
	SetRootComponent(Trigger);
	Orb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Orb"));
	Orb->SetupAttachment(Trigger);
	Orb->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Orb->SetRelativeScale3D(FVector(0.9f));
	Orb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Orb->SetCanEverAffectNavigation(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Trigger);
	Light->SetIntensity(40000.f);
	Light->SetAttenuationRadius(1400.f);
	Light->SetLightColor(FLinearColor(1.f, 0.35f, 0.05f));
}

void AArenaPowerOrb::BeginPlay()
{
	Super::BeginPlay();
	Base = GetActorLocation();
	ArenaColor(Orb, FLinearColor(1.f, 0.3f, 0.02f));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AArenaPowerOrb::OnOverlap);
}

void AArenaPowerOrb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds();
	SetActorLocation(Base + FVector(0.f, 0.f, 20.f * FMath::Sin(T * 2.5f)));
	Orb->AddLocalRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));
	Light->SetIntensity(30000.f + 15000.f * FMath::Sin(T * 5.f));
}

void AArenaPowerOrb::OnOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	AArenaCharacter* C = Cast<AArenaCharacter>(Other);
	if (!HasAuthority() || !C || !C->IsAlive() || C->IsMinion()) { return; }
	C->ApplyPowerBuff(1.25f, 0.2f, 30.f);
	ArenaFx::Spawn(this, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"), GetActorLocation(), FLinearColor(1.f, 0.4f, 0.05f), 3.f);
	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->Announce(this, FString::Printf(TEXT("%s seizes the POWER OF TARTARUS!"), *C->GetDef().DisplayName), true, FLinearColor(1.f, 0.45f, 0.1f));
		GM->OnOrbTaken(C);
	}
	Destroy();
}
