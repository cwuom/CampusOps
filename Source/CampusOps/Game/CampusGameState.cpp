#include "Game/CampusGameState.h"
#include "Game/CampusPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ACampusGameState::ACampusGameState()
{
	TeamScores.Init(0, 2);
	RoundWins.Init(0, 2);
}

void ACampusGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusGameState, Mode);
	DOREPLIFETIME(ACampusGameState, Phase);
	DOREPLIFETIME(ACampusGameState, PhaseEndTime);
	DOREPLIFETIME(ACampusGameState, TeamScores);
	DOREPLIFETIME(ACampusGameState, RoundWins);
	DOREPLIFETIME(ACampusGameState, RoundNumber);
	DOREPLIFETIME(ACampusGameState, ScoreLimit);
	DOREPLIFETIME(ACampusGameState, WinningTeam);
	DOREPLIFETIME(ACampusGameState, RoundWinner);
	DOREPLIFETIME(ACampusGameState, bOvertime);
	DOREPLIFETIME(ACampusGameState, FlagLocation);
	DOREPLIFETIME(ACampusGameState, FlagProgress);
	DOREPLIFETIME(ACampusGameState, FlagTeam);
	DOREPLIFETIME(ACampusGameState, RoundLoadoutName);
	DOREPLIFETIME(ACampusGameState, Rules);
	DOREPLIFETIME(ACampusGameState, Map);
	DOREPLIFETIME(ACampusGameState, Weather);
	DOREPLIFETIME(ACampusGameState, TimeOfDay);
	DOREPLIFETIME(ACampusGameState, WeatherSeed);
}

const FCampusMatchRules& ACampusGameState::GetRules(const UWorld* World)
{
	static const FCampusMatchRules Standard;
	const ACampusGameState* GS = World ? World->GetGameState<ACampusGameState>() : nullptr;
	return GS ? GS->Rules : Standard;
}

void ACampusGameState::MulticastKillFeed_Implementation(const FCampusKillFeedEntry& Entry)
{
	FCampusKillFeedEntry Copy = Entry;
	Copy.Time = GetWorld()->GetTimeSeconds();
	KillFeed.Add(Copy);
	if (KillFeed.Num() > 8)
	{
		KillFeed.RemoveAt(0);
	}
}

float ACampusGameState::GetPhaseTimeRemaining() const
{
	return FMath::Max(0.f, PhaseEndTime - (float)GetServerWorldTimeSeconds());
}

int32 ACampusGameState::GetTeamScore(ECampusTeam Team) const
{
	const int32 Index = (int32)Team;
	return TeamScores.IsValidIndex(Index) ? TeamScores[Index] : 0;
}

int32 ACampusGameState::GetRoundWins(ECampusTeam Team) const
{
	const int32 Index = (int32)Team;
	return RoundWins.IsValidIndex(Index) ? RoundWins[Index] : 0;
}

TArray<ACampusPlayerState*> ACampusGameState::GetTeamPlayers(ECampusTeam Team) const
{
	TArray<ACampusPlayerState*> Out;
	for (APlayerState* PS : PlayerArray)
	{
		ACampusPlayerState* CPS = Cast<ACampusPlayerState>(PS);
		if (CPS && CPS->Team == Team)
		{
			Out.Add(CPS);
		}
	}
	Out.Sort([](const ACampusPlayerState& A, const ACampusPlayerState& B) { return A.Points > B.Points; });
	return Out;
}

FText ACampusGameState::GetModeName() const
{
	return Mode == ECampusMatchMode::Gunfight ? NSLOCTEXT("Campus", "ModeGunfight", "枪战") : NSLOCTEXT("Campus", "ModeTDM", "团队死斗");
}
