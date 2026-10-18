#include "Player/CampusHUD.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusPlayerController.h"
#include "Player/CampusViewmodelComponent.h"
#include "Player/CampusMovementComponent.h"
#include "Player/CampusBreathComponent.h"
#include "Player/CampusInputConfig.h"
#include "Game/CampusMaps.h"
#include "Weapons/CampusWeapon.h"
#include "Weapons/CampusThrowable.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Game/CampusPlayerState.h"
#include "Game/CampusGameInstance.h"
#include "Map/CampusSchoolBuilder.h"
#include "Map/CampusDoor.h"
#include "CampusArmory.h"
#include "CampusPerks.h"
#include "Game/CampusGameState.h"
#include "CampusAssets.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/FontCache.h"
#include "EngineFontServices.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "TextureResource.h"
#include "GlobalRenderResources.h"

#define LOCTEXT_NAMESPACE "CampusHUD"

using CampusUI::EFont;
using CampusUI::ETex;

namespace
{
	const FLinearColor HudWhite(1.f, 1.f, 1.f, 1.f);
	const FLinearColor HudShadow(0.f, 0.f, 0.f, 0.6f);

	FLinearColor WithAlpha(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	FString FormatTime(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	FString WeaponName(FName Id)
	{
		if (Id == TEXT("Melee")) return TEXT("近战");
		if (Id == TEXT("World")) return TEXT("坠落");
		for (int32 i = 0; i < (int32)ECampusThrowable::Count; ++i)
		{
			if (Id == CampusText::ThrowableId((ECampusThrowable)i))
			{
				return CampusText::ThrowableName((ECampusThrowable)i).ToString();
			}
		}
		if (const FCampusWeaponDef* Def = UCampusArmory::Get().FindWeapon(Id))
		{
			return Def->DisplayName.ToString();
		}
		return Id.ToString();
	}

	float Smooth01(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}

	/** Stable pseudo-random 0..1 per seed (particle layouts that must not change from frame to frame). */
	float Hash01(float Seed)
	{
		return FMath::Frac(FMath::Sin(Seed * 12.9898f + 78.233f) * 43758.5453f);
	}
}

ACampusHUD::ACampusHUD()
{
}

ACampusCharacter* ACampusHUD::GetViewCharacter() const
{
	return PlayerOwner ? Cast<ACampusCharacter>(PlayerOwner->GetPawn()) : nullptr;
}

ACampusPlayerController* ACampusHUD::GetCampusPC() const
{
	return Cast<ACampusPlayerController>(PlayerOwner);
}

ECampusTeam ACampusHUD::GetLocalTeam() const
{
	const ACampusPlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<ACampusPlayerState>() : nullptr;
	return PS ? PS->Team : ECampusTeam::None;
}

void ACampusHUD::UpdateTrackedCharacters(ACampusCharacter* C)
{
	if (ViewCharacter.Get() != C)
	{
		ViewCharacter = C;
		NextCharacterRefresh = 0.f;
		Hitmarkers.Reset();
		DamageIndicators.Reset();
		BloodSplats.Reset();
		HitPulse = 0.f;
		EnemyUnderAim = 0.f;
		SmoothedSpread = 0.f;
	}
	if (Now < NextCharacterRefresh)
	{
		return;
	}
	// share the roster across HUD layers; positions and alive state are still read every frame
	NextCharacterRefresh = Now + 0.25f;
	TrackedCharacters.Reset();
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		TrackedCharacters.Add(*It);
	}
}

float ACampusHUD::Bearing(const FVector& Direction)
{
	// Matches the minimap: north is world +Y, east is world -X.
	return FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(-Direction.X, Direction.Y)) + 360.f, 360.f);
}

// ------------------------------------------------------------------ primitives

FSlateFontInfo ACampusHUD::SlateFont(const FHudFont& Font) const
{
	return CampusUI::Font(Font.Family, FMath::Max(1.f, FMath::RoundToFloat(Font.Size * S)), Font.Weight, Font.Tracking);
}

FVector2D ACampusHUD::Measure(const FString& Str, const FHudFont& Font) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return FVector2D(Str.Len() * Font.Size * S * 0.6f, Font.Size * S * 1.3f);
	}
	const TSharedRef<FSlateFontMeasure> Service = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return Service->Measure(Str, SlateFont(Font));
}

FVector2D ACampusHUD::TextInkVerticalBounds(const FString& Str, const FHudFont& Font) const
{
	if (!FEngineFontServices::IsInitialized()) return FVector2D(0.f, Measure(Str, Font).Y);
	const TSharedPtr<FSlateFontCache> Cache = FEngineFontServices::Get().GetFontCache();
	if (!Cache.IsValid()) return FVector2D(0.f, Measure(Str, Font).Y);
	const FSlateFontInfo Info = SlateFont(Font);
	FCharacterList& Characters = Cache->GetCharacterList(Info, Canvas->GetDPIScale());
	float Top = TNumericLimits<float>::Max();
	float Bottom = TNumericLimits<float>::Lowest();
	for (const TCHAR Ch : Str)
	{
		if (FChar::IsWhitespace(Ch)) continue;
		const FCharacterEntry& Entry = Characters.GetCharacter(Ch, Info.FontFallback);
		if (!Entry.Valid || Entry.BitmapRenderScale <= 0.f) continue;
		// match Canvas runtime glyph placement, including the CJK fallback's own descender
		const float GlyphTop = -Entry.VerticalOffset + (Entry.GlobalDescender + Characters.GetMaxHeight()) / Entry.BitmapRenderScale;
		Top = FMath::Min(Top, GlyphTop);
		Bottom = FMath::Max(Bottom, GlyphTop + Entry.VSize * Entry.BitmapRenderScale);
	}
	return Top <= Bottom ? FVector2D(Top, Bottom) : FVector2D(0.f, Measure(Str, Font).Y);
}

void ACampusHUD::Text(const FString& Str, const FVector2D& Pos, const FHudFont& Font, const FLinearColor& Color, float AlignX, bool bShadow)
{
	if (Str.IsEmpty() || Color.A <= 0.01f)
	{
		return;
	}
	FVector2D P = Pos;
	if (AlignX != 0.f)
	{
		P.X -= Measure(Str, Font).X * AlignX;
	}
	P.X = FMath::RoundToFloat(P.X);
	P.Y = FMath::RoundToFloat(P.Y);
	const FSlateFontInfo Info = SlateFont(Font);
	FCanvasTextItem Item(P, FText::FromString(Str), Info, Color);
	const UFont* Asset = Cast<const UFont>(Info.FontObject.Get());
	if (!Asset)
	{
		static TWeakObjectPtr<const UFont> Fallback;
		if (!Fallback.IsValid())
		{
			Fallback = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}
		Asset = Fallback.Get();
	}
	Item.Font = Asset;
	if (!Item.Font)
	{
		return;
	}
	if (bShadow)
	{
		Item.EnableShadow(WithAlpha(HudShadow, Color.A), FVector2D(0.f, 1.5f * S));
	}
	Canvas->DrawItem(Item);
}

void ACampusHUD::Rect(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color)
{
	if (Color.A <= 0.005f)
	{
		return;
	}
	FCanvasTileItem Tile(Pos, Size, Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ACampusHUD::Tex(ETex Id, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color, float RotationDeg)
{
	if (Color.A <= 0.005f)
	{
		return;
	}
	UTexture2D* T = CampusUI::Texture(Id);
	if (!T || !T->GetResource())
	{
		Rect(Pos, Size, Color);
		return;
	}
	FCanvasTileItem Tile(Pos, T->GetResource(), Size, Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	if (RotationDeg != 0.f)
	{
		Tile.Rotation = FRotator(0.f, RotationDeg, 0.f);
		Tile.PivotPoint = FVector2D(0.5f, 0.5f);
	}
	Canvas->DrawItem(Tile);
}

void ACampusHUD::SoftLine(const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color)
{
	const FVector2D D = B - A;
	const float Len = D.Size();
	if (Len < 0.5f)
	{
		return;
	}
	// The line texture is 16px tall with a 14px core, so scale the quad to keep the requested core thickness.
	const float H = Thickness * 16.f / 14.f;
	const FVector2D Mid = (A + B) * 0.5f;
	Tex(ETex::Line, Mid - FVector2D(Len, H) * 0.5f, FVector2D(Len, H), Color, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)));
}

void ACampusHUD::Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness)
{
	FCanvasLineItem Item(A, B);
	Item.SetColor(Color);
	Item.LineThickness = Thickness;
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void ACampusHUD::Tri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	FCanvasTriangleItem Item(A, B, C, GWhiteTexture);
	Item.SetColor(Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void ACampusHUD::Disc(const FVector2D& C, float Radius, const FLinearColor& Color)
{
	Tex(ETex::Disc, C - FVector2D(Radius, Radius) * (64.f / 62.f), FVector2D(Radius, Radius) * (128.f / 62.f), Color);
}

void ACampusHUD::Ring(const FVector2D& C, float Radius, const FLinearColor& Color)
{
	const float Half = Radius * 256.f / 252.f;
	Tex(ETex::Ring, C - FVector2D(Half, Half), FVector2D(Half, Half) * 2.f, Color);
}

float ACampusHUD::KeyCap(const FString& Key, const FVector2D& Pos, float AlignX)
{
	const FHudFont KF = F(EFont::Cond, 14, TEXT("Bold"), 60);
	const FVector2D Sz = Measure(Key, KF);
	const float W = FMath::Max(Sz.X + 12.f * S, 22.f * S);
	const float H = 22.f * S;
	const FVector2D P(Pos.X - W * AlignX, Pos.Y);
	Rect(P, FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, 0.45f));
	const FLinearColor Edge(1.f, 1.f, 1.f, 0.6f);
	Rect(P, FVector2D(W, 1.f), Edge);
	Rect(P + FVector2D(0.f, H - 1.f), FVector2D(W, 1.f), Edge);
	Rect(P, FVector2D(1.f, H), Edge);
	Rect(P + FVector2D(W - 1.f, 0.f), FVector2D(1.f, H), Edge);
	Text(Key, FVector2D(P.X + W * 0.5f, P.Y + (H - Sz.Y) * 0.5f), KF, HudWhite, 0.5f, false);
	return W;
}

// ------------------------------------------------------------------ events

void ACampusHUD::AddHitmarker(bool bKill, bool bHeadshot, float Damage)
{
	const float K = FMath::Clamp(Damage / 35.f, 0.35f, 1.6f) * (bHeadshot ? 1.3f : 1.f) * (bKill ? 1.5f : 1.f);
	Hitmarkers.Add({ (float)GetWorld()->GetTimeSeconds(), bKill, bHeadshot, K });
	// Every hit nudges the crosshair open by how hard it landed; rapid fire stacks toward a cap.
	HitPulse = FMath::Min(HitPulse + 0.22f * K, 0.9f);
	LastHitTime = GetWorld()->GetTimeSeconds();
	if (Hitmarkers.Num() > 6)
	{
		Hitmarkers.RemoveAt(0);
	}
}

void ACampusHUD::AddKillNotice(const FString& VictimName, bool bHeadshot)
{
	KillVictimName = CampusCombatPresentation::CleanPlayerName(VictimName);
	LastKillTime = GetWorld()->GetTimeSeconds();
	bLastKillHeadshot = bHeadshot;
}

void ACampusHUD::AddDamageIndicator(const FVector& SourceLocation, float Damage)
{
	const float T = GetWorld()->GetTimeSeconds();
	const float Strength = FMath::Clamp(Damage / 40.f, 0.4f, 1.f);
	const FVector Origin = PlayerOwner && PlayerOwner->GetPawn() ? PlayerOwner->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	const FVector Direction = (SourceLocation - Origin).GetSafeNormal2D();
	const float MergeCos = FMath::Cos(FMath::DegreesToRadians(15.f));
	bool bMerged = false;
	for (FDamageIndicator& D : DamageIndicators)
	{
		if (T - D.Time < 0.35f && FVector::DotProduct(Direction, (D.Source - Origin).GetSafeNormal2D()) > MergeCos)
		{
			// automatic fire from one direction should read as one sustained threat
			D.Time = T;
			D.Source = SourceLocation;
			D.Strength = FMath::Min(1.f, FMath::Max(D.Strength, Strength) + 0.1f);
			bMerged = true;
			break;
		}
	}
	if (!bMerged)
	{
		DamageIndicators.Add({ T, SourceLocation, Strength });
	}
	if (DamageIndicators.Num() > 4)
	{
		int32 Oldest = 0;
		for (int32 i = 1; i < DamageIndicators.Num(); ++i)
		{
			if (DamageIndicators[i].Time < DamageIndicators[Oldest].Time)
			{
				Oldest = i;
			}
		}
		DamageIndicators.RemoveAt(Oldest);
	}

	// Blood on the lens, thrown toward the screen edge on the side the shot came from (ahead = top edge).
	float Angle = FMath::FRandRange(-PI, PI);
	if (PlayerOwner)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
		const FVector Local = FRotator(0.f, CamRot.Yaw, 0.f).UnrotateVector(SourceLocation - CamLoc);
		Angle = FMath::Atan2(Local.Y, Local.X);
	}
	const int32 Count = Damage > 45.f ? 2 : 1;
	for (int32 i = 0; i < Count; ++i)
	{
		const float A = Angle + FMath::FRandRange(-1.f, 1.f);
		const FVector2D Dir(FMath::Sin(A), -FMath::Cos(A));
		FVector2D Pos = FVector2D(0.5f, 0.5f) + Dir * 0.48f;
		// pin the drop to one edge so even its rotated bounds cannot reach the aiming area
		if (FMath::Abs(Dir.X) > FMath::Abs(Dir.Y)) Pos.X = Dir.X > 0.f ? 0.99f : 0.01f;
		else Pos.Y = Dir.Y > 0.f ? 0.99f : 0.01f;
		const float Size = FMath::FRandRange(0.10f, 0.16f) * FMath::Clamp(Damage / 35.f, 0.75f, 1.2f);
		BloodSplats.Add({ T, Pos, Size, FMath::FRandRange(0.f, 360.f), FMath::Clamp(Damage / 90.f, 0.2f, 0.45f), FMath::FRandRange(0.65f, 1.f) });
	}
	while (BloodSplats.Num() > 3)
	{
		BloodSplats.RemoveAt(0);
	}
}

void ACampusHUD::AddScoreEvent(ECampusScoreEvent Event, int32 Points)
{
	const bool bMedal = CampusCombatPresentation::MedalPriority(Event) > 0;
	MedalQueue.Push(Event, GetWorld()->GetTimeSeconds());
	if (!bMedal)
	{
		Popups.RemoveAll([Event](const FScorePopup& Popup) { return Popup.Event == Event; });
		Popups.Add({ (float)GetWorld()->GetTimeSeconds(), CampusText::ScoreEventName(Event), Points, false, Event });
	}
	if (Popups.Num() > 5)
	{
		Popups.RemoveAt(0);
	}
	if (GetWorld()->GetTimeSeconds() - PointsFlashTime > 2.8f)
	{
		RecentPoints = 0;
	}
	RecentPoints += Points;
	PointsFlashTime = GetWorld()->GetTimeSeconds();
	XPPopTime = PointsFlashTime;
}

void ACampusHUD::ShowBanner(const FText& Title, const FText& Subtitle, const FLinearColor& Color, float Duration)
{
	BannerTitle = Title;
	BannerSubtitle = Subtitle;
	BannerColor = Color;
	BannerTime = GetWorld()->GetTimeSeconds();
	BannerDuration = Duration;
	bCompactBanner = false;
}

void ACampusHUD::OnMatchIntro()
{
	MedalQueue.Reset();
	KillVictimName.Reset();
	Popups.Reset();
	RecentPoints = 0;
	const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>();
	if (!GS)
	{
		return;
	}
	if (GS->Mode == ECampusMatchMode::Gunfight)
	{
		const FText Sub = FText::Format(LOCTEXT("GFRound", "第 {0} 回合  ·  本回合武器：{1}"), FText::AsNumber(GS->RoundNumber), FText::FromString(GS->RoundLoadoutName));
		ShowBanner(LOCTEXT("GFTitle", "枪战"), Sub, CampusUI::Accent(), 3.f);
	}
	else
	{
		const FText Sub = FText::Format(LOCTEXT("TDMSub", "消灭敌方小队  ·  率先拿下 {0} 分的队伍获胜"), FText::AsNumber(GS->ScoreLimit));
		ShowBanner(LOCTEXT("TDMTitle", "团队死斗"), Sub, CampusUI::Accent(), 4.f);
	}
}

void ACampusHUD::OnMatchStarted()
{
	ShowBanner(LOCTEXT("Go", "交战开始"), FText::GetEmpty(), HudWhite, 0.9f);
	bCompactBanner = true;
}

// ------------------------------------------------------------------ main

void ACampusHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	S = Canvas->ClipY / 1080.f;
	Center = FVector2D(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	Now = GetWorld()->GetTimeSeconds();
	Dt = FMath::Clamp(GetWorld()->GetDeltaSeconds(), 0.0001f, 0.1f);
	HitPulse *= FMath::Exp(-14.f * Dt);

	ACampusPlayerController* PC = GetCampusPC();
	const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>();
	ACampusCharacter* C = GetViewCharacter();
	UpdateTrackedCharacters(C);

	if (GS)
	{
		if (GS->Phase != LastPhase)
		{
			if (GS->Phase == ECampusMatchPhase::RoundOver || GS->Phase == ECampusMatchPhase::MatchOver)
			{
				ResultStartTime = Now;
				bResultSlamPlayed = false;
				BannerTime = -100.f;
				// The build-up: a rising sweep that lands in a boom 0.75 s in, right where the title slams down.
				CampusAudio::Play2D(this, TEXT("UI_Deploy"), 0.95f, 0.f, CampusAudio::ECategory::Interface);
				// The weapon goes down out of the shot; the next round's pawn draws its own.
				if (C && C->Viewmodel && C->IsLocalHuman() && C->IsAlive())
				{
					C->Viewmodel->OnHolster(0.45f);
				}
			}
			LastPhase = GS->Phase;
		}
	}
	const bool bResult = GS && (GS->Phase == ECampusMatchPhase::RoundOver || GS->Phase == ECampusMatchPhase::MatchOver);

	float ScoreTop = 60.f * S;
	if (C && C->IsAlive() && !bResult)
	{
		const ACampusWeapon* W = C->GetCurrentWeapon();
		const float RawADS = C->GetADSAlpha();
		// Magnified optics draw their sight picture in the eyepiece itself (picture in picture); the HUD only drops the
		// crosshair while the eye is on it.
		const float ScopeAlpha = (W && W->HasScopeOptic()) ? Smooth01((RawADS - 0.62f) / 0.26f) : 0.f;
		DrawFlash(C);
		DrawLowHealth(C);
		DrawBloodSplats(C);
		DrawNameplates(C);
		DrawPerkAwareness(C);
		if (ScopeAlpha < 0.5f)
		{
			DrawCrosshair(C);
			DrawDoorPrompt(C);
		}
		DrawHitmarkers();
		DrawDamageIndicators(C);
		DrawGrenadeWarnings(C);
		DrawAmmo(C);
		DrawEquipment(C);
		ScoreTop = DrawRadar(C, GS);
		DrawCompass(C);
		DrawFlagMarker(GS);
	}
	else if (C && !bResult)
	{
		DrawHitmarkers();
		DrawDeathScreen(C);
	}

	if (bResult)
	{
		DrawResult(GS);
		if (GS->Phase == ECampusMatchPhase::RoundOver && PC && PC->IsScoreboardVisible())
		{
			DrawScoreboard(GS);
		}
	}
	else
	{
		if (GS)
		{
			DrawScore(GS, ScoreTop);
			DrawKillFeed(GS);
			DrawPhaseInfo(GS);
		}
		DrawPopups();
		DrawBanner();
		if (GS && PC && PC->IsScoreboardVisible())
		{
			DrawScoreboard(GS);
		}
	}
	// the round-winning kill must still show its name and award over the result screen
	DrawKillNotice();
	if (Now - BannerTime > BannerDuration && (!GS || GS->Phase != ECampusMatchPhase::Countdown) && (!PC || !PC->IsScoreboardVisible()))
	{
		DrawMedal();
	}
	DrawLoading(PC);
	DrawFPS();
}

void ACampusHUD::DrawLoading(const ACampusPlayerController* PC)
{
	if (!PC)
	{
		return;
	}
	const float Reveal = PC->GetRevealTime();
	const float Alpha = Reveal >= 0.f ? 1.f - Smooth01((Now - Reveal) / 0.6f) : 1.f;
	if (Alpha <= 0.f)
	{
		return;
	}
	Rect(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(0.012f, 0.014f, 0.018f, Alpha));
	const float Y = Canvas->ClipY * 0.5f;
	Text(LOCTEXT("LoadTitle", "正在部署").ToString(), FVector2D(Center.X, Y - 70.f * S), F(EFont::Sans, 40.f, TEXT("Black"), 200), WithAlpha(HudWhite, Alpha), 0.5f, false);
	const float W = 520.f * S;
	const float P = FMath::Clamp(PC->GetLoadProgress(), 0.f, 1.f);
	Rect(FVector2D(Center.X - W * 0.5f, Y), FVector2D(W, 4.f * S), FLinearColor(1.f, 1.f, 1.f, 0.12f * Alpha));
	Rect(FVector2D(Center.X - W * 0.5f, Y), FVector2D(W * P, 4.f * S), WithAlpha(CampusUI::Accent(), Alpha));
	// A sweep along the bar so a long shader compile still reads as working.
	const float Sweep = FMath::Fmod(Now * 0.6f, 1.f) * W;
	Rect(FVector2D(Center.X - W * 0.5f + Sweep, Y), FVector2D(FMath::Min(40.f * S, W - Sweep), 4.f * S), FLinearColor(1.f, 1.f, 1.f, 0.25f * Alpha));
	const FString Status = PC->IsWorldLoaded() ? LOCTEXT("LoadDone", "加载完成").ToString() : PC->GetLoadStatus().ToString();
	Text(Status, FVector2D(Center.X, Y + 22.f * S), F(EFont::Sans, 16.f, TEXT("Medium"), 40), WithAlpha(HudWhite, 0.75f * Alpha), 0.5f, false);
	Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(P * 100.f)), FVector2D(Center.X + W * 0.5f, Y - 28.f * S), F(EFont::Num, 18.f, TEXT("Bold")), WithAlpha(HudWhite, 0.6f * Alpha), 1.f, false);
}

// ------------------------------------------------------------------ crosshair & hit feedback

void ACampusHUD::DrawCrosshair(ACampusCharacter* C)
{
	ACampusWeapon* W = C->GetCurrentWeapon();
	if (!W)
	{
		return;
	}
	const float ADS = C->GetADSAlpha();
	float Alpha = 1.f - FMath::Clamp(ADS * 3.f, 0.f, 1.f);
	if (C->IsSprinting() || C->IsSwitchingWeapons() || (C->Viewmodel && C->Viewmodel->IsInspecting()))
	{
		Alpha = 0.f;
	}
	if (Alpha <= 0.01f)
	{
		return;
	}
	const float FOV = C->FirstPersonCamera ? C->FirstPersonCamera->FieldOfView : 90.f;
	// A shotgun's bars frame where its pellets will land, on top of the aim error.
	const float Spread = W->GetCurrentSpread() + (W->GetStats().Pellets > 1 ? W->GetStats().PelletSpread : 0.f);
	SmoothedSpread = FMath::FInterpTo(SmoothedSpread, Spread, Dt, 18.f);
	const float BaseGap = FMath::Max(6.f * S, FMath::Tan(FMath::DegreesToRadians(SmoothedSpread)) / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f)) * Canvas->ClipX * 0.5f);

	// Over an enemy the crosshair warms to red (an aim read, not an aim assist): a short trace down the view.
	{
		bool bEnemy = false;
		FVector CamLoc;
		FRotator CamRot;
		PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusCrosshairEnemy), false, C);
		if (GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, CamLoc + CamRot.Vector() * 6000.f, ECC_Visibility, Params))
		{
			const ACampusCharacter* Other = Cast<ACampusCharacter>(Hit.GetActor());
			bEnemy = Other && Other->IsAlive() && C->IsEnemyOf(Other);
		}
		EnemyUnderAim = FMath::FInterpConstantTo(EnemyUnderAim, bEnemy ? 1.f : 0.f, Dt, bEnemy ? 14.f : 6.f);
	}
	const FLinearColor BaseCol = FMath::Lerp(GetCrosshairColor(), FLinearColor(1.f, 0.16f, 0.12f), EnemyUnderAim * 0.85f);
	const ECrosshair Style = GetCrosshairStyle();

	// COD-style hip crosshair: short square bars around a gap that opens with the spread. A hit only nudges it open;
	// the X hitmarker carries the feedback. Snapped to whole pixels so the bars stay crisp.
	const float Gap = FMath::RoundToFloat(BaseGap * (1.f + 0.3f * HitPulse));
	const float Len = FMath::RoundToFloat(9.f * S);
	const float Thick = FMath::Max(2.f, FMath::RoundToFloat(2.f * S));
	const float Edge = FMath::Max(1.f, FMath::RoundToFloat(S));
	const FLinearColor Col = WithAlpha(BaseCol, 0.95f * Alpha);
	const FLinearColor Out = WithAlpha(FLinearColor::Black, 0.55f * Alpha);
	const FVector2D Mid(FMath::RoundToFloat(Center.X), FMath::RoundToFloat(Center.Y));
	auto Box = [&](const FVector2D& Pos, const FVector2D& Size)
	{
		Rect(Pos - FVector2D(Edge, Edge), Size + FVector2D(Edge, Edge) * 2.f, Out);
		Rect(Pos, Size, Col);
	};
	auto Bar = [&](int32 X, int32 Y)
	{
		if (X != 0)
		{
			const float From = X > 0 ? Mid.X + Gap : Mid.X - Gap - Len;
			Box(FVector2D(From, Mid.Y - Thick * 0.5f), FVector2D(Len, Thick));
		}
		else
		{
			const float From = Y > 0 ? Mid.Y + Gap : Mid.Y - Gap - Len;
			Box(FVector2D(Mid.X - Thick * 0.5f, From), FVector2D(Thick, Len));
		}
	};
	auto Dot = [&]()
	{
		Box(Mid - FVector2D(Thick, Thick) * 0.5f, FVector2D(Thick, Thick));
	};
	switch (Style)
	{
	case ECrosshair::Dot:
		Dot();
		break;
	case ECrosshair::T:
		Bar(1, 0);
		Bar(-1, 0);
		Bar(0, 1);
		break;
	case ECrosshair::Chevron:
	{
		const float L = 8.f * S + Gap * 0.6f;
		const float W2 = 2.f * S;
		SoftLine(Mid + FVector2D(-L, L * 0.75f), Mid + FVector2D(0.f, 1.2f * S), W2 + 2.2f * S, Out);
		SoftLine(Mid + FVector2D(0.f, 1.2f * S), Mid + FVector2D(L, L * 0.75f), W2 + 2.2f * S, Out);
		SoftLine(Mid + FVector2D(-L, L * 0.75f), Mid + FVector2D(0.f, 1.2f * S), W2, Col);
		SoftLine(Mid + FVector2D(0.f, 1.2f * S), Mid + FVector2D(L, L * 0.75f), W2, Col);
		break;
	}
	default:
		Bar(1, 0);
		Bar(-1, 0);
		Bar(0, 1);
		Bar(0, -1);
		break;
	}
}

ACampusHUD::ECrosshair ACampusHUD::GetCrosshairStyle() const
{
	const UCampusGameInstance* GI = GetWorld()->GetGameInstance<UCampusGameInstance>();
	const int32 Choice = GI ? GI->GetProfile().CrosshairStyle : 0;
	return Choice > 0 ? (ECrosshair)FMath::Clamp(Choice - 1, 0, (int32)ECrosshair::Count - 1) : ECrosshair::Cross;
}

FLinearColor ACampusHUD::GetCrosshairColor() const
{
	static const FLinearColor Colors[] = {
		FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.35f, 1.f, 0.35f), FLinearColor(0.3f, 0.95f, 1.f),
		FLinearColor(1.f, 0.92f, 0.25f), FLinearColor(1.f, 0.42f, 0.86f), FLinearColor(1.f, 0.26f, 0.2f) };
	const UCampusGameInstance* GI = GetWorld()->GetGameInstance<UCampusGameInstance>();
	return Colors[FMath::Clamp(GI ? GI->GetProfile().CrosshairColor : 0, 0, (int32)UE_ARRAY_COUNT(Colors) - 1)];
}

void ACampusHUD::DrawHitmarkers()
{
	Hitmarkers.RemoveAll([this](const FHitmarker& H) { return Now - H.Time > (H.bKill ? 0.5f : 0.28f); });
	int32 Visible = INDEX_NONE;
	int32 VisiblePriority = -1;
	for (int32 i = Hitmarkers.Num() - 1; i >= 0; --i)
	{
		const FHitmarker& H = Hitmarkers[i];
		const int32 Priority = (H.bKill ? 2 : 0) + (H.bHead ? 1 : 0);
		if (Priority > VisiblePriority)
		{
			Visible = i;
			VisiblePriority = Priority;
		}
	}
	if (Visible != INDEX_NONE)
	{
		// keep a kill or headshot legible when pellets and rapid hits arrive together
		const FHitmarker& H = Hitmarkers[Visible];
		const float Age = Now - H.Time;
		const float T = Age / (H.bKill ? 0.5f : 0.28f);
		const float Pop = 1.f + (H.bKill ? 0.55f : 0.35f) * FMath::Exp(-Age * 28.f);
		const float Size = 0.86f + 0.14f * H.Strength;
		const float Inner = (H.bKill ? 9.f : 7.f) * S * Pop * Size;
		const float Outer = Inner + (H.bKill ? 12.f : (H.bHead ? 11.f : 9.f)) * S * Size;
		const FLinearColor Col = H.bKill ? FLinearColor(1.f, 0.18f, 0.12f, 1.f - T * T) : (H.bHead ? FLinearColor(1.f, 0.86f, 0.35f, 1.f - T) : FLinearColor(1.f, 1.f, 1.f, 1.f - T));
		const float Thick = (H.bKill ? 3.f : 2.3f) * S;
		for (int32 q = 0; q < 4; ++q)
		{
			const float Ang = PI * 0.25f + q * PI * 0.5f;
			const FVector2D D(FMath::Cos(Ang), FMath::Sin(Ang));
			SoftLine(Center + D * Inner, Center + D * Outer, Thick + 2.5f * S, WithAlpha(FLinearColor::Black, Col.A * 0.55f));
			SoftLine(Center + D * Inner, Center + D * Outer, Thick, Col);
		}
	}
}

void ACampusHUD::DrawDamageIndicators(ACampusCharacter* C)
{
	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
	for (int32 i = DamageIndicators.Num() - 1; i >= 0; --i)
	{
		const FDamageIndicator& D = DamageIndicators[i];
		const float Age = Now - D.Time;
		if (Age > 1.8f)
		{
			DamageIndicators.RemoveAt(i);
			continue;
		}
		const FVector Local = FRotator(0.f, CamRot.Yaw, 0.f).UnrotateVector(D.Source - CamLoc);
		const float Angle = FMath::Atan2(Local.Y, Local.X);
		const float Alpha = (1.f - Age / 1.8f) * D.Strength;
		const float R0 = 136.f * S;
		const float R1 = 156.f * S;
		const int32 Segs = 10;
		const float Half = FMath::DegreesToRadians(22.f);
		auto P = [this](float Ang, float R) { return Center + FVector2D(FMath::Sin(Ang), -FMath::Cos(Ang)) * R; };
		for (int32 s = 0; s < Segs; ++s)
		{
			const float A0 = Angle - Half + 2.f * Half * s / Segs;
			const float A1 = Angle - Half + 2.f * Half * (s + 1) / Segs;
			const float Fade = FMath::Square(1.f - FMath::Abs((s + 0.5f) / Segs - 0.5f) * 2.f);
			const FLinearColor Core(0.95f, 0.08f, 0.04f, Alpha * Fade * 0.9f);
			const FLinearColor Glow(0.6f, 0.f, 0.f, Alpha * Fade * 0.35f);
			Tri(P(A0, R0 - 6.f * S), P(A1, R0 - 6.f * S), P(A1, R0), Glow);
			Tri(P(A0, R0 - 6.f * S), P(A1, R0), P(A0, R0), Glow);
			Tri(P(A0, R0), P(A1, R0), P(A1, R1), Core);
			Tri(P(A0, R0), P(A1, R1), P(A0, R1), Core);
		}
		// Arrow tip pointing at the source
		const FLinearColor Tip(1.f, 0.15f, 0.08f, Alpha);
		Tri(P(Angle, R1 + 12.f * S), P(Angle - 0.05f, R1 + 1.f * S), P(Angle + 0.05f, R1 + 1.f * S), Tip);
	}
}

void ACampusHUD::DrawLowHealth(ACampusCharacter* C)
{
	const float H = C->GetHealth01();
	const float RecentHit = FMath::Clamp(1.f - (Now - C->LastDamageTime) / 0.7f, 0.f, 1.f);
	const float Intensity = FMath::Max(FMath::Clamp((0.8f - H) / 0.8f, 0.f, 1.f), RecentHit * 0.28f);
	if (Intensity <= 0.01f)
	{
		return;
	}
	const float Pulse = H < 0.35f ? 0.82f + 0.18f * FMath::Sin(Now * 6.5f) : 1.f;
	const FVector2D Size(Canvas->ClipX, Canvas->ClipY);
	Tex(ETex::Blood, FVector2D::ZeroVector, Size, FLinearColor(0.42f, 0.f, 0.01f, 0.32f * Intensity * Pulse));
	Tex(ETex::Vignette, FVector2D::ZeroVector, Size, FLinearColor(0.f, 0.f, 0.f, 0.14f * Intensity));
}

void ACampusHUD::DrawBloodSplats(ACampusCharacter* C)
{
	const float Health = C->GetHealth01();
	for (int32 i = BloodSplats.Num() - 1; i >= 0; --i)
	{
		const FBloodSplat& B = BloodSplats[i];
		const float Age = Now - B.Time;
		if (Age > B.Life)
		{
			BloodSplats.RemoveAt(i);
			continue;
		}
		// Lands instantly, holds, then thins out; a badly hurt player keeps more of it on screen.
		const float In = FMath::Clamp(Age / 0.05f, 0.f, 1.f);
		const float Out = 1.f - Smooth01((Age - B.Life * 0.35f) / (B.Life * 0.65f));
		const float A = In * Out * B.Strength * (0.65f + 0.15f * (1.f - Health));
		const float Px = B.Size * Canvas->ClipY;
		const FVector2D Pos(B.Pos.X * Canvas->ClipX - Px * 0.5f, B.Pos.Y * Canvas->ClipY - Px * 0.5f);
		Tex(ETex::Splat, Pos, FVector2D(Px, Px), FLinearColor(0.30f, 0.004f, 0.006f, 0.65f * A), B.Rot);
	}
}

// ------------------------------------------------------------------ panels

void ACampusHUD::DrawAmmo(ACampusCharacter* C)
{
	const ACampusWeapon* W = C->GetCurrentWeapon();
	if (!W)
	{
		return;
	}
	if (AmmoWeapon.Get() != W)
	{
		AmmoWeapon = W;
		LastMag = W->GetAmmoInMag();
		MagChangeTime = -100.f;
	}
	const float Right = Canvas->ClipX - 64.f * S;
	const float Bottom = Canvas->ClipY - 54.f * S;
	if (W->GetDef() && W->GetDef()->IsMelee())
	{
		// A blade: its name and which buttons cut, no rounds.
		Tex(ETex::GradR, FVector2D(Right - 470.f * S, Bottom - 110.f * S), FVector2D(560.f * S, 150.f * S), FLinearColor(0.f, 0.f, 0.f, 0.5f));
		Text(W->GetDisplayName().ToString(), FVector2D(Right, Bottom - 96.f * S), F(EFont::Sans, 30.f, TEXT("Bold")), WithAlpha(HudWhite, 0.92f), 1.f);
		Text(TEXT("左键 轻挥   右键 重刺"), FVector2D(Right, Bottom - 44.f * S), F(EFont::Sans, 15.f, TEXT("Bold")), WithAlpha(CampusUI::Accent(), 0.9f), 1.f);
		return;
	}
	const int32 Mag = W->GetAmmoInMag();
	const int32 MagSize = W->GetStats().MagSize;
	const int32 Reserve = W->GetAmmoReserve();
	const bool bLow = Mag <= FMath::Max(1, MagSize / 4);
	if (Mag != LastMag)
	{
		if (Mag > LastMag)
		{
			MagChangeTime = Now;
		}
		LastMag = Mag;
	}
	const float Refill = FMath::Clamp(1.f - (Now - MagChangeTime) / 0.35f, 0.f, 1.f);
	const FLinearColor MagColor = Mag == 0 ? CampusUI::Enemy() : (bLow ? CampusUI::Warning() : HudWhite);

	Tex(ETex::GradR, FVector2D(Right - 470.f * S, Bottom - 150.f * S), FVector2D(560.f * S, 190.f * S), FLinearColor(0.f, 0.f, 0.f, 0.5f));

	const FHudFont MagFont = F(EFont::Num, 66.f, TEXT("Bold"));
	const bool bInfinite = ACampusGameState::GetRules(GetWorld()).bInfiniteAmmo;
	const FHudFont ResFont = bInfinite ? F(EFont::Sans, 34.f, TEXT("Bold")) : F(EFont::Num, 30.f, TEXT("SemiBold"));
	const FString MagStr = FString::FromInt(Mag);
	const FString ResStr = bInfinite ? FString(TEXT("∞")) : FString::FromInt(Reserve);
	const FVector2D ResSize = Measure(ResStr, ResFont);
	Text(ResStr, FVector2D(Right, Bottom - 68.f * S), ResFont, WithAlpha(HudWhite, 0.6f), 1.f);
	Rect(FVector2D(Right - ResSize.X - 13.f * S, Bottom - 66.f * S), FVector2D(2.f * S, 32.f * S), WithAlpha(HudWhite, 0.35f));
	Text(MagStr, FVector2D(Right - ResSize.X - 24.f * S, Bottom - 102.f * S - Refill * 4.f * S), MagFont, FMath::Lerp(MagColor, CampusUI::Accent(), Refill * 0.6f), 1.f);

	FString Name = W->GetDisplayName().ToString();
	Text(Name.ToUpper(), FVector2D(Right, Bottom - 136.f * S), F(EFont::Cond, 22.f, TEXT("Bold"), 90), WithAlpha(HudWhite, 0.92f), 1.f);
	const FString Mode = CampusText::FireModeName(W->GetFireMode()).ToString();
	const float NameW = Measure(Name.ToUpper(), F(EFont::Cond, 22.f, TEXT("Bold"), 90)).X;
	Text(Mode, FVector2D(Right - NameW - 14.f * S, Bottom - 133.f * S), F(EFont::Sans, 14.f, TEXT("Bold")), WithAlpha(CampusUI::Accent(), 0.9f), 1.f);
	if (W->HasFlashlight())
	{
		const FString ModeStr = Mode;
		const float ModeW = Measure(ModeStr, F(EFont::Sans, 14.f, TEXT("Bold"))).X;
		const bool bLit = W->IsFlashlightOn();
		const FString LightKey = UCampusInputConfig::Get()->GetKeyLabel(TEXT("Flashlight")).ToString();
		Text(FString::Printf(TEXT("[%s] 枪灯 %s"), *LightKey, bLit ? TEXT("开") : TEXT("关")), FVector2D(Right - NameW - ModeW - 30.f * S, Bottom - 133.f * S), F(EFont::Sans, 14.f, TEXT("Bold")),
			bLit ? WithAlpha(FLinearColor(1.f, 0.95f, 0.8f), 0.95f) : WithAlpha(HudWhite, 0.45f), 1.f);
	}

	// group large magazines so the strip keeps a fixed width and drawing cost
	const int32 PerTick = FMath::Max(1, FMath::DivideAndRoundUp(MagSize, 30));
	const int32 Ticks = FMath::Max(1, FMath::DivideAndRoundUp(MagSize, PerTick));
	const float TickGap = 2.f * S;
	const float TickW = FMath::Min(6.f * S, (150.f * S - (Ticks - 1) * TickGap) / Ticks);
	const float TickH = 11.f * S;
	for (int32 i = 0; i < Ticks; ++i)
	{
		const bool bFull = (i * PerTick) < Mag;
		const float X = Right - (i + 1) * (TickW + TickGap) + TickGap;
		Rect(FVector2D(X, Bottom - 24.f * S), FVector2D(TickW, TickH), bFull ? WithAlpha(MagColor, 0.9f) : WithAlpha(HudWhite, 0.14f));
	}

	if (W->IsReloading())
	{
		const float P = W->GetReloadProgress();
		const FHudFont RF = F(EFont::Sans, 15.f, TEXT("Bold"), 120);
		Text(TEXT("装填中"), Center + FVector2D(0.f, 64.f * S), RF, WithAlpha(HudWhite, 0.9f), 0.5f);
		Rect(Center + FVector2D(-70.f * S, 90.f * S), FVector2D(140.f * S, 3.f * S), WithAlpha(HudWhite, 0.18f));
		Rect(Center + FVector2D(-70.f * S, 90.f * S), FVector2D(140.f * S * P, 3.f * S), WithAlpha(CampusUI::Accent(), 0.95f));
	}
	else if (bLow && Reserve == 0 && !bInfinite)
	{
		const FString Label = Mag == 0 ? TEXT("弹药耗尽") : TEXT("备用弹药耗尽");
		Text(FString::Printf(TEXT("%s  ·  按 %s 切换副武器"), *Label, *UCampusInputConfig::Get()->GetKeyLabel(TEXT("Secondary")).ToString()), Center + FVector2D(0.f, 64.f * S), F(EFont::Sans, 16.f, TEXT("Bold")), CampusUI::Enemy(), 0.5f);
	}
	else if (bLow)
	{
		const float Blink = 0.65f + 0.35f * FMath::Sin(Now * 8.f);
		const FHudFont RF = F(EFont::Sans, 16.f, TEXT("Bold"), 80);
		const FString Label = TEXT("装填");
		const float LW = Measure(Label, RF).X;
		const FString ReloadKey = UCampusInputConfig::Get()->GetKeyLabel(TEXT("Reload")).ToString();
		const float KW = FMath::Max(Measure(ReloadKey, F(EFont::Cond, 14, TEXT("Bold"), 60)).X + 12.f * S, 24.f * S);
		const float Total = KW + 8.f * S + LW;
		KeyCap(ReloadKey, FVector2D(Center.X - Total * 0.5f, Center.Y + 62.f * S), 0.f);
		Text(Label, FVector2D(Center.X - Total * 0.5f + KW + 8.f * S, Center.Y + 62.f * S), RF, WithAlpha(CampusUI::Warning(), Blink));
	}

	const UCampusMovementComponent* Move = C->GetCampusMovement();
	if (Move && Move->GetTacSprintStamina01() < 0.999f)
	{
		const float StamW = 140.f * S;
		const FVector2D Pos(Center.X - StamW * 0.5f, Canvas->ClipY - 44.f * S);
		Rect(Pos, FVector2D(StamW, 3.f * S), WithAlpha(HudWhite, 0.15f));
		Rect(Pos, FVector2D(StamW * Move->GetTacSprintStamina01(), 3.f * S), WithAlpha(HudWhite, 0.75f));
	}
	DrawBreathHold(C);
}

void ACampusHUD::DrawBreathHold(ACampusCharacter* C)
{
	const UCampusBreathComponent* BC = C->BreathComp;
	// Breath held on the sights: the lungs left, centred under the crosshair.
	if (!BC || (!BC->IsHoldingBreath() && BC->GetBreathReserve01() >= 0.999f))
	{
		return;
	}
	const float BW = 120.f * S;
	const FVector2D Pos(Center.X - BW * 0.5f, Center.Y + 110.f * S);
	const float Reserve = BC->GetBreathReserve01();
	const FLinearColor Col = Reserve < 0.25f ? CampusUI::Warning() : HudWhite;
	Rect(Pos, FVector2D(BW, 2.f * S), WithAlpha(HudWhite, 0.14f));
	Rect(Pos, FVector2D(BW * Reserve, 2.f * S), WithAlpha(Col, BC->IsHoldingBreath() ? 0.85f : 0.4f));
	if (BC->IsHoldingBreath())
	{
		Text(TEXT("屏息"), Pos + FVector2D(BW * 0.5f, -20.f * S), F(EFont::Sans, 13.f, TEXT("Bold"), 200), WithAlpha(HudWhite, 0.75f), 0.5f);
	}
}

void ACampusHUD::GrenadeIcon(ECampusThrowable Type, const FVector2D& P, float Size, const FLinearColor& Color)
{
	const float U = Size / 24.f;
	const FLinearColor Dark(0.f, 0.f, 0.f, 0.55f * Color.A);
	auto Box = [&](float X0, float Y0, float X1, float Y1, const FLinearColor& Col) { Rect(P + FVector2D(X0, Y0) * U, FVector2D(X1 - X0, Y1 - Y0) * U, Col); };
	switch (Type)
	{
	case ECampusThrowable::Frag:
		// Egg body, fuse head, lever down the side and the pull ring.
		Disc(P + FVector2D(0.f, 3.f) * U, 7.6f * U, Color);
		Box(-2.4f, -9.5f, 2.4f, -4.f, Color);
		Box(2.4f, -8.5f, 4.4f, 6.f, Color);
		Ring(P + FVector2D(-5.f, -8.f) * U, 2.6f * U, Color);
		break;
	case ECampusThrowable::Thermite:
		Box(-5.f, -5.f, 5.f, 11.f, Color);
		Box(-2.4f, -10.f, 2.4f, -5.f, Color);
		Box(-5.f, 1.f, 5.f, 3.f, Dark);
		Box(2.4f, -9.f, 4.2f, 4.f, Color);
		break;
	case ECampusThrowable::Flash:
		Box(-3.6f, -6.f, 3.6f, 11.f, Color);
		Box(-2.f, -10.f, 2.f, -6.f, Color);
		for (int32 i = 0; i < 3; ++i)
		{
			Box(-2.f, -3.f + i * 4.5f, -0.6f, -1.2f + i * 4.5f, Dark);
			Box(0.6f, -3.f + i * 4.5f, 2.f, -1.2f + i * 4.5f, Dark);
		}
		break;
	default:
		Box(-5.f, -5.f, 5.f, 11.f, Color);
		Box(-2.4f, -10.f, 2.4f, -5.f, Color);
		Box(-5.f, -5.f, 5.f, -3.f, Dark);
		break;
	}
}

void ACampusHUD::DrawEquipment(ACampusCharacter* C)
{
	const float Right = Canvas->ClipX - 64.f * S;
	const float Y = Canvas->ClipY - 54.f * S - 182.f * S;
	const FHudFont CountFont = F(EFont::Num, 22.f, TEXT("Bold"));
	const bool bChanged = Now - C->GetSelectionChangeTime() < 1.6f;
	float X = Right;
	for (const bool bLethal : { true, false })
	{
		const ECampusThrowable Type = bLethal ? C->GetSelectedLethal() : C->GetSelectedTactical();
		const int32 Count = C->GetThrowableCount(Type);
		const FLinearColor Col = WithAlpha(HudWhite, Count > 0 ? 0.95f : 0.3f);
		const FString CountStr = FString::FromInt(Count);
		const float CW = Measure(CountStr, CountFont).X;
		Text(CountStr, FVector2D(X, Y - 14.f * S), CountFont, Col, 1.f);
		X -= CW + 22.f * S;
		GrenadeIcon(Type, FVector2D(X, Y), 26.f * S, Col);
		X -= 18.f * S;
		const float KW = KeyCap(UCampusInputConfig::Get()->GetKeyLabel(bLethal ? TEXT("Lethal") : TEXT("Tactical")).ToString(), FVector2D(X, Y - 11.f * S), 1.f);
		if (bChanged)
		{
			const float A = 1.f - Smooth01((Now - C->GetSelectionChangeTime() - 1.1f) / 0.5f);
			Text(CampusText::ThrowableName(Type).ToString(), FVector2D(X + 20.f * S, Y - 42.f * S), F(EFont::Sans, 14.f, TEXT("Bold")), WithAlpha(CampusUI::Accent(), A), 0.5f);
		}
		X -= KW + 30.f * S;
	}

	// Cooking a frag: the fuse burns down round the crosshair, red in the last second.
	const float Cook = C->GetCookTime();
	if (C->IsThrowing() && C->GetThrowingType() == ECampusThrowable::Frag && Cook > 0.f)
	{
		const float Fuse = CampusThrowables::Fuse(ECampusThrowable::Frag);
		const float Left = FMath::Clamp(1.f - Cook / Fuse, 0.f, 1.f);
		const FLinearColor RingCol = Left * Fuse < 1.f ? CampusUI::Enemy() : (Left * Fuse < 2.f ? CampusUI::Warning() : HudWhite);
		const float Radius = 26.f * S;
		const int32 Segs = 40;
		for (int32 i = 0; i < Segs; ++i)
		{
			const float A0 = -HALF_PI + 2.f * PI * i / Segs;
			const float A1 = -HALF_PI + 2.f * PI * (i + 1) / Segs;
			const bool bLit = (float)i / Segs < Left;
			SoftLine(Center + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius, Center + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, 3.f * S,
				bLit ? WithAlpha(RingCol, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.35f));
		}
		Text(FString::Printf(TEXT("%.1f"), Left * Fuse), Center + FVector2D(0.f, Radius + 8.f * S), F(EFont::Num, 16.f, TEXT("Bold")), RingCol, 0.5f);
	}
}

void ACampusHUD::DrawGrenadeWarnings(ACampusCharacter* C)
{
	TArray<ACampusThrowable*> Frags;
	ACampusThrowable::GetLiveFrags(GetWorld(), Frags);
	if (Frags.Num() == 0)
	{
		return;
	}
	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
	for (const ACampusThrowable* G : Frags)
	{
		const FVector At = G->GetActorLocation();
		const float D = FVector::Dist(At, CamLoc);
		if (D > 750.f)
		{
			continue;
		}
		const float Danger = 1.f - D / 750.f;
		const float Pulse = 0.7f + 0.3f * FMath::Sin(Now * (10.f + 12.f * Danger));
		const FLinearColor Col = FMath::Lerp(HudWhite, CampusUI::Enemy(), FMath::Clamp(Danger * 1.6f, 0.f, 1.f)) * FLinearColor(1.f, 1.f, 1.f, Pulse);
		const float Size = (30.f + 14.f * Danger) * S;
		FVector Screen = Canvas->Project(At + FVector(0.f, 0.f, 25.f));
		const FVector Local = CamRot.UnrotateVector(At - CamLoc);
		const bool bOnScreen = Local.X > 0.f && Screen.X > Size && Screen.X < Canvas->ClipX - Size && Screen.Y > Size && Screen.Y < Canvas->ClipY - Size;
		FVector2D P;
		if (bOnScreen)
		{
			P = FVector2D(Screen.X, Screen.Y - 24.f * S);
		}
		else
		{
			// Off screen: on a ring round the crosshair, with an arrow toward it.
			const FVector Flat = FRotator(0.f, CamRot.Yaw, 0.f).UnrotateVector(At - CamLoc);
			const float Ang = FMath::Atan2(Flat.Y, Flat.X);
			const FVector2D Dir(FMath::Sin(Ang), -FMath::Cos(Ang));
			P = Center + Dir * 190.f * S;
			const FVector2D Tip = Center + Dir * 222.f * S;
			const FVector2D Side(-Dir.Y, Dir.X);
			Tri(Tip, P + Dir * 18.f * S + Side * 9.f * S, P + Dir * 18.f * S - Side * 9.f * S, Col);
		}
		Disc(P, Size * 0.62f, FLinearColor(0.f, 0.f, 0.f, 0.45f * Pulse));
		GrenadeIcon(ECampusThrowable::Frag, P, Size * 0.75f, Col);
	}
}

void ACampusHUD::DrawFlash(ACampusCharacter* C)
{
	if (!C->Viewmodel)
	{
		return;
	}
	float Whiteout = 0.f;
	float Ghost = 0.f;
	C->Viewmodel->GetFlashState(Whiteout, Ghost);
	if (Whiteout <= 0.002f && Ghost <= 0.002f)
	{
		return;
	}
	const FVector2D Size(Canvas->ClipX, Canvas->ClipY);
	if (UTextureRenderTarget2D* Image = C->Viewmodel->GetFlashAfterimage(); Image && Image->GetResource() && Ghost > 0.002f)
	{
		// The burned-in frame: brighter than the world behind it, slowly dissolving.
		FCanvasTileItem Tile(FVector2D::ZeroVector, Image->GetResource(), Size, FLinearColor(1.f, 0.97f, 0.92f, 0.5f * Ghost));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
	Rect(FVector2D::ZeroVector, Size, FLinearColor(1.f, 1.f, 0.98f, FMath::Clamp(Whiteout * 1.05f, 0.f, 1.f)));
	if (UTextureRenderTarget2D* Image = C->Viewmodel->GetFlashAfterimage(); Image && Image->GetResource() && Ghost > 0.002f && Whiteout > 0.05f)
	{
		// Seen through the white as it lifts: the burned-in frame, a little brighter than the scene.
		FCanvasTileItem Tile(FVector2D::ZeroVector, Image->GetResource(), Size, FLinearColor(1.f, 0.96f, 0.9f, 0.45f * Ghost * (1.f - Whiteout * 0.6f)));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

void ACampusHUD::DrawCompass(ACampusCharacter* C)
{
	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
	const float Heading = Bearing(CamRot.Vector());
	const float Width = 620.f * S;
	const float Y = 26.f * S;
	const float Span = 110.f;
	const float PxPerDeg = Width / Span;
	const float X0 = Center.X;

	Tex(ETex::Band, FVector2D(X0 - Width * 0.6f, Y - 4.f * S), FVector2D(Width * 1.2f, 36.f * S), FLinearColor(0.f, 0.f, 0.f, 0.32f));
	auto EdgeFade = [&](float X) { const float U = FMath::Abs(X - X0) / (Width * 0.5f); return FMath::Clamp(1.f - U * U, 0.f, 1.f); };

	const TCHAR* Cardinal[8] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
	const int32 First = FMath::FloorToInt((Heading - Span * 0.5f) / 5.f) * 5;
	for (int32 Deg = First; Deg <= Heading + Span * 0.5f; Deg += 5)
	{
		const float Rel = FMath::UnwindDegrees((float)Deg - Heading);
		const float X = X0 + Rel * PxPerDeg;
		const float A = EdgeFade(X);
		if (A <= 0.01f)
		{
			continue;
		}
		const int32 Norm = ((Deg % 360) + 360) % 360;
		if (Norm % 45 == 0)
		{
			const bool bNorth = Norm == 0;
			Text(Cardinal[Norm / 45], FVector2D(X, Y - 2.f * S), F(EFont::Cond, bNorth ? 21.f : 18.f, TEXT("Bold"), 40), WithAlpha(bNorth ? CampusUI::Accent() : HudWhite, A * 0.95f), 0.5f);
		}
		else if (Norm % 15 == 0)
		{
			Rect(FVector2D(X - 0.75f * S, Y + 6.f * S), FVector2D(1.5f * S, 10.f * S), WithAlpha(HudWhite, 0.55f * A));
			Text(FString::FromInt(Norm), FVector2D(X, Y + 16.f * S), F(EFont::Num, 12.f, TEXT("SemiBold")), WithAlpha(HudWhite, 0.45f * A), 0.5f, false);
		}
		else
		{
			Rect(FVector2D(X - 0.5f * S, Y + 9.f * S), FVector2D(1.f * S, 6.f * S), WithAlpha(HudWhite, 0.3f * A));
		}
	}

	// Gunfire pings and friendlies along the strip
	const ECampusTeam Mine = GetLocalTeam();
	for (const TWeakObjectPtr<ACampusCharacter>& Entry : TrackedCharacters)
	{
		ACampusCharacter* Other = Entry.Get();
		if (!Other || Other == C || !Other->IsAlive())
		{
			continue;
		}
		const bool bFriend = Other->GetTeam() == Mine && Mine != ECampusTeam::None;
		const float Fired = Now - Other->GetLastUnsuppressedShotTime();
		if (!bFriend && Fired > 2.f)
		{
			continue;
		}
		const float Rel = FMath::UnwindDegrees(Bearing(Other->GetActorLocation() - CamLoc) - Heading);
		if (FMath::Abs(Rel) > Span * 0.5f)
		{
			continue;
		}
		const float X = X0 + Rel * PxPerDeg;
		const float A = EdgeFade(X);
		if (bFriend)
		{
			Tri(FVector2D(X, Y + 30.f * S), FVector2D(X - 5.f * S, Y + 22.f * S), FVector2D(X + 5.f * S, Y + 22.f * S), WithAlpha(CampusUI::Friendly(), 0.85f * A));
		}
		else
		{
			const float Fade = FMath::Clamp(1.f - Fired / 2.f, 0.f, 1.f);
			const FVector2D P(X, Y + 26.f * S);
			Tri(P + FVector2D(0.f, -6.f * S), P + FVector2D(6.f * S, 0.f), P + FVector2D(0.f, 6.f * S), WithAlpha(CampusUI::Enemy(), A * Fade));
			Tri(P + FVector2D(0.f, -6.f * S), P + FVector2D(0.f, 6.f * S), P + FVector2D(-6.f * S, 0.f), WithAlpha(CampusUI::Enemy(), A * Fade));
		}
	}

	Tri(FVector2D(X0, Y - 6.f * S), FVector2D(X0 - 6.f * S, Y - 14.f * S), FVector2D(X0 + 6.f * S, Y - 14.f * S), CampusUI::Accent());
	Text(FString::Printf(TEXT("%03d"), FMath::RoundToInt(Heading) % 360), FVector2D(X0, Y + 34.f * S), F(EFont::Num, 15.f, TEXT("Bold")), WithAlpha(HudWhite, 0.85f), 0.5f);
}

void ACampusHUD::DrawScore(const ACampusGameState* GS, float Top)
{
	const ECampusTeam Mine = GetLocalTeam() == ECampusTeam::None ? ECampusTeam::Alpha : GetLocalTeam();
	const ECampusTeam Theirs = Mine == ECampusTeam::Alpha ? ECampusTeam::Bravo : ECampusTeam::Alpha;
	const float Time = GS->GetPhaseTimeRemaining();

	if (GS->Mode == ECampusMatchMode::Gunfight)
	{
		const float Y = 92.f * S;
		const int32 Wins = GS->ScoreLimit;
		const float Pip = 14.f * S;
		const float Gap = 5.f * S;
		const float Total = Wins * (Pip + Gap);
		for (int32 i = 0; i < Wins; ++i)
		{
			const FVector2D L(Center.X - 78.f * S - Total + i * (Pip + Gap), Y + 10.f * S);
			Rect(L, FVector2D(Pip, Pip * 0.5f), i < GS->GetRoundWins(Mine) ? CampusUI::Friendly() : WithAlpha(CampusUI::Friendly(), 0.2f));
			const FVector2D R(Center.X + 78.f * S + (Wins - 1 - i) * (Pip + Gap), Y + 10.f * S);
			Rect(R, FVector2D(Pip, Pip * 0.5f), i < GS->GetRoundWins(Theirs) ? CampusUI::Enemy() : WithAlpha(CampusUI::Enemy(), 0.2f));
		}
		Text(FString::FromInt(GS->GetRoundWins(Mine)), FVector2D(Center.X - 60.f * S, Y - 4.f * S), F(EFont::Num, 30.f, TEXT("Bold")), CampusUI::Friendly(), 1.f);
		Text(FString::FromInt(GS->GetRoundWins(Theirs)), FVector2D(Center.X + 60.f * S, Y - 4.f * S), F(EFont::Num, 30.f, TEXT("Bold")), CampusUI::Enemy(), 0.f);
		const FLinearColor TimeColor = GS->bOvertime ? CampusUI::Warning() : HudWhite;
		Text(FormatTime(Time), FVector2D(Center.X, Y - 6.f * S), F(EFont::Num, 32.f, TEXT("Bold")), TimeColor, 0.5f);
		Text(FString::Printf(TEXT("第 %d 回合"), FMath::Max(1, GS->RoundNumber)), FVector2D(Center.X, Y + 30.f * S), F(EFont::Sans, 13.f, TEXT("Bold"), 80), WithAlpha(HudWhite, 0.7f), 0.5f);
		if (GS->bOvertime)
		{
			Text(TEXT("加时赛  ·  占领旗帜"), FVector2D(Center.X, Y + 52.f * S), F(EFont::Sans, 16.f, TEXT("Bold")), CampusUI::Warning(), 0.5f);
			const float W = 220.f * S;
			Rect(FVector2D(Center.X - W * 0.5f, Y + 76.f * S), FVector2D(W, 4.f * S), WithAlpha(HudWhite, 0.2f));
			const FLinearColor FC = GS->FlagTeam == Mine ? CampusUI::Friendly() : CampusUI::Enemy();
			Rect(FVector2D(Center.X - W * 0.5f, Y + 76.f * S), FVector2D(W * FMath::Clamp(GS->FlagProgress, 0.f, 1.f), 4.f * S), FC);
		}
		return;
	}

	const float X = 52.f * S;
	float Y = Top;
	const int32 Limit = FMath::Max(GS->ScoreLimit, 1);
	Tex(ETex::GradL, FVector2D(X - 12.f * S, Y - 6.f * S), FVector2D(330.f * S, 84.f * S), FLinearColor(0.f, 0.f, 0.f, 0.5f));
	auto Row = [&](const TCHAR* Label, int32 Score, const FLinearColor& Col)
	{
		Text(Label, FVector2D(X, Y + 2.f * S), F(EFont::Sans, 14.f, TEXT("Bold"), 80), WithAlpha(Col, 0.95f));
		Text(FString::FromInt(Score), FVector2D(X + 64.f * S, Y - 6.f * S), F(EFont::Num, 28.f, TEXT("Bold")), HudWhite);
		const float BX = X + 112.f * S;
		const float BW = 150.f * S;
		Rect(FVector2D(BX, Y + 10.f * S), FVector2D(BW, 5.f * S), WithAlpha(Col, 0.18f));
		Rect(FVector2D(BX, Y + 10.f * S), FVector2D(BW * FMath::Clamp((float)Score / Limit, 0.f, 1.f), 5.f * S), Col);
		Y += 32.f * S;
	};
	Row(TEXT("我方"), GS->GetTeamScore(Mine), CampusUI::Friendly());
	Row(TEXT("敌方"), GS->GetTeamScore(Theirs), CampusUI::Enemy());
	Text(FormatTime(Time), FVector2D(X + 272.f * S, Top + 4.f * S), F(EFont::Num, 30.f, TEXT("Bold")), HudWhite, 0.f);
	Text(FString::Printf(TEXT("目标 %d"), Limit), FVector2D(X + 274.f * S, Top + 38.f * S), F(EFont::Sans, 12.f, TEXT("Bold")), WithAlpha(HudWhite, 0.55f), 0.f);
}

void ACampusHUD::DrawKillFeed(const ACampusGameState* GS)
{
	const ECampusTeam Mine = GetLocalTeam();
	float Y = Canvas->ClipY - 300.f * S;
	const float X = 52.f * S;
	int32 Shown = 0;
	const FHudFont NameFont = F(EFont::Sans, 15.f, TEXT("Bold"));
	const FHudFont WeaponFont = F(EFont::Cond, 14.f, TEXT("SemiBold"), 60);
	for (int32 i = GS->KillFeed.Num() - 1; i >= 0 && Shown < 5; --i)
	{
		const FCampusKillFeedEntry& E = GS->KillFeed[i];
		const float Age = Now - E.Time;
		if (Age > 7.f)
		{
			continue;
		}
		const float Alpha = FMath::Clamp((7.f - Age) / 0.6f, 0.f, 1.f);
		const float Slide = (1.f - Smooth01(Age / 0.18f)) * 20.f * S;
		const FLinearColor KC = E.KillerTeam == Mine ? CampusUI::Friendly() : CampusUI::Enemy();
		const FLinearColor VC = E.VictimTeam == Mine ? CampusUI::Friendly() : CampusUI::Enemy();
		const FString Weapon = WeaponName(E.WeaponId).ToUpper();
		float W = Measure(E.VictimName, NameFont).X + Measure(Weapon, WeaponFont).X + 36.f * S;
		if (!E.KillerName.IsEmpty())
		{
			W += Measure(E.KillerName, NameFont).X + 12.f * S;
		}
		if (E.bHeadshot)
		{
			W += 34.f * S;
		}
		Tex(ETex::GradL, FVector2D(X - 10.f * S - Slide, Y - 3.f * S), FVector2D(W + 80.f * S, 26.f * S), FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha));
		float Cursor = X - Slide;
		if (!E.KillerName.IsEmpty())
		{
			Text(E.KillerName, FVector2D(Cursor, Y), NameFont, WithAlpha(KC, Alpha));
			Cursor += Measure(E.KillerName, NameFont).X + 12.f * S;
		}
		Text(Weapon, FVector2D(Cursor, Y + 1.f * S), WeaponFont, WithAlpha(HudWhite, 0.8f * Alpha), 0.f, false);
		Cursor += Measure(Weapon, WeaponFont).X + 10.f * S;
		if (E.bHeadshot)
		{
			Text(TEXT("爆头"), FVector2D(Cursor, Y + 1.f * S), F(EFont::Sans, 12.f, TEXT("Black")), WithAlpha(CampusUI::Warning(), Alpha), 0.f, false);
			Cursor += 34.f * S;
		}
		Cursor += 4.f * S;
		Text(E.VictimName, FVector2D(Cursor, Y), NameFont, WithAlpha(VC, Alpha));
		Y -= 30.f * S;
		++Shown;
	}
}

void ACampusHUD::DrawKillNotice()
{
	const float KillAge = Now - LastKillTime;
	if (!KillVictimName.IsEmpty() && KillAge >= 0.f && KillAge < 1.8f)
	{
		const float Alpha = Smooth01(KillAge / 0.08f) * (1.f - Smooth01((KillAge - 1.3f) / 0.5f));
		const float Slide = (1.f - Smooth01(KillAge / 0.12f)) * 6.f * S;
		const FVector2D Anchor(Center.X, Canvas->ClipY * 0.78f + Slide);
		const FLinearColor ConfirmColor = bLastKillHeadshot ? CampusUI::Warning() : CampusUI::Enemy();
		const FString Label = bLastKillHeadshot ? LOCTEXT("HeadshotPlayerPrefix", "爆头击杀了 ").ToString() : LOCTEXT("KilledPlayerPrefix", "击杀了 ").ToString();
		const FHudFont ConfirmFont = F(EFont::Sans, 19.f, TEXT("Bold"), 15);
		const float IconSize = 18.f * S;
		const float LabelWidth = Measure(Label, ConfirmFont).X;
		const float MaxNameWidth = FMath::Min(460.f * S, Canvas->ClipX * 0.55f) - LabelWidth - IconSize - 8.f * S;
		FString Name = KillVictimName;
		if (Measure(Name, ConfirmFont).X > MaxNameWidth)
		{
			int32 Lower = 0;
			int32 Upper = Name.Len();
			while (Lower < Upper)
			{
				const int32 Mid = (Lower + Upper + 1) / 2;
				if (Measure(Name.Left(Mid) + TEXT("…"), ConfirmFont).X <= MaxNameWidth) Lower = Mid;
				else Upper = Mid - 1;
			}
			int32 End = Lower;
			if (End > 0 && Name[End - 1] >= 0xd800 && Name[End - 1] <= 0xdbff) --End;
			Name = Name.Left(End) + TEXT("…");
		}
		const float Width = LabelWidth + Measure(Name, ConfirmFont).X + IconSize + 8.f * S;
		const FVector2D Start(Anchor.X - Width * 0.5f, Anchor.Y);
		const FVector2D Ink = TextInkVerticalBounds(Label, ConfirmFont);
		const float IconY = CampusCombatPresentation::CenteredIconTop(Start.Y, Ink.X, Ink.Y, IconSize, 0.08f, 0.92f);
		Tex(bLastKillHeadshot ? ETex::MedalLongshot : ETex::Skull, FVector2D(FMath::RoundToFloat(Start.X), FMath::RoundToFloat(IconY)), FVector2D(IconSize, IconSize), WithAlpha(ConfirmColor, Alpha));
		Text(Label, Start + FVector2D(IconSize + 8.f * S, 0.f), ConfirmFont, WithAlpha(HudWhite, Alpha));
		Text(Name, Start + FVector2D(IconSize + 8.f * S + LabelWidth, 0.f), ConfirmFont, WithAlpha(ConfirmColor, Alpha));
	}
}

void ACampusHUD::DrawPopups()
{
	// Score feedback beside the crosshair: accumulated XP in gold, the newest events stacked to its right.
	const FLinearColor Gold(1.f, 0.80f, 0.26f, 1.f);
	const float Life = 2.8f;
	const FVector2D Anchor(Center.X + 56.f * S, Center.Y - 98.f * S);
	float EventsX = Anchor.X;
	const float XPAge = Now - PointsFlashTime;
	if (RecentPoints > 0 && XPAge < Life)
	{
		const float Alpha = FMath::Clamp(XPAge / 0.06f, 0.f, 1.f) * FMath::Clamp((Life - XPAge) / 0.45f, 0.f, 1.f);
		const float Pop = 1.f + 0.3f * FMath::Exp(-(Now - XPPopTime) * 16.f);
		const FString Num = TEXT("+") + FString::FromInt(RecentPoints);
		const FHudFont NumFont = F(EFont::Num, 34.f, TEXT("Bold"));
		const FHudFont XPFont = F(EFont::Num, 19.f, TEXT("Bold"), 40);
		const FVector2D NumSize = Measure(Num, NumFont);
		const FVector2D PopSize = Measure(Num, F(EFont::Num, 34.f * Pop, TEXT("Bold")));
		// The number grows in place around its centre; the layout uses its resting size.
		Text(Num, FVector2D(Anchor.X + (NumSize.X - PopSize.X) * 0.5f, Anchor.Y - PopSize.Y * 0.5f), F(EFont::Num, 34.f * Pop, TEXT("Bold")), WithAlpha(Gold, Alpha), 0.f);
		const FVector2D XPSize = Measure(TEXT("XP"), XPFont);
		Text(TEXT("XP"), FVector2D(Anchor.X + NumSize.X + 5.f * S, Anchor.Y - XPSize.Y * 0.5f + 3.f * S), XPFont, WithAlpha(Gold, Alpha), 0.f);
		EventsX = Anchor.X + NumSize.X + XPSize.X + 24.f * S;
	}

	const FHudFont EvFont = F(EFont::Sans, 15.f, TEXT("Bold"), 30);
	const float Icon = 16.f * S;
	float Y = Anchor.Y - 13.f * S;
	int32 Row = 0;
	for (int32 i = Popups.Num() - 1; i >= 0; --i)
	{
		const FScorePopup& P = Popups[i];
		const float Age = Now - P.Time;
		if (Age > Life)
		{
			Popups.RemoveAt(i);
			continue;
		}
		const float Alpha = FMath::Clamp(Age / 0.08f, 0.f, 1.f) * FMath::Clamp((Life - Age) / 0.45f, 0.f, 1.f) * (Row == 0 ? 1.f : 0.75f);
		float X = EventsX + (1.f - Smooth01(Age / 0.14f)) * 14.f * S;
		if (P.Event == ECampusScoreEvent::Kill || P.Event == ECampusScoreEvent::Melee)
		{
			const FVector2D Ink = TextInkVerticalBounds(P.Text.ToString(), EvFont);
			const float IconY = CampusCombatPresentation::CenteredIconTop(Y, Ink.X, Ink.Y, Icon, 0.08f, 0.92f);
			Tex(ETex::Skull, FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(IconY)), FVector2D(Icon, Icon), WithAlpha(HudWhite, Alpha));
			X += Icon + 6.f * S;
		}
		Text(P.Text.ToString(), FVector2D(X, Y), EvFont, WithAlpha(P.bMedal ? Gold : HudWhite, Alpha), 0.f);
		Y += 21.f * S;
		++Row;
	}
}

void ACampusHUD::DrawMedalIcon(ECampusScoreEvent Event, const FVector2D& Position, float Radius, const FLinearColor& Color)
{
	ETex Icon;
	switch (Event)
	{
	case ECampusScoreEvent::Headshot: Icon = ETex::MedalHeadshot; break;
	case ECampusScoreEvent::DoubleKill: Icon = ETex::MedalDoubleKill; break;
	case ECampusScoreEvent::TripleKill: Icon = ETex::MedalTripleKill; break;
	case ECampusScoreEvent::FirstBlood: Icon = ETex::MedalFirstBlood; break;
	case ECampusScoreEvent::Longshot: Icon = ETex::MedalLongshot; break;
	case ECampusScoreEvent::Revenge: Icon = ETex::MedalRevenge; break;
	case ECampusScoreEvent::Streak5: Icon = ETex::MedalStreak5; break;
	case ECampusScoreEvent::Melee: Icon = ETex::MedalMelee; break;
	default: return;
	}
	Tex(Icon, Position - FVector2D(Radius), FVector2D(Radius * 2.f), Color);
}

void ACampusHUD::DrawMedal()
{
	if (MedalQueue.Tick(Now))
	{
		CampusAudio::Play2D(this, TEXT("Medal"), 0.65f, 0.f, CampusAudio::ECategory::Interface);
	}
	const CampusCombatPresentation::FMedal* Medal = MedalQueue.GetActive();
	if (!Medal) return;
	const float Age = Now - MedalQueue.GetStartTime();
	const CampusCombatPresentation::FMedalMotion Motion = CampusCombatPresentation::MedalMotion(Age);
	const FVector2D Origin(FMath::RoundToFloat(Center.X), FMath::RoundToFloat(Canvas->ClipY * 0.19f + Motion.OffsetY * S));
	const FVector2D Badge = Origin + FVector2D(-124.f, 0.f) * S;
	const FLinearColor Gold(1.f, 0.8f, 0.36f, Motion.Alpha);
	const FLinearColor White = WithAlpha(HudWhite, Motion.TitleAlpha);
	const FLinearColor Dark(0.012f, 0.016f, 0.02f, 0.72f * Motion.Alpha);
	const FVector2D Left = Origin + FVector2D(-88.f, -27.f) * S;
	const float PanelWidth = 226.f * S * Motion.Reveal;
	Rect(Left, FVector2D(PanelWidth, 54.f * S), Dark);
	Tri(Left + FVector2D(PanelWidth, 0.f), Left + FVector2D(PanelWidth + 16.f * S, 27.f * S), Left + FVector2D(PanelWidth, 54.f * S), Dark);
	Rect(Left, FVector2D(PanelWidth, FMath::Max(1.f, S)), WithAlpha(Gold, 0.65f));
	Rect(Left + FVector2D(0.f, 53.f) * S, FVector2D(PanelWidth, FMath::Max(1.f, S)), WithAlpha(White, 0.20f));
	Disc(Badge, 36.f * S * Motion.BadgeScale, Dark);
	const float RingSize = 74.f * S * Motion.BadgeScale;
	Tex(ETex::MedalRing, Badge - FVector2D(RingSize * 0.5f), FVector2D(RingSize), Gold);
	DrawMedalIcon(Medal->Event, Badge, 25.f * S * Motion.BadgeScale, WithAlpha(HudWhite, Motion.Alpha));
	const FVector2D TitleOffset((1.f - Motion.Reveal) * 5.f * S, 0.f);
	Text(TEXT("COMBAT AWARD"), Origin + FVector2D(-70.f, -21.f) * S + TitleOffset, F(EFont::Cond, 10.f, TEXT("SemiBold"), 180), WithAlpha(FLinearColor(1.f, 0.8f, 0.36f), Motion.DetailAlpha), 0.f, false);
	const FString Title = CampusText::ScoreEventName(Medal->Event).ToString();
	Text(Title, Origin + FVector2D(-70.f, -9.f) * S + TitleOffset, F(EFont::Sans, 25.f, TEXT("Black"), 60), White);
	if (Medal->Count > 1)
	{
		Text(FString::Printf(TEXT("×%d"), Medal->Count), Origin + FVector2D(123.f, 4.f) * S, F(EFont::Num, 18.f, TEXT("Bold")), WithAlpha(FLinearColor(1.f, 0.8f, 0.36f), Motion.TitleAlpha), 1.f);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float Delay = 0.12f + Index * 0.035f;
		const float ChevronAlpha = Motion.DetailAlpha * Smooth01((Age - Delay) / 0.09f);
		const FVector2D Chevron = Origin + FVector2D(149.f + Index * 11.f, -13.f) * S;
		Tex(ETex::MedalChevron, Chevron, FVector2D(10.f, 26.f) * S, WithAlpha(HudWhite, ChevronAlpha * 0.8f));
	}
}

float ACampusHUD::DrawRadar(ACampusCharacter* C, const ACampusGameState* GS)
{
	const float Radius = 118.f * S;
	const FVector2D RC(52.f * S + Radius, 52.f * S + Radius);
	const float Range = 4200.f;
	const float PixelsPerCm = Radius / Range;

	Disc(RC, Radius, FLinearColor(0.015f, 0.02f, 0.022f, 0.62f));
	Ring(RC, Radius, WithAlpha(HudWhite, 0.4f));
	Ring(RC, Radius * 0.5f, WithAlpha(HudWhite, 0.07f));

	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
	const float Yaw = FMath::DegreesToRadians(CamRot.Yaw);
	auto ToRadar = [&](const FVector& World) -> FVector2D
	{
		const FVector2D D(World.X - CamLoc.X, World.Y - CamLoc.Y);
		const float Fwd = D.X * FMath::Cos(Yaw) + D.Y * FMath::Sin(Yaw);
		const float Rgt = -D.X * FMath::Sin(Yaw) + D.Y * FMath::Cos(Yaw);
		return RC + FVector2D(Rgt, -Fwd) * PixelsPerCm;
	};
	auto ClipSegment = [&](FVector2D A, FVector2D B, FVector2D& OutA, FVector2D& OutB) -> bool
	{
		const FVector2D D = B - A;
		const FVector2D Fv = A - RC;
		const float a = D.Dot(D);
		const float b = 2.f * Fv.Dot(D);
		const float c = Fv.Dot(Fv) - Radius * Radius;
		const bool bAIn = c <= 0.f;
		const bool bBIn = (B - RC).SizeSquared() <= Radius * Radius;
		if (bAIn && bBIn)
		{
			OutA = A;
			OutB = B;
			return true;
		}
		const float Disc2 = b * b - 4.f * a * c;
		if (Disc2 < 0.f || a < KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const float Sq = FMath::Sqrt(Disc2);
		const float T0 = FMath::Clamp((-b - Sq) / (2.f * a), 0.f, 1.f);
		const float T1 = FMath::Clamp((-b + Sq) / (2.f * a), 0.f, 1.f);
		if (T1 <= T0)
		{
			return false;
		}
		OutA = A + D * T0;
		OutB = A + D * T1;
		return true;
	};

	// View cone
	const float Cone = FMath::DegreesToRadians(42.f);
	Tri(RC, RC + FVector2D(FMath::Sin(-Cone), -FMath::Cos(-Cone)) * Radius * 0.85f, RC + FVector2D(0.f, -Radius * 0.88f), FLinearColor(1.f, 1.f, 1.f, 0.05f));
	Tri(RC, RC + FVector2D(0.f, -Radius * 0.88f), RC + FVector2D(FMath::Sin(Cone), -FMath::Cos(Cone)) * Radius * 0.85f, FLinearColor(1.f, 1.f, 1.f, 0.05f));

	if (const ACampusSchoolBuilder* Builder = ACampusSchoolBuilder::Find(GetWorld()))
	{
		for (const FCampusMapRect& R : Builder->MinimapRects)
		{
			const FVector2D Mid = (R.Min + R.Max) * 0.5f;
			if (FVector2D::Distance(Mid, FVector2D(CamLoc)) > Range * 2.f)
			{
				continue;
			}
			const FVector Corners[4] = { FVector(R.Min.X, R.Min.Y, 0.f), FVector(R.Max.X, R.Min.Y, 0.f), FVector(R.Max.X, R.Max.Y, 0.f), FVector(R.Min.X, R.Max.Y, 0.f) };
			const FLinearColor Col = R.Kind == 0 ? WithAlpha(HudWhite, 0.6f) : (R.Kind == 1 ? WithAlpha(CampusUI::Accent(), 0.3f) : WithAlpha(HudWhite, 0.22f));
			for (int32 e = 0; e < 4; ++e)
			{
				FVector2D A, B;
				if (ClipSegment(ToRadar(Corners[e]), ToRadar(Corners[(e + 1) % 4]), A, B))
				{
					SoftLine(A, B, (R.Kind == 0 ? 1.6f : 1.1f) * S, Col);
				}
			}
		}
	}

	const ECampusTeam Mine = GetLocalTeam();
	for (const TWeakObjectPtr<ACampusCharacter>& Entry : TrackedCharacters)
	{
		ACampusCharacter* Other = Entry.Get();
		if (!Other || Other == C || !Other->IsAlive())
		{
			continue;
		}
		const bool bFriend = Other->GetTeam() == Mine && Mine != ECampusTeam::None;
		const float Fired = Now - Other->GetLastUnsuppressedShotTime();
		if (!bFriend && Fired >= 1.8f)
		{
			continue;
		}
		FVector2D P = ToRadar(Other->GetActorLocation());
		const FVector2D Off = P - RC;
		if (Off.Size() > Radius - 7.f * S)
		{
			P = RC + Off.GetSafeNormal() * (Radius - 7.f * S);
		}
		if (bFriend)
		{
			const float RelYaw = FMath::DegreesToRadians(Other->GetActorRotation().Yaw) - Yaw;
			const FVector2D Dir(FMath::Sin(RelYaw), -FMath::Cos(RelYaw));
			const FVector2D Perp(-Dir.Y, Dir.X);
			Tri(P + Dir * 8.f * S, P - Dir * 5.f * S + Perp * 5.5f * S, P - Dir * 5.f * S - Perp * 5.5f * S, CampusUI::Friendly());
		}
		else
		{
			const float Fade = FMath::Clamp(1.f - Fired / 1.8f, 0.f, 1.f);
			Disc(P, (6.f + 4.f * FMath::Exp(-Fired * 6.f)) * S, WithAlpha(CampusUI::Enemy(), 0.35f * Fade));
			Disc(P, 4.5f * S, WithAlpha(CampusUI::Enemy(), Fade));
		}
	}

	if (GS && GS->bOvertime)
	{
		FVector2D P = ToRadar(GS->FlagLocation);
		const FVector2D Off = P - RC;
		if (Off.Size() > Radius - 8.f * S)
		{
			P = RC + Off.GetSafeNormal() * (Radius - 8.f * S);
		}
		Rect(P - FVector2D(5.f, 5.f) * S, FVector2D(10.f, 10.f) * S, CampusUI::Warning());
	}

	Tri(RC + FVector2D(0.f, -10.f) * S, RC + FVector2D(7.f, 8.f) * S, RC + FVector2D(-7.f, 8.f) * S, HudWhite);

	// Cardinal letters around the rim (north is world +Y)
	const TCHAR* Letters[4] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
	const FVector WorldDirs[4] = { FVector(0.f, 1.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, -1.f, 0.f), FVector(1.f, 0.f, 0.f) };
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector2D Dir = (ToRadar(CamLoc + WorldDirs[i] * 1000.f) - RC).GetSafeNormal();
		const FVector2D P = RC + Dir * (Radius + 13.f * S);
		const FHudFont LF = F(EFont::Cond, i == 0 ? 17.f : 14.f, TEXT("Bold"));
		Text(Letters[i], P - FVector2D(0.f, Measure(Letters[i], LF).Y * 0.5f), LF, i == 0 ? CampusUI::Accent() : WithAlpha(HudWhite, 0.75f), 0.5f);
	}
	return RC.Y + Radius + 30.f * S;
}

void ACampusHUD::DrawPerkAwareness(ACampusCharacter* C)
{
	const bool bReveal = Now < C->RevealUntil;
	const bool bAlert = C->HasPerk(ECampusPerk::HighAlert);
	if (!bReveal && !bAlert)
	{
		return;
	}
	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector Forward = CamRot.Vector();
	const float HalfFov = FMath::DegreesToRadians(PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetFOVAngle() * 0.5f : 45.f);
	const FLinearColor Recon = CampusPerks::GroupColor(ECampusPerkGroup::Recon);
	for (const TWeakObjectPtr<ACampusCharacter>& Entry : TrackedCharacters)
	{
		ACampusCharacter* Other = Entry.Get();
		if (!Other || Other == C || !Other->IsAlive() || !C->IsEnemyOf(Other))
		{
			continue;
		}
		const FVector Chest = Other->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector To = Chest - CamLoc;
		const float Dist = To.Size();

		// Recon specialty: nearby enemies outlined through walls for a moment.
		if (bReveal && Dist < 2500.f)
		{
			const FVector P = Project(Chest);
			if (P.Z > 0.f)
			{
				const float A = FMath::Clamp((C->RevealUntil - Now) / 0.5f, 0.f, 1.f);
				const FVector2D Pt(P.X, P.Y);
				const float R = FMath::Lerp(16.f, 9.f, FMath::Clamp(Dist / 2500.f, 0.f, 1.f)) * S;
				Tri(Pt + FVector2D(0.f, -R), Pt + FVector2D(R, 0.f), Pt + FVector2D(0.f, R), WithAlpha(CampusUI::Enemy(), 0.75f * A));
				Tri(Pt + FVector2D(0.f, -R), Pt + FVector2D(-R, 0.f), Pt + FVector2D(0.f, R), WithAlpha(CampusUI::Enemy(), 0.75f * A));
				Text(FString::Printf(TEXT("%dm"), FMath::RoundToInt(Dist / 100.f)), FVector2D(Pt.X, Pt.Y + R + 2.f * S), F(EFont::Num, 13.f, TEXT("Bold")), WithAlpha(Recon, A), 0.5f);
			}
		}

		// High Alert: an enemy out of view who has us in their sights pulses the screen edge toward them.
		if (bAlert && Dist < 6000.f && FMath::Acos(FVector::DotProduct(To.GetSafeNormal(), Forward)) > HalfFov)
		{
			const FVector TheirEye = Other->GetPawnViewLocation();
			const FVector ToMe = (CamLoc - TheirEye).GetSafeNormal();
			if (FVector::DotProduct(Other->GetBaseAimRotation().Vector(), ToMe) < FMath::Cos(FMath::DegreesToRadians(14.f)))
			{
				continue;
			}
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusHighAlert), false, C);
			Params.AddIgnoredActor(Other);
			if (GetWorld()->LineTraceSingleByChannel(Hit, TheirEye, CamLoc, ECC_Visibility, Params))
			{
				continue;
			}
			const FVector Local = FRotator(0.f, CamRot.Yaw, 0.f).UnrotateVector(To);
			const float Angle = FMath::Atan2(Local.Y, Local.X);
			const FVector2D Dir(FMath::Sin(Angle), -FMath::Cos(Angle));
			const float Pulse = 0.55f + 0.45f * FMath::Sin(Now * 12.f);
			const FVector2D Edge = Center + Dir * FVector2D(Canvas->ClipX * 0.47f, Canvas->ClipY * 0.44f);
			const FVector2D Side(-Dir.Y, Dir.X);
			Tri(Edge + Dir * 18.f * S, Edge - Dir * 10.f * S + Side * 26.f * S, Edge - Dir * 10.f * S - Side * 26.f * S, WithAlpha(CampusUI::Warning(), 0.85f * Pulse));
			SoftLine(Edge - Side * 120.f * S, Edge + Side * 120.f * S, 5.f * S, WithAlpha(CampusUI::Warning(), 0.35f * Pulse));
		}
	}
}

void ACampusHUD::DrawNameplates(ACampusCharacter* C)
{
	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector Forward = CamRot.Vector();
	const ECampusTeam Mine = GetLocalTeam();
	for (const TWeakObjectPtr<ACampusCharacter>& Entry : TrackedCharacters)
	{
		ACampusCharacter* Other = Entry.Get();
		if (!Other || Other == C || !Other->IsAlive())
		{
			continue;
		}
		const bool bFriend = Other->GetTeam() == Mine && Mine != ECampusTeam::None;
		const FVector Head = Other->GetActorLocation() + FVector(0.f, 0.f, 115.f);
		const FVector ToHead = Head - CamLoc;
		const float Dist = ToHead.Size();
		const float Cos = FVector::DotProduct(ToHead.GetSafeNormal(), Forward);
		if (Cos < 0.2f)
		{
			continue;
		}
		bool bShow = bFriend && Dist < 6000.f;
		if (!bFriend && Cos > FMath::Cos(FMath::DegreesToRadians(2.5f)) && Dist < 5000.f)
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusNameplate), false, C);
			bShow = !GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, Head - FVector(0.f, 0.f, 40.f), ECC_Visibility, Params) || Hit.GetActor() == Other;
		}
		if (!bShow)
		{
			continue;
		}
		const FVector Projected = Project(Head);
		if (Projected.Z <= 0.f)
		{
			continue;
		}
		const float Alpha = bFriend ? FMath::Clamp(1.f - Dist / 6000.f, 0.4f, 1.f) : 1.f;
		const FLinearColor Col = WithAlpha(bFriend ? CampusUI::Friendly() : CampusUI::Enemy(), Alpha);
		Text(Other->GetDisplayName(), FVector2D(Projected.X, Projected.Y - 26.f * S), F(EFont::Sans, 14.f, TEXT("Bold")), Col, 0.5f);
		if (bFriend)
		{
			const FVector2D P(Projected.X, Projected.Y);
			Tri(P + FVector2D(-6.f, -5.f) * S, P + FVector2D(6.f, -5.f) * S, P + FVector2D(0.f, 3.f) * S, Col);
		}
	}
}

void ACampusHUD::DrawScope(ACampusCharacter* C, float Alpha)
{
	FVector CamLoc;
	FRotator CamRot;
	PlayerOwner->GetPlayerViewPoint(CamLoc, CamRot);

	// The eyepiece is the 3D optic on the weapon (its surroundings are defocused by a post process); the HUD only
	// adds what the eye sees inside it.
	FVector2D LensUV;
	float LensH = 0.f;
	if (!C->Viewmodel || !C->Viewmodel->GetScopeEyepieceScreen(LensUV, LensH))
	{
		return;
	}
	const FVector2D Eye(LensUV.X * Canvas->ClipX, LensUV.Y * Canvas->ClipY);
	const float R = LensH * Canvas->ClipY;
	if (R < 8.f)
	{
		return;
	}
	// The reticle is focused at infinity: it sits where the optic's axis points, so it stays on the point of aim
	// while the eyepiece drifts with sway and recoil.
	FVector2D Aim = Eye;
	const ACampusWeapon* W = C->GetCurrentWeapon();
	if (const USceneComponent* Sight = W ? W->GetFPSight() : nullptr)
	{
		const FVector Axis = CamRot.UnrotateVector(Sight->GetForwardVector());
		const float Fov = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetFOVAngle() : 60.f;
		const float Focal = Canvas->ClipX * 0.5f / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f));
		if (Axis.X > 0.2f)
		{
			Aim = Center + FVector2D(Axis.Y / Axis.X, -Axis.Z / Axis.X) * Focal;
		}
	}
	const float Lens = R * 0.97f;
	auto LensLine = [&](const FVector2D& A, const FVector2D& B, float Width, const FLinearColor& Col)
	{
		// Clip to the lens circle.
		const FVector2D D = B - A;
		const FVector2D Fo = A - Eye;
		const float Qa = D | D;
		const float Qb = 2.f * (Fo | D);
		const float Qc = (Fo | Fo) - Lens * Lens;
		const float Disc = Qb * Qb - 4.f * Qa * Qc;
		if (Qa < 1.e-4f || Disc <= 0.f)
		{
			return;
		}
		const float Sq = FMath::Sqrt(Disc);
		const float T0 = FMath::Max((-Qb - Sq) / (2.f * Qa), 0.f);
		const float T1 = FMath::Min((-Qb + Sq) / (2.f * Qa), 1.f);
		if (T1 > T0)
		{
			SoftLine(A + D * T0, A + D * T1, Width, Col);
		}
	};

	// Exit-pupil shadow: darkens the rim of the lens and slides toward the side the eye is off-axis.
	const FVector2D Pupil = Eye + (Eye - Aim).ClampAxes(-R * 0.12f, R * 0.12f);
	// Kept light: a soft vignette at the very rim, the sight picture itself stays bright.
	Tex(ETex::Eyebox, Eye - FVector2D(R, R), FVector2D(R, R) * 2.f, FLinearColor(0.f, 0.f, 0.f, 0.22f * Alpha));
	Tex(ETex::Eyebox, Pupil - FVector2D(R, R) * 0.97f, FVector2D(R, R) * 1.94f, FLinearColor(0.f, 0.f, 0.f, 0.26f * Alpha));
	Tex(ETex::Soft, Eye + FVector2D(-R * 0.62f, -R * 0.72f), FVector2D(R, R) * 0.8f, FLinearColor(0.8f, 0.9f, 1.f, 0.03f * Alpha));

	// ACOG-style reticle in lens units: fine crosshair, heavy outer posts, bullet-drop stadia, illuminated chevron.
	const float U = R / 70.f;
	const FLinearColor Ret(0.01f, 0.01f, 0.01f, 0.92f * Alpha);
	const float Fine = FMath::Max(1.2f * S, 0.32f * U);
	LensLine(Aim - FVector2D(R * 2.f, 0.f), Aim - FVector2D(5.f * U, 0.f), Fine, Ret);
	LensLine(Aim + FVector2D(5.f * U, 0.f), Aim + FVector2D(R * 2.f, 0.f), Fine, Ret);
	LensLine(Aim + FVector2D(0.f, 6.f * U), Aim + FVector2D(0.f, R * 2.f), Fine, Ret);
	LensLine(Aim - FVector2D(R * 2.f, 0.f), Aim - FVector2D(45.f * U, 0.f), 1.2f * U, Ret);
	LensLine(Aim + FVector2D(45.f * U, 0.f), Aim + FVector2D(R * 2.f, 0.f), 1.2f * U, Ret);
	LensLine(Aim + FVector2D(0.f, 50.f * U), Aim + FVector2D(0.f, R * 2.f), 1.2f * U, Ret);
	for (int32 i = 1; i <= 4; ++i)
	{
		const float Y = 6.f * U + i * 4.4f * U;
		const float Wd = (2.8f - i * 0.32f) * U;
		LensLine(Aim + FVector2D(-Wd, Y), Aim + FVector2D(Wd, Y), FMath::Max(1.f * S, 0.28f * U), Ret);
	}
	const FLinearColor Glow(1.f, 0.12f, 0.05f, 0.35f * Alpha);
	const FLinearColor Chevron(1.f, 0.18f, 0.06f, Alpha);
	const float Cw = 2.2f * U;
	const float Ch = 3.f * U;
	Tex(ETex::Soft, Aim - FVector2D(2.8f, 2.f) * U, FVector2D(5.6f, 5.6f) * U, Glow);
	Tri(Aim, Aim + FVector2D(-Cw, Ch), Aim + FVector2D(-Cw * 0.64f, Ch), Chevron);
	Tri(Aim, Aim + FVector2D(-Cw * 0.64f, Ch), Aim + FVector2D(0.f, Ch * 0.33f), Chevron);
	Tri(Aim, Aim + FVector2D(0.f, Ch * 0.33f), Aim + FVector2D(Cw * 0.64f, Ch), Chevron);
	Tri(Aim, Aim + FVector2D(Cw * 0.64f, Ch), Aim + FVector2D(Cw, Ch), Chevron);

	// Brief dim as the eye meets the eyepiece (the scope body drops out of view at the same moment).
	const float Blink = FMath::Clamp(1.f - FMath::Abs(C->GetADSAlpha() - 0.93f) / 0.06f, 0.f, 1.f);
	if (Blink > 0.01f)
	{
		Rect(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(0.f, 0.f, 0.f, 0.45f * Blink));
	}
}

void ACampusHUD::DrawDoorPrompt(ACampusCharacter* C)
{
	const ACampusDoor* Door = C->FindUsableDoor();
	if (!Door)
	{
		return;
	}
	const FString KeyName = UCampusInputConfig::Get()->GetKeyLabel(TEXT("Interact")).ToString();
	const FHudFont KeyFont = F(EFont::Cond, 19.f, TEXT("Bold"));
	const float Key = 26.f * S;
	const float KeyW = FMath::Max(Key, Measure(KeyName, KeyFont).X + 12.f * S);
	const float Gap = 9.f * S;
	const float LabelWidth = 34.f * S;
	const FVector2D KeyPos(Center.X - (KeyW + Gap + LabelWidth) * 0.5f, Center.Y + 64.f * S);
	Rect(KeyPos - FVector2D(2.f, 2.f) * S, FVector2D(KeyW, Key) + FVector2D(4.f, 4.f) * S, FLinearColor(0.f, 0.f, 0.f, 0.5f));
	Rect(KeyPos, FVector2D(KeyW, Key), FLinearColor(0.93f, 0.93f, 0.9f, 0.95f));
	Text(KeyName, FVector2D(KeyPos.X + KeyW * 0.5f, KeyPos.Y + 2.f * S), KeyFont, FLinearColor(0.04f, 0.04f, 0.04f, 1.f), 0.5f, false);
	Text(Door->IsOpen() ? TEXT("关门") : TEXT("开门"), FVector2D(KeyPos.X + KeyW + Gap, KeyPos.Y + 2.f * S), F(EFont::Sans, 17.f, TEXT("Bold")), HudWhite, 0.f, true);
}

void ACampusHUD::DrawFlagMarker(const ACampusGameState* GS)
{
	if (!GS || !GS->bOvertime)
	{
		return;
	}
	const FVector P = Project(GS->FlagLocation + FVector(0.f, 0.f, 150.f));
	if (P.Z <= 0.f)
	{
		return;
	}
	const FVector2D Pos(FMath::Clamp(P.X, 40.f * S, Canvas->ClipX - 40.f * S), FMath::Clamp(P.Y, 80.f * S, Canvas->ClipY - 80.f * S));
	Rect(Pos - FVector2D(13.f, 13.f) * S, FVector2D(26.f, 26.f) * S, WithAlpha(CampusUI::Warning(), 0.9f));
	Text(TEXT("旗"), Pos - FVector2D(0.f, 11.f * S), F(EFont::Sans, 15.f, TEXT("Black")), FLinearColor::Black, 0.5f, false);
}

// ------------------------------------------------------------------ screens

void ACampusHUD::DrawDeathScreen(ACampusCharacter* C)
{
	const APlayerState* Killer = C->KillerState;
	const FString Who = Killer ? Killer->GetPlayerName() : FString(TEXT("意外"));
	const float Y = Canvas->ClipY * 0.70f;
	Tex(ETex::Band, FVector2D(Center.X - 520.f * S, Y), FVector2D(1040.f * S, 110.f * S), FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Text(TEXT("KILLED BY"), FVector2D(Center.X, Y + 12.f * S), F(EFont::Cond, 15.f, TEXT("SemiBold"), 320), WithAlpha(HudWhite, 0.65f), 0.5f);
	Text(Who, FVector2D(Center.X, Y + 30.f * S), F(EFont::Sans, 34.f, TEXT("Black")), CampusUI::Enemy(), 0.5f);
	const ACampusGameState* GS = GetWorld()->GetGameState<ACampusGameState>();
	const FString Sub = (GS && GS->Mode == ECampusMatchMode::Gunfight) ? FString(TEXT("等待回合结束")) : FString(TEXT("即将重新部署…"));
	Text(Sub, FVector2D(Center.X, Y + 78.f * S), F(EFont::Sans, 15.f, TEXT("Medium")), WithAlpha(HudWhite, 0.8f), 0.5f, false);
}

void ACampusHUD::DrawPhaseInfo(const ACampusGameState* GS)
{
	if (GS->Phase == ECampusMatchPhase::Countdown)
	{
		const float Remaining = GS->GetPhaseTimeRemaining();
		const int32 Secs = FMath::Max(FMath::CeilToInt(Remaining), 1);
		const float Frac = Remaining - FMath::FloorToFloat(Remaining);
		const FVector2D P(Center.X, Center.Y - 150.f * S);
		Text(TEXT("比赛即将开始"), FVector2D(Center.X, P.Y - 112.f * S), F(EFont::Sans, 20.f, TEXT("Bold"), 200), WithAlpha(HudWhite, 0.85f), 0.5f);
		Ring(P, (64.f + 10.f * Frac) * S, WithAlpha(CampusUI::Accent(), 0.25f + 0.5f * Frac));
		const FHudFont NF = F(EFont::Num, 96.f * (0.92f + 0.12f * Frac), TEXT("Bold"));
		const FString Num = FString::FromInt(Secs);
		Text(Num, FVector2D(P.X, P.Y - Measure(Num, NF).Y * 0.5f), NF, CampusUI::Accent(), 0.5f);
	}
}

void ACampusHUD::Glow(ETex Id, const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color)
{
	// The UI textures carry their shape in alpha only, so additive blending would fill the whole quad: lift the
	// colour toward white instead and let the alpha shape it.
	Tex(Id, Pos, Size, FLinearColor(FMath::Lerp(Color.R, 1.f, 0.25f), FMath::Lerp(Color.G, 1.f, 0.25f), FMath::Lerp(Color.B, 1.f, 0.25f), Color.A));
}

void ACampusHUD::DrawResult(const ACampusGameState* GS)
{
	const bool bMatch = GS->Phase == ECampusMatchPhase::MatchOver;
	const ECampusTeam Mine = GetLocalTeam() == ECampusTeam::None ? ECampusTeam::Alpha : GetLocalTeam();
	const ECampusTeam Theirs = Mine == ECampusTeam::Alpha ? ECampusTeam::Bravo : ECampusTeam::Alpha;
	const ECampusTeam Winner = bMatch ? GS->WinningTeam : GS->RoundWinner;
	const bool bDraw = Winner == ECampusTeam::None;
	const bool bWon = !bDraw && Winner == Mine;
	const bool bLost = !bDraw && !bWon;

	// Timeline: dark, letterboxed build-up, the title slams down on the boom at 0.75 s, the rest follows it in. A
	// match result then folds up into a header over the scoreboard; a round result fades with its phase.
	const float T = Now - ResultStartTime;
	const float Slam = 0.75f;
	const float U = T - Slam;
	const float Fade = bMatch ? 1.f : FMath::Clamp(GS->GetPhaseTimeRemaining() / 0.4f, 0.f, 1.f);
	const float Fold = bMatch ? Smooth01((T - 4.6f) / 0.6f) : 0.f;
	const float Big = 1.f - Fold;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	const FLinearColor Accent = bDraw ? FLinearColor(0.82f, 0.86f, 0.9f) : (bWon ? FLinearColor(1.f, 0.72f, 0.22f) : FLinearColor(1.f, 0.16f, 0.1f));
	const FLinearColor TitleCol = bDraw ? HudWhite : (bWon ? FLinearColor(1.f, 0.97f, 0.9f) : FLinearColor(1.f, 0.24f, 0.18f));

	if (U >= 0.f && !bResultSlamPlayed)
	{
		bResultSlamPlayed = true;
		if (bWon)
		{
			CampusAudio::Play2D(this, TEXT("Medal"), 0.85f, 0.f, CampusAudio::ECategory::Interface);
			CampusAudio::Play2D(this, TEXT("Kill_Confirm"), 0.7f, 0.f, CampusAudio::ECategory::Interface);
		}
		else if (bLost)
		{
			CampusAudio::Play2D(this, TEXT("Body_Fall"), 0.9f, 0.f);
			CampusAudio::Play2D(this, TEXT("Player_Hit"), 0.8f, 0.f);
		}
		else
		{
			CampusAudio::Play2D(this, TEXT("UI_Select"), 0.6f, 0.f, CampusAudio::ECategory::Interface);
		}
	}

	// ------------------------------------------------------------------ backdrop
	const float Dim = Smooth01(T / 0.5f) * Fade;
	Rect(FVector2D::ZeroVector, FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, (bLost ? 0.55f : 0.42f) * Dim));
	Tex(ETex::Vignette, FVector2D::ZeroVector, FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, 0.8f * Dim));
	// The lower half sinks into shadow (the weapon in hand with it) under an accent wash.
	Tex(ETex::GradUp, FVector2D(0.f, H * 0.35f), FVector2D(W, H * 0.65f), FLinearColor(0.f, 0.f, 0.f, 0.75f * Dim));
	Tex(ETex::GradUp, FVector2D(0.f, H * 0.4f), FVector2D(W, H * 0.6f), WithAlpha(Accent, 0.16f * Dim * Big));

	// Embers rising off a win, ash drifting down over a loss.
	if (U > 0.f && !bDraw)
	{
		const float PA = Smooth01(U / 0.4f) * Fade * (0.35f + 0.65f * Big);
		for (int32 i = 0; i < 56; ++i)
		{
			const float H1 = Hash01(i * 1.7f + 0.3f);
			const float H2 = Hash01(i * 3.1f + 1.1f);
			const float H3 = Hash01(i * 5.3f + 2.7f);
			const float Life = 2.4f + 1.8f * H2;
			const float Age = FMath::Fmod(U + H3 * Life, Life);
			const float K = Age / Life;
			const float Sway = FMath::Sin(Age * (1.1f + H2) + i) * 26.f * S;
			const float Y = bWon ? H * (1.03f - K * (0.6f + 0.45f * H2)) : H * (-0.03f + K * (0.75f + 0.3f * H2));
			const FVector2D P(W * H1 + Sway, Y);
			const float R = (bWon ? 1.4f + 2.6f * H3 : 1.2f + 2.f * H3) * S;
			const float A = FMath::Sin(PI * K) * PA;
			if (bWon)
			{
				Glow(ETex::Soft, P - FVector2D(R, R) * 4.f, FVector2D(R, R) * 8.f, WithAlpha(Accent, 0.35f * A));
				Disc(P, R * 0.6f, FLinearColor(1.f, 0.9f, 0.6f, 0.9f * A));
			}
			else
			{
				Rect(P, FVector2D(R, R), FLinearColor(0.55f, 0.53f, 0.52f, 0.55f * A));
			}
		}
	}

	// ------------------------------------------------------------------ scoreboard (match result, after the fold)
	if (Fold > 0.f)
	{
		// Dipped to near black, the scoreboard swaps in underneath and the dip lifts off it.
		const float CoverIn = Smooth01((T - 4.6f) / 0.2f);
		const float CoverOut = Smooth01((T - 4.8f) / 0.45f);
		if (T >= 4.8f)
		{
			DrawScoreboard(GS);
		}
		Rect(FVector2D::ZeroVector, FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, 0.9f * CoverIn * (1.f - CoverOut)));
	}

	// Letterbox bars slide in for the build-up and leave with the fold.
	const float Bars = Smooth01(T / 0.45f) * Big * Fade;
	if (Bars > 0.f)
	{
		const float BarH = 92.f * S * Bars;
		Rect(FVector2D::ZeroVector, FVector2D(W, BarH), FLinearColor(0.f, 0.f, 0.f, 0.94f));
		Rect(FVector2D(0.f, H - BarH), FVector2D(W, BarH), FLinearColor(0.f, 0.f, 0.f, 0.94f));
		const float Line = W * Smooth01((T - 0.2f) / 0.5f);
		Tex(ETex::Band, FVector2D((W - Line) * 0.5f, BarH), FVector2D(Line, 2.f * S), WithAlpha(Accent, 0.7f * Bars));
		Tex(ETex::Band, FVector2D((W - Line) * 0.5f, H - BarH - 2.f * S), FVector2D(Line, 2.f * S), WithAlpha(Accent, 0.7f * Bars));
	}

	// ------------------------------------------------------------------ the title
	// Before the slam it closes in, accelerating (a win zooms down onto the screen, a loss drops from above); after it
	// the impact squashes it once and the frame shakes.
	const float Approach = FMath::Clamp(T / Slam, 0.f, 1.f);
	const float Accel = Approach * Approach * Approach;
	float Scale = 1.f;
	float DropY = 0.f;
	if (U < 0.f)
	{
		Scale = bLost ? FMath::Lerp(1.15f, 1.f, Accel) : FMath::Lerp(1.7f, 1.f, Accel);
		DropY = bLost ? -(1.f - Accel) * 170.f * S : 0.f;
	}
	else
	{
		Scale = 1.f - 0.07f * FMath::Exp(-U * 9.f) * FMath::Cos(U * 32.f);
	}
	Scale *= FMath::Lerp(1.f, 0.46f, Fold);
	FVector2D Shake = FVector2D::ZeroVector;
	if (U > 0.f)
	{
		const float Amp = (bLost ? 16.f : 10.f) * S * FMath::Exp(-U * 6.5f);
		Shake = FVector2D(FMath::Sin(U * 97.f) + 0.5f * FMath::Sin(U * 151.f), FMath::Cos(U * 83.f) + 0.5f * FMath::Sin(U * 131.f)) * Amp * 0.67f;
	}
	// A loss keeps glitching now and then after the hit.
	const bool bGlitch = bLost && U > 0.f && (U < 0.35f || FMath::Fmod(U, 2.3f) < 0.12f);

	const FString Title = bMatch ? (bDraw ? TEXT("DRAW") : (bWon ? TEXT("VICTORY") : TEXT("DEFEAT")))
		: (bDraw ? TEXT("ROUND DRAW") : (bWon ? TEXT("ROUND WON") : TEXT("ROUND LOST")));
	const FString Sub = bMatch ? (bDraw ? TEXT("平局") : (bWon ? TEXT("胜利") : TEXT("失败")))
		: (bDraw ? TEXT("回合平局") : (bWon ? TEXT("回合胜利") : TEXT("回合失败")));
	// Font sizes stepped so the zoom only ever rasterises a handful of glyph sizes.
	const float BaseSize = bMatch ? 150.f : 112.f;
	const float TitleSize = FMath::Max(12.f, FMath::RoundToFloat(BaseSize * Scale / 4.f) * 4.f);
	const int32 Track = FMath::RoundToInt(U < 0.f ? FMath::Lerp(1300.f, 240.f, Smooth01(Approach)) : 240.f);
	const FHudFont TF = F(EFont::Stencil, TitleSize, TEXT("Regular"), Track);
	const FVector2D TSz = Measure(Title, TF);
	const FVector2D TC = FVector2D(Center.X, FMath::Lerp(H * 0.36f, 92.f * S, Fold) + DropY) + Shake;
	const float TitleA = (U < 0.f ? FMath::Sqrt(Accel) * 0.85f : 1.f) * Fade;

	// Dark strip and a soft accent glow seat the title; the glow flares on impact and then breathes.
	const float Strip = Smooth01(T / 0.6f) * Fade;
	Tex(ETex::Band, FVector2D(TC.X - W * 0.6f, TC.Y - TSz.Y * 0.85f), FVector2D(W * 1.2f, TSz.Y * 1.7f), FLinearColor(0.f, 0.f, 0.f, 0.5f * Strip * Big));
	const float Flare = U < 0.f ? Accel * 0.3f : 0.4f + 0.7f * FMath::Exp(-U * 4.f) + 0.06f * FMath::Sin(T * 3.f);
	Glow(ETex::Soft, TC - FVector2D(TSz.X * 0.85f, TSz.Y * 0.95f), FVector2D(TSz.X * 1.7f, TSz.Y * 1.9f), WithAlpha(Accent, 0.45f * Flare * Fade));

	// Impact shockwave: a hot line racing out from the title to the screen edges.
	if (U > 0.f && U < 1.2f)
	{
		const float E = 1.f - FMath::Exp(-U * 7.f);
		const float Hot = FMath::Exp(-U * 3.2f);
		const float LW = W * 1.25f * E;
		const float LH = FMath::Lerp(14.f, 2.f, E) * S;
		Glow(ETex::Band, FVector2D(TC.X - LW * 0.5f, TC.Y - LH * 0.5f), FVector2D(LW, LH), WithAlpha(Accent, Hot));
		Glow(ETex::Band, FVector2D(TC.X - LW * 0.35f, TC.Y - LH * 0.25f), FVector2D(LW * 0.7f, LH * 0.5f), FLinearColor(1.f, 1.f, 1.f, Hot));
	}

	// Chromatic split: smeared wide on the way in, snapping together on impact (and tearing again in a glitch).
	float Split = U < 0.f ? (1.f - Accel) * 28.f * S : 16.f * S * FMath::Exp(-U * 9.f);
	if (bGlitch)
	{
		Split = FMath::Max(Split, (6.f + 10.f * Hash01(FMath::FloorToFloat(T * 30.f))) * S);
	}
	const FVector2D TP(TC.X, TC.Y - TSz.Y * 0.5f);
	if (Split > 0.5f)
	{
		const FLinearColor SplitA = bWon ? FLinearColor(1.f, 0.5f, 0.1f) : FLinearColor(1.f, 0.05f, 0.05f);
		const FLinearColor SplitB = bWon ? FLinearColor(0.3f, 0.7f, 1.f) : FLinearColor(0.1f, 0.9f, 1.f);
		Text(Title, TP - FVector2D(Split, 0.f), TF, WithAlpha(SplitA, 0.5f * TitleA), 0.5f, false);
		Text(Title, TP + FVector2D(Split, 0.f), TF, WithAlpha(SplitB, 0.5f * TitleA), 0.5f, false);
	}
	Text(Title, TP, TF, WithAlpha(TitleCol, TitleA), 0.5f);
	// A win catches a glint sweeping across the letters.
	if (bWon && U > 0.35f && U < 1.15f)
	{
		const float G = (U - 0.35f) / 0.8f;
		const FVector2D GP(TC.X - TSz.X * 0.6f + TSz.X * 1.2f * G, TC.Y);
		Glow(ETex::Soft, GP - FVector2D(60.f, TSz.Y * 0.55f) * FVector2D(S, 1.f), FVector2D(120.f * S, TSz.Y * 1.1f), FLinearColor(1.f, 0.95f, 0.8f, 0.55f * FMath::Sin(PI * G) * Fade));
	}

	// Hairlines growing out from the centre, above and below the title.
	const float Lines = Smooth01((U - 0.05f) / 0.45f) * Fade * Big;
	if (Lines > 0.f)
	{
		const float LL = W * 0.62f * Lines;
		for (const float Side : { -1.f, 1.f })
		{
			const float Y = TC.Y + Side * (TSz.Y * 0.5f + 8.f * S);
			Tex(ETex::Band, FVector2D(TC.X - LL * 0.5f, Y), FVector2D(LL, 2.f * S), WithAlpha(Accent, 0.9f * Lines));
		}
	}

	// ------------------------------------------------------------------ what follows it in
	const float SubA = Smooth01((U - 0.25f) / 0.35f) * Fade * Big;
	const float SubY = TC.Y + TSz.Y * 0.5f + 26.f * S + 18.f * S * (1.f - SubA);
	if (SubA > 0.f)
	{
		const FHudFont SF = F(EFont::Sans, 38.f, TEXT("Black"), FMath::RoundToInt(FMath::Lerp(1100.f, 520.f, SubA)));
		const FVector2D SSz = Measure(Sub, SF);
		Text(Sub, FVector2D(TC.X, SubY), SF, WithAlpha(Accent, SubA), 0.5f);
		const float Dash = 46.f * S * SubA;
		for (const float Side : { -1.f, 1.f })
		{
			const float X = TC.X + Side * (SSz.X * 0.5f + 22.f * S);
			Rect(FVector2D(Side > 0.f ? X : X - Dash, SubY + SSz.Y * 0.5f - 1.f * S), FVector2D(Dash, 2.f * S), WithAlpha(Accent, 0.8f * SubA));
		}
	}

	const float ScoreA = Smooth01((U - 0.5f) / 0.35f) * Fade * Big;
	const float ScoreY = SubY + 74.f * S;
	if (ScoreA > 0.f)
	{
		const bool bRounds = GS->Mode == ECampusMatchMode::Gunfight;
		const int32 MyFinal = bRounds ? GS->GetRoundWins(Mine) : GS->GetTeamScore(Mine);
		const int32 TheirFinal = bRounds ? GS->GetRoundWins(Theirs) : GS->GetTeamScore(Theirs);
		// The tallies count up to the final score.
		const float Count = Smooth01((U - 0.5f) / 0.8f);
		const FHudFont NF = F(EFont::Num, 66.f, TEXT("Bold"));
		const FHudFont LF = F(EFont::Sans, 17.f, TEXT("Black"), 300);
		const float Rise = 14.f * S * (1.f - ScoreA);
		Text(FString::FromInt(FMath::RoundToInt(MyFinal * Count)), FVector2D(TC.X - 34.f * S, ScoreY + Rise), NF, WithAlpha(CampusUI::Friendly(), ScoreA), 1.f);
		Text(FString::FromInt(FMath::RoundToInt(TheirFinal * Count)), FVector2D(TC.X + 34.f * S, ScoreY + Rise), NF, WithAlpha(CampusUI::Enemy(), ScoreA), 0.f);
		Rect(FVector2D(TC.X - 9.f * S, ScoreY + Rise + 40.f * S), FVector2D(18.f * S, 3.f * S), WithAlpha(HudWhite, 0.7f * ScoreA));
		const float LabelY = ScoreY + Rise + 30.f * S;
		Text(TEXT("我方"), FVector2D(TC.X - 190.f * S, LabelY), LF, WithAlpha(CampusUI::Friendly(), 0.85f * ScoreA), 1.f);
		Text(TEXT("敌方"), FVector2D(TC.X + 190.f * S, LabelY), LF, WithAlpha(CampusUI::Enemy(), 0.85f * ScoreA), 0.f);
	}

	const float InfoA = Smooth01((U - 0.85f) / 0.4f) * Fade * Big;
	if (InfoA > 0.f)
	{
		const FString Info = bMatch ? FString::Printf(TEXT("%s  ·  %s"), *GS->GetModeName().ToString(), *CampusMaps::Get(GS->Map).Name.ToString()) : FString::Printf(TEXT("第 %d 回合"), GS->RoundNumber);
		Text(Info, FVector2D(TC.X, ScoreY + 100.f * S), F(EFont::Cond, 17.f, TEXT("SemiBold"), 380), WithAlpha(HudWhite, 0.7f * InfoA), 0.5f);
	}
	if (Fold > 0.f)
	{
		Text(FString::Printf(TEXT("即将返回大厅  %d"), FMath::CeilToInt(GS->GetPhaseTimeRemaining())), FVector2D(Center.X, TC.Y + TSz.Y * 0.5f + 10.f * S),
			F(EFont::Sans, 15.f, TEXT("Medium"), 80), WithAlpha(HudWhite, 0.8f * Fold), 0.5f, false);
	}

	// ------------------------------------------------------------------ impact layers
	// Sparks thrown out of the title: a win's burst flies out and fades, a loss's embers fall away.
	if (U > 0.f && U < 1.6f && !bDraw)
	{
		const float E = 1.f - FMath::Exp(-U * 4.5f);
		const float A = FMath::Exp(-U * 2.4f) * Fade;
		for (int32 i = 0; i < 28; ++i)
		{
			const float Ang = 2.f * PI * Hash01(i * 7.3f + 0.5f);
			const float Dist = (180.f + 520.f * Hash01(i * 2.9f + 4.1f)) * S;
			const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang) * 0.55f);
			FVector2D P = TC + Dir * TSz.X * 0.25f + Dir * Dist * E;
			if (bLost)
			{
				P.Y += 420.f * S * U * U;
			}
			const float Len = (10.f + 26.f * Hash01(i * 1.3f)) * S * (1.f - E * 0.6f);
			SoftLine(P - Dir.GetSafeNormal() * Len, P, 2.f * S, WithAlpha(i % 3 == 0 ? FLinearColor(1.f, 0.95f, 0.85f) : Accent, A));
		}
	}

	// Glitch: torn horizontal slices across the frame.
	if (bGlitch)
	{
		const float Seed = FMath::FloorToFloat(T * 30.f);
		for (int32 i = 0; i < 6; ++i)
		{
			const float Y = H * Hash01(Seed + i * 3.7f);
			const float SH = (2.f + 14.f * Hash01(Seed * 1.3f + i)) * S;
			const float X = W * 0.3f * (Hash01(Seed + i * 9.1f) - 0.5f);
			Rect(FVector2D(X, Y), FVector2D(W, SH), WithAlpha(i % 2 ? Accent : HudWhite, 0.06f + 0.08f * Hash01(Seed + i)));
		}
	}

	// The flash of the impact, over everything.
	if (U > 0.f)
	{
		const float FlashA = 0.55f * FMath::Exp(-U * 10.f) * Fade;
		Rect(FVector2D::ZeroVector, FVector2D(W, H), FMath::Lerp(FLinearColor(1.f, 1.f, 1.f, FlashA), WithAlpha(Accent, FlashA), 0.35f));
	}
}

void ACampusHUD::DrawBanner()
{
	const float Age = Now - BannerTime;
	if (Age > BannerDuration || BannerTitle.IsEmpty())
	{
		return;
	}
	if (bCompactBanner)
	{
		const float Alpha = Smooth01(Age / 0.1f) * FMath::Clamp((BannerDuration - Age) / 0.25f, 0.f, 1.f);
		Text(BannerTitle.ToString(), FVector2D(Center.X, 130.f * S), F(EFont::Sans, 24.f, TEXT("Bold"), 80), WithAlpha(BannerColor, Alpha), 0.5f);
		return;
	}
	const float In = Smooth01(Age / 0.3f);
	const float Out = FMath::Clamp((BannerDuration - Age) / 0.45f, 0.f, 1.f);
	const float Alpha = In * Out;
	const float Y = Canvas->ClipY * 0.25f;
	const float W = 900.f * S * (0.7f + 0.3f * In);
	Tex(ETex::Band, FVector2D(Center.X - W * 0.5f, Y - 18.f * S), FVector2D(W, 128.f * S), FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha));
	Tex(ETex::Band, FVector2D(Center.X - W * 0.5f, Y - 18.f * S), FVector2D(W, 2.f * S), WithAlpha(BannerColor, 0.9f * Alpha));
	Tex(ETex::Band, FVector2D(Center.X - W * 0.5f, Y + 108.f * S), FVector2D(W, 2.f * S), WithAlpha(BannerColor, 0.9f * Alpha));
	const float Track = 80.f + 300.f * (1.f - In);
	Text(BannerTitle.ToString(), FVector2D(Center.X, Y), F(EFont::Sans, 50.f, TEXT("Black"), FMath::RoundToInt(Track)), WithAlpha(BannerColor, Alpha), 0.5f);
	if (!BannerSubtitle.IsEmpty())
	{
		Text(BannerSubtitle.ToString(), FVector2D(Center.X, Y + 70.f * S), F(EFont::Sans, 17.f, TEXT("Medium"), 60), WithAlpha(HudWhite, 0.85f * Alpha), 0.5f, false);
	}
}

void ACampusHUD::DrawScoreboard(const ACampusGameState* GS)
{
	const ECampusTeam Mine = GetLocalTeam() == ECampusTeam::None ? ECampusTeam::Alpha : GetLocalTeam();
	const ECampusTeam Theirs = Mine == ECampusTeam::Alpha ? ECampusTeam::Bravo : ECampusTeam::Alpha;
	const float W = 980.f * S;
	const float Left = Center.X - W * 0.5f;
	float Y = Canvas->ClipY * 0.24f;
	const float RowH = 34.f * S;
	const float Cols[5] = { Left + 470.f * S, Left + 570.f * S, Left + 660.f * S, Left + 750.f * S, Left + 880.f * S };

	Rect(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(0.f, 0.f, 0.f, 0.35f));
	Text(TEXT("SCOREBOARD"), FVector2D(Left, Y - 64.f * S), F(EFont::Cond, 15.f, TEXT("SemiBold"), 360), CampusUI::Accent());
	Text(FString::Printf(TEXT("%s  ·  %s"), *GS->GetModeName().ToString(), *CampusMaps::Get(GS->Map).Name.ToString()), FVector2D(Left, Y - 46.f * S), F(EFont::Sans, 26.f, TEXT("Black")), HudWhite);

	auto Header = [&](const FString& Title, int32 Score, const FLinearColor& Col)
	{
		Tex(ETex::GradL, FVector2D(Left, Y), FVector2D(W, RowH + 8.f * S), WithAlpha(Col, 0.55f));
		Rect(FVector2D(Left, Y), FVector2D(4.f * S, RowH + 8.f * S), Col);
		Text(Title, FVector2D(Left + 18.f * S, Y + 6.f * S), F(EFont::Sans, 19.f, TEXT("Black"), 80), HudWhite);
		Text(FString::FromInt(Score), FVector2D(Left + W - 18.f * S, Y - 1.f * S), F(EFont::Num, 32.f, TEXT("Bold")), HudWhite, 1.f);
		Y += RowH + 14.f * S;
		const TCHAR* Labels[5] = { TEXT("得分"), TEXT("击杀"), TEXT("死亡"), TEXT("助攻"), TEXT("延迟") };
		for (int32 i = 0; i < 5; ++i)
		{
			Text(Labels[i], FVector2D(Cols[i], Y), F(EFont::Sans, 12.f, TEXT("Bold"), 80), WithAlpha(HudWhite, 0.55f), 0.5f, false);
		}
		Y += 22.f * S;
	};
	auto Rows = [&](ECampusTeam Team)
	{
		int32 Index = 0;
		for (const ACampusPlayerState* PS : GS->GetTeamPlayers(Team))
		{
			const bool bMe = PlayerOwner && PS == PlayerOwner->PlayerState;
			Rect(FVector2D(Left, Y), FVector2D(W, RowH - 3.f * S), bMe ? WithAlpha(CampusUI::Accent(), 0.16f) : FLinearColor(0.02f, 0.025f, 0.03f, (Index % 2) ? 0.62f : 0.5f));
			if (bMe)
			{
				Rect(FVector2D(Left, Y), FVector2D(3.f * S, RowH - 3.f * S), CampusUI::Accent());
			}
			Text(PS->GetPlayerName(), FVector2D(Left + 18.f * S, Y + 6.f * S), F(EFont::Sans, 16.f, bMe ? TEXT("Black") : TEXT("Bold")), HudWhite);
			if (PS->IsABot())
			{
				const float NW = Measure(PS->GetPlayerName(), F(EFont::Sans, 16.f, TEXT("Bold"))).X;
				Text(TEXT("AI"), FVector2D(Left + 30.f * S + NW, Y + 8.f * S), F(EFont::Cond, 13.f, TEXT("Bold"), 100), WithAlpha(HudWhite, 0.45f), 0.f, false);
			}
			const int32 Values[4] = { PS->Points, PS->Kills, PS->Deaths, PS->Assists };
			for (int32 i = 0; i < 4; ++i)
			{
				Text(FString::FromInt(Values[i]), FVector2D(Cols[i], Y + 3.f * S), F(EFont::Num, 20.f, TEXT("Bold")), HudWhite, 0.5f, false);
			}
			Text(PS->IsABot() ? TEXT("—") : FString::FromInt(FMath::RoundToInt(PS->GetPingInMilliseconds())), FVector2D(Cols[4], Y + 3.f * S), F(EFont::Num, 20.f, TEXT("SemiBold")), WithAlpha(HudWhite, 0.65f), 0.5f, false);
			Y += RowH;
			++Index;
		}
		Y += 18.f * S;
	};

	Header(TEXT("我方"), GS->Mode == ECampusMatchMode::Gunfight ? GS->GetRoundWins(Mine) : GS->GetTeamScore(Mine), CampusUI::Friendly());
	Rows(Mine);
	Header(TEXT("敌方"), GS->Mode == ECampusMatchMode::Gunfight ? GS->GetRoundWins(Theirs) : GS->GetTeamScore(Theirs), CampusUI::Enemy());
	Rows(Theirs);
}

void ACampusHUD::DrawFPS()
{
	const UCampusGameInstance* GI = Cast<UCampusGameInstance>(GetGameInstance());
	if (!GI || !GI->GetProfile().bShowFPS)
	{
		return;
	}
	// With frame generation the display gets more frames than the game renders; show both.
	const float Presented = CampusGraphics::PresentedFrameRate();
	const FString Label = Presented > 1.f
		? FString::Printf(TEXT("%d FPS  (渲染 %d)"), FMath::RoundToInt(Presented), FMath::RoundToInt(1.f / Dt))
		: FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(1.f / Dt));
	Text(Label, FVector2D(Canvas->ClipX - 20.f * S, 12.f * S), F(EFont::Num, 15.f, TEXT("Bold")), WithAlpha(CampusUI::Accent(), 0.85f), 1.f, false);
}

#undef LOCTEXT_NAMESPACE
