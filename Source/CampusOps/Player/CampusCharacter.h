#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CampusTypes.h"
#include "CampusCharacter.generated.h"

class UCameraComponent;
class USkeletalMeshComponent;
class UPhysicalAnimationComponent;
class UCampusViewmodelComponent;
class UCampusMovementComponent;
class UCampusFPAnimInstance;
class ACampusWeapon;
class ACampusPlayerState;
class ACampusDoor;
class ACampusEnvironment;
struct FInputActionValue;

DECLARE_MULTICAST_DELEGATE_TwoParams(FCampusOnDamaged, float /*Damage*/, const FVector& /*FromLocation*/);

UCLASS()
class CAMPUSOPS_API ACampusCharacter : public ACharacter
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCampusSlideJumpContinuityTest;
	friend class FCampusViewmodelPawnContactTest;
	friend class FCampusViewmodelShellContactTest;
	friend class FCampusWeaponClearanceChannelTest;
	friend class FCampusWeaponBackWallTest;
#endif

public:
	ACampusCharacter(const FObjectInitializer& ObjectInitializer);

	//~ AActor / APawn / ACharacter
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_PlayerState() override;
	virtual void OnRep_Controller() override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnJumped_Implementation() override;
	virtual bool CanJumpInternal_Implementation() const override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void Destroyed() override;
	virtual FVector GetPawnViewLocation() const override;

	// ------------------------------------------------------------------ components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<UCameraComponent> FirstPersonCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<USceneComponent> ViewmodelPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<USkeletalMeshComponent> ArmsMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<UCampusViewmodelComponent> Viewmodel;
	/** Drives the upper-body physics flinch when hit (third person only). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;
	/** Exertion, held breath and the breathing the local player hears. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<class UCampusBreathComponent> BreathComp;

	UCampusMovementComponent* GetCampusMovement() const;
	UCampusFPAnimInstance* GetArmsAnim() const;

	// ------------------------------------------------------------------ identity
	ACampusPlayerState* GetCampusPlayerState() const;
	ECampusTeam GetTeam() const;
	bool IsEnemyOf(const AActor* Other) const;
	FString GetDisplayName() const;
	/** True for the human player controlling this pawn on this machine. */
	bool IsLocalHuman() const;
	void ApplyOperatorMesh(int32 OperatorIndex);
	/** Third-person body animation (locomotion + upper-body montages); null before the first weapon is equipped. */
	class UCampusTPAnimInstance* GetBodyAnim() const;
	/** Upper-body one-shots on the third-person body, seen by other players (and in this player's shadow). */
	void PlayBodyFire();
	void PlayBodyReload(float Duration);
	/** Team camo on the shared uniform material (body + first-person arms). */
	void UpdateUniform();

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> UniformMaterial;
	float UniformTeam = -1.f;
	float UniformWetness = 0.f;
	float AppliedUniformWetness = 0.f;
	float UniformWetnessElapsed = 0.f;
	TWeakObjectPtr<ACampusEnvironment> WetEnvironment;
	void UpdateUniformWetness(float DeltaSeconds);
	/** Helmet, plate carrier, belt and knee pads riding the third-person body. */
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> GearParts;

	// ------------------------------------------------------------------ health
	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Campus") float Health = 100.f;
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadOnly, Category = "Campus") float MaxHealth = 100.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Campus") bool bHealthRegen = true;
	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category = "Campus") bool bIsDead = false;
	UPROPERTY(Replicated) FVector_NetQuantizeNormal DeathDirection = FVector::ForwardVector;
	UPROPERTY(Replicated) FName DeathBone;
	UPROPERTY(Replicated) TObjectPtr<APlayerState> KillerState;
	/** Server-mirrored aim state for remote viewers (lasers, third-person). */
	UPROPERTY(Replicated) bool bRepAiming = false;

	bool IsAlive() const { return !bIsDead; }
	float GetHealth01() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	/** Server: apply weapon/melee damage. Returns true if this hit killed the character. */
	bool ApplyCampusDamage(float Damage, ECampusHitZone Zone, AController* InstigatorController, AActor* Causer, FName WeaponId, const FVector& Direction, FName Bone);
	/** Debug/automation only (CampusGod). */
	bool bInvulnerable = false;
	/** Server: kill immediately. */
	void Die(AController* Killer, FName WeaponId, bool bHeadshot, const FVector& Direction, FName Bone);

	FCampusOnDamaged OnDamaged;

	/** Melee strike with the current weapon (does nothing while recovering): with a knife a light slash, or the heavy stab
	 *  when bHeavy; anything else is a butt stroke. */
	void MeleeStrike(bool bHeavy = false);

	// ------------------------------------------------------------------ weapons
	UPROPERTY(ReplicatedUsing = OnRep_Weapons) TArray<TObjectPtr<ACampusWeapon>> Weapons;
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponIndex) int32 CurrentWeaponIndex = INDEX_NONE;

	ACampusWeapon* GetCurrentWeapon() const;
	/** Server: destroys existing weapons and spawns the loadout. */
	void GiveLoadout(const FCampusLoadout& Loadout);
	/** Local: start switching to a weapon slot (holster -> equip). */
	void RequestEquip(int32 Index);
	bool IsSwitchingWeapons() const { return PendingWeaponIndex != INDEX_NONE; }

	UFUNCTION(Server, Reliable) void ServerEquipWeapon(int32 Index);
	/** Kind: 0 butt stroke, 1 knife slash, 2 knife stab. */
	UFUNCTION(Server, Reliable) void ServerMelee(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, uint8 Kind);
	UFUNCTION(Server, Reliable) void ServerCancelMelee();
	/** Reliable: the swing also breaks glass, which every machine breaks for itself and must keep in step. */
	UFUNCTION(NetMulticast, Reliable) void MulticastMeleeFX(FVector_NetQuantize Start, FVector_NetQuantizeNormal Direction, bool bHitPerson);

	// ------------------------------------------------------------------ throwables
	/** Grenades left, indexed by ECampusThrowable. */
	UPROPERTY(Replicated) TArray<uint8> Throwables;
	int32 GetThrowableCount(ECampusThrowable Type) const;
	ECampusThrowable GetSelectedLethal() const { return SelectedLethal; }
	ECampusThrowable GetSelectedTactical() const { return SelectedTactical; }
	/** Time the lethal / tactical selection last changed (HUD callout). */
	float GetSelectionChangeTime() const { return SelectionChangeTime; }
	bool IsThrowing() const { return ThrowPhase != EThrowPhase::None; }
	ECampusThrowable GetThrowingType() const { return ThrowType; }
	/** Seconds since the spoon came off a held frag (0 while not cooking). */
	float GetCookTime() const;
	/** Server: blinded (0..1) and deafened (0..1) by a flashbang going off at From. */
	void ApplyFlash(float Blind, float Deaf, const FVector& From);
	/** Server, bots: lob a grenade onto Target. False when it cannot. */
	bool BotThrow(ECampusThrowable Type, const FVector& Target);
	/** Server: speed given to the body if the next hit kills (blasts throw the body); 0 is a normal fall. */
	float NextDeathImpulse = 0.f;
	UPROPERTY(Replicated) float DeathImpulse = 0.f;

	UFUNCTION(Server, Reliable) void ServerThrow(ECampusThrowable Type, FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, float Cooked);
	UFUNCTION(NetMulticast, Unreliable) void MulticastThrowFX(ECampusThrowable Type);
	/** Throw stages (ECampusBodyThrow) played on the body on every other machine, so a throw is seen from the draw on. */
	UFUNCTION(Server, Unreliable) void ServerThrowStage(ECampusThrowable Type, uint8 Stage, float Rate);
	UFUNCTION(NetMulticast, Unreliable) void MulticastThrowStage(ECampusThrowable Type, uint8 Stage, float Rate);

	// ------------------------------------------------------------------ perks
	UPROPERTY(ReplicatedUsing = OnRep_Perks) TArray<ECampusPerk> Perks;
	bool HasPerk(ECampusPerk Perk) const { return Perks.Contains(Perk); }
	/** Family shared by all three perks (Count when mixed). */
	ECampusPerkGroup GetSpecialty() const;
	bool HasSpecialty(ECampusPerkGroup Group) const { return GetSpecialty() == Group; }
	/** Movement speed scale from perks and the Enforcer specialty's post-kill rush. */
	float GetPerkSpeedScale() const;
	/** Multiplier on grenade handling time (Fast Hands). */
	float GetHandlingScale() const;
	/** Server: this character got a kill (perk and specialty rewards). */
	void OnScoredKill();
	/** Local: the Recon specialty reveals nearby enemies until this time. */
	float RevealUntil = -100.f;
	/** Server-time end of the Enforcer specialty's speed rush. */
	UPROPERTY(Replicated) float RushUntil = -100.f;

	// ------------------------------------------------------------------ movement state
	bool IsSprinting() const;
	bool IsTacSprinting() const;
	bool IsSliding() const;
	bool IsMantling() const;
	bool IsAiming() const { return bAimHeld && CanAim(); }
	bool CanAim() const;
	const TCHAR* GetAimBlockReason() const;
	float GetRawADSAlpha() const { return ADSAlpha; }
	/** effective ADS progress, including a local wall obstruction while the aim input remains held */
	float GetADSAlpha() const;
	/** False while recovering from sprint, switching weapons, meleeing, etc. */
	bool CanFireNow() const;
	float GetLastUnsuppressedShotTime() const { return LastLoudShotTime; }
	void NotifyShotFired(bool bSuppressed);

	/** Aim kick applied to the control rotation over a few frames (local players) or to the bot aim offset. */
	void AddAimRecoil(float Pitch, float Yaw);
	/** Visual flinch when hit (local only). */
	void ApplyAimPunch(const FVector& FromDirection, float Damage);
	/** Cosmetic upper-body jolt on the third-person mesh (physics blended over the animation). */
	void PlayHitReaction(FName Bone, const FVector& Direction, float Strength);
	UFUNCTION(NetMulticast, Unreliable) void MulticastHitReact(FName Bone, FVector_NetQuantizeNormal Direction, float Strength);

	FVector GetAimOrigin() const;
	FVector GetAimDirection() const;
	FRotator GetAimRotation() const;
	/** Maps a world location on a first-person primitive to where it appears on screen in world space (accounts for viewmodel FOV). */
	FVector FirstPersonToWorldVisual(const FVector& WorldLocation) const;
	/** Door under the crosshair (or an open one right in front) within reach of the interact key. */
	ACampusDoor* FindUsableDoor() const;
	UFUNCTION(Server, Reliable) void ServerUseDoor(ACampusDoor* Door);

	// ------------------------------------------------------------------ bots
	void BotSetFire(bool bFire);
	void BotSetAim(bool bAim);
	void BotSetSprint(bool bSprint);
	/** Upgrades a running sprint to a tactical sprint (or drops back to a normal sprint). */
	void BotSetTacSprint(bool bTac);
	void BotSetCrouch(bool bCrouch);
	void BotReload();
	void BotJump();
	/** Crouch after Delay seconds of sprinting, which turns the sprint into a slide. */
	void BotSlide(float Delay);
	FRotator BotRecoilOffset = FRotator::ZeroRotator;

	/** Automation: "sprint", "tac", "slide", "jump", "crouch", "reload", "switch", "inspect", "melee", "fire", "aim", "stop". */
	void DebugAction(const FString& Action);
	/** Automation: movement input applied every tick (X right, Y forward). */
	FVector2D ScriptedMove = FVector2D::ZeroVector;

	// ------------------------------------------------------------------ options
	float MouseSensitivity = 2.0f;
	float ADSSensitivityMultiplier = 1.0f;
	float GamepadSensitivity = 1.0f;
	bool bInvertY = false;
	bool bToggleAim = false;
	bool bToggleSprint = false;
	/** Sprint starts as a tactical sprint (weapon raised) while stamina lasts. */
	bool bAutoTacSprint = true;
	float BaseFOV = 90.f;
	float BaseViewmodelFOV = 74.f;

	float LastDamageTime = -100.f;
	FVector LastDamageFrom = FVector::ZeroVector;

protected:
	UFUNCTION() void OnRep_Health(float OldHealth);
	UFUNCTION() void OnRep_Dead();
	UFUNCTION() void OnRep_Perks();
	void UpdateEquipmentRecharge(float DeltaSeconds);
	float EquipmentRechargeTimer = 0.f;
	int32 KillsThisLife = 0;
	uint8 StartLethals = 2;
	uint8 StartTacticals = 2;
	UFUNCTION() void OnRep_Weapons();
	UFUNCTION() void OnRep_CurrentWeaponIndex();

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_LookGamepad(const FInputActionValue& Value);
	void Input_JumpStarted();
	void Input_JumpCompleted();
	void Input_Sprint();
	void Input_SprintReleased();
	void Input_Crouch();
	void Input_CrouchHoldStarted();
	void Input_CrouchHoldCompleted();
	void Input_AimStarted();
	void Input_AimCompleted();
	void Input_FireStarted();
	void Input_FireCompleted();
	void Input_Reload();
	void Input_SwitchWeapon();
	void Input_Primary();
	void Input_Secondary();
	void Input_FireMode();
	void Input_Flashlight();
	void Input_Melee();
	void Input_Inspect();
	void Input_Interact();
	void Input_LethalStarted();
	void Input_LethalCompleted();
	void Input_TacticalStarted();
	void Input_TacticalCompleted();
	void Input_CycleLethal();
	void Input_CycleTactical();

	enum class EThrowPhase : uint8 { None, Prime, Throw, Recover };
	/** Local: grenade out (weapon dipped, pin pulled; a frag cooks while held), released, then the weapon comes back up. */
	void StartThrow(ECampusThrowable Type);
	void UpdateThrow();
	void LaunchFromHand();
	void SpawnThrowable(ECampusThrowable Type, const FVector& Origin, const FVector& Velocity, float FuseLeft);
	EThrowPhase ThrowPhase = EThrowPhase::None;
	ECampusThrowable ThrowType = ECampusThrowable::Frag;
	/** Set by the loadout on the server; the owner may still cycle to anything else it carries. */
	UPROPERTY(Replicated) ECampusThrowable SelectedLethal = ECampusThrowable::Frag;
	UPROPERTY(Replicated) ECampusThrowable SelectedTactical = ECampusThrowable::Flash;
	float SelectionChangeTime = -100.f;
	float ThrowPhaseStart = 0.f;
	/** Seconds into the current phase at the throw's playback rate. */
	float ThrowPhaseClock = 0.f;
	/** Fire / aim during the recovery speeds it up. */
	bool bThrowHurried = false;
	/** Fire / aim hurry the recovery; switching (bPutAway) puts a grenade back before the pin is out. True if cancelled. */
	bool InterruptThrow(bool bPutAway);
	float SpoonTime = -1.f;
	bool bThrowHeld = false;
	bool bPinPulled = false;
	bool bThrowLaunched = false;

	/** Plays a throw stage (ECampusBodyThrow) on the body here and sends it to the other machines. */
	void BroadcastThrowStage(ECampusThrowable Type, uint8 Stage);
	void PlayBodyThrow(ECampusThrowable Type, uint8 Stage, float Rate);
	/** The grenade in the body's left hand: out of the pouch, seated in the fingers, gone at the release. */
	void UpdateBodyGrenade();
	void ClearBodyGrenade();
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BodyGrenadeRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> BodyGrenade;
	ECampusThrowable BodyThrowType = ECampusThrowable::Frag;
	bool bBodyGrenadeSeated = false;

	/** Bots: drawn, swung, and let go from the body's hand at the top of the swing. */
	void BotThrowSwing();
	void BotThrowLaunch();
	FTimerHandle BotThrowHandle;
	ECampusThrowable BotThrowType = ECampusThrowable::Frag;
	FVector BotThrowTarget = FVector::ZeroVector;
	bool bBotThrowing = false;

	void UpdateFirstPersonSetup();
	void ApplyWeaponVisibility();
	void HandleWeaponChanged(bool bPlayEquip);
	void UpdateLocalState(float DeltaSeconds);
	void UpdateRecoil(float DeltaSeconds);
	void UpdateHealthRegen(float DeltaSeconds);
	void UpdateHitReaction(float DeltaSeconds);
	/** Drains the powered-ragdoll strength after death. */
	void UpdateDeathCrumple(float DeltaSeconds);
	/** Stand up out of a slide; keeps the momentum and resumes sprinting (slide cancel). */
	void CancelSlide(bool bResumeSprint, bool bJumping = false);
	/** Abort a weapon switch that is still holstering: the current weapon comes straight back up. */
	void CancelPendingSwitch();
	/** Any new input breaks out of an inspect and out of a melee recovery. */
	void InterruptActions();
	void StopSprinting();
	void StartRagdoll();
	void LookInput(float YawDegrees, float PitchDegrees);

	bool bAimHeld = false;
	bool bFireHeld = false;
	/** Hold-to-sprint: the key is down (the sprint ends with it, and comes back while it stays down). */
	bool bSprintKeyHeld = false;
	bool bCrouchHeldByHold = false;
	/** Hold-crouch was released mid-slide: the slide runs its course, then the character stands. */
	bool bStandAfterSlide = false;
	bool bWasSlidingLastFrame = false;
	float ADSAlpha = 0.f;
	float FireBlockedUntil = 0.f;
	float LastLoudShotTime = -100.f;
	float NotMovingForwardTime = 0.f;
	float MeleeCooldownUntil = 0.f;
	float ServerMeleeReadyTime = 0.f;
	FTimerHandle MeleeContactHandle;
	void ResolveMeleeContact(const FVector& Direction, uint8 Kind, TWeakObjectPtr<ACampusWeapon> AcceptedWeapon);
	bool bWasSprintingLastFrame = false;
	/** The tactical sprint was put off by a reload and resumes when the magazine is in. */
	bool bTacSprintAfterReload = false;
	bool bWasTacSprintingLastFrame = false;

	int32 PendingWeaponIndex = INDEX_NONE;
	float PendingSwitchTime = 0.f;

	FVector2D PendingRecoil = FVector2D::ZeroVector;
	FVector2D RecoilAccum = FVector2D::ZeroVector;
	FVector2D RecoilRecover = FVector2D::ZeroVector;
	float LastRecoilTime = -100.f;

	float FootstepAccum = 0.f;
	/** Standing in open water: how deep (cm, 0 when dry) and where its surface is. */
	float WadeDepth = 0.f;
	float WaterLine = 0.f;
	float WakeAccum = 0.f;
	void UpdateWading(float DeltaSeconds, bool bSilent);
	float LastFallSpeed = 0.f;
	float HitReactWeight = 0.f;
	bool bHitReactActive = false;
	/** 1 at death, falling to 0 as the ragdoll's joints let go. */
	float DeathDrive = 0.f;
	float DeathDriveStrength = 1.f;

public:
	bool WantsToFire() const { return bFireHeld; }
	bool IsAimHeld() const { return bAimHeld; }
};
