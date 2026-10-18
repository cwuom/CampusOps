#include "Player/CampusPlayerController.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusHUD.h"
#include "Player/CampusInputConfig.h"
#include "Player/CampusPlayerCameraManager.h"
#include "Player/CampusViewmodelComponent.h"
#include "Game/CampusGameModeBase.h"
#include "Game/CampusGameInstance.h"
#include "UI/SCampusPauseMenu.h"
#include "CampusAssets.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SWeakWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "ShaderCompiler.h"
#include "ShaderPipelineCache.h"
#include "PipelineStateCache.h"
#include "ContentStreaming.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "CampusLoading"

ACampusPlayerController::ACampusPlayerController()
{
	PlayerCameraManagerClass = ACampusPlayerCameraManager::StaticClass();
}

void ACampusPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Sub->AddMappingContext(UCampusInputConfig::Get()->Context, 0);
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	SendProfileToServer();
	ApplyProfileSettings();
}

void ACampusPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetPauseMenuOpen(false);
	Super::EndPlay(EndPlayReason);
}

void ACampusPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		const UCampusInputConfig* Cfg = UCampusInputConfig::Get();
		Input->BindAction(Cfg->Scoreboard, ETriggerEvent::Started, this, &ACampusPlayerController::OnScoreboardPressed);
		Input->BindAction(Cfg->Scoreboard, ETriggerEvent::Completed, this, &ACampusPlayerController::OnScoreboardReleased);
		Input->BindAction(Cfg->Pause, ETriggerEvent::Started, this, &ACampusPlayerController::OnPausePressed);
	}
}

void ACampusPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (IsLocalController())
	{
		ApplyProfileSettings();
	}
}

void ACampusPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
	ApplyProfileSettings();
}

void ACampusPlayerController::ApplyProfileSettings()
{
	const UCampusGameInstance* GI = Cast<UCampusGameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}
	const FCampusProfile& P = GI->GetProfile();
	if (ACampusCharacter* C = GetPawn<ACampusCharacter>())
	{
		C->MouseSensitivity = P.MouseSensitivity;
		C->ADSSensitivityMultiplier = P.ADSSensitivityMultiplier;
		C->GamepadSensitivity = P.GamepadSensitivity;
		C->bInvertY = P.bInvertY;
		C->bToggleAim = P.bToggleAim;
		C->bToggleSprint = P.bToggleSprint;
		C->bAutoTacSprint = P.bAutoTacSprint;
		C->BaseFOV = P.FieldOfView;
		C->BaseViewmodelFOV = P.ViewmodelFOV;
	}
	UCampusGameInstance::ApplyAudioVolume(this, P.MasterVolume);
	AppliedDuck = -1.f;
}

void ACampusPlayerController::SendProfileToServer()
{
	const UCampusGameInstance* GI = Cast<UCampusGameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}
	const FCampusProfile& P = GI->GetProfile();
	ServerSetProfile(GI->GetActiveLoadout(), P.OperatorIndex, P.PlayerName);
}

void ACampusPlayerController::ServerSetProfile_Implementation(const FCampusLoadout& Loadout, int32 OperatorIndex, const FString& InPlayerName)
{
	if (ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>())
	{
		GM->SetPlayerProfile(this, Loadout, OperatorIndex, InPlayerName);
	}
}

ACampusHUD* ACampusPlayerController::GetCampusHUD() const
{
	return Cast<ACampusHUD>(GetHUD());
}

void ACampusPlayerController::ShowLocalHitmarker(bool bHeadshot, float Damage)
{
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->AddHitmarker(false, bHeadshot, Damage);
	}
	CampusAudio::Play2D(this, bHeadshot ? TEXT("Hit_Head") : TEXT("Hitmarker"), bHeadshot ? 0.9f : 0.75f, 0.03f, CampusAudio::ECategory::Interface);
}

void ACampusPlayerController::ClientHitConfirmed_Implementation(bool bKill, bool bHeadshot, float Damage)
{
	if (!bKill)
	{
		return;
	}
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->AddHitmarker(true, bHeadshot, Damage);
	}
	// On top of the hit sound of the killing round: the confirm, its own take for a headshot.
	CampusAudio::Play2D(this, bHeadshot ? TEXT("Kill_Headshot") : TEXT("Kill_Confirm"), 1.f, 0.02f, CampusAudio::ECategory::Interface);
	CampusAudio::Play2D(this, TEXT("Hit_Kill"), 0.55f, 0.02f, CampusAudio::ECategory::Interface);
	if (ACampusCharacter* C = GetPawn<ACampusCharacter>(); C && C->HasSpecialty(ECampusPerkGroup::Recon))
	{
		C->RevealUntil = GetWorld()->GetTimeSeconds() + 3.f;
	}
}

void ACampusPlayerController::ClientKillNotice_Implementation(const FString& VictimName, bool bHeadshot)
{
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->AddKillNotice(VictimName, bHeadshot);
	}
}

void ACampusPlayerController::ClientFlashbanged_Implementation(float Blind, float Deaf, FVector_NetQuantize From)
{
	if (Blind > 0.02f)
	{
		if (ACampusCharacter* C = GetPawn<ACampusCharacter>(); C && C->Viewmodel)
		{
			C->Viewmodel->OnFlashbanged(Blind, From);
		}
	}
	if (Deaf > 0.02f)
	{
		OnDeafened(Deaf, true);
	}
}

void ACampusPlayerController::ClientEquipmentHit_Implementation(float Damage)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Damage < 25.f && Now - LastEquipmentHitTime < 0.28f)
	{
		return;
	}
	LastEquipmentHitTime = Now;
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->AddHitmarker(false, false, Damage);
	}
	CampusAudio::Play2D(this, TEXT("Hitmarker"), 0.7f, 0.03f, CampusAudio::ECategory::Interface);
}

void ACampusPlayerController::OnDeafened(float Amount, bool bFlash)
{
	Amount = FMath::Clamp(Amount, 0.f, 1.f);
	if (Amount < Deafness * 0.7f)
	{
		return;
	}
	Deafness = FMath::Max(Deafness, Amount);
	// A flashbang leaves the ears ringing for several seconds; a frag's blast a little less.
	DeafRecoverRate = bFlash ? 0.15f : 0.26f;
	CampusAudio::Play2D(this, TEXT("Tinnitus"), 0.8f + 2.2f * Amount, 0.f);
}

bool ACampusPlayerController::IsLookInputIgnored() const
{
	// The view is held only while the camera drops in from the sky (a mouse moved meanwhile would jerk it at the end).
	// Once it has landed the player may look around through the rest of the frozen countdown; only the result screen
	// holds the view again.
	if (Super::IsLookInputIgnored())
	{
		return true;
	}
	if (!IsLocalController())
	{
		return false;
	}
	const ACampusGameState* GS = GetWorld() ? GetWorld()->GetGameState<ACampusGameState>() : nullptr;
	const bool bOver = GS && GS->Phase == ECampusMatchPhase::MatchOver;
	return !bWorldLoaded || IsIntroActive() || bOver || !GetPawn();
}

bool ACampusPlayerController::IsMoveInputIgnored() const
{
	return Super::IsMoveInputIgnored() || (IsLocalController() && (!bWorldLoaded || IsPawnSettling() || IsIntroActive()));
}

bool ACampusPlayerController::IsPawnSettling() const
{
	const APawn* P = GetPawn();
	return P && P != SettledPawn.Get();
}

void ACampusPlayerController::ServerReportLoaded_Implementation()
{
	if (!bClientLoaded)
	{
		bClientLoaded = true;
		UE_LOG(LogTemp, Display, TEXT("[CampusLoad] %s loaded"), *GetName());
	}
}

void ACampusPlayerController::UpdateLoadGate(float DeltaTime)
{
	UWorld* World = GetWorld();
	const double Clock = FPlatformTime::Seconds();
	const int32 Shaders = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	const int32 Pipelines = (int32)FShaderPipelineCache::NumPrecompilesRemaining() + (int32)PipelineStateCache::NumActivePrecacheRequests();
	const bool bPackages = !IsAsyncLoading();
	const bool bSmooth = FApp::GetDeltaTime() < 0.1;

	// The pawn can do nothing at all until the world is in. After that each new one only holds its feet (not the view)
	// until its own meshes and materials are compiled and frames come smoothly again, for a moment at most.
	if (APawn* P = GetPawn(); P && P != SettledPawn.Get())
	{
		if (P != SettlingPawn.Get())
		{
			SettlingPawn = P;
			PawnQuietFrames = 0;
			PawnSettleStart = Clock;
		}
		if (!bWorldLoaded && P->InputEnabled())
		{
			P->DisableInput(this);
			GatedPawn = P;
		}
		const bool bQuiet = bWorldLoaded && Shaders == 0 && Pipelines == 0 && bPackages && bSmooth;
		PawnQuietFrames = bQuiet ? PawnQuietFrames + 1 : 0;
		if (bWorldLoaded && GatedPawn.Get() == P)
		{
			P->EnableInput(this);
			GatedPawn.Reset();
		}
		if (bWorldLoaded && (PawnQuietFrames >= 10 || Clock - PawnSettleStart > 1.5))
		{
			SettledPawn = P;
			SettlingPawn.Reset();
			if (GatedPawn.Get() == P)
			{
				P->EnableInput(this);
			}
			GatedPawn.Reset();
			// Nothing turned while it waited, whatever the mouse did.
			RotationInput = FRotator::ZeroRotator;
		}
	}
	if (bWorldLoaded)
	{
		return;
	}
	if (LoadStartTime < 0.0)
	{
		LoadStartTime = Clock;
	}
	const float Waited = (float)(Clock - LoadStartTime);
	// Test switch: -CampusLoadHold=<seconds> keeps the loading screen up at least that long.
	static const float MinHold = []() { float Hold = 0.f; FParse::Value(FCommandLine::Get(), TEXT("CampusLoadHold="), Hold); return Hold; }();
	bool bLevels = true;
	for (const ULevelStreaming* Level : World->GetStreamingLevels())
	{
		if (Level && Level->ShouldBeLoaded() && (!Level->IsLevelLoaded() || (Level->ShouldBeVisible() && !Level->IsLevelVisible())))
		{
			bLevels = false;
		}
	}
	if (World->GetWorldPartition())
	{
		if (UWorldPartitionSubsystem* WP = World->GetSubsystem<UWorldPartitionSubsystem>(); WP && !WP->IsAllStreamingCompleted())
		{
			bLevels = false;
		}
	}
	// A full streaming pool can leave a few textures wanting for good, so they only hold the gate for a while.
	const int32 Textures = IStreamingManager::Get().GetNumWantingResources();
	const bool bTextures = Textures == 0 || Waited > 12.f;
	const bool bGame = World->GetGameState() != nullptr && PlayerState != nullptr;
	const bool bReady = Shaders == 0 && Pipelines == 0 && bPackages && bLevels && bTextures && bGame && Waited >= MinHold;
	// Then a run of smooth frames, so control comes back with the world actually running, not mid-hitch. A slow
	// machine (or a window in the background, throttled) that never gets there is let in a few seconds after
	// everything is in.
	SmoothReadyFrames = bReady && bSmooth ? SmoothReadyFrames + 1 : 0;
	if (!bReady)
	{
		ReadySince = -1.0;
	}
	else if (ReadySince < 0.0)
	{
		ReadySince = Clock;
	}
	const bool bReadyLongEnough = bReady && ReadySince >= 0.0 && Clock - ReadySince > 3.0;

	PeakShaderJobs = FMath::Max(PeakShaderJobs, Shaders);
	PeakPipelineJobs = FMath::Max(PeakPipelineJobs, Pipelines);
	PeakTextures = FMath::Max(PeakTextures, Textures);
	auto Part = [](int32 Left, int32 Peak) { return Peak > 0 ? 1.f - (float)Left / (float)Peak : 1.f; };
	const float Target = 0.3f * Part(Shaders, PeakShaderJobs) + 0.25f * Part(Pipelines, PeakPipelineJobs) + 0.15f * (bPackages && bLevels ? 1.f : 0.f)
		+ 0.15f * (bTextures ? 1.f : Part(Textures, PeakTextures)) + 0.15f * FMath::Clamp(SmoothReadyFrames / 30.f, 0.f, 1.f);
	LoadProgress = FMath::Max(LoadProgress, FMath::Lerp(LoadProgress, Target, FMath::Clamp(DeltaTime * 6.f, 0.f, 1.f)));
	if (Shaders > 0)
	{
		LoadStatus = FText::Format(LOCTEXT("Shaders", "正在编译着色器  ·  剩余 {0}"), FText::AsNumber(Shaders));
	}
	else if (Pipelines > 0)
	{
		LoadStatus = FText::Format(LOCTEXT("Pipelines", "正在预编译渲染管线  ·  剩余 {0}"), FText::AsNumber(Pipelines));
	}
	else if (!bPackages || !bLevels || !bGame)
	{
		LoadStatus = LOCTEXT("Packages", "正在载入地图与资源");
	}
	else if (!bTextures)
	{
		LoadStatus = FText::Format(LOCTEXT("Textures", "正在流送高清贴图  ·  剩余 {0}"), FText::AsNumber(Textures));
	}
	else
	{
		LoadStatus = LOCTEXT("Settle", "即将完成");
	}

	if (SmoothReadyFrames >= 30 || bReadyLongEnough || Waited > 90.f)
	{
		bWorldLoaded = true;
		LoadProgress = 1.f;
		LoadedTime = World->GetTimeSeconds();
		UE_LOG(LogTemp, Display, TEXT("[CampusLoad] world loaded in %.1fs%s"), Waited, SmoothReadyFrames >= 30 ? TEXT("") : (bReadyLongEnough ? TEXT(" (frames never settled)") : TEXT(" (timed out)")));
		// A pawn already here gets control back through the settle above.
		RotationInput = FRotator::ZeroRotator;
		ServerReportLoaded();
	}
}

void ACampusPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
	{
		return;
	}
	UpdateLoadGate(DeltaTime);
	Deafness = FMath::Max(0.f, Deafness - DeafRecoverRate * DeltaTime);
	// Everything else drops under the ringing and comes back as the hearing does; the world is silent until it has
	// loaded, then fades in.
	const float D = FMath::SmoothStep(0.f, 1.f, Deafness);
	const float Reveal = GetRevealTime();
	const float LoadFade = Reveal >= 0.f ? FMath::SmoothStep(0.f, 1.f, FMath::Clamp(((float)GetWorld()->GetTimeSeconds() - Reveal) / 0.8f, 0.f, 1.f)) : 0.f;
	const float Duck = (1.f - 0.85f * D) * LoadFade;
	if (FMath::Abs(Duck - AppliedDuck) > 0.004f)
	{
		AppliedDuck = Duck;
		const UCampusGameInstance* GI = Cast<UCampusGameInstance>(GetGameInstance());
		UCampusGameInstance::ApplyAudioVolume(this, (GI ? GI->GetProfile().MasterVolume : 0.8f) * Duck);
	}
}

void ACampusPlayerController::ClientOnDamaged_Implementation(FVector_NetQuantize SourceLocation, float Damage)
{
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->AddDamageIndicator(SourceLocation, Damage);
	}
	if (ACampusCharacter* C = GetPawn<ACampusCharacter>())
	{
		C->ApplyAimPunch((C->GetActorLocation() - FVector(SourceLocation)).GetSafeNormal(), Damage);
		C->LastDamageTime = GetWorld()->GetTimeSeconds();
	}
	CampusAudio::Play2D(this, TEXT("Player_Hit"), 0.8f, 0.08f);
}

void ACampusPlayerController::ClientScoreEvent_Implementation(ECampusScoreEvent Event, int32 Points)
{
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->AddScoreEvent(Event, Points);
	}
}

void ACampusPlayerController::ClientSetFrozen_Implementation(bool bInFrozen)
{
	if (bFrozen == bInFrozen)
	{
		return;
	}
	bFrozen = bInFrozen;
	ResetIgnoreMoveInput();
	if (bFrozen)
	{
		SetIgnoreMoveInput(true);
	}
}

void ACampusPlayerController::ClientMatchIntro_Implementation(float Duration)
{
	IntroStartTime = GetWorld()->GetTimeSeconds();
	// The drop fills the countdown: boots on the deck just as the match goes live, never stood frozen on the spot.
	IntroDuration = FMath::Max(1.4f, Duration - 0.25f);
	UE_LOG(LogTemp, Display, TEXT("[CampusIntro] drop starts at %.2f for %.2fs (loaded at %.2f)"), IntroStartTime, IntroDuration, LoadedTime);
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->OnMatchIntro();
	}
	CampusAudio::Play2D(this, TEXT("UI_Deploy"), 0.8f, 0.f, CampusAudio::ECategory::Interface);
}

void ACampusPlayerController::ClientMatchStarted_Implementation()
{
	if (ACampusHUD* HUD = GetCampusHUD())
	{
		HUD->OnMatchStarted();
	}
	CampusAudio::Play2D(this, TEXT("Match_Start"), 0.9f, 0.f, CampusAudio::ECategory::Interface);
}

void ACampusPlayerController::ClientReturnToMenu_Implementation()
{
	UGameplayStatics::OpenLevel(this, FName(CampusAssets::MapMenu), true, TEXT("game=Menu"));
}

float ACampusPlayerController::GetRevealTime() const
{
	if (!bWorldLoaded)
	{
		return -1.f;
	}
	if (IntroStartTime >= LoadedTime - 0.05f)
	{
		return IntroStartTime;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// Joining a match already under way (no drop), or a server that never sends one: let the view in shortly.
	const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>();
	if (Now - LoadedTime > 3.f)
	{
		return LoadedTime + 3.f;
	}
	return GS && GS->Phase == ECampusMatchPhase::Playing && GetPawn() ? LoadedTime : -1.f;
}

bool ACampusPlayerController::IsIntroActive() const
{
	return IntroDuration > 0.f && GetWorld()->GetTimeSeconds() - IntroStartTime < IntroDuration;
}

float ACampusPlayerController::GetIntroAlpha() const
{
	if (IntroDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - IntroStartTime) / IntroDuration, 0.f, 1.f);
}

void ACampusPlayerController::OnScoreboardPressed()
{
	bShowScoreboard = true;
}

void ACampusPlayerController::OnScoreboardReleased()
{
	bShowScoreboard = false;
}

void ACampusPlayerController::OnPausePressed()
{
	SetPauseMenuOpen(!IsPauseMenuOpen());
}

void ACampusPlayerController::SetPauseMenuOpen(bool bOpen)
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (bOpen == IsPauseMenuOpen() || !Viewport)
	{
		return;
	}
	if (bOpen)
	{
		PauseMenu = SNew(SCampusPauseMenu).OwningController(this);
		PauseMenuContainer = SNew(SWeakWidget).PossiblyNullContent(PauseMenu);
		Viewport->AddViewportWidgetContent(PauseMenuContainer.ToSharedRef(), 50);
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(PauseMenu);
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
		if (ACampusCharacter* C = GetPawn<ACampusCharacter>())
		{
			C->BotSetFire(false);
		}
		SetIgnoreLookInput(true);
		CampusAudio::Play2D(this, TEXT("UI_Select"), 0.6f, 0.f, CampusAudio::ECategory::Interface);
	}
	else
	{
		if (PauseMenuContainer.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(PauseMenuContainer.ToSharedRef());
		}
		PauseMenu.Reset();
		PauseMenuContainer.Reset();
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		ResetIgnoreLookInput();
		ApplyProfileSettings();
		SendProfileToServer();
		CampusAudio::Play2D(this, TEXT("UI_Back"), 0.6f, 0.f, CampusAudio::ECategory::Interface);
	}
}

void ACampusPlayerController::LeaveMatch()
{
	SetPauseMenuOpen(false);
	UGameplayStatics::OpenLevel(this, FName(CampusAssets::MapMenu), true, TEXT("game=Menu"));
}

#undef LOCTEXT_NAMESPACE
