#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ACampusPlayerController;
class SWidgetSwitcher;

class CAMPUSOPS_API SCampusPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusPauseMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusPlayerController>, OwningController)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TWeakObjectPtr<ACampusPlayerController> Owner;
	TSharedPtr<SWidgetSwitcher> RightPanel;
	int32 RightIndex = 0;
	float Appear = 0.f;
};
