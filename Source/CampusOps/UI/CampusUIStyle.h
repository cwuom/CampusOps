#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"

class UFont;
class UTexture2D;

namespace CampusUI
{
	/**
	 * Font families built by the bootstrap from SourceArt/Fonts (SIL OFL):
	 * Sans = Barlow, Cond = Barlow Condensed, Num = Rajdhani (all with a Noto Sans SC sub-font for Chinese),
	 * Title = Noto Serif TC Black, Stencil = Black Ops One. Weights: Regular, Medium, SemiBold, Bold, Black.
	 */
	enum class EFont : uint8 { Sans, Cond, Num, Title, Stencil, Count };

	/** Tracking is in 1/1000 em. Falls back to the engine font when the bootstrap has not been run. */
	CAMPUSOPS_API FSlateFontInfo Font(EFont Family, float Size, FName Weight = TEXT("Bold"), int32 Tracking = 0);
	inline FSlateFontInfo Font(int32 Size, const TCHAR* Weight = TEXT("Bold")) { return Font(EFont::Sans, (float)Size, FName(Weight)); }
	CAMPUSOPS_API const UFont* FontAsset(EFont Family);

	enum class ETex : uint8
	{
		White, GradL, GradR, GradUp, GradDown, Band, Soft, Disc, Ring, Line, Vignette, Blood, Scope, Stripes, Splat, Skull, Eyebox,
		MenuCampus, MenuHotel, MenuGunfight,
		MedalRing, MedalChevron, MedalHeadshot, MedalDoubleKill, MedalTripleKill, MedalFirstBlood, MedalLongshot, MedalRevenge, MedalStreak5, MedalMelee,
		Count
	};
	CAMPUSOPS_API UTexture2D* Texture(ETex Id);
	/** Image brush for a UI texture, tinted by the widget colour. */
	CAMPUSOPS_API const FSlateBrush* Brush(ETex Id);

	CAMPUSOPS_API const FSlateBrush* White();
	/** 3px rounded fill, tinted by the widget colour. */
	CAMPUSOPS_API const FSlateBrush* Rounded();
	/** Dark glass panel with a hairline outline. */
	CAMPUSOPS_API const FSlateBrush* Panel();
	/** Hairline outline only. */
	CAMPUSOPS_API const FSlateBrush* Outline();
	/** Fully rounded tag. */
	CAMPUSOPS_API const FSlateBrush* Pill();
	CAMPUSOPS_API const FSlateBrush* KeyCap();

	inline const FLinearColor& Accent() { static const FLinearColor C(0.80f, 1.f, 0.16f, 1.f); return C; }
	inline const FLinearColor& TextMain() { static const FLinearColor C(0.96f, 0.965f, 0.95f, 1.f); return C; }
	inline const FLinearColor& TextDim() { static const FLinearColor C(0.60f, 0.63f, 0.62f, 1.f); return C; }
	inline const FLinearColor& TextFaint() { static const FLinearColor C(0.42f, 0.45f, 0.45f, 1.f); return C; }
	/** Text on the white hover fill. */
	inline const FLinearColor& Ink() { static const FLinearColor C(0.025f, 0.028f, 0.03f, 1.f); return C; }
	inline const FLinearColor& InkDim() { static const FLinearColor C(0.22f, 0.23f, 0.24f, 1.f); return C; }
	inline const FLinearColor& Fill() { static const FLinearColor C(0.93f, 0.94f, 0.93f, 1.f); return C; }
	inline const FLinearColor& PanelColor() { static const FLinearColor C(0.008f, 0.01f, 0.012f, 0.72f); return C; }
	inline const FLinearColor& PanelLight() { static const FLinearColor C(1.f, 1.f, 1.f, 0.05f); return C; }
	inline const FLinearColor& Hairline() { static const FLinearColor C(1.f, 1.f, 1.f, 0.10f); return C; }
	inline const FLinearColor& Positive() { static const FLinearColor C(0.42f, 0.95f, 0.45f, 1.f); return C; }
	inline const FLinearColor& Negative() { static const FLinearColor C(1.f, 0.34f, 0.28f, 1.f); return C; }
	inline const FLinearColor& Warning() { static const FLinearColor C(1.f, 0.72f, 0.18f, 1.f); return C; }
	inline const FLinearColor& Friendly() { static const FLinearColor C(0.22f, 0.64f, 1.f, 1.f); return C; }
	inline const FLinearColor& Enemy() { static const FLinearColor C(1.f, 0.24f, 0.17f, 1.f); return C; }
	inline FLinearColor Alpha(FLinearColor C, float A) { C.A *= A; return C; }

	CAMPUSOPS_API void PlaySound(const TWeakObjectPtr<UObject>& Context, FName Id, float Volume = 0.6f);
}
