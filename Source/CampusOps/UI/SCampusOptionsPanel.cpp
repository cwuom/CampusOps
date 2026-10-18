#include "UI/SCampusOptionsPanel.h"
#include "UI/CampusUIStyle.h"
#include "UI/CampusWidgets.h"
#include "UI/SCampusButton.h"
#include "Game/CampusGameInstance.h"
#include "Game/CampusGraphics.h"
#include "Player/CampusInputConfig.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"

#define LOCTEXT_NAMESPACE "CampusOptions"

namespace
{
	/** The settings reopen on the tab they were left on. */
	int32 GLastOptionsTab = 0;
	FAutoConsoleVariableRef CVarOptionsTab(TEXT("campus.OptionsTab"), GLastOptionsTab, TEXT("Settings tab shown when the settings next open (0 gameplay .. 4 audio)."));
	int32 GAdvancedGraphics = 0;
	FAutoConsoleVariableRef CVarAdvancedGraphics(TEXT("campus.OptionsAdvanced"), GAdvancedGraphics, TEXT("Expand the advanced graphics settings (0 basic, 1 expanded)."));

	const FSliderStyle& CampusSliderStyle()
	{
		static FSliderStyle Style = []()
		{
			FSliderStyle S = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
			const FSlateRoundedBoxBrush Bar(FLinearColor(1.f, 1.f, 1.f, 0.16f), 2.f, FVector2f(8.f, 4.f));
			const FSlateRoundedBoxBrush Thumb(CampusUI::Fill(), 7.f, FVector2f(14.f, 14.f));
			const FSlateRoundedBoxBrush ThumbHot(CampusUI::Accent(), 8.f, FVector2f(16.f, 16.f));
			S.SetNormalBarImage(Bar);
			S.SetHoveredBarImage(Bar);
			S.SetDisabledBarImage(Bar);
			S.SetNormalThumbImage(Thumb);
			S.SetHoveredThumbImage(ThumbHot);
			S.SetDisabledThumbImage(Thumb);
			S.SetBarThickness(4.f);
			return S;
		}();
		return Style;
	}

	/** A key slot: click it, then press the key or mouse button to bind (Escape cancels). */
	class SCampusKeyBind : public SCompoundWidget
	{
	public:
		DECLARE_DELEGATE_OneParam(FOnKeyChosen, FKey);

		SLATE_BEGIN_ARGS(SCampusKeyBind) {}
			SLATE_ATTRIBUTE(FKey, Key)
			SLATE_ARGUMENT(TWeakObjectPtr<UObject>, SoundContext)
			SLATE_EVENT(FOnKeyChosen, OnKeyChosen)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Key = InArgs._Key;
			SoundContext = InArgs._SoundContext;
			OnKeyChosen = InArgs._OnKeyChosen;
			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(220.f)
				.HeightOverride(32.f)
				[
					SNew(SBorder)
					.BorderImage(CampusUI::Rounded())
					.BorderBackgroundColor_Lambda([this]()
					{
						return FSlateColor(bListening ? CampusUI::Alpha(CampusUI::Accent(), 0.85f) : FLinearColor(1.f, 1.f, 1.f, IsHovered() ? 0.16f : 0.06f));
					})
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return bListening ? NSLOCTEXT("CampusOptions", "PressKey", "按下按键…  (ESC 取消)") : UCampusInputConfig::KeyLabel(Key.Get()); })
						.Font(CampusUI::Font(CampusUI::EFont::Sans, 15, TEXT("Bold")))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(bListening ? CampusUI::Ink() : CampusUI::TextMain()); })
					]
				]
			];
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (bListening)
			{
				return Choose(Event.GetEffectingButton());
			}
			return Event.GetEffectingButton() == EKeys::LeftMouseButton ? FReply::Handled() : FReply::Unhandled();
		}

		virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			return OnMouseButtonDown(Geometry, Event);
		}

		virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (bListening || Event.GetEffectingButton() != EKeys::LeftMouseButton || !IsHovered())
			{
				return bListening ? FReply::Handled() : FReply::Unhandled();
			}
			// Listens from the release on, so the click that opened it is not taken as the key.
			bListening = true;
			CampusUI::PlaySound(SoundContext, TEXT("UI_Select"), 0.7f);
			return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
		}

		virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!bListening)
			{
				return FReply::Unhandled();
			}
			return Choose(Event.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
		}

		virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
		{
			if (!bListening)
			{
				return FReply::Unhandled();
			}
			const FKey Pressed = Event.GetKey();
			if (Pressed == EKeys::Escape)
			{
				bListening = false;
				CampusUI::PlaySound(SoundContext, TEXT("UI_Back"), 0.6f);
				return FReply::Handled().ReleaseMouseCapture();
			}
			return UCampusInputConfig::IsBindableKey(Pressed) ? Choose(Pressed) : FReply::Handled();
		}

		virtual void OnFocusLost(const FFocusEvent& Event) override
		{
			bListening = false;
		}

		virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override
		{
			bListening = false;
		}

	private:
		FReply Choose(const FKey& Chosen)
		{
			bListening = false;
			CampusUI::PlaySound(SoundContext, TEXT("UI_Select"), 0.7f);
			OnKeyChosen.ExecuteIfBound(Chosen);
			return FReply::Handled().ReleaseMouseCapture();
		}

		TAttribute<FKey> Key;
		TWeakObjectPtr<UObject> SoundContext;
		FOnKeyChosen OnKeyChosen;
		bool bListening = false;
	};
}

void SCampusOptionsPanel::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	SoundContext = InArgs._SoundContext;
	OnChanged = InArgs._OnChanged;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 14.f)
		[
			SAssignNew(TabBar, SHorizontalBox)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(Pages, SWidgetSwitcher)
		]
	];

	auto GI = GameInstance;
	auto Profile = [GI]() -> FCampusProfile* { return GI.IsValid() ? &GI->EditProfile() : nullptr; };
	const TArray<FText> OnOff = { LOCTEXT("On", "开启"), LOCTEXT("Off", "关闭") };

	AddTab(LOCTEXT("TabGameplay", "游戏"));
	AddSection(LOCTEXT("Gameplay", "游戏   GAMEPLAY"));
	AddChoice(LOCTEXT("BotSkill", "AI 难度"), { LOCTEXT("Recruit", "新兵"), LOCTEXT("Regular", "普通"), LOCTEXT("Hardened", "困难"), LOCTEXT("Veteran", "老兵") },
		[Profile]() { return Profile() ? Profile()->BotSkill : 1; },
		[Profile](int32 V) { if (Profile()) Profile()->BotSkill = V; });
	AddChoice(LOCTEXT("AutoTac", "自动战术冲刺"), OnOff,
		[Profile]() { return Profile() && Profile()->bAutoTacSprint ? 0 : 1; },
		[Profile](int32 V) { if (Profile()) Profile()->bAutoTacSprint = V == 0; });
	AddSection(LOCTEXT("Crosshair", "准星   CROSSHAIR"));
	AddChoice(LOCTEXT("XStyle", "准星样式"),
		{ LOCTEXT("XAuto", "自动"), LOCTEXT("XCross", "十字"), LOCTEXT("XDot", "圆点"), LOCTEXT("XT", "T 形"), LOCTEXT("XChevron", "箭头") },
		[Profile]() { return Profile() ? FMath::Clamp(Profile()->CrosshairStyle, 0, 4) : 0; },
		[Profile](int32 V) { if (Profile()) Profile()->CrosshairStyle = V; });
	AddChoice(LOCTEXT("XColor", "准星颜色"),
		{ LOCTEXT("CWhite", "白"), LOCTEXT("CGreen", "绿"), LOCTEXT("CCyan", "青"), LOCTEXT("CYellow", "黄"), LOCTEXT("CPink", "粉"), LOCTEXT("CRed", "红") },
		[Profile]() { return Profile() ? Profile()->CrosshairColor : 0; },
		[Profile](int32 V) { if (Profile()) Profile()->CrosshairColor = V; });

	AddTab(LOCTEXT("TabControls", "操作"));
	AddSection(LOCTEXT("Controls", "操作   CONTROLS"));
	AddSlider(LOCTEXT("Sens", "鼠标灵敏度"), 0.2f, 10.f, 0.1f,
		[Profile]() { return Profile() ? Profile()->MouseSensitivity : 2.f; },
		[Profile](float V) { if (Profile()) Profile()->MouseSensitivity = V; },
		[](float V) { return FText::FromString(FString::Printf(TEXT("%.1f"), V)); });
	AddSlider(LOCTEXT("ADSSens", "开镜灵敏度倍率"), 0.3f, 2.f, 0.05f,
		[Profile]() { return Profile() ? Profile()->ADSSensitivityMultiplier : 1.f; },
		[Profile](float V) { if (Profile()) Profile()->ADSSensitivityMultiplier = V; },
		[](float V) { return FText::FromString(FString::Printf(TEXT("%.2f"), V)); });
	AddSlider(LOCTEXT("PadSens", "手柄灵敏度"), 0.3f, 3.f, 0.05f,
		[Profile]() { return Profile() ? Profile()->GamepadSensitivity : 1.f; },
		[Profile](float V) { if (Profile()) Profile()->GamepadSensitivity = V; },
		[](float V) { return FText::FromString(FString::Printf(TEXT("%.2f"), V)); });
	AddChoice(LOCTEXT("Invert", "反转 Y 轴"), OnOff,
		[Profile]() { return Profile() && Profile()->bInvertY ? 0 : 1; },
		[Profile](int32 V) { if (Profile()) Profile()->bInvertY = V == 0; });
	AddChoice(LOCTEXT("ToggleAim", "开镜方式"), { LOCTEXT("Hold", "按住"), LOCTEXT("Toggle", "切换") },
		[Profile]() { return Profile() && Profile()->bToggleAim ? 1 : 0; },
		[Profile](int32 V) { if (Profile()) Profile()->bToggleAim = V == 1; });
	AddChoice(LOCTEXT("ToggleSprint", "冲刺方式"), { LOCTEXT("SprintHold", "按住"), LOCTEXT("SprintToggle", "切换") },
		[Profile]() { return Profile() && Profile()->bToggleSprint ? 1 : 0; },
		[Profile](int32 V) { if (Profile()) Profile()->bToggleSprint = V == 1; });

	AddTab(LOCTEXT("TabKeys", "键位"));
	AddSection(LOCTEXT("Keys", "按键   KEY BINDINGS"));
	for (const FCampusBindable& B : UCampusInputConfig::Bindables())
	{
		const FName Id = B.Id;
		AddRow(B.Label,
			SNew(SCampusKeyBind)
			.SoundContext(SoundContext)
			.Key_Lambda([Id]() { return UCampusInputConfig::Get()->GetKey(Id); })
			.OnKeyChosen_Lambda([this, GI, Id](FKey Key) { if (GI.IsValid()) { GI->SetKeyBinding(Id, Key); Changed(); } }));
	}
	AddRow(LOCTEXT("KeysReset", "恢复默认按键"),
		SNew(SCampusButton).Style(ECampusButtonStyle::Segment).SoundContext(SoundContext).Text(LOCTEXT("KeysResetGo", "恢复默认")).FontSize(15).Height(32.f)
		.OnClicked_Lambda([this, GI]() { if (GI.IsValid()) { GI->ResetKeyBindings(); Changed(); } }));

	AddTab(LOCTEXT("TabVisual", "视觉"));
	auto Gfx = [Profile]() -> FCampusGraphicsSettings* { return Profile() ? &Profile()->Graphics : nullptr; };
	auto Apply = [GI]() { if (GI.IsValid()) GI->ApplyGraphicsQuality(); };
	AddSection(LOCTEXT("Display", "显示   DISPLAY"));
	AddChoice(LOCTEXT("WinMode", "显示模式"), { LOCTEXT("Fullscreen", "全屏"), LOCTEXT("Borderless", "无边框"), LOCTEXT("Windowed", "窗口") },
		[Gfx]() { return Gfx() ? Gfx()->WindowMode : 1; },
		[Gfx, Apply](int32 V) { if (Gfx()) { Gfx()->WindowMode = V; Apply(); } });
	{
		TArray<FIntPoint> Res = CampusGraphics::Resolutions();
		Res.Insert(FIntPoint::ZeroValue, 0);
		AddStepper(LOCTEXT("Resolution", "分辨率"), Res.Num(),
			[Gfx, Res]() { return Gfx() ? FMath::Max(Res.IndexOfByKey(Gfx()->Resolution), 0) : 0; },
			[Gfx, Apply, Res](int32 V) { if (Gfx()) { Gfx()->Resolution = Res[V]; Apply(); } },
			[Res](int32 V) { return Res[V].X <= 0 ? LOCTEXT("ResDesktop", "桌面分辨率") : FText::FromString(FString::Printf(TEXT("%d × %d"), Res[V].X, Res[V].Y)); });
	}
	AddSlider(LOCTEXT("FOV", "视野 (FOV)"), 70.f, 120.f, 1.f,
		[Profile]() { return Profile() ? Profile()->FieldOfView : 90.f; },
		[Profile](float V) { if (Profile()) Profile()->FieldOfView = V; },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V)); });
	AddSlider(LOCTEXT("VMFOV", "武器视野"), 55.f, 85.f, 1.f,
		[Profile]() { return Profile() ? Profile()->ViewmodelFOV : 74.f; },
		[Profile](float V) { if (Profile()) Profile()->ViewmodelFOV = V; },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V)); });
	AddSlider(LOCTEXT("Brightness", "亮度"), -1.5f, 1.5f, 0.05f,
		[Gfx]() { return Gfx() ? Gfx()->Brightness : 0.f; },
		[Gfx](float V) { if (Gfx()) { Gfx()->Brightness = V; } },
		[](float V) { return FText::FromString(FString::Printf(TEXT("%+.2f"), V)); }, Apply);
	{
		static const int32 Caps[] = { 0, 30, 60, 90, 120, 144, 165, 240 };
		AddStepper(LOCTEXT("FrameCap", "帧率上限"), UE_ARRAY_COUNT(Caps),
			[Gfx]() { if (!Gfx()) return 0; for (int32 i = 0; i < UE_ARRAY_COUNT(Caps); ++i) { if (Caps[i] == Gfx()->FrameLimit) return i; } return 0; },
			[Gfx, Apply](int32 V) { if (Gfx()) { Gfx()->FrameLimit = Caps[V]; Apply(); } },
			[](int32 V) { return Caps[V] == 0 ? LOCTEXT("Unlimited", "无限制") : FText::AsNumber(Caps[V]); });
	}
	AddChoice(LOCTEXT("VSync", "垂直同步"), OnOff,
		[Gfx]() { return Gfx() && Gfx()->bVSync ? 0 : 1; },
		[Gfx, Apply](int32 V) { if (Gfx()) { Gfx()->bVSync = V == 0; Apply(); } });
	AddChoice(LOCTEXT("FPS", "显示帧率"), OnOff,
		[Profile]() { return Profile() && Profile()->bShowFPS ? 0 : 1; },
		[Profile](int32 V) { if (Profile()) Profile()->bShowFPS = V == 0; });

	AddSection(LOCTEXT("Video", "基础画质   QUALITY"));
	// Any single setting moves the preset to "custom".
	auto Level = [Gfx, Apply](int32 FCampusGraphicsSettings::*Field)
	{
		return TPair<TFunction<int32()>, TFunction<void(int32)>>(
			[Gfx, Field]() { return Gfx() ? Gfx()->*Field : 0; },
			[Gfx, Field, Apply](int32 V) { if (Gfx()) { Gfx()->*Field = V; Gfx()->Preset = 4; Apply(); } });
	};
	const TArray<FText> Levels = { LOCTEXT("Low", "低"), LOCTEXT("Med", "中"), LOCTEXT("High", "高"), LOCTEXT("Ultra", "极高") };

	AddChoice(LOCTEXT("Preset", "画质预设"), { LOCTEXT("PLow", "低"), LOCTEXT("PMed", "中"), LOCTEXT("PHigh", "高"), LOCTEXT("PUltra", "极高"), LOCTEXT("PCustom", "自定义") },
		[Gfx]() { return Gfx() ? Gfx()->Preset : 2; },
		[Gfx, Apply](int32 V) { if (Gfx() && V < 4) { CampusGraphics::ApplyPreset(*Gfx(), V); Apply(); } });
	AddSlider(LOCTEXT("RenderScale", "渲染分辨率"), 50.f, 200.f, 5.f,
		[Gfx]() { return Gfx() ? Gfx()->RenderScale : 100.f; },
		[Gfx](float V) { if (Gfx()) { Gfx()->RenderScale = V; Gfx()->Preset = 4; } },
		[](float V)
		{
			const int32 P = FMath::RoundToInt(V);
			return FText::FromString(P > 100 ? FString::Printf(TEXT("%d%% 超采样"), P) : FString::Printf(TEXT("%d%%"), P));
		}, Apply);
	{
		const auto F = Level(&FCampusGraphicsSettings::AntiAliasing);
		const FText DLSS = CampusGraphics::SupportsDLSS() ? LOCTEXT("DLSS", "DLSS") : LOCTEXT("DLSSNo", "DLSS(不支持)");
		AddChoice(LOCTEXT("AA", "抗锯齿 / 超分"), { LOCTEXT("TSR", "TSR"), LOCTEXT("TAA", "TAA"), LOCTEXT("FXAA", "FXAA"), LOCTEXT("AAOff", "关闭"), DLSS }, F.Key,
			[F](int32 V) { F.Value((V == 4 && !CampusGraphics::SupportsDLSS()) ? 0 : V); });
	}
	TSharedPtr<SVerticalBox> AdvancedRows;
	Rows->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
	[
		SAssignNew(AdvancedGraphics, SExpandableArea)
		.InitiallyCollapsed(GAdvancedGraphics == 0)
		.BorderImage(CampusUI::Rounded())
		.BorderBackgroundColor(FLinearColor(0.025f, 0.035f, 0.04f, 0.95f))
		.BodyBorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
		.BodyBorderBackgroundColor(FLinearColor::Transparent)
		.HeaderPadding(FMargin(16.f, 12.f))
		.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		.OnAreaExpansionChanged_Lambda([this](bool bExpanded)
		{
			GAdvancedGraphics = bExpanded ? 1 : 0;
			if (const TSharedPtr<FActiveTimerHandle> Pending = AdvancedScrollTimer.Pin())
			{
				UnRegisterActiveTimer(Pending.ToSharedRef());
				AdvancedScrollTimer.Reset();
			}
			if (bExpanded && GraphicsScroll.IsValid())
			{
				AdvancedScrollTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SCampusOptionsPanel::ScrollAdvancedIntoView));
			}
		})
		.HeaderContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("AdvancedGraphics", "高级图形设置"))
				.Font(CampusUI::Font(CampusUI::EFont::Sans, 17, TEXT("Bold"))).ColorAndOpacity(CampusUI::TextMain())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("AdvancedGraphicsSub", "超分细节、帧生成与各项画质，按需展开"))
				.Font(CampusUI::Font(CampusUI::EFont::Sans, 13, TEXT("Medium"))).ColorAndOpacity(CampusUI::TextDim())
			]
		]
		.BodyContent()
		[
			SAssignNew(AdvancedRows, SVerticalBox)
		]
	];
	Rows = AdvancedRows;
	{
		const auto F = Level(&FCampusGraphicsSettings::DLSSMode);
		AddChoice(LOCTEXT("DLSSMode", "DLSS 模式"),
			{ LOCTEXT("DLAA", "DLAA"), LOCTEXT("DQuality", "质量"), LOCTEXT("DBalanced", "平衡"), LOCTEXT("DPerf", "性能"), LOCTEXT("DUltraPerf", "超级性能") }, F.Key, F.Value);
	}
	{
		const int32 MaxFG = CampusGraphics::MaxFrameGeneration();
		TArray<FText> FGOptions = { LOCTEXT("FGOff", "关闭") };
		if (MaxFG >= 1) FGOptions.Add(LOCTEXT("FG2", "2x"));
		if (MaxFG >= 2) FGOptions.Add(LOCTEXT("FG3", "3x"));
		if (MaxFG >= 3) FGOptions.Add(LOCTEXT("FG4", "4x"));
		if (MaxFG >= 1) FGOptions.Add(LOCTEXT("FGDyn", "动态"));
		if (MaxFG == 0) FGOptions.Add(LOCTEXT("FGNo", "不支持"));
		// The dynamic option sits last in the list but is value 4 in the settings.
		const int32 DynIndex = MaxFG >= 1 ? MaxFG + 1 : -1;
		AddChoice(LOCTEXT("FrameGen", "DLSS 帧生成"), FGOptions,
			[Gfx, DynIndex]() { if (!Gfx()) return 0; const int32 V = Gfx()->FrameGeneration; return V == 4 ? FMath::Max(DynIndex, 0) : V; },
			[Gfx, Apply, DynIndex, MaxFG](int32 V) { if (Gfx()) { Gfx()->FrameGeneration = MaxFG == 0 ? 0 : (V == DynIndex ? 4 : FMath::Min(V, MaxFG)); Apply(); } });
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::Reflex);
		const FText Off = CampusGraphics::SupportsReflex() ? LOCTEXT("ReflexOff", "关闭") : LOCTEXT("ReflexNo", "不支持");
		AddChoice(LOCTEXT("Reflex", "NVIDIA Reflex 低延迟"), { Off, LOCTEXT("ReflexOn", "开启"), LOCTEXT("ReflexBoost", "开启 + 加速") }, F.Key, F.Value);
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::AntiAliasingQuality);
		TArray<FText> AALevels = Levels;
		AALevels.Add(LOCTEXT("AACine", "电影级"));
		AddChoice(LOCTEXT("AAQ", "抗锯齿质量"), AALevels, F.Key, F.Value);
	}
	AddSlider(LOCTEXT("Sharpness", "锐化"), 0.f, 1.f, 0.01f,
		[Gfx]() { return Gfx() ? Gfx()->Sharpness : 0.5f; },
		[Gfx](float V) { if (Gfx()) { Gfx()->Sharpness = V; } },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V * 100.f)); }, Apply);
	{
		const auto F = Level(&FCampusGraphicsSettings::GlobalIllumination);
		const FText RT = CampusGraphics::SupportsHardwareRayTracing() ? LOCTEXT("GIRT", "光线追踪") : LOCTEXT("GIRTNo", "光追(不支持)");
		AddChoice(LOCTEXT("GI", "全局光照"), { LOCTEXT("GIOff", "屏幕空间"), LOCTEXT("GILow", "低"), LOCTEXT("GIMed", "中"), LOCTEXT("GIHigh", "高"), RT }, F.Key,
			[F](int32 V) { F.Value((V == 4 && !CampusGraphics::SupportsHardwareRayTracing()) ? 3 : V); });
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::Shadows);
		AddChoice(LOCTEXT("Shadows", "阴影质量"), Levels, F.Key, F.Value);
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::Reflections);
		AddChoice(LOCTEXT("Reflections", "反射质量"), Levels, F.Key, F.Value);
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::Textures);
		AddChoice(LOCTEXT("Textures", "贴图质量"), Levels, F.Key, F.Value);
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::Effects);
		AddChoice(LOCTEXT("Effects", "特效 (体积雾 / 云)"), Levels, F.Key, F.Value);
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::ViewDistance);
		AddChoice(LOCTEXT("ViewDist", "视距与植被"), Levels, F.Key, F.Value);
	}
	{
		const auto F = Level(&FCampusGraphicsSettings::PostProcess);
		AddChoice(LOCTEXT("Post", "后期处理"), Levels, F.Key, F.Value);
	}
	AddTab(LOCTEXT("TabAudio", "音效"));
	AddSection(LOCTEXT("Audio", "音量   VOLUME"));
	AddSlider(LOCTEXT("Volume", "主音量"), 0.f, 1.f, 0.01f,
		[Profile]() { return Profile() ? Profile()->MasterVolume : 0.8f; },
		[Profile, this](float V) { if (Profile()) { Profile()->MasterVolume = V; UCampusGameInstance::ApplyAudioVolume(SoundContext.Get(), V); } },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V * 100.f)); });
	auto Mix = [GI]() { if (GI.IsValid()) GI->ApplyAudioMix(); };
	AddSlider(LOCTEXT("EffectsVolume", "音效 (武器 / 脚步 / 爆炸)"), 0.f, 1.f, 0.01f,
		[Profile]() { return Profile() ? Profile()->EffectsVolume : 1.f; },
		[Profile, Mix](float V) { if (Profile()) { Profile()->EffectsVolume = V; Mix(); } },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V * 100.f)); });
	AddSlider(LOCTEXT("AmbienceVolume", "环境 (天气 / 背景声)"), 0.f, 1.f, 0.01f,
		[Profile]() { return Profile() ? Profile()->AmbienceVolume : 1.f; },
		[Profile, Mix](float V) { if (Profile()) { Profile()->AmbienceVolume = V; Mix(); } },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V * 100.f)); });
	AddSlider(LOCTEXT("InterfaceVolume", "界面与提示音"), 0.f, 1.f, 0.01f,
		[Profile]() { return Profile() ? Profile()->InterfaceVolume : 1.f; },
		[Profile, Mix](float V) { if (Profile()) { Profile()->InterfaceVolume = V; Mix(); } },
		[](float V) { return FText::AsNumber(FMath::RoundToInt(V * 100.f)); },
		[this]() { CampusUI::PlaySound(SoundContext, TEXT("UI_Select"), 0.8f); });
	AddSection(LOCTEXT("AudioOutput", "输出   OUTPUT"));
	AddChoice(LOCTEXT("MuteUnfocused", "后台时静音"), OnOff,
		[Profile]() { return Profile() && Profile()->bMuteUnfocused ? 0 : 1; },
		[Profile, Mix](int32 V) { if (Profile()) { Profile()->bMuteUnfocused = V == 0; Mix(); } });

	SelectTab(FMath::Clamp(GLastOptionsTab, 0, NumTabs - 1));
}

void SCampusOptionsPanel::Changed()
{
	if (GameInstance.IsValid())
	{
		GameInstance->SaveProfile();
	}
	OnChanged.ExecuteIfBound();
}

EActiveTimerReturnType SCampusOptionsPanel::ScrollAdvancedIntoView(double, float)
{
	if (!AdvancedGraphics.IsValid() || !GraphicsScroll.IsValid() || !AdvancedGraphics->IsExpanded() || ActiveTab != 3)
	{
		AdvancedScrollTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	// 等待展开后的内容真正排版，旧高度会把滚动目标夹回列表底部
	if (AdvancedGraphics->GetCachedGeometry().GetLocalSize().Y + 0.5f < AdvancedGraphics->GetDesiredSize().Y)
	{
		return EActiveTimerReturnType::Continue;
	}
	GraphicsScroll->ScrollDescendantIntoView(AdvancedGraphics, true, EDescendantScrollDestination::TopOrLeft, 8.f);
	AdvancedScrollTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

void SCampusOptionsPanel::AddTab(const FText& Label)
{
	const int32 Index = NumTabs++;
	TabBar->AddSlot()
	.AutoWidth()
	.Padding(0.f, 0.f, 26.f, 0.f)
	[
		SNew(SCampusButton).Style(ECampusButtonStyle::Tab).SoundContext(SoundContext).Text(Label).FontSize(20).Height(44.f)
		.IsSelected_Lambda([this, Index]() { return ActiveTab == Index; })
		.OnClicked_Lambda([this, Index]() { SelectTab(Index); })
	];
	// Each page scrolls on its own when the host gives it less height than its rows.
	TSharedPtr<SScrollBox> Scroll;
	Pages->AddSlot()
	[
		SAssignNew(Scroll, SScrollBox)
		+ SScrollBox::Slot()
		[
			SAssignNew(Rows, SVerticalBox)
		]
	];
	if (Index == 3) GraphicsScroll = Scroll;
}

void SCampusOptionsPanel::SelectTab(int32 Index)
{
	if (NumTabs <= 0)
	{
		return;
	}
	ActiveTab = (Index % NumTabs + NumTabs) % NumTabs;
	GLastOptionsTab = ActiveTab;
	Pages->SetActiveWidgetIndex(ActiveTab);
}

void SCampusOptionsPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Another settings panel (pause menu, main menu) or campus.OptionsTab moved the shared tab.
	if (GLastOptionsTab != ActiveTab)
	{
		SelectTab(GLastOptionsTab);
	}
	if (AdvancedGraphics.IsValid() && AdvancedGraphics->IsExpanded() != (GAdvancedGraphics != 0))
	{
		AdvancedGraphics->SetExpanded(GAdvancedGraphics != 0);
	}
}

FReply SCampusOptionsPanel::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	const int32 Step = (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder) ? 1 : (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder) ? -1 : 0;
	if (Step != 0)
	{
		SelectTab(ActiveTab + Step);
		CampusUI::PlaySound(SoundContext, TEXT("UI_Tab"), 0.5f);
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(Geometry, Event);
}

void SCampusOptionsPanel::AddSection(const FText& Label)
{
	Rows->AddSlot()
	.AutoHeight()
	.Padding(0.f, Rows->NumSlots() == 0 ? 0.f : 14.f, 0.f, 6.f)
	[
		CampusWidgets::Section(Label)
	];
}

void SCampusOptionsPanel::AddRow(const FText& Label, TSharedRef<SWidget> Control)
{
	Rows->AddSlot()
	.AutoHeight()
	.Padding(0.f, 2.f)
	[
		SNew(SBox)
		.HeightOverride(46.f)
		[
			SNew(SBorder)
			.BorderImage(CampusUI::Rounded())
			.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.36f)); })
			.Padding(FMargin(18.f, 0.f, 14.f, 0.f))
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusUI::Font(CampusUI::EFont::Sans, 16, TEXT("Bold"))).ColorAndOpacity(FSlateColor(CampusUI::TextMain()))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					Control
				]
			]
		]
	];
}

void SCampusOptionsPanel::AddStepper(const FText& Label, int32 Count, TFunction<int32()> Getter, TFunction<void(int32)> Setter, TFunction<FText(int32)> Format)
{
	auto Step = [this, Count, Getter, Setter](int32 Dir)
	{
		Setter((Getter() + Dir + Count) % FMath::Max(Count, 1));
		Changed();
	};
	AddRow(Label,
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Segment).SoundContext(SoundContext).Text(FText::FromString(TEXT("‹"))).FontSize(16).Height(32.f)
			.OnClicked_Lambda([Step]() { Step(-1); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(170.f)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Getter, Format, Count]() { return Format(FMath::Clamp(Getter(), 0, FMath::Max(Count - 1, 0))); })
				.Font(CampusUI::Font(CampusUI::EFont::Sans, 16, TEXT("Bold")))
				.ColorAndOpacity(FSlateColor(CampusUI::Accent()))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SCampusButton).Style(ECampusButtonStyle::Segment).SoundContext(SoundContext).Text(FText::FromString(TEXT("›"))).FontSize(16).Height(32.f)
			.OnClicked_Lambda([Step]() { Step(1); })
		]);
}

void SCampusOptionsPanel::AddSlider(const FText& Label, float Min, float Max, float Step, TFunction<float()> Getter, TFunction<void(float)> Setter, TFunction<FText(float)> Format, TFunction<void()> OnCommit)
{
	AddRow(Label,
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(260.f)
			[
				SNew(SSlider)
				.Style(&CampusSliderStyle())
				.MinValue(Min)
				.MaxValue(Max)
				.StepSize(Step)
				.SliderBarColor(FSlateColor(FLinearColor::White))
				.SliderHandleColor(FSlateColor(FLinearColor::White))
				.Value_Lambda([Getter]() { return Getter(); })
				.OnValueChanged_Lambda([Setter](float V) { Setter(V); })
				.OnMouseCaptureEnd_Lambda([this, OnCommit]() { if (OnCommit) { OnCommit(); } Changed(); })
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(16.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(52.f)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text_Lambda([Getter, Format]() { return Format(Getter()); })
				.Font(CampusUI::Font(CampusUI::EFont::Num, 20, TEXT("Bold")))
				.ColorAndOpacity(FSlateColor(CampusUI::Accent()))
			]
		]);
}

void SCampusOptionsPanel::AddChoice(const FText& Label, TArray<FText> Options, TFunction<int32()> Getter, TFunction<void(int32)> Setter)
{
	AddRow(Label,
		SNew(SCampusSegmented)
		.SoundContext(SoundContext)
		.Options(Options)
		.Selected_Lambda([Getter]() { return Getter(); })
		.OnSelect_Lambda([this, Setter](int32 V) { Setter(V); Changed(); }));
}

#undef LOCTEXT_NAMESPACE
