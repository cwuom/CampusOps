#include "UI/CampusUIStyle.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/StrongObjectPtr.h"

namespace CampusUI
{
	namespace
	{
		const TCHAR* FontPaths[(int32)EFont::Count] = {
			TEXT("/Game/Campus/UI/Fonts/F_Campus_Sans.F_Campus_Sans"),
			TEXT("/Game/Campus/UI/Fonts/F_Campus_Cond.F_Campus_Cond"),
			TEXT("/Game/Campus/UI/Fonts/F_Campus_Num.F_Campus_Num"),
			TEXT("/Game/Campus/UI/Fonts/F_Campus_Title.F_Campus_Title"),
			TEXT("/Game/Campus/UI/Fonts/F_Campus_Stencil.F_Campus_Stencil") };

		const TCHAR* TextureNames[(int32)ETex::Count] = {
			TEXT("T_UI_White"), TEXT("T_UI_GradL"), TEXT("T_UI_GradR"), TEXT("T_UI_GradUp"), TEXT("T_UI_GradDown"), TEXT("T_UI_Band"),
			TEXT("T_UI_Soft"), TEXT("T_UI_Disc"), TEXT("T_UI_Ring"), TEXT("T_UI_Line"), TEXT("T_UI_Vignette"), TEXT("T_UI_Blood"),
			TEXT("T_UI_Scope"), TEXT("T_UI_Stripes"), TEXT("T_UI_Splat"), TEXT("T_UI_Skull"), TEXT("T_UI_Eyebox"),
			TEXT("T_Menu_Campus"), TEXT("T_Menu_Hotel"), TEXT("T_Menu_Gunfight"),
			TEXT("T_UI_MedalRing"), TEXT("T_UI_MedalChevron"), TEXT("T_UI_MedalHeadshot"), TEXT("T_UI_MedalDoubleKill"), TEXT("T_UI_MedalTripleKill"),
			TEXT("T_UI_MedalFirstBlood"), TEXT("T_UI_MedalLongshot"), TEXT("T_UI_MedalRevenge"), TEXT("T_UI_MedalStreak5"), TEXT("T_UI_MedalMelee") };

		struct FFontSlot
		{
			TStrongObjectPtr<UFont> Font;
			bool bTried = false;
		};
		FFontSlot Fonts[(int32)EFont::Count];

		struct FTexSlot
		{
			TStrongObjectPtr<UTexture2D> Texture;
			FSlateBrush Brush;
			bool bTried = false;
		};
		FTexSlot Textures[(int32)ETex::Count];

		FTexSlot& TexSlot(ETex Id)
		{
			FTexSlot& Slot = Textures[FMath::Clamp((int32)Id, 0, (int32)ETex::Count - 1)];
			if (!Slot.bTried)
			{
				Slot.bTried = true;
				const FString Path = FString::Printf(TEXT("/Game/Campus/UI/Textures/%s.%s"), TextureNames[(int32)Id], TextureNames[(int32)Id]);
				if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					Slot.Texture.Reset(Tex);
					Slot.Brush.SetResourceObject(Tex);
					Slot.Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
					Slot.Brush.DrawAs = ESlateBrushDrawType::Image;
					Slot.Brush.Tiling = ESlateBrushTileType::NoTile;
				}
			}
			return Slot;
		}
	}

	const UFont* FontAsset(EFont Family)
	{
		FFontSlot& Slot = Fonts[FMath::Clamp((int32)Family, 0, (int32)EFont::Count - 1)];
		if (!Slot.bTried)
		{
			Slot.bTried = true;
			Slot.Font.Reset(LoadObject<UFont>(nullptr, FontPaths[(int32)Family], nullptr, LOAD_NoWarn | LOAD_Quiet));
		}
		return Slot.Font.Get();
	}

	FSlateFontInfo Font(EFont Family, float Size, FName Weight, int32 Tracking)
	{
		FSlateFontInfo Info;
		if (const UFont* F = FontAsset(Family))
		{
			Info = FSlateFontInfo(F, Size, Weight);
		}
		else
		{
			const bool bBold = Weight != TEXT("Regular") && Weight != TEXT("Medium");
			Info = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::RoundToInt(Size));
		}
		Info.LetterSpacing = Tracking;
		return Info;
	}

	UTexture2D* Texture(ETex Id)
	{
		return TexSlot(Id).Texture.Get();
	}

	const FSlateBrush* Brush(ETex Id)
	{
		FTexSlot& Slot = TexSlot(Id);
		return Slot.Texture.IsValid() ? &Slot.Brush : White();
	}

	const FSlateBrush* White()
	{
		return FCoreStyle::Get().GetBrush("GenericWhiteBox");
	}

	const FSlateBrush* Rounded()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor::White, 3.f);
		return &B;
	}

	const FSlateBrush* Panel()
	{
		static const FSlateRoundedBoxBrush B(PanelColor(), 4.f, Hairline(), 1.f);
		return &B;
	}

	const FSlateBrush* Outline()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, 3.f, FLinearColor::White, 1.f);
		return &B;
	}

	const FSlateBrush* Pill()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor::White, 10.f);
		return &B;
	}

	const FSlateBrush* KeyCap()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor(1.f, 1.f, 1.f, 0.06f), 3.f, FLinearColor(1.f, 1.f, 1.f, 0.45f), 1.f);
		return &B;
	}
}
