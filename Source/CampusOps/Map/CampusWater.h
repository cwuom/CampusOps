#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusWater.generated.h"

class UStaticMeshComponent;
class UDecalComponent;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;

/**
 * A body of open water (the hotel's pool): the translucent surface, the volume people wade in, and a live simulation
 * of its surface. A damped wave equation runs on a height field over the whole pool every frame (on the GPU, two
 * render targets in turn): wading legs leave wakes, splashes and rounds throw out rings, rain pocks it all over, and
 * every wave travels, spreads, interferes and comes back off the pool walls. The surface shades its normals and foam
 * from it; sunlight focused by it plays over the pool floor as caustics.
 *
 * Spawned locally on every machine by the map builder. Wading speed is worked out from the same box everywhere; the
 * simulation and its look are local only.
 */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusWater : public AActor
{
	GENERATED_BODY()

public:
	ACampusWater();

	/** The water fills Volume up to its top face. */
	void SetVolume(const FBox& Volume);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** How deep Feet stands in water (0 when dry); OutSurfaceZ is the water line there. */
	static float DepthAt(const UWorld* World, const FVector& Feet, float* OutSurfaceZ = nullptr);
	/** Pushes the surface at Location over Radius cm; Strength > 0 pushes it down (a body, a splash, a round). */
	static void AddRipple(UWorld* World, const FVector& Location, float Radius, float Strength);
	/** Rain on open water, Amount 0..1: drops pock the surface everywhere. */
	static void Rain(UWorld* World, float Amount, float DeltaSeconds, const FVector& Camera);
	/** How strongly the sun casts caustics through the water (0 overcast or at night). */
	static void SetSunlight(UWorld* World, float Strength);

private:
	struct FBrush
	{
		FVector2D At;
		float Radius;
		float Strength;
	};
	void Push(const FVector& Location, float Radius, float Strength);
	void Step();

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Surface;
	UPROPERTY() TObjectPtr<class UBoxComponent> HitBox;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Caustics;
	/** The height field as the surface material samples it (one float per cell, uploaded every frame). */
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> SimTexture;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CausticMID;
	void Upload();
	FBox Box = FBox(ForceInit);
	FIntPoint SimSize = FIntPoint(128, 640);
	/** Heights now and one step ago (row-major, X across). */
	TArray<float> Height;
	TArray<float> PrevHeight;
	FRandomStream Drops;
	TArray<FBrush> Pending;
	float RainAmount = 0.f;
	float StepAccum = 0.f;
	int32 Frame = 0;
	static TArray<TWeakObjectPtr<ACampusWater>> All;
	static float SunStrength;
};
