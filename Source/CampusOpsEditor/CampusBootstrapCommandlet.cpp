#include "CampusBootstrapCommandlet.h"
#include "CampusSynth.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AnimationRuntime.h"
#include "Map/CampusSchoolBuilder.h"
#include "Menu/CampusMenuStage.h"
#include "Menu/CampusMenuCameraDirector.h"
#include "Menu/CampusMenuGameMode.h"
#include "Game/CampusGameModes.h"
#include "CampusAssets.h"

#include "CampusEditorCommon.h"
#include "CampusAssetImport.h"
#include "CampusUIAssets.h"
#include "CampusWeaponKit.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionPreSkinnedPosition.h"
#include "Materials/MaterialExpressionPreSkinnedNormal.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Game/CampusMaps.h"

DEFINE_LOG_CATEGORY(LogCampusBootstrap);

namespace
{
	using namespace CampusEditor;

	/** Shared runtime parameters (weather, night windows, rain mask) and the render target the rain mask is captured into. */
	void BuildEnvironmentAssets(bool bRainSheetOnly = false)
	{
		if (!bRainSheetOnly)
		{
			const FString Name = TEXT("MPC_Campus_Env");
			UPackage* Package = CreatePackage(*(FString(TEXT("/Game/Campus/Materials/")) + Name));
			Package->FullyLoad();
			UMaterialParameterCollection* MPC = FindObject<UMaterialParameterCollection>(Package, *Name);
			if (!MPC)
			{
				MPC = NewObject<UMaterialParameterCollection>(Package, *Name, RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(MPC);
			}
			for (const TCHAR* Param : { TEXT("Wetness"), TEXT("NightLights"), TEXT("Rain"), TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop"),
				TEXT("WindX"), TEXT("WindY"), TEXT("WindStrength") })
			{
				if (!MPC->GetScalarParameterByName(Param))
				{
					FCollectionScalarParameter P;
					P.ParameterName = Param;
					P.DefaultValue = 0.f;
					MPC->ScalarParameters.Add(P);
				}
			}
			MPC->PostEditChange();
			SaveAsset(MPC);
		}
		if (!bRainSheetOnly)
		{
			const FString Name = TEXT("RT_Campus_RainMask");
			UPackage* Package = CreatePackage(*(FString(TEXT("/Game/Campus/Materials/")) + Name));
			Package->FullyLoad();
			UTextureRenderTarget2D* RT = FindObject<UTextureRenderTarget2D>(Package, *Name);
			if (!RT)
			{
				RT = NewObject<UTextureRenderTarget2D>(Package, *Name, RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(RT);
			}
			RT->RenderTargetFormat = RTF_R32f;
			RT->ClearColor = FLinearColor(1.0e6f, 0.f, 0.f, 1.f);
			RT->bAutoGenerateMips = false;
			RT->InitAutoFormat(1024, 1024);
			RT->UpdateResourceImmediate(true);
			SaveAsset(RT);
		}
		// Rain streak / splash: translucent, unlit (it carries the sky's light), soft toward the ends of its box.
		if (!bRainSheetOnly)
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Rain")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TwoSided = true;
			// A drop right in front of the lens would be a huge smear: they fade out inside the last metre and a half.
			UMaterialExpressionCustom* Ends = B.Custom(TEXT("float z = LP.z / 50.0; return pow(saturate(1.0 - z * z), 1.5) * saturate((D - 70.0) / 160.0);"), CMOT_Float1, { TEXT("LP"), TEXT("D") });
			UMaterialExpressionLocalPosition* LP = B.Make<UMaterialExpressionLocalPosition>();
			LP->LocalOrigin = ELocalPositionOrigin::Instance;
			B.Link(LP, Ends, TEXT("LP"));
			B.Link(B.Make<UMaterialExpressionPixelDepth>(), Ends, TEXT("D"));
			// Drops differ: a few catch the light and stand out, most are faint (otherwise it reads as ruled grey lines).
			UMaterialExpressionCustom* Var = B.Custom(TEXT("float r = frac(R * 7.31); return 0.3 + 1.2 * r * r;"), CMOT_Float1, { TEXT("R") });
			B.Link(B.InstanceRandom(), Var, TEXT("R"));
			B.Out(B.ExposureCompensated(B.Mul(B.Mul(B.Vec(TEXT("Color"), FLinearColor(0.75f, 0.78f, 0.82f)), B.Scalar(TEXT("Intensity"), 1.f)), Var), 0.3f), MP_EmissiveColor);
			B.Out(B.Mul(B.Mul(B.Mul(Ends, B.Scalar(TEXT("Opacity"), 0.3f)), B.CustomData(0, 1.f, false)), Var), MP_Opacity);
			// A falling drop is a little lens: mostly it bends what is behind it (strongest across its middle, nothing at
			// its rim), and only faintly glows with the light it catches.
			B.M->RefractionMethod = RM_2DOffset;
			UMaterialExpressionCustom* Lens = B.Custom(TEXT(R"(
float2 x = clamp(LP.xy / 50.0, -1.0, 1.0);
float z = LP.z / 50.0;
float along = pow(saturate(1.0 - z * z), 1.5);
float bulge = saturate(1.0 - dot(x, x));
return x * bulge * along * saturate((D - 70.0) / 160.0) * 0.004 * saturate(350.0 / max(D, 1.0)) * K;
)"), CMOT_Float2, { TEXT("LP"), TEXT("D"), TEXT("K") });
			UMaterialExpressionLocalPosition* LensLP = B.Make<UMaterialExpressionLocalPosition>();
			LensLP->LocalOrigin = ELocalPositionOrigin::Instance;
			B.Link(LensLP, Lens, TEXT("LP"));
			B.Link(B.Make<UMaterialExpressionPixelDepth>(), Lens, TEXT("D"));
			B.Link(B.Scalar(TEXT("Refraction"), 1.f), Lens, TEXT("K"));
			B.Out(Lens, MP_Refraction);
			FinishMaterial(B.M);
		}
		// Rain curtain: big vertical cards round the camera at a few distances, each drawing columns of falling streaks
		// anchored in the world (so they do not swim as the cards follow the camera), slanted by the wind, with a
		// little haze; per instance 0 column width, 1 streak spacing, 2 streak opacity, 3 haze. Nothing where the
		// sky cannot reach (the same top-down capture that wets the surfaces).
		{
			FMat B{ NewMaterial(TEXT("M_Campus_RainSheet")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TwoSided = false;
			UMaterialParameterCollection* Env = EnvironmentCollection();
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float s = WP.x + WP.y + WP.z * 0.12;
float col = floor(s / W);
float h1 = frac(sin(col * 12.9898) * 43758.5453);
float h2 = frac(sin(col * 78.233 + 1.7) * 12543.311);
float y = WP.z + T * 950.0 * (0.85 + 0.3 * h2) + h1 * 3000.0;
float seg = frac(y / L);
// A drop smears only its fall over the exposure (a hand's length up close), not a long scratch down the view.
float streak = smoothstep(0.0, 0.02, seg) * (1.0 - smoothstep(0.03, 0.15, seg));
float w = abs(frac(s / W) - 0.5) * 2.0;
float thin = saturate(1.0 - w * 2.5);
float a = streak * thin * step(0.5, h2) * Opac + Haze;
float sky = 1.0;
if (MaskSize > 1.0)
{
	float2 mUV = float2((WP.y - MaskY) / MaskSize + 0.5, 0.5 - (WP.x - MaskX) / MaskSize);
	if (all(mUV > 0.0) && all(mUV < 1.0))
	{
		float mTop = MaskTop - Texture2DSampleLevel(MaskTex, MaskTexSampler, mUV, 0).r;
		sky = saturate((WP.z - (mTop - 20.0)) / 40.0);
	}
}
// No edges: the card fades out toward its top and bottom, and wherever it is seen edge-on (looking up or down past it)
// rather than across, so it never shows as a box in the sky.
float edge = saturate((1.0 - abs(LP.z / 50.0)) * 10.0);
float facing = saturate((abs(dot(normalize(V), normalize(N))) - 0.15) * 2.5);
float contact = saturate((SD - PD) / 90.0);
float vis = sky * Amount * edge * facing * contact;
// Each streak bends the view behind it a touch across its width (the water in it is what shows, more than any glow).
float across = (frac(s / W) - 0.5) * 2.0;
Refr = float2(across * thin * streak * step(0.5, h2) * 0.0025 * Opac * 3.0 * vis, 0.0);
return saturate(a) * vis;
)"), CMOT_Float1, { TEXT("WP"), TEXT("T"), TEXT("W"), TEXT("L"), TEXT("Opac"), TEXT("Haze"), TEXT("Amount"), TEXT("MaskTex"), TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop"), TEXT("LP"), TEXT("V"), TEXT("N"), TEXT("SD"), TEXT("PD") }, { { TEXT("Refr"), CMOT_Float2 } });
			B.M->RefractionMethod = RM_2DOffset;
			{
				UMaterialExpressionLocalPosition* SheetLP = B.Make<UMaterialExpressionLocalPosition>();
				SheetLP->LocalOrigin = ELocalPositionOrigin::Instance;
				B.Link(SheetLP, C, TEXT("LP"));
				B.Link(B.Make<UMaterialExpressionCameraVectorWS>(), C, TEXT("V"));
				B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("N"));
			}
			B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
			B.Link(B.Make<UMaterialExpressionTime>(), C, TEXT("T"));
			B.Link(B.Make<UMaterialExpressionSceneDepth>(), C, TEXT("SD"));
			B.Link(B.Make<UMaterialExpressionPixelDepth>(), C, TEXT("PD"));
			B.Link(B.CustomData(0, 4.f, false), C, TEXT("W"));
			B.Link(B.CustomData(1, 160.f, false), C, TEXT("L"));
			B.Link(B.CustomData(2, 0.3f, false), C, TEXT("Opac"));
			B.Link(B.CustomData(3, 0.02f, false), C, TEXT("Haze"));
			B.Link(B.Scalar(TEXT("Amount"), 1.f), C, TEXT("Amount"));
			UTexture* Mask = LoadObject<UTexture>(nullptr, TEXT("/Game/Campus/Materials/RT_Campus_RainMask.RT_Campus_RainMask"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Mask)
			{
				Mask = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			}
			B.Link(B.TexObject(TEXT("RainMask"), Mask, SAMPLERTYPE_LinearColor), C, TEXT("MaskTex"));
			for (const TCHAR* P : { TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop") })
			{
				B.Link(B.Collection(Env, P), C, P);
			}
			B.Out(B.ExposureCompensated(B.Mul(B.Vec(TEXT("Color"), FLinearColor(0.75f, 0.78f, 0.82f)), B.Scalar(TEXT("Intensity"), 1.f)), 0.3f), MP_EmissiveColor);
			B.Out(C, MP_Opacity);
			B.Out(C, MP_Refraction, TEXT("Refr"));
			FinishMaterial(B.M);
		}
		if (bRainSheetOnly)
		{
			return;
		}
		// Water simulation step, drawn into a render target every frame: a damped wave equation on a height field
		// (R the height now, G the height a step ago), clamped at the pool walls so waves reflect off them, plus the
		// disturbances of the frame (up to eight brushes: uv, radius in texels, strength) and the rain's drops.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_WaterSim")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Opaque;
			UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float2 px = float2(1.0 / SizeX, 1.0 / SizeY);
float2 here = Texture2DSampleLevel(Prev, PrevSampler, UV, 0).rg;
float n = Texture2DSampleLevel(Prev, PrevSampler, UV + float2(px.x, 0), 0).r + Texture2DSampleLevel(Prev, PrevSampler, UV - float2(px.x, 0), 0).r
	+ Texture2DSampleLevel(Prev, PrevSampler, UV + float2(0, px.y), 0).r + Texture2DSampleLevel(Prev, PrevSampler, UV - float2(0, px.y), 0).r;
float h = (n * 0.5 - here.g) * Damping;
float4 Br[8] = { B0, B1, B2, B3, B4, B5, B6, B7 };
[unroll] for (int i = 0; i < 8; i++)
{
	float2 d = (UV - Br[i].xy) * float2(SizeX, SizeY);
	float r = max(Br[i].z, 0.5);
	h += Br[i].w * exp(-dot(d, d) / (r * r));
}
float2 cell = floor(UV * float2(SizeX, SizeY));
float k = frac(sin(dot(cell + Seed * 0.6180339, float2(12.9898, 78.233))) * 43758.5453);
h -= (k < Rain ? 0.5 : 0.0);
return float3(clamp(h, -4.0, 4.0), here.r, 0);
)"), CMOT_Float3, { TEXT("Prev"), TEXT("UV"), TEXT("SizeX"), TEXT("SizeY"), TEXT("Damping"), TEXT("Rain"), TEXT("Seed"), TEXT("B0"), TEXT("B1"), TEXT("B2"), TEXT("B3"), TEXT("B4"), TEXT("B5"), TEXT("B6"), TEXT("B7") });
			B.Link(B.TexObject(TEXT("Prev"), White, SAMPLERTYPE_LinearColor), C, TEXT("Prev"));
			B.Link(B.Make<UMaterialExpressionTextureCoordinate>(), C, TEXT("UV"));
			B.Link(B.Scalar(TEXT("SizeX"), 128.f), C, TEXT("SizeX"));
			B.Link(B.Scalar(TEXT("SizeY"), 640.f), C, TEXT("SizeY"));
			B.Link(B.Scalar(TEXT("Damping"), 0.985f), C, TEXT("Damping"));
			B.Link(B.Scalar(TEXT("Rain"), 0.f), C, TEXT("Rain"));
			B.Link(B.Scalar(TEXT("Seed"), 0.f), C, TEXT("Seed"));
			for (int32 i = 0; i < 8; ++i)
			{
				const FString Name = FString::Printf(TEXT("B%d"), i);
				B.Link(B.Vec(*FString::Printf(TEXT("Brush%d"), i), FLinearColor(0.f, 0.f, 1.f, 0.f)), C, *Name, TEXT("RGBA"));
			}
			B.Out(C, MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		// Sunlight focused by the moving surface onto the pool floor and walls: bright wandering threads, projected as a
		// decal straight down through the water. Strength follows the sun (set at runtime).
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Caustics")) };
			B.M->MaterialDomain = MD_Surface;
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Additive;
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
// The pattern tiles every 2.4 m and is built for coordinates round -250 (one period is 2 pi there).
float2 p = frac(WP.xy / 240.0) * 6.2831853 - 250.0;
float t = T * 0.5 + 23.0;
float2 q = p;
float c = 1.0;
const float inten = 0.005;
[unroll] for (int n = 0; n < 5; n++)
{
	float tt = t * (1.0 - (3.5 / float(n + 1)));
	q = p + float2(cos(tt - q.x) + sin(tt + q.y), sin(tt - q.y) + cos(tt + q.x));
	c += 1.0 / length(float2(p.x / (sin(q.x + tt) / inten), p.y / (cos(q.y + tt) / inten)));
}
c /= 5.0;
c = 1.17 - pow(clamp(c, 0.0, 1.17), 1.4);
// The pattern spikes toward infinity where the threads cross: kept to 0..1 so it is light, not a blown-out flash.
return saturate(pow(saturate(c), 8.0));
)"), CMOT_Float1, { TEXT("WP"), TEXT("T") });
			B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
			B.Link(B.Make<UMaterialExpressionTime>(), C, TEXT("T"));
			B.Out(B.Mul(B.Mul(C, B.Vec(TEXT("Color"), FLinearColor(0.85f, 0.97f, 1.f))), B.Scalar(TEXT("Strength"), 0.f)), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		// Pool water: a translucent, refracting surface moved by a sum of small wind waves, clear and pale over the
		// shallow steps, deepening to teal over the floor; the sky and the lamps glint off it.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Water")) };
			B.M->SetShadingModel(MSM_DefaultLit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
			B.M->TwoSided = false;
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float2 p = WP.xy;
float2 g = 0;
const float4 W[6] = {
	float4( 0.94, 0.34, 140.0, 0.12), float4(-0.42, 0.91, 95.0, 0.09), float4(0.71, -0.70, 61.0, 0.06),
	float4(-0.99, -0.12, 38.0, 0.04), float4(0.28, 0.96, 23.0, 0.025), float4(0.83, 0.56, 13.0, 0.015) };
[unroll] for (int i = 0; i < 6; i++)
{
	float k = 6.2831853 / W[i].z;
	float ph = dot(W[i].xy, p) * k + T * sqrt(981.0 * k) * 0.35;
	g += W[i].w * k * W[i].z * 0.12 * cos(ph) * W[i].xy;
}
// The interactive part: the simulated height field over the pool (wakes, splashes, rings off the walls).
float foam = 0.0;
float2 suv = (WP.xy - PoolMin.xy) / max(PoolSize.xy, 1.0);
if (all(suv > 0.0) && all(suv < 1.0))
{
	float2 px = SimTexel.xy;
	float hL = Texture2DSampleLevel(SimTex, SimTexSampler, suv - float2(px.x, 0), 0).r;
	float hR = Texture2DSampleLevel(SimTex, SimTexSampler, suv + float2(px.x, 0), 0).r;
	float hD = Texture2DSampleLevel(SimTex, SimTexSampler, suv - float2(0, px.y), 0).r;
	float hU = Texture2DSampleLevel(SimTex, SimTexSampler, suv + float2(0, px.y), 0).r;
	float2 sg = float2(hR - hL, hU - hD) * SimStrength;
	g += sg;
	foam = saturate(length(sg) * 0.8 - 0.45);
}
Nrm = normalize(float3(-g * Strength, 1.0));
float d = max(D, 0.0);
Opac = saturate(lerp(0.18, 0.82, saturate(d / 220.0)) + foam * 0.6);
float3 shallow = float3(0.30, 0.62, 0.62);
float3 deep = float3(0.015, 0.11, 0.14);
return lerp(lerp(shallow, deep, saturate(d / 140.0)), float3(0.8, 0.86, 0.88), foam);
)"), CMOT_Float3, { TEXT("WP"), TEXT("T"), TEXT("D"), TEXT("Strength"), TEXT("SimTex"), TEXT("PoolMin"), TEXT("PoolSize"), TEXT("SimTexel"), TEXT("SimStrength") }, { { TEXT("Nrm"), CMOT_Float3 }, { TEXT("Opac"), CMOT_Float1 } });
			{
				UTexture* Black = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
				B.Link(B.TexObject(TEXT("SimTex"), Black, SAMPLERTYPE_LinearColor), C, TEXT("SimTex"));
				B.Link(B.Vec(TEXT("PoolMin"), FLinearColor(0.f, 0.f, 0.f, 0.f)), C, TEXT("PoolMin"));
				B.Link(B.Vec(TEXT("PoolSize"), FLinearColor(0.f, 0.f, 0.f, 0.f)), C, TEXT("PoolSize"));
				B.Link(B.Vec(TEXT("SimTexel"), FLinearColor(1.f / 128.f, 1.f / 640.f, 0.f, 0.f)), C, TEXT("SimTexel"));
				B.Link(B.Scalar(TEXT("SimStrength"), 0.35f), C, TEXT("SimStrength"));
			}
			B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
			B.Link(B.Make<UMaterialExpressionTime>(), C, TEXT("T"));
			B.Link(B.Binary(UMaterialExpressionSubtract::StaticClass(), B.Make<UMaterialExpressionSceneDepth>(), B.Make<UMaterialExpressionPixelDepth>()), C, TEXT("D"));
			B.Link(B.Scalar(TEXT("WaveStrength"), 1.f), C, TEXT("Strength"));
			B.Out(C, MP_BaseColor, TEXT("return"));
			B.Out(C, MP_Normal, TEXT("Nrm"));
			B.Out(C, MP_Opacity, TEXT("Opac"));
			B.Out(B.Const(0.02f), MP_Roughness);
			B.Out(B.Const(0.5f), MP_Specular);
			B.Out(B.Const(1.33f), MP_Refraction);
			FinishMaterial(B.M);
		}
		// A ripple ring spreading on the water: per instance 0 its age (0..1 across its life), 1 its strength. Only the
		// slope of the ring shows, as a bright moving line of reflection.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Ripple")) };
			B.M->SetShadingModel(MSM_DefaultLit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float2 q = (UV - 0.5) * 2.0;
float r = length(q);
float wave = 0;
float slope = 0;
[unroll] for (int i = 0; i < 3; i++)
{
	float c = Age - i * 0.13;
	float w = 0.05 + 0.05 * Age;
	float x = (r - c) / w;
	float env = saturate(c * 6.0) * (1.0 - saturate(c));
	wave += exp(-x * x) * env;
	slope += -2.0 * x * exp(-x * x) * env / w;
}
float fade = Strength * (1.0 - smoothstep(0.85, 1.0, r));
float2 dir = r > 0.001 ? q / r : 0;
Nrm = normalize(float3(dir * slope * 0.06 * fade, 1.0));
Opac = saturate(wave * fade * 0.9);
return float3(0.8, 0.9, 0.92);
)"), CMOT_Float3, { TEXT("UV"), TEXT("Age"), TEXT("Strength") }, { { TEXT("Nrm"), CMOT_Float3 }, { TEXT("Opac"), CMOT_Float1 } });
			B.Link(B.Make<UMaterialExpressionTextureCoordinate>(), C, TEXT("UV"));
			B.Link(B.CustomData(0, 0.3f, false), C, TEXT("Age"));
			B.Link(B.CustomData(1, 1.f, false), C, TEXT("Strength"));
			B.Out(C, MP_BaseColor, TEXT("return"));
			B.Out(C, MP_Normal, TEXT("Nrm"));
			B.Out(C, MP_Opacity, TEXT("Opac"));
			B.Out(B.Const(0.04f), MP_Roughness);
			B.Out(B.Const(0.6f), MP_Specular);
			FinishMaterial(B.M);
		}
	}

	void BuildMaterials()
	{
		// Lit: Color / Roughness / Metallic / Emissive, with handling wear (roughness and tone breakup) projected in mesh space
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Lit")) };
			UMaterialExpression* Color = B.Vec(TEXT("Color"), FLinearColor(0.5f, 0.5f, 0.5f));
			UTexture* Grunge = LoadObject<UTexture>(nullptr, TEXT("/Game/Campus/Textures/T_Campus_Grunge.T_Campus_Grunge"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Grunge)
			{
				UMaterialExpressionCustom* Wear = B.Custom(TEXT(R"(
float3 a = abs(N);
float2 uv = (a.z > a.x && a.z > a.y) ? P.xy : ((a.x > a.y) ? P.yz : P.xz);
float4 g1 = Texture2DSample(GrungeTex, GrungeTexSampler, uv / 9.0);
float4 g2 = Texture2DSample(GrungeTex, GrungeTexSampler, uv / 37.0 + 0.3);
Rough = saturate(R + (g1.b - 0.5) * 0.22 + (g2.g - 0.5) * 0.2);
return C * (1.0 + (g2.r - 0.5) * 0.08);
)"), CMOT_Float3, { TEXT("P"), TEXT("N"), TEXT("C"), TEXT("R"), TEXT("GrungeTex") }, { { TEXT("Rough"), CMOT_Float1 } });
				B.Link(B.Interp(B.Make<UMaterialExpressionPreSkinnedPosition>()), Wear, TEXT("P"));
				B.Link(B.Interp(B.Make<UMaterialExpressionPreSkinnedNormal>()), Wear, TEXT("N"));
				B.Link(Color, Wear, TEXT("C"));
				B.Link(B.Scalar(TEXT("Roughness"), 0.5f), Wear, TEXT("R"));
				B.Link(B.TexObject(TEXT("GrungeTex"), Grunge, SAMPLERTYPE_LinearColor), Wear, TEXT("GrungeTex"));
				B.Out(Wear, MP_BaseColor, TEXT("return"));
				B.Out(Wear, MP_Roughness, TEXT("Rough"));
			}
			else
			{
				B.Out(Color, MP_BaseColor);
				B.Out(B.Scalar(TEXT("Roughness"), 0.5f), MP_Roughness);
			}
			B.Out(B.Scalar(TEXT("Metallic"), 0.f), MP_Metallic);
			// Emissive is authored in units of 400 nits (physically lit scene: sun ~75k lux).
			B.Out(B.MulC(B.Mul(Color, B.Scalar(TEXT("Emissive"), 0.f)), 400.f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		// Instanced: per-instance color, roughness, metallic, emissive, tile seams and subtle variation
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Instanced")) };
			UMaterialExpression* Color = B.CustomData(0, 0.5f, true);
			UMaterialExpression* Rough = B.CustomData(3, 0.6f, false);
			UMaterialExpression* Metal = B.CustomData(4, 0.f, false);
			UMaterialExpression* Emis = B.CustomData(5, 0.f, false);
			UMaterialExpression* Tile = B.CustomData(6, 0.f, false);
			UMaterialExpression* Seam = B.CustomData(7, 0.f, false);

			UMaterialExpression* TileSafe = B.Max(Tile, B.Const(1.f));
			UMaterialExpression* WP = B.Make<UMaterialExpressionWorldPosition>();
			UMaterialExpression* F = B.Frac(B.Div(WP, TileSafe));
			UMaterialExpression* Edge = B.Min(F, B.OneMinus(F));
			UMaterialExpression* Line = B.OneMinus(B.Saturate(B.MulC(B.Mul(Edge, TileSafe), 1.25f)));
			UMaterialExpression* Weight = B.OneMinus(B.Abs(B.Make<UMaterialExpressionVertexNormalWS>()));
			UMaterialExpression* Weighted = B.Mul(Line, Weight);
			UMaterialExpression* SeamMask = B.Max(B.Max(B.Mask(Weighted, true, false, false), B.Mask(Weighted, false, true, false)), B.Mask(Weighted, false, false, true));
			UMaterialExpression* Dark = B.OneMinus(B.Mul(SeamMask, Seam));
			UMaterialExpression* Variation = B.LerpC(0.93f, 1.05f, B.InstanceRandom());
			B.Out(B.Mul(B.Mul(Color, Dark), Variation), MP_BaseColor);
			B.Out(Rough, MP_Roughness);
			B.Out(Metal, MP_Metallic);
			B.Out(B.MulC(B.Mul(Color, Emis), 400.f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		// Unlit opaque glow (sparks, emitters): exposure-relative, so it reads the same in sun and in shade.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Unlit")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.Out(B.ExposureCompensated(B.Mul(B.Vec(TEXT("Color"), FLinearColor(1.f, 0.6f, 0.2f)), B.Scalar(TEXT("Intensity"), 10.f)), 0.26f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		// Additive soft sprite (muzzle flash, tracer, laser), exposure-relative like the unlit glow.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Additive")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Additive;
			B.M->TwoSided = true;
			UMaterialExpression* Soft = B.Pow(B.Saturate(B.OneMinus(B.MulC(B.RadialDistance(), 2.f))), 2.2f);
			UMaterialExpression* Glow = B.Mul(B.Vec(TEXT("Color"), FLinearColor(1.f, 0.6f, 0.2f)), B.Scalar(TEXT("Intensity"), 20.f));
			B.Out(B.ExposureCompensated(B.Mul(Glow, Soft), 0.26f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
	}

	// ------------------------------------------------------------------ audio
	void BuildAudio()
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("CampusGenerated/Audio");
		IFileManager::Get().MakeDirectory(*Dir, true);
		// Sounds built from recordings (Tools/BuildAudio.py, CC0 sources) replace the synthesized take of the same name.
		const FString RecordedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceArt/Audio"));
		TArray<TPair<FString, FString>> Files;
		TSet<FString> Names;
		int32 Recorded = 0;
		for (const TPair<FString, TArray<uint8>>& Sound : CampusSynthLibrary::BuildAll())
		{
			Names.Add(Sound.Key);
			const FString Override = RecordedDir / (TEXT("SFX_") + Sound.Key + TEXT(".wav"));
			if (FPaths::FileExists(Override))
			{
				Files.Emplace(Sound.Key, Override);
				++Recorded;
				continue;
			}
			const FString File = FPaths::ConvertRelativePathToFull(Dir / (TEXT("SFX_") + Sound.Key + TEXT(".wav")));
			FFileHelper::SaveArrayToFile(Sound.Value, *File);
			Files.Emplace(Sound.Key, File);
		}
		TArray<FString> Extra;
		IFileManager::Get().FindFiles(Extra, *(RecordedDir / TEXT("SFX_*.wav")), true, false);
		for (const FString& Name : Extra)
		{
			const FString Key = FPaths::GetBaseFilename(Name).RightChop(4);
			if (!Names.Contains(Key))
			{
				Files.Emplace(Key, RecordedDir / Name);
				++Recorded;
			}
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("%d sounds from recordings"), Recorded);
		TArray<UAssetImportTask*> Tasks;
		for (const TPair<FString, FString>& Sound : Files)
		{
			const FString& File = Sound.Value;
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->AddToRoot();
			Task->Filename = File;
			Task->DestinationPath = TEXT("/Game/Campus/Audio");
			Task->DestinationName = TEXT("SFX_") + Sound.Key;
			Task->bAutomated = true;
			Task->bReplaceExisting = true;
			Task->bReplaceExistingSettings = true;
			Task->bSave = true;
			Task->bAsync = false;
			Tasks.Add(Task);
		}
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
		int32 Imported = 0;
		for (UAssetImportTask* Task : Tasks)
		{
			Imported += Task->GetObjects().Num() > 0 ? 1 : 0;
			Task->RemoveFromRoot();
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("Imported %d / %d sounds"), Imported, Tasks.Num());
	}

	// ------------------------------------------------------------------ maps
	UWorld* NewWorld(const FString& PackageName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		// The map is regenerated from scratch; the in-memory package replaces whatever is on disk.
		Package->MarkAsFullyLoaded();
		UWorld::InitializationValues IVS;
		IVS.RequiresHitProxies(false).ShouldSimulatePhysics(false).EnableTraceCollision(true).CreateNavigation(true).CreateAISystem(false).AllowAudioPlayback(false).CreatePhysicsScene(true).CreateWorldPartition(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(*FPackageName::GetShortName(PackageName)), Package, true, ERHIFeatureLevel::Num, &IVS);
		World->SetFlags(RF_Public | RF_Standalone);
		return World;
	}

	void AddEnvironment(UWorld* World, bool bGoldenHour, float GroundZ = 0.f)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// The runtime environment (ACampusEnvironment) re-dresses all of this for the match's time and weather; these
		// are the editor / fallback look.

		// Physical light units throughout (lux / lumens / nits) with real eye adaptation: a classroom is ~5 EV darker
		// than the sunlit yard, so interiors look dark from outside and the windows blow bright from inside.
		// Gameplay map: hazy subtropical daylight. Menu map: low warm sun for the title shots.
		// Gameplay: clear late-morning sun, high enough that the yards are sunlit rather than half in building shadow.
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 3000.f), bGoldenHour ? FRotator(-13.f, 140.f, 0.f) : FRotator(-48.f, 128.f, 0.f), P);
		if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			C->SetMobility(EComponentMobility::Movable);
			C->SetIntensity(bGoldenHour ? 18000.f : 90000.f);
			C->SetAtmosphereSunLight(true);
			C->SetUseTemperature(true);
			C->SetTemperature(bGoldenHour ? 4300.f : 5800.f);
			// The sun's disc is ~0.5 degrees: crisp shadow edges that soften with distance from the caster.
			C->SetLightSourceAngle(bGoldenHour ? 1.f : 0.9f);
			C->ContactShadowLength = 0.02f;
			C->bCastCloudShadows = true;
			C->CloudShadowStrength = 0.8f;
			C->SetDynamicShadowDistanceMovableLight(20000.f);
		}
		ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		if (USkyAtmosphereComponent* A = Atmosphere->FindComponentByClass<USkyAtmosphereComponent>())
		{
			// Physical Rayleigh (deep blue zenith) with the humid aerosol haze of a subtropical city: the towers a
			// kilometre off are already softened and blued, the mountains beyond pale.
			A->SetMieScatteringScale(bGoldenHour ? 0.006f : 0.02f);
			A->SetRayleighScatteringScale(0.0331f);
			A->SetAerialPespectiveViewDistanceScale(bGoldenHour ? 1.f : 1.8f);
			if (GroundZ != 0.f)
			{
				// The planet's surface is the street, far below the playable roof.
				A->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
				Atmosphere->SetActorLocation(FVector(0.f, 0.f, GroundZ));
			}
		}
		ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, P);
		if (USkyLightComponent* C = Sky->GetLightComponent())
		{
			C->SetMobility(EComponentMobility::Movable);
			C->bRealTimeCapture = true;
			C->SourceType = SLS_CapturedScene;
			// The captured atmosphere is already in physical units; a little extra keeps shade from going muddy.
			C->SetIntensity(bGoldenHour ? 1.f : 1.15f);
		}
		AVolumetricCloud* Clouds = World->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		if (UVolumetricCloudComponent* C = Clouds->FindComponentByClass<UVolumetricCloudComponent>())
		{
			// Fair-weather cumulus up high and thin, so they break up into blue sky instead of a grey ceiling.
			C->SetLayerBottomAltitude(bGoldenHour ? 5.f : 3.2f);
			C->SetLayerHeight(bGoldenHour ? 10.f : 3.f);
		}
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector(0.f, 0.f, GroundZ - 100.f), FRotator::ZeroRotator, P);
		if (UExponentialHeightFogComponent* C = Fog->GetComponent())
		{
			C->SetFogDensity(bGoldenHour ? 0.018f : 0.03f);
			C->SetFogHeightFalloff(bGoldenHour ? 0.18f : 0.12f);
			C->SetStartDistance(bGoldenHour ? 1500.f : 800.f);
			C->SetFogMaxOpacity(bGoldenHour ? 0.92f : 0.85f);
			if (!bGoldenHour)
			{
				// Distance haze picks up the sky's blue rather than flat grey, and glows warm toward the sun.
				C->SetFogInscatteringColor(FLinearColor(0.34f, 0.43f, 0.58f));
				C->SetDirectionalInscatteringExponent(8.f);
				C->SetDirectionalInscatteringStartDistance(3000.f);
				C->SetDirectionalInscatteringColor(FLinearColor(0.55f, 0.47f, 0.36f));
				C->SetVolumetricFog(true);
				C->SetVolumetricFogScatteringDistribution(0.6f);
				C->SetVolumetricFogExtinctionScale(0.6f);
				C->SetVolumetricFogDistance(6000.f);
			}
		}
		APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		PPV->bUnbound = true;
		FPostProcessSettings& S = PPV->Settings;
		S.bOverride_MotionBlurAmount = true;
		S.MotionBlurAmount = 0.f;
		// Eye adaptation over the full indoor/outdoor range (EV100): dim classrooms ~8-10, sunlit yard ~14-15.
		S.bOverride_AutoExposureMethod = true;
		S.AutoExposureMethod = AEM_Histogram;
		S.bOverride_AutoExposureMinBrightness = true;
		S.AutoExposureMinBrightness = 5.f;
		S.bOverride_AutoExposureMaxBrightness = true;
		S.AutoExposureMaxBrightness = 15.5f;
		S.bOverride_AutoExposureSpeedUp = true;
		// Stops per second: stepping through a doorway (~4 EV) settles in about a second either way, a short eye
		// adaptation rather than a long fade.
		S.AutoExposureSpeedUp = 6.f;
		S.bOverride_AutoExposureSpeedDown = true;
		S.AutoExposureSpeedDown = 4.f;
		// Meter on the brighter part of the frame: the dark weapon and arms fill the lower view and would otherwise drag
		// the average down and blow out the sunlit yard (and the arms with it).
		S.bOverride_AutoExposureLowPercent = true;
		S.AutoExposureLowPercent = bGoldenHour ? 10.f : 30.f;
		S.bOverride_AutoExposureHighPercent = true;
		S.AutoExposureHighPercent = bGoldenHour ? 90.f : 95.f;
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = bGoldenHour ? 0.f : 0.3f;
		// Clean, bright daylight grade: light vignette, little grain or fringing, neutral-to-warm white balance.
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = bGoldenHour ? 0.4f : 0.22f;
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = 0.5f;
		S.bOverride_FilmGrainIntensity = true;
		S.FilmGrainIntensity = bGoldenHour ? 0.16f : 0.05f;
		S.bOverride_SceneFringeIntensity = true;
		S.SceneFringeIntensity = bGoldenHour ? 0.25f : 0.08f;
		S.bOverride_LocalExposureHighlightContrastScale = true;
		S.LocalExposureHighlightContrastScale = 0.8f;
		S.bOverride_LocalExposureShadowContrastScale = true;
		S.LocalExposureShadowContrastScale = bGoldenHour ? 0.85f : 0.75f;
		S.bOverride_WhiteTemp = true;
		S.WhiteTemp = bGoldenHour ? 6300.f : 6500.f;
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = bGoldenHour ? FVector4(0.95f, 0.95f, 0.95f, 1.f) : FVector4(1.f, 1.f, 1.f, 1.f);
		S.bOverride_ColorContrast = true;
		S.ColorContrast = bGoldenHour ? FVector4(1.04f, 1.04f, 1.04f, 1.f) : FVector4(1.05f, 1.05f, 1.05f, 1.f);
		S.bOverride_ColorGainShadows = true;
		S.ColorGainShadows = bGoldenHour ? FVector4(1.f, 1.f, 1.f, 1.f) : FVector4(1.f, 1.f, 1.03f, 1.f);
		// Lumen quality comes from the video options (scalability); the volume only sets the look.
		S.bOverride_LumenFrontLayerTranslucencyReflections = true;
		S.LumenFrontLayerTranslucencyReflections = true;
		S.bOverride_LumenMaxRoughnessToTraceReflections = true;
		S.LumenMaxRoughnessToTraceReflections = 0.4f;
		S.bOverride_LumenSkylightLeaking = true;
		S.LumenSkylightLeaking = bGoldenHour ? 0.03f : 0.06f;
	}

	/** A playable map: environment, the builder for Map, navigation over its playable bounds, a fallback player start. */
	bool BuildGameMap(ECampusMap Map, FCampusMapPreviewData& OutPreview)
	{
		const FString PackageName = CampusMaps::Get(Map).Package;
		UWorld* World = NewWorld(PackageName);
		const float GroundZ = Map == ECampusMap::Hotel ? -18000.f : 0.f;
		AddEnvironment(World, false, GroundZ);

		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.bDeferConstruction = true;
		ACampusSchoolBuilder* Builder = World->SpawnActor<ACampusSchoolBuilder>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		Builder->MapKind = Map;
		Builder->FinishSpawning(FTransform::Identity);
		P.bDeferConstruction = false;
		UE_LOG(LogCampusBootstrap, Display, TEXT("%s built: %d instances, %d spawns, %d hotspots"), *CampusMaps::Get(Map).Name.ToString(), Builder->InstanceCount, Builder->SpawnPoints.Num(), Builder->Hotspots.Num());

		const FBox Play = Builder->PlayableBounds.IsValid ? Builder->PlayableBounds : FBox(FVector(-4900.f, -5700.f, -200.f), FVector(4900.f, 5700.f, 1200.f));
		ANavMeshBoundsVolume* Nav = World->SpawnActor<ANavMeshBoundsVolume>(Play.GetCenter(), FRotator::ZeroRotator, P);
		UCubeBuilder* Cube = NewObject<UCubeBuilder>();
		Cube->X = Play.GetSize().X;
		Cube->Y = Play.GetSize().Y;
		Cube->Z = Play.GetSize().Z + 400.f;
		UActorFactory::CreateBrushForVolumeActor(Nav, Cube);

		const FTransform Start = Builder->SpawnPoints.Num() > 0 ? Builder->SpawnPoints[0].Transform : FTransform(FRotator(0.f, 90.f, 0.f), FVector(-875.f, -4900.f, 120.f));
		World->SpawnActor<APlayerStart>(Start.GetLocation() + FVector(0.f, 0.f, 10.f), Start.Rotator(), P);

		AWorldSettings* WS = World->GetWorldSettings();
		WS->DefaultGameMode = ACampusGameMode_TDM::StaticClass();
		WS->KillZ = Map == ECampusMap::Hotel ? -3000.f : -2000.f;

		OutPreview.Map = Map;
		OutPreview.Rects = Builder->MinimapRects;
		OutPreview.PlayableBounds = Play;

		const bool bOk = SaveAsset(World, true);
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		return bOk;
	}

	/** The main menu: the special operations HQ, with the menu stage and the camera director. */
	bool BuildMenuMap()
	{
		const FString PackageName = CampusAssets::MapMenu;
		UWorld* World = NewWorld(PackageName);
		AddEnvironment(World, true);

		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.bDeferConstruction = true;
		ACampusSchoolBuilder* Builder = World->SpawnActor<ACampusSchoolBuilder>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		Builder->MapKind = ECampusMap::HQ;
		Builder->FinishSpawning(FTransform::Identity);
		P.bDeferConstruction = false;
		ACampusMenuStage* Stage = World->SpawnActor<ACampusMenuStage>(FVector::ZeroVector, FRotator::ZeroRotator, P);
		Stage->SetupFromBuilder(Builder);
		World->SpawnActor<ACampusMenuCameraDirector>(FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, P);

		AWorldSettings* WS = World->GetWorldSettings();
		WS->DefaultGameMode = ACampusMenuGameMode::StaticClass();

		const bool bOk = SaveAsset(World, true);
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		return bOk;
	}

	bool SaveCatalog(const TArray<FCampusMapPreviewData>& Maps)
	{
		const FString Name = TEXT("DA_MapCatalog");
		UPackage* Package = CreatePackage(*(FString(TEXT("/Game/Campus/Maps/")) + Name));
		Package->FullyLoad();
		UCampusMapCatalog* Catalog = FindObject<UCampusMapCatalog>(Package, *Name);
		if (!Catalog)
		{
			Catalog = NewObject<UCampusMapCatalog>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Catalog);
		}
		Catalog->Maps = Maps;
		return SaveAsset(Catalog);
	}

	void InspectPropMaterials()
	{
		for (const TCHAR* Path : { TEXT("/Game/Campus/Props/SchoolDesk_01/SchoolDesk_01/Materials/SchoolDesk_01.SchoolDesk_01"),
			TEXT("/Game/Campus/Props/island_tree_01/island_tree_01/Materials/island_tree_01_leaves.island_tree_01_leaves"),
			TEXT("/Game/Campus/Props/island_tree_01/island_tree_01/Materials/island_tree_01_branches.island_tree_01_branches") })
		{
			UMaterialInstanceConstant* MI = LoadObject<UMaterialInstanceConstant>(nullptr, Path);
			if (!MI)
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("%s: not an MIC"), Path);
				continue;
			}
			UE_LOG(LogCampusBootstrap, Display, TEXT("%s parent %s blend %d twosided %d"), Path, *GetPathNameSafe(MI->Parent), (int32)MI->GetBlendMode(), MI->IsTwoSided() ? 1 : 0);
			for (const FTextureParameterValue& V : MI->TextureParameterValues)
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("  tex %s = %s"), *V.ParameterInfo.Name.ToString(), *GetPathNameSafe(V.ParameterValue));
			}
			for (const FScalarParameterValue& V : MI->ScalarParameterValues)
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("  scalar %s = %f"), *V.ParameterInfo.Name.ToString(), V.ParameterValue);
			}
			for (const FVectorParameterValue& V : MI->VectorParameterValues)
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("  vector %s = %s"), *V.ParameterInfo.Name.ToString(), *V.ParameterValue.ToString());
			}
			for (const FStaticSwitchParameter& V : MI->GetStaticParameters().StaticSwitchParameters)
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("  switch %s = %d"), *V.ParameterInfo.Name.ToString(), V.Value ? 1 : 0);
			}
		}
	}

	void InspectBody()
	{
		for (const TCHAR* Path : { TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple") })
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Path);
			if (!Mesh)
			{
				continue;
			}
			UE_LOG(LogCampusBootstrap, Display, TEXT("%s bounds %s materials %d nanite %d"), Path, *Mesh->GetBounds().GetBox().ToString(), Mesh->GetMaterials().Num(), Mesh->IsNaniteEnabled() ? 1 : 0);
			for (const FSkeletalMaterial& M : Mesh->GetMaterials())
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("  slot %s = %s"), *M.MaterialSlotName.ToString(), *GetPathNameSafe(M.MaterialInterface));
			}
			// Body cross-sections (reference pose, model faces +Y): fit limits for strapped-on gear.
			if (const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering())
			{
				const FPositionVertexBuffer& Positions = Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
				for (float Z = 4.f; Z < 182.f; Z += 6.f)
				{
					FBox Torso(ForceInit);
					FBox Left(ForceInit);
					for (uint32 i = 0; i < Positions.GetNumVertices(); ++i)
					{
						const FVector P(Positions.VertexPosition(i));
						if (FMath::Abs(P.Z - Z) > 3.f)
						{
							continue;
						}
						(FMath::Abs(P.X) < 17.f ? Torso : Left) += P;
					}
					UE_LOG(LogCampusBootstrap, Display, TEXT("  slice z=%.0f centre %s | outer %s"), Z, Torso.IsValid ? *Torso.ToString() : TEXT("-"), Left.IsValid ? *Left.ToString() : TEXT("-"));
				}
			}
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			for (const TCHAR* Bone : { TEXT("pelvis"), TEXT("spine_03"), TEXT("spine_05"), TEXT("neck_01"), TEXT("head"), TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("hand_r"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l") })
			{
				const int32 Index = Ref.FindBoneIndex(Bone);
				if (Index != INDEX_NONE)
				{
					UE_LOG(LogCampusBootstrap, Display, TEXT("  bone %s at %s"), Bone, *FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index).GetLocation().ToCompactString());
				}
			}
		}
	}

	void Inspect()
	{
		InspectBody();
		InspectPropMaterials();
		for (const TCHAR* Path : { TEXT("/Game/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle"), TEXT("/Game/Weapons/Pistol/Meshes/SKM_Pistol.SKM_Pistol") })
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Path);
			if (!Mesh)
			{
				continue;
			}
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			UE_LOG(LogCampusBootstrap, Display, TEXT("%s bounds origin %s extent %s"), Path, *Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString());
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			for (int32 i = 0; i < Ref.GetNum(); ++i)
			{
				const FTransform CS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, i);
				UE_LOG(LogCampusBootstrap, Display, TEXT("  bone %s at %s"), *Ref.GetBoneName(i).ToString(), *CS.GetLocation().ToString());
			}
			for (int32 i = 0; i < Mesh->NumSockets(); ++i)
			{
				const USkeletalMeshSocket* S = Mesh->GetSocketByIndex(i);
				const int32 Bone = Ref.FindBoneIndex(S->BoneName);
				const FTransform BoneCS = Bone != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Bone) : FTransform::Identity;
				const FTransform CS = S->GetSocketLocalTransform() * BoneCS;
				UE_LOG(LogCampusBootstrap, Display, TEXT("  socket %s (bone %s) at %s rot %s"), *S->SocketName.ToString(), *S->BoneName.ToString(), *CS.GetLocation().ToString(), *CS.Rotator().ToString());
			}
		}
	}
}

UCampusBootstrapCommandlet::UCampusBootstrapCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UCampusBootstrapCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("uiartonly")))
	{
		CampusUIAssets::BuildCombatTextures();
		return 0;
	}
	if (FParse::Param(*Params, TEXT("menuartonly")))
	{
		return CampusImport::ImportMenuTextures(true) ? 0 : 1;
	}
	const bool bInspect = Params.Contains(TEXT("-inspect"));
	if (bInspect)
	{
		Inspect();
		CampusWeaponKit::InspectPistol();
		if (Params.Contains(TEXT("-inspectonly")))
		{
			return 0;
		}
	}
	if (Params.Contains(TEXT("-uimaterialsonly")))
	{
		// The HUD / optic materials alone (reticles, eyepiece, sight picture, smoke and decals).
		CampusUIAssets::BuildMaterials();
		return 0;
	}
	if (Params.Contains(TEXT("-weatheronly")))
	{
		BuildEnvironmentAssets(true);
		CampusImport::BuildSoldierMaterial();
		return 0;
	}
	if (FParse::Param(*Params, TEXT("visualdetailsonly")))
	{
		CampusImport::BuildHotelInteriorMaterials();
		CampusImport::BuildSoldierMaterial();
		CampusWeaponKit::BuildVisualDetails();
		return 0;
	}
	if (Params.Contains(TEXT("-knifeonly")))
	{
		// The knife and the hedge sections, and the HUD / optic materials (they read the textures already built).
		CampusWeaponKit::BuildKnifeOnly();
		CampusUIAssets::BuildMaterials();
		return 0;
	}
	if (Params.Contains(TEXT("-weaponsonly")))
	{
		CampusWeaponKit::BuildWeaponKit();
		CampusImport::BuildSoldierMaterial();
		CampusUIAssets::BuildTextures();
		CampusUIAssets::BuildMaterials();
		return 0;
	}
	if (!Params.Contains(TEXT("-skipaudio")))
	{
		BuildAudio();
	}
	const bool bReimport = Params.Contains(TEXT("-reimport"));
	if (!Params.Contains(TEXT("-skipimport")))
	{
		if (!CampusImport::ImportMenuTextures(bReimport))
		{
			return 1;
		}
		CampusImport::ImportSurfaceTextures(bReimport);
	}
	if (!Params.Contains(TEXT("-skipfonts")))
	{
		CampusUIAssets::BuildFonts();
	}
	if (!Params.Contains(TEXT("-skipmaterials")))
	{
		BuildEnvironmentAssets();
		CampusImport::BuildWeatheringTextures();
		BuildMaterials();
		CampusUIAssets::BuildTextures();
		CampusUIAssets::BuildMaterials();
		CampusImport::BuildSurfaceMaterials();
	}
	if (!Params.Contains(TEXT("-skipterrain")))
	{
		CampusImport::BuildTerrainMesh();
	}
	if (!Params.Contains(TEXT("-skipimport")))
	{
		CampusImport::ImportProps(bReimport);
	}
	if (!Params.Contains(TEXT("-skipmaterials")))
	{
		CampusImport::BuildPropMaterials();
		CampusImport::FixPropMaterials();
		CampusImport::BuildSoldierMaterial();
	}
	if (!Params.Contains(TEXT("-skipweapons")))
	{
		CampusWeaponKit::BuildWeaponKit();
	}
	if (!Params.Contains(TEXT("-skipmaps")))
	{
		// -maps=Hotel,Menu rebuilds only those (Campus, Hotel, Menu); the catalog keeps the others' previews.
		FString Only;
		FParse::Value(*Params, TEXT("-maps="), Only);
		auto Wanted = [&Only](const TCHAR* Name) { return Only.IsEmpty() || Only.Contains(Name); };
		TArray<FCampusMapPreviewData> Previews;
		if (const UCampusMapCatalog* Old = UCampusMapCatalog::Get())
		{
			Previews = Old->Maps;
		}
		bool bOk = true;
		for (const ECampusMap Map : { ECampusMap::Campus, ECampusMap::Hotel })
		{
			if (!Wanted(Map == ECampusMap::Campus ? TEXT("Campus") : TEXT("Hotel")))
			{
				continue;
			}
			FCampusMapPreviewData Preview;
			bOk &= BuildGameMap(Map, Preview);
			Previews.RemoveAll([Map](const FCampusMapPreviewData& D) { return D.Map == Map; });
			Previews.Add(Preview);
		}
		if (Wanted(TEXT("Menu")))
		{
			bOk &= BuildMenuMap();
		}
		bOk &= SaveCatalog(Previews);
		if (!bOk)
		{
			return 1;
		}
	}
	UE_LOG(LogCampusBootstrap, Display, TEXT("Campus bootstrap finished."));
	return 0;
}
