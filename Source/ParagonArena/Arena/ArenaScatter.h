// Dressing in bulk: one mesh, many transforms, drawn as hierarchical instances (grass, ferns, rocks, trees).
// Filled by Tools/build_arena.py; purely visual unless bCollide (then it blocks like the mesh's own collision).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaScatter.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class PARAGONARENA_API AArenaScatter : public AActor
{
	GENERATED_BODY()

public:
	AArenaScatter();
	virtual void OnConstruction(const FTransform& Transform) override;
	/** Rebuilds the instances from Mesh/Instances/Material (called by the map build script). Returns the instance count. */
	UFUNCTION(BlueprintCallable, Category = "Scatter") int32 Rebuild();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TArray<FTransform> Instances;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") TObjectPtr<UMaterialInterface> Material;   // optional override, all slots
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") float CullDistance = 0.f;   // 0 = never cull
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") bool bShadows = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter") bool bCollide = false;

	UPROPERTY(VisibleAnywhere, Category = "Scatter") TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instanced;
};
