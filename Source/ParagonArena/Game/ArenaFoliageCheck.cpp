// -ArenaFoliageCheck: every plant, stone and tree instance of the play area is checked against the ground under it.
//   floating  the instance's base is more than 6 cm above the first surface below it (hangs in the air)
//   buried    its base is inside blocking geometry (a ramp, a wall, a terrain block): it pokes out of a slope or wall
// A line trace down from 40 cm above the base finds the surface; a 5 cm sphere 12-22 cm above the base finds the inside of
// geometry. Floor tiles and the pitched balustrade caps are left out (they are laid on surfaces by construction).
// Prints FOLIAGE lines per mesh, the worst cases, and FOLIAGE_SUMMARY floating=N buried=M checked=K; then quits.
#include "Game/ArenaGameMode.h"
#include "Arena/ArenaScatter.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	struct FFoliageIssue { FString Mesh; FVector At = FVector::ZeroVector; float Gap = 0.f; bool bBuried = false; };
}

void AArenaGameMode::RunFoliageCheck()
{
	UWorld* W = GetWorld();
	TArray<AActor*> Scatters;
	for (TActorIterator<AArenaScatter> It(W); It; ++It) { Scatters.Add(*It); }
	FCollisionQueryParams Q(SCENE_QUERY_STAT(FoliageCheck), false);
	Q.AddIgnoredActors(Scatters);
	FCollisionObjectQueryParams Obj(ECC_WorldStatic);
	TArray<FFoliageIssue> Issues;
	int32 Checked = 0, Floating = 0, Buried = 0;
	for (AActor* A : Scatters)
	{
		const AArenaScatter* Sc = Cast<AArenaScatter>(A);
		if (!Sc || !Sc->Mesh) { continue; }
		const FString Name = Sc->Mesh->GetName();
		if (Name.Contains(TEXT("Plane")) || Name.Contains(TEXT("Trim"))) { continue; }
		const FBox B = Sc->Mesh->GetBoundingBox();
		int32 F = 0, U = 0, N = 0;
		for (const FTransform& T : Sc->Instances)
		{
			const FVector L = T.GetLocation();
			if (FMath::Abs(L.X) > 6980.f || FMath::Abs(L.Y) > 4590.f) { continue; }        // the play area (collision everywhere)
			if (FMath::Abs(T.GetRotation().Rotator().Pitch) > 1.f) { continue; }
			const FVector Base(L.X, L.Y, L.Z + B.Min.Z * T.GetScale3D().Z);
			++N;
			FHitResult Hit;
			const bool bGround = W->LineTraceSingleByObjectType(Hit, Base + FVector(0.f, 0.f, 40.f), Base - FVector(0.f, 0.f, 400.f), Obj, Q);
			const float Gap = bGround ? Base.Z - Hit.ImpactPoint.Z : 999.f;
			// 12 cm up, 5 cm round: clear of the ground a plant stands on, inside anything it is stuck in
			const bool bInside = W->OverlapAnyTestByObjectType(Base + FVector(0.f, 0.f, 17.f), FQuat::Identity, Obj, FCollisionShape::MakeSphere(5.f), Q);
			if (Gap > 6.f) { ++F; Issues.Add({ Name, Base, Gap, false }); }
			else if (bInside && !Sc->bCollide) { ++U; Issues.Add({ Name, Base, Gap, true }); }   // rocks and trees are sunk into the ground on purpose
		}
		Checked += N; Floating += F; Buried += U;
		if (N > 0) { UE_LOG(LogArena, Display, TEXT("FOLIAGE mesh=%s checked=%d floating=%d buried=%d"), *Name, N, F, U); }
	}
	Issues.Sort([](const FFoliageIssue& X, const FFoliageIssue& Y) { return X.Gap > Y.Gap; });
	for (int32 i = 0; i < Issues.Num() && i < 60; ++i)
	{
		const FFoliageIssue& I = Issues[i];
		UE_LOG(LogArena, Display, TEXT("FOLIAGE issue %s mesh=%s at=(%.0f, %.0f, %.0f) gap=%.0f"), I.bBuried ? TEXT("buried") : TEXT("floating"), *I.Mesh, I.At.X, I.At.Y, I.At.Z, I.Gap);
	}
	UE_LOG(LogArena, Display, TEXT("FOLIAGE_SUMMARY floating=%d buried=%d checked=%d"), Floating, Buried, Checked);
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}
