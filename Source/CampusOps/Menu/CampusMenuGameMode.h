#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "CampusTypes.h"
#include "Menu/CampusMenuStage.h"
#include "CampusMenuGameMode.generated.h"

class ACampusMenuCameraDirector;
class ACampusMenuStage;
class SCampusMainMenu;
class UCampusGameInstance;

UCLASS()
class CAMPUSOPS_API ACampusMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACampusMenuGameMode();
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override {}
};

UCLASS()
class CAMPUSOPS_API ACampusMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

	void GoToShot(ECampusMenuShot Shot, ECampusAttachmentSlot Slot = ECampusAttachmentSlot::Muzzle, float Duration = -1.f);
	void PreviewWeapon(const FCampusWeaponLoadout& Loadout);
	/** Mouse drag over the displayed weapon (pixels) turns it on its display (weapon pages only). */
	void TurnShownWeapon(const FVector2D& MouseDelta);
	void PreviewOperator(int32 Index);
	/** bCustom: host with the profile's custom match rules. */
	void Deploy(ECampusMatchMode Mode, int32 TeamSize, bool bBots, bool bCustom = false);
	void Join(const FString& Address);
	void QuitGame();

	UCampusGameInstance* GetCampusGI() const;
	ECampusMenuShot GetCurrentShot() const { return CurrentShot; }
	float GetCameraTransitionAlpha() const;
	/** Viewport pixel position of a slot on the displayed weapon (false when behind the camera). */
	bool GetSlotScreenPosition(ECampusAttachmentSlot Slot, FVector2D& OutPixel) const;
	bool GetWeaponScreenCenter(FVector2D& OutPixel) const;
	const FCampusWeaponLoadout* GetShownWeapon() const;
	void DebugMenu(const FString& Page, int32 Slot);

private:
	FVector2D GetMenuViewSize() const;
	UPROPERTY(Transient) TObjectPtr<ACampusMenuCameraDirector> Director;
	UPROPERTY(Transient) TObjectPtr<ACampusMenuStage> Stage;
	TSharedPtr<SCampusMainMenu> Menu;
	TSharedPtr<SWidget> MenuContainer;
	ECampusMenuShot CurrentShot = ECampusMenuShot::Lobby;
	ECampusAttachmentSlot CurrentSlot = ECampusAttachmentSlot::Muzzle;
	FTimerHandle DeployTimer;
};
