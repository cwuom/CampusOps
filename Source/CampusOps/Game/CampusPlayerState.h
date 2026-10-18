#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CampusTypes.h"
#include "CampusPlayerState.generated.h"

UCLASS()
class CAMPUSOPS_API ACampusPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusTeam Team = ECampusTeam::None;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 Kills = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 Deaths = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 Assists = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 Points = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 Streak = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 OperatorIndex = 0;

	/** Server only: the class the player will spawn with. */
	FCampusLoadout Loadout;
	bool bHasLoadout = false;

	/** Server only: recent damage dealers for assists. */
	TMap<TWeakObjectPtr<APlayerState>, float> RecentAttackers;
	TWeakObjectPtr<APlayerState> LastKiller;
	float LastKillTime = -100.f;
	int32 MultiKillCount = 0;
};
