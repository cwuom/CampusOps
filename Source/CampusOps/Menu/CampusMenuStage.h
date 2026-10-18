#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusTypes.h"
#include "Menu/CampusMenuCameraDirector.h"
#include "CampusMenuStage.generated.h"

class USkeletalMeshComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class ACampusSchoolBuilder;

UENUM(BlueprintType)
enum class ECampusMenuShot : uint8
{
	Title,
	Lobby,
	Loadouts,
	Gunsmith,
	GunsmithSlot,
	Operator,
	Options,
	Deploy
};

/** Props shown in the main menu: operator on the corridor, gunsmith weapon display in a classroom. */
UCLASS()
class CAMPUSOPS_API ACampusMenuStage : public AActor
{
	GENERATED_BODY()

public:
	ACampusMenuStage();
	virtual void Tick(float DeltaSeconds) override;

	void SetupFromBuilder(const ACampusSchoolBuilder* Builder);
	void ShowWeapon(const FCampusWeaponLoadout& Loadout);
	void SetOperator(int32 Index, const FCampusWeaponLoadout& Held);
	FCampusCameraShot GetShot(ECampusMenuShot Shot, ECampusAttachmentSlot Slot = ECampusAttachmentSlot::Muzzle, const FVector2D& ViewSize = FVector2D(1920.f, 1080.f)) const;
	/** World position of an attachment slot on the displayed weapon (gunsmith labels). */
	FVector GetSlotWorldPoint(ECampusAttachmentSlot Slot) const;
	FVector GetWeaponCenterWorld() const;
	/**
	 * the shown weapon framed from LocalDir inside the same clear rectangle used by the slot labels
	 */
	FCampusCameraShot FrameWeapon(const FBox2D& SafeRect, float FOV, const FVector& LocalDir, float Aperture, float Handheld, float Exposure, const FVector2D& ViewSize) const;
	const FCampusWeaponLoadout& GetShownLoadout() const { return Shown; }
	/** Mouse drag on the display: yaw about the vertical, roll about the bore (degrees). */
	void TurnWeapon(float DeltaYaw, float DeltaRoll);
	/** Back to the display's own orientation (a new weapon, or leaving the weapon pages). */
	void ResetWeaponTurn();
	/** Least f-stop that keeps the displayed weapon sharp from Eye as it is turned now (0 with nothing shown). */
	float ApertureToHoldWeapon(const FVector& Eye, const FVector& ViewDir, float FOV) const;

	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USceneComponent> OperatorRoot;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USkeletalMeshComponent> Operator;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USceneComponent> DisplayRoot;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USceneComponent> WeaponPivot;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<UStaticMeshComponent> DisplayTable;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USpotLightComponent> KeyLight;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USpotLightComponent> RimLight;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USpotLightComponent> OperatorLight;

private:
	friend class FCampusUIWeaponProjectionTest;
	FVector FramePointToWorld(const FVector& FramePoint) const;
	/** The shown weapon's extent along the display's axes, from every part on it (invalid before it is built). */
	FBox GetWeaponLocalBounds() const;
	/** How deep the depth of field must be to hold the whole weapon sharp: its thickness and its ends swinging with the sway. */
	float GetWeaponDepth() const;

	UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> WeaponComponents;
	UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> HeldComponents;
	/** Real weapons hung in the armoury rack behind the operator. */
	UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> RackComponents;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> RackRoots;
	FVector TitleCamera = FVector::ZeroVector;
	FVector TitleTarget = FVector::ZeroVector;
	FVector OptionsCamera = FVector::ZeroVector;
	FVector OptionsTarget = FVector::ZeroVector;
	FVector DeployCamera = FVector::ZeroVector;
	FVector DeployTarget = FVector::ZeroVector;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> GearParts;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> WeaponFrame;

	FCampusWeaponLoadout Shown;
	FVector CourtyardFocus = FVector::ZeroVector;
	FVector GateFocus = FVector::ZeroVector;
	float Time = 0.f;
	float ViewYaw = 0.f;
	float ViewRoll = 0.f;
	float TargetYaw = 0.f;
	float TargetRoll = 0.f;
};
