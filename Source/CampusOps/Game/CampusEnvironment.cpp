#include "Game/CampusEnvironment.h"
#include "Game/CampusGameState.h"
#include "Game/CampusMaps.h"
#include "Game/CampusGraphics.h"
#include "Map/CampusSchoolBuilder.h"
#include "Map/CampusWater.h"
#include "CampusAssets.h"
#include "CampusSpring.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/AudioComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Sound/SoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** One time of day: where the sun (or the moon) stands and how the sky, haze and exposure respond. */
	struct FTimePreset
	{
		float SunPitch;
		float SunYaw;
		float SunLux;
		float SunTemp;
		float SkyIntensity;
		float ExposureMin;
		float ExposureMax;
		float ExposureBias;
		float FogDensity;
		float FogFalloff;
		FLinearColor FogColor;
		FLinearColor FogSunColor;
		float Mie;
		float Aerial;
		float CloudCoverage;
		float WhiteTemp;
		float Saturation;
		float Contrast;
		float Vignette;
		float NightLights;
		float Bloom;
	};

	const FTimePreset& TimePreset(ECampusTimeOfDay Time)
	{
		static const FTimePreset Table[(int32)ECampusTimeOfDay::Count] = {
			// Morning: low sun out of the east-south-east, cool haze, long soft shadows.
			{ -14.f, 200.f, 32000.f, 4600.f, 1.0f, 6.5f, 15.f, 0.2f, 0.034f, 0.15f, FLinearColor(0.42f, 0.47f, 0.56f), FLinearColor(0.62f, 0.5f, 0.38f), 0.026f, 1.6f, 0.38f, 6300.f, 1.0f, 1.04f, 0.25f, 0.f, 0.55f },
			// Noon: high subtropical sun, hard shadows, bright humid haze.
			{ -72.f, 100.f, 100000.f, 5900.f, 1.15f, 8.f, 15.5f, 0.3f, 0.026f, 0.12f, FLinearColor(0.34f, 0.43f, 0.58f), FLinearColor(0.55f, 0.47f, 0.36f), 0.02f, 1.8f, 0.42f, 6500.f, 1.0f, 1.05f, 0.2f, 0.f, 0.5f },
			// Afternoon: sun in the south-west, warmer, the haze golden toward it.
			{ -36.f, 48.f, 82000.f, 5300.f, 1.1f, 8.f, 15.5f, 0.3f, 0.03f, 0.12f, FLinearColor(0.38f, 0.44f, 0.55f), FLinearColor(0.64f, 0.5f, 0.34f), 0.022f, 1.7f, 0.45f, 6400.f, 1.02f, 1.05f, 0.22f, 0.f, 0.55f },
			// Dusk: the sun on the western horizon, long and low, the city's windows and street lamps coming on.
			// Blue hour away from the sun: the haze slate blue, the city cool and clear; the orange is a seam along the
			// horizon toward the sun, not a sepia wash over everything.
			// The sun still up: offices lit, the homes only beginning to.
			{ -4.f, -8.f, 6500.f, 3400.f, 1.15f, 3.f, 14.f, 0.f, 0.022f, 0.12f, FLinearColor(0.22f, 0.27f, 0.40f), FLinearColor(0.62f, 0.36f, 0.18f), 0.008f, 0.8f, 0.38f, 6200.f, 1.04f, 1.07f, 0.35f, 0.5f, 0.85f },
			// Night: a high moon (the same light, faint and cold), the city and the lamps carrying the scene. The eye does
			// not open up far enough to turn moonlit walls day-bright: the city stays dark round its lit windows.
			{ -48.f, 150.f, 0.45f, 7800.f, 1.0f, 0.f, 5.f, -0.3f, 0.02f, 0.14f, FLinearColor(0.035f, 0.045f, 0.07f), FLinearColor(0.06f, 0.07f, 0.1f), 0.012f, 1.0f, 0.35f, 7000.f, 0.92f, 1.06f, 0.4f, 1.f, 0.9f },
		};
		return Table[FMath::Clamp((int32)Time, 0, (int32)ECampusTimeOfDay::Count - 1)];
	}

	/** How a weather scales or overrides the time of day's look. */
	struct FWeatherMod
	{
		float SunScale;
		float SkyScale;
		float Coverage;
		float Density;
		float Storm;
		float FogScale;
		float MieScale;
		float Saturation;
		float LightAngle;
		float Rain;
		float Wet;
		float NightLightsAdd;
		float ExposureBias;
	};

	const FWeatherMod& WeatherMod(ECampusWeather Weather)
	{
		static const FWeatherMod Table[(int32)ECampusWeather::Count] = {
			{ 1.f, 1.f, -0.15f, 1.f, 0.f, 1.f, 1.f, 1.f, 0.535f, 0.f, 0.f, 0.f, 0.f },        // Clear
			{ 0.85f, 1.05f, 0.25f, 1.f, 0.f, 1.2f, 1.3f, 0.97f, 1.4f, 0.f, 0.f, 0.f, 0.f },  // Cloudy
			// Under a full cloud deck the sun is gone as a direction: soft shadowless light from the whole sky.
			{ 0.035f, 1.35f, 0.62f, 1.6f, 0.2f, 2.2f, 3.f, 0.86f, 20.f, 0.f, 0.15f, 0.25f, -0.35f }, // Overcast
			// Rain is a gloomy light: the eye does not lift a grey downpour to a bright day, wet stone stays dark.
			{ 0.025f, 1.2f, 0.62f, 1.8f, 0.45f, 2.6f, 3.5f, 0.82f, 25.f, 0.6f, 0.8f, 0.12f, -0.75f }, // Rain
			{ 0.015f, 0.85f, 0.7f, 2.2f, 1.f, 3.f, 4.f, 0.74f, 30.f, 1.f, 1.f, 0.18f, -0.95f }, // Storm
			{ 0.4f, 1.15f, 0.3f, 1.2f, 0.f, 9.f, 2.f, 0.84f, 4.f, 0.f, 0.3f, 0.15f, -0.35f },  // Fog
		};
		return Table[FMath::Clamp((int32)Weather, 0, (int32)ECampusWeather::Count - 1)];
	}

	float GroundZ(ECampusMap Map)
	{
		return Map == ECampusMap::Hotel ? -18000.f : 0.f;
	}

	void SetEnv(UObject* Ctx, FName Param, float Value)
	{
		if (UMaterialParameterCollection* MPC = CampusAssets::EnvironmentCollection())
		{
			UKismetMaterialLibrary::SetScalarParameterValue(Ctx, MPC, Param, Value);
		}
	}
}

ACampusEnvironment::ACampusEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bReplicates = false;
}

ACampusEnvironment* ACampusEnvironment::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<ACampusEnvironment> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void ACampusEnvironment::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (const ACampusSchoolBuilder* Builder = ACampusSchoolBuilder::Find(World))
	{
		Map = Builder->MapKind;
	}
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		break;
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		SkyLight = It->GetLightComponent();
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		Fog = It->GetComponent();
		break;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!Atmosphere.IsValid())
		{
			Atmosphere = It->FindComponentByClass<USkyAtmosphereComponent>();
		}
		if (!Clouds.IsValid())
		{
			Clouds = It->FindComponentByClass<UVolumetricCloudComponent>();
		}
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->bUnbound)
		{
			Post = *It;
			break;
		}
	}
	if (Clouds.IsValid() && Clouds->GetMaterial())
	{
		CloudMID = UMaterialInstanceDynamic::Create(Clouds->GetMaterial(), this);
		Clouds->SetMaterial(CloudMID);
	}

	UStaticMesh* Cube = CampusAssets::Shape(ECampusShape::Cube);
	UStaticMesh* Ball = CampusAssets::Shape(ECampusShape::Sphere);
	UMaterialInterface* RainMat = CampusAssets::RainMaterial();
	auto MakeFX = [this](UStaticMesh* Mesh, UMaterialInterface* Mat, int32 CustomFloats)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetupAttachment(RootComponent);
		ISM->SetStaticMesh(Mesh);
		if (Mat)
		{
			ISM->SetMaterial(0, UMaterialInstanceDynamic::Create(Mat, this));
		}
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->bReceivesDecals = false;
		ISM->SetCanEverAffectNavigation(false);
		ISM->bVisibleInRayTracing = false;
		ISM->bAffectDistanceFieldLighting = false;
		ISM->bAffectDynamicIndirectLighting = false;
		ISM->NumCustomDataFloats = CustomFloats;
		ISM->RegisterComponent();
		return ISM;
	};
	RainMesh = MakeFX(Cube, RainMat, 0);
	SplashMesh = MakeFX(Ball, RainMat, 1);
	SheetMesh = MakeFX(Cube, CampusAssets::RainSheetMaterial(), 4);
	MaskDelay = 0.6f;
}

void ACampusEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (UAudioComponent* A : { AmbBed.Get(), RainOut.Get(), RainIn.Get(), WindBed.Get() })
	{
		if (A)
		{
			A->Stop();
		}
	}
	SetEnv(this, TEXT("Wetness"), 0.f);
	SetEnv(this, TEXT("Rain"), 0.f);
	Super::EndPlay(EndPlayReason);
}

void ACampusEnvironment::ForceConditions(ECampusWeather InWeather, ECampusTimeOfDay InTime)
{
	bForced = true;
	Weather = InWeather;
	TimeOfDay = InTime;
	Apply();
}

void ACampusEnvironment::ResolveConditions(ECampusWeather& OutWeather, ECampusTimeOfDay& OutTime, int32& OutSeed) const
{
	if (const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>())
	{
		OutWeather = GS->Weather;
		OutTime = GS->TimeOfDay;
		OutSeed = GS->WeatherSeed;
		return;
	}
	// The menu has no match: roll this map's odds once per visit.
	static int32 MenuSeed = FMath::Rand();
	FRandomStream Stream(MenuSeed);
	OutWeather = CampusMaps::ResolveWeather(Map, -1, Stream);
	OutTime = CampusMaps::ResolveTime(Map, -2, Stream);
	OutSeed = MenuSeed;
}

void ACampusEnvironment::Apply()
{
	bApplied = true;
	const FTimePreset& T = TimePreset(TimeOfDay);
	const FWeatherMod& W = WeatherMod(Weather);
	const float Ground = GroundZ(Map);
	const bool bNight = TimeOfDay == ECampusTimeOfDay::Night;
	const bool bCampusStormNight = Map == ECampusMap::Campus && bNight && Weather == ECampusWeather::Storm;

	if (UDirectionalLightComponent* S = Sun.Get())
	{
		S->SetMobility(EComponentMobility::Movable);
		S->GetOwner()->SetActorRotation(FRotator(T.SunPitch, T.SunYaw, 0.f));
		BaseSunLux = T.SunLux * W.SunScale;
		if (bCampusStormNight)
		{
			BaseSunLux = 0.18f;
		}
		S->SetIntensity(BaseSunLux);
		S->SetUseTemperature(true);
		S->SetTemperature(T.SunTemp);
		S->SetLightSourceAngle(W.LightAngle);
		// A low sun pours straight through windows into any smoke indoors: kept moderate so a screen reads as smoke lit
		// by the evening light, not as a fire.
		const bool bLowSun = TimeOfDay == ECampusTimeOfDay::Dusk || TimeOfDay == ECampusTimeOfDay::Morning;
		// In a fog bank little sunlight gets in to light it: lit at full strength the whole bank glows white.
		S->SetVolumetricScatteringIntensity(Weather == ECampusWeather::Fog ? 0.25f : (bLowSun ? 0.35f : 0.7f));
		// Cloud shadows only read under a broken sky; a full overcast just dims everything.
		S->bCastCloudShadows = W.Coverage < 0.5f;
		S->MarkRenderStateDirty();
	}
	// Rain or a full cloud deck: the light from the sky is the grey of the clouds, not the colour of the hour.
	const float Wet = W.Rain > 0.f ? 1.f : (W.Coverage > 0.5f ? 0.6f : 0.f);
	// Without Lumen nothing bounces, and Lumen's coarse irradiance field (medium) carries little of it: the shade gets
	// the sky alone. By day under a clear sky that is most of it; in the rain, the storm and at night the bounce of the
	// lit yard and the street lamps on the walls is most of the light, and without it the streets went black. The sky
	// light stands in for it, warmed at night by the lamps.
	AppliedGI = CampusGraphics::GlobalIllumination();
	const bool bFillBounce = AppliedGI <= 1;
	static const float TimeFill[(int32)ECampusTimeOfDay::Count] = { 1.1f, 1.f, 1.f, 1.2f, 6.f };
	static const float WeatherFill[(int32)ECampusWeather::Count] = { 1.f, 1.1f, 1.3f, 1.6f, 2.8f, 1.4f };
	const float Fill = !bFillBounce ? 1.f : FMath::Min(TimeFill[FMath::Clamp((int32)TimeOfDay, 0, (int32)ECampusTimeOfDay::Count - 1)]
		* WeatherFill[FMath::Clamp((int32)Weather, 0, (int32)ECampusWeather::Count - 1)], 8.f);
	if (USkyLightComponent* Sky = SkyLight.Get())
	{
		BaseSkyIntensity = T.SkyIntensity * W.SkyScale * Fill;
		if (bCampusStormNight)
		{
			BaseSkyIntensity *= bFillBounce ? 1.5f : 1.55f;
		}
		Sky->SetIntensity(BaseSkyIntensity);
		// With the sun on the horizon the captured sky is mostly its orange rim and sunset-lit cloud, which would wash
		// every shadow sepia. The open sky overhead is blue at that hour: the fill goes cool, only direct sun is warm.
		const FLinearColor HourTint = TimeOfDay == ECampusTimeOfDay::Dusk ? FLinearColor(0.56f, 0.7f, 1.f)
			: (TimeOfDay == ECampusTimeOfDay::Morning ? FLinearColor(0.78f, 0.86f, 1.f) : FLinearColor::White);
		FLinearColor SkyColor = FMath::Lerp(HourTint, FLinearColor(0.72f, 0.84f, 1.f), Wet);
		if (bFillBounce && bNight)
		{
			SkyColor = FMath::Lerp(SkyColor, FLinearColor(1.f, 0.78f, 0.55f), 0.45f);
		}
		Sky->SetLightColor(SkyColor);
	}
	if (USkyAtmosphereComponent* A = Atmosphere.Get())
	{
		A->SetMieScatteringScale(T.Mie * W.MieScale);
		A->SetAerialPespectiveViewDistanceScale(T.Aerial * (Weather == ECampusWeather::Fog ? 2.f : 1.f));
		// The atmosphere tints the height fog with its own colour; with the sun on the horizon that is the sunset's,
		// and the whole city would sit in a sepia soup. The haze keeps its own slate blue then.
		// Under a rain deck no sunset colour reaches the haze at all: it is the grey of the cloud.
		const float HourFog = TimeOfDay == ECampusTimeOfDay::Dusk ? 0.2f : (TimeOfDay == ECampusTimeOfDay::Morning ? 0.5f : 1.f);
		// A fog bank is lit by the sky: without it the fog has no light of its own and only darkens what it swallows. At
		// full strength it glowed so bright that from a shaded arcade the whole view washed out.
		A->SetHeightFogContribution(Weather == ECampusWeather::Fog ? 0.6f * HourFog : (W.Rain > 0.f ? 0.f : FMath::Lerp(HourFog, 0.3f, FMath::Clamp(W.Coverage, 0.f, 1.f) * 0.5f)));
	}
	if (UVolumetricCloudComponent* C = Clouds.Get())
	{
		C->SetLayerBottomAltitude(W.Coverage > 0.5f ? 1.4f : (bNight ? 4.f : 3.2f));
		C->SetLayerHeight(W.Coverage > 0.5f ? 6.f : 3.5f);
		if (CloudMID)
		{
			CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), FMath::Clamp(T.CloudCoverage + W.Coverage, 0.05f, 1.f));
			CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), W.Density);
			CloudMID->SetScalarParameterValue(TEXT("StormClouds"), W.Storm);
		}
	}
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		F->SetMobility(EComponentMobility::Movable);
		const bool bLowSunFog = TimeOfDay == ECampusTimeOfDay::Dusk || TimeOfDay == ECampusTimeOfDay::Morning;
		float FogZ = Ground - 100.f;
		float Falloff = T.FogFalloff;
		// From 180 m up the haze lies below, between here and the far side of the basin: a little thinner, so the
		// skyline reads out to the hills.
		float Density = T.FogDensity * W.FogScale * (Map == ECampusMap::Hotel ? 0.7f : 1.f);
		if (Weather == ECampusWeather::Fog)
		{
			// The hotel stands in a cloud bank that has swallowed the city below; the school is socked in at ground level.
			FogZ = Map == ECampusMap::Hotel ? -1800.f : -100.f;
			Falloff = Map == ECampusMap::Hotel ? 0.32f : 0.06f;
			Density = Map == ECampusMap::Hotel ? 0.6f : 0.22f;
		}
		else if (Map == ECampusMap::Hotel && W.Rain > 0.f)
		{
			// Rain from 180 m up: a grey curtain thickening toward the street, the towers a few blocks off softening
			// to silhouettes and the far side of the basin gone.
			FogZ = Ground + 15000.f;
			Falloff = 0.12f;
			Density = Weather == ECampusWeather::Storm ? 0.16f : 0.11f;
		}
		F->SetWorldLocation(FVector(0.f, 0.f, FogZ));
		F->SetFogDensity(Density);
		F->SetFogHeightFalloff(Falloff);
		F->SetStartDistance(Weather == ECampusWeather::Fog ? 0.f : 600.f);
		const bool bFogBank = Weather == ECampusWeather::Fog;
		F->SetFogMaxOpacity(bFogBank ? 0.95f : 0.9f);
		const FLinearColor Grey = FMath::Lerp(T.FogColor, FLinearColor(T.FogColor.GetLuminance() * 1.1f, T.FogColor.GetLuminance() * 1.12f, T.FogColor.GetLuminance() * 1.15f), FMath::Clamp(W.Coverage * 1.4f, 0.f, 1.f));
		// A fog bank is a soft neutral grey on top of the sky's light.
		const FLinearColor FogBankGrey = FLinearColor(0.4f, 0.42f, 0.45f) * (bNight ? 0.15f : (TimeOfDay == ECampusTimeOfDay::Dusk ? 0.6f : 1.f));
		F->SetFogInscatteringColor(bFogBank ? FogBankGrey : FMath::Lerp(Grey, FLinearColor(0.2f, 0.23f, 0.27f) * (bNight ? 0.2f : 1.f), Wet));
		// Inside the fog the sun is only a pale disc: little of the forward glow round it.
		F->SetDirectionalInscatteringColor(T.FogSunColor * W.SunScale * (bFogBank ? 0.2f : 1.f));
		// A low sun glows in a tight lobe round it (the seam along the horizon) instead of tinting half the sky.
		F->SetDirectionalInscatteringExponent(bLowSunFog ? 18.f : 8.f);
		F->SetDirectionalInscatteringStartDistance(2500.f);
		F->SetVolumetricFog(true);
		F->SetVolumetricFogScatteringDistribution(Weather == ECampusWeather::Fog ? 0.3f : 0.6f);
		F->SetVolumetricFogExtinctionScale(Weather == ECampusWeather::Fog ? 2.5f : (W.Rain > 0.f ? 1.4f : 0.6f));
		F->SetVolumetricFogDistance(Weather == ECampusWeather::Fog ? 4000.f : 6000.f);
		// Fog droplets scatter nearly all the light that reaches them, a touch cool.
		F->SetVolumetricFogAlbedo(bFogBank ? FColor(232, 235, 240) : FColor::White);
	}
	if (APostProcessVolume* P = Post.Get())
	{
		FPostProcessSettings& S = P->Settings;
		S.bOverride_AutoExposureMinBrightness = true;
		S.AutoExposureMinBrightness = T.ExposureMin + (W.SunScale < 0.5f ? -1.f : 0.f);
		S.bOverride_AutoExposureMaxBrightness = true;
		S.AutoExposureMaxBrightness = T.ExposureMax - (W.SunScale < 0.5f ? 1.5f : 0.f);
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = T.ExposureBias + W.ExposureBias + (bCampusStormNight ? 0.8f : 0.f);
		// avoid pumping when a bright fixture enters the view, while keeping indoor adaptation responsive
		S.bOverride_AutoExposureSpeedUp = true;
		S.AutoExposureSpeedUp = 3.f;
		S.bOverride_AutoExposureSpeedDown = true;
		S.AutoExposureSpeedDown = bCampusStormNight ? 3.8f : 1.5f;
		S.bOverride_LocalExposureHighlightContrastScale = true;
		S.LocalExposureHighlightContrastScale = bNight ? 0.72f : 0.8f;
		S.bOverride_LocalExposureShadowContrastScale = true;
		S.LocalExposureShadowContrastScale = bNight ? 0.72f : 0.82f;
		S.bOverride_WhiteTemp = true;
		// White balance: a higher white temperature warms the picture. Under rain cloud it goes down (cool grey light).
		S.WhiteTemp = T.WhiteTemp - (W.Rain > 0.f ? 500.f : (W.Coverage > 0.5f ? 300.f : 0.f));
		S.bOverride_ColorSaturation = true;
		const float Sat = T.Saturation * W.Saturation;
		S.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
		// Under a cloud deck in the wet the light goes cold and grey: the warm cast of the hour is mostly gone.
		const float Grey = W.Rain > 0.f ? 1.f : (W.Coverage > 0.5f ? 0.6f : 0.f);
		S.bOverride_SceneColorTint = true;
		S.SceneColorTint = FMath::Lerp(FLinearColor::White, FLinearColor(0.9f, 0.94f, 1.f), Grey);
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = FVector4(Sat * (1.f - 0.15f * Grey), Sat * (1.f - 0.15f * Grey), Sat * (1.f - 0.15f * Grey), 1.f);
		S.bOverride_ColorContrast = true;
		S.ColorContrast = FVector4(T.Contrast, T.Contrast, T.Contrast, 1.f);
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = T.Vignette;
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = T.Bloom * (bNight ? 0.55f : (W.Rain > 0.f ? 0.75f : 0.85f));
		// Screen space reflections (the "screen space" GI setting) only on glossy surfaces: on rough plaster, pebble
		// render and concrete they add nothing but noise.
		S.bOverride_ScreenSpaceReflectionMaxRoughness = true;
		S.ScreenSpaceReflectionMaxRoughness = 0.4f;
	}
	if (ClassroomLightLevels.IsEmpty())
	{
		if (ACampusSchoolBuilder* Builder = ACampusSchoolBuilder::Find(GetWorld()))
		{
			TInlineComponentArray<ULocalLightComponent*> Lights(Builder);
			for (ULocalLightComponent* Light : Lights)
			{
				if (Light->ComponentHasTag(TEXT("CampusClassroom")))
				{
					ClassroomLightLevels.Add(Light, Light->Intensity);
				}
			}
		}
	}
	for (const TPair<TWeakObjectPtr<ULocalLightComponent>, float>& Entry : ClassroomLightLevels)
	{
		if (ULocalLightComponent* Light = Entry.Key.Get())
		{
			Light->SetIntensity(Entry.Value * (bCampusStormNight ? 0.52f : 1.f));
		}
	}
	// Caustics under open water follow the direct sun (none under cloud or at night).
	ACampusWater::SetSunlight(GetWorld(), bNight ? 0.f : T.SunLux * W.SunScale * 0.04f);
	NightLights = FMath::Clamp(T.NightLights + W.NightLightsAdd, 0.f, 1.f);
	RainAmount = W.Rain;
	Wetness = W.Wet;
	SetEnv(this, TEXT("NightLights"), NightLights);
	SetEnv(this, TEXT("Wetness"), Wetness);
	SetEnv(this, TEXT("Rain"), RainAmount);
	WindDir = FVector(FMath::Cos(Seed * 0.37f), FMath::Sin(Seed * 0.37f), 0.f) * (Weather == ECampusWeather::Storm ? 0.45f : 0.18f);
	StormStream.Initialize(Seed);
	NextStrike = Clock + StormStream.FRandRange(4.f, 10.f);
	// Rain is water: each streak bends what is behind it (the refraction in its material) and catches some light, a few
	// brightly, most faintly, and far off it all thickens into a grey curtain. Never whiter than the sky behind it.
	static const float RainLight[(int32)ECampusTimeOfDay::Count] = { 0.42f, 0.48f, 0.46f, 0.28f, 0.11f };
	const float RainGlow = RainLight[FMath::Clamp((int32)TimeOfDay, 0, (int32)ECampusTimeOfDay::Count - 1)] * (Weather == ECampusWeather::Storm ? 0.8f : 1.f);
	if (UInstancedStaticMeshComponent* R = RainMesh.Get())
	{
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(R->GetMaterial(0)))
		{
			M->SetVectorParameterValue(TEXT("Color"), bNight ? FLinearColor(0.42f, 0.48f, 0.58f) : FLinearColor(0.66f, 0.7f, 0.75f));
			M->SetScalarParameterValue(TEXT("Intensity"), RainGlow);
			M->SetScalarParameterValue(TEXT("Opacity"), Weather == ECampusWeather::Storm ? 0.38f : 0.3f);
		}
	}
	if (UInstancedStaticMeshComponent* R = SheetMesh.Get())
	{
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(R->GetMaterial(0)))
		{
			M->SetVectorParameterValue(TEXT("Color"), bNight ? FLinearColor(0.4f, 0.45f, 0.55f) : FLinearColor(0.64f, 0.68f, 0.74f));
			M->SetScalarParameterValue(TEXT("Intensity"), RainGlow * 0.85f);
			M->SetScalarParameterValue(TEXT("Amount"), FMath::Clamp(RainAmount, 0.f, 1.f) * (Weather == ECampusWeather::Storm ? 1.3f : 1.f));
		}
	}
	if (UInstancedStaticMeshComponent* R = SplashMesh.Get())
	{
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(R->GetMaterial(0)))
		{
			M->SetVectorParameterValue(TEXT("Color"), bNight ? FLinearColor(0.42f, 0.48f, 0.58f) : FLinearColor(0.72f, 0.76f, 0.8f));
			M->SetScalarParameterValue(TEXT("Intensity"), RainGlow * 1.1f);
			M->SetScalarParameterValue(TEXT("Opacity"), 0.45f);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusEnv] %s: %s"), *CampusMaps::Get(Map).Name.ToString(), *CampusMaps::ConditionsLine(Weather, TimeOfDay).ToString());
}

void ACampusEnvironment::CaptureRainMask()
{
	bMaskCaptured = true;
	UTextureRenderTarget2D* RT = CampusAssets::RainMaskTarget();
	const ACampusSchoolBuilder* Builder = ACampusSchoolBuilder::Find(GetWorld());
	if (!RT || !Builder)
	{
		SetEnv(this, TEXT("MaskSize"), 0.f);
		return;
	}
	const FBox Bounds = Builder->PlayableBounds.IsValid ? Builder->PlayableBounds : FBox(FVector(-6000.f), FVector(6000.f));
	const FVector Center = Bounds.GetCenter();
	// Well past the play area: rooms and arcades round its edge are roofed too, and outside the mask the curtain
	// counts as open sky (rain falling through a classroom).
	const float Size = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) + 8000.f;
	if (RT->SizeX < 2048)
	{
		RT->ResizeTarget(2048, 2048);
	}
	const float Top = Bounds.Max.Z + 3000.f;
	if (!MaskCapture)
	{
		MaskCapture = NewObject<USceneCaptureComponent2D>(this);
		MaskCapture->SetupAttachment(RootComponent);
		MaskCapture->ProjectionType = ECameraProjectionMode::Orthographic;
		MaskCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		MaskCapture->bCaptureEveryFrame = false;
		MaskCapture->bCaptureOnMovement = false;
		MaskCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		MaskCapture->RegisterComponent();
	}
	MaskCapture->ShowOnlyActors.Reset();
	MaskCapture->ShowOnlyActors.Add(const_cast<ACampusSchoolBuilder*>(Builder));
	MaskCapture->OrthoWidth = Size;
	MaskCapture->TextureTarget = RT;
	MaskCapture->SetWorldLocationAndRotation(FVector(Center.X, Center.Y, Top), FRotator(-90.f, 0.f, 0.f));
	MaskCapture->CaptureScene();
	// The falling drops are spawned on the CPU: they read the same mask, so they never fall inside under a roof that
	// has no collision (ceilings, canopies, set dressing).
	MaskHeights.Reset();
	MaskRes = 0;
	if (FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource())
	{
		TArray<FLinearColor> Px;
		if (Res->ReadLinearColorPixels(Px) && Px.Num() == RT->SizeX * RT->SizeY && RT->SizeX == RT->SizeY)
		{
			MaskRes = RT->SizeX;
			MaskHeights.SetNumUninitialized(Px.Num());
			for (int32 i = 0; i < Px.Num(); ++i)
			{
				MaskHeights[i] = Top - Px[i].R;
			}
		}
	}
	if (MaskRes == 0)
	{
		// The single-channel float target the materials sample has no linear-colour readback on every RHI: the same
		// view goes once more into a half-float colour target that does, read and let go (depths here stay well
		// inside half-float range: a few centimetres of precision at the roofs, a few more down at the street).
		UTextureRenderTarget2D* Readback = NewObject<UTextureRenderTarget2D>(this);
		Readback->RenderTargetFormat = RTF_RGBA16f;
		Readback->ClearColor = FLinearColor(60000.f, 0.f, 0.f, 1.f);
		Readback->bAutoGenerateMips = false;
		Readback->InitAutoFormat(RT->SizeX, RT->SizeY);
		Readback->UpdateResourceImmediate(true);
		MaskCapture->TextureTarget = Readback;
		MaskCapture->CaptureScene();
		MaskCapture->TextureTarget = RT;
		TArray<FFloat16Color> Half;
		if (FTextureRenderTargetResource* HalfRes = Readback->GameThread_GetRenderTargetResource();
			HalfRes && HalfRes->ReadFloat16Pixels(Half) && Half.Num() == Readback->SizeX * Readback->SizeY)
		{
			MaskRes = Readback->SizeX;
			MaskHeights.SetNumUninitialized(Half.Num());
			for (int32 i = 0; i < Half.Num(); ++i)
			{
				MaskHeights[i] = Top - Half[i].R.GetFloat();
			}
		}
		Readback->ReleaseResource();
		Readback->MarkAsGarbage();
	}
	MaskOrigin = FVector2D(Center.X, Center.Y);
	MaskExtent = Size;
	FloorCache.Reset();
	UE_LOG(LogTemp, Display, TEXT("[CampusEnv] rain mask %d px over %.0f m (%.1f cm a texel), top %.0f"), MaskRes, Size / 100.f, MaskRes > 0 ? Size / MaskRes : 0.f, Top);
	SetEnv(this, TEXT("MaskX"), Center.X);
	SetEnv(this, TEXT("MaskY"), Center.Y);
	SetEnv(this, TEXT("MaskSize"), Size);
	SetEnv(this, TEXT("MaskTop"), Top);
}

bool ACampusEnvironment::MaskSurfaceAt(const FVector& P, float& OutZ) const
{
	if (MaskRes <= 0 || MaskExtent <= 1.f)
	{
		return false;
	}
	// Same mapping as the rain sheet material.
	const float U = (P.Y - MaskOrigin.Y) / MaskExtent + 0.5f;
	const float V = 0.5f - (P.X - MaskOrigin.X) / MaskExtent;
	if (U <= 0.f || U >= 1.f || V <= 0.f || V >= 1.f)
	{
		return false;
	}
	const int32 X = FMath::Clamp(FMath::FloorToInt(U * MaskRes), 0, MaskRes - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(V * MaskRes), 0, MaskRes - 1);
	// sky-depth clear values are not surfaces, even when the capture falls back to half precision
	float Z = -1.0e9f;
	for (int32 dy = -1; dy <= 1; ++dy)
	{
		for (int32 dx = -1; dx <= 1; ++dx)
		{
			const int32 XX = FMath::Clamp(X + dx, 0, MaskRes - 1);
			const int32 YY = FMath::Clamp(Y + dy, 0, MaskRes - 1);
			const float Height = MaskHeights[YY * MaskRes + XX];
			if (FMath::IsFinite(Height) && Height > -50000.f && Height < 50000.f)
			{
				Z = FMath::Max(Z, Height);
			}
		}
	}
	OutZ = Z;
	return Z > -50000.f;
}

bool ACampusEnvironment::IsRainExposed(const FVector& Position) const
{
	float RoofZ;
	if (MaskSurfaceAt(Position, RoofZ))
	{
		return Position.Z >= RoofZ - 8.f;
	}
	const FVector Start = Position + FVector(0.f, 0.f, 8.f);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Start, Start + FVector(0.f, 0.f, 12000.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(CampusRainExposure), false));
}

bool ACampusEnvironment::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		OutRotation = PC->PlayerCameraManager->GetCameraRotation();
		return true;
	}
	return false;
}

void ACampusEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (!bForced)
	{
		ECampusWeather NewWeather;
		ECampusTimeOfDay NewTime;
		int32 NewSeed;
		ResolveConditions(NewWeather, NewTime, NewSeed);
		if (!bApplied || NewWeather != Weather || NewTime != TimeOfDay || NewSeed != Seed)
		{
			Weather = NewWeather;
			TimeOfDay = NewTime;
			Seed = NewSeed;
			Apply();
		}
	}
	if (bApplied && AppliedGI != CampusGraphics::GlobalIllumination())
	{
		Apply();
	}
	if (!bMaskCaptured)
	{
		// After the builder has laid the map out (it rebuilds on BeginPlay).
		MaskDelay -= DeltaSeconds;
		if (MaskDelay <= 0.f)
		{
			CaptureRainMask();
		}
	}
	FVector Camera;
	FRotator CamRot;
	if (GetViewPoint(Camera, CamRot))
	{
		UpdateRain(DeltaSeconds, Camera);
	}
	UpdateLightning(DeltaSeconds);
	UpdateAudio(DeltaSeconds);
	UpdateWind();
}

void ACampusEnvironment::UpdateWind()
{
	// The breeze the planting moves in: a weather's own strength (still in fog, a fresh breeze ahead of rain, a gale
	// in a storm, a little more up on the roof), gusting over several seconds, and the direction wandering a few
	// degrees either side of the one the rain drives in.
	static const float Base[(int32)ECampusWeather::Count] = { 0.95f, 1.05f, 1.15f, 1.45f, 2.4f, 0.3f };
	const float Strength = Base[FMath::Clamp((int32)Weather, 0, (int32)ECampusWeather::Count - 1)] * (Map == ECampusMap::Hotel ? 1.3f : 1.f);
	const float Gust = FMath::Clamp(0.7f + 0.5f * FMath::PerlinNoise1D(Clock * 0.11f + Seed) + 0.25f * FMath::PerlinNoise1D(Clock * 0.47f + 3.1f), 0.25f, 1.6f);
	FVector Dir = WindDir.GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	Dir = Dir.RotateAngleAxis(14.f * FMath::PerlinNoise1D(Clock * 0.05f + 7.7f), FVector::UpVector);
	SetEnv(this, TEXT("WindX"), Dir.X);
	SetEnv(this, TEXT("WindY"), Dir.Y);
	SetEnv(this, TEXT("WindStrength"), Strength * Gust);
}

void ACampusEnvironment::UpdateRain(float DeltaSeconds, const FVector& Camera)
{
	UInstancedStaticMeshComponent* R = RainMesh.Get();
	if (!R)
	{
		return;
	}
	// the curtains carry distant density, leaving a bounded set of individually moved nearby drops
	const int32 Effects = FMath::Clamp(CampusGraphics::EffectsLevel(), 0, 3);
	static constexpr float Density[] = { 0.4f, 0.65f, 0.85f, 1.f };
	const int32 Target = FMath::RoundToInt(RainAmount * 3600.f * Density[Effects]);
	ACampusWater::Rain(GetWorld(), RainAmount, DeltaSeconds, Camera);
	if (Target == 0)
	{
		if (Drops.Num() > 0)
		{
			Drops.Reset();
			R->ClearInstances();
			SplashMesh->ClearInstances();
			SheetMesh->ClearInstances();
			Splashes.Reset();
		}
		return;
	}
	UpdateRainSheets(Camera);
	// Fine enough that drops do not fall inside a room whose edge crosses a cell.
	const float Cell = 80.f;
	int32 TraceBudget = Effects == 0 ? 12 : 24;
	auto CellAt = [Cell](const FVector& P)
	{
		return FIntPoint(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell));
	};
	auto FloorAt = [&](const FVector& P, float& OutZ) -> bool
	{
		const FIntPoint Key = CellAt(P);
		if (const float* Z = FloorCache.Find(Key))
		{
			OutZ = *Z;
			return *Z > -50000.f;
		}
		if (TraceBudget-- <= 0)
		{
			return false;
		}
		const FVector C((Key.X + 0.5f) * Cell, (Key.Y + 0.5f) * Cell, 0.f);
		FHitResult Hit;
		const FVector Start(C.X, C.Y, Camera.Z + 4000.f);
		const FVector End(C.X, C.Y, Camera.Z - 3000.f);
		const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(CampusRain), false));
		if (FloorCache.Num() > 8000)
		{
			FloorCache.Reset();
		}
		if (!bHit)
		{
			FloorCache.Add(Key, -1.0e9f);
			return false;
		}
		OutZ = Hit.ImpactPoint.Z;
		FloorCache.Add(Key, OutZ);
		return true;
	};
	// Where the map's top-down mask covers the point it gives the topmost surface right there (an umbrella's canopy, a
	// cornice, a roof edge, to a few centimetres); the coarse traced grid is only the fallback outside it.
	auto LandingAt = [&](const FVector& P, float& OutZ) -> bool
	{
		float Surface;
		if (MaskSurfaceAt(P, Surface))
		{
			OutZ = Surface - 1.f;
			return true;
		}
		return FloorAt(P, OutZ);
	};
	FRandomStream& Rng = StormStream;
	auto Respawn = [&](FDrop& D, bool bAnyHeight) -> bool
	{
		// Mostly in front of the camera, where they are seen; within 16 m.
		const float Ang = Rng.FRandRange(0.f, 2.f * PI);
		const float Rad = 1600.f * FMath::Sqrt(Rng.FRand());
		FVector P = Camera + FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f);
		float Floor;
		if (!LandingAt(P, Floor))
		{
			return false;
		}
		const float Top = Camera.Z + 1100.f;
		if (Floor > Top - 100.f)
		{
			return false;
		}
		P.Z = bAnyHeight ? Rng.FRandRange(FMath::Max(Floor, Camera.Z - 500.f), Top) : Rng.FRandRange(Top - 300.f, Top);
		D.Pos = P;
		D.Speed = Rng.FRandRange(850.f, 1050.f);
		D.FloorZ = Floor;
		D.Cell = CellAt(P);
		return true;
	};
	while (Drops.Num() < Target)
	{
		FDrop D;
		if (!Respawn(D, true))
		{
			break;
		}
		Drops.Add(D);
	}
	if (Drops.Num() > Target)
	{
		Drops.SetNum(Target);
	}
	const FVector Wind = WindDir;
	TArray<FTransform>& Transforms = RainTransforms;
	Transforms.Reset();
	Transforms.Reserve(Drops.Num());
	// Streaks lie along their fall (down and with the wind).
	const FQuat Lean = FQuat::FindBetweenNormals(FVector::UpVector, (FVector::UpVector - Wind).GetSafeNormal());
	const int32 SplashLimit = Effects == 0 ? 48 : 96;
	if (Splashes.Num() > SplashLimit)
	{
		Splashes.SetNum(SplashLimit);
	}
	int32 SplashTraceBudget = Effects == 0 ? 4 : 8;
	for (FDrop& D : Drops)
	{
		D.Pos += (FVector(0.f, 0.f, -1.f) + Wind) * D.Speed * DeltaSeconds;
		const FIntPoint NewCell = CellAt(D.Pos);
		if (NewCell != D.Cell)
		{
			float SurfaceZ;
			if (LandingAt(D.Pos, SurfaceZ))
			{
				D.FloorZ = SurfaceZ;
				D.Cell = NewCell;
			}
			else
			{
				// a missing sample must not carry the previous cell's rooftop into open air
				D.Pos.Z = Camera.Z - 10000.f;
				D.FloorZ = Camera.Z - 20000.f;
			}
		}
		const bool bFar = FVector::DistSquared2D(D.Pos, Camera) > FMath::Square(1750.f);
		if (D.Pos.Z <= D.FloorZ || D.Pos.Z < Camera.Z - 600.f || bFar)
		{
			if (D.Pos.Z <= D.FloorZ && !bFar && FVector::DistSquared(D.Pos, Camera) < FMath::Square(900.f)
				&& Splashes.Num() < SplashLimit && SplashTraceBudget > 0)
			{
				// the conservative roof mask stops rain, but only a real surface can produce a splash
				--SplashTraceBudget;
				FHitResult Hit;
				const FVector At(D.Pos.X, D.Pos.Y, D.FloorZ);
				if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, 45.f), At - FVector(0.f, 0.f, 45.f),
					FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(CampusRainSplash), false))
					&& Hit.ImpactNormal.Z >= 0.7f)
				{
					Splashes.Add({ Hit.ImpactPoint + Hit.ImpactNormal * 1.5f, 0.f });
				}
			}
			if (!Respawn(D, bFar))
			{
				D.Pos.Z = Camera.Z - 10000.f;
				D.FloorZ = Camera.Z - 20000.f;
			}
		}
		// About a centimetre wide (a drop smeared by its fall over the exposure), so the near ones read past a few metres.
		Transforms.Add(FTransform(Lean, D.Pos, FVector(0.011f, 0.011f, 0.42f)));
	}
	if (R->GetInstanceCount() != Transforms.Num())
	{
		R->ClearInstances();
		R->AddInstances(Transforms, false, true, false);
	}
	else
	{
		// 实例数未变时只提交变换，避免每帧重建渲染代理
		R->BatchUpdateInstancesTransforms(0, Transforms, true, false, false);
		R->MarkRenderInstancesDirty();
	}

	// Splashes: a crown that spreads and fades in a quarter of a second.
	TArray<FTransform>& SplashT = SplashTransforms;
	SplashT.Reset(SplashLimit);
	for (int32 i = Splashes.Num() - 1; i >= 0; --i)
	{
		Splashes[i].Age += DeltaSeconds;
		if (Splashes[i].Age > 0.22f)
		{
			Splashes.RemoveAtSwap(i);
		}
	}
	for (const FSplash& S : Splashes)
	{
		const float A = S.Age / 0.22f;
		const float Rr = FMath::Lerp(0.03f, 0.16f, FMath::Sqrt(A));
		SplashT.Add(FTransform(FQuat::Identity, S.Pos, FVector(Rr, Rr, 0.02f + 0.03f * (1.f - A))));
	}
	while (SplashT.Num() < SplashLimit)
	{
		SplashT.Add(FTransform(FQuat::Identity, Camera, FVector::ZeroVector));
	}
	UInstancedStaticMeshComponent* SM = SplashMesh.Get();
	if (SM->GetInstanceCount() != SplashT.Num())
	{
		SM->ClearInstances();
		SM->AddInstances(SplashT, false, true, false);
	}
	else
	{
		SM->BatchUpdateInstancesTransforms(0, SplashT, true, false, false);
	}
	for (int32 i = 0; i < SplashLimit; ++i)
	{
		SM->SetCustomDataValue(i, 0, i < Splashes.Num() ? 1.f - Splashes[i].Age / 0.22f : 0.f, false);
	}
	SM->MarkRenderInstancesDirty();
}

void ACampusEnvironment::UpdateRainSheets(const FVector& Camera)
{
	UInstancedStaticMeshComponent* S = SheetMesh.Get();
	if (!S)
	{
		return;
	}
	// keep the curtain near the viewer; a 200 metre card down to the hotel's street reads as a moving flat wall
	struct FLayer { float Dist, W, L, Opac, Haze; };
	const FLayer SheetLayers[] = { { 650.f, 3.5f, 170.f, 0.24f, 0.01f }, { 1500.f, 7.f, 260.f, 0.17f, 0.028f }, { 3400.f, 16.f, 420.f, 0.11f, 0.05f } };
	const float Top = Camera.Z + 1500.f;
	const float Bottom = Camera.Z - 2500.f;
	TArray<FTransform> T;
	TArray<float> Data;
	const int32 LayerCount = CampusGraphics::EffectsLevel() == 0 ? 2 : UE_ARRAY_COUNT(SheetLayers);
	for (int32 Layer = 0; Layer < LayerCount; ++Layer)
	{
		const FLayer& L = SheetLayers[Layer];
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const bool bAlongX = Side < 2;
			const float Sign = (Side % 2) ? 1.f : -1.f;
			const FVector C = Camera + (bAlongX ? FVector(0.f, Sign * L.Dist, 0.f) : FVector(Sign * L.Dist, 0.f, 0.f));
			const FVector Size = bAlongX ? FVector(L.Dist * 2.f, 1.f, Top - Bottom) : FVector(1.f, L.Dist * 2.f, Top - Bottom);
			T.Add(FTransform(FQuat::Identity, FVector(C.X, C.Y, (Top + Bottom) * 0.5f), Size / 100.f));
			Data.Append({ L.W, L.L, L.Opac, L.Haze });
		}
	}
	if (S->GetInstanceCount() != T.Num())
	{
		S->ClearInstances();
		S->AddInstances(T, false, true, false);
		for (int32 i = 0; i < T.Num(); ++i)
		{
			S->SetCustomData(i, TArrayView<const float>(&Data[i * 4], 4), i == T.Num() - 1);
		}
	}
	else
	{
		// 雨幕仍逐帧跟随相机，只更新实例数据
		S->BatchUpdateInstancesTransforms(0, T, true, false, false);
		S->MarkRenderInstancesDirty();
	}
}

void ACampusEnvironment::UpdateLightning(float DeltaSeconds)
{
	if (Weather != ECampusWeather::Storm)
	{
		return;
	}
	if (Clock >= NextStrike)
	{
		StrikeTime = Clock;
		NextStrike = Clock + StormStream.FRandRange(7.f, 19.f);
		const float Distance = StormStream.FRandRange(0.2f, 1.f);
		StrikeFlash = FMath::Lerp(1.f, 0.35f, Distance);
		ThunderAt = Clock + 0.3f + Distance * 3.f;
		ThunderVolume = FMath::Lerp(1.f, 0.45f, Distance);
	}
	const float Since = Clock - StrikeTime;
	// Two or three flickers over a third of a second, then it is gone.
	float Flash = 0.f;
	if (Since < 0.45f)
	{
		Flash = StrikeFlash * FMath::Max(0.f, FMath::Sin(Since * 55.f)) * FMath::Exp(-Since * 6.f);
	}
	if (USkyLightComponent* Sky = SkyLight.Get())
	{
		Sky->SetIntensity(BaseSkyIntensity * (1.f + 5.f * Flash));
	}
	if (UDirectionalLightComponent* S = Sun.Get())
	{
		S->SetIntensity(BaseSunLux + 40.f * Flash);
	}
	if (ThunderAt > 0.f && Clock >= ThunderAt)
	{
		ThunderAt = -1.f;
		CampusAudio::Play2D(this, ThunderVolume > 0.75f ? TEXT("Thunder_Near") : TEXT("Thunder_Far"), ThunderVolume, 0.08f, CampusAudio::ECategory::Ambience);
	}
}

void ACampusEnvironment::SetLoop(TObjectPtr<UAudioComponent>& Comp, FName Sound, float Volume, float DeltaSeconds)
{
	Volume *= CampusAudio::CategoryVolume(CampusAudio::ECategory::Ambience);
	if (!Comp && Volume > 0.005f)
	{
		USoundBase* Base = CampusAssets::Sound(Sound);
		if (!Base)
		{
			return;
		}
		if (USoundWave* Wave = Cast<USoundWave>(Base))
		{
			Wave->bLooping = true;
		}
		Comp = UGameplayStatics::SpawnSound2D(this, Base, 1.f, 1.f, 0.f, nullptr, false, false);
		if (Comp)
		{
			Comp->SetVolumeMultiplier(0.001f);
		}
	}
	if (Comp)
	{
		const float Current = Comp->VolumeMultiplier;
		Comp->SetVolumeMultiplier(FMath::Max(0.001f, FMath::FInterpTo(Current, Volume, DeltaSeconds, 1.5f)));
	}
}

void ACampusEnvironment::UpdateAudio(float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const float Enclosed = CampusAudio::GetEnclosure();
	const bool bNight = TimeOfDay == ECampusTimeOfDay::Night;
	FName Bed;
	float BedVolume = 0.f;
	float Wind = 0.f;
	switch (Map)
	{
	case ECampusMap::Hotel:
		Bed = bNight ? TEXT("Amb_CityNight") : TEXT("Amb_Rooftop");
		BedVolume = 0.55f * (1.f - 0.6f * Enclosed);
		Wind = 0.35f;
		break;
	case ECampusMap::HQ:
		Bed = TEXT("Amb_HQ");
		BedVolume = 0.35f;
		Wind = 0.1f;
		break;
	default:
		Bed = bNight ? TEXT("Amb_CityNight") : TEXT("Amb_City");
		BedVolume = 0.4f * (1.f - 0.5f * Enclosed);
		Wind = 0.f;
		break;
	}
	if (Weather == ECampusWeather::Storm)
	{
		Wind = FMath::Max(Wind, 0.75f);
	}
	else if (Weather == ECampusWeather::Rain)
	{
		Wind = FMath::Max(Wind, 0.3f);
	}
	SetLoop(AmbBed, Bed, BedVolume * (1.f - 0.5f * RainAmount), DeltaSeconds);
	SetLoop(WindBed, TEXT("Wind_Loop"), Wind * (1.f - 0.7f * Enclosed), DeltaSeconds);
	SetLoop(RainOut, TEXT("Rain_Loop"), RainAmount * (1.f - Enclosed) * 0.9f, DeltaSeconds);
	SetLoop(RainIn, TEXT("Rain_Indoor"), RainAmount * Enclosed * 0.75f, DeltaSeconds);
}
