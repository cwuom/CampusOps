#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusTypes.h"
#include "CampusFX.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Fast glowing streak travelling from muzzle to impact. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusTracer : public AActor
{
	GENERATED_BODY()

public:
	ACampusTracer();
	void Init(const FVector& InStart, const FVector& InEnd, const FLinearColor& Color, float InSpeed);
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Streak;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	float Distance = 0.f;
	float Travelled = 0.f;
	float Speed = 30000.f;
	float Length = 220.f;
};

/** Tiny CPU particle system rendered with one instanced mesh (sparks, dust, blood, debris). */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusParticleBurst : public AActor
{
	GENERATED_BODY()

public:
	ACampusParticleBurst();
	void Init(const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& Color, float Emissive, float Speed, float Size, float Life, float Gravity, float Spread);
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Instances;
	TArray<FVector> Positions;
	TArray<FVector> Velocities;
	TArray<FRotator> Rotations;
	TArray<FRotator> Spins;
	TArray<float> Sizes;
	float Age = 0.f;
	float Lifetime = 0.5f;
	float GravityZ = -980.f;
};

/** Camera-facing soft sprites (dust puffs, blood mist, muzzle smoke) that grow, drift and fade. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusSpriteBurst : public AActor
{
	GENERATED_BODY()

public:
	ACampusSpriteBurst();
	/** Count sprites spread in a cone around Direction; sizes in cm. */
	void Init(const FVector& Location, const FVector& Direction, int32 Count, const FLinearColor& Color, float Opacity, float Speed, float StartSize, float EndSize, float Life, float Spread, float Rise);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FSprite
	{
		FVector Position;
		FVector Velocity;
		float Size0;
		float Size1;
		float Spin;
		float SpinRate;
		float Life;
	};
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Instances;
	TArray<FSprite> Sprites;
	FLinearColor Color = FLinearColor::White;
	float Opacity = 0.5f;
	float Rise = 0.f;
	float Age = 0.f;
	float Lifetime = 1.f;
};

/** Persistent soft-sprite emitter fed one puff at a time: the smoke curling off a hot barrel. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusSmokeTrail : public AActor
{
	GENERATED_BODY()

public:
	ACampusSmokeTrail();
	void Emit(const FVector& Position, const FVector& Velocity, float StartSize, float EndSize, float Life, float Opacity);
	/** Colour, buoyancy (cm/s^2), drag (1/s), curl turbulence (cm/s^2) and puff cap; the default is thin barrel smoke. */
	void SetStyle(const FLinearColor& InColor, float InRise, float InDrag, float InCurl, int32 InMaxPuffs);
	bool IsIdle() const { return Puffs.Num() == 0; }
	int32 NumPuffs() const { return Puffs.Num(); }
	virtual void Tick(float DeltaSeconds) override;

private:
	FLinearColor Color = FLinearColor(0.62f, 0.62f, 0.64f);
	float Rise = 14.f;
	float DragRate = 2.4f;
	float Curl = 46.f;
	int32 MaxPuffs = 160;
	struct FPuff
	{
		FVector Position;
		FVector Velocity;
		float Size0;
		float Size1;
		float Spin;
		float SpinRate;
		float Life;
		float Age;
		float Opacity;
		float Seed;
	};
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Instances;
	TArray<FPuff> Puffs;
	TArray<FTransform> Transforms;
};

/** Persistent glowing sparks (thermite, molten slag): streaks that fall, skip off the floor and cool from white to red. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusSparkEmitter : public AActor
{
	GENERATED_BODY()

public:
	ACampusSparkEmitter();
	void Emit(const FVector& Position, const FVector& Velocity, float Life, float Size);
	/** Height of the floor the sparks land on. */
	void SetFloor(float Z) { FloorZ = Z; }
	bool IsIdle() const { return Sparks.Num() == 0; }
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FSpark { FVector Position; FVector Velocity; float Age; float Life; float Size; };
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Hot;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Cool;
	TArray<FSpark> Sparks;
	TArray<FTransform> HotT;
	TArray<FTransform> CoolT;
	float FloorZ = -1.e6f;
};

/** Clods thrown out by a blast: each arcs under gravity, bounces off whatever it hits and trails a streamer of dust. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusDebrisStreamers : public AActor
{
	GENERATED_BODY()

public:
	ACampusDebrisStreamers();
	void Init(const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& ChunkColor, const FLinearColor& DustColor, float Speed, float Spread);
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;

private:
	struct FChunk { FVector Position; FVector Velocity; FRotator Rotation; FRotator Spin; float Size; float EmitClock; bool bResting; };
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Instances;
	UPROPERTY() TObjectPtr<ACampusSmokeTrail> Dust;
	TArray<FChunk> Chunks;
	float Age = 0.f;
};

/** Expanding, cooling fireball sprites (additive, per-instance heat): the flash of a detonation. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusFireBurst : public AActor
{
	GENERATED_BODY()

public:
	ACampusFireBurst();
	void Init(const FVector& Location, const FVector& Up, int32 Count, float Radius, float Life, float Brightness);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FFlame { FVector Position; FVector Velocity; float Size0; float Size1; float Spin; float Life; float Heat; };
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Instances;
	TArray<FFlame> Flames;
	float Age = 0.f;
	float Brightness = 1.f;
};

/** A point light that fades out (explosions, flashbangs). */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusFlashLight : public AActor
{
	GENERATED_BODY()

public:
	ACampusFlashLight();
	void Init(const FLinearColor& Color, float Candelas, float Radius, float Duration, bool bShadows);
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<class UPointLightComponent> Light;
	float Age = 0.f;
	float Duration = 0.2f;
	float Peak = 1000.f;
};

/** Physically simulated brass casing. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusShellCasing : public AActor
{
	GENERATED_BODY()

public:
	ACampusShellCasing();
	void Init(const FVector& Velocity, bool bPistol);

private:
	UFUNCTION() void OnShellHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Shell;
	int32 Bounces = 0;
};

namespace CampusFX
{
	CAMPUSOPS_API ECampusSurface ResolveSurface(const FHitResult& Hit);
	CAMPUSOPS_API ECampusSurface ResolveSurface(const UPrimitiveComponent* Component, const AActor* Actor);
	CAMPUSOPS_API void SpawnTracer(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color = FLinearColor(1.f, 0.72f, 0.38f), float Speed = 32000.f);
	CAMPUSOPS_API void SpawnImpact(UWorld* World, const FVector& Location, const FVector& Normal, ECampusSurface Surface, bool bPlaySound = true);
	CAMPUSOPS_API void SpawnShell(UWorld* World, const FTransform& Eject, const FVector& InheritVelocity, bool bPistol);
	CAMPUSOPS_API void SpawnSprites(UWorld* World, const FVector& Location, const FVector& Direction, int32 Count, const FLinearColor& Color, float Opacity, float Speed, float StartSize, float EndSize, float Life, float Spread, float Rise = 0.f);
	/** Blood mist and droplets where a bullet strikes a character; Direction is the bullet direction. */
	CAMPUSOPS_API void SpawnBloodHit(UWorld* World, const FVector& Location, const FVector& Direction, bool bHeadshot);
	CAMPUSOPS_API void SpawnMuzzleSmoke(UWorld* World, const FVector& Location, const FVector& Forward, float Amount);
	CAMPUSOPS_API FName ImpactSound(ECampusSurface Surface);
	/** Debris chunks and dust thrown from a point (Count chunks); Emissive > 0 makes them glow. */
	CAMPUSOPS_API void SpawnDebris(UWorld* World, const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& Color, float Emissive, float Speed, float Size, float Life, float Gravity, float Spread);
	CAMPUSOPS_API void SpawnFireball(UWorld* World, const FVector& Location, const FVector& Up, int32 Count, float Radius, float Life, float Brightness);
	/** Count clods thrown in a cone (Spread degrees) around Normal, each trailing dust. */
	CAMPUSOPS_API void SpawnDebrisStreamers(UWorld* World, const FVector& Location, const FVector& Normal, int32 Count, const FLinearColor& ChunkColor, const FLinearColor& DustColor, float Speed, float Spread);
	/** A frag/explosive blast: fireball, hot fragments, thrown clods, ground shock dust and a lingering sooty pall. Scale 1 is a hand grenade. */
	CAMPUSOPS_API void SpawnExplosion(UWorld* World, const FVector& Location, const FVector& Normal, float Scale, AActor* Ignore = nullptr);
	/** Blackened burn mark projected onto the surface under Location (Normal points out of it). */
	CAMPUSOPS_API void SpawnScorch(UWorld* World, const FVector& Location, const FVector& Normal, float Size, float Opacity, float LifeSpan);
	/** Brief point light (no shadows unless asked), fading out over Duration. */
	CAMPUSOPS_API void SpawnFlashLight(UWorld* World, const FVector& Location, const FLinearColor& Color, float Candelas, float Radius, float Duration, bool bShadows = false);
}
