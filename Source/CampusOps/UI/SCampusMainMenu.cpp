#include "UI/SCampusMainMenu.h"
#include "UI/SCampusButton.h"
#include "UI/SCampusOptionsPanel.h"
#include "Player/CampusInputConfig.h"
#include "UI/SCampusGunsmithLabels.h"
#include "UI/CampusUIStyle.h"
#include "UI/CampusWidgets.h"
#include "Menu/CampusMenuGameMode.h"
#include "Menu/CampusMenuStage.h"
#include "Menu/CampusMenuPresentation.h"
#include "Map/CampusSchoolBuilder.h"
#include "Game/CampusGameInstance.h"
#include "Game/CampusMaps.h"
#include "CampusArmory.h"
#include "CampusPerks.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "CampusMenu"

using CampusUI::EFont;

namespace
{
	FSlateColor Col(const FLinearColor& C) { return FSlateColor(C); }
	const float BarWidth = 160.f;
	const float PlaylistHeight = 240.f;
	const float PlayReservedHeight = 112.f + 78.f + PlaylistHeight + 18.f + 42.f;

	TSharedRef<SWidget> MenuPanel(TSharedRef<SWidget> Content, const FMargin& Padding = FMargin(20.f, 18.f), bool bBlur = false)
	{
		TSharedRef<SWidget> Panel = SNew(SBorder)
			.BorderImage(CampusUI::Panel())
			.Padding(Padding)
			[
				Content
			];
		if (!bBlur)
		{
			return Panel;
		}
		return SNew(SBackgroundBlur)
			.BlurStrength(2.5f)
			.LowQualityFallbackBrush(CampusUI::Panel())
			.CornerRadius(FVector4(4.f, 4.f, 4.f, 4.f))
			.Padding(0.f)
			[
				Panel
			];
	}

	TSharedRef<SScrollBox> MenuScroll(TSharedRef<SWidget> Content)
	{
		return SNew(SScrollBox)
			.ScrollBarThickness(FVector2D(3.f, 3.f))
			.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
			+ SScrollBox::Slot().Padding(0.f, 0.f, 8.f, 0.f)[ Content ];
	}

	TSharedRef<SWidget> MenuHeader(const FText& Eyebrow, const TAttribute<FText>& Title, const TAttribute<FText>& Sub, float TitleSize = 34.f)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Eyebrow)
				.Font(CampusUI::Font(EFont::Cond, 12, TEXT("SemiBold"), 180))
				.ColorAndOpacity(Col(CampusUI::TextDim()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Title)
				.Font(CampusUI::Font(EFont::Sans, TitleSize, TEXT("Black")))
				.ColorAndOpacity(Col(CampusUI::TextMain()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 14.f)
			[
				SNew(STextBlock).Text(Sub)
				.Font(CampusUI::Font(EFont::Sans, 13, TEXT("Medium")))
				.AutoWrapText(true).ColorAndOpacity(Col(CampusUI::TextDim()))
			];
	}

	TSharedRef<STextBlock> Label(const TAttribute<FText>& Text, EFont Family, float Size, const TCHAR* Weight, const FLinearColor& Color, int32 Tracking = 0)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(CampusUI::Font(Family, Size, Weight, Tracking))
			.ColorAndOpacity(Col(Color));
	}

	TSharedRef<SWidget> Hairline(float Width = 0.f)
	{
		return SNew(SBox)
			.HeightOverride(1.f)
			.WidthOverride(Width > 0.f ? FOptionalSize(Width) : FOptionalSize())
			[
				SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor(Col(CampusUI::Hairline()))
			];
	}

	const FText Tips[] = {
		LOCTEXT("Tip0", "提示：冲刺中按下蹲键即可滑铲，滑铲时可以开镜射击。"),
		LOCTEXT("Tip1", "提示：滑铲途中再按一次蹲键、跳跃或冲刺键可以立即取消滑铲。"),
		LOCTEXT("Tip2", "提示：换弹时开火、冲刺、近战或切枪都能打断换弹；新弹匣插入后打断不会丢失弹药。"),
		LOCTEXT("Tip3", "提示：消音器能让你开火时不暴露在敌方雷达上。"),
		LOCTEXT("Tip4", "提示：双层教学大楼的走廊是中路交火的关键，注意窗口的侧向视线。"),
		LOCTEXT("Tip5", "提示：连按两次冲刺键进入战术冲刺，速度更快但开火前摇更长。")
	};
}

UCampusGameInstance* SCampusMainMenu::GetGI() const
{
	return Owner.IsValid() ? Owner->GetCampusGI() : nullptr;
}

FCampusLoadout& SCampusMainMenu::EditedLoadout()
{
	return GetGI()->EditLoadout(EditLoadoutIndex);
}

FCampusWeaponLoadout& SCampusMainMenu::EditedWeapon()
{
	FCampusLoadout& L = EditedLoadout();
	return bEditSecondary ? L.Secondary : L.Primary;
}

const FCampusWeaponLoadout* SCampusMainMenu::GetLoadoutWeapon(int32 LoadoutIndex, bool bSecondary) const
{
	const UCampusGameInstance* GI = GetGI();
	if (!GI || !GI->GetProfile().Loadouts.IsValidIndex(LoadoutIndex))
	{
		return nullptr;
	}
	const FCampusLoadout& L = GI->GetProfile().Loadouts[LoadoutIndex];
	return bSecondary ? &L.Secondary : &L.Primary;
}

// ------------------------------------------------------------------ construction

void SCampusMainMenu::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	SoundContext = Owner.Get();
	Page = InArgs._ShowTitle ? EPage::Title : EPage::Play;
	TitleAlpha = InArgs._ShowTitle ? 1.f : 0.f;
	ChromeAlpha = InArgs._ShowTitle ? 0.f : 1.f;
	if (const UCampusGameInstance* GI = GetGI())
	{
		EditLoadoutIndex = GI->GetProfile().ActiveLoadout;
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(CampusUI::White())
			.Visibility_Lambda([this]() { return Page == EPage::Play ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderBackgroundColor_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, 0.20f * ChromeAlpha)); })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(860.f)
			[
				SNew(SImage)
				.Image(CampusUI::Brush(CampusUI::ETex::GradL))
				.ColorAndOpacity_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, 0.54f * FMath::Max(ChromeAlpha, TitleAlpha * 0.8f))); })
			]
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.HeightOverride(150.f)
			[
				SNew(SImage)
				.Image(CampusUI::Brush(CampusUI::ETex::GradDown))
				.ColorAndOpacity_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, 0.6f * ChromeAlpha)); })
			]
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.HeightOverride(160.f)
			[
				SNew(SImage)
				.Image(CampusUI::Brush(CampusUI::ETex::GradUp))
				.ColorAndOpacity_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, 0.7f * FMath::Max(ChromeAlpha, TitleAlpha))); })
			]
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		.Padding(48.f, 24.f, 48.f, 0.f)
		[
			BuildTopBar()
		]
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>::CreateLambda([this]()
		{
			const float A = FMath::Clamp(PageAnim, 0.f, 1.f);
			const float Ease = 1.f - FMath::Pow(1.f - A, 3.f);
			return FMargin(CampusMenuPresentation::OuterMargin - 24.f * (1.f - Ease), 112.f, CampusMenuPresentation::OuterMargin + 24.f * (1.f - Ease), 78.f);
		}))
		[
			SAssignNew(ContentWidget, SBox)
			[
				SAssignNew(Switcher, SWidgetSwitcher)
				.WidgetIndex(1)
				+ SWidgetSwitcher::Slot()[ SNullWidget::NullWidget ]
				+ SWidgetSwitcher::Slot()[ BuildPlayPage() ]
				+ SWidgetSwitcher::Slot()[ BuildLoadoutPage() ]
				+ SWidgetSwitcher::Slot()[ BuildGunsmithPage() ]
				+ SWidgetSwitcher::Slot()[ BuildOperatorPage() ]
				+ SWidgetSwitcher::Slot()[ BuildOptionsPage() ]
				+ SWidgetSwitcher::Slot()[ BuildCustomPage() ]
				+ SWidgetSwitcher::Slot()[ BuildEquipmentPage() ]
				+ SWidgetSwitcher::Slot()[ SNullWidget::NullWidget ]
			]
		]
		// Gunsmith slot labels sit beside the weapon in viewport space, above the page panels.
		+ SOverlay::Slot()
		[
			SAssignNew(GunsmithLabels, SCampusGunsmithLabels)
			.Owner(Owner)
			.SoundContext(SoundContext)
			.SelectedSlot_Lambda([this]() { return SelectedSlot; })
			.Visibility_Lambda([this]() { return Page == EPage::Gunsmith ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		.Padding(48.f, 0.f, 48.f, 24.f)
		[
			BuildBottomBar()
		]
		+ SOverlay::Slot()
		[
			BuildTitle()
		]
		+ SOverlay::Slot()
		[
			BuildDeploying()
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0.f, 0.f, 0.f, 92.f)
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() { return (Time - NoticeTime) < 2.6f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderImage(CampusUI::Rounded())
			.BorderBackgroundColor_Lambda([this]() { return Col(CampusUI::Alpha(FLinearColor(0.12f, 0.02f, 0.015f, 0.9f), FMath::Clamp(2.6f - (Time - NoticeTime), 0.f, 1.f))); })
			.Padding(FMargin(18.f, 8.f))
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return Notice; })
				.Font(CampusUI::Font(EFont::Sans, 16, TEXT("Bold")))
				.ColorAndOpacity_Lambda([this]() { return Col(CampusUI::Alpha(FLinearColor(1.f, 0.62f, 0.55f), FMath::Clamp(2.6f - (Time - NoticeTime), 0.f, 1.f))); })
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(CampusUI::White())
			.BorderBackgroundColor_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, Owner.IsValid() ? Owner->GetCameraTransitionAlpha() : 0.f)); })
			.Visibility_Lambda([this]() { return Owner.IsValid() && Owner->GetCameraTransitionAlpha() > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
	];

	if (Page != EPage::Title)
	{
		SetPage(EPage::Play);
	}
}

TSharedRef<SWidget> SCampusMainMenu::BuildTitle()
{
	auto Fade = [this](const FLinearColor& C, float Extra = 1.f) { return Col(CampusUI::Alpha(C, TitleAlpha * Extra)); };
	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return TitleAlpha > 0.01f ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(CampusUI::White())
			.BorderBackgroundColor_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, 0.18f * TitleAlpha)); })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(140.f - 70.f * (1.f - TitleAlpha), 0.f, 0.f, 90.f); }))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TitleCN", "光華行動"))
				.Font(CampusUI::Font(EFont::Title, 150, TEXT("Black"), 60))
				.ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::TextMain()); })
				.ShadowOffset(FVector2D(0.f, 6.f))
				.ShadowColorAndOpacity_Lambda([this]() { return FLinearColor(0.f, 0.f, 0.f, 0.55f * TitleAlpha); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.f, -10.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 18.f, 0.f)
				[
					SNew(SBox).WidthOverride(72.f).HeightOverride(4.f)
					[
						SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor_Lambda([Fade]() { return Fade(CampusUI::Accent()); })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TitleEN", "CAMPUS OPS"))
					.Font(CampusUI::Font(EFont::Stencil, 40, TEXT("Regular"), 520))
					.ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::Accent()); })
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(10.f, 22.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TitleSub", "現代戰爭  ·  台灣中學戰場"))
				.Font(CampusUI::Font(EFont::Sans, 22, TEXT("Medium"), 120))
				.ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::TextDim()); })
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0.f, 0.f, 0.f, 118.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(90.f).HeightOverride(1.f)[ SNew(SImage).Image(CampusUI::Brush(CampusUI::ETex::GradR)).ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::TextMain(), 0.6f); }) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(22.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PressAny", "按任意键开始"))
				.Font(CampusUI::Font(EFont::Sans, 20, TEXT("Bold"), 380))
				.ColorAndOpacity_Lambda([this, Fade]() { return Fade(CampusUI::TextMain(), 0.45f + 0.55f * (0.5f + 0.5f * FMath::Sin(Time * 2.6f))); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(90.f).HeightOverride(1.f)[ SNew(SImage).Image(CampusUI::Brush(CampusUI::ETex::GradL)).ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::TextMain(), 0.6f); }) ]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(64.f, 0.f, 0.f, 36.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TitleBuild", "原型版本 0.3  ·  局域网多人"))
			.Font(CampusUI::Font(EFont::Cond, 14, TEXT("Medium"), 160))
			.ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::TextFaint()); })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(0.f, 0.f, 64.f, 36.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TitleCopy", "© 2026 光華行動開發組  ·  UNREAL ENGINE 5"))
			.Font(CampusUI::Font(EFont::Cond, 14, TEXT("Medium"), 160))
			.ColorAndOpacity_Lambda([Fade]() { return Fade(CampusUI::TextFaint()); })
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildDeploying()
{
	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return DeployAlpha > 0.01f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(CampusUI::White())
			.BorderBackgroundColor_Lambda([this]() { return Col(FLinearColor(0.f, 0.f, 0.f, 0.86f * DeployAlpha)); })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(120.f - 30.f * (1.f - DeployAlpha), 0.f, 0.f, 150.f); }))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				CampusWidgets::Eyebrow(LOCTEXT("Deploying", "DEPLOYING  ·  正在部署"), CampusUI::Accent())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return CampusMaps::Get(DeployMap).Name; })
				.Font(CampusUI::Font(EFont::Title, 96, TEXT("Black"), 40))
				.ColorAndOpacity(Col(CampusUI::TextMain()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const UCampusGameInstance* GI = GetGI();
					const int32 W = GI ? GI->GetProfile().SelectedWeather : -1;
					const int32 T = GI ? GI->GetProfile().SelectedTime : -2;
					const FText Weather = W < 0 ? LOCTEXT("DWRandom", "天气随机") : CampusMaps::WeatherName((ECampusWeather)W);
					const FText TimeText = T == -1 ? LOCTEXT("DTRandom", "时间随机") : CampusMaps::TimeName(T == -2 ? CampusMaps::Get(DeployMap).DefaultTime : (ECampusTimeOfDay)T);
					return FText::Format(LOCTEXT("DeployLatinFmt", "{0}   ·   {1}   ·   {2}  ·  {3}"), CampusMaps::Get(DeployMap).Latin, CampusMaps::Get(DeployMap).Location, TimeText, Weather);
				})
				.Font(CampusUI::Font(EFont::Cond, 18, TEXT("SemiBold"), 160))
				.ColorAndOpacity(Col(FMath::Lerp(CampusUI::TextDim(), CampusUI::Accent(), 0.4f)))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 0.f, 22.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return DeployMode; })
				.Font(CampusUI::Font(EFont::Sans, 24, TEXT("Bold"), 80))
				.ColorAndOpacity(Col(CampusUI::TextDim()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(520.f)
				.HeightOverride(3.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor(Col(FLinearColor(1.f, 1.f, 1.f, 0.12f))) ]
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(CampusUI::White())
						.BorderBackgroundColor(Col(CampusUI::Accent()))
						.RenderTransformPivot(FVector2D(0.f, 0.5f))
						.RenderTransform_Lambda([this]() { return FSlateRenderTransform(FScale2D(FMath::Clamp((Time - DeployStart) / 1.1f, 0.01f, 1.f), 1.f)); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 18.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(620.f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return DeployTip; })
					.Font(CampusUI::Font(EFont::Sans, 16, TEXT("Medium")))
					.AutoWrapText(true)
					.ColorAndOpacity(Col(CampusUI::TextDim()))
				]
			]
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildTopBar()
{
	struct FTab { FText Label; EPage Target; };
	const TArray<FTab> Tabs = {
		{ LOCTEXT("TabPlay", "多人游戏"), EPage::Play },
		{ LOCTEXT("TabWeapons", "武器"), EPage::Loadouts },
		{ LOCTEXT("TabOperator", "干员"), EPage::Operator },
		{ LOCTEXT("TabOptions", "设置"), EPage::Options }
	};
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 30.f, 0.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(LOCTEXT("LogoCN", "光華行動"), EFont::Title, 27, TEXT("Black"), CampusUI::TextMain(), 40)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(1.f, -4.f, 0.f, 0.f)
		[
			Label(LOCTEXT("LogoEN", "CAMPUS OPS"), EFont::Stencil, 12, TEXT("Regular"), CampusUI::Accent(), 520)
		]
	];
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 22.f, 0.f)
	[
		SNew(SBox).WidthOverride(1.f).HeightOverride(36.f)[ SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor(Col(CampusUI::Hairline())) ]
	];
	for (const FTab& Tab : Tabs)
	{
		const EPage Target = Tab.Target;
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SCampusButton)
			.Style(ECampusButtonStyle::Tab)
			.SoundContext(SoundContext)
			.Text(Tab.Label)
			.FontSize(17)
			.Height(42.f)
			.IsSelected_Lambda([this, Target]() { return Page == Target || (Target == EPage::Loadouts && (Page == EPage::Gunsmith || Page == EPage::Equipment)) || (Target == EPage::Play && Page == EPage::Custom); })
			.OnClicked_Lambda([this, Target]() { SetPage(Target); })
		];
	}
	Row->AddSlot().FillWidth(1.f)[ SNew(SSpacer) ];
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
	[
		Label(LOCTEXT("LAN", "局域网"), EFont::Sans, 13, TEXT("Medium"), CampusUI::TextDim())
	];
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const UCampusGameInstance* GI = GetGI();
				return FText::FromString(GI ? GI->GetProfile().PlayerName : TEXT(""));
			})
			.Font(CampusUI::Font(EFont::Sans, 15, TEXT("Bold")))
			.ColorAndOpacity(Col(CampusUI::TextMain()))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			Label(LOCTEXT("Rank", "光華特勤隊"), EFont::Cond, 12, TEXT("Medium"), CampusUI::TextFaint(), 80)
		]
	];
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(44.f)
		.HeightOverride(44.f)
		[
			SNew(SBorder)
			.BorderImage(CampusUI::Rounded())
			.BorderBackgroundColor(Col(CampusUI::Accent()))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const UCampusGameInstance* GI = GetGI();
					const FString Name = GI ? GI->GetProfile().PlayerName : TEXT("?");
					return FText::FromString(Name.Left(1).ToUpper());
				})
				.Font(CampusUI::Font(EFont::Sans, 22, TEXT("Black")))
				.ColorAndOpacity(Col(CampusUI::Ink()))
			]
		]
	];

	return SAssignNew(TopBarWidget, SBox)
		.Visibility_Lambda([this]() { return (Page == EPage::Title || Page == EPage::Deploying || ChromeAlpha < 0.01f) ? EVisibility::Collapsed : EVisibility::Visible; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Row ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[ Hairline() ]
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildBottomBar()
{
	return SAssignNew(BottomBarWidget, SHorizontalBox)
		.Visibility_Lambda([this]() { return (Page == EPage::Title || Page == EPage::Deploying || ChromeAlpha < 0.01f) ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
		+ SHorizontalBox::Slot().AutoWidth()[ CampusWidgets::KeyHint(LOCTEXT("KeyEsc", "ESC"), LOCTEXT("ActBack", "返回")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ CampusWidgets::KeyHint(LOCTEXT("KeyQE", "Q / E"), LOCTEXT("ActTabs", "切换页面")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ CampusWidgets::KeyHint(LOCTEXT("KeyMouse", "左键"), LOCTEXT("ActSelect", "选择")) ]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return (Page == EPage::Gunsmith || Page == EPage::Loadouts) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				CampusWidgets::KeyHint(LOCTEXT("KeyDrag", "左键拖动"), LOCTEXT("ActTurn", "旋转武器"))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label(LOCTEXT("Version", "原型版本 0.3"), EFont::Cond, 12, TEXT("Medium"), CampusUI::TextFaint(), 80)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(150.f)
			[
				SNew(SCampusButton).SoundContext(SoundContext).Text(LOCTEXT("Quit", "退出游戏")).Height(30.f).FontSize(13)
				.OnClicked_Lambda([this]() { if (Owner.IsValid()) Owner->QuitGame(); })
			]
		];
}

TSharedRef<SWidget> SCampusMainMenu::MakeSettingRow(const FText& RowLabel, TSharedRef<SWidget> Control)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			Label(RowLabel, EFont::Sans, 16, TEXT("Bold"), CampusUI::TextMain())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Control
		];
}

// ------------------------------------------------------------------ pages

TSharedRef<SWidget> SCampusMainMenu::BuildLoadoutCard(TFunction<int32()> Index, bool bShowName)
{
	auto WeaponName = [this, Index](bool bSecondary)
	{
		const FCampusWeaponLoadout* W = GetLoadoutWeapon(Index(), bSecondary);
		const FCampusWeaponDef* Def = W ? UCampusArmory::Get().FindWeapon(W->WeaponId) : nullptr;
		return Def ? Def->DisplayName : FText::GetEmpty();
	};
	auto ClassName = [this, Index](bool bSecondary)
	{
		const FCampusWeaponLoadout* W = GetLoadoutWeapon(Index(), bSecondary);
		const FCampusWeaponDef* Def = W ? UCampusArmory::Get().FindWeapon(W->WeaponId) : nullptr;
		return Def ? CampusText::WeaponClassName(Def->Class) : FText::GetEmpty();
	};
	auto Attachments = [this, Index](bool bSecondary)
	{
		const FCampusWeaponLoadout* W = GetLoadoutWeapon(Index(), bSecondary);
		TArray<FString> Parts;
		if (W)
		{
			for (const FName& Id : W->Attachments)
			{
				if (const FCampusAttachmentDef* A = UCampusArmory::Get().FindAttachment(Id))
				{
					Parts.Add(A->DisplayName.ToString());
				}
			}
		}
		return FText::FromString(Parts.Num() > 0 ? FString::Join(Parts, TEXT("  ·  ")) : FString(TEXT("无配件")));
	};

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (bShowName)
	{
		Box->AddSlot().AutoHeight()[ CampusWidgets::Eyebrow(LOCTEXT("CurrentLoadout", "当前配装"), CampusUI::Accent()) ];
		Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 12.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this, Index]()
			{
				const UCampusGameInstance* GI = GetGI();
				return (GI && GI->GetProfile().Loadouts.IsValidIndex(Index())) ? FText::FromString(GI->GetProfile().Loadouts[Index()].Name) : FText::GetEmpty();
			})
			.Font(CampusUI::Font(EFont::Sans, 26, TEXT("Black")))
			.ColorAndOpacity(Col(CampusUI::TextMain()))
		];
	}
	for (const bool bSecondary : { false, true })
	{
		Box->AddSlot().AutoHeight().Padding(0.f, bSecondary ? 14.f : 0.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				Label(bSecondary ? LOCTEXT("SecondaryCaps", "副武器") : LOCTEXT("PrimaryCaps", "主武器"), EFont::Sans, 13, TEXT("Bold"), CampusUI::TextDim())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(STextBlock).Text_Lambda([ClassName, bSecondary]() { return ClassName(bSecondary); })
				.Font(CampusUI::Font(EFont::Sans, 13, TEXT("Medium"))).ColorAndOpacity(Col(CampusUI::TextFaint()))
			]
		];
		Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text_Lambda([WeaponName, bSecondary]() { return WeaponName(bSecondary); })
			.Font(CampusUI::Font(EFont::Cond, bSecondary ? 24 : 32, TEXT("Bold"), 30))
			.ColorAndOpacity(Col(CampusUI::TextMain()))
		];
		Box->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text_Lambda([Attachments, bSecondary]() { return Attachments(bSecondary); })
			.Font(CampusUI::Font(EFont::Sans, 13, TEXT("Medium")))
			.AutoWrapText(true)
			.ColorAndOpacity(Col(bSecondary ? CampusUI::TextDim() : FMath::Lerp(CampusUI::TextDim(), CampusUI::Accent(), 0.45f)))
		];
	}
	auto LoadoutAt = [this, Index]() -> const FCampusLoadout*
	{
		const UCampusGameInstance* GI = GetGI();
		return GI && GI->GetProfile().Loadouts.IsValidIndex(Index()) ? &GI->GetProfile().Loadouts[Index()] : nullptr;
	};
	Box->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 2.f)
	[
		Label(LOCTEXT("GearCaps", "投掷物"), EFont::Sans, 13, TEXT("Bold"), CampusUI::TextDim())
	];
	Box->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text_Lambda([LoadoutAt]()
		{
			const FCampusLoadout* L = LoadoutAt();
			return L ? FText::Format(LOCTEXT("GearFmt", "{0}  ·  {1}"), CampusText::ThrowableName(L->Lethal), CampusText::ThrowableName(L->Tactical)) : FText::GetEmpty();
		})
		.Font(CampusUI::Font(EFont::Sans, 15, TEXT("Bold")))
		.ColorAndOpacity(Col(CampusUI::TextMain()))
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 2.f)
	[
		Label(LOCTEXT("PerkCaps", "技能"), EFont::Sans, 13, TEXT("Bold"), CampusUI::TextDim())
	];
	TSharedRef<SHorizontalBox> PerkRow = SNew(SHorizontalBox);
	for (int32 Slot = 0; Slot < CampusPerks::NumSlots; ++Slot)
	{
		auto PerkAt = [LoadoutAt, Slot]() { const FCampusLoadout* L = LoadoutAt(); return L && L->Perks.IsValidIndex(Slot) ? L->Perks[Slot] : ECampusPerk::None; };
		PerkRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 14.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([PerkAt]() { return CampusPerks::Info(PerkAt()).Name; })
			.Font(CampusUI::Font(EFont::Sans, 15, TEXT("Bold")))
			.ColorAndOpacity_Lambda([PerkAt]() { return Col(CampusPerks::GroupColor(CampusPerks::Info(PerkAt()).Group)); })
		];
	}
	Box->AddSlot().AutoHeight()[ PerkRow ];
	Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(STextBlock)
		.Text_Lambda([LoadoutAt]() { const FCampusLoadout* L = LoadoutAt(); const ECampusPerkGroup G = L ? CampusPerks::GetSpecialty(L->Perks) : ECampusPerkGroup::Count; return G == ECampusPerkGroup::Count ? FText::GetEmpty() : CampusPerks::SpecialtyName(G); })
		.Font(CampusUI::Font(EFont::Sans, 13, TEXT("Medium")))
		.ColorAndOpacity_Lambda([LoadoutAt]() { const FCampusLoadout* L = LoadoutAt(); const ECampusPerkGroup G = L ? CampusPerks::GetSpecialty(L->Perks) : ECampusPerkGroup::Count; return Col(G == ECampusPerkGroup::Count ? CampusUI::TextDim() : CampusPerks::GroupColor(G)); })
	];
	return Box;
}

TSharedRef<SWidget> SCampusMainMenu::BuildPlayPage()
{
	TWeakObjectPtr<UObject> Sound = SoundContext;
	auto SelectedMap = [this]() -> ECampusMap
	{
		const UCampusGameInstance* GI = GetGI();
		const ECampusMap M = GI ? (ECampusMap)FMath::Clamp(GI->GetProfile().SelectedMap, 0, (int32)ECampusMap::Count - 1) : ECampusMap::Campus;
		return CampusMaps::Get(M).bPlayable ? M : ECampusMap::Campus;
	};
	auto Conditions = [this, SelectedMap]()
	{
		const UCampusGameInstance* GI = GetGI();
		const int32 W = GI ? GI->GetProfile().SelectedWeather : -1;
		const int32 T = GI ? GI->GetProfile().SelectedTime : -2;
		const FText Weather = W < 0 ? LOCTEXT("ChipWRandom", "随机天气") : CampusMaps::WeatherName((ECampusWeather)W);
		const FText TimeOfDay = T == -1 ? LOCTEXT("ChipTRandom", "随机时间") : CampusMaps::TimeName(T == -2 ? CampusMaps::Get(SelectedMap()).DefaultTime : (ECampusTimeOfDay)T);
		return FText::Format(LOCTEXT("ChipFmt", "{0}  ·  {1}"), TimeOfDay, Weather);
	};
	TSharedRef<SHorizontalBox> MapRow = SNew(SHorizontalBox);
	for (const ECampusMap M : CampusMaps::Playable())
	{
		MapRow->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SCampusButton).SoundContext(Sound).FontSize(16).Height(38.f)
			.Text(CampusMaps::Get(M).Name)
			.IsSelected_Lambda([SelectedMap, M]() { return SelectedMap() == M; })
			.OnClicked_Lambda([this, M]()
			{
				if (UCampusGameInstance* GI = GetGI())
				{
					GI->EditProfile().SelectedMap = (int32)M;
					GI->SaveProfile();
				}
				RefreshMapCard();
			})
		];
	}
	TSharedRef<SVerticalBox> Settings = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 10.f)[ MapRow ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			MakeSettingRow(LOCTEXT("BotsLabel", "AI 补位"),
				SNew(SCampusSegmented).SoundContext(Sound)
				.Options({ LOCTEXT("BotsOn", "开启"), LOCTEXT("BotsOff", "关闭") })
				.Selected_Lambda([this]() { return bBots ? 0 : 1; })
				.OnSelect_Lambda([this](int32 I) { bBots = I == 0; }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			MakeSettingRow(LOCTEXT("SkillLabel", "AI 难度"),
				SNew(SCampusSegmented).SoundContext(Sound)
				.Options({ LOCTEXT("Recruit", "新兵"), LOCTEXT("Regular", "普通"), LOCTEXT("Hardened", "困难"), LOCTEXT("Veteran", "老兵") })
				.Selected_Lambda([this]() { const UCampusGameInstance* GI = GetGI(); return GI ? GI->GetProfile().BotSkill : 1; })
				.OnSelect_Lambda([this](int32 I) { if (UCampusGameInstance* GI = GetGI()) { GI->EditProfile().BotSkill = I; GI->SaveProfile(); } }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			MakeSettingRow(LOCTEXT("WeatherLabel", "天气"),
				SNew(SCampusSegmented).SoundContext(Sound)
				.Options({ LOCTEXT("WRandom", "随机"), CampusMaps::WeatherName(ECampusWeather::Clear), CampusMaps::WeatherName(ECampusWeather::Cloudy), CampusMaps::WeatherName(ECampusWeather::Overcast),
					CampusMaps::WeatherName(ECampusWeather::Rain), CampusMaps::WeatherName(ECampusWeather::Storm), CampusMaps::WeatherName(ECampusWeather::Fog) })
				.Selected_Lambda([this]() { const UCampusGameInstance* GI = GetGI(); return GI ? GI->GetProfile().SelectedWeather + 1 : 0; })
				.OnSelect_Lambda([this](int32 I) { if (UCampusGameInstance* GI = GetGI()) { GI->EditProfile().SelectedWeather = I - 1; GI->SaveProfile(); } RefreshMapCard(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 10.f)
		[
			MakeSettingRow(LOCTEXT("TimeLabel", "时间"),
				SNew(SCampusSegmented).SoundContext(Sound)
				.Options({ LOCTEXT("TDefault", "默认"), LOCTEXT("TRandom", "随机"), CampusMaps::TimeName(ECampusTimeOfDay::Morning), CampusMaps::TimeName(ECampusTimeOfDay::Noon),
					CampusMaps::TimeName(ECampusTimeOfDay::Afternoon), CampusMaps::TimeName(ECampusTimeOfDay::Dusk), CampusMaps::TimeName(ECampusTimeOfDay::Night) })
				.Selected_Lambda([this]() { const UCampusGameInstance* GI = GetGI(); return GI ? GI->GetProfile().SelectedTime + 2 : 0; })
				.OnSelect_Lambda([this](int32 I) { if (UCampusGameInstance* GI = GetGI()) { GI->EditProfile().SelectedTime = I - 2; GI->SaveProfile(); } RefreshMapCard(); }))
		];
	TSharedRef<SWidget> Join = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox).HeightOverride(40.f)
			[
				SAssignNew(JoinBox, SEditableTextBox)
				.Font(CampusUI::Font(EFont::Num, 18, TEXT("SemiBold"), 30))
				.HintText(LOCTEXT("JoinHint", "主机 IP，例如 192.168.1.20"))
				.Text_Lambda([this]() { const UCampusGameInstance* GI = GetGI(); return FText::FromString(GI ? GI->GetProfile().LastJoinAddress : TEXT("127.0.0.1")); })
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { if (UCampusGameInstance* GI = GetGI()) { GI->EditProfile().LastJoinAddress = Text.ToString(); } })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(132.f)
			[
				SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Join", "加入")).Height(40.f).FontSize(16)
				.OnClicked_Lambda([this]()
				{
					if (Owner.IsValid() && JoinBox.IsValid())
					{
						DeployMode = LOCTEXT("JoinMode", "加入局域网对局");
						DeployStart = Time;
						DeployTip = Tips[FMath::RandRange(0, (int32)UE_ARRAY_COUNT(Tips) - 1)];
						Page = EPage::Deploying;
						Owner->Join(JoinBox->GetText().ToString());
					}
				})
			]
		];
	TSharedRef<SVerticalBox> Intel = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Label(LOCTEXT("MapEye", "当前战场"), EFont::Sans, 12, TEXT("Medium"), CampusUI::TextDim()) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 3.f)
				[
					SNew(STextBlock).Text_Lambda([SelectedMap]() { return CampusMaps::Get(SelectedMap()).Name; })
					.Font(CampusUI::Font(EFont::Sans, 26, TEXT("Black"))).ColorAndOpacity(Col(CampusUI::TextMain()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text_Lambda(Conditions)
					.Font(CampusUI::Font(EFont::Sans, 13, TEXT("Medium"))).ColorAndOpacity(Col(CampusUI::TextDim()))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
			[
				SAssignNew(MapPreview, SCampusMapPreview).DesiredSize(FVector2D(148.f, 82.f))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("MatchSettings", "对局设置")).Height(34.f).FontSize(14)
				.SubText_Lambda([this]() { return bMatchSettingsOpen ? LOCTEXT("CollapseShort", "收起") : LOCTEXT("ExpandShort", "+"); })
				.IsSelected_Lambda([this]() { return bMatchSettingsOpen; })
				.OnClicked_Lambda([this]()
				{
					bMatchSettingsOpen = !bMatchSettingsOpen;
					bJoinOpen = false;
					if (PlayScroll.IsValid()) { if (bMatchSettingsOpen) PlayScroll->ScrollToEnd(); else PlayScroll->ScrollToStart(); }
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("JoinHeader", "局域网加入")).Height(34.f).FontSize(14)
				.SubText_Lambda([this]() { return bJoinOpen ? LOCTEXT("CollapseShort", "收起") : LOCTEXT("ExpandShort", "+"); })
				.IsSelected_Lambda([this]() { return bJoinOpen; })
				.OnClicked_Lambda([this]()
				{
					bJoinOpen = !bJoinOpen;
					bMatchSettingsOpen = false;
					if (bJoinOpen && PlayScroll.IsValid()) PlayScroll->ScrollToEnd();
					if (bJoinOpen && JoinBox.IsValid()) FSlateApplication::Get().SetKeyboardFocus(JoinBox);
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SBox).Visibility_Lambda([this]() { return bMatchSettingsOpen ? EVisibility::Visible : EVisibility::Collapsed; })[ Settings ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SBox).Visibility_Lambda([this]() { return bJoinOpen ? EVisibility::Visible : EVisibility::Collapsed; })[ Join ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("PlayLoadoutLink", "当前配装")).Height(32.f).FontSize(14)
			.SubText_Lambda([this]() { const UCampusGameInstance* GI = GetGI(); return GI ? FText::FromString(GI->GetActiveLoadout().Name) : FText::GetEmpty(); })
			.OnClicked_Lambda([this]() { SetPage(EPage::Loadouts); })
		];
	PlayScroll = MenuScroll(Intel);
	TSharedRef<SHorizontalBox> Modes = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.25f).Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Playlist).SoundContext(Sound).FontSize(34).Height(PlaylistHeight)
			.Image_Lambda([SelectedMap]() { return CampusUI::Brush(SelectedMap() == ECampusMap::Hotel ? CampusUI::ETex::MenuHotel : CampusUI::ETex::MenuCampus); })
			.Eyebrow(LOCTEXT("TDMEye", "TEAM DEATHMATCH"))
			.Text(LOCTEXT("PlayStart", "开始游戏"))
			.Tag(LOCTEXT("TDMTag", "5V5"))
			.SubText(LOCTEXT("TDMPlaylistSub", "团队死斗  ·  率先拿下 60 分"))
			.IsSelected(true)
			.OnClicked_Lambda([this]() { StartMatch(ECampusMatchMode::TeamDeathmatch, 5); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Playlist).SoundContext(Sound).FontSize(28).Height(PlaylistHeight)
			.Image(CampusUI::Brush(CampusUI::ETex::MenuGunfight))
			.Eyebrow(LOCTEXT("GFEye", "GUNFIGHT"))
			.Text(LOCTEXT("GF1", "枪战"))
			.Tag(LOCTEXT("GF1Tag", "1V1"))
			.SubText(LOCTEXT("GF1Sub", "先赢 6 回合 · 随机配装"))
			.OnClicked_Lambda([this]() { StartMatch(ECampusMatchMode::Gunfight, 1); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Playlist).SoundContext(Sound).FontSize(28).Height(PlaylistHeight)
			.Image(CampusUI::Brush(CampusUI::ETex::MenuHotel))
			.Eyebrow(LOCTEXT("GFEye2", "GUNFIGHT"))
			.Text(LOCTEXT("GF2", "枪战"))
			.Tag(LOCTEXT("GF2Tag", "2V2"))
			.SubText(LOCTEXT("GF2Sub", "先赢 6 回合 · 不回血"))
			.OnClicked_Lambda([this]() { StartMatch(ECampusMatchMode::Gunfight, 2); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Playlist).SoundContext(Sound).FontSize(28).Height(PlaylistHeight)
			.Image(CampusUI::Brush(CampusUI::ETex::MenuCampus))
			.Eyebrow(LOCTEXT("CustomEye", "CUSTOM MATCH"))
			.Text(LOCTEXT("Custom", "自定义对局"))
			.SubText(LOCTEXT("CustomSub", "自由规则"))
			.OnClicked_Lambda([this]() { SetPage(EPage::Custom); })
		];
	RefreshMapCard();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 18.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
			[
				SNew(SBox)
				.WidthOverride_Lambda([this]() { return FOptionalSize(bMatchSettingsOpen ? 700.f : 420.f); })
				.MaxDesiredHeight_Lambda([this]() { return FOptionalSize(FMath::Max(160.f, GetCachedGeometry().GetLocalSize().Y - PlayReservedHeight)); })
				[
					MenuPanel(PlayScroll.ToSharedRef(), FMargin(16.f, 14.f), true)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Label(LOCTEXT("DeployList", "快速部署"), EFont::Sans, 22, TEXT("Bold"), CampusUI::TextMain())
		]
		+ SVerticalBox::Slot().AutoHeight()[ Modes ];
}

TSharedRef<SWidget> SCampusMainMenu::BuildLoadoutPage()
{
	TWeakObjectPtr<UObject> Sound = SoundContext;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MenuHeader(LOCTEXT("LoadoutEye", "WEAPONS"), LOCTEXT("LoadoutTitle", "自定义配装"), LOCTEXT("LoadoutSub", "选择配装进行改装  ·  所有配件槽均可同时装配"))
		];
	for (int32 i = 0; i < 3; ++i)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Card).SoundContext(Sound).FontSize(23).Height(78.f)
			.Eyebrow_Lambda([this, i]()
			{
				const UCampusGameInstance* GI = GetGI();
				const bool bActive = GI && GI->GetProfile().ActiveLoadout == i;
				return FText::Format(LOCTEXT("LoadoutEyeFmt", "LOADOUT 0{0}{1}"), FText::AsNumber(i + 1), bActive ? LOCTEXT("InUseEye", "  ·  使用中") : FText::GetEmpty());
			})
			.Text_Lambda([this, i]()
			{
				const UCampusGameInstance* GI = GetGI();
				return FText::FromString(GI && GI->GetProfile().Loadouts.IsValidIndex(i) ? GI->GetProfile().Loadouts[i].Name : TEXT(""));
			})
			.SubText_Lambda([this, i]()
			{
				const FCampusWeaponLoadout* P = GetLoadoutWeapon(i, false);
				const FCampusWeaponLoadout* S = GetLoadoutWeapon(i, true);
				const FCampusWeaponDef* PD = P ? UCampusArmory::Get().FindWeapon(P->WeaponId) : nullptr;
				const FCampusWeaponDef* SD = S ? UCampusArmory::Get().FindWeapon(S->WeaponId) : nullptr;
				return FText::Format(LOCTEXT("LoadoutSubFmt", "{0}  +  {1}"), PD ? PD->DisplayName : FText::GetEmpty(), SD ? SD->DisplayName : FText::GetEmpty());
			})
			.Tag(FText::Format(LOCTEXT("LoadoutTagFmt", "0{0}"), FText::AsNumber(i + 1)))
			.IsSelected_Lambda([this, i]() { return EditLoadoutIndex == i; })
			.OnClicked_Lambda([this, i]()
			{
				EditLoadoutIndex = i;
				if (Owner.IsValid())
				{
					Owner->PreviewWeapon(EditedLoadout().Primary);
				}
			})
		];
	}
	List->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 10.f)[ CampusWidgets::Section(LOCTEXT("Actions", "操作   ACTIONS")) ];
	List->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("EditPrimary", "改装主武器")).SubText(LOCTEXT("EditPrimarySub", "枪匠"))
		.OnClicked_Lambda([this]() { bEditSecondary = false; SelectedSlot = INDEX_NONE; SetPage(EPage::Gunsmith); })
	];
	List->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("EditSecondary", "改装副武器")).SubText(LOCTEXT("EditSecondarySub", "枪匠"))
		.OnClicked_Lambda([this]() { bEditSecondary = true; SelectedSlot = INDEX_NONE; SetPage(EPage::Gunsmith); })
	];
	List->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("EditPerks", "技能与投掷物")).SubText(LOCTEXT("EditPerksSub", "三技能 · 致命 · 战术"))
		.OnClicked_Lambda([this]() { HoveredPerk = ECampusPerk::None; SetPage(EPage::Equipment); })
	];
	List->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Equip", "设为当前配装"))
		.IsSelected_Lambda([this]() { const UCampusGameInstance* GI = GetGI(); return GI && GI->GetProfile().ActiveLoadout == EditLoadoutIndex; })
		.OnClicked_Lambda([this]()
		{
			if (UCampusGameInstance* GI = GetGI())
			{
				GI->EditProfile().ActiveLoadout = EditLoadoutIndex;
				GI->SaveProfile();
				if (Owner.IsValid())
				{
					Owner->PreviewOperator(GI->GetProfile().OperatorIndex);
				}
			}
		})
	];

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(440.f)[ MenuPanel(MenuScroll(List), FMargin(18.f, 16.f), true) ] ]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				MenuPanel(MenuScroll(BuildLoadoutCard([this]() { return EditLoadoutIndex; }, true)))
			]
		];
}

float SCampusMainMenu::GetStatValue(int32 Index, bool bPreview) const
{
	const UCampusGameInstance* GI = GetGI();
	if (!GI)
	{
		return 0.f;
	}
	const FCampusLoadout& L = GI->GetProfile().Loadouts[FMath::Clamp(EditLoadoutIndex, 0, GI->GetProfile().Loadouts.Num() - 1)];
	FCampusWeaponLoadout W = bEditSecondary ? L.Secondary : L.Primary;
	const UCampusArmory& Armory = UCampusArmory::Get();
	if (bPreview && bHasHover && SelectedSlot != INDEX_NONE)
	{
		Armory.SetAttachment(W, HoveredAttachment, (ECampusAttachmentSlot)SelectedSlot);
	}
	return UCampusArmory::ToDisplay(Armory.ComputeStats(W)).Get(Index);
}

TSharedRef<SWidget> SCampusMainMenu::MakeStatRow(int32 Index)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(84.f)
			[
				Label(FCampusDisplayStats::Label(Index), EFont::Sans, 14, TEXT("Bold"), CampusUI::TextDim())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(BarWidth).HeightOverride(6.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SNew(SBorder).BorderImage(CampusUI::Rounded()).BorderBackgroundColor(Col(FLinearColor(1.f, 1.f, 1.f, 0.10f))) ]
				+ SOverlay::Slot().HAlign(HAlign_Left)
				[
					SNew(SBox)
					.WidthOverride_Lambda([this, Index]() { return FOptionalSize(BarWidth * FMath::Min(GetStatValue(Index, false), GetStatValue(Index, true))); })
					[
						SNew(SBorder).BorderImage(CampusUI::Rounded()).BorderBackgroundColor(Col(CampusUI::TextMain()))
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Left)
				.Padding(TAttribute<FMargin>::CreateLambda([this, Index]() { return FMargin(BarWidth * FMath::Min(GetStatValue(Index, false), GetStatValue(Index, true)), 0.f, 0.f, 0.f); }))
				[
					SNew(SBox)
					.WidthOverride_Lambda([this, Index]() { return FOptionalSize(BarWidth * FMath::Abs(GetStatValue(Index, true) - GetStatValue(Index, false))); })
					[
						SNew(SBorder).BorderImage(CampusUI::White())
						.BorderBackgroundColor_Lambda([this, Index]() { return Col(GetStatValue(Index, true) >= GetStatValue(Index, false) ? CampusUI::Positive() : CampusUI::Negative()); })
					]
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this, Index]() { return FText::AsNumber(FMath::RoundToInt(GetStatValue(Index, true) * 100.f)); })
			.Font(CampusUI::Font(EFont::Num, 17, TEXT("Bold")))
			.ColorAndOpacity_Lambda([this, Index]()
			{
				const float Delta = GetStatValue(Index, true) - GetStatValue(Index, false);
				return Col(FMath::Abs(Delta) < 0.005f ? CampusUI::TextMain() : (Delta > 0.f ? CampusUI::Positive() : CampusUI::Negative()));
			})
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildGunsmithPage()
{
	TWeakObjectPtr<UObject> Sound = SoundContext;
	TSharedRef<SVerticalBox> Stats = SNew(SVerticalBox);
	for (int32 i = 0; i < FCampusDisplayStats::Num; ++i)
	{
		Stats->AddSlot().AutoHeight().Padding(0.f, 3.f)[ MakeStatRow(i) ];
	}
	auto CurrentDef = [this]() -> const FCampusWeaponDef*
	{
		const UCampusGameInstance* GI = GetGI();
		if (!GI) return nullptr;
		const FCampusLoadout& L = GI->GetProfile().Loadouts[FMath::Clamp(EditLoadoutIndex, 0, 2)];
		return UCampusArmory::Get().FindWeapon((bEditSecondary ? L.Secondary : L.Primary).WeaponId);
	};

	TSharedRef<SVerticalBox> Info = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CampusWidgets::Eyebrow(TAttribute<FText>::CreateLambda([CurrentDef]() { const FCampusWeaponDef* Def = CurrentDef(); return Def ? CampusText::WeaponClassName(Def->Class) : FText::GetEmpty(); }), CampusUI::Accent())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(STextBlock)
			.Text_Lambda([CurrentDef]() { const FCampusWeaponDef* Def = CurrentDef(); return Def ? Def->DisplayName : FText::GetEmpty(); })
			.Font(CampusUI::Font(EFont::Cond, 34, TEXT("Black"), 20))
			.ColorAndOpacity(Col(CampusUI::TextMain()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				CampusWidgets::Chip(TAttribute<FText>::CreateLambda([this]()
				{
					const UCampusGameInstance* GI = GetGI();
					if (!GI) return FText::GetEmpty();
					const FCampusLoadout& L = GI->GetProfile().Loadouts[FMath::Clamp(EditLoadoutIndex, 0, 2)];
					const FCampusWeaponLoadout& W = bEditSecondary ? L.Secondary : L.Primary;
					return FText::Format(LOCTEXT("ClassCount", "配件  {0} / {1}"), FText::AsNumber(W.Attachments.Num()), FText::AsNumber(UCampusArmory::Get().CountModdableSlots(W.WeaponId)));
				}), CampusUI::Accent(), true)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				CampusWidgets::Chip(TAttribute<FText>::CreateLambda([CurrentDef]()
				{
					const FCampusWeaponDef* Def = CurrentDef();
					if (!Def) return FText::GetEmpty();
					TArray<FString> Modes;
					for (ECampusFireMode M : Def->FireModes) { Modes.Add(CampusText::FireModeName(M).ToString()); }
					return FText::FromString(FString::Join(Modes, TEXT(" / ")));
				}), CampusUI::TextDim())
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(STextBlock)
			.Text_Lambda([CurrentDef]() { const FCampusWeaponDef* Def = CurrentDef(); return Def ? Def->Description : FText::GetEmpty(); })
			.Font(CampusUI::Font(EFont::Sans, 13, TEXT("Medium")))
			.AutoWrapText(true)
			.ColorAndOpacity(Col(CampusUI::TextDim()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[ CampusWidgets::Section(LOCTEXT("StatsHeader", "性能   STATS")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Stats ];

	TSharedRef<SVerticalBox> Catalog = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[ CampusWidgets::Section(LOCTEXT("WeaponsHeader", "武器   WEAPON")) ]
		+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(WeaponListBox, SVerticalBox) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 20.f, 0.f, 0.f)[ Info ];
	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MenuHeader(LOCTEXT("GunsmithEye", "GUNSMITH"), LOCTEXT("Gunsmith", "枪匠"),
				TAttribute<FText>::CreateLambda([this]() { return bEditSecondary ? LOCTEXT("Secondary", "副武器改装") : LOCTEXT("Primary", "主武器改装"); }))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[ MenuScroll(Catalog) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("Back", "返回配装")).Height(36.f).FontSize(15)
			.OnClicked_Lambda([this]() { SetPage(EPage::Loadouts); })
		];
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(CampusMenuPresentation::CatalogWidth)[ MenuPanel(Left, FMargin(16.f, 14.f), true) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(CampusMenuPresentation::AttachmentGap, 98.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(CampusMenuPresentation::AttachmentWidth)
			.Visibility_Lambda([this]() { return SelectedSlot == INDEX_NONE ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				MenuPanel(MenuScroll(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(AttachmentListBox, SVerticalBox) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ SAssignNew(ProsConsBox, SVerticalBox) ]),
					FMargin(14.f, 14.f))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer).Visibility(EVisibility::HitTestInvisible) ];
}

TSharedRef<SWidget> SCampusMainMenu::BuildOperatorPage()
{
	TWeakObjectPtr<UObject> Sound = SoundContext;
	auto OperatorCard = [this, Sound](int32 Index, const FText& Eye, const FText& Name, const FText& Sub)
	{
		return SNew(SCampusButton).Style(ECampusButtonStyle::Card).SoundContext(Sound).FontSize(27).Height(90.f)
			.Eyebrow(Eye).Text(Name).SubText(Sub)
			.Tag(FText::Format(LOCTEXT("OpTag", "0{0}"), FText::AsNumber(Index + 1)))
			.IsSelected_Lambda([this, Index]() { const UCampusGameInstance* GI = GetGI(); return GI && GI->GetProfile().OperatorIndex == Index; })
			.OnClicked_Lambda([this, Index]()
			{
				if (UCampusGameInstance* GI = GetGI())
				{
					GI->EditProfile().OperatorIndex = Index;
					GI->SaveProfile();
				}
				if (Owner.IsValid())
				{
					Owner->PreviewOperator(Index);
				}
			});
	};
	auto Bio = [this]()
	{
		const UCampusGameInstance* GI = GetGI();
		const int32 Op = GI ? GI->GetProfile().OperatorIndex : 0;
		return Op % 2 == 0
			? LOCTEXT("Op0Bio", "代号「阿哲」。光華特勤队突击组长，擅长走廊与教室间的快速突破，习惯使用短枪管与扩容弹匣。")
			: LOCTEXT("Op1Bio", "代号「小雯」。光華特勤队侦察手，负责操场与顶楼的远程压制，偏好精确射手步枪与高倍镜。");
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(440.f)
			[
				MenuPanel(MenuScroll(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ MenuHeader(LOCTEXT("OperatorEye", "OPERATORS"), LOCTEXT("OperatorTitle", "干员"), LOCTEXT("OperatorSub", "选择出战形象  ·  第三人称与击杀回放中可见")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ OperatorCard(0, LOCTEXT("Op0Eye", "ASSAULT  ·  突击手"), LOCTEXT("Op0", "阿哲"), LOCTEXT("Op0Sub", "光華特勤队  ·  近距离突破")) ]
				+ SVerticalBox::Slot().AutoHeight()[ OperatorCard(1, LOCTEXT("Op1Eye", "RECON  ·  侦察手"), LOCTEXT("Op1", "小雯"), LOCTEXT("Op1Sub", "光華特勤队  ·  情报与远程支援")) ]), FMargin(18.f, 16.f), true)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SBox).WidthOverride(300.f)
			[
				MenuPanel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ CampusWidgets::Eyebrow(LOCTEXT("BioEye", "DOSSIER  ·  档案"), CampusUI::Accent()) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text_Lambda(Bio).Font(CampusUI::Font(EFont::Sans, 15, TEXT("Medium"))).AutoWrapText(true).ColorAndOpacity(Col(CampusUI::TextMain()))
					]
				)
			]
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildOptionsPage()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(700.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ MenuHeader(LOCTEXT("OptionsEye", "SETTINGS"), LOCTEXT("OptionsTitle", "设置"), LOCTEXT("OptionsSub", "所有修改立即生效并保存")) ]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					MenuPanel(SNew(SCampusOptionsPanel).GameInstance(GetGI()).SoundContext(SoundContext), FMargin(18.f, 16.f), true)
				]
			]
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildCustomPage()
{
	TWeakObjectPtr<UObject> Sound = SoundContext;
	const TArray<FText> OffOn = { LOCTEXT("COff", "关闭"), LOCTEXT("COn", "开启") };
	const TArray<int32> HealthSteps = { 50, 100, 150, 200, 300, 500, 1000 };
	TArray<FText> HealthLabels;
	for (int32 H : HealthSteps)
	{
		HealthLabels.Add(FText::AsNumber(H));
	}
	auto Save = [this]() { if (UCampusGameInstance* GI = GetGI()) { GI->SaveProfile(); } };
	auto Toggle = [this, Sound, OffOn, Save](const FText& Name, bool FCampusMatchRules::*Field)
	{
		return MakeSettingRow(Name,
			SNew(SCampusSegmented).SoundContext(Sound).Options(OffOn)
			.Selected_Lambda([this, Field]() { const FCampusMatchRules* R = EditRules(); return R && R->*Field ? 1 : 0; })
			.OnSelect_Lambda([this, Field, Save](int32 I) { if (FCampusMatchRules* R = EditRules()) { R->*Field = I == 1; Save(); } }));
	};
	auto Health = [this, Sound, HealthSteps, HealthLabels, Save](const FText& Name, int32 FCampusMatchRules::*Field)
	{
		return MakeSettingRow(Name,
			SNew(SCampusSegmented).SoundContext(Sound).Options(HealthLabels)
			.Selected_Lambda([this, Field, HealthSteps]()
			{
				const FCampusMatchRules* R = EditRules();
				int32 Best = 1;
				for (int32 i = 0; R && i < HealthSteps.Num(); ++i)
				{
					if (FMath::Abs(HealthSteps[i] - R->*Field) < FMath::Abs(HealthSteps[Best] - R->*Field)) Best = i;
				}
				return Best;
			})
			.OnSelect_Lambda([this, Field, HealthSteps, Save](int32 I) { if (FCampusMatchRules* R = EditRules()) { R->*Field = HealthSteps[I]; Save(); } }));
	};
	auto ProfileChoice = [this, Sound, Save](const FText& Name, const TArray<FText>& Options, TFunction<int32(const FCampusProfile&)> Get, TFunction<void(FCampusProfile&, int32)> Set)
	{
		return MakeSettingRow(Name,
			SNew(SCampusSegmented).SoundContext(Sound).Options(Options)
			.Selected_Lambda([this, Get]() { const UCampusGameInstance* GI = GetGI(); return GI ? Get(GI->GetProfile()) : 0; })
			.OnSelect_Lambda([this, Set, Save](int32 I) { if (UCampusGameInstance* GI = GetGI()) { Set(GI->EditProfile(), I); Save(); } }));
	};

	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MenuHeader(LOCTEXT("CustomPageEye", "CUSTOM MATCH"), LOCTEXT("CustomTitle", "自定义对局"), LOCTEXT("CustomPageSub", "自由设定规则  ·  设置自动保存  ·  仅对自定义对局生效"), 40.f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[ CampusWidgets::Section(LOCTEXT("CustomModeSec", "模式   MODE")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
		[
			ProfileChoice(LOCTEXT("CMode", "游戏模式"), { LOCTEXT("CTDM", "团队死斗"), LOCTEXT("CGF", "枪战") },
				[](const FCampusProfile& P) { return FMath::Clamp(P.CustomMode, 0, 1); }, [](FCampusProfile& P, int32 I) { P.CustomMode = I; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
		[
			ProfileChoice(LOCTEXT("CTeam", "每队人数"), { FText::AsNumber(1), FText::AsNumber(2), FText::AsNumber(3), FText::AsNumber(4), FText::AsNumber(5), FText::AsNumber(6) },
				[](const FCampusProfile& P) { return FMath::Clamp(P.CustomTeamSize, 1, 6) - 1; }, [](FCampusProfile& P, int32 I) { P.CustomTeamSize = I + 1; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
		[
			ProfileChoice(LOCTEXT("CBots", "AI 补位"), { LOCTEXT("CBotsOn", "开启"), LOCTEXT("CBotsOff", "关闭") },
				[](const FCampusProfile& P) { return P.bCustomBots ? 0 : 1; }, [](FCampusProfile& P, int32 I) { P.bCustomBots = I == 0; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
		[
			ProfileChoice(LOCTEXT("CSkill", "AI 难度"), { LOCTEXT("CRecruit", "新兵"), LOCTEXT("CRegular", "普通"), LOCTEXT("CHardened", "困难"), LOCTEXT("CVeteran", "老兵") },
				[](const FCampusProfile& P) { return FMath::Clamp(P.BotSkill, 0, 3); }, [](FCampusProfile& P, int32 I) { P.BotSkill = I; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 6.f)[ CampusWidgets::Section(LOCTEXT("CustomRulesSec", "规则   RULES")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Toggle(LOCTEXT("CGod", "无敌（仅玩家）"), &FCampusMatchRules::bGodMode) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Health(LOCTEXT("CHP", "玩家血量"), &FCampusMatchRules::PlayerHealth) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Health(LOCTEXT("CBotHP", "AI 血量"), &FCampusMatchRules::BotHealth) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Toggle(LOCTEXT("CNoReload", "无需换弹"), &FCampusMatchRules::bNoReload) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Toggle(LOCTEXT("CInfAmmo", "无限备弹"), &FCampusMatchRules::bInfiniteAmmo) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Toggle(LOCTEXT("CInfGear", "无限投掷物"), &FCampusMatchRules::bInfiniteEquipment) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ Toggle(LOCTEXT("CRegen", "自动回血"), &FCampusMatchRules::bHealthRegen) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("CStart", "开始自定义对局")).Height(48.f).FontSize(19)
				.OnClicked_Lambda([this]()
				{
					const UCampusGameInstance* GI = GetGI();
					if (!GI) return;
					const FCampusProfile& P = GI->GetProfile();
					StartMatch(P.CustomMode == 1 ? ECampusMatchMode::Gunfight : ECampusMatchMode::TeamDeathmatch, FMath::Clamp(P.CustomTeamSize, 1, 6), true);
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(160.f)
				[
					SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("CBack", "返回")).Height(48.f).FontSize(17)
					.OnClicked_Lambda([this]() { SetPage(EPage::Play); })
				]
			]
		];

	auto Summary = [this]()
	{
		const UCampusGameInstance* GI = GetGI();
		if (!GI) return FText::GetEmpty();
		const FCampusMatchRules& R = GI->GetProfile().CustomRules;
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("玩家  %d HP%s"), R.PlayerHealth, R.bGodMode ? TEXT("  ·  无敌") : TEXT("")));
		Lines.Add(FString::Printf(TEXT("AI  %d HP"), R.BotHealth));
		Lines.Add(R.bNoReload ? TEXT("弹匣永不打空") : (R.bInfiniteAmmo ? TEXT("换弹不消耗备弹") : TEXT("标准弹药")));
		if (R.bInfiniteEquipment) Lines.Add(TEXT("投掷物无限"));
		Lines.Add(R.bHealthRegen ? TEXT("自动回血") : TEXT("不回血"));
		return FText::FromString(FString::Join(Lines, TEXT("\n")));
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(700.f)[ MenuPanel(MenuScroll(Left), FMargin(18.f, 16.f), true) ] ]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SBox).WidthOverride(280.f)
			[
				MenuPanel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ CampusWidgets::Eyebrow(LOCTEXT("CSumEye", "RULESET  ·  规则摘要"), CampusUI::Accent()) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text_Lambda(Summary).Font(CampusUI::Font(EFont::Sans, 18, TEXT("Bold"))).ColorAndOpacity(Col(CampusUI::TextMain()))
					])
			]
		];
}

TSharedRef<SWidget> SCampusMainMenu::MakePerkRow(int32 Slot, ECampusPerk Perk)
{
	const FCampusPerkInfo& Info = CampusPerks::Info(Perk);
	const FLinearColor Color = CampusPerks::GroupColor(Info.Group);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(5.f).HeightOverride(48.f)
			[
				SNew(SBorder).BorderImage(CampusUI::White()).BorderBackgroundColor(Col(Color))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SCampusButton).SoundContext(SoundContext).Height(48.f).FontSize(17)
			.Text(Info.Name)
			.SubText(CampusPerks::GroupName(Info.Group))
			.IsSelected_Lambda([this, Slot, Perk]() { const FCampusLoadout& L = EditedLoadout(); return L.Perks.IsValidIndex(Slot) && L.Perks[Slot] == Perk; })
			.OnHovered_Lambda([this, Perk]() { HoveredPerk = Perk; })
			.OnClicked_Lambda([this, Slot, Perk]()
			{
				FCampusLoadout& L = EditedLoadout();
				CampusPerks::Sanitize(L.Perks);
				L.Perks[Slot] = Perk;
				HoveredPerk = Perk;
				if (UCampusGameInstance* GI = GetGI()) { GI->SaveProfile(); }
			})
		];
}

TSharedRef<SWidget> SCampusMainMenu::BuildEquipmentPage()
{
	TWeakObjectPtr<UObject> Sound = SoundContext;
	auto Save = [this]() { if (UCampusGameInstance* GI = GetGI()) { GI->SaveProfile(); } };
	auto ThrowChoice = [this, Sound, Save](bool bLethal)
	{
		const ECampusThrowable A = bLethal ? ECampusThrowable::Frag : ECampusThrowable::Flash;
		const ECampusThrowable B = bLethal ? ECampusThrowable::Thermite : ECampusThrowable::Smoke;
		return SNew(SCampusSegmented).SoundContext(Sound)
			.Options({ CampusText::ThrowableName(A), CampusText::ThrowableName(B) })
			.Selected_Lambda([this, bLethal, B]() { const FCampusLoadout& L = EditedLoadout(); return (bLethal ? L.Lethal : L.Tactical) == B ? 1 : 0; })
			.OnSelect_Lambda([this, bLethal, A, B, Save](int32 I) { FCampusLoadout& L = EditedLoadout(); (bLethal ? L.Lethal : L.Tactical) = I == 1 ? B : A; Save(); });
	};

	TSharedRef<SHorizontalBox> PerkColumns = SNew(SHorizontalBox);
	for (int32 Slot = 0; Slot < CampusPerks::NumSlots; ++Slot)
	{
		TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 0.f, 0.f, 6.f)
			[
				Label(FText::Format(LOCTEXT("PerkSlot", "技能 {0}"), FText::AsNumber(Slot + 1)), EFont::Sans, 18, TEXT("Black"), CampusUI::TextMain())
			];
		for (ECampusPerk Perk : CampusPerks::PerksForSlot(Slot))
		{
			Column->AddSlot().AutoHeight().Padding(0.f, 2.f)[ MakePerkRow(Slot, Perk) ];
		}
		PerkColumns->AddSlot().FillWidth(1.f).Padding(Slot == 0 ? 0.f : 10.f, 0.f, 0.f, 0.f)[ Column ];
	}

	TSharedRef<SHorizontalBox> Legend = SNew(SHorizontalBox);
	for (int32 G = 0; G < (int32)ECampusPerkGroup::Count; ++G)
	{
		Legend->AddSlot().AutoWidth().Padding(0.f, 0.f, 14.f, 0.f)
		[
			CampusWidgets::Chip(CampusPerks::GroupName((ECampusPerkGroup)G), CampusPerks::GroupColor((ECampusPerkGroup)G))
		];
	}

	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MenuHeader(LOCTEXT("EquipEye", "PERKS & EQUIPMENT"), LOCTEXT("EquipTitle", "技能与投掷物"),
				TAttribute<FText>::CreateLambda([this]() { const FCampusLoadout& L = EditedLoadout(); return FText::Format(LOCTEXT("EquipSub", "{0}  ·  三个技能同组即可获得专精加成"), FText::FromString(L.Name)); }), 40.f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[ CampusWidgets::Section(LOCTEXT("EquipThrowSec", "投掷物   EQUIPMENT")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ MakeSettingRow(FText::Format(LOCTEXT("LethalRow", "致命投掷物  [{0}]"), UCampusInputConfig::Get()->GetKeyLabel(TEXT("Lethal"))), ThrowChoice(true)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[ MakeSettingRow(FText::Format(LOCTEXT("TacticalRow", "战术投掷物  [{0}]"), UCampusInputConfig::Get()->GetKeyLabel(TEXT("Tactical"))), ThrowChoice(false)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 6.f)[ CampusWidgets::Section(LOCTEXT("EquipPerkSec", "技能   PERKS")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ Legend ]
		+ SVerticalBox::Slot().AutoHeight()[ PerkColumns ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(220.f).HAlign(HAlign_Left)
			[
				SNew(SBox).WidthOverride(220.f)
				[
					SNew(SCampusButton).SoundContext(Sound).Text(LOCTEXT("EquipBack", "返回配装")).Height(42.f).FontSize(16)
					.OnClicked_Lambda([this]() { SetPage(EPage::Loadouts); })
				]
			]
		];

	auto SpecialtyGroup = [this]() { return CampusPerks::GetSpecialty(EditedLoadout().Perks); };
	auto Focus = [this]() -> ECampusPerk
	{
		const FCampusLoadout& L = EditedLoadout();
		return HoveredPerk != ECampusPerk::None ? HoveredPerk : (L.Perks.Num() > 0 ? L.Perks[0] : ECampusPerk::None);
	};
	TSharedRef<SVerticalBox> Picked = SNew(SVerticalBox);
	for (int32 Slot = 0; Slot < CampusPerks::NumSlots; ++Slot)
	{
		auto PerkAt = [this, Slot]() { const FCampusLoadout& L = EditedLoadout(); return L.Perks.IsValidIndex(Slot) ? L.Perks[Slot] : ECampusPerk::None; };
		Picked->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
				[
					SNew(SBorder).BorderImage(CampusUI::Rounded())
					.BorderBackgroundColor_Lambda([PerkAt]() { return Col(CampusPerks::GroupColor(CampusPerks::Info(PerkAt()).Group)); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([PerkAt]() { return CampusPerks::Info(PerkAt()).Name; })
				.Font(CampusUI::Font(EFont::Sans, 19, TEXT("Bold"))).ColorAndOpacity(Col(CampusUI::TextMain()))
			]
		];
	}
	TSharedRef<SVerticalBox> Card = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ CampusWidgets::Eyebrow(LOCTEXT("EquipCardEye", "当前技能"), CampusUI::Accent()) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 12.f)[ Picked ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(CampusUI::Rounded()).Padding(FMargin(12.f, 10.f))
			.BorderBackgroundColor_Lambda([SpecialtyGroup]()
			{
				const ECampusPerkGroup G = SpecialtyGroup();
				return Col(G == ECampusPerkGroup::Count ? FLinearColor(1.f, 1.f, 1.f, 0.06f) : CampusUI::Alpha(CampusPerks::GroupColor(G), 0.22f));
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text_Lambda([SpecialtyGroup]() { return CampusPerks::SpecialtyName(SpecialtyGroup()); })
					.Font(CampusUI::Font(EFont::Sans, 18, TEXT("Black")))
					.ColorAndOpacity_Lambda([SpecialtyGroup]() { const ECampusPerkGroup G = SpecialtyGroup(); return Col(G == ECampusPerkGroup::Count ? CampusUI::TextDim() : CampusPerks::GroupColor(G)); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text_Lambda([SpecialtyGroup]() { return CampusPerks::SpecialtyDescription(SpecialtyGroup()); })
					.Font(CampusUI::Font(EFont::Sans, 14, TEXT("Medium"))).AutoWrapText(true).ColorAndOpacity(Col(CampusUI::TextMain()))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 4.f)
		[
			SNew(STextBlock).Text_Lambda([Focus]() { return CampusPerks::Info(Focus()).Name; })
			.Font(CampusUI::Font(EFont::Sans, 20, TEXT("Black")))
			.ColorAndOpacity_Lambda([Focus]() { return Col(CampusPerks::GroupColor(CampusPerks::Info(Focus()).Group)); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text_Lambda([Focus]() { return CampusPerks::Info(Focus()).Description; })
			.Font(CampusUI::Font(EFont::Sans, 15, TEXT("Medium"))).AutoWrapText(true).ColorAndOpacity(Col(CampusUI::TextDim()))
		];

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(800.f)[ MenuPanel(MenuScroll(Left), FMargin(18.f, 16.f), true) ] ]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SBox).WidthOverride(300.f)[ MenuPanel(MenuScroll(Card)) ]
		];
}

// ------------------------------------------------------------------ gunsmith logic

void SCampusMainMenu::RefreshGunsmith()
{
	if (!WeaponListBox.IsValid() || !GetGI())
	{
		return;
	}
	const UCampusArmory& Armory = UCampusArmory::Get();
	TWeakObjectPtr<UObject> Sound = SoundContext;

	// Two columns: eight weapons fit in four rows, leaving the stats panel room on a 16:9 screen.
	WeaponListBox->ClearChildren();
	const TArray<const FCampusWeaponDef*> Weapons = Armory.GetWeapons(bEditSecondary);
	for (int32 Row = 0; Row < Weapons.Num(); Row += 2)
	{
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 i = Row; i < Row + 2; ++i)
		{
			if (!Weapons.IsValidIndex(i))
			{
				Line->AddSlot().FillWidth(1.f)[ SNew(SSpacer) ];
				continue;
			}
			const FName Id = Weapons[i]->Id;
			Line->AddSlot().FillWidth(1.f).Padding(i == Row ? 0.f : 3.f, 0.f, i == Row ? 3.f : 0.f, 0.f)
			[
				SNew(SCampusButton).SoundContext(Sound).Height(36.f).FontSize(15)
				.Text(Weapons[i]->DisplayName)
				.ToolTipText(CampusText::WeaponClassName(Weapons[i]->Class))
				.IsSelected_Lambda([this, Id]() { return GetGI() && EditedWeapon().WeaponId == Id; })
				.OnClicked_Lambda([this, Id]() { SelectWeapon(Id); })
			];
		}
		WeaponListBox->AddSlot().AutoHeight().Padding(0.f, 2.f)[ Line ];
	}

	const FCampusWeaponDef* Def = Armory.FindWeapon(EditedWeapon().WeaponId);
	TArray<ECampusAttachmentSlot> Slots;
	for (ECampusAttachmentSlot Slot : TEnumRange<ECampusAttachmentSlot>())
	{
		if (Def && Def->Anchors.Contains(Slot) && Armory.GetAttachmentsForSlot(Def->Id, Slot).Num() > 0)
		{
			Slots.Add(Slot);
		}
	}
	if (GunsmithLabels.IsValid())
	{
		GunsmithLabels->SetSlots(Slots,
			[this](ECampusAttachmentSlot Slot)
			{
				const FCampusAttachmentDef* A = UCampusArmory::Get().FindAttachment(UCampusArmory::Get().GetAttachmentInSlot(EditedWeapon(), Slot));
				return A ? A->DisplayName : FText::GetEmpty();
			},
			[this](ECampusAttachmentSlot Slot) { SelectSlot((int32)Slot); });
	}

	AttachmentListBox->ClearChildren();
	if (SelectedSlot != INDEX_NONE && Def)
	{
		const ECampusAttachmentSlot Slot = (ECampusAttachmentSlot)SelectedSlot;
		AttachmentListBox->AddSlot().AutoHeight().Padding(4.f, 0.f, 0.f, 4.f)[ CampusWidgets::Eyebrow(LOCTEXT("AttachEye", "SELECT ATTACHMENT"), CampusUI::Accent()) ];
		AttachmentListBox->AddSlot().AutoHeight().Padding(4.f, 0.f, 0.f, 10.f)[ Label(CampusText::SlotName(Slot), EFont::Sans, 26, TEXT("Black"), CampusUI::TextMain()) ];
		auto AddOption = [this, Sound, Slot](FName Id, const FText& Name)
		{
			AttachmentListBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SCampusButton).SoundContext(Sound).Height(44.f).FontSize(16)
				.Text(Name)
				.SubText_Lambda([this, Id, Slot]() { return UCampusArmory::Get().GetAttachmentInSlot(EditedWeapon(), Slot) == Id ? LOCTEXT("Equipped", "已装备") : FText::GetEmpty(); })
				.IsSelected_Lambda([this, Id, Slot]() { return UCampusArmory::Get().GetAttachmentInSlot(EditedWeapon(), Slot) == Id; })
				.OnHovered_Lambda([this, Id]() { HoveredAttachment = Id; bHasHover = true; RefreshProsCons(); })
				.OnClicked_Lambda([this, Id]() { EquipAttachment(Id); })
			];
		};
		AddOption(NAME_None, LOCTEXT("None", "无"));
		for (const FCampusAttachmentDef* A : Armory.GetAttachmentsForSlot(Def->Id, Slot))
		{
			AddOption(A->Id, A->DisplayName);
		}
	}
	RefreshProsCons();
}

void SCampusMainMenu::RefreshProsCons()
{
	if (!ProsConsBox.IsValid())
	{
		return;
	}
	ProsConsBox->ClearChildren();
	if (!bHasHover || SelectedSlot == INDEX_NONE)
	{
		return;
	}
	const FCampusAttachmentDef* A = UCampusArmory::Get().FindAttachment(HoveredAttachment);
	if (!A)
	{
		return;
	}
	TArray<FText> Pros;
	TArray<FText> Cons;
	UCampusArmory::DescribeModifiers(*A, Pros, Cons);
	ProsConsBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[ CampusWidgets::Section(A->DisplayName) ];
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	TSharedRef<SVerticalBox> ProsCol = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> ConsCol = SNew(SVerticalBox);
	for (const FText& P : Pros)
	{
		TSharedRef<STextBlock> Description = Label(FText::Format(LOCTEXT("Pro", "▲  {0}"), P), EFont::Sans, 14, TEXT("Bold"), CampusUI::Positive());
		Description->SetAutoWrapText(true);
		ProsCol->AddSlot().AutoHeight().Padding(0.f, 1.f)[ Description ];
	}
	for (const FText& C : Cons)
	{
		TSharedRef<STextBlock> Description = Label(FText::Format(LOCTEXT("Con", "▼  {0}"), C), EFont::Sans, 14, TEXT("Bold"), CampusUI::Negative());
		Description->SetAutoWrapText(true);
		ConsCol->AddSlot().AutoHeight().Padding(0.f, 1.f)[ Description ];
	}
	if (!Pros.IsEmpty()) Row->AddSlot().FillWidth(1.f)[ ProsCol ];
	if (!Cons.IsEmpty()) Row->AddSlot().FillWidth(1.f)[ ConsCol ];
	ProsConsBox->AddSlot().AutoHeight()[ Row ];
}

void SCampusMainMenu::RefreshChips()
{
}

void SCampusMainMenu::SelectSlot(int32 SlotIndex)
{
	SelectedSlot = SlotIndex;
	bHasHover = false;
	RefreshGunsmith();
	if (Owner.IsValid())
	{
		Owner->GoToShot(ECampusMenuShot::GunsmithSlot, (ECampusAttachmentSlot)SlotIndex);
	}
}

void SCampusMainMenu::SelectWeapon(FName WeaponId)
{
	FCampusWeaponLoadout& W = EditedWeapon();
	if (W.WeaponId == WeaponId)
	{
		return;
	}
	W.WeaponId = WeaponId;
	W.Attachments.Reset();
	SelectedSlot = INDEX_NONE;
	bHasHover = false;
	SaveAndPreview();
	RefreshGunsmith();
	if (Owner.IsValid())
	{
		Owner->GoToShot(ECampusMenuShot::Gunsmith);
	}
}

void SCampusMainMenu::EquipAttachment(FName Id)
{
	if (SelectedSlot == INDEX_NONE)
	{
		return;
	}
	UCampusArmory::Get().SetAttachment(EditedWeapon(), Id, (ECampusAttachmentSlot)SelectedSlot);
	CampusUI::PlaySound(SoundContext, TEXT("Reload_MagIn"), 0.5f);
	SaveAndPreview();
	RefreshGunsmith();
	if (Owner.IsValid())
	{
		Owner->GoToShot(ECampusMenuShot::GunsmithSlot, (ECampusAttachmentSlot)SelectedSlot, 0.45f);
	}
}

void SCampusMainMenu::SaveAndPreview()
{
	if (UCampusGameInstance* GI = GetGI())
	{
		UCampusArmory::Get().Sanitize(EditedWeapon(), bEditSecondary);
		GI->SaveProfile();
	}
	if (Owner.IsValid())
	{
		Owner->PreviewWeapon(EditedWeapon());
		if (GetGI())
		{
			Owner->PreviewOperator(GetGI()->GetProfile().OperatorIndex);
		}
	}
}

// ------------------------------------------------------------------ navigation

void SCampusMainMenu::SetPage(EPage NewPage)
{
	if (Page == EPage::Deploying)
	{
		return;
	}
	const bool bChanged = NewPage != Page;
	Page = NewPage;
	if (bChanged)
	{
		PageAnim = 0.f;
	}
	if (Switcher.IsValid())
	{
		Switcher->SetActiveWidgetIndex((int32)Page);
	}
	if (!Owner.IsValid())
	{
		return;
	}
	switch (Page)
	{
	case EPage::Play:
	case EPage::Custom:
		Owner->GoToShot(ECampusMenuShot::Lobby);
		break;
	case EPage::Equipment:
		Owner->PreviewWeapon(EditedLoadout().Primary);
		Owner->GoToShot(ECampusMenuShot::Operator);
		break;
	case EPage::Loadouts:
		Owner->PreviewWeapon(EditedLoadout().Primary);
		Owner->GoToShot(ECampusMenuShot::Loadouts);
		break;
	case EPage::Gunsmith:
		Owner->PreviewWeapon(EditedWeapon());
		RefreshGunsmith();
		Owner->GoToShot(ECampusMenuShot::Gunsmith);
		break;
	case EPage::Operator:
		Owner->GoToShot(ECampusMenuShot::Operator);
		break;
	case EPage::Options:
		Owner->GoToShot(ECampusMenuShot::Options);
		break;
	default:
		break;
	}
}

void SCampusMainMenu::DismissTitle()
{
	if (Page != EPage::Title)
	{
		return;
	}
	CampusUI::PlaySound(SoundContext, TEXT("UI_Select"), 0.8f);
	Page = EPage::Play;
	PageAnim = -0.6f;
	if (Switcher.IsValid())
	{
		Switcher->SetActiveWidgetIndex((int32)EPage::Play);
	}
	if (Owner.IsValid())
	{
		Owner->GoToShot(ECampusMenuShot::Lobby);
	}
}

void SCampusMainMenu::DebugGoTo(const FString& PageName, int32 Slot)
{
	if (Page == EPage::Title && PageName != TEXT("title"))
	{
		DismissTitle();
	}
	if (PageName == TEXT("play")) SetPage(EPage::Play);
	else if (PageName == TEXT("playsettings")) { bMatchSettingsOpen = true; bJoinOpen = false; SetPage(EPage::Play); if (PlayScroll.IsValid()) PlayScroll->ScrollToEnd(); }
	else if (PageName == TEXT("playjoin")) { bMatchSettingsOpen = false; bJoinOpen = true; SetPage(EPage::Play); if (PlayScroll.IsValid()) PlayScroll->ScrollToEnd(); }
	else if (PageName == TEXT("loadouts")) SetPage(EPage::Loadouts);
	else if (PageName == TEXT("gunsmith")) { bEditSecondary = false; SetPage(EPage::Gunsmith); }
	else if (PageName == TEXT("gunsmith2")) { bEditSecondary = true; SetPage(EPage::Gunsmith); }
	else if (PageName.StartsWith(TEXT("weapon:")) && Page == EPage::Gunsmith) { SelectWeapon(FName(*PageName.Mid(7))); }
	else if (PageName.StartsWith(TEXT("turn:")))
	{
		// Automation: a mouse drag of X,Y pixels over the displayed weapon.
		FString DX, DY;
		if (PageName.Mid(5).Split(TEXT(","), &DX, &DY) && Owner.IsValid())
		{
			Owner->TurnShownWeapon(FVector2D(FCString::Atof(*DX), FCString::Atof(*DY)));
		}
		return;
	}
	else if (PageName.StartsWith(TEXT("attach:")) && Page == EPage::Gunsmith)
	{
		// Automation: fit an attachment directly (its own slot), then show the whole weapon.
		if (const FCampusAttachmentDef* A = UCampusArmory::Get().FindAttachment(FName(*PageName.Mid(7))))
		{
			UCampusArmory::Get().SetAttachment(EditedWeapon(), A->Id, A->Slot);
			SaveAndPreview();
			RefreshGunsmith();
		}
		return;
	}
	else if (PageName == TEXT("strip") && Page == EPage::Gunsmith)
	{
		EditedWeapon().Attachments.Reset();
		SaveAndPreview();
		RefreshGunsmith();
		return;
	}
	else if (PageName == TEXT("operator")) SetPage(EPage::Operator);
	else if (PageName == TEXT("options")) SetPage(EPage::Options);
	else if (PageName == TEXT("custom")) SetPage(EPage::Custom);
	else if (PageName == TEXT("equipment")) SetPage(EPage::Equipment);
	else if (PageName == TEXT("deploycustom")) { const UCampusGameInstance* GI = GetGI(); if (GI) StartMatch(GI->GetProfile().CustomMode == 1 ? ECampusMatchMode::Gunfight : ECampusMatchMode::TeamDeathmatch, GI->GetProfile().CustomTeamSize, true); }
	else if (PageName == TEXT("deploy")) StartMatch(ECampusMatchMode::TeamDeathmatch, 5);
	if (Page == EPage::Gunsmith && Slot >= 0 && Slot < (int32)ECampusAttachmentSlot::Count)
	{
		SelectSlot(Slot);
		const TArray<const FCampusAttachmentDef*> Options = UCampusArmory::Get().GetAttachmentsForSlot(EditedWeapon().WeaponId, (ECampusAttachmentSlot)Slot);
		if (Options.Num() > 0)
		{
			HoveredAttachment = Options.Last()->Id;
			bHasHover = true;
			RefreshProsCons();
		}
	}
}

void SCampusMainMenu::CycleTab(int32 Direction)
{
	const EPage Order[4] = { EPage::Play, EPage::Loadouts, EPage::Operator, EPage::Options };
	int32 Index = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		if (Order[i] == Page || ((Page == EPage::Gunsmith || Page == EPage::Equipment) && Order[i] == EPage::Loadouts) || (Page == EPage::Custom && Order[i] == EPage::Play))
		{
			Index = i;
		}
	}
	SetPage(Order[(Index + Direction + 4) % 4]);
	CampusUI::PlaySound(SoundContext, TEXT("UI_Tab"), 0.5f);
}

void SCampusMainMenu::StartMatch(ECampusMatchMode Mode, int32 TeamSize, bool bCustom)
{
	if (!Owner.IsValid() || Page == EPage::Deploying)
	{
		return;
	}
	const UCampusGameInstance* Profile = GetGI();
	const ECampusMap Map = Profile ? (ECampusMap)FMath::Clamp(Profile->GetProfile().SelectedMap, 0, (int32)ECampusMap::Count - 1) : ECampusMap::Campus;
	DeployMap = Map;
	DeployMode = Mode == ECampusMatchMode::Gunfight
		? FText::Format(LOCTEXT("DeployGF", "枪战  ·  {0}V{0}  ·  {1}"), FText::AsNumber(TeamSize), CampusMaps::Get(Map).Name)
		: FText::Format(LOCTEXT("DeployTDM", "团队死斗  ·  {0}V{0}  ·  {1}"), FText::AsNumber(TeamSize), CampusMaps::Get(Map).Name);
	if (bCustom)
	{
		DeployMode = FText::Format(LOCTEXT("DeployCustom", "自定义对局  ·  {0}"), DeployMode);
	}
	DeployTip = Tips[FMath::RandRange(0, (int32)UE_ARRAY_COUNT(Tips) - 1)];
	DeployStart = Time;
	Page = EPage::Deploying;
	const UCampusGameInstance* GI = GetGI();
	Owner->Deploy(Mode, TeamSize, bCustom ? (GI && GI->GetProfile().bCustomBots) : bBots, bCustom);
}

void SCampusMainMenu::RefreshMapCard()
{
	if (!MapPreview.IsValid())
	{
		return;
	}
	const UCampusGameInstance* GI = GetGI();
	const ECampusMap Map = GI ? (ECampusMap)FMath::Clamp(GI->GetProfile().SelectedMap, 0, (int32)ECampusMap::Count - 1) : ECampusMap::Campus;
	const UCampusMapCatalog* Catalog = UCampusMapCatalog::Get();
	const FCampusMapPreviewData* Data = Catalog ? Catalog->Find(Map) : nullptr;
	MapPreview->SetRects(Data ? Data->Rects : TArray<FCampusMapRect>());
}

FCampusMatchRules* SCampusMainMenu::EditRules()
{
	UCampusGameInstance* GI = GetGI();
	return GI ? &GI->EditProfile().CustomRules : nullptr;
}

FReply SCampusMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Page == EPage::Title)
	{
		DismissTitle();
		return FReply::Handled();
	}
	if (Page == EPage::Deploying)
	{
		return FReply::Handled();
	}
	if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)
	{
		CycleTab(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder)
	{
		CycleTab(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (Page == EPage::Play)
		{
			bMatchSettingsOpen = false;
			bJoinOpen = false;
			if (PlayScroll.IsValid()) PlayScroll->ScrollToStart();
		}
		else if (Page == EPage::Gunsmith)
		{
			if (SelectedSlot != INDEX_NONE)
			{
				SelectedSlot = INDEX_NONE;
				bHasHover = false;
				RefreshGunsmith();
				Owner->GoToShot(ECampusMenuShot::Gunsmith);
			}
			else
			{
				SetPage(EPage::Loadouts);
			}
		}
		else if (Page == EPage::Equipment)
		{
			SetPage(EPage::Loadouts);
		}
		else if (Page != EPage::Play)
		{
			SetPage(EPage::Play);
		}
		CampusUI::PlaySound(SoundContext, TEXT("UI_Back"), 0.6f);
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SCampusMainMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Page == EPage::Title)
	{
		DismissTitle();
		return FReply::Handled();
	}
	// Clicks the buttons and slot labels did not take: on the weapon pages, holding the left button turns the weapon.
	if ((Page == EPage::Gunsmith || Page == EPage::Loadouts) && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bTurningWeapon = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SCampusMainMenu::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bTurningWeapon && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bTurningWeapon = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SCampusMainMenu::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bTurningWeapon && HasMouseCapture())
	{
		if (ACampusMenuPlayerController* PC = Owner.Get())
		{
			PC->TurnShownWeapon(MouseEvent.GetCursorDelta());
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SCampusMainMenu::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bTurningWeapon = false;
}

void SCampusMainMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Time += InDeltaTime;
	PageAnim = FMath::Min(1.f, PageAnim + InDeltaTime * 3.4f);
	const float TitleTarget = Page == EPage::Title ? 1.f : 0.f;
	TitleAlpha = FMath::FInterpTo(TitleAlpha, TitleTarget, InDeltaTime, 6.f);
	const float ChromeTarget = (Page == EPage::Title || Page == EPage::Deploying) ? 0.f : 1.f;
	ChromeAlpha = FMath::FInterpTo(ChromeAlpha, ChromeTarget, InDeltaTime, Page == EPage::Deploying ? 10.f : 3.f);
	DeployAlpha = FMath::FInterpTo(DeployAlpha, Page == EPage::Deploying ? 1.f : 0.f, InDeltaTime, 8.f);
	if (ContentWidget.IsValid())
	{
		// Rounded-box outlines are drawn in their own colour whatever the widget's opacity: a page faded out to nothing
		// (under the title screen, during deployment) would still show every panel's hairline frame.
		const float ContentOpacity = FMath::Clamp(PageAnim, 0.f, 1.f) * ChromeAlpha;
		ContentWidget->SetRenderOpacity(ContentOpacity);
		ContentWidget->SetVisibility(ContentOpacity > 0.01f ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	}
	if (TopBarWidget.IsValid())
	{
		TopBarWidget->SetRenderOpacity(ChromeAlpha);
	}
	if (BottomBarWidget.IsValid())
	{
		BottomBarWidget->SetRenderOpacity(ChromeAlpha);
	}
	if (FSlateApplication::IsInitialized() && !HasAnyUserFocusOrFocusedDescendants() && Page == EPage::Title)
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
	}
}

#undef LOCTEXT_NAMESPACE
