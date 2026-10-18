#include "AI/CampusBotController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusMovementComponent.h"
#include "Weapons/CampusWeapon.h"
#include "Weapons/CampusThrowable.h"
#include "Game/CampusGameModeBase.h"
#include "Game/CampusGameState.h"
#include "Game/CampusPlayerState.h"
#include "Map/CampusSchoolBuilder.h"
#include "CampusSpring.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// 身体和持枪手会伸出胶囊，近身退让需要留出这部分镜头空间
	constexpr float PawnViewClearance = 80.f;

	/** Direction each team pushes along Y: Alpha spawns by the south gate, Bravo on the north field. */
	float TeamForwardSign(ECampusTeam Team)
	{
		return Team == ECampusTeam::Bravo ? -1.f : 1.f;
	}

	const TCHAR* BotStateLabel(int32 State)
	{
		static const TCHAR* Names[] = { TEXT("Roam"), TEXT("Engage"), TEXT("Investigate"), TEXT("Cover") };
		return Names[FMath::Clamp(State, 0, 3)];
	}
}

ACampusBotController::ACampusBotController()
{
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = true;
	bSetControlRotationFromPawnOrientation = false;
}

ACampusCharacter* ACampusBotController::GetCampusPawn() const
{
	return Cast<ACampusCharacter>(GetPawn());
}

ECampusTeam ACampusBotController::GetTeam() const
{
	const ACampusPlayerState* PS = GetPlayerState<ACampusPlayerState>();
	return PS ? PS->Team : ECampusTeam::None;
}

void ACampusBotController::ApplySkill()
{
	static const float Reaction[] = { 0.62f, 0.45f, 0.32f, 0.22f };
	static const float Error[] = { 5.0f, 3.6f, 2.4f, 1.5f };
	static const float Turn[] = { 200.f, 290.f, 390.f, 520.f };
	static const float Head[] = { 0.08f, 0.15f, 0.25f, 0.35f };
	static const float Aggro[] = { 0.35f, 0.5f, 0.62f, 0.75f };
	const int32 S = FMath::Clamp(Skill, 0, 3);
	ReactionTime = Reaction[S] * FMath::FRandRange(0.85f, 1.15f);
	AimErrorDegrees = Error[S];
	TurnSpeed = Turn[S];
	HeadChance = Head[S];
	// Each bot gets a temperament: some push and slide in, others hold angles.
	Aggression = FMath::Clamp(Aggro[S] + FMath::FRandRange(-0.2f, 0.2f), 0.1f, 0.95f);
}

void ACampusBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplySkill();
	SetState(EBotState::Roam);
	Target = nullptr;
	LastSeenTime = -100.f;
	bTriggerDown = false;
	ArrivedTime = -1.f;
	StuckCount = 0;
	SlideReleaseTime = 0.f;
	const float Now = GetWorld()->GetTimeSeconds();
	NextThinkTime = Now + FMath::FRandRange(0.1f, 0.4f);
	RoamRepathTime = 0.f;
	if (InPawn)
	{
		SetControlRotation(InPawn->GetActorRotation());
		StuckCheckLocation = InPawn->GetActorLocation();
		StuckCheckTime = Now;
		IdleAnchor = InPawn->GetActorLocation();
		IdleSince = Now;
	}
	// Spread over the lanes: take the one the fewest teammates are working.
	int32 LaneUse[3] = { 0, 0, 0 };
	for (TActorIterator<ACampusBotController> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->GetTeam() == GetTeam())
		{
			++LaneUse[FMath::Clamp(It->PreferredLane, 0, 2)];
		}
	}
	PreferredLane = 1;
	for (const int32 L : { 0, 2, 1 })
	{
		if (LaneUse[L] < LaneUse[PreferredLane])
		{
			PreferredLane = L;
		}
	}
	if (ACampusCharacter* C = Cast<ACampusCharacter>(InPawn))
	{
		if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
		{
			// Bots steer around each other in doorways and corridors instead of shoving.
			Move->bUseRVOAvoidance = true;
			Move->AvoidanceConsiderationRadius = 280.f;
			Move->AvoidanceWeight = 0.5f;
		}
	}
}

void ACampusBotController::OnUnPossess()
{
	StopMovement();
	Super::OnUnPossess();
}

void ACampusBotController::SetState(EBotState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		StateTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		ArrivedTime = -1.f;
	}
}

void ACampusBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me || !Me->IsAlive())
	{
		return;
	}

	const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>();
	if (GS && GS->Phase != ECampusMatchPhase::Playing)
	{
		Me->BotSetFire(false);
		Me->BotSetSprint(false);
		StopMovement();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (PendingJumpTime > 0.f && Now >= PendingJumpTime)
	{
		PendingJumpTime = 0.f;
		Me->BotJump();
	}
	if (SlideReleaseTime > 0.f && Now >= SlideReleaseTime)
	{
		SlideReleaseTime = 0.f;
		Me->BotSetCrouch(false);
	}
	if (Now >= NextThinkTime)
	{
		NextThinkTime = Now + FMath::FRandRange(0.14f, 0.24f);
		Think();
	}
	// 导航决策之间也让出玩家镜头前的空间，防止推进惯性继续挤近
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && YieldToClosePawn(PC->GetPawn<ACampusCharacter>()))
		{
			break;
		}
	}
	UpdateAimError(DeltaSeconds);
	UpdateCombat(DeltaSeconds);
}

// ------------------------------------------------------------------ senses

bool ACampusBotController::CanSee(const ACampusCharacter* Other, FVector* OutAimPoint) const
{
	const ACampusCharacter* Me = GetCampusPawn();
	if (!Me || !Other || !Other->IsAlive() || GetWorld()->GetTimeSeconds() < BlindUntil)
	{
		return false;
	}
	const FVector Eye = Me->GetPawnViewLocation();
	const FVector Chest = Other->GetMesh()->GetBoneLocation(TEXT("spine_03"));
	const FVector Head = Other->GetMesh()->GetBoneLocation(TEXT("head"));
	const FVector Pelvis = Other->GetMesh()->GetBoneLocation(TEXT("pelvis"));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotLOS), false, Me);
	for (const FVector& P : { Chest, Head, Pelvis })
	{
		// Smoke screens: only someone right at the edge of the cloud is still made out.
		if (ACampusThrowable::SmokeOcclusion(GetWorld(), Eye, P) > 0.5f)
		{
			continue;
		}
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, P, ECC_Visibility, Params) || Hit.GetActor() == Other)
		{
			if (OutAimPoint)
			{
				*OutAimPoint = P;
			}
			return true;
		}
	}
	return false;
}

ACampusCharacter* ACampusBotController::FindVisibleEnemy() const
{
	const ACampusCharacter* Me = GetCampusPawn();
	if (!Me)
	{
		return nullptr;
	}
	const FVector Eye = Me->GetPawnViewLocation();
	const FVector Forward = GetControlRotation().Vector();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bAlert = Now - LastSeenTime < 4.f || State == EBotState::Investigate || State == EBotState::Cover;

	ACampusCharacter* Best = nullptr;
	float BestScore = FLT_MAX;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		ACampusCharacter* Other = *It;
		if (!Other->IsAlive() || !Me->IsEnemyOf(Other))
		{
			continue;
		}
		const FVector ToOther = Other->GetActorLocation() - Eye;
		const float Dist = ToOther.Size();
		if (Dist > ViewDistance)
		{
			continue;
		}
		const float Cos = FVector::DotProduct(ToOther.GetSafeNormal(), Forward);
		// A weapon light shone this way gives its holder away from well outside the cone of attention.
		const ACampusWeapon* OtherWeapon = Other->GetCurrentWeapon();
		const bool bLitBy = OtherWeapon && OtherWeapon->IsFlashlightOn() && Dist < 3500.f
			&& FVector::DotProduct(Other->GetAimDirection(), -ToOther.GetSafeNormal()) > 0.85f;
		// Peripheral vision widens when alert; anything brushing past within a few metres is noticed regardless.
		const bool bInFov = Cos > FMath::Cos(FMath::DegreesToRadians(bAlert ? 100.f : 72.f)) || Dist < 380.f || bLitBy;
		if (!bInFov)
		{
			continue;
		}
		// The current target keeps priority unless someone is much closer.
		const float Score = Dist * (Other == Target.Get() ? 0.6f : 1.f);
		if (Score < BestScore && CanSee(Other))
		{
			Best = Other;
			BestScore = Score;
		}
	}
	return Best;
}

bool ACampusBotController::IsLineOfFireClear(const FVector& From, const FVector& To) const
{
	const ACampusCharacter* Me = GetCampusPawn();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotFriendlyFire), false, Me);
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, CAMPUS_TRACE_WEAPON, FCollisionShape::MakeSphere(12.f), Params))
	{
		const ACampusCharacter* HitChar = Cast<ACampusCharacter>(Hit.GetActor());
		if (HitChar && Me && !Me->IsEnemyOf(HitChar))
		{
			return false;
		}
	}
	return true;
}

// ------------------------------------------------------------------ decisions

void ACampusBotController::Think()
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	UpdateStuck(Now);
	CheckIdle(Now);

	if (Now < BlindUntil)
	{
		// Blind: no shooting, staggering about and turning away from where it came from.
		Me->BotSetFire(false);
		bTriggerDown = false;
		Me->BotSetAim(false);
		if (Now >= NextStrafeTime)
		{
			NextStrafeTime = Now + FMath::FRandRange(0.5f, 1.1f);
			MoveToPoint(Me->GetActorLocation() + FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(150.f, 350.f), false, 40.f);
		}
		return;
	}
	if (FleeGrenades(Now))
	{
		return;
	}

	if (ACampusCharacter* Seen = FindVisibleEnemy())
	{
		if (Target.Get() != Seen)
		{
			// A target that was being tracked a moment ago is reacquired faster than a fresh contact.
			const bool bRecent = Now - LastSeenTime < 1.5f;
			Target = Seen;
			TargetAcquiredTime = Now - (bRecent ? ReactionTime * 0.6f : 0.f) + (Seen->HasPerk(ECampusPerk::ColdBlooded) ? ReactionTime * 0.7f : 0.f);
			bAimHead = FMath::FRand() < HeadChance;
			const float Dist = FVector::Dist(Seen->GetActorLocation(), Me->GetActorLocation());
			AimError = FMath::VRand() * Dist * FMath::Tan(FMath::DegreesToRadians(AimErrorDegrees * 1.6f));
		}
		LastKnownLocation = Seen->GetActorLocation();
		LastSeenTime = Now;
		if (Now - LastIntelShareTime > 2.5f && !Seen->HasPerk(ECampusPerk::Ghost))
		{
			LastIntelShareTime = Now;
			ShareIntel(LastKnownLocation);
		}
		if (State != EBotState::Engage && State != EBotState::Cover)
		{
			SetState(EBotState::Engage);
			SlideReleaseTime = 0.f;
			EngageStartTime = Now;
		}
	}
	else if (State == EBotState::Engage && Now - LastSeenTime > 1.1f)
	{
		Me->BotSetFire(false);
		SetState(EBotState::Investigate);
		MoveToPoint(LastKnownLocation, false, 120.f);
	}
	else if (State == EBotState::Roam || State == EBotState::Investigate)
	{
		HearFootsteps(Now);
	}

	switch (State)
	{
	case EBotState::Roam: ThinkRoam(Now); break;
	case EBotState::Engage: ThinkEngage(Now); break;
	case EBotState::Investigate: ThinkInvestigate(Now); break;
	case EBotState::Cover: ThinkCover(Now); break;
	}
}

void ACampusBotController::ThinkRoam(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	ACampusWeapon* Weapon = Me->GetCurrentWeapon();
	Me->BotSetAim(false);
	if (SlideReleaseTime <= 0.f)
	{
		Me->BotSetCrouch(false);
	}

	const EPathFollowingStatus::Type Status = GetMoveStatus();
	if (Status == EPathFollowingStatus::Idle || Now > RoamRepathTime)
	{
		if (Status == EPathFollowingStatus::Idle && ArrivedTime < 0.f && !RoamGoal.IsZero())
		{
			// Arrived: hold the spot briefly and clear the angles before moving on.
			ArrivedTime = Now;
		}
		const float Hold = FMath::Lerp(1.6f, 0.4f, Aggression);
		if (ArrivedTime < 0.f || Now - ArrivedTime > Hold || Now > RoamRepathTime)
		{
			ArrivedTime = -1.f;
			PickRoamDestination();
		}
	}

	const float DistToGoal = FVector::Dist2D(Me->GetActorLocation(), RoamGoal);
	const bool bMoving = GetMoveStatus() == EPathFollowingStatus::Moving;
	const bool bCleared = !bMoving || DistToGoal < 700.f;
	if (bCleared)
	{
		// Near the goal or holding: walk and check corners.
		Me->BotSetSprint(false);
		if (Now >= NextGlanceTime)
		{
			NextGlanceTime = Now + FMath::FRandRange(0.9f, 2.2f);
			const FVector Base = Me->GetActorLocation();
			const float Yaw = GetControlRotation().Yaw + FMath::FRandRange(-120.f, 120.f);
			SetLookOverride(Base + FRotator(0.f, Yaw, 0.f).Vector() * 1500.f + FVector(0.f, 0.f, 60.f), FMath::FRandRange(0.6f, 1.3f));
		}
	}
	else
	{
		Me->BotSetSprint(true);
		// Long open runs are covered at tactical sprint while the legs have it.
		Me->BotSetTacSprint(DistToGoal > 2200.f && FMath::FRand() < 0.6f + 0.3f * Aggression);
		// And now and then a slide, often hopped out of, the way players keep their momentum up.
		if (DistToGoal > 1600.f && Now >= NextManeuverTime && Me->IsSprinting() && FMath::FRand() < 0.05f + 0.1f * Aggression)
		{
			NextManeuverTime = Now + FMath::FRandRange(4.f, 9.f);
			Me->BotSlide(0.05f);
			SlideReleaseTime = Now + 0.8f;
			PendingJumpTime = FMath::FRand() < 0.6f ? Now + FMath::FRandRange(0.45f, 0.65f) : 0.f;
		}
	}

	if (Weapon && !Weapon->IsReloading() && Weapon->GetAmmoInMag() < Weapon->GetStats().MagSize * 0.6f && Weapon->GetAmmoReserve() > 0)
	{
		Me->BotSetSprint(false);
		Me->BotReload();
	}
	if (Me->CurrentWeaponIndex != 0 && Me->Weapons.Num() > 0 && !Me->IsSwitchingWeapons())
	{
		Me->RequestEquip(0);
	}
}

bool ACampusBotController::YieldToClosePawn(const ACampusCharacter* Other)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me || !Other || Me == Other || !Other->IsAlive())
	{
		return false;
	}
	UCampusMovementComponent* Move = Me->GetCampusMovement();
	if (!Move || Move->IsMantling())
	{
		return false;
	}
	const FVector Separation = Other->GetActorLocation() - Me->GetActorLocation();
	const float Radius = Me->GetCapsuleComponent()->GetScaledCapsuleRadius() + Other->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Height = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + Other->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Clearance = Radius + PawnViewClearance;
	if (FMath::Abs(Separation.Z) >= Height || Separation.SizeSquared2D() >= FMath::Square(Clearance))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotViewClearance), false, Me);
	Params.AddIgnoredActor(Other);
	const ACampusCharacter* Pawns[] = { Me, Other };
	for (const ACampusCharacter* NearbyCharacter : Pawns)
	{
		for (const ACampusWeapon* Weapon : NearbyCharacter->Weapons)
		{
			Params.AddIgnoredActor(Weapon);
		}
	}
	FHitResult Obstruction;
	if (GetWorld()->LineTraceSingleByChannel(Obstruction, Me->GetPawnViewLocation(), Other->GetPawnViewLocation(), ECC_Visibility, Params))
	{
		return false;
	}
	const FVector Toward = Separation.SizeSquared2D() <= KINDA_SMALL_NUMBER ? Me->GetActorForwardVector().GetSafeNormal2D() : Separation.GetSafeNormal2D();
	const FVector Velocity = Move->Velocity;
	const float VerticalSpeed = Move->IsFalling() ? Velocity.Z : 0.f;
	SlideReleaseTime = 0.f;
	PendingJumpTime = 0.f;
	if (Move->IsSliding())
	{
		Move->CancelSlide();
		Me->BotSetCrouch(false);
	}
	Me->BotSetSprint(false);
	StopMovement();
	// 只移除朝玩家逼近的速度，保留横向绕行和空中的竖直运动
	Move->Velocity = Velocity - Toward * FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Velocity, Toward)));
	Move->Velocity.Z = VerticalSpeed;
	if (Move->IsMovingOnGround())
	{
		Me->AddMovementInput(-Toward, 1.f, true);
	}
	return true;
}

void ACampusBotController::ThinkEngage(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	ACampusCharacter* T = Target.Get();
	if (!T || !T->IsAlive())
	{
		Target = nullptr;
		Me->BotSetFire(false);
		SetState(Now - LastSeenTime < 3.f ? EBotState::Investigate : EBotState::Roam);
		return;
	}
	if (YieldToClosePawn(T))
	{
		return;
	}
	ACampusWeapon* Weapon = Me->GetCurrentWeapon();
	const FVector MyLoc = Me->GetActorLocation();
	const float Dist = FVector::Dist(T->GetActorLocation(), MyLoc);
	const FVector2D Range = GetPreferredRange();
	const bool bPushing = SlideReleaseTime > 0.f;
	if (!bPushing)
	{
		Me->BotSetSprint(false);
		Me->BotSetAim(Dist > 650.f || (Skill >= 2 && Dist > 380.f));
	}

	// Out of rounds: up close the pistol comes out, otherwise reload, from cover if there is any.
	const bool bEmpty = Weapon && Weapon->GetAmmoInMag() == 0;
	const bool bCanReload = Weapon && Weapon->GetAmmoReserve() > 0 && !Weapon->IsReloading();
	if (bEmpty && Dist < 1100.f && Me->CurrentWeaponIndex == 0 && Me->Weapons.Num() > 1 && !Me->IsSwitchingWeapons())
	{
		const ACampusWeapon* Sidearm = Me->Weapons[1];
		if (Sidearm && Sidearm->GetAmmoInMag() > 0)
		{
			Me->RequestEquip(1);
			return;
		}
	}
	const bool bHurt = Me->GetHealth01() < 0.5f;
	if ((bHurt || (bEmpty && Dist > 800.f)) && Now - LastCoverTime > 4.f && Now >= NextCoverCheckTime)
	{
		NextCoverCheckTime = Now + 0.8f;
		FVector Cover;
		if (FindCoverFrom(T->GetPawnViewLocation(), Cover))
		{
			CoverPoint = Cover;
			CoverUntil = Now + FMath::FRandRange(1.0f, 2.0f);
			LastCoverTime = Now;
			ConsiderGrenade(Now, true);
			Me->BotSetCrouch(false);
			Me->BotSetAim(false);
			SetState(EBotState::Cover);
			MoveToPoint(Cover, true, 45.f);
			if (bEmpty && bCanReload)
			{
				Me->BotReload();
			}
			return;
		}
	}
	if (bEmpty && bCanReload)
	{
		Me->BotReload();
	}

	if (bPushing || Now < NextStrafeTime)
	{
		return;
	}
	const bool bClose = Dist < 1500.f;
	// Up close players jiggle (quick A-D-A-D) to spoil the other's aim; at range the steps are longer.
	NextStrafeTime = Now + (bClose ? FMath::FRandRange(0.22f, 0.55f) : FMath::FRandRange(0.55f, 1.3f));
	const FVector ToT = (T->GetActorLocation() - MyLoc).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ToT);

	// Close the gap with a sprint into a slide: a hurt or reloading target gets pushed far more often.
	const ACampusWeapon* TargetWeapon = T->GetCurrentWeapon();
	const bool bWeak = T->GetHealth01() < 0.4f || (TargetWeapon && TargetWeapon->IsReloading());
	const float PushChance = bWeak ? 0.3f + 0.45f * Aggression : 0.05f + 0.12f * Aggression;
	if (Dist > 450.f && Dist < 2000.f && !bEmpty && Now >= NextManeuverTime && FMath::FRand() < PushChance)
	{
		NextManeuverTime = Now + FMath::FRandRange(3.5f, 8.f);
		Me->BotSetCrouch(false);
		Me->BotSetAim(false);
		Me->BotSetSprint(true);
		MoveToPoint(MyLoc + ToT * FMath::Min(650.f, Dist - 250.f) + Right * FMath::FRandRange(-180.f, 180.f), true, 40.f);
		Me->BotSlide(0.3f);
		SlideReleaseTime = Now + 1.2f;
		EngageAnchorTime = Now;
		return;
	}

	// A duel that has gone on for a while without either dropping is closed down: flank in and finish it.
	if (!bEmpty && !bHurt && Now - EngageStartTime > FMath::Lerp(7.f, 3.5f, Aggression) && Now >= NextPushTime && TryPush(Now, T))
	{
		EngageStartTime = Now;
		return;
	}

	// Never hold one angle for long: after a few seconds on a spot, take a new one that still sees the target.
	if (FVector::Dist2D(MyLoc, EngageAnchor) > 350.f)
	{
		EngageAnchor = MyLoc;
		EngageAnchorTime = Now;
	}
	if (Now - EngageAnchorTime > FMath::Lerp(4.f, 1.8f, Aggression) && TryReposition(Now, T))
	{
		EngageAnchor = MyLoc;
		EngageAnchorTime = Now;
		return;
	}

	const ACampusWeapon* MyWeapon = Me->GetCurrentWeapon();
	const FCampusWeaponDef* Def = MyWeapon ? MyWeapon->GetDef() : nullptr;
	const bool bSteadyWeapon = Def && (Def->Class == ECampusWeaponClass::Marksman || Def->Class == ECampusWeaponClass::LMG);
	FVector Point = MyLoc;
	bool bCrouch = false;
	if (Dist > Range.Y)
	{
		Point += ToT * FMath::FRandRange(300.f, 650.f) + Right * FMath::FRandRange(-260.f, 260.f);
	}
	else if (Dist < Range.X)
	{
		Point += -ToT * FMath::FRandRange(180.f, 380.f) + Right * FMath::FRandRange(-220.f, 220.f);
	}
	else
	{
		// Mostly reverse direction each step, sometimes double up to break the rhythm.
		StrafeSign = FMath::FRand() < 0.75f ? -StrafeSign : StrafeSign;
		const float Step = bClose ? FMath::FRandRange(90.f, 230.f) : FMath::FRandRange(170.f, 420.f);
		Point += Right * StrafeSign * Step + ToT * FMath::FRandRange(-60.f, 60.f);
		// Long guns drop to a crouch for the long shot but keep shuffling; good players drop-shot now and then up close.
		bCrouch = (bSteadyWeapon && Dist > 1600.f && FMath::FRand() < 0.35f) || (bClose && Skill >= 2 && FMath::FRand() < 0.12f);
	}
	Me->BotSetCrouch(bCrouch);
	MoveToPoint(Point, true, 30.f);
	if (!bCrouch && Dist < 1300.f && Now >= NextManeuverTime && FMath::FRand() < 0.05f + 0.04f * Skill)
	{
		NextManeuverTime = Now + FMath::FRandRange(2.5f, 6.f);
		Me->BotJump();
	}
}

bool ACampusBotController::TryReposition(float Now, const ACampusCharacter* T)
{
	ACampusCharacter* Me = GetCampusPawn();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Me || !T || !Nav)
	{
		return false;
	}
	const FVector MyLoc = Me->GetActorLocation();
	const FVector TargetEye = T->GetPawnViewLocation();
	const FVector ToT = (T->GetActorLocation() - MyLoc).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ToT);
	const FVector2D Range = GetPreferredRange();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotReposition), false, Me);
	Params.AddIgnoredActor(T);
	float BestScore = -FLT_MAX;
	FVector Best = FVector::ZeroVector;
	const float FirstSide = FMath::RandBool() ? 1.f : -1.f;
	for (int32 i = 0; i < 8; ++i)
	{
		// Out to either side, a little forward for the pushers, a little back for the careful.
		const float Side = (i % 2 == 0) ? FirstSide : -FirstSide;
		const FVector Center = MyLoc + Right * Side * FMath::FRandRange(350.f, 800.f) + ToT * FMath::FRandRange(-250.f, 400.f * Aggression);
		FNavLocation P;
		if (!Nav->GetRandomReachablePointInRadius(Center, 220.f, P) || FVector::Dist2D(P.Location, MyLoc) < 250.f)
		{
			continue;
		}
		// It has to see the target from standing eye height, or the move just walks into a wall.
		if (GetWorld()->LineTraceTestByChannel(P.Location + FVector(0.f, 0.f, 150.f), TargetEye, ECC_Visibility, Params))
		{
			continue;
		}
		const float DistT = FVector::Dist2D(P.Location, T->GetActorLocation());
		const float InBand = (DistT > Range.X && DistT < Range.Y) ? 400.f : 0.f;
		// A new angle on the target is worth more than the same line from a step aside.
		const float Angle = FMath::Abs(FVector::DotProduct((T->GetActorLocation() - P.Location).GetSafeNormal2D(), Right));
		const float Score = InBand + Angle * 600.f - FVector::Dist2D(P.Location, MyLoc) * 0.3f + FMath::FRandRange(0.f, 200.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = P.Location;
		}
	}
	if (BestScore <= -FLT_MAX)
	{
		return false;
	}
	Me->BotSetCrouch(false);
	// Strafing there (still facing the target); a longer move is run.
	const bool bRun = FVector::Dist2D(Best, MyLoc) > 550.f && Aggression > 0.5f;
	if (bRun)
	{
		Me->BotSetAim(false);
		Me->BotSetSprint(true);
		SlideReleaseTime = Now + 0.6f;
	}
	NextStrafeTime = Now + 0.9f;
	return MoveToPoint(Best, true, 45.f);
}

bool ACampusBotController::TryPush(float Now, const ACampusCharacter* T)
{
	ACampusCharacter* Me = GetCampusPawn();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Me || !T || !Nav)
	{
		return false;
	}
	NextPushTime = Now + FMath::FRandRange(5.f, 9.f);
	const FVector MyLoc = Me->GetActorLocation();
	const FVector TLoc = T->GetActorLocation();
	const float Dist = FVector::Dist2D(MyLoc, TLoc);
	if (Dist < 500.f)
	{
		return false;
	}
	// Come in from the side rather than straight down their sights: a point beside the target, a third of the way
	// round, at the bot's own fighting distance.
	const FVector2D Range = GetPreferredRange();
	const float Want = FMath::Clamp(Range.X * 1.3f, 350.f, Dist * 0.6f);
	const FVector FromT = (MyLoc - TLoc).GetSafeNormal2D();
	const float Side = FMath::RandBool() ? 1.f : -1.f;
	const FVector Dir = FromT.RotateAngleAxis(Side * FMath::FRandRange(35.f, 70.f), FVector::UpVector);
	FNavLocation P;
	if (!Nav->GetRandomReachablePointInRadius(TLoc + Dir * Want, 250.f, P))
	{
		return false;
	}
	Me->BotSetCrouch(false);
	Me->BotSetAim(false);
	Me->BotSetSprint(true);
	SlideReleaseTime = Now + FMath::Min(1.6f, Dist / 600.f);
	if (Dist > 900.f && FMath::FRand() < 0.35f + 0.3f * Aggression)
	{
		Me->BotSlide(FMath::FRandRange(0.4f, 0.8f));
	}
	ConsiderGrenade(Now, false);
	return MoveToPoint(P.Location, true, 60.f);
}

int32 ACampusBotController::GetLane(const FVector& Location) const
{
	const ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>();
	const ACampusSchoolBuilder* B = GM ? GM->GetBuilder() : nullptr;
	if (!B || !B->PlayableBounds.IsValid)
	{
		return 1;
	}
	const float X0 = B->PlayableBounds.Min.X;
	const float W = FMath::Max(B->PlayableBounds.GetSize().X, 1.f);
	return FMath::Clamp(FMath::FloorToInt((Location.X - X0) / W * 3.f), 0, 2);
}

void ACampusBotController::CheckIdle(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me)
	{
		return;
	}
	const ACampusWeapon* W = Me->GetCurrentWeapon();
	const bool bExcused = State == EBotState::Cover || (W && W->IsReloading()) || Now < BlindUntil || Now < FleeUntil || Me->IsThrowing();
	if (bExcused || FVector::Dist2D(Me->GetActorLocation(), IdleAnchor) > 160.f)
	{
		IdleAnchor = Me->GetActorLocation();
		IdleSince = Now;
		return;
	}
	const float Limit = FMath::Lerp(3.6f, 1.6f, Aggression);
	if (Now - IdleSince < Limit)
	{
		return;
	}
	IdleAnchor = Me->GetActorLocation();
	IdleSince = Now;
	switch (State)
	{
	case EBotState::Engage:
		if (Target.IsValid() && !TryReposition(Now, Target.Get()))
		{
			TryPush(Now, Target.Get());
		}
		break;
	case EBotState::Investigate:
		// Nothing turned up here: sweep on toward the enemy's side instead of watching a doorway.
		SetState(EBotState::Roam);
		PickRoamDestination();
		break;
	default:
		PickRoamDestination();
		break;
	}
}

void ACampusBotController::OnTeammateKilled(const FVector& VictimLocation, const FVector& KillerLocation)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me || !Me->IsAlive() || State == EBotState::Engage || State == EBotState::Cover)
	{
		return;
	}
	if (FVector::Dist(Me->GetActorLocation(), VictimLocation) > 3200.f)
	{
		return;
	}
	// Trade it: the killer is probably still looking at the body, so come at them from where the teammate fell.
	const float Now = GetWorld()->GetTimeSeconds();
	LastKnownLocation = KillerLocation + FVector(FMath::FRandRange(-200.f, 200.f), FMath::FRandRange(-200.f, 200.f), 0.f);
	LastSeenTime = Now - 1.f;
	SetState(EBotState::Investigate);
	SetLookOverride(KillerLocation + FVector(0.f, 0.f, 120.f), 1.f);
	MoveToPoint(FMath::Lerp(VictimLocation, LastKnownLocation, 0.4f), false, 150.f);
}

void ACampusBotController::ThinkInvestigate(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	const float Dist = FVector::Dist2D(Me->GetActorLocation(), LastKnownLocation);
	const bool bFar = Dist > 1100.f;
	Me->BotSetCrouch(false);
	Me->BotSetSprint(bFar);
	Me->BotSetTacSprint(bFar && Dist > 2200.f && Aggression > 0.5f);
	Me->BotSetAim(!bFar && Dist < 700.f);
	ConsiderGrenade(Now, false);

	if (GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		if (ArrivedTime < 0.f)
		{
			ArrivedTime = Now;
		}
		if (Now >= NextGlanceTime)
		{
			NextGlanceTime = Now + FMath::FRandRange(0.6f, 1.2f);
			const float Yaw = GetControlRotation().Yaw + FMath::FRandRange(-150.f, 150.f);
			SetLookOverride(Me->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * 1200.f + FVector(0.f, 0.f, 60.f), 0.9f);
		}
		if (Now - ArrivedTime > FMath::Lerp(1.4f, 0.6f, Aggression))
		{
			Target = nullptr;
			SetState(EBotState::Roam);
			PickRoamDestination();
			return;
		}
	}
	if (Now - LastSeenTime > 10.f)
	{
		Target = nullptr;
		SetState(EBotState::Roam);
	}
}

void ACampusBotController::ThinkCover(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	ACampusWeapon* Weapon = Me->GetCurrentWeapon();
	Me->BotSetSprint(false);
	const bool bInCover = GetMoveStatus() == EPathFollowingStatus::Idle;
	if (bInCover)
	{
		Me->BotSetCrouch(true);
	}
	const bool bInSight = Now - LastSeenTime < 0.3f && Target.IsValid();
	const int32 Mag = Weapon ? Weapon->GetAmmoInMag() : 0;
	// Top up only when it is worth it and nobody is looking: a reload restarted every moment kept bots pinned in
	// cover for good.
	if (Weapon && !bInSight && Mag < Weapon->GetStats().MagSize * 0.6f && Weapon->GetAmmoReserve() > 0 && !Weapon->IsReloading())
	{
		Me->BotReload();
	}
	const bool bReloading = Weapon && Weapon->IsReloading();
	// Cover is a breather, not a camp: someone in view gets fought (from wherever the bot is) unless it is nearly dead
	// or has nothing in the magazine, and it comes back out as soon as it has reloaded and caught a little breath.
	if (bInSight && Mag > 0 && Me->GetHealth01() > 0.3f)
	{
		Me->BotSetCrouch(false);
		SetState(EBotState::Engage);
		EngageStartTime = Now;
		EngageAnchorTime = Now;
		return;
	}
	const bool bHealed = Me->GetHealth01() > 0.6f;
	const bool bOverstayed = Now > CoverUntil + 2.5f;
	if ((!bReloading || bOverstayed) && Now > CoverUntil && (bHealed || Now > CoverUntil + 0.8f))
	{
		// Re-peek toward the last contact, but not from where they last saw us: come out to one side.
		Me->BotSetCrouch(false);
		SetState(EBotState::Investigate);
		const FVector Away = (LastKnownLocation - Me->GetActorLocation()).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Away) * (FMath::RandBool() ? 1.f : -1.f);
		MoveToPoint(LastKnownLocation + Side * FMath::FRandRange(250.f, 450.f), true, 150.f);
	}
}

void ACampusBotController::UpdateStuck(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me || Now - StuckCheckTime < 2.2f)
	{
		return;
	}
	const bool bWantsToMove = GetMoveStatus() == EPathFollowingStatus::Moving && State != EBotState::Engage;
	const float Moved = FVector::Dist2D(Me->GetActorLocation(), StuckCheckLocation);
	StuckCheckLocation = Me->GetActorLocation();
	StuckCheckTime = Now;
	if (!bWantsToMove || Moved > 80.f)
	{
		StuckCount = 0;
		return;
	}
	if (++StuckCount >= 2)
	{
		StuckCount = 0;
		Me->BotJump();
		StopMovement();
		if (State == EBotState::Roam)
		{
			PickRoamDestination();
		}
		else
		{
			MoveToPoint(Me->GetActorLocation() + FMath::VRand().GetSafeNormal2D() * 400.f, false, 60.f);
		}
	}
}

// ------------------------------------------------------------------ grenades

void ACampusBotController::OnFlashbanged(float Blind, const FVector& From)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me || Blind < 0.12f)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	BlindUntil = FMath::Max(BlindUntil, Now + 0.8f + 4.f * Blind);
	Me->BotSetFire(false);
	bTriggerDown = false;
	NextStrafeTime = Now;
	// Flinches away from the light.
	const FVector Away = (Me->GetActorLocation() - From).GetSafeNormal2D();
	SetLookOverride(Me->GetPawnViewLocation() + (Away + FMath::VRand() * 0.6f).GetSafeNormal() * 600.f - FVector(0.f, 0.f, 150.f), BlindUntil - Now);
}

bool ACampusBotController::FleeGrenades(float Now)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me)
	{
		return false;
	}
	if (Now < FleeUntil)
	{
		return true;
	}
	TArray<ACampusThrowable*> Frags;
	ACampusThrowable::GetLiveFrags(GetWorld(), Frags);
	const FVector MyLoc = Me->GetActorLocation();
	for (const ACampusThrowable* G : Frags)
	{
		const FVector ToMe = MyLoc - G->GetActorLocation();
		// Noticed when it is close and visible, or heard clattering in right beside.
		if (ToMe.Size2D() > 560.f || G->GetFuseLeft() > 3.2f || (ToMe.Size2D() > 250.f && !CanSeePoint(G->GetActorLocation())))
		{
			continue;
		}
		FVector Away = ToMe.GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FMath::VRand().GetSafeNormal2D();
		}
		Me->BotSetFire(false);
		bTriggerDown = false;
		Me->BotSetAim(false);
		Me->BotSetCrouch(false);
		Me->BotSetSprint(true);
		MoveToPoint(MyLoc + (Away + FVector::CrossProduct(FVector::UpVector, Away) * FMath::FRandRange(-0.5f, 0.5f)).GetSafeNormal() * 800.f, false, 80.f);
		FleeUntil = Now + FMath::Min(G->GetFuseLeft() + 0.4f, 2.5f);
		return true;
	}
	return false;
}

bool ACampusBotController::CanSeePoint(const FVector& Point) const
{
	const ACampusCharacter* Me = GetCampusPawn();
	if (!Me)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotSeePoint), false, Me);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Me->GetPawnViewLocation(), Point + FVector(0.f, 0.f, 8.f), ECC_Visibility, Params);
}

void ACampusBotController::ConsiderGrenade(float Now, bool bRetreating)
{
	ACampusCharacter* Me = GetCampusPawn();
	if (!Me || Now < NextGrenadeTime || Me->IsSwitchingWeapons())
	{
		return;
	}
	const FVector MyLoc = Me->GetActorLocation();
	const float Dist = FVector::Dist(MyLoc, LastKnownLocation);
	const float SinceSeen = Now - LastSeenTime;
	// Never onto teammates.
	auto TeammateNear = [&](const FVector& Point)
	{
		for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != Me && It->IsAlive() && !Me->IsEnemyOf(*It) && FVector::Dist(It->GetActorLocation(), Point) < 520.f)
			{
				return true;
			}
		}
		return false;
	};
	NextGrenadeTime = Now + FMath::FRandRange(2.f, 4.f);
	if (bRetreating)
	{
		// Pulling back hurt: a smoke between them and the threat.
		if (Me->GetThrowableCount(ECampusThrowable::Smoke) > 0 && Dist > 700.f && FMath::FRand() < 0.35f)
		{
			const FVector Screen = MyLoc + (LastKnownLocation - MyLoc).GetSafeNormal2D() * 420.f;
			if (Me->BotThrow(ECampusThrowable::Smoke, Screen))
			{
				NextGrenadeTime = Now + FMath::FRandRange(14.f, 26.f);
			}
		}
		return;
	}
	// The enemy went to ground a moment ago somewhere it can be reached with a lob.
	if (SinceSeen < 1.f || SinceSeen > 7.f || Dist < 650.f || Dist > 2300.f || TeammateNear(LastKnownLocation))
	{
		return;
	}
	const float Chance = 0.25f + 0.1f * Skill + 0.2f * Aggression;
	if (FMath::FRand() > Chance)
	{
		NextGrenadeTime = Now + FMath::FRandRange(4.f, 8.f);
		return;
	}
	const FVector Aim = LastKnownLocation + FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(0.f, 120.f);
	ECampusThrowable Pick = ECampusThrowable::Frag;
	if (Me->GetThrowableCount(ECampusThrowable::Flash) > 0 && (Dist < 1500.f && FMath::FRand() < 0.4f))
	{
		Pick = ECampusThrowable::Flash;
	}
	else if (Me->GetThrowableCount(ECampusThrowable::Frag) == 0)
	{
		Pick = Me->GetThrowableCount(ECampusThrowable::Thermite) > 0 ? ECampusThrowable::Thermite : ECampusThrowable::Flash;
	}
	if (Me->BotThrow(Pick, Aim))
	{
		NextGrenadeTime = Now + FMath::FRandRange(12.f, 24.f) / (0.75f + 0.25f * Skill);
		SetLookOverride(Aim + FVector(0.f, 0.f, 100.f), 0.6f);
		if (Pick == ECampusThrowable::Flash)
		{
			// Push in behind the flash.
			MoveToPoint(LastKnownLocation, true, 150.f);
		}
	}
}

// ------------------------------------------------------------------ aim and fire

void ACampusBotController::UpdateAimError(float DeltaSeconds)
{
	const ACampusCharacter* Me = GetCampusPawn();
	const ACampusCharacter* T = Target.Get();
	if (!Me || !T)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextErrorTime)
	{
		NextErrorTime = Now + FMath::FRandRange(0.18f, 0.32f);
		const float Tracking = Now - TargetAcquiredTime;
		float Degrees = AimErrorDegrees * (0.35f + 0.65f * FMath::Exp(-Tracking / 0.9f)) * (T->HasPerk(ECampusPerk::ColdBlooded) ? 1.4f : 1.f);
		if (T->GetVelocity().Size2D() > 300.f)
		{
			Degrees *= 1.3f;
		}
		if (T->IsSliding() || T->GetCharacterMovement()->IsFalling())
		{
			Degrees *= 1.35f;
		}
		if (Me->GetVelocity().Size2D() > 200.f)
		{
			Degrees *= 1.2f;
		}		const float Dist = FVector::Dist(T->GetActorLocation(), Me->GetActorLocation());
		AimErrorTarget = FMath::VRand() * Dist * FMath::Tan(FMath::DegreesToRadians(Degrees));
	}
	AimError = FMath::VInterpTo(AimError, AimErrorTarget, DeltaSeconds, 5.f);
}

FVector ACampusBotController::GetAimPoint() const
{
	const ACampusCharacter* T = Target.Get();
	if (!T)
	{
		return LastKnownLocation + FVector(0.f, 0.f, 60.f);
	}
	const FVector Base = T->GetMesh()->GetBoneLocation(bAimHead ? TEXT("head") : TEXT("spine_03"));
	return Base + AimError;
}

FVector2D ACampusBotController::GetPreferredRange() const
{
	const ACampusCharacter* Me = GetCampusPawn();
	const ACampusWeapon* W = Me ? Me->GetCurrentWeapon() : nullptr;
	const FCampusWeaponDef* Def = W ? W->GetDef() : nullptr;
	switch (Def ? Def->Class : ECampusWeaponClass::AssaultRifle)
	{
	case ECampusWeaponClass::SMG: return FVector2D(250.f, 1300.f);
	case ECampusWeaponClass::Marksman: return FVector2D(1200.f, 4200.f);
	case ECampusWeaponClass::Pistol: return FVector2D(200.f, 1000.f);
	case ECampusWeaponClass::Shotgun: return FVector2D(120.f, 650.f);
	case ECampusWeaponClass::LMG: return FVector2D(800.f, 3400.f);
	default: return FVector2D(500.f, 2600.f);
	}
}

void ACampusBotController::UpdateCombat(float DeltaSeconds)
{
	ACampusCharacter* Me = GetCampusPawn();
	ACampusWeapon* Weapon = Me ? Me->GetCurrentWeapon() : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();
	const ACampusCharacter* T = Target.Get();

	const bool bCombatState = State == EBotState::Engage || State == EBotState::Cover;
	const bool bCanEngage = bCombatState && T && T->IsAlive() && Now - LastSeenTime < 0.35f && Now - TargetAcquiredTime > ReactionTime && SlideReleaseTime <= 0.f;
	if (!Me || !Weapon || !bCanEngage)
	{
		if (Me && bTriggerDown)
		{
			Me->BotSetFire(false);
			bTriggerDown = false;
		}
		return;
	}

	const FVector Eye = Me->GetPawnViewLocation();
	const FVector AimPoint = GetAimPoint();
	const FVector ToAim = AimPoint - Eye;
	const float Dist = ToAim.Size();
	const float AngleErr = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(GetControlRotation().Vector(), ToAim.GetSafeNormal()), -1.f, 1.f)));
	const float Tolerance = FMath::Max(2.5f, FMath::RadiansToDegrees(FMath::Atan(70.f / FMath::Max(Dist, 1.f))));
	const bool bOnTarget = AngleErr < Tolerance && IsLineOfFireClear(Eye, AimPoint);

	if (Now >= NextBurstTime && bOnTarget)
	{
		const float Length = FMath::FRandRange(0.35f, 0.9f) * (Dist > 2500.f ? 0.6f : 1.f);
		BurstEndTime = Now + Length;
		NextBurstTime = BurstEndTime + FMath::FRandRange(0.15f, 0.45f);
	}
	const bool bWantFire = Now < BurstEndTime && bOnTarget;

	if (Weapon->GetFireMode() != ECampusFireMode::Auto)
	{
		if (bTriggerDown && Now >= SemiReleaseTime)
		{
			Me->BotSetFire(false);
			bTriggerDown = false;
			SemiReleaseTime = Now + Weapon->GetStats().GetShotInterval() * 1.2f + FMath::FRandRange(0.05f, 0.25f);
			return;
		}
		if (!bTriggerDown && bWantFire && Now >= SemiReleaseTime)
		{
			Me->BotSetFire(true);
			bTriggerDown = true;
			SemiReleaseTime = Now + 0.05f;
		}
		return;
	}

	if (bWantFire != bTriggerDown)
	{
		Me->BotSetFire(bWantFire);
		bTriggerDown = bWantFire;
	}
}

void ACampusBotController::SetLookOverride(const FVector& Point, float Duration)
{
	LookOverride = Point;
	LookOverrideUntil = GetWorld()->GetTimeSeconds() + Duration;
}

void ACampusBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	const FRotator Current = GetControlRotation();
	FRotator Desired = Current;
	const FVector Eye = MyPawn->GetPawnViewLocation();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bTargetFresh = Target.IsValid() && Now - LastSeenTime < 0.6f;
	const bool bSprinting = SlideReleaseTime > 0.f;

	if ((State == EBotState::Engage || State == EBotState::Cover) && bTargetFresh && !bSprinting)
	{
		Desired = (GetAimPoint() - Eye).Rotation();
	}
	else if (Now < LookOverrideUntil)
	{
		Desired = (LookOverride - Eye).Rotation();
	}
	else if (State == EBotState::Investigate || State == EBotState::Cover || (State == EBotState::Engage && !bSprinting))
	{
		Desired = (LastKnownLocation + FVector(0.f, 0.f, 100.f) - Eye).Rotation();
	}
	else
	{
		const FVector Vel = MyPawn->GetVelocity();
		if (Vel.SizeSquared2D() > 100.f)
		{
			Desired = FRotator(0.f, Vel.Rotation().Yaw, 0.f);
		}
	}

	const FRotator Delta = (Desired - Current).GetNormalized();
	const float MaxStep = TurnSpeed * DeltaTime;
	const float Blend = CampusMath::ExpAlpha(12.f, DeltaTime);
	FRotator Step(FMath::Clamp(Delta.Pitch * Blend, -MaxStep, MaxStep), FMath::Clamp(Delta.Yaw * Blend, -MaxStep, MaxStep), 0.f);
	FRotator NewRotation = (Current + Step).GetNormalized();
	NewRotation.Pitch = FMath::ClampAngle(NewRotation.Pitch, -80.f, 80.f);
	NewRotation.Roll = 0.f;
	SetControlRotation(NewRotation);
	if (bUpdatePawn)
	{
		MyPawn->FaceRotation(NewRotation, DeltaTime);
	}
}

// ------------------------------------------------------------------ navigation

void ACampusBotController::PickRoamDestination()
{
	const ACampusCharacter* Me = GetCampusPawn();
	ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>();
	if (!Me || !GM)
	{
		return;
	}
	const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>();
	const ECampusTeam Team = GetTeam();
	const FVector MyLoc = Me->GetActorLocation();
	FVector Goal = FVector::ZeroVector;

	if (GS && GS->bOvertime)
	{
		Goal = GS->FlagLocation;
	}
	else
	{
		const TArray<FVector>& Spots = GM->GetBotHotspots();
		if (Spots.Num() == 0)
		{
			return;
		}
		// Spread out: teammates' current goals are worth less.
		TArray<FVector> Claimed;
		const ACampusCharacter* Leader = nullptr;
		for (TActorIterator<ACampusBotController> It(GetWorld()); It; ++It)
		{
			if (*It != this && It->GetTeam() == Team && !It->RoamGoal.IsZero())
			{
				Claimed.Add(It->RoamGoal);
			}
		}
		for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsAlive() && It->IsPlayerControlled() && It->GetTeam() == Team)
			{
				Leader = *It;
				break;
			}
		}
		FVector Intel;
		const bool bIntel = GM->GetTeamIntel(Team, Intel, 14.f);
		const float Fwd = TeamForwardSign(Team);
		// Bots of the human's team each lean toward supporting the player or holding a lane.
		const bool bSupport = Leader && (GetUniqueID() % 3 == 0);

		float BestScore = -FLT_MAX;
		const bool bGunfight = GM->GetMatchMode() == ECampusMatchMode::Gunfight;
		// Now and then a bot rotates to another lane (a flank, or to back up a fight it heard about).
		if (!bGunfight && FMath::FRand() < 0.18f + 0.12f * Aggression)
		{
			PreferredLane = bIntel ? GetLane(Intel) : (PreferredLane + (FMath::RandBool() ? 1 : 2)) % 3;
		}
		for (int32 Attempt = 0; Attempt < 9; ++Attempt)
		{
			const FVector& Candidate = Spots[FMath::RandRange(0, Spots.Num() - 1)];
			const float Travel = FVector::Dist2D(Candidate, MyLoc);
			float Score = FMath::FRandRange(0.f, 1400.f);
			if (Travel < 700.f)
			{
				Score -= 1800.f;
			}
			Score -= FMath::Max(0.f, Travel - 5000.f) * 0.4f;
			if (!bGunfight)
			{
				const float Push = Candidate.Y * Fwd;
				// Move up toward the middle of the map; the enemy spawn itself is not worth the walk.
				Score += FMath::Clamp(Push, -4000.f, 2600.f) * (0.25f + 0.35f * Aggression);
				if (Push > 3800.f)
				{
					Score -= 2500.f;
				}
			}
			for (const FVector& C : Claimed)
			{
				if (FVector::Dist2D(C, Candidate) < 800.f)
				{
					Score -= 900.f;
				}
			}
			if (!bGunfight && GetLane(Candidate) == PreferredLane)
			{
				Score += 1100.f;
			}
			if (bIntel)
			{
				Score += FMath::Max(0.f, 3200.f - FVector::Dist2D(Candidate, Intel)) * (0.5f + Aggression);
			}
			if (bSupport)
			{
				Score += FMath::Max(0.f, 2600.f - FVector::Dist2D(Candidate, Leader->GetActorLocation())) * 0.7f;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				Goal = Candidate;
			}
		}
		Goal += FVector(FMath::FRandRange(-250.f, 250.f), FMath::FRandRange(-250.f, 250.f), 0.f);
	}
	RoamRepathTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(14.f, 24.f);
	if (!MoveToPoint(Goal, false, 110.f))
	{
		// Unreachable goal (or no navmesh yet): try again soon instead of freezing.
		RoamRepathTime = GetWorld()->GetTimeSeconds() + 1.f;
	}
	RoamGoal = Goal;
}

bool ACampusBotController::FindCoverFrom(const FVector& Threat, FVector& OutCover) const
{
	const ACampusCharacter* Me = GetCampusPawn();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Me || !Nav)
	{
		return false;
	}
	const FVector MyLoc = Me->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotCover), false, Me);
	float BestScore = -FLT_MAX;
	bool bFound = false;
	for (int32 i = 0; i < 16; ++i)
	{
		FNavLocation P;
		if (!Nav->GetRandomReachablePointInRadius(MyLoc, 950.f, P))
		{
			continue;
		}
		const float DistThreat = FVector::Dist2D(P.Location, Threat);
		if (DistThreat < 450.f)
		{
			continue;
		}
		// The navmesh sits on the floor: chest ~1.2 m, standing head ~1.65 m.
		if (!GetWorld()->LineTraceTestByChannel(Threat, P.Location + FVector(0.f, 0.f, 120.f), ECC_Visibility, Params))
		{
			continue;
		}
		const bool bFull = GetWorld()->LineTraceTestByChannel(Threat, P.Location + FVector(0.f, 0.f, 165.f), ECC_Visibility, Params);
		const float Score = -FVector::Dist2D(P.Location, MyLoc) + (bFull ? 450.f : 0.f) + FMath::Min(DistThreat, 2500.f) * 0.15f;
		if (Score > BestScore)
		{
			BestScore = Score;
			OutCover = P.Location;
			bFound = true;
		}
	}
	return bFound;
}

bool ACampusBotController::MoveToPoint(const FVector& Point, bool bStrafe, float Acceptance)
{
	FVector Dest = Point;
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Projected;
		if (Nav->ProjectPointToNavigation(Point, Projected, FVector(300.f, 300.f, 400.f)))
		{
			Dest = Projected.Location;
		}
	}
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Dest, Acceptance, true, true, true, bStrafe);
	return Result != EPathFollowingRequestResult::Failed;
}

// ------------------------------------------------------------------ events

void ACampusBotController::ShareIntel(const FVector& Location) const
{
	const ACampusCharacter* Me = GetCampusPawn();
	const ECampusTeam Team = GetTeam();
	if (ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>())
	{
		GM->ReportTeamIntel(Team, Location);
	}
	if (!Me)
	{
		return;
	}
	for (TActorIterator<ACampusBotController> It(GetWorld()); It; ++It)
	{
		ACampusBotController* Mate = *It;
		const APawn* MatePawn = Mate->GetPawn();
		if (Mate == this || !MatePawn || Mate->GetTeam() != Team)
		{
			continue;
		}
		if (FVector::Dist(MatePawn->GetActorLocation(), Me->GetActorLocation()) < 3800.f && FMath::FRand() < 0.6f)
		{
			Mate->ReceiveIntel(Location);
		}
	}
}

void ACampusBotController::ReceiveIntel(const FVector& Location)
{
	if (State != EBotState::Roam)
	{
		return;
	}
	LastKnownLocation = Location + FVector(FMath::FRandRange(-250.f, 250.f), FMath::FRandRange(-250.f, 250.f), 0.f);
	LastSeenTime = GetWorld()->GetTimeSeconds() - 2.f;
	SetState(EBotState::Investigate);
	MoveToPoint(LastKnownLocation, false, 160.f);
}

void ACampusBotController::OnDamagedBy(AController* Attacker)
{
	const APawn* AttackerPawn = Attacker ? Attacker->GetPawn() : nullptr;
	if (!AttackerPawn)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (State != EBotState::Engage)
	{
		// Snap toward the shooter: contact is made far faster than a cold spot.
		LastKnownLocation = AttackerPawn->GetActorLocation();
		LastSeenTime = Now - 1.f;
		SetLookOverride(AttackerPawn->GetPawnViewLocation(), 1.2f);
		if (State == EBotState::Roam)
		{
			SetState(EBotState::Investigate);
			StopMovement();
		}
		return;
	}
	// Hit mid-fight: break the other's aim at once with a step, and now and then a jump or a drop.
	NextStrafeTime = 0.f;
	ACampusCharacter* Me = GetCampusPawn();
	if (Me && Now >= NextManeuverTime && FMath::FRand() < 0.18f + 0.08f * Skill)
	{
		NextManeuverTime = Now + FMath::FRandRange(2.f, 4.5f);
		if (Me->IsSprinting())
		{
			Me->BotSlide(0.02f);
			SlideReleaseTime = Now + 0.7f;
		}
		else if (FMath::RandBool())
		{
			Me->BotJump();
		}
		else
		{
			Me->BotSetCrouch(true);
		}
	}
}

void ACampusBotController::HearFootsteps(float Now)
{
	const ACampusCharacter* Me = GetCampusPawn();
	if (!Me || Now < NextFootstepCheckTime)
	{
		return;
	}
	NextFootstepCheckTime = Now + 0.5f;
	const FVector MyLoc = Me->GetActorLocation();
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		const ACampusCharacter* Other = *It;
		if (!Other->IsAlive() || !Me->IsEnemyOf(Other) || Other->HasPerk(ECampusPerk::Ninja) || Other->bIsCrouched)
		{
			continue;
		}
		const UCharacterMovementComponent* Move = Other->GetCharacterMovement();
		const float Speed = Other->GetVelocity().Size2D();
		if (!Move || !Move->IsMovingOnGround() || Speed < 300.f)
		{
			continue;
		}
		// Sprinting boots carry about twice as far as a run; floors and walls muffle it somewhat.
		const float Range = (Other->IsSprinting() ? 1300.f : 650.f) * (FMath::Abs(Other->GetActorLocation().Z - MyLoc.Z) > 250.f ? 0.5f : 1.f);
		if (FVector::DistSquared(Other->GetActorLocation(), MyLoc) > FMath::Square(Range))
		{
			continue;
		}
		LastKnownLocation = Other->GetActorLocation() + FVector(FMath::FRandRange(-150.f, 150.f), FMath::FRandRange(-150.f, 150.f), 0.f);
		LastSeenTime = Now - 1.5f;
		SetLookOverride(LastKnownLocation + FVector(0.f, 0.f, 120.f), 0.9f);
		if (State == EBotState::Roam)
		{
			SetState(EBotState::Investigate);
			MoveToPoint(LastKnownLocation, false, 150.f);
		}
		return;
	}
}

void ACampusBotController::HearGunshot(ACampusCharacter* Shooter, const FVector& Location, float Radius)
{
	const ACampusCharacter* Me = GetCampusPawn();
	if (!Me || !Shooter || Shooter == Me || !Me->IsEnemyOf(Shooter) || State == EBotState::Engage || State == EBotState::Cover)
	{
		return;
	}
	if (FVector::Dist(Me->GetActorLocation(), Location) > Radius)
	{
		return;
	}
	LastKnownLocation = Location + FVector(FMath::FRandRange(-300.f, 300.f), FMath::FRandRange(-300.f, 300.f), 0.f);
	LastSeenTime = GetWorld()->GetTimeSeconds() - 1.5f;
	SetLookOverride(Location + FVector(0.f, 0.f, 120.f), 0.8f);
	if (State == EBotState::Roam)
	{
		SetState(EBotState::Investigate);
		MoveToPoint(LastKnownLocation, false, 150.f);
	}
}

FString ACampusBotController::GetDebugString() const
{
	const ACampusCharacter* Me = GetCampusPawn();
	const UPathFollowingComponent* PF = GetPathFollowingComponent();
	const FNavPathSharedPtr Path = PF ? PF->GetPath() : nullptr;
	const ACampusWeapon* W = Me ? Me->GetCurrentWeapon() : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();
	return FString::Printf(TEXT("%s team=%d state=%s move=%d pathPts=%d loc=%s vel=%.0f goal=%s target=%s hp=%.2f mag=%d res=%d reload=%d cover=%.1f seen=%.1f lane=%d alive=%d"),
		*GetNameSafe(Me), (int32)GetTeam(), BotStateLabel((int32)State), (int32)GetMoveStatus(), Path.IsValid() ? Path->GetPathPoints().Num() : 0,
		Me ? *Me->GetActorLocation().ToCompactString() : TEXT("-"), Me ? Me->GetVelocity().Size2D() : 0.f, *RoamGoal.ToCompactString(),
		*GetNameSafe(Target.Get()), Me ? Me->GetHealth01() : 0.f, W ? W->GetAmmoInMag() : -1, W ? W->GetAmmoReserve() : -1,
		W && W->IsReloading() ? 1 : 0, CoverUntil - Now, Now - LastSeenTime, PreferredLane, Me && Me->IsAlive() ? 1 : 0);
}
