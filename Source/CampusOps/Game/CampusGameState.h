#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CampusTypes.h"
#include "CampusGameState.generated.h"

class ACampusPlayerState;

UENUM(BlueprintType)
enum class ECampusScoreEvent : uint8
{
	Kill,
	Headshot,
	Assist,
	FirstBlood,
	DoubleKill,
	TripleKill,
	Longshot,
	Revenge,
	Streak5,
	Melee,
	RoundWin,
	RoundLoss
};

namespace CampusText
{
	inline FText ScoreEventName(ECampusScoreEvent Event)
	{
		switch (Event)
		{
		case ECampusScoreEvent::Kill: return NSLOCTEXT("Campus", "EvKill", "击杀");
		case ECampusScoreEvent::Headshot: return NSLOCTEXT("Campus", "EvHead", "爆头");
		case ECampusScoreEvent::Assist: return NSLOCTEXT("Campus", "EvAssist", "助攻");
		case ECampusScoreEvent::FirstBlood: return NSLOCTEXT("Campus", "EvFirst", "第一滴血");
		case ECampusScoreEvent::DoubleKill: return NSLOCTEXT("Campus", "EvDouble", "双杀");
		case ECampusScoreEvent::TripleKill: return NSLOCTEXT("Campus", "EvTriple", "三杀");
		case ECampusScoreEvent::Longshot: return NSLOCTEXT("Campus", "EvLong", "远距离击杀");
		case ECampusScoreEvent::Revenge: return NSLOCTEXT("Campus", "EvRevenge", "复仇");
		case ECampusScoreEvent::Streak5: return NSLOCTEXT("Campus", "EvStreak", "五连杀");
		case ECampusScoreEvent::Melee: return NSLOCTEXT("Campus", "EvMelee", "近战击杀");
		case ECampusScoreEvent::RoundWin: return NSLOCTEXT("Campus", "EvRoundWin", "回合胜利");
		case ECampusScoreEvent::RoundLoss: return NSLOCTEXT("Campus", "EvRoundLoss", "回合失败");
		default: return FText::GetEmpty();
		}
	}
}

UCLASS()
class CAMPUSOPS_API ACampusGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ACampusGameState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusMatchMode Mode = ECampusMatchMode::TeamDeathmatch;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusMatchPhase Phase = ECampusMatchPhase::Warmup;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") float PhaseEndTime = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") TArray<int32> TeamScores;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") TArray<int32> RoundWins;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 RoundNumber = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") int32 ScoreLimit = 60;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusTeam WinningTeam = ECampusTeam::None;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusTeam RoundWinner = ECampusTeam::None;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") bool bOvertime = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") FVector_NetQuantize FlagLocation = FVector::ZeroVector;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") float FlagProgress = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusTeam FlagTeam = ECampusTeam::None;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") FString RoundLoadoutName;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") FCampusMatchRules Rules;
	/** The map being played and the conditions the host rolled (or picked) for it. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusMap Map = ECampusMap::Campus;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusWeather Weather = ECampusWeather::Clear;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") ECampusTimeOfDay TimeOfDay = ECampusTimeOfDay::Noon;
	/** Seed for lightning strikes and gusts, so every machine sees the same storm. */
	UPROPERTY(Replicated) int32 WeatherSeed = 0;

	/** Rules of the match being played in World (standard rules when there is no game state yet). */
	static const FCampusMatchRules& GetRules(const UWorld* World);

	/** Client-side history filled through MulticastKillFeed. */
	TArray<FCampusKillFeedEntry> KillFeed;

	UFUNCTION(NetMulticast, Reliable) void MulticastKillFeed(const FCampusKillFeedEntry& Entry);

	float GetPhaseTimeRemaining() const;
	int32 GetTeamScore(ECampusTeam Team) const;
	int32 GetRoundWins(ECampusTeam Team) const;
	TArray<ACampusPlayerState*> GetTeamPlayers(ECampusTeam Team) const;
	FText GetModeName() const;
};
