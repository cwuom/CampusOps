#include "Weapons/CampusThrowable.h"
#include "Weapons/CampusFX.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusPlayerController.h"
#include "Player/CampusViewmodelComponent.h"
#include "Game/CampusGameModeBase.h"
#include "Map/CampusWindowGlass.h"
#include "Map/CampusBreakableGlass.h"
#include "CampusAssets.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

// ------------------------------------------------------------------ data

namespace CampusThrowables
{
	float Fuse(ECampusThrowable Type)
	{
		switch (Type)
		{
		case ECampusThrowable::Frag: return 3.6f;
		case ECampusThrowable::Thermite: return 2.5f;
		case ECampusThrowable::Flash: return 1.4f;
		default: return 1.3f;
		}
	}

	float ThrowSpeed(ECampusThrowable Type)
	{
		return (Type == ECampusThrowable::Frag || Type == ECampusThrowable::Flash) ? 1700.f : 1550.f;
	}

	bool IsLethal(ECampusThrowable Type)
	{
		return Type == ECampusThrowable::Frag || Type == ECampusThrowable::Thermite;
	}

	static float CollisionRadius(ECampusThrowable Type)
	{
		return Type == ECampusThrowable::Flash ? 2.4f : 3.3f;
	}

	/** Top of the body, where the fuse screws in (matches the weapon kit models). */
	static float FuseBase(ECampusThrowable Type)
	{
		switch (Type)
		{
		case ECampusThrowable::Frag: return 3.25f;
		case ECampusThrowable::Flash: return 6.f;
		case ECampusThrowable::Smoke: return 7.2f;
		default: return 7.f;
		}
	}

	static const TCHAR* ModelName(ECampusThrowable Type)
	{
		switch (Type)
		{
		case ECampusThrowable::Frag: return TEXT("Frag");
		case ECampusThrowable::Thermite: return TEXT("Thermite");
		case ECampusThrowable::Flash: return TEXT("Flash");
		default: return TEXT("Smoke");
		}
	}

	UStaticMesh* BodyMesh(ECampusThrowable Type)
	{
		return CampusAssets::WeaponMesh(TEXT("Throwables"), FString::Printf(TEXT("SM_Throw_%s"), ModelName(Type)));
	}

	UStaticMesh* LeverMesh(ECampusThrowable Type)
	{
		return CampusAssets::WeaponMesh(TEXT("Throwables"), Type == ECampusThrowable::Frag ? TEXT("SM_Throw_LeverFrag") : (Type == ECampusThrowable::Flash ? TEXT("SM_Throw_LeverFlash") : TEXT("SM_Throw_Lever")));
	}

	UStaticMesh* RingMesh()
	{
		return CampusAssets::WeaponMesh(TEXT("Throwables"), TEXT("SM_Throw_Ring"));
	}

	void BuildModel(AActor* Owner, USceneComponent* Parent, ECampusThrowable Type, bool bLever, bool bRing, bool bFirstPerson, TArray<TObjectPtr<UStaticMeshComponent>>& OutParts)
	{
		auto Add = [&](UStaticMesh* Mesh) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
			C->SetupAttachment(Parent);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->bReceivesDecals = false;
			if (bFirstPerson)
			{
				C->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
				C->SetOnlyOwnerSee(true);
				C->SetCastShadow(false);
			}
			C->SetStaticMesh(Mesh);
			C->RegisterComponent();
			OutParts.Add(C);
			return C;
		};
		if (UStaticMesh* Body = BodyMesh(Type))
		{
			Add(Body);
			if (bLever)
			{
				if (UStaticMesh* Lever = LeverMesh(Type))
				{
					Add(Lever)->ComponentTags.Add(TEXT("Lever"));
				}
			}
			if (bRing)
			{
				if (UStaticMesh* Ring = RingMesh())
				{
					UStaticMeshComponent* C = Add(Ring);
					C->ComponentTags.Add(TEXT("Ring"));
					C->SetRelativeLocation(FVector(FuseBase(Type) + 0.75f, 0.f, 0.f));
				}
			}
			return;
		}
		// Kit not built yet: engine primitives in roughly the right shape and colour.
		UE_LOG(LogTemp, Warning, TEXT("[CampusThrowable] model SM_Throw_%s missing, using a primitive"), ModelName(Type));
		const bool bRound = Type == ECampusThrowable::Frag;
		UStaticMeshComponent* C = Add(CampusAssets::Shape(bRound ? ECampusShape::Sphere : ECampusShape::Cylinder));
		const FLinearColor Col = Type == ECampusThrowable::Frag ? FLinearColor(0.07f, 0.08f, 0.04f) : (Type == ECampusThrowable::Flash ? FLinearColor(0.05f, 0.05f, 0.05f) : FLinearColor(0.16f, 0.17f, 0.15f));
		C->SetMaterial(0, CampusAssets::MakeLitMID(Owner, Col, 0.6f, 0.2f));
		C->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		C->SetRelativeScale3D(bRound ? FVector(0.064f) : (Type == ECampusThrowable::Flash ? FVector(0.044f, 0.044f, 0.13f) : FVector(0.064f, 0.064f, 0.145f)));
	}

	void SpawnSpoon(UWorld* World, ECampusThrowable Type, const FTransform& At, const FVector& Velocity)
	{
		UStaticMesh* Lever = LeverMesh(Type);
		if (!World || !Lever || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusSpoon* Spoon = World->SpawnActor<ACampusSpoon>(ACampusSpoon::StaticClass(), At, Params))
		{
			Spoon->Init(Lever, Velocity);
		}
	}

	float Radius(ECampusThrowable Type)
	{
		return Type == ECampusThrowable::Flash ? 2.3f : 3.2f;
	}

	void SeatInLeftHand(USkeletalMeshComponent* Mesh, USceneComponent* Root, ECampusThrowable Type, const FTransform* Extra)
	{
		if (!Mesh || !Root || Mesh->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE)
		{
			return;
		}
		// Sat in the curl of the fingers: the fingertips curl toward the palm, so the palm side is where they point.
		const FVector Hand = Mesh->GetBoneLocation(TEXT("hand_l"));
		const bool bFingers = Mesh->GetBoneIndex(TEXT("middle_01_l")) != INDEX_NONE && Mesh->GetBoneIndex(TEXT("middle_03_l")) != INDEX_NONE
			&& Mesh->GetBoneIndex(TEXT("index_01_l")) != INDEX_NONE && Mesh->GetBoneIndex(TEXT("pinky_01_l")) != INDEX_NONE;
		FVector Mid = Hand + Mesh->GetComponentQuat().GetForwardVector() * 8.f;
		FVector Palm = -Mesh->GetComponentQuat().GetUpVector();
		FVector Across = Mesh->GetComponentQuat().GetRightVector();
		if (bFingers)
		{
			Mid = Mesh->GetBoneLocation(TEXT("middle_01_l"));
			const FVector Fwd = (Mid - Hand).GetSafeNormal();
			const FVector Curl = Mesh->GetBoneLocation(TEXT("middle_03_l")) - Mid;
			Palm = (Curl - Fwd * (Curl | Fwd)).GetSafeNormal();
			Across = (Mesh->GetBoneLocation(TEXT("index_01_l")) - Mesh->GetBoneLocation(TEXT("pinky_01_l"))).GetSafeNormal();
		}
		const float R = Radius(Type);
		const FVector Center = FMath::Lerp(Hand, Mid, 0.75f) + Palm * (R + 0.4f) + (Extra ? Extra->GetLocation() : FVector::ZeroVector);
		// The fuse toward the thumb, the lever under the fingers.
		FQuat Q = FRotationMatrix::MakeFromXZ(Across, -Palm).ToQuat();
		if (Extra)
		{
			Q = Q * Extra->GetRotation();
		}
		const FTransform BoneT = Mesh->GetSocketTransform(TEXT("hand_l"));
		Root->AttachToComponent(Mesh, FAttachmentTransformRules::KeepWorldTransform, TEXT("hand_l"));
		Root->SetRelativeTransform(FTransform(Q, Center).GetRelativeTransform(BoneT));
	}
}

// ------------------------------------------------------------------ spoon

ACampusSpoon::ACampusSpoon()
{
	SetReplicates(false);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Ignore);
	Mesh->SetCastShadow(false);
	Mesh->BodyInstance.bUseCCD = true;
	Mesh->SetNotifyRigidBodyCollision(true);
	InitialLifeSpan = 8.f;
}

void ACampusSpoon::Init(UStaticMesh* InMesh, const FVector& Velocity)
{
	Mesh->SetStaticMesh(InMesh);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetMassOverrideInKg(NAME_None, 0.015f, true);
	Mesh->SetPhysicsLinearVelocity(Velocity);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector(FMath::FRandRange(-1400.f, 1400.f), FMath::FRandRange(-2400.f, 2400.f), FMath::FRandRange(-1400.f, 1400.f)));
	Mesh->OnComponentHit.AddDynamic(this, &ACampusSpoon::OnSpoonHit);
}

void ACampusSpoon::OnSpoonHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (Bounces < 2 && GetVelocity().Size() > 80.f)
	{
		++Bounces;
		CampusAudio::PlayAt(this, TEXT("Shell_Clink"), GetActorLocation(), 0.25f / Bounces, 0.25f, CampusAssets::EAttenuation::Foley);
	}
}

// ------------------------------------------------------------------ actor

ACampusThrowable::ACampusThrowable()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicatingMovement(true);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(60.f);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
}

void ACampusThrowable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(ACampusThrowable, Type, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME(ACampusThrowable, bDetonated);
	DOREPLIFETIME(ACampusThrowable, DetonationPoint);
	DOREPLIFETIME(ACampusThrowable, DetonationNormal);
	DOREPLIFETIME(ACampusThrowable, FuseRemaining);
}

void ACampusThrowable::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	GlassCheckFrom = GetActorLocation();
}

void ACampusThrowable::Launch(ECampusThrowable InType, ACampusCharacter* InThrower, const FVector& InVelocity, float FuseLeft)
{
	Type = InType;
	Thrower = InThrower;
	InstigatorController = InThrower ? InThrower->GetController() : nullptr;
	Velocity = InVelocity;
	FuseRemaining = FuseLeft;
	Spin = FMath::VRand() * FMath::FRandRange(500.f, 900.f);
	GlassCheckFrom = GetActorLocation();
	BuildVisuals();
}

void ACampusThrowable::OnRep_Type()
{
	BuildVisuals();
}

void ACampusThrowable::BuildVisuals()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (UStaticMeshComponent* P : Parts)
	{
		if (P)
		{
			P->DestroyComponent();
		}
	}
	Parts.Reset();
	CampusThrowables::BuildModel(this, Root, Type, false, false, false, Parts);
	if (bDetonated)
	{
		OnRep_Detonated();
	}
}

void ACampusThrowable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Whatever is already in the air drifts off on its own.
	if (Plume)
	{
		Plume->SetLifeSpan(14.f);
	}
	if (Sparks)
	{
		Sparks->SetLifeSpan(1.5f);
	}
	if (Loop)
	{
		Loop->FadeOut(0.4f, 0.f);
	}
	Super::EndPlay(EndPlayReason);
}

float ACampusThrowable::EffectAge() const
{
	return bDetonated ? float(GetWorld()->GetTimeSeconds() - DetonatedAt) : 0.f;
}

float ACampusThrowable::GetSmokeRadius() const
{
	if (Type != ECampusThrowable::Smoke || !bDetonated)
	{
		return 0.f;
	}
	const float T = EffectAge();
	return T > 24.f ? 0.f : 560.f * FMath::SmoothStep(0.f, 3.2f, T) + 40.f * FMath::SmoothStep(14.f, 22.f, T);
}

float ACampusThrowable::GetSmokeDensity() const
{
	if (Type != ECampusThrowable::Smoke || !bDetonated)
	{
		return 0.f;
	}
	const float T = EffectAge();
	return FMath::SmoothStep(0.f, 2.2f, T) * (1.f - FMath::SmoothStep(15.f, 22.f, T));
}

float ACampusThrowable::SmokeOcclusion(const UWorld* World, const FVector& From, const FVector& To)
{
	if (!World)
	{
		return 0.f;
	}
	const FVector D = To - From;
	const float L = D.Size();
	if (L < 1.f)
	{
		return 0.f;
	}
	const FVector Dir = D / L;
	float Total = 0.f;
	for (TActorIterator<ACampusThrowable> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const float R = It->GetSmokeRadius() * 0.92f;
		const float Density = It->GetSmokeDensity();
		if (R < 50.f || Density < 0.05f)
		{
			continue;
		}
		// The cloud sits on the ground: a flattened sphere centred a little above the canister.
		FVector C = It->GetDetonationPoint() + FVector(0.f, 0.f, R * 0.32f);
		FVector A = From - C;
		FVector B = Dir;
		A.Z /= 0.78f;
		B.Z /= 0.78f;
		const float BB = B.SizeSquared();
		const float T = -(A | B) / BB;
		const float Perp2 = (A + B * T).SizeSquared();
		if (Perp2 >= R * R)
		{
			continue;
		}
		const float Half = FMath::Sqrt((R * R - Perp2) / BB);
		const float T0 = FMath::Max(T - Half, 0.f);
		const float T1 = FMath::Min(T + Half, L);
		if (T1 > T0)
		{
			// Looking through the middle of a full cloud is opaque; skimming its edge only veils.
			Total += (T1 - T0) / (R * 1.1f) * Density;
		}
	}
	return FMath::Clamp(Total, 0.f, 1.f);
}

void ACampusThrowable::GetLiveFrags(const UWorld* World, TArray<ACampusThrowable*>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ACampusThrowable> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->Type == ECampusThrowable::Frag && !It->bDetonated)
		{
			Out.Add(*It);
		}
	}
}

// ------------------------------------------------------------------ flight

void ACampusThrowable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	if (!bDetonated)
	{
		FuseRemaining = FMath::Max(0.f, FuseRemaining - Dt);
		if (HasAuthority())
		{
			Simulate(Dt);
			if (FuseRemaining <= 0.f)
			{
				Detonate();
			}
		}
		const FVector Now = GetActorLocation();
		if (!Now.Equals(GlassCheckFrom, 0.5f))
		{
			CheckGlass(GlassCheckFrom, Now);
			GlassCheckFrom = Now;
		}
		return;
	}
	if (HasAuthority() && Type == ECampusThrowable::Thermite)
	{
		TickThermiteDamage(Dt);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (Type == ECampusThrowable::Smoke)
		{
			TickSmoke(Dt);
		}
		else if (Type == ECampusThrowable::Thermite)
		{
			TickThermite(Dt);
		}
	}
}

void ACampusThrowable::Simulate(float Dt)
{
	FlightTime += Dt;
	if (bStuck)
	{
		return;
	}
	if (bResting)
	{
		// Lying on its side once it has stopped rolling.
		const FRotator Rest(0.f, GetActorRotation().Yaw, GetActorRotation().Roll);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), Rest, Dt, 10.f));
		return;
	}
	Velocity.Z -= 980.f * Dt;
	Velocity *= FMath::Exp(-0.1f * Dt);
	FVector Loc = GetActorLocation();
	FVector Move = Velocity * Dt;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusThrowable), false, this);
	if (FlightTime < 0.4f && Thrower.IsValid())
	{
		Params.AddIgnoredActor(Thrower.Get());
	}
	const float Radius = CampusThrowables::CollisionRadius(Type);
	bool bGround = false;
	for (int32 Iteration = 0; Iteration < 4 && !Move.IsNearlyZero(0.01f); ++Iteration)
	{
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, Loc, Loc + Move, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Radius), Params))
		{
			Loc += Move;
			break;
		}
		if (Hit.bStartPenetrating)
		{
			Loc += Hit.Normal * (Hit.PenetrationDepth + 0.2f);
			continue;
		}
		Loc = Hit.Location + Hit.Normal * 0.1f;
		const float Remaining = 1.f - Hit.Time;
		const float Vn = Velocity | Hit.Normal;
		if (Vn < 0.f)
		{
			SetActorLocation(Loc);
			OnImpact(Hit, Velocity.Size());
			if (bStuck || bDetonated)
			{
				return;
			}
			const FVector VN = Hit.Normal * Vn;
			const FVector VT = Velocity - VN;
			// Steel on concrete: a lively first bounce, then it skitters and rolls.
			const float Restitution = Type == ECampusThrowable::Frag ? 0.36f : 0.28f;
			Velocity = VT * 0.72f - VN * Restitution;
			Spin = FMath::VRand() * FMath::Min(Velocity.Size() * 2.2f, 1100.f);
		}
		bGround |= Hit.Normal.Z > 0.7f;
		Move = Velocity * Dt * Remaining;
	}
	if (bGround)
	{
		const float Friction = FMath::Exp(-2.6f * Dt);
		Velocity.X *= Friction;
		Velocity.Y *= Friction;
		if (Velocity.Size() < 28.f)
		{
			bResting = true;
			Velocity = FVector::ZeroVector;
		}
	}
	SetActorLocation(Loc);
	if (!bResting && !Spin.IsNearlyZero())
	{
		AddActorWorldRotation(FQuat(Spin.GetSafeNormal(), FMath::DegreesToRadians(Spin.Size() * Dt)));
	}
}

void ACampusThrowable::OnImpact(const FHitResult& Hit, float Speed)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Speed > 110.f && Now - LastBounceTime > 0.07f)
	{
		LastBounceTime = (float)Now;
		MulticastBounce(Hit.ImpactPoint, FMath::Clamp(Speed / 1300.f, 0.12f, 1.f), (uint8)CampusFX::ResolveSurface(Hit));
	}
	if (Type != ECampusThrowable::Thermite || bStuck)
	{
		return;
	}
	// Thermite sticks wherever it lands and lights at once.
	bStuck = true;
	Velocity = FVector::ZeroVector;
	SetActorRotation(FRotationMatrix::MakeFromX(Hit.ImpactNormal).Rotator());
	if (ACampusCharacter* Victim = Cast<ACampusCharacter>(Hit.GetActor()))
	{
		StuckToCharacter = Victim;
		if (USkeletalMeshComponent* Body = Victim->GetMesh())
		{
			AttachToComponent(Body, FAttachmentTransformRules::KeepWorldTransform, Hit.BoneName);
		}
	}
	else if (Hit.GetComponent() && Hit.GetComponent()->Mobility == EComponentMobility::Movable)
	{
		AttachToComponent(Hit.GetComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}
	Detonate();
}

void ACampusThrowable::CheckGlass(const FVector& From, const FVector& To)
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusThrowableGlass), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, From, To, CAMPUS_TRACE_WEAPON, Params) && UCampusBreakableGlass::IsGlass(Hit.GetComponent()))
	{
		// Punches a ragged hole through the pane and carries on, slowed.
		UCampusBreakableGlass::SmashAt(GetWorld(), Hit.ImpactPoint, (To - From).GetSafeNormal(), 34.f);
		Velocity *= 0.7f;
	}
}

void ACampusThrowable::MulticastBounce_Implementation(FVector_NetQuantize Location, float Strength, uint8 Surface)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const ECampusSurface S = (ECampusSurface)Surface;
	const FName Sound = S == ECampusSurface::Dirt ? FName(TEXT("Grenade_Bounce_Soft")) : FName(TEXT("Grenade_Bounce"));
	CampusAudio::PlayAt(this, Sound, Location, 0.25f + 0.75f * Strength, 0.08f, CampusAssets::EAttenuation::Impact);
}

// ------------------------------------------------------------------ detonation (server)

void ACampusThrowable::Detonate()
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	FuseRemaining = 0.f;
	DetonationPoint = GetActorLocation();
	FVector Normal = FVector::UpVector;
	if (Type == ECampusThrowable::Thermite && bStuck)
	{
		Normal = GetActorForwardVector();
	}
	else
	{
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusThrowableGround), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Ground, DetonationPoint + FVector(0.f, 0.f, 10.f), DetonationPoint - FVector(0.f, 0.f, 70.f), ECC_Visibility, Params))
		{
			Normal = Ground.ImpactNormal;
			DetonationPoint = Ground.ImpactPoint + Normal * 3.f;
		}
	}
	DetonationNormal = Normal;
	switch (Type)
	{
	case ECampusThrowable::Frag: ApplyFragDamage(); SetLifeSpan(1.5f); break;
	case ECampusThrowable::Flash: ApplyFlash(); SetLifeSpan(1.5f); break;
	case ECampusThrowable::Smoke: SetLifeSpan(25.f); break;
	default: SetLifeSpan(9.f); break;
	}
	if (ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>())
	{
		if (Type == ECampusThrowable::Frag || Type == ECampusThrowable::Flash)
		{
			GM->NotifyGunshot(Thrower.Get(), DetonationPoint, false);
		}
	}
	ForceNetUpdate();
	OnRep_Detonated();
}

namespace
{
	/** Fraction (0..1) of the head, chest and pelvis that the blast at From can reach. */
	float Exposure(UWorld* World, const FVector& From, ACampusCharacter* Victim, const AActor* Ignore)
	{
		USkeletalMeshComponent* Body = Victim->GetMesh();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBlastLOS), false, Ignore);
		int32 Seen = 0;
		for (const TCHAR* Bone : { TEXT("head"), TEXT("spine_03"), TEXT("pelvis") })
		{
			const FVector P = Body && Body->GetBoneIndex(Bone) != INDEX_NONE ? Body->GetBoneLocation(Bone) : Victim->GetActorLocation();
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, From, P, ECC_Visibility, Params) || Hit.GetActor() == Victim)
			{
				++Seen;
			}
		}
		return Seen / 3.f;
	}

	void BreakWindowsAround(UWorld* World, const FVector& Center, float Radius, const AActor* Ignore)
	{
		UCampusBreakableGlass::BlastAt(World, Center, Radius, Ignore);
	}
}

void ACampusThrowable::ApplyFragDamage()
{
	UWorld* World = GetWorld();
	const FVector C = DetonationPoint + FVector(DetonationNormal) * 12.f;
	// M67: lethal within ~5 m in the open, wounding out to ~15 m; here lethal to 2.9 m, hurting to 9 m.
	const float Radius = 900.f;
	const float Lethal = 290.f;
	for (TActorIterator<ACampusCharacter> It(World); It; ++It)
	{
		ACampusCharacter* Victim = *It;
		if (!Victim->IsAlive())
		{
			continue;
		}
		const FVector Chest = Victim->GetMesh() && Victim->GetMesh()->GetBoneIndex(TEXT("spine_03")) != INDEX_NONE ? Victim->GetMesh()->GetBoneLocation(TEXT("spine_03")) : Victim->GetActorLocation();
		const float D = FVector::Dist(C, Chest);
		if (D > Radius)
		{
			continue;
		}
		const float Seen = Exposure(World, C, Victim, this);
		if (Seen <= 0.f)
		{
			continue;
		}
		float Damage = D < Lethal ? 250.f : FMath::Lerp(140.f, 10.f, FMath::Pow((D - Lethal) / (Radius - Lethal), 0.75f));
		Damage *= FMath::Lerp(0.5f, 1.f, Seen);
		const FVector Dir = (Chest - C + FVector(0.f, 0.f, 45.f)).GetSafeNormal();
		Victim->NextDeathImpulse = FMath::Lerp(1500.f, 500.f, D / Radius);
		Victim->ApplyCampusDamage(Damage, ECampusHitZone::UpperTorso, InstigatorController.Get(), this, TEXT("Frag"), Dir, TEXT("spine_03"));
		Victim->NextDeathImpulse = 0.f;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		BreakWindowsAround(World, C, 650.f, this);
	}
}

void ACampusThrowable::ApplyFlash()
{
	UWorld* World = GetWorld();
	const FVector C = DetonationPoint + FVector(DetonationNormal) * 8.f;
	const float Range = 2400.f;
	ACampusPlayerController* ThrowerPC = Cast<ACampusPlayerController>(InstigatorController.Get());
	float Stunned = 0.f;
	for (TActorIterator<ACampusCharacter> It(World); It; ++It)
	{
		ACampusCharacter* Victim = *It;
		if (!Victim->IsAlive())
		{
			continue;
		}
		const FVector Eye = Victim->GetPawnViewLocation();
		const float D = FVector::Dist(Eye, C);
		if (D > Range)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusFlashLOS), false, this);
		Params.AddIgnoredActor(Victim);
		FHitResult Hit;
		const bool bSeen = !World->LineTraceSingleByChannel(Hit, C, Eye, ECC_Visibility, Params);
		// 6-8 million candela: blinding when looked at, still dazzling off to the side, a white wash even from behind.
		const float Facing = Victim->GetAimDirection() | (C - Eye).GetSafeNormal();
		const float Angle = FMath::GetMappedRangeValueClamped(FVector2D(-0.5f, 0.92f), FVector2D(0.14f, 1.f), Facing);
		const float Near = FMath::Pow(1.f - FMath::Clamp((D - 250.f) / (Range - 250.f), 0.f, 1.f), 1.25f);
		const float Blind = bSeen ? FMath::Clamp(Near * Angle * 1.15f, 0.f, 1.f) : 0.f;
		// 170 dB at a metre and a half: it deafens through walls.
		const float Deaf = FMath::Max(FMath::Clamp(1.f - D / 1300.f, 0.f, 1.f), Blind * 0.6f);
		if (Blind < 0.04f && Deaf < 0.05f)
		{
			continue;
		}
		Victim->ApplyFlash(Blind, Deaf, C);
		if (ThrowerPC && Victim->GetController() != ThrowerPC && Thrower.IsValid() && Thrower->IsEnemyOf(Victim))
		{
			Stunned = FMath::Max(Stunned, FMath::Max(Blind, Deaf * 0.6f));
		}
	}
	// A tactical that catches an enemy confirms like a hit, once per bang.
	if (Stunned > 0.2f)
	{
		ThrowerPC->ClientEquipmentHit(15.f + 25.f * Stunned);
	}
}

void ACampusThrowable::TickThermiteDamage(float Dt)
{
	const float Burn = 5.5f;
	if (EffectAge() > Burn)
	{
		return;
	}
	DamageTick -= Dt;
	if (DamageTick > 0.f)
	{
		return;
	}
	const float Step = 0.15f;
	DamageTick = Step;
	UWorld* World = GetWorld();
	const FVector C = GetActorLocation() + GetActorForwardVector() * 12.f;
	for (TActorIterator<ACampusCharacter> It(World); It; ++It)
	{
		ACampusCharacter* Victim = *It;
		if (!Victim->IsAlive())
		{
			continue;
		}
		float Dps = 0.f;
		if (StuckToCharacter.Get() == Victim)
		{
			Dps = 150.f;
		}
		else
		{
			const float D = FVector::Dist(C, Victim->GetActorLocation());
			if (D > 300.f || Exposure(World, C, Victim, this) <= 0.f)
			{
				continue;
			}
			Dps = D < 110.f ? 85.f : FMath::Lerp(55.f, 10.f, (D - 110.f) / 190.f);
		}
		Victim->ApplyCampusDamage(Dps * Step, ECampusHitZone::LowerTorso, InstigatorController.Get(), this, TEXT("Thermite"), (Victim->GetActorLocation() - C).GetSafeNormal(), TEXT("spine_01"));
	}
}

// ------------------------------------------------------------------ effects (clients)

void ACampusThrowable::OnRep_Detonated()
{
	if (!bDetonated || bFXPlayed)
	{
		return;
	}
	bFXPlayed = true;
	DetonatedAt = GetWorld()->GetTimeSeconds();
	if (GetNetMode() != NM_DedicatedServer)
	{
		PlayDetonationFX();
	}
}

void ACampusThrowable::ShakeLocalViewer(float Radius, float Strength, bool bDeafen)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACampusCharacter* Me = PC ? PC->GetPawn<ACampusCharacter>() : nullptr;
	if (!Me || !Me->IsLocalHuman() || !Me->IsAlive())
	{
		return;
	}
	const FVector C = DetonationPoint;
	const FVector Eye = Me->GetPawnViewLocation();
	const float D = FVector::Dist(Eye, C);
	if (D > Radius)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusShakeLOS), false, this);
	Params.AddIgnoredActor(Me);
	FHitResult Hit;
	const bool bOpen = !GetWorld()->LineTraceSingleByChannel(Hit, C + FVector(DetonationNormal) * 10.f, Eye, ECC_Visibility, Params);
	const float K = FMath::Pow(1.f - D / Radius, 1.4f) * Strength * (bOpen ? 1.f : 0.55f);
	if (Me->Viewmodel)
	{
		Me->Viewmodel->OnExplosion(K, C);
	}
	if (bDeafen)
	{
		if (ACampusPlayerController* CPC = Cast<ACampusPlayerController>(PC))
		{
			const float Deaf = FMath::Clamp(1.f - D / 750.f, 0.f, 1.f) * (bOpen ? 1.f : 0.6f);
			if (Deaf > 0.05f)
			{
				CPC->OnDeafened(Deaf * 0.8f, false);
			}
		}
	}
}

void ACampusThrowable::PlayDetonationFX()
{
	switch (Type)
	{
	case ECampusThrowable::Frag:
		PlayFragFX();
		break;
	case ECampusThrowable::Flash:
		PlayFlashFX();
		break;
	case ECampusThrowable::Smoke:
	{
		// The fuse pops, and the canister starts pouring smoke out of its top holes with a fierce hiss.
		CampusAudio::PlayAt(this, TEXT("Smoke_Pop"), GetActorLocation(), 0.9f, 0.05f, CampusAssets::EAttenuation::Impact);
		if (USoundBase* Hiss = CampusAssets::Sound(TEXT("Smoke_Hiss")))
		{
			Loop = UGameplayStatics::SpawnSoundAttached(Hiss, Root, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, CampusAudio::CategoryVolume(CampusAudio::ECategory::Effects), 1.f, 0.f, CampusAssets::Attenuation(CampusAssets::EAttenuation::Impact));
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Plume = GetWorld()->SpawnActor<ACampusSmokeTrail>(ACampusSmokeTrail::StaticClass(), FTransform(DetonationPoint), Params);
		if (Plume)
		{
			// Dense white-grey screening smoke: heavy, slow to rise, rolling outward along the ground.
			Plume->SetStyle(FLinearColor(0.56f, 0.57f, 0.58f), 7.f, 0.55f, 26.f, 260);
		}
		Fog = NewObject<ULocalFogVolumeComponent>(this);
		Fog->SetupAttachment(Root);
		Fog->SetUsingAbsoluteLocation(true);
		Fog->SetUsingAbsoluteRotation(true);
		Fog->SetUsingAbsoluteScale(true);
		Fog->RegisterComponent();
		Fog->SetWorldLocation(DetonationPoint);
		Fog->SetWorldScale3D(FVector(0.05f));
		Fog->SetRadialFogExtinction(0.f);
		Fog->SetHeightFogExtinction(0.f);
		// Grey, fairly absorbing and scattering broadly: it dims what is behind it rather than glowing with every lamp.
		Fog->SetFogAlbedo(FLinearColor(0.62f, 0.62f, 0.63f));
		Fog->SetFogPhaseG(0.1f);
		break;
	}
	default:
	{
		// Thermite: a white-hot jet of molten iron, sparks raining off it, and a column of white smoke.
		CampusAudio::PlayAt(this, TEXT("Thermite_Ignite"), GetActorLocation(), 1.f, 0.05f, CampusAssets::EAttenuation::Impact);
		if (USoundBase* Burn = CampusAssets::Sound(TEXT("Thermite_Burn")))
		{
			Loop = UGameplayStatics::SpawnSoundAttached(Burn, Root, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, CampusAudio::CategoryVolume(CampusAudio::ECategory::Effects), 1.f, 0.f, CampusAssets::Attenuation(CampusAssets::EAttenuation::Impact));
		}
		Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(Root);
		Light->RegisterComponent();
		Light->SetRelativeLocation(FVector(18.f, 0.f, 0.f));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetLightColor(FLinearColor(1.f, 0.74f, 0.46f));
		Light->SetAttenuationRadius(1600.f);
		Light->SetSourceRadius(6.f);
		Light->SetCastShadows(true);
		Light->SetIntensity(0.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Sparks = GetWorld()->SpawnActor<ACampusSparkEmitter>(ACampusSparkEmitter::StaticClass(), FTransform(DetonationPoint), Params);
		if (Sparks)
		{
			FHitResult Floor;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(CampusThermiteFloor), false, this);
			Sparks->SetFloor(GetWorld()->LineTraceSingleByChannel(Floor, DetonationPoint, DetonationPoint - FVector(0.f, 0.f, 400.f), ECC_Visibility, Q) ? Floor.ImpactPoint.Z + 0.5f : DetonationPoint.Z - 400.f);
		}
		Plume = GetWorld()->SpawnActor<ACampusSmokeTrail>(ACampusSmokeTrail::StaticClass(), FTransform(DetonationPoint), Params);
		if (Plume)
		{
			Plume->SetStyle(FLinearColor(0.78f, 0.77f, 0.74f), 110.f, 1.3f, 55.f, 200);
		}
		// The core: a small white-hot glow on the canister mouth.
		UStaticMeshComponent* Core = NewObject<UStaticMeshComponent>(this);
		Core->SetupAttachment(Root);
		Core->SetStaticMesh(CampusAssets::Shape(ECampusShape::Sphere));
		Core->SetMaterial(0, CampusAssets::MakeGlowMID(this, FLinearColor(1.f, 0.72f, 0.38f), 60.f, true));
		Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Core->SetCastShadow(false);
		Core->SetRelativeLocation(FVector(9.f, 0.f, 0.f));
		Core->SetRelativeScale3D(FVector(0.2f));
		Core->ComponentTags.Add(TEXT("Core"));
		Core->RegisterComponent();
		Parts.Add(Core);
		CampusFX::SpawnScorch(GetWorld(), DetonationPoint, DetonationNormal, 150.f, 1.f, 60.f);
		CampusFX::SpawnFlashLight(GetWorld(), DetonationPoint + FVector(DetonationNormal) * 30.f, FLinearColor(1.f, 0.8f, 0.55f), 600000.f, 1600.f, 0.25f);
		break;
	}
	}
}

void ACampusThrowable::PlayFragFX()
{
	UWorld* World = GetWorld();
	const FVector N = DetonationNormal;
	const FVector C = DetonationPoint + N * 15.f;
	for (UStaticMeshComponent* P : Parts)
	{
		if (P)
		{
			P->SetVisibility(false);
		}
	}
	CampusFX::SpawnExplosion(World, DetonationPoint, N, 1.f, this);
	CampusAudio::PlayAt(this, TEXT("Frag_Explode"), C, 1.f, 0.04f, CampusAssets::EAttenuation::Gunshot);
	CampusAudio::PlayAt(this, TEXT("Explosion_Debris"), C, 0.7f, 0.08f, CampusAssets::EAttenuation::Impact);
	ShakeLocalViewer(2200.f, 1.f, true);
	BreakWindowsAround(World, C, 650.f, this);
}

void ACampusThrowable::PlayFlashFX()
{
	UWorld* World = GetWorld();
	const FVector N = DetonationNormal;
	const FVector C = DetonationPoint + N * 10.f;
	for (UStaticMeshComponent* P : Parts)
	{
		if (P)
		{
			P->SetVisibility(false);
		}
	}
	CampusFX::SpawnFlashLight(World, C + N * 20.f, FLinearColor(0.92f, 0.95f, 1.f), 7000000.f, 3200.f, 0.14f, true);
	CampusFX::SpawnFlashLight(World, C + N * 20.f, FLinearColor(1.f, 0.85f, 0.65f), 120000.f, 900.f, 0.7f);
	CampusFX::SpawnFireball(World, C, N, 6, 55.f, 0.1f, 60.f);
	CampusFX::SpawnDebris(World, C, N, 26, FLinearColor(1.f, 0.95f, 0.82f), 70.f, 1700.f, 0.6f, 0.22f, 400.f, 90.f);
	CampusFX::SpawnSprites(World, C, N, 7, FLinearColor(0.78f, 0.78f, 0.77f), 0.45f, 240.f, 30.f, 170.f, 2.8f, 80.f, 25.f);
	CampusFX::SpawnScorch(World, DetonationPoint, N, 70.f, 0.7f, 30.f);
	CampusAudio::PlayAt(this, TEXT("Flash_Bang"), C, 1.f, 0.03f, CampusAssets::EAttenuation::Gunshot);
	ShakeLocalViewer(1000.f, 0.45f, false);
}

void ACampusThrowable::TickSmoke(float Dt)
{
	const float T = EffectAge();
	const float R = GetSmokeRadius();
	const float Density = GetSmokeDensity();
	if (Fog)
	{
		// A flattened dome on the ground: the local fog volume is the bulk of the screen, sprites give it texture.
		const float FogR = FMath::Max(R, 20.f);
		Fog->SetWorldLocation(DetonationPoint + FVector(0.f, 0.f, FogR * 0.3f));
		Fog->SetWorldScale3D(FVector(FogR / ULocalFogVolumeComponent::GetBaseVolumeSize()) * FVector(1.f, 1.f, 0.78f));
		Fog->SetRadialFogExtinction(4.2f * Density);
		Fog->SetHeightFogExtinction(1.6f * Density);
		Fog->SetHeightFogFalloff(1200.f);
	}
	if (Loop && T > 15.f && Loop->IsPlaying())
	{
		Loop->FadeOut(3.f, 0.f);
	}
	if (!Plume || T > 15.f)
	{
		return;
	}
	EmitTimer -= Dt;
	if (EmitTimer > 0.f)
	{
		return;
	}
	// Billowing hard for the first seconds, then a steady stream that keeps the cloud topped up.
	EmitTimer = T < 3.f ? 0.035f : 0.09f;
	const FVector Mouth = GetActorLocation() + GetActorForwardVector() * 7.f;
	const FVector Out = FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(0.4f, 1.f);
	const float Push = T < 3.f ? FMath::FRandRange(180.f, 320.f) : FMath::FRandRange(70.f, 160.f);
	Plume->Emit(Mouth + FVector(0.f, 0.f, 6.f), Out * Push + FVector(0.f, 0.f, FMath::FRandRange(30.f, 90.f)), FMath::FRandRange(40.f, 80.f), FMath::FRandRange(320.f, 460.f), FMath::FRandRange(9.f, 13.f), 0.8f);
}

void ACampusThrowable::TickThermite(float Dt)
{
	const float T = EffectAge();
	const float Burn = 5.5f;
	const float Fade = 1.f - FMath::SmoothStep(Burn - 0.8f, Burn, T);
	if (Light)
	{
		// Thermite flickers fast and hard; the molten core gutters as it burns out.
		const float Flicker = 0.7f + 0.3f * FMath::PerlinNoise1D(T * 23.f) + 0.12f * FMath::Sin(T * 61.f);
		Light->SetIntensity(140000.f * Flicker * Fade);
		if (T > Burn + 0.3f)
		{
			Light->DestroyComponent();
			Light = nullptr;
		}
	}
	for (UStaticMeshComponent* P : Parts)
	{
		if (P && P->ComponentHasTag(TEXT("Core")))
		{
			P->SetRelativeScale3D(FVector((0.18f + 0.06f * FMath::PerlinNoise1D(T * 17.f + 3.f)) * FMath::Max(Fade, 0.01f)));
			P->SetVisibility(Fade > 0.02f);
		}
	}
	if (Loop && T > Burn - 0.5f && Loop->IsPlaying())
	{
		Loop->FadeOut(0.8f, 0.f);
	}
	if (T > Burn)
	{
		return;
	}
	const FVector N = GetActorForwardVector();
	const FVector Mouth = GetActorLocation() + N * 9.f;
	if (Sparks)
	{
		// ~140 sparks a second, a fountain thrown out of the canister mouth.
		const int32 Count = FMath::Max(1, FMath::RoundToInt(140.f * Dt * Fade));
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector D = FMath::VRandCone(N, FMath::DegreesToRadians(65.f));
			Sparks->Emit(Mouth, D * FMath::FRandRange(250.f, 950.f) + FVector(0.f, 0.f, 120.f), FMath::FRandRange(0.4f, 1.3f), FMath::FRandRange(0.5f, 1.2f));
		}
	}
	EmitTimer -= Dt;
	if (Plume && EmitTimer <= 0.f)
	{
		EmitTimer = 0.05f;
		Plume->Emit(Mouth + N * 6.f, N * 60.f + FVector(0.f, 0.f, 90.f) + FMath::VRand() * 25.f, FMath::FRandRange(12.f, 22.f), FMath::FRandRange(120.f, 190.f), FMath::FRandRange(3.f, 4.5f), 0.32f * Fade);
	}
}
