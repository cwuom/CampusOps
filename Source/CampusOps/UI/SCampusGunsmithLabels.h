#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "CampusTypes.h"

class ACampusMenuPlayerController;
class SCanvas;

/**
 * Gunsmith slot labels placed beside each part of the displayed weapon (COD style), each joined to its part by a
 * leader line. Positions follow the projected slot points every frame, so they track camera moves and the sway.
 */
class SCampusGunsmithLabels : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusGunsmithLabels) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusMenuPlayerController>, Owner)
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, SoundContext)
		SLATE_ATTRIBUTE(int32, SelectedSlot)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetSlots(const TArray<ECampusAttachmentSlot>& InSlots, TFunction<FText(ECampusAttachmentSlot)> InAttachmentName, TFunction<void(ECampusAttachmentSlot)> InOnClicked);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FLabel
	{
		ECampusAttachmentSlot Slot = ECampusAttachmentSlot::Muzzle;
		/** Part on screen, local units. */
		FVector2D Anchor = FVector2D::ZeroVector;
		FVector2D Target = FVector2D::ZeroVector;
		FVector2D Size = FVector2D(210.f, 40.f);
		/** icon centre beside the projected attachment */
		FVector2D Icon = FVector2D::ZeroVector;
		bool bVisible = false;
		bool bInit = false;
		bool bHovered = false;
		float Hover = 0.f;
	};

	TArray<FLabel> Labels;
	TSharedPtr<SCanvas> Canvas;
	TWeakObjectPtr<ACampusMenuPlayerController> Owner;
	TWeakObjectPtr<UObject> SoundContext;
	TAttribute<int32> SelectedSlot;
	TFunction<FText(ECampusAttachmentSlot)> AttachmentName;
	TFunction<void(ECampusAttachmentSlot)> OnClicked;
	float Appear = 0.f;
};
