#include "Arena/ArenaScatter.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AArenaScatter::AArenaScatter()
{
	Instanced = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instanced"));
	SetRootComponent(Instanced);
	Instanced->SetMobility(EComponentMobility::Static);
	Instanced->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AArenaScatter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

int32 AArenaScatter::Rebuild()
{
	Instanced->SetStaticMesh(Mesh);
	Instanced->EmptyOverrideMaterials();
	if (Material && Mesh) { for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i) { Instanced->SetMaterial(i, Material); } }
	Instanced->ClearInstances();
	if (Mesh && Instances.Num() > 0) { Instanced->AddInstances(Instances, false, true); }
	Instanced->SetCullDistances(0, FMath::Max(0, FMath::RoundToInt(CullDistance)));
	Instanced->SetCastShadow(bShadows);
	// small plants and pebbles stay out of the distance fields and Lumen's scene: thousands of tiny instances cost
	// more there than they add; wind (world position offset) stops far away, where nobody sees it sway
	const bool bSmall = Mesh && Mesh->GetBounds().SphereRadius < 150.f;
	Instanced->bAffectDistanceFieldLighting = !bSmall;
	Instanced->bAffectDynamicIndirectLighting = !bSmall;
	Instanced->WorldPositionOffsetDisableDistance = bSmall ? 3000 : 6000;
	Instanced->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Instanced->SetCollisionProfileName(bCollide ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Instanced->SetCanEverAffectNavigation(bCollide);
	return Instanced->GetInstanceCount();
}
