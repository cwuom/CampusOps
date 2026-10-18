#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SaveGame.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "CampusTypes.h"
#include "Game/CampusGraphics.h"
#include "CampusGameInstance.generated.h"

USTRUCT(BlueprintType)
struct FCampusProfile
{
	GENERATED_BODY()

	UPROPERTY() FString PlayerName = TEXT("新兵");
	UPROPERTY() float MouseSensitivity = 2.0f;
	UPROPERTY() float ADSSensitivityMultiplier = 1.0f;
	UPROPERTY() float GamepadSensitivity = 1.0f;
	UPROPERTY() float FieldOfView = 90.f;
	UPROPERTY() float ViewmodelFOV = 74.f;
	UPROPERTY() bool bInvertY = false;
	UPROPERTY() bool bToggleAim = false;
	/** False: sprint while the key is held (and picks up again whenever it is still held); true: a press latches it. */
	UPROPERTY() bool bToggleSprint = false;
	UPROPERTY() bool bAutoTacSprint = true;
	/** Keyboard / mouse keys changed from the defaults, by UCampusInputConfig bindable id. */
	UPROPERTY() TMap<FName, FKey> KeyBindings;
	/** 0 = per weapon class, then CampusHUD::ECrosshair - 1. */
	UPROPERTY() int32 CrosshairStyle = 0;
	UPROPERTY() int32 CrosshairColor = 0;
	UPROPERTY() float MasterVolume = 0.8f;
	/** Mix groups (CampusAudio::ECategory): weapons and the world, weather and beds, menus and HUD cues. */
	UPROPERTY() float EffectsVolume = 1.f;
	UPROPERTY() float AmbienceVolume = 1.f;
	UPROPERTY() float InterfaceVolume = 1.f;
	/** Silence while the game window is in the background. */
	UPROPERTY() bool bMuteUnfocused = false;
	/** Superseded by Graphics (kept so older saves load). */
	UPROPERTY() int32 GraphicsQuality = 2;
	UPROPERTY() FCampusGraphicsSettings Graphics;
	UPROPERTY() bool bShowFPS = false;
	UPROPERTY() TArray<FCampusLoadout> Loadouts;
	UPROPERTY() int32 ActiveLoadout = 0;
	/** One-off loadout fixes already applied to this profile. */
	UPROPERTY() int32 LoadoutRevision = 0;
	UPROPERTY() int32 OperatorIndex = 0;
	UPROPERTY() int32 BotSkill = 1;
	UPROPERTY() FString LastJoinAddress = TEXT("127.0.0.1");
	/** Last custom match setup. */
	UPROPERTY() FCampusMatchRules CustomRules;
	UPROPERTY() int32 CustomMode = 0;
	UPROPERTY() int32 CustomTeamSize = 5;
	UPROPERTY() bool bCustomBots = true;
	/** Map to host (ECampusMap), weather (-1 random, else ECampusWeather) and time of day (-2 the map's own, -1 random). */
	UPROPERTY() int32 SelectedMap = 0;
	UPROPERTY() int32 SelectedWeather = -1;
	UPROPERTY() int32 SelectedTime = -2;
};

UCLASS()
class CAMPUSOPS_API UCampusSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() FCampusProfile Profile;
};

UCLASS()
class CAMPUSOPS_API UCampusGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	const FCampusProfile& GetProfile() const { return Profile; }
	FCampusProfile& EditProfile() { return Profile; }
	void SaveProfile();
	const FCampusLoadout& GetActiveLoadout() const;
	FCampusLoadout& EditLoadout(int32 Index);
	void ApplyGraphicsQuality() const;
	/** Pushes the profile's key bindings to the input mapping. */
	void ApplyKeyBindings() const;
	/** Binds a key to a bindable action; whatever else had that key takes this action's old one. */
	void SetKeyBinding(FName Id, const FKey& Key);
	void ResetKeyBindings();
	static void ApplyAudioVolume(const UObject* WorldContext, float Volume);
	/** Mix group volumes and background muting from the profile. */
	void ApplyAudioMix() const;

	/** Rules: custom match rules (null plays the standard rules). */
	void HostMatch(ECampusMatchMode Mode, int32 TeamSize, bool bBots, const FCampusMatchRules* Rules = nullptr);
	void JoinMatch(const FString& Address);

	/** The title ("press any key") screen is only shown on first launch. */
	bool bSeenTitleScreen = false;

	// Debug / automation console commands (also used by -CampusScriptFile=<file> for scripted captures)
	UFUNCTION(Exec) void CampusShot(const FString& Name);
	/** Screenshot every Nth frame for Count frames (Name_000, ...); run with -benchmark -fps=30 for evenly spaced frames. */
	UFUNCTION(Exec) void CampusShotSeq(const FString& Name, int32 Count, int32 Every = 1);
	UFUNCTION(Exec) void CampusMenu(const FString& Page, int32 Slot = -1);
	UFUNCTION(Exec) void CampusAim(int32 bOn);
	UFUNCTION(Exec) void CampusFire(int32 bOn);
	UFUNCTION(Exec) void CampusView(float X, float Y, float Z, float Yaw, float Pitch);
	UFUNCTION(Exec) void CampusHost(const FString& Mode);
	UFUNCTION(Exec) void CampusDebugVM();
	/** Holds the first-person reload at a fixed progress (0..1) for captures; negative releases it. */
	UFUNCTION(Exec) void CampusReloadPose(float Progress, int32 bEmpty = 0);
	UFUNCTION(Exec) void CampusLookAtBot();
	/** Same, from Distance cm away, on whichever side of the enemy has a clear line of sight. */
	UFUNCTION(Exec) void CampusLookAtBotFrom(float Distance);
	/** Stands Distance cm in front of the nearest intact window pane (indoor side when there is floor), aiming at it. */
	UFUNCTION(Exec) void CampusLookAtWindow(float Distance);
	/** Stands Distance in front of the nearest closed door, looking at its vision panel. */
	UFUNCTION(Exec) void CampusLookAtDoor(float Distance);
	/** Kills the nearest enemy as if shot from the local player (chest or head), with the blood hit FX. */
	UFUNCTION(Exec) void CampusKillBot(int32 bHead);
	/** Ends the match for the local player's team: 1 won, 0 lost, 2 draw (the result screen). */
	UFUNCTION(Exec) void CampusEndMatch(int32 Result);
	/** Test: freezes the nearest enemy's brain, turns it Yaw degrees off the line to the local player, and (bThrow) has it throw a frag that way. */
	UFUNCTION(Exec) void CampusBotThrow(float Yaw, int32 bThrow);
	/** Logs navmesh state and every bot's brain (state, path, velocity, goal). */
	UFUNCTION(Exec) void CampusBots();
	/** Logs every instanced mesh with an instance whose bounds centre lies within Radius of (X, Y, Z). */
	UFUNCTION(Exec) void CampusProbe(float X, float Y, float Z, float Radius);
	/** Logs what the camera's centre ray hits: actor, component, mesh and material. */
	UFUNCTION(Exec) void CampusAimTrace();
	/** Logs each tree's collision and whether a bullet trace and a player-sized sweep at its trunk are stopped. */
	UFUNCTION(Exec) void CampusTreeCheck();
	UFUNCTION(Exec) void CampusGod();
	/** Test: re-equips the local player with WeaponId (comma separated attachment ids), keeping the rest of the loadout. */
	UFUNCTION(Exec) void CampusEquip(const FString& WeaponId, const FString& Attachments = TEXT(""));
	/** Survey camera: the local pawn flies without collision and the first-person arms / weapon are hidden (0 restores). */
	UFUNCTION(Exec) void CampusFreeCam(int32 bOn);
	/** Drive the local character: sprint, tac, slide, jump, crouch, reload, switch, inspect, melee, fire, unfire, aim, unaim, stop. */
	UFUNCTION(Exec) void CampusAct(const FString& Action);
	/** Continuous movement input (X right, Y forward) until "CampusMove 0 0". */
	UFUNCTION(Exec) void CampusMove(float X, float Y);
	/** Logs the local pawn's location and velocity, tagged. */
	UFUNCTION(Exec) void CampusWhere(const FString& Tag);
	/** Sets the sun's volumetric fog scattering (tuning smoke and god rays). */
	UFUNCTION(Exec) void CampusSunScatter(float Intensity);
	/** Tune a viewmodel pose (hip, sprint, tac, slide, block; camera space cm / degrees) and the viewmodel FOV (0 keeps it; pose "fov" only sets the FOV). */
	UFUNCTION(Exec) void CampusPose(const FString& Pose, float X, float Y, float Z, float Pitch, float Yaw, float Roll, float ViewmodelFOV = 0.f);
	/** Applies a graphics preset (0 low .. 3 ultra) to the profile's settings. */
	UFUNCTION(Exec) void CampusPreset(int32 Preset);
	/** Anti-aliasing (0 TSR .. 4 DLSS), DLSS mode (0 DLAA .. 4 ultra performance), frame generation (0 off, 1-3 = 2x-4x, 4 dynamic). */
	UFUNCTION(Exec) void CampusUpscale(int32 AntiAliasing, int32 DLSSMode, int32 FrameGeneration);
	/** Sets one video setting and applies it: Scale, AA, AAQ, GI, Shadows, Refl, Tex, FX, View, Post, Sharp. */
	UFUNCTION(Exec) void CampusGfx(const FString& Setting, float Value);
	/** Writes the 特勤处 screens' pictures and live feeds to Saved/AutoShots/hqs_*.png. */
	UFUNCTION(Exec) void CampusDumpScreens();
	/** Tunes the haze at runtime: height fog density, falloff, start distance, max opacity; sky aerosol (Mie) scale and aerial perspective scale. */
	UFUNCTION(Exec) void CampusFog(float Density, float Falloff, float Start, float MaxOpacity, float Mie, float Aerial);
	/** Tunes the fog's light: the sky atmosphere's height fog contribution and the fog's ambient grey scale. */
	UFUNCTION(Exec) void CampusFogSky(float Contribution, float AmbientScale);
	/** Forces the weather (ECampusWeather) and time of day (ECampusTimeOfDay) on this machine. */
	UFUNCTION(Exec) void CampusWeather(int32 Weather, int32 Time);
	virtual void Shutdown() override;

private:
	void EnsureLoadouts();
	bool TickScript(float DeltaTime);

	UPROPERTY() FCampusProfile Profile;

	struct FScriptStep
	{
		float Time = 0.f;
		FString Command;
	};
	TArray<FScriptStep> Script;
	int32 ScriptIndex = 0;
	float ScriptClock = 0.f;
	bool bScriptWaitingForShaders = false;
	FTSTicker::FDelegateHandle ScriptTicker;
	FString ShotSeqName;
	int32 ShotSeqRemaining = 0;
	int32 ShotSeqEvery = 1;
	int32 ShotSeqFrame = 0;
	int32 ShotSeqIndex = 0;
};
