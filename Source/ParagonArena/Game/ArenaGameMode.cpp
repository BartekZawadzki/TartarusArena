#include "Game/ArenaGameMode.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Game/ArenaPlayerController.h"
#include "AI/ArenaBotController.h"
#include "Heroes/ArenaCharacter.h"
#include "UI/ArenaHUD.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "Game/ArenaGameState.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "UnrealClient.h"
#include "Arena/ArenaWorldActors.h"
#include "Engine/TargetPoint.h"
#include "Algo/Reverse.h"
#include "UI/ArenaIconStudio.h"
#include "Misc/App.h"
#include "Arena/ArenaPhysics.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Core/ArenaConquestRules.h"
#include "UI/ArenaSettings.h"
#include "Game/ArenaTeaser.h"

static FAutoConsoleCommandWithWorld GArenaStatus(TEXT("Arena.Status"), TEXT("Prints one ARENA_STATUS line"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (AArenaGameMode* GM = World ? World->GetAuthGameMode<AArenaGameMode>() : nullptr)
		{
			int32 Alive = 0;
			for (TActorIterator<AArenaCharacter> It(World); It; ++It) { Alive += It->IsAlive() ? 1 : 0; }
			UE_LOG(LogArena, Display, TEXT("ARENA_STATUS phase=%d score=%d/%d alive=%d t=%.1f left=%.0f"), (int32)GM->Phase, GM->Score.Points[0], GM->Score.Points[1], Alive, World->GetTimeSeconds(), GM->TimeLeft());
		}
	}));

AArenaGameMode::AArenaGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = AArenaPlayerController::StaticClass();
	HUDClass = AArenaHUD::StaticClass();
	GameStateClass = AArenaGameState::StaticClass();
	DefaultPawnClass = nullptr;
}

AArenaGameMode* AArenaGameMode::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) { return nullptr; }
	if (AArenaGameMode* GM = World->GetAuthGameMode<AArenaGameMode>()) { return GM; }
	static TWeakObjectPtr<AArenaGameMode> Mirror;
	if (Mirror.IsValid() && Mirror->GetWorld() == World) { return Mirror.Get(); }
	for (TActorIterator<AArenaClientMirror> It(World); It; ++It) { Mirror = *It; return *It; }
	return nullptr;
}

void AArenaGameMode::BeginPlay()
{
	Super::BeginPlay();
	// -ArenaStartMap=<Map>: from the default map straight on to <Map> by the menu's own OpenLevel (a Shipping build
	// ignores a map given on its command line; the exe's tests travel like a player does)
	{
		FString StartMap;
		if (FParse::Value(FCommandLine::Get(), TEXT("ArenaStartMap="), StartMap) && !GetWorld()->GetMapName().Contains(StartMap))
		{
			UE_LOG(LogArena, Display, TEXT("ARENA evt=start_map to=%s"), *StartMap);
			FTimerHandle Th;
			GetWorldTimerManager().SetTimer(Th, FTimerDelegate::CreateWeakLambda(this, [this, StartMap]() { UGameplayStatics::OpenLevel(this, FName(*(TEXT("/Game/Maps/") + StartMap)), true, TEXT("")); }), 0.2f, false);
			return;
		}
	}
	AArenaTeaserDirector::MaybeStart(GetWorld());   // -ArenaTeaser=<scene>: the teaser's film camera
	Rules = FArenaDatabase::Get().Rules;
	PreloadAssets();
	GetWorld()->SpawnActor<AArenaIconStudio>();   // portraits, ability icons and the minimap for the HUD
	SetMatchMinutes(Rules.MatchMinutes);
	if (bClientMirror)
	{
		// a LAN client's mirror: the map's lanes and bases for the HUD, nothing else (the server decides)
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == TEXT("BaseA")) { BaseA = It->GetActorLocation(); }
			if (It->PlayerStartTag == TEXT("BaseB")) { BaseB = It->GetActorLocation(); }
		}
		bFountainsOn = true;
		UE_LOG(LogArena, Display, TEXT("ARENA evt=client_mirror"));
		return;
	}

	TMap<FString, TArray<TPair<int32, FVector>>> LanePoints;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->Tags.Contains(TEXT("OrbSpot"))) { OrbSpot = It->GetActorLocation(); }
		for (const FName& Tag : It->Tags)
		{
			FString Lane, Index;
			if (Tag.ToString().StartsWith(TEXT("Lane_")) && Tag.ToString().RightChop(5).Split(TEXT("_"), &Lane, &Index, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				LanePoints.FindOrAdd(Lane).Add(TPair<int32, FVector>(FCString::Atoi(*Index), It->GetActorLocation()));
			}
		}
	}
	LanePoints.KeySort([](const FString& A, const FString& B) { return A < B; });
	for (TPair<FString, TArray<TPair<int32, FVector>>>& L : LanePoints)
	{
		L.Value.Sort([](const TPair<int32, FVector>& A, const TPair<int32, FVector>& B) { return A.Key < B.Key; });
		TArray<FVector>& Path = Lanes.AddDefaulted_GetRef();
		for (const TPair<int32, FVector>& P : L.Value) { Path.Add(P.Value); }
		UE_LOG(LogArena, Display, TEXT("ARENA evt=lane name=%s points=%d"), *L.Key, Path.Num());
	}
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerStartTag == TEXT("BaseA")) { BaseA = It->GetActorLocation(); }
		if (It->PlayerStartTag == TEXT("BaseB")) { BaseB = It->GetActorLocation(); }
	}

	const TCHAR* Cmd = FCommandLine::Get();
	bBotMatch = FParse::Param(Cmd, TEXT("ArenaBotMatch"));
	bConquest = FParse::Param(Cmd, TEXT("ArenaConquest"));
	if (UGameplayStatics::HasOption(OptionsString, TEXT("Conquest")))
	{
		// a LAN host's match: the mode, length and difficulty chosen before hosting
		bConquest = UGameplayStatics::GetIntOption(OptionsString, TEXT("Conquest"), 0) != 0;
		TeamSize = FMath::Clamp(UGameplayStatics::GetIntOption(OptionsString, TEXT("Size"), 5), 1, 5);
		SetMatchMinutes(UGameplayStatics::GetIntOption(OptionsString, TEXT("Minutes"), 10));
		Difficulty = FMath::Clamp(UGameplayStatics::GetIntOption(OptionsString, TEXT("Diff"), 1), 0, 2);
	}
	if (IsConquestMap(GetWorld())) { bConquest = true; TeamSize = 5; }   // the map is Conquest's own
	bShots = FParse::Param(Cmd, TEXT("ArenaShots"));
	FParse::Value(Cmd, TEXT("DiffA="), TeamDifficulty[0]);
	FParse::Value(Cmd, TEXT("DiffB="), TeamDifficulty[1]);
	if (FParse::Param(Cmd, TEXT("ArenaTour"))) { for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It) { if (It->Tags.Contains(TEXT("Tour"))) { TourCams.Add(*It); } } }
	TourCams.Sort([](const TWeakObjectPtr<AActor>& A, const TWeakObjectPtr<AActor>& B) { return A->Tags.Last().ToString() < B->Tags.Last().ToString(); });   // tags: Tour, Tour_NN
	FParse::Value(Cmd, TEXT("Seed="), Seed);
	float Minutes = 3.f;
	if (FParse::Value(Cmd, TEXT("Minutes="), Minutes)) { BotMatchSeconds = Minutes * 60.f; }
	FParse::Value(Cmd, TEXT("Difficulty="), Difficulty);
	FMath::RandInit(Seed);
	FMath::SRandInit(Seed);

	// overview camera for hero select: the map's own (tag SelectCam) or a default one
	ACameraActor* Cam = nullptr;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It) { if (It->Tags.Contains(TEXT("SelectCam"))) { Cam = *It; } }
	if (!Cam) { Cam = GetWorld()->SpawnActor<ACameraActor>(FVector(0.f, -5200.f, 3600.f), FRotator(-32.f, 90.f, 0.f)); }
	// (v19: the map's cameras constrained the aspect ratio: black bars on a 21:9 screen)
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It) { if (It->GetCameraComponent()) { It->GetCameraComponent()->SetConstraintAspectRatio(false); } }
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { if (Cam) { PC->SetViewTarget(Cam); } }
	PhaseStart = GetWorld()->GetTimeSeconds();
	UE_LOG(LogArena, Display, TEXT("ARENA t=0 evt=boot heroes=%d botmatch=%d seed=%d"), FArenaDatabase::Get().Heroes.Num(), bBotMatch ? 1 : 0, Seed);
	bUIDemo = FParse::Param(Cmd, TEXT("ArenaUIDemo"));
	// -ArenaNoHorizon: the ring of cliffs past the forest hidden (an A/B of its rendering cost)
	if (FParse::Param(Cmd, TEXT("ArenaNoHorizon")))
	{
		int32 Hidden = 0;
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMesh* M = It->GetStaticMeshComponent() ? It->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
			if (M && M->GetName() == TEXT("SM_Cliff01") && It->GetActorLocation().Size2D() > 15000.f) { It->SetActorHiddenInGame(true); ++Hidden; }
		}
		UE_LOG(LogArena, Display, TEXT("ARENA evt=no_horizon hidden=%d"), Hidden);
	}
	if (bUIDemo) { bBotMatch = true; }
	// loose props: damped, capped spin, gentle depenetration (nothing pops out of an overlap into the sky)
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld() || !It->IsSimulatingPhysics()) { continue; }
		ArenaPhysics::CalmProp(*It);
		// a moving nav obstacle rides on every prop: bots walk around crates instead of shoving them down the lane
		UBoxComponent* Obstacle = NewObject<UBoxComponent>(It->GetOwner());
		Obstacle->SetupAttachment(*It);
		Obstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Obstacle->SetBoxExtent(It->Bounds.BoxExtent / It->GetComponentScale() + FVector(25.f));
		Obstacle->bDynamicObstacle = true;
		Obstacle->SetCanEverAffectNavigation(true);
		Obstacle->RegisterComponent();
	}
	if (IsConquestMap(GetWorld()) && UGameplayStatics::HasOption(OptionsString, TEXT("Hero")) && GetNetMode() == NM_Standalone && !bBotMatch)
	{
		const int32 Pick = UGameplayStatics::GetIntOption(OptionsString, TEXT("Hero"), 0);
		FTimerHandle Ch;
		GetWorldTimerManager().SetTimer(Ch, FTimerDelegate::CreateWeakLambda(this, [this, Pick]() { PlayerPickHero(Pick); }), 0.3f, false);
	}
	// the training map: its own start (the hero comes from the menu: ?Hero=N in the travel URL)
	if (IsTrainingMap(GetWorld()))
	{
		const int32 Pick = UGameplayStatics::GetIntOption(OptionsString, TEXT("Hero"), 0);
		FTimerHandle Th;
		GetWorldTimerManager().SetTimer(Th, FTimerDelegate::CreateWeakLambda(this, [this, Pick]() { StartTraining(Pick); }), 0.3f, false);
		if (FParse::Param(Cmd, TEXT("ArenaTrainingDemo"))) { bUIDemo = false; bTrainingDemo = true; }
		return;
	}
	bAnimLab = FParse::Param(Cmd, TEXT("ArenaAnimLab"));
	if (bAnimLab) { bBotMatch = true; StartAnimLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaMechLab"))) { bAnimLab = true; bBotMatch = true; LabMode = 1; StartMechLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaFoliageCheck"))) { bAnimLab = true; bBotMatch = true; LabMode = 6; FTimerHandle Fh; GetWorldTimerManager().SetTimer(Fh, this, &AArenaGameMode::RunFoliageCheck, 2.f, false); return; }
	if (FParse::Param(Cmd, TEXT("ArenaConquestLab"))) { bAnimLab = true; bBotMatch = false; bLabHUD = true; LabMode = 8; StartConquestLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaDuelLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = false; LabMode = 9; StartDuelLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaLocoLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = false; LabMode = 11; LabStart = GetWorld()->GetTimeSeconds(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaSkinDeathLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = false; LabMode = 10; LabStart = GetWorld()->GetTimeSeconds(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaFxLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = false; LabMode = 7; StartFxLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaBaseLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = true; LabMode = 5; StartBaseLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaSkillLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = true; LabMode = 4; StartSkillLab(); return; }
	if (FParse::Param(Cmd, TEXT("ArenaAimLab"))) { bAnimLab = true; bBotMatch = true; bLabHUD = true; LabMode = 3; StartAimLab(); return; }
	FString CamFile;
	if (FParse::Value(Cmd, TEXT("ArenaCamShots="), CamFile)) { bAnimLab = true; bBotMatch = true; LabMode = 2; StartCamShots(CamFile); return; }
	if (FParse::Param(Cmd, TEXT("ArenaNetHost")))
	{
		bNetTestHost = true;
		if (GetNetMode() == NM_Standalone)
		{
			FTimerHandle Nh;
			GetWorldTimerManager().SetTimer(Nh, FTimerDelegate::CreateWeakLambda(this, [this]() { UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Arena"), true, TEXT("listen?Conquest=0?Size=5?Minutes=5?Diff=1")); }), 1.f, false);
		}
		return;
	}
	if (bBotMatch && !bUIDemo) { PlayerPickHero(Seed % FMath::Max(1, FArenaDatabase::Get().Heroes.Num())); }
}

void AArenaGameMode::TickNetTestHost(float Now)
{
	auto Check = [this](bool bOk, const FString& What) { LabFails += bOk ? 0 : 1; UE_LOG(LogArena, Display, TEXT("LAB %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What); };
	switch (NetTestStep)
	{
	case 0:
		if (RemoteHumans.Num() > 0) { NetTestAt = Now; NetTestStep = 1; UE_LOG(LogArena, Display, TEXT("ARENA evt=nettest_guest_joined")); }
		else if (Now > 120.f) { Check(false, TEXT("a LAN guest joined within 2 minutes")); NetTestStep = 9; NetTestAt = Now; }
		break;
	case 1:
		// the guest loads the map and picks (its login comes before its map is up); then the host starts
		if ((RemoteHumans.Num() > 0 && RemoteHumans[0].Hero >= 0 && Now - NetTestAt > 2.f) || Now - NetTestAt > 90.f)
		{
			Check(RemoteHumans.Num() > 0 && RemoteHumans[0].Hero >= 0, FString::Printf(TEXT("the guest's pick reached the host (%d)"), RemoteHumans.Num() > 0 ? RemoteHumans[0].Hero : -1));
			PlayerPickHero(0); NetTestStep = 2; NetTestAt = Now;
		}
		break;
	case 2:
		if (Phase == EArenaPhase::Playing && RemoteHumans.Num() > 0 && RemoteHumans[0].PC.IsValid() && FMath::FloorToInt(Now) % 5 == 0 && FMath::FloorToInt(Now) != FMath::FloorToInt(Now - GetWorld()->GetDeltaSeconds()))
		{
			if (const AArenaCharacter* GH = Cast<AArenaCharacter>(RemoteHumans[0].PC->GetPawn()))
			{
				UE_LOG(LogArena, Display, TEXT("ARENA evt=nethost_guestpos t=%.0f at=%s aim=%s target=%s casts=%d"), Now - MatchStart, *GH->GetActorLocation().ToCompactString(), *GH->AimPoint.ToCompactString(),
					GH->AimTarget.IsValid() ? *GH->AimTarget->GetDef().Id.ToString() : TEXT("none"), GH->SlotCasts[0]);
			}
		}
		if (Phase == EArenaPhase::Playing && Now - MatchStart > 50.f)
		{
			const FHuman* G = RemoteHumans.Num() > 0 ? &RemoteHumans[0] : nullptr;
			AArenaCharacter* GH = G && G->PC.IsValid() ? Cast<AArenaCharacter>(G->PC->GetPawn()) : nullptr;
			Check(G && G->Hero >= 0 && GH && GH->HeroIndex == G->Hero && GH->GetTeam() == G->Team, FString::Printf(TEXT("the guest plays its own hero on team %d (%s)"), G ? G->Team : -1, GH ? *GH->GetDef().Id.ToString() : TEXT("none")));
			int32 Casts = 0;
			if (GH) { for (int32 s = 0; s < 5; ++s) { Casts += GH->SlotCasts[s]; } }
			Check(Casts >= 5, FString::Printf(TEXT("the guest's casts ran on the server (%d)"), Casts));
			const AArenaGameState* GS = GetGameState<AArenaGameState>();
			const FArenaHeroStat* St = GS && G ? GS->Stat(G->Team, G->Hero) : nullptr;
			Check(St && (St->DamageToHeroes > 0.f || St->MinionKills > 0 || (GH && GH->Gold > Rules.StartGold + 200.f)), FString::Printf(TEXT("the guest's blows landed (damage to heroes %.0f, minions %d)"), St ? St->DamageToHeroes : -1.f, St ? St->MinionKills : -1));
			NetTestStep = 9; NetTestAt = Now;
		}
		break;
	case 9:
		if (Now - NetTestAt > 15.f)
		{
			UE_LOG(LogArena, Display, TEXT("LAB_SUMMARY fails=%d"), LabFails);
			NetTestStep = 10;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}

void AArenaGameMode::PreloadAssets()
{
	const double T0 = FPlatformTime::Seconds();
	TSet<FString> Paths;
	auto Add = [&Paths](const FString& P) { if (!P.IsEmpty()) { Paths.Add(P); } };
	const FArenaDatabaseFile& Db = FArenaDatabase::Get();
	TArray<const FArenaHeroDef*> All;
	for (const FArenaHeroDef& H : Db.Heroes) { All.Add(&H); }
	All.Add(&Db.Rules.MeleeMinion);
	All.Add(&Db.Rules.RangedMinion);
	// Conquest: the structures, the heavy minions, the camps and the boss (a first tower shot used to stall a frame)
	const FArenaConquestDef& Cq = Db.Rules.Conquest;
	for (const FArenaHeroDef* D : { &Cq.Tower, &Cq.Inhibitor, &Cq.Core, &Cq.SiegeMinion, &Cq.SuperMinion }) { if (!D->Mesh.IsEmpty()) { All.Add(D); } }
	for (const FArenaCampDef& C : Cq.Camps) { if (!C.Unit.Mesh.IsEmpty()) { All.Add(&C.Unit); } }
	for (const FString& F : Cq.BuffFx) { Add(F); }
	Add(Cq.StructureDeathFx); Add(Cq.Core.StaticMesh);
	for (const FArenaHeroDef* H : All)
	{
		for (const FArenaAbilityDef& A : H->Abilities)
		{
			Add(A.Anim); Add(A.Fx); Add(A.CastFx); Add(A.TrailFx); Add(A.Sound); Add(A.ImpactSound);
			for (const FString& V : A.AnimVariants) { Add(V); }
		}
		for (const FString& P : H->DeathAnims) { Add(P); }
		for (const FString& P : H->HitReacts) { Add(P); }
		for (const FString& P : H->StunAnims) { Add(P); }
		for (const FString& P : H->KnockAnims) { Add(P); }
		Add(H->RespawnAnim); Add(H->IntroAnim); Add(H->VictoryAnim); Add(H->DeathFx); Add(H->DeathVanishFx); Add(H->SpawnFx);
		Add(H->PainSound); Add(H->DeathSound); Add(H->KillSound); Add(H->Mesh); Add(H->MeshAlt); Add(H->IdleAnim); Add(H->RunAnim); Add(H->PortraitAnim); Add(H->PortraitPose);
	}
	Add(Db.Rules.RespawnFx); Add(Db.Rules.LevelUpFx);
	Add(TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"));
	int32 Loaded = 0;
	for (const FString& P : Paths)
	{
		if (UObject* O = LoadObject<UObject>(nullptr, *P, nullptr, LOAD_NoWarn | LOAD_Quiet)) { Preloaded.Add(O); ++Loaded; }
	}
	// the anim blueprints (classes) too: the first minion wave stalled 1.1 s loading the mannequin's
	for (const FArenaHeroDef* H : All)
	{
		if (UClass* C = H->AnimClass.IsEmpty() ? nullptr : LoadClass<UObject>(nullptr, *H->AnimClass, nullptr, LOAD_NoWarn | LOAD_Quiet)) { Preloaded.Add(C); ++Loaded; }
	}
	// and the fade copies of their materials (the first minion death would load them)
	for (const FArenaHeroDef* H : All)
	{
		for (const FString* MeshPath : { &H->Mesh, &H->MeshAlt })
		{
			if (const USkeletalMesh* Sk = MeshPath->IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, **MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				for (const FSkeletalMaterial& M : Sk->GetMaterials()) { if (UMaterialInterface* F = AArenaCharacter::FadeCopyOf(M.MaterialInterface)) { Preloaded.Add(F); ++Loaded; } }
			}
		}
	}
	PreloadMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogArena, Display, TEXT("ARENA evt=preload assets=%d ms=%.0f"), Loaded, PreloadMs);
}

void AArenaGameMode::PlayerPickHero(int32 Index)
{
	if (Phase != EArenaPhase::HeroSelect || !FArenaDatabase::Get().Heroes.IsValidIndex(Index)) { return; }
	// Conquest chosen in the menu (on the Arena map): the match is played on its own, bigger map
	if (bConquest && !IsConquestMap(GetWorld()) && GetNetMode() == NM_Standalone && !bBotMatch && !bAnimLab)
	{
		if (AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetWorld()->GetFirstPlayerController()))
		{
			PC->TravelWithLoading(TEXT("Conquest"), FString::Printf(TEXT("?Conquest=1?Hero=%d?Size=5?Minutes=%d?Diff=%d"), Index, MatchMinutes, Difficulty));
			return;
		}
	}
	PlayerHeroIndex = Index;
	AArenaCharacter::LocalTeam = 0;   // the player's team (relative colours: blue allies, red enemies)
	StartMatch();
}

AArenaCharacter* AArenaGameMode::SpawnHero(int32 HeroIndex, int32 Team, int32 SlotInTeam, bool bPlayer, int32 Level, float Xp, APlayerController* OwnerPC)
{
	const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
	if (!Defs.IsValidIndex(HeroIndex)) { return nullptr; }
	const FVector Base = TeamBase(Team);
	const float Ang = (SlotInTeam - 2) * 0.45f;
	const FVector Facing = (TeamBase(1 - Team) - Base).GetSafeNormal2D();
	const FVector Loc = FreeSpot(Base + Facing.RotateAngleAxis(90.f, FVector::UpVector) * (Ang * 400.f) + FVector(0, 0, 60.f), 40.f);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AArenaCharacter* C = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Loc, Facing.Rotation(), P);
	if (!C) { return nullptr; }
	// the body: the player's chosen skin; a bot wears one of the pack's at random now and then (variety, seeded)
	FArenaHeroDef Body = Defs[HeroIndex];
	int32 Skin = bPlayer && !bBotMatch ? FArenaSettings::Get().SkinFor(Body.Id) : (FMath::RandRange(0, 2) == 0 ? FMath::RandRange(0, Body.Skins.Num() - 1) : -1);
	if (ForcedSkin > -2) { Skin = ForcedSkin; }
	if (OwnerPC && !OwnerPC->IsLocalController()) { Skin = -1; for (const FHuman& Hm : RemoteHumans) { if (Hm.PC.Get() == OwnerPC) { Skin = Hm.Skin; } } }
	if (Body.Skins.IsValidIndex(Skin)) { Body.Mesh = Body.Skins[Skin]; Body.MeshAlt.Reset(); }
	C->InitCharacter(Body, Team, Level, false);
	C->SetNetSetup(1, HeroIndex, Body.Skins.IsValidIndex(Skin) ? Skin : -1);
	C->HeroIndex = HeroIndex;
	C->Xp = Xp;
	APlayerController* PC = OwnerPC ? OwnerPC : GetWorld()->GetFirstPlayerController();
	if (bPlayer && PC && !bBotMatch)
	{
		C->PlayerIndex = OwnerPC && !OwnerPC->IsLocalController() ? 1 : 0;
		PC->Possess(C);
		PC->SetControlRotation(Facing.Rotation());
		PC->SetViewTarget(C);
	}
	else
	{
		AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
		AI->Difficulty = Team == 0 && !bBotMatch ? 1 : Difficulty;
		if (bBotMatch && TeamDifficulty[Team] >= 0) { AI->Difficulty = TeamDifficulty[Team]; }
		AI->Possess(C);
		if (AI->Difficulty >= 2) { C->SetBotEdge(Rules.HardBotPower, Rules.HardBotHealth); }
		if (bPlayer && PC) { C->PlayerIndex = 0; PC->SetViewTarget(C); }   // bot match: watch the "player" hero
		if (bConquest) { AI->HomeLane = HeroLanes.FindOrAdd(Team * 100 + HeroIndex, LaneForSlot(SlotInTeam)); }
	}
	if (bConquest) { HeroLanes.FindOrAdd(Team * 100 + HeroIndex, LaneForSlot(SlotInTeam)); }
	Heroes.Add(C);
	if (!C->bAssistedAim && Xp <= 0.f) { C->AutoRank(); }   // bots spend their first point (a respawn restores its ranks); the player picks it
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=spawn hero=%s team=%d level=%d player=%d skin=%d"), GetWorld()->GetTimeSeconds(), *C->GetDef().Id.ToString(), Team, Level, bPlayer ? 1 : 0, C->NetSetupSkin());
	return C;
}

void AArenaGameMode::StartMatch()
{
	const int32 N = FArenaDatabase::Get().Heroes.Num();
	if (N == 0) { return; }
	bFountainsOn = true;
	Heroes.Reset();
	// team 0: the player's hero + the next classes; team 1: the classes in order (5 na 5: all five on each side;
	// Skirmish 3v3 and Duel 1v1 take the first ones; the enemy duellist is never the player's own hero)
	const int32 Size = FMath::Clamp(TeamSize, 1, 5);
	// every hero once when the roster allows it (v15: 11 heroes for 10 places), picked at random by the seed
	TArray<int32> Pool;
	for (int32 i = 0; i < N; ++i) { if (i != PlayerHeroIndex) { Pool.Add(i); } }
	for (int32 i = Pool.Num() - 1; i > 0; --i) { Pool.Swap(i, FMath::RandRange(0, i)); }
	int32 Next = 0;
	auto Draw = [&]() { const int32 H = Pool.Num() > 0 ? Pool[Next % Pool.Num()] : PlayerHeroIndex; ++Next; return H; };
	// the LAN players' picks come out of the pool (a pick already taken, or none, gets a free hero)
	for (FHuman& Hm : RemoteHumans)
	{
		if (!Hm.PC.IsValid()) { continue; }
		if (Hm.Hero < 0 || Hm.Hero == PlayerHeroIndex || !Pool.Contains(Hm.Hero)) { Hm.Hero = Pool.Num() > 0 ? Pool[0] : 0; }
		Pool.Remove(Hm.Hero);
	}
	SpawnHero(PlayerHeroIndex, 0, 0, true, 1, 0.f);
	int32 SlotOf[2] = { 1, 0 };
	for (const FHuman& Hm : RemoteHumans)
	{
		if (Hm.PC.IsValid() && SlotOf[Hm.Team] < Size) { SpawnHero(Hm.Hero, Hm.Team, SlotOf[Hm.Team]++, true, 1, 0.f, Hm.PC.Get()); }
	}
	for (int32 T = 0; T < 2; ++T) { while (SlotOf[T] < Size) { SpawnHero(Draw(), T, SlotOf[T]++, false, 1, 0.f); } }
	for (const TWeakObjectPtr<AArenaCharacter>& H : Heroes) { if (H.IsValid()) { H->Gold = Rules.StartGold; H->PlayIntro(); } }
	// the clock and the score limit for this match (bot matches: -Minutes)
	MatchLength = bBotMatch ? BotMatchSeconds : (bConquest ? Rules.Conquest.MatchMinutes * 60.f : MatchMinutes * 60.f);
	ArenaCore::FScoreRules SR;
	SR.HeroKill = Rules.ScoreHeroKill; SR.MinionKill = Rules.ScoreMinionKill; SR.MinionBase = Rules.ScoreMinionBase;
	SR.Limit = FMath::Max(1, FMath::RoundToInt(Rules.ScorePerMinute * MatchLength / 60.f));
	Score = ArenaCore::FScoreLedger(SR);
	bOvertime = false;
	Phase = EArenaPhase::Countdown;
	PhaseStart = GetWorld()->GetTimeSeconds();
	Announce(this, TEXT("Fight in 3..."), true);
	if (bConquest) { if (NavReady()) { StartConquest(); } else { bConquestPending = true; } }
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes) { if (W.IsValid() && W->PlayerIndex >= 0) { W->Voice(TEXT("Intro_Boast"), 0.f); } }   // the player's hero boasts at the start
}

void AArenaGameMode::SpawnWave()
{
	if (bConquest && Lanes.Num() > 0) { SpawnConquestWave(); return; }
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FVector Base = TeamBase(Team);
		const FVector Fwd = (TeamBase(1 - Team) - Base).GetSafeNormal2D();
		const int32 Total = Rules.MeleeMinionsPerWave + Rules.RangedMinionsPerWave;
		for (int32 i = 0; i < Total; ++i)
		{
			const bool bRanged = i >= Rules.MeleeMinionsPerWave;
			const FVector Loc = FreeSpot(Base + Fwd * 500.f + Fwd.RotateAngleAxis(90.f, FVector::UpVector) * ((i - Total * 0.5f) * 160.f) + FVector(0, 0, 60.f), 30.f);
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (AArenaCharacter* M = GetWorld()->SpawnActor<AArenaCharacter>(AArenaCharacter::StaticClass(), Loc, Fwd.Rotation(), P))
			{
				const int32 WaveLevel = 1 + FMath::Clamp(FMath::FloorToInt((GetWorld()->GetTimeSeconds() - MatchStart) / 120.f), 0, 9);
				M->InitCharacter(bRanged ? Rules.RangedMinion : Rules.MeleeMinion, Team, WaveLevel, true);
				M->SetNetSetup(bRanged ? 3 : 2, -1);
				AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
				AI->Difficulty = 1;
				AI->Possess(M);
				if (Lanes.Num() > 0) { AI->LanePath = LanePath(WaveIndex % Lanes.Num(), Team); }
			}
		}
	}
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=wave lane=%d"), GetWorld()->GetTimeSeconds(), Lanes.Num() > 0 ? WaveIndex % Lanes.Num() : -1);
	++WaveIndex;
}

void AArenaGameMode::NavCheck()
{
	// -ArenaNavCheck: can the key points of the map reach each other on the navmesh? (evt=navpath lines)
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav) { return; }
	TArray<TPair<FString, FVector>> Points = { { TEXT("BaseA"), BaseA }, { TEXT("BaseB"), BaseB }, { TEXT("Orb"), OrbSpot } };
	for (int32 L = 0; L < Lanes.Num(); ++L) { for (int32 i = 0; i < Lanes[L].Num(); ++i) { Points.Add({ FString::Printf(TEXT("Lane%d_%d"), L, i), Lanes[L][i] }); } }
	int32 Bad = 0;
	for (const TPair<FString, FVector>& P : Points)
	{
		FNavLocation Proj;
		const bool bOn = Nav->ProjectPointToNavigation(P.Value, Proj, FVector(200.f, 200.f, 400.f));
		UNavigationPath* Path = bOn ? Nav->FindPathToLocationSynchronously(GetWorld(), BaseA, P.Value) : nullptr;
		const bool bOk = Path && Path->IsValid() && !Path->IsPartial();
		Bad += bOk ? 0 : 1;
		UE_LOG(LogArena, Display, TEXT("ARENA evt=navpath from=BaseA to=%s at=%s projected=%d path=%s len=%.0f"), *P.Key, *P.Value.ToCompactString(), bOn ? 1 : 0,
			!Path || !Path->IsValid() ? TEXT("none") : (Path->IsPartial() ? TEXT("PARTIAL") : TEXT("ok")), Path ? Path->GetPathLength() : 0.f);
	}
	UE_LOG(LogArena, Display, TEXT("ARENA evt=navcheck points=%d unreachable=%d"), Points.Num(), Bad);
}

FVector AArenaGameMode::FreeSpot(const FVector& Desired, float Radius) const
{
	// never spawn a unit into (or onto) another one: try the spot, then rings of 1.5 m and 3 m around it
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius + 15.f, 95.f);
	const FCollisionObjectQueryParams Pawns(ECC_Pawn);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		for (int32 k = 0; k < (Ring == 0 ? 1 : 8); ++k)
		{
			FVector P = Desired + FVector(Ring * 150.f, 0.f, 0.f).RotateAngleAxis(k * 45.f + Ring * 22.5f, FVector::UpVector);
			FNavLocation OnNav;
			if (Ring > 0 && Nav && Nav->ProjectPointToNavigation(P, OnNav, FVector(100.f, 100.f, 300.f))) { P = FVector(OnNav.Location.X, OnNav.Location.Y, Desired.Z); }
			if (!GetWorld()->OverlapAnyTestByObjectType(P, FQuat::Identity, Pawns, Shape)) { return P; }
		}
	}
	return Desired;
}

TArray<FVector> AArenaGameMode::LanePath(int32 LaneIndex, int32 Team) const
{
	TArray<FVector> Path = Lanes.IsValidIndex(LaneIndex) ? Lanes[LaneIndex] : TArray<FVector>();
	if (Team == 1) { Algo::Reverse(Path); }
	return Path;
}

void AArenaGameMode::Announce(AActor* Context, const FString& Text, bool bBig, FLinearColor Color)
{
	FArenaFeedEntry E; E.Text = Text; E.Time = GetWorld()->GetTimeSeconds(); E.bBig = bBig; E.Color = Color;
	Feed.Add(E);
	if (Feed.Num() > 40) { Feed.RemoveAt(0); }
}

void AArenaGameMode::AnnounceFor(int32 Team, const FString& Text, const FLinearColor& Color, const FString& Other, const FLinearColor& OtherColor, bool bBig)
{
	FArenaFeedEntry E; E.Text = Text; E.Time = GetWorld()->GetTimeSeconds(); E.bBig = bBig; E.Color = Color;
	E.Team = Team; E.Other = Other; E.OtherColor = OtherColor;
	Feed.Add(E);
	if (Feed.Num() > 40) { Feed.RemoveAt(0); }
}

void AArenaGameMode::OnCharacterDied(AArenaCharacter* Victim, AArenaCharacter* Killer)
{
	if (!Victim || Phase == EArenaPhase::Ended || (bAnimLab && LabMode != 8) || bTraining) { return; }   // the conquest lab plays the real consequences   // the lab kills its own line-up, training brings its own back
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 VTeam = Victim->GetTeam();
	if (Victim->IsStructure()) { OnStructureDestroyed(Victim, Killer); CheckWinConditions(Now); return; }
	if (Victim->IsMonster()) { OnMonsterKilled(Victim, Killer); return; }
	if (!Victim->IsMinion() && Killer && Killer->IsMinion())
	{
		// LoL: a hero finished by a tower, a minion or a monster is the kill of the last enemy hero who hurt it (10 s)
		for (int32 i = Victim->RecentDamage.Num() - 1; i >= 0; --i)
		{
			const FArenaDamageEvent& E = Victim->RecentDamage[i];
			if (Now - E.Time > 10.f) { break; }
			if (E.SourceHero >= 0 && E.SourceTeam >= 0 && E.SourceTeam != VTeam)
			{
				if (AArenaCharacter* Credit = FindHero(E.SourceTeam, E.SourceHero)) { Killer = Credit; }
				break;
			}
		}
	}
	if (Victim->IsMinion())
	{
		Score.Award(ArenaCore::EScoreEvent::MinionKill, Killer ? Killer->GetTeam() : 1 - VTeam);
		if (Killer && !Killer->IsMinion())
		{
			++Killer->MinionKills;
			Killer->Gold += Rules.GoldMinion;
			if (Killer->IsLocalHero()) { AArenaHUD::AddGoldNumber(this, Victim->GetActorLocation(), Rules.GoldMinion); }
		}
		// XP is shared (MOBA): every hero of the other team near the minion gets it, whoever landed the blow
		for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
		{
			AArenaCharacter* H = W.Get();
			if (H && H->IsAlive() && H->GetTeam() != VTeam && FVector::Dist2D(H->GetActorLocation(), Victim->GetActorLocation()) <= Rules.XpShareRadius * 100.f) { H->AddXp(Rules.XpMinion); }
		}
	}
	else
	{
		AuditHeroDeath(Victim, Killer, Now);
		Score.Award(ArenaCore::EScoreEvent::HeroKill, 1 - VTeam);
		const float KillXp = Rules.XpHeroKill + Rules.XpHeroKillPerLevel * Victim->GetHeroLevel();   // a higher victim is worth more
		if (Killer)
		{
			// shutdown: ending a streak pays its bounty; the killer's own streak grows
			const int32 Bounty = ArenaCore::ShutdownGold(Victim->Streak, Rules.ShutdownPerKill, Rules.ShutdownMax);
			if (Killer->IsMinion()) { ++Killer->Kills; }
			// a killer who died first (an ultimate landing, a shot in flight) still gets the kill: on its respawn record
			else { GiveReward(Killer->GetTeam(), Killer->HeroIndex, Killer, Rules.GoldHeroKill + Bounty, KillXp, 1, 0); }
			if (!Killer->IsMinion())
			{
				Killer->Voice(FirstBlood == 0 ? TEXT("Kill_FirstBlood") : (Bounty > 0 ? TEXT("Kill_Shutdown") : (Killer->Streak >= 2 ? TEXT("Kill_Streak") : TEXT("Kill_Enemy"))), 1.5f);
			}
			if (!Killer->IsMinion())
			{
				if (Killer->IsLocalHero()) { AArenaHUD::AddGoldNumber(this, Victim->GetActorLocation(), Rules.GoldHeroKill + Bounty); }
				if (Bounty > 0) { Announce(this, FString::Printf(TEXT("%s ends %s's streak!  +%d g"), *Killer->GetDef().DisplayName, *Victim->GetDef().DisplayName, Bounty), true, FLinearColor(1.f, 0.8f, 0.2f)); }
				++Killer->Streak;
				if (Killer->Streak >= 3) { Announce(this, FString::Printf(TEXT("%s: streak %d  ·  bounty %d g"), *Killer->GetDef().DisplayName, Killer->Streak, ArenaCore::ShutdownGold(Killer->Streak, Rules.ShutdownPerKill, Rules.ShutdownMax)), false, FLinearColor(1.f, 0.6f, 0.2f)); }
			}
			UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=kill killer=%s victim=%s victim_streak=%d bounty=%d"), Now, *Killer->GetDef().Id.ToString(), *Victim->GetDef().Id.ToString(), Victim->Streak, Bounty);
			Killer->KillStreakWindow = (Now - Killer->LastKillTime < 8.f) ? Killer->KillStreakWindow + 1 : 1;
			Killer->LastKillTime = Now;
			if (++FirstBlood == 1) { Announce(this, TEXT("FIRST BLOOD!"), true, FLinearColor(1.f, 0.1f, 0.1f)); }
			if (Killer->KillStreakWindow == 2) { Announce(this, FString::Printf(TEXT("%s: DOUBLE KILL!"), *Killer->GetDef().DisplayName), true, FLinearColor(1.f, 0.6f, 0.1f)); }
			if (Killer->KillStreakWindow >= 3) { Announce(this, FString::Printf(TEXT("%s: TRIPLE KILL!"), *Killer->GetDef().DisplayName), true, FLinearColor(1.f, 0.85f, 0.1f)); }
		}
		// assists (LoL / Smite): the other heroes who hurt the victim in its last 10 s, alive or waiting to respawn
		// (it was every ally within 20 m: 61 assists to 23 kills in a bot match)
		TSet<int32> Helpers;
		const int32 KillerKey = Killer && !Killer->IsMinion() ? Killer->GetTeam() * 100 + Killer->HeroIndex : -1;
		for (const FArenaDamageEvent& E : Victim->RecentDamage)
		{
			if (Now - E.Time <= 10.f && E.SourceHero >= 0 && E.SourceTeam >= 0 && E.SourceTeam != VTeam) { Helpers.Add(E.SourceTeam * 100 + E.SourceHero); }
		}
		Helpers.Remove(KillerKey);
		for (const int32 Key : Helpers)
		{
			AArenaCharacter* Helper = FindHero(Key / 100, Key % 100);
			GiveReward(Key / 100, Key % 100, Helper, Rules.GoldAssist, KillXp * Rules.XpAssist, 0, 1);
			if (Helper) { Helper->Voice(TEXT("Kill_Assist"), 2.f); }
		}
		// respawn (VR-06): a fresh pawn at the base, same hero, level and XP
		const int32 HeroIndex = Victim->HeroIndex, Level = Victim->GetHeroLevel();
		const float Delay = ArenaCore::RespawnSeconds(Level);
		FArenaKill KillEvt; KillEvt.Time = Now; KillEvt.VictimHero = HeroIndex; KillEvt.VictimTeam = VTeam;
		if (Killer) { KillEvt.KillerHero = Killer->IsMinion() ? -1 : Killer->HeroIndex; KillEvt.KillerTeam = Killer->GetTeam(); KillEvt.bKillerMinion = Killer->IsMinion(); }
		Kills.Add(KillEvt);
		if (Kills.Num() > 30) { Kills.RemoveAt(0); }
		Respawns.RemoveAll([&](const FArenaRespawn& R) { return R.Team == VTeam && R.Hero == HeroIndex; });
		FArenaRespawn& Pending = Respawns.AddDefaulted_GetRef();
		Pending.Team = VTeam; Pending.Hero = HeroIndex; Pending.At = Now + Delay; Pending.Level = Level; Pending.Xp = Victim->Xp; Pending.bPlayer = Victim->PlayerIndex >= 0;
		Pending.Kills = Victim->Kills; Pending.Deaths = Victim->Deaths; Pending.Assists = Victim->Assists; Pending.CastCount = Victim->CastCount;
		FMemory::Memcpy(Pending.SlotCasts, Victim->SlotCasts, sizeof(Pending.SlotCasts));
		Pending.MinionKills = Victim->MinionKills; Pending.DamageToHeroes = Victim->DamageToHeroes; Pending.DamageTaken = Victim->DamageTaken; Pending.HealingDone = Victim->HealingDone;
		Pending.Gold = Victim->Gold; Pending.Items = Victim->Items;
		Pending.Potions[0] = Victim->Potions[0]; Pending.Potions[1] = Victim->Potions[1];
		Pending.Killer = Killer ? (Killer->IsLaneMinion() ? FString(TEXT("Minion")) : Killer->GetDef().DisplayName) : FString(TEXT("Fountain"));
		Pending.Owner = Cast<APlayerController>(Victim->GetController());
		Pending.Recap = Victim->RecentDamage.FilterByPredicate([Now](const FArenaDamageEvent& E) { return Now - E.Time <= 10.f; });
		FMemory::Memcpy(Pending.Ranks, Victim->GetRanks(), sizeof(Pending.Ranks));
		GetWorldTimerManager().SetTimer(Pending.Timer, FTimerDelegate::CreateWeakLambda(this, [this, VTeam, HeroIndex]() { RespawnNow(VTeam, HeroIndex); }), Delay, false);
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=death hero=%s team=%d killer=%s score=%d/%d respawn=%.0f"), Now, *Victim->GetDef().Id.ToString(), VTeam,
			Killer ? *Killer->GetDef().Id.ToString() : TEXT("none"), Score.Points[0], Score.Points[1], Delay);
		Heroes.Remove(Victim);
	}
	CheckWinConditions(Now);
}

void AArenaGameMode::GiveReward(int32 Team, int32 HeroIndex, AArenaCharacter* Live, float GoldAmount, float XpAmount, int32 AddKills, int32 AddAssists)
{
	if (Live && Live->IsAlive())
	{
		Live->Gold += GoldAmount;
		Live->AddXp(XpAmount);
		Live->Kills += AddKills;
		Live->Assists += AddAssists;
		return;
	}
	// dead: the record the respawn restores from (the body's own numbers were copied into it at the death)
	FArenaRespawn* R = Respawns.FindByPredicate([&](const FArenaRespawn& X) { return X.Team == Team && X.Hero == HeroIndex; });
	if (!R) { return; }
	R->Gold += GoldAmount;
	R->Xp += XpAmount;
	R->Level = FMath::Max(R->Level, ArenaCore::LevelForXp(R->Xp, Rules.XpBase, Rules.XpGrowth, FMath::Max(2, Rules.MaxLevel)));
	R->Kills += AddKills;
	R->Assists += AddAssists;
	UE_LOG(LogArena, Display, TEXT("ARENA evt=reward_dead team=%d hero=%d gold=%.0f xp=%.0f kills=%d assists=%d"), Team, HeroIndex, GoldAmount, XpAmount, AddKills, AddAssists);
}

void AArenaGameMode::SyncGameState()
{
	AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (!GS) { return; }
	GS->Phase = Phase;
	GS->PhaseStart = PhaseStart;
	GS->MatchStart = MatchStart;
	GS->MatchLength = MatchLength;
	GS->bOvertime = bOvertime;
	GS->ScoreA = Score.Points[0];
	GS->ScoreB = Score.Points[1];
	GS->ScoreLimit = FMath::Max(1, Score.Rules.Limit);
	GS->WinnerTeam = WinnerTeam;
	// one record per hero: the living from their actors, the dead from their respawn records (a hero dead at the end
	// kept its row; the scoreboard used to read the actors only and lost them)
	TArray<FArenaHeroStat> Stats;
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
	{
		const AArenaCharacter* C = W.Get();
		if (!C || !C->IsAlive() || C->IsMinion()) { continue; }
		FArenaHeroStat& S = Stats.AddDefaulted_GetRef();
		S.Team = C->GetTeam(); S.Hero = C->HeroIndex; S.Level = C->GetHeroLevel();
		S.Kills = C->Kills; S.Deaths = C->Deaths; S.Assists = C->Assists; S.MinionKills = C->MinionKills;
		S.Gold = C->Gold; S.DamageToHeroes = C->DamageToHeroes; S.DamageTaken = C->DamageTaken; S.Healing = C->HealingDone;
		S.Items = C->Items; S.bAlive = true; S.bPlayer = C->PlayerIndex >= 0;
	}
	for (const FArenaRespawn& R : Respawns)
	{
		if (Stats.ContainsByPredicate([&](const FArenaHeroStat& X) { return X.Team == R.Team && X.Hero == R.Hero; })) { continue; }
		FArenaHeroStat& S = Stats.AddDefaulted_GetRef();
		S.Team = R.Team; S.Hero = R.Hero; S.Level = R.Level;
		S.Kills = R.Kills; S.Deaths = R.Deaths; S.Assists = R.Assists; S.MinionKills = R.MinionKills;
		S.Gold = R.Gold; S.DamageToHeroes = R.DamageToHeroes; S.DamageTaken = R.DamageTaken; S.Healing = R.HealingDone;
		S.Items = R.Items; S.bAlive = false; S.bPlayer = R.bPlayer; S.RespawnAt = R.At;
	}
	Stats.Sort([](const FArenaHeroStat& A, const FArenaHeroStat& B) { return A.Team != B.Team ? A.Team < B.Team : A.Hero < B.Hero; });
	for (FArenaHeroStat& St : Stats)
	{
		const float* Ready = ReviveReadyAt.Find(St.Team * 100 + St.Hero);
		St.ReviveReadyAt = Ready ? *Ready : 0.f;
		if (const FArenaRespawn* R = PendingRespawn(St.Team, St.Hero)) { St.KilledBy = R->Killer; }
	}
	GS->HeroStats = MoveTemp(Stats);
	if (GetNetMode() != NM_Standalone)
	{
		// the announcements, the orb and the lobby's settings for the players on other machines
		if (GS->Feed.Num() != FMath::Min(Feed.Num(), 8) || (Feed.Num() > 0 && GS->Feed.Num() > 0 && GS->Feed.Last().Time != Feed.Last().Time))
		{
			GS->Feed.Reset();
			for (int32 i = FMath::Max(0, Feed.Num() - 8); i < Feed.Num(); ++i) { FArenaFeedRep& F = GS->Feed.AddDefaulted_GetRef(); F.Text = Feed[i].Text; F.Time = Feed[i].Time; F.Color = Feed[i].Color; F.bBig = Feed[i].bBig; F.Team = (int8)Feed[i].Team; F.Other = Feed[i].Other; F.OtherColor = Feed[i].OtherColor; }
		}
		FVector OrbAt;
		GS->bOrb = OrbLocation(OrbAt);
		if (GS->bOrb) { GS->OrbPos = OrbAt; }
		GS->TeamSize = TeamSize; GS->MatchMinutes = MatchMinutes; GS->Difficulty = Difficulty;
		GS->Humans = 1 + RemoteHumans.Num();
	}
	GS->bConquest = bConquest;
	if (bConquest) { WriteConquestState(GS); }
	if (GS->KillFeed.Num() != FMath::Min(Kills.Num(), 12) || (Kills.Num() > 0 && GS->KillFeed.Num() > 0 && GS->KillFeed.Last().Time != Kills.Last().Time))
	{
		GS->KillFeed.Reset();
		for (int32 i = FMath::Max(0, Kills.Num() - 12); i < Kills.Num(); ++i)
		{
			const FArenaKill& K = Kills[i];
			FArenaKillRep& R = GS->KillFeed.AddDefaulted_GetRef();
			R.Time = K.Time; R.KillerHero = K.KillerHero; R.KillerTeam = K.KillerTeam; R.bKillerMinion = K.bKillerMinion; R.VictimHero = K.VictimHero; R.VictimTeam = K.VictimTeam;
		}
	}
}

void AArenaGameMode::RespawnNow(int32 Team, int32 HeroIndex)
{
	const int32 I = Respawns.IndexOfByPredicate([&](const FArenaRespawn& R) { return R.Team == Team && R.Hero == HeroIndex; });
	if (I == INDEX_NONE) { return; }
	FArenaRespawn R = Respawns[I];
	GetWorldTimerManager().ClearTimer(Respawns[I].Timer);
	Respawns.RemoveAt(I);
	if (Phase == EArenaPhase::Ended) { return; }
	if (AArenaCharacter* N = SpawnHero(HeroIndex, Team, FMath::RandRange(0, 4), R.bPlayer, R.Level, R.Xp, R.Owner.Get()))
	{
		N->Kills = R.Kills; N->Deaths = R.Deaths; N->Assists = R.Assists; N->CastCount = R.CastCount; FMemory::Memcpy(N->SlotCasts, R.SlotCasts, sizeof(R.SlotCasts));
		N->MinionKills = R.MinionKills; N->DamageToHeroes = R.DamageToHeroes; N->DamageTaken = R.DamageTaken; N->HealingDone = R.HealingDone;
		N->Gold = R.Gold;
		N->Potions[0] = R.Potions[0]; N->Potions[1] = R.Potions[1];
		N->SetRanks(R.Ranks);
		if (!N->bAssistedAim) { N->AutoRank(); }
		N->SetItems(R.Items);
		N->RefillVitals();
		N->PlaySpawnIn();
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=respawn hero=%s team=%d gold=%.0f items=%d"), GetWorld()->GetTimeSeconds(), *N->GetDef().Id.ToString(), Team, N->Gold, N->Items.Num());
	}
}

const FArenaRespawn* AArenaGameMode::PendingRespawn(int32 Team, int32 HeroIndex) const
{
	return Respawns.FindByPredicate([&](const FArenaRespawn& R) { return R.Team == Team && R.Hero == HeroIndex; });
}

int32 AArenaGameMode::ReviveCostFor(int32 Level) const { return ArenaCore::ReviveCost(Level, Rules.ReviveBaseCost, Rules.RevivePerLevel); }

float AArenaGameMode::ReviveReadyIn(int32 Team, int32 HeroIndex) const
{
	const float* At = ReviveReadyAt.Find(Team * 100 + HeroIndex);
	return At ? FMath::Max(0.f, *At - GetWorld()->GetTimeSeconds()) : 0.f;
}

bool AArenaGameMode::TryRevive(int32 Team, int32 HeroIndex)
{
	FArenaRespawn* R = Respawns.FindByPredicate([&](const FArenaRespawn& X) { return X.Team == Team && X.Hero == HeroIndex; });
	const float Now = GetWorld()->GetTimeSeconds();
	if (!R || Phase != EArenaPhase::Playing) { return false; }
	const int32 Cost = ReviveCostFor(R->Level);
	if (!ArenaCore::CanRevive(true, Now, ReviveReadyAt.FindRef(Team * 100 + HeroIndex), FMath::FloorToInt(R->Gold), Cost)) { return false; }
	R->Gold -= Cost;
	ReviveReadyAt.Add(Team * 100 + HeroIndex, Now + Rules.ReviveCooldown);
	const FArenaHeroDef& D = FArenaDatabase::Get().Heroes[HeroIndex];
	AnnounceFor(Team, FString::Printf(TEXT("%s returns to the fight!"), *D.DisplayName), FLinearColor(0.4f, 0.7f, 1.f), FString::Printf(TEXT("%s returns to the fight!"), *D.DisplayName), FLinearColor(1.f, 0.45f, 0.35f), false);
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=revive hero=%s team=%d cost=%d"), Now, *D.Id.ToString(), Team, Cost);
	RespawnNow(Team, HeroIndex);
	if (AArenaCharacter* Back = FindHero(Team, HeroIndex)) { Back->Voice(TEXT("Revive"), 0.f); }
	return true;
}

AArenaCharacter* AArenaGameMode::FindHero(int32 Team, int32 HeroIndex) const
{
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes) { if (W.IsValid() && W->IsAlive() && W->GetTeam() == Team && W->HeroIndex == HeroIndex) { return W.Get(); } }
	return nullptr;
}

bool AArenaGameMode::InShop(const AArenaCharacter* C) const
{
	if (bTraining) { return C && C->IsAlive() && !C->IsMinion(); }   // the training centre: the shop anywhere
	return C && C->IsAlive() && !C->IsMinion() && FVector::Dist2D(C->GetActorLocation(), TeamBase(C->GetTeam())) < Rules.BaseRadius * 100.f;
}

ArenaCore::EBuyResult AArenaGameMode::TryBuy(int32 Team, int32 HeroIndex, int32 ItemIndex)
{
	const TArray<FArenaItemDef>& Db = FArenaDatabase::Get().Items;
	if (!Db.IsValidIndex(ItemIndex)) { return ArenaCore::EBuyResult::NotInShop; }
	const FArenaItemDef& Item = Db[ItemIndex];
	AArenaCharacter* C = FindHero(Team, HeroIndex);
	FArenaRespawn* R = C ? nullptr : Respawns.FindByPredicate([&](const FArenaRespawn& X) { return X.Team == Team && X.Hero == HeroIndex; });
	if (!C && !R) { return ArenaCore::EBuyResult::NotInShop; }
	float& Gold = C ? C->Gold : R->Gold;
	TArray<int32> Items = C ? C->Items : (R ? R->Items : TArray<int32>());
	// MOBA recipes: the components already owned are used up and come off the price
	TArray<int32> Consumed;
	const int32 Price = ArenaCore::BuyPrice(ItemIndex, FArenaDatabase::Recipes(), Items, Consumed);
	const ArenaCore::EBuyResult Result = ArenaCore::CanBuy(FMath::FloorToInt(Gold), Price, Items.Num(), (C && InShop(C)) || R != nullptr, Consumed.Num());
	if (Result != ArenaCore::EBuyResult::Ok) { return Result; }
	Gold -= Price;
	Consumed.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 K : Consumed) { Items.RemoveAt(K); }
	Items.Add(ItemIndex);
	if (C) { C->SetItems(Items); } else { R->Items = Items; }
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=buy hero=%s team=%d item=%s price=%d used=%d gold=%.0f"), GetWorld()->GetTimeSeconds(), *FArenaDatabase::Get().Heroes[HeroIndex].Id.ToString(), Team, *Item.Id.ToString(), Price, Consumed.Num(), Gold);
	return Result;
}

ArenaCore::EBuyResult AArenaGameMode::TryBuyPotion(int32 Team, int32 HeroIndex, int32 Kind)
{
	if (Kind < 0 || Kind > 1) { return ArenaCore::EBuyResult::NotInShop; }
	AArenaCharacter* C = FindHero(Team, HeroIndex);
	FArenaRespawn* R = C ? nullptr : Respawns.FindByPredicate([&](const FArenaRespawn& X) { return X.Team == Team && X.Hero == HeroIndex; });
	if ((!C || !InShop(C)) && !R) { return ArenaCore::EBuyResult::NotInShop; }
	int32& Count = C ? C->Potions[Kind] : R->Potions[Kind];
	float& Gold = C ? C->Gold : R->Gold;
	if (Count >= Rules.PotionMax) { return ArenaCore::EBuyResult::InventoryFull; }
	if (Gold < Rules.PotionCost) { return ArenaCore::EBuyResult::NotEnoughGold; }
	Gold -= Rules.PotionCost;
	++Count;
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=buy_potion hero=%s team=%d kind=%d count=%d gold=%.0f"), GetWorld()->GetTimeSeconds(), *FArenaDatabase::Get().Heroes[HeroIndex].Id.ToString(), Team, Kind, Count, Gold);
	return ArenaCore::EBuyResult::Ok;
}

FVector AArenaGameMode::BaseSpot(int32 Team) const
{
	const FVector Ring = FVector(260.f, 0.f, 0.f).RotateAngleAxis(FMath::FRandRange(0.f, 360.f), FVector::UpVector);
	return FreeSpot(TeamBase(Team) + Ring + FVector(0.f, 0.f, 60.f), 40.f);
}

TArray<int32> AArenaGameMode::InventoryOf(int32 Team, int32 HeroIndex) const
{
	if (const AArenaCharacter* C = FindHero(Team, HeroIndex)) { return C->Items; }
	if (const FArenaRespawn* R = PendingRespawn(Team, HeroIndex)) { return R->Items; }
	return TArray<int32>();
}

int32 AArenaGameMode::PriceFor(int32 Team, int32 HeroIndex, int32 ItemIndex, int32* OutConsumed) const
{
	TArray<int32> Consumed;
	const int32 P = ArenaCore::BuyPrice(ItemIndex, FArenaDatabase::Recipes(), InventoryOf(Team, HeroIndex), Consumed);
	if (OutConsumed) { *OutConsumed = Consumed.Num(); }
	return P;
}

bool AArenaGameMode::TrySell(int32 Team, int32 HeroIndex, int32 InventoryPos)
{
	AArenaCharacter* C = FindHero(Team, HeroIndex);
	FArenaRespawn* R = C ? nullptr : Respawns.FindByPredicate([&](const FArenaRespawn& X) { return X.Team == Team && X.Hero == HeroIndex; });
	if ((!C || !InShop(C)) && !R) { return false; }
	TArray<int32> Items = C ? C->Items : R->Items;
	if (!Items.IsValidIndex(InventoryPos)) { return false; }
	const int32 Item = Items[InventoryPos];
	const int32 Value = ArenaCore::SellValue(Item, FArenaDatabase::Recipes());
	Items.RemoveAt(InventoryPos);
	if (C) { C->Gold += Value; C->SetItems(Items); } else { R->Gold += Value; R->Items = Items; }
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=sell hero=%s team=%d item=%s value=%d"), GetWorld()->GetTimeSeconds(), *FArenaDatabase::Get().Heroes[HeroIndex].Id.ToString(), Team, *FArenaDatabase::Get().Items[Item].Id.ToString(), Value);
	return true;
}

void AArenaGameMode::BuyBuild(int32 Team, int32 HeroIndex)
{
	// MOBA shopping: the next item of the build whole when the purse allows, else its priciest affordable part
	const FArenaHeroDef& D = FArenaDatabase::Get().Heroes[HeroIndex];
	const TArray<ArenaCore::FRecipe>& Db = FArenaDatabase::Recipes();
	for (int32 Guard = 0; Guard < 6; ++Guard)
	{
		const AArenaCharacter* C = FindHero(Team, HeroIndex);
		const FArenaRespawn* R = C ? nullptr : PendingRespawn(Team, HeroIndex);
		const TArray<int32> Owned = InventoryOf(Team, HeroIndex);
		const float Gold = C ? C->Gold : (R ? R->Gold : 0.f);
		int32 Target = INDEX_NONE;
		for (const FName& B : D.Build) { const int32 I = FArenaDatabase::ItemIndex(B); if (I != INDEX_NONE && !Owned.Contains(I)) { Target = I; break; } }
		if (Target == INDEX_NONE) { return; }
		TArray<int32> Used;
		int32 Buy = INDEX_NONE;
		const int32 Full = ArenaCore::BuyPrice(Target, Db, Owned, Used);
		if (Full <= Gold && Owned.Num() - Used.Num() < ArenaCore::InventorySlots) { Buy = Target; }
		else
		{
			// every part in the target's tree that would be used up by the target once owned
			TArray<int32> Parts;
			TFunction<void(int32, int32)> Collect = [&](int32 I, int32 Depth) { if (Depth > 6 || !Db.IsValidIndex(I)) { return; } for (int32 P : Db[I].From) { Parts.Add(P); Collect(P, Depth + 1); } };
			Collect(Target, 0);
			int32 Best = -1;
			for (int32 P : Parts)
			{
				TArray<int32> PartUsed;
				const int32 PartPrice = ArenaCore::BuyPrice(P, Db, Owned, PartUsed);
				if (PartPrice > Gold || Owned.Num() - PartUsed.Num() >= ArenaCore::InventorySlots) { continue; }
				TArray<int32> Test = Owned;
				for (int32 K = PartUsed.Num() - 1; K >= 0; --K) { Test.RemoveAt(PartUsed[K]); }
				Test.Add(P);
				TArray<int32> After;
				ArenaCore::BuyPrice(Target, Db, Test, After);
				if (!After.Contains(Test.Num() - 1)) { continue; }          // the target would not use it: not needed
				const int32 Value = ArenaCore::TotalCost(P, Db);
				if (Value > Best) { Best = Value; Buy = P; }
			}
		}
		if (Buy == INDEX_NONE || TryBuy(Team, HeroIndex, Buy) != ArenaCore::EBuyResult::Ok) { return; }
	}
}

void AArenaGameMode::BotEconomy(float Now)
{
	// bots shop at their fountain or while dead, and pay to come back when the wait is long and the purse is full
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
	{
		if (W.IsValid() && !W->bAssistedAim && InShop(W.Get()))
		{
			// every hero a bot plays (also the watched one in a bot match): two health potions, then the build
			while (W->Potions[0] < 2 && W->Gold >= Rules.PotionCost + 200 && TryBuyPotion(W->GetTeam(), W->HeroIndex, 0) == ArenaCore::EBuyResult::Ok) {}
			BuyBuild(W->GetTeam(), W->HeroIndex);
		}
	}
	TArray<FArenaRespawn> Pending = Respawns;
	for (const FArenaRespawn& R : Pending)
	{
		if (R.bPlayer && !bBotMatch) { continue; }
		BuyBuild(R.Team, R.Hero);
		const FArenaRespawn* Now2 = PendingRespawn(R.Team, R.Hero);
		if (Now2 && Now2->At - Now > 8.f && Now2->Gold >= ReviveCostFor(Now2->Level) + 600.f) { TryRevive(R.Team, R.Hero); }
	}
}

float AArenaGameMode::TimeLeft() const
{
	if (Phase == EArenaPhase::HeroSelect || Phase == EArenaPhase::Countdown) { return MatchLength; }
	const float End = Phase == EArenaPhase::Ended ? PhaseStart : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.f, MatchLength - (End - MatchStart));
}

void AArenaGameMode::SetMatchMinutes(int32 Minutes)
{
	if (Phase != EArenaPhase::HeroSelect) { return; }
	MatchMinutes = FMath::Clamp(Minutes, 1, 60);
	MatchLength = MatchMinutes * 60.f;
	Score.Rules.Limit = ArenaCore::ScoreLimitFor(MatchMinutes, Rules.ScorePerMinute);
}

void AArenaGameMode::CheckWinConditions(float Now)
{
	if (Phase != EArenaPhase::Playing) { return; }
	if (bConquest)
	{
		for (const FStructure& St : Structures) { if (St.Kind == 3 && !St.bAlive) { EndMatch(1 - St.Team); return; } }
		if (Now - MatchStart >= MatchLength) { EndMatch(ArenaConquest::TimeoutWinner(StructuresLeft(0), StructuresLeft(1), Score.Points[0], Score.Points[1])); }
		return;
	}
	if (Score.LimitReached()) { EndMatch(Score.Winner(false)); return; }
	if (Now - MatchStart < MatchLength) { return; }
	const int32 W = Score.Winner(true);
	if (W != ArenaCore::NoTeam) { EndMatch(W); return; }
	if (!bOvertime)
	{
		bOvertime = true;
		OvertimeStart = Now;
		Announce(this, TEXT("OVERTIME! Next point wins"), true, FLinearColor(1.f, 0.85f, 0.2f));
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=overtime score=%d/%d"), Now, Score.Points[0], Score.Points[1]);
	}
	else if (bBotMatch && Now - OvertimeStart > 30.f) { EndMatch(ArenaCore::NoTeam); }   // unattended runs end in a draw
}

void AArenaGameMode::MinionReachedPortal(AArenaCharacter* Minion, int32 PortalTeam)
{
	if (!Minion || Phase != EArenaPhase::Playing) { return; }
	Score.Award(ArenaCore::EScoreEvent::MinionReachedBase, 1 - PortalTeam);
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=portal team=%d score=%d/%d"), GetWorld()->GetTimeSeconds(), PortalTeam, Score.Points[0], Score.Points[1]);
	Minion->Destroy();
	CheckWinConditions(GetWorld()->GetTimeSeconds());
}

void AArenaGameMode::OnOrbTaken(AArenaCharacter* Taker)
{
	Orb = nullptr;
	NextOrb = GetWorld()->GetTimeSeconds() + 60.f;
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=orb_taken hero=%s team=%d"), GetWorld()->GetTimeSeconds(), Taker ? *Taker->GetDef().Id.ToString() : TEXT("none"), Taker ? Taker->GetTeam() : -1);
}

bool AArenaGameMode::OrbLocation(FVector& Out) const
{
	if (!Orb.IsValid()) { return false; }
	Out = Orb->GetActorLocation();
	return true;
}

void AArenaGameMode::EndMatch(int32 Winner)
{
	if (Phase == EArenaPhase::Ended) { return; }
	Phase = EArenaPhase::Ended;
	WinnerTeam = Winner;
	PhaseStart = GetWorld()->GetTimeSeconds();
	if (Winner >= 0) { AnnounceFor(Winner, TEXT("VICTORY!"), FLinearColor(1.f, 0.8f, 0.2f), TEXT("DEFEAT"), FLinearColor(0.8f, 0.1f, 0.1f), true); }
	else { Announce(this, TEXT("DRAW"), true, FLinearColor(0.8f, 0.8f, 0.8f)); }
	// every human's hero speaks its line (on its player's machine)
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		if (It->PlayerIndex >= 0 && !It->IsMinion()) { It->NextVoiceAt = 0.f; It->Voice(Winner < 0 ? TEXT("Status_Defeat") : (It->GetTeam() == Winner ? TEXT("Status_Victory") : TEXT("Status_Defeat")), 0.f); }
	}
	for (FArenaRespawn& R : Respawns) { GetWorldTimerManager().ClearTimer(R.Timer); }
	// everyone stops where they stand; the winners celebrate with their emote
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive()) { continue; }
		if (AAIController* AI = Cast<AAIController>(It->GetController())) { AI->StopMovement(); }
		It->GetCharacterMovement()->StopMovementImmediately();
		if (!It->IsMinion() && It->GetTeam() == Winner) { It->PlayVictory(); }
	}
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=victory winner=%d score=%d/%d"), GetWorld()->GetTimeSeconds(), Winner, Score.Points[0], Score.Points[1]);
	if (!bBotMatch && FArenaSettings::Get().bTutorial) { FArenaSettings::Get().bTutorial = false; FArenaSettings::Get().Save(); }
	WriteSummaryAndMaybeQuit();
}

void AArenaGameMode::WriteSummaryAndMaybeQuit()
{
	int32 Casts = 0, All4 = 0, HeroCount = 0, Stuck = 0;
	TMap<int32, AArenaCharacter*> Unique;   // (team, hero) -> newest instance (dead bodies linger during ragdoll)
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsMinion()) { continue; }
		AArenaCharacter*& Slot = Unique.FindOrAdd(It->GetTeam() * 100 + It->HeroIndex);
		if (!Slot || (It->IsAlive() && !Slot->IsAlive()) || It->CastCount > Slot->CastCount) { Slot = *It; }
	}
	for (const TPair<int32, AArenaCharacter*>& P : Unique)
	{
		const AArenaCharacter* H = P.Value;
		++HeroCount;
		for (int32 s = 0; s < 5; ++s) { Casts += H->SlotCasts[s]; }
		All4 += (H->SlotCasts[1] > 0 && H->SlotCasts[2] > 0 && H->SlotCasts[3] > 0 && H->SlotCasts[4] > 0) ? 1 : 0;
		UE_LOG(LogArena, Display, TEXT("ARENA_HERO id=%s team=%d level=%d k=%d d=%d a=%d casts=%d,%d,%d,%d,%d"), *H->GetDef().Id.ToString(), H->GetTeam(), H->GetHeroLevel(),
			H->Kills, H->Deaths, H->Assists, H->SlotCasts[0], H->SlotCasts[1], H->SlotCasts[2], H->SlotCasts[3], H->SlotCasts[4]);
	}
	Stuck = StuckTotal;
	const float Dur = GetWorld()->GetTimeSeconds() - MatchStart;
	int32 ItemsBought = 0;
	for (const TPair<int32, AArenaCharacter*>& P : Unique) { ItemsBought += P.Value->Items.Num(); }
	if (bConquest)
	{
		int32 Towers[2] = { 0, 0 }, Inhibs[2] = { 0, 0 };
		for (const FStructure& St : Structures) { if (!St.bAlive) { (St.Kind == 1 ? Towers : Inhibs)[St.Team] += St.Kind <= 2 ? 1 : 0; } }
		UE_LOG(LogArena, Display, TEXT("CONQUEST_SUMMARY winner=%d towers_lost=%d/%d inhibs_lost=%d/%d left=%d/%d boss_team=%d camps=%d struct_dmg_A=%.0f+%.0f struct_dmg_B=%.0f+%.0f"), WinnerTeam, Towers[0], Towers[1], Inhibs[0], Inhibs[1], StructuresLeft(0), StructuresLeft(1), BossTeam, Camps.Num(),
			StructureDamage[0][0], StructureDamage[0][1], StructureDamage[1][0], StructureDamage[1][1]);
	}
	UE_LOG(LogArena, Display, TEXT("DEATH_AUDIT tower_dive=%d outnumbered=%d low_engage=%d fair=%d | bots: tower_dive=%d outnumbered=%d low_engage=%d fair=%d"),
		DeathAudit[0], DeathAudit[1], DeathAudit[2], DeathAudit[3], DeathAuditBots[0], DeathAuditBots[1], DeathAuditBots[2], DeathAuditBots[3]);
	UE_LOG(LogArena, Display, TEXT("ARENA_BOTS jumps=%d"), AArenaBotController::Jumps);
	UE_LOG(LogArena, Display, TEXT("ARENA_SUMMARY seed=%d duration=%.0f score=%d/%d limit=%d winner=%d heroes=%d casts=%d all4=%d stuck=%d items=%d yaw_snaps=%d/%d"),
		Seed, Dur, Score.Points[0], Score.Points[1], Score.Rules.Limit, WinnerTeam, HeroCount, Casts, All4, Stuck, ItemsBought, AArenaCharacter::YawSnaps, AArenaCharacter::YawFrames);
	// per hero, for the balance analysis (melee against ranged, v17)
	if (const AArenaGameState* SumGS = GetGameState<AArenaGameState>())
	{
		const TArray<FArenaHeroDef>& Defs = FArenaDatabase::Get().Heroes;
		for (const FArenaHeroStat& St : SumGS->HeroStats)
		{
			if (!Defs.IsValidIndex(St.Hero)) { continue; }
			const bool bMeleeKit = Defs[St.Hero].Abilities.IsValidIndex(0) && Defs[St.Hero].Abilities[0].Range < 5.f;
			UE_LOG(LogArena, Display, TEXT("ARENA_HERO team=%d hero=%s melee=%d level=%d k=%d d=%d a=%d dmg=%.0f taken=%.0f gold=%.0f minions=%d"), St.Team, *Defs[St.Hero].Id.ToString(), bMeleeKit ? 1 : 0,
				St.Level, St.Kills, St.Deaths, St.Assists, St.DamageToHeroes, St.DamageTaken, St.Gold, St.MinionKills);
		}
	}
	if (FrameMs.Num() > 30)
	{
		TArray<float> Sorted = FrameMs;
		Sorted.Sort();
		double Sum = 0.0; for (float Ms : Sorted) { Sum += Ms; }
		const float Avg = Sum / Sorted.Num(), P99 = Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt(Sorted.Num() * 0.99f))];
		const float P95 = Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt(Sorted.Num() * 0.95f))];
		UE_LOG(LogArena, Display, TEXT("ARENA_PERF frames=%d avg_fps=%.1f p1_low_fps=%.1f p95_ms=%.2f p99_ms=%.2f worst_ms=%.1f res=%dx%d"), Sorted.Num(), 1000.f / FMath::Max(0.01f, Avg), 1000.f / FMath::Max(0.01f, P99), P95, P99, Sorted.Last(),
			GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY().X : 0, GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY().Y : 0);
		// the Shipping exe writes no log: the numbers also go to Saved/ArenaPerf.txt for its checks
		const FString Line = FString::Printf(TEXT("seed=%d duration=%.0f frames=%d avg_fps=%.1f p1_low_fps=%.1f p95_ms=%.2f worst_ms=%.1f hitches=%d winner=%d score=%d/%d all4=%d preload_ms=%.0f\n"),
			Seed, GetWorld()->GetTimeSeconds() - MatchStart, Sorted.Num(), 1000.f / FMath::Max(0.01f, Avg), 1000.f / FMath::Max(0.01f, P99), P95, Sorted.Last(), Hitches, WinnerTeam, Score.Points[0], Score.Points[1], All4, PreloadMs);
		FFileHelper::SaveStringToFile(Line, *(FPaths::ProjectSavedDir() / TEXT("ArenaPerf.txt")), FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}
	// the ultimate opens at level 5 (~1-2 min in): a short match needs 8 of 10 heroes with all four, a long one all of them
	const float Played = GetWorld()->GetTimeSeconds() - MatchStart;
	const int32 Need = Played >= 540.f ? HeroCount : FMath::CeilToInt(HeroCount * 0.8f);
	UE_LOG(LogArena, Display, TEXT("%s VR-09 heroes that cast all 4 abilities: %d/%d (need %d)"), All4 >= Need && HeroCount > 0 ? TEXT("PASS") : TEXT("FAIL"), All4, HeroCount, Need);
	UE_LOG(LogArena, Display, TEXT("%s VR-08 stuck episodes > 3 s: %d"), Stuck == 0 ? TEXT("PASS") : TEXT("FAIL"), Stuck);
	UE_LOG(LogArena, Display, TEXT("%s GS-08 distinct heroes in the match: %d/10"), HeroCount == 10 ? TEXT("PASS") : TEXT("FAIL"), HeroCount);
	if (bBotMatch)
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); }), bUIDemo ? 3.f : 1.f, false);
	}
}

void AArenaGameMode::UIShot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("%s.png"), Name), true, false);
	PerfSkipUntil = GetWorld()->GetTimeSeconds() + 0.7f;
	UE_LOG(LogArena, Display, TEXT("ARENA evt=ui_shot name=%s"), Name);
}

void AArenaGameMode::TickUIDemo(float Now)
{
	AArenaPlayerController* PC = Cast<AArenaPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC) { return; }
	const float T = Now - MatchStart;
	// the front end first: main menu, the play screen, the hero browser previewing the ultimate, the settings
	if (UIFrontStep < 99 && Phase == EArenaPhase::HeroSelect)
	{
		const int32 N = FMath::Max(1, FArenaDatabase::Get().Heroes.Num());
		switch (UIFrontStep)
		{
		case 0: if (Now > 2.f) { PC->OpenMenu(EArenaMenu::Main); ++UIFrontStep; } break;
		case 1: if (Now > 2.8f) { UIShot(TEXT("UI_00_Menu")); ++UIFrontStep; } break;
		case 2: if (Now > 3.2f) { PC->MenuAction(TEXT("Play"), 0); ++UIFrontStep; } break;
		case 3: if (Now > 3.8f) { UIShot(TEXT("UI_08_Play")); ++UIFrontStep; } break;
		case 4: if (Now > 4.2f) { PC->MenuAction(TEXT("Heroes"), 0); PC->MenuAction(TEXT("BrowseHero"), Seed % N); ++UIFrontStep; } break;
		case 5: if (Now > 5.f) { PC->MenuAction(TEXT("BrowseSlot"), 4); ++UIFrontStep; } break;
		case 6: if (Now > 6.1f) { UIShot(TEXT("UI_09_Heroes")); ++UIFrontStep; } break;
		case 7: if (Now > 6.5f) { PC->MenuAction(TEXT("Back"), 0); PC->MenuAction(TEXT("Settings"), 0); ++UIFrontStep; } break;
		case 8: if (Now > 7.1f) { UIShot(TEXT("UI_10_Settings")); ++UIFrontStep; } break;
		case 9: if (Now > 7.5f) { PC->OpenMenu(EArenaMenu::HeroPick); UIFrontDoneAt = Now; UIFrontStep = 99; } break;
		default: break;
		}
		return;
	}
	switch (UIDemoStep)
	{
	case 0: if (Phase == EArenaPhase::HeroSelect && Now > UIFrontDoneAt + 0.6f) { UIShot(TEXT("UI_01_HeroSelect")); ++UIDemoStep; } break;
	case 1: if (Now > UIFrontDoneAt + 1.4f) { PC->OpenMenu(EArenaMenu::None); PlayerPickHero(Seed % FMath::Max(1, FArenaDatabase::Get().Heroes.Num())); ++UIDemoStep; } break;
	case 2: if (Phase == EArenaPhase::Playing && T > 14.f) { UIShot(TEXT("UI_02_Match")); ++UIDemoStep; } break;
	case 3: if (T > 20.f) { PC->SetShopOpen(true); ++UIDemoStep; } break;
	case 4: if (T > 20.8f) { UIShot(TEXT("UI_03_Shop")); ++UIDemoStep; } break;
	case 5: if (T > 21.5f) { PC->SetShopOpen(false); PC->bScoreboard = true; ++UIDemoStep; } break;
	case 6: if (T > 22.2f) { UIShot(TEXT("UI_04_Scoreboard")); ++UIDemoStep; } break;
	case 7:
		if (T > 23.f)
		{
			PC->bScoreboard = false;
			PC->DemoPauseClicks({ FName(TEXT("Settings")), FName(TEXT("Back")), FName(TEXT("Resume")) });
			++UIDemoStep;
		}
		break;
	case 8:
		// the player aims its ultimate: the hero is taken over by the player controller for a moment
		if (T > 24.f && !PC->IsDemoClicking())
		{
			// waits for the hero to be alive (it may be respawning), gives up after a minute
			AArenaCharacter* H = FindHero(0, PlayerHeroIndex);
			if (H && !H->IsBusy()) { UIDemoBot = H->GetController(); PC->Possess(H); PC->BeginAim(H->GetRank(4) > 0 ? 4 : 2); UIDemoAimAt = T; ++UIDemoStep; }
			else if (T > 85.f) { UIDemoStep = 11; }
		}
		break;
	case 9: if (T > UIDemoAimAt + 1.2f) { UIShot(TEXT("UI_07_Aim")); ++UIDemoStep; } break;
	case 10:
		if (T > UIDemoAimAt + 1.6f)
		{
			PC->CancelAim();
			if (AArenaCharacter* H = Cast<AArenaCharacter>(PC->GetPawn())) { PC->UnPossess(); if (UIDemoBot.IsValid()) { UIDemoBot->Possess(H); } PC->SetViewTarget(H); }
			++UIDemoStep;
		}
		break;
	case 11: if (PendingRespawn(0, PlayerHeroIndex) && T > 30.f) { ++UIDemoStep; } else if (T > 150.f || Phase == EArenaPhase::Ended) { UIDemoStep = 13; } break;
	case 12: { const FArenaRespawn* R = PendingRespawn(0, PlayerHeroIndex); if (R && Now > R->At - (ArenaCore::RespawnSeconds(R->Level) - 1.5f)) { UIShot(TEXT("UI_05_Death")); ++UIDemoStep; } else if (!R) { UIDemoStep = 11; } } break;
	case 13: if (Phase == EArenaPhase::Ended && Now > PhaseStart + 1.5f) { UIShot(TEXT("UI_06_End")); ++UIDemoStep; } break;
	default: break;
	}
}

void AArenaGameMode::AddPing(int32 Team, int32 HeroIndex, const FVector& At)
{
	AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (!GS || Phase != EArenaPhase::Playing) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	// one ping a second per hero (no spam)
	for (const FArenaPingRep& P : GS->Pings) { if (P.Team == Team && P.Hero == HeroIndex && Now - P.Time < 1.f) { return; } }
	GS->Pings.RemoveAll([Now](const FArenaPingRep& P) { return Now - P.Time > 10.f; });
	FArenaPingRep& P = GS->Pings.AddDefaulted_GetRef();
	P.Team = (uint8)Team; P.Hero = HeroIndex; P.Pos = At; P.Time = Now;
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=ping team=%d hero=%d at=%s"), Now, Team, HeroIndex, *At.ToCompactString());
}

bool AArenaGameMode::PingFor(int32 Team, FVector& Out) const
{
	const AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (!GS) { return false; }
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = GS->Pings.Num() - 1; i >= 0; --i)
	{
		if (GS->Pings[i].Team == Team && Now - GS->Pings[i].Time <= 10.f) { Out = GS->Pings[i].Pos; return true; }
	}
	return false;
}

void AArenaGameMode::MirrorFromState()
{
	// a LAN client: the HUD reads this object; everything comes from the replicated state
	const AArenaGameState* GS = GetGameState<AArenaGameState>();
	if (!GS) { GS = AArenaGameState::Get(this); }
	if (!GS) { return; }
	// the server's clock in this world's: every time the HUD compares with its own world time
	const float Off = GetWorld()->GetTimeSeconds() - GS->GetServerWorldTimeSeconds();
	Phase = GS->Phase; PhaseStart = GS->PhaseStart + Off; MatchStart = GS->MatchStart + Off; MatchLength = GS->MatchLength;
	bOvertime = GS->bOvertime; WinnerTeam = GS->WinnerTeam; bConquest = GS->bConquest;
	Score.Points[0] = GS->ScoreA; Score.Points[1] = GS->ScoreB; Score.Rules.Limit = GS->ScoreLimit;
	TeamSize = GS->TeamSize; MatchMinutes = GS->MatchMinutes; Difficulty = GS->Difficulty;
	// the local player's hero
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const AArenaCharacter* Mine = Cast<AArenaCharacter>(PC->GetPawn())) { if (Mine->HeroIndex >= 0) { PlayerHeroIndex = Mine->HeroIndex; } }
	}
	Heroes.Reset();
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { if (It->IsAlive() && !It->IsMinion() && It->HeroIndex >= 0) { Heroes.Add(*It); } }
	// the dead heroes: their respawn records as the HUD knows them
	Respawns.Reset();
	for (const FArenaHeroStat& St : GS->HeroStats)
	{
		ReviveReadyAt.Add(St.Team * 100 + St.Hero, St.ReviveReadyAt + Off);
		if (St.bAlive) { continue; }
		FArenaRespawn& R = Respawns.AddDefaulted_GetRef();
		R.Team = St.Team; R.Hero = St.Hero; R.At = St.RespawnAt + Off; R.Level = St.Level; R.Gold = St.Gold; R.Items = St.Items; R.Killer = St.KilledBy; R.bPlayer = St.bPlayer;
		R.Kills = St.Kills; R.Deaths = St.Deaths; R.Assists = St.Assists;
	}
	Feed.Reset();
	for (const FArenaFeedRep& F : GS->Feed) { FArenaFeedEntry& E = Feed.AddDefaulted_GetRef(); E.Text = F.Text; E.Time = F.Time + Off; E.Color = F.Color; E.bBig = F.bBig; E.Team = F.Team; E.Other = F.Other; E.OtherColor = F.OtherColor; }
	Kills.Reset();
	for (const FArenaKillRep& K : GS->KillFeed)
	{
		FArenaKill& E = Kills.AddDefaulted_GetRef();
		E.Time = K.Time + Off; E.KillerHero = K.KillerHero; E.KillerTeam = K.KillerTeam; E.bKillerMinion = K.bKillerMinion; E.VictimHero = K.VictimHero; E.VictimTeam = K.VictimTeam;
	}
	if (GS->bOrb)
	{
		if (!Orb.IsValid()) { for (TActorIterator<AArenaPowerOrb> It(GetWorld()); It; ++It) { Orb = *It; } }
		OrbSpot = GS->OrbPos;
	}
	else { Orb = nullptr; }
}

void AArenaGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (!NewPlayer || NewPlayer->IsLocalController() || bClientMirror) { return; }
	// the teams alternate: the first guest plays against the host, the next one with the host, ... (a tie went to
	// team 1: two guests both played against a lone host) and a full team is skipped
	int32 OnTeam[2] = { 1, 0 };
	for (const FHuman& H : RemoteHumans) { if (H.PC.IsValid()) { ++OnTeam[FMath::Clamp(H.Team, 0, 1)]; } }
	FHuman& H = RemoteHumans.AddDefaulted_GetRef();
	H.PC = NewPlayer;
	H.Team = OnTeam[1] < OnTeam[0] ? 1 : 0;
	if (OnTeam[H.Team] >= TeamSize && OnTeam[1 - H.Team] < TeamSize) { H.Team = 1 - H.Team; }
	if (AArenaPlayerController* APC = Cast<AArenaPlayerController>(NewPlayer)) { APC->NetTeam = H.Team; }
	AnnounceFor(H.Team, TEXT("A player joined (your team)"), FLinearColor(0.5f, 0.85f, 1.f), TEXT("A player joined (enemy team)"), FLinearColor(0.5f, 0.85f, 1.f), true);
	UE_LOG(LogArena, Display, TEXT("ARENA evt=net_join team=%d humans=%d phase=%d"), H.Team, RemoteHumans.Num() + 1, (int32)Phase);
	if (Phase == EArenaPhase::Countdown || Phase == EArenaPhase::Playing) { TakeOverBot(NewPlayer, H.Team); }
}

void AArenaGameMode::Logout(AController* Exiting)
{
	if (APlayerController* PC = Cast<APlayerController>(Exiting); PC && !bClientMirror)
	{
		const int32 I = RemoteHumans.IndexOfByPredicate([PC](const FHuman& H) { return H.PC.Get() == PC; });
		if (I != INDEX_NONE)
		{
			// the hero stays in the match, played by a bot again
			if (AArenaCharacter* C = Cast<AArenaCharacter>(PC->GetPawn()); C && C->IsAlive())
			{
				PC->UnPossess();
				AArenaBotController* AI = GetWorld()->SpawnActor<AArenaBotController>();
				AI->Difficulty = Difficulty;
				if (bConquest) { AI->HomeLane = LaneOf(C->GetTeam(), C->HeroIndex); }   // the hero's own lane
				AI->Possess(C);
				if (AI->Difficulty >= 2) { C->SetBotEdge(Rules.HardBotPower, Rules.HardBotHealth); }
				C->PlayerIndex = -1;
			}
			for (FArenaRespawn& R : Respawns) { if (R.Owner.Get() == PC) { R.Owner = nullptr; R.bPlayer = false; } }
			Announce(this, TEXT("A player disconnected: a bot takes over their hero"), false, FLinearColor(1.f, 0.7f, 0.3f));
			RemoteHumans.RemoveAt(I);
		}
	}
	Super::Logout(Exiting);
}

void AArenaGameMode::RemotePickHero(APlayerController* PC, int32 Hero, int32 Skin)
{
	if (Phase != EArenaPhase::HeroSelect) { return; }   // a pick after the start would re-point the guest's shop and revive at another hero
	for (FHuman& H : RemoteHumans)
	{
		if (H.PC.Get() != PC) { continue; }
		H.Hero = FArenaDatabase::Get().Heroes.IsValidIndex(Hero) ? Hero : -1;
		H.Skin = Skin;
		UE_LOG(LogArena, Display, TEXT("ARENA evt=net_pick team=%d hero=%d"), H.Team, H.Hero);
		Announce(this, FString::Printf(TEXT("Player picked: %s"), FArenaDatabase::Get().Heroes.IsValidIndex(Hero) ? *FArenaDatabase::Get().Heroes[Hero].DisplayName : TEXT("?")), false, FLinearColor(0.5f, 0.85f, 1.f));
	}
}

bool AArenaGameMode::HumanSlot(const APlayerController* PC, int32& OutTeam, int32& OutHero) const
{
	if (!PC) { return false; }
	if (PC->IsLocalController()) { OutTeam = 0; OutHero = PlayerHeroIndex; return true; }
	for (const FHuman& H : RemoteHumans) { if (H.PC.Get() == PC) { OutTeam = H.Team; OutHero = H.Hero; return true; } }
	return false;
}

void AArenaGameMode::TakeOverBot(APlayerController* PC, int32 Team)
{
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
	{
		AArenaCharacter* C = W.Get();
		if (!C || !C->IsAlive() || C->GetTeam() != Team || C->PlayerIndex >= 0) { continue; }
		if (AController* Old = C->GetController()) { Old->UnPossess(); Old->Destroy(); }
		C->SetBotEdge(0.f, 0.f);
		C->PlayerIndex = 1;
		PC->Possess(C);
		for (FHuman& H : RemoteHumans) { if (H.PC.Get() == PC) { H.Hero = C->HeroIndex; } }
		UE_LOG(LogArena, Display, TEXT("ARENA evt=net_takeover team=%d hero=%s"), Team, *C->GetDef().Id.ToString());
		return;
	}
	// every bot of the team is dead: the guest takes the first one waiting to respawn and comes back with it
	for (FArenaRespawn& R : Respawns)
	{
		if (R.Team != Team || R.bPlayer) { continue; }
		R.bPlayer = true;
		R.Owner = PC;
		for (FHuman& H : RemoteHumans) { if (H.PC.Get() == PC) { H.Hero = R.Hero; } }
		UE_LOG(LogArena, Display, TEXT("ARENA evt=net_takeover_respawn team=%d hero=%d"), Team, R.Hero);
		return;
	}
	UE_LOG(LogArena, Warning, TEXT("ARENA evt=net_takeover_none team=%d"), Team);
}

bool AArenaGameMode::NavReady() const
{
	// the navigation data registered is not enough: its tiles may still be loading (a headless run starts before
	// them); ready when both bases and every lane's middle project onto it
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || !Nav->GetDefaultNavDataInstance() || Nav->IsNavigationBuildInProgress()) { return false; }   // the props' obstacles rebuild tiles at the start
	TArray<FVector> Probe = { BaseA, BaseB };
	for (const TArray<FVector>& L : Lanes) { if (L.Num() > 0) { Probe.Add(L[L.Num() / 2]); } }
	for (const FVector& P : Probe)
	{
		FNavLocation On;
		if (!Nav->ProjectPointToNavigation(P, On, FVector(300.f, 300.f, 900.f))) { return false; }
	}
	return true;
}

void AArenaGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bClientMirror) { MirrorFromState(); return; }
	if (bNetTestHost && GetNetMode() == NM_ListenServer) { TickNetTestHost(GetWorld()->GetTimeSeconds()); }
	if (bConquestPending && (NavReady() || GetWorld()->GetTimeSeconds() > 20.f))
	{
		if (!NavReady()) { UE_LOG(LogArena, Warning, TEXT("ARENA evt=conquest_nav_timeout")); }
		bConquestPending = false;
		StartConquest();
	}
	SyncGameState();
	const float Now = GetWorld()->GetTimeSeconds();
	{
		// -ArenaCmdAt=60:profilegpu|90:stat unit : console commands at set times (GPU profiles of a running match)
		static TArray<TPair<float, FString>> Cmds;
		static bool bParsed = false;
		if (!bParsed)
		{
			bParsed = true;
			FString Arg;
			if (FParse::Value(FCommandLine::Get(), TEXT("ArenaCmdAt="), Arg, false))
			{
				TArray<FString> Items;
				Arg.ParseIntoArray(Items, TEXT("|"));
				for (const FString& It : Items)
				{
					FString At, Cmd;
					if (It.Split(TEXT(":"), &At, &Cmd)) { Cmds.Add(TPair<float, FString>(FCString::Atof(*At), Cmd)); }
				}
			}
		}
		for (int32 i = Cmds.Num() - 1; i >= 0; --i)
		{
			if (Now < Cmds[i].Key) { continue; }
			if (APlayerController* CmdPC = GetWorld()->GetFirstPlayerController()) { CmdPC->ConsoleCommand(Cmds[i].Value); }
			UE_LOG(LogArena, Display, TEXT("ARENA evt=cmd t=%.1f cmd=%s"), Now, *Cmds[i].Value);
			Cmds.RemoveAt(i);
		}
	}
	if (bUIDemo) { TickUIDemo(Now); }
	if (bTraining) { TickTraining(Now, DeltaSeconds); if (bTrainingDemo) { TickTrainingDemo(Now); } return; }
	if (bAnimLab) { if (LabMode == 1) { TickMechLab(Now); } else if (LabMode == 2) { TickCamShots(Now); } else if (LabMode == 3) { TickAimLab(Now); } else if (LabMode == 4) { TickSkillLab(Now); } else if (LabMode == 5) { TickBaseLab(Now); } else if (LabMode == 7) { TickFxLab(Now); } else if (LabMode == 8) { TickConquestLab(Now, DeltaSeconds); } else if (LabMode == 9) { TickDuelLab(Now); } else if (LabMode == 10) { TickSkinDeathLab(Now); } else if (LabMode == 11) { TickLocoLab(Now); } else { TickAnimLab(Now); } return; }
	if (Phase == EArenaPhase::Countdown && Now - PhaseStart >= 3.f)
	{
		Phase = EArenaPhase::Playing;
		MatchStart = Now;
		for (const TWeakObjectPtr<AArenaCharacter>& H : Heroes) { if (H.IsValid()) { H->StopIntro(); } }
		NextWave = Now + 5.f;
		NextShot = 6.f;
		NextOrb = Now + 45.f;
		Announce(this, TEXT("FIGHT!"), true, FLinearColor(1.f, 0.4f, 0.1f));
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=match_start length=%.0f limit=%d"), Now, MatchLength, Score.Rules.Limit);
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance()) : nullptr;
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=nav navdata=%s agent_radius=%.0f"), Now, Nav && Nav->GetDefaultNavDataInstance() ? TEXT("present") : TEXT("MISSING"), Recast ? Recast->AgentRadius : -1.f);
		if (FParse::Param(FCommandLine::Get(), TEXT("ArenaNavCheck"))) { NavCheck(); }
	}
	if (Phase != EArenaPhase::Playing) { return; }
	if (Now >= NextWave) { SpawnWave(); NextWave = Now + FMath::Max(5.f, Rules.WaveInterval); }
	if (bConquest) { TickConquest(Now, DeltaSeconds); }
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes) { if (W.IsValid() && W->IsAlive()) { W->Gold += Rules.GoldPerSecond * DeltaSeconds; W->AddXp(Rules.XpPerSecond * DeltaSeconds); } }
	for (FArenaRespawn& R : Respawns) { R.Gold += Rules.GoldPerSecond * DeltaSeconds; }
	if (Now >= NextEconomy) { NextEconomy = Now + 1.f; BotEconomy(Now); }
	const bool bPerfFrame = Now >= PerfSkipUntil;
	if (FrameMs.Num() < 200000 && bPerfFrame) { FrameMs.Add(FApp::GetDeltaTime() * 1000.f); }
	if (bPerfFrame && FApp::GetDeltaTime() > 0.1 && Now - MatchStart > 1.f && Hitches < 40)
	{
		++Hitches;
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=hitch ms=%.0f"), Now, FApp::GetDeltaTime() * 1000.0);
	}
	if (!bConquest && !Orb.IsValid() && NextOrb > 0.f && Now >= NextOrb)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Orb = GetWorld()->SpawnActor<AArenaPowerOrb>(AArenaPowerOrb::StaticClass(), OrbSpot, FRotator::ZeroRotator, P);
		NextOrb = -1.f;
		Announce(this, TEXT("THE POWER OF TARTARUS has appeared on the altar!"), true, FLinearColor(1.f, 0.45f, 0.1f));
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=orb_spawn"), Now);
	}
	if (TourShotAt > 0.f && Now >= TourShotAt)
	{
		TourShotAt = -1.f;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("ArenaShot_%02d.png"), ShotIndex++), false, false);
		PerfSkipUntil = Now + 0.7f;
	}
	if (bShots && Now - MatchStart >= NextShot)
	{
		APlayerController* TourPC = GetWorld()->GetFirstPlayerController();
		if (TourCams.Num() > 0 && TourPC && TourCams[TourIndex % TourCams.Num()].IsValid())
		{
			TourPC->SetViewTarget(TourCams[TourIndex++ % TourCams.Num()].Get());   // shot taken a moment later, once the view has settled
			TourShotAt = Now + 1.f;
		}
		else { FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("ArenaShot_%02d.png"), ShotIndex++), false, false); }
		PerfSkipUntil = Now + 0.7f;
		NextShot += 15.f;
		UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=screenshot n=%d"), Now, ShotIndex);
	}

	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It)
	{
		AArenaCharacter* C = *It;
		if (!C->IsAlive()) { continue; }
		const int32 Enemy = 1 - C->GetTeam();
		if (!bConquest && C->IsLaneMinion() && FVector::Dist2D(C->GetActorLocation(), TeamBase(Enemy)) < 450.f) { MinionReachedPortal(C, Enemy); continue; }
	}
	CheckWinConditions(Now);
}

bool AArenaGameMode::InEnemyTowerRange(int32 Team, const FVector& At, const AArenaCharacter* Who, bool* bTargetsWho, float Margin) const
{
	if (bTargetsWho) { *bTargetsWho = false; }
	bool bIn = false;
	for (const FStructure& St : Structures)
	{
		const AArenaCharacter* T = St.Unit.Get();
		if (!St.bAlive || !T || St.Team == Team || T->GetDef().Abilities.Num() == 0 || T->GetDef().Abilities[0].Damage <= 0.f) { continue; }
		const float R = T->Ability(0).Range * 100.f + T->GetCapsuleComponent()->GetScaledCapsuleRadius() + Margin;
		if (FVector::Dist2D(At, T->GetActorLocation()) > R) { continue; }
		bIn = true;
		if (bTargetsWho && Who && St.Target.Get() == Who) { *bTargetsWho = true; }
	}
	return bIn;
}

void AArenaGameMode::AuditHeroDeath(AArenaCharacter* Victim, AArenaCharacter* Killer, float Now)
{
	if (!Victim) { return; }
	const int32 VTeam = Victim->GetTeam();
	const FVector At = Victim->GetActorLocation();
	const TCHAR* KillerKind = !Killer ? TEXT("none") : (Killer->IsStructure() ? TEXT("structure") : (Killer->IsMonster() ? TEXT("monster") : (Killer->IsMinion() ? TEXT("minion") : TEXT("hero"))));
	bool bShotByTower = false;
	const bool bInTower = InEnemyTowerRange(VTeam, At, Victim, &bShotByTower, 60.f);
	int32 Enemies = 0, Allies = 0;
	for (const TWeakObjectPtr<AArenaCharacter>& W : Heroes)
	{
		const AArenaCharacter* H = W.Get();
		if (!H || H == Victim || !H->IsAlive() || FVector::Dist2D(H->GetActorLocation(), At) > 1500.f) { continue; }
		(H->GetTeam() == VTeam ? Allies : Enemies) += 1;
	}
	// the health it had 6 s before the death: the damage taken since then (heals ignored)
	float Taken = 0.f;
	for (const FArenaDamageEvent& E : Victim->RecentDamage) { Taken += Now - E.Time <= 6.f ? E.Amount : 0.f; }
	const float Hp6 = FMath::Clamp(Taken / FMath::Max(1.f, Victim->GetMaxHealth()), 0.f, 1.f);
	const AArenaBotController* AI = Cast<AArenaBotController>(Victim->GetController());
	const bool bBot = !Victim->bAssistedAim;
	const bool bRetreating = AI && AI->IsRetreating();
	int32 Cat = 3;
	if ((Killer && Killer->IsStructure()) || (bInTower && bShotByTower)) { Cat = 0; }
	else if (Enemies >= Allies + 2) { Cat = 1; }
	else if (Hp6 < 0.35f && !bRetreating) { Cat = 2; }
	static const TCHAR* Names[] = { TEXT("tower_dive"), TEXT("outnumbered"), TEXT("low_engage"), TEXT("fair") };
	++DeathAudit[Cat];
	if (bBot) { ++DeathAuditBots[Cat]; }
	UE_LOG(LogArena, Display, TEXT("ARENA t=%.1f evt=death_audit victim=%s team=%d bot=%d killer=%s killer_kind=%s in_tower=%d shot_by_tower=%d enemies=%d allies=%d hp6s=%.2f retreat=%d level=%d cat=%s at=%s"),
		Now, *Victim->GetDef().Id.ToString(), VTeam, bBot ? 1 : 0, Killer ? *Killer->GetDef().Id.ToString() : TEXT("-"), KillerKind, bInTower ? 1 : 0, bShotByTower ? 1 : 0,
		Enemies, Allies, Hp6, bRetreating ? 1 : 0, Victim->GetHeroLevel(), Names[Cat], *At.ToCompactString());
}
