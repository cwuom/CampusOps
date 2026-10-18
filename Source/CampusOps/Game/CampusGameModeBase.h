#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CampusTypes.h"
#include "Game/CampusGameState.h"
#include "CampusGameModeBase.generated.h"

class ACampusCharacter;
class ACampusPlayerState;
class ACampusBotController;
class ACampusSchoolBuilder;

UCLASS()
class CAMPUSOPS_API ACampusGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACampusGameModeBase();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override { return false; }

	virtual bool CanDamage(const ACampusCharacter* Victim, const AController* InstigatorController) const;
	void NotifyDamage(ACampusCharacter* Victim, AController* InstigatorController, float Damage);
	virtual void NotifyKilled(ACampusCharacter* Victim, AController* Killer, FName WeaponId, bool bHeadshot);
	void NotifyGunshot(ACampusCharacter* Shooter, const FVector& Location, bool bSuppressed);
	void SetPlayerProfile(AController* Controller, const FCampusLoadout& Loadout, int32 OperatorIndex, const FString& PlayerName);

	ECampusMatchMode GetMatchMode() const { return MatchMode; }
	ACampusSchoolBuilder* GetBuilder() const;
	ACampusGameState* GetCampusGameState() const;
	int32 GetBotSkill() const { return BotSkill; }
	const TArray<FVector>& GetBotHotspots() const;
	/** Bots call out enemy contacts; teammates use the freshest one to pick where to push. */
	void ReportTeamIntel(ECampusTeam Team, const FVector& Location);
	bool GetTeamIntel(ECampusTeam Team, FVector& OutLocation, float MaxAge) const;
	/** Automation: ends the match now with Winner (None for a draw). */
	void DebugEndMatch(ECampusTeam Winner) { EndMatch(Winner); }

protected:
	virtual void OnPhaseExpired(ECampusMatchPhase Phase);
	virtual void BeginCountdown();
	/** Every human player's machine has reported its world loaded. */
	bool AllPlayersLoaded() const;
	virtual void BeginPlaying();
	virtual void OnTeamKill(ACampusPlayerState* Killer, ACampusPlayerState* Victim) {}
	virtual FCampusLoadout GetLoadoutFor(AController* Controller) const;
	virtual bool UseGunfightSpawns() const { return false; }
	virtual bool AllowRespawn() const { return true; }

	void SetPhase(ECampusMatchPhase NewPhase, float Duration);
	void EndMatch(ECampusTeam Winner);
	void SetAllFrozen(bool bFrozen);
	void QueueRespawn(AController* Controller, float Delay);
	bool PickSpawn(AController* Controller, FTransform& Out) const;
	ECampusTeam PickTeamForNewPlayer() const;
	ACampusBotController* AddBot(ECampusTeam Team);
	void FillBots();
	void RemoveBotFromTeam(ECampusTeam Team);
	int32 CountTeam(ECampusTeam Team, bool bHumansOnly) const;
	void AwardScore(ACampusPlayerState* PS, ECampusScoreEvent Event, int32 Points);
	void ReturnToMenu();
	void SpawnAllWithoutPawn(bool bPlayIntro);
	void KillAllPawns();

	UPROPERTY(EditDefaultsOnly, Category = "Campus") ECampusMatchMode MatchMode = ECampusMatchMode::TeamDeathmatch;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") int32 TeamSize = 5;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") bool bFillWithBots = true;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") int32 BotSkill = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") float RespawnDelay = 4.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") float MatchDuration = 600.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") int32 ScoreLimit = 60;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") float CountdownDuration = 5.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") float PostMatchDuration = 10.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus") bool bHealthRegen = true;
	FCampusMatchRules Rules;
	ECampusMap PlayedMap = ECampusMap::Campus;
	ECampusWeather Weather = ECampusWeather::Clear;
	ECampusTimeOfDay TimeOfDay = ECampusTimeOfDay::Noon;
	int32 WeatherSeed = 0;

	TMap<TWeakObjectPtr<AController>, double> RespawnQueue;
	FVector TeamIntelLocation[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float TeamIntelTime[2] = { -100.f, -100.f };
	bool bFirstBloodTaken = false;
	bool bMatchEnded = false;
	int32 BotCounter = 0;
};
