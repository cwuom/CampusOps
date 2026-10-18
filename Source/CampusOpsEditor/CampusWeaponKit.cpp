#include "CampusWeaponKit.h"
#include "CampusMeshBuilder.h"
#include "CampusEditorCommon.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Map/CampusSurfaces.h"

using namespace CampusEditor;
using CampusMesh::FBuilder;

namespace
{
	const TCHAR* KitRoot = TEXT("/Game/Campus/Weapons");

	/** Material slots shared by every weapon mesh (index = slot). */
	enum ESlot : int32
	{
		Anod, Poly, Steel, Wood, Bakelite, Rubber, Fde, Bright, Matte, Dark, Brass, Emitter, Stipple, GearTan, GearDark, GearShell, Marking, EyeRubber,
		GrenadeOD, GrenadeGray, MarkYellow, MarkRed, NumSlots
	};
	TArray<UMaterialInterface*> GSlots;

	FString PartPath(const TCHAR* Folder, const FString& Name)
	{
		return FString::Printf(TEXT("%s/%s/%s"), KitRoot, Folder, *Name);
	}

	// ------------------------------------------------------------------ textures

	struct FNoise
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

	uint8 Byte(float V)
	{
		return (uint8)FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255);
	}

	UTexture2D* SaveTexture(const FString& Name, int32 W, int32 H, const TArray<FColor>& Pixels, bool bSRGB, TextureCompressionSettings Compression, TextureGroup Group, TextureAddress Address = TA_Wrap)
	{
		const FString PackageName = FString(KitRoot) / TEXT("Textures") / Name;
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
		Tex->AddressX = Address;
		Tex->AddressY = Address;
		Tex->MipGenSettings = TMGS_FromTextureGroup;
		Tex->PostEditChange();
		SaveAsset(Tex);
		return Tex;
	}

	/** Plots an anti-aliased point into a wrapping float field. */
	void Splat(TArray<float>& F, int32 S, float X, float Y, float Value, bool bWrap = true)
	{
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float Fx = X - X0;
		const float Fy = Y - Y0;
		for (int32 Dy = 0; Dy < 2; ++Dy)
		{
			for (int32 Dx = 0; Dx < 2; ++Dx)
			{
				int32 Px = X0 + Dx;
				int32 Py = Y0 + Dy;
				if (bWrap)
				{
					Px = ((Px % S) + S) % S;
					Py = ((Py % S) + S) % S;
				}
				else if (Px < 0 || Py < 0 || Px >= S || Py >= S)
				{
					continue;
				}
				const float Wt = (Dx ? Fx : 1.f - Fx) * (Dy ? Fy : 1.f - Fy);
				float& Out = F[Py * S + Px];
				Out = FMath::Max(Out, Value * Wt * 1.6f);
			}
		}
	}

	void Line(TArray<float>& F, int32 S, FVector2D A, FVector2D B, float Value, bool bWrap = true)
	{
		const int32 Steps = FMath::Max(2, FMath::CeilToInt(FVector2D::Distance(A, B) * 1.5f));
		for (int32 k = 0; k <= Steps; ++k)
		{
			const FVector2D P = FMath::Lerp(A, B, (float)k / Steps);
			Splat(F, S, P.X, P.Y, Value, bWrap);
		}
	}

	/** Normal map from a wrapping height field. */
	TArray<FColor> HeightToNormal(const TArray<float>& Height, int32 S, float Strength)
	{
		TArray<FColor> Out;
		Out.SetNumUninitialized(S * S);
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				auto H = [&](int32 Px, int32 Py) { return Height[((Py % S + S) % S) * S + ((Px % S + S) % S)]; };
				const float Dx = (H(X + 1, Y) - H(X - 1, Y)) * Strength;
				const float Dy = (H(X, Y + 1) - H(X, Y - 1)) * Strength;
				const FVector N = FVector(-Dx, -Dy, 1.f).GetSafeNormal();
				Out[Y * S + X] = FColor(Byte(N.X * 0.5f + 0.5f), Byte(N.Y * 0.5f + 0.5f), Byte(N.Z * 0.5f + 0.5f), 255);
			}
		});
		return Out;
	}

	struct FKitTextures
	{
		UTexture2D* Grain = nullptr;
		UTexture2D* GrainN = nullptr;
		UTexture2D* StippleN = nullptr;
		UTexture2D* LensDirt = nullptr;
	};

	FKitTextures BuildTextures()
	{
		FKitTextures T;
		const int32 S = 1024;
		FNoise N1;
		N1.Seed = 11;
		FNoise N2;
		N2.Seed = 29;
		FNoise N3;
		N3.Seed = 53;

		// Scratches: short, mostly near-parallel strokes (handling, holster, sling wear), a few long gouges.
		TArray<float> Scratch;
		Scratch.SetNumZeroed(S * S);
		FRandomStream Rng(4242);
		for (int32 i = 0; i < 1100; ++i)
		{
			const FVector2D A(Rng.FRand() * S, Rng.FRand() * S);
			const float Ang = Rng.FRand() < 0.55f ? Rng.FRandRange(-0.35f, 0.35f) : Rng.FRandRange(0.f, PI);
			const float Len = i < 60 ? Rng.FRandRange(120.f, 380.f) : Rng.FRandRange(6.f, 90.f);
			const FVector2D B = A + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Len;
			Line(Scratch, S, A, B, Rng.FRandRange(0.25f, 1.f));
		}
		TArray<FColor> Grain;
		Grain.SetNumUninitialized(S * S);
		TArray<float> Height;
		Height.SetNumUninitialized(S * S);
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S;
				const float V = (Y + 0.5f) / S;
				const float Fine = N1.FBM(U, V, 64, 4, 0.55f);
				const float Blotch = FMath::Clamp((N2.FBM(U, V, 6, 5) - 0.5f) * 2.2f + 0.5f, 0.f, 1.f);
				const float Speck = FMath::Pow(FMath::Clamp(1.f - N3.Cells(U, V, 96) / 0.18f, 0.f, 1.f), 2.f);
				const float Sc = FMath::Min(Scratch[Y * S + X], 1.f);
				Grain[Y * S + X] = FColor(Byte(Fine), Byte(Sc), Byte(Blotch), Byte(Speck));
				Height[Y * S + X] = N1.FBM(U, V, 128, 3, 0.5f) * 0.6f + N2.FBM(U, V, 256, 2) * 0.4f - Sc * 0.9f;
			}
		});
		T.Grain = SaveTexture(TEXT("T_Weapon_Grain"), S, S, Grain, false, TC_Masks, TEXTUREGROUP_World);
		T.GrainN = SaveTexture(TEXT("T_Weapon_Grain_N"), S, S, HeightToNormal(Height, S, 2.2f), false, TC_Normalmap, TEXTUREGROUP_WorldNormalMap);

		// Stippling: dense round bumps (grips, rubber pads).
		TArray<float> Stip;
		Stip.SetNumUninitialized(S * S);
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S;
				const float V = (Y + 0.5f) / S;
				const float D = N3.Cells(U, V, 72);
				Stip[Y * S + X] = FMath::SmoothStep(0.55f, 0.f, D) + N1.Value(U, V, 256) * 0.15f;
			}
		});
		T.StippleN = SaveTexture(TEXT("T_Weapon_Stipple_N"), S, S, HeightToNormal(Stip, S, 6.f), false, TC_Normalmap, TEXTUREGROUP_WorldNormalMap);

		// Lens dirt (not tiled, spans the glass): R smudges / fingerprints / wipes, G dust and hairs, B fine scratches, A edge grime.
		TArray<float> Smudge;
		TArray<float> Dust;
		TArray<float> LensScratch;
		Smudge.SetNumZeroed(S * S);
		Dust.SetNumZeroed(S * S);
		LensScratch.SetNumZeroed(S * S);
		FRandomStream LR(77);
		struct FPrint { FVector2D C; float R; float Phase; float Ang; };
		TArray<FPrint> Prints;
		for (int32 i = 0; i < 5; ++i)
		{
			FPrint& P = Prints.AddDefaulted_GetRef();
			P.C = FVector2D(LR.FRandRange(0.15f, 0.85f), LR.FRandRange(0.15f, 0.85f)) * S;
			P.R = (float)LR.FRandRange(70.f, 170.f);
			P.Phase = (float)LR.FRandRange(0.f, 6.f);
			P.Ang = (float)LR.FRandRange(0.f, PI);
		}
		for (int32 i = 0; i < 900; ++i)
		{
			const FVector2D P(LR.FRand() * S, LR.FRand() * S);
			const float R = LR.FRand() < 0.92f ? LR.FRandRange(0.5f, 1.6f) : LR.FRandRange(2.f, 4.5f);
			const float Val = LR.FRandRange(0.3f, 1.f);
			for (float Dy = -R; Dy <= R; Dy += 0.5f)
			{
				for (float Dx = -R; Dx <= R; Dx += 0.5f)
				{
					if (Dx * Dx + Dy * Dy <= R * R)
					{
						Splat(Dust, S, P.X + Dx, P.Y + Dy, Val * (1.f - FMath::Sqrt(Dx * Dx + Dy * Dy) / (R + 0.5f)), false);
					}
				}
			}
		}
		for (int32 i = 0; i < 7; ++i)
		{
			FVector2D P(LR.FRand() * S, LR.FRand() * S);
			float Ang = LR.FRand() * 2.f * PI;
			for (int32 k = 0; k < 140; ++k)
			{
				const FVector2D Q = P + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * 1.5f;
				Line(Dust, S, P, Q, 0.7f, false);
				P = Q;
				Ang += LR.FRandRange(-0.08f, 0.08f);
			}
		}
		for (int32 i = 0; i < 26; ++i)
		{
			const FVector2D A(LR.FRand() * S, LR.FRand() * S);
			const float Ang = LR.FRand() * PI;
			const float Len = LR.FRandRange(30.f, 260.f);
			Line(LensScratch, S, A, A + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Len, LR.FRandRange(0.3f, 0.9f), false);
		}
		TArray<FColor> Lens;
		Lens.SetNumUninitialized(S * S);
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const FVector2D P(X + 0.5f, Y + 0.5f);
				const float U = P.X / S;
				const float V = P.Y / S;
				float Sm = 0.f;
				for (const FPrint& F : Prints)
				{
					const FVector2D D = P - F.C;
					const float Dist = D.Size();
					if (Dist < F.R)
					{
						// Ridges of a fingerprint: warped concentric arcs fading to the rim.
						const float Warp = N1.Value(U * 3.f, V * 3.f, 8) * 9.f;
						const float Ridge = 0.5f + 0.5f * FMath::Sin(Dist * 0.55f + Warp + F.Phase);
						const float Fade = FMath::SmoothStep(F.R, F.R * 0.35f, Dist);
						Sm = FMath::Max(Sm, Ridge * Fade * 0.75f);
					}
				}
				// Wipe marks: broad soft streaks.
				const float Wipe = FMath::Clamp((N2.FBM(U * 0.6f + V * 0.25f, V * 2.2f, 4, 4) - 0.55f) * 3.f, 0.f, 1.f) * 0.45f;
				Sm = FMath::Max(Sm, Wipe);
				const float R = FVector2D::Distance(FVector2D(U, V), FVector2D(0.5f, 0.5f));
				const float Edge = FMath::SmoothStep(0.36f, 0.5f, R) * (0.6f + 0.4f * N3.Value(U, V, 16));
				Lens[Y * S + X] = FColor(Byte(Sm), Byte(FMath::Min(Dust[Y * S + X], 1.f)), Byte(FMath::Min(LensScratch[Y * S + X], 1.f)), Byte(Edge));
			}
		});
		T.LensDirt = SaveTexture(TEXT("T_Lens_Dirt"), S, S, Lens, false, TC_Default, TEXTUREGROUP_World, TA_Clamp);
		return T;
	}

	// ------------------------------------------------------------------ materials

	const TCHAR* WeaponHLSL = TEXT(R"(
float2 uv = UV * DetailScale;
float4 g = Texture2DSample(GrainTex, GrainTexSampler, uv);
float4 gl = Texture2DSample(GrainTex, GrainTexSampler, UV * 0.31 + float2(0.21, 0.63));
float3 n1 = Texture2DSample(GrainNTex, GrainNTexSampler, uv).xyz * 2.0 - 1.0;
float3 n2 = Texture2DSample(StippleNTex, StippleNTexSampler, UV * StippleScale).xyz * 2.0 - 1.0;
float2 nxy = lerp(n1.xy * NormalStrength, n2.xy * 1.3, Stipple);
float3 col = BaseColor;
if (WoodAmount > 0.0)
{
	float3 w = Texture2DSample(WoodTex, WoodTexSampler, float2(UV.x * 0.9, UV.y * 0.18) * WoodScale).rgb;
	col = lerp(col, w * BaseColor * 1.8, WoodAmount);
}
col *= (0.96 + 0.08 * g.r) * lerp(0.97, 1.03, VC.b);
// Finish wear only breaks through on the sharpest, most handled edges, as sparse chips: a continuous bright edge
// reads as a toy outline at viewmodel distance.
float edge = saturate(VC.r);
float chip = smoothstep(0.58, 0.82, gl.b * 0.6 + g.r * 0.5);
float wear = saturate(edge * edge * chip) * Wear;
float scratch = saturate(g.g * 1.7 - 0.9) * Wear * 0.18 * saturate(edge * 0.7 + 0.3);
wear = saturate(max(wear, scratch));
col = lerp(col, col * 1.35 + 0.008, edge * 0.25 * (1.0 - wear));
float grime = saturate(VC.g * 0.9 + smoothstep(0.58, 0.95, gl.r) * 0.45) * Grime;
float up = saturate(NW.z);
float dust = saturate(up * 1.4 - 0.25) * smoothstep(0.35, 0.85, gl.g * 0.5 + g.a * 0.6 + gl.b * 0.3) * Dust;
col = lerp(col, col * 0.45, grime);
col = lerp(col, float3(0.32, 0.29, 0.24), dust * 0.5);
col = lerp(col, EdgeColor, wear);
float machining = (Texture2DSample(GrainTex, GrainTexSampler, float2(uv.x * 0.35, uv.y * 5.0)).r - 0.5) * 0.024 * (1.0 - Stipple) * saturate(Metallic + 0.2);
float handling = smoothstep(0.58, 0.9, gl.b) * (1.0 - VC.g) * 0.045;
float rough = saturate(Roughness * (0.94 + 0.12 * g.r) + grime * 0.12 + dust * 0.24 + machining - handling);
OutRough = lerp(rough, EdgeRough, wear);
OutMetal = lerp(Metallic, EdgeMetal, wear) * (1.0 - dust * 0.6);
OutNormal = normalize(float3(nxy * (1.0 - dust * 0.5), 1.0));
return col;
)");

	struct FSlotLook
	{
		const TCHAR* Name;
		FLinearColor Base;
		float Rough;
		float Metal;
		FLinearColor Edge;
		float EdgeMetal;
		float EdgeRough;
		float Wear;
		float Grime;
		float Dust;
		float DetailScale;
		float NormalStrength;
		float Stipple;
		float Wood;
		float Emissive;
	};

	void BuildMaterials(const FKitTextures& Tex)
	{
		UTexture* WoodD = LoadObject<UTexture>(nullptr, *CampusSurfaces::TexturePath(TEXT("wood_table_worn"), TEXT("D")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		FMat B{ NewMaterial(TEXT("M_Campus_Weapon"), FString(KitRoot) / TEXT("Materials")) };
		UMaterialExpressionCustom* C = B.Custom(WeaponHLSL, CMOT_Float3,
			{ TEXT("UV"), TEXT("VC"), TEXT("NW"), TEXT("BaseColor"), TEXT("Roughness"), TEXT("Metallic"), TEXT("EdgeColor"), TEXT("EdgeMetal"), TEXT("EdgeRough"),
			  TEXT("Wear"), TEXT("Grime"), TEXT("Dust"), TEXT("DetailScale"), TEXT("NormalStrength"), TEXT("Stipple"), TEXT("StippleScale"), TEXT("WoodAmount"), TEXT("WoodScale"),
			  TEXT("GrainTex"), TEXT("GrainNTex"), TEXT("StippleNTex"), TEXT("WoodTex") },
			{ { TEXT("OutRough"), CMOT_Float1 }, { TEXT("OutMetal"), CMOT_Float1 }, { TEXT("OutNormal"), CMOT_Float3 } });
		B.Link(B.Make<UMaterialExpressionTextureCoordinate>(), C, TEXT("UV"));
		B.Link(B.Make<UMaterialExpressionVertexColor>(), C, TEXT("VC"));
		B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), C, TEXT("NW"));
		B.Link(B.Vec(TEXT("BaseColor"), FLinearColor(0.03f, 0.03f, 0.033f)), C, TEXT("BaseColor"));
		B.Link(B.Vec(TEXT("EdgeColor"), FLinearColor(0.5f, 0.5f, 0.52f)), C, TEXT("EdgeColor"));
		const TPair<const TCHAR*, float> Scalars[] = {
			{ TEXT("Roughness"), 0.45f }, { TEXT("Metallic"), 0.6f }, { TEXT("EdgeMetal"), 1.f }, { TEXT("EdgeRough"), 0.3f }, { TEXT("Wear"), 0.5f },
			{ TEXT("Grime"), 0.5f }, { TEXT("Dust"), 0.35f }, { TEXT("DetailScale"), 4.f }, { TEXT("NormalStrength"), 0.4f }, { TEXT("Stipple"), 0.f },
			{ TEXT("StippleScale"), 7.f }, { TEXT("WoodAmount"), 0.f }, { TEXT("WoodScale"), 1.f } };
		for (const TPair<const TCHAR*, float>& S : Scalars)
		{
			B.Link(B.Scalar(S.Key, S.Value), C, S.Key);
		}
		B.Link(B.TexObject(TEXT("GrainTex"), Tex.Grain, SAMPLERTYPE_Masks), C, TEXT("GrainTex"));
		B.Link(B.TexObject(TEXT("GrainNTex"), Tex.GrainN, SAMPLERTYPE_Normal), C, TEXT("GrainNTex"));
		B.Link(B.TexObject(TEXT("StippleNTex"), Tex.StippleN, SAMPLERTYPE_Normal), C, TEXT("StippleNTex"));
		B.Link(B.TexObject(TEXT("WoodTex"), WoodD ? WoodD : Tex.Grain, WoodD ? SAMPLERTYPE_Color : SAMPLERTYPE_Masks), C, TEXT("WoodTex"));
		B.Out(C, MP_BaseColor, TEXT("return"));
		B.Out(C, MP_Roughness, TEXT("OutRough"));
		B.Out(C, MP_Metallic, TEXT("OutMetal"));
		B.Out(C, MP_Normal, TEXT("OutNormal"));
		// Emissive slot (laser emitters, fibre optics), physical units: ~400 nits per unit.
		B.Out(B.MulC(B.Mul(B.Vec(TEXT("EmissiveColor"), FLinearColor(1.f, 0.05f, 0.02f)), B.Scalar(TEXT("Emissive"), 0.f)), 400.f), MP_EmissiveColor);
		B.M->SetUsageByFlag(MATUSAGE_StaticMesh, true);
		FinishMaterial(B.M);

		const FSlotLook Looks[NumSlots] = {
			// Anodising, phosphate, Cerakote and polymer are dielectric coatings: dark, satin, and only the chips are metal.
			{ TEXT("Anodized"), FLinearColor(0.022f, 0.023f, 0.025f), 0.44f, 0.06f, FLinearColor(0.34f, 0.345f, 0.36f), 1.f, 0.36f, 0.5f, 0.5f, 0.15f, 4.f, 0.4f, 0.f, 0.f, 0.f },
			{ TEXT("Polymer"), FLinearColor(0.02f, 0.02f, 0.021f), 0.58f, 0.f, FLinearColor(0.05f, 0.05f, 0.05f), 0.f, 0.48f, 0.3f, 0.6f, 0.18f, 6.f, 0.6f, 0.f, 0.f, 0.f },
			{ TEXT("Steel"), FLinearColor(0.03f, 0.031f, 0.033f), 0.55f, 0.3f, FLinearColor(0.42f, 0.42f, 0.43f), 1.f, 0.32f, 0.5f, 0.6f, 0.15f, 5.f, 0.6f, 0.f, 0.f, 0.f },
			{ TEXT("Wood"), FLinearColor(0.20f, 0.085f, 0.04f), 0.48f, 0.f, FLinearColor(0.30f, 0.17f, 0.09f), 0.f, 0.62f, 0.5f, 0.55f, 0.3f, 1.f, 0.3f, 0.f, 1.f, 0.f },
			{ TEXT("Bakelite"), FLinearColor(0.09f, 0.026f, 0.015f), 0.48f, 0.f, FLinearColor(0.18f, 0.075f, 0.042f), 0.f, 0.55f, 0.35f, 0.5f, 0.3f, 6.f, 0.4f, 0.f, 0.f, 0.f },
			{ TEXT("Rubber"), FLinearColor(0.022f, 0.022f, 0.022f), 0.86f, 0.f, FLinearColor(0.04f, 0.04f, 0.04f), 0.f, 0.9f, 0.15f, 0.6f, 0.5f, 3.f, 0.8f, 0.7f, 0.f, 0.f },
			{ TEXT("FDE"), FLinearColor(0.27f, 0.21f, 0.135f), 0.62f, 0.f, FLinearColor(0.36f, 0.34f, 0.3f), 0.6f, 0.4f, 0.45f, 0.55f, 0.45f, 5.f, 0.4f, 0.f, 0.f, 0.f },
			{ TEXT("Bright"), FLinearColor(0.52f, 0.52f, 0.53f), 0.3f, 1.f, FLinearColor(0.72f, 0.72f, 0.72f), 1.f, 0.22f, 0.3f, 0.55f, 0.1f, 6.f, 0.3f, 0.f, 0.f, 0.f },
			{ TEXT("Matte"), FLinearColor(0.021f, 0.022f, 0.024f), 0.5f, 0.04f, FLinearColor(0.3f, 0.3f, 0.32f), 1.f, 0.38f, 0.35f, 0.4f, 0.15f, 5.f, 0.35f, 0.f, 0.f, 0.f },
			{ TEXT("Dark"), FLinearColor(0.006f, 0.006f, 0.006f), 0.8f, 0.f, FLinearColor(0.006f, 0.006f, 0.006f), 0.f, 0.8f, 0.f, 0.f, 0.f, 4.f, 0.2f, 0.f, 0.f, 0.f },
			{ TEXT("Brass"), FLinearColor(0.78f, 0.56f, 0.24f), 0.3f, 1.f, FLinearColor(0.88f, 0.68f, 0.36f), 1.f, 0.24f, 0.2f, 0.25f, 0.1f, 8.f, 0.2f, 0.f, 0.f, 0.f },
			{ TEXT("Emitter"), FLinearColor(0.2f, 0.01f, 0.008f), 0.1f, 0.f, FLinearColor(0.2f, 0.01f, 0.008f), 0.f, 0.1f, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f, 0.f, 4.f },
			{ TEXT("Stippled"), FLinearColor(0.027f, 0.027f, 0.028f), 0.78f, 0.f, FLinearColor(0.07f, 0.07f, 0.07f), 0.f, 0.6f, 0.25f, 0.6f, 0.45f, 6.f, 0.5f, 1.f, 0.f, 0.f },
			// Load-bearing gear: 500D Cordura (the stipple normal at a fine scale reads as the weave), painted helmet shell.
			{ TEXT("GearTan"), FLinearColor(0.15f, 0.118f, 0.078f), 0.9f, 0.f, FLinearColor(0.19f, 0.155f, 0.11f), 0.f, 0.9f, 0.3f, 0.6f, 0.5f, 9.f, 0.6f, 1.f, 0.f, 0.f },
			{ TEXT("GearDark"), FLinearColor(0.03f, 0.031f, 0.03f), 0.88f, 0.f, FLinearColor(0.06f, 0.06f, 0.058f), 0.f, 0.88f, 0.25f, 0.45f, 0.5f, 9.f, 0.6f, 1.f, 0.f, 0.f },
			{ TEXT("GearShell"), FLinearColor(0.14f, 0.12f, 0.09f), 0.7f, 0.f, FLinearColor(0.21f, 0.19f, 0.16f), 0.f, 0.62f, 0.4f, 0.5f, 0.4f, 4.f, 0.35f, 0.f, 0.f, 0.f },
			// Paint-filled engraving (turret ticks, index lines): off-white enamel, no wear.
			{ TEXT("Marking"), FLinearColor(0.56f, 0.56f, 0.53f), 0.55f, 0.f, FLinearColor(0.56f, 0.56f, 0.53f), 0.f, 0.55f, 0.f, 0.25f, 0.1f, 4.f, 0.1f, 0.f, 0.f, 0.f },
			// Optic eyecups: the ring framing the sight picture at ADS, kept clean black (no grip dust). Matte: its inner
			// cone is seen at grazing angles a few cm from the eye, where any sheen mirrors the sky as a blotchy grey band.
			{ TEXT("EyeRubber"), FLinearColor(0.011f, 0.011f, 0.012f), 0.9f, 0.f, FLinearColor(0.02f, 0.02f, 0.02f), 0.f, 0.9f, 0.f, 0.f, 0.f, 10.f, 0.03f, 0.f, 0.f, 0.f },
			// Grenade paint: olive drab and grey-green enamel over steel, chipped to bright metal at the edges, and stencil bands.
			{ TEXT("GrenadeOD"), FLinearColor(0.052f, 0.058f, 0.03f), 0.56f, 0.f, FLinearColor(0.34f, 0.34f, 0.35f), 1.f, 0.34f, 0.6f, 0.6f, 0.3f, 5.f, 0.5f, 0.f, 0.f, 0.f },
			{ TEXT("GrenadeGray"), FLinearColor(0.10f, 0.108f, 0.094f), 0.6f, 0.f, FLinearColor(0.34f, 0.34f, 0.35f), 1.f, 0.34f, 0.55f, 0.6f, 0.3f, 5.f, 0.45f, 0.f, 0.f, 0.f },
			{ TEXT("MarkYellow"), FLinearColor(0.56f, 0.40f, 0.04f), 0.55f, 0.f, FLinearColor(0.3f, 0.3f, 0.3f), 0.5f, 0.45f, 0.3f, 0.5f, 0.2f, 4.f, 0.2f, 0.f, 0.f, 0.f },
			{ TEXT("MarkRed"), FLinearColor(0.32f, 0.03f, 0.02f), 0.55f, 0.f, FLinearColor(0.3f, 0.3f, 0.3f), 0.5f, 0.45f, 0.3f, 0.5f, 0.2f, 4.f, 0.2f, 0.f, 0.f, 0.f },
		};
		GSlots.Reset();
		for (const FSlotLook& L : Looks)
		{
			const FString Name = FString(TEXT("MI_Weapon_")) + L.Name;
			const FString PackageName = FString(KitRoot) / TEXT("Materials") / Name;
			UPackage* Package = CreatePackage(*PackageName);
			Package->FullyLoad();
			UMaterialInstanceConstant* MI = FindObject<UMaterialInstanceConstant>(Package, *Name);
			if (!MI)
			{
				MI = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(MI);
			}
			UMaterialEditingLibrary::SetMaterialInstanceParent(MI, B.M);
			UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, TEXT("BaseColor"), L.Base);
			UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, TEXT("EdgeColor"), L.Edge);
			if (L.Emissive > 0.f)
			{
				UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, TEXT("EmissiveColor"), FLinearColor(1.f, 0.04f, 0.02f));
			}
			const TPair<const TCHAR*, float> Values[] = {
				{ TEXT("Roughness"), L.Rough }, { TEXT("Metallic"), L.Metal }, { TEXT("EdgeMetal"), L.EdgeMetal }, { TEXT("EdgeRough"), L.EdgeRough },
				{ TEXT("Wear"), L.Wear }, { TEXT("Grime"), L.Grime }, { TEXT("Dust"), L.Dust }, { TEXT("DetailScale"), L.DetailScale },
				{ TEXT("NormalStrength"), L.NormalStrength }, { TEXT("Stipple"), L.Stipple }, { TEXT("WoodAmount"), L.Wood }, { TEXT("Emissive"), L.Emissive } };
			for (const TPair<const TCHAR*, float>& V : Values)
			{
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, V.Key, V.Value);
			}
			UMaterialEditingLibrary::UpdateMaterialInstance(MI);
			SaveAsset(MI);
			GSlots.Add(MI);
		}
	}

	// ------------------------------------------------------------------ modelling helpers

	FTransform AlongY(const FVector& P) { return FTransform(FQuat(FVector::ZAxisVector, HALF_PI), P); }
	FTransform AlongZ(const FVector& P) { return FTransform(FQuat(FVector::YAxisVector, -HALF_PI), P); }

	/** Profile drawn in the YZ plane, extruded along X. */
	void PrismX(FBuilder& B, const TArray<FVector2D>& ProfileYZ, float X0, float X1, float Bevel, float Smooth = 28.f, const FVector& Offset = FVector::ZeroVector)
	{
		B.Prism(ProfileYZ, -X1, -X0, Bevel, Smooth, FTransform(FQuat(FVector::ZAxisVector, HALF_PI), Offset));
	}

	enum class EFace : uint8 { Up, Down, Right, Left };

	FQuat FaceQuat(EFace Face)
	{
		switch (Face)
		{
		case EFace::Down: return FQuat(FVector::XAxisVector, PI);
		case EFace::Right: return FQuat(FVector::XAxisVector, -HALF_PI);
		case EFace::Left: return FQuat(FVector::XAxisVector, HALF_PI);
		default: return FQuat::Identity;
		}
	}

	void Rail(FBuilder& B, const FVector& Start, float Length, EFace Face)
	{
		B.SetFrame(FTransform(FaceQuat(Face), Start));
		B.Picatinny(FVector::ZeroVector, Length);
		B.ResetFrame();
	}

	/** Cylinder along Y (pins, buttons, turret caps). */
	void Pin(FBuilder& B, const FVector& Center, float Radius, float Length, int32 Segments = 14)
	{
		B.Tube(-Length * 0.5f, Length * 0.5f, Radius, Segments, AlongY(Center), FMath::Min(0.06f, Radius * 0.3f));
	}

	/** Box rotated about X (around the bore), e.g. octagonal handguard panels and flash-hider prongs. */
	void BoxRolled(FBuilder& B, const FVector& Center, const FVector& Size, float RollRad, float Bevel)
	{
		B.SetFrame(FTransform(FQuat(FVector::XAxisVector, RollRad), Center));
		B.Box(FVector::ZeroVector, Size, Bevel);
		B.ResetFrame();
	}

	/** Knurling / grip ribs: Count ribs standing on radius R around T's X axis, between X0 and X1. */
	void Knurl(FBuilder& B, const FTransform& T, float X0, float X1, float R, int32 Count, float Depth = 0.1f, float Width = 0.14f, float PhaseRad = 0.f)
	{
		for (int32 k = 0; k < Count; ++k)
		{
			const float Ang = PhaseRad + 2.f * PI * k / Count;
			const FTransform Local(FQuat(FVector::XAxisVector, -Ang), FVector((X0 + X1) * 0.5f, FMath::Sin(Ang) * R, FMath::Cos(Ang) * R));
			B.SetFrame(Local * T);
			B.Box(FVector::ZeroVector, FVector(X1 - X0, Width, Depth * 2.f), FMath::Min(0.03f, Width * 0.25f));
		}
		B.ResetFrame();
	}

	/**
	 * Capped target turret standing on a scope tube along T's X axis (Base = distance of the saddle surface from the
	 * bore): saddle boss, zero-stop collar, knurled cap with paint-filled ticks and index line.
	 */
	void Turret(FBuilder& B, const FTransform& T, float Base, float Height, float Radius, int32 Ribs)
	{
		const float R = Radius;
		B.SetSlot(Anod);
		B.Lathe({ FVector2D(Base - 0.6f, R * 0.8f), FVector2D(Base + 0.3f, R * 0.8f), FVector2D(Base + 0.34f, R * 0.9f), FVector2D(Base + 0.52f, R * 0.9f),
			FVector2D(Base + 0.56f, R * 0.84f), FVector2D(Base + 0.62f, R), FVector2D(Base + Height - 0.16f, R), FVector2D(Base + Height, R - 0.16f), FVector2D(Base + Height, 0.f) },
			40, T, 30.f);
		Knurl(B, T, Base + 0.78f, Base + Height - 0.26f, R, Ribs, 0.07f, 0.13f);
		B.SetSlot(Marking);
		Knurl(B, T, Base + 0.63f, Base + 0.74f, R, 20, 0.012f, 0.045f);
		B.SetFrame(T);
		B.Box(FVector(Base + Height + 0.005f, 0.f, R * 0.42f), FVector(0.03f, 0.1f, R * 0.7f), 0.f);
		B.ResetFrame();
		B.SetSlot(Dark);
		B.Tube(Base + Height - 0.02f, Base + Height + 0.01f, R * 0.28f, 16, T, 0.f);
	}

	/** A pistol grip outline extruded to Width; Shift moves it along X. */
	void GripPrism(FBuilder& B, const TArray<FVector2D>& Profile, float HalfWidth, float Bevel, float ShiftX = 0.f)
	{
		TArray<FVector2D> P = Profile;
		for (FVector2D& V : P)
		{
			V.X += ShiftX;
		}
		B.Prism(P, -HalfWidth, HalfWidth, Bevel, 40.f);
	}

	/** A magazine body from a side outline (frame space), with ribs, a floor plate and the top cartridges. */
	void Magazine(FBuilder& B, const FVector& Top, const TArray<FVector2D>& Outline, float HalfWidth, int32 BodySlot, float Bevel, float CartridgeRadius, float CartridgeLength, bool bRibs)
	{
		TArray<FVector2D> P = Outline;
		for (FVector2D& V : P)
		{
			V += FVector2D(Top.X, Top.Z);
		}
		B.SetSlot(BodySlot);
		B.Prism(P, -HalfWidth, HalfWidth, Bevel, 30.f);
		if (bRibs)
		{
			// Grip ribs low on both sides, spanning the middle of the body at their height.
			float MinZ = 0.f;
			for (const FVector2D& V : P)
			{
				MinZ = FMath::Min(MinZ, V.Y);
			}
			for (int32 i = 0; i < 3; ++i)
			{
				const float Z = FMath::Lerp(Top.Z, MinZ, 0.68f + i * 0.06f);
				float X0 = FLT_MAX;
				float X1 = -FLT_MAX;
				for (int32 k = 0; k < P.Num(); ++k)
				{
					const FVector2D& A = P[k];
					const FVector2D& C = P[(k + 1) % P.Num()];
					if ((A.Y - Z) * (C.Y - Z) <= 0.f && !FMath::IsNearlyEqual(A.Y, C.Y))
					{
						const float X = FMath::Lerp(A.X, C.X, (Z - A.Y) / (C.Y - A.Y));
						X0 = FMath::Min(X0, X);
						X1 = FMath::Max(X1, X);
					}
				}
				if (X1 > X0)
				{
					for (const float S : { -1.f, 1.f })
					{
						B.Box(FVector((X0 + X1) * 0.5f, S * (HalfWidth + 0.03f), Z), FVector((X1 - X0) * 0.72f, 0.08f, 0.22f), 0.03f);
					}
				}
			}
		}
		// Top rounds (seen when the magazine is out).
		B.SetSlot(Brass);
		const float Len = CartridgeLength;
		const float R = CartridgeRadius;
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector C(Top.X - Len * 0.45f + i * 0.25f, (i ? -0.32f : 0.32f) * R * 2.f, Top.Z + 0.25f - i * R * 1.5f);
			B.Lathe({ FVector2D(0.f, R), FVector2D(Len * 0.62f, R), FVector2D(Len * 0.7f, R * 0.62f), FVector2D(Len * 0.78f, R * 0.6f),
				FVector2D(Len * 0.92f, R * 0.45f), FVector2D(Len, R * 0.08f) }, 12, FTransform(C));
		}
	}

	FVector2D Mirror(const FVector2D& V) { return FVector2D(-V.X, V.Y); }

	void SaveKit(FBuilder& B, const TCHAR* Folder, const FString& Name)
	{
		B.Save(PartPath(Folder, Name), GSlots, false);
	}

	/** Flip-up sight on a rail: Rear = aperture, else front post; Up = deployed. */
	void FlipSight(FBuilder& B, float X, float RailTop, float SightLine, bool bRear, bool bUp)
	{
		B.SetSlot(Anod);
		B.Box(FVector(X, 0.f, RailTop + 0.3f), FVector(2.4f, 2.35f, 0.6f), 0.14f);
		B.Box(FVector(X, 1.18f, RailTop - 0.2f), FVector(2.0f, 0.3f, 0.95f), 0.08f);
		B.Box(FVector(X, -1.18f, RailTop - 0.2f), FVector(2.0f, 0.3f, 0.95f), 0.08f);
		B.SetSlot(Steel);
		Pin(B, FVector(X + 0.6f, 0.f, RailTop - 0.3f), 0.22f, 2.9f, 8);
		B.SetSlot(Anod);
		if (!bUp)
		{
			B.Box(FVector(X + (bRear ? 0.35f : -0.35f), 0.f, RailTop + 0.78f), FVector(1.9f, 1.45f, 0.36f), 0.1f);
			return;
		}
		const float H = SightLine - RailTop;
		// Protective ears: thin plates, tapering toward the top.
		for (const float S : { -1.f, 1.f })
		{
			B.Prism({ { X - 0.35f, RailTop + 0.55f }, { X + 0.7f, RailTop + 0.55f }, { X + 0.3f, SightLine + 0.35f }, { X - 0.15f, SightLine + 0.35f } }, S * 0.82f, S * 1.0f, 0.05f);
		}
		if (bRear)
		{
			// Thin aperture leaf with the large (CQB) 5.5 mm aperture: a ghost ring at cheek weld.
			B.Box(FVector(X, 0.f, (RailTop + 0.6f + SightLine - 0.5f) * 0.5f), FVector(0.14f, 0.7f, SightLine - 0.5f - RailTop - 0.6f), 0.04f);
			B.Lathe({ FVector2D(-0.07f, 0.275f), FVector2D(-0.07f, 0.52f), FVector2D(0.07f, 0.52f), FVector2D(0.07f, 0.275f), FVector2D(-0.07f, 0.275f) }, 28, FTransform(FVector(X, 0.f, SightLine)));
		}
		else
		{
			B.Box(FVector(X, 0.f, RailTop + 0.75f), FVector(0.7f, 0.7f, 0.3f), 0.08f);
			B.SetSlot(Steel);
			B.Box(FVector(X, 0.f, (RailTop + 0.9f + SightLine) * 0.5f), FVector(0.22f, 0.22f, SightLine - RailTop - 0.9f), 0.03f);
		}
	}

	// ------------------------------------------------------------------ M4A1

	const TArray<FVector2D> A2Grip = {
		{ 4.0f, 4.5f }, { 0.0f, 4.5f }, { -0.6f, 3.6f }, { -1.4f, 0.6f }, { -2.3f, -3.0f }, { -3.0f, -6.2f }, { -2.7f, -7.2f }, { -1.9f, -7.5f },
		{ -0.3f, -7.3f }, { 0.1f, -6.4f }, { 0.2f, -4.6f }, { 0.75f, -3.5f }, { 0.6f, -2.7f }, { 1.0f, -1.0f }, { 2.4f, 2.0f }, { 3.6f, 3.9f } };

	const TArray<FVector2D> StanagOutline = {
		{ 3.3f, 0.f }, { 3.3f, -5.f }, { 3.6f, -9.f }, { 4.2f, -13.f }, { 5.0f, -16.8f }, { 5.7f, -19.f }, { 5.9f, -19.3f }, { 5.9f, -20.1f },
		{ -0.9f, -20.6f }, { -1.1f, -19.8f }, { -1.4f, -18.6f }, { -2.4f, -13.f }, { -3.0f, -9.f }, { -3.3f, -5.f }, { -3.3f, 0.f } };

	TArray<FVector2D> StretchOutline(const TArray<FVector2D>& Outline, float ScaleZ, float ExtraCurve)
	{
		TArray<FVector2D> Out = Outline;
		float MinZ = 0.f;
		for (const FVector2D& V : Outline)
		{
			MinZ = FMath::Min(MinZ, V.Y);
		}
		for (FVector2D& V : Out)
		{
			const float T = MinZ < 0.f ? V.Y / MinZ : 0.f;
			V.Y *= ScaleZ;
			V.X += ExtraCurve * T * T;
		}
		return Out;
	}

	void BuildM4()
	{
		const TCHAR* W = TEXT("M4A1");
		{
			FBuilder B;
			B.SetSlot(Anod);
			// Upper receiver and its flat-top rail.
			B.Prism({ { -1.8f, 7.3f }, { 17.6f, 7.3f }, { 17.6f, 8.1f }, { 18.1f, 8.4f }, { 18.1f, 11.1f }, { 17.6f, 11.6f }, { -1.0f, 11.6f }, { -1.8f, 10.9f } }, -1.15f, 1.15f, 0.16f);
			Rail(B, FVector(-1.0f, 0.f, 11.6f), 18.4f, EFace::Up);
			// Forward assist and brass deflector on the right.
			B.Box(FVector(0.6f, 1.25f, 10.0f), FVector(2.6f, 0.6f, 1.4f), 0.2f);
			B.Tube(-1.4f, 1.8f, 0.55f, 16, FTransform(FVector(0.f, 1.6f, 10.0f)));
			B.Tube(-1.95f, -1.3f, 0.64f, 16, FTransform(FVector(0.f, 1.6f, 10.0f)));
			B.Box(FVector(5.4f, 1.35f, 10.3f), FVector(1.1f, 0.5f, 1.7f), 0.22f);
			// Ejection port (open) with its cover hanging below.
			B.SetSlot(Dark);
			B.SetCavity(1.f);
			B.Box(FVector(10.2f, 1.165f, 9.6f), FVector(6.3f, 0.03f, 1.7f), 0.f);
			B.SetCavity(0.f);
			B.SetSlot(Anod);
			B.Box(FVector(10.2f, 1.45f, 7.95f), FVector(6.4f, 0.14f, 1.2f), 0.05f, FRotator(0.f, 0.f, 30.f));
			// Lower receiver, flared magwell, buffer tower.
			B.Prism({ { -1.8f, 7.3f }, { 17.2f, 7.3f }, { 17.2f, 6.0f }, { 16.2f, 5.0f }, { 16.2f, 4.4f }, { 8.6f, 4.4f }, { 8.6f, 4.9f }, { 4.0f, 4.9f }, { 3.6f, 4.5f },
				{ 0.0f, 4.5f }, { -0.6f, 5.2f }, { -1.8f, 6.2f } }, -1.15f, 1.15f, 0.16f);
			B.Prism({ { 8.8f, 4.6f }, { 16.6f, 4.6f }, { 16.9f, 1.2f }, { 16.5f, 0.8f }, { 9.2f, 0.8f }, { 8.7f, 1.3f } }, -1.45f, 1.45f, 0.22f);
			B.Box(FVector(-1.3f, 0.f, 8.6f), FVector(1.2f, 2.3f, 2.4f), 0.2f);
			// Pins, bolt catch, magazine release, selector.
			B.SetSlot(Steel);
			Pin(B, FVector(16.4f, 0.f, 6.5f), 0.32f, 2.6f);
			Pin(B, FVector(-0.6f, 0.f, 6.6f), 0.32f, 2.6f);
			Pin(B, FVector(7.3f, 1.4f, 5.5f), 0.45f, 0.5f);
			Pin(B, FVector(3.0f, -1.3f, 5.9f), 0.5f, 0.35f);
			B.Box(FVector(2.2f, -1.45f, 6.05f), FVector(1.6f, 0.2f, 0.45f), 0.08f, FRotator(-15.f, 0.f, 0.f));
			B.SetSlot(Anod);
			B.Box(FVector(8.0f, -1.3f, 6.1f), FVector(1.6f, 0.35f, 1.0f), 0.1f);
			// Trigger guard and trigger.
			B.Box(FVector(8.3f, 0.f, 3.6f), FVector(0.5f, 0.9f, 1.8f), 0.12f);
			B.Box(FVector(6.1f, 0.f, 2.55f), FVector(4.9f, 0.9f, 0.42f), 0.12f);
			B.Box(FVector(3.9f, 0.f, 3.6f), FVector(0.5f, 0.9f, 1.9f), 0.12f);
			B.SetSlot(Steel);
			B.Prism({ { 5.5f, 4.5f }, { 6.0f, 4.5f }, { 5.9f, 3.6f }, { 5.55f, 3.0f }, { 5.25f, 3.15f }, { 5.45f, 3.7f } }, -0.18f, 0.18f, 0.05f);
			SaveKit(B, W, TEXT("SM_M4A1_Receiver"));
		}
		{
			// Bolt carrier showing through the open ejection port.
			FBuilder B;
			B.SetSlot(Bright);
			B.Box(FVector(10.4f, 1.19f, 9.6f), FVector(5.4f, 0.03f, 1.2f), 0.01f);
			B.SetSlot(Dark);
			B.Box(FVector(9.0f, 1.21f, 9.85f), FVector(1.3f, 0.02f, 0.45f), 0.f);
			SaveKit(B, W, TEXT("SM_M4A1_Bolt"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Box(FVector(-2.25f, 0.f, 11.15f), FVector(1.0f, 4.0f, 0.55f), 0.18f);
			B.Box(FVector(-1.15f, -1.55f, 11.42f), FVector(1.0f, 0.8f, 0.32f), 0.1f);
			B.Box(FVector(1.0f, 0.f, 11.2f), FVector(4.6f, 0.9f, 0.4f), 0.08f);
			SaveKit(B, W, TEXT("SM_M4A1_Charging"));
		}
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Poly);
			GripPrism(B, A2Grip, 1.4f, 0.45f);
			if (bStippled)
			{
				// Grip tape wraps.
				B.SetSlot(Rubber);
				B.Box(FVector(-0.9f, 0.f, -2.2f), FVector(2.9f, 2.95f, 2.4f), 0.4f, FRotator(-18.f, 0.f, 0.f));
			}
			SaveKit(B, W, bStippled ? TEXT("SM_M4A1_grip_stippled") : TEXT("SM_M4A1_Grip"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Lathe({ FVector2D(-2.6f, 1.75f), FVector2D(-1.8f, 1.75f) }, 8, FTransform(FVector(0.f, 0.f, 8.9f)), 35.f, false, 22.5f);
			B.Tube(-22.8f, -2.6f, 1.55f, 24, FTransform(FVector(0.f, 0.f, 8.9f)));
			B.SetSlot(Poly);
			B.Prism({ { -12.6f, 11.4f }, { -22.6f, 11.7f }, { -23.6f, 12.3f }, { -24.2f, 11.8f }, { -24.2f, 1.8f }, { -23.4f, 1.2f }, { -21.6f, 1.6f }, { -15.2f, 6.4f },
				{ -13.4f, 7.0f }, { -12.6f, 7.6f } }, -1.75f, 1.75f, 0.35f);
			B.Box(FVector(-14.2f, 0.f, 6.6f), FVector(2.2f, 1.1f, 0.7f), 0.2f);
			B.SetSlot(Rubber);
			B.Box(FVector(-24.6f, 0.f, 7.0f), FVector(0.8f, 3.5f, 11.0f), 0.35f);
			B.SetSlot(Steel);
			Pin(B, FVector(-20.6f, 0.f, 3.4f), 0.4f, 3.7f, 10);
			SaveKit(B, W, TEXT("SM_M4A1_Stock"));
		}
		{
			// 12" free-float M-LOK rail: octagonal panels, slotted at 3, 6 and 9 o'clock, full top rail.
			FBuilder B;
			B.SetSlot(Anod);
			const float X0 = 17.6f;
			const float X1 = 48.1f;
			const float Cz = 9.4f;
			const float Apothem = 2.2f;
			const float Thick = 0.3f;
			const float Side = 2.f * Apothem * FMath::Tan(FMath::DegreesToRadians(22.5f)) + 0.12f;
			for (int32 k = 0; k < 8; ++k)
			{
				// k = 0 top, 2 right, 4 bottom, 6 left (clockwise seen from behind).
				const float Psi = k * PI * 0.25f;
				const FVector Dir(0.f, FMath::Sin(Psi), FMath::Cos(Psi));
				const FVector Center = FVector((X0 + X1) * 0.5f, 0.f, Cz) + Dir * (Apothem - Thick * 0.5f);
				const bool bSlotted = (k == 2 || k == 4 || k == 6);
				if (!bSlotted)
				{
					BoxRolled(B, Center, FVector(X1 - X0, Side, Thick), -Psi, 0.08f);
					continue;
				}
				const float SlotH = 0.7f;
				const float Band = (Side - SlotH) * 0.5f;
				const FVector Lateral = FQuat(FVector::XAxisVector, -Psi).RotateVector(FVector::YAxisVector);
				for (const float S : { -1.f, 1.f })
				{
					BoxRolled(B, Center + Lateral * S * (SlotH + Band) * 0.5f, FVector(X1 - X0, Band, Thick), -Psi, 0.06f);
				}
				// Bars between the 32 mm slots on a 40 mm pitch.
				const float First = 19.4f;
				TArray<TPair<float, float>> Solid;
				Solid.Add({ X0, First });
				for (float S = First; S + 3.2f < X1 - 1.f; S += 4.0f)
				{
					Solid.Add({ S + 3.2f, FMath::Min(S + 4.0f, X1) });
				}
				Solid.Last().Value = X1;
				for (const TPair<float, float>& Seg : Solid)
				{
					if (Seg.Value - Seg.Key > 0.05f)
					{
						BoxRolled(B, FVector((Seg.Key + Seg.Value) * 0.5f, Center.Y, Center.Z), FVector(Seg.Value - Seg.Key, SlotH + 0.02f, Thick), -Psi, 0.05f);
					}
				}
			}
			Rail(B, FVector(17.9f, 0.f, Cz + Apothem), 30.0f, EFace::Up);
			B.Lathe({ FVector2D(X0, 2.36f), FVector2D(X0 + 0.9f, 2.36f) }, 8, FTransform(FVector(0.f, 0.f, Cz)), 35.f, false, 22.5f);
			B.Lathe({ FVector2D(X1 - 0.5f, 2.34f), FVector2D(X1, 2.34f) }, 8, FTransform(FVector(0.f, 0.f, Cz)), 35.f, false, 22.5f);
			// QD sling cup and panel screws.
			B.SetSlot(Steel);
			Pin(B, FVector(20.0f, -2.25f, Cz), 0.55f, 0.35f, 12);
			for (const float Sx : { 21.0f, 44.5f })
			{
				Pin(B, FVector(Sx, 2.23f, Cz + 1.3f), 0.18f, 0.12f, 8);
				Pin(B, FVector(Sx, -2.23f, Cz + 1.3f), 0.18f, 0.12f, 8);
			}
			SaveKit(B, W, TEXT("SM_M4A1_Handguard"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Tube(17.6f, 19.2f, 1.6f, 20, FTransform(FVector(0.f, 0.f, 9.5f)));
			B.Tube(19.2f, 48.4f, 0.95f, 20, FTransform(FVector(0.f, 0.f, 9.5f)));
			B.Box(FVector(36.5f, 0.f, 9.65f), FVector(2.2f, 2.0f, 2.4f), 0.2f);
			B.Tube(18.5f, 36.0f, 0.25f, 10, FTransform(FVector(0.f, 0.f, 10.95f)));
			SaveKit(B, W, TEXT("SM_M4A1_Barrel"));
		}
		{
			// A2 birdcage, authored from the muzzle thread forward.
			FBuilder B;
			B.SetSlot(Steel);
			B.Tube(0.f, 1.9f, 1.08f, 20);
			B.Tube(5.0f, 5.6f, 1.1f, 20);
			for (int32 k = 0; k < 6; ++k)
			{
				const float A = k * PI / 3.f + PI / 6.f;
				BoxRolled(B, FVector(3.45f, FMath::Sin(A) * 0.92f, FMath::Cos(A) * 0.92f), FVector(3.2f, 0.42f, 0.32f), -A, 0.06f);
			}
			BoxRolled(B, FVector(3.45f, 0.f, -0.92f), FVector(3.2f, 1.2f, 0.32f), PI, 0.08f);
			B.SetSlot(Dark);
			B.Tube(1.9f, 5.0f, 0.82f, 16);
			B.Tube(5.55f, 5.62f, 0.45f, 12);
			SaveKit(B, W, TEXT("SM_M4A1_FlashHider"));
		}
		const FVector MagTop(12.8f, 0.f, 4.3f);
		{
			FBuilder B;
			// FDE polymer magazine: reads apart from the black rifle, so the magazine change is legible.
			Magazine(B, MagTop, StanagOutline, 1.25f, Fde, 0.25f, 0.48f, 5.6f, true);
			B.SetSlot(Fde);
			B.Box(FVector(MagTop.X + 2.5f, 0.f, MagTop.Z - 20.25f), FVector(7.4f, 2.9f, 0.7f), 0.25f, FRotator(-4.f, 0.f, 0.f));
			SaveKit(B, W, TEXT("SM_M4A1_Mag"));
		}
		{
			FBuilder B;
			const TArray<FVector2D> Ext = StretchOutline(StanagOutline, 1.42f, 3.f);
			Magazine(B, MagTop, Ext, 1.25f, Poly, 0.25f, 0.48f, 5.6f, true);
			B.SetSlot(Poly);
			B.Box(FVector(MagTop.X + 5.0f, 0.f, MagTop.Z - 28.9f), FVector(7.4f, 2.9f, 0.7f), 0.25f, FRotator(-9.f, 0.f, 0.f));
			SaveKit(B, W, TEXT("SM_M4A1_mag_extended"));
		}
		{
			// Two magazines coupled side by side.
			FBuilder B;
			Magazine(B, MagTop, StanagOutline, 1.25f, Poly, 0.25f, 0.48f, 5.6f, true);
			Magazine(B, MagTop + FVector(0.6f, 2.75f, 0.f), StanagOutline, 1.25f, Poly, 0.25f, 0.48f, 5.6f, false);
			B.SetSlot(Rubber);
			for (const float Z : { -8.f, -15.f })
			{
				B.Box(FVector(MagTop.X + 0.9f + (-Z) * 0.12f, 1.375f, MagTop.Z + Z), FVector(7.4f, 5.7f, 1.1f), 0.2f, FRotator(-10.f, 0.f, 0.f));
			}
			SaveKit(B, W, TEXT("SM_M4A1_mag_fast"));
		}
		for (const bool bUp : { true, false })
		{
			FBuilder B;
			FlipSight(B, 0.4f, 12.55f, 14.45f, true, bUp);
			SaveKit(B, W, bUp ? TEXT("SM_M4A1_RearSight") : TEXT("SM_M4A1_RearSightFolded"));
			FBuilder F;
			FlipSight(F, 45.4f, 12.55f, 14.45f, false, bUp);
			SaveKit(F, W, bUp ? TEXT("SM_M4A1_FrontSight") : TEXT("SM_M4A1_FrontSightFolded"));
		}
	}

	// ------------------------------------------------------------------ AK-47

	void BuildAK()
	{
		const TCHAR* W = TEXT("AK47");
		{
			FBuilder B;
			B.SetSlot(Steel);
			// Stamped receiver and the rounded, ribbed dust cover.
			B.Prism({ { -3.0f, 9.7f }, { 21.0f, 9.7f }, { 21.0f, 5.4f }, { 20.0f, 4.6f }, { 15.6f, 4.6f }, { 15.6f, 4.4f }, { 9.8f, 4.4f }, { 9.4f, 4.7f },
				{ 4.6f, 4.4f }, { 0.4f, 4.4f }, { -1.6f, 4.8f }, { -3.0f, 5.8f } }, -1.6f, 1.6f, 0.1f);
			PrismX(B, { { -1.62f, 9.6f }, { 1.62f, 9.6f }, { 1.62f, 10.4f }, { 1.2f, 11.1f }, { 0.5f, 11.42f }, { -0.5f, 11.42f }, { -1.2f, 11.1f }, { -1.62f, 10.4f } }, -3.4f, 19.6f, 0.1f, 40.f);
			// AKM stamped stiffening ribs across the cover, following its arch.
			// Pressed into the shoulders of the cover only: the crown under the sight line stays clean.
			for (float X = 1.f; X < 17.5f; X += 2.4f)
			{
				for (const float S : { -1.f, 1.f })
				{
					PrismX(B, { { S * 1.6f, 10.2f }, { S * 1.7f, 10.2f }, { S * 1.7f, 10.45f }, { S * 1.27f, 11.17f }, { S * 1.15f, 11.12f }, { S * 1.58f, 10.42f } }, X, X + 0.5f, 0.04f, 40.f);
				}
			}
			B.Box(FVector(-3.6f, 0.f, 10.4f), FVector(0.6f, 0.9f, 0.6f), 0.15f);
			B.SetSlot(Dark);
			B.SetCavity(1.f);
			B.Box(FVector(12.7f, 0.f, 4.42f), FVector(5.6f, 2.6f, 0.06f), 0.f);
			B.Box(FVector(13.0f, 1.615f, 8.55f), FVector(7.2f, 0.03f, 1.3f), 0.f);
			B.SetCavity(0.f);
			// Rivets.
			B.SetSlot(Steel);
			for (const FVector& R : { FVector(19.0f, 0.f, 6.0f), FVector(19.0f, 0.f, 7.6f), FVector(-1.6f, 0.f, 6.6f), FVector(-0.4f, 0.f, 8.4f) })
			{
				Pin(B, R, 0.24f, 3.3f, 10);
			}
			// Rear sight block and leaf, gas tube, barrel, gas block, front sight, cleaning rod. The sight line runs about
			// 2 cm over the dust cover crown and 1 cm over the upper handguard, as on an AKM: the post stands clear of the
			// furniture and the cover sits low in the aiming view.
			B.Box(FVector(23.0f, 0.f, 10.8f), FVector(4.4f, 2.6f, 3.6f), 0.25f);
			B.Box(FVector(24.0f, 0.f, 12.75f), FVector(5.0f, 1.5f, 0.35f), 0.06f, FRotator(-3.f, 0.f, 0.f));
			B.Box(FVector(21.6f, 0.f, 12.9f), FVector(0.6f, 1.5f, 0.75f), 0.08f);
			B.Tube(25.5f, 42.0f, 0.85f, 18, FTransform(FVector(0.f, 0.f, 11.3f)));
			B.Tube(21.0f, 57.0f, 0.82f, 20, FTransform(FVector(0.f, 0.f, 9.0f)));
			B.Box(FVector(43.0f, 0.f, 10.1f), FVector(2.6f, 2.1f, 3.0f), 0.25f);
			B.Tube(26.0f, 54.0f, 0.32f, 10, FTransform(FVector(0.f, 0.f, 6.9f)));
			B.Box(FVector(53.2f, 0.f, 10.0f), FVector(3.2f, 2.2f, 3.4f), 0.2f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(53.4f, S * 0.85f, 12.85f), FVector(1.4f, 0.32f, 2.4f), 0.1f);
			}
			B.Box(FVector(53.4f, 0.f, 12.45f), FVector(0.32f, 0.32f, 1.7f), 0.04f);
			B.Box(FVector(52.4f, 0.f, 7.7f), FVector(2.0f, 0.8f, 0.8f), 0.15f);
			// Selector lever (right side), trigger guard, magazine release, trigger.
			B.Box(FVector(10.0f, 1.72f, 8.7f), FVector(10.5f, 0.16f, 0.9f), 0.05f);
			B.Box(FVector(15.4f, 1.75f, 8.0f), FVector(1.0f, 0.2f, 1.8f), 0.08f);
			B.Box(FVector(5.6f, 0.f, 2.6f), FVector(9.2f, 1.0f, 0.32f), 0.08f);
			B.Box(FVector(1.1f, 0.f, 3.5f), FVector(0.4f, 1.0f, 2.0f), 0.08f);
			B.Box(FVector(10.1f, 0.f, 3.5f), FVector(0.4f, 1.0f, 2.0f), 0.08f);
			B.Box(FVector(10.7f, 0.f, 3.4f), FVector(0.5f, 1.3f, 1.6f), 0.08f);
			B.Prism({ { 4.8f, 4.4f }, { 5.3f, 4.4f }, { 5.2f, 3.5f }, { 4.85f, 2.95f }, { 4.55f, 3.1f }, { 4.75f, 3.6f } }, -0.2f, 0.2f, 0.05f);
			SaveKit(B, W, TEXT("SM_AK47_Receiver"));
		}
		{
			// Bolt carrier and charging handle: recoils with every shot.
			FBuilder B;
			B.SetSlot(Steel);
			B.Box(FVector(12.0f, 1.66f, 8.6f), FVector(7.5f, 0.05f, 1.0f), 0.01f);
			B.Box(FVector(16.3f, 1.9f, 8.7f), FVector(0.7f, 0.6f, 0.5f), 0.1f);
			B.Box(FVector(16.3f, 2.4f, 8.7f), FVector(1.0f, 0.75f, 0.85f), 0.25f);
			SaveKit(B, W, TEXT("SM_AK47_Bolt"));
		}
		{
			FBuilder B;
			B.SetSlot(Wood);
			PrismX(B, { { -2.0f, 9.4f }, { -2.05f, 8.2f }, { -1.85f, 6.8f }, { -1.3f, 5.6f }, { -0.5f, 5.1f }, { 0.5f, 5.1f }, { 1.3f, 5.6f }, { 1.85f, 6.8f }, { 2.05f, 8.2f }, { 2.0f, 9.4f } },
				21.4f, 38.0f, 0.3f, 35.f);
			PrismX(B, { { -1.35f, 10.4f }, { 1.35f, 10.4f }, { 1.4f, 11.3f }, { 1.1f, 12.05f }, { 0.4f, 12.4f }, { -0.4f, 12.4f }, { -1.1f, 12.05f }, { -1.4f, 11.3f } }, 26.0f, 38.6f, 0.22f, 35.f);
			B.SetSlot(Steel);
			B.Prism(CampusMesh::RoundedRect(FVector2D(-2.15f, 4.95f), FVector2D(2.15f, 9.6f), 1.2f, 3), -39.0f, -38.0f, 0.1f, 40.f, FTransform(FQuat(FVector::ZAxisVector, HALF_PI)));
			B.Box(FVector(38.9f, 0.f, 11.5f), FVector(0.8f, 2.4f, 1.8f), 0.2f);
			SaveKit(B, W, TEXT("SM_AK47_Handguard"));
		}
		{
			FBuilder B;
			B.SetSlot(Wood);
			B.Prism({ { -3.0f, 9.4f }, { -9.0f, 8.8f }, { -26.0f, 7.5f }, { -34.8f, 7.7f }, { -35.2f, 6.8f }, { -35.2f, -4.2f }, { -34.2f, -4.8f }, { -21.0f, -0.6f },
				{ -9.0f, 3.9f }, { -4.0f, 5.0f }, { -3.0f, 5.6f } }, -1.85f, 1.85f, 0.5f, 30.f);
			B.SetSlot(Steel);
			B.Prism({ { -35.0f, 7.9f }, { -35.8f, 7.7f }, { -35.8f, -4.6f }, { -35.0f, -5.0f } }, -1.95f, 1.95f, 0.12f);
			B.Box(FVector(-4.5f, 0.f, 9.0f), FVector(3.2f, 1.2f, 0.5f), 0.12f);
			SaveKit(B, W, TEXT("SM_AK47_Stock"));
		}
		const TArray<FVector2D> AkGrip = { { 3.4f, 4.4f }, { 0.0f, 4.4f }, { -0.6f, 3.4f }, { -1.7f, -0.6f }, { -2.6f, -4.8f }, { -3.1f, -7.4f }, { -2.6f, -8.0f },
			{ -1.2f, -8.1f }, { -0.3f, -7.6f }, { -0.1f, -5.8f }, { 0.6f, -2.0f }, { 1.6f, 0.6f }, { 2.9f, 3.4f } };
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Bakelite);
			GripPrism(B, AkGrip, 1.45f, 0.5f);
			SaveKit(B, W, bStippled ? TEXT("SM_AK47_grip_stippled") : TEXT("SM_AK47_Grip"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Tube(0.f, 3.0f, 1.05f, 20);
			B.Box(FVector(3.05f, 0.f, -0.5f), FVector(0.7f, 1.7f, 1.0f), 0.2f);
			B.SetSlot(Dark);
			B.Tube(2.95f, 3.05f, 0.5f, 12);
			SaveKit(B, W, TEXT("SM_AK47_Brake"));
		}
		const FVector MagTop(12.7f, 0.f, 4.5f);
		const TArray<FVector2D> AkMag = { { 3.2f, 0.f }, { 3.4f, -3.5f }, { 4.3f, -8.f }, { 6.0f, -12.5f }, { 8.6f, -17.f }, { 11.6f, -20.8f }, { 12.2f, -21.2f }, { 12.9f, -21.9f },
			{ 6.6f, -24.4f }, { 5.6f, -24.0f }, { 3.5f, -20.6f }, { 1.3f, -16.2f }, { -0.8f, -11.4f }, { -2.2f, -6.6f }, { -2.9f, -2.4f }, { -3.0f, 0.f } };
		{
			FBuilder B;
			Magazine(B, MagTop, AkMag, 1.35f, Steel, 0.15f, 0.56f, 5.4f, false);
			B.SetSlot(Steel);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(MagTop.X + 2.6f, S * 1.38f, MagTop.Z - 9.5f), FVector(1.2f, 0.08f, 7.0f), 0.04f, FRotator(-24.f, 0.f, 0.f));
			}
			SaveKit(B, W, TEXT("SM_AK47_Mag"));
		}
		{
			FBuilder B;
			Magazine(B, MagTop, StretchOutline(AkMag, 1.35f, 3.f), 1.35f, Steel, 0.15f, 0.56f, 5.4f, false);
			SaveKit(B, W, TEXT("SM_AK47_mag_extended"));
		}
		{
			FBuilder B;
			Magazine(B, MagTop, AkMag, 1.35f, Steel, 0.15f, 0.56f, 5.4f, false);
			Magazine(B, MagTop + FVector(0.4f, 2.95f, 0.f), AkMag, 1.35f, Steel, 0.15f, 0.56f, 5.4f, false);
			B.SetSlot(Rubber);
			B.Box(FVector(MagTop.X + 3.0f, 1.47f, MagTop.Z - 8.f), FVector(6.6f, 6.0f, 1.1f), 0.2f, FRotator(-20.f, 0.f, 0.f));
			SaveKit(B, W, TEXT("SM_AK47_mag_fast"));
		}
		{
			// Side-rail optic mount carrying a rail over the dust cover.
			FBuilder B;
			B.SetSlot(Anod);
			B.Box(FVector(10.0f, -1.78f, 8.2f), FVector(7.0f, 0.3f, 1.6f), 0.08f);
			B.Box(FVector(10.0f, -2.05f, 10.0f), FVector(6.0f, 0.5f, 4.0f), 0.18f);
			B.Box(FVector(10.0f, -0.4f, 11.95f), FVector(8.0f, 3.4f, 0.6f), 0.15f);
			Rail(B, FVector(4.0f, -0.4f, 12.25f), 12.0f, EFace::Up);
			B.SetSlot(Steel);
			Pin(B, FVector(8.0f, -2.4f, 9.0f), 0.55f, 0.5f, 8);
			SaveKit(B, W, TEXT("SM_AK47_OpticMount"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Box(FVector(34.0f, 0.f, 5.0f), FVector(7.5f, 3.0f, 0.45f), 0.12f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(34.0f, S * 1.95f, 6.0f), FVector(7.0f, 0.3f, 2.2f), 0.1f);
			}
			SaveKit(B, W, TEXT("SM_AK47_UnderRail"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Box(FVector(35.5f, 2.15f, 7.2f), FVector(6.0f, 0.3f, 2.0f), 0.08f);
			SaveKit(B, W, TEXT("SM_AK47_SideRail"));
		}
	}

	// ------------------------------------------------------------------ MP5

	void BuildMP5()
	{
		const TCHAR* W = TEXT("MP5");
		{
			FBuilder B;
			B.SetSlot(Steel);
			PrismX(B, { { -1.7f, 5.3f }, { 1.7f, 5.3f }, { 1.7f, 9.6f }, { 1.45f, 10.5f }, { 0.85f, 11.0f }, { 0.f, 11.15f }, { -0.85f, 11.0f }, { -1.45f, 10.5f }, { -1.7f, 9.6f } },
				-6.0f, 24.0f, 0.12f, 40.f);
			B.Box(FVector(-6.2f, 0.f, 8.6f), FVector(0.5f, 3.2f, 5.0f), 0.15f);
			// Cocking tube, front sight hood and post, rear drum sight.
			B.Tube(18.5f, 33.5f, 0.95f, 18, FTransform(FVector(0.f, 0.f, 12.0f)));
			B.Tube(33.5f, 34.0f, 0.8f, 18, FTransform(FVector(0.f, 0.f, 12.0f)));
			B.Box(FVector(21.0f, 0.f, 11.3f), FVector(5.0f, 1.6f, 1.4f), 0.2f);
			B.HollowTube(32.0f, 33.6f, 1.25f, 1.0f, 20, FTransform(FVector(0.f, 0.f, 13.0f)));
			B.Box(FVector(32.8f, 0.f, 12.55f), FVector(1.6f, 1.2f, 1.0f), 0.15f);
			B.Box(FVector(32.8f, 0.f, 12.75f), FVector(0.25f, 0.25f, 0.5f), 0.03f);
			// Rear diopter drum: the two drum cheeks either side of an open 3.4 mm peep, on its base block. The sight line
			// goes through the peep, so at the cheek it opens into a ghost ring round the front post.
			for (const float S : { -1.f, 1.f })
			{
				Pin(B, FVector(-2.0f, S * 0.82f, 12.4f), 1.05f, 0.56f, 24);
				// Knurled rim of each drum cheek.
				Knurl(B, AlongY(FVector(-2.0f, S * 0.82f, 12.4f)), -0.22f, 0.22f, 1.05f, 22, 0.05f, 0.1f);
			}
			B.HollowTube(-2.55f, -1.45f, 0.6f, 0.34f, 22, FTransform(FVector(0.f, 0.f, 13.0f)));
			// The drum's other apertures, round its face (the open one is on the sight line).
			B.SetSlot(Dark);
			for (const float A : { 60.f, 140.f, 220.f })
			{
				const float Rad = FMath::DegreesToRadians(A);
				B.Tube(-0.31f, 0.31f, 0.16f, 10, FTransform(FQuat(FVector::ZAxisVector, HALF_PI), FVector(-2.0f + FMath::Sin(Rad) * 0.98f, 0.f, 12.4f + FMath::Cos(Rad) * 0.98f)), 0.f);
			}
			B.SetSlot(Steel);
			B.Box(FVector(-2.0f, 0.f, 11.9f), FVector(1.6f, 1.1f, 1.05f), 0.12f);
			B.Box(FVector(-2.0f, 0.f, 11.35f), FVector(2.2f, 2.2f, 0.7f), 0.15f);
			// Barrel, three-lug, trunnion.
			B.Tube(24.0f, 34.5f, 0.72f, 18, FTransform(FVector(0.f, 0.f, 8.6f)));
			B.Tube(34.5f, 36.6f, 0.85f, 18, FTransform(FVector(0.f, 0.f, 8.6f)));
			for (int32 k = 0; k < 3; ++k)
			{
				const float A = k * 2.f * PI / 3.f;
				BoxRolled(B, FVector(35.0f, FMath::Sin(A) * 0.95f, 8.6f + FMath::Cos(A) * 0.95f), FVector(0.9f, 0.45f, 0.3f), -A, 0.06f);
			}
			B.Box(FVector(24.3f, 0.f, 8.6f), FVector(0.8f, 2.6f, 2.6f), 0.15f);
			// Magwell and paddle release.
			B.Box(FVector(11.6f, 0.f, 4.2f), FVector(4.2f, 2.6f, 2.4f), 0.15f);
			B.Box(FVector(9.0f, 0.f, 3.6f), FVector(0.4f, 1.4f, 1.6f), 0.08f);
			B.SetSlot(Dark);
			B.SetCavity(1.f);
			B.Box(FVector(11.6f, 0.f, 3.0f), FVector(3.8f, 2.2f, 0.04f), 0.f);
			B.Box(FVector(12.5f, 1.71f, 9.0f), FVector(4.6f, 0.03f, 1.2f), 0.f);
			B.SetCavity(0.f);
			// Polymer trigger housing, guard, selector, trigger.
			B.SetSlot(Poly);
			B.Box(FVector(2.6f, 0.f, 4.35f), FVector(14.4f, 3.1f, 1.9f), 0.35f);
			B.Box(FVector(5.4f, 0.f, 2.0f), FVector(6.0f, 1.1f, 0.45f), 0.15f);
			B.Box(FVector(8.4f, 0.f, 2.8f), FVector(0.6f, 1.1f, 1.6f), 0.15f);
			B.SetSlot(Steel);
			Pin(B, FVector(1.8f, -1.6f, 4.4f), 0.5f, 0.3f);
			B.Box(FVector(1.2f, -1.72f, 4.9f), FVector(1.4f, 0.16f, 0.4f), 0.05f, FRotator(20.f, 0.f, 0.f));
			B.Prism({ { 5.2f, 3.4f }, { 5.7f, 3.4f }, { 5.6f, 2.8f }, { 5.25f, 2.35f }, { 4.95f, 2.5f }, { 5.15f, 2.95f } }, -0.18f, 0.18f, 0.05f);
			SaveKit(B, W, TEXT("SM_MP5_Receiver"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Box(FVector(29.6f, -0.95f, 12.0f), FVector(1.2f, 0.45f, 0.8f), 0.12f);
			B.Box(FVector(30.9f, -1.4f, 12.0f), FVector(2.8f, 0.45f, 0.65f), 0.15f, FRotator(0.f, -12.f, 0.f));
			B.SetSlot(Poly);
			B.Box(FVector(32.2f, -1.75f, 12.0f), FVector(0.9f, 0.85f, 0.95f), 0.3f);
			SaveKit(B, W, TEXT("SM_MP5_Charging"));
		}
		{
			FBuilder B;
			B.SetSlot(Poly);
			PrismX(B, { { -2.0f, 11.0f }, { -2.15f, 9.0f }, { -1.9f, 6.6f }, { -1.2f, 5.6f }, { 0.f, 5.35f }, { 1.2f, 5.6f }, { 1.9f, 6.6f }, { 2.15f, 9.0f }, { 2.0f, 11.0f }, { 1.2f, 11.5f }, { -1.2f, 11.5f } },
				20.0f, 30.8f, 0.3f, 35.f);
			PrismX(B, { { -2.3f, 10.6f }, { -2.4f, 8.6f }, { -2.1f, 6.0f }, { -1.3f, 5.0f }, { 0.f, 4.8f }, { 1.3f, 5.0f }, { 2.1f, 6.0f }, { 2.4f, 8.6f }, { 2.3f, 10.6f } },
				30.6f, 31.6f, 0.3f, 35.f);
			SaveKit(B, W, TEXT("SM_MP5_Handguard"));
		}
		{
			FBuilder B;
			B.SetSlot(Poly);
			B.Prism({ { -6.0f, 10.9f }, { -12.0f, 10.6f }, { -27.0f, 10.2f }, { -30.0f, 11.0f }, { -30.6f, 10.4f }, { -30.6f, -1.2f }, { -29.6f, -2.4f }, { -27.0f, -1.6f },
				{ -14.0f, 4.0f }, { -8.5f, 6.5f }, { -6.0f, 6.6f } }, -1.75f, 1.75f, 0.45f, 30.f);
			B.SetSlot(Rubber);
			B.Box(FVector(-30.9f, 0.f, 4.8f), FVector(0.6f, 3.4f, 12.0f), 0.25f);
			SaveKit(B, W, TEXT("SM_MP5_Stock"));
		}
		const TArray<FVector2D> NavyGrip = { { 1.6f, 3.4f }, { -3.6f, 3.4f }, { -4.0f, 1.6f }, { -4.6f, -2.4f }, { -5.2f, -6.0f }, { -4.7f, -7.4f }, { -2.6f, -7.6f }, { -1.6f, -6.8f },
			{ -1.2f, -5.6f }, { -0.7f, -4.8f }, { -0.9f, -4.0f }, { -0.3f, -3.2f }, { -0.5f, -2.4f }, { 0.2f, -1.5f }, { 0.6f, 0.0f }, { 1.3f, 2.4f } };
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Poly);
			GripPrism(B, NavyGrip, 1.5f, 0.45f, 1.8f);
			SaveKit(B, W, bStippled ? TEXT("SM_MP5_grip_stippled") : TEXT("SM_MP5_Grip"));
		}
		const FVector MagTop(11.6f, 0.f, 4.8f);
		const TArray<FVector2D> Mp5Mag = { { 1.9f, 0.f }, { 2.1f, -5.f }, { 2.8f, -10.f }, { 3.9f, -14.8f }, { 4.3f, -15.4f }, { 4.4f, -16.1f }, { 0.4f, -16.6f }, { 0.1f, -15.9f },
			{ -0.9f, -11.2f }, { -1.6f, -5.8f }, { -1.9f, 0.f } };
		{
			FBuilder B;
			Magazine(B, MagTop, Mp5Mag, 0.95f, Steel, 0.12f, 0.36f, 2.9f, false);
			B.SetSlot(Steel);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(MagTop.X + 0.6f, S * 0.97f, MagTop.Z - 8.f), FVector(0.5f, 0.06f, 12.f), 0.03f, FRotator(-9.f, 0.f, 0.f));
			}
			SaveKit(B, W, TEXT("SM_MP5_Mag"));
		}
		{
			FBuilder B;
			Magazine(B, MagTop, StretchOutline(Mp5Mag, 1.45f, 2.5f), 0.95f, Steel, 0.12f, 0.36f, 2.9f, false);
			SaveKit(B, W, TEXT("SM_MP5_mag_extended"));
		}
		{
			FBuilder B;
			Magazine(B, MagTop, Mp5Mag, 0.95f, Steel, 0.12f, 0.36f, 2.9f, false);
			Magazine(B, MagTop + FVector(0.3f, 2.1f, 0.f), Mp5Mag, 0.95f, Steel, 0.12f, 0.36f, 2.9f, false);
			B.SetSlot(Rubber);
			B.Box(FVector(MagTop.X + 1.0f, 1.05f, MagTop.Z - 7.f), FVector(4.6f, 4.2f, 1.0f), 0.2f, FRotator(-8.f, 0.f, 0.f));
			SaveKit(B, W, TEXT("SM_MP5_mag_fast"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			for (const float X : { 2.0f, 11.0f })
			{
				B.Box(FVector(X, 0.f, 11.25f), FVector(1.6f, 3.7f, 1.0f), 0.15f);
			}
			B.Box(FVector(6.5f, 0.f, 11.9f), FVector(11.0f, 2.4f, 0.6f), 0.15f);
			Rail(B, FVector(1.0f, 0.f, 12.2f), 11.0f, EFace::Up);
			SaveKit(B, W, TEXT("SM_MP5_ClawMount"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Box(FVector(26.0f, 0.f, 5.15f), FVector(6.5f, 3.0f, 0.45f), 0.12f);
			SaveKit(B, W, TEXT("SM_MP5_UnderRail"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Box(FVector(26.5f, 2.25f, 8.2f), FVector(5.5f, 0.3f, 1.8f), 0.08f);
			SaveKit(B, W, TEXT("SM_MP5_SideRail"));
		}
	}

	// ------------------------------------------------------------------ MK14 EBR

	void BuildMK14()
	{
		const TCHAR* W = TEXT("MK14");
		{
			FBuilder B;
			B.SetSlot(Fde);
			B.Prism({ { -6.0f, 12.2f }, { 20.0f, 12.2f }, { 20.0f, 4.6f }, { 15.6f, 4.6f }, { 15.6f, 4.4f }, { 7.8f, 4.4f }, { 7.6f, 4.8f }, { 4.2f, 4.8f }, { 3.8f, 4.4f },
				{ 0.0f, 4.4f }, { -2.0f, 5.6f }, { -6.0f, 6.6f } }, -2.6f, 2.6f, 0.25f);
			PrismX(B, { { -2.6f, 5.0f }, { 2.6f, 5.0f }, { 2.9f, 5.6f }, { 2.9f, 11.6f }, { 2.6f, 12.2f }, { -2.6f, 12.2f }, { -2.9f, 11.6f }, { -2.9f, 5.6f } }, 20.0f, 36.0f, 0.2f);
			Rail(B, FVector(-5.0f, 0.f, 12.2f), 40.0f, EFace::Up);
			Rail(B, FVector(21.0f, 2.9f, 9.0f), 14.0f, EFace::Right);
			Rail(B, FVector(21.0f, -2.9f, 9.0f), 14.0f, EFace::Left);
			Rail(B, FVector(22.0f, 0.f, 5.0f), 12.0f, EFace::Down);
			B.Box(FVector(11.8f, 0.f, 3.5f), FVector(7.6f, 3.4f, 2.0f), 0.25f);
			B.Box(FVector(36.2f, 0.f, 8.6f), FVector(0.6f, 5.4f, 7.0f), 0.2f);
			B.SetSlot(Dark);
			B.SetCavity(1.f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(10.0f, S * 2.615f, 9.0f), FVector(6.0f, 0.04f, 2.0f), 0.f);
				B.Box(FVector(-1.5f, S * 2.615f, 9.6f), FVector(4.0f, 0.04f, 1.8f), 0.f);
			}
			B.Box(FVector(14.0f, 2.62f, 10.4f), FVector(10.0f, 0.04f, 1.0f), 0.f);
			B.Box(FVector(11.8f, 0.f, 2.48f), FVector(6.8f, 2.8f, 0.04f), 0.f);
			B.SetCavity(0.f);
			B.SetSlot(Steel);
			B.Box(FVector(7.4f, 0.f, 3.4f), FVector(0.4f, 1.4f, 1.4f), 0.08f);
			B.Box(FVector(2.3f, 0.f, 2.4f), FVector(4.6f, 1.1f, 0.42f), 0.12f);
			B.Box(FVector(4.5f, 0.f, 3.4f), FVector(0.45f, 1.1f, 2.0f), 0.12f);
			B.Box(FVector(0.1f, 0.f, 3.4f), FVector(0.45f, 1.1f, 2.0f), 0.12f);
			B.Prism({ { 2.1f, 4.4f }, { 2.6f, 4.4f }, { 2.5f, 3.5f }, { 2.15f, 2.95f }, { 1.85f, 3.1f }, { 2.05f, 3.6f } }, -0.2f, 0.2f, 0.05f);
			B.Box(FVector(38.0f, 0.f, 7.6f), FVector(4.0f, 1.7f, 1.9f), 0.25f);
			B.Tube(36.0f, 40.5f, 1.15f, 20, FTransform(FVector(0.f, 0.f, 9.4f)));
			SaveKit(B, W, TEXT("SM_MK14_Chassis"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Box(FVector(17.0f, 3.15f, 10.4f), FVector(1.4f, 1.0f, 1.0f), 0.28f);
			B.Box(FVector(22.0f, 2.75f, 10.4f), FVector(9.0f, 0.35f, 0.6f), 0.08f);
			SaveKit(B, W, TEXT("SM_MK14_OpRod"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Tube(-24.0f, -6.0f, 1.6f, 24, FTransform(FVector(0.f, 0.f, 9.0f)));
			B.SetSlot(Fde);
			B.Prism({ { -11.0f, 11.8f }, { -28.0f, 12.0f }, { -29.6f, 11.4f }, { -29.6f, 0.6f }, { -28.4f, 0.0f }, { -17.0f, 4.6f }, { -12.5f, 6.6f }, { -11.0f, 7.0f } }, -2.0f, 2.0f, 0.35f);
			B.SetSlot(Poly);
			B.Box(FVector(-20.0f, 0.f, 12.8f), FVector(13.0f, 3.0f, 1.2f), 0.4f);
			B.SetSlot(Rubber);
			B.Box(FVector(-29.9f, 0.f, 6.0f), FVector(0.6f, 3.8f, 11.6f), 0.25f);
			SaveKit(B, W, TEXT("SM_MK14_Stock"));
		}
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Poly);
			TArray<FVector2D> Grip = A2Grip;
			for (FVector2D& V : Grip)
			{
				V.Y -= 0.1f;
			}
			GripPrism(B, Grip, 1.45f, 0.45f);
			SaveKit(B, W, bStippled ? TEXT("SM_MK14_grip_stippled") : TEXT("SM_MK14_Grip"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Tube(0.f, 5.4f, 1.35f, 20);
			B.SetSlot(Dark);
			for (const float S : { -1.f, 1.f })
			{
				for (const float X : { 1.4f, 2.6f, 3.8f })
				{
					B.Box(FVector(X, S * 1.33f, 0.f), FVector(0.6f, 0.06f, 1.4f), 0.f);
				}
			}
			B.Tube(5.35f, 5.42f, 0.6f, 12);
			SaveKit(B, W, TEXT("SM_MK14_Brake"));
		}
		const FVector MagTop(11.8f, 0.f, 4.4f);
		const TArray<FVector2D> M14Mag = { { 3.5f, 0.f }, { 3.7f, -12.6f }, { 3.9f, -13.4f }, { 4.0f, -14.2f }, { -3.4f, -14.6f }, { -3.6f, -13.8f }, { -3.7f, 0.f } };
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			FBuilder B;
			const TArray<FVector2D> Outline = Variant == 1 ? StretchOutline(M14Mag, 1.5f, 1.f) : M14Mag;
			Magazine(B, MagTop, Outline, 1.45f, Steel, 0.15f, 0.6f, 6.6f, false);
			if (Variant == 2)
			{
				Magazine(B, MagTop + FVector(0.f, 3.1f, 0.f), Outline, 1.45f, Steel, 0.15f, 0.6f, 6.6f, false);
			}
			B.SetSlot(Steel);
			for (const float Z : { -5.5f, -10.f })
			{
				B.Box(FVector(MagTop.X + 0.15f, Variant == 2 ? 1.55f : 0.f, MagTop.Z + Z * (Variant == 1 ? 1.5f : 1.f)), FVector(7.5f, Variant == 2 ? 6.2f : 3.05f, 0.3f), 0.08f);
			}
			SaveKit(B, W, Variant == 0 ? TEXT("SM_MK14_Mag") : (Variant == 1 ? TEXT("SM_MK14_mag_extended") : TEXT("SM_MK14_mag_fast")));
		}
		for (const bool bUp : { true, false })
		{
			FBuilder B;
			FlipSight(B, -2.0f, 13.15f, 15.05f, true, bUp);
			SaveKit(B, W, bUp ? TEXT("SM_MK14_RearSight") : TEXT("SM_MK14_RearSightFolded"));
			FBuilder F;
			FlipSight(F, 33.5f, 13.15f, 15.05f, false, bUp);
			SaveKit(F, W, bUp ? TEXT("SM_MK14_FrontSight") : TEXT("SM_MK14_FrontSightFolded"));
		}
	}

	// ------------------------------------------------------------------ AK-pattern derivatives (RPK, Saiga-12)

	/** Where the AK receiver's front end goes on a derivative: barrel, gas system, front sight and cleaning rod. */
	struct FAkLayout
	{
		float BarrelEnd = 57.f;
		float BarrelR = 0.82f;
		float GasEnd = 42.f;
		float GasBlockX = 43.f;
		float FrontX = 53.2f;
		/** 0: no cleaning rod. */
		float RodEnd = 54.f;
		/** Half width of the magazine well opening (wider for shotshells). */
		float MagwellHalf = 1.3f;
	};

	const TArray<FVector2D> AkGripOutline = { { 3.4f, 4.4f }, { 0.0f, 4.4f }, { -0.6f, 3.4f }, { -1.7f, -0.6f }, { -2.6f, -4.8f }, { -3.1f, -7.4f }, { -2.6f, -8.0f },
		{ -1.2f, -8.1f }, { -0.3f, -7.6f }, { -0.1f, -5.8f }, { 0.6f, -2.0f }, { 1.6f, 0.6f }, { 2.9f, 3.4f } };

	/** The stamped AKM receiver with the front end laid out by L (same frame as SM_AK47_Receiver). */
	void AkReceiver(FBuilder& B, const FAkLayout& L)
	{
		B.SetSlot(Steel);
		B.Prism({ { -3.0f, 9.7f }, { 21.0f, 9.7f }, { 21.0f, 5.4f }, { 20.0f, 4.6f }, { 15.6f, 4.6f }, { 15.6f, 4.4f }, { 9.8f, 4.4f }, { 9.4f, 4.7f },
			{ 4.6f, 4.4f }, { 0.4f, 4.4f }, { -1.6f, 4.8f }, { -3.0f, 5.8f } }, -1.6f, 1.6f, 0.1f);
		PrismX(B, { { -1.62f, 9.6f }, { 1.62f, 9.6f }, { 1.62f, 10.4f }, { 1.2f, 11.1f }, { 0.5f, 11.42f }, { -0.5f, 11.42f }, { -1.2f, 11.1f }, { -1.62f, 10.4f } }, -3.4f, 19.6f, 0.1f, 40.f);
		for (float X = 1.f; X < 17.5f; X += 2.4f)
		{
			for (const float S : { -1.f, 1.f })
			{
				PrismX(B, { { S * 1.6f, 10.2f }, { S * 1.7f, 10.2f }, { S * 1.7f, 10.45f }, { S * 1.27f, 11.17f }, { S * 1.15f, 11.12f }, { S * 1.58f, 10.42f } }, X, X + 0.5f, 0.04f, 40.f);
			}
		}
		B.Box(FVector(-3.6f, 0.f, 10.4f), FVector(0.6f, 0.9f, 0.6f), 0.15f);
		if (L.MagwellHalf > 1.5f)
		{
			// Flared well for the fat shotshell magazine.
			B.Box(FVector(12.7f, 0.f, 3.9f), FVector(7.2f, L.MagwellHalf * 2.f + 0.5f, 1.8f), 0.25f);
		}
		B.SetSlot(Dark);
		B.SetCavity(1.f);
		B.Box(FVector(12.7f, 0.f, L.MagwellHalf > 1.5f ? 2.98f : 4.42f), FVector(5.6f, L.MagwellHalf * 2.f, 0.06f), 0.f);
		B.Box(FVector(13.0f, 1.615f, 8.55f), FVector(7.2f, 0.03f, 1.3f), 0.f);
		B.SetCavity(0.f);
		B.SetSlot(Steel);
		for (const FVector& R : { FVector(19.0f, 0.f, 6.0f), FVector(19.0f, 0.f, 7.6f), FVector(-1.6f, 0.f, 6.6f), FVector(-0.4f, 0.f, 8.4f) })
		{
			Pin(B, R, 0.24f, 3.3f, 10);
		}
		// Rear sight block and leaf, gas tube, barrel, gas block, front sight, cleaning rod.
		B.Box(FVector(23.0f, 0.f, 10.8f), FVector(4.4f, 2.6f, 3.6f), 0.25f);
		B.Box(FVector(24.0f, 0.f, 12.75f), FVector(5.0f, 1.5f, 0.35f), 0.06f, FRotator(-3.f, 0.f, 0.f));
		B.Box(FVector(21.6f, 0.f, 12.9f), FVector(0.6f, 1.5f, 0.75f), 0.08f);
		B.Tube(25.5f, L.GasEnd, 0.85f, 18, FTransform(FVector(0.f, 0.f, 11.3f)));
		B.Tube(21.0f, L.BarrelEnd, L.BarrelR, 20, FTransform(FVector(0.f, 0.f, 9.0f)));
		B.Box(FVector(L.GasBlockX, 0.f, 10.1f), FVector(2.6f, 2.1f, 3.0f), 0.25f);
		if (L.RodEnd > 26.f)
		{
			B.Tube(26.0f, L.RodEnd, 0.32f, 10, FTransform(FVector(0.f, 0.f, 6.9f)));
		}
		B.Box(FVector(L.FrontX, 0.f, 10.0f), FVector(3.2f, 2.2f, 3.4f), 0.2f);
		for (const float S : { -1.f, 1.f })
		{
			B.Box(FVector(L.FrontX + 0.2f, S * 0.85f, 12.85f), FVector(1.4f, 0.32f, 2.4f), 0.1f);
		}
		B.Box(FVector(L.FrontX + 0.2f, 0.f, 12.45f), FVector(0.32f, 0.32f, 1.7f), 0.04f);
		// Selector lever (right side), trigger guard, magazine release, trigger.
		B.Box(FVector(10.0f, 1.72f, 8.7f), FVector(10.5f, 0.16f, 0.9f), 0.05f);
		B.Box(FVector(15.4f, 1.75f, 8.0f), FVector(1.0f, 0.2f, 1.8f), 0.08f);
		B.Box(FVector(5.6f, 0.f, 2.6f), FVector(9.2f, 1.0f, 0.32f), 0.08f);
		B.Box(FVector(1.1f, 0.f, 3.5f), FVector(0.4f, 1.0f, 2.0f), 0.08f);
		B.Box(FVector(10.1f, 0.f, 3.5f), FVector(0.4f, 1.0f, 2.0f), 0.08f);
		B.Box(FVector(10.7f, 0.f, 3.4f), FVector(0.5f, 1.3f, 1.6f), 0.08f);
		B.Prism({ { 4.8f, 4.4f }, { 5.3f, 4.4f }, { 5.2f, 3.5f }, { 4.85f, 2.95f }, { 4.55f, 3.1f }, { 4.75f, 3.6f } }, -0.2f, 0.2f, 0.05f);
	}

	void AkGrips(const TCHAR* W)
	{
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Bakelite);
			GripPrism(B, AkGripOutline, 1.45f, 0.5f);
			SaveKit(B, W, bStippled ? FString::Printf(TEXT("SM_%s_grip_stippled"), W) : FString::Printf(TEXT("SM_%s_Grip"), W));
		}
	}

	void BuildRPK()
	{
		const TCHAR* W = TEXT("RPK");
		{
			FBuilder B;
			FAkLayout L;
			// Long, heavy-profile barrel; the front sight and gas block further out, no cleaning rod (the bipod rides there).
			L.BarrelEnd = 69.f;
			L.BarrelR = 0.95f;
			L.FrontX = 65.2f;
			L.RodEnd = 0.f;
			AkReceiver(B, L);
			// Folded bipod: clamp round the barrel behind the front sight, legs lying back along it.
			B.SetSlot(Steel);
			B.Tube(62.6f, 64.2f, 1.3f, 18, FTransform(FVector(0.f, 0.f, 9.0f)));
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(55.0f, S * 1.05f, 7.55f), FVector(15.0f, 0.5f, 0.55f), 0.12f, FRotator(0.f, S * 1.5f, 0.f));
				B.Box(FVector(47.9f, S * 1.2f, 7.4f), FVector(1.4f, 0.9f, 0.7f), 0.15f);
				B.Box(FVector(62.0f, S * 1.0f, 8.1f), FVector(1.2f, 0.6f, 1.2f), 0.1f);
			}
			SaveKit(B, W, TEXT("SM_RPK_Receiver"));
		}
		{
			// Club-foot stock: the deep toe that sits on the shoulder for sustained fire.
			FBuilder B;
			B.SetSlot(Wood);
			B.Prism({ { -3.0f, 9.4f }, { -9.0f, 8.8f }, { -26.0f, 7.6f }, { -35.2f, 7.8f }, { -35.6f, 6.8f }, { -35.6f, -5.6f }, { -34.4f, -6.2f }, { -30.5f, -5.0f },
				{ -28.0f, -1.6f }, { -25.0f, -1.0f }, { -20.0f, 0.2f }, { -9.0f, 3.9f }, { -4.0f, 5.0f }, { -3.0f, 5.6f } }, -1.95f, 1.95f, 0.5f, 30.f);
			B.SetSlot(Steel);
			B.Prism({ { -35.4f, 8.0f }, { -36.2f, 7.8f }, { -36.2f, -5.9f }, { -35.4f, -6.4f } }, -2.05f, 2.05f, 0.12f);
			B.Box(FVector(-4.5f, 0.f, 9.0f), FVector(3.2f, 1.2f, 0.5f), 0.12f);
			SaveKit(B, W, TEXT("SM_RPK_Stock"));
		}
		AkGrips(W);
		{
			// 75-round drum: the AK feed lips and tower going down into a steel drum with a winding key on the left.
			FBuilder B;
			const FVector MagTop(12.7f, 0.f, 4.5f);
			const TArray<FVector2D> Tower = { { 3.2f, 0.f }, { 3.4f, -3.5f }, { 4.2f, -6.6f }, { -2.4f, -6.6f }, { -2.9f, -2.4f }, { -3.0f, 0.f } };
			Magazine(B, MagTop, Tower, 1.35f, Steel, 0.15f, 0.56f, 5.4f, false);
			const FVector Drum(MagTop.X + 1.2f, 0.f, MagTop.Z - 14.2f);
			B.SetSlot(Steel);
			B.Lathe({ FVector2D(-3.3f, 0.f), FVector2D(-3.3f, 7.6f), FVector2D(-3.0f, 8.5f), FVector2D(3.0f, 8.5f), FVector2D(3.3f, 7.6f), FVector2D(3.3f, 0.f) }, 40, AlongY(Drum), 30.f);
			for (const float Y : { -1.6f, 1.6f })
			{
				B.Lathe({ FVector2D(Y - 0.25f, 8.45f), FVector2D(Y - 0.25f, 8.7f), FVector2D(Y + 0.25f, 8.7f), FVector2D(Y + 0.25f, 8.45f) }, 40, AlongY(Drum), 30.f, true, 0.f, false);
			}
			// Winding key and its folding lever on the left face.
			B.SetSlot(Bright);
			B.Tube(-3.9f, -3.2f, 1.6f, 20, AlongY(Drum));
			B.Box(Drum + FVector(2.4f, -3.95f, 0.f), FVector(4.6f, 0.35f, 0.9f), 0.12f);
			// Pressed stiffening rings in the faces.
			B.SetSlot(Steel);
			for (const float S : { -1.f, 1.f })
			{
				B.Lathe({ FVector2D(S * 3.32f, 4.6f), FVector2D(S * 3.42f, 4.6f), FVector2D(S * 3.42f, 5.4f), FVector2D(S * 3.32f, 5.4f) }, 36, AlongY(Drum), 30.f, true, 0.f, false);
			}
			SaveKit(B, W, TEXT("SM_RPK_Mag"));
		}
	}

	void BuildSaiga()
	{
		const TCHAR* W = TEXT("SAIGA12");
		{
			FBuilder B;
			FAkLayout L;
			// Short, fat 12 gauge barrel with the gas system close in, wide magazine well.
			L.BarrelEnd = 52.f;
			L.BarrelR = 1.15f;
			L.GasEnd = 39.f;
			L.GasBlockX = 40.f;
			L.FrontX = 49.0f;
			L.RodEnd = 0.f;
			L.MagwellHalf = 2.0f;
			AkReceiver(B, L);
			B.SetSlot(Dark);
			B.Tube(51.95f, 52.05f, 0.95f, 18, FTransform(FVector(0.f, 0.f, 9.0f)), 0.f);
			SaveKit(B, W, TEXT("SM_SAIGA12_Receiver"));
		}
		{
			// Black polymer handguard (lower and gas tube cover) with grip ribs.
			FBuilder B;
			B.SetSlot(Poly);
			PrismX(B, { { -2.1f, 9.4f }, { -2.15f, 8.2f }, { -1.95f, 6.6f }, { -1.35f, 5.4f }, { -0.5f, 4.9f }, { 0.5f, 4.9f }, { 1.35f, 5.4f }, { 1.95f, 6.6f }, { 2.15f, 8.2f }, { 2.1f, 9.4f } },
				21.4f, 37.0f, 0.35f, 35.f);
			PrismX(B, { { -1.4f, 10.4f }, { 1.4f, 10.4f }, { 1.45f, 11.3f }, { 1.15f, 12.1f }, { 0.4f, 12.45f }, { -0.4f, 12.45f }, { -1.15f, 12.1f }, { -1.45f, 11.3f } }, 25.6f, 37.6f, 0.25f, 35.f);
			for (float X = 23.f; X < 36.f; X += 1.6f)
			{
				for (const float S : { -1.f, 1.f })
				{
					B.Box(FVector(X, S * 2.12f, 7.4f), FVector(0.5f, 0.12f, 2.6f), 0.05f);
				}
			}
			B.SetSlot(Steel);
			B.Box(FVector(37.9f, 0.f, 11.5f), FVector(0.8f, 2.4f, 1.8f), 0.2f);
			SaveKit(B, W, TEXT("SM_SAIGA12_Handguard"));
		}
		{
			// Side-folding polymer stock on a steel hinge block.
			FBuilder B;
			B.SetSlot(Steel);
			B.Box(FVector(-4.0f, 0.f, 7.4f), FVector(2.0f, 3.4f, 4.4f), 0.25f);
			B.SetSlot(Poly);
			B.Prism({ { -5.0f, 9.6f }, { -12.0f, 9.4f }, { -27.0f, 9.0f }, { -32.6f, 9.4f }, { -33.2f, 8.6f }, { -33.2f, -2.6f }, { -32.2f, -3.4f }, { -27.5f, -2.2f },
				{ -22.0f, 1.8f }, { -12.0f, 5.0f }, { -5.0f, 5.6f } }, -1.8f, 1.8f, 0.55f, 30.f);
			B.SetSlot(Rubber);
			B.Box(FVector(-33.5f, 0.f, 3.0f), FVector(0.7f, 3.8f, 12.6f), 0.3f);
			SaveKit(B, W, TEXT("SM_SAIGA12_Stock"));
		}
		AkGrips(W);
		const FVector MagTop(12.7f, 0.f, 3.0f);
		const TArray<FVector2D> ShellMag = { { 4.6f, 0.f }, { 4.8f, -4.f }, { 5.4f, -9.f }, { 6.2f, -13.5f }, { 6.4f, -14.4f }, { 6.4f, -15.3f }, { -2.9f, -16.4f }, { -3.2f, -15.6f },
			{ -3.6f, -10.f }, { -3.7f, -4.f }, { -3.6f, 0.f } };
		auto Shells = [](FBuilder& B, const FVector& Top)
		{
			// Red plastic hulls on brass heads, staggered in the feed lips.
			for (int32 i = 0; i < 2; ++i)
			{
				const FVector C(Top.X - 3.3f + i * 0.4f, (i ? -0.5f : 0.5f), Top.Z + 0.55f - i * 1.5f);
				B.SetSlot(Brass);
				B.Tube(0.f, 1.1f, 1.06f, 16, FTransform(C));
				B.SetSlot(MarkRed);
				B.Tube(1.1f, 6.6f, 1.0f, 16, FTransform(C));
			}
		};
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			FBuilder B;
			const TArray<FVector2D> Outline = Variant == 1 ? StretchOutline(ShellMag, 1.45f, 1.5f) : ShellMag;
			Magazine(B, MagTop, Outline, 1.95f, Poly, 0.3f, 0.02f, 0.1f, true);
			Shells(B, MagTop);
			if (Variant == 2)
			{
				Magazine(B, MagTop + FVector(0.4f, 4.1f, 0.f), Outline, 1.95f, Poly, 0.3f, 0.02f, 0.1f, false);
				B.SetSlot(Rubber);
				B.Box(FVector(MagTop.X + 1.2f, 2.05f, MagTop.Z - 8.f), FVector(8.6f, 8.2f, 1.1f), 0.2f, FRotator(-6.f, 0.f, 0.f));
			}
			SaveKit(B, W, Variant == 0 ? TEXT("SM_SAIGA12_Mag") : (Variant == 1 ? TEXT("SM_SAIGA12_mag_extended") : TEXT("SM_SAIGA12_mag_fast")));
		}
	}

	// ------------------------------------------------------------------ HK416 (M4 lower, piston upper furniture)

	void BuildHK416()
	{
		const TCHAR* W = TEXT("HK416");
		{
			// Quad rail free-float handguard in FDE: square section, rails on all four faces, cooling slots between.
			FBuilder B;
			B.SetSlot(Fde);
			const float X0 = 17.6f;
			const float X1 = 44.5f;
			const float Cz = 9.4f;
			PrismX(B, CampusMesh::RoundedRect(FVector2D(-2.15f, Cz - 2.15f), FVector2D(2.15f, Cz + 2.15f), 0.9f, 3), X0, X1, 0.12f, 40.f);
			Rail(B, FVector(X0 + 0.3f, 0.f, Cz + 2.15f), X1 - X0 - 0.6f, EFace::Up);
			Rail(B, FVector(X0 + 2.0f, 0.f, Cz - 2.15f), X1 - X0 - 4.0f, EFace::Down);
			Rail(B, FVector(X1 - 12.0f, 2.15f, Cz), 11.5f, EFace::Right);
			Rail(B, FVector(X1 - 12.0f, -2.15f, Cz), 11.5f, EFace::Left);
			B.SetSlot(Dark);
			B.SetCavity(1.f);
			for (float X = X0 + 2.f; X < X1 - 13.f; X += 2.4f)
			{
				for (const float S : { -1.f, 1.f })
				{
					B.Box(FVector(X, S * 2.16f, Cz), FVector(1.3f, 0.03f, 2.0f), 0.f);
				}
			}
			B.SetCavity(0.f);
			// Barrel nut / end cap and the piston gas block just ahead of the rail.
			B.SetSlot(Steel);
			B.Tube(X1, X1 + 1.6f, 1.25f, 20, FTransform(FVector(0.f, 0.f, 9.5f)));
			B.Tube(X1 + 1.6f, 50.0f, 0.95f, 20, FTransform(FVector(0.f, 0.f, 9.5f)));
			SaveKit(B, W, TEXT("SM_HK416_Handguard"));
		}
		{
			FBuilder B;
			B.SetSlot(Fde);
			B.Lathe({ FVector2D(-2.6f, 1.75f), FVector2D(-1.8f, 1.75f) }, 8, FTransform(FVector(0.f, 0.f, 8.9f)), 35.f, false, 22.5f);
			B.SetSlot(Anod);
			B.Tube(-22.8f, -2.6f, 1.55f, 24, FTransform(FVector(0.f, 0.f, 8.9f)));
			// HK-style enhanced stock: deeper cheek and a storage compartment.
			B.SetSlot(Fde);
			B.Prism({ { -11.8f, 11.6f }, { -22.6f, 11.9f }, { -24.0f, 12.6f }, { -24.8f, 11.9f }, { -24.8f, 1.2f }, { -23.8f, 0.6f }, { -21.6f, 1.2f }, { -15.2f, 5.9f },
				{ -13.0f, 6.6f }, { -11.8f, 7.4f } }, -2.0f, 2.0f, 0.4f);
			B.SetSlot(Rubber);
			B.Box(FVector(-25.2f, 0.f, 6.6f), FVector(0.8f, 3.9f, 11.6f), 0.35f);
			SaveKit(B, W, TEXT("SM_HK416_Stock"));
		}
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Poly);
			GripPrism(B, A2Grip, 1.45f, 0.5f);
			SaveKit(B, W, bStippled ? TEXT("SM_HK416_grip_stippled") : TEXT("SM_HK416_Grip"));
		}
		const FVector MagTop(12.8f, 0.f, 4.3f);
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			// Steel-bodied magazines (the HK pattern), dark.
			FBuilder B;
			const TArray<FVector2D> Outline = Variant == 1 ? StretchOutline(StanagOutline, 1.42f, 3.f) : StanagOutline;
			Magazine(B, MagTop, Outline, 1.25f, Matte, 0.2f, 0.48f, 5.6f, true);
			if (Variant == 2)
			{
				Magazine(B, MagTop + FVector(0.6f, 2.75f, 0.f), Outline, 1.25f, Matte, 0.2f, 0.48f, 5.6f, false);
				B.SetSlot(Rubber);
				B.Box(FVector(MagTop.X + 1.9f, 1.375f, MagTop.Z - 10.f), FVector(7.4f, 5.7f, 1.1f), 0.2f, FRotator(-10.f, 0.f, 0.f));
			}
			SaveKit(B, W, Variant == 0 ? TEXT("SM_HK416_Mag") : (Variant == 1 ? TEXT("SM_HK416_mag_extended") : TEXT("SM_HK416_mag_fast")));
		}
		for (const bool bUp : { true, false })
		{
			FBuilder F;
			FlipSight(F, 42.6f, 12.55f, 14.45f, false, bUp);
			SaveKit(F, W, bUp ? TEXT("SM_HK416_FrontSight") : TEXT("SM_HK416_FrontSightFolded"));
		}
	}

	// ------------------------------------------------------------------ UMP45

	void BuildUMP()
	{
		const TCHAR* W = TEXT("UMP45");
		{
			FBuilder B;
			B.SetSlot(Poly);
			// One-piece polymer receiver: slab sides, the front third its own handguard.
			B.Prism({ { -6.0f, 11.6f }, { 29.2f, 11.6f }, { 30.0f, 10.9f }, { 30.0f, 6.4f }, { 29.2f, 5.6f }, { 16.4f, 5.6f }, { 16.0f, 4.6f }, { 9.8f, 4.6f },
				{ 9.4f, 5.4f }, { 0.4f, 5.4f }, { -2.4f, 6.4f }, { -6.0f, 7.2f } }, -1.75f, 1.75f, 0.45f, 30.f);
			B.Box(FVector(22.5f, 0.f, 8.6f), FVector(13.0f, 3.9f, 5.4f), 0.6f);
			// Grip ribs on the handguard sides.
			for (float X = 18.f; X < 28.f; X += 1.4f)
			{
				for (const float S : { -1.f, 1.f })
				{
					B.Box(FVector(X, S * 1.98f, 7.6f), FVector(0.45f, 0.1f, 2.6f), 0.04f);
				}
			}
			// Integral trigger guard.
			B.Box(FVector(5.4f, 0.f, 2.3f), FVector(8.2f, 1.1f, 0.6f), 0.2f);
			B.Box(FVector(9.6f, 0.f, 3.5f), FVector(0.9f, 1.1f, 2.6f), 0.2f);
			B.Box(FVector(1.2f, 0.f, 3.8f), FVector(0.8f, 1.1f, 2.6f), 0.2f, FRotator(-20.f, 0.f, 0.f));
			// Magazine well and paddle release.
			B.Box(FVector(13.2f, 0.f, 3.8f), FVector(6.2f, 3.0f, 2.2f), 0.3f);
			B.Box(FVector(9.5f, 0.f, 2.6f), FVector(0.5f, 1.6f, 1.6f), 0.1f);
			B.SetSlot(Anod);
			Rail(B, FVector(-4.6f, 0.f, 11.6f), 32.0f, EFace::Up);
			Rail(B, FVector(22.5f, 0.f, 5.6f), 6.5f, EFace::Down);
			Rail(B, FVector(23.0f, 1.95f, 8.6f), 5.5f, EFace::Right);
			Rail(B, FVector(23.0f, -1.95f, 8.6f), 5.5f, EFace::Left);
			B.SetSlot(Dark);
			B.SetCavity(1.f);
			B.Box(FVector(13.2f, 0.f, 2.68f), FVector(5.4f, 2.4f, 0.04f), 0.f);
			B.Box(FVector(12.0f, 1.77f, 9.2f), FVector(5.0f, 0.03f, 1.4f), 0.f);
			B.SetCavity(0.f);
			// Barrel stub and its thread protector.
			B.SetSlot(Steel);
			B.Tube(29.5f, 35.0f, 0.85f, 18, FTransform(FVector(0.f, 0.f, 8.6f)));
			B.Tube(35.0f, 36.2f, 1.0f, 18, FTransform(FVector(0.f, 0.f, 8.6f)));
			// Selector, trigger.
			Pin(B, FVector(2.0f, -1.78f, 7.0f), 0.55f, 0.3f);
			B.Box(FVector(1.4f, -1.9f, 7.5f), FVector(1.5f, 0.16f, 0.45f), 0.05f, FRotator(25.f, 0.f, 0.f));
			B.Prism({ { 5.2f, 5.4f }, { 5.7f, 5.4f }, { 5.6f, 4.4f }, { 5.25f, 3.6f }, { 4.95f, 3.8f }, { 5.15f, 4.5f } }, -0.18f, 0.18f, 0.05f);
			SaveKit(B, W, TEXT("SM_UMP45_Receiver"));
		}
		{
			// Non-reciprocating charging handle at the left front, travels in its slot on the empty reload.
			FBuilder B;
			B.SetSlot(Poly);
			B.Box(FVector(25.8f, -2.25f, 10.4f), FVector(1.8f, 0.9f, 1.1f), 0.3f);
			B.SetSlot(Steel);
			B.Box(FVector(25.8f, -1.85f, 10.4f), FVector(0.8f, 0.4f, 0.5f), 0.08f);
			SaveKit(B, W, TEXT("SM_UMP45_Charging"));
		}
		{
			// Side-folding skeleton stock: top bar, diagonal strut, butt.
			FBuilder B;
			B.SetSlot(Poly);
			B.Box(FVector(-7.0f, 0.f, 8.8f), FVector(2.4f, 3.4f, 4.6f), 0.4f);
			B.Box(FVector(-17.0f, 0.f, 10.0f), FVector(20.0f, 2.2f, 1.7f), 0.5f);
			B.Box(FVector(-17.0f, 0.f, 4.2f), FVector(20.6f, 1.8f, 1.4f), 0.5f, FRotator(-17.f, 0.f, 0.f));
			B.Prism({ { -26.0f, 11.0f }, { -28.6f, 11.0f }, { -28.6f, -1.6f }, { -27.4f, -2.0f }, { -26.0f, -0.8f } }, -1.6f, 1.6f, 0.45f, 30.f);
			B.SetSlot(Rubber);
			B.Box(FVector(-28.9f, 0.f, 4.6f), FVector(0.6f, 3.4f, 12.8f), 0.25f);
			SaveKit(B, W, TEXT("SM_UMP45_Stock"));
		}
		const TArray<FVector2D> UmpGrip = { { 1.2f, 5.4f }, { -3.4f, 5.4f }, { -3.8f, 3.4f }, { -4.6f, -1.0f }, { -5.3f, -4.8f }, { -4.9f, -6.2f }, { -2.9f, -6.5f }, { -1.7f, -5.6f },
			{ -1.2f, -4.0f }, { -0.4f, -1.8f }, { 0.2f, 0.4f }, { 0.9f, 3.4f } };
		for (const bool bStippled : { false, true })
		{
			FBuilder B;
			B.SetSlot(bStippled ? Stipple : Poly);
			GripPrism(B, UmpGrip, 1.55f, 0.5f, 1.2f);
			SaveKit(B, W, bStippled ? TEXT("SM_UMP45_grip_stippled") : TEXT("SM_UMP45_Grip"));
		}
		const FVector MagTop(13.2f, 0.f, 4.4f);
		// .45 ACP box magazine: straight, translucent-free polymer with a witness window strip.
		const TArray<FVector2D> UmpMag = { { 2.4f, 0.f }, { 2.6f, -12.8f }, { 2.9f, -13.6f }, { 2.9f, -14.4f }, { -2.6f, -14.8f }, { -2.8f, -14.0f }, { -2.6f, 0.f } };
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			FBuilder B;
			const TArray<FVector2D> Outline = Variant == 1 ? StretchOutline(UmpMag, 1.45f, 0.5f) : UmpMag;
			Magazine(B, MagTop, Outline, 1.2f, Poly, 0.25f, 0.55f, 3.2f, true);
			if (Variant == 2)
			{
				Magazine(B, MagTop + FVector(0.3f, 2.65f, 0.f), Outline, 1.2f, Poly, 0.25f, 0.55f, 3.2f, false);
				B.SetSlot(Rubber);
				B.Box(FVector(MagTop.X, 1.32f, MagTop.Z - 8.f), FVector(6.2f, 5.4f, 1.0f), 0.2f);
			}
			SaveKit(B, W, Variant == 0 ? TEXT("SM_UMP45_Mag") : (Variant == 1 ? TEXT("SM_UMP45_mag_extended") : TEXT("SM_UMP45_mag_fast")));
		}
		for (const bool bUp : { true, false })
		{
			FBuilder B;
			FlipSight(B, -2.6f, 12.55f, 14.35f, true, bUp);
			SaveKit(B, W, bUp ? TEXT("SM_UMP45_RearSight") : TEXT("SM_UMP45_RearSightFolded"));
			FBuilder F;
			FlipSight(F, 26.5f, 12.55f, 14.35f, false, bUp);
			SaveKit(F, W, bUp ? TEXT("SM_UMP45_FrontSight") : TEXT("SM_UMP45_FrontSightFolded"));
		}
	}

	// ------------------------------------------------------------------ attachments

	/** Rail clamp sitting on a 1913 rail whose top is at Z = 0. */
	void RailClamp(FBuilder& B, float Length, float Width = 2.5f, float Height = 1.1f)
	{
		B.Box(FVector(0.f, 0.f, Height * 0.5f), FVector(Length, Width, Height), 0.15f);
		for (const float S : { -1.f, 1.f })
		{
			B.Box(FVector(0.f, S * (Width * 0.5f - 0.05f), -0.25f), FVector(Length - 0.6f, 0.36f, 0.9f), 0.1f);
		}
	}

	void BuildAttachments()
	{
		const TCHAR* A = TEXT("Attachments");
		{
			// Open reflex with a large window (Holosun 510 / "slimline" class): low base on the rail clamp, rear control
			// pad, and a thin raked hoop, so at the eye it reads as a big window in a hairline frame.
			FBuilder B;
			B.SetSlot(Anod);
			RailClamp(B, 4.2f);
			B.SetSlot(Steel);
			Pin(B, FVector(0.4f, 0.f, -0.45f), 0.3f, 3.0f, 10);
			Pin(B, FVector(0.4f, 1.7f, -0.45f), 0.55f, 0.5f, 6);
			// Everything behind the hoop stays under the window's lower edge (Z 1.72): it is the closest thing to the eye.
			B.SetSlot(Matte);
			B.Box(FVector(-0.2f, 0.f, 1.32f), FVector(4.8f, 3.0f, 0.45f), 0.12f);
			B.Box(FVector(-1.4f, 1.62f, 1.3f), FVector(1.6f, 0.3f, 0.5f), 0.08f);
			const FRotator Rake(-8.f, 0.f, 0.f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(0.9f, S * 1.72f, 2.92f), FVector(0.7f, 0.22f, 2.85f), 0.06f, Rake);
			}
			B.Box(FVector(0.72f, 0.f, 4.22f), FVector(0.7f, 3.66f, 0.24f), 0.06f, Rake);
			B.Box(FVector(0.9f, 0.f, 1.62f), FVector(0.7f, 3.66f, 0.2f), 0.05f);
			B.Box(FVector(-1.15f, 0.f, 1.61f), FVector(0.4f, 0.5f, 0.2f), 0.05f);
			B.SetSlot(Emitter);
			B.Box(FVector(-0.94f, 0.f, 1.64f), FVector(0.03f, 0.1f, 0.1f), 0.f);
			B.SetSlot(Rubber);
			B.Box(FVector(-1.4f, 1.79f, 1.32f), FVector(1.0f, 0.06f, 0.3f), 0.02f);
			SaveKit(B, A, TEXT("SM_Att_RedDot"));
		}
		{
			// Holographic sight (EXPS3 style): QD base, rear control module, hooded double-pane window.
			FBuilder B;
			B.SetSlot(Matte);
			B.Box(FVector(-0.8f, 0.f, 0.6f), FVector(9.6f, 3.0f, 1.2f), 0.2f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(-0.8f, S * 1.25f, -0.25f), FVector(8.0f, 0.4f, 0.9f), 0.1f);
			}
			B.SetSlot(Anod);
			B.Box(FVector(-3.0f, -1.72f, 0.2f), FVector(1.4f, 0.3f, 0.9f), 0.1f);
			B.Box(FVector(1.6f, -1.72f, 0.2f), FVector(1.4f, 0.3f, 0.9f), 0.1f);
			// The rear module stays under the window line so it never cuts into the sight picture.
			B.SetSlot(Matte);
			B.Box(FVector(-3.9f, 0.f, 1.85f), FVector(2.6f, 3.2f, 1.3f), 0.3f);
			for (const float S : { -1.f, 1.f })
			{
				B.Prism({ { -0.6f, 1.2f }, { 2.7f, 1.2f }, { 2.7f, 4.9f }, { 2.2f, 5.4f }, { -0.1f, 5.4f }, { -0.6f, 4.9f } }, S * 1.56f, S * 1.8f, 0.08f);
			}
			B.Box(FVector(1.05f, 0.f, 5.27f), FVector(3.3f, 3.6f, 0.26f), 0.1f);
			B.Box(FVector(1.05f, 0.f, 2.28f), FVector(3.3f, 3.6f, 0.26f), 0.08f);
			B.Box(FVector(2.65f, 0.f, 1.4f), FVector(0.35f, 3.5f, 0.4f), 0.08f);
			Pin(B, FVector(3.4f, 0.f, 0.95f), 0.72f, 3.2f, 16);
			B.SetSlot(Rubber);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(-3.9f, S * 0.7f, 2.55f), FVector(0.9f, 0.6f, 0.25f), 0.08f);
			}
			SaveKit(B, A, TEXT("SM_Att_Holo"));
		}
		{
			// Prism scope (TA31 style): TA51 flat-top mount with knurled thumb nuts, forged housing around the prism,
			// objective bell, ocular with rubber guard, capped windage / elevation adjusters and the ribbed fibre-optic
			// shroud along the top deck.
			FBuilder B;
			const float Axis = 4.0f;
			const FTransform T(FVector(0.f, 0.f, Axis));
			B.SetSlot(Matte);
			RailClamp(B, 7.6f, 2.6f, 1.2f);
			B.SetSlot(Steel);
			for (const float X : { -2.4f, 2.4f })
			{
				Pin(B, FVector(X, 0.f, 0.15f), 0.28f, 3.4f, 8);
				const FTransform NutT(FQuat(FVector::ZAxisVector, -HALF_PI), FVector(X, -1.6f, 0.15f));
				B.Lathe({ FVector2D(0.f, 0.55f), FVector2D(0.05f, 0.68f), FVector2D(0.55f, 0.68f), FVector2D(0.62f, 0.55f), FVector2D(0.62f, 0.f) }, 24, NutT, 30.f);
				Knurl(B, NutT, 0.12f, 0.5f, 0.68f, 18, 0.05f, 0.1f);
			}
			// The tubes are uncapped outer shells and nothing reaches into the bore, so the eye looks straight through
			// the eyepiece to the (magnified) world, as the real optic relays it.
			B.SetSlot(Anod);
			PrismX(B, { { -1.4f, 1.2f }, { 1.4f, 1.2f }, { 1.55f, 2.3f }, { 1.85f, 3.2f }, { 1.85f, 4.7f }, { 1.5f, 5.4f }, { 1.0f, 5.72f },
				{ -1.0f, 5.72f }, { -1.5f, 5.4f }, { -1.85f, 4.7f }, { -1.85f, 3.2f }, { -1.55f, 2.3f } }, -4.4f, 4.4f, 0.22f, 40.f);
			B.Lathe({ FVector2D(4.2f, 1.7f), FVector2D(5.0f, 1.8f), FVector2D(6.0f, 2.16f), FVector2D(8.55f, 2.2f), FVector2D(8.7f, 2.3f), FVector2D(9.05f, 2.3f) },
				48, T, 35.f, true, 0.f, false);
			B.HollowTube(8.95f, 9.35f, 2.3f, 1.98f, 48, T, 0.06f);
			B.Lathe({ FVector2D(-8.55f, 1.98f), FVector2D(-6.1f, 1.98f), FVector2D(-5.2f, 1.8f), FVector2D(-4.2f, 1.7f) }, 48, T, 35.f, true, 0.f, false);
			B.Tube(-6.45f, -6.25f, 2.03f, 48, T, 0.03f);
			Turret(B, FTransform(FQuat(FVector::ZAxisVector, HALF_PI), FVector(0.6f, 0.f, Axis)), 1.8f, 0.85f, 0.78f, 24);
			Turret(B, AlongZ(FVector(2.75f, 0.f, 0.f)), 5.6f, 0.7f, 0.66f, 22);
			// Fibre-optic collector: a dark rod under a ribbed shroud on the deck (it gathers light, it doesn't glow).
			const FTransform FibreT(FVector(0.f, 0.f, 6.08f));
			B.SetSlot(Dark);
			B.Tube(-3.7f, 1.7f, 0.3f, 16, FibreT, 0.05f);
			B.SetSlot(Matte);
			B.Box(FVector(-1.0f, 0.f, 5.86f), FVector(5.6f, 0.9f, 0.3f), 0.1f);
			for (int32 k = 0; k < 11; ++k)
			{
				const float X = -3.5f + k * 0.5f;
				B.Tube(X - 0.13f, X + 0.13f, 0.42f, 16, FibreT, 0.04f);
			}
			B.Tube(-3.95f, -3.6f, 0.46f, 16, FibreT, 0.08f);
			B.Tube(1.6f, 1.95f, 0.46f, 16, FibreT, 0.08f);
			B.SetSlot(EyeRubber);
			B.Lathe({ FVector2D(-9.5f, 1.82f), FVector2D(-9.3f, 1.98f), FVector2D(-8.7f, 2.0f), FVector2D(-8.7f, 1.9f), FVector2D(-9.5f, 1.76f), FVector2D(-9.5f, 1.82f) },
				48, T, 35.f, true, 0.f, false);
			// Lens retaining rings just inside the eyecup and the objective: the glass sits in a dark bezel, recessed,
			// rather than the tube reading as an open pipe. Matte black, as the eyecup: seen edge-on round the picture.
			B.HollowTube(-9.2f, -8.75f, 1.9f, 1.55f, 48, T, 0.f);
			B.HollowTube(8.3f, 8.65f, 2.0f, 1.75f, 48, T, 0.f);
			SaveKit(B, A, TEXT("SM_Att_Acog"));
		}
		{
			// 6x rifle scope: 30 mm tube on two-piece rings, 50 mm objective with a sunshade lip, capped elevation /
			// windage turrets and a side-focus wheel on the saddle, power ring with throw lever, fast-focus eyepiece and
			// rubber eyecup. Rear lens at X -13.7, objective at +14.0 (see optic_sniper).
			FBuilder B;
			const float Axis = 4.7f;
			const FTransform T(FVector(0.f, 0.f, Axis));
			for (const float X : { -4.6f, 4.2f })
			{
				B.SetSlot(Matte);
				B.SetFrame(FTransform(FVector(X, 0.f, 0.f)));
				RailClamp(B, 2.0f, 2.7f, 1.1f);
				B.ResetFrame();
				B.SetSlot(Anod);
				PrismX(B, { { -1.3f, 1.0f }, { 1.3f, 1.0f }, { 1.12f, Axis - 1.1f }, { -1.12f, Axis - 1.1f } }, X - 0.9f, X + 0.9f, 0.14f, 40.f);
				B.HollowTube(X - 0.9f, X + 0.9f, 1.95f, 1.5f, 48, T, 0.1f);
				for (const float S : { -1.f, 1.f })
				{
					B.SetSlot(Anod);
					B.Box(FVector(X, S * 1.92f, Axis + 0.05f), FVector(1.8f, 0.5f, 0.75f), 0.12f);
					B.SetSlot(Dark);
					B.Box(FVector(X, S * 1.92f, Axis - 0.04f), FVector(1.82f, 0.52f, 0.04f), 0.f);
					B.SetSlot(Steel);
					for (const float DX : { -0.45f, 0.45f })
					{
						B.Tube(0.f, 0.16f, 0.2f, 12, AlongZ(FVector(X + DX, S * 1.92f, Axis + 0.42f)), 0.04f);
					}
				}
				Pin(B, FVector(X, 1.55f, 0.35f), 0.45f, 0.5f, 6);
				Pin(B, FVector(X, -1.5f, 0.35f), 0.32f, 0.4f, 12);
			}
			B.SetSlot(Anod);
			B.Lathe({
				FVector2D(-13.85f, 1.92f), FVector2D(-13.8f, 2.12f), FVector2D(-13.65f, 2.2f), FVector2D(-11.9f, 2.2f), FVector2D(-11.8f, 2.12f), FVector2D(-11.7f, 2.2f),
				FVector2D(-10.7f, 2.2f), FVector2D(-10.45f, 2.06f), FVector2D(-9.5f, 1.86f), FVector2D(-9.1f, 1.8f), FVector2D(-9.1f, 1.5f),
				FVector2D(-2.5f, 1.5f), FVector2D(-2.3f, 1.72f), FVector2D(-2.0f, 1.95f), FVector2D(2.0f, 1.95f), FVector2D(2.3f, 1.72f), FVector2D(2.5f, 1.5f),
				FVector2D(6.0f, 1.5f), FVector2D(6.6f, 1.58f), FVector2D(7.4f, 1.84f), FVector2D(8.4f, 2.24f), FVector2D(9.4f, 2.6f), FVector2D(10.4f, 2.84f),
				FVector2D(11.3f, 2.93f), FVector2D(13.4f, 2.93f), FVector2D(13.55f, 2.86f), FVector2D(13.7f, 2.93f), FVector2D(14.5f, 2.93f) },
				64, T, 32.f, true, 0.f, false);
			// Fast-focus eyepiece grip, power ring and its throw lever.
			Knurl(B, T, -13.5f, -12.1f, 2.2f, 56, 0.06f, 0.12f);
			B.Lathe({ FVector2D(-9.15f, 1.6f), FVector2D(-9.15f, 1.78f), FVector2D(-9.05f, 1.86f), FVector2D(-7.3f, 1.86f), FVector2D(-7.2f, 1.78f), FVector2D(-7.2f, 1.6f) },
				48, T, 30.f);
			Knurl(B, T, -8.85f, -7.95f, 1.86f, 30, 0.07f, 0.16f);
			B.SetFrame(T);
			B.Box(FVector(-7.65f, 2.12f, 0.35f), FVector(0.6f, 0.9f, 0.42f), 0.12f, FRotator(0.f, 0.f, -15.f));
			B.ResetFrame();
			B.SetSlot(Marking);
			Knurl(B, T, -7.62f, -7.42f, 1.86f, 7, 0.012f, 0.05f, -0.9f);
			Knurl(B, T, -10.62f, -10.5f, 2.14f, 1, 0.012f, 0.08f);
			Turret(B, AlongZ(FVector(0.f, 0.f, Axis)), 1.95f, 1.5f, 1.28f, 36);
			Turret(B, FTransform(FQuat(FVector::ZAxisVector, HALF_PI), FVector(0.f, 0.f, Axis)), 1.95f, 1.25f, 1.28f, 36);
			Turret(B, FTransform(FQuat(FVector::ZAxisVector, -HALF_PI), FVector(0.f, 0.f, Axis)), 1.95f, 0.9f, 1.7f, 44);
			// Sunshade lip, lens retainers (the glass sits recessed in a dark bezel), eyecup.
			B.SetSlot(Anod);
			B.HollowTube(14.3f, 15.2f, 2.97f, 2.58f, 64, T, 0.08f);
			B.SetSlot(EyeRubber);
			B.HollowTube(13.85f, 14.3f, 2.62f, 2.42f, 48, T, 0.f);
			B.HollowTube(-13.95f, -13.55f, 1.96f, 1.78f, 48, T, 0.f);
			B.SetSlot(EyeRubber);
			B.Lathe({ FVector2D(-14.8f, 1.95f), FVector2D(-14.65f, 2.28f), FVector2D(-13.8f, 2.3f), FVector2D(-13.8f, 2.12f), FVector2D(-14.8f, 1.88f), FVector2D(-14.8f, 1.95f) },
				64, T, 35.f, true, 0.f, false);
			SaveKit(B, A, TEXT("SM_Att_Sniper"));
		}
		{
			// Suppressor: hex wrench collar, fluted can, front cap.
			FBuilder B;
			B.SetSlot(Matte);
			B.Lathe({ FVector2D(0.f, 1.15f), FVector2D(2.2f, 1.15f) }, 6, FTransform::Identity, 35.f, false, 30.f);
			B.Lathe({ FVector2D(2.2f, 1.2f), FVector2D(2.4f, 1.55f), FVector2D(2.8f, 1.95f), FVector2D(5.8f, 1.95f), FVector2D(6.0f, 1.86f), FVector2D(6.5f, 1.86f), FVector2D(6.7f, 1.95f),
				FVector2D(12.8f, 1.95f), FVector2D(13.0f, 1.86f), FVector2D(13.5f, 1.86f), FVector2D(13.7f, 1.95f), FVector2D(19.0f, 1.95f), FVector2D(19.6f, 1.75f), FVector2D(20.4f, 1.6f),
				FVector2D(20.6f, 1.2f), FVector2D(20.6f, 0.45f) }, 32);
			B.SetSlot(Dark);
			B.Tube(20.45f, 20.62f, 0.45f, 16);
			SaveKit(B, A, TEXT("SM_Att_Suppressor"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Lathe({ FVector2D(0.f, 0.9f), FVector2D(0.2f, 1.15f), FVector2D(5.4f, 1.15f), FVector2D(5.6f, 0.95f) }, 24);
			B.SetSlot(Dark);
			for (const float X : { 1.6f, 2.8f, 4.0f })
			{
				B.Box(FVector(X, 0.f, 1.13f), FVector(0.55f, 1.2f, 0.06f), 0.f);
			}
			for (const float S : { -1.f, 1.f })
			{
				for (const float X : { 2.2f, 3.6f })
				{
					B.Box(FVector(X, S * 1.13f, 0.f), FVector(0.55f, 0.06f, 0.9f), 0.f);
				}
			}
			B.Tube(5.55f, 5.62f, 0.5f, 12);
			SaveKit(B, A, TEXT("SM_Att_Comp"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Tube(0.f, 2.0f, 1.05f, 20);
			for (int32 k = 0; k < 3; ++k)
			{
				const float Ang = k * 2.f * PI / 3.f;
				BoxRolled(B, FVector(3.9f, FMath::Sin(Ang) * 0.8f, FMath::Cos(Ang) * 0.8f), FVector(3.8f, 0.55f, 0.38f), -Ang, 0.08f);
			}
			B.SetSlot(Dark);
			B.Tube(2.0f, 5.8f, 0.55f, 12);
			SaveKit(B, A, TEXT("SM_Att_FlashHider3"));
		}
		{
			FBuilder B;
			B.SetSlot(Steel);
			B.Tube(0.f, 10.f, 1.f, 24, FTransform::Identity, 0.04f);
			B.SetSlot(Dark);
			B.Tube(9.9f, 10.02f, 0.42f, 12);
			SaveKit(B, A, TEXT("SM_Att_BarrelTube"));
		}
		{
			// PEQ-style aiming laser on a short M-LOK rail at 3 o'clock (outward = +Y).
			FBuilder B;
			B.SetSlot(Anod);
			Rail(B, FVector(-2.0f, 0.f, 0.f), 4.0f, EFace::Right);
			B.SetSlot(Matte);
			B.Box(FVector(0.f, 1.25f, 0.f), FVector(3.6f, 0.7f, 2.6f), 0.15f);
			B.Box(FVector(0.4f, 2.55f, 0.f), FVector(6.4f, 1.9f, 3.2f), 0.35f);
			B.SetSlot(Steel);
			B.Tube(3.55f, 3.75f, 0.45f, 16, FTransform(FVector(0.f, 2.55f, 0.75f)));
			B.Tube(3.55f, 3.75f, 0.52f, 16, FTransform(FVector(0.f, 2.55f, -0.65f)));
			B.SetSlot(Emitter);
			B.Tube(3.74f, 3.78f, 0.3f, 16, FTransform(FVector(0.f, 2.55f, 0.75f)));
			B.SetSlot(Dark);
			B.Tube(3.74f, 3.78f, 0.38f, 16, FTransform(FVector(0.f, 2.55f, -0.65f)));
			B.SetSlot(Rubber);
			B.Box(FVector(-0.8f, 2.55f, 1.65f), FVector(1.2f, 1.2f, 0.16f), 0.05f);
			SaveKit(B, A, TEXT("SM_Att_Laser"));
		}
		{
			// Skeletonised stock on its own buffer tube.
			FBuilder B;
			B.SetSlot(Anod);
			B.Tube(-20.5f, 0.f, 1.55f, 24);
			B.Lathe({ FVector2D(-0.8f, 1.75f), FVector2D(0.f, 1.75f) }, 8, FTransform::Identity, 35.f, false, 22.5f);
			B.SetSlot(Fde);
			B.Box(FVector(-14.0f, 0.f, 1.6f), FVector(8.0f, 1.6f, 0.9f), 0.25f);
			B.Prism({ { -18.8f, 2.6f }, { -22.0f, 2.8f }, { -22.6f, 2.0f }, { -22.6f, -8.8f }, { -21.8f, -9.4f }, { -18.8f, -8.4f } }, -1.6f, 1.6f, 0.3f);
			B.Box(FVector(-15.25f, 0.f, -4.7f), FVector(10.8f, 1.2f, 0.9f), 0.25f, FRotator(37.8f, 0.f, 0.f));
			B.Box(FVector(-11.6f, 0.f, -1.6f), FVector(1.8f, 1.0f, 0.6f), 0.15f);
			B.SetSlot(Rubber);
			B.Box(FVector(-22.9f, 0.f, -3.3f), FVector(0.6f, 3.3f, 12.4f), 0.28f);
			SaveKit(B, A, TEXT("SM_Att_StockTac"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			B.Tube(-20.5f, 0.f, 1.55f, 24);
			B.SetSlot(Poly);
			B.Prism({ { -6.0f, 2.6f }, { -23.6f, 2.9f }, { -24.6f, 2.2f }, { -24.6f, -9.2f }, { -23.6f, -9.8f }, { -12.0f, -4.2f }, { -7.0f, -2.0f } }, -1.9f, 1.9f, 0.4f, 30.f);
			B.Box(FVector(-15.5f, 0.f, 3.5f), FVector(13.0f, 2.8f, 1.4f), 0.4f);
			B.SetSlot(Steel);
			for (const float X : { -11.0f, -20.0f })
			{
				B.Tube(-0.4f, 0.4f, 0.35f, 8, AlongZ(FVector(X, 0.f, 2.9f)));
			}
			B.Tube(-0.8f, 0.8f, 0.6f, 12, AlongZ(FVector(-21.0f, 0.f, -10.4f)));
			B.SetSlot(Rubber);
			B.Box(FVector(-24.9f, 0.f, -3.4f), FVector(0.6f, 3.8f, 12.6f), 0.28f);
			SaveKit(B, A, TEXT("SM_Att_StockHeavy"));
		}
		{
			// Vertical grip on its own rail section; the anchor is the handguard's bottom face.
			FBuilder B;
			B.SetSlot(Anod);
			Rail(B, FVector(-2.4f, 0.f, 0.f), 4.8f, EFace::Down);
			B.SetSlot(Poly);
			B.Box(FVector(0.f, 0.f, -1.35f), FVector(4.2f, 2.7f, 0.8f), 0.2f);
			B.Lathe({ FVector2D(-11.6f, 1.1f), FVector2D(-11.2f, 1.45f), FVector2D(-10.4f, 1.6f), FVector2D(-7.8f, 1.56f), FVector2D(-7.2f, 1.4f), FVector2D(-6.6f, 1.55f),
				FVector2D(-5.2f, 1.52f), FVector2D(-4.6f, 1.38f), FVector2D(-4.0f, 1.5f), FVector2D(-2.2f, 1.45f), FVector2D(-1.7f, 1.25f) }, 24, AlongZ(FVector::ZeroVector), 32.f);
			SaveKit(B, A, TEXT("SM_Att_VertGrip"));
		}
		{
			FBuilder B;
			B.SetSlot(Anod);
			Rail(B, FVector(-3.6f, 0.f, 0.f), 7.2f, EFace::Down);
			B.SetSlot(Poly);
			B.Prism({ { -3.6f, -0.9f }, { 3.4f, -0.9f }, { 2.6f, -1.8f }, { -1.2f, -3.4f }, { -3.4f, -2.6f } }, -1.3f, 1.3f, 0.35f);
			SaveKit(B, A, TEXT("SM_Att_AngledGrip"));
		}
	}

	// ------------------------------------------------------------------ pistol (Poly Haven service pistol, re-framed)

	const TCHAR* PistolPieces[] = { TEXT("pistol_a"), TEXT("slide_a"), TEXT("magazine_loaded"), TEXT("hammer_a"), TEXT("trigger_a") };

	UStaticMesh* LoadPistolPiece(const TCHAR* Piece)
	{
		const FString Name = FString(TEXT("service_pistol_")) + Piece;
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Campus/Props/service_pistol/service_pistol/StaticMeshes/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** Copies an imported mesh into the weapon kit with a new pivot / orientation. */
	UStaticMesh* Reframe(UStaticMesh* Src, const FTransform& T, const FString& PackageName)
	{
		const FMeshDescription* SrcMD = Src ? Src->GetMeshDescription(0) : nullptr;
		if (!SrcMD)
		{
			return nullptr;
		}
		FMeshDescription Copy = *SrcMD;
		FStaticMeshOperations::ApplyTransform(Copy, T, true);
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
		Mesh->GetSourceModel(0).BuildSettings = Src->GetSourceModel(0).BuildSettings;
		Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;
		FMeshDescription* MD = Mesh->CreateMeshDescription(0);
		*MD = MoveTemp(Copy);
		Mesh->CommitMeshDescription(0);
		Mesh->GetStaticMaterials() = Src->GetStaticMaterials();
		Mesh->GetNaniteSettings().bEnabled = false;
		Mesh->Build(false);
		Mesh->PostEditChange();
		SaveAsset(Mesh);
		return Mesh;
	}

	void BuildPistol()
	{
		UStaticMesh* Frame = LoadPistolPiece(TEXT("pistol_a"));
		UStaticMesh* Slide = LoadPistolPiece(TEXT("slide_a"));
		UStaticMesh* Hammer = LoadPistolPiece(TEXT("hammer_a"));
		if (!Frame || !Slide || !Hammer)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("Service pistol pieces missing; the X16 keeps the template pistol"));
			return;
		}
		// Imported axes: the longest frame extent is the bore, the shortest the width; the hammer sits behind the
		// trigger and the slide above the grip, which fixes the signs.
		const FBox FB = Frame->GetBoundingBox();
		const FBox SB = Slide->GetBoundingBox();
		const FBox HB = Hammer->GetBoundingBox();
		const FBox TB = LoadPistolPiece(TEXT("trigger_a")) ? LoadPistolPiece(TEXT("trigger_a"))->GetBoundingBox() : HB;
		const FVector Size = FB.GetSize();
		int32 Long = 0;
		int32 Thin = 0;
		for (int32 i = 1; i < 3; ++i)
		{
			if (Size[i] > Size[Long]) Long = i;
			if (Size[i] < Size[Thin]) Thin = i;
		}
		const int32 Upx = 3 - Long - Thin;
		const float FwdSign = TB.GetCenter()[Long] > HB.GetCenter()[Long] ? 1.f : -1.f;
		const float UpSign = SB.GetCenter()[Upx] > FB.GetCenter()[Upx] ? 1.f : -1.f;
		FVector Fwd = FVector::ZeroVector;
		Fwd[Long] = FwdSign;
		FVector Up = FVector::ZeroVector;
		Up[Upx] = UpSign;
		const FMatrix Basis = FRotationMatrix::MakeFromXZ(Fwd, Up);
		// Rows of the inverse map source axes onto (forward, right, up).
		const FMatrix ToFrame = Basis.GetTransposed();
		auto ToF = [&ToFrame](const FVector& P) { return ToFrame.TransformVector(P); };
		// Pivot: the web of the hand sits just ahead of the hammer, ~4 cm under the slide.
		const FBox SlideF(ToF(SB.Min).ComponentMin(ToF(SB.Max)), ToF(SB.Min).ComponentMax(ToF(SB.Max)));
		const FVector HammerF = ToF(HB.GetCenter());
		const FVector Pivot(HammerF.X + 2.4f, 0.f, SlideF.Min.Z - 4.6f);
		const FTransform Final = FTransform(ToFrame) * FTransform(-Pivot);
		for (const TCHAR* Piece : PistolPieces)
		{
			UStaticMesh* Src = LoadPistolPiece(Piece);
			if (!Src)
			{
				continue;
			}
			const FString Out = PartPath(TEXT("X16"), FString(TEXT("SM_X16_")) + Piece);
			// The source scene lays the magazine out 10 cm behind the pistol; seat it in the grip.
			const bool bMag = FCString::Strstr(Piece, TEXT("magazine")) != nullptr;
			UStaticMesh* Mesh = Reframe(Src, bMag ? FTransform(FVector(10.f, 0.f, 0.f)) * Final : Final, Out);
			if (Mesh)
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("X16 %s bounds %s"), Piece, *Mesh->GetBoundingBox().ToString());
			}
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("X16 frame: long axis %d (sign %.0f), up axis %d (sign %.0f), pivot %s, slide frame-space %s"), Long, FwdSign, Upx, UpSign,
			*Pivot.ToString(), *SlideF.ToString());

		// Extended magazine: a +5 base pad; grip panels for the stippled grip; a short accessory rail under the dust cover.
		UStaticMesh* MagMesh = LoadObject<UStaticMesh>(nullptr, *(PartPath(TEXT("X16"), TEXT("SM_X16_magazine_loaded")) + TEXT(".SM_X16_magazine_loaded")));
		if (MagMesh)
		{
			const FBox MB = MagMesh->GetBoundingBox();
			FBuilder B;
			B.SetSlot(Poly);
			B.Box(FVector(MB.GetCenter().X - 0.2f, 0.f, MB.Min.Z - 1.6f), FVector(MB.GetSize().X + 0.5f, MB.GetSize().Y + 0.3f, 3.2f), 0.4f, FRotator(-14.f, 0.f, 0.f));
			SaveKit(B, TEXT("X16"), TEXT("SM_X16_mag_extended"));
		}
		{
			const FBox FrameF = LoadObject<UStaticMesh>(nullptr, *(PartPath(TEXT("X16"), TEXT("SM_X16_pistol_a")) + TEXT(".SM_X16_pistol_a")))->GetBoundingBox();
			FBuilder B;
			B.SetSlot(Stipple);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(-1.2f, S * (FrameF.Max.Y - 0.05f), -2.0f), FVector(3.8f, 0.2f, 6.6f), 0.08f, FRotator(-16.f, 0.f, 0.f));
			}
			SaveKit(B, TEXT("X16"), TEXT("SM_X16_grip_stippled"));
			FBuilder R;
			R.SetSlot(Anod);
			Rail(R, FVector(SlideF.Max.X - 7.5f, 0.f, SlideF.Min.Z - 1.6f), 4.0f, EFace::Down);
			SaveKit(R, TEXT("X16"), TEXT("SM_X16_Rail"));
		}
	}

	// ------------------------------------------------------------------ clipped hedge

	/**
	 * A metre of clipped hedge (box / privet / orange jasmine): a dark irregular core and several thousand small leaves
	 * on a rounded shell over it, some tucked in, most at the cut, a few new sprigs standing proud, each tilted its own
	 * way. Model space: X along the row from -50 to 50, the cut 44 cm across and 64 cm high, origin on the soil. The
	 * leaf material colours each leaf from the vertex colours (B a per-leaf random, G how deep in the mass it sits).
	 */
	void BuildHedge()
	{
		UMaterialInterface* LeafMat = nullptr;
		{
			FMat B{ NewMaterial(TEXT("M_Campus_HedgeLeaf")) };
			B.M->TwoSided = true;
			// Leaves let a good part of the light through: lit only from the front, a mass of them shades itself black
			// (each leaf in the shadow of the ones over it).
			B.M->SetShadingModel(MSM_TwoSidedFoliage);
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float r = VC.b;
float d = VC.g;
float3 c = lerp(Dark, Light, smoothstep(0.05, 0.95, frac(r * 7.13)));
c = lerp(c, Young, step(0.9, r) * (1.0 - d));
return c * lerp(1.0, 0.3, d);
)"), CMOT_Float3, { TEXT("VC"), TEXT("Dark"), TEXT("Light"), TEXT("Young") });
			B.Link(B.Make<UMaterialExpressionVertexColor>(), C, TEXT("VC"));
			B.Link(B.Vec(TEXT("Dark"), FLinearColor(0.036f, 0.075f, 0.021f)), C, TEXT("Dark"));
			B.Link(B.Vec(TEXT("Light"), FLinearColor(0.09f, 0.16f, 0.042f)), C, TEXT("Light"));
			B.Link(B.Vec(TEXT("Young"), FLinearColor(0.2f, 0.27f, 0.075f)), C, TEXT("Young"));
			B.Out(C, MP_BaseColor);
			B.Out(B.MulC(C, 0.9f), MP_SubsurfaceColor);
			B.Out(B.Scalar(TEXT("Roughness"), 0.62f), MP_Roughness);
			B.Out(B.Scalar(TEXT("Specular"), 0.3f), MP_Specular);
			// The outer leaves flutter a few millimetres in the wind; the core and the leaves deep in it stay put.
			UMaterialExpressionCustom* Flutter = B.Custom(TEXT(R"(
float k = (1.0 - VC.g) * WS;
float w = sin(T * 7.0 + dot(WP, float3(0.37, 0.29, 0.41))) + 0.5 * sin(T * 11.3 + dot(WP, float3(0.13, 0.47, 0.21)));
return float3(WX, WY, 0.35) * w * 0.25 * k;
)"), CMOT_Float3, { TEXT("VC"), TEXT("WP"), TEXT("T"), TEXT("WX"), TEXT("WY"), TEXT("WS") });
			B.Link(B.Make<UMaterialExpressionVertexColor>(), Flutter, TEXT("VC"));
			B.Link(B.Make<UMaterialExpressionWorldPosition>(), Flutter, TEXT("WP"));
			B.Link(B.Make<UMaterialExpressionTime>(), Flutter, TEXT("T"));
			UMaterialParameterCollection* Env = CampusEditor::EnvironmentCollection();
			B.Link(B.Collection(Env, TEXT("WindX")), Flutter, TEXT("WX"));
			B.Link(B.Collection(Env, TEXT("WindY")), Flutter, TEXT("WY"));
			B.Link(B.Collection(Env, TEXT("WindStrength")), Flutter, TEXT("WS"));
			B.Out(Flutter, MP_WorldPositionOffset);
			B.M->MaxWorldPositionOffsetDisplacement = 2.f;
			B.M->SetUsageByFlag(MATUSAGE_StaticMesh, true);
			B.M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
			B.M->SetUsageByFlag(MATUSAGE_Nanite, true);
			FinishMaterial(B.M);
			LeafMat = B.M;
		}
		const TArray<UMaterialInterface*> Slots = { LeafMat };
		const float L = 100.f;
		const float D = 44.f;
		const float H = 64.f;
		const FVector Half(L * 0.5f, D * 0.5f, H * 0.5f);
		const FVector Center(0.f, 0.f, H * 0.5f);
		const float Round = 8.f;
		// Variants 0-2 are lengths of the row (open at both ends, so they run on into each other without a notch at the
		// joint); variant 3 is the row's rounded end, closed at +X.
		for (int32 Variant = 0; Variant < 4; ++Variant)
		{
			const bool bEnd = Variant == 3;
			FRandomStream Rng(1015 + Variant * 7919);
			FBuilder B;
			B.SetSlot(0);
			// Core: only ever seen as the shade between the leaves.
			B.SetCavity(1.f);
			B.Box(Center - FVector(bEnd ? 4.f : 0.f, 0.f, 3.f), FVector(bEnd ? L - 8.f : L + 2.f, D - 10.f, H - 8.f), 4.f);
			// The cut: rounded across the top shoulders (and round the end), the top a little crowned and wavy along
			// the row.
			auto Surface = [&](const FVector& P, FVector& OutN) -> FVector
			{
				const FVector Inner = Half - FVector(Round);
				const FVector Rel = P - Center;
				const FVector Core(FMath::Clamp(Rel.X, -Half.X - 1.f, bEnd ? Inner.X : Half.X + 1.f), FMath::Clamp(Rel.Y, -Inner.Y, Inner.Y), FMath::Clamp(Rel.Z, -Inner.Z, Inner.Z));
				OutN = (Rel - Core).GetSafeNormal();
				if (OutN.IsNearlyZero())
				{
					OutN = FVector::UpVector;
				}
				const float Wave = 1.2f * FMath::Sin(P.X * 0.09f + Variant * 2.f) + 0.8f * FMath::Sin(P.X * 0.23f + Variant);
				return Center + Core + OutN * (Round + (OutN.Z > 0.5f ? Wave + 1.f : Wave * 0.5f));
			};
			// Top, the two faces and (the end piece) its closed end.
			const float Areas[5] = { L * D, L * H, L * H, bEnd ? D * H : 0.f, 0.f };
			const float Total = Areas[0] + Areas[1] + Areas[2] + Areas[3] + Areas[4];
			const int32 Count = bEnd ? 8000 : 7000;
			for (int32 i = 0; i < Count; ++i)
			{
				// A point on one of the five visible faces, by area, pushed onto the rounded cut.
				float Pick = Rng.FRand() * Total;
				int32 Face = 0;
				while (Face < 4 && Pick > Areas[Face])
				{
					Pick -= Areas[Face];
					++Face;
				}
				FVector P;
				switch (Face)
				{
				case 0: P = FVector(Rng.FRandRange(-Half.X, Half.X), Rng.FRandRange(-Half.Y, Half.Y), H); break;
				case 1: P = FVector(Rng.FRandRange(-Half.X, Half.X), Half.Y, Rng.FRandRange(2.f, H)); break;
				case 2: P = FVector(Rng.FRandRange(-Half.X, Half.X), -Half.Y, Rng.FRandRange(2.f, H)); break;
				case 3: P = FVector(Half.X, Rng.FRandRange(-Half.Y, Half.Y), Rng.FRandRange(2.f, H)); break;
				default: P = FVector(-Half.X, Rng.FRandRange(-Half.Y, Half.Y), Rng.FRandRange(2.f, H)); break;
				}
				FVector N;
				P = Surface(P, N);
				// Depth in the mass: most leaves sit at the cut, some deeper (darker), a few sprigs stand out of it.
				const float Layer = Rng.FRand();
				float Out = FMath::Lerp(-3.f, 2.f, FMath::Pow(Layer, 0.35f));
				const bool bSprig = Rng.FRand() < 0.025f && N.Z > -0.2f;
				if (bSprig)
				{
					Out = Rng.FRandRange(3.f, 8.f);
				}
				const FVector C = P + N * Out;
				if (C.Z < 3.f)
				{
					continue;
				}
				// Each leaf faces out roughly along the cut, tilted its own way, pointing any way across it.
				const FVector Facing = (N + Rng.VRand() * 0.85f).GetSafeNormal();
				FVector Axis = FVector::CrossProduct(Facing, Rng.VRand()).GetSafeNormal();
				if (Axis.IsNearlyZero())
				{
					continue;
				}
				const FVector Side = FVector::CrossProduct(Facing, Axis);
				const float Len = Rng.FRandRange(2.4f, 3.8f) * (bSprig ? 0.85f : 1.f);
				const float Wid = Len * Rng.FRandRange(0.42f, 0.58f);
				TArray<FVector> Points;
				for (int32 k = 0; k < 6; ++k)
				{
					const float A = 2.f * PI * k / 6.f;
					// Pointed at the tip, rounder at the base.
					const float Along = FMath::Cos(A) * Len * 0.5f;
					const float Across = FMath::Sin(A) * Wid * 0.5f * (FMath::Cos(A) > 0.f ? 0.8f : 1.f);
					Points.Add(C + Axis * Along + Side * Across);
				}
				B.SetCavity(FMath::Clamp(0.45f * (1.f - Layer) - (bSprig ? 0.3f : 0.f), 0.f, 1.f));
				B.Polygon(Points, Facing);
			}
			B.SetCavity(0.f);
			const FString Name = bEnd ? FString(TEXT("SM_Hedge_End")) : FString::Printf(TEXT("SM_Hedge_Section_%d"), Variant);
			if (UStaticMesh* Mesh = B.Save(FString(TEXT("/Game/Campus/Meshes/")) + Name, Slots, true))
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("%s: %d triangles, bounds %s"), *Name, B.NumTriangles(), *Mesh->GetBoundingBox().ToString());
			}
		}
	}

	// ------------------------------------------------------------------ turf

	/**
	 * A square metre of lawn a week after mowing: some two thousand blades of uneven height, each tapered, leaning its
	 * own way and curving over toward the tip, the odd longer one the mower missed. Lit as thin leaves (light comes
	 * through), darker at the root, and bending downwind with height like the rest of the planting.
	 */
	void BuildTurf()
	{
		UMaterialInterface* BladeMat = nullptr;
		{
			FMat B{ NewMaterial(TEXT("M_Campus_TurfBlade")) };
			B.M->TwoSided = true;
			B.M->SetShadingModel(MSM_TwoSidedFoliage);
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float r = VC.b;
float root = VC.g;
float3 c = lerp(Deep, Fresh, smoothstep(0.1, 0.9, frac(r * 5.71)));
c = lerp(c, Dry, step(0.94, frac(r * 3.37)));
return c * lerp(1.0, 0.35, root);
)"), CMOT_Float3, { TEXT("VC"), TEXT("Deep"), TEXT("Fresh"), TEXT("Dry") });
			B.Link(B.Make<UMaterialExpressionVertexColor>(), C, TEXT("VC"));
			B.Link(B.Vec(TEXT("Deep"), FLinearColor(0.04f, 0.085f, 0.022f)), C, TEXT("Deep"));
			B.Link(B.Vec(TEXT("Fresh"), FLinearColor(0.085f, 0.16f, 0.035f)), C, TEXT("Fresh"));
			B.Link(B.Vec(TEXT("Dry"), FLinearColor(0.16f, 0.15f, 0.07f)), C, TEXT("Dry"));
			B.Out(C, MP_BaseColor);
			B.Out(B.MulC(C, 0.9f), MP_SubsurfaceColor);
			B.Out(B.Scalar(TEXT("Roughness"), 0.6f), MP_Roughness);
			B.Out(B.Scalar(TEXT("Specular"), 0.35f), MP_Specular);
			UMaterialExpressionCustom* Wind = B.Custom(TEXT(R"(
float2 dir = float2(WX, WY);
float h = max(LP.z, 0.0);
float bend = h * 0.4;
float phase = dot(WP.xy, float2(0.0071, 0.0053));
float gust = 0.5 + 0.5 * sin(dot(WP.xy, dir) * 0.004 - T * 1.9);
float sway = 0.6 + 0.4 * sin(T * 1.7 + phase * 6.2831);
float2 along = dir * bend * WS * (0.35 + 0.65 * gust) * sway;
float flutter = sin(T * 8.0 + dot(WP, float3(0.41, 0.33, 0.0))) * 0.06 * h * WS;
float3 o = float3(along + float2(-dir.y, dir.x) * flutter, 0.0);
o.z -= length(o.xy) * 0.35;
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
			B.M->MaxWorldPositionOffsetDisplacement = 12.f;
			B.M->SetUsageByFlag(MATUSAGE_StaticMesh, true);
			B.M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
			B.M->SetUsageByFlag(MATUSAGE_Nanite, true);
			FinishMaterial(B.M);
			BladeMat = B.M;
		}
		const TArray<UMaterialInterface*> Slots = { BladeMat };
		for (int32 Variant = 0; Variant < 2; ++Variant)
		{
			FRandomStream Rng(4711 + Variant * 977);
			FBuilder B;
			B.SetSlot(0);
			const int32 Count = 2000;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector Base(Rng.FRandRange(-50.f, 50.f), Rng.FRandRange(-50.f, 50.f), 0.f);
				const bool bLong = Rng.FRand() < 0.05f;
				const float Height = bLong ? Rng.FRandRange(14.f, 22.f) : Rng.FRandRange(5.f, 12.f);
				const float Width = Rng.FRandRange(0.35f, 0.6f);
				// Lean and facing: the blade plane turns any way; it leans out of the vertical and curls over more toward the tip.
				const float Yaw = Rng.FRandRange(0.f, 2.f * PI);
				const FVector Across(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
				const FVector LeanDir = FVector(-Across.Y, Across.X, 0.f) * (Rng.FRand() < 0.5f ? 1.f : -1.f);
				const float Lean = Rng.FRandRange(0.05f, bLong ? 0.55f : 0.35f);
				const FVector Mid = Base + FVector(0.f, 0.f, Height * 0.55f) + LeanDir * Height * Lean * 0.35f;
				const FVector Tip = Base + FVector(0.f, 0.f, Height * (1.f - Lean * 0.35f)) + LeanDir * Height * Lean;
				// Lit mostly from above, as a sward is (a blade side-on would otherwise go dark).
				const FVector Normal = (FVector::CrossProduct(Across, (Tip - Base).GetSafeNormal()).GetSafeNormal() * 0.45f + FVector::UpVector * 0.55f).GetSafeNormal();
				B.SetCavity(0.75f);
				B.Polygon({ Base - Across * Width * 0.5f, Base + Across * Width * 0.5f, Mid + Across * Width * 0.32f, Mid - Across * Width * 0.32f }, Normal);
				B.SetCavity(0.2f);
				B.Polygon({ Mid - Across * Width * 0.32f, Mid + Across * Width * 0.32f, Tip }, Normal);
			}
			B.SetCavity(0.f);
			const FString Name = FString::Printf(TEXT("SM_Turf_Patch_%d"), Variant);
			if (UStaticMesh* Mesh = B.Save(FString(TEXT("/Game/Campus/Meshes/")) + Name, Slots, true))
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("%s: %d triangles, bounds %s"), *Name, B.NumTriangles(), *Mesh->GetBoundingBox().ToString());
			}
		}
	}

	// ------------------------------------------------------------------ knife

	/**
	 * Fixed-blade fighting knife. Modelled along its own axis (X from the guard to the tip, Z toward the spine) and
	 * stood up in the weapon frame along the pistol grip's rake, so the fist closes round the handle exactly as it
	 * does round the X16's grip and the blade comes out of the top of it, edge forward.
	 */
	void BuildKnife()
	{
		const FTransform Axis(FRotator(76.6f, 0.f, 0.f));
		FBuilder B;
		B.SetFrame(Axis);
		// Blade: a drop point with a slight clip, black coated; the bright ground edge stands just proud of the coating.
		const TArray<FVector2D> Blade = {
			{ 2.4f, 1.15f }, { 13.8f, 1.15f }, { 16.2f, 0.98f }, { 18.2f, 0.62f }, { 19.5f, 0.14f }, { 18.9f, -0.38f }, { 17.6f, -0.98f },
			{ 15.6f, -1.42f }, { 12.f, -1.62f }, { 4.1f, -1.62f }, { 3.4f, -1.28f }, { 2.9f, -1.45f }, { 2.4f, -1.45f } };
		B.SetSlot(Matte);
		B.Prism(Blade, -0.2f, 0.2f, 0.11f, 30.f);
		const TArray<FVector2D> Edge = {
			{ 4.1f, -1.2f }, { 12.f, -1.2f }, { 15.4f, -1.0f }, { 17.3f, -0.62f }, { 18.6f, -0.12f }, { 19.62f, 0.14f }, { 19.0f, -0.46f },
			{ 17.65f, -1.07f }, { 15.62f, -1.52f }, { 12.f, -1.72f }, { 4.1f, -1.72f } };
		B.SetSlot(Bright);
		B.Prism(Edge, -0.11f, 0.11f, 0.07f, 30.f);
		// Fuller on both flats and the spine's thumb ramp jimping.
		B.SetSlot(Dark);
		for (const float S : { -1.f, 1.f })
		{
			B.Box(FVector(8.6f, S * 0.2f, 0.42f), FVector(8.4f, 0.03f, 0.42f), 0.01f);
		}
		B.SetSlot(Matte);
		for (int32 i = 0; i < 6; ++i)
		{
			B.Box(FVector(3.2f + i * 0.42f, 0.f, 1.2f), FVector(0.16f, 0.38f, 0.14f), 0.03f);
		}
		// Guard: a flat steel plate, a little swept toward the edge side.
		B.SetSlot(Steel);
		B.Prism({ { 1.85f, 1.6f }, { 2.45f, 1.5f }, { 2.45f, -1.9f }, { 2.2f, -2.3f }, { 1.8f, -2.2f }, { 1.85f, -1.7f } }, -0.55f, 0.55f, 0.12f, 30.f);
		// Handle: an oval grip with finger rings, slightly swelled toward the butt so it locks in the fist. Lathe
		// profiles run toward +X (their outward side is to the left of the direction of travel).
		B.SetSlot(Rubber);
		const FTransform Oval(FQuat::Identity, FVector::ZeroVector, FVector(1.f, 0.78f, 1.f));
		B.Lathe({ { -9.2f, 1.44f }, { -8.7f, 1.5f }, { -7.6f, 1.36f }, { -5.2f, 1.42f }, { -2.4f, 1.3f }, { 0.2f, 1.38f }, { 1.4f, 1.3f }, { 1.85f, 1.18f } }, 28, Oval, 30.f);
		for (const float X : { -0.9f, -2.9f, -4.9f, -6.9f })
		{
			B.Lathe({ { X - 0.18f, 1.2f }, { X - 0.1f, 1.47f }, { X + 0.1f, 1.47f }, { X + 0.18f, 1.2f } }, 28, Oval, 40.f, true, 0.f, false);
		}
		// Pommel with a glass breaker.
		B.SetSlot(Steel);
		B.Lathe({ { -10.65f, 0.f }, { -10.5f, 0.55f }, { -10.1f, 1.08f }, { -9.6f, 1.36f }, { -9.15f, 1.3f } }, 24, Oval, 32.f);
		B.ResetFrame();
		UStaticMesh* Saved = B.Save(PartPath(TEXT("KNIFE"), TEXT("SM_KNIFE_Knife")), GSlots, false);
		FString Slots;
		for (const FStaticMaterial& M : Saved ? Saved->GetStaticMaterials() : TArray<FStaticMaterial>())
		{
			Slots += GetNameSafe(M.MaterialInterface) + TEXT(" ");
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("Knife built: %d triangles, materials: %s"), B.NumTriangles(), *Slots);
	}

	// ------------------------------------------------------------------ soldier gear
	// Authored in the mannequin's reference-pose space (model faces +Y, left = +X, Z up, cm) around its measured body
	// cross-sections, then strapped rigidly to a bone at runtime.

	void BuildGear(bool bCarrierOnly = false)
	{
		const TCHAR* G = TEXT("Gear");
		{
			// Plate carrier: plate bags front and back, cummerbund, shoulder straps, MOLLE rows, triple mag pouch,
			// admin pouch, radio pouch and drag handle.
			FBuilder B;
			B.SetSlot(GearTan);
			B.Box(FVector(0.f, 18.8f, 127.f), FVector(26.f, 4.0f, 30.f), 1.2f);
			B.Box(FVector(0.f, -17.2f, 129.f), FVector(26.f, 4.0f, 32.f), 1.2f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(S * 17.6f, 0.8f, 113.5f), FVector(2.0f, 31.0f, 15.f), 0.6f);
				B.Box(FVector(S * 10.f, 9.6f, 149.2f), FVector(4.6f, 20.6f, 1.4f), 0.35f, FRotator(0.f, 0.f, 42.f));
				B.Box(FVector(S * 10.f, -7.4f, 151.2f), FVector(4.6f, 20.4f, 1.4f), 0.35f, FRotator(0.f, 0.f, -29.f));
				B.Box(FVector(S * 10.f, 1.6f, 156.2f), FVector(4.8f, 6.f, 1.5f), 0.4f);
			}
			for (const float X : { -8.4f, 0.f, 8.4f })
			{
				B.Box(FVector(X, 22.5f, 117.f), FVector(7.6f, 3.6f, 13.f), 0.8f);
				B.Box(FVector(X, 22.7f, 123.6f), FVector(7.9f, 4.0f, 1.2f), 0.4f);
			}
			B.Box(FVector(0.f, 21.9f, 131.5f), FVector(14.f, 2.2f, 9.f), 0.6f);
			B.Box(FVector(-7.f, -20.9f, 127.f), FVector(7.f, 3.6f, 14.f), 0.8f);
			B.SetSlot(GearDark);
			for (const float Z : { 126.f, 136.5f, 140.f })
			{
				B.Box(FVector(0.f, 20.9f, Z), FVector(24.f, 0.4f, 1.1f), 0.1f);
			}
			for (const float Z : { 120.f, 128.f, 136.f })
			{
				B.Box(FVector(0.f, -19.3f, Z), FVector(24.f, 0.4f, 1.1f), 0.1f);
			}
			for (const float X : { -8.4f, 0.f, 8.4f })
			{
				B.Box(FVector(X, 24.4f, 119.f), FVector(7.0f, 0.3f, 1.0f), 0.08f);
			}
			B.Box(FVector(0.f, -19.6f, 144.4f), FVector(8.f, 1.4f, 2.0f), 0.3f);
			B.Box(FVector(-7.f, -21.0f, 135.f), FVector(1.0f, 1.0f, 4.f), 0.2f);
			// pouch seams and compressed lips break up the rigid plate silhouette
			B.SetSlot(GearTan);
			for (const float X : { -8.4f, 0.f, 8.4f })
			{
				for (const float Side : { -1.f, 1.f })
				{
					B.Box(FVector(X + Side * 3.35f, 24.38f, 117.f), FVector(0.18f, 0.10f, 10.f), 0.04f);
				}
				B.Box(FVector(X, 24.5f, 112.3f), FVector(6.5f, 0.16f, 0.35f), 0.08f);
			}
			B.Box(FVector(0.f, 23.12f, 135.f), FVector(12.f, 0.12f, 0.3f), 0.05f);
			B.SetSlot(GearDark);
			for (const float Side : { -1.f, 1.f })
			{
				B.Box(FVector(Side * 9.8f, 19.6f, 145.f), FVector(3.7f, 1.1f, 2.4f), 0.25f);
				B.Box(FVector(Side * 9.8f, 20.2f, 145.f), FVector(2.2f, 0.12f, 1.2f), 0.18f);
			}
			SaveKit(B, G, TEXT("SM_Gear_Carrier"));
		}
		if (bCarrierOnly)
		{
			return;
		}
		{
			// Battle belt with a buckle, pistol mag pouch and a dump pouch.
			FBuilder B;
			B.SetSlot(GearTan);
			B.Box(FVector(0.f, 14.6f, 92.f), FVector(35.f, 1.8f, 5.f), 0.6f);
			B.Box(FVector(0.f, -12.6f, 92.f), FVector(35.f, 1.8f, 5.f), 0.6f);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(S * 18.5f, 1.f, 92.f), FVector(1.8f, 27.f, 5.f), 0.6f);
			}
			B.Box(FVector(-9.f, 16.6f, 90.f), FVector(4.2f, 2.6f, 7.5f), 0.5f);
			B.Box(FVector(7.f, -15.6f, 88.5f), FVector(10.f, 4.f, 9.f), 1.0f);
			B.SetSlot(GearDark);
			B.Box(FVector(0.f, 15.7f, 92.f), FVector(5.f, 0.8f, 3.6f), 0.25f);
			SaveKit(B, G, TEXT("SM_Gear_Belt"));
		}
		{
			// High-cut helmet: tilted shell (brow up, nape down), ARC rails, NVG shroud, ear protection.
			FBuilder B;
			B.SetSlot(GearShell);
			const FTransform Shell(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(16.f)) * FQuat(FVector::YAxisVector, -HALF_PI), FVector(0.f, 1.5f, 168.5f));
			B.Lathe({ FVector2D(0.f, 12.2f), FVector2D(3.1f, 12.4f), FVector2D(6.3f, 11.9f), FVector2D(8.9f, 10.5f), FVector2D(11.f, 8.4f), FVector2D(12.6f, 5.9f), FVector2D(13.6f, 2.9f), FVector2D(14.f, 0.f) }, 40, Shell);
			B.SetSlot(GearDark);
			for (const float S : { -1.f, 1.f })
			{
				B.Box(FVector(S * 12.5f, 1.2f, 172.f), FVector(1.0f, 10.f, 2.2f), 0.3f, FRotator(0.f, 0.f, 0.f));
				B.Tube(-1.6f, 1.6f, 4.3f, 18, FTransform(FVector(S * 10.6f, 2.0f, 164.f)), 0.5f);
			}
			B.Box(FVector(0.f, 13.6f, 178.f), FVector(5.5f, 1.8f, 3.6f), 0.4f);
			SaveKit(B, G, TEXT("SM_Gear_Helmet"));
		}
		for (const float S : { 1.f, -1.f })
		{
			// Knee pad: padded sleeve front with a hard cap.
			FBuilder B;
			B.SetSlot(GearTan);
			B.Box(FVector(S * 12.3f, 10.6f, 50.5f), FVector(10.f, 1.6f, 13.f), 0.6f);
			B.SetSlot(GearDark);
			B.Box(FVector(S * 12.3f, 11.9f, 51.f), FVector(8.f, 1.4f, 9.5f), 0.9f);
			SaveKit(B, G, S > 0.f ? TEXT("SM_Gear_KneePad_L") : TEXT("SM_Gear_KneePad_R"));
		}
	}
}

namespace
{
	// ------------------------------------------------------------------ throwables
	// Frame: +X up through the fuse, origin at the body centre. The lever lies along -Z (under the fingers); the pull
	// ring hangs on +Y. The ring mesh is authored about the fuse centre and placed per grenade at runtime.

	/** M201-pattern fuse from Base (top of the body) up: threaded neck, body, striker housing, pin bosses. */
	void GrenadeFuse(FBuilder& B, float Base)
	{
		B.SetSlot(Steel);
		B.Tube(Base - 0.5f, Base + 0.05f, 0.82f, 18, FTransform::Identity, 0.05f);
		B.SetSlot(Matte);
		B.Lathe({ FVector2D(Base, 1.02f), FVector2D(Base + 1.15f, 1.02f), FVector2D(Base + 1.3f, 0.92f), FVector2D(Base + 1.5f, 0.62f), FVector2D(Base + 1.6f, 0.f) }, 22);
		// Striker housing and the lever's pivot ears on the lever side.
		B.Box(FVector(Base + 0.9f, 0.f, -0.95f), FVector(1.2f, 1.5f, 0.55f), 0.12f);
		B.SetSlot(Steel);
		for (const float S : { -1.f, 1.f })
		{
			Pin(B, FVector(Base + 0.75f, S * 1.02f, 0.25f), 0.22f, 0.3f, 10);
		}
	}

	/** The spoon: a pressed strip from over the fuse top down along the body at radius BodyR(x), from TopX to EndX. */
	void GrenadeLever(FBuilder& B, float FuseBase, TFunctionRef<float(float)> BodyR, float EndX)
	{
		TArray<FVector2D> Path = { { FuseBase + 1.65f, -0.25f }, { FuseBase + 1.7f, -0.95f }, { FuseBase + 1.25f, -1.3f }, { FuseBase + 0.3f, -1.38f } };
		const int32 Steps = 9;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const float X = FMath::Lerp(FuseBase - 0.4f, EndX, (float)i / Steps);
			Path.Add({ X, -(BodyR(X) + 0.14f) });
		}
		// Thickness outward along the path normal (away from the body).
		TArray<FVector2D> Outline = Path;
		for (int32 i = Path.Num() - 1; i >= 0; --i)
		{
			const FVector2D Tangent = (Path[FMath::Min(i + 1, Path.Num() - 1)] - Path[FMath::Max(i - 1, 0)]).GetSafeNormal();
			FVector2D N(-Tangent.Y, Tangent.X);
			if ((N | (Path[i] - FVector2D(FuseBase - 1.f, 0.f))) < 0.f)
			{
				N = -N;
			}
			Outline.Add(Path[i] + N * 0.13f);
		}
		B.SetSlot(GrenadeOD);
		B.Prism(Outline, -0.72f, 0.72f, 0.03f, 35.f);
	}

	void BuildThrowables()
	{
		const TCHAR* T = TEXT("Throwables");
		auto FragR = [](float X) { return FMath::Sqrt(FMath::Max(3.2f * 3.2f - X * X, 0.f)) * (X > 0.f ? 1.f - 0.06f * X / 3.2f : 1.f); };
		const float FragFuse = 3.25f;
		{
			// M67 fragmentation grenade: a 6.4 cm steel sphere, a weld seam round its middle, the fuse on top.
			FBuilder B;
			B.SetSlot(GrenadeOD);
			TArray<FVector2D> Profile;
			for (int32 i = 0; i <= 20; ++i)
			{
				const float X = FMath::Lerp(-3.2f, 3.05f, (float)i / 20);
				Profile.Add({ X, FMath::Max(FragR(X), i == 0 ? 0.f : 0.6f) });
			}
			Profile[0].Y = 0.f;
			B.Lathe(Profile, 40, FTransform::Identity, 50.f);
			B.SetSlot(Steel);
			B.Tube(-0.12f, 0.12f, 3.23f, 40, FTransform::Identity, 0.04f);
			B.SetSlot(MarkYellow);
			B.Tube(1.55f, 1.85f, FragR(1.7f) + 0.01f, 40, FTransform::Identity, 0.02f);
			GrenadeFuse(B, FragFuse);
			SaveKit(B, T, TEXT("SM_Throw_Frag"));
		}
		{
			FBuilder B;
			GrenadeLever(B, FragFuse, FragR, -1.6f);
			SaveKit(B, T, TEXT("SM_Throw_LeverFrag"));
		}
		auto Canister = [&](const TCHAR* Name, float Radius, float Half, int32 BodySlot, int32 BandSlot, bool bPorts, bool bTopVents)
		{
			FBuilder B;
			B.SetSlot(BodySlot);
			B.Lathe({ FVector2D(-Half, 0.f), FVector2D(-Half, Radius - 0.25f), FVector2D(-Half + 0.25f, Radius), FVector2D(Half - 0.25f, Radius), FVector2D(Half, Radius - 0.25f), FVector2D(Half, 0.f) }, 40, FTransform::Identity, 40.f);
			// Rolled seams at both ends.
			B.SetSlot(Steel);
			B.Tube(-Half + 0.35f, -Half + 0.6f, Radius + 0.05f, 40, FTransform::Identity, 0.03f);
			B.Tube(Half - 0.6f, Half - 0.35f, Radius + 0.05f, 40, FTransform::Identity, 0.03f);
			if (BandSlot >= 0)
			{
				B.SetSlot(BandSlot);
				B.Tube(Half * 0.45f, Half * 0.65f, Radius + 0.02f, 40, FTransform::Identity, 0.02f);
			}
			B.SetSlot(Dark);
			if (bPorts)
			{
				// M84: the steel tube is perforated with two rows of ports.
				for (int32 Row = 0; Row < 2; ++Row)
				{
					for (int32 k = 0; k < 6; ++k)
					{
						const float A = (k + Row * 0.5f) * 2.f * PI / 6.f;
						B.SetFrame(FTransform(FQuat(FVector::XAxisVector, -A), FVector(Row ? -1.8f : 1.8f, 0.f, 0.f)));
						B.Box(FVector(0.f, 0.f, Radius - 0.08f), FVector(1.9f, 0.75f, 0.3f), 0.08f);
					}
				}
				B.ResetFrame();
			}
			if (bTopVents)
			{
				// Smoke vents round the fuse on the top plate.
				for (int32 k = 0; k < 4; ++k)
				{
					const float A = k * HALF_PI + PI * 0.25f;
					B.Tube(Half - 0.02f, Half + 0.03f, 0.4f, 14, FTransform(FVector(0.f, FMath::Cos(A) * Radius * 0.62f, FMath::Sin(A) * Radius * 0.62f)), 0.f);
				}
			}
			GrenadeFuse(B, Half);
			SaveKit(B, T, Name);
		};
		Canister(TEXT("SM_Throw_Flash"), 2.2f, 6.f, GrenadeOD, -1, true, false);
		Canister(TEXT("SM_Throw_Smoke"), 3.1f, 7.2f, GrenadeGray, MarkYellow, false, true);
		Canister(TEXT("SM_Throw_Thermite"), 3.1f, 7.f, GrenadeGray, MarkRed, false, false);
		{
			FBuilder B;
			GrenadeLever(B, 6.f, [](float) { return 2.2f; }, -1.5f);
			SaveKit(B, T, TEXT("SM_Throw_LeverFlash"));
		}
		{
			FBuilder B;
			GrenadeLever(B, 7.2f, [](float) { return 3.1f; }, -0.5f);
			SaveKit(B, T, TEXT("SM_Throw_Lever"));
		}
		{
			// Safety pin through the fuse and its pull ring (a wire torus) hanging on the right.
			FBuilder B;
			B.SetSlot(Bright);
			B.Tube(-1.35f, 1.35f, 0.09f, 8, FTransform(FQuat(FVector::ZAxisVector, HALF_PI), FVector(0.f, 0.f, 0.25f)), 0.f);
			TArray<FVector2D> Wire;
			for (int32 i = 0; i <= 10; ++i)
			{
				const float A = 2.f * PI * i / 10;
				Wire.Add({ 0.12f * FMath::Cos(A), 1.15f + 0.12f * FMath::Sin(A) });
			}
			B.Lathe(Wire, 24, FTransform(FQuat(FVector::ZAxisVector, HALF_PI), FVector(-0.35f, 1.5f, 1.05f)), 60.f, true, 0.f, false);
			SaveKit(B, T, TEXT("SM_Throw_Ring"));
		}
	}
}

namespace CampusWeaponKit
{
	void BuildVisualDetails()
	{
		auto Texture = [](const TCHAR* Name)
		{
			return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/Textures/%s.%s"), KitRoot, Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		};
		FKitTextures Textures;
		Textures.Grain = Texture(TEXT("T_Weapon_Grain"));
		Textures.GrainN = Texture(TEXT("T_Weapon_Grain_N"));
		Textures.StippleN = Texture(TEXT("T_Weapon_Stipple_N"));
		if (!Textures.Grain || !Textures.GrainN || !Textures.StippleN)
		{
			UE_LOG(LogCampusBootstrap, Error, TEXT("Visual details need the existing weapon grain and stipple textures"));
			return;
		}
		BuildMaterials(Textures);
		BuildGear(true);
	}

	void InspectPistol()
	{
		for (const TCHAR* Piece : PistolPieces)
		{
			if (UStaticMesh* M = LoadPistolPiece(Piece))
			{
				UE_LOG(LogCampusBootstrap, Display, TEXT("service_pistol %s bounds %s"), Piece, *M->GetBoundingBox().ToString());
			}
		}
	}

	void BuildWeaponKit()
	{
		const FKitTextures Tex = BuildTextures();
		BuildMaterials(Tex);
		if (GSlots.Num() != NumSlots)
		{
			UE_LOG(LogCampusBootstrap, Error, TEXT("Weapon materials incomplete"));
			return;
		}
		BuildM4();
		BuildAK();
		BuildMP5();
		BuildMK14();
		BuildRPK();
		BuildSaiga();
		BuildHK416();
		BuildUMP();
		BuildAttachments();
		BuildPistol();
		BuildKnife();
		BuildHedge();
		BuildTurf();
		BuildGear();
		BuildThrowables();
		UE_LOG(LogCampusBootstrap, Display, TEXT("Weapon kit built"));
	}

	void BuildKnifeOnly()
	{
		// The kit's material instances, in slot order, as BuildMaterials saved them.
		static const TCHAR* SlotNames[NumSlots] = { TEXT("Anodized"), TEXT("Polymer"), TEXT("Steel"), TEXT("Wood"), TEXT("Bakelite"), TEXT("Rubber"), TEXT("FDE"),
			TEXT("Bright"), TEXT("Matte"), TEXT("Dark"), TEXT("Brass"), TEXT("Emitter"), TEXT("Stippled"), TEXT("GearTan"), TEXT("GearDark"), TEXT("GearShell"),
			TEXT("Marking"), TEXT("EyeRubber"), TEXT("GrenadeOD"), TEXT("GrenadeGray"), TEXT("MarkYellow"), TEXT("MarkRed") };
		GSlots.Reset();
		for (const TCHAR* Name : SlotNames)
		{
			const FString Path = FString::Printf(TEXT("%s/Materials/MI_Weapon_%s.MI_Weapon_%s"), KitRoot, Name, Name);
			UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Path);
			if (!M)
			{
				UE_LOG(LogCampusBootstrap, Error, TEXT("Weapon material %s missing: run the full weapon kit"), *Path);
				return;
			}
			GSlots.Add(M);
		}
		BuildKnife();
		BuildHedge();
		BuildTurf();
	}
}
