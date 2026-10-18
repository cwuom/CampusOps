#include "UI/CampusWidgets.h"
#include "UI/CampusUIStyle.h"
#include "UI/SCampusButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"

namespace CampusWidgets
{
	TSharedRef<SWidget> Eyebrow(const TAttribute<FText>& Text, const FLinearColor& Color)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(18.f)
				.HeightOverride(2.f)
				[
					SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor(FSlateColor(Color))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(CampusUI::Font(CampusUI::EFont::Cond, 14, TEXT("SemiBold"), 320))
				.ColorAndOpacity(FSlateColor(Color))
			];
	}

	TSharedRef<SWidget> Header(const FText& EyebrowText, const TAttribute<FText>& Title, const TAttribute<FText>& Sub, float TitleSize)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Eyebrow(EyebrowText, CampusUI::Accent())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(CampusUI::Font(CampusUI::EFont::Sans, TitleSize, TEXT("Black")))
				.ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
				.ShadowOffset(FVector2D(0.f, 2.f))
				.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.45f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 18.f)
			[
				SNew(STextBlock)
				.Text(Sub)
				.Font(CampusUI::Font(CampusUI::EFont::Sans, 15, TEXT("Medium")))
				.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
			];
	}

	TSharedRef<SWidget> Section(const FText& Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(CampusUI::Font(CampusUI::EFont::Cond, 14, TEXT("SemiBold"), 260))
				.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(12.f, 1.f, 0.f, 0.f)
			[
				SNew(SBox)
				.HeightOverride(1.f)
				[
					SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor(FSlateColor(CampusUI::Hairline()))
				]
			];
	}

	TSharedRef<SWidget> Glass(TSharedRef<SWidget> Content, const FMargin& Padding, float Blur)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBackgroundBlur)
				.BlurStrength(Blur)
				.CornerRadius(FVector4(4.f, 4.f, 4.f, 4.f))
				.Padding(0.f)
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(CampusUI::Panel())
				.Padding(Padding)
				[
					Content
				]
			];
	}

	TSharedRef<SWidget> KeyHint(const FText& Key, const FText& Action)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(CampusUI::KeyCap())
				.Padding(FMargin(7.f, 1.f, 7.f, 2.f))
				[
					SNew(STextBlock)
					.Text(Key)
					.Font(CampusUI::Font(CampusUI::EFont::Cond, 13, TEXT("Bold"), 60))
					.ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 22.f, 0.f)
			[
				SNew(STextBlock)
				.Text(Action)
				.Font(CampusUI::Font(CampusUI::EFont::Sans, 14, TEXT("Medium")))
				.ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
			];
	}

	TSharedRef<SWidget> Chip(const TAttribute<FText>& Text, const FLinearColor& Color, bool bFilled)
	{
		return SNew(SBorder)
			.BorderImage(bFilled ? CampusUI::Pill() : CampusUI::Outline())
			.BorderBackgroundColor(FSlateColor(bFilled ? Color : CampusUI::Alpha(Color, 0.55f)))
			.Padding(FMargin(9.f, 2.f, 9.f, 3.f))
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(CampusUI::Font(CampusUI::EFont::Sans, 13, TEXT("Bold")))
				.ColorAndOpacity(FSlateColor(bFilled ? CampusUI::Ink() : Color))
			];
	}
}

// ------------------------------------------------------------------ segmented

void SCampusSegmented::Construct(const FArguments& InArgs)
{
	Selected = InArgs._Selected;
	OnSelect = InArgs._OnSelect;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < InArgs._Options.Num(); ++i)
	{
		Row->AddSlot()
		.AutoWidth()
		.Padding(i == 0 ? 0.f : 4.f, 0.f, 0.f, 0.f)
		[
			SNew(SCampusButton)
			.Style(ECampusButtonStyle::Segment)
			.SoundContext(InArgs._SoundContext)
			.Text(InArgs._Options[i])
			.FontSize(14)
			.Height(32.f)
			.IsSelected_Lambda([this, i]() { return Selected.Get(-1) == i; })
			.OnClicked_Lambda([this, i]() { OnSelect.ExecuteIfBound(i); })
		];
	}
	ChildSlot[ Row ];
}

// ------------------------------------------------------------------ map preview

void SCampusMapPreview::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	SetRects(InArgs._Rects);
	StartTime = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
}

void SCampusMapPreview::SetRects(const TArray<FCampusMapRect>& InRects)
{
	Rects = InRects;
	Bounds = FBox2D(ForceInit);
	for (const FCampusMapRect& R : Rects)
	{
		Bounds += R.Min;
		Bounds += R.Max;
	}
}

int32 SCampusMapPreview::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (!Bounds.bIsValid || Size.X < 4.f)
	{
		return LayerId;
	}
	// World Y runs left-to-right, world X top-to-bottom (north up for the school's main gate).
	const FVector2D World = Bounds.GetSize();
	const float Pad = 10.f;
	const float Scale = FMath::Min((Size.X - 2.f * Pad) / FMath::Max(World.Y, 1.0), (Size.Y - 2.f * Pad) / FMath::Max(World.X, 1.0));
	const FVector2D Origin((Size.X - World.Y * Scale) * 0.5f, (Size.Y - World.X * Scale) * 0.5f);
	auto ToLocal = [&](const FVector2D& P) { return FVector2f(Origin.X + (P.Y - Bounds.Min.Y) * Scale, Origin.Y + (Bounds.Max.X - P.X) * Scale); };

	const double Now = FSlateApplication::Get().GetCurrentTime() - StartTime;
	const float Reveal = FMath::Clamp((float)Now / 0.9f, 0.f, 1.f);
	const FLinearColor Building(0.92f, 0.94f, 0.92f, 0.62f);
	const FLinearColor Sports = CampusUI::Alpha(CampusUI::Accent(), 0.55f);
	const FLinearColor Cover(1.f, 1.f, 1.f, 0.25f);
	const FLinearColor Green(0.45f, 0.7f, 0.4f, 0.35f);

	int32 i = 0;
	for (const FCampusMapRect& R : Rects)
	{
		if ((float)i++ / FMath::Max(Rects.Num(), 1) > Reveal)
		{
			break;
		}
		const FVector2f A = ToLocal(FVector2D(R.Max.X, R.Min.Y));
		const FVector2f B = ToLocal(FVector2D(R.Min.X, R.Max.Y));
		const FVector2f TL(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
		const FVector2f BR(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
		const FLinearColor Line = R.Kind == 0 ? Building : (R.Kind == 1 ? Sports : (R.Kind == 3 ? Green : Cover));
		FLinearColor Fill = Line;
		Fill.A *= R.Kind == 0 ? 0.22f : 0.12f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(BR - TL), FSlateLayoutTransform(TL)), CampusUI::White(), ESlateDrawEffect::None, Fill * InWidgetStyle.GetColorAndOpacityTint());
		TArray<FVector2f> Points = { TL, FVector2f(BR.X, TL.Y), BR, FVector2f(TL.X, BR.Y), TL };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Line * InWidgetStyle.GetColorAndOpacityTint(), true, R.Kind == 0 ? 1.2f : 1.f);
	}

	// Sweep line for a "live intel" feel.
	const float Sweep = FMath::Fmod((float)Now * 0.18f, 1.f);
	const float X = Size.X * Sweep;
	TArray<FVector2f> SweepLine = { FVector2f(X, 4.f), FVector2f(X, Size.Y - 4.f) };
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), SweepLine, ESlateDrawEffect::None, CampusUI::Alpha(CampusUI::Accent(), 0.35f * InWidgetStyle.GetColorAndOpacityTint().A), true, 1.f);
	return LayerId + 2;
}
