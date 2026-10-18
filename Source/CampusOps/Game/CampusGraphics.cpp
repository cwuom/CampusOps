#include "Game/CampusGraphics.h"
#include "Map/CampusSchoolBuilder.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RenderUtils.h"
#include "UObject/UObjectIterator.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#if CAMPUS_WITH_DLSS
#include "DLSSLibrary.h"
#include "StreamlineLibraryDLSSG.h"
#include "StreamlineLibraryReflex.h"
#endif

namespace
{
	float GBrightness = 0.f;
	FCampusGraphicsSettings GPendingNvidia;
	bool GNvidiaDeferred = false;
	float GOpticSupersample = 1.f;
	bool GLowOpticView = false;
	bool GLocalLightShadows = false;
	bool GLumenGI = true;
	int32 GGlobalIllumination = 2;
	bool GTerrainShadows = true;
	int32 GViewDistance = 3;
	int32 GEffectsLevel = 3;

	void SetInt(const TCHAR* Name, int32 Value)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		// Some of these recreate every render proxy when set, so only touch them when they change.
		if (Var && Var->GetInt() != Value)
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	void SetFloat(const TCHAR* Name, float Value)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var && !FMath::IsNearlyEqual(Var->GetFloat(), Value))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	struct FPresetRow
	{
		float Scale;
		int32 AAQuality, GI, Shadows, Reflections, Textures, Effects, ViewDistance, PostProcess;
	};

	// Low is sized for 80 fps on a GTX 1650 class card at 1080p: no Lumen (screen space GI + distance field AO), no
	// Nanite, cascaded shadows, half the internal resolution upscaled by TSR. Medium brings in Lumen's irradiance field,
	// ultra is native with hardware ray traced GI.
	const FPresetRow PresetRows[4] = {
		{ 50.f, 0, 0, 0, 0, 1, 0, 1, 0 },
		{ 75.f, 2, 1, 1, 1, 2, 1, 2, 1 },
		{ 100.f, 2, 2, 2, 2, 3, 2, 3, 2 },
		{ 100.f, 4, 4, 3, 3, 3, 3, 3, 3 },
	};

	// The NVIDIA blueprint libraries come up with the PostEngineInit plugins, which load after the PostEngineInit
	// broadcast and after the profile is first applied.
	bool NvidiaReady()
	{
#if CAMPUS_WITH_DLSS
		return FModuleManager::Get().IsModuleLoaded(TEXT("DLSSBlueprint")) && FModuleManager::Get().IsModuleLoaded(TEXT("StreamlineDLSSGBlueprint"))
			&& FModuleManager::Get().IsModuleLoaded(TEXT("StreamlineReflexBlueprint"));
#else
		return false;
#endif
	}

	bool UseDLSS(const FCampusGraphicsSettings& S)
	{
		return S.AntiAliasing == 4 && CampusGraphics::SupportsDLSS();
	}

	/** DLSS super resolution, frame generation and Reflex; returns whether DLSS owns the screen percentage. */
	bool ApplyNvidia(const FCampusGraphicsSettings& S)
	{
#if CAMPUS_WITH_DLSS
		if (!NvidiaReady())
		{
			return false;
		}
		const bool bDLSS = UseDLSS(S);
		if (UDLSSLibrary::IsDLSSSupported())
		{
			static const UDLSSMode Modes[5] = { UDLSSMode::DLAA, UDLSSMode::Quality, UDLSSMode::Balanced, UDLSSMode::Performance, UDLSSMode::UltraPerformance };
			UDLSSMode Mode = bDLSS ? Modes[FMath::Clamp(S.DLSSMode, 0, 4)] : UDLSSMode::Off;
			if (Mode != UDLSSMode::Off && !UDLSSLibrary::IsDLSSModeSupported(Mode))
			{
				Mode = UDLSSMode::Quality;
			}
			UDLSSLibrary::SetDLSSMode(nullptr, Mode);
			UDLSSLibrary::EnableDLAA(bDLSS && Mode == UDLSSMode::DLAA);
		}
		// Frame generation needs Reflex to keep the added latency down, so it forces it on.
		static const EStreamlineDLSSGMode FGModes[5] = { EStreamlineDLSSGMode::Off, EStreamlineDLSSGMode::On2X, EStreamlineDLSSGMode::On3X, EStreamlineDLSSGMode::On4X, EStreamlineDLSSGMode::OnDynamic };
		EStreamlineDLSSGMode FG = FGModes[FMath::Clamp(S.FrameGeneration, 0, 4)];
		if (FG != EStreamlineDLSSGMode::Off && (!UStreamlineLibraryDLSSG::IsDLSSGSupported() || !UStreamlineLibraryDLSSG::IsDLSSGModeSupported(FG)))
		{
			FG = UStreamlineLibraryDLSSG::IsDLSSGSupported() && UStreamlineLibraryDLSSG::IsDLSSGModeSupported(EStreamlineDLSSGMode::On2X) ? EStreamlineDLSSGMode::On2X : EStreamlineDLSSGMode::Off;
		}
		if (UStreamlineLibraryReflex::IsReflexSupported())
		{
			const int32 Reflex = FG != EStreamlineDLSSGMode::Off ? FMath::Max(S.Reflex, 1) : FMath::Clamp(S.Reflex, 0, 2);
			UStreamlineLibraryReflex::SetReflexMode(Reflex == 0 ? EStreamlineReflexMode::Off : Reflex == 1 ? EStreamlineReflexMode::Enabled : EStreamlineReflexMode::Boost);
		}
		if (UStreamlineLibraryDLSSG::IsDLSSGSupported())
		{
			UStreamlineLibraryDLSSG::SetDLSSGMode(FG);
		}
		UE_LOG(LogTemp, Display, TEXT("[CampusGraphics] DLSS %d mode %d | frame generation %d | Reflex %d"), bDLSS ? 1 : 0, S.DLSSMode, (int32)FG, S.Reflex);
		return bDLSS;
#else
		return false;
#endif
	}
}

bool CampusGraphics::SupportsDLSS()
{
#if CAMPUS_WITH_DLSS
	return NvidiaReady() && UDLSSLibrary::IsDLSSSupported();
#else
	return false;
#endif
}

int32 CampusGraphics::MaxFrameGeneration()
{
#if CAMPUS_WITH_DLSS
	if (!NvidiaReady() || !UStreamlineLibraryDLSSG::IsDLSSGSupported())
	{
		return 0;
	}
	return UStreamlineLibraryDLSSG::IsDLSSGModeSupported(EStreamlineDLSSGMode::On4X) ? 3 : UStreamlineLibraryDLSSG::IsDLSSGModeSupported(EStreamlineDLSSGMode::On3X) ? 2 : 1;
#else
	return 0;
#endif
}

float CampusGraphics::PresentedFrameRate()
{
#if CAMPUS_WITH_DLSS
	if (NvidiaReady() && UStreamlineLibraryDLSSG::IsDLSSGSupported() && UStreamlineLibraryDLSSG::GetDLSSGMode() != EStreamlineDLSSGMode::Off)
	{
		float Hz = 0.f;
		int32 Presented = 0;
		UStreamlineLibraryDLSSG::GetDLSSGFrameTiming(Hz, Presented);
		return Presented > 1 ? Hz : 0.f;
	}
#endif
	return 0.f;
}

bool CampusGraphics::SupportsReflex()
{
#if CAMPUS_WITH_DLSS
	return NvidiaReady() && UStreamlineLibraryReflex::IsReflexSupported();
#else
	return false;
#endif
}

void CampusGraphics::ApplyPreset(FCampusGraphicsSettings& S, int32 Preset)
{
	S.Preset = FMath::Clamp(Preset, 0, 3);
	const FPresetRow& R = PresetRows[S.Preset];
	S.RenderScale = R.Scale;
	S.AntiAliasing = 0;
	// RTX cards upscale with DLSS instead: performance on low, up to DLAA (native) on ultra. Before the plugin is up
	// this goes by the adapter; Apply falls back to TSR if DLSS turns out to be unavailable.
	const bool bRTX = NvidiaReady() ? SupportsDLSS() : (GRHIVendorId == 0x10DE && GRHIAdapterName.Contains(TEXT("RTX")));
	if (bRTX)
	{
		static const int32 DLSSModes[4] = { 3, 2, 1, 0 };
		S.AntiAliasing = 4;
		S.DLSSMode = DLSSModes[S.Preset];
		S.Reflex = 1;
	}
	S.AntiAliasingQuality = R.AAQuality;
	S.GlobalIllumination = (R.GI == 4 && !SupportsHardwareRayTracing()) ? 3 : R.GI;
	S.Shadows = R.Shadows;
	S.Reflections = R.Reflections;
	S.Textures = R.Textures;
	S.Effects = R.Effects;
	S.ViewDistance = R.ViewDistance;
	S.PostProcess = R.PostProcess;
}

bool CampusGraphics::SupportsHardwareRayTracing()
{
	return GRHISupportsRayTracing && IsRayTracingAllowed();
}

int32 CampusGraphics::DetectPreset()
{
	float GPUIndex = -1.f;
	if (UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		GUS->RunHardwareBenchmark();
		GPUIndex = GUS->GetLastGPUBenchmarkResult();
	}
	const double VRAMGB = GRHIGlobals.GpuInfo.DedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0);
	// Synth benchmark GPU index, measured: RTX 5070 Ti ~780 (high: ~110 fps at 1440p). GTX 1650 class cards land
	// around 120-150. Ultra is only picked for cards that keep it above ~100 fps.
	int32 Preset = GPUIndex < 0.f ? 1 : GPUIndex < 240.f ? 0 : GPUIndex < 420.f ? 1 : GPUIndex < 1000.f ? 2 : 3;
	// 4 GB cards run out of memory for Lumen's surface cache, virtual shadow pages and 2K textures together.
	if (VRAMGB > 0.1 && VRAMGB < 4.6)
	{
		Preset = 0;
	}
	else if (VRAMGB > 0.1 && VRAMGB < 7.5)
	{
		Preset = FMath::Min(Preset, 1);
	}
	if (!SupportsHardwareRayTracing())
	{
		Preset = FMath::Min(Preset, 2);
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusGraphics] %s, %.1f GB VRAM, GPU index %.0f, ray tracing %d -> preset %d"),
		*GRHIAdapterName, VRAMGB, GPUIndex, SupportsHardwareRayTracing() ? 1 : 0, Preset);
	return Preset;
}

void CampusGraphics::Apply(const FCampusGraphicsSettings& S)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS)
	{
		return;
	}
	const int32 GI = FMath::Clamp(S.GlobalIllumination, 0, 4);
	const int32 Shadows = FMath::Clamp(S.Shadows, 0, 3);
	const int32 Effects = FMath::Clamp(S.Effects, 0, 3);
	const int32 Post = FMath::Clamp(S.PostProcess, 0, 3);

	const float RenderScale = FMath::Clamp(S.RenderScale, 50.f, 200.f);
	const int32 AAQuality = FMath::Clamp(S.AntiAliasingQuality, 0, 4);
	// The scalability group stops at 100%; supersampling goes straight to the screen percentage below.
	GUS->SetResolutionScaleValueEx(FMath::Min(RenderScale, 100.f));
	GUS->SetAntiAliasingQuality(AAQuality);
	// Scalability 0 would turn distance field sky occlusion off as well; GI off still wants it (level 1).
	GUS->SetGlobalIlluminationQuality(GI <= 1 ? 1 : GI == 2 ? 2 : 3);
	GUS->SetShadowQuality(Shadows);
	GUS->SetReflectionQuality(FMath::Clamp(S.Reflections, 0, 3));
	GUS->SetTextureQuality(FMath::Clamp(S.Textures, 0, 3));
	GUS->SetVisualEffectQuality(Effects);
	GUS->SetViewDistanceQuality(FMath::Clamp(S.ViewDistance, 0, 3));
	GUS->SetFoliageQuality(FMath::Clamp(S.ViewDistance, 0, 3));
	GUS->SetPostProcessingQuality(Post);
	GUS->SetShadingQuality(FMath::Max(Post, 1));
	GUS->SetFrameRateLimit((float)FMath::Max(S.FrameLimit, 0));
	GUS->SetVSyncEnabled(S.bVSync);
	// The first apply honours -ResX / -windowed on the command line; later ones only touch the window when it changes.
	static bool bFirstApply = true;
	bool bDisplayChanged = bFirstApply;
	if (!IsRunningDedicatedServer() && !GIsEditor)
	{
		const EWindowMode::Type Mode = S.WindowMode == 0 ? EWindowMode::Fullscreen : S.WindowMode == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
		const FIntPoint Desktop = GUS->GetDesktopResolution();
		FIntPoint Res = (S.Resolution.X >= 640 && S.Resolution.Y >= 360) ? S.Resolution : Desktop;
		if (Mode == EWindowMode::WindowedFullscreen)
		{
			Res = Desktop;
		}
		bDisplayChanged |= GUS->GetFullscreenMode() != Mode || GUS->GetScreenResolution() != Res;
		GUS->SetFullscreenMode(Mode);
		GUS->SetScreenResolution(Res);
	}
	if (bDisplayChanged)
	{
		GUS->ApplySettings(bFirstApply);
	}
	else
	{
		GUS->ApplyNonResolutionSettings();
		GUS->SaveSettings();
	}
	bFirstApply = false;

	// DLSS takes over from TSR as the temporal upscaler, and picks the screen percentage for its mode.
	static const int32 AAMethods[5] = { 4, 2, 1, 0, 4 };
	SetInt(TEXT("r.AntiAliasingMethod"), AAMethods[FMath::Clamp(S.AntiAliasing, 0, 4)]);
	SetFloat(TEXT("r.ScreenPercentage"), RenderScale);
	GPendingNvidia = S;
	// Turning DLSS off hands the screen percentage back at 100: whenever TSR upscales, the scale is set again after.
	if (NvidiaReady())
	{
		if (!ApplyNvidia(S))
		{
			SetFloat(TEXT("r.ScreenPercentage"), RenderScale);
		}
	}
	else if (CAMPUS_WITH_DLSS && !GNvidiaDeferred && !IsRunningCommandlet() && !IsRunningDedicatedServer())
	{
		GNvidiaDeferred = true;
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
		{
			if (!ApplyNvidia(GPendingNvidia))
			{
				SetFloat(TEXT("r.ScreenPercentage"), FMath::Clamp(GPendingNvidia.RenderScale, 50.f, 200.f));
			}
		});
	}
	// Tonemapper sharpening re-cuts the edges TSR has just resolved (stair steps, crawling texture); kept gentle.
	SetFloat(TEXT("r.Tonemapper.Sharpen"), FMath::Clamp(S.Sharpness, 0.f, 1.f) * 0.6f);
	// Thin geometry (wires, railings, window bars, distant edges) keeps its history instead of flickering, and the
	// higher levels accumulate more samples per pixel for a steadier image.
	SetInt(TEXT("r.TSR.ThinGeometryDetection"), AAQuality >= 2 ? 1 : -1);
	SetFloat(TEXT("r.TSR.History.SampleCount"), AAQuality >= 4 ? 32.f : AAQuality >= 3 ? 24.f : 16.f);
	SetFloat(TEXT("r.TSR.ShadingRejection.Flickering.Period"), AAQuality >= 3 ? 3.f : 2.f);

	// GI off: screen space GI and distance field AO instead of Lumen (and screen space reflections with it).
	const bool bLumen = GI >= 1;
	GLumenGI = bLumen;
	GGlobalIllumination = GI;
	SetInt(TEXT("r.DynamicGlobalIlluminationMethod"), bLumen ? 1 : 2);
	SetInt(TEXT("r.ReflectionMethod"), bLumen ? 1 : 2);
	// Screen space reflections are traced stochastically: without their temporal smoothing (off by default) every
	// rough wall near a lamp sparkles with single-pixel fireflies, like snow on the plaster.
	SetInt(TEXT("r.SSR.Temporal"), 1);
	SetInt(TEXT("r.SSGI.Quality"), Post >= 2 ? 3 : 2);
	// Lowest settings draw the fallback meshes instead of Nanite. The maps are mostly simple procedural shapes, so the
	// picture barely changes, while on a GTX 1650 class card Nanite's culling and visibility-buffer passes (for the
	// view, the shadow cascades and an optic's view) are a third of the frame.
	SetInt(TEXT("r.Nanite"), Post == 0 ? 0 : 1);
	// Ray tracing only runs when something uses it: traced GI, or MegaLights shadowing the classroom fixtures.
	const bool bHWRT = SupportsHardwareRayTracing();
	const bool bMegaLights = bHWRT && Shadows >= 2;
	const bool bRayTracing = bHWRT && (GI == 4 || bMegaLights);
	SetInt(TEXT("r.RayTracing.Enable"), bRayTracing ? 1 : 0);
	SetInt(TEXT("r.Lumen.HardwareRayTracing"), (bRayTracing && GI == 4) ? 1 : 0);
	SetInt(TEXT("r.MegaLights.Allowed"), bMegaLights ? 1 : 0);
	GLocalLightShadows = bMegaLights;
	// Low shadows: classic cascades (two, 1024) instead of virtual shadow maps. On a GTX 1650 class card the virtual
	// pages are the largest fixed cost of the frame (marking, allocating and rasterising pages, again for an optic's
	// view); the cascades cost a fraction of that with harder, shorter-reaching sun shadows.
	SetInt(TEXT("r.Shadow.Virtual.Enable"), Shadows >= 1 ? 1 : 0);
	// The mountain ranges' self-shadowing costs half a millisecond of virtual shadow pages; only ultra shadows keep it.
	GTerrainShadows = Shadows >= 3;
	GEffectsLevel = Effects;
	GViewDistance = FMath::Clamp(S.ViewDistance, 0, 3);

	// Clouds: the shadow map and sky occlusion are the fixed cost, the view march scales with resolution.
	static const int32 CloudSteps[4] = { 96, 192, 384, 768 };
	static const int32 CloudShadowRes[4] = { 256, 384, 640, 2048 };
	static const int32 CloudShadowSteps[4] = { 24, 40, 56, 128 };
	SetInt(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), CloudSteps[Effects]);
	SetInt(TEXT("r.VolumetricCloud.ShadowMap.MaxResolution"), CloudShadowRes[Effects]);
	SetFloat(TEXT("r.VolumetricCloud.ShadowMap.RaySampleMaxCount"), (float)CloudShadowSteps[Effects]);
	SetInt(TEXT("r.VolumetricCloud.SkyAO.MaxResolution"), CloudShadowRes[Effects]);
	SetInt(TEXT("r.VolumetricCloud.SkyAO.TraceSampleCount"), Effects >= 2 ? 10 : 4);
	SetInt(TEXT("r.VolumetricCloud.ShadowMap"), Effects >= 1 ? 1 : 0);
	SetInt(TEXT("r.VolumetricCloud.SkyAO"), Effects >= 1 ? 1 : 0);
	SetInt(TEXT("r.VolumetricFog"), Effects >= 3 ? 1 : 0);

	// The optic's view has a temporal upscaler of its own and is laid over the display pixels of the eyepiece one to
	// one from high up; the lower levels render it smaller and upsample it.
	static const float OpticSupersample[4] = { 0.6f, 0.8f, 1.f, 1.f };
	GOpticSupersample = OpticSupersample[Post];
	GLowOpticView = Post == 0;
	GBrightness = FMath::Clamp(S.Brightness, -1.5f, 1.5f);

	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			ApplyToWorld(Context.World());
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusGraphics] preset %d scale %.0f AA %d/%d GI %d shadows %d refl %d tex %d fx %d view %d post %d | RT %d MegaLights %d (DLSS / frame generation applied separately)"),
		S.Preset, RenderScale, S.AntiAliasing, AAQuality, GI, Shadows, S.Reflections, S.Textures, Effects, S.ViewDistance, Post, bRayTracing ? 1 : 0, bMegaLights ? 1 : 0);
}

bool CampusGraphics::LocalLightShadows()
{
	return GLocalLightShadows;
}

bool CampusGraphics::LumenGI()
{
	return GLumenGI;
}

int32 CampusGraphics::GlobalIllumination()
{
	return GGlobalIllumination;
}

void CampusGraphics::ApplyToWorld(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Without MegaLights each shadowed fixture would render its own shadow map: unshadowed, with a shorter reach so
	// the light stays in its room.
	for (TObjectIterator<URectLightComponent> It; It; ++It)
	{
		URectLightComponent* Light = *It;
		if (Light->GetWorld() != World || !Cast<ACampusSchoolBuilder>(Light->GetOwner()))
		{
			continue;
		}
		if (Light->ComponentHasTag(BounceLightTag))
		{
			// Lumen's irradiance field (medium) is too coarse to carry a room's bounce either.
			const bool bFill = GGlobalIllumination <= 1;
			if (Light->IsVisible() != bFill)
			{
				Light->SetVisibility(bFill);
			}
			continue;
		}
		if (Light->CastShadows != GLocalLightShadows)
		{
			Light->SetCastShadows(GLocalLightShadows);
			Light->SetAttenuationRadius(GLocalLightShadows ? 800.f : 450.f);
		}
	}
	// The map's lamps and downlights the same way (the bright ones are shadowed with MegaLights). Changing settings in
	// a match has to take them back off: without virtual shadow maps each would re-render the scene into a cubemap
	// every frame.
	for (TObjectIterator<ULocalLightComponent> It; It; ++It)
	{
		ULocalLightComponent* Light = *It;
		if (Light->IsA<URectLightComponent>() || Light->GetWorld() != World || !Cast<ACampusSchoolBuilder>(Light->GetOwner()))
		{
			continue;
		}
		const bool bWant = GLocalLightShadows && Light->IntensityUnits == ELightUnits::Lumens && Light->Intensity > 1500.f;
		if (Light->CastShadows != bWant)
		{
			Light->SetCastShadows(bWant);
		}
	}
	// Low view distance stops the city at ~1.1 km; the outer districts beyond only fill the skyline.
	const int32 FarCityCull = GViewDistance == 0 ? 110000 : 0;
	for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
	{
		UInstancedStaticMeshComponent* ISM = *It;
		if (ISM->GetWorld() == World && ISM->ComponentHasTag(TEXT("FarCity")) && ISM->InstanceEndCullDistance != FarCityCull)
		{
			ISM->SetCullDistances(0, FarCityCull);
		}
	}
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* Mesh = *It;
		if (Mesh->GetWorld() == World && Cast<ACampusSchoolBuilder>(Mesh->GetOwner()) && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetFName() == TEXT("SM_Campus_Terrain")
			&& Mesh->CastShadow != GTerrainShadows)
		{
			Mesh->SetCastShadow(GTerrainShadows);
		}
	}
}

bool CampusGraphics::TerrainShadows()
{
	return GTerrainShadows;
}

int32 CampusGraphics::EffectsLevel()
{
	return GEffectsLevel;
}

bool CampusGraphics::LowOpticView()
{
	return GLowOpticView;
}

float CampusGraphics::ViewHeightPixels()
{
	FVector2D ViewSize(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewSize);
	}
	return FMath::Max(ViewSize.Y, 360.f);
}

int32 CampusGraphics::OpticCaptureSize(float Diameter)
{
	const float Pixels = ViewHeightPixels() * FMath::Clamp(Diameter, 0.2f, 1.f) * GOpticSupersample;
	return FMath::Clamp(FMath::CeilToInt(Pixels / 16.f) * 16, 256, 2048);
}

float CampusGraphics::BrightnessBias()
{
	return GBrightness;
}

TArray<FIntPoint> CampusGraphics::Resolutions()
{
	TArray<FIntPoint> Out;
	FScreenResolutionArray Modes;
	if (RHIGetAvailableResolutions(Modes, true))
	{
		for (const FScreenResolutionRHI& M : Modes)
		{
			if (M.Width >= 1280 && M.Height >= 720)
			{
				Out.AddUnique(FIntPoint((int32)M.Width, (int32)M.Height));
			}
		}
	}
	Out.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y > B.X * B.Y; });
	return Out;
}
