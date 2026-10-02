#include "UI/ArenaIconStudio.h"
#include "Data/ArenaTypes.h"
#include "Heroes/ArenaCharacter.h"
#include "Arena/ArenaScatter.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystem.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Arena/ArenaFx.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "TextureResource.h"
#include "PipelineStateCache.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Materials/Material.h"
#include "MaterialShared.h"

static TAutoConsoleVariable<float> CVarPortraitExposure(TEXT("arena.IconPortraitExposure"), 0.4f, TEXT("Exposure bias (EV) of the portrait and ability icon captures (0.4: with the fill light a face at +1 EV burned out)."));
static TAutoConsoleVariable<float> CVarMapExposure(TEXT("arena.IconMapExposure"), 3.f, TEXT("Exposure bias (EV) of the minimap capture."));
static TAutoConsoleVariable<float> CVarLight(TEXT("arena.IconLight"), 60.f, TEXT("Intensity (candela) of the booth key light."));

namespace
{
	constexpr int32 PortraitSize = 1024, IconSize = 256, MapW = 1280, MapH = 800;   // high end: sharp on 1440p / 4K (portraits up to 300 px, icons up to 128 px); mips for the small ones
	constexpr float MapOrthoDefault = 15600.f;
	/** The capture's width: the navigation bounds' footprint at the minimap's aspect (a map bigger than the Arena
	 *  was cut off at 156 m). */
	float MapOrthoFor(UWorld* W)
	{
		for (TActorIterator<ANavMeshBoundsVolume> It(W); It; ++It)
		{
			const FBox B = It->GetComponentsBoundingBox(true);
			if (B.IsValid) { return FMath::Max(B.GetSize().X, B.GetSize().Y * float(MapW) / float(MapH)) * 1.02f; }
		}
		return MapOrthoDefault;
	}
	constexpr float BoothSpacing = 2500.f;

	UTextureRenderTarget2D* NewTarget(UObject* Outer, int32 W, int32 H, bool bMips = false)
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Outer);
		RT->RenderTargetFormat = RTF_RGBA8;
		RT->ClearColor = FLinearColor::Black;
		RT->bAutoGenerateMips = bMips;
		RT->InitAutoFormat(W, H);
		RT->UpdateResourceImmediate(true);
		return RT;
	}
}

AArenaIconStudio::AArenaIconStudio()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AArenaIconStudio* AArenaIconStudio::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) { return nullptr; }
	for (TActorIterator<AArenaIconStudio> It(World); It; ++It) { return *It; }
	return nullptr;
}

FVector2D AArenaIconStudio::MinimapUV(const FVector& World) const
{
	if (MiniOrthoW <= 0.f || MiniOrthoH <= 0.f) { return FVector2D(0.5f, 0.5f); }
	const FVector D = World - MiniCam;
	return FVector2D(0.5f + FVector::DotProduct(D, MiniRight) / MiniOrthoW, 0.5f - FVector::DotProduct(D, MiniUp) / MiniOrthoH);
}

USceneCaptureComponent2D* AArenaIconStudio::MakeCapture(UTextureRenderTarget2D* Target, float Fov, float ExposureBias)
{
	USceneCaptureComponent2D* C = NewObject<USceneCaptureComponent2D>(this);
	C->SetupAttachment(GetRootComponent());
	C->bCaptureEveryFrame = false;
	C->bCaptureOnMovement = false;
	C->bAlwaysPersistRenderingState = true;
	C->TextureTarget = Target;
	C->FOVAngle = Fov;
	C->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	C->ShowFlags.SetAtmosphere(false);
	C->ShowFlags.SetFog(false);
	C->ShowFlags.SetVolumetricFog(false);
	C->ShowFlags.SetMotionBlur(false);
	FPostProcessSettings& PP = C->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = 0;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = ExposureBias;
	C->PostProcessBlendWeight = 1.f;
	C->RegisterComponent();
	Captures.Add(C);
	return C;
}

AStaticMeshActor* AArenaIconStudio::SpawnBackdrop(const FVector& At, const FVector& Facing, float Size, const FLinearColor& Tint)
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_ArenaBackdrop.M_ArenaBackdrop"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Plane || !Mat) { return nullptr; }
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(At, FRotationMatrix::MakeFromZ(Facing - At).Rotator(), P);
	if (!A) { return nullptr; }
	UStaticMeshComponent* M = A->GetStaticMeshComponent();
	M->SetMobility(EComponentMobility::Movable);
	M->SetStaticMesh(Plane);
	M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	M->SetCastShadow(false);
	A->SetActorScale3D(FVector(Size));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mat, A);
	MID->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(Tint, FLinearColor(0.55f, 0.6f, 0.75f), 0.35f));
	M->SetMaterial(0, MID);
	Backdrops.Add(A);
	return A;
}

ASkeletalMeshActor* AArenaIconStudio::SpawnBooth(int32 HeroIndex, const FVector& At, const FString& AnimPath, float Frame, const FLinearColor& Rim, bool bPortraitLights)
{
	const FArenaHeroDef& D = FArenaDatabase::Get().Heroes[HeroIndex];
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASkeletalMeshActor* A = GetWorld()->SpawnActor<ASkeletalMeshActor>(At, FRotator::ZeroRotator, P);
	if (!A) { return nullptr; }
	USkeletalMeshComponent* M = A->GetSkeletalMeshComponent();
	M->SetMobility(EComponentMobility::Movable);
	M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	M->SetCanEverAffectNavigation(false);
	if (USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *D.Mesh)) { M->SetSkeletalMesh(SK); }
	M->SetRelativeRotation(FRotator(0.f, D.MeshYaw, 0.f));
	M->SetRelativeScale3D(FVector(D.Scale));
	M->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (UAnimSequenceBase* Anim = AnimPath.IsEmpty() ? nullptr : LoadObject<UAnimSequenceBase>(nullptr, *AnimPath))
	{
		M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		M->SetAnimation(Anim);
		M->Play(Frame < 0.f);                                               // portraits loop; ability shots freeze below
		if (Frame >= 0.f) { M->SetPosition(Anim->GetPlayLength() * Frame, false); M->SetPlayRate(0.f); }
	}
	// key light from the camera side, a coloured rim from behind, and for portraits a cool fill from the other side
	// of the camera (the shadow side of the face was black)
	for (int32 L = 0; L < (bPortraitLights ? 3 : 2); ++L)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(GetRootComponent());
		Light->SetWorldLocation(At + (L == 0 ? FVector(240.f, -160.f, 230.f) : (L == 1 ? FVector(-200.f, 170.f, 240.f) : FVector(220.f, 200.f, 190.f))));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(CVarLight.GetValueOnGameThread() * (L == 0 ? (bPortraitLights ? D.PortraitLight : 0.8f) : (L == 1 ? (bPortraitLights ? 0.9f : 1.6f) : 0.35f * (bPortraitLights ? FMath::Max(1.f, D.PortraitLight) : 1.f))));
		Light->SetAttenuationRadius(900.f);
		Light->SetCastShadows(false);
		Light->SetLightColor(L == 0 ? FLinearColor(1.f, 0.93f, 0.84f) : (L == 1 ? Rim : FLinearColor(0.7f, 0.8f, 1.f)));
		Light->RegisterComponent();
		Lights.Add(Light);
	}
	return A;
}

void AArenaIconStudio::BeginPlay()
{
	Super::BeginPlay();
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	for (int32 H = 0; H < Heroes.Num(); ++H)
	{
		const FArenaHeroDef& D = Heroes[H];
		Portraits.Add(NewTarget(this, PortraitSize, PortraitSize, true));
		PortraitBooths.Add(SpawnBooth(H, Origin + FVector(H * BoothSpacing, 0.f, 0.f), D.PortraitPose.IsEmpty() ? D.PortraitAnim : D.PortraitPose, -1.f, FArenaDatabase::Hex(D.Tint), true));
		// the backdrop exists before the shot (a mesh or material still compiling at the moment of the capture is not
		// drawn); Shoot puts it exactly behind the head
		{
			const float Sc = FMath::Max(0.5f, D.Scale);
			const FVector Head = Origin + FVector(H * BoothSpacing, 0.f, 165.f * Sc);
			const FVector Eye = Head + FVector(145.f, -40.f, 6.f) * Sc;
			SpawnBackdrop(Head + (Head - Eye).GetSafeNormal() * 170.f * Sc, Eye, 3.2f * Sc, FArenaDatabase::Hex(D.Tint));
		}
		for (int32 S = 0; S < 5; ++S)
		{
			Icons.Add(NewTarget(this, IconSize, IconSize));
			const FArenaAbilityDef* Ab = D.Abilities.IsValidIndex(S) ? &D.Abilities[S] : nullptr;
			const FString Anim = !Ab ? D.PortraitAnim : (Ab->AnimVariants.Num() > 0 ? Ab->AnimVariants[0] : (!Ab->Anim.IsEmpty() ? Ab->Anim : D.PortraitAnim));
			const float Frame = Ab && Anim != D.PortraitAnim ? 0.38f : -1.f;   // the wind-up / strike moment
			IconBooths.Add(SpawnBooth(H, Origin + FVector(H * BoothSpacing, (S + 1) * BoothSpacing, 0.f), Anim, Frame, Ab ? FArenaDatabase::Hex(Ab->Color) : FLinearColor::White, false));
		}
	}
	MinimapRT = NewTarget(this, MapW, MapH);
	// from 0.9 s (poses evaluated, bounds settled), as soon as nothing is left to compile
	WaitStart = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(WaitTimer, this, &AArenaIconStudio::TryShoot, 0.25f, true, 0.9f);
}

void AArenaIconStudio::TryShoot()
{
	const float Waited = GetWorld()->GetTimeSeconds() - WaitStart;
	int32 Pending = static_cast<int32>(PipelineStateCache::NumActivePrecacheRequests());
	for (const ASkeletalMeshActor* A : PortraitBooths) { if (A && A->GetSkeletalMeshComponent()->IsPSOPrecaching()) { ++Pending; } }
	for (const ASkeletalMeshActor* A : IconBooths) { if (A && A->GetSkeletalMeshComponent()->IsPSOPrecaching()) { ++Pending; } }
		// the portrait backdrops too: a shot taken while their material compiled showed the engine's grey checker
		for (const TObjectPtr<AStaticMeshActor>& B : Backdrops)
		{
			const UStaticMeshComponent* SM = B ? B->GetStaticMeshComponent() : nullptr;
			UMaterialInterface* Mat = SM ? SM->GetMaterial(0) : nullptr;
			UMaterial* Base = Mat ? Mat->GetMaterial() : nullptr;
			const FMaterialResource* Res = Base ? Base->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
			if (Res && !Res->IsGameThreadShaderMapComplete()) { ++Pending; }
		}
	const bool bDone = Pending == 0 || Waited > 40.f;
	if (!bDone && (bShotOnce || Waited < 10.f)) { return; }
	// a first picture after 10 s whatever is still compiling (the menu should not stay blank), the final one when
	// the compiling is over
	UE_LOG(LogArena, Display, TEXT("ARENA evt=icons_shoot waited=%.1f pending=%d final=%d"), Waited, Pending, bDone ? 1 : 0);
	Shoot();
	bShotOnce = true;
	bReady = true;
	if (!bDone) { return; }
	GetWorldTimerManager().ClearTimer(WaitTimer);
	FTimerHandle F;
	GetWorldTimerManager().SetTimer(F, this, &AArenaIconStudio::Finish, 0.5f, false);
}

void AArenaIconStudio::Shoot()
{
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	const float Bias = CVarPortraitExposure.GetValueOnGameThread();
	for (int32 H = 0; H < Heroes.Num(); ++H)
	{
		const float Scale = FMath::Max(0.5f, Heroes[H].Scale);
		// portrait: a three-quarter close-up of the face
		if (ASkeletalMeshActor* A = PortraitBooths.IsValidIndex(H) ? PortraitBooths[H].Get() : nullptr)
		{
			USkeletalMeshComponent* M = A->GetSkeletalMeshComponent();
			const FArenaHeroDef& D = Heroes[H];
			const FName Bone = D.PortraitBone.IsEmpty() ? FName(TEXT("head")) : FName(*D.PortraitBone);
			const FVector Head = M->DoesSocketExist(Bone) ? M->GetSocketLocation(Bone) : A->GetActorLocation() + FVector(0.f, 0.f, 165.f * Scale);
			const FVector CamOff = D.PortraitCam.Num() == 3 ? FVector(D.PortraitCam[0], D.PortraitCam[1], D.PortraitCam[2]) : FVector(145.f, -40.f, 6.f);
			const FVector LookOff = D.PortraitLook.Num() == 3 ? FVector(D.PortraitLook[0], D.PortraitLook[1], D.PortraitLook[2]) : FVector(0.f, 0.f, -14.f);
			const FVector Eye = Head + CamOff * Scale;
			if (FParse::Param(FCommandLine::Get(), TEXT("ArenaIconExport")) && !bShotOnce)
			{
				// the framing's numbers: the head-like bones, relative to the booth (tuning a hero's portrait)
				FString Bones;
				for (int32 B = 0; B < M->GetNumBones(); ++B)
				{
					const FString N = M->GetBoneName(B).ToString();
					if (N.Contains(TEXT("head")) || N.Contains(TEXT("neck")) || N.Contains(TEXT("jaw")) || N.Contains(TEXT("spine_03")))
					{
						Bones += FString::Printf(TEXT(" %s=%s"), *N, *(M->GetBoneLocation(M->GetBoneName(B)) - A->GetActorLocation()).ToCompactString());
					}
				}
				UE_LOG(LogArena, Display, TEXT("ARENA evt=portrait_bones hero=%s bounds=%s/%s%s"), *D.Id.ToString(), *(M->Bounds.Origin - A->GetActorLocation()).ToCompactString(), *M->Bounds.BoxExtent.ToCompactString(), *Bones);
			}
			USceneCaptureComponent2D* C = MakeCapture(Portraits[H], 30.f, Bias);
			C->ShowOnlyActors.Add(A);
			// a studio backdrop behind the head: a soft glow of the hero's colour, dark at the edges (the capture
			// used to show the empty sky: a flat light-blue card)
			if (AStaticMeshActor* Back = Backdrops.IsValidIndex(H) ? Backdrops[H].Get() : nullptr)
			{
				const FVector BackAt = Head + (Head - Eye).GetSafeNormal() * 170.f * Scale;
				Back->SetActorLocationAndRotation(BackAt, FRotationMatrix::MakeFromZ(Eye - BackAt).Rotator());
				C->ShowOnlyActors.Add(Back);
			}
			C->SetWorldLocationAndRotation(Eye, (Head + LookOff * Scale - Eye).Rotation());
			C->CaptureScene();
		}
		// ability icons: the whole action pose, framed by the posed bounds
		for (int32 S = 0; S < 5; ++S)
		{
			ASkeletalMeshActor* A = IconBooths.IsValidIndex(H * 5 + S) ? IconBooths[H * 5 + S].Get() : nullptr;
			if (!A) { continue; }
			USkeletalMeshComponent* M = A->GetSkeletalMeshComponent();
			const FBoxSphereBounds B = M->Bounds;
			const FVector Chest = M->DoesSocketExist(TEXT("spine_03")) ? M->GetSocketLocation(TEXT("spine_03")) : B.Origin;
			const FVector Center = FMath::Lerp(B.Origin, Chest, 0.5f);
			const float Radius = FMath::Clamp(B.SphereRadius, 80.f, 260.f) * Scale;
			const FVector Eye = Center + FVector(1.f, -0.75f, 0.28f).GetSafeNormal() * Radius * 1.55f;
			USceneCaptureComponent2D* C = MakeCapture(Icons[H * 5 + S], 42.f, Bias);
			C->ShowOnlyActors.Add(A);
			C->SetWorldLocationAndRotation(Eye, (Center - Eye).Rotation());
			C->CaptureScene();
		}
	}
	// minimap: orthographic, straight down, sun only (no point-light blobs); units, canopies and foliage left out
	USceneCaptureComponent2D* Map = MakeCapture(MinimapRT, 90.f, CVarMapExposure.GetValueOnGameThread());
	Map->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	Map->ShowFlags.SetPointLights(false);
	Map->ShowFlags.SetSpotLights(false);
	Map->ShowFlags.SetBloom(false);
	// no shadows: the trees are left out of the map but still cast theirs (blue blotches across the lanes), and a
	// low sun streaked the plateaus
	Map->ShowFlags.SetDynamicShadows(false);
	Map->ShowFlags.SetContactShadows(false);
	Map->ShowFlags.SetAmbientOcclusion(false);
	Map->ProjectionType = ECameraProjectionMode::Orthographic;
	const float MapOrtho = MapOrthoFor(GetWorld());
	Map->OrthoWidth = MapOrtho;
	for (TActorIterator<AArenaCharacter> It(GetWorld()); It; ++It) { Map->HiddenActors.Add(*It); }
	for (TActorIterator<AArenaScatter> It(GetWorld()); It; ++It)
	{
		const FString MeshPath = It->Mesh ? It->Mesh->GetPathName() : FString();
		if (MeshPath.Contains(TEXT("/Trees/")) || MeshPath.Contains(TEXT("/Foliage/"))) { Map->HiddenActors.Add(*It); }
	}
	Map->SetWorldLocationAndRotation(FVector(0.f, 0.f, 12000.f), FRotator(-90.f, -90.f, 0.f));   // base A on the left, base B on the right
	Map->CaptureScene();
	// exposure from the picture itself: whatever the sun, the sky light or GI, the map lands on a readable middle
	// grey (a fixed bias burned it white once the sun got brighter). Two corrections, measured on the read-back.
	float MapBias = CVarMapExposure.GetValueOnGameThread();
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		FTextureRenderTargetResource* Res = MinimapRT->GameThread_GetRenderTargetResource();
		TArray<FColor> Px;
		if (!Res || !Res->ReadPixels(Px) || Px.Num() == 0) { break; }
		double Sum = 0.0;
		int32 N = 0;
		for (int32 i = 0; i < Px.Num(); i += 7) { Sum += (0.2126 * Px[i].R + 0.7152 * Px[i].G + 0.0722 * Px[i].B) / 255.0; ++N; }
		const float Mean = N > 0 ? static_cast<float>(Sum / N) : 0.5f;
		UE_LOG(LogArena, Display, TEXT("ARENA evt=minimap pass=%d bias=%.2f mean=%.3f"), Pass, MapBias, Mean);
		if (FMath::Abs(Mean - 0.44f) < 0.04f || Pass == 2) { break; }
		// display values are about exposure^(1/2.2): the step in EV is 2.2 x log2 of the ratio
		MapBias += FMath::Clamp(2.2f * FMath::Log2(0.44f / FMath::Max(0.02f, Mean)), -4.f, 4.f);
		Map->PostProcessSettings.AutoExposureBias = MapBias;
		Map->CaptureScene();
	}
	MiniCam = Map->GetComponentLocation();
	MiniRight = Map->GetRightVector();
	MiniUp = Map->GetUpVector();
	MiniOrthoW = MapOrthoFor(GetWorld());
	MiniOrthoH = MiniOrthoW * MapH / MapW;
}

void AArenaIconStudio::PreviewPlay(const FString& AnimPath, bool bLoop)
{
	if (!PreviewBooth) { return; }
	USkeletalMeshComponent* M = PreviewBooth->GetSkeletalMeshComponent();
	if (UAnimSequenceBase* Anim = AnimPath.IsEmpty() ? nullptr : LoadObject<UAnimSequenceBase>(nullptr, *AnimPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		M->PlayAnimation(Anim, bLoop);
	}
}

void AArenaIconStudio::SetPreviewMesh(const FString& MeshPath)
{
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	if (!PreviewBooth || !Heroes.IsValidIndex(PreviewHeroIndex)) { return; }
	const FString& Path = MeshPath.IsEmpty() ? Heroes[PreviewHeroIndex].Mesh : MeshPath;
	if (USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *Path)) { PreviewBooth->GetSkeletalMeshComponent()->SetSkeletalMesh(SK); }
}

void AArenaIconStudio::SetPreview(int32 HeroIndex, int32 Slot)
{
	const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
	if (!Heroes.IsValidIndex(HeroIndex)) { return; }
	const FArenaHeroDef& D = Heroes[HeroIndex];
	const FVector At = Origin + FVector(-6000.f, -6000.f, 0.f);   // well away from the portrait and icon booths
	const float Sc = FMath::Max(0.5f, D.Scale);
	if (!PreviewRT) { PreviewRT = NewTarget(this, 1080, 1350); }   // the hero browser's stage, sharp on 1440p
	if (!PreviewBooth) { PreviewBooth = SpawnBooth(HeroIndex, At, D.PortraitAnim, -1.f, FLinearColor(0.75f, 0.85f, 1.f), true); PreviewHeroIndex = HeroIndex; }
	if (PreviewBooth && PreviewHeroIndex != HeroIndex)
	{
		// another hero on the same stage (its lights stay: a new booth per hero stacked them up)
		USkeletalMeshComponent* M = PreviewBooth->GetSkeletalMeshComponent();
		if (USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *D.Mesh)) { M->SetSkeletalMesh(SK); }
		M->SetRelativeScale3D(FVector(D.Scale));
		PreviewHeroIndex = HeroIndex;
	}
	if (PreviewBooth) { PreviewBooth->GetSkeletalMeshComponent()->SetRelativeRotation(FRotator(0.f, D.MeshYaw - 25.f, 0.f)); }
	const FVector Chest = At + FVector(0.f, 0.f, 105.f * Sc);
	const FVector Eye = Chest + FVector(1.f, -0.35f, 0.12f).GetSafeNormal() * 430.f * Sc;
	if (!PreviewBack)
	{
		PreviewBack = SpawnBackdrop(Chest + (Chest - Eye).GetSafeNormal() * 400.f, Eye, 9.f, FArenaDatabase::Hex(D.Tint));
	}
	else
	{
		const FVector BackAt = Chest + (Chest - Eye).GetSafeNormal() * 400.f;
		PreviewBack->SetActorLocationAndRotation(BackAt, FRotationMatrix::MakeFromZ(Eye - BackAt).Rotator());
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(PreviewBack->GetStaticMeshComponent()->GetMaterial(0)))
		{
			MID->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FArenaDatabase::Hex(D.Tint), FLinearColor(0.55f, 0.6f, 0.75f), 0.35f));
		}
	}
	if (!PreviewCapture)
	{
		PreviewCapture = MakeCapture(PreviewRT, 38.f, CVarPortraitExposure.GetValueOnGameThread());
	}
	PreviewCapture->ShowOnlyActors.Reset();
	if (PreviewBooth) { PreviewCapture->ShowOnlyActors.Add(PreviewBooth); }
	if (PreviewBack) { PreviewCapture->ShowOnlyActors.Add(PreviewBack); }
	PreviewCapture->SetWorldLocationAndRotation(Eye, (Chest - Eye).Rotation());
	PreviewCapture->bCaptureEveryFrame = true;
	GetWorldTimerManager().ClearTimer(PreviewIdle);
	GetWorldTimerManager().ClearTimer(PreviewImpact);
	if (Slot < 0 || !D.Abilities.IsValidIndex(Slot)) { PreviewPlay(D.PortraitAnim, true); return; }
	// the ability: its animation once, its cast effect on the hero, its impact effect ahead after its delay, then idle
	const FArenaAbilityDef& Ab = D.Abilities[Slot];
	const FString Anim = Ab.AnimVariants.Num() > 0 ? Ab.AnimVariants[0] : (!Ab.Anim.IsEmpty() ? Ab.Anim : D.PortraitAnim);
	PreviewPlay(Anim, false);
	USceneComponent* Root = PreviewBooth ? PreviewBooth->GetRootComponent() : nullptr;
	const FLinearColor C = FArenaDatabase::Hex(Ab.Color);
	auto FxAt = [this, Root, C](const FString& Path, const FVector& Where, float Scale)
	{
		if (!Root || Path.IsEmpty()) { return; }
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (UParticleSystem* PS = Cast<UParticleSystem>(Obj)) { UGameplayStatics::SpawnEmitterAttached(PS, Root, NAME_None, Where, FRotator::ZeroRotator, FVector(Scale), EAttachLocation::KeepWorldPosition, true); }
		else if (UNiagaraSystem* NS = Cast<UNiagaraSystem>(Obj)) { if (UNiagaraComponent* N = UNiagaraFunctionLibrary::SpawnSystemAttached(NS, Root, NAME_None, Where, FRotator::ZeroRotator, FVector(Scale), EAttachLocation::KeepWorldPosition, true, ENCPoolMethod::None)) { N->SetVariableLinearColor(TEXT("Color"), C); } }
	};
	FxAt(Ab.CastFx, At + FVector(0.f, 0.f, 60.f * Sc), 1.f);
	const FVector Ahead = At + FVector(Ab.Archetype == EArenaArchetype::Buff ? 0.f : 230.f * Sc, 0.f, 20.f);
	const FString Impact = Ab.Archetype == EArenaArchetype::GroundAoE && !Ab.TrailFx.IsEmpty() ? Ab.TrailFx : Ab.Fx;
	const FString ImpactFx = Ab.Fx;
	GetWorldTimerManager().SetTimer(PreviewImpact, FTimerDelegate::CreateWeakLambda(this, [FxAt, ImpactFx, Ahead, Ab]()
	{
		FxAt(ImpactFx, Ahead, ArenaFx::BlastScale(Ab.FxScale, FMath::Max(1.f, Ab.Radius), 0.2f, 0.6f, 1.0f));
	}), FMath::Max(0.1f, Ab.Delay), false);
	if (Ab.Archetype == EArenaArchetype::GroundAoE) { FxAt(Impact, Ahead, 0.8f); }
	float Len = 1.5f;
	if (UAnimSequenceBase* A = LoadObject<UAnimSequenceBase>(nullptr, *Anim, nullptr, LOAD_NoWarn | LOAD_Quiet)) { Len = FMath::Clamp(A->GetPlayLength(), 0.5f, 4.f); }
	const FString Idle = D.PortraitAnim;
	GetWorldTimerManager().SetTimer(PreviewIdle, FTimerDelegate::CreateWeakLambda(this, [this, Idle]() { PreviewPlay(Idle, true); }), Len + 0.4f, false);
}

void AArenaIconStudio::StopPreview()
{
	if (PreviewCapture) { PreviewCapture->bCaptureEveryFrame = false; }
}

void AArenaIconStudio::Finish()
{
	bReady = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("ArenaIconExport")))
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Icons");
		const TArray<FArenaHeroDef>& Heroes = FArenaDatabase::Get().Heroes;
		for (int32 H = 0; H < Heroes.Num(); ++H)
		{
			UKismetRenderingLibrary::ExportRenderTarget(this, Portrait(H), Dir, FString::Printf(TEXT("Portrait_%s.png"), *Heroes[H].Id.ToString()));
			for (int32 S = 0; S < 5; ++S) { UKismetRenderingLibrary::ExportRenderTarget(this, AbilityIcon(H, S), Dir, FString::Printf(TEXT("Ability_%s_%d.png"), *Heroes[H].Id.ToString(), S)); }
		}
		UKismetRenderingLibrary::ExportRenderTarget(this, MinimapRT, Dir, TEXT("Minimap.png"));
	}
	for (ASkeletalMeshActor* A : PortraitBooths) { if (A) { A->Destroy(); } }
	for (ASkeletalMeshActor* A : IconBooths) { if (A) { A->Destroy(); } }
	for (UPointLightComponent* L : Lights) { if (L) { L->DestroyComponent(); } }
	for (USceneCaptureComponent2D* C : Captures) { if (C) { C->DestroyComponent(); } }
	PortraitBooths.Reset(); IconBooths.Reset(); Lights.Reset(); Captures.Reset();
	UE_LOG(LogArena, Display, TEXT("ARENA evt=icons portraits=%d icons=%d minimap=%dx%d"), Portraits.Num(), Icons.Num(), MapW, MapH);
}
