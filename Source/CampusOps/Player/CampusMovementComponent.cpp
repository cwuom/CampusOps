#include "Player/CampusMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Map/CampusWater.h"

UCampusMovementComponent::UCampusMovementComponent()
{
	MaxWalkSpeed = RunSpeed;
	MaxWalkSpeedCrouched = 250.f;
	MaxAcceleration = 3400.f;
	BrakingDecelerationWalking = 2600.f;
	GroundFriction = 9.f;
	BrakingDecelerationFalling = 0.f;
	FallingLateralFriction = 0.f;
	AirControl = 0.42f;
	JumpZVelocity = 470.f;
	GravityScale = 1.2f;
	MaxStepHeight = 42.f;
	NavAgentProps.bCanCrouch = true;
	SetCrouchedHalfHeight(60.f);
	bCanWalkOffLedgesWhenCrouching = true;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
}

// ------------------------------------------------------------------ saved moves

void UCampusMovementComponent::FSavedMove_Campus::Clear()
{
	Super::Clear();
	bSavedWantsToSprint = false;
	bSavedWantsToTacSprint = false;
	bSavedWantsToAim = false;
	bSavedWantsToMantle = false;
	SavedTacStamina = 0.f;
	SavedSlideTime = 0.f;
	SavedSlideCooldown = 0.f;
}

uint8 UCampusMovementComponent::FSavedMove_Campus::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint) Result |= FLAG_Custom_0;
	if (bSavedWantsToTacSprint) Result |= FLAG_Custom_1;
	if (bSavedWantsToAim) Result |= FLAG_Custom_2;
	if (bSavedWantsToMantle) Result |= FLAG_Custom_3;
	return Result;
}

bool UCampusMovementComponent::FSavedMove_Campus::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Campus* Other = static_cast<const FSavedMove_Campus*>(NewMove.Get());
	if (bSavedWantsToSprint != Other->bSavedWantsToSprint || bSavedWantsToTacSprint != Other->bSavedWantsToTacSprint || bSavedWantsToAim != Other->bSavedWantsToAim
		|| bSavedWantsToMantle != Other->bSavedWantsToMantle)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void UCampusMovementComponent::FSavedMove_Campus::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const UCampusMovementComponent* Move = Cast<UCampusMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedWantsToSprint = Move->bWantsToSprint;
		bSavedWantsToTacSprint = Move->bWantsToTacSprint;
		bSavedWantsToAim = Move->bWantsToAim;
		bSavedWantsToMantle = Move->bWantsToMantle;
		SavedTacStamina = Move->TacSprintStamina;
		SavedSlideTime = Move->SlideTime;
		SavedSlideCooldown = Move->SlideCooldownRemaining;
	}
}

void UCampusMovementComponent::FSavedMove_Campus::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UCampusMovementComponent* Move = Cast<UCampusMovementComponent>(C->GetCharacterMovement()))
	{
		Move->bWantsToSprint = bSavedWantsToSprint;
		Move->bWantsToTacSprint = bSavedWantsToTacSprint;
		Move->bWantsToAim = bSavedWantsToAim;
		Move->bWantsToMantle = bSavedWantsToMantle;
		Move->TacSprintStamina = SavedTacStamina;
		Move->SlideTime = SavedSlideTime;
		Move->SlideCooldownRemaining = SavedSlideCooldown;
	}
}

FSavedMovePtr UCampusMovementComponent::FNetworkPredictionData_Client_Campus::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Campus());
}

FNetworkPredictionData_Client* UCampusMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);
	if (ClientPredictionData == nullptr)
	{
		UCampusMovementComponent* MutableThis = const_cast<UCampusMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Campus(*this);
		MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = 92.f;
		MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = 140.f;
	}
	return ClientPredictionData;
}

void UCampusMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToTacSprint = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bWantsToAim = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
	bWantsToMantle = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}

// ------------------------------------------------------------------ state queries

bool UCampusMovementComponent::CanSprintInCurrentState() const
{
	if (!UpdatedComponent || bWantsToAim || IsCrouching())
	{
		return false;
	}
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking)
	{
		return false;
	}
	const FVector Dir = (Acceleration.IsNearlyZero() ? Velocity : Acceleration).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Dir, UpdatedComponent->GetForwardVector()) > 0.6f;
}

bool UCampusMovementComponent::IsSprinting() const
{
	return bWantsToSprint && CanSprintInCurrentState();
}

bool UCampusMovementComponent::IsTacSprinting() const
{
	return bWantsToTacSprint && TacSprintStamina > 0.f && IsSprinting();
}

float UCampusMovementComponent::GetMaxSpeed() const
{
	if (IsSliding())
	{
		return TacSprintSpeed * 1.3f;
	}
	if (IsMantling())
	{
		return TacSprintSpeed;
	}
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking)
	{
		const float Wade = WadeScale();
		if (IsCrouching())
		{
			return MaxWalkSpeedCrouched * WeaponSpeedMultiplier * (bWantsToAim ? 0.8f : 1.f) * Wade;
		}
		if (IsTacSprinting())
		{
			return TacSprintSpeed * WeaponSpeedMultiplier * Wade;
		}
		if (IsSprinting())
		{
			return SprintSpeed * WeaponSpeedMultiplier * Wade;
		}
		return RunSpeed * WeaponSpeedMultiplier * (bWantsToAim ? WeaponADSSpeedMultiplier : 1.f) * Wade;
	}
	if (MovementMode == MOVE_Falling)
	{
		return (bWantsToTacSprint && TacSprintStamina > 0.f ? TacSprintSpeed : (bWantsToSprint ? SprintSpeed : RunSpeed)) * WeaponSpeedMultiplier;
	}
	return Super::GetMaxSpeed();
}

float UCampusMovementComponent::WadeScale() const
{
	if (!CharacterOwner || !UpdatedComponent)
	{
		return 1.f;
	}
	// Knee-deep water drags at the legs: a run becomes a wade, a sprint a hard slog.
	const FVector Feet = UpdatedComponent->GetComponentLocation() - FVector(0.f, 0.f, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float Depth = ACampusWater::DepthAt(GetWorld(), Feet);
	return FMath::Lerp(1.f, 0.55f, FMath::Clamp(Depth / 45.f, 0.f, 1.f));
}

float UCampusMovementComponent::GetMaxBrakingDeceleration() const
{
	if (IsSliding())
	{
		return SlideBraking;
	}
	return Super::GetMaxBrakingDeceleration();
}

bool UCampusMovementComponent::IsMovingOnGround() const
{
	return Super::IsMovingOnGround() || IsSliding();
}

bool UCampusMovementComponent::CanCrouchInCurrentState() const
{
	return Super::CanCrouchInCurrentState() || (IsSliding() && UpdatedComponent && !UpdatedComponent->IsSimulatingPhysics());
}

bool UCampusMovementComponent::CanAttemptJump() const
{
	// A slide is a crouch: the stand-up request (bWantsToCrouch cleared by the slide jump) must not block the jump.
	if (IsSliding() || (IsCrouching() && !bWantsToCrouch && IsMovingOnGround()))
	{
		return IsJumpAllowed();
	}
	return Super::CanAttemptJump();
}

bool UCampusMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	// Jump into a ledge: climb it instead.
	if (TryMantle())
	{
		return true;
	}
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const bool bFromSlide = IsSliding() || Now - LastSlideExitTime < 0.2f;
	if (IsSliding())
	{
		ExitSlide();
		SlideCooldownRemaining = SlideCooldown * 0.5f;
	}
	if (!Super::DoJump(bReplayingMoves, DeltaTime))
	{
		return false;
	}
	if (bFromSlide)
	{
		// Slide jump: the slide's momentum carries into the air with a little extra push.
		const FVector Dir = Velocity.GetSafeNormal2D();
		Velocity += Dir * SlideJumpBoost;
		if (!bReplayingMoves)
		{
			TacSprintStamina = FMath::Min(TacSprintDuration, TacSprintStamina + SlideCancelStaminaRefund * 0.5f);
		}
	}
	return true;
}

// ------------------------------------------------------------------ update

void UCampusMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (IsSprinting())
	{
		LastSprintingTime = Now;
		LastSprintingSpeed = Velocity.Size2D();
	}
	// Aiming drops the sprint (and the speed with it) at once, so a slide pressed just after raising the sights still
	// goes off with the sprint's momentum.
	const bool bAimMomentum = !bWantsToSprint && bWantsToAim && Now - LastSprintingTime < 0.35f && LastSprintingSpeed > SlideEnterSpeed;
	if (MovementMode == MOVE_Walking && bWantsToCrouch && !IsCrouching() && SlideCooldownRemaining <= 0.f && WadeScale() > 0.9f
		&& ((bWantsToSprint && Velocity.SizeSquared2D() > FMath::Square(SlideEnterSpeed)) || bAimMomentum))
	{
		if (bAimMomentum)
		{
			const FVector Dir = Velocity.SizeSquared2D() > 1.f ? Velocity.GetSafeNormal2D() : UpdatedComponent->GetForwardVector().GetSafeNormal2D();
			Velocity = Dir * LastSprintingSpeed + FVector(0.f, 0.f, Velocity.Z);
		}
		EnterSlide();
	}
	// Holding jump against a ledge climbs it, also mid-air (jump at a wall, then pull up).
	if (bWantsToMantle && !IsMantling() && (IsFalling() || IsMovingOnGround()))
	{
		TryMantle();
	}

	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
}

void UCampusMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	if (IsTacSprinting())
	{
		TacSprintStamina = FMath::Max(0.f, TacSprintStamina - DeltaSeconds);
	}
	else if (!bWantsToTacSprint || !IsMovingOnGround())
	{
		TacSprintStamina = FMath::Min(TacSprintDuration, TacSprintStamina + DeltaSeconds * TacSprintRechargeRate);
	}

	if (SlideCooldownRemaining > 0.f && !IsSliding())
	{
		SlideCooldownRemaining = FMath::Max(0.f, SlideCooldownRemaining - DeltaSeconds);
	}
}

void UCampusMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);
	if (CustomMovementMode == (uint8)ECampusCustomMovement::Slide)
	{
		PhysSlide(DeltaTime, Iterations);
	}
	else if (CustomMovementMode == (uint8)ECampusCustomMovement::Mantle)
	{
		PhysMantle(DeltaTime, Iterations);
	}
}

void UCampusMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	// Crouch held through the landing of a sprint jump: land straight into a slide.
	const bool bSlideOnLand = bWantsToCrouch && (bWantsToSprint || bWantsToAim) && SlideCooldownRemaining <= 0.f && Velocity.SizeSquared2D() > FMath::Square(SlideEnterSpeed);
	Super::ProcessLanded(Hit, remainingTime, Iterations);
	if (bSlideOnLand && MovementMode == MOVE_Walking)
	{
		EnterSlide();
	}
}

// ------------------------------------------------------------------ slide

void UCampusMovementComponent::EnterSlide()
{
	// Shrink to the crouched capsule while still walking: custom movement modes crouch in place, which would leave
	// the capsule hanging above the floor and end the slide on its first frame.
	if (!IsCrouching())
	{
		Crouch(false);
	}
	SlideTime = 0.f;
	Velocity += Velocity.GetSafeNormal2D() * SlideImpulse;
	SetMovementMode(MOVE_Custom, (uint8)ECampusCustomMovement::Slide);
}

void UCampusMovementComponent::ExitSlide()
{
	SlideCooldownRemaining = SlideCooldown;
	LastSlideExitTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	SetMovementMode(MOVE_Walking);
}

void UCampusMovementComponent::CancelSlide()
{
	if (IsSliding())
	{
		ExitSlide();
		SlideCooldownRemaining = SlideCooldown * 0.5f;
		// Slide cancel: the legs get some tactical sprint back, so slide-cancel chains keep the pace up.
		TacSprintStamina = FMath::Min(TacSprintDuration, TacSprintStamina + SlideCancelStaminaRefund);
	}
}

bool UCampusMovementComponent::GetSlideSurface(FHitResult& Hit) const
{
	if (!CharacterOwner)
	{
		return false;
	}
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector End = Start + FVector::DownVector * (HalfHeight + 40.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusSlide), false, CharacterOwner);
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

void UCampusMovementComponent::PhysSlide(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}
	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	SlideTime += DeltaTime;

	FHitResult SurfaceHit;
	if (!bWantsToCrouch || !GetSlideSurface(SurfaceHit) || Velocity.SizeSquared2D() < FMath::Square(SlideExitSpeed) || SlideTime > SlideMaxDuration)
	{
		ExitSlide();
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	Velocity += SlideGravity * FVector::DownVector * DeltaTime;

	// Strafe input bends the path; it never adds speed, so the slide always runs down at the same rate.
	const float Steer = FVector::DotProduct(Acceleration.GetSafeNormal2D(), UpdatedComponent->GetRightVector());
	if (FMath::Abs(Steer) > 0.3f)
	{
		Velocity = Velocity.RotateAngleAxis(Steer * SlideSteerRate * DeltaTime, FVector::UpVector);
	}
	Acceleration = FVector::ZeroVector;

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		CalcVelocity(DeltaTime, SlideFriction, false, GetMaxBrakingDeceleration());
	}
	ApplyRootMotionToVelocity(DeltaTime);

	++Iterations;
	bJustTeleported = false;

	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FQuat OldRotation = UpdatedComponent->GetComponentQuat();
	FHitResult Hit(1.f);
	const FVector Adjusted = Velocity * DeltaTime;
	SafeMoveUpdatedComponent(Adjusted, OldRotation, true, Hit);

	if (Hit.Time < 1.f)
	{
		HandleImpact(Hit, DeltaTime, Adjusted);
		SlideAlongSurface(Adjusted, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}

	FHitResult NewSurfaceHit;
	if (!GetSlideSurface(NewSurfaceHit) || Velocity.SizeSquared2D() < FMath::Square(SlideExitSpeed))
	{
		ExitSlide();
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	}
}

// ------------------------------------------------------------------ mantle

bool UCampusMovementComponent::TryMantle()
{
	if (!CharacterOwner || !UpdatedComponent || IsMantling() || IsSliding() || !(IsFalling() || IsMovingOnGround()))
	{
		return false;
	}
	// Only when pushing into the ledge (or already moving at it), never backing away from it.
	const FVector Fwd = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const FVector Intent = (Acceleration.IsNearlyZero() ? Velocity : Acceleration).GetSafeNormal2D();
	if (!Intent.IsNearlyZero() && FVector::DotProduct(Intent, Fwd) < 0.5f)
	{
		return false;
	}
	if (IsCrouching())
	{
		// 站起请求还未调整真实胶囊时，不能用蹲姿尺寸缓存整条攀爬路径
		if (bWantsToCrouch)
		{
			return false;
		}
		UnCrouch(false);
		if (IsCrouching())
		{
			return false;
		}
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const FVector Feet = Loc - FVector(0.f, 0.f, HalfHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusMantle), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();

	// A near-vertical face within reach, somewhere between the knees and the head.
	FHitResult Wall;
	bool bWall = false;
	for (const float Z : { 35.f, 55.f, 95.f, 135.f, 165.f })
	{
		const FVector Start = Feet + FVector(0.f, 0.f, Z);
		if (GetWorld()->SweepSingleByChannel(Wall, Start, Start + Fwd * (Radius + MantleReach), FQuat::Identity, Channel, FCollisionShape::MakeSphere(8.f), Params, Response)
			&& !Wall.bStartPenetrating && FMath::Abs(Wall.ImpactNormal.Z) < 0.35f)
		{
			bWall = true;
			break;
		}
	}
	if (!bWall || FVector::DotProduct(-Wall.ImpactNormal.GetSafeNormal2D(), Fwd) < 0.55f)
	{
		return false;
	}

	// The top of the ledge, just past the face.
	const FVector Over = Wall.ImpactPoint - Wall.ImpactNormal.GetSafeNormal2D() * (Radius * 0.6f + 6.f);
	FHitResult Top;
	const FVector DownStart(Over.X, Over.Y, Feet.Z + MantleMaxHeight + 25.f);
	const FVector DownEnd(Over.X, Over.Y, Feet.Z + MantleMinHeight - 10.f);
	if (!GetWorld()->SweepSingleByChannel(Top, DownStart, DownEnd, FQuat::Identity, Channel, FCollisionShape::MakeSphere(6.f), Params, Response)
		|| Top.bStartPenetrating || Top.ImpactNormal.Z < 0.7f)
	{
		return false;
	}
	const float LedgeHeight = Top.ImpactPoint.Z - Feet.Z;
	if (LedgeHeight < MantleMinHeight || LedgeHeight > MantleMaxHeight)
	{
		return false;
	}

	// 先确认站立终点和抬升后的横移通道，避免持续按跳时反复抓取被挡住的台面
	const FVector Into = -Wall.ImpactNormal.GetSafeNormal2D();
	const FVector Target = FVector(Over.X, Over.Y, Top.ImpactPoint.Z) + Into * (Radius * 0.6f) + FVector(0.f, 0.f, HalfHeight + 3.f);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	if (GetWorld()->OverlapBlockingTestByChannel(Target, FQuat::Identity, Channel, Shape, Params, Response))
	{
		return false;
	}
	const FVector Rise(Loc.X, Loc.Y, Target.Z);
	if (GetWorld()->SweepTestByChannel(Loc, Rise, FQuat::Identity, Channel, FCollisionShape::MakeCapsule(Radius - 2.f, HalfHeight - 2.f), Params, Response))
	{
		return false;
	}
	if (GetWorld()->SweepTestByChannel(Rise, Target, FQuat::Identity, Channel, Shape, Params, Response))
	{
		return false;
	}

	MantleStart = Loc;
	MantleTarget = Target;
	MantleDirection = Into;
	// A forearm's length onto the top: the hand is planted on it, not hooked over the edge.
	MantleLedge = FVector(Wall.ImpactPoint.X, Wall.ImpactPoint.Y, Top.ImpactPoint.Z) + Into * 18.f;
	MantleHeight = LedgeHeight;
	MantleTime = 0.f;
	// Low walls are vaulted quickly; chest-high ledges take a real pull-up (with the weight of a loaded-out body).
	MantleDuration = FMath::GetMappedRangeValueClamped(FVector2D(MantleMinHeight, MantleMaxHeight), FVector2D(0.42f, 0.78f), LedgeHeight);
	MantleExitSpeed = FMath::Clamp(Velocity.Size2D(), 260.f, SprintSpeed);
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Custom, (uint8)ECampusCustomMovement::Mantle);
	UE_LOG(LogTemp, Display, TEXT("[CampusMantle] start from=%s to=%s height=%.1f halfHeight=%.1f duration=%.3f"), *MantleStart.ToString(), *MantleTarget.ToString(), MantleHeight, HalfHeight, MantleDuration);
	return true;
}

void UCampusMovementComponent::PhysMantle(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME || !UpdatedComponent)
	{
		return;
	}
	MantleTime += DeltaTime;
	const float A = FMath::Clamp(MantleTime / FMath::Max(MantleDuration, 0.05f), 0.f, 1.f);
	// 抬升后段才逐渐横移，利用胶囊球帽的余量放缓越顶，同时保留逐帧碰撞约束
	const float Up = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((A - 0.06f) / 0.64f, 0.f, 1.f));
	const float Across = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((A - 0.55f) / 0.45f, 0.f, 1.f));
	const FVector Desired(FMath::Lerp(MantleStart.X, MantleTarget.X, Across), FMath::Lerp(MantleStart.Y, MantleTarget.Y, Across), FMath::Lerp(MantleStart.Z, MantleTarget.Z, Up));
	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	FHitResult Hit;
	SafeMoveUpdatedComponent(Desired - OldLocation, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.bBlockingHit)
	{
		// 中途出现障碍时停在碰撞前，不能把受阻位移或脱嵌修正带成下一帧冲量
		Velocity = FVector::ZeroVector;
		UE_LOG(LogTemp, Display, TEXT("[CampusMantle] blocked at=%s alpha=%.3f actor=%s"), *UpdatedComponent->GetComponentLocation().ToString(), A, *GetNameSafe(Hit.GetActor()));
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(0.f, Iterations);
		return;
	}
	Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
	if (A >= 1.f)
	{
		Velocity = MantleDirection * MantleExitSpeed * 0.65f;
		UE_LOG(LogTemp, Display, TEXT("[CampusMantle] complete at=%s"), *UpdatedComponent->GetComponentLocation().ToString());
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(0.f, Iterations);
	}
}
