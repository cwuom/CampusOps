#include "UI/SCampusButton.h"
#include "UI/CampusUIStyle.h"
#include "CampusAssets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"

void CampusUI::PlaySound(const TWeakObjectPtr<UObject>& Context, FName Id, float Volume)
{
	if (UObject* Obj = Context.Get())
	{
		CampusAudio::Play2D(Obj, Id, Volume, 0.02f, CampusAudio::ECategory::Interface);
	}
}

namespace
{
	float EaseOut(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return 1.f - FMath::Pow(1.f - X, 3.f);
	}

	float SmoothState(float Current, float Target, float DeltaTime, float Response)
	{
		const float Next = FMath::Lerp(Current, Target, 1.f - FMath::Exp(-Response * FMath::Max(DeltaTime, 0.f)));
		return FMath::Abs(Next - Target) < 0.001f ? Target : Next;
	}
}

void SCampusButton::Construct(const FArguments& InArgs)
{
	IsSelected = InArgs._IsSelected;
	OnClicked = InArgs._OnClicked;
	OnHoveredDelegate = InArgs._OnHovered;
	SoundContext = InArgs._SoundContext;
	Style = InArgs._Style;

	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	switch (Style)
	{
	case ECampusButtonStyle::Card: Content = BuildCard(InArgs); break;
	case ECampusButtonStyle::Playlist: Content = BuildPlaylist(InArgs); break;
	case ECampusButtonStyle::Tab: Content = BuildTab(InArgs); break;
	case ECampusButtonStyle::Segment: Content = BuildSegment(InArgs); break;
	default: Content = BuildRow(InArgs); break;
	}

	ChildSlot
	[
		SAssignNew(Button, SButton)
		.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
		.ContentPadding(FMargin(0.f))
		.NormalPaddingOverride(FMargin(0.f))
		.PressedPaddingOverride(FMargin(0.f))
		.OnClicked(this, &SCampusButton::HandleClicked)
		.OnHovered(this, &SCampusButton::HandleHovered)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ Content ]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(CampusUI::Rounded())
				.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.f, Press * 0.16f)); })
				.Visibility(EVisibility::HitTestInvisible)
			]
		]
	];
}

TSharedRef<SWidget> SCampusButton::HoverFill() const
{
	return SNew(SBorder)
		.BorderImage(CampusUI::Rounded())
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.02f, 0.07f, 0.085f, 0.95f)))
		.RenderTransformPivot(FVector2D(0.f, 0.5f))
		.RenderTransform_Lambda([this]() { return FSlateRenderTransform(FScale2D(FMath::Max(EaseOut(Highlight), 0.001f), 1.f)); })
		.Visibility_Lambda([this]() { return Highlight > 0.01f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; });
}

TSharedRef<SWidget> SCampusButton::BuildRow(const FArguments& InArgs)
{
	const int32 Size = InArgs._FontSize;
	return SNew(SBox)
		.HeightOverride(InArgs._Height)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(CampusUI::Rounded())
				.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.18f + 0.12f * Selected)); })
			]
			+ SOverlay::Slot()[ HoverFill() ]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.Padding(0.f, 7.f)
			[
				SNew(SBox)
				.WidthOverride(3.f)
				[
					SNew(SBorder)
					.BorderImage(CampusUI::White())
					.BorderBackgroundColor_Lambda([this]() { return FSlateColor(CampusUI::Alpha(CampusUI::Accent(), Selected)); })
				]
			]
			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			.Padding(18.f, 0.f, 14.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._Text)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Font(CampusUI::Font(CampusUI::EFont::Sans, Size, TEXT("Bold")))
					.ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._SubText)
					.Font(CampusUI::Font(CampusUI::EFont::Sans, FMath::Max(11, Size - 5), TEXT("Medium")))
					.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(10.f, 0.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("›")))
					.Font(CampusUI::Font(CampusUI::EFont::Cond, Size + 8, TEXT("Bold")))
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(CampusUI::Alpha(CampusUI::Accent(), Highlight)); })
				]
			]
		];
}

TSharedRef<SWidget> SCampusButton::BuildCard(const FArguments& InArgs)
{
	const int32 Size = InArgs._FontSize;
	TAttribute<FText> TagText = InArgs._Tag;
	return SNew(SBox)
		.MinDesiredHeight(InArgs._Height)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(CampusUI::Rounded())
				.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.035f + 0.04f * Selected)); })
			]
			+ SOverlay::Slot()[ HoverFill() ]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.Padding(0.f, 10.f)
			[
				SNew(SBox)
				.WidthOverride(3.f)
				[
					SNew(SBorder)
					.BorderImage(CampusUI::White())
					.BorderBackgroundColor_Lambda([this]() { return FSlateColor(CampusUI::Alpha(CampusUI::Accent(), FMath::Max(Selected, Highlight))); })
				]
			]
			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			.Padding(18.f, 10.f, 18.f, 10.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(InArgs._Eyebrow)
					.Font(CampusUI::Font(CampusUI::EFont::Cond, 12, TEXT("SemiBold"), 160))
					.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 1.f, 0.f, 3.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Text)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Font(CampusUI::Font(CampusUI::EFont::Sans, Size, TEXT("Black")))
						.ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(12.f, 3.f, 0.f, 0.f)
					[
						SNew(SBox)
						.Visibility_Lambda([TagText]() { return TagText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						[
							SNew(STextBlock)
							.Text(TagText)
							.Font(CampusUI::Font(CampusUI::EFont::Cond, 13, TEXT("Bold"), 80))
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(FMath::Lerp(CampusUI::TextDim(), CampusUI::Accent(), Selected)); })
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(InArgs._SubText)
					.Font(CampusUI::Font(CampusUI::EFont::Sans, 14, TEXT("Medium")))
					.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
				]
			]
		];
}

TSharedRef<SWidget> SCampusButton::BuildPlaylist(const FArguments& InArgs)
{
	const FLinearColor Cyan(0.10f, 0.72f, 0.88f);
	return SNew(SBox)
		.HeightOverride(InArgs._Height)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFill)
				[
					SNew(SImage).Image(InArgs._Image)
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform_Lambda([this]() { return FSlateRenderTransform(FScale2D(1.f + 0.025f * Highlight)); })
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(FLinearColor(0.72f + 0.20f * Highlight, 0.75f + 0.20f * Highlight, 0.78f + 0.20f * Highlight)); })
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(CampusUI::Brush(CampusUI::ETex::GradUp))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.96f)))
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder).BorderImage(CampusUI::White())
				.BorderBackgroundColor_Lambda([this, Cyan]() { return FSlateColor(CampusUI::Alpha(Cyan, Selected * 0.10f)); })
			]
			+ SOverlay::Slot().VAlign(VAlign_Bottom).Padding(20.f, 16.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock).Text(InArgs._Eyebrow)
					.Font(CampusUI::Font(CampusUI::EFont::Cond, 12, TEXT("SemiBold"), 160))
					.ColorAndOpacity_Lambda([this, Cyan]() { return FSlateColor(FMath::Lerp(CampusUI::TextDim(), Cyan, FMath::Max(Selected, Highlight))); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(InArgs._Text)
						.Font(CampusUI::Font(CampusUI::EFont::Sans, InArgs._FontSize, TEXT("Black")))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(InArgs._Tag)
						.Font(CampusUI::Font(CampusUI::EFont::Num, 21, TEXT("Bold")))
						.ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(InArgs._SubText)
					.Font(CampusUI::Font(CampusUI::EFont::Sans, 14, TEXT("Medium")))
					.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder).BorderImage(CampusUI::Outline())
				.BorderBackgroundColor_Lambda([this, Cyan]() { return FSlateColor(CampusUI::Alpha(Cyan, 0.16f + 0.84f * FMath::Max(Selected, Highlight))); })
			]
		];
}

TSharedRef<SWidget> SCampusButton::BuildTab(const FArguments& InArgs)
{
	return SNew(SBox)
		.HeightOverride(InArgs._Height)
		.Padding(FMargin(14.f, 0.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._Text)
				.Font(CampusUI::Font(CampusUI::EFont::Sans, InArgs._FontSize, TEXT("Bold"), 40))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(FMath::Lerp(CampusUI::TextDim(), CampusUI::TextMain(), FMath::Max(Selected, Highlight))); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(2.f)
				[
					SNew(SBorder)
					.BorderImage(CampusUI::White())
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform_Lambda([this]() { return FSlateRenderTransform(FScale2D(FMath::Max(EaseOut(FMath::Max(Selected, Highlight * 0.35f)), 0.001f), 1.f)); })
					.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FMath::Lerp(CampusUI::Alpha(CampusUI::TextMain(), 0.5f), CampusUI::Accent(), Selected)); })
				]
			]
		];
}

TSharedRef<SWidget> SCampusButton::BuildSegment(const FArguments& InArgs)
{
	return SNew(SBox)
		.HeightOverride(InArgs._Height)
		.MinDesiredWidth(64.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(CampusUI::Rounded())
				.BorderBackgroundColor_Lambda([this]()
				{
					const FLinearColor Idle(1.f, 1.f, 1.f, 0.05f + 0.09f * Highlight);
					return FSlateColor(FMath::Lerp(Idle, CampusUI::Fill(), Selected));
				})
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(14.f, 0.f))
			[
				SNew(STextBlock)
				.Text(InArgs._Text)
				.Font(CampusUI::Font(CampusUI::EFont::Sans, InArgs._FontSize, TEXT("Bold")))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(FMath::Lerp(CampusUI::TextMain(), CampusUI::Ink(), Selected)); })
			]
		];
}

bool SCampusButton::IsActive() const
{
	return IsSelected.Get(false);
}

void SCampusButton::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const bool bHover = Button.IsValid() && (Button->IsHovered() || Button->HasKeyboardFocus()) && IsEnabled();
	const bool bFillStyle = Style == ECampusButtonStyle::Row || Style == ECampusButtonStyle::Card || Style == ECampusButtonStyle::Playlist;
	Highlight = SmoothState(Highlight, bHover ? 1.f : 0.f, InDeltaTime, bFillStyle ? 18.f : 16.f);
	Selected = SmoothState(Selected, IsActive() ? 1.f : 0.f, InDeltaTime, 18.f);
	const bool bPressed = Button.IsValid() && Button->IsPressed() && IsEnabled();
	Press = SmoothState(Press, bPressed ? 1.f : 0.f, InDeltaTime, bPressed ? 32.f : 20.f);
}

FReply SCampusButton::HandleClicked()
{
	CampusUI::PlaySound(SoundContext, TEXT("UI_Select"), 0.7f);
	OnClicked.ExecuteIfBound();
	return FReply::Handled();
}

void SCampusButton::HandleHovered()
{
	CampusUI::PlaySound(SoundContext, TEXT("UI_Hover"), 0.35f);
	OnHoveredDelegate.ExecuteIfBound();
}
