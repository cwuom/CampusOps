#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CampusSpring.h"
#include "CampusTypes.h"
#include "CampusViewmodelComponent.generated.h"

class ACampusCharacter;
class ACampusWeapon;
class UPrimitiveComponent;

/**
 * Procedural first-person animation layer. Drives the ViewmodelPivot (arms + weapon rig) and exposes
 * camera offsets / FOV for the camera manager. Only ticks for the local human player.
 */
UCLASS(ClassGroup = Campus, meta = (BlueprintSpawnableComponent))
class CAMPUSOPS_API UCampusViewmodelComponent : public UActorComponent
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCampusSlideJumpContinuityTest;
	friend class FCampusViewmodelPawnContactTest;
	friend class FCampusViewmodelShellContactTest;
	friend class FCampusWeaponClearanceFloorTest;
	friend class FCampusWeaponClearanceCornerTest;
	friend class FCampusWeaponClearanceChannelTest;
	friend class FCampusSlideCarryTest;
	friend class FCampusWeaponBackWallTest;
	friend class FCampusMantleHandTest;
	friend class FCampusKnifeMotionTest;
	friend class FCampusReloadMotionTest;
	friend class FCampusWallRepeatTest;
	friend class FCampusScopeCaptureStabilityTest;
#endif

public:
	UCampusViewmodelComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Events from character / weapon
	void OnWeaponFired(const FCampusWeaponStats& Stats, float InADSAlpha, bool bFirstShot);
	void OnLanded(float FallSpeed);
	void OnJumped();
	void OnCrouchChanged(float HeightDelta);
	void OnDamaged(const FVector& FromDirection, float Damage);
	void OnReloadStart(float Duration, bool bEmpty);
	void OnReloadInsert();
	void OnReloadBolt();
	void OnReloadEnd();
	/** Reload interrupted (fire / sprint / aim / switch): blend out instead of snapping. */
	void OnReloadCancel(bool bInserted);
	void OnSlideCancel(bool bJumping = false);
	void OnEquip(float Duration);
	void OnHolster(float Duration);
	/** Holster aborted: raise the weapon again from wherever it currently is. */
	void CancelHolster(float RaiseDuration);
	/** The weapon comes up from low ready (touching down from the deploy drop). */
	void RaiseFromLowReady(float Duration);
	void OnMelee();
	/** Knife: a light slash (forehand and backhand alternate) or the heavy stab. */
	void OnKnifeSlash(bool bHeavy);
	/** Grenade: drawn from the pouch into the support hand and held (pin, then the cook) until released. */
	/** Rate: playback speed of the whole throw (Fast Hands > 1). */
	void OnThrowStart(ECampusThrowable Type, float Rate = 1.f);
	/** The grenade went back in the pouch before the pin came out. */
	void OnThrowCancel();
	/** Speeds up the rest of the throw (the recovery is being cut short). */
	void HurryThrow(float Rate) { ThrowRate *= Rate; }
	void OnThrowPin();
	/** The spoon flies off a cooking frag. */
	void OnThrowSpoon();
	void OnThrowRelease();
	/** The grenade has left the hand. */
	void OnThrowLaunched();
	void OnThrowEnd();
	/** A blast nearby (Strength 0..1): the view is jolted and rumbles, the weapon is knocked about. */
	void OnExplosion(float Strength, const FVector& From);
	/** Flashbang: the eye is overexposed (Blind 0..1) and the frame of that instant burns in as an afterimage. */
	void OnFlashbanged(float Blind, const FVector& From);
	/** Whiteout and afterimage strength right now (0..1), for the HUD. */
	void GetFlashState(float& OutWhite, float& OutGhost) const;
	class UTextureRenderTarget2D* GetFlashAfterimage() const { return FlashImage; }
	void OnInspect();
	/** Blends an inspect out instead of snapping. */
	void CancelInspect();
	/** New input: blend out of an inspect, and out of a melee once the strike has landed. */
	void InterruptActions();
	void ResetCalibration() { bCalibrated = false; }
	bool IsInspecting() const { return InspectTime >= 0.f && !bInspectFading; }
	FString GetDebugString() const;
	/** Automation: overrides a pose ("hip", "sprint", "tac", "slide", "block") to tune framing at runtime. */
	void SetPoseOverride(FName Pose, const FVector& Offset, const FRotator& Rotation) { PoseOverrides.Add(Pose, FTransform(Rotation, Offset)); }
	/** World position of the grenade in the support hand as the player sees it (false when none is held). */
	bool GetHandGrenadeLocation(FVector& OutWorld) const;
	/** Automation: holds the reload animation at a fixed progress (0..1) without reloading; negative releases it. */
	void SetDebugReload(float Progress, bool bEmpty);

	// Outputs for the camera manager
	FVector CameraLocationOffset = FVector::ZeroVector;
	FRotator CameraRotationOffset = FRotator::ZeroRotator;
	float CurrentFOV = 90.f;
	float CurrentViewmodelFOV = 74.f;
	/** Eased ADS amount actually used for the pose (0..1). */
	float ADSPoseAlpha = 0.f;
	/** local wall avoidance also releases the sight picture, zoom and aiming sensitivity */
	float GetWallObstructionAlpha() const { return FMath::Clamp(BlockAlpha, 0.f, 1.f); }
	/** Magnified optic eyepiece on screen: centre in viewport UV, radius as a fraction of the view height. */
	bool GetScopeEyepieceScreen(FVector2D& OutCenterUV, float& OutRadius) const;

private:
	void Calibrate(ACampusWeapon* Weapon, float DeltaTime, bool bForce);
	void CacheWeaponClearance(ACampusWeapon* Weapon);
	void ConstrainWeaponPose(ACampusWeapon* Weapon, FTransform& Pose, float DeltaTime);
	void UpdateHeadLock();
	/** Magnified optic: renders the view along the optic's axis into its eyepiece (picture in picture) while aiming. */
	void UpdateScopeView(ACampusWeapon* Weapon, const FCampusWeaponStats* Stats, float RawADS, float MainZoom);
	void PrepareScopeTarget(int32 RequestedSize, bool bResetBudget);
	/** Lays the sight picture into the eyepiece's circle on screen (Live 0 takes it off); Roll is the optic's (cos, sin). */
	void SetScopeComposite(float Live, const FVector2D& CenterUV, float Radius, const FVector2D& Roll, float Gain, const FVector2D& PupilShadow);
	/**
	 * Procedural reload: support hand target (weapon frame space), the magazine's offset from its seat and visibility,
	 * the IK weight, and how far the elbow is held down while the hand is away at the pouch. False outside a reload.
	 */
	bool EvaluateReload(const ACampusWeapon* Weapon, const FCampusWeaponDef* Def, const FVector& HandGrip, const FTransform& FrameT, const FTransform& CamT, FVector& OutHand, FVector& OutMagOffset,
		FRotator& OutMagRot, FVector& OutMagPivot, bool& bOutMagVisible, float& OutIK, float& OutMagGrip, float& OutRackGrip, float& OutElbowDrop) const;
	/** Grenade throw: weapon dip weight, the support hand's path and wrist turn (camera space); false when not throwing. */
	/** OutFingers / OutThumb: how far the support hand's fingers open from the handguard grip (round the grenade, then off it at the release). */
	bool EvaluateThrow(float& OutLower, FVector& OutHandCam, float& OutIK, FVector& OutElbowCam, FQuat& OutTurnCam, float& OutFingers, float& OutThumb) const;
	void PlaceHandGrenade();
	void ClearHandGrenade();
	float ThrowTime = -1.f;
	float ThrowReleaseAt = -1.f;
	float ThrowRate = 1.f;
	bool bHandGrenadeGone = false;
	/** Cancelled draw: the hand returns to the handguard from where it was. */
	float ThrowCancelAt = -1.f;
	ECampusThrowable ThrowType = ECampusThrowable::Frag;
	FVector ThrowStartHand = FVector::ZeroVector;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> HandGrenade;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> HandGrenadeRoot;
	bool bHandGrenadePlaced = false;
	/** Blast rumble (decays over a second or so, unlike the per-shot shake). */
	float Rumble = 0.f;
	float FlashBlind = 0.f;
	float FlashTime = -100.f;
	UPROPERTY(Transient) TObjectPtr<class USceneCaptureComponent2D> FlashCapture;
	UPROPERTY(Transient) TObjectPtr<class UTextureRenderTarget2D> FlashImage;
	void UpdateFlashPost();

	/** Elbow drop actually applied (smoothed). */
	float ElbowDrop = 0.f;
	/** Extra elbow drop that keeps a folded support arm out of the frame (rises while it is in view, eases off after). */
	float ElbowGuard = 0.f;
	/** Support-hand target in weapon frame space, smoothed so a cancelled reload glides back instead of snapping. */
	FVector SmoothLeftHand = FVector::ZeroVector;
	bool bSmoothLeftHandInit = false;
	bool bWasReloadPath = false;
	float ReloadReturnTime = -1.f;
	FVector ReloadReturnStart = FVector::ZeroVector;
	FQuat ReloadLastTurn = FQuat::Identity;
	FQuat ReloadReturnTurn = FQuat::Identity;
	float ReloadLastFingers = 0.f;
	float ReloadReturnFingers = 0.f;
	float WallAdvance = 0.f;
	bool bMantleHandWasMoving = false;
	bool bMantleHandActive = false;
	bool bMantleHandReturning = false;
	bool bMantleHandHasRest = false;
	float MantleHandTime = 0.f;
	float MantleHandAlpha = 0.f;
	float MantleHandReturnAlpha = 0.f;
	FVector MantleHandCamera = FVector::ZeroVector;
	FVector MantleHandStartCamera = FVector::ZeroVector;
	FVector MantleRestEffector = FVector::ZeroVector;
	FVector MantleHandContactWorld = FVector::ZeroVector;
	float MantleLookPose = 0.f;
	float MantleRollPose = 0.f;
	float MantleTuckPoseAlpha = 0.f;
	float ReloadSideClearance = -8.f;
	FBox WeaponClearanceBox = FBox(ForceInit);
	TWeakObjectPtr<USceneComponent> ClearanceFrame;
	/** residual clearance in rendered world space, so turning cannot rotate an old wall push into a new direction */
	FVector WallCorrection = FVector::ZeroVector;
	bool bReloadMagKicked = false;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ScopeViewMID;
	UPROPERTY(Transient) TObjectPtr<class USceneCaptureComponent2D> ScopeCapture;
	UPROPERTY(Transient) TObjectPtr<class UTextureRenderTarget2D> ScopeTarget;
	int32 ScopeViewIndex = INDEX_NONE;
	/** Frames the capture has been rendering into its target as it is now (0 after it was created, resized or idle). */
	int32 ScopeFramesRendered = 0;
	int32 ScopeCapturePixelBudget = 0;
	float ScopeCaptureViewHeight = 0.f;
	TWeakObjectPtr<const USceneComponent> ScopeCaptureOptic;

	UPROPERTY(Transient) TObjectPtr<ACampusCharacter> Character;
	TWeakObjectPtr<ACampusWeapon> CalibratedWeapon;

	// Calibration of the sight relative to the pivot
	bool bCalibrated = false;
	FVector CalibSight = FVector::ZeroVector;
	FQuat CalibGunRot = FQuat::Identity;
	FVector CalibGrip = FVector::ZeroVector;
	FVector HeadLockOffset = FVector::ZeroVector;
	bool bHeadLockInit = false;
	// Springs
	FCampusSpringVector SwayRot;
	FCampusSpringVector SwayPos;
	FCampusSpringVector RecoilRot;
	FCampusSpringVector RecoilPos;
	/** Very stiff layer: the instant of the shot (the receiver slamming back into the shoulder). */
	FCampusSpringVector RecoilSnapPos;
	/** Slow layer that builds under sustained fire (the muzzle climbing and the shoulder giving) and settles after. */
	FCampusSpringVector RecoilClimb;
	/** Shots in the current string; the roll torque alternates and the climb grows with it. */
	int32 ShotString = 0;
	float LastShotTime = -100.f;
	FCampusSpringVector CamKick;
	FCampusSpringVector ImpactRot;
	FCampusSpringVector ImpactPos;
	FCampusSpringVector MoveTiltRot;
	FCampusSpringVector MoveTiltPos;
	FCampusSpringVector InertiaRot;
	FCampusSpringVector InertiaPos;
	FVector PrevLocalVel = FVector::ZeroVector;
	FVector SmoothAccel = FVector::ZeroVector;
	FCampusSpringVector CamImpact;
	FCampusSpringFloat CamHeight;
	/** Frames left before the arms are shown after the deploy drop (-1: no reveal under way). */
	int32 RevealFrames = -1;
	FVector StepFeet = FVector::ZeroVector;
	bool bStepFeetValid = false;
	FCampusSpringFloat SprintAlpha;
	FCampusSpringFloat TacSprintAlpha;
	FCampusSpringFloat SlideAlpha;
	FCampusSpringFloat CrouchAlpha;
	FCampusSpringFloat AirAlpha;
	FCampusSpringFloat BobWeight;
	FCampusSpringFloat FOVKick;
	FCampusSpringFloat ReloadWeight;
	/** How far the weapon is brought round to the support hand for racking its charging handle. */
	FCampusSpringFloat RackWeight;
	float BlockAlpha = 0.f;
	float WallTargetAlpha = 0.f;
	TWeakObjectPtr<AActor> WallHitActor;
	TWeakObjectPtr<UPrimitiveComponent> WallHitComponent;
	float WallHitDistance = 0.f;
	bool bWallHitPenetrating = false;
	bool bWallPoseActive = false;
	bool bWallHighReady = false;
	bool bWasSliding = false;
	bool bSlideJumpPending = false;
	bool bWasMantling = false;
	bool bEquipSettled = true;

	float BobPhase = 0.f;
	float BreathPhase = 0.f;
	float Time = 0.f;
	float ShakeAmount = 0.f;
	FRotator PrevControlRotation = FRotator::ZeroRotator;
	bool bHasPrevRotation = false;
	FVector2D LookVelocity = FVector2D::ZeroVector;

	// Timelines (-1 = inactive)
	float EquipTime = -1.f;
	float EquipDuration = 0.5f;
	/** Raise curve: lowered weight = (1 - progress)^k. */
	static constexpr float EquipEaseExponent = 3.4f;
	float HolsterTime = -1.f;
	float HolsterDuration = 0.2f;
	float ReloadTime = -1.f;
	float ReloadDuration = 2.f;
	bool bReloadEmpty = false;
	float DebugReloadProgress = -1.f;
	float InspectTime = -1.f;
	float MeleeTime = -1.f;
	float KnifeTime = -1.f;
	int32 KnifeSwing = 0;
	bool bKnifeHeavy = false;
	bool bKnifeThrustKicked = false;
	/** Interrupted inspect / melee: the pose freezes and its weight fades 1 -> 0. */
	bool bInspectFading = false;
	float InspectFade = 1.f;
	bool bMeleeFading = false;
	float MeleeFade = 1.f;
	float LeftHandIKAlpha = 0.f;
	float LoweredAlpha = 0.f;
	bool bFPHidden = false;
	TWeakObjectPtr<ACampusWeapon> HiddenWeapon;
	TMap<FName, FTransform> PoseOverrides;
	FTransform HandsOffset = FTransform::Identity;
};
