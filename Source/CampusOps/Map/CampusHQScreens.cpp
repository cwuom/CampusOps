#include "Map/CampusHQScreens.h"
#include "Game/CampusMaps.h"
#include "Game/CampusGraphics.h"
#include "UI/CampusUIStyle.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "TextureResource.h"

namespace
{
	// Restrained command-centre palette: navy ground, cyan linework, white type, amber / red / green states.
	const FLinearColor Ground(0.006f, 0.011f, 0.018f);
	const FLinearColor Band(0.018f, 0.04f, 0.06f);
	const FLinearColor Line(0.22f, 0.62f, 0.82f);
	const FLinearColor LineDim(0.06f, 0.15f, 0.21f);
	const FLinearColor Ink(0.80f, 0.86f, 0.90f);
	const FLinearColor InkDim(0.36f, 0.45f, 0.52f);
	const FLinearColor Amber(1.f, 0.62f, 0.12f);
	const FLinearColor Red(1.f, 0.22f, 0.16f);
	const FLinearColor Green(0.30f, 0.92f, 0.42f);
	const FLinearColor Blue(0.22f, 0.55f, 1.f);

	struct FDisplayFormat
	{
		int32 Width;
		int32 Height;
		/** Redraws per second. */
		float Rate;
	};

	// Picture sizes follow the panels they span: one panel is 241 x 106 cm, two side by side 488 x 106, a 2 x 2 block
	// 488 x 218, one column of two 241 x 218 (6 cm bezels included).
	const FDisplayFormat Formats[(int32)ECampusHQDisplay::Count] = {
		{ 1344, 600, 6.f },   // Map
		{ 640, 580, 20.f },   // Radar
		{ 640, 580, 8.f },    // Drone
		{ 1344, 600, 8.f },   // Cameras
		{ 768, 338, 4.f },    // Telemetry
		{ 1472, 320, 2.f },   // Comms
		{ 768, 338, 2.f },    // Clock
		{ 768, 338, 1.f },    // Readiness
		{ 768, 338, 0.5f },   // Weather
	};

	/** Thin wrapper over canvas items: boxes, lines, rings, discs and text in pixel units. */
	struct FPaint
	{
		UCanvas* C;

		void Box(float X, float Y, float W, float H, const FLinearColor& Col) const
		{
			FCanvasTileItem T(FVector2D(X, Y), FVector2D(W, H), Col);
			T.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(T);
		}

		void Frame(float X, float Y, float W, float H, const FLinearColor& Col, float Th = 1.5f) const
		{
			Box(X, Y, W, Th, Col);
			Box(X, Y + H - Th, W, Th, Col);
			Box(X, Y, Th, H, Col);
			Box(X + W - Th, Y, Th, H, Col);
		}

		void Seg(const FVector2D& A, const FVector2D& B, const FLinearColor& Col, float Th = 1.5f) const
		{
			FCanvasLineItem L(A, B);
			L.SetColor(Col);
			L.LineThickness = Th;
			C->DrawItem(L);
		}

		void Dashed(const FVector2D& A, const FVector2D& B, const FLinearColor& Col, float Th, float Dash, float Phase = 0.f) const
		{
			const float Len = FVector2D::Distance(A, B);
			const FVector2D D = (B - A) / FMath::Max(Len, 1.f);
			for (float T = -FMath::Fmod(Phase, Dash * 2.f); T < Len; T += Dash * 2.f)
			{
				const float T0 = FMath::Max(T, 0.f);
				const float T1 = FMath::Min(T + Dash, Len);
				if (T1 > T0)
				{
					Seg(A + D * T0, A + D * T1, Col, Th);
				}
			}
		}

		void Ring(const FVector2D& Ce, float R, const FLinearColor& Col, float Th = 1.5f, int32 N = 72) const
		{
			for (int32 i = 0; i < N; ++i)
			{
				const float A0 = 2.f * PI * i / N;
				const float A1 = 2.f * PI * (i + 1) / N;
				Seg(Ce + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R, Ce + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R, Col, Th);
			}
		}

		void Tri(const FVector2D& A, const FVector2D& B, const FVector2D& Cc, const FLinearColor& Col) const
		{
			FCanvasTriangleItem T(A, B, Cc, GWhiteTexture);
			T.SetColor(Col);
			T.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(T);
		}

		void Disc(const FVector2D& Ce, float R, const FLinearColor& Col, int32 N = 20) const
		{
			for (int32 i = 0; i < N; ++i)
			{
				const float A0 = 2.f * PI * i / N;
				const float A1 = 2.f * PI * (i + 1) / N;
				Tri(Ce, Ce + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R, Ce + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R, Col);
			}
		}

		static FSlateFontInfo FontFor(CampusUI::EFont Family, float Pt, FName Weight)
		{
			return CampusUI::Font(Family, Pt, Weight);
		}

		static float Width(const FString& S, float Pt, CampusUI::EFont Family = CampusUI::EFont::Sans, FName Weight = TEXT("SemiBold"))
		{
			if (!FSlateApplication::IsInitialized())
			{
				return S.Len() * Pt * 0.6f;
			}
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, FontFor(Family, Pt, Weight)).X;
		}

		/** Align 0 left, 1 centre, 2 right of X; Y is the top of the line. */
		void Text(const FString& S, float X, float Y, float Pt, const FLinearColor& Col, int32 Align = 0, CampusUI::EFont Family = CampusUI::EFont::Sans, FName Weight = TEXT("SemiBold")) const
		{
			const float W = Align == 0 ? 0.f : Width(S, Pt, Family, Weight);
			FCanvasTextItem Item(FVector2D(X - (Align == 1 ? W * 0.5f : Align == 2 ? W : 0.f), Y), FText::FromString(S), FontFor(Family, Pt, Weight), Col);
			const UFont* Asset = CampusUI::FontAsset(Family);
			Item.Font = Asset ? Asset : GEngine->GetSmallFont();
			Item.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(Item);
		}

		/** Title bar: Chinese title, its English caption, and a status on the right. Returns the height used. */
		float Header(const FVector2D& Size, const FString& Zh, const FString& En, const FString& Right = FString(), const FLinearColor& RightCol = Green) const
		{
			const float H = FMath::Clamp(Size.Y * 0.11f, 30.f, 46.f);
			Box(0.f, 0.f, Size.X, H, Band);
			Box(0.f, H - 2.f, Size.X, 2.f, LineDim);
			Box(0.f, 0.f, 4.f, H, Line);
			const float Pt = H * 0.42f;
			Text(Zh, 14.f, H * 0.12f, Pt, Ink, 0, CampusUI::EFont::Sans, TEXT("Bold"));
			Text(En, 14.f + Width(Zh, Pt, CampusUI::EFont::Sans, TEXT("Bold")) + 12.f, H * 0.30f, Pt * 0.62f, InkDim, 0, CampusUI::EFont::Cond, TEXT("SemiBold"));
			if (!Right.IsEmpty())
			{
				Text(Right, Size.X - 14.f, H * 0.26f, Pt * 0.72f, RightCol, 2, CampusUI::EFont::Cond, TEXT("Bold"));
			}
			return H;
		}
	};

	FString Hms(double Seconds)
	{
		const int64 S = (int64)FMath::Fmod(FMath::Max(Seconds, 0.0), 86400.0);
		return FString::Printf(TEXT("%02lld:%02lld:%02lld"), S / 3600, (S / 60) % 60, S % 60);
	}

	struct FCommsLine
	{
		const TCHAR* Who;
		const TCHAR* Text;
	};

	// The assault on the school in the radio log, round and round.
	const FCommsLine CommsLines[] = {
		{ TEXT("HQ"), TEXT("各單位注意，行動代號「光華」，全區戒備。") },
		{ TEXT("ALPHA-1"), TEXT("收到，已抵達集結點，準備就緒。") },
		{ TEXT("BRAVO-2"), TEXT("收到，東側圍牆外待命。") },
		{ TEXT("HQ"), TEXT("UAV-2 上線，熱成像確認行政大樓三樓兩名目標。") },
		{ TEXT("CHARLIE-1"), TEXT("狙擊位置就緒，視野良好，風向西北 12 節。") },
		{ TEXT("ALPHA-1"), TEXT("COPY. 沿北棟走廊推進，預計兩分鐘抵達中庭。") },
		{ TEXT("HQ"), TEXT("注意，司令台後方有移動目標，研判為敵哨。") },
		{ TEXT("BRAVO-2"), TEXT("發現可疑車輛，位置 E21050 N27450。") },
		{ TEXT("UAV-2"), TEXT("目標持續靜止，無增援跡象。") },
		{ TEXT("HQ"), TEXT("H 小時前 15 分鐘，各組確認通訊。") },
		{ TEXT("ALPHA-2"), TEXT("通訊良好，破門組就位。") },
		{ TEXT("ALL"), TEXT("ROGER.") },
		{ TEXT("CHARLIE-1"), TEXT("一號目標確認，等待射擊許可。") },
		{ TEXT("HQ"), TEXT("准許，依交戰規則執行。") },
		{ TEXT("ALPHA-1"), TEXT("中庭淨空，進入行政大樓。") },
		{ TEXT("BRAVO-2"), TEXT("東側封鎖完成，無人脫離。") },
	};

	FLinearColor CallsignColor(const TCHAR* Who)
	{
		const FString W(Who);
		if (W == TEXT("HQ")) return Amber;
		if (W.StartsWith(TEXT("ALPHA"))) return Blue;
		if (W.StartsWith(TEXT("BRAVO"))) return FLinearColor(0.35f, 0.85f, 0.95f);
		if (W.StartsWith(TEXT("CHARLIE"))) return Green;
		return InkDim;
	}
}

ACampusHQScreens::ACampusHQScreens()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Rng.Initialize(20261015);
}

float ACampusHQScreens::Aspect(ECampusHQDisplay Display)
{
	const FDisplayFormat& F = Formats[FMath::Clamp((int32)Display, 0, (int32)ECampusHQDisplay::Count - 1)];
	return (float)F.Width / (float)F.Height;
}

UTextureRenderTarget2D* ACampusHQScreens::Target(ECampusHQDisplay Display)
{
	const int32 Index = (int32)Display;
	if (Targets.Num() < (int32)ECampusHQDisplay::Count)
	{
		Targets.SetNum((int32)ECampusHQDisplay::Count);
		NextDraw.Init(0.f, (int32)ECampusHQDisplay::Count);
	}
	if (!Targets.IsValidIndex(Index))
	{
		return nullptr;
	}
	if (!Targets[Index])
	{
		const FDisplayFormat& F = Formats[Index];
		// Mipmapped, so type and hairlines stay put on a panel seen across the hangar.
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, F.Width, F.Height, RTF_RGBA8_SRGB, Ground, true);
		if (RT)
		{
			RT->MipsSamplerFilter = TF_Trilinear;
			RT->Filter = TF_Trilinear;
		}
		Targets[Index] = RT;
	}
	return Targets[Index];
}

void ACampusHQScreens::DumpToDisk()
{
	static const TCHAR* Names[(int32)ECampusHQDisplay::Count] = { TEXT("map"), TEXT("radar"), TEXT("drone"), TEXT("cameras"), TEXT("telemetry"), TEXT("comms"), TEXT("clock"), TEXT("readiness"), TEXT("weather") };
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("AutoShots");
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		if (Targets[i])
		{
			UKismetRenderingLibrary::ExportRenderTarget(this, Targets[i], Dir, FString::Printf(TEXT("hqs_%s.png"), Names[i]));
		}
	}
	for (int32 i = 0; i < CaptureTargets.Num(); ++i)
	{
		if (CaptureTargets[i])
		{
			UKismetRenderingLibrary::ExportRenderTarget(this, CaptureTargets[i], Dir, FString::Printf(TEXT("hqs_feed%d.png"), i));
		}
	}
}

void ACampusHQScreens::BeginPlay()
{
	Super::BeginPlay();
	for (int32 i = 0; i < 48; ++i)
	{
		NetIn.Add(0.45f + 0.25f * FMath::Sin(i * 0.35f) + Rng.FRandRange(-0.08f, 0.08f));
		NetOut.Add(0.3f + 0.15f * FMath::Sin(i * 0.22f + 1.f) + Rng.FRandRange(-0.06f, 0.06f));
		LinkQuality.Add(0.85f + Rng.FRandRange(-0.05f, 0.05f));
	}
	for (int32 i = 0; i < 8; ++i)
	{
		CommsShown.Add(i);
		CommsTime.Add(MissionSeconds() - (8 - i) * 23.0);
	}
	NextComms = 3.f;

	// Security cameras round the hangar and the drone over the pad: small, monochrome, a few frames a second at most.
	for (int32 i = 0; i < CameraSpots.Num(); ++i)
	{
		const FCamera& Spot = CameraSpots[i];
		const bool bDrone = i == 0;
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, bDrone ? 448 : 384, bDrone ? 404 : 216, RTF_RGBA8_SRGB, FLinearColor::Black, false);
		USceneCaptureComponent2D* Cap = NewObject<USceneCaptureComponent2D>(this);
		Cap->SetupAttachment(RootComponent);
		Cap->CaptureSource = SCS_FinalColorLDR;
		Cap->TextureTarget = RT;
		Cap->bCaptureEveryFrame = false;
		Cap->bCaptureOnMovement = false;
		Cap->FOVAngle = Spot.FOV;
		FEngineShowFlags& Flags = Cap->ShowFlags;
		Flags.SetMotionBlur(false);
		Flags.SetLumenGlobalIllumination(false);
		Flags.SetLumenReflections(false);
		Flags.SetVolumetricFog(false);
		Flags.SetDynamicShadows(CampusGraphics::EffectsLevel() >= 2);
		FPostProcessSettings& PP = Cap->PostProcessSettings;
		Cap->PostProcessBlendWeight = 1.f;
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FVector4(0.f, 0.f, 0.f, 1.f);
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(1.f, 1.f, 1.f, bDrone ? 1.35f : 1.12f);
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = bDrone ? 3.5f : 2.5f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.7f;
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.35f;
		Cap->RegisterComponent();
		Cap->SetWorldLocationAndRotation(Spot.Location, Spot.Rotation);
		Captures.Add(Cap);
		CaptureTargets.Add(RT);
	}
	for (int32 i = 0; i < (int32)ECampusHQDisplay::Count; ++i)
	{
		Target((ECampusHQDisplay)i);
	}
}

double ACampusHQScreens::MissionSeconds() const
{
	return 23.0 * 3600.0 + 40.0 * 60.0 + Clock;
}

void ACampusHQScreens::UpdateCaptures(float DeltaSeconds)
{
	if (Captures.Num() == 0)
	{
		return;
	}
	// The drone circles the pad.
	if (USceneCaptureComponent2D* Drone = Captures[0])
	{
		const float A = Clock * 0.05f;
		const FVector At = DroneTarget + FVector(FMath::Cos(A) * 1800.f, FMath::Sin(A) * 1800.f, 3600.f);
		Drone->SetWorldLocationAndRotation(At, (DroneTarget - At).Rotation());
	}
	const int32 Effects = CampusGraphics::EffectsLevel();
	const float Every = Effects >= 2 ? 0.07f : Effects == 1 ? 0.3f : 1.5f;
	CaptureTimer -= DeltaSeconds;
	if (CaptureTimer > 0.f)
	{
		return;
	}
	CaptureTimer = Every;
	NextCapture = (NextCapture + 1) % Captures.Num();
	if (USceneCaptureComponent2D* Cap = Captures[NextCapture])
	{
		Cap->CaptureScene();
	}
}

void ACampusHQScreens::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	Clock += DeltaSeconds;
	UpdateCaptures(DeltaSeconds);

	TelemetryTimer -= DeltaSeconds;
	if (TelemetryTimer <= 0.f)
	{
		TelemetryTimer = 0.25f;
		auto Walk = [this](TArray<float>& H, float Center, float Swing)
		{
			const float Last = H.Num() ? H.Last() : Center;
			H.Add(FMath::Clamp(Last + (Center - Last) * 0.15f + Rng.FRandRange(-Swing, Swing), 0.04f, 0.98f));
			if (H.Num() > 48)
			{
				H.RemoveAt(0);
			}
		};
		Walk(NetIn, 0.5f + 0.2f * FMath::Sin(Clock * 0.3f), 0.07f);
		Walk(NetOut, 0.32f, 0.05f);
		Walk(LinkQuality, 0.86f, 0.02f);
	}
	NextComms -= DeltaSeconds;
	if (NextComms <= 0.f)
	{
		NextComms = Rng.FRandRange(3.5f, 7.f);
		CommsShown.Add((CommsShown.Num() ? CommsShown.Last() + 1 : 0) % UE_ARRAY_COUNT(CommsLines));
		CommsTime.Add(MissionSeconds());
		if (CommsShown.Num() > 12)
		{
			CommsShown.RemoveAt(0);
			CommsTime.RemoveAt(0);
		}
	}

	for (int32 i = 0; i < (int32)ECampusHQDisplay::Count; ++i)
	{
		if (NextDraw.IsValidIndex(i) && Clock >= NextDraw[i])
		{
			NextDraw[i] = Clock + 1.f / Formats[i].Rate;
			Redraw((ECampusHQDisplay)i);
		}
	}
}

void ACampusHQScreens::Redraw(ECampusHQDisplay Display)
{
	UTextureRenderTarget2D* RT = Target(Display);
	if (!RT)
	{
		return;
	}
	UCanvas* Canvas = nullptr;
	FVector2D Size(RT->SizeX, RT->SizeY);
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, Canvas, Size, Ctx);
	if (Canvas && Canvas->Canvas)
	{
		FCanvas* DrawCanvas = Canvas->Canvas;
		const uint32 AllowedModes = DrawCanvas->GetAllowedModes();
		// 同一张屏幕的图元可以保留顺序合批，避免每条雷达线都单独提交渲染
		DrawCanvas->SetAllowedModes(AllowedModes & ~FCanvas::Allow_Flush);
		FCanvasTileItem Bg(FVector2D::ZeroVector, Size, Ground);
		Canvas->DrawItem(Bg);
		switch (Display)
		{
		case ECampusHQDisplay::Map: DrawMap(Canvas, Size); break;
		case ECampusHQDisplay::Radar: DrawRadar(Canvas, Size); break;
		case ECampusHQDisplay::Drone: DrawDrone(Canvas, Size); break;
		case ECampusHQDisplay::Cameras: DrawCameras(Canvas, Size); break;
		case ECampusHQDisplay::Telemetry: DrawTelemetry(Canvas, Size); break;
		case ECampusHQDisplay::Comms: DrawComms(Canvas, Size); break;
		case ECampusHQDisplay::Clock: DrawClock(Canvas, Size); break;
		case ECampusHQDisplay::Readiness: DrawReadiness(Canvas, Size); break;
		case ECampusHQDisplay::Weather: DrawWeather(Canvas, Size); break;
		default: break;
		}
		DrawCanvas->SetAllowedModes(AllowedModes);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
	// Rebuilds the mip chain from what was just drawn.
	RT->UpdateResourceImmediate(false);
}

void ACampusHQScreens::DrawMap(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("戰術地圖"), TEXT("TACTICAL MAP"), TEXT("目標：光華國中   OBJ: KUANG-HUA JHS"), Amber);
	const FBox2D Area(FVector2D(70.f, Top + 10.f), FVector2D(Size.X - 20.f, Size.Y - 44.f));

	const UCampusMapCatalog* Catalog = UCampusMapCatalog::Get();
	const FCampusMapPreviewData* Campus = Catalog ? Catalog->Find(ECampusMap::Campus) : nullptr;
	FBox Bounds = Campus && Campus->PlayableBounds.IsValid ? Campus->PlayableBounds : FBox(FVector(-5000.f, -5800.f, 0.f), FVector(5000.f, 5800.f, 0.f));
	// North up: world +X runs up the picture, +Y to the right.
	const FVector2D WorldMin(Bounds.Min.Y, Bounds.Min.X);
	const FVector2D WorldSize(Bounds.Max.Y - Bounds.Min.Y, Bounds.Max.X - Bounds.Min.X);
	const float Scale = FMath::Min(Area.GetSize().X / (WorldSize.X * 1.9f), Area.GetSize().Y / (WorldSize.Y * 1.08f));
	const FVector2D Center = Area.GetCenter() + FVector2D(-Area.GetSize().X * 0.08f, 0.f);
	auto ToScreen = [&](float WX, float WY)
	{
		return Center + FVector2D((WY - (Bounds.Min.Y + Bounds.Max.Y) * 0.5f) * Scale, -(WX - (Bounds.Min.X + Bounds.Max.X) * 0.5f) * Scale);
	};

	// The city round the school: blocks between a street grid, faint.
	FRandomStream City(77);
	for (float Y = Area.Min.Y; Y < Area.Max.Y; Y += 46.f)
	{
		for (float X = Area.Min.X; X < Area.Max.X; X += 58.f)
		{
			const float W = City.FRandRange(26.f, 48.f);
			const float H = City.FRandRange(20.f, 38.f);
			P.Box(X + 4.f, Y + 4.f, W, H, FLinearColor(0.016f, 0.03f, 0.042f));
		}
	}
	// The campus block itself is cleared and walled.
	const FVector2D C0 = ToScreen(Bounds.Max.X, Bounds.Min.Y);
	const FVector2D C1 = ToScreen(Bounds.Min.X, Bounds.Max.Y);
	P.Box(C0.X - 8.f, C0.Y - 8.f, C1.X - C0.X + 16.f, C1.Y - C0.Y + 16.f, Ground);
	// Grid every 25 m with eastings / northings.
	const float GridStep = 2500.f;
	for (float WY = FMath::FloorToFloat(Bounds.Min.Y / GridStep) * GridStep - GridStep * 4.f; WY <= Bounds.Max.Y + GridStep * 4.f; WY += GridStep)
	{
		const float SX = ToScreen(0.f, WY).X;
		if (SX < Area.Min.X || SX > Area.Max.X)
		{
			continue;
		}
		P.Box(SX, Area.Min.Y, 1.f, Area.GetSize().Y, LineDim);
		P.Text(FString::Printf(TEXT("E %d"), 20900 + FMath::RoundToInt(WY / 100.f)), SX, Area.Max.Y + 6.f, 13.f, InkDim, 1, CampusUI::EFont::Num);
	}
	for (float WX = FMath::FloorToFloat(Bounds.Min.X / GridStep) * GridStep - GridStep; WX <= Bounds.Max.X + GridStep; WX += GridStep)
	{
		const float SY = ToScreen(WX, 0.f).Y;
		if (SY < Area.Min.Y || SY > Area.Max.Y)
		{
			continue;
		}
		P.Box(Area.Min.X, SY, Area.GetSize().X, 1.f, LineDim);
		P.Text(FString::Printf(TEXT("N %d"), 27400 + FMath::RoundToInt(WX / 100.f)), Area.Min.X - 6.f, SY - 8.f, 13.f, InkDim, 2, CampusUI::EFont::Num);
	}
	// The school as surveyed: buildings outlined, courts and lawns tinted.
	if (Campus)
	{
		for (const FCampusMapRect& R : Campus->Rects)
		{
			const FVector2D A = ToScreen(R.Max.X, R.Min.Y);
			const FVector2D B = ToScreen(R.Min.X, R.Max.Y);
			const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
			const FVector2D Sz(FMath::Abs(B.X - A.X), FMath::Abs(B.Y - A.Y));
			switch (R.Kind)
			{
			case 0:
				P.Box(Min.X, Min.Y, Sz.X, Sz.Y, FLinearColor(0.04f, 0.10f, 0.15f));
				P.Frame(Min.X, Min.Y, Sz.X, Sz.Y, Line, 1.5f);
				break;
			case 1:
				P.Box(Min.X, Min.Y, Sz.X, Sz.Y, FLinearColor(0.03f, 0.07f, 0.06f));
				P.Frame(Min.X, Min.Y, Sz.X, Sz.Y, FLinearColor(0.12f, 0.35f, 0.3f), 1.f);
				break;
			case 3:
				P.Box(Min.X, Min.Y, Sz.X, Sz.Y, FLinearColor(0.02f, 0.06f, 0.035f));
				break;
			default:
				P.Frame(Min.X, Min.Y, Sz.X, Sz.Y, LineDim, 1.f);
				break;
			}
		}
	}
	P.Frame(C0.X - 8.f, C0.Y - 8.f, C1.X - C0.X + 16.f, C1.Y - C0.Y + 16.f, FLinearColor(0.16f, 0.4f, 0.55f), 1.f);

	// Infiltration route from the south gate to the admin block, the assault force moving up it.
	const FVector2D Route[] = { ToScreen(Bounds.Min.X - 600.f, -900.f), ToScreen(-4300.f, -900.f), ToScreen(-2600.f, -1800.f), ToScreen(-1400.f, -1800.f), ToScreen(-200.f, -600.f) };
	float RouteLen = 0.f;
	for (int32 i = 1; i < UE_ARRAY_COUNT(Route); ++i)
	{
		P.Dashed(Route[i - 1], Route[i], Amber, 2.f, 9.f, -Clock * 18.f);
		RouteLen += FVector2D::Distance(Route[i - 1], Route[i]);
	}
	auto AlongRoute = [&](float T)
	{
		float D = FMath::Fmod(T, 1.f) * RouteLen;
		for (int32 i = 1; i < UE_ARRAY_COUNT(Route); ++i)
		{
			const float L = FVector2D::Distance(Route[i - 1], Route[i]);
			if (D <= L)
			{
				return TPair<FVector2D, FVector2D>(FMath::Lerp(Route[i - 1], Route[i], D / FMath::Max(L, 1.f)), (Route[i] - Route[i - 1]).GetSafeNormal());
			}
			D -= L;
		}
		return TPair<FVector2D, FVector2D>(Route[UE_ARRAY_COUNT(Route) - 1], FVector2D(0.f, -1.f));
	};
	const float Progress = FMath::Fmod(Clock / 150.f, 1.f) * 0.85f;
	for (int32 i = 0; i < 4; ++i)
	{
		const TPair<FVector2D, FVector2D> At = AlongRoute(FMath::Max(Progress - i * 0.03f, 0.f));
		const FVector2D F = At.Value;
		const FVector2D Side(-F.Y, F.X);
		const FVector2D Pos = At.Key + Side * ((i % 2) ? 9.f : -9.f);
		P.Tri(Pos + F * 11.f, Pos - F * 7.f + Side * 7.f, Pos - F * 7.f - Side * 7.f, Blue);
		if (i == 0)
		{
			P.Text(TEXT("ALPHA"), Pos.X + 12.f, Pos.Y - 20.f, 13.f, Blue, 0, CampusUI::EFont::Cond, TEXT("Bold"));
		}
	}
	// Hostiles: confirmed ones steady, last-seen ones blinking.
	const FVector2D Hostiles[] = { ToScreen(300.f, -300.f), ToScreen(800.f, 900.f), ToScreen(1900.f, -1200.f), ToScreen(-600.f, 2600.f), ToScreen(2400.f, 2200.f) };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Hostiles); ++i)
	{
		const bool bLastSeen = i >= 3;
		if (bLastSeen && FMath::Fmod(Clock + i * 0.3f, 1.2f) > 0.75f)
		{
			continue;
		}
		const FVector2D H = Hostiles[i];
		const float R = 8.f;
		const FLinearColor Col = bLastSeen ? CampusUI::Alpha(Red, 0.6f) : Red;
		P.Seg(H + FVector2D(0.f, -R), H + FVector2D(R, 0.f), Col, 2.f);
		P.Seg(H + FVector2D(R, 0.f), H + FVector2D(0.f, R), Col, 2.f);
		P.Seg(H + FVector2D(0.f, R), H + FVector2D(-R, 0.f), Col, 2.f);
		P.Seg(H + FVector2D(-R, 0.f), H + FVector2D(0.f, -R), Col, 2.f);
		P.Text(FString::Printf(TEXT("T%d"), i + 1), H.X + 11.f, H.Y - 9.f, 12.f, Col, 0, CampusUI::EFont::Cond, TEXT("Bold"));
	}
	// The objective, ringed and pulsing.
	const FVector2D Obj = ToScreen(-200.f, -600.f);
	const float Pulse = FMath::Fmod(Clock, 2.f) / 2.f;
	P.Ring(Obj, 16.f, Amber, 2.5f, 48);
	P.Ring(Obj, 16.f + 30.f * Pulse, CampusUI::Alpha(Amber, 1.f - Pulse), 1.5f, 48);
	P.Disc(Obj, 5.f, Amber, 16);
	P.Text(TEXT("OBJ 甲  行政大樓"), Obj.X + 22.f, Obj.Y - 9.f, 14.f, Amber, 0, CampusUI::EFont::Sans, TEXT("Bold"));

	// Compass, scale bar, legend.
	const FVector2D Rose(Area.Max.X - 46.f, Area.Min.Y + 52.f);
	P.Ring(Rose, 30.f, InkDim, 1.f, 48);
	P.Tri(Rose + FVector2D(0.f, -30.f), Rose + FVector2D(7.f, 0.f), Rose + FVector2D(-7.f, 0.f), Ink);
	P.Tri(Rose + FVector2D(0.f, 30.f), Rose + FVector2D(7.f, 0.f), Rose + FVector2D(-7.f, 0.f), InkDim);
	P.Text(TEXT("N"), Rose.X, Rose.Y - 56.f, 16.f, Ink, 1, CampusUI::EFont::Cond, TEXT("Bold"));
	const float Bar = 5000.f * Scale;
	const FVector2D BarAt(Area.Max.X - 30.f - Bar, Area.Max.Y - 24.f);
	P.Box(BarAt.X, BarAt.Y, Bar, 3.f, Ink);
	P.Box(BarAt.X, BarAt.Y - 6.f, 2.f, 9.f, Ink);
	P.Box(BarAt.X + Bar * 0.5f, BarAt.Y - 4.f, 2.f, 7.f, Ink);
	P.Box(BarAt.X + Bar - 2.f, BarAt.Y - 6.f, 2.f, 9.f, Ink);
	P.Text(TEXT("0"), BarAt.X, BarAt.Y - 24.f, 12.f, InkDim, 1, CampusUI::EFont::Num);
	P.Text(TEXT("50 M"), BarAt.X + Bar, BarAt.Y - 24.f, 12.f, InkDim, 1, CampusUI::EFont::Num);
	const float LX = Area.Max.X - 250.f;
	float LY = Area.Min.Y + 110.f;
	for (const TPair<FString, FLinearColor>& L : { TPair<FString, FLinearColor>(TEXT("友軍  FRIENDLY"), Blue), TPair<FString, FLinearColor>(TEXT("敵方  HOSTILE"), Red), TPair<FString, FLinearColor>(TEXT("路線  ROUTE"), Amber) })
	{
		P.Box(LX, LY + 6.f, 14.f, 4.f, L.Value);
		P.Text(L.Key, LX + 22.f, LY, 13.f, InkDim, 0, CampusUI::EFont::Sans, TEXT("SemiBold"));
		LY += 22.f;
	}
	P.Text(FString::Printf(TEXT("UTC+8  %s     GRID 25 M     SRC: UAV-2 / SAT"), *Hms(MissionSeconds())), 20.f, Size.Y - 26.f, 13.f, InkDim, 0, CampusUI::EFont::Num);
}

void ACampusHQScreens::DrawRadar(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("雷達監視"), TEXT("RADAR"), TEXT("ACTIVE"));
	const FLinearColor Phos(0.25f, 0.95f, 0.45f);
	const FVector2D Ce(Size.X * 0.5f, Top + (Size.Y - Top - 40.f) * 0.5f + 4.f);
	const float R = FMath::Min(Size.X * 0.44f, (Size.Y - Top - 40.f) * 0.47f);
	P.Disc(Ce, R, FLinearColor(0.01f, 0.03f, 0.018f), 64);
	for (int32 i = 1; i <= 4; ++i)
	{
		P.Ring(Ce, R * i / 4.f, CampusUI::Alpha(Phos, i == 4 ? 0.55f : 0.22f), i == 4 ? 1.5f : 1.f, 96);
		P.Text(FString::Printf(TEXT("%d"), i * 2), Ce.X + R * i / 4.f * 0.71f + 4.f, Ce.Y - R * i / 4.f * 0.71f - 2.f, 11.f, CampusUI::Alpha(Phos, 0.6f), 0, CampusUI::EFont::Num);
	}
	for (int32 i = 0; i < 12; ++i)
	{
		const float A = i * PI / 6.f;
		P.Seg(Ce, Ce + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, CampusUI::Alpha(Phos, i % 3 == 0 ? 0.22f : 0.08f), 1.f);
	}
	// Sweep: a bright leading edge and a fading phosphor trail behind it.
	const float Sweep = FMath::Fmod(Clock * 1.4f, 2.f * PI);
	for (int32 i = 0; i < 40; ++i)
	{
		const float A0 = Sweep - i * 0.025f;
		const float A1 = A0 - 0.025f;
		P.Tri(Ce, Ce + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R, Ce + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R, CampusUI::Alpha(Phos, 0.32f * FMath::Square(1.f - i / 40.f)));
	}
	P.Seg(Ce, Ce + FVector2D(FMath::Cos(Sweep), FMath::Sin(Sweep)) * R, Phos, 2.f);
	// Contacts light up as the sweep crosses them and fade until the next pass.
	FRandomStream Contacts(5);
	for (int32 i = 0; i < 9; ++i)
	{
		const float A = Contacts.FRandRange(0.f, 2.f * PI) + Clock * Contacts.FRandRange(-0.01f, 0.01f);
		const float D = Contacts.FRandRange(0.2f, 0.95f) * R;
		const float Since = FMath::Fmod(Sweep - A + 4.f * PI, 2.f * PI) / (2.f * PI);
		const float Glow = FMath::Square(1.f - Since);
		const FVector2D At = Ce + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D;
		P.Disc(At, 3.5f, CampusUI::Alpha(i == 2 ? Red : Phos, 0.25f + 0.75f * Glow), 10);
		if (Glow > 0.3f)
		{
			P.Text(FString::Printf(TEXT("%03d"), 100 + i * 37), At.X + 7.f, At.Y - 7.f, 11.f, CampusUI::Alpha(Phos, Glow), 0, CampusUI::EFont::Num);
		}
	}
	P.Text(TEXT("頻率 FREQ  2.8 GHz"), 16.f, Size.Y - 32.f, 13.f, InkDim);
	P.Text(FString::Printf(TEXT("方位 %03d°   增益 72"), FMath::RoundToInt(FMath::RadiansToDegrees(Sweep) + 90.f) % 360), Size.X - 16.f, Size.Y - 32.f, 13.f, InkDim, 2);
}

void ACampusHQScreens::DrawDrone(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("無人機"), TEXT("UAV-2  THERMAL"), FMath::Fmod(Clock, 1.f) < 0.6f ? TEXT("● LIVE") : TEXT("  LIVE"), Red);
	const FBox2D Feed(FVector2D(8.f, Top + 8.f), FVector2D(Size.X - 8.f, Size.Y - 34.f));
	if (CaptureTargets.Num() > 0 && CaptureTargets[0])
	{
		FCanvasTileItem T(Feed.Min, CaptureTargets[0]->GetResource(), Feed.GetSize(), FLinearColor(0.95f, 0.97f, 0.95f));
		Canvas->DrawItem(T);
	}
	const FVector2D C = Feed.GetCenter();
	const FLinearColor Hud(0.85f, 0.95f, 0.85f, 0.9f);
	P.Seg(C + FVector2D(-40.f, 0.f), C + FVector2D(-12.f, 0.f), Hud, 1.5f);
	P.Seg(C + FVector2D(12.f, 0.f), C + FVector2D(40.f, 0.f), Hud, 1.5f);
	P.Seg(C + FVector2D(0.f, -40.f), C + FVector2D(0.f, -12.f), Hud, 1.5f);
	P.Seg(C + FVector2D(0.f, 12.f), C + FVector2D(0.f, 40.f), Hud, 1.5f);
	// Track box on the target, corner brackets on the frame.
	const float B = 34.f;
	for (const FVector2D& S : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		const FVector2D K = C + S * B;
		P.Seg(K, K - FVector2D(S.X * 12.f, 0.f), Amber, 2.f);
		P.Seg(K, K - FVector2D(0.f, S.Y * 12.f), Amber, 2.f);
		const FVector2D F(S.X > 0.f ? Feed.Max.X - 10.f : Feed.Min.X + 10.f, S.Y > 0.f ? Feed.Max.Y - 10.f : Feed.Min.Y + 10.f);
		P.Seg(F, F - FVector2D(S.X * 26.f, 0.f), Hud, 2.f);
		P.Seg(F, F - FVector2D(0.f, S.Y * 26.f), Hud, 2.f);
	}
	const float Heading = FMath::Fmod(FMath::RadiansToDegrees(Clock * 0.05f) + 180.f, 360.f);
	P.Text(FString::Printf(TEXT("HDG %03d"), FMath::RoundToInt(Heading)), Feed.Min.X + 16.f, Feed.Min.Y + 12.f, 14.f, Hud, 0, CampusUI::EFont::Num, TEXT("Bold"));
	P.Text(TEXT("ALT 360 M AGL"), Feed.Min.X + 16.f, Feed.Min.Y + 32.f, 14.f, Hud, 0, CampusUI::EFont::Num, TEXT("Bold"));
	P.Text(TEXT("WHT  ZOOM 4.0X"), Feed.Max.X - 16.f, Feed.Min.Y + 12.f, 14.f, Hud, 2, CampusUI::EFont::Num, TEXT("Bold"));
	P.Text(TEXT("TGT  E21012 N27318"), Feed.Max.X - 16.f, Feed.Max.Y - 30.f, 14.f, Amber, 2, CampusUI::EFont::Num, TEXT("Bold"));
	P.Text(FString::Printf(TEXT("%s  UTC+8"), *Hms(MissionSeconds())), 14.f, Size.Y - 28.f, 13.f, InkDim, 0, CampusUI::EFont::Num);
	P.Text(TEXT("LINK 98%"), Size.X - 14.f, Size.Y - 28.f, 13.f, Green, 2, CampusUI::EFont::Num, TEXT("Bold"));
}

void ACampusHQScreens::DrawCameras(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("監視器"), TEXT("CCTV"), TEXT("4 / 4 ONLINE"));
	const float Gap = 8.f;
	const FVector2D Cell((Size.X - Gap * 3.f) * 0.5f, (Size.Y - Top - Gap * 3.f) * 0.5f);
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector2D At(Gap + (i % 2) * (Cell.X + Gap), Top + Gap + (i / 2) * (Cell.Y + Gap));
		P.Box(At.X, At.Y, Cell.X, Cell.Y, FLinearColor(0.01f, 0.012f, 0.012f));
		const int32 Cam = i + 1;
		if (CaptureTargets.IsValidIndex(Cam) && CaptureTargets[Cam])
		{
			// The feed keeps its own 16:9 picture, centred in the cell.
			const float W = FMath::Min(Cell.X, Cell.Y * 16.f / 9.f);
			const float H = W * 9.f / 16.f;
			FCanvasTileItem T(At + (Cell - FVector2D(W, H)) * 0.5f, CaptureTargets[Cam]->GetResource(), FVector2D(W, H), FLinearColor(0.82f, 0.9f, 0.86f));
			Canvas->DrawItem(T);
		}
		P.Frame(At.X, At.Y, Cell.X, Cell.Y, LineDim, 1.f);
		const FString Name = CameraSpots.IsValidIndex(Cam) ? CameraSpots[Cam].Name : FString::Printf(TEXT("CAM %02d"), Cam);
		P.Box(At.X, At.Y, P.Width(Name, 15.f) + 20.f, 26.f, FLinearColor(0.f, 0.f, 0.f, 0.55f));
		P.Text(Name, At.X + 10.f, At.Y + 4.f, 15.f, Ink, 0, CampusUI::EFont::Sans, TEXT("Bold"));
		if (FMath::Fmod(Clock + i * 0.17f, 1.f) < 0.6f)
		{
			P.Disc(FVector2D(At.X + Cell.X - 50.f, At.Y + 14.f), 5.f, Red, 12);
		}
		P.Text(TEXT("REC"), At.X + Cell.X - 12.f, At.Y + 5.f, 14.f, Ink, 2, CampusUI::EFont::Cond, TEXT("Bold"));
		P.Text(FString::Printf(TEXT("2026-10-15  %s"), *Hms(MissionSeconds())), At.X + Cell.X - 12.f, At.Y + Cell.Y - 26.f, 14.f, Ink, 2, CampusUI::EFont::Num, TEXT("Bold"));
	}
}

void ACampusHQScreens::DrawTelemetry(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("系統狀態"), TEXT("SYSTEM TELEMETRY"), TEXT("NOMINAL"));
	const float Pad = 14.f;
	const FBox2D G(FVector2D(Pad, Top + 34.f), FVector2D(Size.X * 0.58f, Size.Y - 18.f));
	P.Text(TEXT("數據鏈流量  DATALINK  Mb/s"), G.Min.X, Top + 10.f, 13.f, InkDim);
	for (int32 i = 0; i <= 3; ++i)
	{
		P.Box(G.Min.X, G.Min.Y + G.GetSize().Y * i / 3.f, G.GetSize().X, 1.f, LineDim);
	}
	auto Plot = [&](const TArray<float>& H, const FLinearColor& Col)
	{
		for (int32 i = 1; i < H.Num(); ++i)
		{
			const FVector2D A(G.Min.X + G.GetSize().X * (i - 1) / 47.f, G.Max.Y - G.GetSize().Y * H[i - 1]);
			const FVector2D B(G.Min.X + G.GetSize().X * i / 47.f, G.Max.Y - G.GetSize().Y * H[i]);
			P.Seg(A, B, Col, 2.f);
		}
	};
	Plot(NetIn, Line);
	Plot(NetOut, Amber);
	P.Text(FString::Printf(TEXT("IN %.0f"), NetIn.Last() * 120.f), G.Max.X - 4.f, Top + 10.f, 13.f, Line, 2, CampusUI::EFont::Num, TEXT("Bold"));
	// Bars: link quality per relay.
	const float X0 = Size.X * 0.64f;
	P.Text(TEXT("中繼站  RELAYS"), X0, Top + 10.f, 13.f, InkDim);
	const float BarW = (Size.X - X0 - Pad) / 6.f;
	for (int32 i = 0; i < 6; ++i)
	{
		const float Q = FMath::Clamp(LinkQuality.Last() - 0.08f * ((i * 5) % 4) + 0.03f * FMath::Sin(Clock * 0.7f + i), 0.f, 1.f);
		const float H = (Size.Y - Top - 70.f) * Q;
		P.Box(X0 + i * BarW + 3.f, Size.Y - 34.f - H, BarW - 8.f, H, Q > 0.75f ? Line : Amber);
		P.Text(FString::Printf(TEXT("R%d"), i + 1), X0 + i * BarW + BarW * 0.5f - 2.f, Size.Y - 30.f, 12.f, InkDim, 1, CampusUI::EFont::Num);
	}
}

void ACampusHQScreens::DrawComms(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("通訊日誌"), TEXT("COMMS LOG"), TEXT("CH 3   ENCRYPTED"));
	const float LineH = 24.f;
	const int32 Rows = FMath::Max(1, FMath::FloorToInt((Size.Y - Top - 12.f) / LineH));
	const int32 First = FMath::Max(0, CommsShown.Num() - Rows);
	for (int32 i = First; i < CommsShown.Num(); ++i)
	{
		const float Y = Top + 8.f + (i - First) * LineH;
		const FCommsLine& L = CommsLines[CommsShown[i] % UE_ARRAY_COUNT(CommsLines)];
		const bool bNewest = i == CommsShown.Num() - 1;
		P.Text(Hms(CommsTime[i]), 16.f, Y, 15.f, InkDim, 0, CampusUI::EFont::Num, TEXT("SemiBold"));
		P.Text(FString::Printf(TEXT("[%s]"), L.Who), 110.f, Y, 15.f, CallsignColor(L.Who), 0, CampusUI::EFont::Cond, TEXT("Bold"));
		P.Text(L.Text, 230.f, Y, 15.f, bNewest ? Ink : FLinearColor(0.62f, 0.68f, 0.72f));
	}
	if (FMath::Fmod(Clock, 1.f) < 0.5f)
	{
		P.Box(Size.X - 30.f, Size.Y - 22.f, 12.f, 3.f, Ink);
	}
}

void ACampusHQScreens::DrawClock(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("任務計時"), TEXT("MISSION CLOCK"));
	const double ToH = FMath::Max(0.0, 15.0 * 60.0 - FMath::Fmod((double)Clock, 15.0 * 60.0));
	P.Text(TEXT("H-HOUR"), 20.f, Top + 12.f, 15.f, InkDim, 0, CampusUI::EFont::Cond, TEXT("Bold"));
	P.Text(FString::Printf(TEXT("-%s"), *Hms(ToH)), 18.f, Top + 34.f, 74.f, ToH < 120.0 ? Amber : Ink, 0, CampusUI::EFont::Num, TEXT("SemiBold"));
	const float RX = Size.X * 0.66f;
	P.Box(RX - 18.f, Top + 14.f, 1.f, Size.Y - Top - 28.f, LineDim);
	P.Text(TEXT("本地時間  LOCAL"), RX, Top + 14.f, 13.f, InkDim);
	P.Text(Hms(MissionSeconds()), RX, Top + 34.f, 30.f, Ink, 0, CampusUI::EFont::Num, TEXT("SemiBold"));
	P.Text(TEXT("日期  DATE"), RX, Top + 86.f, 13.f, InkDim);
	P.Text(TEXT("2026-10-15"), RX, Top + 106.f, 26.f, Ink, 0, CampusUI::EFont::Num, TEXT("SemiBold"));
	P.Text(TEXT("ZULU  15:47"), RX, Top + 150.f, 15.f, InkDim, 0, CampusUI::EFont::Num);
}

void ACampusHQScreens::DrawReadiness(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("戰備狀態"), TEXT("READINESS"));
	struct FRow { const TCHAR* Zh; const TCHAR* En; int32 State; };
	// Intelligence flips to ready a while after the start, logistics stays late.
	const FRow Rows[] = {
		{ TEXT("人員"), TEXT("PERSONNEL"), 0 }, { TEXT("裝備"), TEXT("EQUIPMENT"), 0 }, { TEXT("通訊"), TEXT("COMMS"), 0 },
		{ TEXT("情報"), TEXT("INTEL"), FMath::Fmod(Clock, 120.f) < 70.f ? 1 : 0 }, { TEXT("後勤"), TEXT("LOGISTICS"), 2 } };
	const float RowH = (Size.Y - Top - 12.f) / UE_ARRAY_COUNT(Rows);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
	{
		const FRow& R = Rows[i];
		const float Y = Top + 6.f + i * RowH;
		const FLinearColor Col = R.State == 0 ? Green : R.State == 1 ? Amber : Red;
		const TCHAR* Word = R.State == 0 ? TEXT("就緒  READY") : R.State == 1 ? TEXT("注意  CAUTION") : TEXT("延遲  DELAY");
		P.Box(14.f, Y + RowH - 2.f, Size.X - 28.f, 1.f, LineDim);
		P.Text(R.Zh, 18.f, Y + RowH * 0.5f - 11.f, 18.f, Ink, 0, CampusUI::EFont::Sans, TEXT("Bold"));
		P.Text(R.En, 70.f, Y + RowH * 0.5f - 8.f, 13.f, InkDim, 0, CampusUI::EFont::Cond);
		P.Disc(FVector2D(Size.X * 0.56f, Y + RowH * 0.5f), 6.f, Col, 14);
		P.Text(Word, Size.X - 18.f, Y + RowH * 0.5f - 10.f, 17.f, Col, 2, CampusUI::EFont::Sans, TEXT("Bold"));
	}
}

void ACampusHQScreens::DrawWeather(UCanvas* Canvas, const FVector2D& Size)
{
	const FPaint P{ Canvas };
	const float Top = P.Header(Size, TEXT("天氣資訊"), TEXT("WEATHER"), TEXT("CWA 23:30"), InkDim);
	// Cloud: a few overlapping discs.
	const FVector2D Cl(70.f, Top + 70.f);
	for (const FVector3f& D : { FVector3f(-20.f, 8.f, 20.f), FVector3f(4.f, -6.f, 26.f), FVector3f(28.f, 8.f, 18.f), FVector3f(4.f, 14.f, 20.f) })
	{
		P.Disc(Cl + FVector2D(D.X, D.Y), D.Z, FLinearColor(0.45f, 0.52f, 0.58f), 24);
	}
	P.Text(TEXT("22°C"), 130.f, Top + 26.f, 52.f, Ink, 0, CampusUI::EFont::Num, TEXT("SemiBold"));
	P.Text(TEXT("多雲  CLOUDY"), 132.f, Top + 98.f, 16.f, InkDim);
	struct FRow { const TCHAR* Zh; const TCHAR* Value; };
	const FRow Rows[] = { { TEXT("風向 WIND"), TEXT("315° NW") }, { TEXT("風速 SPEED"), TEXT("12 kt") }, { TEXT("能見度 VIS"), TEXT("10 km") }, { TEXT("濕度 HUM"), TEXT("78 %") }, { TEXT("氣壓 PRES"), TEXT("1012 hPa") }, { TEXT("降雨 PRECIP"), TEXT("10 %") } };
	const float X0 = Size.X * 0.52f;
	const float RowH = (Size.Y - Top - 12.f) / UE_ARRAY_COUNT(Rows);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
	{
		const float Y = Top + 6.f + i * RowH;
		P.Text(Rows[i].Zh, X0, Y + RowH * 0.5f - 9.f, 14.f, InkDim);
		P.Text(Rows[i].Value, Size.X - 18.f, Y + RowH * 0.5f - 10.f, 16.f, Ink, 2, CampusUI::EFont::Num, TEXT("SemiBold"));
	}
}
