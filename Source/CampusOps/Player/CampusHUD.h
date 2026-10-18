#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Fonts/SlateFontInfo.h"
#include "Game/CampusGameState.h"
#include "Player/CampusCombatPresentation.h"
#include "UI/CampusUIStyle.h"
#include "CampusHUD.generated.h"

class ACampusCharacter;
class ACampusPlayerController;
class ACampusWeapon;

UCLASS()
class CAMPUSOPS_API ACampusHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACampusHUD();
	virtual void DrawHUD() override;

	/** Damage scales the hitmarker and the crosshair kick; rapid hits stack up to a cap. */
	void AddHitmarker(bool bKill, bool bHeadshot, float Damage = 30.f);
	void AddKillNotice(const FString& VictimName, bool bHeadshot);
	void AddDamageIndicator(const FVector& SourceLocation, float Damage);
	void AddScoreEvent(ECampusScoreEvent Event, int32 Points);
	void ShowBanner(const FText& Title, const FText& Subtitle, const FLinearColor& Color, float Duration);
	void OnMatchIntro();
	void OnMatchStarted();

private:
	struct FHitmarker { float Time; bool bKill; bool bHead; float Strength; };
	struct FDamageIndicator { float Time; FVector Source; float Strength; };
	struct FScorePopup { float Time; FText Text; int32 Points; bool bMedal; ECampusScoreEvent Event; };
	/** Blood splatter on the screen edge after taking damage (biased toward the attacker). */
	struct FBloodSplat { float Time; FVector2D Pos; float Size; float Rot; float Strength; float Life; };

	enum class ECrosshair : uint8 { Cross, Dot, T, Chevron, Count };

	/** Font + size (1080p pixels) + weight + tracking (1/1000 em). */
	struct FHudFont
	{
		CampusUI::EFont Family = CampusUI::EFont::Sans;
		float Size = 16.f;
		FName Weight = TEXT("Bold");
		int32 Tracking = 0;
	};
	static FHudFont F(CampusUI::EFont Family, float Size, const TCHAR* Weight = TEXT("Bold"), int32 Tracking = 0) { return { Family, Size, FName(Weight), Tracking }; }

	TArray<FHitmarker> Hitmarkers;
	TArray<TWeakObjectPtr<ACampusCharacter>> TrackedCharacters;
	TWeakObjectPtr<ACampusCharacter> ViewCharacter;
	float NextCharacterRefresh = 0.f;
	/** 0..1 while an enemy is under the crosshair (it tints toward red). */
	float EnemyUnderAim = 0.f;
	TArray<FDamageIndicator> DamageIndicators;
	TArray<FScorePopup> Popups;
	TArray<FBloodSplat> BloodSplats;
	/** Crosshair hit kick: extra gap scale, decaying back to rest. */
	float HitPulse = 0.f;
	/** Round / match result screen: when it started, and whether the slam's sound has gone off. */
	float ResultStartTime = -100.f;
	bool bResultSlamPlayed = false;
	float XPPopTime = -100.f;
	FText BannerTitle;
	FText BannerSubtitle;
	FLinearColor BannerColor = FLinearColor::White;
	float BannerTime = -100.f;
	float BannerDuration = 0.f;
	bool bCompactBanner = false;
	float LastHitTime = -100.f;
	float LastKillTime = -100.f;
	bool bLastKillHeadshot = false;
	FString KillVictimName;
	CampusCombatPresentation::FMedalQueue MedalQueue;
	float PointsFlashTime = -100.f;
	int32 RecentPoints = 0;
	ECampusMatchPhase LastPhase = ECampusMatchPhase::Warmup;
	float SmoothedSpread = 0.f;
	int32 LastMag = -1;
	TWeakObjectPtr<const ACampusWeapon> AmmoWeapon;
	float MagChangeTime = -100.f;

	float S = 1.f;
	FVector2D Center = FVector2D::ZeroVector;
	float Now = 0.f;
	float Dt = 0.f;

	ACampusCharacter* GetViewCharacter() const;
	ACampusPlayerController* GetCampusPC() const;
	ECampusTeam GetLocalTeam() const;
	void UpdateTrackedCharacters(ACampusCharacter* C);

	void DrawCrosshair(ACampusCharacter* C);
	ECrosshair GetCrosshairStyle() const;
	FLinearColor GetCrosshairColor() const;
	void DrawHitmarkers();
	void DrawDoorPrompt(ACampusCharacter* C);
	void DrawDamageIndicators(ACampusCharacter* C);
	void DrawLowHealth(ACampusCharacter* C);
	void DrawBloodSplats(ACampusCharacter* C);
	void DrawAmmo(ACampusCharacter* C);
	/** Lethal / tactical counts beside the ammo, the cook timer round the crosshair. */
	void DrawEquipment(ACampusCharacter* C);
	/** Gauge of the breath left while holding it on the sights. */
	void DrawBreathHold(ACampusCharacter* C);
	/** Live frags nearby: an icon over each (or at the edge of the view, pointing to it). */
	void DrawGrenadeWarnings(ACampusCharacter* C);
	/** Flashbang whiteout and the burned-in afterimage. */
	void DrawFlash(ACampusCharacter* C);
	/** Small line icon of a grenade, centred on Pos, Size ~ its height in pixels. */
	void GrenadeIcon(ECampusThrowable Type, const FVector2D& Pos, float Size, const FLinearColor& Color);
	void DrawCompass(ACampusCharacter* C);
	void DrawScore(const ACampusGameState* GS, float Top);
	void DrawKillFeed(const ACampusGameState* GS);
	void DrawPopups();
	void DrawKillNotice();
	void DrawMedal();
	void DrawMedalIcon(ECampusScoreEvent Event, const FVector2D& Position, float Radius, const FLinearColor& Color);
	float DrawRadar(ACampusCharacter* C, const ACampusGameState* GS);
	void DrawNameplates(ACampusCharacter* C);
	/** Recon specialty wall-hack pings and the High Alert edge warning. */
	void DrawPerkAwareness(ACampusCharacter* C);
	void DrawScope(ACampusCharacter* C, float Alpha);
	void DrawScoreboard(const ACampusGameState* GS);
	void DrawDeathScreen(ACampusCharacter* C);
	void DrawPhaseInfo(const ACampusGameState* GS);
	/** COD-style VICTORY / DEFEAT (and round won / lost): the title slams in, then a match result makes way for the scoreboard. */
	void DrawResult(const ACampusGameState* GS);
	/** Additive tile (glows, flares). */
	void Glow(CampusUI::ETex Id, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color);
	void DrawBanner();
	void DrawFlagMarker(const ACampusGameState* GS);
	/** Covers the half-loaded world until ACampusPlayerController::IsWorldLoaded, then fades away. */
	void DrawLoading(const ACampusPlayerController* PC);
	void DrawFPS();

	FSlateFontInfo SlateFont(const FHudFont& Font) const;
	FVector2D Measure(const FString& Str, const FHudFont& Font) const;
	FVector2D TextInkVerticalBounds(const FString& Str, const FHudFont& Font) const;
	void Text(const FString& Str, const FVector2D& Pos, const FHudFont& Font, const FLinearColor& Color, float AlignX = 0.f, bool bShadow = true);
	void Rect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color);
	void Tex(CampusUI::ETex Id, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color, float RotationDeg = 0.f);
	/** Anti-aliased line drawn as a rotated feathered quad. */
	void SoftLine(const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color);
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness);
	void Tri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);
	void Disc(const FVector2D& C, float Radius, const FLinearColor& Color);
	void Ring(const FVector2D& C, float Radius, const FLinearColor& Color);
	/** Keycap outline with a key label; returns its width. */
	float KeyCap(const FString& Key, const FVector2D& Pos, float AlignX);
	/** Heading in degrees (0 = north on the minimap, clockwise). */
	static float Bearing(const FVector& Direction);
};
