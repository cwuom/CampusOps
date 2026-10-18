#include "UI/SCampusPauseMenu.h"
#include "UI/SCampusButton.h"
#include "UI/SCampusOptionsPanel.h"
#include "UI/CampusUIStyle.h"
#include "UI/CampusWidgets.h"
#include "Player/CampusPlayerController.h"
#include "Game/CampusGameInstance.h"
#include "Game/CampusGameState.h"
#include "Game/CampusMaps.h"
#include "CampusArmory.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusPause"

void SCampusPauseMenu::Construct(const FArguments& InArgs)
{
	Owner = InArgs._OwningController;
	UCampusGameInstance* GI = Owner.IsValid() ? Cast<UCampusGameInstance>(Owner->GetGameInstance()) : nullptr;
	TWeakObjectPtr<UCampusGameInstance> WeakGI = GI;
	TWeakObjectPtr<UObject> Sound = Owner.Get();

	TSharedRef<SVerticalBox> Loadouts = SNew(SVerticalBox);
	for (int32 i = 0; i < 3; ++i)
	{
		Loadouts->AddSlot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SCampusButton)
			.Style(ECampusButtonStyle::Card)
			.SoundContext(Sound)
			.FontSize(24)
			.Height(90.f)
			.Eyebrow(FText::Format(LOCTEXT("LoadoutEye", "LOADOUT 0{0}"), FText::AsNumber(i + 1)))
			.Tag(FText::Format(LOCTEXT("LoadoutTag", "0{0}"), FText::AsNumber(i + 1)))
			.Text_Lambda([WeakGI, i]()
			{
				return WeakGI.IsValid() ? FText::FromString(WeakGI->GetProfile().Loadouts[i].Name) : FText::GetEmpty();
			})
			.SubText_Lambda([WeakGI, i]()
			{
				if (!WeakGI.IsValid()) return FText::GetEmpty();
				const FCampusLoadout& L = WeakGI->GetProfile().Loadouts[i];
				const FCampusWeaponDef* P = UCampusArmory::Get().FindWeapon(L.Primary.WeaponId);
				const FCampusWeaponDef* S = UCampusArmory::Get().FindWeapon(L.Secondary.WeaponId);
				return FText::Format(LOCTEXT("LoadoutSub", "{0}  +  {1}"), P ? P->DisplayName : FText::GetEmpty(), S ? S->DisplayName : FText::GetEmpty());
			})
			.IsSelected_Lambda([WeakGI, i]() { return WeakGI.IsValid() && WeakGI->GetProfile().ActiveLoadout == i; })
			.OnClicked_Lambda([WeakGI, i]()
			{
				if (WeakGI.IsValid())
				{
					WeakGI->EditProfile().ActiveLoadout = i;
					WeakGI->SaveProfile();
				}
			})
		];
	}

	auto ModeName = [this]()
	{
		const ACampusGameState* GS = Owner.IsValid() && Owner->GetWorld() ? Owner->GetWorld()->GetGameState<ACampusGameState>() : nullptr;
		return GS ? FText::Format(LOCTEXT("ModeMapFmt", "{0}  ·  {1}  ·  {2}"), GS->GetModeName(), CampusMaps::Get(GS->Map).Name, CampusMaps::ConditionsLine(GS->Weather, GS->TimeOfDay)) : LOCTEXT("Map", "多人对战");
	};

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.BlurStrength_Lambda([this]() { return 6.f * Appear; })
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(CampusUI::White())
			.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.42f * Appear)); })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(1000.f)
			[
				SNew(SImage)
				.Image(CampusUI::Brush(CampusUI::ETex::GradL))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.6f * Appear)); })
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Fill)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(80.f - 40.f * (1.f - Appear), 110.f, 0.f, 70.f); }))
		[
			SNew(SBox)
			.WidthOverride(440.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					CampusWidgets::Header(LOCTEXT("PausedEye", "PAUSED"), LOCTEXT("Paused", "暂停"), TAttribute<FText>::CreateLambda(ModeName))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Resume", "继续游戏"))
					.OnClicked_Lambda([this]() { if (Owner.IsValid()) Owner->SetPauseMenuOpen(false); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Loadout", "更换配装")).SubText(LOCTEXT("NextSpawn", "下次部署生效"))
					.IsSelected_Lambda([this]() { return RightIndex == 0; })
					.OnClicked_Lambda([this]() { RightIndex = 0; if (RightPanel.IsValid()) RightPanel->SetActiveWidgetIndex(0); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Options", "设置"))
					.IsSelected_Lambda([this]() { return RightIndex == 1; })
					.OnClicked_Lambda([this]() { RightIndex = 1; if (RightPanel.IsValid()) RightPanel->SetActiveWidgetIndex(1); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 20.f, 0.f, 2.f)
				[
					SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Leave", "退出比赛")).SubText(LOCTEXT("LeaveSub", "返回大厅"))
					.OnClicked_Lambda([this]() { if (Owner.IsValid()) Owner->LeaveMatch(); })
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNullWidget::NullWidget
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ CampusWidgets::KeyHint(LOCTEXT("KeyEsc", "ESC"), LOCTEXT("ActResume", "返回游戏")) ]
					+ SHorizontalBox::Slot().AutoWidth()[ CampusWidgets::KeyHint(LOCTEXT("KeyTab", "TAB"), LOCTEXT("ActScore", "计分板")) ]
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(0.f, 0.f, 90.f - 40.f * (1.f - Appear), 0.f); }))
		[
			SNew(SBox)
			.WidthOverride(640.f)
			.MaxDesiredHeight(820.f)
			[
				SAssignNew(RightPanel, SWidgetSwitcher)
				.WidgetIndex(0)
				+ SWidgetSwitcher::Slot()
				[
					CampusWidgets::Glass(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ CampusWidgets::Eyebrow(LOCTEXT("ClassesEye", "LOADOUTS  ·  自定义配装"), CampusUI::Accent()) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 14.f)
						[
							SNew(STextBlock).Text(LOCTEXT("ClassesHint", "选择的配装将在下次部署时生效")).Font(CampusUI::Font(CampusUI::EFont::Sans, 14, TEXT("Medium"))).ColorAndOpacity(FSlateColor(CampusUI::TextDim()))
						]
						+ SVerticalBox::Slot().AutoHeight()[ Loadouts ])
				]
				+ SWidgetSwitcher::Slot()
				[
					CampusWidgets::Glass(SNew(SCampusOptionsPanel).GameInstance(WeakGI).SoundContext(Sound))
				]
			]
		]
	];
}

void SCampusPauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Appear = FMath::FInterpTo(Appear, 1.f, InDeltaTime, 10.f);
	SetRenderOpacity(FMath::Clamp(Appear * 1.4f, 0.f, 1.f));
}

#undef LOCTEXT_NAMESPACE
