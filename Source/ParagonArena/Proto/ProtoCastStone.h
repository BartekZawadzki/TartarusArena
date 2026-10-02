// Prototype 2 (the Hades controls): the Cast — a stone thrown at the cursor (RMB, as Hades' Bloodstone). It flies
// straight, hits the first enemy (damage and a slow) or stops on a wall, and the thrower gets it back a few seconds
// later. A grey sphere (greybox).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProtoCastStone.generated.h"

class AProtoCharacter;
class UStaticMeshComponent;

UCLASS()
class PARAGONARENA_API AProtoCastStone : public AActor
{
	GENERATED_BODY()

public:
	AProtoCastStone();
	virtual void Tick(float DeltaSeconds) override;
	void Launch(AProtoCharacter* InThrower, const FVector& Direction);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;

private:
	TWeakObjectPtr<AProtoCharacter> Thrower;
	FVector Velocity = FVector::ZeroVector;
	float Travelled = 0.f;
	bool bDone = false;
};
