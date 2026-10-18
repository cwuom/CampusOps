#pragma once

#include "CoreMinimal.h"
#include "CampusTypes.h"

namespace CampusMenuPresentation
{
	inline constexpr float OuterMargin = 48.f;
	inline constexpr float CatalogWidth = 360.f;
	inline constexpr float AttachmentGap = 14.f;
	inline constexpr float AttachmentWidth = 300.f;
	inline constexpr int32 SlotCount = static_cast<int32>(ECampusAttachmentSlot::Count);

	struct FWeaponLayout
	{
		FBox2D Weapon;
		FBox2D LabelArea;
	};

	struct FAttachmentLabel
	{
		ECampusAttachmentSlot Slot = ECampusAttachmentSlot::Muzzle;
		FVector2D Anchor = FVector2D::ZeroVector;
		FBox2D Bounds;
	};

	inline FWeaponLayout WeaponLayout(const FVector2D& Size, bool bAttachmentPanel, bool bLoadouts = false)
	{
		FWeaponLayout Layout;
		const float Width = static_cast<float>(Size.X);
		const float Height = static_cast<float>(Size.Y);
		const float PanelRight = OuterMargin + CatalogWidth + (bAttachmentPanel ? AttachmentGap + AttachmentWidth : 0.f);
		const float Left = FMath::Min(PanelRight + 24.f, FMath::Max(20.f, Width - 180.f));
		const float Right = Width - (bLoadouts ? OuterMargin + AttachmentWidth + 24.f : 20.f);
		const float Top = 150.f;
		const float Bottom = Height - 120.f;
		Layout.LabelArea = FBox2D(FVector2D(Left, Top), FVector2D(Right, Bottom));
		if (bLoadouts)
		{
			Layout.Weapon = FBox2D(FVector2D(Left + 12.f, Top + 24.f), FVector2D(Right - 12.f, Bottom - 24.f));
			return Layout;
		}
		const float Available = Right - Left;
		const bool bSideLabels = Available >= 900.f;
		const float LabelWidth = FMath::Min(210.f, (Available - 16.f) * 0.5f);
		if (bSideLabels)
		{
			Layout.Weapon = FBox2D(FVector2D(Left + LabelWidth + 24.f, Top + 24.f), FVector2D(Right - LabelWidth - 24.f, Bottom - 24.f));
		}
		else
		{
			constexpr float BandHeight = 92.f;
			Layout.Weapon = FBox2D(FVector2D(Left + 12.f, Top + BandHeight + 24.f), FVector2D(Right - 12.f, Bottom - BandHeight - 24.f));
		}
		return Layout;
	}

	inline float AttachmentLabelVerticalBias(ECampusAttachmentSlot Slot)
	{
		switch (Slot)
		{
		case ECampusAttachmentSlot::Optic: return -1.2f;
		case ECampusAttachmentSlot::Barrel:
		case ECampusAttachmentSlot::Laser: return -1.f;
		case ECampusAttachmentSlot::Muzzle: return -0.3f;
		case ECampusAttachmentSlot::Stock: return -0.4f;
		case ECampusAttachmentSlot::Magazine: return 1.2f;
		case ECampusAttachmentSlot::Underbarrel:
		case ECampusAttachmentSlot::RearGrip: return 1.f;
		default: return 0.f;
		}
	}

	inline void PlaceAttachmentLabels(const FVector2D& Size, bool bAttachmentPanel, const FVector2D& WeaponCenter, TArray<FAttachmentLabel>& Labels)
	{
		const FBox2D Area = WeaponLayout(Size, bAttachmentPanel).LabelArea;
		const FVector2D LabelSize(FMath::Min(210.0, Area.GetSize().X), 40.f);
		constexpr float RowStep = 46.f;
		constexpr float Gap = 6.f;
		const FVector2D IconOffset(13.f, 20.f);
		const bool bSingleColumn = Area.GetSize().X < LabelSize.X * 2.f + Gap;
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			FAttachmentLabel& Label = Labels[Index];
			FVector2D Direction = (Label.Anchor - WeaponCenter).GetSafeNormal();
			Direction.Y += AttachmentLabelVerticalBias(Label.Slot);
			Direction.Normalize();
			FVector2D Preferred = Label.Anchor + Direction * 80.f - IconOffset;
			Preferred.X -= LabelSize.X * 0.55f * FMath::Clamp((-Direction.X - 0.15) / 0.5, 0.0, 1.0);
			FVector2D Best = Area.Min;
			double BestScore = TNumericLimits<double>::Max();
			auto Consider = [&](FVector2D Candidate)
			{
				Candidate.X = FMath::Clamp(Candidate.X, Area.Min.X, Area.Max.X - LabelSize.X);
				Candidate.Y = FMath::Clamp(Candidate.Y, Area.Min.Y, Area.Max.Y - LabelSize.Y);
				const FBox2D Bounds(Candidate, Candidate + LabelSize);
				for (int32 Other = 0; Other < Index; ++Other)
				{
					if (Bounds.ExpandBy(Gap * 0.5f).Intersect(Labels[Other].Bounds)) return;
				}
				const double Score = FVector2D::DistSquared(Candidate, Preferred);
				if (Score < BestScore)
				{
					BestScore = Score;
					Best = Candidate;
				}
			};
			if (!bSingleColumn)
			{
				for (int32 Row = -8; Row <= 8; ++Row)
				{
					for (int32 Column = -2; Column <= 2; ++Column)
					{
						Consider(Preferred + FVector2D(Column * LabelSize.X * 0.5f, Row * RowStep));
					}
				}
			}
			if (BestScore == TNumericLimits<double>::Max())
			{
				// shared rows keep every option clickable when the attachment panel leaves little room
				for (double Y = Area.Min.Y; Y + LabelSize.Y <= Area.Max.Y; Y += RowStep)
				{
					for (double X = Area.Min.X; X + LabelSize.X <= Area.Max.X; X += LabelSize.X + Gap)
					{
						Consider(FVector2D(X, Y));
					}
				}
			}
			Label.Bounds = FBox2D(Best, Best + LabelSize);
		}
	}
}
