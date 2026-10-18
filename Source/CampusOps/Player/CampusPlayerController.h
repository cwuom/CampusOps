#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CampusTypes.h"
#include "Game/CampusGameState.h"
#include "CampusPlayerController.generated.h"

class ACampusHUD;
class SCampusPauseMenu;

UCLASS()
class CAMPUSOPS_API ACampusPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACampusPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void AcknowledgePossession(APawn* InPawn) override;
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(Client, Reliable) void ClientOnDamaged(FVector_NetQuantize SourceLocation, float Damage);
	UFUNCTION(Client, Unreliable) void ClientHitConfirmed(bool bKill, bool bHeadshot, float Damage);
	UFUNCTION(Client, Reliable) void ClientKillNotice(const FString& VictimName, bool bHeadshot);
	UFUNCTION(Client, Reliable) void ClientScoreEvent(ECampusScoreEvent Event, int32 Points);
	UFUNCTION(Client, Reliable) void ClientSetFrozen(bool bInFrozen);
	UFUNCTION(Client, Reliable) void ClientMatchIntro(float Duration);
	UFUNCTION(Client, Reliable) void ClientMatchStarted();
	UFUNCTION(Client, Reliable) void ClientReturnToMenu();
	UFUNCTION(Server, Reliable) void ServerSetProfile(const FCampusLoadout& Loadout, int32 OperatorIndex, const FString& InPlayerName);
	/** Blinded (whiteout and burned-in afterimage) and deafened (ringing, the world muffled) by a flashbang. */
	UFUNCTION(Client, Reliable) void ClientFlashbanged(float Blind, float Deaf, FVector_NetQuantize From);
	/** Hitmarker for grenade damage (bullet hits are shown by the shooter's own trace). */
	UFUNCTION(Client, Unreliable) void ClientEquipmentHit(float Damage);

	/** Local: ears ringing after a blast (Amount 0..1); the world is ducked under the tinnitus and recovers. */
	void OnDeafened(float Amount, bool bFlash);
	virtual void PlayerTick(float DeltaTime) override;

	void ShowLocalHitmarker(bool bHeadshot, float Damage);
	void ApplyProfileSettings();
	void SendProfileToServer();
	ACampusHUD* GetCampusHUD() const;

	bool IsMatchFrozen() const { return bFrozen; }
	bool IsScoreboardVisible() const { return bShowScoreboard; }
	bool IsIntroActive() const;
	float GetIntroAlpha() const;
	float IntroStartTime = -100.f;
	float IntroDuration = 0.f;

	void SetPauseMenuOpen(bool bOpen);
	bool IsPauseMenuOpen() const { return PauseMenu.IsValid(); }
	void LeaveMatch();

	/**
	 * Local: everything is in (shaders and pipeline states compiled, packages and streaming levels loaded, textures
	 * streamed) and frames have come smoothly for a moment. Until then the player cannot look, move or act.
	 */
	bool IsWorldLoaded() const { return bWorldLoaded; }
	/** Loading screen: 0..1, what is still outstanding, and when the load finished (world seconds). */
	float GetLoadProgress() const { return LoadProgress; }
	const FText& GetLoadStatus() const { return LoadStatus; }
	float GetLoadedTime() const { return LoadedTime; }
	/**
	 * When the loading cover starts to lift (world seconds), or a negative value while it stays up. It waits past the
	 * load for the deploy drop to begin, so the first thing seen is the sky, never the empty spawn or a spectator view.
	 */
	float GetRevealTime() const;
	/** Server: this player's machine has finished loading; the match does not start without it. */
	bool HasClientLoaded() const { return bClientLoaded; }
	UFUNCTION(Server, Reliable) void ServerReportLoaded();
	virtual bool IsLookInputIgnored() const override;
	virtual bool IsMoveInputIgnored() const override;

protected:
	void UpdateLoadGate(float DeltaTime);
	bool bWorldLoaded = false;
	bool bClientLoaded = false;
	float LoadProgress = 0.f;
	FText LoadStatus;
	float LoadedTime = -100.f;
	double LoadStartTime = -1.0;
	int32 SmoothReadyFrames = 0;
	/** Clock time everything was first in (-1 while still loading). */
	double ReadySince = -1.0;
	int32 PeakShaderJobs = 0;
	int32 PeakPipelineJobs = 0;
	int32 PeakTextures = 0;
	/** The pawn whose input the gate switched off, handed back once loaded. */
	TWeakObjectPtr<APawn> GatedPawn;
	/** Each new pawn (spawn, respawn) also waits for its own meshes and materials to be ready before taking control. */
	TWeakObjectPtr<APawn> SettledPawn;
	TWeakObjectPtr<APawn> SettlingPawn;
	int32 PawnQuietFrames = 0;
	double PawnSettleStart = 0.0;
	bool IsPawnSettling() const;

	void OnScoreboardPressed();
	void OnScoreboardReleased();
	void OnPausePressed();

	bool bFrozen = false;
	bool bShowScoreboard = false;
	/** 1 deaf .. 0 normal hearing; recovers over a few seconds. */
	float Deafness = 0.f;
	float DeafRecoverRate = 0.3f;
	/** Thermite burns in quick ticks; its hitmarkers are paced instead of drawn for every one. */
	float LastEquipmentHitTime = -100.f;
	float AppliedDuck = -1.f;
	TSharedPtr<SCampusPauseMenu> PauseMenu;
	TSharedPtr<class SWidget> PauseMenuContainer;
};
