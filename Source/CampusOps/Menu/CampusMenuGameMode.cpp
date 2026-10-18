#include "Menu/CampusMenuGameMode.h"
#include "Menu/CampusMenuCameraDirector.h"
#include "Menu/CampusMenuStage.h"
#include "Map/CampusSchoolBuilder.h"
#include "UI/SCampusMainMenu.h"
#include "Game/CampusGameInstance.h"
#include "CampusAssets.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/SWeakWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"

ACampusMenuGameMode::ACampusMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ACampusMenuPlayerController::StaticClass();
}

UCampusGameInstance* ACampusMenuPlayerController::GetCampusGI() const
{
	return Cast<UCampusGameInstance>(GetGameInstance());
}

void ACampusMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	UWorld* World = GetWorld();
	for (TActorIterator<ACampusMenuCameraDirector> It(World); It; ++It)
	{
		Director = *It;
		break;
	}
	for (TActorIterator<ACampusMenuStage> It(World); It; ++It)
	{
		Stage = *It;
		break;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!Director)
	{
		Director = World->SpawnActor<ACampusMenuCameraDirector>(ACampusMenuCameraDirector::StaticClass(), FTransform::Identity, Params);
	}
	if (!Stage)
	{
		Stage = World->SpawnActor<ACampusMenuStage>(ACampusMenuStage::StaticClass(), FTransform::Identity, Params);
	}
	Stage->SetupFromBuilder(ACampusSchoolBuilder::Find(World));

	UCampusGameInstance* GI = GetCampusGI();
	const bool bTitle = GI ? !GI->bSeenTitleScreen : true;
	if (GI)
	{
		PreviewWeapon(GI->GetActiveLoadout().Primary);
		PreviewOperator(GI->GetProfile().OperatorIndex);
		UCampusGameInstance::ApplyAudioVolume(this, GI->GetProfile().MasterVolume);
	}

	Director->SnapTo(Stage->GetShot(bTitle ? ECampusMenuShot::Title : ECampusMenuShot::Lobby));
	CurrentShot = bTitle ? ECampusMenuShot::Title : ECampusMenuShot::Lobby;
	SetViewTarget(Director);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(1.f, 0.f, bTitle ? 2.5f : 1.2f, FLinearColor::Black, false, false);
	}

	if (UGameViewportClient* Viewport = World->GetGameViewport())
	{
		Menu = SNew(SCampusMainMenu).Owner(this).ShowTitle(bTitle);
		MenuContainer = SNew(SWeakWidget).PossiblyNullContent(Menu);
		Viewport->AddViewportWidgetContent(MenuContainer.ToSharedRef(), 10);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Menu);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
	}
}

void ACampusMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		if (MenuContainer.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(MenuContainer.ToSharedRef());
		}
	}
	Menu.Reset();
	MenuContainer.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACampusMenuPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!Director)
	{
		return;
	}
	float X = 0.f;
	float Y = 0.f;
	int32 SizeX = 1;
	int32 SizeY = 1;
	if (GetMousePosition(X, Y))
	{
		GetViewportSize(SizeX, SizeY);
		Director->SetParallaxInput(FVector2D(X / FMath::Max(SizeX, 1) * 2.f - 1.f, Y / FMath::Max(SizeY, 1) * 2.f - 1.f));
	}
	const bool bWeaponShot = CurrentShot == ECampusMenuShot::Gunsmith || CurrentShot == ECampusMenuShot::GunsmithSlot || CurrentShot == ECampusMenuShot::Loadouts;
	float FStop = 0.f;
	if (bWeaponShot && Stage && Director->Camera)
	{
		Director->FollowFraming(Stage->GetShot(CurrentShot, CurrentSlot, GetMenuViewSize()), DeltaTime);
		FStop = Stage->ApertureToHoldWeapon(Director->Camera->GetComponentLocation(), Director->Camera->GetForwardVector(), Director->Camera->FieldOfView);
	}
	Director->SetWeaponCloseup(bWeaponShot, FStop);
}

void ACampusMenuPlayerController::TurnShownWeapon(const FVector2D& MouseDelta)
{
	const bool bWeaponShot = CurrentShot == ECampusMenuShot::Gunsmith || CurrentShot == ECampusMenuShot::GunsmithSlot || CurrentShot == ECampusMenuShot::Loadouts;
	if (Stage && bWeaponShot)
	{
		// Drag right turns the muzzle away to the right; drag down rolls the top toward the camera.
		Stage->TurnWeapon(-MouseDelta.X * 0.35f, MouseDelta.Y * 0.3f);
	}
}

void ACampusMenuPlayerController::GoToShot(ECampusMenuShot Shot, ECampusAttachmentSlot Slot, float Duration)
{
	if (!Director || !Stage)
	{
		return;
	}
	const bool bGunsmithLocal = (CurrentShot == ECampusMenuShot::Gunsmith || CurrentShot == ECampusMenuShot::GunsmithSlot || CurrentShot == ECampusMenuShot::Loadouts)
		&& (Shot == ECampusMenuShot::Gunsmith || Shot == ECampusMenuShot::GunsmithSlot || Shot == ECampusMenuShot::Loadouts);
	const bool bOperatorLocal = (CurrentShot == ECampusMenuShot::Lobby || CurrentShot == ECampusMenuShot::Operator)
		&& (Shot == ECampusMenuShot::Lobby || Shot == ECampusMenuShot::Operator);
	const float Time = Duration < 0.f ? 0.28f : FMath::Min(Duration, 0.45f);
	if (Shot != CurrentShot || Shot == ECampusMenuShot::GunsmithSlot)
	{
		CampusAudio::Play2D(this, bGunsmithLocal ? TEXT("UI_Tab") : TEXT("UI_Whoosh"), bGunsmithLocal ? 0.3f : 0.55f, 0.03f, CampusAudio::ECategory::Interface);
	}
	CurrentShot = Shot;
	CurrentSlot = Slot;
	if (Shot != ECampusMenuShot::Gunsmith && Shot != ECampusMenuShot::GunsmithSlot && Shot != ECampusMenuShot::Loadouts)
	{
		Stage->ResetWeaponTurn();
	}
	const FCampusCameraShot Target = Stage->GetShot(Shot, Slot, GetMenuViewSize());
	if ((bGunsmithLocal || bOperatorLocal) && !Director->IsCutting())
	{
		// keep the shared subject framed when changing views within one display
		Director->OrbitTo(Target, Time);
	}
	else
	{
		Director->CutTo(Target);
	}
	if (UCampusGameInstance* GI = GetCampusGI())
	{
		GI->bSeenTitleScreen = true;
	}
}

FVector2D ACampusMenuPlayerController::GetMenuViewSize() const
{
	if (Menu.IsValid())
	{
		const FVector2D Size = Menu->GetCachedGeometry().GetLocalSize();
		if (Size.X > 1.f && Size.Y > 1.f) return Size;
	}
	int32 Width = 0;
	int32 Height = 0;
	GetViewportSize(Width, Height);
	return FVector2D(Width, Height);
}

float ACampusMenuPlayerController::GetCameraTransitionAlpha() const
{
	return Director ? Director->GetCutOpacity() : 0.f;
}

bool ACampusMenuPlayerController::GetSlotScreenPosition(ECampusAttachmentSlot Slot, FVector2D& OutPixel) const
{
	return Stage && ProjectWorldLocationToScreen(Stage->GetSlotWorldPoint(Slot), OutPixel, true);
}

bool ACampusMenuPlayerController::GetWeaponScreenCenter(FVector2D& OutPixel) const
{
	return Stage && ProjectWorldLocationToScreen(Stage->GetWeaponCenterWorld(), OutPixel, true);
}

const FCampusWeaponLoadout* ACampusMenuPlayerController::GetShownWeapon() const
{
	return Stage ? &Stage->GetShownLoadout() : nullptr;
}

void ACampusMenuPlayerController::PreviewWeapon(const FCampusWeaponLoadout& Loadout)
{
	if (Stage)
	{
		Stage->ShowWeapon(Loadout);
	}
}

void ACampusMenuPlayerController::PreviewOperator(int32 Index)
{
	if (Stage)
	{
		const UCampusGameInstance* GI = GetCampusGI();
		Stage->SetOperator(Index, GI ? GI->GetActiveLoadout().Primary : FCampusWeaponLoadout());
	}
}

void ACampusMenuPlayerController::Deploy(ECampusMatchMode Mode, int32 TeamSize, bool bBots, bool bCustom)
{
	GoToShot(ECampusMenuShot::Deploy);
	CampusAudio::Play2D(this, TEXT("UI_Deploy"), 0.9f, 0.f, CampusAudio::ECategory::Interface);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(0.f, 1.f, 0.9f, FLinearColor::Black, false, true);
	}
	TWeakObjectPtr<ACampusMenuPlayerController> WeakThis(this);
	GetWorldTimerManager().SetTimer(DeployTimer, FTimerDelegate::CreateLambda([WeakThis, Mode, TeamSize, bBots, bCustom]()
	{
		if (WeakThis.IsValid())
		{
			if (UCampusGameInstance* GI = WeakThis->GetCampusGI())
			{
				const FCampusMatchRules Rules = GI->GetProfile().CustomRules;
				GI->HostMatch(Mode, TeamSize, bBots, bCustom ? &Rules : nullptr);
			}
		}
	}), 1.0f, false);
}

void ACampusMenuPlayerController::Join(const FString& Address)
{
	if (UCampusGameInstance* GI = GetCampusGI())
	{
		if (PlayerCameraManager)
		{
			PlayerCameraManager->StartCameraFade(0.f, 1.f, 0.5f, FLinearColor::Black, false, true);
		}
		GI->JoinMatch(Address);
	}
}

void ACampusMenuPlayerController::DebugMenu(const FString& Page, int32 Slot)
{
	if (Menu.IsValid())
	{
		Menu->DebugGoTo(Page, Slot);
	}
}

void ACampusMenuPlayerController::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
