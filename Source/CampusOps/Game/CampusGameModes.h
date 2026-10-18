#pragma once

#include "CoreMinimal.h"
#include "Game/CampusGameModeBase.h"
#include "CampusGameModes.generated.h"

/** 5v5 Team Deathmatch: first team to the score limit (or highest when time runs out). */
UCLASS()
class CAMPUSOPS_API ACampusGameMode_TDM : public ACampusGameModeBase
{
	GENERATED_BODY()

public:
	ACampusGameMode_TDM();

protected:
	virtual void OnTeamKill(ACampusPlayerState* Killer, ACampusPlayerState* Victim) override;
};

/**
 * Gunfight: 1v1 (or 2v2 with ?TeamSize=2) rounds on the basketball courts. Both sides get the same
 * random loadout, rotating every two rounds. No respawns, no health regen. When the round timer expires
 * an overtime flag appears; capture it or win on remaining health. First to 6 rounds.
 */
UCLASS()
class CAMPUSOPS_API ACampusGameMode_Gunfight : public ACampusGameModeBase
{
	GENERATED_BODY()

public:
	ACampusGameMode_Gunfight();
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyKilled(ACampusCharacter* Victim, AController* Killer, FName WeaponId, bool bHeadshot) override;

protected:
	virtual void OnPhaseExpired(ECampusMatchPhase Phase) override;
	virtual void BeginCountdown() override;
	virtual void BeginPlaying() override;
	virtual FCampusLoadout GetLoadoutFor(AController* Controller) const override { return RoundLoadout; }
	virtual bool UseGunfightSpawns() const override { return true; }
	virtual bool AllowRespawn() const override { return false; }

	void StartOvertime();
	void EndRound(ECampusTeam Winner);
	void CheckElimination();
	float GetTeamHealth(ECampusTeam Team) const;
	int32 GetAliveCount(ECampusTeam Team) const;

	UPROPERTY(EditDefaultsOnly, Category = "Gunfight") float RoundTime = 40.f;
	UPROPERTY(EditDefaultsOnly, Category = "Gunfight") float OvertimeTime = 12.f;
	UPROPERTY(EditDefaultsOnly, Category = "Gunfight") float CaptureTime = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "Gunfight") float RoundOverTime = 3.5f;

	FCampusLoadout RoundLoadout;
	FRandomStream LoadoutStream;
};
