#include "UI/SCampusGunsmithLabels.h"
#include "UI/CampusUIStyle.h"
#include "Menu/CampusMenuGameMode.h"
#include "Menu/CampusMenuPresentation.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Rendering/DrawElements.h"

namespace
{
	const float IconR = 13.f;
	const float LabelH = 40.f;

	void Circle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& C, float R, const FLinearColor& Col, float Thick)
	{
		TArray<FVector2f> Pts;
		for (int32 i = 0; i <= 28; ++i)
		{
			const float A = 2.f * PI * i / 28.f;
			Pts.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Col, true, Thick);
	}
}

void SCampusGunsmithLabels::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	SoundContext = InArgs._SoundContext;
	SelectedSlot = InArgs._SelectedSlot;
	ChildSlot
	[
		SAssignNew(Canvas, SCanvas).Visibility(EVisibility::SelfHitTestInvisible)
	];
}

void SCampusGunsmithLabels::SetSlots(const TArray<ECampusAttachmentSlot>& InSlots, TFunction<FText(ECampusAttachmentSlot)> InAttachmentName, TFunction<void(ECampusAttachmentSlot)> InOnClicked)
{
	AttachmentName = MoveTemp(InAttachmentName);
	OnClicked = MoveTemp(InOnClicked);

	// Slots that stay keep their position, so rebuilding after an equip does not make labels jump.
	TArray<FLabel> Old = MoveTemp(Labels);
	Labels.Reset();
	for (ECampusAttachmentSlot Slot : InSlots)
	{
		FLabel L;
		L.Slot = Slot;
		if (const FLabel* Prev = Old.FindByPredicate([Slot](const FLabel& O) { return O.Slot == Slot; }))
		{
			L = *Prev;
			L.bHovered = false;
		}
		Labels.Add(L);
	}

	Canvas->ClearChildren();
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		const ECampusAttachmentSlot Slot = Labels[i].Slot;
		Canvas->AddSlot()
		.Position_Lambda([this, i]() { return Labels.IsValidIndex(i) ? Labels[i].Icon - FVector2D(IconR, LabelH * 0.5f) : FVector2D(-4000.f, -4000.f); })
		.Size_Lambda([this, i]() { return Labels.IsValidIndex(i) ? Labels[i].Size : FVector2D::ZeroVector; })
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0.f))
			.Visibility_Lambda([this, i]() { return Labels.IsValidIndex(i) && Labels[i].bVisible ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnHovered_Lambda([this, i]()
			{
				if (Labels.IsValidIndex(i))
				{
					Labels[i].bHovered = true;
				}
				CampusUI::PlaySound(SoundContext, TEXT("UI_Hover"), 0.3f);
			})
			.OnUnhovered_Lambda([this, i]() { if (Labels.IsValidIndex(i)) { Labels[i].bHovered = false; } })
			.OnClicked_Lambda([this, Slot]()
			{
				CampusUI::PlaySound(SoundContext, TEXT("UI_Select"), 0.6f);
				if (OnClicked)
				{
					OnClicked(Slot);
				}
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(IconR * 2.f).HeightOverride(IconR * 2.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(9.f, 0.f, 0.f, 0.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(CampusText::SlotName(Slot))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Font(CampusUI::Font(CampusUI::EFont::Sans, 15, TEXT("Bold"), 40))
						.ColorAndOpacity_Lambda([this, i, Slot]()
						{
							if (SelectedSlot.Get(INDEX_NONE) == (int32)Slot)
							{
								return FSlateColor(CampusUI::Accent());
							}
							const float H = Labels.IsValidIndex(i) ? Labels[i].Hover : 0.f;
							return FSlateColor(CampusUI::Alpha(CampusUI::TextMain(), (0.82f + 0.18f * H) * Appear));
						})
						.ShadowOffset(FVector2D(1.f, 1.f))
						.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([this, Slot]() { return AttachmentName ? AttachmentName(Slot) : FText::GetEmpty(); })
						.ToolTipText_Lambda([this, Slot]() { return AttachmentName ? AttachmentName(Slot) : FText::GetEmpty(); })
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Visibility_Lambda([this, Slot]() { return AttachmentName && !AttachmentName(Slot).IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						.Font(CampusUI::Font(CampusUI::EFont::Sans, 12, TEXT("Medium")))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(CampusUI::Alpha(CampusUI::Accent(), Appear)); })
						.ShadowOffset(FVector2D(1.f, 1.f))
						.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f))
					]
				]
			]
		];
	}
}

void SCampusGunsmithLabels::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	ACampusMenuPlayerController* PC = Owner.Get();
	int32 ViewX = 0;
	int32 ViewY = 0;
	if (PC)
	{
		PC->GetViewportSize(ViewX, ViewY);
	}
	const FVector2D Local = AllottedGeometry.GetLocalSize();
	FVector2D CenterPx;
	if (!PC || ViewX <= 0 || ViewY <= 0 || Local.X <= 1.f || !PC->GetWeaponScreenCenter(CenterPx))
	{
		for (FLabel& L : Labels)
		{
			L.bVisible = false;
		}
		Appear = 0.f;
		return;
	}
	const FVector2D Scale(Local.X / ViewX, Local.Y / ViewY);
	TArray<CampusMenuPresentation::FAttachmentLabel> Placement;
	Appear = FMath::Min(1.f, Appear + InDeltaTime * 3.f);

	for (FLabel& L : Labels)
	{
		FVector2D Px;
		L.bVisible = PC->GetSlotScreenPosition(L.Slot, Px);
		if (!L.bVisible)
		{
			continue;
		}
		L.Anchor = Px * Scale;
		CampusMenuPresentation::FAttachmentLabel& Placed = Placement.AddDefaulted_GetRef();
		Placed.Slot = L.Slot;
		Placed.Anchor = L.Anchor;
		L.Hover = FMath::FInterpTo(L.Hover, L.bHovered ? 1.f : 0.f, InDeltaTime, 14.f);
	}
	CampusMenuPresentation::PlaceAttachmentLabels(Local, SelectedSlot.Get(INDEX_NONE) != INDEX_NONE, CenterPx * Scale, Placement);

	for (FLabel& L : Labels)
	{
		if (!L.bVisible)
		{
			continue;
		}
		const CampusMenuPresentation::FAttachmentLabel* Placed = Placement.FindByPredicate([&L](const CampusMenuPresentation::FAttachmentLabel& Entry) { return Entry.Slot == L.Slot; });
		if (!Placed) continue;
		L.Size = Placed->Bounds.GetSize();
		L.Target = Placed->Bounds.Min + FVector2D(IconR, LabelH * 0.5f);
		L.Icon = L.Target;
		L.bInit = true;
	}
}

int32 SCampusGunsmithLabels::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* Disc = CampusUI::Brush(CampusUI::ETex::Disc);
	const int32 Selected = SelectedSlot.Get(INDEX_NONE);
	const float Tint = InWidgetStyle.GetColorAndOpacityTint().A * Appear;
	for (const FLabel& L : Labels)
	{
		if (!L.bVisible || !L.bInit)
		{
			continue;
		}
		const bool bSelected = (int32)L.Slot == Selected;
		const bool bEquipped = AttachmentName && !AttachmentName(L.Slot).IsEmpty();
		const FLinearColor Col = bSelected ? CampusUI::Alpha(CampusUI::Accent(), Tint) : FLinearColor(1.f, 1.f, 1.f, (0.72f + 0.28f * L.Hover) * Tint);
		const FVector2f Icon(L.Icon);
		const FVector2f Part(L.Anchor);

		// Leader line from the icon rim to the part, ending on a small dot.
		const FVector2f ToPart = Part - Icon;
		const float Len = ToPart.Size();
		if (Len > IconR + 8.f)
		{
			const FVector2f Dir = ToPart / Len;
			TArray<FVector2f> Line = { Icon + Dir * (IconR + 3.f), Part - Dir * 3.f };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, CampusUI::Alpha(Col, 0.55f), true, 1.2f);
		}
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(7.f, 7.f), FSlateLayoutTransform(Part - FVector2f(3.5f, 3.5f))), Disc, ESlateDrawEffect::None, Col);

		// Icon: dark disc and ring, with a plus for an empty slot or a filled dot once something is fitted.
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(IconR * 2.f, IconR * 2.f), FSlateLayoutTransform(Icon - FVector2f(IconR, IconR))),
			Disc, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, (0.42f + 0.2f * L.Hover) * Tint));
		Circle(OutDrawElements, LayerId + 1, AllottedGeometry, Icon, IconR, Col, 1.5f);
		if (bEquipped)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(9.f, 9.f), FSlateLayoutTransform(Icon - FVector2f(4.5f, 4.5f))),
				Disc, ESlateDrawEffect::None, bSelected ? Col : CampusUI::Alpha(CampusUI::Accent(), Tint));
		}
		else
		{
			const float P = 5.f;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ Icon - FVector2f(P, 0.f), Icon + FVector2f(P, 0.f) }, ESlateDrawEffect::None, Col, true, 1.6f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ Icon - FVector2f(0.f, P), Icon + FVector2f(0.f, P) }, ESlateDrawEffect::None, Col, true, 1.6f);
		}
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}
