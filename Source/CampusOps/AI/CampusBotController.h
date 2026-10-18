#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CampusTypes.h"
#include "CampusBotController.generated.h"

class ACampusCharacter;

/**
 * Server-side bot for both teams. Senses with FOV + line of sight, hearing (gunshots, nearby sprinting) and shared
 * team intel; patrols lanes toward the front while spreading out from teammates and checking corners; fights at the
 * range its weapon likes, strafes, crouches, slides and jumps; falls back to cover when hurt or reloading.
 */
UCLASS()
class CAMPUSOPS_API ACampusBotController : public AAIController
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCampusBotCloseSpacingTest;
#endif

public:
	ACampusBotController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

	/** 0 recruit .. 3 veteran */
	int32 Skill = 1;

	void OnDamagedBy(AController* Attacker);
	void HearGunshot(ACampusCharacter* Shooter, const FVector& Location, float Radius);
	/** A teammate called out an enemy at Location. */
	void ReceiveIntel(const FVector& Location);
	/** Blinded by a flashbang (0..1): sees nothing, stops shooting and stumbles for a few seconds. */
	void OnFlashbanged(float Blind, const FVector& From);
	/** A teammate died nearby: go and trade the kill (KillerLocation is where the shot came from). */
	void OnTeammateKilled(const FVector& VictimLocation, const FVector& KillerLocation);
	FString GetDebugString() const;

private:
	enum class EBotState : uint8
	{
		Roam,
		Engage,
		Investigate,
		Cover
	};

	ACampusCharacter* GetCampusPawn() const;
	ECampusTeam GetTeam() const;
	void ApplySkill();
	void Think();
	void ThinkRoam(float Now);
	void ThinkEngage(float Now);
	bool YieldToClosePawn(const ACampusCharacter* Other);
	void ThinkInvestigate(float Now);
	void ThinkCover(float Now);
	void UpdateCombat(float DeltaSeconds);
	void UpdateAimError(float DeltaSeconds);
	void UpdateStuck(float Now);
	ACampusCharacter* FindVisibleEnemy() const;
	bool CanSee(const ACampusCharacter* Other, FVector* OutAimPoint = nullptr) const;
	bool IsLineOfFireClear(const FVector& From, const FVector& To) const;
	void PickRoamDestination();
	bool FindCoverFrom(const FVector& Threat, FVector& OutCover) const;
	bool MoveToPoint(const FVector& Point, bool bStrafe, float Acceptance);
	void ShareIntel(const FVector& Location) const;
	void SetLookOverride(const FVector& Point, float Duration);
	FVector GetAimPoint() const;
	/** Preferred engagement band (cm) for the equipped weapon. */
	FVector2D GetPreferredRange() const;
	void SetState(EBotState NewState);
	/** Runs from a live frag landing close by; true while running. */
	bool FleeGrenades(float Now);
	bool CanSeePoint(const FVector& Point) const;
	/** Lobs a frag or a flash into where the enemy went to ground, or smokes the lane to cover a retreat. */
	void ConsiderGrenade(float Now, bool bRetreating);
	/** Turns toward and checks out enemies heard running or sprinting nearby (not crouch-walkers or Ninjas). */
	void HearFootsteps(float Now);
	/** Mid-fight: moves to a fresh firing position off to the side (a flank or a new angle) that still sees T. */
	bool TryReposition(float Now, const ACampusCharacter* T);
	/** A duel that drags on is closed down: a flanking rush to a point beside the target. */
	bool TryPush(float Now, const ACampusCharacter* T);
	/** Anything standing still too long (no reload, no cover break) gets moving again. */
	void CheckIdle(float Now);
	/** 0 west .. 2 east lane of the map for a location. */
	int32 GetLane(const FVector& Location) const;
	int32 PreferredLane = 1;
	FVector IdleAnchor = FVector::ZeroVector;
	float IdleSince = 0.f;
	float EngageStartTime = 0.f;
	float NextPushTime = 0.f;
	/** Where the bot took up its current firing position, and when: it moves on before it turns into a camper. */
	FVector EngageAnchor = FVector::ZeroVector;
	float EngageAnchorTime = 0.f;
	/** Side of the last strafe (players alternate: A-D-A-D). */
	float StrafeSign = 1.f;
	/** Jump queued at the end of a slide (slide-hop). */
	float PendingJumpTime = 0.f;
	float NextFootstepCheckTime = 0.f;
	float BlindUntil = 0.f;
	float FleeUntil = 0.f;
	float NextGrenadeTime = 0.f;

	EBotState State = EBotState::Roam;
	TWeakObjectPtr<ACampusCharacter> Target;
	FVector LastKnownLocation = FVector::ZeroVector;
	FVector RoamGoal = FVector::ZeroVector;
	FVector CoverPoint = FVector::ZeroVector;
	FVector LookOverride = FVector::ZeroVector;
	float LookOverrideUntil = 0.f;
	float LastSeenTime = -100.f;
	float TargetAcquiredTime = 0.f;
	float StateTime = 0.f;
	float NextThinkTime = 0.f;
	float NextStrafeTime = 0.f;
	float NextGlanceTime = 0.f;
	float NextCoverCheckTime = 0.f;
	float CoverUntil = 0.f;
	float LastCoverTime = -100.f;
	float BurstEndTime = 0.f;
	float NextBurstTime = 0.f;
	float NextErrorTime = 0.f;
	float RoamRepathTime = 0.f;
	float ArrivedTime = -1.f;
	float SlideReleaseTime = 0.f;
	float NextManeuverTime = 0.f;
	float LastIntelShareTime = -100.f;
	bool bAimHead = false;
	bool bTriggerDown = false;
	float SemiReleaseTime = 0.f;
	FVector AimError = FVector::ZeroVector;
	FVector AimErrorTarget = FVector::ZeroVector;

	// Stuck detection
	FVector StuckCheckLocation = FVector::ZeroVector;
	float StuckCheckTime = 0.f;
	int32 StuckCount = 0;

	// Skill-driven tuning
	float ReactionTime = 0.5f;
	float AimErrorDegrees = 4.f;
	float TurnSpeed = 260.f;
	float HeadChance = 0.15f;
	float ViewDistance = 6000.f;
	float Aggression = 0.5f;
};
