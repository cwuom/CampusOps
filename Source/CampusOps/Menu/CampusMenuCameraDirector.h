#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusSpring.h"
#include "CampusMenuCameraDirector.generated.h"

class UCameraComponent;

USTRUCT(BlueprintType)
struct FCampusCameraShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") float FOV = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") float FocusDistance = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") float Aperture = 2.8f;
	/** Scales the handheld drift (0 = locked off tripod). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") float Handheld = 1.f;
	/** Auto-exposure bias (EV) for this framing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") float Exposure = 0.f;
	/** Point the shot looks at; orbit moves interpolate around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot") FVector Target = FVector::ZeroVector;

	static FCampusCameraShot LookAt(const FVector& From, const FVector& InTarget, float InFOV, float InAperture = 2.8f, float InHandheld = 1.f, float InExposure = 0.f)
	{
		FCampusCameraShot Shot;
		Shot.Location = From;
		Shot.Target = InTarget;
		Shot.Rotation = (InTarget - From).Rotation();
		Shot.FOV = InFOV;
		Shot.FocusDistance = FVector::Dist(From, InTarget);
		Shot.Aperture = InAperture;
		Shot.Handheld = InHandheld;
		Shot.Exposure = InExposure;
		return Shot;
	}
};

/**
 * Menu camera with "camera operator" behaviour: moves along a curved dolly/crane path with eased
 * timing, a spring follow layer for a soft settle, handheld Perlin drift, breathing, mouse parallax
 * and animated depth of field.
 */
UCLASS()
class CAMPUSOPS_API ACampusMenuCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	ACampusMenuCameraDirector();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus") TObjectPtr<UCameraComponent> Camera;

	void SnapTo(const FCampusCameraShot& Shot);
	void CutTo(const FCampusCameraShot& Shot);
	void FollowFraming(const FCampusCameraShot& Shot, float DeltaSeconds);
	void MoveTo(const FCampusCameraShot& Shot, float Duration, float ArcHeight = 60.f);
	/** Close-quarters move around the look-at target (focus, direction and distance interpolate), so the subject stays framed. */
	void OrbitTo(const FCampusCameraShot& Shot, float Duration);
	const FVector& GetCurrentLocation() const { return Current.Location; }
	bool IsMoving() const { return bMoving || IsCutting(); }
	bool IsCutting() const { return bCutPending || CutOpacity > 0.f; }
	float GetCutOpacity() const { return CutOpacity; }
	float GetMoveAlpha() const { return MoveDuration > 0.f ? FMath::Clamp(MoveTime / MoveDuration, 0.f, 1.f) : 1.f; }
	void SetParallaxInput(const FVector2D& Normalized) { ParallaxTarget = Normalized; }
	/**
	 * Weapon close-up (gunsmith, loadouts): FStop is the least f-stop that holds the whole weapon sharp as it is
	 * turned right now (0 when not on a weapon). The lens is then locked off, without colour fringing or much grain:
	 * any movement of a silhouette against the blurred backdrop crawls through the temporal upscaler.
	 */
	void SetWeaponCloseup(bool bOn, float FStop) { bCloseupTarget = bOn; CloseupFStop = FStop; }

private:
	bool bCloseupTarget = false;
	float CloseupFStop = 0.f;
	float CloseupAlpha = 0.f;
	float SmoothedFStopFloor = 0.f;
	FCampusCameraShot From;
	FCampusCameraShot To;
	FCampusCameraShot Current;
	FVector P1 = FVector::ZeroVector;
	FVector P2 = FVector::ZeroVector;
	float MoveTime = 0.f;
	float MoveDuration = 0.f;
	bool bMoving = false;
	bool bInitialized = false;
	FCampusCameraShot PendingCut;
	bool bCutPending = false;
	float CutOpacity = 0.f;
	/** Orbit framing: the follow spring is bypassed so position and aim stay locked together. */
	bool bOrbit = false;

	FCampusSpringVector LocationFollow;
	FQuat SmoothedRotation = FQuat::Identity;
	FVector2D Parallax = FVector2D::ZeroVector;
	FVector2D ParallaxTarget = FVector2D::ZeroVector;
	float Time = 0.f;
};
