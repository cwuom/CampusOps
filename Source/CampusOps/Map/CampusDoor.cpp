#include "Map/CampusDoor.h"
#include "Map/CampusSchoolBuilder.h"
#include "Map/CampusBreakableGlass.h"
#include "CampusTypes.h"
#include "Player/CampusCharacter.h"
#include "CampusAssets.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float DoorOpenAngle = 95.f;
	constexpr float DoorThickness = 4.f;

	/** Per-instance data in the building's surface layout: colour, roughness, metal, emissive, tiling, dirt, half extents. */
	TArray<float> Look(const FLinearColor& Color, float Rough, float Metal, float Tile, const FVector& Size)
	{
		return { Color.R, Color.G, Color.B, Rough, Metal, 0.f, Tile, 0.f, (float)Size.X * 0.5f, (float)Size.Y * 0.5f, (float)Size.Z * 0.5f };
	}
}

ACampusDoor::ACampusDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	RootComponent = Hinge;
	Swing = CreateDefaultSubobject<USceneComponent>(TEXT("Swing"));
	Swing->SetupAttachment(Hinge);
	Swing->SetMobility(EComponentMobility::Movable);

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(Swing);
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetCanEverAffectNavigation(false);
	// Rounds are stopped by the leaf's own pieces instead, so they can pass through the vision panel's glass.
	Blocker->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Ignore);
	Blocker->ComponentTags.Add(ACampusSchoolBuilder::SurfaceTag(1));
}

void ACampusDoor::Setup(float InWidth, float InHeight)
{
	Width = InWidth;
	Height = InHeight;
}

void ACampusDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ACampusDoor, Width, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ACampusDoor, Height, COND_InitialOnly);
	DOREPLIFETIME(ACampusDoor, OpenSide);
	DOREPLIFETIME_CONDITION(ACampusDoor, MaxOpenPos, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ACampusDoor, MaxOpenNeg, COND_InitialOnly);
}

void ACampusDoor::BeginPlay()
{
	Super::BeginPlay();
	BuildLeaf();
	Angle = TargetAngle();
	Swing->SetRelativeRotation(FRotator(0.f, Angle, 0.f));
}

bool ACampusDoor::MeasureClearance(TConstArrayView<FTransform> Decor)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusDoorSwing), false, this);
	const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::InitType::AllStaticObjects);
	// The leaf from just past the jamb (the wall's half thickness) to the latch edge, clear of floor and head.
	const float X0 = 14.f;
	const float X1 = FMath::Max(Width - 3.f, X0 + 10.f);
	const FCollisionShape Leaf = FCollisionShape::MakeBox(FVector((X1 - X0) * 0.5f, 1.5f, FMath::Max(Height * 0.5f - 8.f, 20.f)));
	const FTransform Base = GetActorTransform();
	// Decor has no collision: sample the leaf (both faces) against those boxes directly, from past the jamb and its
	// frame trim (which the leaf legitimately sits against) to the latch edge.
	TArray<FTransform, TInlineAllocator<32>> Near;
	for (const FTransform& T : Decor)
	{
		if (FVector::DistSquared2D(T.GetLocation(), Base.GetLocation()) < FMath::Square(Width + 150.f))
		{
			Near.Add(T);
		}
	}
	auto HitsDecor = [&](const FQuat& Rot)
	{
		for (float D = 14.f; D < Width - 1.f; D += 4.f)
		{
			for (float Z = 12.f; Z < Height - 4.f; Z += 22.f)
			{
				for (const float Face : { -DoorThickness * 0.5f, DoorThickness * 0.5f })
				{
					const FVector P = Base.GetLocation() + Rot.RotateVector(FVector(D, Face, Z));
					for (const FTransform& T : Near)
					{
						const FVector L = T.InverseTransformPosition(P);
						if (FMath::Abs(L.X) < 49.f && FMath::Abs(L.Y) < 49.f && FMath::Abs(L.Z) < 49.f)
						{
							return true;
						}
					}
				}
			}
		}
		return false;
	};
	auto Free = [&](float Side)
	{
		float Reached = 0.f;
		for (float A = 10.f; A <= DoorOpenAngle + 0.1f; A += 5.f)
		{
			const FQuat Rot = Base.GetRotation() * FQuat(FVector::UpVector, FMath::DegreesToRadians(Side * A));
			const FVector Center = Base.GetLocation() + Rot.RotateVector(FVector((X0 + X1) * 0.5f, 0.f, Height * 0.5f + 2.f));
			if (World->OverlapAnyTestByObjectType(Center, Rot, Objects, Leaf, Params) || HitsDecor(Rot))
			{
				break;
			}
			Reached = A;
		}
		// Stops a little short of whatever it would hit.
		return Reached >= DoorOpenAngle ? DoorOpenAngle : FMath::Max(Reached - 2.f, 0.f);
	};
	MaxOpenPos = Free(1.f);
	MaxOpenNeg = Free(-1.f);
	return FMath::Max(MaxOpenPos, MaxOpenNeg) >= 70.f;
}

void ACampusDoor::BuildLeaf()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	UStaticMesh* Cube = CampusAssets::Shape(ECampusShape::Cube);
	UStaticMesh* Cylinder = CampusAssets::Shape(ECampusShape::Cylinder);
	auto MakeSet = [&](UStaticMesh* Mesh, ECampusMat Mat)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetupAttachment(Swing);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetStaticMesh(Mesh);
		ISM->SetMaterial(0, CampusAssets::SurfaceMaterial(Mat));
		ISM->SetNumCustomDataFloats(ACampusSchoolBuilder::NumCustomFloats);
		ISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ISM->SetCollisionObjectType(ECC_WorldDynamic);
		ISM->SetCollisionResponseToAllChannels(ECR_Ignore);
		ISM->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
		ISM->ComponentTags.Add(ACampusSchoolBuilder::SurfaceTag(1));
		ISM->SetCanEverAffectNavigation(false);
		ISM->RegisterComponent();
		return ISM;
	};
	Paint = MakeSet(Cube, ECampusMat::PaintedMetal);
	Steel = MakeSet(Cube, ECampusMat::Metal);
	UInstancedStaticMeshComponent* Round = MakeSet(Cylinder, ECampusMat::Metal);
	Glass = NewObject<UCampusBreakableGlass>(this, TEXT("Glass"));
	Glass->SetupAttachment(Swing);
	Glass->RegisterComponent();

	// The palette's door green, stainless fittings and clear glazing.
	const FLinearColor Green(0.12f, 0.27f, 0.20f);
	const FLinearColor Stainless(0.62f, 0.62f, 0.64f);
	const FLinearColor Closer(0.08f, 0.08f, 0.085f);
	auto Add = [](UInstancedStaticMeshComponent* Set, const FVector& Center, const FVector& Size, const TArray<float>& Data, const FRotator& Rot = FRotator::ZeroRotator)
	{
		const int32 i = Set->AddInstance(FTransform(Rot, Center, Size / 100.f));
		Set->SetCustomData(i, Data, false);
	};
	auto Box = [&](UInstancedStaticMeshComponent* Set, const FVector& Min, const FVector& Max, const FLinearColor& Color, float Rough, float Metal, float Tile)
	{
		const FVector Size = Max - Min;
		Add(Set, (Min + Max) * 0.5f, Size, Look(Color, Rough, Metal, Tile, Size));
	};

	const float W = Width - 1.f;
	const float H = Height - 1.5f;
	const float X0 = 0.5f;
	const float T = DoorThickness * 0.5f;
	// Leaf around a vision panel: stiles, the solid lower half and the head rail.
	const float Wx0 = X0 + W * 0.3f;
	const float Wx1 = X0 + W * 0.7f;
	const float Wz0 = FMath::Min(115.f, H * 0.55f);
	const float Wz1 = FMath::Min(185.f, H - 22.f);
	Box(Paint, FVector(X0, -T, 0.f), FVector(Wx0, T, H), Green, 0.5f, 0.f, 0.9f);
	Box(Paint, FVector(Wx1, -T, 0.f), FVector(X0 + W, T, H), Green, 0.5f, 0.f, 0.9f);
	Box(Paint, FVector(Wx0, -T, 0.f), FVector(Wx1, T, Wz0), Green, 0.5f, 0.f, 0.9f);
	Box(Paint, FVector(Wx0, -T, Wz1), FVector(Wx1, T, H), Green, 0.5f, 0.f, 0.9f);
	Glass->SetPanes({ FBox(FVector(Wx0, -0.4f, Wz0), FVector(Wx1, 0.4f, Wz1)) }, { 3 });
	for (const float S : { -1.f, 1.f })
	{
		const float F = S * T;
		const float Out = S * 1.2f;
		// glazing beads
		Box(Paint, FVector(Wx0 - 2.5f, FMath::Min(F, F + Out), Wz0 - 2.5f), FVector(Wx1 + 2.5f, FMath::Max(F, F + Out), Wz0), Green, 0.45f, 0.f, 0.9f);
		Box(Paint, FVector(Wx0 - 2.5f, FMath::Min(F, F + Out), Wz1), FVector(Wx1 + 2.5f, FMath::Max(F, F + Out), Wz1 + 2.5f), Green, 0.45f, 0.f, 0.9f);
		Box(Paint, FVector(Wx0 - 2.5f, FMath::Min(F, F + Out), Wz0), FVector(Wx0, FMath::Max(F, F + Out), Wz1), Green, 0.45f, 0.f, 0.9f);
		Box(Paint, FVector(Wx1, FMath::Min(F, F + Out), Wz0), FVector(Wx1 + 2.5f, FMath::Max(F, F + Out), Wz1), Green, 0.45f, 0.f, 0.9f);
		// raised lower panel
		Box(Paint, FVector(X0 + 12.f, FMath::Min(F, F + S * 0.6f), 30.f), FVector(X0 + W - 12.f, FMath::Max(F, F + S * 0.6f), Wz0 - 14.f), Green, 0.5f, 0.f, 0.9f);
		// kick plate
		Box(Steel, FVector(X0 + 3.f, FMath::Min(F, F + S * 0.25f), 2.f), FVector(X0 + W - 3.f, FMath::Max(F, F + S * 0.25f), 26.f), Stainless, 0.35f, 1.f, 0.6f);
		// lever handle: rosette, neck, lever
		Add(Round, FVector(X0 + W - 9.f, S * (T + 0.5f), 100.f), FVector(6.f, 6.f, 1.f), Look(Stainless, 0.3f, 1.f, 0.6f, FVector::ZeroVector), FRotator(0.f, 0.f, 90.f));
		Box(Steel, FVector(X0 + W - 10.f, FMath::Min(S * (T + 0.8f), S * (T + 3.4f)), 99.f), FVector(X0 + W - 8.f, FMath::Max(S * (T + 0.8f), S * (T + 3.4f)), 101.f), Stainless, 0.3f, 1.f, 0.6f);
		Box(Steel, FVector(X0 + W - 22.f, FMath::Min(S * (T + 2.6f), S * (T + 4.2f)), 98.8f), FVector(X0 + W - 8.f, FMath::Max(S * (T + 2.6f), S * (T + 4.2f)), 101.2f), Stainless, 0.3f, 1.f, 0.6f);
	}
	// hinge knuckles and the overhead closer (corridor side)
	for (const float Z : { 22.f, H * 0.5f, H - 26.f })
	{
		Add(Round, FVector(0.f, 0.f, Z), FVector(2.4f, 2.4f, 10.f), Look(Stainless, 0.35f, 1.f, 0.6f, FVector::ZeroVector));
	}
	Box(Steel, FVector(X0 + 6.f, T, H - 12.f), FVector(X0 + 30.f, T + 4.5f, H - 5.f), Closer, 0.5f, 0.6f, 0.6f);
	Box(Steel, FVector(X0 + 28.f, T + 1.f, H - 9.5f), FVector(X0 + 46.f, T + 2.6f, H - 8.f), Closer, 0.5f, 0.6f, 0.6f);

	for (UInstancedStaticMeshComponent* Set : { Paint.Get(), Steel.Get(), Round })
	{
		Set->MarkRenderStateDirty();
	}
	Blocker->SetBoxExtent(FVector(W * 0.5f, T + 0.5f, H * 0.5f));
	Blocker->SetRelativeLocation(FVector(X0 + W * 0.5f, 0.f, H * 0.5f));
}

FVector ACampusDoor::GetHandleLocation() const
{
	return GetActorTransform().TransformPosition(FVector(Width * 0.5f, 0.f, 100.f));
}

void ACampusDoor::Toggle(const AActor* User)
{
	if (!HasAuthority())
	{
		return;
	}
	if (OpenSide != 0)
	{
		OpenSide = 0;
	}
	else
	{
		// Swing away from whoever pushes it, unless that side is blocked (stairs, a wall) and the other is clearer.
		const float Side = User ? FVector::DotProduct(User->GetActorLocation() - GetActorLocation(), GetActorRightVector()) : 1.f;
		OpenSide = Side >= 0.f ? -1 : 1;
		const float Preferred = OpenSide > 0 ? MaxOpenPos : MaxOpenNeg;
		const float Other = OpenSide > 0 ? MaxOpenNeg : MaxOpenPos;
		if (Preferred < 70.f && Other > Preferred)
		{
			OpenSide = -OpenSide;
		}
	}
	ForceNetUpdate();
}

void ACampusDoor::OpenForBots()
{
	if (OpenSide != 0)
	{
		return;
	}
	const FVector Center = GetHandleLocation();
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		ACampusCharacter* C = *It;
		if (!C->IsAlive() || C->IsPlayerControlled())
		{
			continue;
		}
		const FVector ToDoor = Center - C->GetActorLocation();
		const FVector Vel = C->GetVelocity();
		if (FVector2D(ToDoor).SizeSquared() < FMath::Square(150.f) && FMath::Abs(ToDoor.Z) < 160.f && (FVector2D(Vel) | FVector2D(ToDoor)) > 0.f)
		{
			Toggle(C);
			return;
		}
	}
}

void ACampusDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		BotScanTime -= DeltaSeconds;
		if (BotScanTime <= 0.f)
		{
			BotScanTime = 0.2f;
			OpenForBots();
		}
	}
	const float Target = TargetAngle();
	if (FMath::IsNearlyEqual(Angle, Target, 0.05f) && FMath::Abs(AngularVelocity) < 0.1f)
	{
		return;
	}
	// Swings with a little weight: quick start, eased stop with a slight settle.
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const float Omega = 2.f * PI * 1.6f;
	AngularVelocity += (Omega * Omega * (Target - Angle) - 2.f * 0.82f * Omega * AngularVelocity) * Dt;
	Angle += AngularVelocity * Dt;
	Swing->SetRelativeRotation(FRotator(0.f, Angle, 0.f));
}
