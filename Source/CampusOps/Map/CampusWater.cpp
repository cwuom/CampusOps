#include "Map/CampusWater.h"
#include "CampusAssets.h"
#include "CampusTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"

#include "Engine/Texture2D.h"
#include "RenderingThread.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

TArray<TWeakObjectPtr<ACampusWater>> ACampusWater::All;
float ACampusWater::SunStrength = 0.f;

namespace
{
	/** The simulation runs at a fixed rate so waves travel at the same speed whatever the frame rate. */
	constexpr float SimStep = 1.f / 60.f;
	/** Texels per metre of pool. */
	constexpr float SimDensity = 13.f;
}

ACampusWater::ACampusWater()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	RootComponent = Surface;
	Surface->SetMobility(EComponentMobility::Movable);
	Surface->SetCastShadow(false);
	Surface->bReceivesDecals = false;
	Surface->SetCanEverAffectNavigation(false);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Rounds stop at the surface (a splash); people, the camera and sight lines go through it.
	HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HitBox"));
	HitBox->SetupAttachment(Surface);
	HitBox->SetUsingAbsoluteScale(true);
	HitBox->SetMobility(EComponentMobility::Movable);
	HitBox->SetCanEverAffectNavigation(false);
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCollisionObjectType(ECC_WorldStatic);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
	HitBox->ComponentTags.Add(TEXT("Surface_Water"));

	// Caustics: an additive sheet of focused light lying just above the pool floor, seen refracted through the surface.
	Caustics = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Caustics"));
	Caustics->SetupAttachment(Surface);
	Caustics->SetUsingAbsoluteLocation(true);
	Caustics->SetUsingAbsoluteScale(true);
	Caustics->SetUsingAbsoluteRotation(true);
	Caustics->SetMobility(EComponentMobility::Movable);
	Caustics->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Caustics->SetCastShadow(false);
	Caustics->bReceivesDecals = false;
	Caustics->SetCanEverAffectNavigation(false);
}

void ACampusWater::SetVolume(const FBox& Volume)
{
	Box = Volume;
	const FVector Size = Volume.GetSize();
	Surface->SetStaticMesh(CampusAssets::PlaneMesh());
	SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(Volume.GetCenter().X, Volume.GetCenter().Y, Volume.Max.Z), FVector(Size.X / 100.f, Size.Y / 100.f, 1.f)));
	HitBox->SetBoxExtent(FVector(Size.X * 0.5f, Size.Y * 0.5f, 1.f));
	HitBox->SetWorldLocation(FVector(Volume.GetCenter().X, Volume.GetCenter().Y, Volume.Max.Z - 1.f));

	const UWorld* World = GetWorld();
	const bool bVisuals = World && World->GetNetMode() != NM_DedicatedServer;
	if (bVisuals)
	{
		SimSize = FIntPoint(FMath::Clamp(FMath::RoundToInt(Size.X / 100.f * SimDensity), 16, 256), FMath::Clamp(FMath::RoundToInt(Size.Y / 100.f * SimDensity), 16, 1024));
		Height.Init(0.f, SimSize.X * SimSize.Y);
		PrevHeight.Init(0.f, SimSize.X * SimSize.Y);
		Drops.Initialize(1234);
		SimTexture = UTexture2D::CreateTransient(SimSize.X, SimSize.Y, PF_R32_FLOAT);
		if (SimTexture)
		{
			SimTexture->SRGB = false;
			SimTexture->Filter = TF_Bilinear;
			// Clamped at the edges, like the waves against the walls.
			SimTexture->AddressX = TA_Clamp;
			SimTexture->AddressY = TA_Clamp;
			SimTexture->CompressionSettings = TC_HDR;
			SimTexture->UpdateResource();
		}
		UE_LOG(LogTemp, Display, TEXT("[CampusWater] simulation %dx%d"), SimSize.X, SimSize.Y);
	}
	if (UMaterialInterface* M = CampusAssets::WaterMaterial())
	{
		SurfaceMID = UMaterialInstanceDynamic::Create(M, this);
		SurfaceMID->SetVectorParameterValue(TEXT("PoolMin"), FLinearColor(Volume.Min.X, Volume.Min.Y, 0.f, 0.f));
		SurfaceMID->SetVectorParameterValue(TEXT("PoolSize"), FLinearColor(Size.X, Size.Y, 0.f, 0.f));
		SurfaceMID->SetVectorParameterValue(TEXT("SimTexel"), FLinearColor(1.f / SimSize.X, 1.f / SimSize.Y, 0.f, 0.f));
		SurfaceMID->SetScalarParameterValue(TEXT("SimStrength"), 1.1f);
		Surface->SetMaterial(0, SurfaceMID);
	}
	if (UMaterialInterface* M = CampusAssets::CausticsMaterial())
	{
		CausticMID = UMaterialInstanceDynamic::Create(M, this);
		Caustics->SetStaticMesh(CampusAssets::PlaneMesh());
		Caustics->SetMaterial(0, CausticMID);
		Caustics->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Volume.GetCenter().X, Volume.GetCenter().Y, Volume.Min.Z + 0.6f), FVector(Size.X / 100.f, Size.Y / 100.f, 1.f)));
	}
	All.AddUnique(this);
	// The environment may have set the light before this water existed.
	SetSunlight(GetWorld(), SunStrength);
}

void ACampusWater::EndPlay(const EEndPlayReason::Type Reason)
{
	All.Remove(this);
	Super::EndPlay(Reason);
}

float ACampusWater::DepthAt(const UWorld* World, const FVector& Feet, float* OutSurfaceZ)
{
	for (const TWeakObjectPtr<ACampusWater>& W : All)
	{
		const ACampusWater* Water = W.Get();
		if (!Water || Water->GetWorld() != World)
		{
			continue;
		}
		const FBox& B = Water->Box;
		if (Feet.X >= B.Min.X && Feet.X <= B.Max.X && Feet.Y >= B.Min.Y && Feet.Y <= B.Max.Y && Feet.Z <= B.Max.Z + 2.f && Feet.Z >= B.Min.Z - 40.f)
		{
			if (OutSurfaceZ)
			{
				*OutSurfaceZ = B.Max.Z;
			}
			return FMath::Max(0.f, B.Max.Z - Feet.Z);
		}
	}
	return 0.f;
}

void ACampusWater::AddRipple(UWorld* World, const FVector& Location, float Radius, float Strength)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (const TWeakObjectPtr<ACampusWater>& W : All)
	{
		ACampusWater* Water = W.Get();
		if (Water && Water->GetWorld() == World && Water->Box.ExpandBy(FVector(5.f, 5.f, 60.f)).IsInsideXY(Location) && FMath::Abs(Location.Z - Water->Box.Max.Z) < 80.f)
		{
			Water->Push(Location, Radius, Strength);
			return;
		}
	}
}

void ACampusWater::Rain(UWorld* World, float Amount, float DeltaSeconds, const FVector& Camera)
{
	for (const TWeakObjectPtr<ACampusWater>& W : All)
	{
		if (ACampusWater* Water = W.Get(); Water && Water->GetWorld() == World)
		{
			Water->RainAmount = FMath::Clamp(Amount, 0.f, 1.f);
		}
	}
}

void ACampusWater::SetSunlight(UWorld* World, float Strength)
{
	SunStrength = Strength;
	for (const TWeakObjectPtr<ACampusWater>& W : All)
	{
		if (ACampusWater* Water = W.Get(); Water && Water->GetWorld() == World && Water->CausticMID)
		{
			Water->CausticMID->SetScalarParameterValue(TEXT("Strength"), Strength);
			Water->Caustics->SetVisibility(Strength > 0.01f);
		}
	}
}

void ACampusWater::Push(const FVector& Location, float Radius, float Strength)
{
	// A ripple's "radius" is how far its ring should spread: the disturbance itself is a fraction of that.
	const FVector Size = Box.GetSize();
	const FVector2D UV((Location.X - Box.Min.X) / FMath::Max(Size.X, 1.f), (Location.Y - Box.Min.Y) / FMath::Max(Size.Y, 1.f));
	const float Texels = FMath::Clamp(Radius * 0.003f * SimDensity, 0.8f, 6.f);
	if (Pending.Num() >= 8)
	{
		// Merge into the nearest, so a burst of hits still all register.
		int32 Best = 0;
		for (int32 i = 1; i < Pending.Num(); ++i)
		{
			if (FVector2D::DistSquared(Pending[i].At, UV) < FVector2D::DistSquared(Pending[Best].At, UV))
			{
				Best = i;
			}
		}
		Pending[Best].Strength += Strength * 0.5f;
		return;
	}
	Pending.Add({ UV, Texels, Strength });
}

void ACampusWater::Step()
{
	const int32 W = SimSize.X;
	const int32 H = SimSize.Y;
	if (Height.Num() != W * H)
	{
		return;
	}
	// The frame's disturbances: each a soft dent pressed into the surface.
	for (const FBrush& B : Pending)
	{
		const float Cx = B.At.X * W;
		const float Cy = B.At.Y * H;
		const int32 R = FMath::CeilToInt(B.Radius * 2.f);
		for (int32 y = FMath::Max(0, FMath::FloorToInt(Cy) - R); y <= FMath::Min(H - 1, FMath::FloorToInt(Cy) + R); ++y)
		{
			for (int32 x = FMath::Max(0, FMath::FloorToInt(Cx) - R); x <= FMath::Min(W - 1, FMath::FloorToInt(Cx) + R); ++x)
			{
				const float Dx = x + 0.5f - Cx;
				const float Dy = y + 0.5f - Cy;
				Height[y * W + x] -= B.Strength * FMath::Exp(-(Dx * Dx + Dy * Dy) / (B.Radius * B.Radius));
			}
		}
	}
	Pending.Reset();
	// Rain: drops landing all over, each a tiny pock.
	const int32 RainDrops = FMath::RoundToInt(RainAmount * W * H * 0.0006f);
	for (int32 i = 0; i < RainDrops; ++i)
	{
		const int32 x = Drops.RandRange(1, W - 2);
		const int32 y = Drops.RandRange(1, H - 2);
		Height[y * W + x] -= Drops.FRandRange(0.25f, 0.6f);
	}
	// The damped wave equation: each cell moves toward the mean of its neighbours, keeping its momentum. The cells off
	// the edge mirror the ones inside, so waves bounce back off the pool walls.
	const float Damping = 0.993f;
	for (int32 y = 0; y < H; ++y)
	{
		const int32 Up = y > 0 ? y - 1 : 1;
		const int32 Dn = y < H - 1 ? y + 1 : H - 2;
		for (int32 x = 0; x < W; ++x)
		{
			const int32 Lf = x > 0 ? x - 1 : 1;
			const int32 Rt = x < W - 1 ? x + 1 : W - 2;
			const float N = Height[y * W + Lf] + Height[y * W + Rt] + Height[Up * W + x] + Height[Dn * W + x];
			PrevHeight[y * W + x] = FMath::Clamp((N * 0.5f - PrevHeight[y * W + x]) * Damping, -4.f, 4.f);
		}
	}
	Swap(Height, PrevHeight);
	++Frame;
}

void ACampusWater::Upload()
{
	if (!SimTexture || !SimTexture->GetResource())
	{
		return;
	}
	// The render thread takes its own copy of the heights, freed once uploaded.
	const int32 W = SimSize.X;
	const int32 H = SimSize.Y;
	float* Copy = new float[W * H];
	FMemory::Memcpy(Copy, Height.GetData(), sizeof(float) * W * H);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, W, H);
	SimTexture->UpdateTextureRegions(0, 1, Region, W * sizeof(float), sizeof(float), reinterpret_cast<uint8*>(Copy),
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			delete[] reinterpret_cast<float*>(Data);
			delete Regions;
		});
	if (SurfaceMID)
	{
		SurfaceMID->SetTextureParameterValue(TEXT("SimTex"), SimTexture);
	}
}

void ACampusWater::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Height.Num() == 0)
	{
		return;
	}
	// Fixed steps, at most a few per frame (a hitch slows the water a moment rather than blowing it up).
	StepAccum = FMath::Min(StepAccum + DeltaSeconds, SimStep * 4.f);
	bool bStepped = false;
	while (StepAccum >= SimStep)
	{
		StepAccum -= SimStep;
		Step();
		bStepped = true;
	}
	if (bStepped)
	{
		Upload();
	}
}
