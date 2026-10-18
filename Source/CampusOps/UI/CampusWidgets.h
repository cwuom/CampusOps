#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Map/CampusSchoolBuilder.h"

/** Shared building blocks for the menus (header, panels, key hints, segmented choice, map preview). */
namespace CampusWidgets
{
	/** Small letter-spaced caption ("MULTIPLAYER") above a page title. */
	CAMPUSOPS_API TSharedRef<SWidget> Eyebrow(const TAttribute<FText>& Text, const FLinearColor& Color);
	/** Eyebrow + big title + dim subtitle. */
	CAMPUSOPS_API TSharedRef<SWidget> Header(const FText& EyebrowText, const TAttribute<FText>& Title, const TAttribute<FText>& Sub, float TitleSize = 46.f);
	/** Section label with a hairline running to the right edge. */
	CAMPUSOPS_API TSharedRef<SWidget> Section(const FText& Label);
	/** Background-blurred glass panel with hairline outline. */
	CAMPUSOPS_API TSharedRef<SWidget> Glass(TSharedRef<SWidget> Content, const FMargin& Padding = FMargin(24.f, 20.f), float Blur = 14.f);
	/** Keycap + action label, e.g. [ESC] 返回. */
	CAMPUSOPS_API TSharedRef<SWidget> KeyHint(const FText& Key, const FText& Action);
	/** Small rounded tag. */
	CAMPUSOPS_API TSharedRef<SWidget> Chip(const TAttribute<FText>& Text, const FLinearColor& Color, bool bFilled = false);
}

/** Row of mutually exclusive segments (e.g. AI difficulty). */
class CAMPUSOPS_API SCampusSegmented : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusSegmented) {}
		SLATE_ARGUMENT(TArray<FText>, Options)
		SLATE_ATTRIBUTE(int32, Selected)
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, SoundContext)
		SLATE_EVENT(TDelegate<void(int32)>, OnSelect)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<int32> Selected;
	TDelegate<void(int32)> OnSelect;
};

/** Top-down outline of the school drawn from the builder's minimap rectangles. */
class CAMPUSOPS_API SCampusMapPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusMapPreview) : _DesiredSize(FVector2D(400.f, 230.f)) {}
		SLATE_ARGUMENT(FVector2D, DesiredSize)
		SLATE_ARGUMENT(TArray<FCampusMapRect>, Rects)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetRects(const TArray<FCampusMapRect>& InRects);
	virtual FVector2D ComputeDesiredSize(float) const override { return DesiredSize; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FCampusMapRect> Rects;
	FBox2D Bounds = FBox2D(ForceInit);
	FVector2D DesiredSize;
	double StartTime = 0.0;
};
