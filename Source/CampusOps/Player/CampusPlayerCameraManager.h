#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "CampusPlayerCameraManager.generated.h"

UCLASS()
class CAMPUSOPS_API ACampusPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	ACampusPlayerCameraManager();

protected:
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void UpdateSightFocus(const class ACampusCharacter* Char, FMinimalViewInfo& POV);
	/**
	 * Deploy intro: the camera swoops in from high behind the spawn, dropping first and then gliding in level, looking
	 * ahead and down over the ground the player is about to fight on (never straight down past the edge), and brakes
	 * into the eyes as the match goes live.
	 */
	void UpdateIntroDrop(const class ACampusPlayerController* PC, FMinimalViewInfo& POV);
	float IntroCachedStart = -100.f;
	FVector IntroSkyLocation = FVector::ZeroVector;
	FVector IntroControl = FVector::ZeroVector;
	FVector IntroEye = FVector::ZeroVector;
	FVector IntroFwd = FVector::ForwardVector;
	bool bIntroWindPlayed = false;
	bool bIntroUnderRoof = false;

	bool bSightFocusActive = false;
	float SavedMaxBackgroundRadius = 0.025f;
	float SavedMaxForegroundRadius = 0.025f;
	FMinimalViewInfo LastAlivePOV;
	bool bHasAlivePOV = false;
	bool bDeathCamInit = false;
	float DeathStartTime = 0.f;
	FVector DeathCamLocation = FVector::ZeroVector;
};
