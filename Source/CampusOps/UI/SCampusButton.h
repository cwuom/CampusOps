#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SButton;

enum class ECampusButtonStyle : uint8
{
	/** list row: dark emphasis on hover, light text and an accent bar when selected */
	Row,
	/** large tile with a quiet caption and a compact tag to keep attention on the title */
	Card,
	Playlist,
	/** Top navigation tab: text with an accent underline. */
	Tab,
	/** Compact segment inside a segmented control. */
	Segment
};

class CAMPUSOPS_API SCampusButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusButton)
		: _FontSize(18)
		, _Height(46.f)
		, _IsSelected(false)
		, _Style(ECampusButtonStyle::Row)
	{}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(FText, SubText)
		SLATE_ATTRIBUTE(FText, Eyebrow)
		SLATE_ATTRIBUTE(FText, Tag)
		SLATE_ATTRIBUTE(const FSlateBrush*, Image)
		SLATE_ARGUMENT(int32, FontSize)
		SLATE_ARGUMENT(float, Height)
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_ARGUMENT(ECampusButtonStyle, Style)
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, SoundContext)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_EVENT(FSimpleDelegate, OnHovered)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	friend class FCampusUIHoverContrastTest;
	TSharedRef<SWidget> BuildRow(const FArguments& InArgs);
	TSharedRef<SWidget> BuildCard(const FArguments& InArgs);
	TSharedRef<SWidget> BuildPlaylist(const FArguments& InArgs);
	TSharedRef<SWidget> BuildTab(const FArguments& InArgs);
	TSharedRef<SWidget> BuildSegment(const FArguments& InArgs);
	TSharedRef<SWidget> HoverFill() const;

	FReply HandleClicked();
	void HandleHovered();
	bool IsActive() const;
	TAttribute<bool> IsSelected;
	FSimpleDelegate OnClicked;
	FSimpleDelegate OnHoveredDelegate;
	TWeakObjectPtr<UObject> SoundContext;
	TSharedPtr<SButton> Button;
	ECampusButtonStyle Style = ECampusButtonStyle::Row;
	float Highlight = 0.f;
	float Selected = 0.f;
	float Press = 0.f;
};
