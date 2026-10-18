#include "Menu/CampusMenuCameraDirector.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FVector Bezier(const FVector& A, const FVector& B, const FVector& C, const FVector& D, float T)
	{
		const float U = 1.f - T;
		return A * (U * U * U) + B * (3.f * U * U * T) + C * (3.f * U * T * T) + D * (T * T * T);
	}

	/** Lens clearance kept from every visible mesh along a flight. */
	constexpr float FlightClearance = 22.f;
	constexpr int32 FlightSamples = 32;

	/**
	 * Boxes of everything visible inside Region. Most set dressing has no collision, so the flight is planned against
	 * render bounds (per instance for instanced meshes) rather than physics. Boxes holding either end of the move are
	 * left out: the camera starts and finishes there, so they are rooms or sky, not obstacles.
	 */
	void GatherObstacles(const UWorld* World, const FBox& Region, const FVector& A, const FVector& B, TArray<FBox>& Out)
	{
		auto Consider = [&](const FBox& Box, float Margin = FlightClearance)
		{
			const FBox Fat = Box.ExpandBy(Margin);
			if (Fat.Intersect(Region) && !Fat.IsInsideOrOn(A) && !Fat.IsInsideOrOn(B))
			{
				Out.Add(Fat);
			}
		};
		for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (It->IsHidden() || It->IsA<ACampusMenuCameraDirector>())
			{
				continue;
			}
			for (UActorComponent* C : It->GetComponents())
			{
				const UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C);
				if (!P || !P->IsRegistered() || !P->IsVisible() || P->bHiddenInGame || !P->Bounds.GetBox().Intersect(Region))
				{
					continue;
				}
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(P))
				{
					const UStaticMesh* Mesh = ISM->GetStaticMesh();
					if (!Mesh)
					{
						continue;
					}
					const FBox MeshBox = Mesh->GetBounds().GetBox();
					for (int32 i = 0, N = ISM->GetInstanceCount(); i < N; ++i)
					{
						FTransform T;
						if (ISM->GetInstanceTransform(i, T, true))
						{
							Consider(MeshBox.TransformBy(T));
						}
					}
				}
				else if (P->IsA<USkeletalMeshComponent>() && P->Bounds.BoxExtent.Z > 60.f)
				{
					// A person: the lens keeps a good arm's length off, not a prop's few centimetres.
					Consider(P->Bounds.GetBox(), FlightClearance + 45.f);
				}
				else if (P->IsA<UStaticMeshComponent>() || P->IsA<USkeletalMeshComponent>())
				{
					Consider(P->Bounds.GetBox());
				}
			}
		}
	}

	/** How badly a curve runs through the obstacles: the number of its sampled segments that cross any box. */
	int32 CountBlocked(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const TArray<FBox>& Obstacles, float& OutLength)
	{
		int32 Blocked = 0;
		OutLength = 0.f;
		FVector Prev = A;
		for (int32 s = 1; s <= FlightSamples; ++s)
		{
			const FVector P = Bezier(A, B, C, D, s / (float)FlightSamples);
			const FVector Seg = P - Prev;
			OutLength += Seg.Size();
			if (!Seg.IsNearlyZero())
			{
				const FVector Inv(1.f / (FMath::Abs(Seg.X) > UE_SMALL_NUMBER ? Seg.X : UE_SMALL_NUMBER), 1.f / (FMath::Abs(Seg.Y) > UE_SMALL_NUMBER ? Seg.Y : UE_SMALL_NUMBER), 1.f / (FMath::Abs(Seg.Z) > UE_SMALL_NUMBER ? Seg.Z : UE_SMALL_NUMBER));
				for (const FBox& Box : Obstacles)
				{
					if (FMath::LineBoxIntersection(Box, Prev, P, Seg, Inv))
					{
						++Blocked;
						break;
					}
				}
			}
			Prev = P;
		}
		return Blocked;
	}
}

ACampusMenuCameraDirector::ACampusMenuCameraDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->bConstrainAspectRatio = false;
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);
	Camera->PostProcessBlendWeight = 1.f;
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_DepthOfFieldFstop = true;
	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth = 36.f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.55f;
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = 0.12f;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.8f;
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = 0.6f;
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(0.92f, 0.92f, 0.92f, 1.f);
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.f);
	PP.bOverride_WhiteTemp = true;
	PP.WhiteTemp = 6100.f;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 0.f;
}

void ACampusMenuCameraDirector::SnapTo(const FCampusCameraShot& Shot)
{
	From = Shot;
	To = Shot;
	Current = Shot;
	bMoving = false;
	bOrbit = false;
	MoveTime = 0.f;
	MoveDuration = 0.f;
	LocationFollow.Reset(Shot.Location);
	SmoothedRotation = Shot.Rotation.Quaternion();
	bInitialized = true;
}

void ACampusMenuCameraDirector::CutTo(const FCampusCameraShot& Shot)
{
	if (!bInitialized)
	{
		SnapTo(Shot);
		return;
	}
	PendingCut = Shot;
	bCutPending = true;
	bMoving = false;
	Current.Location = LocationFollow.Value;
	Current.Rotation = SmoothedRotation.Rotator();
	bOrbit = true;
}

void ACampusMenuCameraDirector::FollowFraming(const FCampusCameraShot& Shot, float DeltaSeconds)
{
	if (IsMoving()) return;
	const float Alpha = CampusMath::ExpAlpha(6.f, FMath::Max(DeltaSeconds, 0.f));
	const float Distance = FVector::Dist(Current.Location, Current.Target);
	const float Desired = FVector::Dist(Shot.Location, Shot.Target);
	// make room immediately when a turn widens the silhouette, then ease back in
	const float FramedDistance = Desired > Distance ? Desired : FMath::Lerp(Distance, Desired, Alpha);
	Current.Target = Desired > Distance ? Shot.Target : FMath::Lerp(Current.Target, Shot.Target, Alpha);
	Current.Location = Current.Target + (Shot.Location - Shot.Target).GetSafeNormal() * FramedDistance;
	Current.Rotation = (Current.Target - Current.Location).Rotation();
	Current.FOV = Shot.FOV;
	Current.FocusDistance = FramedDistance;
	Current.Aperture = Shot.Aperture;
	bOrbit = true;
}

void ACampusMenuCameraDirector::MoveTo(const FCampusCameraShot& Shot, float Duration, float ArcHeight)
{
	if (!bInitialized)
	{
		SnapTo(Shot);
		return;
	}
	// Start from where the camera actually is, so a move that interrupts another carries on without a jump.
	From = Current;
	From.Location = LocationFollow.Value;
	From.Rotation = SmoothedRotation.Rotator();
	To = Shot;
	MoveTime = 0.f;
	MoveDuration = FMath::Max(Duration, 0.05f);
	bMoving = true;
	bOrbit = false;

	const FVector Start = From.Location;
	const FVector End = To.Location;
	const float Dist = FVector::Dist(Start, End);
	const FVector Lead0 = From.Rotation.Vector() * Dist * 0.22f;
	const FVector Lead1 = To.Rotation.Vector() * Dist * 0.28f;
	P1 = Start + Lead0 + FVector(0.f, 0.f, ArcHeight);
	P2 = End - Lead1 + FVector(0.f, 0.f, ArcHeight * 0.5f);
	if (Dist < 40.f || !GetWorld())
	{
		return;
	}

	// One continuous take: of a family of crane and swing curves between the two set-ups, fly the one that keeps the
	// lens clear of the set (stairs, consoles, the operator); among clear ones, the shortest, preferring the authored arc.
	FVector Side = FVector::CrossProduct(End - Start, FVector::UpVector).GetSafeNormal();
	if (Side.IsNearlyZero())
	{
		Side = FVector::RightVector;
	}
	struct FCandidate { FVector C1, C2; };
	TArray<FCandidate, TInlineAllocator<48>> Candidates;
	Candidates.Add({ P1, P2 });
	for (const float Arc : { ArcHeight, 0.f, 120.f, 220.f, 340.f })
	{
		for (const float Swing : { 0.f, 0.3f, -0.3f, 0.6f, -0.6f })
		{
			const FVector Offset = Side * (Swing * Dist);
			Candidates.Add({ Start + Lead0 * 0.6f + Offset + FVector(0.f, 0.f, Arc), End - Lead1 * 0.6f + Offset + FVector(0.f, 0.f, Arc * 0.6f) });
		}
	}
	FBox Region(ForceInit);
	Region += Start;
	Region += End;
	for (const FCandidate& C : Candidates)
	{
		Region += C.C1;
		Region += C.C2;
	}
	TArray<FBox> Obstacles;
	GatherObstacles(GetWorld(), Region.ExpandBy(FlightClearance), Start, End, Obstacles);

	int32 BestBlocked = MAX_int32;
	float BestScore = MAX_flt;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		float Length = 0.f;
		const int32 Blocked = CountBlocked(Start, Candidates[i].C1, Candidates[i].C2, End, Obstacles, Length);
		const float Score = Length * (i == 0 ? 0.85f : 1.f);
		if (Blocked < BestBlocked || (Blocked == BestBlocked && Score < BestScore))
		{
			BestBlocked = Blocked;
			BestScore = Score;
			P1 = Candidates[i].C1;
			P2 = Candidates[i].C2;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CampusMenuCam] move %.0fcm, %d obstacles, best path blocked on %d/%d samples"), Dist, Obstacles.Num(), BestBlocked, FlightSamples);
}

void ACampusMenuCameraDirector::OrbitTo(const FCampusCameraShot& Shot, float Duration)
{
	if (!bInitialized)
	{
		SnapTo(Shot);
		bOrbit = true;
		return;
	}
	// Start from where the camera actually is, so an interrupted move continues without a jump.
	From = Current;
	From.Location = LocationFollow.Value;
	From.Rotation = SmoothedRotation.Rotator();
	From.Target = From.Location + From.Rotation.Vector() * FMath::Max(FVector::Dist(Current.Location, Current.Target), 10.f);
	To = Shot;
	MoveTime = 0.f;
	MoveDuration = FMath::Max(Duration, 0.05f);
	bMoving = true;
	bOrbit = true;
}

void ACampusMenuCameraDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized)
	{
		return;
	}
	const float Elapsed = FMath::Max(DeltaSeconds, 0.f);
	const float Dt = FMath::Min(Elapsed, 0.05f);
	Time += Dt;
	if (bCutPending)
	{
		CutOpacity = FMath::Min(1.f, CutOpacity + Elapsed / 0.1f);
		if (CutOpacity >= 1.f)
		{
			SnapTo(PendingCut);
			bCutPending = false;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			}
		}
	}
	else
	{
		CutOpacity = FMath::Max(0.f, CutOpacity - Elapsed / 0.14f);
	}

	if (bMoving)
	{
		MoveTime += Elapsed;
		const float T = FMath::Clamp(MoveTime / MoveDuration, 0.f, 1.f);
		const float E = CampusMath::SmootherStep01(T);
		const float R = CampusMath::SmootherStep01(FMath::Clamp(T * 1.1f, 0.f, 1.f));
		if (bOrbit)
		{
			const FVector Focus = FMath::Lerp(From.Target, To.Target, E);
			const FVector D0 = From.Location - From.Target;
			const FVector D1 = To.Location - To.Target;
			const float L0 = FMath::Max(D0.Size(), 1.f);
			const float L1 = FMath::Max(D1.Size(), 1.f);
			const FVector Dir = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(D0 / L0, D1 / L1), E).RotateVector(D0 / L0);
			const float Dist = FMath::Exp(FMath::Lerp(FMath::Loge(L0), FMath::Loge(L1), E));
			Current.Target = Focus;
			Current.Location = Focus + Dir * Dist;
			Current.Rotation = (Focus - Current.Location).Rotation();
		}
		else
		{
			Current.Location = Bezier(From.Location, P1, P2, To.Location, E);
			Current.Rotation = FQuat::Slerp(From.Rotation.Quaternion(), To.Rotation.Quaternion(), R).Rotator();
		}
		Current.FOV = FMath::Lerp(From.FOV, To.FOV, E);
		Current.FocusDistance = FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(From.FocusDistance, 1.f)), FMath::Loge(FMath::Max(To.FocusDistance, 1.f)), E));
		Current.Aperture = FMath::Lerp(From.Aperture, To.Aperture, E);
		Current.Handheld = FMath::Lerp(From.Handheld, To.Handheld, E);
		Current.Exposure = FMath::Lerp(From.Exposure, To.Exposure, E);
		if (T >= 1.f)
		{
			bMoving = false;
			Current = To;
			LocationFollow.Reset(To.Location);
			SmoothedRotation = To.Rotation.Quaternion();
		}
	}

	if (bOrbit)
	{
		LocationFollow.Reset(Current.Location);
		SmoothedRotation = Current.Rotation.Quaternion();
	}
	else
	{
		LocationFollow.Update(Current.Location, 2.6f, 0.84f, Dt);
		SmoothedRotation = FQuat::Slerp(SmoothedRotation, Current.Rotation.Quaternion(), CampusMath::ExpAlpha(8.f, Dt)).GetNormalized();
	}

	CloseupAlpha = FMath::FInterpTo(CloseupAlpha, bCloseupTarget ? 1.f : 0.f, Dt, 4.f);
	SmoothedFStopFloor = FMath::FInterpTo(SmoothedFStopFloor, bCloseupTarget ? CloseupFStop : 0.f, Dt, 6.f);
	const float N = Current.Handheld * (bMoving ? 0.35f : 1.f) * (1.f - CloseupAlpha);
	FVector NoiseLoc(FMath::PerlinNoise1D(Time * 0.31f), FMath::PerlinNoise1D(Time * 0.27f + 5.3f), FMath::PerlinNoise1D(Time * 0.23f + 9.1f));
	NoiseLoc *= 1.8f * N;
	NoiseLoc.Z += FMath::Sin(Time * 0.95f) * 0.7f * N;
	const FRotator NoiseRot(FMath::PerlinNoise1D(Time * 0.41f + 2.2f) * 0.35f * N, FMath::PerlinNoise1D(Time * 0.37f + 7.7f) * 0.45f * N, FMath::PerlinNoise1D(Time * 0.29f + 3.3f) * 0.25f * N);

	Parallax = FMath::Lerp(Parallax, ParallaxTarget, CampusMath::ExpAlpha(3.5f, Dt));
	const float ParallaxScale = (bMoving ? 0.3f : 1.f) * (1.f - CloseupAlpha);
	const FRotator ParallaxRot(-Parallax.Y * 0.8f * ParallaxScale, Parallax.X * 1.3f * ParallaxScale, 0.f);

	const FQuat FinalRot = SmoothedRotation * (NoiseRot + ParallaxRot).Quaternion();
	SetActorLocationAndRotation(LocationFollow.Value + SmoothedRotation.RotateVector(NoiseLoc), FinalRot);

	Camera->SetFieldOfView(Current.FOV);
	Camera->PostProcessSettings.DepthOfFieldFocalDistance = Current.FocusDistance;
	Camera->PostProcessSettings.DepthOfFieldFstop = FMath::Max(Current.Aperture, SmoothedFStopFloor);
	Camera->PostProcessSettings.AutoExposureBias = Current.Exposure;
	Camera->PostProcessSettings.SceneFringeIntensity = 0.6f * (1.f - CloseupAlpha);
	Camera->PostProcessSettings.FilmGrainIntensity = FMath::Lerp(0.12f, 0.03f, CloseupAlpha);
}
