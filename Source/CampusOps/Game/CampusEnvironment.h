#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusTypes.h"
#include "CampusEnvironment.generated.h"

class UInstancedStaticMeshComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;

/**
 * Time of day and weather, applied on every machine (spawned locally by the map builder; not replicated itself).
 *
 * The match's conditions come from the game state (rolled or picked by the host); the menu rolls its own. Applying
 * them moves and colours the sun (or the moon at night), sets the sky light, the atmosphere's haze, the height fog,
 * the cloud cover and the grade, and drives the shared material parameters: wet ground (puddles only where the sky
 * reaches - a top-down depth capture of the map masks the covered floors) and the lit windows of the city at dusk and
 * night. Rain falls round the camera and splashes where it lands, storms flash and thunder, and each map has its own
 * ambient bed.
 */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusEnvironment : public AActor
{
	GENERATED_BODY()

public:
	ACampusEnvironment();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Forces conditions (automation / menu preview); the game state's are used otherwise. */
	void ForceConditions(ECampusWeather InWeather, ECampusTimeOfDay InTime);

	ECampusWeather GetWeather() const { return Weather; }
	ECampusTimeOfDay GetTimeOfDay() const { return TimeOfDay; }
	/** 0 dry .. 1 downpour, for footsteps and the HUD. */
	float GetRainAmount() const { return RainAmount; }
	/** whether rain can reach a point, including roofs with no collision */
	bool IsRainExposed(const FVector& Position) const;

	static ACampusEnvironment* Find(const UWorld* World);

private:
	void ResolveConditions(ECampusWeather& OutWeather, ECampusTimeOfDay& OutTime, int32& OutSeed) const;
	void Apply();
	void CaptureRainMask();
	void UpdateRain(float DeltaSeconds, const FVector& Camera);
	void UpdateLightning(float DeltaSeconds);
	/** Wind for the planting (shared material parameters WindX / WindY / WindStrength). */
	void UpdateWind();
	void UpdateAudio(float DeltaSeconds);
	void SetLoop(TObjectPtr<UAudioComponent>& Comp, FName Sound, float Volume, float DeltaSeconds);
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	ECampusMap Map = ECampusMap::Campus;
	ECampusWeather Weather = ECampusWeather::Clear;
	ECampusTimeOfDay TimeOfDay = ECampusTimeOfDay::Noon;
	int32 Seed = 0;
	bool bApplied = false;
	/** GI level the conditions were applied with (at the lowest two the sky light fills in for the bounce). */
	int32 AppliedGI = -1;
	bool bForced = false;
	bool bMaskCaptured = false;
	float MaskDelay = 0.f;
	float RainAmount = 0.f;
	float NightLights = 0.f;
	float Wetness = 0.f;
	float Clock = 0.f;

	// Lights and volumes found in the level (spawned by the bootstrap)
	TWeakObjectPtr<class UDirectionalLightComponent> Sun;
	TWeakObjectPtr<class USkyLightComponent> SkyLight;
	TWeakObjectPtr<class UExponentialHeightFogComponent> Fog;
	TWeakObjectPtr<class USkyAtmosphereComponent> Atmosphere;
	TWeakObjectPtr<class UVolumetricCloudComponent> Clouds;
	TWeakObjectPtr<class APostProcessVolume> Post;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	float BaseSunLux = 0.f;
	float BaseSkyIntensity = 1.f;
	TMap<TWeakObjectPtr<class ULocalLightComponent>, float> ClassroomLightLevels;

	// Rain: streaks falling in a column round the camera, dying on the first surface below them
	struct FDrop
	{
		FVector Pos;
		float Speed;
		float FloorZ;
		FIntPoint Cell;
	};
	TArray<FDrop> Drops;
	TArray<FTransform> RainTransforms;
	TArray<FTransform> SplashTransforms;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> RainMesh;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SplashMesh;
	/** Rain curtain cards following the camera at three distances. */
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SheetMesh;
	void UpdateRainSheets(const FVector& Camera);
	struct FSplash
	{
		FVector Pos;
		float Age;
	};
	TArray<FSplash> Splashes;
	/** height of an actual hit under each cell; missing surfaces are never cached as a plane */
	TMap<FIntPoint, float> FloorCache;
	/** CPU copy of the rain mask: height of the topmost surface per texel (rain stops there, whatever its collision). */
	TArray<float> MaskHeights;
	int32 MaskRes = 0;
	FVector2D MaskOrigin = FVector2D::ZeroVector;
	float MaskExtent = 0.f;
	/** Topmost surface at a point from the mask; false outside it. */
	bool MaskSurfaceAt(const FVector& P, float& OutZ) const;
	FVector WindDir = FVector(0.3f, 0.1f, 0.f);

	// Storm
	float NextStrike = 0.f;
	float StrikeTime = -100.f;
	float StrikeFlash = 0.f;
	float ThunderAt = -1.f;
	float ThunderVolume = 0.f;
	FRandomStream StormStream;

	// Ambient beds
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> AmbBed;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> RainOut;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> RainIn;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindBed;

	UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> MaskCapture;
};
