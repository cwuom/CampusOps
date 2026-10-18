#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusTypes.h"
#include "CampusThrowable.generated.h"

class ACampusCharacter;
class ACampusSmokeTrail;
class ACampusSparkEmitter;
class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class ULocalFogVolumeComponent;
class UAudioComponent;

/** Fuse, throw speed and model of each throwable. */
namespace CampusThrowables
{
	/** Seconds from the spoon flying off to the detonation (frag cooks from the pin pull). */
	CAMPUSOPS_API float Fuse(ECampusThrowable Type);
	CAMPUSOPS_API float ThrowSpeed(ECampusThrowable Type);
	CAMPUSOPS_API bool IsLethal(ECampusThrowable Type);
	/** Body mesh (lever and ring are separate parts so the spoon can fly and the pin come out). */
	CAMPUSOPS_API UStaticMesh* BodyMesh(ECampusThrowable Type);
	CAMPUSOPS_API UStaticMesh* LeverMesh(ECampusThrowable Type);
	CAMPUSOPS_API UStaticMesh* RingMesh();
	/** Builds the model (body, lever, ring) under Parent, frame: +X up through the fuse, origin at the body centre. */
	CAMPUSOPS_API void BuildModel(AActor* Owner, USceneComponent* Parent, ECampusThrowable Type, bool bLever, bool bRing, bool bFirstPerson, TArray<TObjectPtr<UStaticMeshComponent>>& OutParts);
	/** Spoon flicked off a grenade when it leaves the hand. */
	CAMPUSOPS_API void SpawnSpoon(UWorld* World, ECampusThrowable Type, const FTransform& At, const FVector& Velocity);
	/** Ball radius of the body (cm). */
	CAMPUSOPS_API float Radius(ECampusThrowable Type);
	/**
	 * Attaches Root to Mesh's hand_l with the grenade seated in the curl of the fingers: in the palm, the fuse up past
	 * the forefinger, the lever under the fingers. Extra (grenade frame) nudges it.
	 */
	CAMPUSOPS_API void SeatInLeftHand(USkeletalMeshComponent* Mesh, USceneComponent* Root, ECampusThrowable Type, const FTransform* Extra = nullptr);
}

/** The safety lever flicked off a grenade as it leaves the hand: tumbles away and clinks on the floor. */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusSpoon : public AActor
{
	GENERATED_BODY()

public:
	ACampusSpoon();
	void Init(UStaticMesh* InMesh, const FVector& Velocity);

private:
	UFUNCTION() void OnSpoonHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	int32 Bounces = 0;
};

/**
 * A thrown grenade. The server simulates the flight (sphere sweeps, bounces, rolls to rest; thermite sticks) and
 * detonates it; movement replicates. Every machine plays the detonation from the replicated state, and smoke clouds
 * and thermite keep burning on every machine until they are spent.
 */
UCLASS(NotPlaceable)
class CAMPUSOPS_API ACampusThrowable : public AActor
{
	GENERATED_BODY()

public:
	ACampusThrowable();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Server: send it on its way. FuseLeft is what remains of the fuse (a cooked frag has less). */
	void Launch(ECampusThrowable InType, ACampusCharacter* InThrower, const FVector& Velocity, float FuseLeft);

	ECampusThrowable GetType() const { return Type; }
	bool HasDetonated() const { return bDetonated; }
	/** Seconds until it goes off (server estimate on clients). */
	float GetFuseLeft() const { return FuseRemaining; }
	FVector GetDetonationPoint() const { return DetonationPoint; }
	/** Smoke grenades: current cloud radius (0 until it pops and once it has cleared). */
	float GetSmokeRadius() const;
	/** Smoke grenades: how thick the cloud is now (0..1). */
	float GetSmokeDensity() const;

	/** How much of the line From-To is hidden by smoke clouds (0 clear .. 1 blind). */
	static float SmokeOcclusion(const UWorld* World, const FVector& From, const FVector& To);
	/** Every frag still waiting to go off. */
	static void GetLiveFrags(const UWorld* World, TArray<ACampusThrowable*>& Out);

	UFUNCTION(NetMulticast, Unreliable) void MulticastBounce(FVector_NetQuantize Location, float Strength, uint8 Surface);

protected:
	UFUNCTION() void OnRep_Type();
	UFUNCTION() void OnRep_Detonated();

	UPROPERTY(ReplicatedUsing = OnRep_Type) ECampusThrowable Type = ECampusThrowable::Frag;
	UPROPERTY(ReplicatedUsing = OnRep_Detonated) bool bDetonated = false;
	UPROPERTY(Replicated) FVector_NetQuantize DetonationPoint = FVector::ZeroVector;
	UPROPERTY(Replicated) FVector_NetQuantizeNormal DetonationNormal = FVector::UpVector;
	UPROPERTY(Replicated) float FuseRemaining = 3.f;

	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY(Transient) TObjectPtr<ULocalFogVolumeComponent> Fog;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Loop;
	UPROPERTY(Transient) TObjectPtr<ACampusSmokeTrail> Plume;
	UPROPERTY(Transient) TObjectPtr<ACampusSparkEmitter> Sparks;

private:
	void BuildVisuals();
	void Simulate(float DeltaSeconds);
	void OnImpact(const FHitResult& Hit, float Speed);
	void CheckGlass(const FVector& From, const FVector& To);
	/** Server. */
	void Detonate();
	void ApplyFragDamage();
	void ApplyFlash();
	void TickThermiteDamage(float DeltaSeconds);
	/** Everywhere but a dedicated server. */
	void PlayDetonationFX();
	void PlayFragFX();
	void PlayFlashFX();
	void TickSmoke(float DeltaSeconds);
	void TickThermite(float DeltaSeconds);
	void ShakeLocalViewer(float Radius, float Strength, bool bDeafen);
	float EffectAge() const;

	TWeakObjectPtr<ACampusCharacter> Thrower;
	TWeakObjectPtr<AController> InstigatorController;
	FVector Velocity = FVector::ZeroVector;
	FVector Spin = FVector::ZeroVector;
	float FlightTime = 0.f;
	bool bResting = false;
	bool bStuck = false;
	bool bFXPlayed = false;
	double DetonatedAt = 0.0;
	float DamageTick = 0.f;
	float EmitTimer = 0.f;
	float LastBounceTime = -1.f;
	FVector GlassCheckFrom = FVector::ZeroVector;
	TWeakObjectPtr<ACampusCharacter> StuckToCharacter;
};
