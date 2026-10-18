#include "Game/CampusPlayerState.h"
#include "Net/UnrealNetwork.h"

void ACampusPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusPlayerState, Team);
	DOREPLIFETIME(ACampusPlayerState, Kills);
	DOREPLIFETIME(ACampusPlayerState, Deaths);
	DOREPLIFETIME(ACampusPlayerState, Assists);
	DOREPLIFETIME(ACampusPlayerState, Points);
	DOREPLIFETIME(ACampusPlayerState, Streak);
	DOREPLIFETIME(ACampusPlayerState, OperatorIndex);
}
