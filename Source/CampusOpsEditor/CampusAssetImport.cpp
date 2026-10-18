#include "CampusAssetImport.h"
#include "CampusEditorCommon.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionPreSkinnedPosition.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Map/CampusPropLibrary.h"
#include "Map/CampusSurfaces.h"

using namespace CampusEditor;

namespace
{
	const TCHAR* TextureFolder = TEXT("/Game/Campus/Textures");
	const TCHAR* SurfaceFolder = TEXT("/Game/Campus/Materials/Surfaces");
	const TCHAR* PropFolder = TEXT("/Game/Campus/Props");

	FString SourceRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceArt/PolyHaven"));
	}

	template <typename T>
	T* LoadQuiet(const FString& ObjectPath)
	{
		return LoadObject<T>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UAssetImportTask* MakeTask(const FString& File, const FString& DestinationPath, const FString& DestinationName)
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = File;
		Task->DestinationPath = DestinationPath;
		Task->DestinationName = DestinationName;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bReplaceExistingSettings = true;
		Task->bSave = false;
		Task->bAsync = false;
		return Task;
	}

	enum class ETexKind : uint8 { Color, Normal, Masks };

	void ApplyTextureSettings(UTexture2D* Tex, ETexKind Kind)
	{
		Tex->PreEditChange(nullptr);
		switch (Kind)
		{
		case ETexKind::Color:
			Tex->SRGB = true;
			Tex->CompressionSettings = TC_Default;
			Tex->LODGroup = TEXTUREGROUP_World;
			break;
		case ETexKind::Normal:
			Tex->SRGB = false;
			Tex->CompressionSettings = TC_Normalmap;
			Tex->LODGroup = TEXTUREGROUP_WorldNormalMap;
			break;
		case ETexKind::Masks:
			Tex->SRGB = false;
			Tex->CompressionSettings = TC_Masks;
			Tex->LODGroup = TEXTUREGROUP_World;
			break;
		}
		Tex->VirtualTextureStreaming = false;
		// Sampled with world-space UVs on huge instanced boxes, so the streamer's UV-density estimate would pick tiny mips.
		Tex->NeverStream = true;
		Tex->AddressX = TA_Wrap;
		Tex->AddressY = TA_Wrap;
		Tex->PostEditChange();
	}

	/** Average linear luminance of a colour texture's source pixels. */
	float AverageLuminance(UTexture2D* Tex)
	{
		FImage Image;
		if (!Tex || !Tex->Source.IsValid() || !Tex->Source.GetMipImage(Image, 0))
		{
			return 0.25f;
		}
		FImage BGRA;
		Image.CopyTo(BGRA, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const TArrayView64<FColor> Pixels = BGRA.AsBGRA8();
		double Sum = 0.0;
		int64 Count = 0;
		for (int64 i = 0; i < Pixels.Num(); i += 37)
		{
			const FLinearColor L = FLinearColor::FromSRGBColor(Pixels[i]);
			Sum += 0.299 * L.R + 0.587 * L.G + 0.114 * L.B;
			++Count;
		}
		return Count > 0 ? (float)(Sum / Count) : 0.25f;
	}

	UTexture2D* CreateTexture(const FString& Name, int32 W, int32 H, const TArray<FColor>& Pixels, bool bSRGB, TextureCompressionSettings Compression, TextureGroup Group)
	{
		const FString PackageName = FString(TextureFolder) / Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UTexture2D* Tex = FindObject<UTexture2D>(Package, *Name);
		if (!Tex)
		{
			Tex = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Tex);
		}
		Tex->PreEditChange(nullptr);
		Tex->Source.Init(W, H, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Tex->SRGB = bSRGB;
		Tex->CompressionSettings = Compression;
		Tex->LODGroup = Group;
		Tex->NeverStream = true;
		Tex->AddressX = TA_Wrap;
		Tex->AddressY = TA_Wrap;
		Tex->MipGenSettings = TMGS_FromTextureGroup;
		Tex->PostEditChange();
		SaveAsset(Tex);
		return Tex;
	}

	/** Tileable value noise / cellular noise on the unit square. */
	struct FTileNoise
	{
		uint32 Seed = 1;

		float Lattice(int32 X, int32 Y, int32 Period) const
		{
			X = ((X % Period) + Period) % Period;
			Y = ((Y % Period) + Period) % Period;
			uint32 H = (uint32)X * 374761393u + (uint32)Y * 668265263u + Seed * 2246822519u;
			H = (H ^ (H >> 13)) * 1274126177u;
			H ^= H >> 16;
			return (H & 0xFFFFFFu) / 16777215.f;
		}
		float Value(float U, float V, int32 Period) const
		{
			const float X = U * Period;
			const float Y = V * Period;
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			auto Fade = [](float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); };
			const float Sx = Fade(X - X0);
			const float Sy = Fade(Y - Y0);
			const float A = FMath::Lerp(Lattice(X0, Y0, Period), Lattice(X0 + 1, Y0, Period), Sx);
			const float B = FMath::Lerp(Lattice(X0, Y0 + 1, Period), Lattice(X0 + 1, Y0 + 1, Period), Sx);
			return FMath::Lerp(A, B, Sy);
		}
		float FBM(float U, float V, int32 Period, int32 Octaves, float Gain = 0.5f) const
		{
			float Sum = 0.f;
			float Amp = 1.f;
			float Norm = 0.f;
			for (int32 o = 0; o < Octaves; ++o)
			{
				Sum += Amp * Value(U, V, Period << o);
				Norm += Amp;
				Amp *= Gain;
			}
			return Sum / Norm;
		}
		/** Distance (in cell units) to the nearest jittered feature point. */
		float Cells(float U, float V, int32 Period) const
		{
			const float X = U * Period;
			const float Y = V * Period;
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			float Best = 10.f;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					const int32 Cx = X0 + Dx;
					const int32 Cy = Y0 + Dy;
					const float Px = Cx + Lattice(Cx, Cy, Period);
					const float Py = Cy + Lattice(Cx + 17, Cy + 31, Period);
					Best = FMath::Min(Best, FMath::Sqrt(FMath::Square(X - Px) + FMath::Square(Y - Py)));
				}
			}
			return Best;
		}
	};

	uint8 ToByte(float V)
	{
		return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255);
	}

	float Stretch(float V, float Contrast)
	{
		return FMath::Clamp((V - 0.5f) * Contrast + 0.5f, 0.f, 1.f);
	}

	// HLSL for M_Campus_Surface: world-space planar projection of a photoscanned set, palette tint,
	// weathering (wall-base grime, leak streaks below slabs, corner dirt from the distance field, blotches)
	// and wet patches on outdoor floors. Outputs a world-space normal.
	const TCHAR* SurfaceHLSL = TEXT(R"(
float3 cN = normalize(VN);
float3 cA = abs(cN);
float3 cT;
float3 cB;
if (cA.z >= cA.x && cA.z >= cA.y)
{
	cT = float3(1.0, 0.0, 0.0);
	cB = float3(0.0, cN.z > 0.0 ? -1.0 : 1.0, 0.0);
}
else if (cA.x >= cA.y)
{
	cT = float3(0.0, cN.x > 0.0 ? -1.0 : 1.0, 0.0);
	cB = float3(0.0, 0.0, -1.0);
}
else
{
	cT = float3(cN.y > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
	cB = float3(0.0, 0.0, -1.0);
}
float2 cW = float2(dot(WP, cT), dot(WP, cB));
float cScale = ITile > 0.5 ? ITile : TexScale;
float2 cUV = cW / cScale;

// Anti-tiling: a second layer, rotated ~37 deg, rescaled and offset, is blended in by a large-scale noise mask, so
// the photo set no longer repeats on a visible grid. Its normals are rotated back into the first layer's frame.
const float cCs = 0.7986;
const float cSn = 0.6018;
float2 cUV2 = float2(cUV.x * cCs - cUV.y * cSn, cUV.x * cSn + cUV.y * cCs) * 0.61 + float2(0.37, 0.71);
float4 cGL = Texture2DSample(GrungeTex, GrungeTexSampler, cW / 1900.0 + 0.13);
float cMix = smoothstep(0.38, 0.62, cGL.r * 0.65 + cGL.b * 0.35) * AntiTile;
float3 cBase = lerp(Texture2DSample(BaseTex, BaseTexSampler, cUV).rgb, Texture2DSample(BaseTex, BaseTexSampler, cUV2).rgb, cMix);
float3 cArm = lerp(Texture2DSample(ArmTex, ArmTexSampler, cUV).rgb, Texture2DSample(ArmTex, ArmTexSampler, cUV2).rgb, cMix);
float2 cN1 = Texture2DSample(NormalTex, NormalTexSampler, cUV).rg * 2.0 - 1.0;
float2 cN2 = Texture2DSample(NormalTex, NormalTexSampler, cUV2).rg * 2.0 - 1.0;
cN2 = float2(cN2.x * cCs + cN2.y * cSn, -cN2.x * cSn + cN2.y * cCs);
float2 cNxy = lerp(cN1, cN2, cMix) * NormalStrength;
float3 cNts = float3(cNxy, sqrt(saturate(1.0 - dot(cNxy, cNxy))));

float cLum = dot(cBase, float3(0.299, 0.587, 0.114));
float cDetail = clamp(lerp(1.0, cLum / max(AvgLum, 0.02), DetailContrast), 0.0, 2.5);
float3 cAlbedo = lerp(cBase, Tint * cDetail, TintStrength);

// Grime from two samples of the grunge set at unrelated scales and turned ~34 deg apart: alone each repeats on a
// visible grid (every 8.5 m, every 1.4 m), together the pattern never lines up. Re-stretched after the blend.
float2 cWr = float2(cW.x * 0.8290 - cW.y * 0.5592, cW.x * 0.5592 + cW.y * 0.8290);
float4 cGM = lerp(Texture2DSample(GrungeTex, GrungeTexSampler, cW / 850.0), Texture2DSample(GrungeTex, GrungeTexSampler, cWr / 1370.0 + 0.29), 0.45);
float4 cGD = lerp(Texture2DSample(GrungeTex, GrungeTexSampler, cW / 140.0), Texture2DSample(GrungeTex, GrungeTexSampler, cWr / 233.0 + 0.61), 0.4);
cGM = saturate((cGM - 0.5) * 1.4 + 0.5);
cAlbedo *= 1.0 + (cGM.r - 0.5) * 0.45 * Macro;
// Broad tone drift over tens of metres (sun-bleached patches, repaints).
cAlbedo *= 1.0 + (Texture2DSample(GrungeTex, GrungeTexSampler, cW / 2600.0 + 0.41).g - 0.5) * 0.28 * Macro;

float cZ = fmod(WP.z - 15.0 + 36000.0, 360.0);
// Storey index: each floor gets its own run of grime and leaks instead of a copy of the one below.
float cFloorIx = floor((WP.z - 15.0 + 36000.0) / 360.0);
float cVert = saturate(1.0 - cA.z * 1.5);
// Splash-back at the foot of a wall: its height and strength wander along the wall (a downpipe, a sheltered stretch).
float cBandN = Texture2DSample(GrungeTex, GrungeTexSampler, float2(cW.x / 1100.0 + cFloorIx * 0.173, 0.31 + cFloorIx * 0.29)).r;
float cGround = pow(saturate(1.0 - cZ / (60.0 * lerp(0.4, 1.7, cBandN))), 1.5) * cVert * saturate(0.35 + cGD.r * 1.1) * lerp(0.45, 1.25, cBandN);
// Leak streaks under the slab edge: unevenly spaced (the column coordinate is warped), of uneven length and
// strength, and shifted on every storey.
float cWarp = (Texture2DSample(GrungeTex, GrungeTexSampler, float2(cW.x / 700.0 + cFloorIx * 0.41, 0.77)).g - 0.5) * 0.9;
float cLenN = Texture2DSample(GrungeTex, GrungeTexSampler, float2(cW.x / 520.0 + 0.13 + cFloorIx * 0.23, 0.43)).b;
float cSV = (360.0 - cZ) / 360.0 / lerp(0.5, 1.2, cLenN);
// Softened toward the bottom of each run: rainwater fans out and fades down a wall, it is not a row of dark drips.
float cStreak = Texture2DSample(StreakTex, StreakTexSampler, float2(cW.x / 240.0 * 0.85 + cWarp + cFloorIx * 0.371, saturate(cSV))).r
	* saturate((1.0 - cSV) * 4.0) * (1.0 - 0.5 * saturate(cSV)) * cVert * lerp(0.2, 1.1, cLenN);
// Corner grime from the global distance field, sampled 15 cm off the surface: only something well inside that (another
// face of a corner) counts. The field coarsens with distance from the eye, and a centimetre short on a plain wall turned
// the whole wall into corner dirt, a rash of dark spots across far walls; past ~10 m it is not trusted at all.
float cCornerTrust = saturate(1.0 - (length(WP - Cam) - 700.0) / 500.0);
float cCorner = saturate((10.0 - CornerD) / 7.0) * saturate(0.4 + cGD.g) * cCornerTrust;
float cBlotch = saturate((cGM.g - 0.52) * 2.5);
float cDirt = saturate(cGround * GroundDirt + cStreak * Streaks + cCorner * GroundDirt * 0.7 + cBlotch * 0.22 * Macro) * IDirt * Weathering;
cAlbedo = lerp(cAlbedo, cAlbedo * float3(0.34, 0.32, 0.29), cDirt);

float cRough = lerp(cArm.g * RoughScale, IRough, RoughFromInstance);
cRough = saturate(cRough + (cGD.b - 0.5) * 0.18 + cDirt * 0.2);
float cMetal = lerp(cArm.b, IMetal, MetalFromInstance) * (1.0 - cDirt * 0.6);

float cHoriz = saturate((cN.z - 0.7) / 0.25);
// Rain only wets what the sky reaches: a top-down depth capture of the map gives the highest surface over each point.
float cSky = 1.0;
if (MaskSize > 1.0)
{
	float2 mUV = float2((WP.y - MaskY) / MaskSize + 0.5, 0.5 - (WP.x - MaskX) / MaskSize);
	if (all(mUV > 0.0) && all(mUV < 1.0))
	{
		float mTop = MaskTop - Texture2DSampleLevel(MaskTex, MaskTexSampler, mUV, 0).r;
		cSky = saturate((WP.z - (mTop - 40.0)) / 25.0);
	}
}
float cRainWet = GWet * cSky;
float cWetAmt = max(Wetness, cRainWet * RainPuddles);
float cWet = saturate((cGM.a - (1.0 - cWetAmt * 0.55)) * 5.0) * cHoriz * saturate(cWetAmt * 4.0);
// Everything the rain reaches goes darker and glossier, puddles or not; walls streak less than floors.
cAlbedo *= lerp(1.0, 0.7, cRainWet * (0.45 + 0.55 * cHoriz));
cRough = lerp(cRough, cRough * 0.5, cRainWet);
cAlbedo *= lerp(1.0, 0.5, cWet);
cRough = lerp(cRough, 0.05, cWet);
cNts = normalize(lerp(cNts, float3(0.0, 0.0, 1.0), cWet * 0.85));

float3 cTn = normalize(cT - cN * dot(cN, cT));
float3 cBn = normalize(cB - cN * dot(cN, cB));
NormalWS = normalize(cTn * cNts.x + cBn * cNts.y + cN * cNts.z);

// Boxes: rounded edges (normal bent toward the neighbouring face) and edge wear, from the per-instance half extents.
if (HE.x > 0.5 && BevelRadius > 0.0)
{
	float3 eX = normalize(AX);
	float3 eY = normalize(AY);
	float3 eZ = normalize(AZ);
	float3 u = clamp(LP / 50.0, -1.0, 1.0);
	float3 dist = (1.0 - abs(u)) * HE;
	float3 ln = abs(float3(dot(cN, eX), dot(cN, eY), dot(cN, eZ)));
	float3 face = (ln.x >= ln.y && ln.x >= ln.z) ? float3(1.0, 0.0, 0.0) : ((ln.y >= ln.z) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0));
	float rad = clamp(min(min(HE.x, HE.y), HE.z) * 0.4, 0.15, BevelRadius);
	float3 k = saturate(1.0 - dist / rad) * (1.0 - face);
	float3 k2 = k * k;
	NormalWS = normalize(NormalWS + (eX * sign(u.x) * k2.x + eY * sign(u.y) * k2.y + eZ * sign(u.z) * k2.z) * 1.3);
	float edge = max(k.x, max(k.y, k.z));
	float chip = saturate((cGD.g - 0.3) * 2.5);
	float wear = saturate(edge * 1.5 - 0.2) * EdgeWear * (0.3 + 0.7 * chip) * Weathering;
	cAlbedo = lerp(cAlbedo, cAlbedo * 1.3 + 0.02, wear);
	cRough = saturate(cRough + wear * EdgeRough);
	// Grime settles in the band just inside the edge.
	float band = saturate(1.0 - abs(edge - 0.15) * 5.0) * (1.0 - edge) * 0.12 * Weathering;
	cAlbedo *= 1.0 - band;
}
Rough = cRough;
Metal = cMetal;
AO = lerp(cArm.r, 1.0, 0.25);
// Emissive in units of 400 nits: light panels ~2400 nits, bright indoors and dim against daylight.
Emis = Tint * IEmis * 400.0;
return cAlbedo;
)");

	UMaterialInstanceConstant* NewInstance(const FString& Name, UMaterialInterface* Parent)
	{
		const FString PackageName = FString(SurfaceFolder) / Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UMaterialInstanceConstant* MI = FindObject<UMaterialInstanceConstant>(Package, *Name);
		if (!MI)
		{
			MI = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(MI);
		}
		UMaterialEditingLibrary::SetMaterialInstanceParent(MI, Parent);
		return MI;
	}

	/** Instance-space inputs of the box materials: local position (-50..50), the instance's axes in world space, half extents. */
	void LinkBoxInputs(FMat& B, UMaterialExpressionCustom* C)
	{
		UMaterialExpressionLocalPosition* LP = B.Make<UMaterialExpressionLocalPosition>();
		LP->LocalOrigin = ELocalPositionOrigin::Instance;
		B.Link(LP, C, TEXT("LP"));
		const TCHAR* AxisNames[3] = { TEXT("AX"), TEXT("AY"), TEXT("AZ") };
		for (int32 i = 0; i < 3; ++i)
		{
			UMaterialExpressionConstant3Vector* V = B.Make<UMaterialExpressionConstant3Vector>();
			V->Constant = FLinearColor(i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f);
			UMaterialExpressionTransform* T = B.Make<UMaterialExpressionTransform>();
			T->TransformSourceType = TRANSFORMSOURCE_Instance;
			T->TransformType = TRANSFORM_World;
			B.Link(V, T);
			B.Link(T, C, AxisNames[i]);
		}
		B.Link(B.CustomData(8, 0.f, true), C, TEXT("HE"));
	}

	// Distant city blocks: a whole facade from one box. Bays and floors are laid out on the box's faces; each window
	// opens onto a room (interior mapping: the eye ray is traced into a box room behind the glass, so the back wall,
	// side walls, floor and ceiling shift with parallax), with curtains, aluminium frames, iron window cages (鐵窗),
	// air conditioners with rust streaks, balconies, office ribbon glazing and shop fronts with signs on the ground
	// floor. Every pattern is filtered by its screen footprint, so it fades to its average instead of shimmering.
	const TCHAR* FacadeHLSL = TEXT(R"(
struct FFac
{
	float Hash(float3 p) { return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453); }
	float Rect(float2 p, float2 lo, float2 hi, float2 fw)
	{
		float2 a = smoothstep(lo - fw, lo + fw, p) * (1.0 - smoothstep(hi - fw, hi + fw, p));
		return a.x * a.y;
	}
	float Bars(float x, float period, float w, float fw)
	{
		float f = abs(frac(x / period + 0.5) - 0.5) * period;
		float sharp = 1.0 - smoothstep(w * 0.5 - fw * 0.5, w * 0.5 + fw * 0.5, f);
		return lerp(sharp, w / period, saturate(fw / period * 1.5 - 0.25));
	}
	float3 Room(float2 rp, float hw, float hgt, float depth, float3 d, float rr, float rr2)
	{
		float tx = ((d.x > 0.0 ? hw : -hw) - rp.x) / (abs(d.x) > 1e-4 ? d.x : 1e-4);
		float ty = ((d.y > 0.0 ? hgt : 0.0) - rp.y) / (abs(d.y) > 1e-4 ? d.y : 1e-4);
		float tz = depth / max(d.z, 1e-3);
		float t = min(min(tx, ty), tz);
		float3 p = float3(rp, 0.0) + d * t;
		float3 wallC = lerp(float3(0.56, 0.53, 0.47), float3(0.44, 0.47, 0.49), rr);
		float3 c;
		if (t >= tz - 1e-3)
		{
			c = wallC * (p.y < 90.0 ? lerp(0.25, 0.6, rr2) : 1.0);
			c *= 1.0 - 0.5 * step(abs(p.x - (rr - 0.5) * hw), hw * 0.18) * step(p.y, 190.0) * step(0.5, rr2);
		}
		else if (t >= ty - 1e-3)
		{
			c = d.y > 0.0 ? float3(0.72, 0.71, 0.68) : lerp(float3(0.28, 0.20, 0.13), float3(0.42, 0.41, 0.39), rr2);
		}
		else
		{
			c = wallC * 0.78;
		}
		return c * lerp(0.6, 0.2, saturate(p.z / depth));
	}
};
FFac F;

float3 n = normalize(VN);
float3 eX = normalize(AX);
float3 eY = normalize(AY);
float3 eZ = normalize(AZ);
float3 he = max(HE, 1.0);
float3 u = clamp(LP / 50.0, -1.0, 1.0);
float3 off = u.x * he.x * eX + u.y * he.y * eY + u.z * he.z * eZ;
float4 gM = Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy + WP.z * 0.7) / 2600.0);
float4 gF = Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy * 0.7 + WP.z) / 640.0);
Metal = 0.0;
Spec = 0.5;
AO = 1.0;
NormalWS = n;
Emis = 0.0;
if (abs(n.z) > 0.7)
{
	Rough = 0.92;
	return float3(0.27, 0.26, 0.245) * (0.7 + 0.6 * gM.r) * (1.0 - 0.35 * gF.g);
}

float3 T = normalize(cross(float3(0.0, 0.0, 1.0), n));
float3 lnA = abs(float3(dot(n, eX), dot(n, eY), dot(n, eZ)));
float W = lnA.x > lnA.y ? he.y : he.x;
float h = dot(off, T) + W;
// Heights from the building's own foot, so blocks stand anywhere (the hotel's street is 180 m below its roof).
float z = u.z * he.z + he.z;
float topZ = 2.0 * he.z;
float face = floor(dot(n.xy, float2(1.7, 3.1)) * 10.0);
float style = floor(Seed * 2.999);
float FH = 310.0;
float BW = style > 1.5 ? 165.0 : lerp(330.0, 440.0, frac(Seed * 17.31));
float nb = max(1.0, round(2.0 * W / BW));
float bw = 2.0 * W / nb;
float bayI = floor(h / bw);
float bx = h - (bayI + 0.5) * bw;
float flI = floor(z / FH);
float fz = z - flI * FH;
float2 fw = max(float2(fwidth(h), fwidth(z)), 0.02);
float2 q = float2(bx, fz);
float r0 = F.Hash(float3(bayI, flI, face + Seed * 97.0));
float r1 = F.Hash(float3(bayI + 31.0, flI * 1.7, face + Seed * 53.0));
float r2 = F.Hash(float3(bayI * 0.37, flI + 11.0, face + Seed * 211.0));
float rb = frac(Seed * 41.7);

float3 V = normalize(WP - Cam);
float3 d = float3(dot(V, T), V.z, -dot(V, n));

float3 wall = Tint * (0.84 + 0.32 * gM.r) * (1.0 - 0.16 * gF.b);
wall *= 1.0 - 0.05 * F.Bars(z, 6.5, 0.8, fw.y);
float3 albedo = wall;
float rough = 0.86;
float ao = 1.0;
float3 bend = 0.0;

float colW = style > 1.5 ? 6.0 : 30.0;
float hw = bw * 0.5 - colW;
float onCol = smoothstep(hw - fw.x, hw + fw.x, abs(bx));
float slab = 1.0 - smoothstep(20.0 - fw.y, 20.0 + fw.y, fz);
float parapet = smoothstep(topZ - 100.0 - fw.y, topZ - 100.0 + fw.y, z);
if (style < 1.5)
{
	albedo = lerp(albedo, albedo * 1.07 + 0.012, onCol * 0.7);
	bend += T * sign(bx) * 0.55 * (1.0 - smoothstep(0.0, 6.0 + fw.x, abs(abs(bx) - hw)));
}
albedo = lerp(albedo, float3(0.34, 0.33, 0.31) * (0.85 + 0.3 * gM.g), slab * 0.8);
bend += float3(0.0, 0.0, 0.5) * (1.0 - smoothstep(0.0, 5.0 + fw.y, abs(fz - 20.0)));
ao *= 1.0 - 0.22 * (1.0 - smoothstep(20.0, 45.0, fz)) * (1.0 - slab);

float sill = style > 1.5 ? 75.0 : 90.0;
float head = style > 1.5 ? 285.0 : 238.0;
float ww = style > 1.5 ? hw * 2.0 - 4.0 : hw * 2.0 * lerp(0.55, 0.82, frac(Seed * 7.3));
float2 lo = float2(-ww * 0.5, sill);
float2 hi = float2(ww * 0.5, head);
if (style > 0.5 && style < 1.5)
{
	lo = float2(-hw + 10.0, 22.0);
	hi = float2(hw - 10.0, 250.0);
}
float ground = 1.0 - step(1.0, flI);
if (ground > 0.5)
{
	lo = float2(-hw, 0.0);
	hi = float2(hw, 255.0);
}
float open = F.Rect(q, lo, hi, fw) * (1.0 - parapet);
)" R"(
// the room behind the glass
float3 room = F.Room(float2(bx, fz - 20.0), hw, FH - 40.0, ground > 0.5 ? 900.0 : 420.0, d, r0, r1) * 0.32;
if (style > 1.5)
{
	room *= float3(0.62, 0.74, 0.74);
	room = lerp(room, room * 0.55 + 0.03, F.Bars(fz, 7.0, 3.0, fw.y) * step(0.45, r1));
}
else
{
	float cx = lerp(lo.x, hi.x, r2);
	float curtain = step(0.42, r1) * (r0 > 0.5 ? step(bx, cx) : step(cx, bx));
	float3 cloth = lerp(float3(0.62, 0.58, 0.50), lerp(float3(0.42, 0.48, 0.55), float3(0.58, 0.44, 0.42), r2), step(0.6, r0));
	room = lerp(room, cloth * (0.28 + 0.12 * F.Bars(bx, 9.0, 4.5, fw.x)), curtain);
}
if (ground > 0.5)
{
	float shutter = step(0.55, r2);
	float3 sh = float3(0.40, 0.40, 0.39) * (0.85 + 0.15 * F.Bars(fz, 8.0, 4.0, fw.y)) * (0.8 + 0.4 * gF.r);
	room = lerp(room * 0.8, sh, shutter);
}
// reveal shadows: the lintel and jambs shade the top and sides of the opening
float reveal = (1.0 - smoothstep(hi.y - 16.0 - fw.y, hi.y - fw.y, fz)) * 0.45 + 0.55;
reveal *= 0.8 + 0.2 * smoothstep(lo.x, lo.x + 10.0 + fw.x, bx) * (1.0 - smoothstep(hi.x - 10.0 - fw.x, hi.x, bx));
albedo = lerp(albedo, room * reveal, open);
rough = lerp(rough, 0.05, open * (1.0 - step(0.55, r2) * ground));
// After dark the rooms light up, home by home: at sunset only a scattering, most of them by full night. Ceiling lights
// here are mostly daylight-white tubes and LED panels, with warm lamps in a third of the homes; shops are all lit.
// A lit room seen from outside is a few tens of nits, dimmer behind curtains, and much less than the sky at dusk.
float litChance = ground > 0.5 ? 0.85 : (0.03 + 0.45 * Night * Night);
float lit = step(F.Hash(float3(bayI * 1.37, flI * 0.71, face + Seed * 13.0)), litChance) * open * (1.0 - parapet) * step(0.001, Night);
// Windows down to a pixel or two stay points of light with dark wall between (as the eye sees a distant district),
// a little dimmer and more even so they do not sparkle as the view moves.
float tiny = saturate(max(fw.x / max(ww, 1.0), fw.y / max(hi.y - lo.y, 1.0)) * 2.5 - 0.35);
float3 lamp = r1 > 0.66 ? float3(1.0, 0.76, 0.50) : (r1 > 0.2 ? float3(0.86, 0.93, 1.0) : float3(1.0, 0.94, 0.84));
float curtainDim = (style < 1.5 && r1 > 0.42) ? 0.4 : 1.0;
Emis = lamp * lit * curtainDim * (9.0 + 16.0 * lerp(r0, 0.5, tiny)) * lerp(1.0, 0.6, tiny) * Night * (ground > 0.5 ? 1.5 : 1.0);

// aluminium frames and mullions
float3 alu = lerp(float3(0.55, 0.56, 0.57), float3(0.14, 0.12, 0.10), step(0.75, rb));
float inner = F.Rect(q, lo + 5.0, hi - 5.0, fw);
float frame = saturate(open - inner + F.Bars(bx, max(hi.x - lo.x, 1.0) * 0.5, 4.0, fw.x) * inner * step(style, 1.5) * (1.0 - ground));
albedo = lerp(albedo, alu, frame * 0.9);
rough = lerp(rough, 0.4, frame);

// iron window cage (鐵窗) standing proud of the punched windows, or enclosing a balcony
float cageOn = (style < 0.5 ? step(0.3, r0) : (style < 1.5 ? step(0.55, r0) : 0.0)) * (1.0 - ground) * (1.0 - parapet);
float cage = F.Rect(q, lo - float2(10.0, 8.0), hi + float2(10.0, 8.0), fw) * cageOn;
float bars = saturate(F.Bars(bx, 12.0, 1.6, fw.x) + F.Bars(fz - lo.y + 8.0, (hi.y - lo.y + 16.0) / 3.0, 3.0, fw.y));
float3 iron = lerp(float3(0.10, 0.10, 0.105), float3(0.48, 0.48, 0.46), step(0.7, r2)) * (0.85 + 0.3 * gF.g);
albedo = lerp(albedo * (1.0 - 0.18 * cage), iron, cage * bars);
rough = lerp(rough, 0.55, cage * bars);
ao *= 1.0 - 0.15 * cage * (1.0 - open);

// balcony parapet and railing in front of the opening
if (style > 0.5 && style < 1.5 && ground < 0.5)
{
	float par = F.Rect(q, float2(-hw, 20.0), float2(hw, 62.0), fw) * (1.0 - parapet);
	float rail = F.Rect(q, float2(-hw, 62.0), float2(hw, 118.0), fw) * F.Bars(bx, 11.0, 1.5, fw.x) * (1.0 - parapet);
	albedo = lerp(albedo, wall * 1.08 + 0.01, par);
	albedo = lerp(albedo, float3(0.12, 0.12, 0.12), rail);
	ao *= 1.0 - 0.3 * F.Rect(q, float2(-hw, 228.0), float2(hw, 250.0), fw);
	bend += float3(0.0, 0.0, 0.45) * (1.0 - smoothstep(0.0, 4.0 + fw.y, abs(fz - 62.0))) * (1.0 - onCol);
}

)" R"(
// split air conditioners (hung under a window or on the balcony), with the rust stain they leave below
if (style < 1.5 && ground < 0.5 && r1 > 0.45)
{
	float ax = (r2 - 0.5) * max(ww - 80.0, 0.0);
	float2 alo = style < 0.5 ? float2(ax - 40.0, sill - 66.0) : float2(hw - 92.0, 24.0);
	float2 ahi = alo + float2(80.0, 56.0);
	float ac = F.Rect(q, alo, ahi, fw) * (1.0 - parapet);
	float3 acC = float3(0.62, 0.62, 0.60) * (0.9 - 0.25 * F.Bars(fz, 4.5, 1.4, fw.y) * F.Rect(q, alo + float2(6.0, 8.0), ahi - float2(30.0, 8.0), fw));
	albedo = lerp(albedo, acC, ac);
	rough = lerp(rough, 0.5, ac);
	bend += float3(0.0, 0.0, 0.6) * (1.0 - smoothstep(0.0, 3.0 + fw.y, abs(fz - ahi.y))) * F.Rect(q, alo, float2(ahi.x, ahi.y + 4.0), fw);
	float below = F.Rect(q, float2(alo.x, alo.y - 140.0), float2(ahi.x, alo.y), fw);
	float stain = Texture2DSample(StreakTex, StreakTexSampler, float2(h / 160.0, saturate((alo.y - fz) / 160.0))).r;
	albedo *= 1.0 - below * stain * 0.45 * float3(0.8, 1.0, 1.15);
	ao *= 1.0 - 0.3 * F.Rect(q, float2(alo.x, alo.y - 12.0), float2(ahi.x, alo.y), fw);
}

// shop fronts: a sign over most of them
if (ground > 0.5)
{
	float sign = F.Rect(q, float2(-hw, 258.0), float2(hw, 300.0), fw) * step(0.3, r0);
	float3 sc = r1 < 0.25 ? float3(0.05, 0.40, 0.18) : (r1 < 0.5 ? float3(0.75, 0.32, 0.05) : (r1 < 0.75 ? float3(0.62, 0.06, 0.05) : float3(0.68, 0.68, 0.66)));
	albedo = lerp(albedo, sc * (0.85 + 0.3 * F.Bars(bx, 60.0, 30.0, fw.x)), sign);
	rough = lerp(rough, 0.45, sign);
	Emis += sc * sign * Night * 30.0;
}

// weathering: streaks from the sills, grime at the street, a lighter coping on the parapet
float streak = Texture2DSample(StreakTex, StreakTexSampler, float2(h / 230.0, saturate((sill - fz) / 300.0))).r;
albedo *= 1.0 - 0.3 * streak * step(fz, sill) * (1.0 - open) * F.Rect(q, float2(lo.x - 10.0, -1.0), float2(hi.x + 10.0, sill), fw);
albedo *= lerp(0.65, 1.0, smoothstep(0.0, 120.0, z));
albedo = lerp(albedo, albedo * 1.12 + 0.02, smoothstep(topZ - 12.0 - fw.y, topZ - 12.0 + fw.y, z));
albedo *= 1.0 - 0.25 * saturate((gM.g - 0.55) * 3.0);
// After dark a distant block is seen by the little moon and sky light on it, while the eye opens up for the lit
// windows: the walls go dark round them instead of reading as day-lit cream.
albedo *= lerp(1.0, 0.28, saturate(Night * 1.4 - 0.4));

Rough = rough;
AO = ao;
NormalWS = normalize(n + bend);
return albedo;
)");

	// Bar grille / welded mesh as one masked panel (thin along local X): vertical bars every Pitch cm (Width cm thick),
	// horizontal wires every WirePitch cm (0: two rails), a frame round the edge. Bar coverage is filtered by its screen
	// footprint and dithered over time, so TSR resolves sub-pixel bars into a steady screen instead of moiré.
	const TCHAR* GrilleHLSL = TEXT(R"(
float3 n = normalize(VN);
float3 eX = normalize(AX);
float3 he = max(HE, 0.05);
float3 u = clamp(LP / 50.0, -1.0, 1.0);
float a = u.y * he.y;
float b = u.z * he.z;
float2 fw = max(float2(fwidth(a), fwidth(b)), 0.001);
float pitch = max(Pitch, 1.0);
float w = max(Width, 0.2);
float fa = abs(frac(a / pitch + 0.5) - 0.5) * pitch;
float vb = lerp(1.0 - smoothstep(w * 0.5 - fw.x * 0.5, w * 0.5 + fw.x * 0.5, fa), w / pitch, saturate(fw.x / pitch * 1.5 - 0.25));
float hb;
if (WirePitch > 0.5)
{
	float fb = abs(frac(b / WirePitch + 0.5) - 0.5) * WirePitch;
	hb = lerp(1.0 - smoothstep(w * 0.5 - fw.y * 0.5, w * 0.5 + fw.y * 0.5, fb), w / WirePitch, saturate(fw.y / WirePitch * 1.5 - 0.25));
}
else
{
	float rails = min(abs(abs(b) - he.z * 0.34), abs(b));
	hb = 1.0 - smoothstep(1.4 - fw.y, 1.4 + fw.y, rails);
}
float edge = min(he.y - abs(a), he.z - abs(b));
float frame = 1.0 - smoothstep(2.5 - max(fw.x, fw.y), 2.5 + max(fw.x, fw.y), edge);
float cov = saturate(max(max(vb, hb), frame));
if (abs(dot(n, eX)) < 0.7) cov = 1.0;
float2 sp = Parameters.SvPosition.xy + float(View.StateFrameIndexMod8) * float2(32.665, 11.815);
float dith = frac(52.9829189 * frac(dot(sp, float2(0.06711056, 0.00583715))));
Mask = cov > dith * 0.98 + 0.01 ? 1.0 : 0.0;
float4 g = Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy + WP.z) / 300.0);
float rust = saturate((g.g - 0.45) * 2.5);
Rough = saturate(IRough + rust * 0.3);
return lerp(Tint, Tint * float3(1.6, 0.9, 0.55) + float3(0.04, 0.015, 0.0), rust * 0.6);
)");

	UMaterial* BuildFacadeMaterial(UTexture2D* Grunge, UTexture2D* Streaks)
	{
		FMat B{ NewMaterial(TEXT("M_Campus_Facade")) };
		B.M->bTangentSpaceNormal = false;
		UMaterialExpressionCustom* C = B.Custom(FacadeHLSL, CMOT_Float3,
			{ TEXT("WP"), TEXT("VN"), TEXT("Cam"), TEXT("LP"), TEXT("AX"), TEXT("AY"), TEXT("AZ"), TEXT("HE"), TEXT("Tint"), TEXT("Seed"), TEXT("GrungeTex"), TEXT("StreakTex"), TEXT("Night") },
			{ { TEXT("Rough"), CMOT_Float1 }, { TEXT("Metal"), CMOT_Float1 }, { TEXT("Spec"), CMOT_Float1 }, { TEXT("NormalWS"), CMOT_Float3 }, { TEXT("AO"), CMOT_Float1 }, { TEXT("Emis"), CMOT_Float3 } });
		B.Link(B.Collection(CampusEditor::EnvironmentCollection(), TEXT("NightLights")), C, TEXT("Night"));
		B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("VN"));
		B.Link(B.Make<UMaterialExpressionCameraPositionWS>(), C, TEXT("Cam"));
		LinkBoxInputs(B, C);
		B.Link(B.CustomData(0, 0.5f, true), C, TEXT("Tint"));
		B.Link(B.InstanceRandom(), C, TEXT("Seed"));
		B.Link(B.TexObject(TEXT("GrungeTex"), Grunge, SAMPLERTYPE_LinearColor), C, TEXT("GrungeTex"));
		B.Link(B.TexObject(TEXT("StreakTex"), Streaks, SAMPLERTYPE_LinearGrayscale), C, TEXT("StreakTex"));
		B.Out(C, MP_BaseColor, TEXT("return"));
		B.Out(C, MP_Roughness, TEXT("Rough"));
		B.Out(C, MP_Metallic, TEXT("Metal"));
		B.Out(C, MP_Specular, TEXT("Spec"));
		B.Out(C, MP_Normal, TEXT("NormalWS"));
		B.Out(C, MP_AmbientOcclusion, TEXT("AO"));
		// Lit windows hold their brightness against the eye's adaptation: at dusk (exposed for daylight) and on a dark
		// roof at night (wide open) a lit room reads a couple of stops over the walls, never burnt out to white.
		B.Out(B.ExposureCompensated(C, 0.06f, TEXT("Emis")), MP_EmissiveColor);
		FinishMaterial(B.M);
		return B.M;
	}

	// Curtain-wall skyscraper: glass in a grid of mullions and spandrels laid out on the box's faces, sky reflections
	// from the glazing, and offices behind it - dark by day, lit floor by floor as night falls (some always burning).
	const TCHAR* TowerHLSL = TEXT(R"(
float3 n = normalize(VN);
float3 eX = normalize(AX);
float3 eY = normalize(AY);
float3 eZ = normalize(AZ);
float3 he = max(HE, 1.0);
float3 u = clamp(LP / 50.0, -1.0, 1.0);
float3 off = u.x * he.x * eX + u.y * he.y * eY + u.z * he.z * eZ;
Metal = 0.0;
Spec = 0.5;
Emis = 0.0;
if (abs(n.z) > 0.7)
{
	Rough = 0.85;
	return float3(0.22, 0.22, 0.21);
}
float3 T = normalize(cross(float3(0.0, 0.0, 1.0), n));
float3 lnA = abs(float3(dot(n, eX), dot(n, eY), dot(n, eZ)));
float W = lnA.x > lnA.y ? he.y : he.x;
float h = dot(off, T) + W;
float z = u.z * he.z + he.z;
float face = floor(dot(n.xy, float2(1.7, 3.1)) * 10.0);
float BW = Bay > 1.0 ? Bay : 150.0;
float FH = FloorH > 1.0 ? FloorH : 400.0;
float bayI = floor(h / BW);
float flI = floor(z / FH);
float bx = h - (bayI + 0.5) * BW;
float fz = z - flI * FH;
float2 fw = max(float2(fwidth(h), fwidth(z)), 0.02);
float hsh = frac(sin(dot(float3(floor(bayI / 2.0), flI, face + Seed * 91.0), float3(12.9898, 78.233, 37.719))) * 43758.5453);
float hsh2 = frac(sin(dot(float3(bayI, flI * 1.3, face + Seed * 17.0), float3(39.346, 11.135, 83.155))) * 53758.5453);
float floorLit = frac(sin(dot(float2(flI, Seed * 7.0 + face), float2(17.17, 41.3))) * 24634.63);
// Spandrel band over each slab and the vertical mullions; both fade to their average as they shrink on screen.
float spandrel = 1.0 - smoothstep(85.0 - fw.y, 85.0 + fw.y, fz);
float mull = 1.0 - smoothstep(3.0 - fw.x, 3.0 + fw.x, abs(bx) - BW * 0.5 + 3.0);
mull = lerp(mull, 6.0 / BW, saturate(fw.x / BW * 2.0));
spandrel = lerp(spandrel, 85.0 / FH, saturate(fw.y / FH * 2.0));
// Glass reads as one reflective skin with faint panel-to-panel variation; spandrels and mullions are darker bands.
float3 glass = Tint * (0.92 + 0.16 * hsh2);
float3 frame = Tint * 0.55 + 0.02;
float3 c = lerp(glass, frame, spandrel);
c = lerp(c, frame * 0.8, mull);
Rough = lerp(lerp(IRough, 0.3, spandrel), 0.3, mull);
Spec = lerp(0.85, 0.4, max(spandrel, mull));
// Offices: whole floors are let to one tenant and mostly lit together after dark, a few bays dark, some floors empty.
float floorOn = step(floorLit, Lit * 0.25 + Night * 0.35);
float bayOn = step(0.18, hsh);
// A bay down to a pixel or two would only sparkle: it fades to the floor's average.
float tiny = saturate(fw.x / BW * 2.5 - 0.35);
bayOn = lerp(bayOn, 0.82, tiny);
hsh2 = lerp(hsh2, 0.6, tiny);
float glassMask = (1.0 - spandrel) * (1.0 - mull);
float3 tube = lerp(float3(0.88, 0.94, 1.0), float3(1.0, 0.84, 0.62), step(0.55, floorLit));
float ceilingGlow = 0.55 + 0.45 * smoothstep(FH - 160.0, FH - 70.0, fz);
float blinds = 0.75 + 0.25 * step(0.5, frac(fz / 9.0)) * step(0.6, hsh2);
// Seen through tinted glazing an office reads a few tens of nits at most, and floors differ: never a white block.
Emis = tube * floorOn * bayOn * glassMask * ceilingGlow * blinds * (2.5 + 5.0 * hsh2 * hsh2) * max(Night, 0.03);
// The curtain wall goes dark after sunset round its lit floors (it only mirrors a night sky).
c *= lerp(1.0, 0.4, saturate(Night * 1.4 - 0.4));
return c;
)");

	UMaterial* BuildTowerMaterial()
	{
		FMat B{ NewMaterial(TEXT("M_Campus_Tower")) };
		B.M->bTangentSpaceNormal = false;
		UMaterialExpressionCustom* C = B.Custom(TowerHLSL, CMOT_Float3,
			{ TEXT("WP"), TEXT("VN"), TEXT("LP"), TEXT("AX"), TEXT("AY"), TEXT("AZ"), TEXT("HE"), TEXT("Tint"), TEXT("IRough"), TEXT("Lit"), TEXT("Bay"), TEXT("FloorH"), TEXT("Seed"), TEXT("Night") },
			{ { TEXT("Rough"), CMOT_Float1 }, { TEXT("Metal"), CMOT_Float1 }, { TEXT("Spec"), CMOT_Float1 }, { TEXT("Emis"), CMOT_Float3 } });
		B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("VN"));
		LinkBoxInputs(B, C);
		B.Link(B.CustomData(0, 0.12f, true), C, TEXT("Tint"));
		B.Link(B.CustomData(3, 0.08f, false), C, TEXT("IRough"));
		B.Link(B.CustomData(5, 0.3f, false), C, TEXT("Lit"));
		B.Link(B.CustomData(6, 150.f, false), C, TEXT("Bay"));
		B.Link(B.CustomData(7, 400.f, false), C, TEXT("FloorH"));
		B.Link(B.InstanceRandom(), C, TEXT("Seed"));
		B.Link(B.Collection(CampusEditor::EnvironmentCollection(), TEXT("NightLights")), C, TEXT("Night"));
		B.Out(C, MP_BaseColor, TEXT("return"));
		B.Out(C, MP_Roughness, TEXT("Rough"));
		B.Out(C, MP_Metallic, TEXT("Metal"));
		B.Out(C, MP_Specular, TEXT("Spec"));
		// As the homes: lit floors hold their brightness against the eye's adaptation.
		B.Out(B.ExposureCompensated(C, 0.15f, TEXT("Emis")), MP_EmissiveColor);
		FinishMaterial(B.M);
		return B.M;
	}

	UMaterial* BuildGrilleMaterial(UTexture2D* Grunge)
	{
		FMat B{ NewMaterial(TEXT("M_Campus_Grille")) };
		B.M->BlendMode = BLEND_Masked;
		B.M->TwoSided = true;
		B.M->OpacityMaskClipValue = 0.5f;
		B.M->DitherOpacityMask = false;
		UMaterialExpressionCustom* C = B.Custom(GrilleHLSL, CMOT_Float3,
			{ TEXT("WP"), TEXT("VN"), TEXT("LP"), TEXT("AX"), TEXT("AY"), TEXT("AZ"), TEXT("HE"), TEXT("Tint"), TEXT("IRough"), TEXT("Pitch"), TEXT("Width"), TEXT("WirePitch"), TEXT("GrungeTex") },
			{ { TEXT("Mask"), CMOT_Float1 }, { TEXT("Rough"), CMOT_Float1 } });
		B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("VN"));
		LinkBoxInputs(B, C);
		B.Link(B.CustomData(0, 0.12f, true), C, TEXT("Tint"));
		B.Link(B.CustomData(3, 0.5f, false), C, TEXT("IRough"));
		B.Link(B.CustomData(6, 12.f, false), C, TEXT("Pitch"));
		B.Link(B.CustomData(7, 1.6f, false), C, TEXT("Width"));
		B.Link(B.CustomData(5, 0.f, false), C, TEXT("WirePitch"));
		B.Link(B.TexObject(TEXT("GrungeTex"), Grunge, SAMPLERTYPE_LinearColor), C, TEXT("GrungeTex"));
		B.Out(C, MP_BaseColor, TEXT("return"));
		B.Out(C, MP_Roughness, TEXT("Rough"));
		B.Out(B.CustomData(4, 0.f, false), MP_Metallic);
		B.Out(C, MP_OpacityMask, TEXT("Mask"));
		FinishMaterial(B.M);
		return B.M;
	}

	// Mountain range seen from 2-12 km: subtropical broadleaf forest as canopy clumps and darker gullies, grassland
	// (芒草) on the high ridges, bare earth on the steepest slopes, and the city's ground plain near the school.
	// Normals are perturbed by the canopy height so the low sun picks out the texture of the forest.
	const TCHAR* TerrainHLSL = TEXT(R"(
float3 n = normalize(VN);
float dist = length(WP.xy);
float4 g0 = Texture2DSample(GrungeTex, GrungeTexSampler, WP.xy / 90000.0 + 0.17);
float4 g1 = Texture2DSample(GrungeTex, GrungeTexSampler, WP.xy / 21000.0 + 0.53);
float4 g2 = Texture2DSample(GrungeTex, GrungeTexSampler, WP.xy / 5200.0);
float e = 1500.0;
float hC = Texture2DSample(GrungeTex, GrungeTexSampler, WP.xy / 5200.0).b * 0.6 + g1.r * 0.4;
float hX = Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy + float2(e, 0.0)) / 5200.0).b * 0.6 + Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy + float2(e, 0.0)) / 21000.0 + 0.53).r * 0.4;
float hY = Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy + float2(0.0, e)) / 5200.0).b * 0.6 + Texture2DSample(GrungeTex, GrungeTexSampler, (WP.xy + float2(0.0, e)) / 21000.0 + 0.53).r * 0.4;
float slope = 1.0 - saturate(n.z);
float hill = saturate(WP.z / 3000.0);

float3 forestDark = float3(0.018, 0.040, 0.016);
float3 forest = float3(0.030, 0.068, 0.024);
float3 forestLight = float3(0.058, 0.090, 0.030);
float canopy = saturate(g2.b * 1.3 - 0.15);
float3 c = lerp(forestDark, forest, canopy);
c = lerp(c, forestLight, saturate((g1.r - 0.55) * 3.0) * 0.8);
c = lerp(c, c * float3(1.2, 1.05, 0.8), saturate((g0.g - 0.5) * 2.0));
float gully = saturate((0.42 - g1.g) * 3.0) * hill;
c *= 1.0 - 0.35 * gully;
float grass = smoothstep(0.62, 0.8, WP.z / max(PeakHeight, 1.0) + (g1.b - 0.5) * 0.25) * (1.0 - slope * 0.8);
c = lerp(c, float3(0.075, 0.085, 0.04) * (0.8 + 0.4 * g2.r), grass);
float bare = smoothstep(0.6, 0.85, slope + (g2.g - 0.5) * 0.3) * hill;
c = lerp(c, float3(0.09, 0.08, 0.06), bare * 0.5);

// City plain: streets, car parks and low roofs between the blocks, dark and only gently varied.
float urban = (1.0 - hill) * (1.0 - smoothstep(190000.0, 260000.0, dist));
float3 city = float3(0.055, 0.056, 0.058) * (0.8 + 0.4 * g2.r) * (0.9 + 0.2 * g1.g);
city = lerp(city, float3(0.03, 0.05, 0.022), saturate((g2.g - 0.58) * 4.0) * 0.8);
c = lerp(c, city, urban);

float bump = (1.0 - urban) * (0.6 + 0.6 * hill);
float3 grad = float3(hX - hC, hY - hC, 0.0) * bump * 2.2;
NormalWS = normalize(n - grad + float3(0.0, 0.0, 0.0));
Rough = lerp(0.88, 0.95, urban);
AO = lerp(1.0, 0.6, gully * 0.7 + (1.0 - canopy) * 0.25 * (1.0 - urban));
return c;
)");

	UMaterial* BuildTerrainMaterial(UTexture2D* Grunge)
	{
		FMat B{ NewMaterial(TEXT("M_Campus_Terrain")) };
		B.M->bTangentSpaceNormal = false;
		B.M->SetUsageByFlag(MATUSAGE_Nanite, true);
		UMaterialExpressionCustom* C = B.Custom(TerrainHLSL, CMOT_Float3, { TEXT("WP"), TEXT("VN"), TEXT("PeakHeight"), TEXT("GrungeTex") },
			{ { TEXT("Rough"), CMOT_Float1 }, { TEXT("NormalWS"), CMOT_Float3 }, { TEXT("AO"), CMOT_Float1 } });
		B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("VN"));
		B.Link(B.Scalar(TEXT("PeakHeight"), 70000.f), C, TEXT("PeakHeight"));
		B.Link(B.TexObject(TEXT("GrungeTex"), Grunge, SAMPLERTYPE_LinearColor), C, TEXT("GrungeTex"));
		B.Out(C, MP_BaseColor, TEXT("return"));
		B.Out(C, MP_Roughness, TEXT("Rough"));
		B.Out(C, MP_Normal, TEXT("NormalWS"));
		B.Out(C, MP_AmbientOcclusion, TEXT("AO"));
		B.Out(B.Const(0.4f), MP_Specular);
		FinishMaterial(B.M);
		return B.M;
	}

	// ------------------------------------------------------------------ props
	enum class EPropCollision : uint8 { Box, None, Trunk, Pole, Pot };

	struct FPropRule
	{
		const TCHAR* Id;
		uint8 Impact;
		EPropCollision Collision;
	};

	const FPropRule PropRules[] = {
		{ TEXT("island_tree_01"), 3, EPropCollision::Trunk }, { TEXT("island_tree_02"), 3, EPropCollision::Trunk }, { TEXT("island_tree_03"), 3, EPropCollision::Trunk },
		{ TEXT("shrub_01"), 3, EPropCollision::None }, { TEXT("shrub_02"), 3, EPropCollision::None }, { TEXT("shrub_04"), 3, EPropCollision::None },
		{ TEXT("grass_medium_01"), 3, EPropCollision::None }, { TEXT("periwinkle_plant"), 3, EPropCollision::None },
		{ TEXT("fern_02"), 3, EPropCollision::None }, { TEXT("grass_bermuda_01"), 3, EPropCollision::None },
		{ TEXT("shrub_03"), 3, EPropCollision::None }, { TEXT("calathea_orbifolia_01"), 3, EPropCollision::None },
		{ TEXT("pachira_aquatica_01"), 3, EPropCollision::Pot }, { TEXT("potted_plant_01"), 3, EPropCollision::Pot }, { TEXT("potted_plant_02"), 3, EPropCollision::Pot },
		{ TEXT("SchoolDesk_01"), 2, EPropCollision::Box }, { TEXT("SchoolChair_01"), 2, EPropCollision::Box },
		{ TEXT("ceiling_fan"), 1, EPropCollision::None }, { TEXT("mounted_fluorescent_lights"), 1, EPropCollision::None },
		{ TEXT("wall_clock"), 2, EPropCollision::None }, { TEXT("fire_alarm"), 1, EPropCollision::None },
		{ TEXT("korean_fire_extinguisher_01"), 1, EPropCollision::Box }, { TEXT("metal_office_desk"), 1, EPropCollision::Box },
		{ TEXT("steel_frame_shelves_01"), 1, EPropCollision::Box }, { TEXT("plastic_monobloc_chair_01"), 2, EPropCollision::Box },
		{ TEXT("WetFloorSign_01"), 2, EPropCollision::Box }, { TEXT("metal_trash_can"), 1, EPropCollision::Box }, { TEXT("trashbag"), 3, EPropCollision::Box },
		{ TEXT("exterior_aircon_unit"), 1, EPropCollision::Box }, { TEXT("rollershutter_door"), 1, EPropCollision::Box },
		{ TEXT("rollershutter_window_01"), 1, EPropCollision::Box }, { TEXT("street_lamp_01"), 1, EPropCollision::Pole },
		{ TEXT("utility_box_01"), 1, EPropCollision::Box }, { TEXT("utility_box_02"), 1, EPropCollision::Box },
		{ TEXT("security_camera_01"), 1, EPropCollision::None }, { TEXT("fire_hydrant"), 1, EPropCollision::Box },
		{ TEXT("water_manhole_cover"), 1, EPropCollision::None }, { TEXT("concrete_road_barrier"), 0, EPropCollision::Box },
		{ TEXT("concrete_road_barrier_02"), 0, EPropCollision::Box }, { TEXT("modular_electricity_poles"), 0, EPropCollision::None },
		{ TEXT("korean_public_payphone_01"), 1, EPropCollision::Box }, { TEXT("painted_wooden_bench"), 2, EPropCollision::Box },
		{ TEXT("cardboard_box_01"), 2, EPropCollision::Box }, { TEXT("plastic_crate_01"), 2, EPropCollision::Box },
		{ TEXT("plastic_crate_03"), 2, EPropCollision::Box }, { TEXT("wooden_crate_01"), 2, EPropCollision::Box },
		{ TEXT("Barrel_01"), 1, EPropCollision::Box }, { TEXT("old_tyre"), 3, EPropCollision::Box }, { TEXT("cement_bag"), 3, EPropCollision::Box },
		{ TEXT("portable_generator"), 1, EPropCollision::Box }, { TEXT("hand_truck"), 1, EPropCollision::Box },
		{ TEXT("propane_tank"), 1, EPropCollision::Box }, { TEXT("wooden_military_crate"), 2, EPropCollision::Box },
		{ TEXT("service_pistol"), 1, EPropCollision::None },
	};

	/** Box/Pot collisions are per piece (files often hold several variants side by side); trunks and poles collide as modelled. */
	void SetCollision(UStaticMesh* Mesh, EPropCollision Mode, const FBox& PropBounds, bool bPrimaryPiece)
	{
		const FString PieceName = Mesh->GetName().ToLower();
		const bool bFoliage = PieceName.Contains(TEXT("leaves")) || PieceName.Contains(TEXT("leaf"));
		FBox Bounds = PropBounds;
		if (Mode == EPropCollision::Box || Mode == EPropCollision::Pot)
		{
			Bounds = Mesh->GetBoundingBox();
			bPrimaryPiece = !bFoliage && (Mode == EPropCollision::Box || PieceName.Contains(TEXT("pot")));
		}
		UBodySetup* Body = Mesh->GetBodySetup();
		if (!Body)
		{
			Mesh->CreateBodySetup();
			Body = Mesh->GetBodySetup();
		}
		if (!Body)
		{
			return;
		}
		Body->Modify();
		Body->RemoveSimpleCollision();
		Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		if (Mode == EPropCollision::Trunk || Mode == EPropCollision::Pole)
		{
			// The model's own trunk and boughs (or the lamp's post and arm) collide, not a capsule guessed at the origin:
			// trunks lean, fork and thicken toward the root, and a capsule let bullets and players through all of that.
			// Leaves never collide.
			const bool bSolidPiece = !bFoliage;
			Body->CollisionTraceFlag = bSolidPiece ? CTF_UseComplexAsSimple : CTF_UseSimpleAsComplex;
			FMeshSectionInfoMap& Sections = Mesh->GetSectionInfoMap();
			FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
			const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
			const int32 NumSections = Mesh->GetNumSections(0);
			for (int32 s = 0; s < NumSections; ++s)
			{
				FMeshSectionInfo Info = Sections.Get(0, s);
				const FString MatName = Materials.IsValidIndex(Info.MaterialIndex)
					? (Materials[Info.MaterialIndex].MaterialSlotName.ToString() + TEXT(" ") + GetNameSafe(Materials[Info.MaterialIndex].MaterialInterface)).ToLower() : FString();
				Info.bEnableCollision = bSolidPiece && !MatName.Contains(TEXT("leaves")) && !MatName.Contains(TEXT("leaf"));
				Sections.Set(0, s, Info);
				if (RenderData && RenderData->LODResources.Num() > 0 && RenderData->LODResources[0].Sections.IsValidIndex(s))
				{
					RenderData->LODResources[0].Sections[s].bEnableCollision = Info.bEnableCollision;
				}
			}
			Body->InvalidatePhysicsData();
			Body->CreatePhysicsMeshes();
			Mesh->MarkPackageDirty();
			return;
		}
		const FVector Size = Bounds.GetSize();
		if (bPrimaryPiece)
		{
			switch (Mode)
			{
			case EPropCollision::Box:
			case EPropCollision::Pot:
			{
				FKBoxElem Box(FMath::Max(Size.X, 1.f), FMath::Max(Size.Y, 1.f), FMath::Max(Size.Z, 1.f));
				Box.Center = Bounds.GetCenter();
				Body->AggGeom.BoxElems.Add(Box);
				break;
			}
			case EPropCollision::Trunk:
			case EPropCollision::Pole:
			case EPropCollision::None:
				break;
			}
		}
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		Mesh->MarkPackageDirty();
	}

	TArray<UStaticMesh*> FindStaticMeshes(const FString& Folder)
	{
		TArray<FAssetData> Assets;
		IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
		Registry.ScanPathsSynchronous({ Folder }, true);
		Registry.GetAssetsByPath(FName(*Folder), Assets, true);
		TArray<UStaticMesh*> Meshes;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
			{
				if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset()))
				{
					Meshes.Add(Mesh);
				}
			}
		}
		return Meshes;
	}
}

namespace CampusImport
{
	bool ImportMenuTextures(bool bReimport)
	{
		const FString Folder = TEXT("/Game/Campus/UI/Textures");
		const FString Source = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceArt/Menu"));
		struct FMenuTexture { const TCHAR* File; const TCHAR* Name; };
		const FMenuTexture MenuTextures[] = {
			{ TEXT("MenuCampus.png"), TEXT("T_Menu_Campus") },
			{ TEXT("MenuHotel.png"), TEXT("T_Menu_Hotel") },
			{ TEXT("MenuGunfight.png"), TEXT("T_Menu_Gunfight") }
		};
		TArray<UAssetImportTask*> Tasks;
		bool bOk = true;
		for (const FMenuTexture& MenuTexture : MenuTextures)
		{
			if (!bReimport && LoadQuiet<UTexture2D>(Folder / MenuTexture.Name + TEXT(".") + MenuTexture.Name))
			{
				continue;
			}
			const FString File = Source / MenuTexture.File;
			if (!FPaths::FileExists(File))
			{
				UE_LOG(LogCampusBootstrap, Error, TEXT("Missing menu screenshot %s"), *File);
				bOk = false;
				continue;
			}
			Tasks.Add(MakeTask(File, Folder, MenuTexture.Name));
		}
		if (Tasks.IsEmpty())
		{
			return bOk;
		}
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
		for (UAssetImportTask* Task : Tasks)
		{
			UTexture2D* Texture = nullptr;
			for (UObject* Object : Task->GetObjects())
			{
				if ((Texture = Cast<UTexture2D>(Object)))
				{
					break;
				}
			}
			if (!Texture)
			{
				Texture = LoadQuiet<UTexture2D>(Task->DestinationPath / Task->DestinationName + TEXT(".") + Task->DestinationName);
			}
			if (Texture)
			{
				Texture->PreEditChange(nullptr);
				Texture->SRGB = true;
				Texture->CompressionSettings = TC_EditorIcon;
				Texture->LODGroup = TEXTUREGROUP_UI;
				Texture->MipGenSettings = TMGS_SimpleAverage;
				Texture->Filter = TF_Trilinear;
				Texture->NeverStream = true;
				Texture->VirtualTextureStreaming = false;
				Texture->AddressX = TA_Clamp;
				Texture->AddressY = TA_Clamp;
				Texture->MaxTextureSize = 2048;
				Texture->Downscale.Default = 1.f;
				Texture->PostEditChange();
				bOk &= SaveAsset(Texture);
			}
			else
			{
				UE_LOG(LogCampusBootstrap, Error, TEXT("Menu screenshot import failed: %s"), *Task->Filename);
				bOk = false;
			}
			Task->RemoveFromRoot();
		}
		return bOk;
	}

	void ImportSurfaceTextures(bool bReimport)
	{
		struct FMap { const TCHAR* File; const TCHAR* Suffix; ETexKind Kind; };
		const FMap Maps[] = { { TEXT("diff"), TEXT("D"), ETexKind::Color }, { TEXT("nor_dx"), TEXT("N"), ETexKind::Normal }, { TEXT("arm"), TEXT("ARM"), ETexKind::Masks } };

		TArray<UAssetImportTask*> Tasks;
		TArray<ETexKind> Kinds;
		TSet<FString> Seen;
		TArray<const TCHAR*> Sets;
		for (int32 i = 0; i < CampusSurfaces::Num; ++i)
		{
			if (const TCHAR* Set = CampusSurfaces::Info((ECampusMat)i).Texture)
			{
				Sets.Add(Set);
			}
		}
		// uniforms and gloves
		Sets.Add(TEXT("denim_fabric"));
		Sets.Add(TEXT("fabric_leather_01"));
		for (const TCHAR* Set : Sets)
		{
			if (Seen.Contains(Set))
			{
				continue;
			}
			Seen.Add(Set);
			for (const FMap& Map : Maps)
			{
				const FString File = SourceRoot() / TEXT("Textures") / Set / FString::Printf(TEXT("%s_%s.jpg"), Set, Map.File);
				if (!FPaths::FileExists(File))
				{
					UE_LOG(LogCampusBootstrap, Warning, TEXT("Missing texture source %s (run Tools/FetchAssets.ps1)"), *File);
					continue;
				}
				if (!bReimport)
				{
					if (UTexture2D* Existing = LoadQuiet<UTexture2D>(CampusSurfaces::TexturePath(Set, Map.Suffix)))
					{
						if (!Existing->NeverStream)
						{
							ApplyTextureSettings(Existing, Map.Kind);
							SaveAsset(Existing);
						}
						continue;
					}
				}
				Tasks.Add(MakeTask(File, FString(TextureFolder) / Set, FString::Printf(TEXT("T_%s_%s"), Set, Map.Suffix)));
				Kinds.Add(Map.Kind);
			}
		}
		if (Tasks.Num() == 0)
		{
			UE_LOG(LogCampusBootstrap, Display, TEXT("Surface textures up to date"));
			return;
		}
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
		int32 Imported = 0;
		for (int32 i = 0; i < Tasks.Num(); ++i)
		{
			UTexture2D* Tex = nullptr;
			for (UObject* Object : Tasks[i]->GetObjects())
			{
				Tex = Cast<UTexture2D>(Object);
				if (Tex)
				{
					break;
				}
			}
			if (!Tex)
			{
				Tex = LoadQuiet<UTexture2D>(Tasks[i]->DestinationPath / Tasks[i]->DestinationName + TEXT(".") + Tasks[i]->DestinationName);
			}
			if (Tex)
			{
				ApplyTextureSettings(Tex, Kinds[i]);
				SaveAsset(Tex);
				++Imported;
			}
			else
			{
				UE_LOG(LogCampusBootstrap, Warning, TEXT("Texture import failed: %s"), *Tasks[i]->Filename);
			}
			Tasks[i]->RemoveFromRoot();
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("Imported %d / %d surface textures"), Imported, Tasks.Num());
	}

	void BuildWeatheringTextures()
	{
		// Grunge: R macro dirt, G stains, B fine grain, A wet-patch mask. Tileable.
		{
			const int32 S = 1024;
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(S * S);
			FTileNoise N;
			N.Seed = 7;
			FTileNoise N2;
			N2.Seed = 19;
			ParallelFor(S, [&](int32 Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					const float U = (X + 0.5f) / S;
					const float V = (Y + 0.5f) / S;
					const float Macro = Stretch(N.FBM(U, V, 4, 6), 2.2f);
					const float Edge = N2.FBM(U, V, 16, 4);
					const float SpotA = FMath::Pow(FMath::Clamp(1.f - N.Cells(U, V, 6) / 0.5f, 0.f, 1.f), 2.f) * (0.55f + 0.9f * Edge);
					const float SpotB = FMath::Pow(FMath::Clamp(1.f - N2.Cells(U, V, 14) / 0.4f, 0.f, 1.f), 2.f) * (0.4f + 0.8f * Edge) * 0.7f;
					const float Stains = FMath::Clamp(FMath::Max(SpotA, SpotB) * 1.2f, 0.f, 1.f);
					const float Grain = Stretch(N2.FBM(U, V, 64, 3), 2.5f);
					const float Wet = Stretch(N.FBM(U + 0.37f, V + 0.11f, 3, 5), 2.6f);
					Pixels[Y * S + X] = FColor(ToByte(Macro), ToByte(Stains), ToByte(Grain), ToByte(Wet));
				}
			});
			CreateTexture(TEXT("T_Campus_Grunge"), S, S, Pixels, false, TC_Default, TEXTUREGROUP_World);
		}
		// Leak streaks: V = 0 just under a slab, running down the wall. Tileable horizontally.
		{
			const int32 W = 512;
			const int32 H = 512;
			TArray<float> Value;
			Value.SetNumZeroed(W * H);
			FRandomStream Rng(1337);
			FTileNoise N;
			N.Seed = 3;
			for (int32 i = 0; i < 240; ++i)
			{
				const float Cx = Rng.FRand() * W;
				const float Width = Rng.FRandRange(1.2f, 8.f);
				const float Length = Rng.FRandRange(0.1f, 0.95f) * H;
				const float Intensity = Rng.FRandRange(0.2f, 1.f);
				const float Start = Rng.FRandRange(0.f, 0.06f) * H;
				for (int32 Y = (int32)Start; Y < FMath::Min((int32)(Start + Length), H); ++Y)
				{
					const float T = (Y - Start) / Length;
					const float Fall = FMath::Pow(1.f - T, 0.8f) * (0.55f + 0.45f * N.Value(i * 0.071f, Y / (float)H, 24));
					const float Wobble = (N.Value(i * 0.13f, Y / (float)H, 8) - 0.5f) * Width * 1.6f;
					const int32 Span = FMath::CeilToInt(Width * 2.5f);
					for (int32 Dx = -Span; Dx <= Span; ++Dx)
					{
						const int32 X = (((int32)(Cx + Wobble) + Dx) % W + W) % W;
						const float Falloff = FMath::Exp(-FMath::Square(Dx / Width));
						float& Out = Value[Y * W + X];
						Out = FMath::Max(Out, Intensity * Fall * Falloff);
					}
				}
			}
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(W * H);
			for (int32 Y = 0; Y < H; ++Y)
			{
				const float Lip = FMath::Clamp(1.f - Y / (H * 0.08f), 0.f, 1.f) * 0.35f;
				for (int32 X = 0; X < W; ++X)
				{
					const uint8 B = ToByte(FMath::Clamp(Value[Y * W + X] + Lip, 0.f, 1.f));
					Pixels[Y * W + X] = FColor(B, B, B, 255);
				}
			}
			CreateTexture(TEXT("T_Campus_Streaks"), W, H, Pixels, false, TC_Grayscale, TEXTUREGROUP_World);
		}
		// Neutral fallbacks for untextured surfaces.
		{
			TArray<FColor> Gray;
			Gray.Init(FColor(188, 188, 188, 255), 16);
			CreateTexture(TEXT("T_Campus_Flat_D"), 4, 4, Gray, true, TC_Default, TEXTUREGROUP_World);
			TArray<FColor> Normal;
			Normal.Init(FColor(128, 128, 255, 255), 16);
			CreateTexture(TEXT("T_Campus_Flat_N"), 4, 4, Normal, false, TC_Normalmap, TEXTUREGROUP_WorldNormalMap);
			TArray<FColor> Arm;
			Arm.Init(FColor(255, 128, 0, 255), 16);
			CreateTexture(TEXT("T_Campus_Flat_ARM"), 4, 4, Arm, false, TC_Masks, TEXTUREGROUP_World);
		}
	}

	void BuildSurfaceMaterials()
	{
		auto Tex = [](const TCHAR* Name) { return LoadQuiet<UTexture2D>(FString::Printf(TEXT("%s/%s.%s"), TextureFolder, Name, Name)); };
		UTexture2D* FlatD = Tex(TEXT("T_Campus_Flat_D"));
		UTexture2D* FlatN = Tex(TEXT("T_Campus_Flat_N"));
		UTexture2D* FlatARM = Tex(TEXT("T_Campus_Flat_ARM"));
		UTexture2D* Grunge = Tex(TEXT("T_Campus_Grunge"));
		UTexture2D* Streaks = Tex(TEXT("T_Campus_Streaks"));
		if (!FlatD || !FlatN || !FlatARM || !Grunge || !Streaks)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("Weathering textures missing; run without -skipmaterials"));
			return;
		}

		FMat B{ NewMaterial(TEXT("M_Campus_Surface")) };
		B.M->bTangentSpaceNormal = false;
		UMaterialExpression* WP = B.Make<UMaterialExpressionWorldPosition>();
		UMaterialExpression* VN = B.Make<UMaterialExpressionVertexNormalWS>();
		UMaterialExpression* CornerD = B.DistanceToNearestSurface(B.Add(WP, B.MulC(VN, 15.f)));
		UMaterialExpressionCustom* C = B.Custom(SurfaceHLSL, CMOT_Float3,
			{ TEXT("WP"), TEXT("VN"), TEXT("Tint"), TEXT("IRough"), TEXT("IMetal"), TEXT("IEmis"), TEXT("ITile"), TEXT("IDirt"), TEXT("CornerD"),
			  TEXT("BaseTex"), TEXT("NormalTex"), TEXT("ArmTex"), TEXT("GrungeTex"), TEXT("StreakTex"),
			  TEXT("TexScale"), TEXT("TintStrength"), TEXT("RoughScale"), TEXT("RoughFromInstance"), TEXT("MetalFromInstance"), TEXT("NormalStrength"),
			  TEXT("GroundDirt"), TEXT("Streaks"), TEXT("Wetness"), TEXT("Macro"), TEXT("AvgLum"), TEXT("Weathering"), TEXT("AntiTile"), TEXT("DetailContrast"),
			  TEXT("LP"), TEXT("AX"), TEXT("AY"), TEXT("AZ"), TEXT("HE"), TEXT("BevelRadius"), TEXT("EdgeWear"), TEXT("EdgeRough"),
			  TEXT("GWet"), TEXT("RainPuddles"), TEXT("MaskTex"), TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop"), TEXT("Cam") },
			{ { TEXT("Rough"), CMOT_Float1 }, { TEXT("Metal"), CMOT_Float1 }, { TEXT("NormalWS"), CMOT_Float3 }, { TEXT("AO"), CMOT_Float1 }, { TEXT("Emis"), CMOT_Float3 } });
		B.Link(WP, C, TEXT("WP"));
		B.Link(VN, C, TEXT("VN"));
		B.Link(B.Make<UMaterialExpressionCameraPositionWS>(), C, TEXT("Cam"));
		{
			UMaterialParameterCollection* Env = CampusEditor::EnvironmentCollection();
			B.Link(B.Collection(Env, TEXT("Wetness")), C, TEXT("GWet"));
			B.Link(B.Scalar(TEXT("RainPuddles"), 1.f), C, TEXT("RainPuddles"));
			UTexture* Mask = LoadObject<UTexture>(nullptr, TEXT("/Game/Campus/Materials/RT_Campus_RainMask.RT_Campus_RainMask"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			B.Link(B.TexObject(TEXT("RainMask"), Mask ? Mask : FlatD, SAMPLERTYPE_LinearColor), C, TEXT("MaskTex"));
			for (const TCHAR* P : { TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop") })
			{
				B.Link(B.Collection(Env, P), C, P);
			}
		}
		{
			UMaterialExpressionLocalPosition* LP = B.Make<UMaterialExpressionLocalPosition>();
			LP->LocalOrigin = ELocalPositionOrigin::Instance;
			B.Link(LP, C, TEXT("LP"));
			const TCHAR* AxisNames[3] = { TEXT("AX"), TEXT("AY"), TEXT("AZ") };
			for (int32 i = 0; i < 3; ++i)
			{
				UMaterialExpressionConstant3Vector* V = B.Make<UMaterialExpressionConstant3Vector>();
				V->Constant = FLinearColor(i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f);
				UMaterialExpressionTransform* T = B.Make<UMaterialExpressionTransform>();
				T->TransformSourceType = TRANSFORMSOURCE_Instance;
				T->TransformType = TRANSFORM_World;
				B.Link(V, T);
				B.Link(T, C, AxisNames[i]);
			}
			B.Link(B.CustomData(8, 0.f, true), C, TEXT("HE"));
			B.Link(B.Scalar(TEXT("BevelRadius"), 1.6f), C, TEXT("BevelRadius"));
			B.Link(B.Scalar(TEXT("EdgeWear"), 0.55f), C, TEXT("EdgeWear"));
			B.Link(B.Scalar(TEXT("EdgeRough"), 0.12f), C, TEXT("EdgeRough"));
		}
		B.Link(B.CustomData(0, 0.5f, true), C, TEXT("Tint"));
		B.Link(B.CustomData(3, 0.6f, false), C, TEXT("IRough"));
		B.Link(B.CustomData(4, 0.f, false), C, TEXT("IMetal"));
		B.Link(B.CustomData(5, 0.f, false), C, TEXT("IEmis"));
		B.Link(B.CustomData(6, 0.f, false), C, TEXT("ITile"));
		B.Link(B.CustomData(7, 1.f, false), C, TEXT("IDirt"));
		B.Link(CornerD, C, TEXT("CornerD"));
		B.Link(B.TexObject(TEXT("BaseTex"), FlatD, SAMPLERTYPE_Color), C, TEXT("BaseTex"));
		B.Link(B.TexObject(TEXT("NormalTex"), FlatN, SAMPLERTYPE_Normal), C, TEXT("NormalTex"));
		B.Link(B.TexObject(TEXT("ArmTex"), FlatARM, SAMPLERTYPE_Masks), C, TEXT("ArmTex"));
		B.Link(B.TexObject(TEXT("GrungeTex"), Grunge, SAMPLERTYPE_LinearColor), C, TEXT("GrungeTex"));
		B.Link(B.TexObject(TEXT("StreakTex"), Streaks, SAMPLERTYPE_LinearGrayscale), C, TEXT("StreakTex"));
		const TPair<const TCHAR*, float> Scalars[] = {
			{ TEXT("TexScale"), 200.f }, { TEXT("TintStrength"), 1.f }, { TEXT("RoughScale"), 1.f }, { TEXT("RoughFromInstance"), 0.f },
			{ TEXT("MetalFromInstance"), 1.f }, { TEXT("NormalStrength"), 1.f }, { TEXT("GroundDirt"), 0.5f }, { TEXT("Streaks"), 0.5f },
			{ TEXT("Wetness"), 0.f }, { TEXT("Macro"), 0.5f }, { TEXT("AvgLum"), 0.25f }, { TEXT("Weathering"), 1.f }, { TEXT("AntiTile"), 0.f }, { TEXT("DetailContrast"), 0.9f } };
		for (const TPair<const TCHAR*, float>& S : Scalars)
		{
			B.Link(B.Scalar(S.Key, S.Value), C, S.Key);
		}
		B.Out(C, MP_BaseColor, TEXT("return"));
		B.Out(C, MP_Roughness, TEXT("Rough"));
		B.Out(C, MP_Metallic, TEXT("Metal"));
		B.Out(C, MP_Normal, TEXT("NormalWS"));
		B.Out(C, MP_AmbientOcclusion, TEXT("AO"));
		B.Out(C, MP_EmissiveColor, TEXT("Emis"));
		FinishMaterial(B.M);

		// Clear window glass: thin, mostly transparent, reflective at grazing angles (Lumen front-layer reflections).
		UMaterial* GlassMaster = nullptr;
		{
			FMat G{ NewMaterial(TEXT("M_Campus_Glass")) };
			G.M->BlendMode = BLEND_Translucent;
			G.M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
			G.M->SetShadingModel(MSM_DefaultLit);
			UMaterialExpressionCustom* Fres = G.Custom(TEXT("float f = pow(1.0 - saturate(abs(dot(normalize(V), normalize(N)))), 4.0); return lerp(0.1, 0.7, f);"), CMOT_Float1,
				{ TEXT("V"), TEXT("N") });
			G.Link(G.Make<UMaterialExpressionCameraVectorWS>(), Fres, TEXT("V"));
			G.Link(G.Make<UMaterialExpressionVertexNormalWS>(), Fres, TEXT("N"));
			G.Out(G.Vec(TEXT("Tint"), FLinearColor(0.02f, 0.03f, 0.03f)), MP_BaseColor);
			G.Out(G.Const(0.03f), MP_Roughness);
			G.Out(G.Const(0.f), MP_Metallic);
			G.Out(G.Const(0.6f), MP_Specular);
			G.Out(Fres, MP_Opacity);
			FinishMaterial(G.M);
			GlassMaster = G.M;
		}

		UMaterial* FacadeMaster = BuildFacadeMaterial(Grunge, Streaks);
		UMaterial* GrilleMaster = BuildGrilleMaterial(Grunge);
		UMaterial* TowerMaster = BuildTowerMaterial();
		BuildTerrainMaterial(Grunge);

		for (int32 i = 0; i < CampusSurfaces::Num; ++i)
		{
			const FCampusMatInfo& Info = CampusSurfaces::Info((ECampusMat)i);
			UMaterial* OwnMaster = (ECampusMat)i == ECampusMat::Glass ? GlassMaster
				: ((ECampusMat)i == ECampusMat::Facade || (ECampusMat)i == ECampusMat::FacadeFar) ? FacadeMaster
				: ((ECampusMat)i == ECampusMat::Tower || (ECampusMat)i == ECampusMat::TowerFar) ? TowerMaster
				: (ECampusMat)i == ECampusMat::Grille ? GrilleMaster : nullptr;
			if (OwnMaster)
			{
				UMaterialInstanceConstant* OwnMI = NewInstance(FString(TEXT("MI_Surface_")) + Info.Name, OwnMaster);
				UMaterialEditingLibrary::UpdateMaterialInstance(OwnMI);
				SaveAsset(OwnMI);
				continue;
			}
			UTexture2D* D = Info.Texture ? LoadQuiet<UTexture2D>(CampusSurfaces::TexturePath(Info.Texture, TEXT("D"))) : nullptr;
			UTexture2D* N = Info.Texture ? LoadQuiet<UTexture2D>(CampusSurfaces::TexturePath(Info.Texture, TEXT("N"))) : nullptr;
			UTexture2D* ARM = Info.Texture ? LoadQuiet<UTexture2D>(CampusSurfaces::TexturePath(Info.Texture, TEXT("ARM"))) : nullptr;
			const bool bTextured = D && N && ARM;
			if (Info.Texture && !bTextured)
			{
				UE_LOG(LogCampusBootstrap, Warning, TEXT("Surface %s: texture set %s incomplete, using flat fallback"), Info.Name, Info.Texture);
			}
			UMaterialInstanceConstant* MI = NewInstance(FString(TEXT("MI_Surface_")) + Info.Name, B.M);
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("BaseTex"), bTextured ? D : FlatD);
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("NormalTex"), bTextured ? N : FlatN);
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, TEXT("ArmTex"), bTextured ? ARM : FlatARM);
			const float AvgLum = AverageLuminance(bTextured ? D : FlatD);
			const TPair<const TCHAR*, float> Values[] = {
				{ TEXT("TexScale"), Info.TexScale }, { TEXT("TintStrength"), bTextured ? Info.TintStrength : 1.f }, { TEXT("RoughScale"), Info.RoughScale },
				{ TEXT("RoughFromInstance"), bTextured ? Info.RoughFromInstance : 1.f }, { TEXT("MetalFromInstance"), bTextured ? Info.MetalFromInstance : 1.f },
				{ TEXT("NormalStrength"), Info.NormalStrength }, { TEXT("GroundDirt"), Info.GroundDirt }, { TEXT("Streaks"), Info.Streaks },
				{ TEXT("Wetness"), Info.Wetness }, { TEXT("Macro"), Info.Macro }, { TEXT("AvgLum"), AvgLum }, { TEXT("AntiTile"), bTextured ? Info.AntiTile : 0.f },
				{ TEXT("DetailContrast"), Info.DetailContrast },
				// Turf, soil and rubber soak the rain up instead of pooling it.
				{ TEXT("RainPuddles"), Info.Impact == 3 ? 0.25f : 1.f } };
			for (const TPair<const TCHAR*, float>& V : Values)
			{
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, V.Key, V.Value);
			}
			if ((ECampusMat)i == ECampusMat::Hedge)
			{
				// Shears leave a hedge with soft, rounded shoulders, not a chipped arris.
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("BevelRadius"), 7.f);
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("EdgeWear"), 0.f);
			}
			UMaterialEditingLibrary::UpdateMaterialInstance(MI);
			SaveAsset(MI);
			UE_LOG(LogCampusBootstrap, Display, TEXT("Surface %s: %s (avg luminance %.3f)"), Info.Name, bTextured ? Info.Texture : TEXT("flat"), AvgLum);
		}
		BuildHotelInteriorMaterials();
	}

	void BuildHotelInteriorMaterials()
	{
		UTexture2D* D = LoadQuiet<UTexture2D>(CampusSurfaces::TexturePath(TEXT("white_plaster_02"), TEXT("D")));
		UTexture2D* N = LoadQuiet<UTexture2D>(CampusSurfaces::TexturePath(TEXT("white_plaster_02"), TEXT("N")));
		if (!D || !N)
		{
			UE_LOG(LogCampusBootstrap, Error, TEXT("Hotel interior needs the existing white_plaster_02 texture set"));
			return;
		}
		FMat B{ NewMaterial(TEXT("M_Campus_HotelInterior")) };
		B.M->bTangentSpaceNormal = false;
		UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float3 n = normalize(VN);
float3 a = abs(n);
float3 t = a.z >= a.x && a.z >= a.y ? float3(1.0, 0.0, 0.0)
	: (a.x >= a.y ? float3(0.0, n.x > 0.0 ? -1.0 : 1.0, 0.0) : float3(n.y > 0.0 ? 1.0 : -1.0, 0.0, 0.0));
float3 b = normalize(cross(n, t));
float2 uv = float2(dot(WP, t), dot(WP, b)) / 100.0;
float3 photo = Texture2DSample(BaseTex, BaseTexSampler, uv).rgb;
float lum = dot(photo, float3(0.299, 0.587, 0.114));
float detail = clamp(1.0 + (lum / max(AvgLum, 0.02) - 1.0) * Detail, 0.88, 1.12);
float2 grain = (Texture2DSample(NormalTex, NormalTexSampler, uv).rg * 2.0 - 1.0) * Strength;
NormalWS = normalize(n + t * grain.x + b * grain.y);
Rough = saturate(IRough + (detail - 1.0) * 0.15);
return Tint * detail;
)"), CMOT_Float3, { TEXT("VN"), TEXT("WP"), TEXT("Tint"), TEXT("IRough"), TEXT("BaseTex"), TEXT("NormalTex"), TEXT("AvgLum"), TEXT("Detail"), TEXT("Strength") },
			{ { TEXT("NormalWS"), CMOT_Float3 }, { TEXT("Rough"), CMOT_Float1 } });
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("VN"));
		B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
		B.Link(B.CustomData(0, 0.7f, true), C, TEXT("Tint"));
		B.Link(B.CustomData(3, 0.78f, false), C, TEXT("IRough"));
		B.Link(B.TexObject(TEXT("BaseTex"), D, SAMPLERTYPE_Color), C, TEXT("BaseTex"));
		B.Link(B.TexObject(TEXT("NormalTex"), N, SAMPLERTYPE_Normal), C, TEXT("NormalTex"));
		B.Link(B.Scalar(TEXT("AvgLum"), AverageLuminance(D)), C, TEXT("AvgLum"));
		B.Link(B.Scalar(TEXT("Detail"), 0.12f), C, TEXT("Detail"));
		B.Link(B.Scalar(TEXT("Strength"), 0.08f), C, TEXT("Strength"));
		B.Out(C, MP_BaseColor);
		B.Out(C, MP_Normal, TEXT("NormalWS"));
		B.Out(C, MP_Roughness, TEXT("Rough"));
		FinishMaterial(B.M);
		for (const ECampusMat Mat : { ECampusMat::HotelPaint, ECampusMat::HotelDado })
		{
			const FCampusMatInfo& Info = CampusSurfaces::Info(Mat);
			UMaterialInstanceConstant* MI = NewInstance(FString(TEXT("MI_Surface_")) + Info.Name, B.M);
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("Detail"), Info.DetailContrast);
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, TEXT("Strength"), Info.NormalStrength);
			UMaterialEditingLibrary::UpdateMaterialInstance(MI);
			SaveAsset(MI);
		}
	}

	void BuildTerrainMesh()
	{
		UMaterialInterface* Material = LoadQuiet<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Terrain.M_Campus_Terrain"));
		if (!Material)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("M_Campus_Terrain missing; run without -skipmaterials"));
			return;
		}
		// Ridged multifractal: sharp crests and branching spurs, like eroded ranges, rather than rounded blobs.
		auto Ridged = [](const FVector2D& P, int32 Octaves)
		{
			float Sum = 0.f, Amp = 0.5f, Freq = 1.f, Prev = 1.f, Norm = 0.f;
			for (int32 i = 0; i < Octaves; ++i)
			{
				float N = 1.f - FMath::Abs(FMath::PerlinNoise2D(P * Freq + FVector2D(i * 17.3f, i * 5.1f)));
				N *= N;
				Sum += N * Amp * Prev;
				Norm += Amp;
				Prev = FMath::Clamp(N * 1.6f, 0.f, 1.f);
				Freq *= 2.07f;
				Amp *= 0.5f;
			}
			return Sum / Norm;
		};
		auto Fbm = [](const FVector2D& P, int32 Octaves)
		{
			float Sum = 0.f, Amp = 0.5f, Freq = 1.f, Norm = 0.f;
			for (int32 i = 0; i < Octaves; ++i)
			{
				Sum += FMath::PerlinNoise2D(P * Freq + FVector2D(i * 3.7f, i * 11.9f)) * Amp;
				Norm += Amp;
				Freq *= 2.f;
				Amp *= 0.5f;
			}
			return Sum / Norm;
		};
		// Heights in cm. The city plain round the school stays flat (just under the street level); hills rise from
		// ~2.5 km and the main range (陽明山-like, ~800 m) stands to the north, lower ranges round the other sides.
		auto Height = [&](float X, float Y)
		{
			const float R = FMath::Sqrt(X * X + Y * Y);
			if (R < 180000.f)
			{
				return -3.f;
			}
			const FVector2D P(X, Y);
			const float North = FMath::SmoothStep(-0.35f, 0.75f, Y / FMath::Max(R, 1.f));
			const float Side = 0.38f + 0.62f * North;
			const float Rise = FMath::SmoothStep(200000.f, 560000.f, R) * (1.f - 0.35f * FMath::SmoothStep(1000000.f, 1250000.f, R));
			const float MainRidge = FMath::Exp(-FMath::Square((Y - 760000.f) / 280000.f)) * FMath::SmoothStep(-0.2f, 0.4f, Y / FMath::Max(R, 1.f));
			// Old volcanic ranges: broad rounded shoulders with a few sharper crests, not an alpine sawtooth.
			const float Crest = Ridged(P / 260000.f, 4);
			const float Rolling = Fbm(P / 110000.f + FVector2D(4.1f, 9.3f), 5) * 0.5f + 0.5f;
			const float Valleys = FMath::SmoothStep(0.15f, 0.55f, Fbm(P / 330000.f + FVector2D(1.7f, 2.2f), 3) * 0.5f + 0.5f);
			float H = (Crest * 0.5f + Rolling * 0.5f) * (0.45f + 0.55f * Valleys);
			H = H * (52000.f * Side + 48000.f * MainRidge) * Rise;
			// Foothills and isolated knolls on the plain's edge.
			H += FMath::Max(0.f, Fbm(P / 40000.f + FVector2D(7.7f, 0.3f), 4)) * 9000.f * FMath::SmoothStep(200000.f, 320000.f, R);
			return H - 3.f;
		};

		const int32 NumA = 1440;
		const int32 NumR = 230;
		const float R0 = 9000.f;
		const float R1 = 1250000.f;
		TArray<float> Radii;
		Radii.SetNum(NumR);
		for (int32 i = 0; i < NumR; ++i)
		{
			Radii[i] = R0 * FMath::Pow(R1 / R0, i / float(NumR - 1));
		}

		const FString PackageName = TEXT("/Game/Campus/Meshes/SM_Campus_Terrain");
		const FString Name = FPackageName::GetShortName(PackageName);
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Name);
		if (!Mesh)
		{
			Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		Mesh->PreEditChange(nullptr);
		if (Mesh->GetNumSourceModels() != 1)
		{
			Mesh->SetNumSourceModels(1);
		}
		FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bUseMikkTSpace = true;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bRemoveDegenerates = true;
		Source.BuildSettings.DistanceFieldResolutionScale = 0.f;

		FMeshDescription* MD = Mesh->CreateMeshDescription(0);
		MD->Empty();
		FStaticMeshAttributes Attributes(*MD);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = MD->CreatePolygonGroup();
		SlotNames[Group] = TEXT("Terrain");

		const int32 NumVerts = 1 + NumA * NumR;
		MD->ReserveNewVertices(NumVerts);
		MD->ReserveNewVertexInstances(NumVerts);
		MD->ReserveNewTriangles(NumA * (2 * NumR - 1));
		TArray<FVertexInstanceID> Inst;
		Inst.SetNum(NumVerts);
		auto AddVertex = [&](int32 Index, float X, float Y, float Spacing)
		{
			const float Z = Height(X, Y);
			const float D = FMath::Max(Spacing, 500.f);
			const FVector Nrm = FVector(Height(X - D, Y) - Height(X + D, Y), Height(X, Y - D) - Height(X, Y + D), 2.f * D).GetSafeNormal();
			const FVertexID V = MD->CreateVertex();
			Positions[V] = FVector3f(X, Y, Z);
			const FVertexInstanceID VI = MD->CreateVertexInstance(V);
			Normals[VI] = FVector3f(Nrm);
			UVs.Set(VI, 0, FVector2f(X, Y) / 100000.f);
			Inst[Index] = VI;
		};
		AddVertex(0, 0.f, 0.f, R0);
		TArray<int32> RowStart;
		RowStart.SetNum(NumR);
		{
			TArray<FVector3f> Pts;
			Pts.SetNum(NumA * NumR);
			ParallelFor(NumR, [&](int32 i)
			{
				for (int32 a = 0; a < NumA; ++a)
				{
					const float Ang = 2.f * PI * a / NumA;
					Pts[i * NumA + a] = FVector3f(FMath::Cos(Ang) * Radii[i], FMath::Sin(Ang) * Radii[i], 0.f);
				}
			});
			for (int32 i = 0; i < NumR; ++i)
			{
				RowStart[i] = 1 + i * NumA;
				const float Spacing = i > 0 ? Radii[i] - Radii[i - 1] : R0;
				for (int32 a = 0; a < NumA; ++a)
				{
					AddVertex(RowStart[i] + a, Pts[i * NumA + a].X, Pts[i * NumA + a].Y, Spacing);
				}
			}
		}
		// Front faces point up: winding (C - A) x (B - A) along +Z.
		auto AddTri = [&](int32 A, int32 B, int32 C)
		{
			const FVector3f PA = Positions[MD->GetVertexInstanceVertex(Inst[A])];
			const FVector3f PB = Positions[MD->GetVertexInstanceVertex(Inst[B])];
			const FVector3f PC = Positions[MD->GetVertexInstanceVertex(Inst[C])];
			TArray<FVertexInstanceID, TFixedAllocator<3>> Corners;
			if (FVector3f::CrossProduct(PC - PA, PB - PA).Z >= 0.f)
			{
				Corners = { Inst[A], Inst[B], Inst[C] };
			}
			else
			{
				Corners = { Inst[A], Inst[C], Inst[B] };
			}
			MD->CreateTriangle(Group, Corners);
		};
		for (int32 a = 0; a < NumA; ++a)
		{
			AddTri(0, RowStart[0] + a, RowStart[0] + (a + 1) % NumA);
		}
		for (int32 i = 0; i + 1 < NumR; ++i)
		{
			for (int32 a = 0; a < NumA; ++a)
			{
				const int32 A0 = RowStart[i] + a, A1 = RowStart[i] + (a + 1) % NumA;
				const int32 B0 = RowStart[i + 1] + a, B1 = RowStart[i + 1] + (a + 1) % NumA;
				AddTri(A0, B0, B1);
				AddTri(A0, B1, A1);
			}
		}
		const int32 NumTris = MD->Triangles().Num();
		Mesh->CommitMeshDescription(0);
		Mesh->GetStaticMaterials().Reset();
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Terrain"), TEXT("Terrain")));
		Mesh->GetNaniteSettings().bEnabled = true;
		Mesh->GetNaniteSettings().FallbackRelativeError = 2.f;
		Mesh->Build(false);
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();
		SaveAsset(Mesh);
		UE_LOG(LogCampusBootstrap, Display, TEXT("Terrain built: %d vertices, %d triangles, bounds %s"), NumVerts, NumTris, *Mesh->GetBoundingBox().ToString());
	}

	void BuildPropMaterials()
	{
		UTexture* White = LoadQuiet<UTexture>(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		UTexture* FlatNormal = LoadQuiet<UTexture>(FString::Printf(TEXT("%s/T_Campus_Flat_N.T_Campus_Flat_N"), TextureFolder));
		UTexture* FlatMasks = LoadQuiet<UTexture>(FString::Printf(TEXT("%s/T_Campus_Flat_ARM.T_Campus_Flat_ARM"), TextureFolder));
		// Parameter names match the Interchange glTF masters so re-parented instances keep their values.
		for (const bool bFoliage : { false, true })
		{
			FMat B{ NewMaterial(bFoliage ? TEXT("M_Campus_PropFoliage") : TEXT("M_Campus_Prop")) };
			B.M->SetUsageByFlag(MATUSAGE_Nanite, true);
			B.M->TwoSided = true;
			if (bFoliage)
			{
				B.M->BlendMode = BLEND_Masked;
				B.M->SetShadingModel(MSM_TwoSidedFoliage);
				B.M->OpacityMaskClipValue = 0.4f;
			}
			UMaterialExpression* UV = B.Make<UMaterialExpressionTextureCoordinate>();
			UMaterialExpressionCustom* Xf = B.Custom(TEXT("return UV * float2(OS.z, OSA) + OS.xy;"), CMOT_Float2, { TEXT("UV"), TEXT("OS"), TEXT("OSA") });
			B.Link(UV, Xf, TEXT("UV"));
			UMaterialExpression* OffsetScale = B.Vec(TEXT("BaseColorTexture_OffsetScale"), FLinearColor(0.f, 0.f, 1.f, 1.f));
			B.Link(OffsetScale, Xf, TEXT("OS"));
			B.Link(OffsetScale, Xf, TEXT("OSA"), TEXT("A"));
			UMaterialExpression* Base = B.TexSample(TEXT("BaseColorTexture"), White, SAMPLERTYPE_Color, Xf);
			UMaterialExpression* Normal = B.TexSample(TEXT("NormalTexture"), FlatNormal, SAMPLERTYPE_Normal, Xf);
			UMaterialExpression* MR = B.TexSample(TEXT("MetallicRoughnessTexture"), FlatMasks, SAMPLERTYPE_Masks, Xf);

			UMaterialExpression* Color = B.Mul(Base, B.Mask(B.Vec(TEXT("BaseColorFactor"), FLinearColor::White), true, true, true));
			B.Out(Color, MP_BaseColor);
			B.Out(B.Mul(B.Mask(MR, false, true, false), B.Scalar(TEXT("RoughnessFactor"), 1.f)), MP_Roughness);
			B.Out(B.Mul(B.Mask(MR, false, false, true), B.Scalar(TEXT("MetallicFactor"), 1.f)), MP_Metallic);
			UMaterialExpressionCustom* NormalScale = B.Custom(TEXT("float3 n = N; n.xy *= S; return normalize(n);"), CMOT_Float3, { TEXT("N"), TEXT("S") });
			B.Link(Normal, NormalScale, TEXT("N"));
			B.Link(B.Scalar(TEXT("NormalScale"), 1.f), NormalScale, TEXT("S"));
			B.Out(NormalScale, MP_Normal);
			if (bFoliage)
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(Base, TEXT("A"), MP_OpacityMask);
				// Leaves pass a good share of the light: dense shrubs otherwise go near black inside and on the shaded side.
				B.Out(B.MulC(Color, 1.0f), MP_SubsurfaceColor);
				// Wind: everything bends downwind with height above its own base (a blade's tip most, a tree's crown a
				// few centimetres), in gusts that roll across the planting the way the wind goes, with a slower sway
				// across it and the leaves fluttering on top. A plant whose tip goes over also comes down a little.
				UMaterialExpressionCustom* Wind = B.Custom(TEXT(R"(
float2 dir = float2(WX, WY);
float s = WS;
float h = max(LP.z, 0.0);
// Up to ~6 cm at the tips: a grass blade goes right over, a crown's leaves (on branches that do not move) only stir.
float bend = min(h * 0.45 / (1.0 + h / 40.0), 8.0);
float phase = dot(WP.xy, float2(0.0071, 0.0053));
float gust = 0.5 + 0.5 * sin(dot(WP.xy, dir) * 0.004 - T * 1.9);
float sway = 0.6 + 0.4 * sin(T * 1.7 + phase * 6.2831);
float2 along = dir * bend * s * (0.35 + 0.65 * gust) * sway;
float2 across = float2(-dir.y, dir.x) * bend * s * 0.14 * sin(T * 2.3 + phase * 4.0);
float flutter = sin(T * 9.0 + dot(WP, float3(0.31, 0.27, 0.35))) * 0.8 * s * saturate(h / 30.0);
float3 o = float3(along + across, 0.0);
o += float3(dir.y, -dir.x, 0.7) * flutter;
o.z -= length(o.xy) * 0.3 * saturate(h / 60.0);
return o;
)"), CMOT_Float3, { TEXT("WP"), TEXT("LP"), TEXT("T"), TEXT("WX"), TEXT("WY"), TEXT("WS") });
				B.Link(B.Make<UMaterialExpressionWorldPosition>(), Wind, TEXT("WP"));
				UMaterialExpressionLocalPosition* LP = B.Make<UMaterialExpressionLocalPosition>();
				LP->LocalOrigin = ELocalPositionOrigin::Instance;
				B.Link(LP, Wind, TEXT("LP"));
				B.Link(B.Make<UMaterialExpressionTime>(), Wind, TEXT("T"));
				UMaterialParameterCollection* Env = CampusEditor::EnvironmentCollection();
				B.Link(B.Collection(Env, TEXT("WindX")), Wind, TEXT("WX"));
				B.Link(B.Collection(Env, TEXT("WindY")), Wind, TEXT("WY"));
				B.Link(B.Collection(Env, TEXT("WindStrength")), Wind, TEXT("WS"));
				B.Out(Wind, MP_WorldPositionOffset);
				// Nanite culls by the bounds swept by the offset.
				B.M->MaxWorldPositionOffsetDisplacement = 15.f;
			}
			FinishMaterial(B.M);
		}
	}

	void BuildSoldierMaterial()
	{
		UTexture* Grunge = LoadQuiet<UTexture>(FString::Printf(TEXT("%s/T_Campus_Grunge.T_Campus_Grunge"), TextureFolder));
		UTexture* Fabric = LoadQuiet<UTexture>(CampusSurfaces::TexturePath(TEXT("denim_fabric"), TEXT("N")));
		UTexture* Leather = LoadQuiet<UTexture>(CampusSurfaces::TexturePath(TEXT("fabric_leather_01"), TEXT("N")));
		UTexture* FlatNormal = LoadQuiet<UTexture>(FString::Printf(TEXT("%s/T_Campus_Flat_N.T_Campus_Flat_N"), TextureFolder));
		if (!Grunge)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("Soldier material needs T_Campus_Grunge"));
			return;
		}
		// Regions come from the mannequin's reference (A-) pose, mirrored on X; the model faces +Y.
		static const TCHAR* SoldierHLSL = TEXT(R"(
float3 q = float3(abs(P.x), P.y, P.z);
float3 elbow = float3(35.0, -1.9, 120.9);
float3 wrist = float3(47.8, 15.7, 104.5);
float3 d = normalize(wrist - elbow);
float along = dot(q - wrist, d);
// Arms, down to the fingertips (which hang below the hips in the A-pose but well outside the thighs).
float arm = saturate(step(17.0, q.x) * step(92.0, q.z) + step(25.0, q.x) * step(70.0, q.z) * step(q.z, 92.0));
float glove = arm * step(-4.0, along);
float cuff = arm * step(-9.0, along) * (1.0 - glove);
float boots = step(q.z, 15.0);
float head = step(149.0, q.z) * (1.0 - arm);
float belt = step(91.0, q.z) * step(q.z, 98.0) * (1.0 - arm);
float vest = step(101.0, q.z) * step(q.z, 143.0) * step(q.x, 18.5) * (1.0 - arm);
float knee = step(43.0, q.z) * step(q.z, 57.0) * step(3.0, q.y) * (1.0 - arm);

// Multicam-style layering: soft-edged blotches a few centimetres across, low contrast (reads as fabric up close).
float2 c1 = float2(q.x + q.y * 0.7, q.z) / 24.0;
float2 c2 = float2(q.y - q.x * 0.5, q.z * 0.9) / 15.0;
float n1 = Texture2DSample(GrungeTex, GrungeTexSampler, c1).r;
float n2 = Texture2DSample(GrungeTex, GrungeTexSampler, c2 + 0.37).r;
float n3 = Texture2DSample(GrungeTex, GrungeTexSampler, c1 * 2.1 + 0.71).g;
bool alpha = Team < 0.5;
float3 cA = alpha ? float3(0.34, 0.30, 0.22) : float3(0.24, 0.25, 0.26);
float3 cB = alpha ? float3(0.24, 0.18, 0.11) : float3(0.17, 0.18, 0.19);
float3 cC = alpha ? float3(0.23, 0.24, 0.15) : float3(0.31, 0.32, 0.33);
float3 cD = alpha ? float3(0.11, 0.095, 0.07) : float3(0.09, 0.09, 0.10);
float3 col = cA;
col = lerp(col, cB, smoothstep(0.56, 0.64, n1));
col = lerp(col, cC, smoothstep(0.55, 0.62, n2) * (1.0 - smoothstep(0.56, 0.64, n1)));
// Sparse dark branches, soft and low contrast so they don't read as spots on the sleeves up close.
col = lerp(col, cD, smoothstep(0.72, 0.84, n3 * 0.6 + n2 * 0.4) * 0.5);
float rough = 0.81 + (n3 - 0.5) * 0.10;
float3 vestCol = alpha ? float3(0.20, 0.18, 0.12) : float3(0.11, 0.12, 0.13);
col = lerp(col, vestCol * (0.85 + 0.3 * n2), vest);
col = lerp(col, float3(0.045, 0.045, 0.04), belt);
col = lerp(col, float3(0.06, 0.058, 0.055), knee);
col = lerp(col, float3(0.055, 0.045, 0.035), boots);
rough = lerp(rough, 0.6, boots);
col = lerp(col, float3(0.03, 0.03, 0.03), head);
// Charcoal-olive synthetic gloves with darker, slicker knuckle pads (light tan reads as bare skin in the sun).
float knuckle = glove * smoothstep(5.0, 8.0, along);
float3 gloveCol = lerp(float3(0.05, 0.052, 0.042), float3(0.022, 0.022, 0.02), knuckle * 0.85) * (0.85 + 0.3 * n3);
float palm = glove * smoothstep(0.0, 5.0, q.y - wrist.y) * (1.0 - knuckle);
gloveCol = lerp(gloveCol, gloveCol * 1.16, palm * smoothstep(0.4, 0.8, n2));
col = lerp(col, gloveCol, glove);
rough = lerp(rough, lerp(0.74, 0.46, knuckle) - palm * 0.06, glove);
col = lerp(col, float3(0.05, 0.045, 0.035), cuff);
col *= 1.0 - saturate((60.0 - q.z) / 60.0) * 0.18;
Rough = rough;
Weave = lerp(1.0, 0.45, saturate(glove + boots));
Synthetic = saturate(glove + boots + knee);
return col;
)");
		FMat B{ NewMaterial(TEXT("M_Campus_Soldier")) };
		UMaterialExpressionCustom* C = B.Custom(SoldierHLSL, CMOT_Float3, { TEXT("P"), TEXT("Team"), TEXT("GrungeTex") }, { { TEXT("Rough"), CMOT_Float1 }, { TEXT("Weave"), CMOT_Float1 }, { TEXT("Synthetic"), CMOT_Float1 } });
		B.Link(B.Interp(B.Make<UMaterialExpressionPreSkinnedPosition>()), C, TEXT("P"));
		B.Link(B.Scalar(TEXT("Team"), 0.f), C, TEXT("Team"));
		B.Link(B.TexObject(TEXT("GrungeTex"), Grunge, SAMPLERTYPE_LinearColor), C, TEXT("GrungeTex"));
		// per-character wetness survives shelter, while the mask remains a fallback for material previews
		UMaterialExpressionCustom* Wet = B.Custom(TEXT(R"(
float sky = 1.0;
if (MaskSize > 1.0)
{
	float2 mUV = float2((WP.y - MaskY) / MaskSize + 0.5, 0.5 - (WP.x - MaskX) / MaskSize);
	if (all(mUV > 0.0) && all(mUV < 1.0))
	{
		float mTop = MaskTop - Texture2DSampleLevel(MaskTex, MaskTexSampler, mUV, 0).r;
		sky = saturate((WP.z - (mTop - 60.0)) / 80.0);
	}
}
float soaked = ActorWetness >= 0.0 ? saturate(ActorWetness) : saturate(Wetness) * sky;
float w = soaked * lerp(0.75, 1.0, saturate(N.z));
// water fills the weave while synthetic pads keep a tighter highlight
float2 cell = floor(P.xz * 0.45);
float2 beadUV = frac(P.xz * 0.45) - 0.5;
float seed = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
float bead = (1.0 - smoothstep(0.06, 0.16, length(beadUV))) * step(0.72, seed) * w;
WetRough = lerp(R, min(R, lerp(0.25, 0.48, Weave)), w);
WetRough = lerp(WetRough, 0.18, bead * 0.65);
WetLevel = w;
WetSpec = lerp(0.5, 0.62, w);
float3 dark = Col * lerp(1.0, 0.6, w);
float l = dot(dark, float3(0.3, 0.59, 0.11));
return lerp(dark, l + (dark - l) * 1.25, w);
)"), CMOT_Float3, { TEXT("Col"), TEXT("R"), TEXT("WP"), TEXT("N"), TEXT("P"), TEXT("Weave"), TEXT("ActorWetness"), TEXT("Wetness"), TEXT("MaskTex"), TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop") }, { { TEXT("WetRough"), CMOT_Float1 }, { TEXT("WetLevel"), CMOT_Float1 }, { TEXT("WetSpec"), CMOT_Float1 } });
		B.Link(C, Wet, TEXT("Col"));
		B.Link(C, Wet, TEXT("R"), TEXT("Rough"));
		B.Link(B.Make<UMaterialExpressionWorldPosition>(), Wet, TEXT("WP"));
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), Wet, TEXT("N"));
		B.Link(B.Interp(B.Make<UMaterialExpressionPreSkinnedPosition>()), Wet, TEXT("P"));
		B.Link(C, Wet, TEXT("Weave"), TEXT("Weave"));
		B.Link(B.Scalar(TEXT("ActorWetness"), -1.f), Wet, TEXT("ActorWetness"));
		{
			UMaterialParameterCollection* Env = CampusEditor::EnvironmentCollection();
			B.Link(B.Collection(Env, TEXT("Wetness")), Wet, TEXT("Wetness"));
			for (const TCHAR* P : { TEXT("MaskX"), TEXT("MaskY"), TEXT("MaskSize"), TEXT("MaskTop") })
			{
				B.Link(B.Collection(Env, P), Wet, P);
			}
			UTexture* Mask = LoadQuiet<UTexture>(TEXT("/Game/Campus/Materials/RT_Campus_RainMask.RT_Campus_RainMask"));
			if (!Mask)
			{
				Mask = LoadQuiet<UTexture>(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			}
			B.Link(B.TexObject(TEXT("RainMask"), Mask, SAMPLERTYPE_LinearColor), Wet, TEXT("MaskTex"));
		}
		B.Out(Wet, MP_BaseColor, TEXT("return"));
		B.Out(Wet, MP_Roughness, TEXT("WetRough"));
		B.Out(Wet, MP_Specular, TEXT("WetSpec"));
		UMaterialExpressionTextureCoordinate* UV = B.Make<UMaterialExpressionTextureCoordinate>();
		UV->UTiling = 18.f;
		UV->VTiling = 18.f;
		UMaterialExpression* Weave = B.TexSample(TEXT("FabricNormal"), Fabric ? Fabric : FlatNormal, SAMPLERTYPE_Normal, UV);
		UMaterialExpressionTextureCoordinate* LeatherUV = B.Make<UMaterialExpressionTextureCoordinate>();
		LeatherUV->UTiling = 6.f;
		LeatherUV->VTiling = 6.f;
		UMaterialExpression* LeatherGrain = B.TexSample(TEXT("SyntheticNormal"), Leather ? Leather : FlatNormal, SAMPLERTYPE_Normal, LeatherUV);
		UMaterialExpressionCustom* N = B.Custom(TEXT("float3 n = lerp(F, L, Synthetic); return normalize(float3(n.xy * S * W * lerp(1.0, 0.6, Wet), n.z));"), CMOT_Float3,
			{ TEXT("F"), TEXT("L"), TEXT("S"), TEXT("W"), TEXT("Wet"), TEXT("Synthetic") });
		B.Link(Weave, N, TEXT("F"));
		B.Link(LeatherGrain, N, TEXT("L"));
		B.Link(C, N, TEXT("Synthetic"), TEXT("Synthetic"));
		B.Link(B.Scalar(TEXT("FabricStrength"), 0.65f), N, TEXT("S"));
		B.Link(C, N, TEXT("W"), TEXT("Weave"));
		B.Link(Wet, N, TEXT("Wet"), TEXT("WetLevel"));
		B.Out(N, MP_Normal);
		FinishMaterial(B.M);
	}

	void FixPropMaterials()
	{
		UMaterial* Opaque = LoadQuiet<UMaterial>(TEXT("/Game/Campus/Materials/M_Campus_Prop.M_Campus_Prop"));
		UMaterial* Foliage = LoadQuiet<UMaterial>(TEXT("/Game/Campus/Materials/M_Campus_PropFoliage.M_Campus_PropFoliage"));
		UCampusPropLibrary* Library = LoadQuiet<UCampusPropLibrary>(FString(UCampusPropLibrary::PackagePath) + TEXT(".DA_CampusProps"));
		if (!Opaque || !Foliage || !Library)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("Prop materials or library missing"));
			return;
		}
		TSet<UObject*> Seen;
		int32 Fixed = 0;
		for (const FCampusPropEntry& Entry : Library->Props)
		{
			for (UStaticMesh* Mesh : Entry.Meshes)
			{
				if (!Mesh)
				{
					continue;
				}
				for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
				{
					UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Slot.MaterialInterface);
					if (!MI || Seen.Contains(MI))
					{
						continue;
					}
					Seen.Add(MI);
					const FString ParentPath = GetPathNameSafe(MI->Parent);
					const bool bFoliage = MI->Parent == Foliage || ParentPath.Contains(TEXT("Blend")) || ParentPath.Contains(TEXT("Mask"));
					for (const FTextureParameterValue& V : MI->TextureParameterValues)
					{
						UTexture2D* Tex = Cast<UTexture2D>(V.ParameterValue);
						if (!Tex || Seen.Contains(Tex))
						{
							continue;
						}
						Seen.Add(Tex);
						const FString Param = V.ParameterInfo.Name.ToString();
						Tex->PreEditChange(nullptr);
						if (Param.Contains(TEXT("Normal")))
						{
							Tex->SRGB = false;
							Tex->CompressionSettings = TC_Normalmap;
							Tex->LODGroup = TEXTUREGROUP_WorldNormalMap;
							// glTF normal maps are OpenGL (+Y); the engine expects DirectX (-Y).
							if (Tex->GetName().Contains(TEXT("nor_gl")))
							{
								Tex->bFlipGreenChannel = true;
							}
						}
						else if (Param.Contains(TEXT("MetallicRoughness")) || Param.Contains(TEXT("Occlusion")))
						{
							Tex->SRGB = false;
							Tex->CompressionSettings = TC_Masks;
						}
						else
						{
							Tex->SRGB = true;
							Tex->CompressionSettings = TC_Default;
						}
						Tex->PostEditChange();
						SaveAsset(Tex);
					}
					MI->BasePropertyOverrides.bOverride_BlendMode = false;
					MI->BasePropertyOverrides.bOverride_ShadingModel = false;
					MI->BasePropertyOverrides.bOverride_TwoSided = false;
					MI->BasePropertyOverrides.bOverride_OpacityMaskClipValue = false;
					UMaterialEditingLibrary::SetMaterialInstanceParent(MI, bFoliage ? Foliage : Opaque);
					UMaterialEditingLibrary::UpdateMaterialInstance(MI);
					SaveAsset(MI);
					++Fixed;
				}
			}
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("Re-parented %d prop materials"), Fixed);
	}

	void ImportProps(bool bReimport)
	{
		const FString LibraryName = TEXT("DA_CampusProps");
		UPackage* LibraryPackage = CreatePackage(UCampusPropLibrary::PackagePath);
		LibraryPackage->FullyLoad();
		UCampusPropLibrary* Library = FindObject<UCampusPropLibrary>(LibraryPackage, *LibraryName);
		if (!Library)
		{
			Library = NewObject<UCampusPropLibrary>(LibraryPackage, *LibraryName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Library);
		}

		for (const FPropRule& Rule : PropRules)
		{
			const FString File = SourceRoot() / TEXT("Models") / Rule.Id / FString(Rule.Id) + TEXT(".gltf");
			const FString Folder = FString(PropFolder) / Rule.Id;
			FCampusPropEntry* Entry = Library->Props.FindByPredicate([&](const FCampusPropEntry& E) { return E.Id == FName(Rule.Id); });
			const bool bHave = Entry && Entry->Meshes.Num() > 0 && !Entry->Meshes.Contains(nullptr);
			if (bHave && !bReimport)
			{
				for (int32 i = 0; i < Entry->Meshes.Num(); ++i)
				{
					SetCollision(Entry->Meshes[i], Rule.Collision, Entry->Bounds, i == 0);
					SaveAsset(Entry->Meshes[i]);
				}
				Entry->Impact = Rule.Impact;
				continue;
			}
			if (!FPaths::FileExists(File))
			{
				UE_LOG(LogCampusBootstrap, Warning, TEXT("Missing model source %s (run Tools/FetchAssets.ps1)"), *File);
				continue;
			}

			UAssetImportTask* Task = MakeTask(File, Folder, FString());
			Task->bSave = true;
			FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });
			TArray<UStaticMesh*> Meshes;
			for (UObject* Object : Task->GetObjects())
			{
				if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
				{
					Meshes.AddUnique(Mesh);
				}
			}
			Task->RemoveFromRoot();
			if (Meshes.Num() == 0)
			{
				Meshes = FindStaticMeshes(Folder);
			}
			if (Meshes.Num() == 0)
			{
				UE_LOG(LogCampusBootstrap, Warning, TEXT("Prop %s: import produced no static mesh"), Rule.Id);
				continue;
			}
			Meshes.Sort([](const UStaticMesh& A, const UStaticMesh& Bm) { return A.GetBoundingBox().GetVolume() > Bm.GetBoundingBox().GetVolume(); });

			FBox Bounds(ForceInit);
			int32 Triangles = 0;
			for (UStaticMesh* Mesh : Meshes)
			{
				Bounds += Mesh->GetBoundingBox();
				Triangles += Mesh->GetNumTriangles(0);
			}
			for (int32 i = 0; i < Meshes.Num(); ++i)
			{
				SetCollision(Meshes[i], Rule.Collision, Bounds, i == 0);
				SaveAsset(Meshes[i]);
			}

			if (!Entry)
			{
				Entry = &Library->Props.AddDefaulted_GetRef();
			}
			Entry->Id = Rule.Id;
			Entry->Meshes.Reset();
			for (UStaticMesh* Mesh : Meshes)
			{
				Entry->Meshes.Add(Mesh);
			}
			Entry->Bounds = Bounds;
			Entry->Impact = Rule.Impact;
		}

		for (const FCampusPropEntry& Entry : Library->Props)
		{
			FString Names;
			for (const UStaticMesh* Mesh : Entry.Meshes)
			{
				Names += GetNameSafe(Mesh) + TEXT(" ");
			}
			UE_LOG(LogCampusBootstrap, Display, TEXT("Prop %-28s size %s min %s pieces [%s]"), *Entry.Id.ToString(), *Entry.Bounds.GetSize().ToCompactString(), *Entry.Bounds.Min.ToCompactString(), *Names);
		}
		SaveAsset(Library);
	}
}
