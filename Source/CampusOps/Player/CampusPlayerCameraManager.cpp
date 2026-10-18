#include "Player/CampusPlayerCameraManager.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusPlayerController.h"
#include "Player/CampusViewmodelComponent.h"
#include "CampusSpring.h"
#include "CampusAssets.h"
#include "Game/CampusGraphics.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Weapons/CampusWeapon.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"

ACampusPlayerCameraManager::ACampusPlayerCameraManager()
{
	ViewPitchMin = -88.f;
	ViewPitchMax = 88.f;
	DefaultFOV = 90.f;
}

void ACampusPlayerCameraManager::UpdateSightFocus(const ACampusCharacter* Char, FMinimalViewInfo& POV)
{
	// Aiming down iron sights the eye focuses on the front post: the rear sight and the receiver right under the eye
	// go soft while the target stays sharp (background blur clamped off). Optics keep everything sharp: their reticle
	// glass and eyepiece sit as close as the rear sight does.
	const ACampusWeapon* W = Char->IsLocalHuman() ? Char->GetCurrentWeapon() : nullptr;
	const FCampusWeaponDef* Def = W ? W->GetDef() : nullptr;
	const float Irons = (Def && !W->HasOptic() && Char->FirstPersonCamera) ? CampusMath::SmootherStep01(FMath::Clamp((Char->GetADSAlpha() - 0.35f) / 0.65f, 0.f, 1.f)) : 0.f;
	static IConsoleVariable* MaxBackground = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxBackgroundRadius"));
	static IConsoleVariable* MaxForeground = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxForegroundRadius"));
	if (Irons <= 0.01f)
	{
		if (bSightFocusActive && MaxBackground && MaxForeground)
		{
			MaxBackground->Set(SavedMaxBackgroundRadius, ECVF_SetByCode);
			MaxForeground->Set(SavedMaxForegroundRadius, ECVF_SetByCode);
		}
		bSightFocusActive = false;
		return;
	}
	if (!bSightFocusActive && MaxBackground && MaxForeground)
	{
		SavedMaxBackgroundRadius = MaxBackground->GetFloat();
		SavedMaxForegroundRadius = MaxForeground->GetFloat();
		// Soft enough that the rear ring still reads as a ring around the post.
		MaxBackground->Set(0.0005f, ECVF_SetByCode);
		MaxForeground->Set(0.005f, ECVF_SetByCode);
	}
	bSightFocusActive = true;
	// First-person primitives are drawn at a fraction of their depth, which is what the depth of field sees.
	const float FrontX = Def->FrontSightX > 0.f ? Def->FrontSightX : Def->Muzzle.X - 3.f;
	const float EyeToFront = FMath::Max(FrontX - Def->IronSight.X, 10.f) + FMath::Max(Def->IronEyeRelief, 8.f);
	POV.PostProcessBlendWeight = 1.f;
	POV.PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
	POV.PostProcessSettings.DepthOfFieldFocalDistance = EyeToFront * Char->FirstPersonCamera->FirstPersonScale;
	POV.PostProcessSettings.bOverride_DepthOfFieldFstop = true;
	POV.PostProcessSettings.DepthOfFieldFstop = FMath::Lerp(22.f, 4.5f, Irons);
	POV.PostProcessSettings.bOverride_DepthOfFieldSensorWidth = true;
	POV.PostProcessSettings.DepthOfFieldSensorWidth = 36.f;
}

void ACampusPlayerCameraManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bSightFocusActive)
	{
		if (IConsoleVariable* MaxBackground = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxBackgroundRadius")))
		{
			MaxBackground->Set(SavedMaxBackgroundRadius, ECVF_SetByCode);
		}
		if (IConsoleVariable* MaxForeground = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DOF.Kernel.MaxForegroundRadius")))
		{
			MaxForeground->Set(SavedMaxForegroundRadius, ECVF_SetByCode);
		}
		bSightFocusActive = false;
	}
	Super::EndPlay(EndPlayReason);
}

void ACampusPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	Super::UpdateViewTarget(OutVT, DeltaTime);

	// Brightness option: an exposure offset on top of the map's own bias (camera settings override the volume's).
	if (const float Brightness = CampusGraphics::BrightnessBias(); !FMath::IsNearlyZero(Brightness))
	{
		float MapBias = 0.f;
		for (IInterface_PostProcessVolume& Volume : GetWorld()->GetPostProcessVolumeIterator())
		{
			const FPostProcessVolumeProperties Props = Volume.GetProperties();
			if (Props.bIsUnbound && Props.bIsEnabled && Props.Settings && Props.Settings->bOverride_AutoExposureBias)
			{
				MapBias = Props.Settings->AutoExposureBias;
				break;
			}
		}
		OutVT.POV.PostProcessBlendWeight = 1.f;
		OutVT.POV.PostProcessSettings.bOverride_AutoExposureBias = true;
		OutVT.POV.PostProcessSettings.AutoExposureBias = MapBias + Brightness;
	}

	ACampusCharacter* Char = Cast<ACampusCharacter>(OutVT.Target);
	const ACampusPlayerController* PC = Cast<ACampusPlayerController>(GetOwningPlayerController());
	if (!Char)
	{
		bDeathCamInit = false;
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();

	if (Char->bIsDead)
	{
		const FVector Body = Char->GetMesh()->GetBoneLocation(TEXT("pelvis"));
		const APawn* KillerPawn = Char->KillerState ? Char->KillerState->GetPawn() : nullptr;
		const FMinimalViewInfo From = bHasAlivePOV ? LastAlivePOV : OutVT.POV;
		if (!bDeathCamInit)
		{
			bDeathCamInit = true;
			DeathStartTime = Now;
			FVector Dir = KillerPawn ? (KillerPawn->GetActorLocation() - Body).GetSafeNormal2D() : From.Rotation.Vector().GetSafeNormal2D();
			if (Dir.IsNearlyZero())
			{
				Dir = FVector::ForwardVector;
			}
			const FVector Desired = Body - Dir * 300.f + FVector(0.f, 0.f, 180.f);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusDeathCam), false, Char);
			const FVector TraceStart = Body + FVector(0.f, 0.f, 60.f);
			DeathCamLocation = GetWorld()->SweepSingleByChannel(Hit, TraceStart, Desired, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(20.f), Params)
				? Hit.Location
				: Desired;
		}
		const FVector LookAt = KillerPawn ? FMath::Lerp(Body, KillerPawn->GetPawnViewLocation(), 0.65f) : Body;
		const FRotator TargetRot = (LookAt - DeathCamLocation).Rotation();
		const float A = CampusMath::SmootherStep01((Now - DeathStartTime) / 0.8f);
		OutVT.POV.Location = FMath::Lerp(From.Location, DeathCamLocation, A);
		OutVT.POV.Rotation = FQuat::Slerp(From.Rotation.Quaternion(), TargetRot.Quaternion(), A).Rotator();
		OutVT.POV.FOV = FMath::Lerp(From.FOV, 75.f, A);
		OutVT.POV.PostProcessBlendWeight = 1.f;
		OutVT.POV.PostProcessSettings.bOverride_ColorSaturation = true;
		OutVT.POV.PostProcessSettings.ColorSaturation = FVector4(1.f - 0.75f * A, 1.f - 0.75f * A, 1.f - 0.75f * A, 1.f);
		OutVT.POV.PostProcessSettings.bOverride_VignetteIntensity = true;
		OutVT.POV.PostProcessSettings.VignetteIntensity = 0.4f + 0.6f * A;
		return;
	}

	bDeathCamInit = false;
	if (Char->Viewmodel && Char->IsLocalHuman())
	{
		OutVT.POV.Rotation = (OutVT.POV.Rotation.Quaternion() * Char->Viewmodel->CameraRotationOffset.Quaternion()).Rotator();
		OutVT.POV.Location += OutVT.POV.Rotation.RotateVector(Char->Viewmodel->CameraLocationOffset);
	}

	const float Health01 = Char->GetHealth01();
	if (Health01 < 0.6f)
	{
		const float Hurt = 1.f - Health01 / 0.6f;
		OutVT.POV.PostProcessBlendWeight = 1.f;
		OutVT.POV.PostProcessSettings.bOverride_ColorSaturation = true;
		OutVT.POV.PostProcessSettings.ColorSaturation = FVector4(1.f - 0.45f * Hurt, 1.f - 0.45f * Hurt, 1.f - 0.45f * Hurt, 1.f);
		OutVT.POV.PostProcessSettings.bOverride_VignetteIntensity = true;
		OutVT.POV.PostProcessSettings.VignetteIntensity = 0.4f + 0.5f * Hurt;
	}

	UpdateSightFocus(Char, OutVT.POV);

	LastAlivePOV = OutVT.POV;
	bHasAlivePOV = true;

	if (PC && PC->IsIntroActive())
	{
		UpdateIntroDrop(PC, OutVT.POV);
	}
}

void ACampusPlayerCameraManager::UpdateIntroDrop(const ACampusPlayerController* PC, FMinimalViewInfo& POV)
{
	const FVector Eye = POV.Location;
	auto Curve = [](const FVector& S, const FVector& C, const FVector& E, float U)
	{
		return S * FMath::Square(1.f - U) + C * (2.f * U * (1.f - U)) + E * (U * U);
	};
	if (!FMath::IsNearlyEqual(IntroCachedStart, PC->IntroStartTime))
	{
		// A curve from high behind the spawn that comes down onto the eyes from straight above: it clears whatever
		// stands round the spawn (sign frames, rails, plant) by coming in over it. Each candidate is swept along its
		// whole length; under a roof (or if nothing is clear) it is a short drop down a clear column.
		IntroCachedStart = PC->IntroStartTime;
		bIntroWindPlayed = false;
		IntroEye = Eye;
		IntroFwd = POV.Rotation.Vector().GetSafeNormal2D();
		if (IntroFwd.IsNearlyZero())
		{
			IntroFwd = FVector::ForwardVector;
		}
		const FVector Up = FVector::UpVector;
		const FVector Right = FVector::CrossProduct(Up, IntroFwd);
		const FVector Offsets[] = {
			-IntroFwd * 1000.f + Up * 2200.f,
			-IntroFwd * 500.f + Up * 2400.f,
			-IntroFwd * 600.f + Right * 800.f + Up * 2200.f,
			-IntroFwd * 600.f - Right * 800.f + Up * 2200.f,
			Up * 2400.f };
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusIntroDrop), false, PC->GetPawn());
		const FCollisionShape Ball = FCollisionShape::MakeSphere(60.f);
		auto Clear = [&](const FVector& S, const FVector& C)
		{
			FVector Prev = S;
			for (int32 i = 1; i <= 16; ++i)
			{
				const FVector P = Curve(S, C, Eye, i / 16.f);
				if (GetWorld()->SweepTestByChannel(Prev, i == 16 ? P + FVector(0.f, 0.f, 30.f) : P, FQuat::Identity, ECC_Camera, Ball, Params))
				{
					return false;
				}
				Prev = P;
			}
			return true;
		};
		bIntroUnderRoof = true;
		for (const FVector& O : Offsets)
		{
			const FVector S = Eye + O;
			const FVector C = Eye + Up * (O.Z * 0.42f);
			if (Clear(S, C))
			{
				IntroSkyLocation = S;
				IntroControl = C;
				bIntroUnderRoof = false;
				break;
			}
		}
		if (bIntroUnderRoof)
		{
			FHitResult Hit;
			const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Eye + FVector(0.f, 0.f, 20.f), Eye + Up * 1200.f, FQuat::Identity, ECC_Camera, Ball, Params);
			IntroSkyLocation = Eye + Up * (bHit ? FMath::Max(40.f, Hit.Distance - 80.f) : 1200.f);
			IntroControl = (IntroSkyLocation + Eye) * 0.5f;
		}
	}
	const float A = PC->GetIntroAlpha();
	const float T = GetWorld()->GetTimeSeconds();
	// Eases out of the top and decelerates onto the feet, without hanging anywhere along the way.
	const float U = CampusMath::SmootherStep01(FMath::Clamp(A / 0.93f, 0.f, 1.f));
	FVector Loc = Curve(IntroSkyLocation, IntroControl, Eye, U);
	const float Height = (IntroSkyLocation - Eye).Size();
	// Buffeting while it is fast and high.
	const float Speed = FMath::Sin(PI * FMath::Clamp(A / 0.93f, 0.f, 1.f)) * (1.f - U);
	Loc += FVector(FMath::Sin(T * 7.3f) * 8.f, FMath::Cos(T * 6.1f) * 8.f, FMath::Sin(T * 9.7f) * 4.f) * Speed * FMath::Clamp(Height / 2000.f, 0.f, 1.f);

	// Looking ahead of the spawn and down onto it, never steeper than a dive would let the eyes go: the deck and the
	// skyline are in frame, the street far below never is. The view settles onto the player's own on the way down.
	const FVector Ahead = Eye + IntroFwd * 900.f - FVector(0.f, 0.f, 80.f);
	FRotator Look = (Ahead - Loc).Rotation();
	Look.Pitch = FMath::Clamp(Look.Pitch, bIntroUnderRoof ? -70.f : -50.f, -6.f);
	Look.Roll = FMath::Sin(T * 1.7f) * 4.f * Speed;
	const float RotA = CampusMath::SmootherStep01(FMath::Clamp((A - 0.5f) / 0.42f, 0.f, 1.f));
	POV.Rotation = FQuat::Slerp(Look.Quaternion(), POV.Rotation.Quaternion(), RotA).Rotator();
	POV.Location = Loc;
	POV.FOV = FMath::Lerp(POV.FOV + 14.f, POV.FOV, CampusMath::SmootherStep01(A));
	// The touchdown dip is the viewmodel's (it moves the camera and the arms together; moving only the view here would
	// shift it off the first-person arms and show them from below).
	if (!bIntroWindPlayed && Height > 400.f)
	{
		bIntroWindPlayed = true;
		CampusAudio::Play2D(this, TEXT("Drop_Wind"), 0.9f, 0.f);
	}
}
