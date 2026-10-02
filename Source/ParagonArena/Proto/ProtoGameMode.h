// The prototype's mode (map /Game/Maps/Proto, its world settings name this game mode): a menu of its own, a 1v1 or
// 1v2 against the test bot, a sandbox with a passive bot, respawns, and the ProtoLab (-ProtoLab) that measures the
// mechanics with PASS / FAIL lines.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ProtoGameMode.generated.h"

class AProtoCharacter;

UENUM()
enum class EProtoPhase : uint8 { Menu, Playing };

UENUM()
enum class EProtoMode : uint8 { Duel, TwoBots, Sandbox };

UCLASS()
class PARAGONARENA_API AProtoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AProtoGameMode();
	virtual void BeginPlay() override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override { return nullptr; }

	static AProtoGameMode* Get(const UObject* Context);

	/** Prototype 2: the same map with Hades' controls (the URL option ?Hades or -ProtoHades): a fixed camera above,
	 *  the cursor aims, the cast, the dash-strike, the call. */
	bool bHades = false;
	EProtoPhase Phase = EProtoPhase::Menu;
	EProtoMode Mode = EProtoMode::Duel;
	int32 Difficulty = 1;                      // 0 easy, 1 normal, 2 hard: reactions and choices, not aim
	float MatchStart = 0.f;
	TWeakObjectPtr<AProtoCharacter> Player;
	TArray<TWeakObjectPtr<AProtoCharacter>> Bots;
	struct FRespawn { TWeakObjectPtr<AProtoCharacter> Who; float At = 0.f; };
	TArray<FRespawn> Respawns;
	int32 PlayerKOs = 0, BotKOs = 0;

	void StartMatch(EProtoMode InMode);
	void BackToMenu();
	void OnKnockout(AProtoCharacter* Victim, AProtoCharacter* Killer);
	FTransform SpawnPoint(int32 Team, int32 Index) const;
	TArray<FVector> PatrolPoints;

	// ---- the ProtoLab ----
	bool bLab = false;
	int32 LabStep = 0, LabFails = 0, LabPasses = 0;
	float LabT0 = 0.f, LabStepT = 0.f;
	float LabValue[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	TWeakObjectPtr<AProtoCharacter> LabA, LabB;
	TArray<TWeakObjectPtr<AActor>> LabProps;
	void TickLab(float Now);
	void TickHadesLab(float Now);
	// -ProtoUIShots: the screens a player sees (the menu, the controls, the card at the start, a fight in the town, the
	// pause), each a screenshot, then quit — runs on real time (the card and the pause stop the game's clock)
	bool bUIShots = false;
	int32 UIStep = 0;
	float UIT0 = 0.f;
	void TickUIShots();
	TWeakObjectPtr<AProtoCharacter> LabC;
	void Check(bool bOk, const FString& What);
	static void LabFile(const FString& Line);
	AProtoCharacter* SpawnChar(int32 Team, const FVector& At, float Yaw, bool bBot, bool bPossessByPlayer);
	AActor* LabBox(const FVector& Center, const FVector& Size);
	void Shot(const TCHAR* Name);
	int32 ShotIndex = 0;

private:
	class ACameraActor* OverviewCam = nullptr;
};
