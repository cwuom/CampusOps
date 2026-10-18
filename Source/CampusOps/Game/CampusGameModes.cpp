#include "Game/CampusGameModes.h"
#include "Game/CampusGameState.h"
#include "Game/CampusPlayerState.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusPlayerController.h"
#include "Map/CampusSchoolBuilder.h"
#include "CampusArmory.h"
#include "EngineUtils.h"

// ------------------------------------------------------------------ TDM

ACampusGameMode_TDM::ACampusGameMode_TDM()
{
	MatchMode = ECampusMatchMode::TeamDeathmatch;
	TeamSize = 5;
	ScoreLimit = 60;
	MatchDuration = 600.f;
	RespawnDelay = 4.f;
	CountdownDuration = 6.f;
	bHealthRegen = true;
}

void ACampusGameMode_TDM::OnTeamKill(ACampusPlayerState* Killer, ACampusPlayerState* Victim)
{
	ACampusGameState* GS = GetCampusGameState();
	if (!GS || !Killer)
	{
		return;
	}
	const int32 Index = (int32)Killer->Team;
	if (GS->TeamScores.IsValidIndex(Index))
	{
		++GS->TeamScores[Index];
		if (GS->TeamScores[Index] >= ScoreLimit)
		{
			EndMatch(Killer->Team);
		}
	}
}

// ------------------------------------------------------------------ Gunfight

ACampusGameMode_Gunfight::ACampusGameMode_Gunfight()
{
	MatchMode = ECampusMatchMode::Gunfight;
	TeamSize = 1;
	ScoreLimit = 6;
	CountdownDuration = 4.f;
	bHealthRegen = false;
	LoadoutStream.Initialize(FMath::Rand());
}

void ACampusGameMode_Gunfight::OnPhaseExpired(ECampusMatchPhase Phase)
{
	ACampusGameState* GS = GetCampusGameState();
	switch (Phase)
	{
	case ECampusMatchPhase::Playing:
		if (GS && !GS->bOvertime)
		{
			StartOvertime();
		}
		else
		{
			const float A = GetTeamHealth(ECampusTeam::Alpha);
			const float B = GetTeamHealth(ECampusTeam::Bravo);
			EndRound(FMath::IsNearlyEqual(A, B) ? ECampusTeam::None : (A > B ? ECampusTeam::Alpha : ECampusTeam::Bravo));
		}
		break;
	case ECampusMatchPhase::RoundOver:
		BeginCountdown();
		break;
	default:
		Super::OnPhaseExpired(Phase);
		break;
	}
}

void ACampusGameMode_Gunfight::BeginCountdown()
{
	ACampusGameState* GS = GetCampusGameState();
	if (!GS)
	{
		return;
	}
	++GS->RoundNumber;
	if ((GS->RoundNumber - 1) % 2 == 0)
	{
		RoundLoadout = UCampusArmory::Get().MakeRandomLoadout(LoadoutStream);
		const FCampusWeaponDef* Primary = UCampusArmory::Get().FindWeapon(RoundLoadout.Primary.WeaponId);
		const FCampusWeaponDef* Secondary = UCampusArmory::Get().FindWeapon(RoundLoadout.Secondary.WeaponId);
		GS->RoundLoadoutName = FString::Printf(TEXT("%s + %s"),
			Primary ? *Primary->DisplayName.ToString() : TEXT("?"),
			Secondary ? *Secondary->DisplayName.ToString() : TEXT("?"));
	}
	GS->bOvertime = false;
	GS->FlagProgress = 0.f;
	GS->FlagTeam = ECampusTeam::None;
	GS->RoundWinner = ECampusTeam::None;

	KillAllPawns();
	SpawnAllWithoutPawn(true);
	SetAllFrozen(true);
	SetPhase(ECampusMatchPhase::Countdown, CountdownDuration);
}

void ACampusGameMode_Gunfight::BeginPlaying()
{
	SetAllFrozen(false);
	SetPhase(ECampusMatchPhase::Playing, RoundTime);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(It->Get()))
		{
			PC->ClientMatchStarted();
		}
	}
}

void ACampusGameMode_Gunfight::StartOvertime()
{
	ACampusGameState* GS = GetCampusGameState();
	if (!GS)
	{
		return;
	}
	GS->bOvertime = true;
	GS->FlagProgress = 0.f;
	GS->FlagTeam = ECampusTeam::None;
	if (const ACampusSchoolBuilder* B = GetBuilder())
	{
		GS->FlagLocation = B->GunfightFlag;
	}
	SetPhase(ECampusMatchPhase::Playing, OvertimeTime);
}

void ACampusGameMode_Gunfight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACampusGameState* GS = GetCampusGameState();
	if (!GS || GS->Phase != ECampusMatchPhase::Playing || !GS->bOvertime)
	{
		return;
	}
	bool bAlpha = false;
	bool bBravo = false;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && FVector::Dist2D(It->GetActorLocation(), GS->FlagLocation) < 250.f)
		{
			(It->GetTeam() == ECampusTeam::Alpha ? bAlpha : bBravo) = true;
		}
	}
	if (bAlpha != bBravo)
	{
		const ECampusTeam Team = bAlpha ? ECampusTeam::Alpha : ECampusTeam::Bravo;
		if (GS->FlagTeam != Team)
		{
			GS->FlagTeam = Team;
			GS->FlagProgress = 0.f;
		}
		GS->FlagProgress += DeltaSeconds / CaptureTime;
		if (GS->FlagProgress >= 1.f)
		{
			EndRound(Team);
		}
	}
}

void ACampusGameMode_Gunfight::NotifyKilled(ACampusCharacter* Victim, AController* Killer, FName WeaponId, bool bHeadshot)
{
	Super::NotifyKilled(Victim, Killer, WeaponId, bHeadshot);
	CheckElimination();
}

void ACampusGameMode_Gunfight::CheckElimination()
{
	const ACampusGameState* GS = GetCampusGameState();
	if (!GS || GS->Phase != ECampusMatchPhase::Playing)
	{
		return;
	}
	const int32 A = GetAliveCount(ECampusTeam::Alpha);
	const int32 B = GetAliveCount(ECampusTeam::Bravo);
	if (A == 0 && B == 0)
	{
		EndRound(ECampusTeam::None);
	}
	else if (A == 0)
	{
		EndRound(ECampusTeam::Bravo);
	}
	else if (B == 0)
	{
		EndRound(ECampusTeam::Alpha);
	}
}

void ACampusGameMode_Gunfight::EndRound(ECampusTeam Winner)
{
	ACampusGameState* GS = GetCampusGameState();
	if (!GS || GS->Phase != ECampusMatchPhase::Playing)
	{
		return;
	}
	GS->RoundWinner = Winner;
	GS->bOvertime = false;
	if (Winner != ECampusTeam::None)
	{
		const int32 Index = (int32)Winner;
		++GS->RoundWins[Index];
		GS->TeamScores[Index] = GS->RoundWins[Index];
	}
	for (APlayerState* PS : GS->PlayerArray)
	{
		ACampusPlayerState* CPS = Cast<ACampusPlayerState>(PS);
		if (CPS && Winner != ECampusTeam::None)
		{
			AwardScore(CPS, CPS->Team == Winner ? ECampusScoreEvent::RoundWin : ECampusScoreEvent::RoundLoss, CPS->Team == Winner ? 250 : 0);
		}
	}
	if (Winner != ECampusTeam::None && GS->RoundWins[(int32)Winner] >= ScoreLimit)
	{
		EndMatch(Winner);
		return;
	}
	SetAllFrozen(true);
	SetPhase(ECampusMatchPhase::RoundOver, RoundOverTime);
}

float ACampusGameMode_Gunfight::GetTeamHealth(ECampusTeam Team) const
{
	float Total = 0.f;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() == Team)
		{
			Total += It->Health;
		}
	}
	return Total;
}

int32 ACampusGameMode_Gunfight::GetAliveCount(ECampusTeam Team) const
{
	int32 Count = 0;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() == Team && It->GetController())
		{
			++Count;
		}
	}
	return Count;
}
