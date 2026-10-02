#include "Proto/ProtoHadesPlayerController.h"
#include "Proto/ProtoCharacter.h"
#include "Proto/ProtoGameMode.h"
#include "Proto/ProtoHUD.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"

AProtoHadesPlayerController::AProtoHadesPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
}

float AProtoHadesPlayerController::CameraYaw() const
{
	const AProtoCharacter* C = Char();
	return C ? C->SpringArm->GetComponentRotation().Yaw : 45.f;
}

void AProtoHadesPlayerController::SetMenu(EProtoMenu M)
{
	Super::SetMenu(M);
	// the cursor stays on in play (it aims, as in Hades), the mouse is never captured
	bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
	SetInputMode(Mode);
}

void AProtoHadesPlayerController::SetupInputComponent()
{
	APlayerController::SetupInputComponent();
	Mapping = NewObject<UInputMappingContext>(this);
	HMove = MakeAction(EInputActionValueType::Axis2D);
	HAimPad = MakeAction(EInputActionValueType::Axis2D);
	HAttack = MakeAction(EInputActionValueType::Boolean, true);   // also the menus' click
	HSpecial = MakeAction(EInputActionValueType::Boolean);
	HCast = MakeAction(EInputActionValueType::Boolean);
	HDash = MakeAction(EInputActionValueType::Boolean);
	HJump = MakeAction(EInputActionValueType::Boolean, true);     // also closes the controls card
	HCall = MakeAction(EInputActionValueType::Boolean);
	ActMenu = MakeAction(EInputActionValueType::Boolean, true);
	ActHelp = MakeAction(EInputActionValueType::Boolean, true);
	// Hades' layout: WASD, LMB attack, RMB cast, Q special, F call, (Space dash — here Shift), and the jump on Space;
	// a pad: the left stick moves, the right stick aims, A jump, B dash, X attack, Y special, RT cast, RB call
	auto Swz = [this]() { return NewObject<UInputModifierSwizzleAxis>(this); };
	auto Neg = [this]() { return NewObject<UInputModifierNegate>(this); };
	FEnhancedActionKeyMapping& W = Mapping->MapKey(HMove, EKeys::W); W.Modifiers.Add(Swz());
	FEnhancedActionKeyMapping& Sk = Mapping->MapKey(HMove, EKeys::S); Sk.Modifiers.Add(Swz()); Sk.Modifiers.Add(Neg());
	FEnhancedActionKeyMapping& Ak = Mapping->MapKey(HMove, EKeys::A); Ak.Modifiers.Add(Neg());
	Mapping->MapKey(HMove, EKeys::D);
	FEnhancedActionKeyMapping& Stick = Mapping->MapKey(HMove, EKeys::Gamepad_Left2D); Stick.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	FEnhancedActionKeyMapping& Right = Mapping->MapKey(HAimPad, EKeys::Gamepad_Right2D); Right.Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	Mapping->MapKey(HAttack, EKeys::LeftMouseButton); Mapping->MapKey(HAttack, EKeys::Gamepad_FaceButton_Left);
	Mapping->MapKey(HCast, EKeys::RightMouseButton); Mapping->MapKey(HCast, EKeys::Gamepad_RightTrigger);
	Mapping->MapKey(HSpecial, EKeys::Q); Mapping->MapKey(HSpecial, EKeys::Gamepad_FaceButton_Top);
	Mapping->MapKey(HDash, EKeys::LeftShift); Mapping->MapKey(HDash, EKeys::Gamepad_FaceButton_Right);
	Mapping->MapKey(HJump, EKeys::SpaceBar); Mapping->MapKey(HJump, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(HCall, EKeys::F); Mapping->MapKey(HCall, EKeys::Gamepad_RightShoulder);
	Mapping->MapKey(ActMenu, EKeys::Escape); Mapping->MapKey(ActMenu, EKeys::Gamepad_Special_Right);
	Mapping->MapKey(ActHelp, EKeys::F1);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindActionValueLambda(HMove, ETriggerEvent::Triggered, [this](const FInputActionValue& V)
		{
			LastMove = V.Get<FVector2D>();
			// on the screen: W is up the screen (the fixed camera's way)
			if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->Move(LastMove, FRotator(0.f, CameraYaw(), 0.f)); }
		});
		EIC->BindActionValueLambda(HMove, ETriggerEvent::Completed, [this](const FInputActionValue&) { LastMove = FVector2D::ZeroVector; });
		EIC->BindActionValueLambda(HAimPad, ETriggerEvent::Triggered, [this](const FInputActionValue& V) { PadAim = V.Get<FVector2D>(); });
		EIC->BindActionValueLambda(HAimPad, ETriggerEvent::Completed, [this](const FInputActionValue&) { PadAim = FVector2D::ZeroVector; });
		// LMB: a click in a menu, else the attack; held, it keeps attacking
		EIC->BindActionValueLambda(HAttack, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			if (Menu == EProtoMenu::Intro) { Dismiss(); return; }
			if (Menu != EProtoMenu::None)
			{
				float MX = 0.f, MY = 0.f;
				if (AProtoHUD* HUD = Cast<AProtoHUD>(GetHUD()); HUD && GetMousePosition(MX, MY))
				{
					const AArenaHUD::FMenuHit M = HUD->MenuHitAt(FVector2D(MX, MY));
					if (!M.Action.IsNone()) { MenuAction(M.Action, M.Arg); }
				}
				return;
			}
			LightDownAt = LastLightPress = GetWorld()->GetRealTimeSeconds();
			if (AProtoCharacter* C = Char()) { C->LightAttack(); }
		});
		EIC->BindActionValueLambda(HAttack, ETriggerEvent::Triggered, [this](const FInputActionValue&)
		{
			const float Real = GetWorld()->GetRealTimeSeconds();
			if (Menu != EProtoMenu::None || LightDownAt < 0.f || Real - LightDownAt < 0.3f || Real - LastLightPress < 0.18f) { return; }
			LastLightPress = Real;
			if (AProtoCharacter* C = Char()) { C->LightAttack(); }
		});
		EIC->BindActionValueLambda(HAttack, ETriggerEvent::Completed, [this](const FInputActionValue&) { LightDownAt = -1.f; });
		EIC->BindActionValueLambda(HCast, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->CastStone(); } });
		EIC->BindActionValueLambda(HSpecial, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->StartHeavy(); } });
		EIC->BindActionValueLambda(HSpecial, ETriggerEvent::Completed, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->ReleaseHeavy(); } });
		EIC->BindActionValueLambda(HDash, ETriggerEvent::Started, [this](const FInputActionValue&) { if (Menu == EProtoMenu::None) { DashByKeys(); } });
		EIC->BindActionValueLambda(HJump, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			if (Menu == EProtoMenu::Intro) { Dismiss(); return; }
			if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->PressJump(); }
		});
		EIC->BindActionValueLambda(HJump, ETriggerEvent::Completed, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char()) { C->ReleaseJump(); } });
		EIC->BindActionValueLambda(HCall, ETriggerEvent::Started, [this](const FInputActionValue&) { if (AProtoCharacter* C = Char(); C && Menu == EProtoMenu::None) { C->CallWrath(); } });
		EIC->BindActionValueLambda(ActMenu, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			const AProtoGameMode* GM = AProtoGameMode::Get(this);
			if (Menu == EProtoMenu::Intro) { Dismiss(); return; }
			if (!GM || GM->Phase != EProtoPhase::Playing) { if (Menu == EProtoMenu::Controls) { SetMenu(MenuBack); } return; }
			if (Menu == EProtoMenu::None) { MenuBack = EProtoMenu::Pause; SetMenu(EProtoMenu::Pause); }
			else if (Menu == EProtoMenu::Controls) { SetMenu(MenuBack); }
			else { SetMenu(EProtoMenu::None); }
		});
		EIC->BindActionValueLambda(ActHelp, ETriggerEvent::Started, [this](const FInputActionValue&)
		{
			const AProtoGameMode* GM = AProtoGameMode::Get(this);
			if (Menu == EProtoMenu::Intro) { Dismiss(); }
			else if (Menu == EProtoMenu::None && GM && GM->Phase == EProtoPhase::Playing) { SetMenu(EProtoMenu::Intro); }
		});
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Sub->AddMappingContext(Mapping, 0);
	}
}

void AProtoHadesPlayerController::DashByKeys()
{
	AProtoCharacter* C = Char();
	if (!C) { return; }
	// the dash goes where the keys point (on the screen); standing still, where the hero faces (Hades)
	const FRotator Yaw(0.f, CameraYaw(), 0.f);
	FVector Dir = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * LastMove.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * LastMove.X;
	if (Dir.IsNearlyZero()) { Dir = C->GetActorForwardVector(); }
	C->Dodge(Dir);
}

void AProtoHadesPlayerController::PlayerTick(float DeltaTime)
{
	APlayerController::PlayerTick(DeltaTime);   // not the third-person controller's camera aim
	AProtoCharacter* C = Char();
	if (!C) { RestoreCutaway(); return; }
	// the aim: the right stick's way, else the cursor on the ground at the hero's height
	if (!PadAim.IsNearlyZero())
	{
		const FRotator Yaw(0.f, CameraYaw(), 0.f);
		const FVector Dir = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * PadAim.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * PadAim.X;
		C->SetAimAt(C->GetActorLocation() + Dir.GetSafeNormal2D() * 400.f);
	}
	else if (const AProtoGameMode* GM = AProtoGameMode::Get(this); !GM || !GM->bLab)   // the lab aims by itself
	{
		float MX = 0.f, MY = 0.f;
		FVector Origin, Dir;
		if (GetMousePosition(MX, MY) && DeprojectScreenPositionToWorld(MX, MY, Origin, Dir) && Dir.Z < -0.05f)
		{
			const float PlaneZ = C->GetActorLocation().Z - 40.f;
			const float T = (PlaneZ - Origin.Z) / Dir.Z;
			if (T > 0.f) { C->SetAimAt(Origin + Dir * T); }
		}
	}
	UpdateCutaway(C);
}

void AProtoHadesPlayerController::UpdateCutaway(AProtoCharacter* C)
{
	// the buildings between the camera and the hero go (their collision stays): the hero is never lost behind a roof
	if (!PlayerCameraManager) { return; }
	const FVector Cam = PlayerCameraManager->GetCameraLocation();
	const FVector Hero = C->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	TSet<TWeakObjectPtr<AActor>> Now;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ProtoCutaway), false, C);
	for (int32 i = 0; i < 8; ++i)
	{
		FHitResult H;
		if (!GetWorld()->SweepSingleByChannel(H, Cam, Hero, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(35.f), Q)) { break; }
		AActor* A = H.GetActor();
		if (!A) { break; }
		Q.AddIgnoredActor(A);
		if (A->IsA<AStaticMeshActor>() && A->GetComponentsBoundingBox().Max.Z > Hero.Z + 40.f) { Now.Add(A); }
	}
	for (const TWeakObjectPtr<AActor>& A : CutAway) { if (A.IsValid() && !Now.Contains(A)) { A->SetActorHiddenInGame(false); } }
	for (const TWeakObjectPtr<AActor>& A : Now) { if (A.IsValid()) { A->SetActorHiddenInGame(true); } }
	CutAway = MoveTemp(Now);
}

void AProtoHadesPlayerController::RestoreCutaway()
{
	for (const TWeakObjectPtr<AActor>& A : CutAway) { if (A.IsValid()) { A->SetActorHiddenInGame(false); } }
	CutAway.Reset();
}

void AProtoHadesPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	RestoreCutaway();
	Super::EndPlay(Reason);
}
