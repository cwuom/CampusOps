#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CampusMovementComponent.generated.h"

UENUM(BlueprintType)
enum class ECampusCustomMovement : uint8
{
	None = 0,
	Slide = 1,
	Mantle = 2
};

/**
 * COD-style locomotion: run / sprint / tactical sprint (stamina limited) / slide / mantle.
 * Sprint, tactical sprint, aim and mantle intent are sent through compressed flags so movement stays predicted.
 */
UCLASS()
class CAMPUSOPS_API UCampusMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	class FSavedMove_Campus : public FSavedMove_Character
	{
	public:
		typedef FSavedMove_Character Super;

		uint8 bSavedWantsToSprint : 1;
		uint8 bSavedWantsToTacSprint : 1;
		uint8 bSavedWantsToAim : 1;
		uint8 bSavedWantsToMantle : 1;
		float SavedTacStamina = 0.f;
		float SavedSlideTime = 0.f;
		float SavedSlideCooldown = 0.f;

		virtual void Clear() override;
		virtual uint8 GetCompressedFlags() const override;
		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
		virtual void PrepMoveFor(ACharacter* C) override;
	};

	class FNetworkPredictionData_Client_Campus : public FNetworkPredictionData_Client_Character
	{
	public:
		typedef FNetworkPredictionData_Client_Character Super;
		FNetworkPredictionData_Client_Campus(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}
		virtual FSavedMovePtr AllocateNewMove() override;
	};

public:
	UCampusMovementComponent();

	UPROPERTY(EditDefaultsOnly, Category = "Campus|Movement") float RunSpeed = 470.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Movement") float SprintSpeed = 690.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Movement") float TacSprintSpeed = 850.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Movement") float TacSprintDuration = 4.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Movement") float TacSprintRechargeRate = 0.75f;

	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideEnterSpeed = 500.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideImpulse = 260.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideExitSpeed = 300.f;
	/** Speed loss per second = 2 * SlideFriction * speed + SlideBraking (the engine's braking friction factor doubles the friction): ~1 s / 5.5 m out of a sprint, ~7 m out of a tactical sprint. */
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideFriction = 0.28f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideBraking = 260.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideGravity = 4000.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideMaxDuration = 1.15f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideCooldown = 0.45f;
	/** Degrees per second the slide path bends under full strafe input. */
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideSteerRate = 55.f;
	/** Extra horizontal speed when jumping out of a slide (slide jump). */
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideJumpBoost = 90.f;
	/** Tactical sprint seconds handed back by a slide cancel. */
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Slide") float SlideCancelStaminaRefund = 1.2f;

	/** Ledge heights above the feet that can be climbed over (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Mantle") float MantleMinHeight = 40.f;
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Mantle") float MantleMaxHeight = 175.f;
	/** How far in front of the capsule a ledge is looked for (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Campus|Mantle") float MantleReach = 55.f;

	/** Input-driven state (owning client), mirrored on the server via compressed flags. */
	bool bWantsToSprint = false;
	bool bWantsToTacSprint = false;
	bool bWantsToAim = false;
	/** Jump is held: climb any ledge in reach, on the ground or in the air. */
	bool bWantsToMantle = false;

	/** From the equipped weapon; identical on client and server because the weapon is replicated. */
	float WeaponSpeedMultiplier = 1.f;
	float WeaponADSSpeedMultiplier = 0.55f;

	float TacSprintStamina = 4.f;
	float SlideTime = 0.f;
	float SlideCooldownRemaining = 0.f;

	bool IsSprinting() const;
	bool IsTacSprinting() const;
	bool IsSliding() const { return MovementMode == MOVE_Custom && CustomMovementMode == (uint8)ECampusCustomMovement::Slide; }
	bool IsMantling() const { return MovementMode == MOVE_Custom && CustomMovementMode == (uint8)ECampusCustomMovement::Mantle; }
	/** 0..1 progress through the current mantle (0 when not mantling). */
	float GetMantleAlpha() const { return IsMantling() && MantleDuration > 0.f ? FMath::Clamp(MantleTime / MantleDuration, 0.f, 1.f) : 0.f; }
	/** The lip of the ledge being climbed (world), where the support hand plants; and how high it was above the feet. */
	FVector GetMantleLedge() const { return MantleLedge; }
	float GetMantleHeight() const { return MantleHeight; }
	float GetTacSprintStamina01() const { return TacSprintDuration > 0.f ? TacSprintStamina / TacSprintDuration : 0.f; }
	bool CanSprintInCurrentState() const;
	/** Leave the slide immediately (slide cancel / slide jump); velocity is kept and some tactical sprint comes back. */
	void CancelSlide();
	/** Speed scale from wading in open water (1 on dry ground). */
	float WadeScale() const;

	virtual float GetMaxSpeed() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual bool IsMovingOnGround() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;

private:
	void EnterSlide();
	void ExitSlide();
	void PhysSlide(float DeltaTime, int32 Iterations);
	bool GetSlideSurface(FHitResult& Hit) const;
	bool TryMantle();
	void PhysMantle(float DeltaTime, int32 Iterations);

	float LastSlideExitTime = -100.f;
	float LastSprintingTime = -100.f;
	float LastSprintingSpeed = 0.f;
	FVector MantleStart = FVector::ZeroVector;
	FVector MantleTarget = FVector::ZeroVector;
	FVector MantleDirection = FVector::ForwardVector;
	FVector MantleLedge = FVector::ZeroVector;
	float MantleHeight = 0.f;
	float MantleTime = 0.f;
	float MantleDuration = 0.4f;
	float MantleExitSpeed = 0.f;
};
