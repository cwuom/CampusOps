#include "Weapons/CampusFX.h"
#include "CampusAssets.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Player/CampusCharacter.h"

// ------------------------------------------------------------------ tracer

ACampusTracer::ACampusTracer()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	Streak = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Streak"));
	RootComponent = Streak;
	Streak->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Streak->SetCastShadow(false);
	Streak->bReceivesDecals = false;
}

void ACampusTracer::Init(const FVector& InStart, const FVector& InEnd, const FLinearColor& Color, float InSpeed)
{
	Start = InStart;
	End = InEnd;
	Distance = FVector::Dist(Start, End);
	Speed = InSpeed;
	Length = FMath::Clamp(Distance * 0.12f, 60.f, 260.f);
	Streak->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cylinder));
	Streak->SetMaterial(0, CampusAssets::MakeGlowMID(this, Color, 18.f, true));
	SetActorRotation((End - Start).Rotation() + FRotator(-90.f, 0.f, 0.f));
	Tick(0.f);
}

void ACampusTracer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Travelled += Speed * DeltaSeconds;
	const float Head = FMath::Min(Travelled, Distance);
	const float Tail = FMath::Clamp(Travelled - Length, 0.f, Distance);
	if (Tail >= Distance - 1.f || Distance < 1.f)
	{
		Destroy();
		return;
	}
	const FVector Dir = (End - Start).GetSafeNormal();
	const FVector Mid = Start + Dir * ((Head + Tail) * 0.5f);
	const float SegLen = FMath::Max(Head - Tail, 1.f);
	SetActorLocation(Mid);
	SetActorRotation(FRotationMatrix::MakeFromZ(Dir).Rotator());
	SetActorScale3D(FVector(0.012f, 0.012f, SegLen / 100.f));
}

// ------------------------------------------------------------------ particles

ACampusParticleBurst::ACampusParticleBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->bReceivesDecals = false;
	Instances->SetMobility(EComponentMobility::Movable);
}

void ACampusParticleBurst::Init(const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& Color, float Emissive, float Speed, float Size, float Life, float Gravity, float Spread)
{
	Lifetime = Life;
	GravityZ = -Gravity;
	SetActorLocation(Location);
	Instances->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
	if (Emissive > 0.f)
	{
		Instances->SetMaterial(0, CampusAssets::MakeGlowMID(this, Color, Emissive, false));
	}
	else
	{
		Instances->SetMaterial(0, CampusAssets::MakeLitMID(this, Color, 0.9f, 0.f));
	}

	Positions.SetNum(Count);
	Velocities.SetNum(Count);
	Rotations.SetNum(Count);
	Spins.SetNum(Count);
	Sizes.SetNum(Count);
	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Dir = FMath::VRandCone(Normal, FMath::DegreesToRadians(Spread));
		Positions[i] = FVector::ZeroVector;
		Velocities[i] = Dir * Speed * FMath::FRandRange(0.35f, 1.f);
		Rotations[i] = FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f);
		Spins[i] = FRotator(FMath::FRandRange(-720.f, 720.f), FMath::FRandRange(-720.f, 720.f), 0.f);
		Sizes[i] = Size * FMath::FRandRange(0.5f, 1.2f) / 100.f;
		Transforms.Add(FTransform(Rotations[i], Positions[i], FVector(Sizes[i])));
	}
	Instances->AddInstances(Transforms, false, false);
}

void ACampusParticleBurst::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}
	const float Fade = 1.f - FMath::Square(Age / Lifetime);
	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		Velocities[i].Z += GravityZ * DeltaSeconds;
		Velocities[i] *= FMath::Max(0.f, 1.f - 2.5f * DeltaSeconds);
		Positions[i] += Velocities[i] * DeltaSeconds;
		Rotations[i] += Spins[i] * DeltaSeconds;
		Instances->UpdateInstanceTransform(i, FTransform(Rotations[i], Positions[i], FVector(Sizes[i] * Fade)), false, i == Positions.Num() - 1, true);
	}
}

// ------------------------------------------------------------------ sprites

ACampusSpriteBurst::ACampusSpriteBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->bReceivesDecals = false;
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->NumCustomDataFloats = 4;
}

void ACampusSpriteBurst::Init(const FVector& Location, const FVector& Direction, int32 Count, const FLinearColor& InColor, float InOpacity, float Speed, float StartSize, float EndSize, float Life, float Spread, float InRise)
{
	UMaterialInterface* Material = CampusAssets::SmokeMaterial();
	if (!Material || Count <= 0)
	{
		Destroy();
		return;
	}
	SetActorLocation(Location);
	Instances->SetStaticMesh(CampusAssets::PlaneMesh());
	Instances->SetMaterial(0, Material);
	Instances->SetNumCustomDataFloats(4);
	Color = InColor;
	Opacity = InOpacity;
	Rise = InRise;
	Lifetime = Life * 1.15f;
	const FVector Dir = Direction.GetSafeNormal();
	Sprites.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector D = FMath::VRandCone(Dir, FMath::DegreesToRadians(Spread));
		FSprite& Sp = Sprites.AddDefaulted_GetRef();
		Sp.Position = Location + D * FMath::FRandRange(0.f, 4.f);
		Sp.Velocity = D * Speed * FMath::FRandRange(0.35f, 1.f);
		Sp.Size0 = StartSize * FMath::FRandRange(0.7f, 1.2f);
		Sp.Size1 = EndSize * FMath::FRandRange(0.75f, 1.25f);
		Sp.Spin = FMath::FRandRange(0.f, 2.f * PI);
		Sp.SpinRate = FMath::FRandRange(-1.5f, 1.5f);
		Sp.Life = Life * FMath::FRandRange(0.75f, 1.1f);
		const int32 Index = Instances->AddInstance(FTransform(FQuat::Identity, Sp.Position, FVector(Sp.Size0 / 100.f)), true);
		Instances->SetCustomDataValue(Index, 0, 0.f, false);
		Instances->SetCustomDataValue(Index, 1, Color.R, false);
		Instances->SetCustomDataValue(Index, 2, Color.G, false);
		Instances->SetCustomDataValue(Index, 3, Color.B, false);
	}
	Tick(0.f);
}

void ACampusSpriteBurst::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Lifetime || Sprites.Num() == 0)
	{
		Destroy();
		return;
	}
	FVector Cam = GetActorLocation() + FVector(0.f, -100.f, 0.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Cam = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	const float Drag = FMath::Exp(-3.2f * DeltaSeconds);
	for (int32 i = 0; i < Sprites.Num(); ++i)
	{
		FSprite& Sp = Sprites[i];
		const float T = FMath::Clamp(Age / FMath::Max(Sp.Life, 0.01f), 0.f, 1.f);
		Sp.Velocity *= Drag;
		Sp.Velocity.Z += Rise * DeltaSeconds;
		Sp.Position += Sp.Velocity * DeltaSeconds;
		Sp.Spin += Sp.SpinRate * DeltaSeconds;
		const float Grow = 1.f - FMath::Square(1.f - T);
		const float Size = FMath::Lerp(Sp.Size0, Sp.Size1, Grow);
		const FVector ToCam = (Cam - Sp.Position).GetSafeNormal();
		const FQuat Q = FRotationMatrix::MakeFromZ(ToCam).ToQuat() * FQuat(FVector::UpVector, Sp.Spin);
		Instances->UpdateInstanceTransform(i, FTransform(Q, Sp.Position, FVector(Size / 100.f)), true, false, true);
		const float FadeIn = FMath::Clamp(T / 0.06f, 0.f, 1.f);
		Instances->SetCustomDataValue(i, 0, Opacity * FadeIn * FMath::Pow(1.f - T, 1.5f), false);
	}
	// 变换与自定义数据统一提交，不重建渲染代理
	Instances->MarkRenderInstancesDirty();
}

// ------------------------------------------------------------------ smoke trail

ACampusSmokeTrail::ACampusSmokeTrail()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->bReceivesDecals = false;
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->NumCustomDataFloats = 4;
}

void ACampusSmokeTrail::SetStyle(const FLinearColor& InColor, float InRise, float InDrag, float InCurl, int32 InMaxPuffs)
{
	Color = InColor;
	Rise = InRise;
	DragRate = InDrag;
	Curl = InCurl;
	MaxPuffs = FMath::Max(InMaxPuffs, 1);
}

void ACampusSmokeTrail::Emit(const FVector& Position, const FVector& Velocity, float StartSize, float EndSize, float Life, float Opacity)
{
	if (Puffs.Num() >= MaxPuffs || Opacity <= 0.002f)
	{
		return;
	}
	if (!Instances->GetStaticMesh())
	{
		UMaterialInterface* Material = CampusAssets::SmokeMaterial();
		if (!Material)
		{
			return;
		}
		Instances->SetStaticMesh(CampusAssets::PlaneMesh());
		Instances->SetMaterial(0, Material);
		Instances->SetNumCustomDataFloats(4);
	}
	FPuff& P = Puffs.AddDefaulted_GetRef();
	P.Position = Position;
	P.Velocity = Velocity;
	P.Size0 = StartSize * FMath::FRandRange(0.8f, 1.2f);
	P.Size1 = EndSize * FMath::FRandRange(0.8f, 1.25f);
	P.Spin = FMath::FRandRange(0.f, 2.f * PI);
	P.SpinRate = FMath::FRandRange(-1.2f, 1.2f);
	P.Life = FMath::Max(Life * FMath::FRandRange(0.85f, 1.15f), 0.05f);
	P.Age = 0.f;
	P.Opacity = Opacity;
	P.Seed = FMath::FRandRange(0.f, 100.f);
}

void ACampusSmokeTrail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (int32 i = Puffs.Num() - 1; i >= 0; --i)
	{
		Puffs[i].Age += DeltaSeconds;
		if (Puffs[i].Age >= Puffs[i].Life)
		{
			Puffs.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}
	Instances->ClearInstances();
	if (Puffs.Num() == 0)
	{
		return;
	}
	FVector Cam = GetActorLocation();
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Cam = PC->PlayerCameraManager->GetCameraLocation();
	}
	const float Drag = FMath::Exp(-DragRate * DeltaSeconds);
	Transforms.Reset(Puffs.Num());
	for (FPuff& P : Puffs)
	{
		const float T = P.Age / P.Life;
		// Warm smoke keeps rising while it slows, and the air it rises through makes it wander and curl.
		const FVector Wander(FMath::PerlinNoise1D(P.Seed + P.Age * 1.7f), FMath::PerlinNoise1D(P.Seed * 1.3f + 37.f + P.Age * 1.7f), 0.f);
		P.Velocity = P.Velocity * Drag + (Wander * Curl + FVector(0.f, 0.f, Rise)) * DeltaSeconds;
		P.Position += P.Velocity * DeltaSeconds;
		P.Spin += P.SpinRate * DeltaSeconds;
		const float Size = FMath::Lerp(P.Size0, P.Size1, 1.f - FMath::Square(1.f - T));
		const FQuat Q = FRotationMatrix::MakeFromZ((Cam - P.Position).GetSafeNormal()).ToQuat() * FQuat(FVector::UpVector, P.Spin);
		Transforms.Add(FTransform(Q, P.Position, FVector(Size / 100.f)));
	}
	Instances->AddInstances(Transforms, false, true);
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		const FPuff& P = Puffs[i];
		const float T = P.Age / P.Life;
		const float Alpha = P.Opacity * FMath::Clamp(T / 0.08f, 0.f, 1.f) * FMath::Pow(1.f - T, 1.6f);
		Instances->SetCustomDataValue(i, 0, Alpha, false);
		Instances->SetCustomDataValue(i, 1, Color.R, false);
		Instances->SetCustomDataValue(i, 2, Color.G, false);
		Instances->SetCustomDataValue(i, 3, Color.B, i == Puffs.Num() - 1);
	}
}

// ------------------------------------------------------------------ sparks

ACampusSparkEmitter::ACampusSparkEmitter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	Hot = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Hot"));
	RootComponent = Hot;
	Cool = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Cool"));
	Cool->SetupAttachment(Hot);
	for (UInstancedStaticMeshComponent* C : { Hot.Get(), Cool.Get() })
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bReceivesDecals = false;
		C->SetMobility(EComponentMobility::Movable);
	}
}

void ACampusSparkEmitter::Emit(const FVector& Position, const FVector& Velocity, float Life, float Size)
{
	if (Sparks.Num() >= 420)
	{
		return;
	}
	if (!Hot->GetStaticMesh())
	{
		Hot->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
		Cool->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
		Hot->SetMaterial(0, CampusAssets::MakeGlowMID(this, FLinearColor(1.f, 0.86f, 0.6f), 70.f, false));
		Cool->SetMaterial(0, CampusAssets::MakeGlowMID(this, FLinearColor(1.f, 0.32f, 0.06f), 16.f, false));
	}
	Sparks.Add({ Position, Velocity, 0.f, FMath::Max(Life, 0.05f), Size });
}

void ACampusSparkEmitter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HotT.Reset(Sparks.Num());
	CoolT.Reset(Sparks.Num());
	const float Drag = FMath::Exp(-1.1f * DeltaSeconds);
	for (int32 i = Sparks.Num() - 1; i >= 0; --i)
	{
		FSpark& S = Sparks[i];
		S.Age += DeltaSeconds;
		if (S.Age >= S.Life)
		{
			Sparks.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		S.Velocity = S.Velocity * Drag + FVector(0.f, 0.f, -980.f * DeltaSeconds);
		S.Position += S.Velocity * DeltaSeconds;
		if (S.Position.Z < FloorZ && S.Velocity.Z < 0.f)
		{
			// Skips along the floor, losing most of its speed.
			S.Position.Z = FloorZ;
			S.Velocity.Z *= -0.28f;
			S.Velocity.X *= 0.55f;
			S.Velocity.Y *= 0.55f;
		}
		const float T = S.Age / S.Life;
		const float Speed = S.Velocity.Size();
		// A streak as long as the spark travels in a sixtieth of a second, thinning as it cools.
		const float Len = FMath::Clamp(Speed * 0.016f, S.Size, 30.f);
		const float Thick = S.Size * (1.f - 0.6f * T);
		const FQuat Q = Speed > 1.f ? FRotationMatrix::MakeFromX(S.Velocity / Speed).ToQuat() : FQuat::Identity;
		(T < 0.35f ? HotT : CoolT).Add(FTransform(Q, S.Position, FVector(Len, Thick, Thick) / 100.f));
	}
	Hot->ClearInstances();
	Cool->ClearInstances();
	if (HotT.Num() > 0)
	{
		Hot->AddInstances(HotT, false, true);
	}
	if (CoolT.Num() > 0)
	{
		Cool->AddInstances(CoolT, false, true);
	}
}

// ------------------------------------------------------------------ debris streamers

ACampusDebrisStreamers::ACampusDebrisStreamers()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->bReceivesDecals = false;
	Instances->SetMobility(EComponentMobility::Movable);
}

void ACampusDebrisStreamers::Init(const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& ChunkColor, const FLinearColor& DustColor, float Speed, float Spread)
{
	SetActorLocation(Location);
	Instances->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
	Instances->SetMaterial(0, CampusAssets::MakeLitMID(this, ChunkColor, 0.95f, 0.f));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Dust = GetWorld()->SpawnActor<ACampusSmokeTrail>(ACampusSmokeTrail::StaticClass(), FTransform(Location), Params);
	if (Dust)
	{
		Dust->SetStyle(DustColor, 10.f, 1.8f, 38.f, 320);
	}
	const FVector N = Normal.GetSafeNormal();
	for (int32 i = 0; i < Count; ++i)
	{
		// Thrown out at a slant rather than straight up: the cone's axis is the surface normal, its core hollowed out.
		FVector D = FMath::VRandCone(N, FMath::DegreesToRadians(Spread));
		if ((D | N) > 0.93f)
		{
			D = (D + FMath::VRand().GetSafeNormal() * 0.5f).GetSafeNormal();
		}
		FChunk& C = Chunks.AddDefaulted_GetRef();
		C.Position = Location + D * 8.f;
		C.Velocity = D * Speed * FMath::FRandRange(0.45f, 1.f);
		C.Rotation = FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f));
		C.Spin = FRotator(FMath::FRandRange(-900.f, 900.f), FMath::FRandRange(-900.f, 900.f), FMath::FRandRange(-600.f, 600.f));
		C.Size = FMath::FRandRange(2.5f, 6.5f);
		C.EmitClock = FMath::FRandRange(0.f, 0.02f);
		C.bResting = false;
		Instances->AddInstance(FTransform(C.Rotation, C.Position, FVector(C.Size / 100.f)), true);
	}
	SetLifeSpan(4.f);
}

void ACampusDebrisStreamers::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	UWorld* World = GetWorld();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CampusDebrisStreamer), false);
	const float Drag = FMath::Exp(-0.35f * DeltaSeconds);
	// The dust streamer is thick off the blast and thins as the clod slows; resting clods stop shedding.
	const float Shed = FMath::Clamp(1.f - Age / 1.3f, 0.f, 1.f);
	const float Shrink = FMath::Clamp((4.f - Age) / 0.6f, 0.f, 1.f);
	for (int32 i = 0; i < Chunks.Num(); ++i)
	{
		FChunk& C = Chunks[i];
		if (!C.bResting)
		{
			C.Velocity = C.Velocity * Drag + FVector(0.f, 0.f, -980.f * DeltaSeconds);
			const FVector Next = C.Position + C.Velocity * DeltaSeconds;
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, C.Position, Next, ECC_Visibility, Query))
			{
				// Rubble barely bounces: most of the normal speed is lost and the rest is scrubbed by friction.
				const FVector HN = Hit.ImpactNormal;
				const float Vn = C.Velocity | HN;
				const FVector Tangent = C.Velocity - Vn * HN;
				C.Velocity = Tangent * 0.45f - Vn * 0.28f * HN;
				C.Position = Hit.ImpactPoint + HN * C.Size * 0.5f;
				C.Spin *= 0.5f;
				if (C.Velocity.SizeSquared() < FMath::Square(90.f) && HN.Z > 0.6f)
				{
					C.bResting = true;
				}
			}
			else
			{
				C.Position = Next;
			}
			C.Rotation += C.Spin * DeltaSeconds;
			C.EmitClock -= DeltaSeconds;
			if (Dust && Shed > 0.f && C.EmitClock <= 0.f)
			{
				C.EmitClock = 0.018f;
				Dust->Emit(C.Position, C.Velocity * 0.06f + FMath::VRand() * 18.f, C.Size * 3.f, C.Size * (12.f + 18.f * Age), FMath::FRandRange(1.1f, 2.2f), 0.95f * Shed);
			}
		}
		Instances->UpdateInstanceTransform(i, FTransform(C.Rotation, C.Position, FVector(C.Size * Shrink / 100.f)), true, i == Chunks.Num() - 1, true);
	}
}

void ACampusDebrisStreamers::Destroyed()
{
	if (Dust)
	{
		Dust->SetLifeSpan(2.5f);
		Dust = nullptr;
	}
	Super::Destroyed();
}

// ------------------------------------------------------------------ fireball

ACampusFireBurst::ACampusFireBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->bReceivesDecals = false;
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->NumCustomDataFloats = 4;
}

void ACampusFireBurst::Init(const FVector& Location, const FVector& Up, int32 Count, float Radius, float Life, float InBrightness)
{
	UMaterialInterface* Material = CampusAssets::FireMaterial();
	if (!Material || Count <= 0)
	{
		Destroy();
		return;
	}
	SetActorLocation(Location);
	Instances->SetStaticMesh(CampusAssets::PlaneMesh());
	Instances->SetMaterial(0, Material);
	Instances->SetNumCustomDataFloats(4);
	Brightness = InBrightness;
	const FVector N = Up.GetSafeNormal();
	for (int32 i = 0; i < Count; ++i)
	{
		// Mostly up and out of the surface it went off on, the core first and hottest.
		FVector D = FMath::VRand();
		if ((D | N) < 0.f)
		{
			D -= 1.4f * (D | N) * N;
		}
		D = (D + N * 0.35f).GetSafeNormal();
		FFlame& F = Flames.AddDefaulted_GetRef();
		const float Core = (float)i / Count;
		F.Position = Location + D * Radius * 0.15f * FMath::FRand();
		F.Velocity = D * Radius * FMath::FRandRange(4.f, 9.f) * (0.5f + Core);
		F.Size0 = Radius * FMath::FRandRange(0.25f, 0.45f);
		F.Size1 = Radius * FMath::FRandRange(0.8f, 1.35f);
		F.Spin = FMath::FRandRange(0.f, 2.f * PI);
		F.Life = Life * FMath::FRandRange(0.55f, 1.1f) * (1.15f - 0.4f * Core);
		F.Heat = FMath::FRandRange(0.8f, 1.f) * (1.f - 0.35f * Core);
		const int32 Index = Instances->AddInstance(FTransform(FQuat::Identity, F.Position, FVector(F.Size0 / 100.f)), true);
		for (int32 c = 0; c < 4; ++c)
		{
			Instances->SetCustomDataValue(Index, c, 0.f, false);
		}
	}
	Tick(0.f);
}

void ACampusFireBurst::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	FVector Cam = GetActorLocation();
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Cam = PC->PlayerCameraManager->GetCameraLocation();
	}
	// The blast front decelerates hard in a few hundredths of a second; the hot gas then rises and rolls.
	const float Drag = FMath::Exp(-11.f * DeltaSeconds);
	bool bAlive = false;
	for (int32 i = 0; i < Flames.Num(); ++i)
	{
		FFlame& F = Flames[i];
		const float T = FMath::Clamp(Age / F.Life, 0.f, 1.f);
		bAlive |= T < 1.f;
		F.Velocity = F.Velocity * Drag + FVector(0.f, 0.f, 260.f * DeltaSeconds);
		F.Position += F.Velocity * DeltaSeconds;
		F.Spin += DeltaSeconds * 0.8f;
		const float Grow = 1.f - FMath::Square(1.f - FMath::Min(T * 1.6f, 1.f));
		const float Size = FMath::Lerp(F.Size0, F.Size1, Grow);
		const FQuat Q = FRotationMatrix::MakeFromZ((Cam - F.Position).GetSafeNormal()).ToQuat() * FQuat(FVector::UpVector, F.Spin);
		Instances->UpdateInstanceTransform(i, FTransform(Q, F.Position, FVector(Size / 100.f)), true, false, true);
		// Blackbody cooling: white-yellow core, orange, then dull red as it turns to smoke.
		const float H = F.Heat * (1.f - T);
		const FLinearColor White(1.f, 0.93f, 0.78f), Yellow(1.f, 0.66f, 0.24f), Orange(1.f, 0.34f, 0.07f), Red(0.45f, 0.07f, 0.02f);
		const FLinearColor C = H > 0.75f ? FMath::Lerp(Yellow, White, (H - 0.75f) / 0.25f) : (H > 0.4f ? FMath::Lerp(Orange, Yellow, (H - 0.4f) / 0.35f) : FMath::Lerp(Red, Orange, H / 0.4f));
		const float Intensity = Brightness * FMath::Pow(1.f - T, 1.6f) * FMath::Clamp(Age / 0.02f, 0.f, 1.f) * (0.35f + 0.65f * H);
		Instances->SetCustomDataValue(i, 0, Intensity, false);
		Instances->SetCustomDataValue(i, 1, C.R, false);
		Instances->SetCustomDataValue(i, 2, C.G, false);
		Instances->SetCustomDataValue(i, 3, C.B, false);
	}
	// 火焰的变换和颜色仍逐帧更新，只保留原有渲染代理
	Instances->MarkRenderInstancesDirty();
	if (!bAlive)
	{
		Destroy();
	}
}

// ------------------------------------------------------------------ flash light

ACampusFlashLight::ACampusFlashLight()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	RootComponent = Light;
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetCastShadows(false);
}

void ACampusFlashLight::Init(const FLinearColor& Color, float Candelas, float Radius, float InDuration, bool bShadows)
{
	Peak = Candelas;
	Duration = FMath::Max(InDuration, 0.02f);
	Light->SetLightColor(Color);
	Light->SetAttenuationRadius(Radius);
	Light->SetSourceRadius(20.f);
	Light->SetCastShadows(bShadows);
	Light->SetIntensity(Peak);
}

void ACampusFlashLight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Duration)
	{
		Destroy();
		return;
	}
	Light->SetIntensity(Peak * FMath::Square(1.f - Age / Duration));
}

// ------------------------------------------------------------------ shell

ACampusShellCasing::ACampusShellCasing()
{
	SetReplicates(false);
	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	RootComponent = Shell;
	Shell->SetCollisionProfileName(TEXT("PhysicsActor"));
	Shell->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Shell->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Shell->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Shell->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Ignore);
	Shell->SetCastShadow(false);
	Shell->BodyInstance.bUseCCD = true;
	Shell->SetNotifyRigidBodyCollision(true);
	InitialLifeSpan = 3.0f;
}

void ACampusShellCasing::Init(const FVector& Velocity, bool bPistol)
{
	Shell->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cylinder));
	Shell->SetMaterial(0, CampusAssets::MakeLitMID(this, FLinearColor(0.55f, 0.36f, 0.08f), 0.3f, 1.f));
	const float Len = bPistol ? 1.9f : 4.4f;
	SetActorScale3D(FVector(0.011f, 0.011f, Len / 100.f));
	Shell->SetSimulatePhysics(true);
	Shell->SetMassOverrideInKg(NAME_None, 0.02f, true);
	Shell->SetPhysicsLinearVelocity(Velocity);
	Shell->SetPhysicsAngularVelocityInDegrees(FVector(FMath::FRandRange(-900.f, 900.f), FMath::FRandRange(-900.f, 900.f), FMath::FRandRange(-1800.f, 1800.f)));
	Shell->OnComponentHit.AddDynamic(this, &ACampusShellCasing::OnShellHit);
}

void ACampusShellCasing::OnShellHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (Bounces < 2 && GetVelocity().Size() > 60.f)
	{
		++Bounces;
		CampusAudio::PlayAt(this, TEXT("Shell_Clink"), GetActorLocation(), 0.35f / Bounces, 0.15f, CampusAssets::EAttenuation::Foley);
	}
}

#include "Map/CampusWater.h"

// ------------------------------------------------------------------ helpers

namespace CampusFX
{
	ECampusSurface ResolveSurface(const UPrimitiveComponent* Component, const AActor* Actor)
	{
		if (Cast<ACampusCharacter>(Actor))
		{
			return ECampusSurface::Flesh;
		}
		if (Component)
		{
			for (const FName& Tag : Component->ComponentTags)
			{
				if (Tag == TEXT("Surface_Metal")) return ECampusSurface::Metal;
				if (Tag == TEXT("Surface_Wood")) return ECampusSurface::Wood;
				if (Tag == TEXT("Surface_Dirt")) return ECampusSurface::Dirt;
				if (Tag == TEXT("Surface_Glass")) return ECampusSurface::Glass;
				if (Tag == TEXT("Surface_Water")) return ECampusSurface::Water;
			}
		}
		return ECampusSurface::Concrete;
	}

	ECampusSurface ResolveSurface(const FHitResult& Hit)
	{
		return ResolveSurface(Hit.GetComponent(), Hit.GetActor());
	}

	FName ImpactSound(ECampusSurface Surface)
	{
		switch (Surface)
		{
		case ECampusSurface::Metal: return TEXT("Impact_Metal");
		case ECampusSurface::Wood: return TEXT("Impact_Wood");
		case ECampusSurface::Dirt: return TEXT("Impact_Dirt");
		case ECampusSurface::Flesh: return TEXT("Impact_Flesh");
		case ECampusSurface::Glass: return TEXT("Impact_Glass");
		case ECampusSurface::Water: return TEXT("Bullet_Water");
		default: return TEXT("Impact_Concrete");
		}
	}

	void SpawnTracer(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Speed)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusTracer* Tracer = World->SpawnActor<ACampusTracer>(ACampusTracer::StaticClass(), FTransform(Start), Params))
		{
			Tracer->Init(Start, End, Color, Speed);
		}
	}

	static void Burst(UWorld* World, const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& Color, float Emissive, float Speed, float Size, float Life, float Gravity, float Spread)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusParticleBurst* P = World->SpawnActor<ACampusParticleBurst>(ACampusParticleBurst::StaticClass(), FTransform(Location), Params))
		{
			P->Init(Location, Normal, Count, Color, Emissive, Speed, Size, Life, Gravity, Spread);
		}
	}

	void SpawnDebris(UWorld* World, const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& Color, float Emissive, float Speed, float Size, float Life, float Gravity, float Spread)
	{
		if (World && World->GetNetMode() != NM_DedicatedServer)
		{
			Burst(World, Location, Normal, Count, Color, Emissive, Speed, Size, Life, Gravity, Spread);
		}
	}

	void SpawnFireball(UWorld* World, const FVector& Location, const FVector& Up, int32 Count, float Radius, float Life, float Brightness)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusFireBurst* F = World->SpawnActor<ACampusFireBurst>(ACampusFireBurst::StaticClass(), FTransform(Location), Params))
		{
			F->Init(Location, Up, Count, Radius, Life, Brightness);
		}
	}

	void SpawnDebrisStreamers(UWorld* World, const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& ChunkColor, const FLinearColor& DustColor, float Speed, float Spread)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusDebrisStreamers* D = World->SpawnActor<ACampusDebrisStreamers>(ACampusDebrisStreamers::StaticClass(), FTransform(Location), Params))
		{
			D->Init(Location, Normal, Count, ChunkColor, DustColor, Speed, Spread);
		}
	}

	void SpawnExplosion(UWorld* World, const FVector& Location, const FVector& Normal, float Scale, AActor* Ignore)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		const FVector N = Normal.GetSafeNormal();
		const FVector C = Location + N * 15.f * Scale;
		const float S = Scale;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CampusExplosionFloor), false, Ignore);
		FHitResult Floor;
		const bool bFloor = World->LineTraceSingleByChannel(Floor, C, C - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Query);
		// Nothing under it (a grenade gone off over the roof edge): no floor for the fragments to skip off and no
		// dust, clods or scorch from a ground that is not there.
		const float FloorZ = bFloor ? Floor.ImpactPoint.Z + 0.5f : -1.e6f;
		const bool bGround = bFloor && C.Z - FloorZ < (N.Z > 0.6f ? 120.f : 60.f) * FMath::Max(S, 1.f);
		const bool bSurface = bGround || N.Z <= 0.6f;

		// 1. The flash and fireball: brief, the core white-hot, gone within a quarter second.
		SpawnFlashLight(World, C + N * 50.f, FLinearColor(1.f, 0.66f, 0.36f), 1800000.f * S, 2600.f * S, 0.24f, true);
		SpawnFlashLight(World, C + N * 60.f, FLinearColor(1.f, 0.45f, 0.18f), 90000.f * S, 1400.f * S, 0.9f);
		SpawnFireball(World, C, N, 26, 230.f * S, 0.55f, 18.f);
		SpawnFireball(World, C, N, 10, 100.f * S, 0.18f, 45.f);

		// 2. Fragments: white-hot streaks flung out hemispherically, skipping off the floor as they cool.
		if (ACampusSparkEmitter* Frags = World->SpawnActor<ACampusSparkEmitter>(ACampusSparkEmitter::StaticClass(), FTransform(C), Params))
		{
			Frags->SetFloor(FloorZ);
			for (int32 i = 0; i < 90; ++i)
			{
				FVector D = FMath::VRand();
				if ((D | N) < -0.1f)
				{
					D -= 1.2f * (D | N) * N;
				}
				Frags->Emit(C + D * 10.f, D.GetSafeNormal() * FMath::FRandRange(1600.f, 4800.f) * FMath::Sqrt(S), FMath::FRandRange(0.12f, 0.55f), FMath::FRandRange(0.7f, 1.4f));
			}
			Frags->SetLifeSpan(1.5f);
		}

		// 3. Clods and grit thrown out on arcs, each trailing a streamer of dust.
		if (bSurface)
		{
			SpawnDebrisStreamers(World, C, N, FMath::RoundToInt(12 * S), FLinearColor(0.16f, 0.14f, 0.12f), FLinearColor(0.34f, 0.31f, 0.27f), 1500.f * FMath::Sqrt(S), 62.f);
			SpawnDebris(World, C, N, 30, FLinearColor(0.24f, 0.22f, 0.19f), 0.f, 1700.f, 2.2f, 1.4f, 980.f, 80.f);
		}

		// 4. The shock front racing out along the ground, lifting a low ring of dust that hangs.
		if (bGround)
		{
			const FVector Up = N.Z > 0.6f ? N : FVector::UpVector;
			for (int32 i = 0; i < 14; ++i)
			{
				const FVector Out = FVector::VectorPlaneProject(FRotator(0.f, i * (360.f / 14.f) + FMath::FRandRange(-10.f, 10.f), 0.f).Vector(), Up).GetSafeNormal();
				SpawnSprites(World, FVector(C.X, C.Y, FloorZ + 25.f) + Out * 40.f * S, Out + Up * 0.1f, 2, FLinearColor(0.38f, 0.35f, 0.30f), 1.25f, 1300.f * S, 60.f * S, 360.f * S, 3.4f, 16.f, 10.f);
			}
		}

		// 5. The pall: a dense, sooty billow that boils up out of the fireball, curls and thins over several seconds.
		if (ACampusSmokeTrail* Pall = World->SpawnActor<ACampusSmokeTrail>(ACampusSmokeTrail::StaticClass(), FTransform(C), Params))
		{
			Pall->SetStyle(FLinearColor(0.15f, 0.14f, 0.13f), 38.f, 2.1f, 70.f, 120);
			for (int32 i = 0; i < 46; ++i)
			{
				const float Core = (float)i / 46.f;
				FVector D = FMath::VRand();
				if ((D | N) < 0.f)
				{
					D -= 1.5f * (D | N) * N;
				}
				D = (D + N * 0.6f).GetSafeNormal();
				const FVector V = D * FMath::FRandRange(120.f, 820.f) * S * (0.4f + Core);
				Pall->Emit(C + D * FMath::FRandRange(5.f, 40.f) * S, V, FMath::FRandRange(70.f, 120.f) * S, FMath::FRandRange(320.f, 520.f) * S, FMath::FRandRange(4.f, 7.5f), FMath::Lerp(2.2f, 1.3f, Core));
			}
			Pall->SetLifeSpan(9.f);
		}
		// Lighter dust kicked up around the base, drifting longer than the soot.
		if (bSurface)
		{
			SpawnSprites(World, C, N + FVector(0.f, 0.f, 0.3f), 10, FLinearColor(0.42f, 0.39f, 0.34f), 0.9f, 420.f * S, 90.f * S, 420.f * S, 6.f, 75.f, 22.f);
			SpawnScorch(World, Location, N, 230.f * S, 0.95f, 45.f);
		}
	}

	void SpawnScorch(UWorld* World, const FVector& Location, const FVector& Normal, float Size, float Opacity, float LifeSpan)
	{
		UMaterialInterface* Mat = World && World->GetNetMode() != NM_DedicatedServer ? CampusAssets::ScorchMaterial() : nullptr;
		if (!Mat)
		{
			return;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Mat, World);
		MID->SetScalarParameterValue(TEXT("Seed"), FMath::FRand());
		MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		const FVector N = Normal.GetSafeNormal();
		const FRotator Rot = FRotationMatrix::MakeFromXZ(-N, FMath::Abs(N.Z) > 0.9f ? FVector::RightVector : FVector::UpVector).Rotator() + FRotator(0.f, 0.f, FMath::FRandRange(0.f, 360.f));
		if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, MID, FVector(Size * 0.25f, Size, Size), Location, Rot, LifeSpan))
		{
			Decal->SetFadeScreenSize(0.001f);
			Decal->SetFadeOut(LifeSpan * 0.75f, LifeSpan * 0.25f, false);
			Decal->SetSortOrder(2);
		}
	}

	void SpawnFlashLight(UWorld* World, const FVector& Location, const FLinearColor& Color, float Candelas, float Radius, float Duration, bool bShadows)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusFlashLight* L = World->SpawnActor<ACampusFlashLight>(ACampusFlashLight::StaticClass(), FTransform(Location), Params))
		{
			L->Init(Color, Candelas, Radius, Duration, bShadows);
		}
	}

	void SpawnSprites(UWorld* World, const FVector& Location, const FVector& Direction, int32 Count, const FLinearColor& Color, float Opacity, float Speed, float StartSize, float EndSize, float Life, float Spread, float Rise)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACampusSpriteBurst* S = World->SpawnActor<ACampusSpriteBurst>(ACampusSpriteBurst::StaticClass(), FTransform(Location), Params))
		{
			S->Init(Location, Direction, Count, Color, Opacity, Speed, StartSize, EndSize, Life, Spread, Rise);
		}
	}

	void SpawnBloodHit(UWorld* World, const FVector& Location, const FVector& Direction, bool bHeadshot)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		const FVector D = Direction.GetSafeNormal();
		const float Big = bHeadshot ? 1.5f : 1.f;
		// Entry puff back toward the shooter, a heavier exit spray, and fast droplets.
		SpawnSprites(World, Location, -D + FVector(0.f, 0.f, 0.2f), 3, FLinearColor(0.20f, 0.012f, 0.01f), 0.8f, 110.f * Big, 8.f, 34.f * Big, 0.32f, 55.f, -70.f);
		SpawnSprites(World, Location + D * 12.f, D, 4, FLinearColor(0.16f, 0.008f, 0.007f), 0.7f, 260.f * Big, 6.f, 46.f * Big, 0.42f, 32.f, -110.f);
		// Droplets thrown out of the far side, falling fast.
		SpawnSprites(World, Location + D * 6.f, D, bHeadshot ? 12 : 8, FLinearColor(0.11f, 0.004f, 0.004f), 0.95f, 480.f * Big, 2.8f, 2.f, 0.5f, 26.f, -900.f);

		// Spatter on whatever is close behind (thinner and wider the further it flew), and drips on the floor below.
		UMaterialInterface* BloodMat = CampusAssets::BloodDecalMaterial();
		if (!BloodMat)
		{
			return;
		}
		auto Splat = [&](const FVector& At, const FVector& Normal, const FVector& Throw, float Size, float Opacity)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BloodMat, World);
			const FVector Along = FVector::VectorPlaneProject(Throw, Normal);
			MID->SetScalarParameterValue(TEXT("Seed"), FMath::FRand());
			MID->SetScalarParameterValue(TEXT("Stretch"), FMath::Clamp(Along.Size(), 0.f, 0.95f));
			MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
			const FVector Axis = Along.IsNearlyZero(0.05f) ? FVector::CrossProduct(Normal, FVector::RightVector).GetSafeNormal() : Along.GetSafeNormal();
			const FRotator Rot = FRotationMatrix::MakeFromXY(-Normal, Axis).Rotator();
			if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, MID, FVector(6.f, Size, Size), At, Rot, 40.f))
			{
				Decal->SetFadeScreenSize(0.001f);
				Decal->SetFadeOut(30.f, 8.f, false);
				Decal->SetSortOrder(1);
			}
		};
		FCollisionObjectQueryParams Statics(ECC_WorldStatic);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CampusBloodSpatter), false);
		const float Reach = bHeadshot ? 320.f : 240.f;
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Location + D * 15.f, Location + D * Reach, Statics, Query))
		{
			const float Near = 1.f - Hit.Distance / Reach;
			Splat(Hit.ImpactPoint, Hit.ImpactNormal, D, FMath::Lerp(22.f, 9.f, Near) * Big * FMath::FRandRange(0.85f, 1.2f), FMath::Lerp(0.55f, 0.95f, Near));
		}
		if (FMath::FRand() < (bHeadshot ? 0.9f : 0.55f) && World->LineTraceSingleByObjectType(Hit, Location, Location - FVector(0.f, 0.f, 260.f), Statics, Query))
		{
			Splat(Hit.ImpactPoint + D.GetSafeNormal2D() * FMath::FRandRange(10.f, 40.f), Hit.ImpactNormal, D * 0.3f, FMath::FRandRange(6.f, 11.f) * Big, 0.85f);
		}
	}

	void SpawnMuzzleSmoke(UWorld* World, const FVector& Location, const FVector& Forward, float Amount)
	{
		SpawnSprites(World, Location + Forward * 6.f, Forward + FVector(0.f, 0.f, 0.15f), 2, FLinearColor(0.5f, 0.5f, 0.5f), 0.16f * Amount, 70.f, 4.f, 32.f, 0.9f, 28.f, 28.f);
	}

	void SpawnImpact(UWorld* World, const FVector& Location, const FVector& Normal, ECampusSurface Surface, bool bPlaySound)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		const FVector N = Normal.GetSafeNormal();
		switch (Surface)
		{
		case ECampusSurface::Flesh:
			SpawnBloodHit(World, Location, -N, false);
			break;
		case ECampusSurface::Glass:
			// Glittering chips and a faint powder of crushed glass; the pane itself shatters separately.
			Burst(World, Location, -N, 14, FLinearColor(0.75f, 0.82f, 0.85f), 0.f, 520.f, 0.9f, 0.6f, 980.f, 50.f);
			SpawnSprites(World, Location, -N, 2, FLinearColor(0.8f, 0.82f, 0.84f), 0.2f, 60.f, 4.f, 26.f, 0.5f, 40.f, 0.f);
			break;
		case ECampusSurface::Water:
			// A thin column thrown straight up out of the cavity, droplets falling back round it, a puff of spray,
			// and the ring spreading out on the surface.
			Burst(World, Location, FVector::UpVector, 16, FLinearColor(0.72f, 0.8f, 0.82f), 0.f, 520.f, 0.6f, 0.75f, 980.f, 14.f);
			Burst(World, Location, FVector::UpVector, 10, FLinearColor(0.72f, 0.8f, 0.82f), 0.f, 240.f, 0.45f, 0.55f, 980.f, 55.f);
			SpawnSprites(World, Location + FVector(0.f, 0.f, 6.f), FVector::UpVector, 2, FLinearColor(0.86f, 0.9f, 0.92f), 0.3f, 90.f, 6.f, 34.f, 0.55f, 30.f, 10.f);
			ACampusWater::AddRipple(World, Location, 90.f, 1.f);
			break;
		case ECampusSurface::Metal:
			// A hard spray of sparks off the steel, a few slow embers falling after it, and a puff of scorched paint.
			Burst(World, Location, N, 16, FLinearColor(1.f, 0.55f, 0.15f), 50.f, 1300.f, 0.5f, 0.22f, 1400.f, 55.f);
			Burst(World, Location, N, 4, FLinearColor(1.f, 0.42f, 0.1f), 30.f, 320.f, 0.7f, 0.6f, 900.f, 70.f);
			SpawnSprites(World, Location + N * 2.f, N, 3, FLinearColor(0.38f, 0.38f, 0.38f), 0.4f, 110.f, 5.f, 40.f, 0.7f, 30.f, 20.f);
			break;
		case ECampusSurface::Wood:
			Burst(World, Location, N, 9, FLinearColor(0.32f, 0.2f, 0.09f), 0.f, 460.f, 1.3f, 0.7f, 980.f, 40.f);
			SpawnSprites(World, Location + N * 2.f, N, 2, FLinearColor(0.42f, 0.33f, 0.22f), 0.45f, 110.f, 6.f, 42.f, 0.7f, 30.f, 10.f);
			break;
		case ECampusSurface::Dirt:
			Burst(World, Location, N, 12, FLinearColor(0.18f, 0.13f, 0.08f), 0.f, 400.f, 1.8f, 0.8f, 800.f, 30.f);
			SpawnSprites(World, Location + N * 3.f, N + FVector(0.f, 0.f, 0.5f), 4, FLinearColor(0.30f, 0.24f, 0.17f), 0.6f, 160.f, 10.f, 85.f, 1.1f, 35.f, 10.f);
			break;
		default:
			// Concrete and plaster: chips thrown out of the crater, a couple of sparks off the aggregate, a sharp jet
			// of powder straight out of the hole and a slower cloud that drifts and hangs.
			Burst(World, Location, N, 13, FLinearColor(0.42f, 0.4f, 0.37f), 0.f, 560.f, 1.1f, 0.7f, 900.f, 45.f);
			Burst(World, Location, N, 4, FLinearColor(1.f, 0.6f, 0.25f), 25.f, 850.f, 0.4f, 0.14f, 1200.f, 30.f);
			SpawnSprites(World, Location + N * 2.f, N, 2, FLinearColor(0.5f, 0.48f, 0.45f), 0.7f, 420.f, 4.f, 34.f, 0.35f, 10.f, 0.f);
			SpawnSprites(World, Location + N * 2.f, N, 4, FLinearColor(0.46f, 0.44f, 0.41f), 0.5f, 130.f, 9.f, 85.f, 1.4f, 36.f, 14.f);
			break;
		}

		if (Surface != ECampusSurface::Flesh && Surface != ECampusSurface::Water)
		{
			if (UMaterialInterface* DecalMat = CampusAssets::DecalMaterial())
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(DecalMat, World);
				MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 1.f, 1.f));
				MID->SetScalarParameterValue(TEXT("Opacity"), 0.92f);
				const FRotator Rot = (-N).Rotation() + FRotator(0.f, 0.f, FMath::FRandRange(0.f, 360.f));
				const float Size = FMath::FRandRange(3.4f, 4.6f);
				if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, MID, FVector(4.f, Size, Size), Location, Rot, 25.f))
				{
					Decal->SetFadeScreenSize(0.002f);
					Decal->SetFadeOut(20.f, 5.f, false);
				}
			}
		}

		if (bPlaySound)
		{
			CampusAudio::PlayAt(World, ImpactSound(Surface), Location, Surface == ECampusSurface::Flesh ? 0.9f : 0.55f, 0.12f, CampusAssets::EAttenuation::Impact);
		}
	}

	void SpawnShell(UWorld* World, const FTransform& Eject, const FVector& InheritVelocity, bool bPistol)
	{
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACampusShellCasing* ShellActor = World->SpawnActor<ACampusShellCasing>(ACampusShellCasing::StaticClass(), FTransform(Eject.GetRotation(), Eject.GetLocation()), Params);
		if (ShellActor)
		{
			const FVector Right = Eject.GetRotation().GetRightVector();
			const FVector Up = Eject.GetRotation().GetUpVector();
			const FVector Fwd = Eject.GetRotation().GetForwardVector();
			const FVector Vel = InheritVelocity + Right * FMath::FRandRange(260.f, 360.f) + Up * FMath::FRandRange(120.f, 220.f) - Fwd * FMath::FRandRange(0.f, 60.f);
			ShellActor->Init(Vel, bPistol);
		}
	}
}
