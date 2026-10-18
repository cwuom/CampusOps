#include "Game/CampusGameModeBase.h"
#include "Game/CampusGameState.h"
#include "Game/CampusPlayerState.h"
#include "AI/CampusBotController.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusPlayerController.h"
#include "Player/CampusHUD.h"
#include "Map/CampusSchoolBuilder.h"
#include "Game/CampusMaps.h"
#include "CampusArmory.h"
#include "CampusAssets.h"
#include "CampusOps.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"

namespace
{
	const TCHAR* GBotNames[] = {
		TEXT("志明"), TEXT("春嬌"), TEXT("家豪"), TEXT("怡君"), TEXT("俊傑"), TEXT("雅婷"), TEXT("冠宇"), TEXT("宗翰"),
		TEXT("柏翰"), TEXT("欣怡"), TEXT("承恩"), TEXT("品妍"), TEXT("子軒"), TEXT("詩涵"), TEXT("宇翔"), TEXT("佳穎"),
		TEXT("建宏"), TEXT("淑芬"), TEXT("文雄"), TEXT("美玲")
	};
}

ACampusGameModeBase::ACampusGameModeBase()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ACampusCharacter::StaticClass();
	PlayerControllerClass = ACampusPlayerController::StaticClass();
	PlayerStateClass = ACampusPlayerState::StaticClass();
	GameStateClass = ACampusGameState::StaticClass();
	HUDClass = ACampusHUD::StaticClass();
	bUseSeamlessTravel = false;
}

void ACampusGameModeBase::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	if (UGameplayStatics::HasOption(Options, TEXT("Bots")))
	{
		bFillWithBots = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), 1) != 0;
	}
	TeamSize = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("TeamSize"), TeamSize), 1, 6);
	BotSkill = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Skill"), BotSkill), 0, 3);
	ScoreLimit = FMath::Max(1, UGameplayStatics::GetIntOption(Options, TEXT("ScoreLimit"), ScoreLimit));
	MatchDuration = FMath::Max(30.f, (float)UGameplayStatics::GetIntOption(Options, TEXT("TimeLimit"), (int32)MatchDuration));

	Rules.bCustom = UGameplayStatics::GetIntOption(Options, TEXT("Custom"), 0) != 0;
	if (Rules.bCustom)
	{
		Rules.bGodMode = UGameplayStatics::GetIntOption(Options, TEXT("God"), 0) != 0;
		Rules.PlayerHealth = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("HP"), 100), 10, 1000);
		Rules.BotHealth = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("BotHP"), 100), 10, 1000);
		Rules.bNoReload = UGameplayStatics::GetIntOption(Options, TEXT("NoReload"), 0) != 0;
		Rules.bInfiniteAmmo = UGameplayStatics::GetIntOption(Options, TEXT("InfAmmo"), 0) != 0;
		Rules.bInfiniteEquipment = UGameplayStatics::GetIntOption(Options, TEXT("InfGear"), 0) != 0;
		Rules.bHealthRegen = UGameplayStatics::GetIntOption(Options, TEXT("Regen"), 1) != 0;
	}
	bHealthRegen = bHealthRegen && Rules.bHealthRegen;

	// Weather -1 / time -1 roll by the map's odds, time -2 takes the map's own default; absent means rolled.
	FRandomStream Stream(FMath::Rand());
	const ACampusSchoolBuilder* Builder = ACampusSchoolBuilder::Find(GetWorld());
	PlayedMap = Builder ? Builder->MapKind : CampusMaps::FromPackage(MapName);
	Weather = CampusMaps::ResolveWeather(PlayedMap, UGameplayStatics::GetIntOption(Options, TEXT("Weather"), -1), Stream);
	TimeOfDay = CampusMaps::ResolveTime(PlayedMap, UGameplayStatics::GetIntOption(Options, TEXT("Time"), -1), Stream);
	WeatherSeed = Stream.RandHelper(1 << 30);
	UE_LOG(LogCampus, Display, TEXT("Map %s: %s"), *CampusMaps::Get(PlayedMap).Name.ToString(), *CampusMaps::ConditionsLine(Weather, TimeOfDay).ToString());
}

void ACampusGameModeBase::InitGameState()
{
	Super::InitGameState();
	if (ACampusGameState* GS = GetCampusGameState())
	{
		GS->Mode = MatchMode;
		GS->ScoreLimit = ScoreLimit;
		GS->Phase = ECampusMatchPhase::Warmup;
		GS->Rules = Rules;
		GS->Map = PlayedMap;
		GS->Weather = Weather;
		GS->TimeOfDay = TimeOfDay;
		GS->WeatherSeed = WeatherSeed;
	}
}

ACampusGameState* ACampusGameModeBase::GetCampusGameState() const
{
	return GetGameState<ACampusGameState>();
}

ACampusSchoolBuilder* ACampusGameModeBase::GetBuilder() const
{
	return ACampusSchoolBuilder::Find(GetWorld());
}

const TArray<FVector>& ACampusGameModeBase::GetBotHotspots() const
{
	static const TArray<FVector> Empty;
	const ACampusSchoolBuilder* B = GetBuilder();
	if (!B)
	{
		return Empty;
	}
	return UseGunfightSpawns() ? B->GunfightHotspots : B->Hotspots;
}

void ACampusGameModeBase::ReportTeamIntel(ECampusTeam Team, const FVector& Location)
{
	if (Team == ECampusTeam::Alpha || Team == ECampusTeam::Bravo)
	{
		TeamIntelLocation[(int32)Team] = Location;
		TeamIntelTime[(int32)Team] = GetWorld()->GetTimeSeconds();
	}
}

bool ACampusGameModeBase::GetTeamIntel(ECampusTeam Team, FVector& OutLocation, float MaxAge) const
{
	if (Team != ECampusTeam::Alpha && Team != ECampusTeam::Bravo)
	{
		return false;
	}
	if (GetWorld()->GetTimeSeconds() - TeamIntelTime[(int32)Team] > MaxAge)
	{
		return false;
	}
	OutLocation = TeamIntelLocation[(int32)Team];
	return true;
}

void ACampusGameModeBase::StartPlay()
{
	Super::StartPlay();
	if (ACampusSchoolBuilder* B = GetBuilder())
	{
		B->SetGunfightMode(UseGunfightSpawns());
	}
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		const int32 NumBounds = Nav->GetNavigationBounds().Num();
		ANavigationData* NavData = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (!NavData && NumBounds > 0)
		{
			NavData = Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
		}
		const double BuildStart = FPlatformTime::Seconds();
		if (NavData)
		{
			Nav->Build();
		}
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData);
		UE_LOG(LogCampus, Display, TEXT("Navigation: %d bounds, nav data %s, %d tiles, built in %.2fs"), NumBounds, *GetNameSafe(NavData),
			Recast ? Recast->GetNavMeshTilesCount() : -1, FPlatformTime::Seconds() - BuildStart);
	}
	else
	{
		UE_LOG(LogCampus, Warning, TEXT("Navigation: no navigation system in this world"));
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACampusPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ACampusPlayerState>() : nullptr)
		{
			if (PS->Team == ECampusTeam::None)
			{
				PS->Team = PickTeamForNewPlayer();
			}
		}
	}
	FillBots();
	SetPhase(ECampusMatchPhase::Warmup, 1.5f);
}

void ACampusGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	ACampusPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	if (PS->Team == ECampusTeam::None)
	{
		PS->Team = PickTeamForNewPlayer();
	}
	if (CountTeam(PS->Team, false) > TeamSize)
	{
		RemoveBotFromTeam(PS->Team);
	}

	const ACampusGameState* GS = GetCampusGameState();
	if (GS && GS->Phase == ECampusMatchPhase::Playing && AllowRespawn())
	{
		QueueRespawn(NewPlayer, 0.5f);
	}
}

void ACampusGameModeBase::Logout(AController* Exiting)
{
	RespawnQueue.Remove(Exiting);
	Super::Logout(Exiting);
	if (!bMatchEnded && Cast<APlayerController>(Exiting))
	{
		FillBots();
	}
}

void ACampusGameModeBase::SetPlayerProfile(AController* Controller, const FCampusLoadout& Loadout, int32 OperatorIndex, const FString& PlayerName)
{
	ACampusPlayerState* PS = Controller ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	FCampusLoadout Clean = Loadout;
	UCampusArmory::Get().SanitizeLoadout(Clean);
	PS->Loadout = Clean;
	PS->bHasLoadout = true;
	PS->OperatorIndex = FMath::Clamp(OperatorIndex, 0, 1);
	if (!PlayerName.IsEmpty())
	{
		ChangeName(Controller, PlayerName.Left(16), false);
	}
}

FCampusLoadout ACampusGameModeBase::GetLoadoutFor(AController* Controller) const
{
	const ACampusPlayerState* PS = Controller ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (PS && PS->bHasLoadout)
	{
		return PS->Loadout;
	}
	return UCampusArmory::Get().MakeDefaultLoadout(0);
}

// ------------------------------------------------------------------ phases

void ACampusGameModeBase::SetPhase(ECampusMatchPhase NewPhase, float Duration)
{
	if (ACampusGameState* GS = GetCampusGameState())
	{
		GS->Phase = NewPhase;
		GS->PhaseEndTime = (float)GS->GetServerWorldTimeSeconds() + Duration;
	}
}

void ACampusGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACampusGameState* GS = GetCampusGameState();
	if (!GS)
	{
		return;
	}
	if (GS->GetServerWorldTimeSeconds() >= GS->PhaseEndTime)
	{
		OnPhaseExpired(GS->Phase);
	}

	const double Now = GetWorld()->GetTimeSeconds();
	TArray<TWeakObjectPtr<AController>> Ready;
	for (const TPair<TWeakObjectPtr<AController>, double>& Pair : RespawnQueue)
	{
		if (Now >= Pair.Value)
		{
			Ready.Add(Pair.Key);
		}
	}
	for (const TWeakObjectPtr<AController>& C : Ready)
	{
		RespawnQueue.Remove(C);
		if (AController* Controller = C.Get())
		{
			if (GS->Phase == ECampusMatchPhase::Playing || GS->Phase == ECampusMatchPhase::Countdown)
			{
				if (APawn* Old = Controller->GetPawn())
				{
					Controller->UnPossess();
				}
				RestartPlayer(Controller);
			}
		}
	}
}

void ACampusGameModeBase::OnPhaseExpired(ECampusMatchPhase Phase)
{
	switch (Phase)
	{
	case ECampusMatchPhase::Warmup:
		// Nobody spawns and the countdown does not run while a player is still loading (a stuck client holds it a minute at most).
		if (!AllPlayersLoaded() && GetWorld()->GetTimeSeconds() < 60.f)
		{
			SetPhase(ECampusMatchPhase::Warmup, 0.2f);
			break;
		}
		BeginCountdown();
		break;
	case ECampusMatchPhase::Countdown:
		BeginPlaying();
		break;
	case ECampusMatchPhase::Playing:
	{
		const ACampusGameState* GS = GetCampusGameState();
		const int32 A = GS ? GS->GetTeamScore(ECampusTeam::Alpha) : 0;
		const int32 B = GS ? GS->GetTeamScore(ECampusTeam::Bravo) : 0;
		EndMatch(A == B ? ECampusTeam::None : (A > B ? ECampusTeam::Alpha : ECampusTeam::Bravo));
		break;
	}
	case ECampusMatchPhase::MatchOver:
		ReturnToMenu();
		SetPhase(ECampusMatchPhase::MatchOver, 9999.f);
		break;
	default:
		break;
	}
}

bool ACampusGameModeBase::AllPlayersLoaded() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ACampusPlayerController* PC = Cast<ACampusPlayerController>(It->Get()); PC && !PC->HasClientLoaded())
		{
			return false;
		}
	}
	return true;
}

void ACampusGameModeBase::BeginCountdown()
{
	SpawnAllWithoutPawn(true);
	SetAllFrozen(true);
	SetPhase(ECampusMatchPhase::Countdown, CountdownDuration);
}

void ACampusGameModeBase::BeginPlaying()
{
	SetAllFrozen(false);
	SetPhase(ECampusMatchPhase::Playing, MatchDuration);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(It->Get()))
		{
			PC->ClientMatchStarted();
		}
	}
}

void ACampusGameModeBase::EndMatch(ECampusTeam Winner)
{
	if (bMatchEnded)
	{
		return;
	}
	bMatchEnded = true;
	RespawnQueue.Reset();
	if (ACampusGameState* GS = GetCampusGameState())
	{
		GS->WinningTeam = Winner;
	}
	SetAllFrozen(true);
	SetPhase(ECampusMatchPhase::MatchOver, PostMatchDuration);
}

void ACampusGameModeBase::SetAllFrozen(bool bFrozen)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(It->Get()))
		{
			PC->ClientSetFrozen(bFrozen);
		}
	}
}

void ACampusGameModeBase::ReturnToMenu()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(It->Get()))
		{
			if (!PC->IsLocalController())
			{
				PC->ClientReturnToMenu();
			}
		}
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		GetWorld()->ServerTravel(TEXT("?Restart"), true);
		return;
	}
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UGameplayStatics::OpenLevel(this, FName(CampusAssets::MapMenu), true, TEXT("game=Menu"));
	}), 0.4f, false);
}

void ACampusGameModeBase::SpawnAllWithoutPawn(bool bPlayIntro)
{
	TArray<AController*> Controllers;
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* C = It->Get();
		if (C && C->GetPlayerState<ACampusPlayerState>())
		{
			Controllers.Add(C);
		}
	}
	for (AController* C : Controllers)
	{
		APawn* Pawn = C->GetPawn();
		const ACampusCharacter* Char = Cast<ACampusCharacter>(Pawn);
		if (!Pawn || (Char && !Char->IsAlive()))
		{
			if (Pawn)
			{
				C->UnPossess();
			}
			RestartPlayer(C);
		}
		if (bPlayIntro)
		{
			if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(C))
			{
				PC->ClientMatchIntro(CountdownDuration);
			}
		}
	}
}

void ACampusGameModeBase::KillAllPawns()
{
	TArray<APawn*> Pawns;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		Pawns.Add(*It);
	}
	for (APawn* P : Pawns)
	{
		if (AController* C = P->GetController())
		{
			C->UnPossess();
		}
		P->Destroy();
	}
}

void ACampusGameModeBase::QueueRespawn(AController* Controller, float Delay)
{
	if (Controller)
	{
		RespawnQueue.Add(Controller, GetWorld()->GetTimeSeconds() + Delay);
	}
}

// ------------------------------------------------------------------ spawning

void ACampusGameModeBase::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || !IsValid(NewPlayer))
	{
		return;
	}
	FTransform SpawnTransform;
	if (PickSpawn(NewPlayer, SpawnTransform))
	{
		RestartPlayerAtTransform(NewPlayer, SpawnTransform);
	}
	else
	{
		Super::RestartPlayer(NewPlayer);
	}
}

void ACampusGameModeBase::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	ACampusCharacter* Char = Cast<ACampusCharacter>(PlayerPawn);
	if (!Char)
	{
		return;
	}
	Char->bHealthRegen = bHealthRegen;
	const ACampusPlayerState* PS = Char->GetCampusPlayerState();
	const bool bBot = PS && PS->IsABot();
	Char->MaxHealth = (float)(bBot ? Rules.BotHealth : Rules.PlayerHealth);
	Char->Health = Char->MaxHealth;
	Char->bInvulnerable = Rules.bGodMode && !bBot;
	if (PS)
	{
		Char->ApplyOperatorMesh(PS->OperatorIndex);
	}
	Char->GiveLoadout(GetLoadoutFor(Char->GetController()));
}

bool ACampusGameModeBase::PickSpawn(AController* Controller, FTransform& Out) const
{
	const ACampusSchoolBuilder* Builder = GetBuilder();
	const ACampusPlayerState* PS = Controller ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!Builder || !PS || Builder->SpawnPoints.Num() == 0)
	{
		return false;
	}
	const ACampusGameState* GS = GetCampusGameState();
	const bool bGunfight = UseGunfightSpawns();
	const bool bInitial = !GS || GS->Phase != ECampusMatchPhase::Playing;

	TArray<ACampusCharacter*> Alive;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetController() != Controller)
		{
			Alive.Add(*It);
		}
	}

	float BestScore = -FLT_MAX;
	const FCampusSpawnPoint* Best = nullptr;
	for (const FCampusSpawnPoint& SP : Builder->SpawnPoints)
	{
		if (SP.bGunfight != bGunfight)
		{
			continue;
		}
		if ((bGunfight || bInitial) && SP.Team != PS->Team)
		{
			continue;
		}
		const FVector Loc = SP.Transform.GetLocation();
		float MinEnemy = 6000.f;
		float MinFriend = 6000.f;
		bool bOccupied = false;
		bool bSeen = false;
		for (const ACampusCharacter* Other : Alive)
		{
			const float D = FVector::Dist(Other->GetActorLocation(), Loc);
			if (D < 120.f)
			{
				bOccupied = true;
			}
			if (Other->GetTeam() == PS->Team)
			{
				MinFriend = FMath::Min(MinFriend, D);
			}
			else
			{
				MinEnemy = FMath::Min(MinEnemy, D);
				if (!bInitial && D < 4500.f)
				{
					FHitResult Hit;
					FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusSpawnLOS), false, Other);
					const FVector Eye = Loc + FVector(0.f, 0.f, 160.f);
					if (!GetWorld()->LineTraceSingleByChannel(Hit, Other->GetPawnViewLocation(), Eye, ECC_Visibility, Params))
					{
						bSeen = true;
					}
				}
			}
		}
		float Score = FMath::Min(MinEnemy, 4500.f) + FMath::FRandRange(0.f, 400.f);
		if (bSeen) Score -= 4000.f;
		if (bOccupied) Score -= 8000.f;
		if (!bInitial)
		{
			if (SP.Team == PS->Team) Score += 900.f;
			else if (SP.Team != ECampusTeam::None) Score -= 1500.f;
			if (MinFriend < 2500.f) Score += 400.f;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &SP;
		}
	}
	if (!Best)
	{
		return false;
	}
	Out = Best->Transform;
	return true;
}

ECampusTeam ACampusGameModeBase::PickTeamForNewPlayer() const
{
	const int32 A = CountTeam(ECampusTeam::Alpha, true);
	const int32 B = CountTeam(ECampusTeam::Bravo, true);
	return A <= B ? ECampusTeam::Alpha : ECampusTeam::Bravo;
}

int32 ACampusGameModeBase::CountTeam(ECampusTeam Team, bool bHumansOnly) const
{
	int32 Count = 0;
	if (const AGameStateBase* GS = GameState)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const ACampusPlayerState* CPS = Cast<ACampusPlayerState>(PS);
			if (CPS && CPS->Team == Team && (!bHumansOnly || !CPS->IsABot()))
			{
				++Count;
			}
		}
	}
	return Count;
}

ACampusBotController* ACampusGameModeBase::AddBot(ECampusTeam Team)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACampusBotController* Bot = GetWorld()->SpawnActor<ACampusBotController>(ACampusBotController::StaticClass(), FTransform::Identity, Params);
	if (!Bot)
	{
		return nullptr;
	}
	if (ACampusPlayerState* PS = Bot->GetPlayerState<ACampusPlayerState>())
	{
		const int32 Index = BotCounter++ % UE_ARRAY_COUNT(GBotNames);
		PS->SetPlayerName(GBotNames[Index]);
		PS->Team = Team;
		PS->SetIsABot(true);
		FRandomStream Stream(FMath::Rand());
		PS->Loadout = UCampusArmory::Get().MakeDefaultLoadout(Stream.RandRange(0, 2));
		PS->bHasLoadout = true;
		PS->OperatorIndex = Stream.RandRange(0, 1);
	}
	Bot->Skill = BotSkill;

	const ACampusGameState* GS = GetCampusGameState();
	if (GS && (GS->Phase == ECampusMatchPhase::Playing) && AllowRespawn())
	{
		QueueRespawn(Bot, 0.2f);
	}
	return Bot;
}

void ACampusGameModeBase::FillBots()
{
	if (!bFillWithBots)
	{
		return;
	}
	for (ECampusTeam Team : { ECampusTeam::Alpha, ECampusTeam::Bravo })
	{
		int32 Guard = 0;
		while (CountTeam(Team, false) < TeamSize && Guard++ < 12)
		{
			AddBot(Team);
		}
	}
}

void ACampusGameModeBase::RemoveBotFromTeam(ECampusTeam Team)
{
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		ACampusBotController* Bot = Cast<ACampusBotController>(It->Get());
		const ACampusPlayerState* PS = Bot ? Bot->GetPlayerState<ACampusPlayerState>() : nullptr;
		if (PS && PS->Team == Team)
		{
			RespawnQueue.Remove(Bot);
			if (APawn* Pawn = Bot->GetPawn())
			{
				Pawn->Destroy();
			}
			Bot->Destroy();
			return;
		}
	}
}

// ------------------------------------------------------------------ combat events

bool ACampusGameModeBase::CanDamage(const ACampusCharacter* Victim, const AController* InstigatorController) const
{
	const ACampusGameState* GS = GetCampusGameState();
	if (GS && GS->Phase != ECampusMatchPhase::Playing)
	{
		return false;
	}
	if (!InstigatorController || InstigatorController == Victim->GetController())
	{
		return true;
	}
	const ACampusPlayerState* IPS = InstigatorController->GetPlayerState<ACampusPlayerState>();
	return !IPS || IPS->Team != Victim->GetTeam();
}

void ACampusGameModeBase::NotifyDamage(ACampusCharacter* Victim, AController* InstigatorController, float Damage)
{
	ACampusPlayerState* VPS = Victim ? Victim->GetCampusPlayerState() : nullptr;
	APlayerState* IPS = InstigatorController ? InstigatorController->PlayerState.Get() : nullptr;
	if (VPS && IPS && IPS != VPS)
	{
		VPS->RecentAttackers.Add(IPS, GetWorld()->GetTimeSeconds());
	}
	if (ACampusBotController* Bot = Cast<ACampusBotController>(Victim ? Victim->GetController() : nullptr))
	{
		Bot->OnDamagedBy(InstigatorController);
	}
}

void ACampusGameModeBase::NotifyGunshot(ACampusCharacter* Shooter, const FVector& Location, bool bSuppressed)
{
	const float Radius = (bSuppressed ? 900.f : 4500.f) * (Shooter && Shooter->HasPerk(ECampusPerk::Ghost) ? 0.4f : 1.f);
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		if (ACampusBotController* Bot = Cast<ACampusBotController>(It->Get()))
		{
			Bot->HearGunshot(Shooter, Location, Radius);
		}
	}
}

void ACampusGameModeBase::AwardScore(ACampusPlayerState* PS, ECampusScoreEvent Event, int32 Points)
{
	if (!PS)
	{
		return;
	}
	PS->Points += Points;
	if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(PS->GetOwner()))
	{
		PC->ClientScoreEvent(Event, Points);
	}
}

void ACampusGameModeBase::NotifyKilled(ACampusCharacter* Victim, AController* Killer, FName WeaponId, bool bHeadshot)
{
	ACampusPlayerState* VPS = Victim ? Victim->GetCampusPlayerState() : nullptr;
	ACampusPlayerState* KPS = Killer ? Killer->GetPlayerState<ACampusPlayerState>() : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();

	if (VPS)
	{
		++VPS->Deaths;
		VPS->Streak = 0;
	}

	const bool bValidKill = KPS && VPS && KPS != VPS && KPS->Team != VPS->Team;
	if (bValidKill)
	{
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(Killer))
		{
			PC->ClientKillNotice(VPS->GetPlayerName(), bHeadshot);
		}
		++KPS->Kills;
		++KPS->Streak;
		AwardScore(KPS, ECampusScoreEvent::Kill, 100);
		if (bHeadshot)
		{
			AwardScore(KPS, ECampusScoreEvent::Headshot, 50);
		}
		if (WeaponId == TEXT("Melee"))
		{
			AwardScore(KPS, ECampusScoreEvent::Melee, 50);
		}
		if (!bFirstBloodTaken)
		{
			bFirstBloodTaken = true;
			AwardScore(KPS, ECampusScoreEvent::FirstBlood, 100);
		}
		KPS->MultiKillCount = (Now - KPS->LastKillTime < 4.f) ? KPS->MultiKillCount + 1 : 1;
		KPS->LastKillTime = Now;
		if (KPS->MultiKillCount == 2)
		{
			AwardScore(KPS, ECampusScoreEvent::DoubleKill, 50);
		}
		else if (KPS->MultiKillCount == 3)
		{
			AwardScore(KPS, ECampusScoreEvent::TripleKill, 100);
		}
		if (KPS->Streak == 5)
		{
			AwardScore(KPS, ECampusScoreEvent::Streak5, 150);
		}
		if (const APawn* KillerPawn = Killer->GetPawn())
		{
			if (FVector::Dist(KillerPawn->GetActorLocation(), Victim->GetActorLocation()) > 4000.f)
			{
				AwardScore(KPS, ECampusScoreEvent::Longshot, 50);
			}
		}
		if (KPS->LastKiller.Get() == VPS)
		{
			AwardScore(KPS, ECampusScoreEvent::Revenge, 50);
			KPS->LastKiller = nullptr;
		}
		VPS->LastKiller = KPS;
		OnTeamKill(KPS, VPS);
		if (ACampusCharacter* KillerChar = Cast<ACampusCharacter>(Killer->GetPawn()))
		{
			KillerChar->OnScoredKill();
		}
	}

	if (VPS)
	{
		for (const TPair<TWeakObjectPtr<APlayerState>, float>& Pair : VPS->RecentAttackers)
		{
			ACampusPlayerState* Assister = Cast<ACampusPlayerState>(Pair.Key.Get());
			if (Assister && Assister != KPS && Assister->Team != VPS->Team && Now - Pair.Value < 6.f)
			{
				++Assister->Assists;
				AwardScore(Assister, ECampusScoreEvent::Assist, 50);
			}
		}
		VPS->RecentAttackers.Reset();
	}

	if (ACampusGameState* GS = GetCampusGameState())
	{
		FCampusKillFeedEntry Entry;
		Entry.KillerName = KPS ? KPS->GetPlayerName() : FString();
		Entry.KillerTeam = KPS ? KPS->Team : ECampusTeam::None;
		Entry.VictimName = VPS ? VPS->GetPlayerName() : FString();
		Entry.VictimTeam = VPS ? VPS->Team : ECampusTeam::None;
		Entry.WeaponId = WeaponId;
		Entry.bHeadshot = bHeadshot;
		GS->MulticastKillFeed(Entry);
	}

	if (Victim && VPS)
	{
		const APawn* KillerPawn = Killer ? Killer->GetPawn() : nullptr;
		const FVector KillerAt = KillerPawn ? KillerPawn->GetActorLocation() : Victim->GetActorLocation();
		for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
		{
			ACampusBotController* Bot = Cast<ACampusBotController>(It->Get());
			const ACampusPlayerState* BPS = Bot ? Bot->GetPlayerState<ACampusPlayerState>() : nullptr;
			if (BPS && BPS->Team == VPS->Team && Bot != Victim->GetController())
			{
				Bot->OnTeammateKilled(Victim->GetActorLocation(), KillerAt);
			}
		}
	}

	if (AllowRespawn() && Victim && Victim->GetController())
	{
		QueueRespawn(Victim->GetController(), RespawnDelay);
	}
}
