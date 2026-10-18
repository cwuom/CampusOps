#include "CampusUIAssets.h"
#include "CampusEditorCommon.h"
#include "Async/ParallelFor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionPixelDepth.h"

using namespace CampusEditor;

namespace
{
	const TCHAR* FontFolder = TEXT("/Game/Campus/UI/Fonts");
	const TCHAR* UITextureFolder = TEXT("/Game/Campus/UI/Textures");
	const TCHAR* FXTextureFolder = TEXT("/Game/Campus/Textures");

	template <typename T>
	T* FindOrCreate(const FString& Folder, const FString& Name)
	{
		UPackage* Package = CreatePackage(*(Folder / Name));
		Package->FullyLoad();
		T* Asset = FindObject<T>(Package, *Name);
		if (!Asset)
		{
			Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		return Asset;
	}

	// ------------------------------------------------------------------ fonts

	UFontFace* ImportFace(const TCHAR* FileName)
	{
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceArt/Fonts") / FileName);
		TArray<uint8> Data;
		if (!FFileHelper::LoadFileToArray(Data, *File) || Data.Num() < 1024)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("Missing font %s (run Tools/FetchFonts.ps1)"), *File);
			return nullptr;
		}
		const FString Name = TEXT("FF_") + FPaths::GetBaseFilename(FileName).Replace(TEXT("-"), TEXT("_"));
		UFontFace* Face = FindOrCreate<UFontFace>(FontFolder, Name);
		static_cast<IFontFaceInterface*>(Face)->InitializeFromBulkData(File, EFontHinting::Default, Data.GetData(), Data.Num());
		Face->LoadingPolicy = EFontLoadingPolicy::LazyLoad;
		Face->CacheSubFaces();
		SaveAsset(Face);
		return Face;
	}

	using FFaceList = TArray<TPair<FName, UFontFace*>>;

	void FillTypeface(FTypeface& Typeface, const FFaceList& Faces)
	{
		Typeface.Fonts.Reset();
		for (const TPair<FName, UFontFace*>& F : Faces)
		{
			if (F.Value)
			{
				FTypefaceEntry& E = Typeface.Fonts.AddDefaulted_GetRef();
				E.Name = F.Key;
				E.Font = FFontData(F.Value);
			}
		}
	}

	void MakeFont(const TCHAR* Name, const FFaceList& Latin, const FFaceList& Cjk, float CjkScale)
	{
		UFont* Font = FindOrCreate<UFont>(FontFolder, Name);
		Font->FontCacheType = EFontCacheType::Runtime;
		FCompositeFont& CF = Font->GetMutableInternalCompositeFont();
		FillTypeface(CF.DefaultTypeface, Latin);
		CF.SubTypefaces.Reset();
		if (Cjk.Num() > 0)
		{
			FCompositeSubFont& Sub = CF.SubTypefaces.AddDefaulted_GetRef();
			Sub.EditorName = TEXT("CJK");
			Sub.ScalingFactor = CjkScale;
			FillTypeface(Sub.Typeface, Cjk);
			const int32 Ranges[][2] = {
				{ 0x2E80, 0x2FDF }, { 0x3000, 0x303F }, { 0x3040, 0x31FF }, { 0x3200, 0x4DBF }, { 0x4E00, 0x9FFF },
				{ 0xF900, 0xFAFF }, { 0xFE30, 0xFE4F }, { 0xFF00, 0xFFEF }, { 0x20000, 0x2FA1F } };
			for (const auto& R : Ranges)
			{
				Sub.CharacterRanges.Add(FInt32Range(FInt32RangeBound::Inclusive(R[0]), FInt32RangeBound::Inclusive(R[1])));
			}
			FillTypeface(CF.FallbackTypeface.Typeface, Cjk);
			CF.FallbackTypeface.ScalingFactor = CjkScale;
		}
		Font->LegacyFontName = Latin.Num() > 0 ? Latin[0].Key : NAME_None;
		Font->LegacyFontSize = 24;
		CF.MakeDirty();
		Font->PostEditChange();
		SaveAsset(Font);
	}

	// ------------------------------------------------------------------ textures

	UTexture2D* SaveTexture(const FString& Folder, const FString& Name, int32 W, int32 H, const TArray<FColor>& Pixels, bool bUI, bool bMips = false)
	{
		UTexture2D* Tex = FindOrCreate<UTexture2D>(Folder, Name);
		Tex->PreEditChange(nullptr);
		Tex->Source.Init(W, H, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Tex->SRGB = true;
		Tex->CompressionSettings = bUI ? TC_EditorIcon : TC_Default;
		Tex->LODGroup = bUI ? TEXTUREGROUP_UI : TEXTUREGROUP_Effects;
		Tex->MipGenSettings = bMips ? (bUI ? TMGS_SimpleAverage : TMGS_FromTextureGroup) : TMGS_NoMipmaps;
		if (bUI)
		{
			Tex->Filter = bMips ? TF_Trilinear : TF_Bilinear;
			Tex->VirtualTextureStreaming = false;
			Tex->MaxTextureSize = 0;
			Tex->Downscale.Default = 1.f;
		}
		Tex->NeverStream = true;
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		Tex->PostEditChange();
		SaveAsset(Tex);
		return Tex;
	}

	/** White texture whose alpha is Fn(u, v) with u, v in [0, 1] at pixel centres. */
	template <typename FnType>
	void AlphaTexture(const TCHAR* Name, int32 W, int32 H, FnType&& Fn, const FString& Folder = UITextureFolder, bool bMips = false)
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(W * H);
		ParallelFor(H, [&](int32 Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const float A = FMath::Clamp(Fn((X + 0.5f) / W, (Y + 0.5f) / H), 0.f, 1.f);
				Pixels[Y * W + X] = FColor(255, 255, 255, (uint8)FMath::RoundToInt(A * 255.f));
			}
		});
		SaveTexture(Folder, Name, W, H, Pixels, true, bMips);
	}

	float SkullAlpha(float U, float V)
	{
		// centre the visible ink, rather than the old texture's uneven transparent padding
		U = (U - 0.5f) * (0.62f / 0.76f) + 0.5f;
		V = (V - 0.5f) * (0.69f / 0.84f) + 0.455f;
		constexpr float Pixel = 1.f / 256.f;
		auto Circle = [&](float X, float Y, float R) { return FMath::Clamp((R - FVector2f(U - X, V - Y).Size()) / Pixel + 0.5f, 0.f, 1.f); };
		auto Box = [&](float X0, float Y0, float X1, float Y1)
		{
			const float D = FMath::Min(FMath::Min(U - X0, X1 - U), FMath::Min(V - Y0, Y1 - V));
			return FMath::Clamp(D / Pixel + 0.5f, 0.f, 1.f);
		};
		float Alpha = FMath::Max(Circle(0.5f, 0.42f, 0.31f), Box(0.33f, 0.5f, 0.67f, 0.8f));
		Alpha *= 1.f - Circle(0.385f, 0.45f, 0.085f);
		Alpha *= 1.f - Circle(0.615f, 0.45f, 0.085f);
		const float NoseHalf = (V - 0.55f) * 0.45f;
		Alpha *= 1.f - FMath::Clamp((NoseHalf - FMath::Abs(U - 0.5f)) / Pixel + 0.5f, 0.f, 1.f) * Box(0.f, 0.55f, 1.f, 0.65f);
		for (const float Tooth : { 0.425f, 0.5f, 0.575f })
		{
			Alpha *= 1.f - Box(Tooth - 0.012f, 0.69f, Tooth + 0.012f, 0.82f);
		}
		return Alpha;
	}

	float ShapeCoverage(float Distance)
	{
		return FMath::Clamp(Distance * 256.f + 0.5f, 0.f, 1.f);
	}

	float SegmentAlpha(const FVector2f& P, const FVector2f& A, const FVector2f& B, float HalfWidth = 0.022f)
	{
		const FVector2f D = B - A;
		const float T = FMath::Clamp(FVector2f::DotProduct(P - A, D) / FMath::Max(D.SizeSquared(), 0.000001f), 0.f, 1.f);
		return ShapeCoverage(HalfWidth - (P - A - D * T).Size());
	}

	float TriangleAlpha(const FVector2f& P, const FVector2f& A, const FVector2f& B, const FVector2f& C)
	{
		const float Direction = FVector2f::CrossProduct(B - A, C - A) >= 0.f ? 1.f : -1.f;
		auto Edge = [&](const FVector2f& X, const FVector2f& Y)
		{
			return Direction * FVector2f::CrossProduct(Y - X, P - X) / FMath::Max((Y - X).Size(), 0.000001f);
		};
		return ShapeCoverage(FMath::Min3(Edge(A, B), Edge(B, C), Edge(C, A)));
	}

	float Smooth(float E0, float E1, float X)
	{
		const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Tileable value noise (same hash family as the weathering textures). */
	float Hash(int32 X, int32 Y, int32 Period, uint32 Seed)
	{
		X = ((X % Period) + Period) % Period;
		Y = ((Y % Period) + Period) % Period;
		uint32 H = (uint32)X * 374761393u + (uint32)Y * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return (H & 0xFFFFFFu) / 16777215.f;
	}
	float Noise(float U, float V, int32 Period, uint32 Seed)
	{
		const float X = U * Period;
		const float Y = V * Period;
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		auto Fade = [](float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); };
		const float Sx = Fade(X - X0);
		const float Sy = Fade(Y - Y0);
		return FMath::Lerp(FMath::Lerp(Hash(X0, Y0, Period, Seed), Hash(X0 + 1, Y0, Period, Seed), Sx),
			FMath::Lerp(Hash(X0, Y0 + 1, Period, Seed), Hash(X0 + 1, Y0 + 1, Period, Seed), Sx), Sy);
	}
	float FBM(float U, float V, int32 Period, int32 Octaves, uint32 Seed)
	{
		float Sum = 0.f;
		float Amp = 1.f;
		float Norm = 0.f;
		for (int32 o = 0; o < Octaves; ++o)
		{
			Sum += Amp * Noise(U, V, Period << o, Seed + o * 7);
			Norm += Amp;
			Amp *= 0.5f;
		}
		return Sum / Norm;
	}
}

namespace CampusUIAssets
{
	void BuildFonts()
	{
		UFontFace* BarlowRegular = ImportFace(TEXT("Barlow-Regular.ttf"));
		UFontFace* BarlowMedium = ImportFace(TEXT("Barlow-Medium.ttf"));
		UFontFace* BarlowSemi = ImportFace(TEXT("Barlow-SemiBold.ttf"));
		UFontFace* BarlowBold = ImportFace(TEXT("Barlow-Bold.ttf"));
		UFontFace* CondMedium = ImportFace(TEXT("BarlowCondensed-Medium.ttf"));
		UFontFace* CondSemi = ImportFace(TEXT("BarlowCondensed-SemiBold.ttf"));
		UFontFace* CondBold = ImportFace(TEXT("BarlowCondensed-Bold.ttf"));
		UFontFace* CondBlack = ImportFace(TEXT("BarlowCondensed-ExtraBold.ttf"));
		UFontFace* NumMedium = ImportFace(TEXT("Rajdhani-Medium.ttf"));
		UFontFace* NumSemi = ImportFace(TEXT("Rajdhani-SemiBold.ttf"));
		UFontFace* NumBold = ImportFace(TEXT("Rajdhani-Bold.ttf"));
		UFontFace* Stencil = ImportFace(TEXT("BlackOpsOne-Regular.ttf"));
		UFontFace* SC_Regular = ImportFace(TEXT("NotoSansSC-Regular.otf"));
		UFontFace* SC_Medium = ImportFace(TEXT("NotoSansSC-Medium.otf"));
		UFontFace* SC_Bold = ImportFace(TEXT("NotoSansSC-Bold.otf"));
		UFontFace* SC_Black = ImportFace(TEXT("NotoSansSC-Black.otf"));
		UFontFace* SerifBlack = ImportFace(TEXT("NotoSerifTC-Black.otf"));

		const FName Regular(TEXT("Regular")), Medium(TEXT("Medium")), SemiBold(TEXT("SemiBold")), Bold(TEXT("Bold")), Black(TEXT("Black"));
		const FFaceList Cjk = { { Regular, SC_Regular }, { Medium, SC_Medium }, { SemiBold, SC_Bold }, { Bold, SC_Bold }, { Black, SC_Black } };

		MakeFont(TEXT("F_Campus_Sans"), { { Regular, BarlowRegular }, { Medium, BarlowMedium }, { SemiBold, BarlowSemi }, { Bold, BarlowBold }, { Black, BarlowBold } }, Cjk, 1.f);
		MakeFont(TEXT("F_Campus_Cond"), { { Regular, CondMedium }, { Medium, CondMedium }, { SemiBold, CondSemi }, { Bold, CondBold }, { Black, CondBlack } }, Cjk, 0.94f);
		MakeFont(TEXT("F_Campus_Num"), { { Regular, NumMedium }, { Medium, NumMedium }, { SemiBold, NumSemi }, { Bold, NumBold }, { Black, NumBold } }, Cjk, 0.9f);
		MakeFont(TEXT("F_Campus_Title"), { { Regular, SerifBlack }, { Bold, SerifBlack }, { Black, SerifBlack } }, {}, 1.f);
		MakeFont(TEXT("F_Campus_Stencil"), { { Regular, Stencil }, { Bold, Stencil }, { Black, Stencil } }, { { Regular, SC_Black }, { Bold, SC_Black }, { Black, SC_Black } }, 0.92f);
		UE_LOG(LogCampusBootstrap, Display, TEXT("UI fonts built"));
	}

	void BuildCombatTextures()
	{
		AlphaTexture(TEXT("T_UI_Ring"), 512, 512, [](float U, float V)
		{
			const float R = FVector2f(U - 0.5f, V - 0.5f).Size() * 512.f;
			return FMath::Clamp(1.6f - FMath::Abs(R - 252.f), 0.f, 1.f);
		}, UITextureFolder, true);
		AlphaTexture(TEXT("T_UI_Line"), 64, 16, [](float, float V)
		{
			return FMath::Clamp(7.f - FMath::Abs(V - 0.5f) * 16.f, 0.f, 1.f);
		}, UITextureFolder, true);
		AlphaTexture(TEXT("T_UI_Skull"), 256, 256, SkullAlpha, UITextureFolder, true);
		AlphaTexture(TEXT("T_UI_MedalRing"), 256, 256, [](float U, float V)
		{
			const float R = FVector2f(U - 0.5f, V - 0.5f).Size();
			const float Outer = ShapeCoverage(0.012f - FMath::Abs(R - 0.471f));
			const float Inner = ShapeCoverage(0.005f - FMath::Abs(R - 0.405f)) * 0.28f;
			return FMath::Max(Outer, Inner);
		}, UITextureFolder, true);
		AlphaTexture(TEXT("T_UI_MedalChevron"), 128, 256, [](float U, float V)
		{
			const FVector2f P((U - 0.5f) * 0.5f, V - 0.5f);
			return FMath::Max(SegmentAlpha(P, { -0.16f, -0.43f }, { 0.16f, 0.f }, 0.044f), SegmentAlpha(P, { 0.16f, 0.f }, { -0.16f, 0.43f }, 0.044f));
		}, UITextureFolder, true);
		const TCHAR* Names[] = {
			TEXT("T_UI_MedalHeadshot"), TEXT("T_UI_MedalDoubleKill"), TEXT("T_UI_MedalTripleKill"), TEXT("T_UI_MedalFirstBlood"),
			TEXT("T_UI_MedalLongshot"), TEXT("T_UI_MedalRevenge"), TEXT("T_UI_MedalStreak5"), TEXT("T_UI_MedalMelee")
		};
		for (int32 Icon = 0; Icon < UE_ARRAY_COUNT(Names); ++Icon)
		{
			// mip coverage preserves these small silhouettes when the HUD is below 1080p
			AlphaTexture(Names[Icon], 256, 256, [Icon](float U, float V)
			{
				const FVector2f P(U - 0.5f, V - 0.5f);
				auto Circle = [&](const FVector2f& Center, float Radius) { return ShapeCoverage(Radius - (P - Center).Size()); };
				auto Ring = [&](float Radius, float HalfWidth) { return ShapeCoverage(HalfWidth - FMath::Abs(P.Size() - Radius)); };
				auto Skull = [&](float X, float Y, float Size) { return SkullAlpha((P.X - X) / Size + 0.5f, (P.Y - Y) / Size + 0.5f); };
				auto Stroke = [&](float X0, float Y0, float X1, float Y1, float Width = 0.022f) { return SegmentAlpha(P, { X0, Y0 }, { X1, Y1 }, Width); };
				float Alpha = 0.f;
				switch (Icon)
				{
				case 0:
					Alpha = FMath::Max(Skull(0.f, 0.f, 0.60f), Ring(0.35f, 0.018f));
					for (int32 Axis = 0; Axis < 4; ++Axis)
					{
						const float Angle = Axis * PI * 0.5f;
						const FVector2f D(FMath::Cos(Angle), FMath::Sin(Angle));
						Alpha = FMath::Max(Alpha, SegmentAlpha(P, D * 0.29f, D * 0.45f));
					}
					break;
				case 1:
				case 2:
					{
						const int32 Count = Icon == 1 ? 2 : 3;
						for (int32 Index = 0; Index < Count; ++Index)
						{
							const float X = (Index - (Count - 1) * 0.5f) * (Count == 2 ? 0.36f : 0.27f);
							const float Y = Count == 3 && Index == 1 ? -0.15f : 0.05f;
							Alpha = FMath::Max(Alpha, Skull(X, Y, Count == 2 ? 0.49f : 0.41f));
							Alpha = FMath::Max(Alpha, Stroke(X - 0.065f, 0.37f, X + 0.065f, 0.37f));
						}
					}
					break;
				case 3:
					Alpha = FMath::Max(TriangleAlpha(P, { 0.f, -0.45f }, { -0.21f, 0.10f }, { 0.21f, 0.10f }), Circle({ 0.f, 0.10f }, 0.21f));
					Alpha = FMath::Max(Alpha, Stroke(-0.32f, 0.39f, 0.32f, 0.39f));
					break;
				case 4:
					Alpha = FMath::Max(Ring(0.29f, 0.022f), Ring(0.11f, 0.02f));
					Alpha = FMath::Max(Alpha, Stroke(-0.46f, 0.f, -0.19f, 0.f));
					Alpha = FMath::Max(Alpha, Stroke(0.19f, 0.f, 0.46f, 0.f));
					Alpha = FMath::Max(Alpha, Stroke(0.f, -0.46f, 0.f, -0.19f));
					Alpha = FMath::Max(Alpha, Stroke(0.f, 0.19f, 0.f, 0.46f));
					break;
				case 5:
					Alpha = Skull(0.f, 0.015f, 0.47f);
					if (FMath::Atan2(P.Y, P.X) > -0.85f || P.X < 0.f) Alpha = FMath::Max(Alpha, Ring(0.35f, 0.026f));
					Alpha = FMath::Max(Alpha, TriangleAlpha(P, { 0.10f, -0.45f }, { 0.41f, -0.31f }, { 0.15f, -0.17f }));
					break;
				case 6:
					Alpha = Stroke(-0.34f, -0.38f, 0.34f, -0.38f);
					Alpha = FMath::Max(Alpha, Stroke(-0.34f, -0.38f, -0.28f, 0.19f));
					Alpha = FMath::Max(Alpha, Stroke(0.34f, -0.38f, 0.28f, 0.19f));
					Alpha = FMath::Max(Alpha, Stroke(-0.28f, 0.19f, 0.f, 0.44f));
					Alpha = FMath::Max(Alpha, Stroke(0.28f, 0.19f, 0.f, 0.44f));
					Alpha = FMath::Max(Alpha, Stroke(-0.16f, -0.17f, 0.f, 0.27f, 0.035f));
					Alpha = FMath::Max(Alpha, Stroke(0.16f, -0.17f, 0.f, 0.27f, 0.035f));
					break;
				case 7:
					for (const float Side : { -1.f, 1.f })
					{
						Alpha = FMath::Max(Alpha, TriangleAlpha(P, { Side * -0.35f, -0.44f }, { Side * 0.25f, 0.17f }, { Side * 0.10f, 0.31f }));
						Alpha = FMath::Max(Alpha, Stroke(Side * 0.11f, 0.08f, Side * 0.35f, 0.32f, 0.027f));
						Alpha = FMath::Max(Alpha, Stroke(Side * 0.23f, 0.25f, Side * 0.38f, 0.42f, 0.034f));
					}
					break;
				default: break;
				}
				return Alpha;
			}, UITextureFolder, true);
		}
	}

	void BuildTextures()
	{
		BuildCombatTextures();
		AlphaTexture(TEXT("T_UI_White"), 4, 4, [](float, float) { return 1.f; });
		AlphaTexture(TEXT("T_UI_GradL"), 256, 4, [](float U, float) { return FMath::Pow(1.f - U, 1.35f); });
		AlphaTexture(TEXT("T_UI_GradR"), 256, 4, [](float U, float) { return FMath::Pow(U, 1.35f); });
		AlphaTexture(TEXT("T_UI_GradUp"), 4, 256, [](float, float V) { return FMath::Pow(V, 1.35f); });
		AlphaTexture(TEXT("T_UI_GradDown"), 4, 256, [](float, float V) { return FMath::Pow(1.f - V, 1.35f); });
		// Horizontal band that fades at both ends (banners, separators)
		AlphaTexture(TEXT("T_UI_Band"), 256, 4, [](float U, float) { return Smooth(0.f, 0.22f, U) * Smooth(1.f, 0.78f, U); });
		AlphaTexture(TEXT("T_UI_Soft"), 128, 128, [](float U, float V)
		{
			const float R = FVector2D(U - 0.5f, V - 0.5f).Size() * 2.f;
			return FMath::Square(FMath::Clamp(1.f - R, 0.f, 1.f));
		});
		AlphaTexture(TEXT("T_UI_Disc"), 128, 128, [](float U, float V)
		{
			const float R = FVector2D(U - 0.5f, V - 0.5f).Size() * 128.f;
			return FMath::Clamp(62.f - R, 0.f, 1.f);
		});
		AlphaTexture(TEXT("T_UI_Vignette"), 512, 512, [](float U, float V)
		{
			const float R = FVector2D((U - 0.5f) * 2.f, (V - 0.5f) * 2.f).Size() / 1.41421f;
			return FMath::Pow(Smooth(0.32f, 1.f, R), 1.6f);
		});
		// Damage vignette with an organic, uneven edge
		AlphaTexture(TEXT("T_UI_Blood"), 512, 512, [](float U, float V)
		{
			const FVector2D P((U - 0.5f) * 2.f, (V - 0.5f) * 2.f);
			const float R = FMath::Max(FMath::Abs(P.X) * 0.92f, FMath::Abs(P.Y)) * 0.55f + P.Size() * 0.45f;
			const float N = FBM(U, V, 6, 4, 31);
			return FMath::Pow(Smooth(0.45f, 1.05f, R + (N - 0.5f) * 0.32f), 1.3f) * (0.75f + 0.25f * FBM(U, V, 24, 3, 77));
		});
		// Scope: opaque outside the eyepiece, soft inner shadow, transparent lens.
		AlphaTexture(TEXT("T_UI_Scope"), 1024, 1024, [](float U, float V)
		{
			const float R = FVector2D(U - 0.5f, V - 0.5f).Size() * 2.f;
			const float Edge = Smooth(0.955f, 0.985f, R);
			const float Shadow = Smooth(0.62f, 0.97f, R) * 0.72f;
			return FMath::Max(Edge, FMath::Pow(Shadow, 1.6f));
		});
		// Eyepiece exit-pupil shadow: soft inner darkening of a magnified optic's rear lens, clear outside the lens
		// (the 3D eyepiece rim and the defocused surroundings are there).
		AlphaTexture(TEXT("T_UI_Eyebox"), 512, 512, [](float U, float V)
		{
			const float R = FVector2D(U - 0.5f, V - 0.5f).Size() * 2.f;
			const float Inside = 1.f - Smooth(0.985f, 1.f, R);
			return Inside * FMath::Pow(Smooth(0.68f, 0.995f, R), 1.6f) * 0.85f;
		});
		// Diagonal hazard stripes (loading bars, warnings)
		AlphaTexture(TEXT("T_UI_Stripes"), 64, 64, [](float U, float V)
		{
			const float S = FMath::Fmod(U * 4.f + V * 4.f, 1.f);
			return FMath::Clamp((FMath::Abs(S - 0.5f) - 0.22f) * 18.f, 0.f, 1.f);
		});
		// Blood splatter for the damage overlay: lobed metaball blob, satellite droplets, uneven density.
		AlphaTexture(TEXT("T_UI_Splat"), 512, 512, [](float U, float V)
		{
			const FVector2D P(U - 0.5f, V - 0.5f);
			static const FVector2D Lobes[6] = { { 0.f, 0.f }, { 0.09f, -0.05f }, { -0.08f, 0.06f }, { 0.04f, 0.1f }, { -0.1f, -0.07f }, { 0.12f, 0.08f } };
			static const float Radii[6] = { 0.2f, 0.13f, 0.12f, 0.1f, 0.09f, 0.07f };
			float Field = 0.f;
			for (int32 i = 0; i < 6; ++i)
			{
				Field += FMath::Square(Radii[i]) / FMath::Max((P - Lobes[i]).SizeSquared(), 1.e-4f);
			}
			for (int32 i = 0; i < 18; ++i)
			{
				const float Ang = Hash(i, 3, 997, 5) * 2.f * PI;
				const float Dist = 0.22f + Hash(i, 7, 997, 5) * 0.24f;
				const float R = 0.006f + Hash(i, 11, 997, 5) * 0.022f;
				Field += 0.9f * FMath::Square(R) / FMath::Max((P - FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Dist).SizeSquared(), 1.e-5f);
			}
			const float Edge = Smooth(0.9f, 1.25f, Field * (0.75f + 0.5f * FBM(U, V, 8, 4, 91)));
			const float Density = 0.72f + 0.28f * FBM(U, V, 20, 3, 17);
			return Edge * Density * Smooth(0.5f, 0.42f, P.Size());
		});

		// ---- FX sprites (lit-agnostic, sampled by FX materials)
		AlphaTexture(TEXT("T_FX_Smoke"), 256, 256, [](float U, float V)
		{
			const float R = FVector2D(U - 0.5f, V - 0.5f).Size() * 2.f;
			const float N = FBM(U, V, 4, 5, 13);
			return FMath::Pow(FMath::Clamp(1.f - R * (0.78f + 0.45f * N), 0.f, 1.f), 1.5f) * (0.55f + 0.6f * N);
		}, FXTextureFolder);
		// Flame puff: a ragged, ridged billow (the fireball sprites), brightest in the folds.
		AlphaTexture(TEXT("T_FX_Flame"), 256, 256, [](float U, float V)
		{
			const float R = FVector2D(U - 0.5f, V - 0.5f).Size() * 2.f;
			const float N1 = FBM(U, V, 3, 5, 71);
			const float N2 = FBM(U, V, 6, 4, 113);
			const float Ridge = 1.f - FMath::Abs(N2 * 2.f - 1.f);
			const float Body = FMath::Clamp(1.f - R * (0.65f + 0.7f * N1), 0.f, 1.f);
			return FMath::Pow(Body, 1.1f) * (0.3f + 0.7f * Ridge * Ridge);
		}, FXTextureFolder);
		// Bullet strike in glass: a crushed, frosted core, radial cracks running out (forking as they go) and concentric
		// ring fractures stepping between them. Alpha only; the material darkens the hole itself.
		{
			const int32 S = 512;
			struct FSeg { FVector2D A, B; float Width, Bright; };
			TArray<FSeg> Segs;
			FRandomStream Rng(9137);
			const FVector2D C(0.5f, 0.5f);
			const int32 NumRays = 15;
			TArray<TArray<FVector2D>> Rays;
			for (int32 i = 0; i < NumRays; ++i)
			{
				float Ang = 2.f * PI * (i + Rng.FRandRange(-0.3f, 0.3f)) / NumRays;
				const float Reach = Rng.FRandRange(0.30f, 0.49f);
				TArray<FVector2D>& Pts = Rays.AddDefaulted_GetRef();
				float R = 0.035f;
				Pts.Add(C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
				while (R < Reach)
				{
					R = FMath::Min(R + Rng.FRandRange(0.04f, 0.075f), Reach);
					Ang += Rng.FRandRange(-0.09f, 0.09f);
					Pts.Add(C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
					const FVector2D& P0 = Pts[Pts.Num() - 2];
					const FVector2D& P1 = Pts.Last();
					Segs.Add({ P0, P1, FMath::Lerp(0.0032f, 0.0016f, R / 0.5f), FMath::Lerp(1.f, 0.7f, R / 0.5f) });
					if (R > 0.12f && Rng.FRand() < 0.3f)
					{
						const float BAng = Ang + (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(0.4f, 0.75f);
						const FVector2D BEnd = P1 + FVector2D(FMath::Cos(BAng), FMath::Sin(BAng)) * Rng.FRandRange(0.05f, 0.12f);
						Segs.Add({ P1, BEnd, 0.0014f, 0.6f });
					}
				}
			}
			// Ring fractures: chords between neighbouring rays at a few radii, not every gap.
			for (const float Ring : { 0.075f, 0.13f, 0.21f, 0.3f })
			{
				for (int32 i = 0; i < NumRays; ++i)
				{
					if (Rng.FRand() > (Ring < 0.2f ? 0.85f : 0.55f))
					{
						continue;
					}
					auto At = [&](const TArray<FVector2D>& Pts) -> TOptional<FVector2D>
					{
						for (int32 k = 1; k < Pts.Num(); ++k)
						{
							const float R0 = FVector2D::Distance(Pts[k - 1], C);
							const float R1 = FVector2D::Distance(Pts[k], C);
							if (R0 <= Ring && R1 >= Ring)
							{
								return FMath::Lerp(Pts[k - 1], Pts[k], (Ring - R0) / FMath::Max(R1 - R0, 1.e-4f));
							}
						}
						return {};
					};
					const TOptional<FVector2D> P0 = At(Rays[i]);
					const TOptional<FVector2D> P1 = At(Rays[(i + 1) % NumRays]);
					if (P0 && P1)
					{
						// Slightly bowed outward, in two pieces.
						const FVector2D Mid = (*P0 + *P1) * 0.5f;
						const FVector2D Bow = Mid + (Mid - C).GetSafeNormal() * Ring * Rng.FRandRange(0.02f, 0.08f);
						Segs.Add({ *P0, Bow, 0.0015f, 0.75f });
						Segs.Add({ Bow, *P1, 0.0015f, 0.75f });
					}
				}
			}
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(S * S);
			ParallelFor(S, [&](int32 Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					const FVector2D P((X + 0.5f) / S, (Y + 0.5f) / S);
					float A = 0.f;
					for (const FSeg& G : Segs)
					{
						const FVector2D AB = G.B - G.A;
						const float T = FMath::Clamp(FVector2D::DotProduct(P - G.A, AB) / FMath::Max(AB.SizeSquared(), 1.e-8f), 0.f, 1.f);
						const float D = (float)FVector2D::Distance(P, G.A + AB * T);
						A = FMath::Max(A, G.Bright * Smooth(G.Width * 1.6f, G.Width * 0.3f, D));
					}
					// The crushed zone round the hole: frosted, dense near the centre.
					const float R = (float)FVector2D::Distance(P, C);
					const float Frost = Smooth(0.075f, 0.02f, R) * (0.55f + 0.45f * FBM((float)P.X, (float)P.Y, 48, 3, 77));
					A = FMath::Max(A, Frost);
					Pixels[Y * S + X] = FColor(255, 255, 255, (uint8)FMath::RoundToInt(FMath::Clamp(A, 0.f, 1.f) * 255.f));
				}
			});
			SaveTexture(FXTextureFolder, TEXT("T_FX_GlassCrack"), S, S, Pixels, false, true);
		}
		// Blast scorch: soot thickest at the centre, thrown out in uneven rays, with a ragged edge.
		{
			const int32 S = 512;
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(S * S);
			ParallelFor(S, [&](int32 Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					const float U = (X + 0.5f) / S;
					const float V = (Y + 0.5f) / S;
					const FVector2D P(U - 0.5f, V - 0.5f);
					const float R = P.Size() * 2.f;
					const float Ang = FMath::Atan2(P.Y, P.X);
					const float Rays = FBM(FMath::Cos(Ang) * 0.5f + 0.5f, FMath::Sin(Ang) * 0.5f + 0.5f, 10, 4, 211);
					const float Reach = 0.42f + 0.55f * Rays * Rays;
					const float Grain = FBM(U, V, 24, 4, 307);
					const float Soot = (1.f - Smooth(0.15f, Reach, R + (Grain - 0.5f) * 0.25f)) * (0.7f + 0.3f * Grain);
					const float Pit = (1.f - Smooth(0.f, 0.12f, R)) * 0.6f;
					const float A = FMath::Clamp(Soot + Pit, 0.f, 1.f);
					const float L = FMath::Lerp(0.012f, 0.07f, Smooth(0.f, 0.8f, R)) * (0.8f + 0.4f * Grain);
					const uint8 Rc = (uint8)FMath::RoundToInt(FMath::Pow(L, 1.f / 2.2f) * 255.f);
					const uint8 Gc = (uint8)FMath::RoundToInt(FMath::Pow(L * 0.93f, 1.f / 2.2f) * 255.f);
					const uint8 Bc = (uint8)FMath::RoundToInt(FMath::Pow(L * 0.86f, 1.f / 2.2f) * 255.f);
					Pixels[Y * S + X] = FColor(Rc, Gc, Bc, (uint8)FMath::RoundToInt(A * 255.f));
				}
			});
			SaveTexture(FXTextureFolder, TEXT("T_FX_Scorch"), S, S, Pixels, false, true);
		}
		AlphaTexture(TEXT("T_FX_Flash"), 256, 256, [](float U, float V)
		{
			const FVector2D P(U - 0.5f, V - 0.5f);
			const float R = P.Size() * 2.f;
			const float Ang = FMath::Atan2(P.Y, P.X);
			const float Spikes = FMath::Pow(FMath::Abs(FMath::Cos(Ang * 3.f)), 6.f) * 0.55f + FMath::Pow(FMath::Abs(FMath::Cos(Ang * 2.f + 0.7f)), 10.f) * 0.35f;
			const float Core = FMath::Pow(FMath::Clamp(1.f - R * 2.2f, 0.f, 1.f), 1.4f);
			const float Petal = FMath::Clamp(1.f - R / FMath::Max(0.2f + Spikes, 0.05f), 0.f, 1.f);
			return FMath::Clamp(Core + FMath::Pow(Petal, 1.8f) * 0.9f, 0.f, 1.f) * (0.8f + 0.2f * FBM(U, V, 8, 3, 5));
		}, FXTextureFolder);

		// Bullet hole: dark crater, chipped rim and short radial cracks (RGB colour, A coverage).
		{
			const int32 S = 256;
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(S * S);
			ParallelFor(S, [&](int32 Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					const float U = (X + 0.5f) / S;
					const float V = (Y + 0.5f) / S;
					const FVector2D P(U - 0.5f, V - 0.5f);
					const float R = P.Size() * 2.f;
					const float Ang = FMath::Atan2(P.Y, P.X);
					const float Wobble = 1.f + (FBM(FMath::Cos(Ang) * 0.5f + 0.5f, FMath::Sin(Ang) * 0.5f + 0.5f, 6, 3, 9) - 0.5f) * 0.6f;
					const float Hole = 1.f - Smooth(0.10f, 0.15f, R / Wobble);
					const float Rim = (1.f - Smooth(0.15f, 0.42f, R / Wobble)) * (1.f - Hole);
					const float CrackA = FMath::Pow(FMath::Abs(FMath::Cos(Ang * 4.5f + 1.3f)), 60.f);
					const float CrackB = FMath::Pow(FMath::Abs(FMath::Cos(Ang * 3.f + 0.2f)), 90.f);
					const float Crack = FMath::Max(CrackA, CrackB) * (1.f - Smooth(0.2f, 0.85f, R)) * Smooth(0.08f, 0.16f, R);
					const float Dust = (1.f - Smooth(0.3f, 0.98f, R)) * FBM(U, V, 16, 3, 41) * 0.55f;
					const float A = FMath::Clamp(Hole + Rim * 0.85f + Crack * 0.8f + Dust, 0.f, 1.f);
					// Crater is black; the rim is pale pulverised material.
					const float Pale = FMath::Clamp(Rim * 1.4f - Hole + Dust * 0.6f, 0.f, 1.f);
					const float L = FMath::Lerp(0.02f, 0.55f, Pale) * (1.f - Crack * 0.7f);
					const uint8 B = (uint8)FMath::RoundToInt(FMath::Pow(L, 1.f / 2.2f) * 255.f);
					Pixels[Y * S + X] = FColor(B, B, B, (uint8)FMath::RoundToInt(A * 255.f));
				}
			});
			SaveTexture(FXTextureFolder, TEXT("T_FX_BulletHole"), S, S, Pixels, false, true);
		}

		// Optic glass grime: R oily smears and two fingerprints (ridged whorls), G dust specks and a few lint fibres,
		// B fine wipe scratches. Sampled across a lens or a sight window (UV 0..1).
		{
			const int32 S = 512;
			struct FBlob { FVector2D C; FVector2D R; float Ang; float Amount; };
			TArray<FBlob> Smears;
			for (int32 i = 0; i < 7; ++i)
			{
				Smears.Add({ FVector2D(0.15f + 0.7f * Hash(i, 1, 9973, 401), 0.15f + 0.7f * Hash(i, 2, 9973, 401)),
					FVector2D(0.08f + 0.16f * Hash(i, 3, 9973, 401), 0.04f + 0.08f * Hash(i, 4, 9973, 401)), Hash(i, 5, 9973, 401) * PI, 0.35f + 0.5f * Hash(i, 6, 9973, 401) });
			}
			const FBlob Prints[2] = { { FVector2D(0.7f, 0.32f), FVector2D(0.13f, 0.17f), 0.5f, 1.f }, { FVector2D(0.26f, 0.72f), FVector2D(0.11f, 0.15f), -0.7f, 0.7f } };
			struct FSpeck { FVector2D C; float R; float A; };
			TArray<FSpeck> Specks;
			for (int32 i = 0; i < 160; ++i)
			{
				const float Big = Hash(i, 9, 9973, 503);
				Specks.Add({ FVector2D(Hash(i, 7, 9973, 503), Hash(i, 8, 9973, 503)), (0.6f + 2.4f * Big * Big * Big) / S, 0.4f + 0.6f * Hash(i, 10, 9973, 503) });
			}
			struct FLine { FVector2D A; FVector2D B; float W; float Amount; float Bend; };
			TArray<FLine> Scratches;
			for (int32 i = 0; i < 9; ++i)
			{
				const FVector2D A(Hash(i, 11, 9973, 607), Hash(i, 12, 9973, 607));
				const float Ang = Hash(i, 13, 9973, 607) * PI;
				const float Len = 0.15f + 0.4f * Hash(i, 14, 9973, 607);
				Scratches.Add({ A, A + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Len, 0.6f / S, 0.25f + 0.5f * Hash(i, 15, 9973, 607), 0.f });
			}
			TArray<FLine> Fibres;
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2D A(0.15f + 0.7f * Hash(i, 16, 9973, 709), 0.15f + 0.7f * Hash(i, 17, 9973, 709));
				const float Ang = Hash(i, 18, 9973, 709) * 2.f * PI;
				Fibres.Add({ A, A + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * (0.03f + 0.05f * Hash(i, 19, 9973, 709)), 1.2f / S, 0.8f, (Hash(i, 20, 9973, 709) - 0.5f) * 0.05f });
			}
			auto SegDist = [](const FVector2D& P, const FVector2D& A, const FVector2D& B, float Bend)
			{
				const FVector2D AB = B - A;
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.e-8f), 0.f, 1.f);
				const FVector2D N(-AB.Y, AB.X);
				const FVector2D Q = A + AB * T + N.GetSafeNormal() * Bend * FMath::Sin(T * PI);
				return FVector2D::Distance(P, Q);
			};
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(S * S);
			ParallelFor(S, [&](int32 Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					const FVector2D P((X + 0.5f) / S, (Y + 0.5f) / S);
					float Smear = 0.f;
					for (const FBlob& B : Smears)
					{
						const FVector2D D = (P - B.C).GetRotated(FMath::RadiansToDegrees(B.Ang));
						const float E = FMath::Square(D.X / B.R.X) + FMath::Square(D.Y / B.R.Y);
						// Wiped streaks: the density varies along the smear like a cloth dragged across it.
						const float Streak = 0.6f + 0.4f * FBM(D.X * 2.f + 0.5f, D.Y * 18.f + 0.5f, 6, 3, 811);
						Smear += B.Amount * FMath::Exp(-E * 1.6f) * Streak;
					}
					for (const FBlob& F : Prints)
					{
						const FVector2D D = (P - F.C).GetRotated(FMath::RadiansToDegrees(F.Ang));
						const float E = FMath::Sqrt(FMath::Square(D.X / F.R.X) + FMath::Square(D.Y / F.R.Y));
						const float Warp = (FBM(P.X, P.Y, 8, 3, 919) - 0.5f) * 0.25f;
						const float Ridges = 0.5f + 0.5f * FMath::Cos((E + Warp) * 2.f * PI * 11.f);
						Smear += F.Amount * (1.f - Smooth(0.7f, 1.f, E)) * (0.25f + 0.75f * Ridges) * 0.9f;
					}
					Smear *= 0.7f + 0.3f * FBM(P.X, P.Y, 12, 4, 1013);
					float Dust = 0.f;
					for (const FSpeck& D : Specks)
					{
						const float Dist = FVector2D::Distance(P, D.C);
						Dust = FMath::Max(Dust, D.A * (1.f - Smooth(D.R * 0.6f, D.R * 1.6f, Dist)));
					}
					for (const FLine& F : Fibres)
					{
						Dust = FMath::Max(Dust, F.Amount * (1.f - Smooth(F.W * 0.5f, F.W * 1.5f, SegDist(P, F.A, F.B, F.Bend))));
					}
					float Scratch = 0.f;
					for (const FLine& L : Scratches)
					{
						Scratch = FMath::Max(Scratch, L.Amount * (1.f - Smooth(L.W * 0.4f, L.W * 1.8f, SegDist(P, L.A, L.B, 0.f))));
					}
					auto Enc = [](float V) { return (uint8)FMath::RoundToInt(FMath::Pow(FMath::Clamp(V, 0.f, 1.f), 1.f / 2.2f) * 255.f); };
					Pixels[Y * S + X] = FColor(Enc(Smear), Enc(Dust), Enc(Scratch), 255);
				}
			});
			UTexture2D* Dirt = SaveTexture(FXTextureFolder, TEXT("T_FX_LensDirt"), S, S, Pixels, false, true);
			if (Dirt)
			{
				Dirt->AddressX = TA_Wrap;
				Dirt->AddressY = TA_Wrap;
				Dirt->PostEditChange();
				SaveAsset(Dirt);
			}
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("UI textures built"));
	}

	void BuildMaterials()
	{
		auto LoadTex = [](const TCHAR* Folder, const TCHAR* Name) { return LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Folder, Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet); };
		UTexture* Smoke = LoadTex(FXTextureFolder, TEXT("T_FX_Smoke"));
		UTexture* Flash = LoadTex(FXTextureFolder, TEXT("T_FX_Flash"));
		UTexture* Hole = LoadTex(FXTextureFolder, TEXT("T_FX_BulletHole"));

		// Optic glass with a parallax-free reticle: the mark is drawn where the eye ray lines up with the optic's
		// axis, so it sits on the point of aim and drifts inside the window when the weapon sways (like the real thing).
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Reticle")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TwoSided = true;
			B.M->bDisableDepthTest = false;
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float3 V = normalize(WP - Cam);
float3 Nn = normalize(N);
if (dot(V, Nn) < 0.0) Nn = -Nn;
float dv = max(dot(V, Nn), 0.05);
float2 ang = float2(dot(V, normalize(T1)), dot(V, normalize(T2))) / dv / 0.000290888;
float r = length(ang);
float aa = max(fwidth(r), 0.35);
float dotM = 1.0 - smoothstep(DotMOA - aa, DotMOA + aa, r);
float ringM = (1.0 - smoothstep(RingWidth - aa, RingWidth + aa, abs(r - RingMOA))) * Ring;
float2 aAng = abs(ang);
float ticks = Ring * (1.0 - smoothstep(RingWidth - aa, RingWidth + aa, min(aAng.x, aAng.y))) * step(RingMOA + RingWidth * 0.5, max(aAng.x, aAng.y)) * step(max(aAng.x, aAng.y), RingMOA + RingWidth * 5.0);
float mark = saturate(dotM + ringM + ticks);
float glow = exp(-r * r / (Halo * Halo)) * 0.35 + exp(-pow(abs(r - RingMOA) / (Halo * 0.6), 2.0)) * 0.12 * Ring;
float2 uvc = UV - 0.5;
float window = Round > 0.5 ? (1.0 - smoothstep(0.47, 0.5, length(uvc))) : (1.0 - smoothstep(0.46, 0.5, max(abs(uvc.x), abs(uvc.y))));
float fres = pow(1.0 - saturate(dot(V, Nn)), 3.0);
float sheen = saturate(0.5 - uvc.y) * 0.6 + fres;
if (Coated > 0.5)
{
	// A magnified optic's eyepiece seen off-axis: deep multi-coated glass (purple to green across the coating), the
	// edges of the lens stack inside as faint rings, and a soft window of sky caught in the upper half. LensOpacity
	// fades it as the eye comes onto the eyepiece.
	float rr = length(uvc) * 2.0;
	float3 coat = lerp(float3(0.16, 0.05, 0.24), float3(0.04, 0.22, 0.13), saturate(uvc.x * 1.4 + 0.5 + fres));
	float rings = exp(-pow((rr - 0.58) / 0.035, 2.0)) * 0.6 + exp(-pow((rr - 0.80) / 0.03, 2.0)) * 0.4;
	float2 hp = uvc - float2(-0.13, -0.17);
	float spot = exp(-dot(hp, hp) / 0.010) * 0.9 + exp(-dot(hp, hp) / 0.05) * 0.25;
	float3 lens = coat * (0.35 + fres * 2.0 + saturate(0.4 - uvc.y) * 0.6) + rings * float3(0.10, 0.11, 0.12) + spot * float3(0.85, 0.9, 1.0) + float3(0.012, 0.014, 0.016);
	Opac = saturate(LensOpacity) * window;
	return lens * window;
}
// Handled glass: oily smears and prints catch the light as a faint milky haze (strongest where the sheen is), dust
// specks sit on it as pale points; the reticle itself is projected and stays crisp.
float3 grime = Texture2DSampleLevel(DirtTex, DirtTexSampler, UV * 0.85 + DirtOffset.xy, 1.0).rgb * Dirt;
float haze = grime.r * (0.5 + sheen) + grime.b * (0.3 + fres * 2.0);
float film = saturate(haze * 0.5 + grime.g * 1.1);
Opac = max(saturate(LensOpacity * (0.6 + sheen) + mark + glow * 0.4 + film) * window, Debug * window);
return (Color * Intensity * (mark + glow) + LensTint * sheen * 0.35 + LensTint * 2.6 * film) * window + float3(0.0, Debug * 4.0, 0.0) * window * saturate(1.0 - r / 600.0);
)"), CMOT_Float3, { TEXT("WP"), TEXT("Cam"), TEXT("N"), TEXT("T1"), TEXT("T2"), TEXT("UV"), TEXT("Color"), TEXT("Intensity"), TEXT("DotMOA"), TEXT("RingMOA"), TEXT("RingWidth"), TEXT("Ring"), TEXT("Halo"), TEXT("Round"), TEXT("LensTint"), TEXT("LensOpacity"), TEXT("Debug"), TEXT("Coated"), TEXT("DirtTex"), TEXT("Dirt"), TEXT("DirtOffset") },
				{ { TEXT("Opac"), CMOT_Float1 } });
			UTexture* LensDirt = LoadTex(FXTextureFolder, TEXT("T_FX_LensDirt"));
			B.Link(B.TexObject(TEXT("DirtTex"), LensDirt ? LensDirt : LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black")), SAMPLERTYPE_Color), C, TEXT("DirtTex"));
			B.Link(B.Scalar(TEXT("Dirt"), 1.f), C, TEXT("Dirt"));
			B.Link(B.Vec(TEXT("DirtOffset"), FLinearColor(0.f, 0.f, 0.f)), C, TEXT("DirtOffset"));
			auto LocalAxis = [&B](float X, float Y, float Z)
			{
				UMaterialExpressionConstant3Vector* V = B.Make<UMaterialExpressionConstant3Vector>();
				V->Constant = FLinearColor(X, Y, Z);
				UMaterialExpressionTransform* T = B.Make<UMaterialExpressionTransform>();
				T->TransformSourceType = TRANSFORMSOURCE_Local;
				T->TransformType = TRANSFORM_World;
				B.Link(V, T);
				return T;
			};
			B.Link(B.Make<UMaterialExpressionWorldPosition>(), C, TEXT("WP"));
			B.Link(B.Make<UMaterialExpressionCameraPositionWS>(), C, TEXT("Cam"));
			B.Link(LocalAxis(0.f, 0.f, 1.f), C, TEXT("N"));
			B.Link(LocalAxis(1.f, 0.f, 0.f), C, TEXT("T1"));
			B.Link(LocalAxis(0.f, 1.f, 0.f), C, TEXT("T2"));
			B.Link(B.Make<UMaterialExpressionTextureCoordinate>(), C, TEXT("UV"));
			B.Link(B.Vec(TEXT("Color"), FLinearColor(1.f, 0.05f, 0.03f)), C, TEXT("Color"));
			B.Link(B.Vec(TEXT("LensTint"), FLinearColor(0.30f, 0.37f, 0.40f)), C, TEXT("LensTint"));
			const TPair<const TCHAR*, float> Scalars[] = {
				{ TEXT("Intensity"), 30.f }, { TEXT("DotMOA"), 9.f }, { TEXT("RingMOA"), 150.f }, { TEXT("RingWidth"), 7.f },
				{ TEXT("Ring"), 0.f }, { TEXT("Halo"), 15.f }, { TEXT("Round"), 1.f }, { TEXT("LensOpacity"), 0.07f }, { TEXT("Debug"), 0.f }, { TEXT("Coated"), 0.f } };
			for (const TPair<const TCHAR*, float>& S : Scalars)
			{
				B.Link(B.Scalar(S.Key, S.Value), C, S.Key);
			}
			// The dot keeps the same apparent brightness whether the eye is adapted to a classroom or to the sun.
			B.Out(B.ExposureCompensated(C, 0.26f, TEXT("return")), MP_EmissiveColor);
			B.Out(C, MP_Opacity, TEXT("Opac"));
			FinishMaterial(B.M);
		}

		// Magnified optic eyepiece. At the hip it is lit glass: near black with ~4% reflectance, so the surroundings
		// show in it through the scene's own reflections, the rims of the lens stack behind barely catch the light, and
		// the exit pupil floats where the eye ray lines up with the axis. With the eye on it (Live) it shows the
		// magnified view (View: a scene capture along the optic's axis, scene-referred HDR) with a duplex reticle, a red
		// centre dot and the soft shading at the rim of the exit pupil. The rest of the view stays as it is.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_ScopeLens")) };
			B.M->SetShadingModel(MSM_DefaultLit);
			B.M->BlendMode = BLEND_Masked;
			B.M->TwoSided = true;
			// The picture moves with the aim while the glass barely moves: the temporal upscaler must not carry the
			// glass's history over (it smeared the swaying picture into mush).
			B.M->bHasPixelAnimation = true;
			UTexture* Black = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			UMaterialExpression* UV = B.Make<UMaterialExpressionTextureCoordinate>();
			UMaterialExpressionCustom* SampleUV = B.Custom(TEXT("float2 c = UV - 0.5; c.x *= FlipU > 0.5 ? -1.0 : 1.0; c.y *= FlipV > 0.5 ? -1.0 : 1.0; return (SwapUV > 0.5 ? c.yx : c) + 0.5;"),
				CMOT_Float2, { TEXT("UV"), TEXT("FlipU"), TEXT("FlipV"), TEXT("SwapUV") });
			B.Link(UV, SampleUV, TEXT("UV"));
			B.Link(B.Scalar(TEXT("FlipU"), 0.f), SampleUV, TEXT("FlipU"));
			B.Link(B.Scalar(TEXT("FlipV"), 0.f), SampleUV, TEXT("FlipV"));
			B.Link(B.Scalar(TEXT("SwapUV"), 0.f), SampleUV, TEXT("SwapUV"));
			// The capture is supersampled against the eyepiece on screen. Trilinear filtering blurs that away (it blends in
			// the next mip from a texel ratio of one up); four taps over the pixel's own footprint in the full mip resolve
			// it sharp without aliasing, and only past twice the pixel's size does a smaller mip take over.
			UMaterialExpressionCustom* View = B.Custom(TEXT(R"(
float2 dx = ddx(SUV);
float2 dy = ddy(SUV);
float w, h;
ViewTex.GetDimensions(w, h);
float fp = max(length(dx * float2(w, h)), length(dy * float2(w, h)));
float lod = max(log2(max(fp, 1.0)) - 1.0, 0.0);
return 0.25 * (Texture2DSampleLevel(ViewTex, ViewTexSampler, SUV + 0.25 * (dx + dy), lod).rgb
	+ Texture2DSampleLevel(ViewTex, ViewTexSampler, SUV + 0.25 * (dx - dy), lod).rgb
	+ Texture2DSampleLevel(ViewTex, ViewTexSampler, SUV - 0.25 * (dx - dy), lod).rgb
	+ Texture2DSampleLevel(ViewTex, ViewTexSampler, SUV - 0.25 * (dx + dy), lod).rgb);
)"), CMOT_Float3, { TEXT("SUV"), TEXT("ViewTex") });
			B.Link(SampleUV, View, TEXT("SUV"));
			B.Link(B.TexObject(TEXT("View"), Black, SAMPLERTYPE_Color), View, TEXT("ViewTex"));
			UMaterialExpressionCustom* LiveC = B.Custom(TEXT(R"(
float2 c = UV - 0.5;
float r = length(c) * 2.0;
float aa = max(fwidth(r), 0.0015);
float2 a = abs(c);
float thin = 0.0016;
float thick = 0.011;
float gap = step(0.03, max(a.x, a.y));
float lines = max(1.0 - smoothstep(thin - aa, thin + aa, a.y), 1.0 - smoothstep(thin - aa, thin + aa, a.x)) * gap;
float posts = max((1.0 - smoothstep(thick - aa, thick + aa, a.y)) * step(0.2, a.x), (1.0 - smoothstep(thick - aa, thick + aa, a.x)) * step(0.2, c.y));
float ink = saturate(max(lines, posts));
float rim = 1.0 - smoothstep(0.72, 1.0, r) * 0.9;
float2 sp = c - Shadow.xy;
float pupil = 1.0 - smoothstep(0.30, 0.5, length(sp)) * 0.55;
Mask = 1.0 - step(1.0, r);
float3 s = Scene * Gain * rim * pupil;
// A clean ocular, the way a looked-after optic is: only a faint veil where the glass is smeared and the odd wipe
// mark flaring on bright parts of the picture. Dust specks and heavy smears read as dirt on the screen.
float3 grime = Texture2DSampleLevel(DirtTex, DirtTexSampler, UV * 0.9 + 0.05, 2.0).rgb * Dirt * float3(0.18, 0.0, 0.5);
float3 veil = 0.0;
for (int k = 0; k < 6; ++k)
{
	float a = k * 1.0472 + 0.4;
	veil += Texture2DSampleLevel(ViewTex, ViewTexSampler, saturate(UV + float2(cos(a), sin(a)) * 0.09), 0).rgb;
}
veil = veil / 6.0 * Gain * rim;
s = lerp(s, s * 0.72 + veil * 0.42, saturate(grime.r * 0.65));
s *= 1.0 - smoothstep(0.1, 0.7, grime.g) * 0.3;
s += veil * grime.b * 0.2;
return s * (1.0 - ink * 0.96);
)"), CMOT_Float3, { TEXT("UV"), TEXT("Scene"), TEXT("Gain"), TEXT("Shadow"), TEXT("DirtTex"), TEXT("Dirt"), TEXT("ViewTex") }, { { TEXT("Mask"), CMOT_Float1 } });
			UTexture* LensDirt = LoadTex(FXTextureFolder, TEXT("T_FX_LensDirt"));
			UTexture* DirtOrBlack = LensDirt ? LensDirt : Black;
			B.Link(UV, LiveC, TEXT("UV"));
			B.Link(View, LiveC, TEXT("Scene"));
			B.Link(B.Scalar(TEXT("Gain"), 1.f), LiveC, TEXT("Gain"));
			B.Link(B.Vec(TEXT("Shadow"), FLinearColor(0.f, 0.f, 0.f)), LiveC, TEXT("Shadow"));
			UMaterialExpression* DirtAmount = B.Scalar(TEXT("Dirt"), 1.f);
			B.Link(B.TexObject(TEXT("DirtTex"), DirtOrBlack, SAMPLERTYPE_Color), LiveC, TEXT("DirtTex"));
			B.Link(DirtAmount, LiveC, TEXT("Dirt"));
			// Same parameter name as the sampled View: both take the scene capture set on the instance.
			B.Link(B.TexObject(TEXT("View"), Black, SAMPLERTYPE_Color), LiveC, TEXT("ViewTex"));
			// The capture may come back pre-exposed; PreExposed 1 takes the view's exposure back out.
			UMaterialExpression* Live = B.Mul(LiveC, B.Lerp(B.Const(1.f), B.Make<UMaterialExpressionEyeAdaptationInverse>(), B.Scalar(TEXT("PreExposed"), 0.f)));
			UMaterialExpressionCustom* CoatC = B.Custom(TEXT(R"(
float2 c = UV - 0.5;
float rr = length(c) * 2.0;
float3 V = normalize(WP - Cam);
float3 Nn = normalize(N);
if (dot(V, Nn) > 0.0) Nn = -Nn;
float dv = max(-dot(V, Nn), 0.05);
// The exit pupil is a real image a few cm behind the eyepiece: off the axis it drifts across the glass (parallax)
// and fades out as the eye leaves the eye box.
float2 off = float2(dot(V, normalize(T1)), dot(V, normalize(T2))) / dv * PupilK;
float inBox = saturate(1.0 - length(off) / 0.45);
float pupil = (1.0 - smoothstep(PupilR * 0.55, PupilR, length(c - off))) * inBox * inBox;
float rings = exp(-pow((rr - 0.62) / 0.03, 2.0)) * 0.5 + exp(-pow((rr - 0.84) / 0.025, 2.0)) * 0.35;
float dotM = 1.0 - smoothstep(0.008, 0.013, length(c));
Dot = dotM * Live;
float hip = 1.0 - Live;
// Faint prints break up the reflections on the glass; a looked-after lens shows no dust specks.
float3 grime = Texture2DSample(DirtTex, DirtTexSampler, UV * 0.9 + 0.05).rgb * Dirt * float3(0.35, 0.0, 0.3);
Spec = 0.5 * hip;
Rough = lerp(saturate(0.035 + rings * 0.06 + grime.r * 0.32 + grime.b * 0.2), 1.0, Live);
Base = (float3(0.0055, 0.006, 0.0065) + grime.g * 0.05 + grime.r * 0.006) * hip;
// The lens sits at the bottom of the eyecup, so the scene's own reflection rays mostly find the tube wall. The open
// sky and the ground behind the shooter still show in the convex glass, sky in its upper half and ground below
// (Fresnel), the multi-coating tinting them green at the centre and violet toward the rim: dark glass, not a hole.
float3 Nc = normalize(Nn + (normalize(T1) * c.x + normalize(T2) * c.y) * 0.9);
float dc = max(-dot(V, Nc), 0.05);
float3 R = reflect(V, Nc);
float fres = 0.04 + 0.96 * pow(1.0 - dc, 5.0);
float3 env = lerp(float3(0.17, 0.155, 0.14), float3(0.72, 0.82, 1.0), smoothstep(-0.15, 0.4, R.z));
float3 coatTint = lerp(float3(0.78, 1.0, 0.86), float3(0.95, 0.78, 1.0), saturate(rr * 1.15));
float glassSheen = (0.22 + fres * 1.6) * (1.0 - smoothstep(0.85, 0.99, rr) * 0.5);
return (PupilColor * pupil + rings * 0.03 + env * coatTint * glassSheen) * hip;
)"), CMOT_Float3, { TEXT("UV"), TEXT("Live"), TEXT("WP"), TEXT("Cam"), TEXT("N"), TEXT("T1"), TEXT("T2"), TEXT("PupilK"), TEXT("PupilR"), TEXT("PupilColor"), TEXT("DirtTex"), TEXT("Dirt") },
				{ { TEXT("Dot"), CMOT_Float1 }, { TEXT("Spec"), CMOT_Float1 }, { TEXT("Rough"), CMOT_Float1 }, { TEXT("Base"), CMOT_Float3 } });
			UMaterialExpression* LiveAmount = B.Scalar(TEXT("Live"), 0.f);
			B.Link(UV, CoatC, TEXT("UV"));
			B.Link(LiveAmount, CoatC, TEXT("Live"));
			B.Link(B.TexObject(TEXT("DirtTex"), DirtOrBlack, SAMPLERTYPE_Color), CoatC, TEXT("DirtTex"));
			B.Link(DirtAmount, CoatC, TEXT("Dirt"));
			{
				auto LocalAxis = [&B](float X, float Y, float Z)
				{
					UMaterialExpressionConstant3Vector* V = B.Make<UMaterialExpressionConstant3Vector>();
					V->Constant = FLinearColor(X, Y, Z);
					UMaterialExpressionTransform* T = B.Make<UMaterialExpressionTransform>();
					T->TransformSourceType = TRANSFORMSOURCE_Local;
					T->TransformType = TRANSFORM_World;
					B.Link(V, T);
					return T;
				};
				B.Link(B.Make<UMaterialExpressionWorldPosition>(), CoatC, TEXT("WP"));
				B.Link(B.Make<UMaterialExpressionCameraPositionWS>(), CoatC, TEXT("Cam"));
				B.Link(LocalAxis(0.f, 0.f, 1.f), CoatC, TEXT("N"));
				B.Link(LocalAxis(1.f, 0.f, 0.f), CoatC, TEXT("T1"));
				B.Link(LocalAxis(0.f, 1.f, 0.f), CoatC, TEXT("T2"));
				B.Link(B.Scalar(TEXT("PupilK"), 2.f), CoatC, TEXT("PupilK"));
				B.Link(B.Scalar(TEXT("PupilR"), 0.13f), CoatC, TEXT("PupilR"));
				B.Link(B.Vec(TEXT("PupilColor"), FLinearColor(0.42f, 0.46f, 0.5f)), CoatC, TEXT("PupilColor"));
			}
			UMaterialExpression* Coat = B.ExposureCompensated(CoatC, 0.26f, TEXT("return"));
			B.Out(CoatC, MP_BaseColor, TEXT("Base"));
			B.Out(CoatC, MP_Specular, TEXT("Spec"));
			B.Out(CoatC, MP_Roughness, TEXT("Rough"));
			B.Out(B.Const(0.f), MP_Metallic);
			UMaterialExpression* DotGlow = B.ExposureCompensated(B.MulC(B.Vec(TEXT("DotColor"), FLinearColor(1.f, 0.05f, 0.02f)), 6.f), 0.26f);
			UMaterialExpression* DotOut = B.Make<UMaterialExpressionMultiply>();
			B.Link(DotGlow, DotOut, TEXT("A"));
			B.Link(CoatC, DotOut, TEXT("B"), TEXT("Dot"));
			B.Out(B.Add(B.Add(Coat, B.Mul(Live, LiveAmount)), DotOut), MP_EmissiveColor);
			B.Out(LiveC, MP_OpacityMask, TEXT("Mask"));
			FinishMaterial(B.M);
		}

		// Magnified optic sight picture, laid into the eyepiece's circle on screen after the temporal upscaler: one
		// capture texel per display pixel, sampled bicubic, with no jitter or history of the main view's on it. Before
		// bloom and the tone curve, so it is graded exactly like the view around it.
		// Center (viewport UV) and Radius (fraction of the view height) are the eyepiece's circle, Basis (cosine, sine of
		// the optic's roll) turns screen axes onto the capture's, Gain takes the capture's exposure out.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_ScopeView")) };
			B.M->MaterialDomain = MD_PostProcess;
			B.M->BlendableLocation = BL_SceneColorBeforeBloom;
			UMaterialExpressionSceneTexture* Scene = B.Make<UMaterialExpressionSceneTexture>();
			Scene->SceneTextureId = PPI_PostProcessInput0;
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float2 vuv = GetViewportUV(Parameters);
// Values here are scene-referred: the output is pre-exposed again on the way out.
float3 base = SceneTextureLookup(ViewportUVToSceneTextureUV(vuv, 14), 14, false).rgb * View.OneOverPreExposure + Dummy.rgb * 0.0;
float2 q = (vuv - Center.xy) * float2(Aspect, 1.0) / max(Radius, 1e-4);
float r = length(q);
float aa = max(fwidth(r), 1e-4);
float inside = (1.0 - smoothstep(1.0 - 1.5 * aa, 1.0, r)) * saturate(Live);
if (inside <= 0.0)
{
	return base;
}
float2 c = float2(dot(q, Basis.xy), dot(q, float2(-Basis.y, Basis.x))) * 0.5;
float2 uv = c + 0.5;
float w, h;
ViewTex.GetDimensions(w, h);
float2 texel = float2(w, h);
float fp = max(length(ddx(uv) * texel), length(ddy(uv) * texel));
float3 pic;
if (fp > 1.5)
{
	pic = Texture2DSampleLevel(ViewTex, ViewTexSampler, uv, log2(fp)).rgb;
}
else
{
	// Catmull-Rom in five bilinear taps: sharp at one texel per pixel, no ringing worth the name.
	float2 sp = uv * texel;
	float2 tc = floor(sp - 0.5) + 0.5;
	float2 f = sp - tc;
	float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);
	float2 w12 = w1 + w2;
	float2 t0 = (tc - 1.0) / texel;
	float2 t3 = (tc + 2.0) / texel;
	float2 t12 = (tc + w2 / w12) / texel;
	pic = Texture2DSampleLevel(ViewTex, ViewTexSampler, float2(t12.x, t0.y), 0).rgb * (w12.x * w0.y)
		+ Texture2DSampleLevel(ViewTex, ViewTexSampler, float2(t0.x, t12.y), 0).rgb * (w0.x * w12.y)
		+ Texture2DSampleLevel(ViewTex, ViewTexSampler, float2(t12.x, t12.y), 0).rgb * (w12.x * w12.y)
		+ Texture2DSampleLevel(ViewTex, ViewTexSampler, float2(t3.x, t12.y), 0).rgb * (w3.x * w12.y)
		+ Texture2DSampleLevel(ViewTex, ViewTexSampler, float2(t12.x, t3.y), 0).rgb * (w12.x * w3.y);
	pic /= (w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y);
}
pic = clamp(pic, 0.0, 32768.0);
float gain = Gain;
// A narrow falloff at the field stop and the exit pupil's shadow coming in from the side the eye is off the axis:
// the middle three quarters of the field stay fully bright.
float rim = 1.0 - smoothstep(0.84, 1.0, r) * 0.75;
float pupil = 1.0 - smoothstep(0.38, 0.52, length(q * 0.5 - Shadow.xy)) * 0.5;
float3 s = pic * gain * rim * pupil;
// A clean ocular: a faint veil where the glass is smeared and the odd wipe mark flaring on bright parts of the picture.
float3 grime = Texture2DSampleLevel(DirtTex, DirtTexSampler, uv * 0.9 + 0.05, 2.0).rgb * Dirt * float3(0.18, 0.0, 0.5);
if (grime.r + grime.b > 0.002)
{
	float3 veil = 0.0;
	for (int k = 0; k < 6; ++k)
	{
		float a = k * 1.0472 + 0.4;
		veil += Texture2DSampleLevel(ViewTex, ViewTexSampler, saturate(uv + float2(cos(a), sin(a)) * 0.09), 3.0).rgb;
	}
	veil = clamp(veil, 0.0, 32768.0) / 6.0 * gain * rim;
	s = lerp(s, s * 0.72 + veil * 0.42, saturate(grime.r * 0.65));
	s += veil * grime.b * 0.2;
}
// Duplex reticle in the optic's own axes (it rolls with the scope): fine crosshair, heavy posts left, right and
// below, and a small illuminated centre dot.
float2 ac = abs(c);
float px = max(fwidth(c.x), 1e-5);
float thin = max(0.0016, px * 0.6);
float thick = 0.011;
float gap = step(0.03, max(ac.x, ac.y));
float lines = max(1.0 - smoothstep(thin - px, thin + px, ac.y), 1.0 - smoothstep(thin - px, thin + px, ac.x)) * gap;
float posts = max((1.0 - smoothstep(thick - px, thick + px, ac.y)) * step(0.2, ac.x), (1.0 - smoothstep(thick - px, thick + px, ac.x)) * step(0.2, c.y));
float ink = saturate(max(lines, posts));
s *= 1.0 - ink * 0.96;
float dotM = 1.0 - smoothstep(0.008 - px, 0.012 + px, length(c));
// The illuminated dot keeps its apparent brightness however the eye has adapted (the pre-exposure follows it).
s = lerp(s, DotColor.rgb * 1.56 * View.OneOverPreExposure, dotM);
return lerp(base, s, inside);
)"), CMOT_Float3, { TEXT("Center"), TEXT("Radius"), TEXT("Aspect"), TEXT("Basis"), TEXT("Live"), TEXT("Gain"), TEXT("Shadow"), TEXT("ViewTex"), TEXT("DirtTex"), TEXT("Dirt"), TEXT("DotColor"), TEXT("Dummy") });
			UTexture* Black = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			UTexture* LensDirt = LoadTex(FXTextureFolder, TEXT("T_FX_LensDirt"));
			B.Link(B.Vec(TEXT("Center"), FLinearColor(0.5f, 0.5f, 0.f)), C, TEXT("Center"));
			B.Link(B.Scalar(TEXT("Radius"), 0.25f), C, TEXT("Radius"));
			B.Link(B.Scalar(TEXT("Aspect"), 1.7778f), C, TEXT("Aspect"));
			B.Link(B.Vec(TEXT("Basis"), FLinearColor(1.f, 0.f, 0.f)), C, TEXT("Basis"));
			B.Link(B.Scalar(TEXT("Live"), 0.f), C, TEXT("Live"));
			B.Link(B.Scalar(TEXT("Gain"), 1.f), C, TEXT("Gain"));
			B.Link(B.Vec(TEXT("Shadow"), FLinearColor(0.f, 0.f, 0.f)), C, TEXT("Shadow"));
			B.Link(B.TexObject(TEXT("View"), Black, SAMPLERTYPE_LinearColor), C, TEXT("ViewTex"));
			B.Link(B.TexObject(TEXT("DirtTex"), LensDirt ? LensDirt : Black, SAMPLERTYPE_Color), C, TEXT("DirtTex"));
			B.Link(B.Scalar(TEXT("Dirt"), 1.f), C, TEXT("Dirt"));
			B.Link(B.Vec(TEXT("DotColor"), FLinearColor(1.f, 0.05f, 0.02f)), C, TEXT("DotColor"));
			// The scene texture node is what makes the post-process inputs available to SceneTextureLookup.
			B.Link(Scene, C, TEXT("Dummy"));
			B.Out(C, MP_EmissiveColor);
			FinishMaterial(B.M);
		}

		// Soft camera-facing sprite (dust, blood mist, muzzle smoke), lit by the scene. Per-instance data: 0 opacity, 1-3 colour.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Smoke")) };
			B.M->SetShadingModel(MSM_DefaultLit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TranslucencyLightingMode = TLM_VolumetricNonDirectional;
			B.M->TwoSided = true;
			UMaterialExpression* Tex = B.TexSample(TEXT("SpriteTex"), Smoke, SAMPLERTYPE_Color, nullptr);
			UMaterialExpressionDepthFade* Fade = B.Make<UMaterialExpressionDepthFade>();
			Fade->FadeDistanceDefault = 30.f;
			// Sprites thin out as the eye moves into them, so standing in a cloud is a haze, not a flat card on the lens.
			// Opacity above 1 saturates the soft edge into a dense billow (blast palls); the darker core reads as self-shadowing.
			UMaterialExpressionCustom* C = B.Custom(TEXT("float size = length(Axis) * 50.0; float nearFade = saturate((Depth - 20.0) / max(size * 0.8, 60.0)); float a = saturate(A * Op); Opac = a * Fade * nearFade; float shade = lerp(1.0, 0.72, saturate(A * Op - 0.6) * saturate(Op - 1.0)); return Col * (0.75 + 0.25 * A) * shade;"), CMOT_Float3,
				{ TEXT("A"), TEXT("Op"), TEXT("Fade"), TEXT("Col"), TEXT("Depth"), TEXT("Axis") }, { { TEXT("Opac"), CMOT_Float1 } });
			B.Link(Tex, C, TEXT("A"), TEXT("A"));
			B.Link(B.CustomData(0, 0.5f, false), C, TEXT("Op"));
			B.Link(Fade, C, TEXT("Fade"));
			B.Link(B.Make<UMaterialExpressionPixelDepth>(), C, TEXT("Depth"));
			{
				// The sprite's own size: its plane's X axis carried into world space by the instance scale.
				UMaterialExpressionConstant3Vector* X = B.Make<UMaterialExpressionConstant3Vector>();
				X->Constant = FLinearColor(1.f, 0.f, 0.f);
				UMaterialExpressionTransform* T = B.Make<UMaterialExpressionTransform>();
				T->TransformSourceType = TRANSFORMSOURCE_Local;
				T->TransformType = TRANSFORM_World;
				B.Link(X, T);
				B.Link(T, C, TEXT("Axis"));
			}
			B.Link(B.CustomData(1, 0.4f, true), C, TEXT("Col"));
			B.Out(C, MP_BaseColor, TEXT("return"));
			B.Out(B.Const(0.9f), MP_Roughness);
			B.Out(C, MP_Opacity, TEXT("Opac"));
			FinishMaterial(B.M);
		}

		// Fireball sprite: additive flame billows, per-instance intensity (0) and blackbody colour (1-3), soft where
		// they meet geometry.
		if (UTexture* Flame = LoadTex(FXTextureFolder, TEXT("T_FX_Flame")))
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Fire")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Additive;
			B.M->TwoSided = true;
			UMaterialExpression* Tex = B.TexSample(TEXT("SpriteTex"), Flame, SAMPLERTYPE_Color, nullptr);
			UMaterialExpressionDepthFade* Fade = B.Make<UMaterialExpressionDepthFade>();
			Fade->FadeDistanceDefault = 40.f;
			UMaterialExpressionCustom* C = B.Custom(TEXT("float a = A * A * Fade; return Col * I * a * lerp(0.7, 1.5, A);"), CMOT_Float3,
				{ TEXT("A"), TEXT("I"), TEXT("Col"), TEXT("Fade") });
			B.Link(Tex, C, TEXT("A"), TEXT("A"));
			B.Link(B.CustomData(0, 1.f, false), C, TEXT("I"));
			B.Link(B.CustomData(1, 1.f, true), C, TEXT("Col"));
			B.Link(Fade, C, TEXT("Fade"));
			B.Out(B.ExposureCompensated(C, 0.3f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		// Blast scorch decal.
		if (UTexture* Scorch = LoadTex(FXTextureFolder, TEXT("T_FX_Scorch")))
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Scorch")) };
			B.M->MaterialDomain = MD_DeferredDecal;
			B.M->BlendMode = BLEND_Translucent;
			UMaterialExpression* Tex = B.TexSample(TEXT("ScorchTex"), Scorch, SAMPLERTYPE_Color, nullptr);
			B.Out(Tex, MP_BaseColor);
			B.Out(B.Const(0.96f), MP_Roughness);
			UMaterialExpressionCustom* Op = B.Custom(TEXT("return saturate(A * O);"), CMOT_Float1, { TEXT("A"), TEXT("O") });
			B.Link(Tex, Op, TEXT("A"), TEXT("A"));
			B.Link(B.Scalar(TEXT("Opacity"), 1.f), Op, TEXT("O"));
			B.Out(Op, MP_Opacity);
			FinishMaterial(B.M);
		}

		// Cracks round a bullet hole in a pane: lit, translucent white fracture lines, the hole itself dark.
		if (UTexture* Crack = LoadTex(FXTextureFolder, TEXT("T_FX_GlassCrack")))
		{
			FMat B{ NewMaterial(TEXT("M_Campus_GlassCrack")) };
			B.M->SetShadingModel(MSM_DefaultLit);
			B.M->BlendMode = BLEND_Translucent;
			B.M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
			B.M->TwoSided = true;
			UMaterialExpression* Tex = B.TexSample(TEXT("CrackTex"), Crack, SAMPLERTYPE_Color, nullptr);
			UMaterialExpressionCustom* C = B.Custom(TEXT("float r = length(UV - 0.5) * 2.0; float hole = saturate((0.045 - r) / 0.02); Opac = saturate(A * 0.9 + hole * 0.92); return lerp(float3(0.78, 0.8, 0.8), float3(0.01, 0.01, 0.012), hole);"), CMOT_Float3,
				{ TEXT("A"), TEXT("UV") }, { { TEXT("Opac"), CMOT_Float1 } });
			B.Link(Tex, C, TEXT("A"), TEXT("A"));
			B.Link(B.Make<UMaterialExpressionTextureCoordinate>(), C, TEXT("UV"));
			B.Out(C, MP_BaseColor, TEXT("return"));
			B.Out(B.Const(0.22f), MP_Roughness);
			B.Out(B.Const(0.6f), MP_Specular);
			B.Out(C, MP_Opacity, TEXT("Opac"));
			FinishMaterial(B.M);
		}

		// Muzzle flash petals: additive, textured.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Flash")) };
			B.M->SetShadingModel(MSM_Unlit);
			B.M->BlendMode = BLEND_Additive;
			B.M->TwoSided = true;
			UMaterialExpression* Tex = B.TexSample(TEXT("FlashTex"), Flash, SAMPLERTYPE_Color, nullptr);
			UMaterialExpressionCustom* C = B.Custom(TEXT("float a = A * A; return Col * Intensity * a * lerp(float3(1.0, 0.92, 0.75), float3(1.0, 1.0, 1.0), a);"), CMOT_Float3,
				{ TEXT("A"), TEXT("Col"), TEXT("Intensity") });
			B.Link(Tex, C, TEXT("A"), TEXT("A"));
			B.Link(B.Vec(TEXT("Color"), FLinearColor(1.f, 0.6f, 0.25f)), C, TEXT("Col"));
			B.Link(B.Scalar(TEXT("Intensity"), 30.f), C, TEXT("Intensity"));
			B.Out(B.ExposureCompensated(C, 0.3f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}

		// Bullet hole decal (replaces the flat disc).
		if (Hole)
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Decal")) };
			B.M->MaterialDomain = MD_DeferredDecal;
			B.M->BlendMode = BLEND_Translucent;
			UMaterialExpression* Tex = B.TexSample(TEXT("HoleTex"), Hole, SAMPLERTYPE_Color, nullptr);
			B.Out(B.Mul(Tex, B.Vec(TEXT("Color"), FLinearColor(1.f, 1.f, 1.f))), MP_BaseColor);
			B.Out(B.Const(0.92f), MP_Roughness);
			UMaterialExpressionCustom* Op = B.Custom(TEXT("return A * O;"), CMOT_Float1, { TEXT("A"), TEXT("O") });
			B.Link(Tex, Op, TEXT("A"), TEXT("A"));
			B.Link(B.Scalar(TEXT("Opacity"), 0.95f), Op, TEXT("O"));
			B.Out(Op, MP_Opacity);
			FinishMaterial(B.M);
		}

		// Blood spatter thrown onto the wall or floor behind a hit: an irregular wet core stretched along the throw,
		// satellite droplets and thin trailing spines, darkening toward the edges as it soaks in.
		if (UTexture* G = LoadTex(FXTextureFolder, TEXT("T_Campus_Grunge")))
		{
			FMat B{ NewMaterial(TEXT("M_Campus_BloodDecal")) };
			B.M->MaterialDomain = MD_DeferredDecal;
			B.M->BlendMode = BLEND_Translucent;
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
float2 c = UV - 0.5;
float2 sc = float2(Seed * 0.37, Seed * 0.71);
float n1 = Texture2DSample(Tex, TexSampler, UV * 0.9 + sc).g;
float n2 = Texture2DSample(Tex, TexSampler, UV * 3.1 + sc.yx).b;
float n3 = Texture2DSample(Tex, TexSampler, UV * 7.3 + sc * 1.7).r;
float2 s = float2(c.x * lerp(1.0, 0.55, Stretch), c.y);
float r = length(s);
float core = 1.0 - smoothstep(0.17 + n1 * 0.1, 0.21 + n1 * 0.12, r);
float ang = atan2(c.y, c.x);
float spines = pow(saturate(Texture2DSample(Tex, TexSampler, float2(ang * 0.6 + Seed, 0.37)).g * 1.6 - 0.55), 2.0) * (1.0 - smoothstep(0.16, 0.46, r)) * smoothstep(0.1, 0.18, r);
float drops = step(0.74, n2) * step(0.45, n3) * (1.0 - smoothstep(0.3, 0.48, r));
float a = saturate(core + spines * 2.5 + drops);
float edge = saturate(1.0 - core) * a;
Rough = lerp(0.22, 0.55, edge + n3 * 0.3);
Opac = a * Opacity;
return lerp(float3(0.10, 0.006, 0.005), float3(0.045, 0.004, 0.003), edge * 0.7 + n1 * 0.3);
)"), CMOT_Float3, { TEXT("UV"), TEXT("Tex"), TEXT("Seed"), TEXT("Stretch"), TEXT("Opacity") }, { { TEXT("Rough"), CMOT_Float1 }, { TEXT("Opac"), CMOT_Float1 } });
			B.Link(B.Make<UMaterialExpressionTextureCoordinate>(), C, TEXT("UV"));
			B.Link(B.TexObject(TEXT("Tex"), G, SAMPLERTYPE_LinearColor), C, TEXT("Tex"));
			B.Link(B.Scalar(TEXT("Seed"), 0.f), C, TEXT("Seed"));
			B.Link(B.Scalar(TEXT("Stretch"), 0.f), C, TEXT("Stretch"));
			B.Link(B.Scalar(TEXT("Opacity"), 0.95f), C, TEXT("Opacity"));
			B.Out(C, MP_BaseColor, TEXT("return"));
			B.Out(C, MP_Roughness, TEXT("Rough"));
			B.Out(C, MP_Opacity, TEXT("Opac"));
			FinishMaterial(B.M);
		}

		// Signs and posters: texture drawn at runtime into a render target (Chinese text via the UI fonts).
		// UVs come from the cube's local position so the text reads correctly on the +X face (and unmirrored on the back).
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Sign")) };
			UTexture* White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			UMaterialExpressionTransform* LocalN = B.Make<UMaterialExpressionTransform>();
			LocalN->TransformSourceType = TRANSFORMSOURCE_World;
			LocalN->TransformType = TRANSFORM_Local;
			B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), LocalN);
			UMaterialExpressionCustom* FaceUV = B.Custom(TEXT(R"(
bool front = abs(N.x) >= max(abs(N.y), abs(N.z));
float2 uv = float2(N.x >= 0.0 ? (50.0 - P.y) / 100.0 : (P.y + 50.0) / 100.0, (50.0 - P.z) / 100.0);
return front ? saturate(uv) : float2(0.02, 0.02);
)"), CMOT_Float2, { TEXT("P"), TEXT("N") });
			B.Link(B.Make<UMaterialExpressionLocalPosition>(), FaceUV, TEXT("P"));
			B.Link(LocalN, FaceUV, TEXT("N"));
			UMaterialExpression* Tex = B.TexSample(TEXT("SignTex"), White, SAMPLERTYPE_Color, FaceUV);
			UMaterialExpression* Grunge = nullptr;
			if (UTexture* G = LoadTex(FXTextureFolder, TEXT("T_Campus_Grunge")))
			{
				UMaterialExpressionTextureCoordinate* UV = B.Make<UMaterialExpressionTextureCoordinate>();
				UV->UTiling = 1.7f;
				UV->VTiling = 0.9f;
				Grunge = B.TexSample(TEXT("GrungeTex"), G, SAMPLERTYPE_LinearColor, UV);
			}
			UMaterialExpressionCustom* C = B.Custom(TEXT("float d = saturate((G.g - 0.45) * 2.0) * 0.35 + (G.r - 0.5) * 0.15; Rough = saturate(R + d * 0.3); return T * (1.0 - d) * Tint;"), CMOT_Float3,
				{ TEXT("T"), TEXT("G"), TEXT("R"), TEXT("Tint") }, { { TEXT("Rough"), CMOT_Float1 } });
			B.Link(Tex, C, TEXT("T"));
			if (Grunge)
			{
				B.Link(Grunge, C, TEXT("G"));
			}
			else
			{
				B.Link(B.Const(0.5f), C, TEXT("G"));
			}
			B.Link(B.Scalar(TEXT("Roughness"), 0.55f), C, TEXT("R"));
			B.Link(B.Vec(TEXT("Tint"), FLinearColor::White), C, TEXT("Tint"));
			B.Out(C, MP_BaseColor, TEXT("return"));
			B.Out(C, MP_Roughness, TEXT("Rough"));
			// Emissive in units of 150 nits (exit signs ~200 nits in their green).
			B.Out(B.MulC(B.Mul(Tex, B.Scalar(TEXT("Emissive"), 0.f)), 150.f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}

		// LCD panel (the 特勤处 video wall, consoles, the briefing table): the box's +X face shows UVMin..UVMax of the Screen
		// picture (several panels share one picture), behind anti-glare glass that still catches the hangar lights; the
		// other faces are the black bezel.
		{
			FMat B{ NewMaterial(TEXT("M_Campus_Display")) };
			UTexture* Black = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			UMaterialExpressionTransform* LocalN = B.Make<UMaterialExpressionTransform>();
			LocalN->TransformSourceType = TRANSFORMSOURCE_World;
			LocalN->TransformType = TRANSFORM_Local;
			B.Link(B.Make<UMaterialExpressionVertexNormalWS>(), LocalN);
			UMaterialExpressionCustom* FaceUV = B.Custom(TEXT(R"(
Front = (N.x > 0.0 && N.x >= max(abs(N.y), abs(N.z))) ? 1.0 : 0.0;
float2 uv = saturate(float2((50.0 - P.y) / 100.0, (50.0 - P.z) / 100.0));
return lerp(UVMin.xy, UVMax.xy, uv);
)"), CMOT_Float2, { TEXT("P"), TEXT("N"), TEXT("UVMin"), TEXT("UVMax") }, { { TEXT("Front"), CMOT_Float1 } });
			B.Link(B.Make<UMaterialExpressionLocalPosition>(), FaceUV, TEXT("P"));
			B.Link(LocalN, FaceUV, TEXT("N"));
			B.Link(B.Vec(TEXT("UVMin"), FLinearColor(0.f, 0.f, 0.f)), FaceUV, TEXT("UVMin"));
			B.Link(B.Vec(TEXT("UVMax"), FLinearColor(1.f, 1.f, 0.f)), FaceUV, TEXT("UVMax"));
			UMaterialExpression* Tex = B.TexSample(TEXT("Screen"), Black, SAMPLERTYPE_Color, FaceUV);
			UMaterialExpressionCustom* C = B.Custom(TEXT(R"(
Rough = lerp(0.5, 0.2, Front);
Base = lerp(0.025, 0.004, Front);
return T * Front * Emissive;
)"), CMOT_Float3, { TEXT("T"), TEXT("Front"), TEXT("Emissive") }, { { TEXT("Rough"), CMOT_Float1 }, { TEXT("Base"), CMOT_Float1 } });
			B.Link(Tex, C, TEXT("T"));
			B.Link(FaceUV, C, TEXT("Front"), TEXT("Front"));
			B.Link(B.Scalar(TEXT("Emissive"), 1.6f), C, TEXT("Emissive"));
			B.Out(C, MP_BaseColor, TEXT("Base"));
			B.Out(C, MP_Roughness, TEXT("Rough"));
			B.Out(B.Const(0.5f), MP_Specular);
			// A monitor reads at the same brightness to an eye adapted to a dark hangar or to daylight: in absolute
			// nits the wall burnt out to white at night.
			B.Out(B.ExposureCompensated(C, 0.5f), MP_EmissiveColor);
			FinishMaterial(B.M);
		}
		UE_LOG(LogCampusBootstrap, Display, TEXT("UI / FX materials built"));
	}
}
