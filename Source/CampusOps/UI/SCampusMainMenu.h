#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "CampusTypes.h"

class ACampusMenuPlayerController;
class UCampusGameInstance;
class SWidgetSwitcher;
class SVerticalBox;
class SWrapBox;
class SEditableTextBox;
class SCampusMapPreview;

class CAMPUSOPS_API SCampusMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusMainMenu)
		: _ShowTitle(true)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusMenuPlayerController>, Owner)
		SLATE_ARGUMENT(bool, ShowTitle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	/** Automation hook: "title", "play", "loadouts", "gunsmith", "operator", "options" (+ optional gunsmith slot). */
	void DebugGoTo(const FString& PageName, int32 Slot);

private:
	enum class EPage : uint8
	{
		Title,
		Play,
		Loadouts,
		Gunsmith,
		Operator,
		Options,
		Custom,
		Equipment,
		Deploying
	};

	void SetPage(EPage NewPage);
	void DismissTitle();
	void CycleTab(int32 Direction);

	TSharedRef<SWidget> BuildTitle();
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildBottomBar();
	TSharedRef<SWidget> BuildDeploying();
	TSharedRef<SWidget> BuildPlayPage();
	TSharedRef<SWidget> BuildLoadoutPage();
	TSharedRef<SWidget> BuildGunsmithPage();
	TSharedRef<SWidget> BuildOperatorPage();
	TSharedRef<SWidget> BuildOptionsPage();
	TSharedRef<SWidget> BuildCustomPage();
	TSharedRef<SWidget> BuildEquipmentPage();
	TSharedRef<SWidget> MakePerkRow(int32 Slot, ECampusPerk Perk);
	TSharedRef<SWidget> BuildLoadoutCard(TFunction<int32()> Index, bool bShowName);
	TSharedRef<SWidget> MakeStatRow(int32 Index);
	TSharedRef<SWidget> MakeSettingRow(const FText& Label, TSharedRef<SWidget> Control);

	UCampusGameInstance* GetGI() const;
	FCampusLoadout& EditedLoadout();
	FCampusWeaponLoadout& EditedWeapon();
	const FCampusWeaponLoadout* GetLoadoutWeapon(int32 LoadoutIndex, bool bSecondary) const;
	void RefreshGunsmith();
	void RefreshProsCons();
	void RefreshChips();
	void SelectSlot(int32 SlotIndex);
	void SelectWeapon(FName WeaponId);
	void EquipAttachment(FName Id);
	void SaveAndPreview();
	void StartMatch(ECampusMatchMode Mode, int32 TeamSize, bool bCustom = false);
	/** Map card: the selected map's minimap from the catalog. */
	void RefreshMapCard();
	FCampusMatchRules* EditRules();
	float GetStatValue(int32 Index, bool bPreview) const;

	TWeakObjectPtr<ACampusMenuPlayerController> Owner;
	TWeakObjectPtr<UObject> SoundContext;
	EPage Page = EPage::Title;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SVerticalBox> WeaponListBox;
	TSharedPtr<class SCampusGunsmithLabels> GunsmithLabels;
	TSharedPtr<SVerticalBox> AttachmentListBox;
	TSharedPtr<SVerticalBox> ProsConsBox;
	TSharedPtr<SWrapBox> PrimaryChips;
	TSharedPtr<SEditableTextBox> JoinBox;
	TSharedPtr<class SScrollBox> PlayScroll;
	TSharedPtr<SCampusMapPreview> MapPreview;
	TSharedPtr<SWidget> ContentWidget;
	TSharedPtr<SWidget> TopBarWidget;
	TSharedPtr<SWidget> BottomBarWidget;

	float PageAnim = 1.f;
	float TitleAlpha = 1.f;
	float ChromeAlpha = 0.f;
	float DeployAlpha = 0.f;
	float DeployStart = 0.f;
	float Time = 0.f;
	int32 EditLoadoutIndex = 0;
	bool bEditSecondary = false;
	/** Left mouse held on the empty part of a weapon page: the drag turns the displayed weapon. */
	bool bTurningWeapon = false;
	int32 SelectedSlot = INDEX_NONE;
	FName HoveredAttachment;
	bool bHasHover = false;
	ECampusPerk HoveredPerk = ECampusPerk::None;
	bool bBots = true;
	bool bMatchSettingsOpen = false;
	bool bJoinOpen = false;
	FText Notice;
	float NoticeTime = -10.f;
	FText DeployMode;
	FText DeployTip;
	ECampusMap DeployMap = ECampusMap::Campus;
	FString ChipsKey;
};
