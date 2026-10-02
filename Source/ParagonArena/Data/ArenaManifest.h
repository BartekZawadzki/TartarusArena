// Hard references to every asset heroes.json names (meshes, anim blueprints, animations, FX, sounds).
// Soft paths in JSON are invisible to the cooker; this asset (built by Tools/build_manifest.py, cooked
// via DirectoriesToAlwaysCook=/Game/Arena) makes the packaged game carry exactly what the data uses —
// not the whole multi-GB Paragon packs.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ArenaManifest.generated.h"

UCLASS(BlueprintType)
class PARAGONARENA_API UArenaAssetManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena") TArray<TObjectPtr<UObject>> Assets;
};
