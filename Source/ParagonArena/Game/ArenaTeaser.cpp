#include "Game/ArenaTeaser.h"
#include "Heroes/ArenaCharacter.h"
#include "Proto/ProtoCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CoreGlobals.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogTeaser, Log, All);

namespace
{
	int32 GTeaserFrames = 0;
	double GTeaserReal0 = 0.0;

	void TeaserFile(const FString& Line)
	{
		// a Shipping build writes no log: the director's own lines go to Saved/Teaser.txt
		FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *(FPaths::ProjectSavedDir() / TEXT("Teaser.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}

	bool IsHero(const AArenaCharacter* C) { return C && C->IsAlive() && !C->IsMinion() && !C->IsStructure() && C->GetTeam() >= 0 && C->GetTeam() <= 1; }

	/** The fight around a hero: the middle of the heroes (both sides) within 14 m of it. */
	FVector ClusterAround(UWorld* W, const AActor* Star)
	{
		FVector Sum = Star->GetActorLocation();
		int32 N = 1;
		for (TActorIterator<AArenaCharacter> It(W); It; ++It)
		{
			if (*It != Star && IsHero(*It) && FVector::Dist2D(It->GetActorLocation(), Star->GetActorLocation()) < 1400.f) { Sum += It->GetActorLocation(); ++N; }
		}
		return Sum / N;
	}

	/** The living hero nearest a spot (the film stays on its fight when its star falls). */
	AActor* NearestHero(UWorld* W, const FVector& At)
	{
		AActor* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		for (TActorIterator<AArenaCharacter> It(W); It; ++It)
		{
			const float D = FVector::Dist2D(It->GetActorLocation(), At);
			if (IsHero(*It) && D < BestD) { BestD = D; Best = *It; }
		}
		return Best;
	}

	/** The side to film a hero from: a clear line to the lens over the next 40 degrees of the orbit, the face preferred. */
	float ClearAngle(UWorld* W, const AActor* Star, float Dist, float Height)
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(TeaserSide), false);
		for (TActorIterator<AArenaCharacter> It(W); It; ++It) { Q.AddIgnoredActor(*It); }
		const FVector Eye = Star->GetActorLocation() + FVector(0.f, 0.f, 100.f);
		const float Yaw = Star->GetActorRotation().Yaw;
		float Best = Yaw + 35.f, BestScore = -100.f;
		for (int32 i = 0; i < 24; ++i)
		{
			const float A = Yaw + i * 15.f;
			float Clear = 0.f;
			for (int32 k = 0; k < 5; ++k)
			{
				const FVector Cam = Star->GetActorLocation() + FRotator(0.f, A + k * 10.f, 0.f).Vector() * Dist + FVector(0.f, 0.f, Height);
				FHitResult H;
				Clear += W->LineTraceSingleByChannel(H, Eye, Cam, ECC_Visibility, Q) ? H.Time : 1.f;
			}
			const float Score = Clear / 5.f + 0.35f * FMath::Cos(FMath::DegreesToRadians(A - Yaw));
			if (Score > BestScore) { BestScore = Score; Best = A; }
		}
		return Best;
	}

	void SetScreenPercentage(float Pct)
	{
		if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))) { CV->Set(Pct, ECVF_SetByCode); }
	}
}

AArenaTeaserDirector::AArenaTeaserDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void AArenaTeaserDirector::MaybeStart(UWorld* World)
{
	FString Scene;
	if (!World || !FParse::Value(FCommandLine::Get(), TEXT("ArenaTeaser="), Scene)) { return; }
	// the startup map travels on to -ArenaStartMap: the director starts there
	FString StartMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("ArenaStartMap="), StartMap) && !World->GetMapName().Contains(StartMap)) { return; }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AArenaTeaserDirector* D = World->SpawnActor<AArenaTeaserDirector>(AArenaTeaserDirector::StaticClass(), FTransform::Identity, P))
	{
		D->Scene = Scene.ToLower();
		FParse::Value(FCommandLine::Get(), TEXT("TeaserSeconds="), D->Seconds);
		FParse::Value(FCommandLine::Get(), TEXT("TeaserPreroll="), D->Preroll);
		FParse::Value(FCommandLine::Get(), TEXT("TeaserWait="), D->MaxWait);
		FParse::Value(FCommandLine::Get(), TEXT("TeaserOrbit="), D->OrbitR);
		FParse::Value(FCommandLine::Get(), TEXT("TeaserHeight="), D->OrbitH);
		if (FParse::Value(FCommandLine::Get(), TEXT("TeaserSlomo="), D->Slomo)) { D->Slomo = FMath::Clamp(D->Slomo, 0.1f, 1.f); }
		// the film's clock: a fixed 1/30 s per frame however slowly a frame renders and saves (-benchmark alone
		// did not hold in the packaged game: 51 frames for a 12 s clip)
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 30.0);
		UE_LOG(LogTeaser, Display, TEXT("TEASER evt=director scene=%s seconds=%.0f preroll=%.0f"), *D->Scene, D->Seconds, D->Preroll);
		TeaserFile(FString::Printf(TEXT("TEASER_START %s scene=%s map=%s seconds=%.0f"), *FDateTime::Now().ToString(), *D->Scene, *World->GetMapName(), D->Seconds));
	}
}

bool AArenaTeaserDirector::HotSpot(FVector& Out, int32& Heat, AActor*& Star) const
{
	Heat = 0;
	Star = nullptr;
	// the prototype's fighters
	TArray<AActor*> Fighters;
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It) { if (It->IsAlive()) { Fighters.Add(*It); } }
	if (Fighters.Num() >= 2)
	{
		FVector Sum = FVector::ZeroVector;
		for (AActor* A : Fighters) { Sum += A->GetActorLocation(); }
		Out = Sum / Fighters.Num();
		for (AActor* A : Fighters) { if (FVector::Dist2D(A->GetActorLocation(), Out) < 900.f) { ++Heat; } }
		Star = Fighters[0];
		return true;
	}
	// the arena's heroes: the one with the most heroes of both sides around it
	TArray<AArenaCharacter*> Heroes;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (C->IsAlive() && !C->IsMinion() && !C->IsStructure() && C->GetTeam() >= 0 && C->GetTeam() <= 1) { Heroes.Add(C); }
	}
	int32 BestScore = -1;
	for (AArenaCharacter* H : Heroes)
	{
		int32 Enemies = 0, Allies = 0;
		for (AArenaCharacter* O : Heroes)
		{
			if (O == H || FVector::Dist2D(O->GetActorLocation(), H->GetActorLocation()) > 1400.f) { continue; }
			(O->GetTeam() == H->GetTeam() ? Allies : Enemies) += 1;
		}
		if (Enemies == 0) { continue; }
		const int32 Score = FMath::Min(Enemies, Allies + 1) * 2 + Enemies + Allies;
		if (Score > BestScore)
		{
			BestScore = Score;
			Heat = Enemies + Allies + 1;
			Star = H;
			FVector Sum = FVector::ZeroVector;
			int32 N = 0;
			for (AArenaCharacter* O : Heroes) { if (FVector::Dist2D(O->GetActorLocation(), H->GetActorLocation()) <= 1400.f) { Sum += O->GetActorLocation(); ++N; } }
			Out = Sum / FMath::Max(1, N);
		}
	}
	return Star != nullptr;
}

bool AArenaTeaserDirector::StructureUnderAttack(FVector& Out, int32& Heat) const
{
	Heat = 0;
	TArray<AArenaCharacter*> Units, Structures;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive()) { continue; }
		(It->IsStructure() ? Structures : Units).Add(*It);
	}
	for (AArenaCharacter* S : Structures)
	{
		int32 N = 0;
		for (AArenaCharacter* U : Units) { if (U->GetTeam() != S->GetTeam() && U->GetTeam() <= 1 && FVector::Dist2D(U->GetActorLocation(), S->GetActorLocation()) < 1500.f) { ++N; } }
		if (N > Heat) { Heat = N; Out = S->GetActorLocation(); }
	}
	return Heat > 0;
}

void AArenaTeaserDirector::Aim(const FVector& Loc, const FVector& LookAt, float Fov, float Dt, float Lag, bool bSnap)
{
	// keep the lens out of the walls: pull in along the line from the subject
	FVector Want = Loc;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(TeaserCam), false);
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { Q.AddIgnoredActor(*It); }
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It) { Q.AddIgnoredActor(*It); }
	// (the trace starts clear of the subject: a tower's own mesh is not a wall; never nearer than that start)
	const float Skip = Scene == TEXT("tower") ? 500.f : 150.f;
	const FVector Dir = (Loc - LookAt).GetSafeNormal();
	if (Scene != TEXT("aerial") && Scene != TEXT("hades") && FVector::Dist(Loc, LookAt) > Skip + 50.f && GetWorld()->LineTraceSingleByChannel(Hit, LookAt + Dir * Skip, Loc, ECC_Visibility, Q))
	{
		Want = Hit.Location - Dir * 60.f;
	}
	if (bSnap || CamLoc.IsZero()) { CamLoc = Want; }
	else { CamLoc = FMath::VInterpTo(CamLoc, Want, Dt, Lag); }
	const FRotator WantRot = (LookAt - CamLoc).Rotation();
	CamRot = (bSnap || CamRot.IsZero()) ? WantRot : FMath::RInterpTo(CamRot, WantRot, Dt, Lag * 1.4f);
	Cam->SetActorLocationAndRotation(CamLoc, CamRot);
	Cam->GetCameraComponent()->SetFieldOfView(Fov);
}

void AArenaTeaserDirector::UpdateCutaway(const FVector& From, const FVector& To)
{
	TSet<TWeakObjectPtr<AActor>> Now;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(TeaserCut), false);
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It; ++It) { Q.AddIgnoredActor(*It); }
	for (int32 i = 0; i < 8; ++i)
	{
		FHitResult H;
		if (!GetWorld()->SweepSingleByChannel(H, From, To, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(60.f), Q)) { break; }
		AActor* A = H.GetActor();
		if (!A) { break; }
		Q.AddIgnoredActor(A);
		if (A->IsA<AStaticMeshActor>() && A->GetComponentsBoundingBox().Max.Z > To.Z + 40.f) { Now.Add(A); }
	}
	for (const TWeakObjectPtr<AActor>& A : CutAway) { if (A.IsValid() && !Now.Contains(A)) { A->SetActorHiddenInGame(false); } }
	for (const TWeakObjectPtr<AActor>& A : Now) { if (A.IsValid()) { A->SetActorHiddenInGame(true); } }
	CutAway = MoveTemp(Now);
}

void AArenaTeaserDirector::SetupStage()
{
	// the prototype scenes: the two fighters face each other on a good street (the town around them)
	bPlaced = true;
	const FVector Spot = Scene == TEXT("hades") ? FVector(-3900.f, 0.f, 110.f) : FVector(-1600.f, 0.f, 110.f);
	int32 i = 0;
	for (TActorIterator<AProtoCharacter> It(GetWorld()); It && i < 2; ++It, ++i)
	{
		const FVector At = Spot + FVector(0.f, i == 0 ? -260.f : 260.f, 0.f);
		It->GetCharacterMovement()->StopMovementImmediately();
		It->SetActorLocationAndRotation(At, FRotator(0.f, i == 0 ? 90.f : -90.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		It->bAlerted = true;
	}
	UE_LOG(LogTeaser, Display, TEXT("TEASER evt=stage fighters=%d"), i);
}

void AArenaTeaserDirector::StartRecording()
{
	const float Now = GetWorld()->GetTimeSeconds();
	bRecording = true;
	RecStart = Now + 0.15f;   // the full screen percentage first, the frames after
	SetScreenPercentage(100.f);
	if (Slomo < 1.f) { UGameplayStatics::SetGlobalTimeDilation(this, Slomo); }
	if (Scene == TEXT("aerial"))
	{
		// over the whole map: its bounds from the units and structures (the prototype's town: its known box)
		FBox B(ForceInit);
		for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { B += It->GetActorLocation(); }
		if (!B.IsValid || B.GetExtent().Size2D() < 2000.f) { B = FBox(FVector(-10000.f, -6000.f, 0.f), FVector(10000.f, 6000.f, 0.f)); }
		const FVector C = B.GetCenter(), E = B.GetExtent();
		const float H = FMath::Clamp(FMath::Max(E.X, E.Y) * 0.45f, 2500.f, 7000.f);
		AerialA = C + FVector(-E.X * 0.95f, -E.Y * 1.15f, H);
		AerialB = C + FVector(E.X * 0.25f, -E.Y * 0.95f, H * 0.8f);
		AerialLook = C + FVector(0.f, 0.f, 0.f);
	}
	GTeaserFrames = 0;
	GTeaserReal0 = FPlatformTime::Seconds();
	UE_LOG(LogTeaser, Display, TEXT("TEASER evt=record_start t=%.1f scene=%s"), Now, *Scene);
	TeaserFile(FString::Printf(TEXT("REC_START t=%.1f fixed=%d dt=%.4f"), Now, FApp::UseFixedTimeStep() ? 1 : 0, FApp::GetFixedDeltaTime()));
}

void AArenaTeaserDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	// the camera moves at the film's speed even when the game is slowed
	const float Now = W->GetTimeSeconds(), Dt = DeltaSeconds / FMath::Max(0.05f, W->GetWorldSettings()->GetEffectiveTimeDilation());
	if (Born < 0.f)
	{
		Born = Now;
		Cam = W->SpawnActor<ACameraActor>();
		Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
		SetScreenPercentage(30.f);   // the preroll renders cheap
	}
	// the view is the director's, the HUD hidden
	if (APlayerController* PC = W->GetFirstPlayerController())
	{
		if (PC->GetViewTarget() != Cam) { PC->SetViewTarget(Cam); }
		if (PC->MyHUD) { PC->MyHUD->bShowHUD = false; }
	}
	if (!bPlaced && (Scene == TEXT("duel") || Scene == TEXT("hades")) && Now - Born > 0.5f)
	{
		int32 N = 0;
		for (TActorIterator<AProtoCharacter> It(W); It; ++It) { ++N; }
		if (N >= 2) { SetupStage(); }
	}
	FVector Spot = Focus;
	int32 Heat = 0;
	AActor* Star = nullptr;
	const bool bHot = HotSpot(Spot, Heat, Star);
	if (!bRecording)
	{
		const float Age = Now - Born;
		bool bReady = Age >= Preroll;
		if (Scene == TEXT("fight") || Scene == TEXT("follow")) { bReady = bReady && bHot && Heat >= 4; }
		if (Scene == TEXT("hero")) { bReady = bReady && bHot && Heat >= 3; }
		if (Scene == TEXT("tower")) { FVector S; int32 H = 0; bReady = bReady && StructureUnderAttack(S, H) && H >= 3; }
		if (Scene == TEXT("duel") || Scene == TEXT("hades")) { bReady = bReady && bPlaced && Heat >= 2; }
		if (Age >= MaxWait) { bReady = true; }
		if (bReady)
		{
			StartRecording();
			if (Scene == TEXT("follow") || Scene == TEXT("hero") || Scene == TEXT("fight")) { Followed = Star; }
			if (Scene == TEXT("hero") && Star) { Angle = ClearAngle(W, Star, 480.f, 120.f); }
		}
	}
	const bool bFirst = CamLoc.IsZero();
	const float T = bRecording ? FMath::Max(0.f, Now - RecStart) : 0.f;
	if (Scene == TEXT("aerial"))
	{
		if (bRecording)
		{
			const float U = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(T / FMath::Max(1.f, Seconds), 0.f, 1.f), 1.6f);
			Aim(FMath::Lerp(AerialA, AerialB, U), AerialLook + (AerialB - AerialA) * 0.2f * U, 60.f, Dt, 0.f, true);
		}
		else if (Cam) { Aim(FVector(0.f, 0.f, 6000.f), FVector::ZeroVector, 60.f, Dt, 0.f, true); }
	}
	else if (Scene == TEXT("fight"))
	{
		// before the take: the hottest fight; during it: the same fight (its star, or the hero nearest when it falls)
		if (!bRecording) { Followed = Star; }
		AActor* A = Followed.Get();
		if (!A || !IsHero(Cast<AArenaCharacter>(A))) { A = NearestHero(W, Focus.IsZero() ? Spot : Focus); Followed = A; }
		if (A) { const FVector C = ClusterAround(W, A); Focus = bFirst ? C : FMath::VInterpTo(Focus, C, Dt, 1.5f); }
		// low and close: the lens at the heroes' shoulders, looking a little up into the fight, breathing gently
		Angle += 9.f * Dt;
		const float R = FMath::DegreesToRadians(Angle);
		const float Bob = 15.f * FMath::Sin(T * 0.9f);
		Aim(Focus + FVector(FMath::Cos(R) * OrbitR, FMath::Sin(R) * OrbitR, OrbitH + Bob), Focus + FVector(0.f, 0.f, 70.f), 62.f, Dt, 3.f, bFirst);
	}
	else if (Scene == TEXT("follow"))
	{
		AActor* A = Followed.Get();
		const AArenaCharacter* AC = Cast<AArenaCharacter>(A);
		if (!A || (AC && !AC->IsAlive())) { A = (bRecording && !Focus.IsZero()) ? NearestHero(W, Focus) : Star; Followed = A; }
		if (A) { Focus = A->GetActorLocation(); }
		if (A)
		{
			const FVector F = A->GetActorForwardVector().GetSafeNormal2D(), Rt = FVector::CrossProduct(FVector::UpVector, F);
			const FVector L = A->GetActorLocation();
			Aim(L - F * 480.f + Rt * 210.f + FVector(0.f, 0.f, 170.f), L + F * 260.f + FVector(0.f, 0.f, 90.f), 62.f, Dt, 4.f, bFirst);
		}
	}
	else if (Scene == TEXT("hero"))
	{
		// low and close on the star of the fight: the lens looks up at the hero and turns slowly around it
		AActor* A = Followed.Get();
		const AArenaCharacter* AC = Cast<AArenaCharacter>(A);
		if (!A || (AC && !AC->IsAlive())) { A = (bRecording && !Focus.IsZero()) ? NearestHero(W, Focus) : Star; Followed = A; }
		if (A) { Focus = A->GetActorLocation(); }
		if (A)
		{
			if (!bRecording) { Angle = ClearAngle(W, A, 480.f, 120.f); }
			else { Angle += 8.f * Dt; }
			const FVector L = A->GetActorLocation();
			const FVector Dir = FRotator(0.f, Angle, 0.f).Vector();
			Aim(L + Dir * 480.f + FVector(0.f, 0.f, 120.f), L + FVector(0.f, 0.f, 100.f), 48.f, Dt, 4.f, bFirst);
		}
	}
	else if (Scene == TEXT("tower"))
	{
		FVector S;
		int32 H = 0;
		if (StructureUnderAttack(S, H)) { Focus = bFirst ? S : FMath::VInterpTo(Focus, S, Dt, 0.8f); }
		Angle += 8.f * Dt;
		const float R = FMath::DegreesToRadians(Angle);
		Aim(Focus + FVector(FMath::Cos(R) * 1500.f, FMath::Sin(R) * 1500.f, 650.f), Focus + FVector(0.f, 0.f, 250.f), 60.f, Dt, 2.f, bFirst);
	}
	else if (Scene == TEXT("duel"))
	{
		// a side camera on the pair, drifting slowly around them
		TArray<AActor*> F;
		for (TActorIterator<AProtoCharacter> It(W); It && F.Num() < 2; ++It) { if (It->IsAlive()) { F.Add(*It); } }
		if (F.Num() == 2)
		{
			const FVector Mid = (F[0]->GetActorLocation() + F[1]->GetActorLocation()) * 0.5f;
			Focus = bFirst ? Mid : FMath::VInterpTo(Focus, Mid, Dt, 2.f);
			FVector Line = (F[1]->GetActorLocation() - F[0]->GetActorLocation()).GetSafeNormal2D();
			if (Line.IsNearlyZero()) { Line = FVector(1.f, 0.f, 0.f); }
			const FVector Side = FVector::CrossProduct(Line, FVector::UpVector).RotateAngleAxis(FMath::Sin(T * 0.35f) * 25.f, FVector::UpVector);
			const float Dist = 650.f + 0.5f * FVector::Dist2D(F[0]->GetActorLocation(), F[1]->GetActorLocation());
			Aim(Focus + Side * Dist + FVector(0.f, 0.f, 210.f), Focus + FVector(0.f, 0.f, 70.f), 55.f, Dt, 2.5f, bFirst);
		}
	}
	else if (Scene == TEXT("hades"))
	{
		// prototype 2's own view: fixed above at 55 degrees, turned 45, on the pair
		if (bHot) { Focus = bFirst ? Spot : FMath::VInterpTo(Focus, Spot, Dt, 3.f); }
		const FVector Back = FRotator(-55.f, 45.f, 0.f).Vector();
		const FVector Loc = Focus - Back * 1900.f;
		Aim(Loc, Focus, 55.f, Dt, 0.f, true);
		UpdateCutaway(Loc, Focus + FVector(0.f, 0.f, 40.f));
	}
	// the frames: one per rendered frame while recording (MovieFrameNNNNN.png)
	if (bRecording && Now >= RecStart && GIsDumpingMovie == 0) { GIsDumpingMovie = -1; }
	if (bRecording && Now >= RecStart) { ++GTeaserFrames; }
	if (bRecording && GTeaserFrames >= FMath::RoundToInt(Seconds * 30.f))
	{
		GIsDumpingMovie = 0;
		bRecording = false;
		UE_LOG(LogTeaser, Display, TEXT("TEASER evt=record_end t=%.1f"), Now);
		TeaserFile(FString::Printf(TEXT("REC_END t=%.1f ticks=%d real=%.1f dt=%.4f heat=%d"), Now, GTeaserFrames, FPlatformTime::Seconds() - GTeaserReal0, Dt, Heat));
		for (const TWeakObjectPtr<AActor>& A : CutAway) { if (A.IsValid()) { A->SetActorHiddenInGame(false); } }
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		SetActorTickEnabled(false);
	}
}
