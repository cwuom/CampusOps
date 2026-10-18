#pragma once

#include "CoreMinimal.h"
#include "CampusGraphics.generated.h"

class UWorld;

/** Video options saved with the profile. Levels are 0 = low .. 3 = ultra unless noted. */
USTRUCT(BlueprintType)
struct FCampusGraphicsSettings
{
	GENERATED_BODY()

	/** 0..3 = the preset the levels below were taken from, 4 = custom. */
	UPROPERTY() int32 Preset = 2;
	/** 0 fullscreen, 1 borderless window, 2 window. */
	UPROPERTY() int32 WindowMode = 1;
	/** Output resolution; (0, 0) follows the desktop. */
	UPROPERTY() FIntPoint Resolution = FIntPoint::ZeroValue;
	/** Internal render resolution, percent of the output (upscaled by the anti-aliasing; above 100 supersampled). */
	UPROPERTY() float RenderScale = 100.f;
	/** 0 TSR, 1 TAA, 2 FXAA, 3 off, 4 NVIDIA DLSS (falls back to TSR where unsupported). */
	UPROPERTY() int32 AntiAliasing = 0;
	/** DLSS super resolution: 0 DLAA (native), 1 quality, 2 balanced, 3 performance, 4 ultra performance. Replaces the render scale. */
	UPROPERTY() int32 DLSSMode = 1;
	/** DLSS frame generation: 0 off, 1 2x, 2 3x, 3 4x (RTX 50 multi frame generation), 4 dynamic. */
	UPROPERTY() int32 FrameGeneration = 0;
	/** NVIDIA Reflex low latency: 0 off, 1 on, 2 on + boost. Frame generation forces it on. */
	UPROPERTY() int32 Reflex = 1;
	/** 0 low .. 3 ultra, 4 cinematic. */
	UPROPERTY() int32 AntiAliasingQuality = 2;
	UPROPERTY() float Sharpness = 0.5f;
	/** 0 screen space (no Lumen), 1 Lumen irradiance field, 2 Lumen screen probes, 3 detailed probes, 4 hardware ray traced. */
	UPROPERTY() int32 GlobalIllumination = 2;
	UPROPERTY() int32 Shadows = 2;
	UPROPERTY() int32 Reflections = 2;
	UPROPERTY() int32 Textures = 3;
	/** Volumetric fog / clouds, particles. */
	UPROPERTY() int32 Effects = 2;
	/** Draw distance and foliage density. */
	UPROPERTY() int32 ViewDistance = 3;
	UPROPERTY() int32 PostProcess = 2;
	/** Exposure offset in stops. */
	UPROPERTY() float Brightness = 0.f;
	/** 0 = unlimited. */
	UPROPERTY() int32 FrameLimit = 0;
	UPROPERTY() bool bVSync = false;
	/** Settings layout the values were saved with; a mismatch re-probes the hardware for a starting preset. */
	UPROPERTY() int32 Revision = 0;
	static constexpr int32 CurrentRevision = 4;
};

namespace CampusGraphics
{
	/** Level settings of a preset (display options and brightness are left alone). */
	CAMPUSOPS_API void ApplyPreset(FCampusGraphicsSettings& S, int32 Preset);
	/** Starting preset from a quick GPU benchmark and ray tracing support. */
	CAMPUSOPS_API int32 DetectPreset();
	/** Pushes the settings to the engine (scalability, cvars, window, frame pacing). */
	CAMPUSOPS_API void Apply(const FCampusGraphicsSettings& S);
	/** Per-world parts: local light shadows (only cheap with MegaLights). */
	CAMPUSOPS_API void ApplyToWorld(UWorld* World);
	/** Whether the classroom fixtures cast shadows (MegaLights is running). */
	CAMPUSOPS_API bool LocalLightShadows();
	/** Whether Lumen is the GI (off: screen space GI). */
	CAMPUSOPS_API bool LumenGI();
	/** The global illumination level applied (FCampusGraphicsSettings::GlobalIllumination). At 1 and below the maps'
	 *  bounce fill lights come on and the sky light fills in for the bounce outdoors. */
	CAMPUSOPS_API int32 GlobalIllumination();
	/** Component tag of the fill lights standing in for bounce light at the lowest GI levels. */
	inline const FName BounceLightTag(TEXT("CampusBounce"));
	/** Whether the distant terrain casts shadows (ultra shadows only). */
	CAMPUSOPS_API bool TerrainShadows();
	/** Size a magnified optic's capture renders at for an eyepiece Diameter (fraction of the view height) on screen. */
	CAMPUSOPS_API int32 OpticCaptureSize(float Diameter);
	/** Height of the game view in pixels. */
	CAMPUSOPS_API float ViewHeightPixels();
	/** Effects level (0 low .. 3 ultra): rain density and the like. */
	CAMPUSOPS_API int32 EffectsLevel();
	/** Low post-processing: an optic's magnified view leaves out its costlier screen-space passes. */
	CAMPUSOPS_API bool LowOpticView();
	/** Whether hardware ray tracing is available on this machine. */
	CAMPUSOPS_API bool SupportsHardwareRayTracing();
	/** NVIDIA DLSS super resolution is available (RTX GPU, D3D12/Vulkan, driver new enough). */
	CAMPUSOPS_API bool SupportsDLSS();
	/** Highest DLSS frame generation option available: 0 none, 1 2x, 3 up to 4x (multi frame generation). */
	CAMPUSOPS_API int32 MaxFrameGeneration();
	CAMPUSOPS_API bool SupportsReflex();
	/** Frames per second reaching the display while DLSS frame generation runs, 0 when it is off. */
	CAMPUSOPS_API float PresentedFrameRate();
	/** Exposure offset (stops) the cameras add. */
	CAMPUSOPS_API float BrightnessBias();
	/** Output resolutions the display supports (largest first). */
	CAMPUSOPS_API TArray<FIntPoint> Resolutions();
}
