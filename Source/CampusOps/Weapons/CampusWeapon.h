#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusTypes.h"
#include "CampusWeapon.generated.h"

class ACampusCharacter;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UTexture;

/** One rendered copy of the weapon (first person or third person) built from the mesh + attachment parts. */
USTRUCT()
struct FCampusWeaponRig
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<USceneComponent> Frame;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<USceneComponent> Sight;
	UPROPERTY() TObjectPtr<USceneComponent> Muzzle;
	UPROPERTY() TObjectPtr<USceneComponent> Eject;
	UPROPERTY() TObjectPtr<USceneComponent> LaserOrigin;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Flash;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FlashSide;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Reticle;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LaserBeam;
	/** Weapon light emitter (lens centre, facing down the bore) and its lit lens; null without a light fitted. */
	UPROPERTY() TObjectPtr<USceneComponent> LightOrigin;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LightLens;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	/** Magnified optic body and front lens: hidden once the eye is on the eyepiece (the rear lens stays). */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ScopeBody;
	/** Magnified optic's coated rear lens. */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ScopeEyepiece;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ScopeEyepieceComp;
	/** Animated model parts (null when the model has none). */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Magazine;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Bolt;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Charging;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Slide;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Hammer;
	FTransform MagazineRest = FTransform::Identity;
	FTransform BoltRest = FTransform::Identity;
	FTransform ChargingRest = FTransform::Identity;
	FTransform SlideRest = FTransform::Identity;
};

UCLASS()
class CAMPUSOPS_API ACampusWeapon : public AActor
{
	GENERATED_BODY()

public:
	ACampusWeapon();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnRep_Owner() override;
	virtual void Destroyed() override;

	/** Server: configure from a loadout (replicates to everyone). */
	void InitializeWeapon(const FCampusWeaponLoadout& InLoadout);
	/** Recomputes the stats (the owner's perks changed). */
	void RefreshStats();
	/** Server: Scavenger refill (capped a little above the starting reserve). */
	void AddReserveMagazines(int32 Count);
	/** False when the match rules give bottomless magazines. */
	bool ConsumesAmmo() const;
	void AttachToCharacter(ACampusCharacter* InCharacter);
	void SetEquipped(bool bInEquipped);
	bool IsEquipped() const { return bEquipped; }
	void OnEquipped();
	void OnHolster();
	void OnOwnerDied();
	void RefreshViewerVisibility();
	void SetFirstPersonHidden(bool bInHidden);

	// Trigger / actions (called on the controlling machine)
	void OnTriggerPressed();
	void OnTriggerReleased();
	void RequestReload();
	void CancelReload();
	void CycleFireMode();

	bool IsReloading() const { return bReloading; }
	/** The new magazine is in: cancelling from here keeps the refill. */
	bool IsReloadInserted() const { return bReloading && bReloadInserted; }
	float GetReloadProgress() const;
	ECampusFireMode GetFireMode() const { return FireMode; }
	int32 GetAmmoInMag() const { return AmmoInMag; }
	int32 GetAmmoReserve() const { return AmmoReserve; }
	float GetCurrentSpread() const;
	bool HasScopeOptic() const;
	bool HasOptic() const;
	/** Magnified optic on the first-person rig: rear lens centre (world) and radius (cm). False without one. */
	bool GetScopeEyepiece(FVector& OutCenter, float& OutRadius) const;
	/** Distance along the sight line from a magnified optic's rear lens forward to its sight point (cm); 0 without one. */
	float GetScopeEyepieceOffset() const;
	/** At full ADS only a magnified optic's eyepiece remains: the eye looks through it, not at the scope body. */
	void SetScopeBodyHidden(bool bHide);
	/** Eyepiece glass: coated at the hip (Live 0), the magnified View with the eye on it (Live 1), scaled by Gain back to
	 *  scene radiance. PupilShadow offsets the exit-pupil shading (lens UV units) toward the side the eye is off-axis. */
	void SetScopeEyepieceView(float Live, UTexture* View, float Gain, const FVector2D& PupilShadow);
	UMaterialInstanceDynamic* GetScopeEyepieceMaterial() const;
	/** The eyepiece glass on the first-person rig (null without a magnified optic). */
	const USceneComponent* GetScopeEyepieceComponent() const;
	/** First-person magazine during a reload: moved off its seat and turned about Pivot (weapon frame space), and/or hidden. */
	void SetReloadMagazine(const FVector& Offset, const FRotator& Rotation, const FVector& Pivot, bool bVisible);
	/** Seated first-person magazine bounds in weapon frame space; false when the model has no separate magazine. */
	bool GetMagazineBox(FBox& OutBox) const;
	/** Charging handle (or bolt handle) grab point in weapon frame space and which side it is on (+1 right, -1 left). */
	bool GetRackHandle(FVector& OutPoint, float& OutSide) const;
	bool HasLaser() const;
	/** A weapon light is fitted (gunsmith); only then can it be switched on. */
	bool HasFlashlight() const;
	bool IsFlashlightOn() const { return bLightOn && HasFlashlight(); }
	/** Owner: switches the fitted light on / off (does nothing without one). */
	void ToggleFlashlight();
	/** Fitted foregrip (an underbarrel grip the support hand holds instead of the handguard), or null. */
	const FCampusAttachmentDef* GetForegrip() const;

	const FCampusWeaponDef* GetDef() const;
	const FCampusWeaponStats& GetStats() const { return Stats; }
	const FCampusWeaponLoadout& GetLoadout() const { return Loadout; }
	FName GetWeaponId() const { return Loadout.WeaponId; }
	FText GetDisplayName() const;

	USceneComponent* GetFPSight() const { return FPRig.Sight; }
	USceneComponent* GetFPFrame() const { return FPRig.Frame; }
	USkeletalMeshComponent* GetFPMesh() const { return FPRig.Mesh; }
	USceneComponent* GetTPMuzzle() const { return TPRig.Muzzle; }

	/** Builds a stand-alone visual copy of a loadout (menu preview). Returns the root frame. */
	static USceneComponent* BuildPreview(AActor* Owner, USceneComponent* Parent, const FCampusWeaponLoadout& Loadout, TArray<TObjectPtr<UActorComponent>>& OutComponents, FName Socket = NAME_None);
	/** Weapon-frame location of an attachment slot for this loadout (used by the gunsmith camera). */
	static FVector GetSlotFocusPoint(const FCampusWeaponLoadout& Loadout, ECampusAttachmentSlot Slot);

	// Networking
	UFUNCTION(Server, Reliable) void ServerFire(const FCampusShot& Shot);
	UFUNCTION(Server, Reliable) void ServerStartReload();
	UFUNCTION(Server, Reliable) void ServerCancelReload(bool bInserted);
	UFUNCTION(Server, Reliable) void ServerSetFireMode(ECampusFireMode Mode);
	UFUNCTION(Server, Reliable) void ServerSetFlashlight(bool bOn);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShotFX(const FCampusShot& Shot);
	UFUNCTION(NetMulticast, Unreliable) void MulticastReloadFX(bool bEmpty);
	UFUNCTION(Client, Reliable) void ClientSyncAmmo(int32 Mag, int32 Reserve);

protected:
	UFUNCTION() void OnRep_Loadout();

	UPROPERTY(ReplicatedUsing = OnRep_Loadout) FCampusWeaponLoadout Loadout;
	UPROPERTY(Replicated) int32 AmmoInMag = 0;
	UPROPERTY(Replicated) int32 AmmoReserve = 0;
	/** Off by default; COND_SkipOwner, the owner switches it locally. */
	UPROPERTY(Replicated) bool bLightOn = false;

	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<UPointLightComponent> MuzzleLight;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<class USpotLightComponent> WeaponLight;
	UPROPERTY() FCampusWeaponRig FPRig;
	UPROPERTY() FCampusWeaponRig TPRig;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FlashMaterials;

	UPROPERTY(Transient) TObjectPtr<ACampusCharacter> OwnerCharacter;

private:
	static void BuildRig(AActor* Owner, USceneComponent* AttachTo, FName Socket, const FCampusWeaponLoadout& InLoadout, bool bFirstPerson, FCampusWeaponRig& OutRig, TArray<TObjectPtr<UMaterialInstanceDynamic>>* OutFlashMaterials);
	static void DestroyRig(FCampusWeaponRig& Rig);
	void RebuildVisuals();
	void ApplyStats();

	bool CanFire() const;
	void FireOnce();
	void TraceShot(const FVector& Origin, const FVector& Direction, FCampusShot& OutShot) const;
	void ProcessShotAuthority(const FCampusShot& Shot);
	void PlayLocalShotFX(const FCampusShot& Shot);
	void PlayRemoteShotFX(const FCampusShot& Shot);	void PlayImpactFX(const FCampusShot& Shot) const;
	void ShowMuzzleFlash(bool bFirstPerson);
	void UpdateLaser(float DeltaSeconds);
	void UpdateFlashlight(float DeltaSeconds);
	/** Bolt / slide cycling, slide lock on empty, charging handle on empty reloads. */
	void AnimateParts(float DeltaSeconds);
	void StartReloadInternal();
	void TickReload();
	/** Draws up to Wanted rounds from the reserve (it is not drawn down under infinite ammo rules). */
	int32 TakeFromReserve(int32 Wanted);
	bool IsLocallyControlledShooter() const;
	bool IsBotShooter() const;

	FCampusWeaponStats Stats;
	ECampusFireMode FireMode = ECampusFireMode::Auto;
	bool bEquipped = false;
	bool bFirstPersonHidden = false;
	bool bVisualsBuilt = false;
	FName BuiltWeaponId;
	TArray<FName> BuiltAttachments;

	double LastFireTime = -100.0;
	double LastServerFireTime = -100.0;
	int32 BurstRemaining = 0;
	bool bSemiLatched = false;
	int32 ShotsInSpray = 0;
	float Bloom = 0.f;
	float FlashHideTime = 0.f;
	/** 1 right after a shot, runs down to 0 over one cycle. */
	float BoltKick = 0.f;
	/** Local first-person barrel heat (0..1): builds with every shot, and a hot barrel smokes once firing stops. */
	void TickBarrelSmoke(float DeltaSeconds);
	float BarrelHeat = 0.f;
	float WispTimer = 0.f;
	/** Weapon light: eased output (dimmed close to a surface) and how far it is drawn back behind a wall the muzzle is in. */
	float LightScale = 1.f;
	float LightPullBack = 0.f;
	UPROPERTY(Transient) TObjectPtr<class ACampusSmokeTrail> SmokeTrail;

	bool bReloading = false;
	bool bReloadInserted = false;
	int32 ReloadTaken = 0;
	bool bReloadMagOutPlayed = false;
	bool bReloadBoltPlayed = false;
	bool bEmptyReload = false;
	double ReloadStart = 0.0;
	float ReloadDuration = 2.f;
	double ServerReloadStart = 0.0;
	bool bServerReloading = false;
};
