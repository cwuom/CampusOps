#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "CampusInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/** A keyboard / mouse binding the player can change in the options (gamepad buttons stay fixed). */
struct FCampusBindable
{
	FName Id;
	FText Label;
	FKey Default;
};

/** Input actions and the default keyboard/mouse + gamepad mapping, created at runtime (no input assets needed). */
UCLASS(Transient)
class CAMPUSOPS_API UCampusInputConfig : public UObject
{
	GENERATED_BODY()

public:
	static UCampusInputConfig* Get();

	/** Every rebindable action, in the order the options list them. */
	static const TArray<FCampusBindable>& Bindables();
	/** Short key name for prompts and the options list ("T", "鼠标右键", "左 Shift"); "未绑定" for none. */
	static FText KeyLabel(const FKey& Key);
	/** Keys that cannot be bound (Escape opens the menu, gamepad buttons are fixed). */
	static bool IsBindableKey(const FKey& Key);

	/** Remaps keyboard / mouse with the player's keys (missing ids keep their default) and refreshes local players' input. */
	void ApplyBindings(const TMap<FName, FKey>& Overrides);
	/** Key currently bound to a bindable id. */
	FKey GetKey(FName Id) const;
	FText GetKeyLabel(FName Id) const { return KeyLabel(GetKey(Id)); }

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;

	UPROPERTY() TObjectPtr<UInputAction> Move;
	UPROPERTY() TObjectPtr<UInputAction> Look;
	UPROPERTY() TObjectPtr<UInputAction> LookGamepad;
	UPROPERTY() TObjectPtr<UInputAction> Jump;
	UPROPERTY() TObjectPtr<UInputAction> Sprint;
	UPROPERTY() TObjectPtr<UInputAction> Crouch;
	UPROPERTY() TObjectPtr<UInputAction> CrouchHold;
	UPROPERTY() TObjectPtr<UInputAction> Aim;
	UPROPERTY() TObjectPtr<UInputAction> Fire;
	UPROPERTY() TObjectPtr<UInputAction> Reload;
	UPROPERTY() TObjectPtr<UInputAction> SwitchWeapon;
	UPROPERTY() TObjectPtr<UInputAction> Primary;
	UPROPERTY() TObjectPtr<UInputAction> Secondary;
	UPROPERTY() TObjectPtr<UInputAction> FireMode;
	UPROPERTY() TObjectPtr<UInputAction> Flashlight;
	UPROPERTY() TObjectPtr<UInputAction> Melee;
	UPROPERTY() TObjectPtr<UInputAction> Inspect;
	UPROPERTY() TObjectPtr<UInputAction> Interact;
	UPROPERTY() TObjectPtr<UInputAction> Lethal;
	UPROPERTY() TObjectPtr<UInputAction> Tactical;
	UPROPERTY() TObjectPtr<UInputAction> CycleLethal;
	UPROPERTY() TObjectPtr<UInputAction> CycleTactical;
	UPROPERTY() TObjectPtr<UInputAction> Scoreboard;
	UPROPERTY() TObjectPtr<UInputAction> Pause;

private:
	void Build();
	void MapAll();
	UInputAction* ActionFor(FName Id) const;

	TMap<FName, FKey> Keys;
};
