#include "Proto/ProtoCastStone.h"
#include "Proto/ProtoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AProtoCastStone::AProtoCastStone()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);
	Mesh->SetWorldScale3D(FVector(0.32f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) { Mesh->SetStaticMesh(Sphere.Object); }
	InitialLifeSpan = 3.f;
}

void AProtoCastStone::Launch(AProtoCharacter* InThrower, const FVector& Direction)
{
	Thrower = InThrower;
	Velocity = Direction.GetSafeNormal2D() * AProtoCharacter::Tuning().CastSpeed;
}

void AProtoCastStone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDone) { return; }
	AProtoCharacter* By = Thrower.Get();
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;
	// the flight: a small sphere swept along; the first enemy or wall stops it
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ProtoCast), false, this);
	if (By) { Q.AddIgnoredActor(By); }
	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(22.f), Q);
	if (bHit)
	{
		SetActorLocation(Hit.Location);
		bDone = true;
		if (AProtoCharacter* C = Cast<AProtoCharacter>(Hit.GetActor()); C && By && C->Team != By->Team && C->IsAlive())
		{
			By->CastLanded(C);
		}
		SetLifeSpan(0.6f);   // lodged a moment, then gone (the thrower gets its stone back on a timer)
		return;
	}
	SetActorLocation(To);
	Travelled += (To - From).Size();
	if (Travelled >= AProtoCharacter::Tuning().CastRange) { bDone = true; SetLifeSpan(0.3f); }
}
