#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusHQScreens.generated.h"

class UCanvas;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/** What a display of the 特勤处 video wall (or a console, or the briefing table) shows. */
enum class ECampusHQDisplay : uint8
{
	Map,
	Radar,
	Drone,
	Cameras,
	Telemetry,
	Comms,
	Clock,
	Readiness,
	Weather,
	Count
};

/**
 * Live content of the operations centre's screens: a tactical map of the school with the assault force moving in, a
 * radar sweep, a drone's thermal feed and the hangar's security cameras (low-rate scene captures), telemetry graphs,
 * the radio log, the mission clock, readiness and weather boards. Each display is drawn with the canvas into its own
 * render target at its own rate; several physical panels can share one (a picture spanning the video wall).
 */
UCLASS()
class CAMPUSOPS_API ACampusHQScreens : public AActor
{
	GENERATED_BODY()

public:
	ACampusHQScreens();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The render target a display shows (created on first use). Aspect is width / height of the whole picture. */
	UTextureRenderTarget2D* Target(ECampusHQDisplay Display);
	static float Aspect(ECampusHQDisplay Display);
	/** Writes every picture and live feed to Saved/AutoShots/hqs_*.png (scripted captures). */
	void DumpToDisk();

	/** Where the drone and the security cameras look from (set by the builder before BeginPlay). */
	struct FCamera
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float FOV = 70.f;
		FString Name;
	};
	TArray<FCamera> CameraSpots;
	FVector DroneTarget = FVector::ZeroVector;

private:
	void Redraw(ECampusHQDisplay Display);
	void DrawMap(UCanvas* Canvas, const FVector2D& Size);
	void DrawRadar(UCanvas* Canvas, const FVector2D& Size);
	void DrawDrone(UCanvas* Canvas, const FVector2D& Size);
	void DrawCameras(UCanvas* Canvas, const FVector2D& Size);
	void DrawTelemetry(UCanvas* Canvas, const FVector2D& Size);
	void DrawComms(UCanvas* Canvas, const FVector2D& Size);
	void DrawClock(UCanvas* Canvas, const FVector2D& Size);
	void DrawReadiness(UCanvas* Canvas, const FVector2D& Size);
	void DrawWeather(UCanvas* Canvas, const FVector2D& Size);
	void UpdateCaptures(float DeltaSeconds);
	/** Mission time of day in seconds (the HQ is at night; the clock runs from 23:40). */
	double MissionSeconds() const;

	UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> CaptureTargets;
	TArray<float> NextDraw;
	float Clock = 0.f;
	float CaptureTimer = 0.f;
	int32 NextCapture = 0;
	/** Telemetry history (0..1), newest last. */
	TArray<float> NetIn, NetOut, LinkQuality;
	float TelemetryTimer = 0.f;
	/** Radio log: line indices already shown, newest last, and when the next one comes in. */
	TArray<int32> CommsShown;
	TArray<double> CommsTime;
	float NextComms = 0.f;
	FRandomStream Rng;
};
