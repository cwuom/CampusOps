#include "Player/CampusInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "CampusInput"

UCampusInputConfig* UCampusInputConfig::Get()
{
	static UCampusInputConfig* Instance = nullptr;
	if (!Instance)
	{
		Instance = NewObject<UCampusInputConfig>(GetTransientPackage(), TEXT("CampusInputConfig"));
		Instance->AddToRoot();
		Instance->Build();
	}
	return Instance;
}

const TArray<FCampusBindable>& UCampusInputConfig::Bindables()
{
	static const TArray<FCampusBindable> List = {
		{ TEXT("MoveForward"), LOCTEXT("MoveForward", "前进"), EKeys::W },
		{ TEXT("MoveBack"), LOCTEXT("MoveBack", "后退"), EKeys::S },
		{ TEXT("MoveLeft"), LOCTEXT("MoveLeft", "向左移动"), EKeys::A },
		{ TEXT("MoveRight"), LOCTEXT("MoveRight", "向右移动"), EKeys::D },
		{ TEXT("Jump"), LOCTEXT("Jump", "跳跃 / 攀爬"), EKeys::SpaceBar },
		{ TEXT("Sprint"), LOCTEXT("Sprint", "冲刺"), EKeys::LeftShift },
		{ TEXT("Crouch"), LOCTEXT("Crouch", "蹲下 / 滑铲"), EKeys::C },
		{ TEXT("CrouchHold"), LOCTEXT("CrouchHold", "按住蹲下"), EKeys::LeftControl },
		{ TEXT("Fire"), LOCTEXT("Fire", "开火"), EKeys::LeftMouseButton },
		{ TEXT("Aim"), LOCTEXT("Aim", "瞄准"), EKeys::RightMouseButton },
		{ TEXT("Reload"), LOCTEXT("Reload", "装填"), EKeys::R },
		{ TEXT("Primary"), LOCTEXT("Primary", "主武器"), EKeys::One },
		{ TEXT("Secondary"), LOCTEXT("Secondary", "副武器"), EKeys::Two },
		{ TEXT("FireMode"), LOCTEXT("FireMode", "射击模式"), EKeys::B },
		{ TEXT("Flashlight"), LOCTEXT("Flashlight", "枪灯开关"), EKeys::T },
		{ TEXT("Melee"), LOCTEXT("Melee", "近战"), EKeys::V },
		{ TEXT("Inspect"), LOCTEXT("Inspect", "检视武器"), EKeys::I },
		{ TEXT("Interact"), LOCTEXT("Interact", "互动 / 开门"), EKeys::F },
		{ TEXT("Lethal"), LOCTEXT("Lethal", "致命装备"), EKeys::G },
		{ TEXT("Tactical"), LOCTEXT("Tactical", "战术装备"), EKeys::Q },
		{ TEXT("CycleLethal"), LOCTEXT("CycleLethal", "切换致命装备"), EKeys::Z },
		{ TEXT("CycleTactical"), LOCTEXT("CycleTactical", "切换战术装备"), EKeys::X },
		{ TEXT("Scoreboard"), LOCTEXT("Scoreboard", "计分板"), EKeys::Tab },
	};
	return List;
}

FText UCampusInputConfig::KeyLabel(const FKey& Key)
{
	if (!Key.IsValid())
	{
		return LOCTEXT("Unbound", "未绑定");
	}
	static const TMap<FKey, FText> Names = {
		{ EKeys::LeftMouseButton, LOCTEXT("LMB", "鼠标左键") },
		{ EKeys::RightMouseButton, LOCTEXT("RMB", "鼠标右键") },
		{ EKeys::MiddleMouseButton, LOCTEXT("MMB", "鼠标中键") },
		{ EKeys::ThumbMouseButton, LOCTEXT("Thumb1", "鼠标侧键 1") },
		{ EKeys::ThumbMouseButton2, LOCTEXT("Thumb2", "鼠标侧键 2") },
		{ EKeys::MouseScrollUp, LOCTEXT("WheelUp", "滚轮上") },
		{ EKeys::MouseScrollDown, LOCTEXT("WheelDown", "滚轮下") },
		{ EKeys::SpaceBar, LOCTEXT("Space", "空格") },
		{ EKeys::LeftShift, LOCTEXT("LShift", "左 Shift") },
		{ EKeys::RightShift, LOCTEXT("RShift", "右 Shift") },
		{ EKeys::LeftControl, LOCTEXT("LCtrl", "左 Ctrl") },
		{ EKeys::RightControl, LOCTEXT("RCtrl", "右 Ctrl") },
		{ EKeys::LeftAlt, LOCTEXT("LAlt", "左 Alt") },
		{ EKeys::RightAlt, LOCTEXT("RAlt", "右 Alt") },
		{ EKeys::Enter, LOCTEXT("Enter", "回车") },
		{ EKeys::BackSpace, LOCTEXT("Backspace", "退格") },
		{ EKeys::CapsLock, LOCTEXT("Caps", "Caps") },
		{ EKeys::Tilde, FText::FromString(TEXT("`")) },
	};
	if (const FText* Name = Names.Find(Key))
	{
		return *Name;
	}
	return Key.GetDisplayName(false);
}

bool UCampusInputConfig::IsBindableKey(const FKey& Key)
{
	return Key.IsValid() && !Key.IsGamepadKey() && !Key.IsTouch() && !Key.IsAxis1D() && !Key.IsAxis2D() && Key != EKeys::Escape;
}

FKey UCampusInputConfig::GetKey(FName Id) const
{
	const FKey* Key = Keys.Find(Id);
	return Key ? *Key : EKeys::Invalid;
}

UInputAction* UCampusInputConfig::ActionFor(FName Id) const
{
	static const FName MoveIds[4] = { TEXT("MoveForward"), TEXT("MoveBack"), TEXT("MoveLeft"), TEXT("MoveRight") };
	for (const FName& M : MoveIds)
	{
		if (Id == M)
		{
			return Move;
		}
	}
	UInputAction* const Actions[] = { Jump, Sprint, Crouch, CrouchHold, Fire, Aim, Reload, Primary, Secondary, FireMode, Flashlight, Melee, Inspect, Interact, Lethal, Tactical, CycleLethal, CycleTactical, Scoreboard };
	for (UInputAction* A : Actions)
	{
		if (A && A->GetFName() == FName(*(TEXT("IA_") + Id.ToString())))
		{
			return A;
		}
	}
	return nullptr;
}

void UCampusInputConfig::Build()
{
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};

	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Campus"));

	Move = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	Look = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	LookGamepad = MakeAction(TEXT("IA_LookGamepad"), EInputActionValueType::Axis2D);
	Jump = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	Sprint = MakeAction(TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	Crouch = MakeAction(TEXT("IA_Crouch"), EInputActionValueType::Boolean);
	CrouchHold = MakeAction(TEXT("IA_CrouchHold"), EInputActionValueType::Boolean);
	Aim = MakeAction(TEXT("IA_Aim"), EInputActionValueType::Boolean);
	Fire = MakeAction(TEXT("IA_Fire"), EInputActionValueType::Boolean);
	Reload = MakeAction(TEXT("IA_Reload"), EInputActionValueType::Boolean);
	SwitchWeapon = MakeAction(TEXT("IA_SwitchWeapon"), EInputActionValueType::Boolean);
	Primary = MakeAction(TEXT("IA_Primary"), EInputActionValueType::Boolean);
	Secondary = MakeAction(TEXT("IA_Secondary"), EInputActionValueType::Boolean);
	FireMode = MakeAction(TEXT("IA_FireMode"), EInputActionValueType::Boolean);
	Flashlight = MakeAction(TEXT("IA_Flashlight"), EInputActionValueType::Boolean);
	Melee = MakeAction(TEXT("IA_Melee"), EInputActionValueType::Boolean);
	Inspect = MakeAction(TEXT("IA_Inspect"), EInputActionValueType::Boolean);
	Interact = MakeAction(TEXT("IA_Interact"), EInputActionValueType::Boolean);
	Lethal = MakeAction(TEXT("IA_Lethal"), EInputActionValueType::Boolean);
	Tactical = MakeAction(TEXT("IA_Tactical"), EInputActionValueType::Boolean);
	CycleLethal = MakeAction(TEXT("IA_CycleLethal"), EInputActionValueType::Boolean);
	CycleTactical = MakeAction(TEXT("IA_CycleTactical"), EInputActionValueType::Boolean);
	Scoreboard = MakeAction(TEXT("IA_Scoreboard"), EInputActionValueType::Boolean);
	Pause = MakeAction(TEXT("IA_Pause"), EInputActionValueType::Boolean);

	for (const FCampusBindable& B : Bindables())
	{
		Keys.Add(B.Id, B.Default);
	}
	MapAll();
}

void UCampusInputConfig::ApplyBindings(const TMap<FName, FKey>& Overrides)
{
	Keys.Reset();
	for (const FCampusBindable& B : Bindables())
	{
		const FKey* Key = Overrides.Find(B.Id);
		Keys.Add(B.Id, (Key && IsBindableKey(*Key)) ? *Key : B.Default);
	}
	MapAll();
	if (!GEngine)
	{
		return;
	}
	for (const FWorldContext& WC : GEngine->GetWorldContexts())
	{
		if (!WC.OwningGameInstance)
		{
			continue;
		}
		for (ULocalPlayer* LP : WC.OwningGameInstance->GetLocalPlayers())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
			{
				Sub->RequestRebuildControlMappings();
			}
		}
	}
}

void UCampusInputConfig::MapAll()
{
	Context->UnmapAll();
	auto Map = [this](UInputAction* Action, const FKey& Key) -> FEnhancedActionKeyMapping&
	{
		return Context->MapKey(Action, Key);
	};

	// Keyboard / mouse, as the player has them. Movement goes to 2D (X = right, Y = forward).
	for (const FCampusBindable& B : Bindables())
	{
		const FKey Key = GetKey(B.Id);
		UInputAction* Action = ActionFor(B.Id);
		if (!Key.IsValid() || !Action)
		{
			continue;
		}
		FEnhancedActionKeyMapping& M = Map(Action, Key);
		if (B.Id == TEXT("MoveForward") || B.Id == TEXT("MoveBack"))
		{
			M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (B.Id == TEXT("MoveBack") || B.Id == TEXT("MoveLeft"))
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}
	Map(Look, EKeys::Mouse2D);
	Map(Pause, EKeys::Escape);
	// The wheel switches weapons and P is a second pause key, unless the player has given them to something else.
	for (const FKey& Key : { EKeys::MouseScrollUp, EKeys::MouseScrollDown })
	{
		if (!Keys.FindKey(Key))
		{
			Map(SwitchWeapon, Key);
		}
	}
	if (!Keys.FindKey(EKeys::P))
	{
		Map(Pause, EKeys::P);
	}

	// Gamepad (fixed).
	{
		FEnhancedActionKeyMapping& Stick = Map(Move, EKeys::Gamepad_Left2D);
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Context);
		DeadZone->LowerThreshold = 0.2f;
		Stick.Modifiers.Add(DeadZone);
	}
	{
		FEnhancedActionKeyMapping& Stick = Map(LookGamepad, EKeys::Gamepad_Right2D);
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Context);
		DeadZone->LowerThreshold = 0.15f;
		Stick.Modifiers.Add(DeadZone);
	}
	Map(Jump, EKeys::Gamepad_FaceButton_Bottom);
	Map(Sprint, EKeys::Gamepad_LeftThumbstick);
	Map(Crouch, EKeys::Gamepad_FaceButton_Right);
	Map(Aim, EKeys::Gamepad_LeftTrigger);
	Map(Fire, EKeys::Gamepad_RightTrigger);
	Map(Reload, EKeys::Gamepad_FaceButton_Left);
	Map(SwitchWeapon, EKeys::Gamepad_FaceButton_Top);
	Map(FireMode, EKeys::Gamepad_DPad_Down);
	Map(Melee, EKeys::Gamepad_RightThumbstick);
	Map(Inspect, EKeys::Gamepad_DPad_Up);
	Map(Interact, EKeys::Gamepad_DPad_Right);
	Map(Lethal, EKeys::Gamepad_RightShoulder);
	Map(Tactical, EKeys::Gamepad_LeftShoulder);
	Map(CycleLethal, EKeys::Gamepad_DPad_Left);
	Map(Scoreboard, EKeys::Gamepad_Special_Left);
	Map(Pause, EKeys::Gamepad_Special_Right);
}

#undef LOCTEXT_NAMESPACE
