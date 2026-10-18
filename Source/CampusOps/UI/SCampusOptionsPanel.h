#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UCampusGameInstance;
class SVerticalBox;
class SHorizontalBox;
class SWidgetSwitcher;
class SExpandableArea;
class SScrollBox;

/** Settings bound directly to the saved profile, in tabs: gameplay, controls, key bindings, visuals, audio. */
class CAMPUSOPS_API SCampusOptionsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusOptionsPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCampusGameInstance>, GameInstance)
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, SoundContext)
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	/** Q / E and the gamepad shoulders step through the tabs. */
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** Starts a tab; the rows added after it go on its page. */
	void AddTab(const FText& Label);
	void SelectTab(int32 Index);
	void AddSection(const FText& Label);
	/** OnCommit runs once the slider is released (settings that are expensive to apply on every step). */
	void AddSlider(const FText& Label, float Min, float Max, float Step, TFunction<float()> Getter, TFunction<void(float)> Setter, TFunction<FText(float)> Format, TFunction<void()> OnCommit = nullptr);
	/** "‹ value ›" cycling through Count entries (long lists such as resolutions). */
	void AddStepper(const FText& Label, int32 Count, TFunction<int32()> Getter, TFunction<void(int32)> Setter, TFunction<FText(int32)> Format);
	void AddChoice(const FText& Label, TArray<FText> Options, TFunction<int32()> Getter, TFunction<void(int32)> Setter);
	void AddRow(const FText& Label, TSharedRef<SWidget> Control);
	void Changed();
	EActiveTimerReturnType ScrollAdvancedIntoView(double InCurrentTime, float InDeltaTime);

	TWeakObjectPtr<UCampusGameInstance> GameInstance;
	TWeakObjectPtr<UObject> SoundContext;
	FSimpleDelegate OnChanged;
	/** Rows of the tab being built. */
	TSharedPtr<SVerticalBox> Rows;
	TSharedPtr<SHorizontalBox> TabBar;
	TSharedPtr<SWidgetSwitcher> Pages;
	TSharedPtr<SExpandableArea> AdvancedGraphics;
	TSharedPtr<SScrollBox> GraphicsScroll;
	TWeakPtr<FActiveTimerHandle> AdvancedScrollTimer;
	int32 NumTabs = 0;
	int32 ActiveTab = 0;
};
