#include "Player/CampusCharacter.h"
#include "Player/CampusMovementComponent.h"
#include "Player/CampusViewmodelComponent.h"
#include "Player/CampusFPAnimInstance.h"
#include "Player/CampusTPAnimInstance.h"
#include "Player/CampusInputConfig.h"
#include "Player/CampusPlayerController.h"
#include "Player/CampusBreathComponent.h"
#include "Game/CampusPlayerState.h"
#include "Game/CampusGameModeBase.h"
#include "Game/CampusGameState.h"
#include "Game/CampusEnvironment.h"
#include "Player/CampusWetness.h"
#include "Weapons/CampusWeapon.h"
#include "Weapons/CampusThrowable.h"
#include "AI/CampusBotController.h"
#include "Map/CampusDoor.h"
#include "Map/CampusBreakableGlass.h"
#include "Map/CampusWater.h"
#include "Weapons/CampusFX.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "CampusAssets.h"
#include "CampusArmory.h"
#include "CampusPerks.h"
#include "CampusSpring.h"
#include "CampusOps.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"

ACampusCharacter::ACampusCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCampusMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	BaseEyeHeight = 70.f;
	CrouchedEyeHeight = 38.f;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(34.f, 92.f);
	Capsule->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Ignore);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(Capsule);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(90.f);
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->bEnableFirstPersonScale = true;
	FirstPersonCamera->FirstPersonFieldOfView = 74.f;
	FirstPersonCamera->FirstPersonScale = 0.6f;

	ViewmodelPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewmodelPivot"));
	ViewmodelPivot->SetupAttachment(FirstPersonCamera);

	ArmsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ArmsMesh"));
	ArmsMesh->SetupAttachment(ViewmodelPivot);
	ArmsMesh->SetOnlyOwnerSee(true);
	ArmsMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	ArmsMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	ArmsMesh->SetCastShadow(false);
	ArmsMesh->bReceivesDecals = false;
	ArmsMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	ArmsMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	ArmsMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	ArmsMesh->SetAnimInstanceClass(UCampusFPAnimInstance::StaticClass());

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -92.f), FRotator(0.f, -90.f, 0.f));
	Body->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	Body->bCastHiddenShadow = true;
	// Physics is enabled only so hit reactions can blend simulated bodies; every object channel is ignored, so nothing collides.
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_Pawn);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->bReceivesDecals = false;

	Viewmodel = CreateDefaultSubobject<UCampusViewmodelComponent>(TEXT("Viewmodel"));
	PhysicalAnimation = CreateDefaultSubobject<UPhysicalAnimationComponent>(TEXT("PhysicalAnimation"));
	BreathComp = CreateDefaultSubobject<UCampusBreathComponent>(TEXT("Breath"));

	bReplicates = true;
}

void ACampusCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusCharacter, Health);
	DOREPLIFETIME(ACampusCharacter, bHealthRegen);
	DOREPLIFETIME(ACampusCharacter, bIsDead);
	DOREPLIFETIME(ACampusCharacter, DeathDirection);
	DOREPLIFETIME(ACampusCharacter, DeathBone);
	DOREPLIFETIME(ACampusCharacter, KillerState);
	DOREPLIFETIME(ACampusCharacter, Weapons);
	DOREPLIFETIME_CONDITION(ACampusCharacter, CurrentWeaponIndex, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACampusCharacter, bRepAiming, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACampusCharacter, Throwables, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACampusCharacter, SelectedLethal, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACampusCharacter, SelectedTactical, COND_OwnerOnly);
	DOREPLIFETIME(ACampusCharacter, DeathImpulse);
	DOREPLIFETIME(ACampusCharacter, MaxHealth);
	DOREPLIFETIME(ACampusCharacter, Perks);
	DOREPLIFETIME(ACampusCharacter, RushUntil);
}

// ------------------------------------------------------------------ perks

ECampusPerkGroup ACampusCharacter::GetSpecialty() const
{
	return CampusPerks::GetSpecialty(Perks);
}

float ACampusCharacter::GetPerkSpeedScale() const
{
	float Scale = HasPerk(ECampusPerk::Lightweight) ? 1.07f : 1.f;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GS && GS->GetServerWorldTimeSeconds() < RushUntil)
	{
		Scale *= 1.1f;
	}
	return Scale;
}

float ACampusCharacter::GetHandlingScale() const
{
	return HasPerk(ECampusPerk::FastHands) ? 0.77f : 1.f;
}

void ACampusCharacter::OnRep_Perks()
{
	for (ACampusWeapon* W : Weapons)
	{
		if (W)
		{
			W->RefreshStats();
		}
	}
	if (IsLocalHuman() && HasSpecialty(ECampusPerkGroup::Recon))
	{
		RevealUntil = GetWorld()->GetTimeSeconds() + 3.f;
	}
}

void ACampusCharacter::OnScoredKill()
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}
	++KillsThisLife;
	if (HasPerk(ECampusPerk::QuickFix))
	{
		LastDamageTime = -100.f;
	}
	if (HasSpecialty(ECampusPerkGroup::Enforcer))
	{
		LastDamageTime = -100.f;
		if (const AGameStateBase* GS = GetWorld()->GetGameState())
		{
			RushUntil = (float)GS->GetServerWorldTimeSeconds() + 5.f;
		}
	}
	if (HasPerk(ECampusPerk::Scavenger))
	{
		for (ACampusWeapon* W : Weapons)
		{
			if (W)
			{
				W->AddReserveMagazines(1);
			}
		}
		if (KillsThisLife % 2 == 0 && Throwables.IsValidIndex((int32)SelectedLethal) && Throwables[(int32)SelectedLethal] < 9)
		{
			++Throwables[(int32)SelectedLethal];
		}
	}
}

void ACampusCharacter::UpdateEquipmentRecharge(float DeltaSeconds)
{
	if (bIsDead || !HasSpecialty(ECampusPerkGroup::Strategist))
	{
		return;
	}
	EquipmentRechargeTimer += DeltaSeconds;
	if (EquipmentRechargeTimer < 25.f)
	{
		return;
	}
	EquipmentRechargeTimer = 0.f;
	auto Refill = [this](ECampusThrowable Type, uint8 Cap)
	{
		const int32 Index = (int32)Type;
		if (Throwables.IsValidIndex(Index) && Throwables[Index] < Cap)
		{
			++Throwables[Index];
		}
	};
	Refill(SelectedLethal, StartLethals);
	Refill(SelectedTactical, StartTacticals);
}

UCampusMovementComponent* ACampusCharacter::GetCampusMovement() const
{
	return Cast<UCampusMovementComponent>(GetCharacterMovement());
}

UCampusFPAnimInstance* ACampusCharacter::GetArmsAnim() const
{
	return ArmsMesh ? Cast<UCampusFPAnimInstance>(ArmsMesh->GetAnimInstance()) : nullptr;
}

UCampusTPAnimInstance* ACampusCharacter::GetBodyAnim() const
{
	return GetMesh() ? Cast<UCampusTPAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
}

void ACampusCharacter::PlayBodyFire()
{
	UCampusTPAnimInstance* TP = GetBodyAnim();
	const ACampusWeapon* W = GetCurrentWeapon();
	if (!TP || !W || bIsDead)
	{
		return;
	}
	const FCampusWeaponDef* Def = W->GetDef();
	const bool bPistol = Def && Def->bPistolAnims;
	TP->PlayUpper(CampusAssets::Anim(bPistol ? CampusAssets::EAnim::PistolFire : CampusAssets::EAnim::RifleFire), 0.f, 0.03f, 0.18f);
}

void ACampusCharacter::PlayBodyReload(float Duration)
{
	UCampusTPAnimInstance* TP = GetBodyAnim();
	const ACampusWeapon* W = GetCurrentWeapon();
	if (!TP || !W || bIsDead)
	{
		return;
	}
	const FCampusWeaponDef* Def = W->GetDef();
	const bool bPistol = Def && Def->bPistolAnims;
	TP->PlayUpper(CampusAssets::Anim(bPistol ? CampusAssets::EAnim::PistolReload : CampusAssets::EAnim::RifleReload), Duration, 0.12f, 0.22f);
}

// ------------------------------------------------------------------ lifecycle

void ACampusCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Health = MaxHealth;
	}
	const ACampusPlayerState* PS = GetCampusPlayerState();
	ApplyOperatorMesh(PS ? PS->OperatorIndex : 0);
	UpdateFirstPersonSetup();
}

void ACampusCharacter::ApplyOperatorMesh(int32 OperatorIndex)
{
	USkeletalMesh* BodyMesh = CampusAssets::OperatorMesh(OperatorIndex);
	if (!BodyMesh)
	{
		return;
	}
	if (GetMesh()->GetSkeletalMeshAsset() != BodyMesh)
	{
		GetMesh()->SetSkeletalMesh(BodyMesh);
		for (UStaticMeshComponent* Part : GearParts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		GearParts.Reset();
	}
	if (ArmsMesh->GetSkeletalMeshAsset() != BodyMesh)
	{
		ArmsMesh->SetSkeletalMesh(BodyMesh);
		ArmsMesh->HideBoneByName(TEXT("neck_01"), PBO_None);
		ArmsMesh->HideBoneByName(TEXT("thigh_l"), PBO_None);
		ArmsMesh->HideBoneByName(TEXT("thigh_r"), PBO_None);
	}
	CampusAssets::AttachSoldierGear(GetMesh(), GearParts);
	if (UCampusFPAnimInstance* Anim = GetArmsAnim())
	{
		if (!Anim->GetBasePose())
		{
			Anim->SetBasePose(CampusAssets::Anim(CampusAssets::EAnim::RifleIdle));
		}
	}
	if (!UniformMaterial)
	{
		if (UMaterialInterface* Soldier = CampusAssets::SoldierMaterial())
		{
			UniformMaterial = UMaterialInstanceDynamic::Create(Soldier, this);
		}
	}
	if (UniformMaterial)
	{
		for (USkeletalMeshComponent* Skin : { GetMesh(), ArmsMesh.Get() })
		{
			for (int32 i = 0; i < Skin->GetNumMaterials(); ++i)
			{
				Skin->SetMaterial(i, UniformMaterial);
			}
		}
		UniformTeam = -1.f;
		UniformMaterial->SetScalarParameterValue(TEXT("ActorWetness"), UniformWetness);
		AppliedUniformWetness = UniformWetness;
		UpdateUniform();
	}
}

void ACampusCharacter::UpdateUniform()
{
	const float Team = GetTeam() == ECampusTeam::Bravo ? 1.f : 0.f;
	if (UniformMaterial && Team != UniformTeam)
	{
		UniformMaterial->SetScalarParameterValue(TEXT("Team"), Team);
		UniformTeam = Team;
		CampusAssets::ColorSoldierGear(GearParts, Team > 0.5f ? 1 : 0);
	}
}

void ACampusCharacter::UpdateUniformWetness(float DeltaSeconds)
{
	if (!UniformMaterial || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UniformWetnessElapsed += DeltaSeconds;
	if (UniformWetnessElapsed < 0.2f)
	{
		return;
	}
	const float Elapsed = UniformWetnessElapsed;
	UniformWetnessElapsed = 0.f;
	if (!WetEnvironment.IsValid())
	{
		WetEnvironment = ACampusEnvironment::Find(GetWorld());
	}
	const ACampusEnvironment* Environment = WetEnvironment.Get();
	const float Rain = Environment ? Environment->GetRainAmount() : 0.f;
	const FVector Head = GetActorLocation() + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 8.f);
	const float Exposure = Rain > 0.f && Environment->IsRainExposed(Head) ? Rain : 0.f;
	UniformWetness = CampusWetness::Advance(UniformWetness, Exposure, Elapsed);
	if (Exposure == 0.f && UniformWetness < 0.001f)
	{
		UniformWetness = 0.f;
	}
	if (!FMath::IsNearlyEqual(AppliedUniformWetness, UniformWetness, 0.0001f))
	{
		UniformMaterial->SetScalarParameterValue(TEXT("ActorWetness"), UniformWetness);
		AppliedUniformWetness = UniformWetness;
	}
}

void ACampusCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (const ACampusPlayerState* PS = GetCampusPlayerState())
	{
		ApplyOperatorMesh(PS->OperatorIndex);
	}
	UpdateFirstPersonSetup();
}

void ACampusCharacter::UnPossessed()
{
	Super::UnPossessed();
	UpdateFirstPersonSetup();
}

void ACampusCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (const ACampusPlayerState* PS = GetCampusPlayerState())
	{
		ApplyOperatorMesh(PS->OperatorIndex);
	}
	UpdateFirstPersonSetup();
}

void ACampusCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	UpdateFirstPersonSetup();
}

void ACampusCharacter::Destroyed()
{
	if (HasAuthority())
	{
		for (ACampusWeapon* W : Weapons)
		{
			if (W)
			{
				W->Destroy();
			}
		}
	}
	Super::Destroyed();
}

void ACampusCharacter::UpdateFirstPersonSetup()
{
	const bool bLocal = IsLocalHuman();
	ArmsMesh->SetVisibility(bLocal && IsAlive(), false);
	ArmsMesh->SetComponentTickEnabled(bLocal);
	if (Viewmodel)
	{
		Viewmodel->SetComponentTickEnabled(bLocal && IsAlive());
	}
	ApplyWeaponVisibility();
}

void ACampusCharacter::ApplyWeaponVisibility()
{
	for (ACampusWeapon* W : Weapons)
	{
		if (W)
		{
			W->RefreshViewerVisibility();
		}
	}
}

bool ACampusCharacter::IsLocalHuman() const
{
	return IsLocallyControlled() && IsPlayerControlled();
}

ACampusPlayerState* ACampusCharacter::GetCampusPlayerState() const
{
	return GetPlayerState<ACampusPlayerState>();
}

ECampusTeam ACampusCharacter::GetTeam() const
{
	const ACampusPlayerState* PS = GetCampusPlayerState();
	return PS ? PS->Team : ECampusTeam::None;
}

bool ACampusCharacter::IsEnemyOf(const AActor* Other) const
{
	const ACampusCharacter* OtherChar = Cast<ACampusCharacter>(Other);
	if (!OtherChar || OtherChar == this)
	{
		return false;
	}
	const ECampusTeam A = GetTeam();
	const ECampusTeam B = OtherChar->GetTeam();
	if (A == ECampusTeam::None || B == ECampusTeam::None)
	{
		return true;
	}
	return A != B;
}

FString ACampusCharacter::GetDisplayName() const
{
	const APlayerState* PS = GetPlayerState();
	return PS ? PS->GetPlayerName() : FString(TEXT("?"));
}

// ------------------------------------------------------------------ tick

void ACampusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateUniform();
	UpdateUniformWetness(DeltaSeconds);

	UCampusMovementComponent* Move = GetCampusMovement();
	if (Move && GetLocalRole() != ROLE_SimulatedProxy)
	{
		if (const ACampusWeapon* W = GetCurrentWeapon())
		{
			Move->WeaponSpeedMultiplier = W->GetStats().MoveSpeedMultiplier * GetPerkSpeedScale();
			Move->WeaponADSSpeedMultiplier = W->GetStats().ADSMoveSpeedMultiplier;
		}
	}

	if (HasAuthority())
	{
		UpdateHealthRegen(DeltaSeconds);
		UpdateEquipmentRecharge(DeltaSeconds);
		bRepAiming = Move && Move->bWantsToAim;
	}
	if (IsLocallyControlled())
	{
		if (!ScriptedMove.IsNearlyZero() && Controller && IsAlive())
		{
			const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), ScriptedMove.Y);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), ScriptedMove.X);
		}
		UpdateLocalState(DeltaSeconds);
	}
	if (IsLocalHuman() && FirstPersonCamera)
	{
		CampusAudio::UpdateEnvironment(GetWorld(), FirstPersonCamera->GetComponentLocation(), this, DeltaSeconds);
	}
	UpdateHitReaction(DeltaSeconds);
	UpdateDeathCrumple(DeltaSeconds);
	UpdateBodyGrenade();

	if (Move && Move->IsFalling())
	{
		LastFallSpeed = -GetVelocity().Z;
	}

	// Footsteps (cosmetic; Ninja silences them for the enemy viewing on this machine)
	bool bSilentToViewer = false;
	if (HasPerk(ECampusPerk::Ninja) && !IsLocallyControlled())
	{
		const APlayerController* Viewer = GetWorld()->GetFirstPlayerController();
		bSilentToViewer = !Viewer || IsEnemyOf(Viewer->GetPawn());
	}
	if (GetNetMode() != NM_DedicatedServer && IsAlive())
	{
		UpdateWading(DeltaSeconds, bSilentToViewer);
	}
	if (GetNetMode() != NM_DedicatedServer && !bSilentToViewer && IsAlive() && Move && Move->IsMovingOnGround() && !IsSliding())
	{
		const float Speed = GetVelocity().Size2D();
		if (Speed > 150.f)
		{
			FootstepAccum += Speed * DeltaSeconds;
			const float Stride = IsSprinting() ? 270.f : (bIsCrouched ? 150.f : 210.f);
			if (FootstepAccum >= Stride)
			{
				FootstepAccum = 0.f;
				const float Volume = bIsCrouched ? 0.25f : (IsSprinting() ? 0.9f : 0.55f);
				if (WadeDepth > 4.f)
				{
					// Each stride through the water: the leg shoving through it, a ring spreading from the shin, spray
					// off the knee when running.
					const FVector Shin(GetActorLocation().X, GetActorLocation().Y, WaterLine);
					CampusAudio::PlayAt(this, TEXT("Water_Wade"), Shin, IsLocalHuman() ? Volume * 0.7f : Volume * 1.2f, 0.1f, CampusAssets::EAttenuation::Foley);
					ACampusWater::AddRipple(GetWorld(), Shin + GetVelocity().GetSafeNormal2D() * 25.f, 70.f + Speed * 0.12f, 0.9f);
					CampusFX::SpawnDebris(GetWorld(), Shin + GetVelocity().GetSafeNormal2D() * 30.f, (FVector::UpVector + GetVelocity().GetSafeNormal2D() * 0.7f).GetSafeNormal(), Speed > 400.f ? 14 : 7,
						FLinearColor(0.75f, 0.83f, 0.86f), 0.f, 160.f + Speed * 0.4f, 0.55f, 0.6f, 980.f, 35.f);
				}
				else
				{
					CampusAudio::PlayAt(this, CampusAudio::FootstepSound(Move->CurrentFloor.HitResult), GetActorLocation() - FVector(0.f, 0.f, 90.f), IsLocalHuman() ? Volume * 0.5f : Volume, 0.08f, CampusAssets::EAttenuation::Foley);
				}
			}
		}
	}
}

void ACampusCharacter::UpdateWading(float DeltaSeconds, bool bSilent)
{
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	float Surface = 0.f;
	const float Depth = ACampusWater::DepthAt(GetWorld(), Feet, &Surface);
	const float Was = WadeDepth;
	WadeDepth = Depth;
	WaterLine = Surface;
	if (Depth <= 0.f)
	{
		WakeAccum = 0.f;
		return;
	}
	const FVector At(GetActorLocation().X, GetActorLocation().Y, Surface);
	const float Fall = -GetVelocity().Z;
	if (Was <= 0.f && !bSilent)
	{
		// Into the water: a jump or a drop throws up a plume, stepping off the edge a smaller slap.
		const float Hard = FMath::Clamp((Fall - 100.f) / 600.f, 0.f, 1.f);
		const float Big = 0.35f + 0.65f * FMath::Max(Hard, FMath::Clamp(GetVelocity().Size2D() / 600.f, 0.f, 1.f) * 0.5f);
		CampusAudio::PlayAt(this, Hard > 0.2f ? FName(TEXT("Water_Splash")) : FName(TEXT("Water_Wade")), At, 0.6f + 0.6f * Big, 0.08f, CampusAssets::EAttenuation::Foley);
		CampusFX::SpawnDebris(GetWorld(), At, FVector::UpVector, FMath::RoundToInt(10 + 30 * Big), FLinearColor(0.75f, 0.83f, 0.86f), 0.f, 220.f + 420.f * Big, 0.6f, 0.9f, 980.f, 40.f);
		CampusFX::SpawnSprites(GetWorld(), At + FVector(0.f, 0.f, 10.f), FVector::UpVector, 3, FLinearColor(0.88f, 0.92f, 0.94f), 0.25f * Big, 80.f, 15.f, 70.f, 0.8f, 60.f, 10.f);
		ACampusWater::AddRipple(GetWorld(), At, 120.f + 160.f * Big, 1.f);
		ACampusWater::AddRipple(GetWorld(), At, 60.f + 60.f * Big, 0.8f);
	}
	// The legs push the water aside all the time they move through it: a bow wave and a wake trailing back, a little
	// stirring even standing still.
	const float Speed2D = GetVelocity().Size2D();
	const float Push = (0.05f + Speed2D * 0.0006f) * FMath::Clamp(Depth / 30.f, 0.3f, 1.f) * FMath::Clamp(DeltaSeconds * 60.f, 0.2f, 2.f);
	ACampusWater::AddRipple(GetWorld(), At + GetVelocity().GetSafeNormal2D() * 18.f, 60.f, Push);
	WakeAccum = 0.f;
}

void ACampusCharacter::UpdateLocalState(float DeltaSeconds)
{
	UCampusMovementComponent* Move = GetCampusMovement();
	const float Now = GetWorld()->GetTimeSeconds();
	ACampusWeapon* W = GetCurrentWeapon();

	if (!IsAlive() || !Move)
	{
		ADSAlpha = FMath::FInterpConstantTo(ADSAlpha, 0.f, DeltaSeconds, 6.f);
		bFireHeld = false;
		return;
	}

	if (!bToggleSprint && IsLocalHuman())
	{
		// Hold to sprint: letting go ends it (a slide or jump in progress keeps its momentum to the end), and while
		// the key stays down the sprint picks up again once nothing holds it back (sights, trigger, crouch).
		if (!bSprintKeyHeld && Move->bWantsToSprint && !Move->IsSliding() && !Move->IsFalling() && !Move->IsMantling())
		{
			StopSprinting();
		}
		else if (bSprintKeyHeld && !Move->bWantsToSprint && !bAimHeld && !bFireHeld && !bIsCrouched && !IsThrowing() && Move->IsMovingOnGround() && Move->CanSprintInCurrentState())
		{
			Move->bWantsToSprint = true;
			Move->bWantsToTacSprint = bAutoTacSprint && !(W && W->IsReloading()) && Move->TacSprintStamina > 0.5f;
			NotMovingForwardTime = 0.f;
		}
	}
	if (Move->bWantsToSprint)
	{
		// Sprint survives jumps, slides and mantles so the pace carries through; it only drops when the stick
		// leaves forward on the ground (or for aiming / crouching).
		const bool bTransit = Move->IsFalling() || Move->IsSliding() || Move->IsMantling();
		if (!bTransit && !Move->CanSprintInCurrentState())
		{
			NotMovingForwardTime += DeltaSeconds;
			if (NotMovingForwardTime > 0.15f)
			{
				StopSprinting();
			}
		}
		else
		{
			NotMovingForwardTime = 0.f;
		}
		if (Move->bWantsToTacSprint && Move->TacSprintStamina <= 0.f)
		{
			Move->bWantsToTacSprint = false;
		}
		if (W && W->IsReloading())
		{
			if (Move->bWantsToTacSprint)
			{
				Move->bWantsToTacSprint = false;
				bTacSprintAfterReload = true;
			}
		}
		else if (bTacSprintAfterReload)
		{
			bTacSprintAfterReload = false;
			Move->bWantsToTacSprint = Move->TacSprintStamina > 0.5f;
		}
	}
	else
	{
		bTacSprintAfterReload = false;
	}
	const bool bSlidingNow = IsSliding();
	if (bWasSlidingLastFrame && !bSlidingNow && bStandAfterSlide)
	{
		bStandAfterSlide = false;
		if (!bCrouchHeldByHold)
		{
			UnCrouch();
		}
	}
	bWasSlidingLastFrame = bSlidingNow;

	const bool bSprintingNow = IsSprinting();
	if (bWasSprintingLastFrame && !bSprintingNow && W)
	{
		const float Delay = bWasTacSprintingLastFrame ? W->GetStats().TacSprintToFireTime : W->GetStats().SprintToFireTime;
		FireBlockedUntil = FMath::Max(FireBlockedUntil, Now + Delay);
	}
	bWasSprintingLastFrame = bSprintingNow;
	bWasTacSprintingLastFrame = IsTacSprinting();

	UpdateThrow();
	if (bAimHeld && ThrowPhase == EThrowPhase::Recover)
	{
		InterruptThrow(false);
	}

	Move->bWantsToAim = IsAiming();
	const float ADSTime = W ? FMath::Max(W->GetStats().ADSTime, 0.05f) : 0.25f;
	ADSAlpha = FMath::FInterpConstantTo(ADSAlpha, IsAiming() ? 1.f : 0.f, DeltaSeconds, 1.f / ADSTime);

	if (PendingWeaponIndex != INDEX_NONE && Now >= PendingSwitchTime)
	{
		const int32 NewIndex = PendingWeaponIndex;
		PendingWeaponIndex = INDEX_NONE;
		CurrentWeaponIndex = NewIndex;
		if (!HasAuthority())
		{
			ServerEquipWeapon(NewIndex);
		}
		HandleWeaponChanged(true);
	}

	UpdateRecoil(DeltaSeconds);
}

void ACampusCharacter::UpdateRecoil(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bHuman = IsPlayerControlled();

	if (!PendingRecoil.IsNearlyZero(0.0005f))
	{
		FVector2D Step = PendingRecoil * CampusMath::ExpAlpha(30.f, DeltaSeconds);
		if (PendingRecoil.Size() < 0.01f)
		{
			Step = PendingRecoil;
		}
		PendingRecoil -= Step;
		if (bHuman)
		{
			AddControllerPitchInput(Step.X);
			AddControllerYawInput(Step.Y);
		}
		else
		{
			BotRecoilOffset += FRotator(Step.X, Step.Y, 0.f);
		}
	}

	if (Now - LastRecoilTime > 0.12f && !RecoilAccum.IsNearlyZero())
	{
		const ACampusWeapon* W = GetCurrentWeapon();
		RecoilRecover += RecoilAccum * (W ? W->GetStats().RecoilRecovery : 0.35f);
		RecoilAccum = FVector2D::ZeroVector;
	}

	if (!RecoilRecover.IsNearlyZero(0.0005f))
	{
		FVector2D Step = RecoilRecover * CampusMath::ExpAlpha(9.f, DeltaSeconds);
		if (RecoilRecover.Size() < 0.01f)
		{
			Step = RecoilRecover;
		}
		RecoilRecover -= Step;
		if (bHuman)
		{
			AddControllerPitchInput(-Step.X);
			AddControllerYawInput(-Step.Y);
		}
		else
		{
			BotRecoilOffset -= FRotator(Step.X, Step.Y, 0.f);
		}
	}

	if (!bHuman)
	{
		BotRecoilOffset = FMath::RInterpTo(BotRecoilOffset, FRotator::ZeroRotator, DeltaSeconds, 3.f);
	}
}

void ACampusCharacter::AddAimRecoil(float Pitch, float Yaw)
{
	PendingRecoil += FVector2D(Pitch, Yaw);
	RecoilAccum += FVector2D(Pitch, Yaw);
	LastRecoilTime = GetWorld()->GetTimeSeconds();
}

void ACampusCharacter::ApplyAimPunch(const FVector& FromDirection, float Damage)
{
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnDamaged(FromDirection, Damage);
	}
	if (IsPlayerControlled())
	{
		const float S = FMath::Clamp(Damage / 30.f, 0.3f, 1.5f);
		PendingRecoil += FVector2D(FMath::FRandRange(0.6f, 1.4f) * S, FMath::FRandRange(-0.8f, 0.8f) * S);
	}
}

void ACampusCharacter::PlayHitReaction(FName Bone, const FVector& Direction, float Strength)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (bIsDead || IsLocalHuman() || !Body || !Body->GetPhysicsAsset() || GetNetMode() == NM_DedicatedServer || !PhysicalAnimation)
	{
		return;
	}
	static const FName Root(TEXT("spine_01"));
	if (Body->GetBoneIndex(Root) == INDEX_NONE)
	{
		return;
	}
	if (!bHitReactActive)
	{
		PhysicalAnimation->SetSkeletalMeshComponent(Body);
		FPhysicalAnimationData Drive;
		Drive.bIsLocalSimulation = true;
		Drive.OrientationStrength = 900.f;
		Drive.AngularVelocityStrength = 90.f;
		Drive.PositionStrength = 0.f;
		Drive.VelocityStrength = 0.f;
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Root, Drive, true);
		Body->SetAllBodiesBelowSimulatePhysics(Root, true, true);
		bHitReactActive = true;
	}
	HitReactWeight = FMath::Clamp(HitReactWeight + 0.38f * Strength, 0.f, 0.72f);
	Body->SetAllBodiesBelowPhysicsBlendWeight(Root, HitReactWeight, false, true);
	// lower-body hits still jolt the driven torso without applying an impulse to a kinematic leg
	const FName ImpulseBone = !Bone.IsNone() && Body->IsSimulatingPhysics(Bone) ? Bone : Root;
	if (Body->IsSimulatingPhysics(ImpulseBone))
	{
		Body->AddImpulse(Direction.GetSafeNormal() * FMath::Lerp(380.f, 900.f, FMath::Clamp(Strength, 0.f, 1.f)), ImpulseBone, true);
	}

	// Directional flinch montage under the physics jolt; heavier hits pick the bigger reactions.
	if (UCampusTPAnimInstance* TP = GetBodyAnim())
	{
		using CampusAssets::EAnim;
		EAnim Pick;
		if (FVector::DotProduct(Direction.GetSafeNormal2D(), GetActorForwardVector()) > 0.3f)
		{
			Pick = EAnim::HitBack;
		}
		else if (Strength > 0.85f)
		{
			Pick = EAnim::HitFrontHeavy;
		}
		else if (Strength > 0.55f)
		{
			Pick = FMath::RandBool() ? EAnim::HitFrontMedium1 : EAnim::HitFrontMedium2;
		}
		else
		{
			static const EAnim Light[] = { EAnim::HitFrontLight1, EAnim::HitFrontLight2, EAnim::HitFrontLight3, EAnim::HitFrontLight4 };
			Pick = Light[FMath::RandRange(0, 3)];
		}
		TP->PlayUpper(CampusAssets::Anim(Pick), 0.f, 0.04f, 0.25f);
	}
}

void ACampusCharacter::MulticastHitReact_Implementation(FName Bone, FVector_NetQuantizeNormal Direction, float Strength)
{
	PlayHitReaction(Bone, Direction, Strength);
}

void ACampusCharacter::UpdateHitReaction(float DeltaSeconds)
{
	if (!bHitReactActive)
	{
		return;
	}
	static const FName Root(TEXT("spine_01"));
	USkeletalMeshComponent* Body = GetMesh();
	if (bIsDead || !Body)
	{
		bHitReactActive = false;
		HitReactWeight = 0.f;
		return;
	}
	HitReactWeight = FMath::Max(0.f, HitReactWeight - DeltaSeconds * 1.8f);
	Body->SetAllBodiesBelowPhysicsBlendWeight(Root, HitReactWeight, false, true);
	if (HitReactWeight <= 0.f)
	{
		Body->SetAllBodiesBelowSimulatePhysics(Root, false, true);
		bHitReactActive = false;
	}
}

void ACampusCharacter::UpdateHealthRegen(float DeltaSeconds)
{
	if (!bHealthRegen || bIsDead || Health >= MaxHealth)
	{
		return;
	}
	const bool bQuickFix = HasPerk(ECampusPerk::QuickFix);
	if (GetWorld()->GetTimeSeconds() - LastDamageTime < (bQuickFix ? 2.4f : 4.0f))
	{
		return;
	}
	// Scaled to the health pool so a custom 300 HP match regenerates in the same time as a standard one.
	Health = FMath::Min(MaxHealth, Health + 0.55f * MaxHealth * (bQuickFix ? 1.4f : 1.f) * DeltaSeconds);
}

// ------------------------------------------------------------------ movement / state

bool ACampusCharacter::IsSprinting() const
{
	const UCampusMovementComponent* Move = GetCampusMovement();
	return Move && Move->IsSprinting();
}

bool ACampusCharacter::IsTacSprinting() const
{
	const UCampusMovementComponent* Move = GetCampusMovement();
	return Move && Move->IsTacSprinting();
}

bool ACampusCharacter::IsSliding() const
{
	const UCampusMovementComponent* Move = GetCampusMovement();
	return Move && Move->IsSliding();
}

bool ACampusCharacter::IsMantling() const
{
	const UCampusMovementComponent* Move = GetCampusMovement();
	return Move && Move->IsMantling();
}

bool ACampusCharacter::CanAim() const
{
	return GetAimBlockReason() == nullptr;
}

const TCHAR* ACampusCharacter::GetAimBlockReason() const
{
	const ACampusWeapon* W = GetCurrentWeapon();
	if (!IsAlive()) return TEXT("dead");
	if (!W) return TEXT("no_weapon");
	if (W->IsReloading()) return TEXT("reload");
	if (IsSwitchingWeapons()) return TEXT("switch");
	if (IsThrowing()) return TEXT("throw");
	if (IsMantling()) return TEXT("mantle");
	if (W->GetDef() && W->GetDef()->IsMelee()) return TEXT("melee_weapon");
	if (GetWorld()->GetTimeSeconds() < MeleeCooldownUntil - 0.45f) return TEXT("melee_recovery");
	return nullptr;
}

bool ACampusCharacter::CanFireNow() const
{
	// One hand is on the ledge while climbing.
	if (!IsAlive() || !GetCurrentWeapon() || IsSwitchingWeapons() || IsSprinting() || IsThrowing() || IsMantling())
	{
		return false;
	}
	if (const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>())
	{
		if (GS->Phase == ECampusMatchPhase::Countdown || GS->Phase == ECampusMatchPhase::RoundOver || GS->Phase == ECampusMatchPhase::MatchOver)
		{
			return false;
		}
	}
	return GetWorld()->GetTimeSeconds() >= FireBlockedUntil;
}

void ACampusCharacter::NotifyShotFired(bool bSuppressed)
{
	if (!bSuppressed)
	{
		LastLoudShotTime = GetWorld()->GetTimeSeconds();
	}
	if (Viewmodel)
	{
		Viewmodel->CancelInspect();
	}
}

void ACampusCharacter::StopSprinting()
{
	UCampusMovementComponent* Move = GetCampusMovement();
	if (!Move)
	{
		return;
	}
	if (Move->IsSprinting())
	{
		if (const ACampusWeapon* W = GetCurrentWeapon())
		{
			const float Delay = Move->IsTacSprinting() ? W->GetStats().TacSprintToFireTime : W->GetStats().SprintToFireTime;
			FireBlockedUntil = FMath::Max(FireBlockedUntil, GetWorld()->GetTimeSeconds() + Delay);
		}
	}
	Move->bWantsToSprint = false;
	Move->bWantsToTacSprint = false;
	NotMovingForwardTime = 0.f;
}

void ACampusCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, CrouchedEyeHeight));
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnCrouchChanged(ScaledHalfHeightAdjust + (BaseEyeHeight - CrouchedEyeHeight));
	}
}

void ACampusCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnCrouchChanged(-(ScaledHalfHeightAdjust + (BaseEyeHeight - CrouchedEyeHeight)));
	}
}

void ACampusCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnLanded(LastFallSpeed);
	}
	if (LastFallSpeed > 350.f)
	{
		CampusAudio::PlayAt(this, TEXT("Land"), GetActorLocation() - FVector(0.f, 0.f, 90.f), FMath::Clamp(LastFallSpeed / 900.f, 0.3f, 1.f), 0.08f, CampusAssets::EAttenuation::Foley);
	}
	LastFallSpeed = 0.f;
}

void ACampusCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnJumped();
	}
}

bool ACampusCharacter::CanJumpInternal_Implementation() const
{
	if (!IsAlive())
	{
		return false;
	}
	// Slide jump: the slide (a crouch) has just been cancelled and the stand-up is still pending this frame.
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (IsSliding() || (bIsCrouched && Move && !Move->bWantsToCrouch))
	{
		return JumpIsAllowedInternal();
	}
	return Super::CanJumpInternal_Implementation();
}

void ACampusCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (HasAuthority() && IsAlive())
	{
		Die(nullptr, TEXT("World"), false, FVector::UpVector, NAME_None);
	}
}

float ACampusCharacter::GetADSAlpha() const
{
	const float Wall = Viewmodel ? Viewmodel->GetWallObstructionAlpha() : 0.f;
	return ADSAlpha * (1.f - CampusMath::SmoothStep01((Wall - 0.04f) / 0.3f));
}

FVector ACampusCharacter::GetPawnViewLocation() const
{
	return FirstPersonCamera ? FirstPersonCamera->GetComponentLocation() : Super::GetPawnViewLocation();
}

FRotator ACampusCharacter::GetAimRotation() const
{
	if (Controller)
	{
		return Controller->GetControlRotation() + BotRecoilOffset;
	}
	return GetBaseAimRotation();
}

FVector ACampusCharacter::GetAimOrigin() const
{
	return GetPawnViewLocation();
}

FVector ACampusCharacter::GetAimDirection() const
{
	return GetAimRotation().Vector();
}

FVector ACampusCharacter::FirstPersonToWorldVisual(const FVector& WorldLocation) const
{
	if (!FirstPersonCamera || !FirstPersonCamera->bEnableFirstPersonFieldOfView)
	{
		return WorldLocation;
	}
	const FTransform CamT = FirstPersonCamera->GetComponentTransform();
	FVector Local = CamT.InverseTransformPositionNoScale(WorldLocation);
	const float WorldHalf = FMath::DegreesToRadians(FirstPersonCamera->FieldOfView * 0.5f);
	const float FPHalf = FMath::DegreesToRadians(FirstPersonCamera->FirstPersonFieldOfView * 0.5f);
	const float K = FMath::Tan(WorldHalf) / FMath::Max(FMath::Tan(FPHalf), 0.01f);
	Local.Y *= K;
	Local.Z *= K;
	return CamT.TransformPositionNoScale(Local);
}

// ------------------------------------------------------------------ input

void ACampusCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogCampus, Error, TEXT("CampusCharacter requires the Enhanced Input component."));
		return;
	}
	const UCampusInputConfig* Cfg = UCampusInputConfig::Get();
	Input->BindAction(Cfg->Move, ETriggerEvent::Triggered, this, &ACampusCharacter::Input_Move);
	Input->BindAction(Cfg->Look, ETriggerEvent::Triggered, this, &ACampusCharacter::Input_Look);
	Input->BindAction(Cfg->LookGamepad, ETriggerEvent::Triggered, this, &ACampusCharacter::Input_LookGamepad);
	Input->BindAction(Cfg->Jump, ETriggerEvent::Started, this, &ACampusCharacter::Input_JumpStarted);
	Input->BindAction(Cfg->Jump, ETriggerEvent::Completed, this, &ACampusCharacter::Input_JumpCompleted);
	Input->BindAction(Cfg->Sprint, ETriggerEvent::Started, this, &ACampusCharacter::Input_Sprint);
	Input->BindAction(Cfg->Sprint, ETriggerEvent::Completed, this, &ACampusCharacter::Input_SprintReleased);
	Input->BindAction(Cfg->Crouch, ETriggerEvent::Started, this, &ACampusCharacter::Input_Crouch);
	Input->BindAction(Cfg->CrouchHold, ETriggerEvent::Started, this, &ACampusCharacter::Input_CrouchHoldStarted);
	Input->BindAction(Cfg->CrouchHold, ETriggerEvent::Completed, this, &ACampusCharacter::Input_CrouchHoldCompleted);
	Input->BindAction(Cfg->Aim, ETriggerEvent::Started, this, &ACampusCharacter::Input_AimStarted);
	Input->BindAction(Cfg->Aim, ETriggerEvent::Completed, this, &ACampusCharacter::Input_AimCompleted);
	Input->BindAction(Cfg->Fire, ETriggerEvent::Started, this, &ACampusCharacter::Input_FireStarted);
	Input->BindAction(Cfg->Fire, ETriggerEvent::Completed, this, &ACampusCharacter::Input_FireCompleted);
	Input->BindAction(Cfg->Reload, ETriggerEvent::Started, this, &ACampusCharacter::Input_Reload);
	Input->BindAction(Cfg->SwitchWeapon, ETriggerEvent::Started, this, &ACampusCharacter::Input_SwitchWeapon);
	Input->BindAction(Cfg->Primary, ETriggerEvent::Started, this, &ACampusCharacter::Input_Primary);
	Input->BindAction(Cfg->Secondary, ETriggerEvent::Started, this, &ACampusCharacter::Input_Secondary);
	Input->BindAction(Cfg->FireMode, ETriggerEvent::Started, this, &ACampusCharacter::Input_FireMode);
	Input->BindAction(Cfg->Flashlight, ETriggerEvent::Started, this, &ACampusCharacter::Input_Flashlight);
	Input->BindAction(Cfg->Melee, ETriggerEvent::Started, this, &ACampusCharacter::Input_Melee);
	Input->BindAction(Cfg->Inspect, ETriggerEvent::Started, this, &ACampusCharacter::Input_Inspect);
	Input->BindAction(Cfg->Interact, ETriggerEvent::Started, this, &ACampusCharacter::Input_Interact);
	Input->BindAction(Cfg->Lethal, ETriggerEvent::Started, this, &ACampusCharacter::Input_LethalStarted);
	Input->BindAction(Cfg->Lethal, ETriggerEvent::Completed, this, &ACampusCharacter::Input_LethalCompleted);
	Input->BindAction(Cfg->Tactical, ETriggerEvent::Started, this, &ACampusCharacter::Input_TacticalStarted);
	Input->BindAction(Cfg->Tactical, ETriggerEvent::Completed, this, &ACampusCharacter::Input_TacticalCompleted);
	Input->BindAction(Cfg->CycleLethal, ETriggerEvent::Started, this, &ACampusCharacter::Input_CycleLethal);
	Input->BindAction(Cfg->CycleTactical, ETriggerEvent::Started, this, &ACampusCharacter::Input_CycleTactical);
}

// ------------------------------------------------------------------ throwables

int32 ACampusCharacter::GetThrowableCount(ECampusThrowable Type) const
{
	const int32 Index = (int32)Type;
	return Throwables.IsValidIndex(Index) ? Throwables[Index] : 0;
}

float ACampusCharacter::GetCookTime() const
{
	return (ThrowPhase == EThrowPhase::Prime && SpoonTime >= 0.f) ? GetWorld()->GetTimeSeconds() - SpoonTime : 0.f;
}

void ACampusCharacter::Input_LethalStarted() { StartThrow(SelectedLethal); }
void ACampusCharacter::Input_TacticalStarted() { StartThrow(SelectedTactical); }

void ACampusCharacter::Input_LethalCompleted()
{
	if (ThrowPhase == EThrowPhase::Prime && CampusThrowables::IsLethal(ThrowType))
	{
		bThrowHeld = false;
	}
}

void ACampusCharacter::Input_TacticalCompleted()
{
	if (ThrowPhase == EThrowPhase::Prime && !CampusThrowables::IsLethal(ThrowType))
	{
		bThrowHeld = false;
	}
}

void ACampusCharacter::Input_CycleLethal()
{
	if (IsThrowing())
	{
		return;
	}
	const ECampusThrowable Other = SelectedLethal == ECampusThrowable::Frag ? ECampusThrowable::Thermite : ECampusThrowable::Frag;
	if (GetThrowableCount(Other) <= 0)
	{
		return;
	}
	SelectedLethal = Other;
	SelectionChangeTime = GetWorld()->GetTimeSeconds();
	CampusAudio::Play2D(this, TEXT("UI_Tab"), 0.5f, 0.f, CampusAudio::ECategory::Interface);
}

void ACampusCharacter::Input_CycleTactical()
{
	if (IsThrowing())
	{
		return;
	}
	const ECampusThrowable Other = SelectedTactical == ECampusThrowable::Flash ? ECampusThrowable::Smoke : ECampusThrowable::Flash;
	if (GetThrowableCount(Other) <= 0)
	{
		return;
	}
	SelectedTactical = Other;
	SelectionChangeTime = GetWorld()->GetTimeSeconds();
	CampusAudio::Play2D(this, TEXT("UI_Tab"), 0.5f, 0.f, CampusAudio::ECategory::Interface);
}

void ACampusCharacter::StartThrow(ECampusThrowable Type)
{
	if (!IsAlive() || IsThrowing() || GetThrowableCount(Type) <= 0 || IsSwitchingWeapons())
	{
		return;
	}
	if (const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>())
	{
		if (GS->Phase == ECampusMatchPhase::Countdown || GS->Phase == ECampusMatchPhase::RoundOver || GS->Phase == ECampusMatchPhase::MatchOver)
		{
			return;
		}
	}
	InterruptActions();
	if (UCampusMovementComponent* Move = GetCampusMovement())
	{
		// The tactical sprint needs the support hand; a grenade drops it to a run.
		Move->bWantsToTacSprint = false;
	}
	// Aim stays held through the throw (CanAim is false while throwing): the sights come straight back up after it.
	bFireHeld = false;
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		W->OnTriggerReleased();
		W->CancelReload();
	}
	ThrowPhase = EThrowPhase::Prime;
	ThrowType = Type;
	ThrowPhaseStart = GetWorld()->GetTimeSeconds();
	ThrowPhaseClock = 0.f;
	SpoonTime = -1.f;
	bThrowHeld = true;
	bPinPulled = false;
	bThrowLaunched = false;
	bThrowHurried = false;
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnThrowStart(Type, 1.f / GetHandlingScale());
	}
	BroadcastThrowStage(Type, (uint8)ECampusBodyThrow::Draw);
	CampusAudio::Play2D(this, TEXT("Grenade_Draw"), 0.55f, 0.06f);
}

void ACampusCharacter::UpdateThrow()
{
	if (ThrowPhase == EThrowPhase::None)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// Phase time at the throw's playback rate; the viewmodel runs the same clock.
	const float Rate = (1.f / GetHandlingScale()) * (bThrowHurried && ThrowPhase == EThrowPhase::Recover ? 2.6f : 1.f);
	ThrowPhaseClock += GetWorld()->GetDeltaSeconds() * Rate;
	const float T = ThrowPhaseClock;
	switch (ThrowPhase)
	{
	case EThrowPhase::Prime:
	{
		if (!bPinPulled && T >= 0.26f)
		{
			bPinPulled = true;
			CampusAudio::Play2D(this, TEXT("Grenade_Pin"), 0.75f, 0.05f);
			if (Viewmodel && IsLocalHuman())
			{
				Viewmodel->OnThrowPin();
			}
		}
		// A frag is cooked: the spoon flies as soon as the pin is out and the fuse is burning in the hand.
		if (ThrowType == ECampusThrowable::Frag && SpoonTime < 0.f && T >= 0.36f)
		{
			SpoonTime = Now;
			CampusAudio::Play2D(this, TEXT("Grenade_Spoon"), 0.6f, 0.05f);
			if (Viewmodel && IsLocalHuman())
			{
				Viewmodel->OnThrowSpoon();
			}
		}
		const bool bCookedOff = ThrowType == ECampusThrowable::Frag && GetCookTime() >= CampusThrowables::Fuse(ThrowType) - 0.05f;
		if (bCookedOff || (!bThrowHeld && T >= 0.44f))
		{
			ThrowPhase = EThrowPhase::Throw;
			ThrowPhaseStart = Now;
			ThrowPhaseClock = 0.f;
			if (Viewmodel && IsLocalHuman())
			{
				Viewmodel->OnThrowRelease();
			}
			BroadcastThrowStage(ThrowType, (uint8)ECampusBodyThrow::Swing);
			CampusAudio::Play2D(this, TEXT("Grenade_Throw"), 0.6f, 0.06f);
			if (bCookedOff)
			{
				LaunchFromHand();
			}
		}
		break;
	}
	case EThrowPhase::Throw:
		// Leaves the hand at the top of the viewmodel's overhand swing (UCampusViewmodelComponent::EvaluateThrow).
		if (!bThrowLaunched && T >= 0.17f)
		{
			LaunchFromHand();
		}
		if (T >= 0.36f)
		{
			ThrowPhase = EThrowPhase::Recover;
			ThrowPhaseStart = Now;
			ThrowPhaseClock = 0.f;
		}
		break;
	default:
		if (T >= 0.38f)
		{
			ThrowPhase = EThrowPhase::None;
			FireBlockedUntil = FMath::Max(FireBlockedUntil, Now + 0.05f);
			if (Viewmodel && IsLocalHuman())
			{
				Viewmodel->OnThrowEnd();
			}
		}
		break;
	}
}

void ACampusCharacter::LaunchFromHand()
{
	if (bThrowLaunched)
	{
		return;
	}
	bThrowLaunched = true;
	const FRotator Aim = GetControlRotation();
	const FVector Eye = GetPawnViewLocation();
	// Out of the left hand at the top of the overhand swing, just left of and above the line of sight: for the local
	// player exactly where the grenade is seen leaving the fingers.
	FVector Origin = Eye + Aim.RotateVector(FVector(30.f, -10.f, 3.f));
	FVector InHand;
	if (Viewmodel && IsLocalHuman() && Viewmodel->GetHandGrenadeLocation(InHand) && FVector::DistSquared(InHand, Eye) < FMath::Square(90.f))
	{
		Origin = InHand;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusThrowOrigin), false, this);
	if (GetWorld()->SweepSingleByChannel(Hit, Eye, Origin, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(4.f), Params))
	{
		Origin = Hit.Location - (Origin - Eye).GetSafeNormal() * 2.f;
	}
	const FVector Dir = (Aim + FRotator(6.f, 0.f, 0.f)).Vector();
	const float Cooked = SpoonTime >= 0.f ? GetWorld()->GetTimeSeconds() - SpoonTime : 0.f;
	if (Viewmodel && IsLocalHuman())
	{
		if (ThrowType != ECampusThrowable::Frag)
		{
			Viewmodel->OnThrowSpoon();
		}
		Viewmodel->OnThrowLaunched();
	}
	if (HasAuthority())
	{
		ServerThrow_Implementation(ThrowType, Origin, Dir, Cooked);
	}
	else
	{
		ServerThrow(ThrowType, Origin, Dir, Cooked);
		// Shown at once; the server's count replicates back.
		if (!ACampusGameState::GetRules(GetWorld()).bInfiniteEquipment && Throwables.IsValidIndex((int32)ThrowType) && Throwables[(int32)ThrowType] > 0)
		{
			--Throwables[(int32)ThrowType];
		}
	}
}

void ACampusCharacter::ServerThrow_Implementation(ECampusThrowable Type, FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, float Cooked)
{
	const int32 Index = (int32)Type;
	if (!IsAlive() || !Throwables.IsValidIndex(Index) || Throwables[Index] == 0)
	{
		return;
	}
	const FVector Eye = GetPawnViewLocation();
	FVector From = Origin;
	if (FVector::DistSquared(From, Eye) > FMath::Square(200.f))
	{
		From = Eye;
	}
	if (!ACampusGameState::GetRules(GetWorld()).bInfiniteEquipment)
	{
		--Throwables[Index];
	}
	const float Fuse = CampusThrowables::Fuse(Type);
	const bool bCookedOff = Type == ECampusThrowable::Frag && Cooked >= Fuse - 0.06f;
	const float FuseLeft = Type == ECampusThrowable::Frag ? FMath::Max(Fuse - FMath::Clamp(Cooked, 0.f, Fuse), 0.02f) : Fuse;
	// The run carries into the throw.
	const FVector Velocity = bCookedOff ? GetVelocity() : FVector(Direction).GetSafeNormal() * CampusThrowables::ThrowSpeed(Type) + GetVelocity() * 0.6f;
	SpawnThrowable(Type, From, Velocity, FuseLeft);
	MulticastThrowFX(Type);
}

void ACampusCharacter::SpawnThrowable(ECampusThrowable Type, const FVector& Origin, const FVector& Velocity, float FuseLeft)
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACampusThrowable* G = GetWorld()->SpawnActor<ACampusThrowable>(ACampusThrowable::StaticClass(), FTransform(Velocity.Rotation(), Origin), Params))
	{
		G->Launch(Type, this, Velocity, FuseLeft);
	}
}

void ACampusCharacter::MulticastThrowFX_Implementation(ECampusThrowable Type)
{
	if (IsLocallyControlled() && IsPlayerControlled())
	{
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// The grenade has left the hand: the swing's whoosh where the body is.
	CampusAudio::PlayAt(this, TEXT("Grenade_Throw"), GetPawnViewLocation(), 0.6f, 0.06f, CampusAssets::EAttenuation::Foley);
}

void ACampusCharacter::BroadcastThrowStage(ECampusThrowable Type, uint8 Stage)
{
	const float Rate = 1.f / GetHandlingScale();
	PlayBodyThrow(Type, Stage, Rate);
	if (HasAuthority())
	{
		MulticastThrowStage(Type, Stage, Rate);
	}
	else if (IsLocallyControlled())
	{
		ServerThrowStage(Type, Stage, Rate);
	}
}

void ACampusCharacter::ServerThrowStage_Implementation(ECampusThrowable Type, uint8 Stage, float Rate)
{
	if (Stage > (uint8)ECampusBodyThrow::PutAway || !IsAlive())
	{
		return;
	}
	Rate = FMath::Clamp(Rate, 0.3f, 3.f);
	PlayBodyThrow(Type, Stage, Rate);
	MulticastThrowStage(Type, Stage, Rate);
}

void ACampusCharacter::MulticastThrowStage_Implementation(ECampusThrowable Type, uint8 Stage, float Rate)
{
	// The server and the machine that started the throw have played it already.
	if (HasAuthority() || IsLocallyControlled())
	{
		return;
	}
	PlayBodyThrow(Type, Stage, Rate);
}

void ACampusCharacter::PlayBodyThrow(ECampusThrowable Type, uint8 Stage, float Rate)
{
	UCampusTPAnimInstance* TP = GetBodyAnim();
	if (!TP || Stage > (uint8)ECampusBodyThrow::PutAway)
	{
		return;
	}
	TP->SetThrowStage((ECampusBodyThrow)Stage, Rate);
	if (Stage != (uint8)ECampusBodyThrow::Draw || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	BodyThrowType = Type;
	ClearBodyGrenade();
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		BodyGrenadeRoot = NewObject<USceneComponent>(this);
		BodyGrenadeRoot->SetupAttachment(Body, TEXT("hand_l"));
		BodyGrenadeRoot->RegisterComponent();
		CampusThrowables::BuildModel(this, BodyGrenadeRoot, Type, true, false, false, BodyGrenade);
		for (UStaticMeshComponent* Part : BodyGrenade)
		{
			// Seen by everyone but its owner, who has the one in the first-person hand (the shadow is still the body's).
			Part->SetFirstPersonPrimitiveType(Body->FirstPersonPrimitiveType);
		}
		BodyGrenadeRoot->SetVisibility(false, true);
	}
	if (!IsLocalHuman())
	{
		CampusAudio::PlayAt(this, TEXT("Grenade_Draw"), GetActorLocation() + FVector(0.f, 0.f, 40.f), 0.55f, 0.06f, CampusAssets::EAttenuation::Foley);
		FTimerHandle PinHandle;
		GetWorldTimerManager().SetTimer(PinHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			CampusAudio::PlayAt(this, TEXT("Grenade_Pin"), GetActorLocation() + FVector(0.f, 0.f, 40.f), 0.7f, 0.05f, CampusAssets::EAttenuation::Foley);
		}), 0.26f / FMath::Max(Rate, 0.1f), false);
	}
}

void ACampusCharacter::ClearBodyGrenade()
{
	for (UStaticMeshComponent* Part : BodyGrenade)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	BodyGrenade.Reset();
	if (BodyGrenadeRoot)
	{
		BodyGrenadeRoot->DestroyComponent();
		BodyGrenadeRoot = nullptr;
	}
	bBodyGrenadeSeated = false;
}

void ACampusCharacter::UpdateBodyGrenade()
{
	UCampusTPAnimInstance* TP = GetBodyAnim();
	if (!TP)
	{
		return;
	}
	ECampusBodyThrow Stage = TP->GetThrowStage();
	if (Stage != ECampusBodyThrow::None && !IsAlive())
	{
		TP->SetThrowStage(ECampusBodyThrow::None);
		Stage = ECampusBodyThrow::None;
	}
	if (Stage != ECampusBodyThrow::None)
	{
		TP->SetThrowFrame(GetActorForwardVector(), GetActorUpVector(), FRotator::NormalizeAxis(GetBaseAimRotation().Pitch));
	}
	if (!BodyGrenadeRoot)
	{
		return;
	}
	if (Stage == ECampusBodyThrow::None)
	{
		ClearBodyGrenade();
		return;
	}
	// Out of the pouch once the hand is there; it leaves the fingers at the top of the swing, or goes back in.
	const float Age = TP->GetThrowAge();
	const bool bShow = (Stage == ECampusBodyThrow::Draw && Age >= 0.12f) || (Stage == ECampusBodyThrow::Swing && Age < 0.17f)
		|| (Stage == ECampusBodyThrow::PutAway && Age < 0.25f);
	if (BodyGrenadeRoot->IsVisible() != bShow)
	{
		BodyGrenadeRoot->SetVisibility(bShow, true);
	}
	if (bShow && !bBodyGrenadeSeated)
	{
		CampusThrowables::SeatInLeftHand(GetMesh(), BodyGrenadeRoot, BodyThrowType);
		bBodyGrenadeSeated = Stage != ECampusBodyThrow::Draw || Age >= 0.3f;
	}
}

bool ACampusCharacter::BotThrow(ECampusThrowable Type, const FVector& Target)
{
	const int32 Index = (int32)Type;
	if (!HasAuthority() || !IsAlive() || bBotThrowing || !Throwables.IsValidIndex(Index) || Throwables[Index] == 0)
	{
		return false;
	}
	const FRotator Aim = GetControlRotation();
	const FVector Origin = GetPawnViewLocation() + FRotator(0.f, Aim.Yaw, 0.f).RotateVector(FVector(25.f, -12.f, 4.f));
	FVector Velocity;
	if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(this, Velocity, Origin, Target, 0.f, 0.55f))
	{
		return false;
	}
	if (Velocity.Size() > 2000.f)
	{
		return false;
	}
	// Drawn and primed like anyone else's: the grenade is seen coming out and thrown, not conjured at the head.
	bBotThrowing = true;
	BotThrowType = Type;
	BotThrowTarget = Target;
	BroadcastThrowStage(Type, (uint8)ECampusBodyThrow::Draw);
	FireBlockedUntil = FMath::Max(FireBlockedUntil, GetWorld()->GetTimeSeconds() + 1.15f);
	GetWorldTimerManager().SetTimer(BotThrowHandle, this, &ACampusCharacter::BotThrowSwing, 0.48f, false);
	return true;
}

void ACampusCharacter::BotThrowSwing()
{
	if (!IsAlive())
	{
		bBotThrowing = false;
		return;
	}
	BroadcastThrowStage(BotThrowType, (uint8)ECampusBodyThrow::Swing);
	GetWorldTimerManager().SetTimer(BotThrowHandle, this, &ACampusCharacter::BotThrowLaunch, 0.17f, false);
}

void ACampusCharacter::BotThrowLaunch()
{
	bBotThrowing = false;
	const int32 Index = (int32)BotThrowType;
	if (!IsAlive() || !Throwables.IsValidIndex(Index) || Throwables[Index] == 0)
	{
		return;
	}
	// From the hand at the top of the swing (the body is posed on this machine), never from inside a wall.
	const FVector Eye = GetPawnViewLocation();
	FVector Origin = Eye + FRotator(0.f, GetControlRotation().Yaw, 0.f).RotateVector(FVector(25.f, -12.f, 10.f));
	if (const USkeletalMeshComponent* Body = GetMesh())
	{
		const FVector Hand = Body->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE ? Body->GetBoneLocation(TEXT("hand_l")) : Origin;
		if (FVector::DistSquared(Hand, Eye) < FMath::Square(90.f))
		{
			Origin = Hand;
		}
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotThrowOrigin), false, this);
	if (GetWorld()->SweepSingleByChannel(Hit, Eye, Origin, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(4.f), Params))
	{
		Origin = Hit.Location - (Origin - Eye).GetSafeNormal() * 2.f;
	}
	FVector Velocity;
	if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(this, Velocity, Origin, BotThrowTarget, 0.f, 0.55f) || Velocity.Size() > 2000.f)
	{
		Velocity = (BotThrowTarget - Origin).GetSafeNormal() * CampusThrowables::ThrowSpeed(BotThrowType);
	}
	if (!ACampusGameState::GetRules(GetWorld()).bInfiniteEquipment)
	{
		--Throwables[Index];
	}
	// Bots cook their frags a moment, so they cannot always be thrown straight back.
	const float Fuse = CampusThrowables::Fuse(BotThrowType);
	SpawnThrowable(BotThrowType, Origin, Velocity, BotThrowType == ECampusThrowable::Frag ? Fuse - FMath::FRandRange(0.4f, 1.3f) : Fuse);
	MulticastThrowFX(BotThrowType);
}

void ACampusCharacter::ApplyFlash(float Blind, float Deaf, const FVector& From)
{
	if (!HasAuthority() || !IsAlive())
	{
		return;
	}
	if (HasPerk(ECampusPerk::TacMask))
	{
		Blind *= 0.35f;
		Deaf *= 0.35f;
	}
	if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(Controller))
	{
		PC->ClientFlashbanged(Blind, Deaf, From);
	}
	else if (ACampusBotController* Bot = Cast<ACampusBotController>(Controller))
	{
		Bot->OnFlashbanged(Blind, From);
	}
}

ACampusDoor* ACampusCharacter::FindUsableDoor() const
{
	if (!FirstPersonCamera || !IsAlive())
	{
		return nullptr;
	}
	const FVector Eye = FirstPersonCamera->GetComponentLocation();
	const FVector Dir = GetControlRotation().Vector();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusUseDoor), false, this);
	if (GetWorld()->SweepSingleByChannel(Hit, Eye, Eye + Dir * 190.f, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8.f), Params))
	{
		if (ACampusDoor* Door = Cast<ACampusDoor>(Hit.GetActor()))
		{
			return Door;
		}
	}
	// An open door is no longer in the way of the trace: take the nearest one in front within reach.
	ACampusDoor* Best = nullptr;
	float BestDistance = 165.f;
	for (TActorIterator<ACampusDoor> It(GetWorld()); It; ++It)
	{
		const FVector To = It->GetHandleLocation() - Eye;
		const float Distance = To.Size();
		if (Distance < BestDistance && (To / FMath::Max(Distance, 1.f) | Dir) > 0.55f)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}

void ACampusCharacter::Input_Interact()
{
	ACampusDoor* Door = FindUsableDoor();
	if (!Door)
	{
		return;
	}
	if (HasAuthority())
	{
		Door->Toggle(this);
	}
	else
	{
		ServerUseDoor(Door);
	}
}

void ACampusCharacter::ServerUseDoor_Implementation(ACampusDoor* Door)
{
	if (Door && IsAlive() && FVector::Dist(Door->GetHandleLocation(), GetActorLocation()) < 320.f)
	{
		Door->Toggle(this);
	}
}

void ACampusCharacter::Input_Move(const FInputActionValue& Value)
{
	if (!IsAlive() || !Controller)
	{
		return;
	}
	const FVector2D V = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void ACampusCharacter::LookInput(float YawDegrees, float PitchDegrees)
{
	AddControllerYawInput(YawDegrees);
	AddControllerPitchInput(PitchDegrees);
}

void ACampusCharacter::Input_Look(const FInputActionValue& Value)
{
	if (!IsAlive())
	{
		return;
	}
	const FVector2D V = Value.Get<FVector2D>();
	float Sens = MouseSensitivity * 0.022f;
	if (const ACampusWeapon* W = GetCurrentWeapon())
	{
		const float Zoom = FMath::Max(W->GetStats().ADSZoom, 1.f);
		Sens *= FMath::Lerp(1.f, ADSSensitivityMultiplier / Zoom, GetADSAlpha());
	}
	LookInput(V.X * Sens, V.Y * Sens * (bInvertY ? -1.f : 1.f));
}

void ACampusCharacter::Input_LookGamepad(const FInputActionValue& Value)
{
	if (!IsAlive())
	{
		return;
	}
	const FVector2D V = Value.Get<FVector2D>();
	const float Dt = GetWorld()->GetDeltaSeconds();
	auto Curve = [](float X) { return FMath::Sign(X) * X * X; };
	float Scale = GamepadSensitivity;
	if (const ACampusWeapon* W = GetCurrentWeapon())
	{
		Scale *= FMath::Lerp(1.f, 0.55f / FMath::Max(W->GetStats().ADSZoom * 0.85f, 1.f), GetADSAlpha());
	}
	LookInput(Curve(V.X) * 230.f * Scale * Dt, Curve(V.Y) * 150.f * Scale * Dt * (bInvertY ? -1.f : 1.f));
}

void ACampusCharacter::Input_JumpStarted()
{
	if (!IsAlive())
	{
		return;
	}
	InterruptActions();
	if (UCampusMovementComponent* Move = GetCampusMovement())
	{
		Move->bWantsToMantle = true;
	}
	if (IsSliding())
	{
		// Slide jump: breaks the slide at any point and carries its momentum into the air.
		CancelSlide(true, true);
		Jump();
		return;
	}
	if (bIsCrouched)
	{
		UnCrouch();
		return;
	}
	Jump();
}

void ACampusCharacter::CancelSlide(bool bResumeSprint, bool bJumping)
{
	UCampusMovementComponent* Move = GetCampusMovement();
	if (!Move || !IsSliding())
	{
		return;
	}
	bCrouchHeldByHold = false;
	bStandAfterSlide = false;
	UnCrouch();
	Move->CancelSlide();
	if (bResumeSprint && !bAimHeld && !bFireHeld)
	{
		Move->bWantsToSprint = true;
		// The one-handed tactical carry would pull the weapon out of a magazine change: it waits for the reload.
		const bool bReloadingNow = GetCurrentWeapon() && GetCurrentWeapon()->IsReloading();
		Move->bWantsToTacSprint = bAutoTacSprint && !bReloadingNow && Move->TacSprintStamina > 0.5f;
		bTacSprintAfterReload = bAutoTacSprint && bReloadingNow;
		NotMovingForwardTime = 0.f;
	}
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnSlideCancel(bJumping);
	}
}

void ACampusCharacter::Input_JumpCompleted()
{
	StopJumping();
	if (UCampusMovementComponent* Move = GetCampusMovement())
	{
		Move->bWantsToMantle = false;
	}
}

void ACampusCharacter::Input_Sprint()
{
	UCampusMovementComponent* Move = GetCampusMovement();
	bSprintKeyHeld = true;
	if (!IsAlive() || !Move)
	{
		return;
	}
	// Aiming wins over sprinting: on the sights the sprint key holds the breath instead (slides still stand up, sights kept).
	if (bAimHeld && !IsSliding())
	{
		if (BreathComp && IsAiming())
		{
			BreathComp->SetHoldBreath(true);
		}
		return;
	}
	InterruptActions();
	if (IsSliding())
	{
		CancelSlide(true);
		return;
	}
	// No sprinting in the air: mid-jump the press never takes the trigger away.
	if (Move->IsFalling() && bFireHeld)
	{
		return;
	}
	if (!Move->bWantsToSprint)
	{
		if (bIsCrouched)
		{
			UnCrouch();
		}
		Move->bWantsToSprint = true;
		// A sprint carries the reload on; the tactical sprint (one hand off the weapon) waits until it is done.
		const bool bReloadingNow = GetCurrentWeapon() && GetCurrentWeapon()->IsReloading();
		Move->bWantsToTacSprint = bAutoTacSprint && !bReloadingNow && Move->TacSprintStamina > 0.5f;
		bTacSprintAfterReload = bAutoTacSprint && bReloadingNow;
		NotMovingForwardTime = 0.f;
	}
	else if (Move->TacSprintStamina > 0.5f)
	{
		if (GetCurrentWeapon() && GetCurrentWeapon()->IsReloading())
		{
			bTacSprintAfterReload = true;
		}
		else
		{
			Move->bWantsToTacSprint = true;
		}
	}
}

void ACampusCharacter::Input_SprintReleased()
{
	bSprintKeyHeld = false;
	if (BreathComp)
	{
		BreathComp->SetHoldBreath(false);
	}
}

void ACampusCharacter::Input_Crouch()
{
	if (!IsAlive())
	{
		return;
	}
	InterruptActions();
	if (IsSliding())
	{
		CancelSlide(true);
		return;
	}
	if (bIsCrouched || GetCharacterMovement()->bWantsToCrouch)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void ACampusCharacter::Input_CrouchHoldStarted()
{
	if (!IsAlive())
	{
		return;
	}
	InterruptActions();
	if (IsSliding())
	{
		CancelSlide(true);
		return;
	}
	bCrouchHeldByHold = true;
	Crouch();
}

void ACampusCharacter::Input_CrouchHoldCompleted()
{
	if (!bCrouchHeldByHold)
	{
		return;
	}
	bCrouchHeldByHold = false;
	if (IsSliding())
	{
		// A tap still gives the full slide; stand up once it ends.
		bStandAfterSlide = true;
		return;
	}
	UnCrouch();
}

void ACampusCharacter::Input_AimStarted()
{
	if (!IsAlive())
	{
		return;
	}
	// A blade has no sights: the aim button is its heavy stab.
	if (const ACampusWeapon* W = GetCurrentWeapon(); W && W->GetDef() && W->GetDef()->IsMelee())
	{
		MeleeStrike(true);
		return;
	}
	bAimHeld = bToggleAim ? !bAimHeld : true;
	if (bAimHeld)
	{
		StopSprinting();
		InterruptActions();
		InterruptThrow(false);
		// Aiming breaks a reload whenever there is something to shoot: rounds left in the old magazine, or the new one
		// already seated (the bolt / settle is skipped). An empty, unseated magazine keeps reloading.
		if (ACampusWeapon* W = GetCurrentWeapon())
		{
			if (W->IsReloading() && (W->IsReloadInserted() || W->GetAmmoInMag() > 0))
			{
				W->CancelReload();
			}
		}
		CampusAudio::Play2D(this, TEXT("ADS_In"), 0.35f, 0.05f);
	}
}

void ACampusCharacter::Input_AimCompleted()
{
	if (!bToggleAim && bAimHeld)
	{
		bAimHeld = false;
		CampusAudio::Play2D(this, TEXT("ADS_Out"), 0.25f, 0.05f);
	}
}

void ACampusCharacter::Input_FireStarted()
{
	if (!IsAlive())
	{
		return;
	}
	bFireHeld = true;
	InterruptActions();
	InterruptThrow(false);
	// Also clears a sprint carried through a jump or slide, which would otherwise pick up again on landing and cut the burst off.
	StopSprinting();
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		W->OnTriggerPressed();
	}
}

void ACampusCharacter::Input_FireCompleted()
{
	bFireHeld = false;
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		W->OnTriggerReleased();
	}
}

void ACampusCharacter::Input_Reload()
{
	ACampusWeapon* W = GetCurrentWeapon();
	if (!W || !IsAlive() || IsSwitchingWeapons() || IsThrowing())
	{
		return;
	}
	InterruptActions();
	// Reloads on the run: a tactical sprint drops to a sprint for the magazine change and picks up again after it.
	UCampusMovementComponent* Move = GetCampusMovement();
	if (Move && !W->IsReloading() && W->GetAmmoInMag() < W->GetStats().MagSize && W->GetAmmoReserve() > 0 && Move->bWantsToTacSprint)
	{
		Move->bWantsToTacSprint = false;
		bTacSprintAfterReload = true;
	}
	W->RequestReload();
}

void ACampusCharacter::Input_SwitchWeapon()
{
	if (InterruptThrow(true) || Weapons.Num() < 2)
	{
		return;
	}
	if (IsSwitchingWeapons())
	{
		CancelPendingSwitch();
		return;
	}
	RequestEquip(CurrentWeaponIndex == 0 ? 1 : 0);
}

void ACampusCharacter::Input_Primary()
{
	if (InterruptThrow(true))
	{
		return;
	}
	if (IsSwitchingWeapons() && CurrentWeaponIndex == 0)
	{
		CancelPendingSwitch();
		return;
	}
	RequestEquip(0);
}

void ACampusCharacter::Input_Secondary()
{
	if (InterruptThrow(true))
	{
		return;
	}
	if (IsSwitchingWeapons() && CurrentWeaponIndex == 1)
	{
		CancelPendingSwitch();
		return;
	}
	RequestEquip(1);
}

void ACampusCharacter::CancelPendingSwitch()
{
	if (!IsSwitchingWeapons())
	{
		return;
	}
	PendingWeaponIndex = INDEX_NONE;
	const float Now = GetWorld()->GetTimeSeconds();
	FireBlockedUntil = FMath::Max(FireBlockedUntil, Now + 0.12f);
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->CancelHolster(0.22f);
	}
	CampusAudio::Play2D(this, TEXT("Weapon_Equip"), 0.4f, 0.05f);
}

void ACampusCharacter::InterruptActions()
{
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->InterruptActions();
	}
}

bool ACampusCharacter::InterruptThrow(bool bPutAway)
{
	if (ThrowPhase == EThrowPhase::Recover && !bThrowHurried)
	{
		// Firing, aiming or switching cuts the recovery short: the weapon snaps back up.
		bThrowHurried = true;
		if (Viewmodel && IsLocalHuman())
		{
			Viewmodel->HurryThrow(2.6f);
		}
		return false;
	}
	if (bPutAway && ThrowPhase == EThrowPhase::Prime && !bPinPulled)
	{
		ThrowPhase = EThrowPhase::None;
		FireBlockedUntil = FMath::Max(FireBlockedUntil, GetWorld()->GetTimeSeconds() + 0.28f);
		if (Viewmodel && IsLocalHuman())
		{
			Viewmodel->OnThrowCancel();
		}
		BroadcastThrowStage(ThrowType, (uint8)ECampusBodyThrow::PutAway);
		CampusAudio::Play2D(this, TEXT("Weapon_Holster"), 0.4f, 0.05f);
		return true;
	}
	return false;
}

void ACampusCharacter::Input_FireMode()
{
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		if (IsAlive())
		{
			W->CycleFireMode();
		}
	}
}

void ACampusCharacter::Input_Flashlight()
{
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		W->ToggleFlashlight();
	}
}

void ACampusCharacter::Input_Melee()
{
	// With a knife out the melee key is its heavy stab.
	MeleeStrike(true);
}

void ACampusCharacter::MeleeStrike(bool bHeavy)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (!IsAlive() || Now < MeleeCooldownUntil || IsThrowing() || IsMantling())
	{
		return;
	}
	// A melee breaks a weapon switch that is still holstering; the current weapon does the strike.
	CancelPendingSwitch();
	InterruptActions();
	const ACampusWeapon* Current = GetCurrentWeapon();
	const bool bKnife = Current && Current->GetDef() && Current->GetDef()->IsMelee();
	bHeavy = bHeavy && bKnife;
	// Butt stroke: lands ~0.2 s in, from 0.3 s the recovery can be broken by firing, aiming, switching or sprinting.
	// Knife: the light slash lands at ~0.1 s and the next can follow at 0.42 s (two of them kill); the heavy stab is
	// drawn back first, lands at ~0.22 s and kills, but the arm needs a second to come back.
	const float Cooldown = !bKnife ? 0.75f : (bHeavy ? 1.0f : 0.42f);
	MeleeCooldownUntil = Now + Cooldown;
	FireBlockedUntil = FMath::Max(FireBlockedUntil, Now + (bKnife ? Cooldown : 0.3f));
	// A knife cuts on the run; the sprint picks up again after the swing (hold to sprint) or with the next press.
	StopSprinting();
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		W->CancelReload();
	}
	if (Viewmodel && IsLocalHuman())
	{
		if (bKnife)
		{
			Viewmodel->OnKnifeSlash(bHeavy);
		}
		else
		{
			Viewmodel->OnMelee();
		}
	}
	CampusAudio::Play2D(this, TEXT("Melee_Swing"), bKnife ? (bHeavy ? 0.85f : 0.6f) : 0.8f, bKnife ? 0.14f : 0.08f);
	const uint8 Kind = !bKnife ? 0 : (bHeavy ? 2 : 1);
	if (HasAuthority())
	{
		ServerMelee_Implementation(GetAimOrigin(), GetAimDirection(), Kind);
	}
	else
	{
		ServerMelee(GetAimOrigin(), GetAimDirection(), Kind);
	}
}

void ACampusCharacter::Input_Inspect()
{
	if (IsAlive() && Viewmodel && !IsAiming() && !bFireHeld)
	{
		Viewmodel->OnInspect();
	}
}

void ACampusCharacter::DebugAction(const FString& Action)
{
	UCampusMovementComponent* Move = GetCampusMovement();
	if (Action == TEXT("sprint")) { if (Move) { Move->bWantsToSprint = false; } Input_Sprint(); }
	else if (Action == TEXT("sprintonly")) { if (Move) { Move->bWantsToSprint = false; } Input_Sprint(); if (Move) { Move->bWantsToTacSprint = false; } }
	else if (Action == TEXT("tac")) { Input_Sprint(); Input_Sprint(); }
	else if (Action == TEXT("slide") || Action == TEXT("crouch")) { Input_Crouch(); }
	else if (Action == TEXT("jump"))
	{
		// Held briefly like a real key press: the jump itself happens on the next movement update.
		Input_JumpStarted();
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() { Input_JumpCompleted(); }), 0.3f, false);
	}
	else if (Action == TEXT("reload")) { Input_Reload(); }
	else if (Action == TEXT("switch")) { Input_SwitchWeapon(); }
	else if (Action == TEXT("inspect")) { Input_Inspect(); }
	else if (Action == TEXT("light")) { Input_Flashlight(); }
	else if (Action == TEXT("door")) { Input_Interact(); }
	else if (Action == TEXT("melee")) { Input_Melee(); }
	else if (Action == TEXT("fire")) { Input_FireStarted(); }
	else if (Action == TEXT("unfire")) { Input_FireCompleted(); }
	else if (Action == TEXT("aim")) { Input_AimStarted(); }
	else if (Action == TEXT("unaim")) { Input_AimCompleted(); }
	else if (Action == TEXT("stop")) { ScriptedMove = FVector2D::ZeroVector; bSprintKeyHeld = false; StopSprinting(); Input_FireCompleted(); Input_AimCompleted(); }
	// Grenades: "frag" / "thermite" / "flash" / "smoke" take one out and hold it, "release" throws it.
	else if (Action == TEXT("frag")) { StartThrow(ECampusThrowable::Frag); }
	else if (Action == TEXT("thermite")) { StartThrow(ECampusThrowable::Thermite); }
	else if (Action == TEXT("flash")) { StartThrow(ECampusThrowable::Flash); }
	else if (Action == TEXT("smoke")) { StartThrow(ECampusThrowable::Smoke); }
	else if (Action == TEXT("release")) { bThrowHeld = false; }
	else if (Action == TEXT("refill")) { if (HasAuthority()) { Throwables.Init(9, (int32)ECampusThrowable::Count); } }
	else if ((Action.StartsWith(TEXT("drop")) || Action.StartsWith(TEXT("place"))) && HasAuthority())
	{
		// "dropfrag700": a grenade lands 7 m ahead and goes off a second later; "place..." lies there for a minute (captures).
		const bool bPlace = Action.StartsWith(TEXT("place"));
		const FString Rest = Action.RightChop(bPlace ? 5 : 4);
		int32 Split = Rest.Len();
		while (Split > 0 && FChar::IsDigit(Rest[Split - 1]))
		{
			--Split;
		}
		const float Distance = Split < Rest.Len() ? FCString::Atof(*Rest.RightChop(Split)) : 700.f;
		const FString Name = Rest.Left(Split);
		ECampusThrowable Type = ECampusThrowable::Frag;
		for (int32 i = 0; i < (int32)ECampusThrowable::Count; ++i)
		{
			if (Name.Equals(CampusText::ThrowableId((ECampusThrowable)i).ToString(), ESearchCase::IgnoreCase))
			{
				Type = (ECampusThrowable)i;
			}
		}
		const FVector At = GetActorLocation() + FRotator(0.f, GetControlRotation().Yaw, 0.f).Vector() * Distance + FVector(0.f, 0.f, 40.f);
		SpawnThrowable(Type, At, FVector(0.f, 0.f, -100.f), bPlace ? 60.f : (Type == ECampusThrowable::Frag ? 1.f : CampusThrowables::Fuse(Type) * 0.5f));
	}
	else if (Action == TEXT("flashme"))
	{
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(GetController()))
		{
			PC->ClientFlashbanged(1.f, 1.f, GetActorLocation() + GetActorForwardVector() * 400.f);
		}
	}
	else if (Action == TEXT("hurtcritical") && HasAuthority())
	{
		// 调试重伤经过真实伤害链，同时兼容已经开启无敌的取样脚本
		const bool bWasInvulnerable = bInvulnerable;
		bInvulnerable = false;
		ApplyCampusDamage(MaxHealth * 0.85f, ECampusHitZone::UpperTorso, nullptr, this, NAME_None, GetActorForwardVector(), NAME_None);
		bInvulnerable = bWasInvulnerable;
		UE_LOG(LogTemp, Display, TEXT("[CampusHurtCritical] Health=%.1f MaxHealth=%.1f GodRestored=%d"), Health, MaxHealth, bInvulnerable ? 1 : 0);
	}
	else if (Action == TEXT("hurt"))
	{
		// Local damage feedback only (works under CampusGod): a 35-damage hit from ahead-right.
		if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(GetController()))
		{
			PC->ClientOnDamaged(GetActorLocation() + GetActorForwardVector() * 600.f + GetActorRightVector() * 350.f, 35.f);
		}
	}
}

// ------------------------------------------------------------------ bots

void ACampusCharacter::BotSetFire(bool bFire)
{
	if (bFire == bFireHeld)
	{
		return;
	}
	if (bFire)
	{
		Input_FireStarted();
	}
	else
	{
		Input_FireCompleted();
	}
}

void ACampusCharacter::BotSetAim(bool bAim)
{
	bAimHeld = bAim;
	if (bAim)
	{
		StopSprinting();
	}
}

void ACampusCharacter::BotSetSprint(bool bSprint)
{
	UCampusMovementComponent* Move = GetCampusMovement();
	if (!Move)
	{
		return;
	}
	if (bSprint && !Move->bWantsToSprint && !bAimHeld)
	{
		if (bIsCrouched)
		{
			UnCrouch();
		}
		Move->bWantsToSprint = true;
		NotMovingForwardTime = 0.f;
	}
	else if (!bSprint && Move->bWantsToSprint)
	{
		StopSprinting();
	}
}

void ACampusCharacter::BotSetTacSprint(bool bTac)
{
	UCampusMovementComponent* Move = GetCampusMovement();
	if (!Move || !Move->bWantsToSprint)
	{
		return;
	}
	Move->bWantsToTacSprint = bTac && Move->TacSprintStamina > 0.5f;
}

void ACampusCharacter::BotSetCrouch(bool bCrouch)
{
	if (bCrouch && !bIsCrouched && !GetCharacterMovement()->bWantsToCrouch)
	{
		Crouch();
	}
	else if (!bCrouch && (bIsCrouched || GetCharacterMovement()->bWantsToCrouch))
	{
		if (IsSliding())
		{
			CancelSlide(true);
		}
		UnCrouch();
	}
}

void ACampusCharacter::BotReload()
{
	if (ACampusWeapon* W = GetCurrentWeapon())
	{
		W->RequestReload();
	}
}

void ACampusCharacter::BotJump()
{
	if (!IsAlive())
	{
		return;
	}
	if (IsSliding())
	{
		CancelSlide(true, true);
	}
	else if (bIsCrouched)
	{
		UnCrouch();
	}
	Jump();
}

void ACampusCharacter::BotSlide(float Delay)
{
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (IsAlive() && IsSprinting())
		{
			Crouch();
		}
	}), FMath::Max(Delay, 0.01f), false);
}

// ------------------------------------------------------------------ weapons

ACampusWeapon* ACampusCharacter::GetCurrentWeapon() const
{
	return Weapons.IsValidIndex(CurrentWeaponIndex) ? Weapons[CurrentWeaponIndex].Get() : nullptr;
}

void ACampusCharacter::GiveLoadout(const FCampusLoadout& Loadout)
{
	if (!HasAuthority())
	{
		return;
	}
	for (ACampusWeapon* Old : Weapons)
	{
		if (Old)
		{
			Old->Destroy();
		}
	}
	Weapons.Reset();

	const UCampusArmory& Armory = UCampusArmory::Get();
	auto Spawn = [this, &Armory](const FCampusWeaponLoadout& WeaponLoadout)
	{
		if (!Armory.FindWeapon(WeaponLoadout.WeaponId))
		{
			return;
		}
		ACampusWeapon* W = GetWorld()->SpawnActorDeferred<ACampusWeapon>(ACampusWeapon::StaticClass(), GetActorTransform(), this, this, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!W)
		{
			return;
		}
		W->InitializeWeapon(WeaponLoadout);
		W->FinishSpawning(GetActorTransform());
		W->AttachToCharacter(this);
		Weapons.Add(W);
	};
	Perks = Loadout.Perks;
	CampusPerks::Sanitize(Perks);
	KillsThisLife = 0;
	EquipmentRechargeTimer = 0.f;
	Spawn(Loadout.Primary);
	Spawn(Loadout.Secondary);
	// The equipped lethal and tactical, two of each (three with Bandolier).
	const uint8 Count = HasPerk(ECampusPerk::Bandolier) ? 3 : 2;
	Throwables.Init(0, (int32)ECampusThrowable::Count);
	SelectedLethal = CampusPerks::IsLethalThrowable(Loadout.Lethal) ? Loadout.Lethal : ECampusThrowable::Frag;
	SelectedTactical = !CampusPerks::IsLethalThrowable(Loadout.Tactical) && Loadout.Tactical < ECampusThrowable::Count ? Loadout.Tactical : ECampusThrowable::Flash;
	Throwables[(int32)SelectedLethal] = Count;
	Throwables[(int32)SelectedTactical] = Count;
	StartLethals = Count;
	StartTacticals = Count;
	if (IsLocalHuman() && HasSpecialty(ECampusPerkGroup::Recon))
	{
		RevealUntil = GetWorld()->GetTimeSeconds() + 3.f;
	}

	CurrentWeaponIndex = Weapons.Num() > 0 ? 0 : INDEX_NONE;
	PendingWeaponIndex = INDEX_NONE;
	HandleWeaponChanged(true);
}

void ACampusCharacter::RequestEquip(int32 Index)
{
	if (!IsAlive() || Index == CurrentWeaponIndex || !Weapons.IsValidIndex(Index) || !Weapons[Index] || PendingWeaponIndex != INDEX_NONE || IsThrowing())
	{
		return;
	}
	ServerCancelMelee();
	ACampusWeapon* Current = GetCurrentWeapon();
	float HolsterTime = 0.f;
	if (Current)
	{
		const FCampusWeaponDef* Def = Current->GetDef();
		HolsterTime = ((Def && Def->bPistolAnims) ? 0.16f : 0.24f) * GetHandlingScale();
		Current->OnHolster();
	}
	PendingWeaponIndex = Index;
	PendingSwitchTime = GetWorld()->GetTimeSeconds() + HolsterTime;
	if (Viewmodel && IsLocalHuman())
	{
		Viewmodel->OnHolster(HolsterTime);
	}
	CampusAudio::Play2D(this, TEXT("Weapon_Holster"), 0.5f, 0.05f);
}

void ACampusCharacter::ServerEquipWeapon_Implementation(int32 Index)
{
	if (!Weapons.IsValidIndex(Index) || !IsAlive() || Index == CurrentWeaponIndex)
	{
		return;
	}
	if (ACampusWeapon* Current = GetCurrentWeapon())
	{
		Current->OnHolster();
	}
	CurrentWeaponIndex = Index;
	HandleWeaponChanged(false);
}

void ACampusCharacter::HandleWeaponChanged(bool bPlayEquip)
{
	for (int32 i = 0; i < Weapons.Num(); ++i)
	{
		if (Weapons[i])
		{
			Weapons[i]->SetEquipped(i == CurrentWeaponIndex);
		}
	}

	ACampusWeapon* W = GetCurrentWeapon();
	if (!W)
	{
		return;
	}
	const FCampusWeaponDef* Def = W->GetDef();
	const bool bPistol = Def && Def->bPistolAnims;

	if (!bIsDead)
	{
		if (UClass* AnimClass = CampusAssets::ThirdPersonAnimClass(bPistol))
		{
			if (GetMesh()->GetAnimClass() != UCampusTPAnimInstance::StaticClass())
			{
				GetMesh()->SetAnimInstanceClass(UCampusTPAnimInstance::StaticClass());
			}
			if (UCampusTPAnimInstance* TP = GetBodyAnim())
			{
				TP->SetLocomotionClass(AnimClass);
				if (bPlayEquip || !IsLocallyControlled())
				{
					TP->PlayUpper(CampusAssets::Anim(bPistol ? CampusAssets::EAnim::PistolEquip : CampusAssets::EAnim::RifleEquip), W->GetStats().EquipTime, 0.08f, 0.2f);
				}
			}
		}
	}
	if (UCampusFPAnimInstance* Anim = GetArmsAnim())
	{
		Anim->SetBasePose(CampusAssets::Anim(bPistol ? CampusAssets::EAnim::PistolIdle : CampusAssets::EAnim::RifleIdle));
		Anim->StopActions(0.1f);
	}

	if (IsLocallyControlled())
	{
		const float EquipTime = W->GetStats().EquipTime;
		FireBlockedUntil = FMath::Max(FireBlockedUntil, GetWorld()->GetTimeSeconds() + EquipTime * 0.85f);
		if (bPlayEquip && Viewmodel && IsLocalHuman())
		{
			Viewmodel->OnEquip(EquipTime);
		}
		W->OnEquipped();
	}
}

void ACampusCharacter::OnRep_Weapons()
{
	for (ACampusWeapon* W : Weapons)
	{
		if (W)
		{
			W->AttachToCharacter(this);
		}
	}
	if (IsLocallyControlled() && (CurrentWeaponIndex == INDEX_NONE || !GetCurrentWeapon()) && Weapons.Num() > 0 && Weapons[0])
	{
		CurrentWeaponIndex = 0;
	}
	HandleWeaponChanged(true);
}

void ACampusCharacter::OnRep_CurrentWeaponIndex()
{
	HandleWeaponChanged(false);
}

void ACampusCharacter::ServerMelee_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, uint8 Kind)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Dir = FVector(Direction).GetSafeNormal();
	if (!IsAlive() || IsThrowing() || IsMantling() || Now < ServerMeleeReadyTime || Dir.IsNearlyZero() || Dir.ContainsNaN())
	{
		return;
	}
	ACampusWeapon* Current = GetCurrentWeapon();
	const bool bKnife = Current && Current->GetDef() && Current->GetDef()->IsMelee();
	Kind = bKnife ? FMath::Clamp<uint8>(Kind, 1, 2) : 0;
	ServerMeleeReadyTime = Now + (bKnife ? (Kind == 2 ? 1.f : 0.42f) : 0.75f);
	const float ContactDelay = Kind == 1 ? 0.1f : (Kind == 2 ? 0.22f : 0.2f);
	// damage lands with the blade, and an old swing cannot follow the player onto a different weapon
	const TWeakObjectPtr<ACampusWeapon> AcceptedWeapon(Current);
	GetWorldTimerManager().SetTimer(MeleeContactHandle, FTimerDelegate::CreateWeakLambda(this, [this, Dir, Kind, AcceptedWeapon]()
	{
		ResolveMeleeContact(Dir, Kind, AcceptedWeapon);
	}), ContactDelay, false);
}

void ACampusCharacter::ServerCancelMelee_Implementation()
{
	GetWorldTimerManager().ClearTimer(MeleeContactHandle);
}

void ACampusCharacter::ResolveMeleeContact(const FVector& Dir, uint8 Kind, TWeakObjectPtr<ACampusWeapon> AcceptedWeapon)
{
	const ACampusWeapon* Current = GetCurrentWeapon();
	if (!IsAlive() || IsSwitchingWeapons() || IsThrowing() || IsMantling() || AcceptedWeapon.IsStale() || Current != AcceptedWeapon.Get())
	{
		return;
	}
	const FVector Start = GetPawnViewLocation();
	const float Reach = Kind == 2 ? Current->GetStats().RangeNear : 170.f;
	// Light slash: 55, two to put a full-health player down; the stab and the butt stroke kill outright.
	const float MeleeDamage = Kind == 1 ? 55.f : 135.f;
	const FVector End = Start + Dir * Reach;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusMelee), false, this);
	GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, CAMPUS_TRACE_WEAPON, FCollisionShape::MakeSphere(28.f), Params);
	bool bHitPerson = false;
	for (const FHitResult& Hit : Hits)
	{
		ACampusCharacter* Victim = Cast<ACampusCharacter>(Hit.GetActor());
		if (Victim && Victim != this && Victim->IsAlive() && IsEnemyOf(Victim))
		{
			FHitResult Obstruction;
			FCollisionQueryParams SightParams(SCENE_QUERY_STAT(CampusMeleeCover), false, this);
			SightParams.AddIgnoredActor(Victim);
			if (GetWorld()->LineTraceSingleByChannel(Obstruction, Start, Hit.ImpactPoint, CAMPUS_TRACE_WEAPON, SightParams))
			{
				break;
			}
			Victim->ApplyCampusDamage(MeleeDamage, ECampusHitZone::UpperTorso, Controller, this, TEXT("Melee"), Dir, Hit.BoneName);
			CampusAudio::PlayAt(this, TEXT("Melee_Hit"), Hit.ImpactPoint, 1.f, 0.05f, CampusAssets::EAttenuation::Impact);
			bHitPerson = true;
			break;
		}
	}
	MulticastMeleeFX(Start, Dir, bHitPerson);
}

void ACampusCharacter::MulticastMeleeFX_Implementation(FVector_NetQuantize Start, FVector_NetQuantizeNormal Direction, bool bHitPerson)
{
	if (!IsLocallyControlled())
	{
		CampusAudio::PlayAt(this, TEXT("Melee_Swing"), GetActorLocation(), 0.8f, 0.08f, CampusAssets::EAttenuation::Foley);
	}
	if (bHitPerson)
	{
		return;
	}
	// A swing into a window: the butt (or the blade's pommel) through the glass. Every machine traces the same line
	// from the same replicated start, so the pane breaks the same way everywhere.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusMeleeGlass), false, this);
	const FVector Dir = FVector(Direction).GetSafeNormal();
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * 150.f, CAMPUS_TRACE_WEAPON, Params) && UCampusBreakableGlass::IsGlass(Hit.GetComponent()))
	{
		UCampusBreakableGlass::SmashAt(GetWorld(), Hit.ImpactPoint, Dir, 40.f);
		CampusAudio::PlayAt(this, TEXT("Melee_Hit"), Hit.ImpactPoint, 0.6f, 0.05f, CampusAssets::EAttenuation::Impact);
	}
}

// ------------------------------------------------------------------ damage & death

bool ACampusCharacter::ApplyCampusDamage(float Damage, ECampusHitZone Zone, AController* InstigatorController, AActor* Causer, FName WeaponId, const FVector& Direction, FName Bone)
{
	if (!HasAuthority() || bIsDead || Damage <= 0.f || bInvulnerable)
	{
		return false;
	}

	ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>();
	if (GM && !GM->CanDamage(this, InstigatorController))
	{
		return false;
	}
	if (HasPerk(ECampusPerk::FlakJacket))
	{
		if (WeaponId == CampusText::ThrowableId(ECampusThrowable::Frag))
		{
			Damage *= 0.5f;
			NextDeathImpulse = 0.f;
		}
		else if (WeaponId == CampusText::ThrowableId(ECampusThrowable::Thermite))
		{
			Damage *= 0.4f;
		}
	}

	Health = FMath::Max(0.f, Health - Damage);
	LastDamageTime = GetWorld()->GetTimeSeconds();
	const FVector SourceLocation = Causer ? Causer->GetActorLocation() : GetActorLocation() - Direction * 500.f;
	LastDamageFrom = SourceLocation;

	if (GM)
	{
		GM->NotifyDamage(this, InstigatorController, Damage);
	}

	if (ACampusPlayerController* VictimPC = Cast<ACampusPlayerController>(Controller))
	{
		VictimPC->ClientOnDamaged(SourceLocation, Damage);
	}
	OnDamaged.Broadcast(Damage, SourceLocation);

	const bool bKilled = Health <= 0.f;
	const bool bHeadshot = Zone == ECampusHitZone::Head;
	if (ACampusPlayerController* ShooterPC = Cast<ACampusPlayerController>(InstigatorController))
	{
		if (ShooterPC != Controller)
		{
			// Gunshots draw their own hitmarker on the shooter's machine; grenades, thermite and melee are only known here.
			const bool bGunshot = UCampusArmory::Get().FindWeapon(WeaponId) != nullptr;
			const ACampusPlayerState* ShooterPS = ShooterPC->GetPlayerState<ACampusPlayerState>();
			const bool bEnemy = !ShooterPS || ShooterPS->Team == ECampusTeam::None || ShooterPS->Team != GetTeam();
			if (bKilled)
			{
				ShooterPC->ClientHitConfirmed(true, bHeadshot, Damage);
			}
			else if (!bGunshot && bEnemy)
			{
				ShooterPC->ClientEquipmentHit(Damage);
			}
		}
	}
	if (bKilled)
	{
		Die(InstigatorController, WeaponId, bHeadshot, Direction, Bone);
	}
	else
	{
		MulticastHitReact(Bone, Direction.GetSafeNormal(), FMath::Clamp(Damage / 40.f, 0.3f, 1.2f));
	}
	return bKilled;
}

void ACampusCharacter::Die(AController* Killer, FName WeaponId, bool bHeadshot, const FVector& Direction, FName Bone)
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}
	bIsDead = true;
	Health = 0.f;
	DeathDirection = Direction.GetSafeNormal();
	DeathBone = Bone;
	DeathImpulse = NextDeathImpulse;
	KillerState = Killer ? Killer->PlayerState : nullptr;

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OnRep_Dead();

	if (ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>())
	{
		GM->NotifyKilled(this, Killer, WeaponId, bHeadshot);
	}
	SetLifeSpan(15.f);
}

void ACampusCharacter::OnRep_Health(float OldHealth)
{
}

void ACampusCharacter::OnRep_Dead()
{
	if (!bIsDead)
	{
		return;
	}
	bFireHeld = false;
	bAimHeld = false;
	ThrowPhase = EThrowPhase::None;
	for (ACampusWeapon* W : Weapons)
	{
		if (W)
		{
			W->OnOwnerDied();
		}
	}
	ArmsMesh->SetVisibility(false, false);
	if (Viewmodel)
	{
		Viewmodel->SetComponentTickEnabled(false);
	}
	StartRagdoll();
	CampusAudio::PlayAt(this, TEXT("Body_Fall"), GetActorLocation(), 0.8f, 0.05f, CampusAssets::EAttenuation::Foley);
}

void ACampusCharacter::StartRagdoll()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (PhysicalAnimation && bHitReactActive)
	{
		PhysicalAnimation->SetStrengthMultiplyer(0.f);
	}
	bHitReactActive = false;
	HitReactWeight = 0.f;
	Body->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
	Body->SetOwnerNoSee(false);
	for (UStaticMeshComponent* Part : GearParts)
	{
		if (Part)
		{
			Part->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
			Part->SetOwnerNoSee(false);
		}
	}
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	Body->WakeAllRigidBodies();
	Body->bBlendPhysics = true;

	const FName ImpulseBone = DeathBone.IsNone() ? FName(TEXT("spine_03")) : DeathBone;
	const bool bHeadshot = DeathBone == TEXT("head") || DeathBone == TEXT("neck_01");
	// Powered ragdoll: the joints keep reaching for the animated pose (a heavy hit reaction on the upper body, the
	// legs settling to a stand) with a strength that drains away over half a second, legs first. The body jolts,
	// the knees buckle and it folds, instead of dropping like an empty suit. Headshots go limp almost at once.
	static const FName Pelvis(TEXT("pelvis"));
	if (PhysicalAnimation && Body->GetPhysicsAsset() && Body->GetBoneIndex(Pelvis) != INDEX_NONE && GetNetMode() != NM_DedicatedServer)
	{
		PhysicalAnimation->SetSkeletalMeshComponent(Body);
		FPhysicalAnimationData Drive;
		Drive.bIsLocalSimulation = true;
		Drive.OrientationStrength = 1500.f;
		Drive.AngularVelocityStrength = 130.f;
		Drive.PositionStrength = 0.f;
		Drive.VelocityStrength = 0.f;
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Pelvis, Drive, true);
		FPhysicalAnimationData Legs = Drive;
		Legs.OrientationStrength = 380.f;
		Legs.AngularVelocityStrength = 45.f;
		for (const TCHAR* Thigh : { TEXT("thigh_l"), TEXT("thigh_r") })
		{
			if (Body->GetBoneIndex(Thigh) != INDEX_NONE)
			{
				PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Thigh, Legs, true);
			}
		}
		// A blast leaves nothing to hold the body up.
		DeathDriveStrength = DeathImpulse > 0.f ? 0.12f : (bHeadshot ? 0.3f : 1.f);
		DeathDrive = 1.f;
		PhysicalAnimation->SetStrengthMultiplyer(DeathDriveStrength);
		if (UCampusTPAnimInstance* TP = GetBodyAnim())
		{
			const bool bFromBehind = FVector::DotProduct(FVector(DeathDirection).GetSafeNormal2D(), GetActorForwardVector()) > 0.3f;
			TP->PlayUpper(CampusAssets::Anim(bFromBehind ? CampusAssets::EAnim::HitBack : CampusAssets::EAnim::HitFrontHeavy), 0.f, 0.03f, 0.3f);
		}
	}
	if (Body->GetBoneIndex(ImpulseBone) != INDEX_NONE)
	{
		Body->AddImpulse(FVector(DeathDirection) * (bHeadshot ? 700.f : 560.f) + FVector(0.f, 0.f, 60.f), ImpulseBone, true);
		// Carry some of the run speed so sprinting kills fall forward instead of dropping on the spot.
		Body->SetAllPhysicsLinearVelocity(GetVelocity() * 0.6f, true);
		if (DeathImpulse > 0.f)
		{
			// Thrown by the blast: the whole body leaves the ground and tumbles.
			Body->SetAllPhysicsLinearVelocity(FVector(DeathDirection) * DeathImpulse + FVector(0.f, 0.f, DeathImpulse * 0.3f), true);
			Body->SetAllPhysicsAngularVelocityInDegrees(FMath::VRand() * 240.f, true);
		}
	}
	GetCharacterMovement()->DisableMovement();
}

void ACampusCharacter::UpdateDeathCrumple(float DeltaSeconds)
{
	if (DeathDrive <= 0.f || !PhysicalAnimation)
	{
		return;
	}
	DeathDrive = FMath::Max(0.f, DeathDrive - DeltaSeconds / 0.55f);
	PhysicalAnimation->SetStrengthMultiplyer(DeathDriveStrength * DeathDrive * DeathDrive);
}
