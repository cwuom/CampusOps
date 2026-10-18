#include "Player/CampusViewmodelComponent.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusFPAnimInstance.h"
#include "Player/CampusMovementComponent.h"
#include "Player/CampusPlayerController.h"
#include "Player/CampusBreathComponent.h"
#include "Weapons/CampusWeapon.h"
#include "Weapons/CampusThrowable.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "SceneTypes.h"
#include "SceneManagement.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ContentStreaming.h"
#include "CampusAssets.h"
#include "Game/CampusGraphics.h"
#include "Game/CampusEnvironment.h"
#include "TimerManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	/** Eye to rear lens of a magnified optic at full ADS (cm): the eyepiece fills about half the view height. */
	constexpr float ScopeEyeRelief = 10.5f;
	/** Zoom of the view around a magnified optic's eyepiece at full ADS (the magnification is inside the eyepiece). */
	constexpr float ScopeMainZoom = 1.3f;
	constexpr int32 ScopeMinCaptureSize = 256;
	constexpr int32 ScopeMaxCaptureSize = 2048;
	constexpr int32 ScopeCaptureSizeStep = 16;
	constexpr float ScopeCaptureHeadroom = 1.25f;
	constexpr float ScopeCaptureGrowthSlack = 1.125f;
	constexpr float ScopeCaptureShrinkThreshold = 0.6f;
	constexpr float KnifeSlashDuration = 0.45f;
	constexpr float KnifeStabDuration = 0.8f;
	constexpr float KnifeSlashContact = 0.10f;
	constexpr float KnifeStabContact = 0.22f;

	// The eye's view state (its exposure, which the magnified view is rendered at) is not exposed: explicit
	// instantiation of a template may name private members, which is the one sanctioned way to reach it.
	template<typename Tag, typename Tag::Type Member>
	struct TPrivateMember
	{
		friend typename Tag::Type GetPrivateMember(Tag) { return Member; }
	};
	struct FLocalPlayerViewStates
	{
		using Type = TArray<FSceneViewStateReference> ULocalPlayer::*;
		friend Type GetPrivateMember(FLocalPlayerViewStates);
	};
	template struct TPrivateMember<FLocalPlayerViewStates, &ULocalPlayer::ViewStates>;

	TAutoConsoleVariable<int32> CVarScopeDebug(TEXT("campus.ScopeDebug"), 0,
		TEXT("Magnified optic debugging: 1 logs the eye's exposure every half second, 2 leaves the sight picture out, 4 renders the capture without Lumen, 8 does not render the capture."));

	float ViewAspect()
	{
		FVector2D Size(1920.f, 1080.f);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Size);
		}
		return Size.X / FMath::Max(Size.Y, 1.f);
	}

	int32 ScopeCaptureAllocation(int32 CurrentSize, int32 RequestedSize, bool bResetBudget)
	{
		RequestedSize = FMath::Clamp(RequestedSize, ScopeMinCaptureSize, ScopeMaxCaptureSize);
		if (CurrentSize > 0 && !bResetBudget && RequestedSize <= CurrentSize * ScopeCaptureGrowthSlack && RequestedSize >= CurrentSize * ScopeCaptureShrinkThreshold)
		{
			return CurrentSize;
		}
		// 后坐会改变镜片投影尺寸，预留余量避免量化边界触发捕获重新预热
		const int32 ReservedSize = FMath::CeilToInt(RequestedSize * ScopeCaptureHeadroom / ScopeCaptureSizeStep) * ScopeCaptureSizeStep;
		return FMath::Clamp(ReservedSize, ScopeMinCaptureSize, ScopeMaxCaptureSize);
	}

	FVector ReloadReach(const FVector& Start, const FVector& End, float Progress, float SideClearance)
	{
		const float T = CampusMath::SmootherStep01(Progress);
		FVector Control = (Start + End) * 0.5f;
		Control.Y = FMath::Min3(Start.Y, End.Y, static_cast<double>(SideClearance)) - 5.f;
		Control.Z -= 2.f;
		return FMath::Lerp(FMath::Lerp(Start, Control, T), FMath::Lerp(Control, End, T), T);
	}

	float KnifeStrikeWeight(float Age, bool bHeavy)
	{
		const float Start = bHeavy ? 0.15f : 0.045f;
		const float Contact = bHeavy ? KnifeStabContact : KnifeSlashContact;
		const float Recover = bHeavy ? 0.28f : 0.135f;
		const float Duration = bHeavy ? KnifeStabDuration : KnifeSlashDuration;
		return CampusMath::SmootherStep01((Age - Start) / (Contact - Start))
			* (1.f - CampusMath::SmootherStep01((Age - Recover) / (Duration - Recover)));
	}

	float BlendWallAlpha(float Current, float Target, float DeltaTime)
	{
		Target = FMath::Clamp(Target, 0.f, 1.f);
		const float Rate = Target > Current ? 12.f : 7.f;
		constexpr float MaximumSpeed = 3.5f;
		const float Distance = FMath::Abs(Target - Current);
		const float ExponentialRange = MaximumSpeed / Rate;
		// 大幅接触先限速，进入小范围后指数收敛，避免边缘反复触发时腕部瞬间折转
		const float LinearTime = FMath::Max(0.f, (Distance - ExponentialRange) / MaximumSpeed);
		const float LinearStep = FMath::Min(FMath::Max(DeltaTime, 0.f), LinearTime);
		const float Remaining = (Distance - MaximumSpeed * LinearStep) * FMath::Exp(-Rate * FMath::Max(DeltaTime - LinearStep, 0.f));
		return Target + FMath::Sign(Current - Target) * Remaining;
	}

	bool PreferHighWallReady(float GunPitch)
	{
		// only an already raised carry stays muzzle-up; other poses tuck below the chest
		return GunPitch > 45.f;
	}

	struct FWallContact
	{
		int32 Probe;
		FVector Normal;
		FVector Location;
	};
	using FWallContacts = TArray<FWallContact, TInlineAllocator<12>>;
	using FWallProbeEnds = TArray<FVector, TInlineAllocator<12>>;

	FVector WallProbeCorrection(const FWallProbeEnds& Ends, const FWallContacts& Contacts)
	{
		FVector Correction = FVector::ZeroVector;
		for (int32 Pass = 0; Pass < 8; ++Pass)
		{
			bool bMoved = false;
			for (const FWallContact& Contact : Contacts)
			{
				const double Clearance = FVector::DotProduct(Ends[Contact.Probe] + Correction - Contact.Location, Contact.Normal);
				const double Missing = 1.f - Clearance;
				if (Missing > KINDA_SMALL_NUMBER)
				{
					Correction += Contact.Normal * Missing;
					bMoved = true;
				}
			}
			if (!bMoved)
			{
				break;
			}
		}
		return Correction;
	}

	float WallForwardAdvance(const FWallProbeEnds& Ends, const FWallContacts& Contacts, const FVector& ForwardStep, float Maximum)
	{
		float Advance = FMath::Max(0.f, Maximum);
		for (const FWallContact& Contact : Contacts)
		{
			const float IntoWall = -FVector::DotProduct(ForwardStep, Contact.Normal);
			if (IntoWall > KINDA_SMALL_NUMBER)
			{
				for (const FVector& End : Ends)
				{
					const float Room = FVector::DotProduct(End - Contact.Location, Contact.Normal) - 1.f;
					Advance = FMath::Min(Advance, FMath::Max(0.f, Room / IntoWall));
				}
			}
		}
		return Advance;
	}

	FTransform WallTuckPose(const FTransform& Pose, const FQuat& Calibration, const FVector& GripLocal, float Alpha, float LowPitch, const FVector& HandShift, bool bHighReady = false)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
		if (Alpha <= SMALL_NUMBER)
		{
			return Pose;
		}
		const FRotator Gun = (Pose.GetRotation() * Calibration).Rotator();
		FRotator Low = Gun;
		// keep the side chosen on contact through a sprint transition instead of reversing the muzzle at an angle threshold
		Low.Pitch = bHighReady ? FMath::Abs(LowPitch) : -FMath::Abs(LowPitch);
		// 先把双手压低再收拢，退墙时反序展开，枪托不会沿途从镜头中央扫过
		const float Fold = FMath::Clamp((Alpha - 0.35f) / 0.65f, 0.f, 1.f);
		const FQuat Rotation = (FQuat::Slerp(Gun.Quaternion(), Low.Quaternion(), Fold) * Calibration.Inverse()).GetNormalized();
		const FVector Start = Pose.TransformPosition(GripLocal);
		FVector Close = Start + HandShift;
		Close.X = FMath::Max(Close.X, 18.5);
		Close.Y = bHighReady ? FMath::Clamp(Close.Y, 18.0, 22.0) : FMath::Clamp(Close.Y, 8.0, 12.0);
		Close.Z = FMath::Min(Close.Z, bHighReady ? -50.0 : -36.0);
		// lower the whole rig below the chest so neither the stock nor the support arm crosses the eye
		const float SideArc = Alpha < 1.f ? 6.f * FMath::Sin(PI * Alpha) : 0.f;
		const FVector Grip = FMath::Lerp(Start, Close, CampusMath::SmoothStep01(Alpha / 0.5f))
			+ FVector(0.f, SideArc, 0.f);
		return FTransform(Rotation, Grip - Rotation.RotateVector(GripLocal * Pose.GetScale3D()), Pose.GetScale3D());
	}
}

UCampusViewmodelComponent::UCampusViewmodelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After animation (bone-based calibration and head lock use this frame's pose) but before the camera manager
	// builds the view, so the camera height, FOV and arms all land in the same frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
}

void UCampusViewmodelComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACampusCharacter>(GetOwner());
}

void UCampusViewmodelComponent::UpdateHeadLock()
{
	USkeletalMeshComponent* Arms = Character->ArmsMesh;
	if (!Arms || !Arms->GetSkeletalMeshAsset())
	{
		return;
	}
	const int32 HeadIndex = Arms->GetBoneIndex(TEXT("head"));
	if (HeadIndex == INDEX_NONE)
	{
		return;
	}
	const FVector HeadCS = Arms->GetBoneTransform(HeadIndex, FTransform::Identity).GetLocation();
	const FQuat RelRot = Arms->GetRelativeRotation().Quaternion();
	const FVector Desired = -RelRot.RotateVector(HeadCS);
	if (!bHeadLockInit)
	{
		HeadLockOffset = Desired;
		bHeadLockInit = true;
	}
	else
	{
		HeadLockOffset = FMath::Lerp(HeadLockOffset, Desired, CampusMath::ExpAlpha(30.f, GetWorld()->GetDeltaSeconds()));
	}
	Arms->SetRelativeLocation(HeadLockOffset);
}

void UCampusViewmodelComponent::Calibrate(ACampusWeapon* Weapon, float DeltaTime, bool bForce)
{
	if (!Weapon)
	{
		return;
	}
	USceneComponent* Sight = Weapon->GetFPSight();
	USceneComponent* Frame = Weapon->GetFPFrame();
	if (!Sight || !Frame || !Frame->GetAttachParent())
	{
		return;
	}

	const bool bWeaponChanged = CalibratedWeapon.Get() != Weapon;
	if (bWeaponChanged || ClearanceFrame.Get() != Frame)
	{
		CacheWeaponClearance(Weapon);
		bSmoothLeftHandInit = false;
		bWasReloadPath = false;
		ReloadReturnTime = -1.f;
		bMantleHandWasMoving = false;
		bMantleHandActive = false;
		bMantleHandReturning = false;
		bMantleHandHasRest = false;
		MantleHandAlpha = 0.f;
		MantleLookPose = 0.f;
		MantleRollPose = 0.f;
		MantleTuckPoseAlpha = 0.f;
		WallCorrection = FVector::ZeroVector;
		WallAdvance = 0.f;
		BlockAlpha = 0.f;
		WallTargetAlpha = 0.f;
		bWallPoseActive = false;
		bWallHighReady = false;
	}
	const UCampusFPAnimInstance* Anim = Character->GetArmsAnim();
	const bool bActionPlaying = Anim && Anim->IsAnyMontagePlaying();
	if (!bForce && bCalibrated && !bWeaponChanged && bActionPlaying)
	{
		return;
	}

	FVector S, G;
	FQuat R;
	USkeletalMeshComponent* Arms = Character->ArmsMesh;
	const int32 HandIndex = Arms->GetBoneIndex(TEXT("hand_r"));
	if (Anim && Anim->HasAnimatedHand() && HandIndex != INDEX_NONE)
	{
		// The rig hangs off hand_r: measure it where the animation alone holds it, so the procedural hands offset
		// (which is derived from this calibration) never feeds back into it.
		const FTransform HandWorld = Arms->GetBoneTransform(HandIndex);
		const FTransform HandPivot = Anim->GetAnimatedHandR() * Arms->GetRelativeTransform();
		const FTransform SightPivot = Sight->GetComponentTransform().GetRelativeTransform(HandWorld) * HandPivot;
		S = SightPivot.GetLocation();
		R = SightPivot.GetRotation();
		G = (Frame->GetComponentTransform().GetRelativeTransform(HandWorld) * HandPivot).GetLocation();
	}
	else
	{
		const FTransform PivotT = Character->ViewmodelPivot->GetComponentTransform();
		S = PivotT.InverseTransformPositionNoScale(Sight->GetComponentLocation());
		R = PivotT.InverseTransformRotation(Sight->GetComponentQuat());
		G = PivotT.InverseTransformPositionNoScale(Frame->GetComponentLocation());
	}

	if (!bCalibrated || bWeaponChanged || bForce)
	{
		CalibSight = S;
		CalibGunRot = R;
		CalibGrip = G;
		bCalibrated = true;
		CalibratedWeapon = Weapon;
	}
	else
	{
		const float A = CampusMath::ExpAlpha(8.f, DeltaTime);
		CalibSight = FMath::Lerp(CalibSight, S, A);
		CalibGunRot = FQuat::Slerp(CalibGunRot, R, A).GetNormalized();
		CalibGrip = FMath::Lerp(CalibGrip, G, A);
	}
}

void UCampusViewmodelComponent::CacheWeaponClearance(ACampusWeapon* Weapon)
{
	USceneComponent* Frame = Weapon ? Weapon->GetFPFrame() : nullptr;
	ClearanceFrame = Frame;
	WeaponClearanceBox.Init();
	ReloadSideClearance = -8.f;
	if (!Frame)
	{
		return;
	}
	TArray<USceneComponent*> Children;
	Frame->GetChildrenComponents(true, Children);
	for (const USceneComponent* Child : Children)
	{
		const UStaticMeshComponent* Part = Cast<UStaticMeshComponent>(Child);
		const UStaticMesh* Mesh = Part ? Part->GetStaticMesh() : nullptr;
		// generated model parts use SM_; exclude muzzle flashes, laser beams and other effect primitives
		if (!Mesh || !Mesh->GetName().StartsWith(TEXT("SM_")))
		{
			continue;
		}
		const FBox Box = Mesh->GetBoundingBox().TransformBy(Part->GetComponentTransform().GetRelativeTransform(Frame->GetComponentTransform()));
		WeaponClearanceBox += Box;
		const FString Name = Mesh->GetName();
		if (Name.Contains(TEXT("Chassis")) || Name.Contains(TEXT("Receiver")))
		{
			// the glove needs clearance from the receiver, which can be wider than its magazine
			ReloadSideClearance = FMath::Min(ReloadSideClearance, static_cast<float>(Box.Min.Y) - 5.2f);
		}
	}
	if (!WeaponClearanceBox.IsValid)
	{
		const FCampusWeaponDef* WeaponDef = Weapon->GetDef();
		WeaponClearanceBox = FBox(FVector(-8.f, -4.f, -10.f), WeaponDef ? WeaponDef->Muzzle + FVector(6.f, 4.f, 4.f) : FVector(70.f, 4.f, 12.f));
	}
}

void UCampusViewmodelComponent::ConstrainWeaponPose(ACampusWeapon* Weapon, FTransform& Pose, float DeltaTime)
{
	if (!Weapon || !WeaponClearanceBox.IsValid)
	{
		return;
	}
	const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
	const FVector Eye = CamT.GetLocation();
	FMinimalViewInfo RenderView;
	RenderView.Location = Eye;
	RenderView.Rotation = (CamT.GetRotation() * CameraRotationOffset.Quaternion()).Rotator();
	RenderView.FOV = CurrentFOV;
	RenderView.FirstPersonFOV = Character->FirstPersonCamera->bEnableFirstPersonFieldOfView ? CurrentViewmodelFOV : CurrentFOV;
	RenderView.FirstPersonScale = Character->FirstPersonCamera->bEnableFirstPersonScale ? Character->FirstPersonCamera->FirstPersonScale : 1.f;
	const FVector Center = WeaponClearanceBox.GetCenter();
	const FVector Max = WeaponClearanceBox.Max;
	const FVector Min = WeaponClearanceBox.Min;
	TArray<FVector, TInlineAllocator<12>> Probes = {
		FVector(Max.X, Center.Y, Center.Z),
		FVector(Center.X, Min.Y, Max.Z),
		FVector(Center.X, Max.Y, Max.Z)
	};
	// 枪托、弹匣底部和枪管外缘也必须留在障碍外，不能只检查上沿和枪口中心
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		Probes.Add(FVector(Corner & 1 ? Max.X : Min.X, Corner & 2 ? Max.Y : Min.Y, Corner & 4 ? Max.Z : Min.Z));
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusWeaponBlock), false, Character);
	Params.AddIgnoredActor(Weapon);
	FCollisionResponseParams Response;
	// 角色不会触发环境收枪，过滤发生在查询阶段，仍可看到角色身后的墙
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	WallHitActor.Reset();
	WallHitComponent.Reset();
	WallHitDistance = 0.f;
	bWallHitPenetrating = false;
	const FTransform ReadyGun(Pose.GetRotation() * CalibGunRot, Pose.TransformPosition(CalibGrip));
	const float LeadDistance = 45.f + FMath::Min(static_cast<float>(Character->GetVelocity().Size2D()) * 0.08f, 45.f);
	FWallContacts ReadyContacts;
	float Target = 0.f;
	for (int32 Index = 0; Index < Probes.Num(); ++Index)
	{
		const FVector EndCam = ReadyGun.TransformPosition(Probes[Index]);
		// 枪托后方只检查实际穿透，向后延长预测会把安全的身后墙面误判为前方阻挡
		if (EndCam.X < 4.f) continue;
		const FVector End = RenderView.TransformWorldToFirstPerson(CamT.TransformPosition(EndCam), false);
		const FVector Direction = CamT.GetRotation().GetForwardVector();
		FHitResult Hit;
		auto IsForwardSurface = [&](const FHitResult& Candidate)
		{
			const FVector Normal = Candidate.bStartPenetrating ? Candidate.Normal : Candidate.ImpactNormal;
			return Normal.Z <= 0.55f && FVector::DotProduct(Normal, Direction) < -0.15f;
		};
		// 横向预留动作时间，纵向保持窄范围；身侧安全墙面不能继续压掉已转开的瞄准
		bool bHit = GetWorld()->SweepSingleByChannel(Hit, Eye, End + Direction * LeadDistance, CamT.GetRotation(), ECC_Pawn,
			FCollisionShape::MakeBox(FVector(4.f, 50.f, 4.f)), Params, Response);
		if (!bHit || !IsForwardSurface(Hit))
		{
			bHit = GetWorld()->SweepSingleByChannel(Hit, Eye, End + Direction * LeadDistance, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeSphere(4.f), Params, Response);
		}
		if (bHit && IsForwardSurface(Hit))
		{
			// 低头时地面只参与实际间隙约束，提前看到地面不能触发向下收枪
			if (Hit.Normal.Z > 0.55f) continue;
			const float Gap = FVector::DotProduct(Hit.Location - End, Direction);
			const float ProbeTarget = CampusMath::SmoothStep01((LeadDistance - Gap) / (LeadDistance * 0.6f));
			if (!WallHitActor.IsValid() || ProbeTarget >= Target)
			{
				WallHitActor = Hit.GetActor();
				WallHitComponent = Hit.GetComponent();
				WallHitDistance = Hit.Distance;
				bWallHitPenetrating = Hit.bStartPenetrating;
			}
			Target = FMath::Max(Target, ProbeTarget);
			const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
			ReadyContacts.Add({ Index, Normal, Hit.ImpactPoint + Normal * 4.f });
		}
	}
	// sense the authored pose before tucking, so clearing the muzzle cannot cancel its own avoidance pose
	WallTargetAlpha = Target;
	if (!bWallPoseActive && Target > 0.02f)
	{
		bWallHighReady = PreferHighWallReady(ReadyGun.GetRotation().Rotator().Pitch);
		bWallPoseActive = true;
	}
	BlockAlpha = BlendWallAlpha(BlockAlpha, Target, DeltaTime);
	if (Target <= SMALL_NUMBER && BlockAlpha < 0.002f)
	{
		bWallPoseActive = false;
	}
	const FCampusWeaponDef* Def = Weapon->GetDef();
	if (!Def || !Def->IsMelee())
	{
		const FTransform* Override = PoseOverrides.Find(TEXT("block"));
		const float LowPitch = Override ? Override->Rotator().Pitch : -88.f;
		const FVector HandShift = Override ? Override->GetLocation() : FVector(-4.f, 0.f, -26.f);
		auto Tuck = [&](float Alpha) { return WallTuckPose(Pose, CalibGunRot, CalibGrip, Alpha, LowPitch, HandShift, bWallHighReady); };
		Pose = Tuck(BlockAlpha);
		// use available room in front of the wrist, rather than holding the glove unnecessarily close to the eye
		const FTransform TuckedGun(Pose.GetRotation() * CalibGunRot, Pose.TransformPosition(CalibGrip));
		const FVector ForwardStep = RenderView.TransformWorldToFirstPerson(Eye + CamT.GetRotation().GetForwardVector(), false) - Eye;
		FWallProbeEnds Ends;
		for (const FVector& Probe : Probes)
		{
			Ends.Add(RenderView.TransformWorldToFirstPerson(CamT.TransformPosition(TuckedGun.TransformPosition(Probe)), false));
		}
		const float Advance = WallForwardAdvance(Ends, ReadyContacts, ForwardStep, 30.f - static_cast<float>(TuckedGun.GetLocation().X));
		const float Settled = CampusMath::SmoothStep01((BlockAlpha - 0.98f) / 0.02f);
		WallAdvance = FMath::Lerp(WallAdvance, Advance * Settled, CampusMath::ExpAlpha(10.f, DeltaTime));
		Pose.AddToTranslation(FVector(WallAdvance, 0.f, 0.f));
	}
	const FTransform GunCam(Pose.GetRotation() * CalibGunRot, Pose.TransformPosition(CalibGrip));
	FWallProbeEnds EndPoints;
	for (int32 Index = 0; Index < Probes.Num(); ++Index)
	{
		const FVector EndCam = GunCam.TransformPosition(Probes[Index]);
		// sweep the rendered first-person position, including the engine's depth scale and FOV correction
		const FVector End = RenderView.TransformWorldToFirstPerson(CamT.TransformPosition(EndCam), false);
		EndPoints.Add(End);
	}
	WallCorrection *= FMath::Exp(-12.f * DeltaTime);
	// 墙边只约束真正命中的探点，修正后重新扫描，不能把有限墙边无限外推到整把枪
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		FWallProbeEnds CorrectedEnds;
		FWallContacts Contacts;
		for (int32 Index = 0; Index < EndPoints.Num(); ++Index)
		{
			const FVector End = EndPoints[Index] + WallCorrection;
			CorrectedEnds.Add(End);
			FHitResult Hit;
			if (GetWorld()->SweepSingleByChannel(Hit, Eye, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(4.f), Params, Response))
			{
				if (!WallHitActor.IsValid())
				{
					WallHitActor = Hit.GetActor();
					WallHitComponent = Hit.GetComponent();
					WallHitDistance = Hit.Distance;
					bWallHitPenetrating = Hit.bStartPenetrating;
				}
				const FVector Normal = (Hit.bStartPenetrating ? Hit.Normal : Hit.ImpactNormal).GetSafeNormal();
				const FVector SafeCenter = Hit.bStartPenetrating ? Hit.Location + Normal * Hit.PenetrationDepth : Hit.ImpactPoint + Normal * 4.f;
				Contacts.Add({ Index, Normal, SafeCenter });
			}
		}
		const FVector Missing = WallProbeCorrection(CorrectedEnds, Contacts);
		if (Missing.IsNearlyZero(0.001f)) break;
		WallCorrection += Missing;
	}
	const FVector Corrected = RenderView.TransformFirstPersonToWorld(Eye + WallCorrection, false);
	Pose.AddToTranslation(CamT.InverseTransformVectorNoScale(Corrected - Eye));
}

void UCampusViewmodelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Character || !Character->IsLocalHuman() || !Character->ViewmodelPivot)
	{
		return;
	}

	const float Dt = FMath::Clamp(DeltaTime, 0.0001f, 0.05f);
	Time += Dt;
	if (ThrowTime >= 0.f)
	{
		ThrowTime += Dt * ThrowRate;
		if (ThrowCancelAt >= 0.f && ThrowTime - ThrowCancelAt > 0.32f)
		{
			OnThrowEnd();
		}
	}

	ACampusWeapon* Weapon = Character->GetCurrentWeapon();
	const FCampusWeaponDef* Def = Weapon ? Weapon->GetDef() : nullptr;
	const FCampusWeaponStats* Stats = Weapon ? &Weapon->GetStats() : nullptr;
	UCampusMovementComponent* Move = Character->GetCampusMovement();

	UpdateHeadLock();

	// ------------------------------------------------------------------ look velocity
	const FRotator ControlRot = Character->GetControlRotation();
	if (bHasPrevRotation)
	{
		const FRotator Delta = (ControlRot - PrevControlRotation).GetNormalized();
		const FVector2D Raw(Delta.Yaw / Dt, Delta.Pitch / Dt);
		LookVelocity = FMath::Lerp(LookVelocity, Raw, CampusMath::ExpAlpha(22.f, Dt));
	}
	PrevControlRotation = ControlRot;
	bHasPrevRotation = true;

	Calibrate(Weapon, Dt, false);

	// ------------------------------------------------------------------ base poses
	// ADS levels the sight line onto the eye with the head where the animation authored it (eye ~6 cm ahead of the
	// head bone), so the sight sits at its natural cheek-weld distance and the shoulders stay beside the camera.
	const float MaxSightDistance = Def ? Def->AimDistance : 26.f;
	const FQuat RAds = CalibGunRot.Inverse();
	const FVector SightRot = RAds.RotateVector(CalibSight);
	float SightDistance = FMath::Clamp(SightRot.X - 6.f, 9.f, MaxSightDistance);
	if (Weapon && Weapon->HasScopeOptic())
	{
		// A magnified optic is held at its eye relief, whatever the cheek weld of the animation would give.
		SightDistance = ScopeEyeRelief + Weapon->GetScopeEyepieceOffset();
	}
	else if (Weapon && Def && Def->IronEyeRelief > 0.f && !Weapon->HasOptic())
	{
		// Peep irons: the eye sits right behind the rear aperture, so it opens into a ghost ring around the post. Open
		// notches far forward (AK) are aimed from the stock comb, well behind the receiver.
		SightDistance = Def->IronEyeRelief;
	}
	const FVector TAds = FVector(SightDistance, 0.f, 0.f) - SightRot;
	const FTransform ADSPose(RAds, TAds);
	const FVector GripAds = ADSPose.TransformPosition(CalibGrip);

	// Automation can override any of the tuned poses at runtime.
	auto Tuned = [this](const TCHAR* Name, const FRotator& Rot, const FVector& Loc)
	{
		const FTransform* Override = PoseOverrides.Find(FName(Name));
		return Override ? TPair<FRotator, FVector>(Override->Rotator(), Override->GetLocation()) : TPair<FRotator, FVector>(Rot, Loc);
	};

	const TPair<FRotator, FVector> Hip = Tuned(TEXT("hip"), Def ? Def->HipRotation : FRotator(4.f, -8.f, -10.f), Def ? Def->HipOffset : FVector(14.f, 9.f, -2.f));
	FTransform HipPose = CampusMath::RotateAround(ADSPose, FQuat(Hip.Key), GripAds);
	HipPose.AddToTranslation(Hip.Value);

	const float RawADS = Character->GetADSAlpha();
	ADSPoseAlpha = CampusMath::SmoothStep01(RawADS);
	const float NotADS = 1.f - ADSPoseAlpha;

	FTransform Pose = CampusMath::LerpTransform(HipPose, ADSPose, ADSPoseAlpha);
	const FVector Grip = Pose.TransformPosition(CalibGrip);

	auto ApplyOffset = [&Pose, &Grip](const FRotator& Rot, const FVector& Loc, float Alpha)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
		if (Alpha <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		FTransform Target = CampusMath::RotateAround(Pose, FQuat(Rot), Grip);
		Target.AddToTranslation(Loc);
		Pose = CampusMath::LerpTransform(Pose, Target, Alpha);
	};
	// The sight comes up on an arc rather than a straight slide: a slight cant and dip mid-way, settled at either end.
	{
		const float Mid = FMath::Sin(PI * ADSPoseAlpha);
		const TPair<FRotator, FVector> Arc = Tuned(TEXT("adsarc"), FRotator(-1.2f, 0.4f, -4.5f), FVector(0.f, 0.3f, -0.7f));
		ApplyOffset(Arc.Key * Mid, Arc.Value * Mid, 1.f);
	}
	// Absolute carry poses (sprints): the grip goes to a camera-space point and the weapon frame takes a camera-space
	// orientation, whatever the hip / ADS tuning is. GunRot = Pose rotation * calibrated gun rotation.
	auto ApplyCarry = [this, &Pose](const FQuat& GunRot, const FVector& GripPos, float Alpha)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
		if (Alpha <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		// The hand leads and the weapon turns after it: the grip is out at the shoulder before the muzzle swings up, so a
		// long stock pivots down outside the frame instead of sweeping through the middle of it (and the reverse coming
		// out: the muzzle drops first, then the hand comes in).
		const float PosAlpha = CampusMath::SmoothStep01(FMath::Min(Alpha * 1.6f, 1.f));
		const float RotAlpha = FMath::Pow(Alpha, 1.7f);
		// Swing the barrel along a great circle onto the carry direction and twist about it separately: the weapon
		// flips straight up beside the shoulder instead of the shortest-arc blend rolling it across the screen.
		const FQuat From = Pose.GetRotation() * CalibGunRot;
		const FQuat Swing = FQuat::FindBetweenNormals(From.GetForwardVector(), GunRot.GetForwardVector());
		const FQuat Twist = (Swing * From).Inverse() * GunRot;
		const FQuat Gun = FQuat::Slerp(FQuat::Identity, Swing, RotAlpha) * From * FQuat::Slerp(FQuat::Identity, Twist, RotAlpha);
		const FQuat R = (Gun * CalibGunRot.Inverse()).GetNormalized();
		const FVector Grip = FMath::Lerp(Pose.TransformPosition(CalibGrip), GripPos, PosAlpha);
		Pose = FTransform(R, Grip - R.RotateVector(CalibGrip));
	};

	// Bring-up arc: mid-transition the weapon dips and cants a little, so aiming reads as lifting it to the eye.
	const float AimArc = FMath::Sin(PI * ADSPoseAlpha) * (Character->IsAimHeld() ? 1.f : 0.55f);
	ApplyOffset(FRotator(-1.4f, 1.2f, 7.f), FVector(-1.2f, 0.4f, -1.4f), AimArc);

	// ------------------------------------------------------------------ locomotion poses
	const bool bTac = Character->IsTacSprinting();
	const bool bSprint = Character->IsSprinting() && !bTac;
	const bool bSlide = Character->IsSliding();
	const bool bAir = Move && Move->IsFalling();

	// Reloading on the run keeps the weapon up in the reload pose, only a little lowered.
	SprintAlpha.Update(bSprint ? (ReloadTime >= 0.f ? 0.3f : 1.f) : 0.f, 3.0f, 0.85f, Dt);
	TacSprintAlpha.Update(bTac ? 1.f : 0.f, 3.6f, 0.82f, Dt);
	SlideAlpha.Update(bSlide ? 1.f : 0.f, 3.2f, 0.9f, Dt);
	CrouchAlpha.Update((Character->bIsCrouched && !bSlide) ? 1.f : 0.f, 3.f, 1.f, Dt);
	AirAlpha.Update(bAir ? 1.f : 0.f, 2.5f, 0.9f, Dt);
	if (bSlide != bWasSliding)
	{
		if (bSlide)
		{
			bSlideJumpPending = false;
		}
		// 跳出滑铲使用起跳反馈，避免同一次过渡再叠加站起和滑铲结束冲量
		if (bSlide || !bSlideJumpPending)
		{
			CamImpact.AddImpulse(bSlide ? FVector(-10.f, 0.f, 12.f) : FVector(5.f, 0.f, -4.f));
			ImpactPos.AddImpulse(bSlide ? FVector(0.f, 0.f, -28.f) : FVector(0.f, 0.f, 16.f));
			ImpactRot.AddImpulse(bSlide ? FVector(-16.f, 0.f, -30.f) : FVector(8.f, 0.f, 10.f));
		}
		bWasSliding = bSlide;
	}
	if (!bSlide && !Character->bIsCrouched)
	{
		bSlideJumpPending = false;
	}

	if (Def)
	{
		// Sprint (and what tactical sprint falls back to when the legs give out): still "in a firing position" - the hip
		// carry turned across the body, muzzle low and to the left, canted, both hands on.
		const TPair<FRotator, FVector> Sprint = Tuned(TEXT("sprint"), Def->SprintRotation, Def->SprintOffset);
		ApplyOffset(Sprint.Key, Sprint.Value, SprintAlpha.Value * NotADS);
		// Tactical sprint: raised one-handed beside the right shoulder, muzzle up, the weapon's left side to the eye,
		// leaning in toward the centre and rocking side to side with every stride.
		const TPair<FRotator, FVector> Tac = Tuned(TEXT("tac"), Def->TacSprintRotation, Def->TacSprintOffset);
		// The arm swings the weapon across with each stride (the grip travels, the muzzle stays fairly steady).
		const float Stride = FMath::Sin(BobPhase);
		const float Lean = Tuned(TEXT("taclean"), FRotator(Def->TacSprintLean, 0.f, 0.f), FVector::ZeroVector).Key.Pitch + 4.f * Stride;
		const FQuat TacRot = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Lean)) * Tac.Key.Quaternion();
		const FVector TacGrip = Tac.Value + FVector(0.f, 4.5f * Stride, 2.4f * FMath::Abs(Stride) - 1.2f);
		ApplyCarry(TacRot, TacGrip, TacSprintAlpha.Value * NotADS);
	}
	// Aiming during a slide keeps the sight on the eye line; only the hip-fire slide pose is canted.
	const TPair<FRotator, FVector> Slide = Tuned(TEXT("slide"), FRotator(6.f, 2.f, -10.f), FVector(-1.f, -1.f, 1.5f));
	ApplyOffset(Slide.Key, Slide.Value, SlideAlpha.Value * NotADS);
	ApplyOffset(FRotator(0.f, 0.f, -5.f), FVector(0.f, -0.6f, -0.8f), CrouchAlpha.Value * NotADS);

	// ------------------------------------------------------------------ action timelines
	float Lowered = 0.f;
	if (HolsterTime >= 0.f)
	{
		HolsterTime += Dt;
		Lowered = CampusMath::SmoothStep01(HolsterTime / FMath::Max(HolsterDuration, 0.01f));
	}
	if (EquipTime >= 0.f)
	{
		EquipTime += Dt;
		const float A = EquipTime / FMath::Max(EquipDuration, 0.01f);
		// Ease-out: the weapon pops into view and decelerates onto the shoulder rather than lingering below the frame.
		Lowered = FMath::Pow(1.f - FMath::Clamp(A, 0.f, 1.f), EquipEaseExponent);
		if (!bEquipSettled && A >= 0.72f)
		{
			// Weapon seats into the shoulder at the end of the raise.
			bEquipSettled = true;
			RecoilPos.AddImpulse(FVector(-18.f, 0.f, -10.f));
			RecoilRot.AddImpulse(FVector(-14.f, 6.f, 18.f));
		}
		if (A >= 1.f)
		{
			EquipTime = -1.f;
			Lowered = 0.f;
		}
	}
	LoweredAlpha = Lowered;
	// The raise comes up from low right, rolling in, rather than sliding straight up. Fully lowered is just out of
	// frame, so the weapon is in view from about a third of the way through the raise.
	ApplyOffset(FRotator(-42.f, 12.f, -30.f), FVector(-5.f, 3.f, -18.f), Lowered);
	ApplyOffset(FRotator(6.f, -10.f, 18.f), FVector(0.f, -1.f, 2.f), FMath::Sin(PI * Lowered) * 0.6f);

	float ReloadTarget = 0.f;
	if (ReloadTime >= 0.f)
	{
		ReloadTime = DebugReloadProgress >= 0.f ? DebugReloadProgress * ReloadDuration : ReloadTime + Dt;
		const float A = ReloadTime / FMath::Max(ReloadDuration, 0.1f);
		// A tactical reload is done once the magazine is seated: the weapon comes back up as the support hand returns
		// to the handguard, rather than the arm reaching out to a weapon still turned in the reload pose.
		const float Insert = Stats ? FMath::Clamp(Stats->ReloadInsertFraction, 0.5f, 0.8f) : 0.65f;
		const float OutStart = bReloadEmpty ? 0.84f : FMath::Min(Insert + 0.03f, 0.84f);
		ReloadTarget = CampusMath::SmoothStep01(A / 0.15f) * (1.f - CampusMath::SmoothStep01((A - OutStart) / (1.f - OutStart)));
		if (A >= 1.f && DebugReloadProgress < 0.f)
		{
			ReloadTime = -1.f;
		}
	}
	// Spring-driven so an interrupted reload blends back to the ready pose instead of popping. Rifles cant the top
	// outboard so the magazine well faces the support hand; pistols tip the grip toward it.
	ReloadWeight.Update(ReloadTarget, ReloadTime >= 0.f ? 7.f : 4.5f, 1.f, Dt);
	const bool bPistolReload = Def && Def->bPistolAnims;
	// The weapon comes up toward the middle of the view so the magazine change happens in sight.
	const TPair<FRotator, FVector> ReloadPose = Tuned(TEXT("reload"), Def ? Def->ReloadRotation : FRotator::ZeroRotator, Def ? Def->ReloadOffset : FVector::ZeroVector);
	// The reload turn pivots on the support hand. On the sights the stock and optic are at the eye, so turned there they
	// would swing through the lens (a magazine run dry while aiming starts its reload on the sights): the weapon comes
	// off the sights first and only turns in as it does.
	const float OffSights = FMath::Square(NotADS);
	ApplyOffset(ReloadPose.Key, ReloadPose.Value, FMath::Clamp(ReloadWeight.Value, 0.f, 1.f) * OffSights);
	// Racking a charging handle after an empty magazine: the weapon is brought to the support hand (tipped so the handle
	// comes up and toward it, and dropped a little) instead of the arm stretching across the view to reach it.
	{
		FVector Handle;
		float HandleSide = 1.f;
		const bool bRackNow = ReloadTime >= 0.f && bReloadEmpty && Def && !bPistolReload && Def->bRackHandle && Weapon && Weapon->GetRackHandle(Handle, HandleSide)
			&& (HandleSide < 0.f || !Weapon->HasOptic() || Def->bRackUnderOptic);
		float RackTarget = 0.f;
		if (bRackNow)
		{
			const float A = ReloadTime / FMath::Max(ReloadDuration, 0.1f);
			const float I = Stats ? FMath::Clamp(Stats->ReloadInsertFraction, 0.5f, 0.8f) : 0.65f;
			RackTarget = CampusMath::SmoothStep01((A - (I + 0.04f)) / 0.1f) * (1.f - CampusMath::SmoothStep01((A - 0.88f) / 0.1f));
		}
		RackWeight.Update(RackTarget, 7.f, 1.f, Dt);
		const bool bRightHandle = HandleSide > 0.f;
		const TPair<FRotator, FVector> Rack = bRightHandle
			? Tuned(TEXT("rackr"), FRotator::ZeroRotator, FVector::ZeroVector)
			// A cocking tube up front on the left (HK): muzzle tipped down and right, so the hand reaches forward and low.
			: Tuned(TEXT("rackl"), FRotator(-10.f, 10.f, 0.f), FVector(6.f, 4.f, -4.f));
		ApplyOffset(Rack.Key, Rack.Value, FMath::Clamp(RackWeight.Value, 0.f, 1.f) * OffSights);
	}
	if (ReloadTime >= 0.f && !bReloadMagKicked && ReloadTime / FMath::Max(ReloadDuration, 0.1f) >= 0.2f)
	{
		// The magazine lets go: the weapon jumps a little as the weight comes off.
		bReloadMagKicked = true;
		RecoilPos.AddImpulse(FVector(-8.f, 0.f, 16.f));
		RecoilRot.AddImpulse(FVector(-10.f, 4.f, 8.f));
	}

	// Interrupted actions hold their pose and fade out over ~0.16 s instead of snapping back.
	auto Fade = [Dt](bool bFading, float& Weight, float& Timeline)
	{
		if (!bFading)
		{
			return false;
		}
		Weight -= Dt / 0.16f;
		if (Weight <= 0.f)
		{
			Weight = 1.f;
			Timeline = -1.f;
			return true;
		}
		return false;
	};

	if (InspectTime >= 0.f)
	{
		if (!bInspectFading)
		{
			InspectTime += Dt;
		}
		const float W = bInspectFading ? CampusMath::SmoothStep01(InspectFade) : 1.f;
		const float A = InspectTime / 3.2f;
		const float P1 = CampusMath::SmoothStep01(A / 0.18f) * (1.f - CampusMath::SmoothStep01((A - 0.40f) / 0.14f));
		const float P2 = CampusMath::SmoothStep01((A - 0.46f) / 0.14f) * (1.f - CampusMath::SmoothStep01((A - 0.82f) / 0.18f));
		ApplyOffset(FRotator(10.f, -38.f, 38.f), FVector(5.f, -7.f, 3.f), P1 * W);
		ApplyOffset(FRotator(-12.f, 32.f, -58.f), FVector(3.f, -5.f, 5.f), P2 * W);
		if (Fade(bInspectFading, InspectFade, InspectTime))
		{
			bInspectFading = false;
		}
		else if (A >= 1.f)
		{
			InspectTime = -1.f;
		}
	}

	if (MeleeTime >= 0.f)
	{
		if (!bMeleeFading)
		{
			MeleeTime += Dt;
		}
		const float W = bMeleeFading ? CampusMath::SmoothStep01(MeleeFade) : 1.f;
		const float A = MeleeTime / 0.5f;
		const float Wind = CampusMath::SmoothStep01(A / 0.22f) * (1.f - CampusMath::SmoothStep01((A - 0.22f) / 0.12f));
		const float Strike = CampusMath::SmoothStep01((A - 0.2f) / 0.1f) * (1.f - CampusMath::SmoothStep01((A - 0.45f) / 0.55f));
		ApplyOffset(FRotator(12.f, 28.f, 32.f), FVector(-6.f, 9.f, 3.f), Wind * W);
		ApplyOffset(FRotator(-6.f, -42.f, -42.f), FVector(20.f, -9.f, 2.f), Strike * W);
		if (Fade(bMeleeFading, MeleeFade, MeleeTime))
		{
			bMeleeFading = false;
		}
		else if (A >= 1.f)
		{
			MeleeTime = -1.f;
		}
	}

	// Knife: cuts alternate forehand (from high right down across to the left) and backhand (back across from the
	// left). The hand cocks the blade back, whips through in about a tenth of a second with the wrist turning the
	// edge into the line, follows through past the centre and drifts back to the guard. The free hand counters it.
	FVector KnifeGuardShift = FVector::ZeroVector;
	// Automation: "knifet" holds the cut at X seconds (Y odd: backhand); a negative X lets it go.
	bool bKnifeHeld = false;
	if (const FTransform* Hold = PoseOverrides.Find(TEXT("knifet")))
	{
		bKnifeHeld = Hold->GetLocation().X >= 0.f;
		KnifeTime = bKnifeHeld ? (float)Hold->GetLocation().X : -1.f;
		KnifeSwing = FMath::RoundToInt(Hold->GetLocation().Y);
		// Y 3 or more: the heavy stab.
		bKnifeHeavy = KnifeSwing >= 3;
		if (!bKnifeHeld)
		{
			PoseOverrides.Remove(TEXT("knifet"));
		}
	}
	if (KnifeTime >= 0.f && bKnifeHeavy)
	{
		// Heavy stab: the knife is drawn back beside the shoulder, then driven straight in at the middle of the view
		// with the weight behind it, held a beat, and pulled back out to the guard.
		KnifeTime += bKnifeHeld ? 0.f : Dt;
		const float A = KnifeTime / KnifeStabDuration;
		const float Draw = CampusMath::SmoothStep01(KnifeTime / 0.15f) * (1.f - CampusMath::SmoothStep01((KnifeTime - 0.15f) / (KnifeStabContact - 0.15f)));
		const float Drive = KnifeStrikeWeight(KnifeTime, true);
		const TPair<FRotator, FVector> Back = Tuned(TEXT("knifedraw"), FRotator(16.f, 14.f, 10.f), FVector(-9.f, 4.f, 3.f));
		// Driven in with the blade angled a little across to the left, so it is seen past the fist, not end-on behind it.
		const TPair<FRotator, FVector> In = Tuned(TEXT("knifestab"), FRotator(-14.f, -26.f, -20.f), FVector(15.f, -4.f, -4.f));
		ApplyOffset(Back.Key, Back.Value, Draw);
		ApplyOffset(In.Key, In.Value, Drive);
		KnifeGuardShift = FVector(-6.f, 3.f, 3.f) * Draw + FVector(-4.f, 2.f, -2.f) * Drive;
		if (!bKnifeThrustKicked && KnifeTime >= KnifeStabContact)
		{
			// The lunge: the head goes with the arm.
			bKnifeThrustKicked = true;
			CamImpact.AddImpulse(FVector(7.f, -2.f, 0.f));
			ImpactPos.AddImpulse(FVector(30.f, 0.f, 0.f));
		}
		if (A >= 1.f && !bKnifeHeld)
		{
			KnifeTime = -1.f;
		}
	}
	else if (KnifeTime >= 0.f)
	{
		KnifeTime += bKnifeHeld ? 0.f : Dt;
		const float A = KnifeTime / KnifeSlashDuration;
		const float Wind = CampusMath::SmoothStep01(KnifeTime / 0.06f) * (1.f - CampusMath::SmoothStep01((KnifeTime - 0.06f) / (KnifeSlashContact - 0.06f)));
		const float Through = KnifeStrikeWeight(KnifeTime, false);
		const bool bBackhand = (KnifeSwing & 1) != 0;
		if (!bBackhand)
		{
			const TPair<FRotator, FVector> W0 = Tuned(TEXT("knifewind"), FRotator(22.f, 28.f, 30.f), FVector(-5.f, 7.f, 7.f));
			// Through the line: the blade laid flat pointing left, across the middle of the view, the fist up near centre.
			const TPair<FRotator, FVector> C0 = Tuned(TEXT("knifecut"), FRotator(-6.f, -22.f, -50.f), FVector(10.f, -7.f, -1.f));
			ApplyOffset(W0.Key, W0.Value, Wind);
			ApplyOffset(C0.Key, C0.Value, Through);
		}
		else
		{
			const TPair<FRotator, FVector> W1 = Tuned(TEXT("knifewindb"), FRotator(10.f, -24.f, -46.f), FVector(4.f, -8.f, 2.f));
			// Backhand: the wrist turned palm up, the edge leading out to the right, the blade flat across the view.
			const TPair<FRotator, FVector> C1 = Tuned(TEXT("knifecutb"), FRotator(-4.f, 24.f, 68.f), FVector(9.f, 5.f, -2.f));
			ApplyOffset(W1.Key, W1.Value, Wind);
			ApplyOffset(C1.Key, C1.Value, Through);
		}
		KnifeGuardShift = FVector(-5.f, bBackhand ? -4.f : 5.f, 4.f) * Through;
		if (!bKnifeThrustKicked && KnifeTime >= KnifeSlashContact)
		{
			bKnifeThrustKicked = true;
			const float Side = bBackhand ? 1.f : -1.f;
			CamImpact.AddImpulse(FVector(2.f, 3.f * Side, 1.f * Side));
			ImpactPos.AddImpulse(FVector(12.f, 8.f * Side, -3.f));
		}
		if (A >= 1.f && !bKnifeHeld)
		{
			KnifeTime = -1.f;
		}
	}

	// Grenade: the weapon dips low and rolls out to the right while the support hand draws, primes and throws.
	float ThrowLower = 0.f;
	float ThrowIK = 0.f;
	FVector ThrowHandCam = FVector::ZeroVector;
	FVector ThrowElbowCam = FVector::ZeroVector;
	FQuat ThrowTurnCam = FQuat::Identity;
	float ThrowFingers = 0.f;
	float ThrowThumb = 0.f;
	const bool bThrowing = EvaluateThrow(ThrowLower, ThrowHandCam, ThrowIK, ThrowElbowCam, ThrowTurnCam, ThrowFingers, ThrowThumb);
	if (bThrowing)
	{
		// Lowered and canted in the gun hand, but still in view at the lower right.
		const TPair<FRotator, FVector> Dip = Tuned(TEXT("throwdip"), FRotator(-4.f, 4.f, -10.f), FVector(0.f, 1.f, -2.f));
		ApplyOffset(Dip.Key, Dip.Value, ThrowLower);
	}

	// Mantle: the weapon is tucked down to the right in the gun hand while the support hand reaches out, plants on the
	// lip and pushes the body up over it, then takes the handguard again as the weapon comes back up.
	FVector MantlePlantWorld = FVector::ZeroVector;
	// The head drops to look at the hand on the lip (lower ledges more: they are well below the eye), rolls a little to
	// the planted side, and comes back up level as the body goes over the top.
	float MantleLook = 0.f;
	float MantleRoll = 0.f;
	{
		const bool bMantling = Move && Move->IsMantling();
		const float Hands = 1.f - 0.6f * FMath::Clamp(ReloadWeight.Value, 0.f, 1.f);
		if (bMantling && !bWasMantling)
		{
			MantleHandContactWorld = Move->GetMantleLedge() - Character->FirstPersonCamera->GetRightVector() * 16.f;
			// The hand slaps down on the ledge and the head dips toward it.
			const float Big = FMath::GetMappedRangeValueClamped(FVector2D(40.f, 175.f), FVector2D(0.5f, 1.f), Move->GetMantleHeight());
			ImpactPos.AddImpulse(FVector(-10.f, 0.f, -45.f) * Big * Hands);
			ImpactRot.AddImpulse(FVector(-30.f, 6.f, 22.f) * Big * Hands);
			CamImpact.AddImpulse(FVector(-16.f, 0.f, 7.f) * Big);
			CampusAudio::Play2D(this, TEXT("Footstep"), 0.55f, 0.1f);
		}
		else if (!bMantling && bWasMantling)
		{
			// Over the top: the weight comes onto the feet.
			ImpactPos.AddImpulse(FVector(0.f, 0.f, -28.f * Hands));
			ImpactRot.AddImpulse(FVector(-14.f * Hands, 0.f, -8.f * Hands));
			CamImpact.AddImpulse(FVector(-7.f, 0.f, -4.f));
			CampusAudio::Play2D(this, TEXT("Land"), 0.35f, 0.06f);
		}
		bWasMantling = bMantling;
		if (bMantling)
		{
			const float A = Move->GetMantleAlpha();
			auto Seg = [A](float T0, float T1) { return CampusMath::SmootherStep01((A - T0) / FMath::Max(T1 - T0, 0.001f)); };
			const TPair<FRotator, FVector> Look = Tuned(TEXT("mantlecam"), FRotator(-22.f, 0.f, -3.5f), FVector::ZeroVector);
			const float Low = FMath::GetMappedRangeValueClamped(FVector2D(40.f, 175.f), FVector2D(1.f, 0.35f), Move->GetMantleHeight());
			MantleLookPose = Look.Key.Pitch * FMath::Sin(PI * FMath::Clamp(A / 0.8f, 0.f, 1.f)) * Low;
			MantleRollPose = Look.Key.Roll * FMath::Sin(PI * A);
			MantleTuckPoseAlpha = Seg(0.f, 0.18f) * (1.f - Seg(0.7f, 1.f));
			MantlePlantWorld = MantleHandContactWorld;
		}
		else
		{
			// 攀爬被中断时镜头和持枪姿态也要退回，不能只平滑左手目标
			const float MantlePoseDecay = FMath::Exp(-12.f * Dt);
			MantleLookPose *= MantlePoseDecay;
			MantleRollPose *= MantlePoseDecay;
			MantleTuckPoseAlpha *= MantlePoseDecay;
		}
		MantleLook = MantleLookPose;
		MantleRoll = MantleRollPose;
		ApplyOffset(FRotator(MantleLook * 0.85f, 0.f, 0.f), FVector::ZeroVector, 1.f);
		const TPair<FRotator, FVector> Tuck = Tuned(TEXT("mantletuck"), FRotator(-12.f, 9.f, -8.f), FVector(-2.f, 2.5f, -5.f));
		ApplyOffset(Tuck.Key, Tuck.Value, MantleTuckPoseAlpha);
	}
	// ------------------------------------------------------------------ additive layers
	const float Stability = Stats ? FMath::Max(Stats->AimStability, 0.3f) : 1.f;
	const float ADSDamp = FMath::Lerp(1.f, 0.22f / Stability, ADSPoseAlpha);

	// Look sway is kept small and stiff: the weapon stays on the crosshair through a flick (a trailing weapon reads as
	// the aim itself lagging the mouse), only a slight drag and a quick settle.
	const FVector SwayRotTarget(
		FMath::Clamp(-LookVelocity.Y * 0.004f, -1.8f, 1.8f),
		FMath::Clamp(-LookVelocity.X * 0.0045f, -2.2f, 2.2f),
		FMath::Clamp(LookVelocity.X * 0.0035f, -1.8f, 1.8f));
	SwayRot.Update(SwayRotTarget * ADSDamp, 9.f, 0.8f, Dt);
	const FVector SwayPosTarget(0.f, FMath::Clamp(-LookVelocity.X * 0.0012f, -0.6f, 0.6f), FMath::Clamp(-LookVelocity.Y * 0.0012f, -0.5f, 0.5f));
	SwayPos.Update(SwayPosTarget * ADSDamp, 9.f, 0.8f, Dt);

	const FVector Velocity = Character->GetVelocity();
	const float Speed = Velocity.Size2D();
	const float RunSpeed = Move ? Move->RunSpeed : 470.f;
	const bool bGrounded = !bAir && !bSlide;
	BobWeight.Update(bGrounded ? FMath::Clamp(Speed / RunSpeed, 0.f, 1.8f) : 0.f, 4.f, 1.f, Dt);
	// One BobPhase cycle = a left and a right step (footsteps every 270 cm sprinting, 210 cm running): the sway goes
	// side to side once per stride and the bob dips once per footfall.
	const float StepRate = (bTac || bSprint) ? 1.28f : 1.12f;
	const float RefSpeed = (bTac || bSprint) ? (Move ? Move->SprintSpeed : 680.f) : RunSpeed;
	if (BobWeight.Value > 0.03f)
	{
		BobPhase += Dt * 2.f * PI * StepRate * FMath::Clamp(Speed / RefSpeed, 0.45f, 1.3f);
		BobPhase = FMath::Fmod(BobPhase, 4.f * PI);
	}
	const float S1 = FMath::Sin(BobPhase);
	const float S2 = FMath::Sin(BobPhase * 2.f);
	const float C2 = FMath::Cos(BobPhase * 2.f);
	const float BobAmount = FMath::Max(BobWeight.Value, 0.f) * FMath::Lerp(1.f, 0.12f, ADSPoseAlpha) * (1.f - 0.4f * CrouchAlpha.Value);
	// The tactical sprint carry already rocks with the stride, so it takes less of the generic bob.
	const float SprintBoost = 1.f + 1.3f * SprintAlpha.Value + 0.7f * TacSprintAlpha.Value;
	const FVector BobPos = FVector(S2 * 0.24f, S1 * 0.95f, -FMath::Abs(S1) * 1.05f + 0.52f) * BobAmount * SprintBoost;
	const FVector BobRot = FVector(C2 * 0.7f, S1 * 0.6f, S1 * 1.4f) * BobAmount * SprintBoost;

	// Stepping up onto a sill, a curb or a stair (or down off one): the capsule snaps up to it in one frame, the eyes
	// follow over a few frames and the weapon dips with the step instead of the whole view jolting.
	{
		const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const bool bOnFloor = Move && Move->IsMovingOnGround() && !bSlide;
		if (bOnFloor && bStepFeetValid)
		{
			const float Rise = (float)(Feet.Z - StepFeet.Z);
			const float Across = (float)FVector::Dist2D(Feet, StepFeet);
			// Steeper than any walkable slope: a step, not a ramp.
			if (FMath::Abs(Rise) > 3.f && FMath::Abs(Rise) < 50.f && FMath::Abs(Rise) > Across * 1.1f + 1.f)
			{
				CamHeight.Value -= Rise * 0.85f;
				ImpactPos.AddImpulse(FVector(0.f, 0.f, -FMath::Abs(Rise) * 1.6f));
				ImpactRot.AddImpulse(FVector(FMath::Abs(Rise) * 0.35f, 0.f, 0.f));
			}
		}
		StepFeet = Feet;
		bStepFeetValid = bOnFloor;
	}

	const FVector LocalVel = Character->GetActorTransform().InverseTransformVectorNoScale(Velocity);
	const float TiltScale = FMath::Lerp(1.f, 0.25f, ADSPoseAlpha);
	MoveTiltRot.Update(FVector(-LocalVel.X * 0.0016f, 0.f, LocalVel.Y * 0.006f) * TiltScale, 3.5f, 0.85f, Dt);
	MoveTiltPos.Update(FVector(-LocalVel.X * 0.0035f, -LocalVel.Y * 0.004f, -FMath::Abs(LocalVel.X) * 0.0012f) * TiltScale, 3.5f, 0.85f, Dt);

	// Inertia: the weapon lags a change of pace (setting off, stopping, flipping a strafe) and settles past its rest
	// with a little overshoot, so the hands carry weight instead of being bolted to the camera.
	const FVector LocalAccel = Dt > KINDA_SMALL_NUMBER ? FVector((LocalVel - PrevLocalVel) / Dt).GetClampedToMaxSize(5000.f) : FVector::ZeroVector;
	PrevLocalVel = LocalVel;
	SmoothAccel = FMath::Lerp(SmoothAccel, FVector(LocalAccel.X, LocalAccel.Y, 0.f), CampusMath::ExpAlpha(14.f, Dt));
	const float InertiaScale = FMath::Lerp(1.f, 0.2f, ADSPoseAlpha) * (bGrounded ? 1.f : 0.4f);
	InertiaPos.Update(FVector(-SmoothAccel.X * 0.0006f, -SmoothAccel.Y * 0.0005f, -FMath::Abs(SmoothAccel.X) * 0.0002f).BoundToCube(1.8f) * InertiaScale, 4.2f, 0.42f, Dt);
	InertiaRot.Update(FVector(FMath::Clamp(SmoothAccel.X * 0.0006f, -2.4f, 2.4f), FMath::Clamp(-SmoothAccel.Y * 0.0005f, -2.f, 2.f), FMath::Clamp(SmoothAccel.Y * 0.0011f, -3.5f, 3.5f)) * InertiaScale, 4.2f, 0.42f, Dt);

	const FVector AirPos(0.f, 0.f, 1.2f * AirAlpha.Value * NotADS);
	const FVector AirRot(3.f * AirAlpha.Value * NotADS, 0.f, 0.f);

	// Breathing sway: heavier and quicker when winded, near still while the breath is held, and a tremor once a held
	// breath has run out.
	const UCampusBreathComponent* Breath = Character->BreathComp;
	const float BreathSway = Breath ? Breath->GetSwayScale() : 1.f;
	BreathPhase += Dt * (1.f + (Breath ? 0.9f * Breath->GetExertion01() : 0.f));
	const float BreathScale = FMath::Lerp(1.f, 0.35f / Stability, ADSPoseAlpha) * (1.f - 0.5f * FMath::Clamp(BobWeight.Value, 0.f, 1.f)) * BreathSway;
	FVector BreathRot = FVector(FMath::Sin(BreathPhase * 1.25f) * 0.28f, FMath::Sin(BreathPhase * 0.71f) * 0.22f, FMath::Sin(BreathPhase * 0.53f) * 0.3f) * BreathScale;
	const FVector BreathPos(0.f, 0.f, FMath::Sin(BreathPhase * 1.25f + 0.6f) * 0.12f * BreathScale);
	if (Breath && Breath->GetBreathShake01() > 0.f)
	{
		BreathRot += FVector(FMath::Sin(Time * 17.f), FMath::Sin(Time * 13.3f + 1.f), 0.f) * 0.09f * Breath->GetBreathShake01() * FMath::Lerp(0.6f, 1.f, ADSPoseAlpha);
	}

	RecoilRot.Update(FVector::ZeroVector, 7.5f, 0.42f, Dt);
	RecoilPos.Update(FVector::ZeroVector, 9.0f, 0.55f, Dt);
	RecoilSnapPos.Update(FVector::ZeroVector, 22.f, 0.5f, Dt);
	RecoilClimb.Update(FVector::ZeroVector, 2.6f, 0.92f, Dt);
	ImpactRot.Update(FVector::ZeroVector, 5.f, 0.5f, Dt);
	ImpactPos.Update(FVector::ZeroVector, 5.f, 0.55f, Dt);
	const FVector ClimbPos(-RecoilClimb.Value.X * 0.3f, 0.f, RecoilClimb.Value.X * 0.06f);

	const FVector AddRot = SwayRot.Value + BobRot + MoveTiltRot.Value + InertiaRot.Value + AirRot + BreathRot + RecoilRot.Value + RecoilClimb.Value + ImpactRot.Value;
	const FVector AddPos = SwayPos.Value + BobPos + MoveTiltPos.Value + InertiaPos.Value + AirPos + BreathPos + RecoilPos.Value + RecoilSnapPos.Value + ClimbPos + ImpactPos.Value;

	// 本帧镜头先确定，碰撞约束与手部逆投影才能使用相同的视点和视场角
	CamKick.Update(FVector::ZeroVector, 9.f, 0.55f, Dt);
	CamImpact.Update(FVector::ZeroVector, 4.5f, 0.45f, Dt);
	CamHeight.Update(0.f, 4.f, 0.85f, Dt);
	ShakeAmount = FMath::Max(0.f, ShakeAmount - Dt * 9.f);
	Rumble = FMath::Max(0.f, Rumble - Dt * 1.3f);
	const FVector Shake = FVector(FMath::PerlinNoise1D(Time * 37.f), FMath::PerlinNoise1D(Time * 41.f + 13.f), FMath::PerlinNoise1D(Time * 29.f + 71.f)) * ShakeAmount
		+ FVector(FMath::PerlinNoise1D(Time * 17.f + 5.f), FMath::PerlinNoise1D(Time * 21.f + 9.f), FMath::PerlinNoise1D(Time * 13.f + 31.f)) * Rumble * Rumble * 3.f;
	const float HeadBob = FMath::Clamp(BobWeight.Value, 0.f, 1.5f) * (0.15f + 0.35f * SprintAlpha.Value + 0.55f * TacSprintAlpha.Value) * (1.f - 0.8f * ADSPoseAlpha);
	const float EyeHeight = Character->bIsCrouched ? Character->CrouchedEyeHeight : Character->BaseEyeHeight;
	Character->FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight + CamHeight.Value + S2 * HeadBob * 0.6f));
	CameraLocationOffset = FVector::ZeroVector;
	const float ReloadLook = Tuned(TEXT("reloadcam"), FRotator(Def && Def->bPistolAnims ? -3.f : -4.f, 0.f, 0.f), FVector::ZeroVector).Key.Pitch;
	CameraRotationOffset = FRotator(
		CamKick.Value.X + CamImpact.Value.X + Shake.X * 0.6f + ReloadLook * FMath::Clamp(ReloadWeight.Value, 0.f, 1.f) + MantleLook,
		CamKick.Value.Y + CamImpact.Value.Y + Shake.Y * 0.6f,
		CamKick.Value.Z + CamImpact.Value.Z + Shake.Z * 0.4f + SlideAlpha.Value * 4.5f * (1.f - 0.65f * ADSPoseAlpha) + S1 * HeadBob * 0.25f + FMath::Clamp(LocalVel.Y * 0.0012f, -0.8f, 0.8f) * NotADS + MantleRoll);
	FOVKick.Update(0.f, 6.f, 0.7f, Dt);
	const bool bScope = Weapon && Weapon->HasScopeOptic();
	const float MainZoomTarget = Stats ? (bScope ? FMath::Min(Stats->ADSZoom, ScopeMainZoom) : FMath::Max(Stats->ADSZoom, 1.f)) : 1.f;
	const float ViewADS = CampusMath::SmoothStep01(Character->GetADSAlpha());
	const float Zoom = FMath::Lerp(1.f, MainZoomTarget, ViewADS);
	const float BaseFOV = Character->BaseFOV + 3.f * SprintAlpha.Value + 7.f * TacSprintAlpha.Value + 6.f * SlideAlpha.Value * NotADS;
	CurrentFOV = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(BaseFOV * 0.5f)) / Zoom)) + FOVKick.Value;
	CurrentViewmodelFOV = Character->BaseViewmodelFOV - 6.f * ViewADS + 2.f * SprintAlpha.Value;
	Character->FirstPersonCamera->SetFieldOfView(CurrentFOV);
	Character->FirstPersonCamera->FirstPersonFieldOfView = CurrentViewmodelFOV;

	FTransform Final = CampusMath::RotateAround(Pose, FQuat(FRotator(AddRot.X, AddRot.Y, AddRot.Z)), Pose.TransformPosition(CalibGrip));
	Final.AddToTranslation(AddPos);
	ConstrainWeaponPose(Weapon, Final, Dt);
	// The body stays on the sight-aligned pose and every procedural layer moves only the hands (and the weapon they
	// hold), so the shoulders and upper arms never swing into the camera. Off the sight the authored cheek weld
	// relaxes: the shoulders drop and level out, so the support arm rises to the handguard from below the frame
	// instead of lying straight across the bottom of it.
	// Racking a charging handle the support arm reaches right across the weapon: the shoulders drop and draw back
	// under the eye (the hands stay where they are), so the arm comes up into the view from below rather than across
	// the lens.
	const FVector RackBody = Tuned(TEXT("rackbody"), FRotator::ZeroRotator, FVector(-10.f, -6.f, -22.f)).Value * FMath::Clamp(RackWeight.Value, 0.f, 1.f);
	const FTransform BodyPose(FRotator(0.f, 0.f, -10.f * NotADS).Quaternion() * ADSPose.GetRotation(), ADSPose.GetLocation() - FVector(0.f, 0.f, 9.f * NotADS) + RackBody);
	Character->ViewmodelPivot->SetRelativeTransform(BodyPose);
	const FTransform ArmsRel = Character->ArmsMesh->GetRelativeTransform();
	HandsOffset = ArmsRel * Final * BodyPose.Inverse() * ArmsRel.Inverse();

	// ------------------------------------------------------------------ support hand IK
	if (UCampusFPAnimInstance* ArmsAnim = Character->GetArmsAnim())
	{
		USkeletalMeshComponent* Arms = Character->ArmsMesh;
		const float KnifeElbow = Def && Def->IsMelee() && KnifeTime >= 0.f ? KnifeStrikeWeight(KnifeTime, bKnifeHeavy) : 0.f;
		// 刀具从画面下方向前送，右肘不再沿举枪姿态横扫镜头
		const FVector RightPoleWorld = Character->FirstPersonCamera->GetComponentTransform().TransformVectorNoScale(FVector(-10.f, 12.f, -45.f) * KnifeElbow);
		ArmsAnim->SetRightElbowPole(Arms->GetComponentTransform().InverseTransformVectorNoScale(RightPoleWorld));
		USceneComponent* Frame = Weapon ? Weapon->GetFPFrame() : nullptr;
		const int32 HandR = Arms->GetBoneIndex(TEXT("hand_r"));
		float IKTarget = 0.f;
		float ReloadFingerCurl = 0.f;
		FVector Effector = FVector::ZeroVector;
		FVector PoleCS = FVector::ZeroVector;
		FQuat TurnCS = FQuat::Identity;
		float TurnWeight = 0.f;
		if (Def && Frame && HandR != INDEX_NONE && (Def->bLeftHandIK || ReloadTime >= 0.f || bWasReloadPath || ReloadReturnTime >= 0.f
			|| (bSmoothLeftHandInit && LeftHandIKAlpha > 0.001f && !Def->IsMelee())))
		{
			const FTransform PreviousRight = Arms->GetBoneTransform(HandR);
			const FTransform HandRWorld = ArmsAnim->GetOffsetHandR(HandsOffset) * Arms->GetComponentTransform();
			// 弹袋属于相机空间，武器抓点属于骨骼空间，两者都要使用本帧双手偏移后的坐标
			const FTransform FrameT = Frame->GetComponentTransform().GetRelativeTransform(PreviousRight) * HandRWorld;
			const FTransform* GripOverride = PoseOverrides.Find(TEXT("lhand"));
			// A foregrip is held instead of the handguard (the hand would otherwise sit on top of it).
			const FCampusAttachmentDef* Foregrip = Def->bLeftHandIK && Def->Model.Num() > 0 ? Weapon->GetForegrip() : nullptr;
			// Pistols take the two-handed grip from the animation; their reload starts from under the grip.
			const FVector LeftGrip = GripOverride ? GripOverride->GetLocation()
				: Foregrip ? Def->GetAnchor(ECampusAttachmentSlot::Underbarrel) + Foregrip->HandGrip + Tuned(TEXT("fgrip"), FRotator::ZeroRotator, FVector::ZeroVector).Value
				: (Def->bLeftHandIK ? Def->LeftHandGrip * (Def->Model.Num() > 0 ? 1.f : Def->MeshScale.X) : FVector(-2.f, -4.5f, -2.f));
			FVector HandFrame = LeftGrip;
			FVector MagOffset = FVector::ZeroVector;
			FRotator MagRot = FRotator::ZeroRotator;
			FVector MagPivot = FVector::ZeroVector;
			bool bMagVisible = true;
			float ReloadIK = 1.f;
			float MagGrip = 0.f;
			float RackGrip = 0.f;
			const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
			float DropTarget = 0.f;
			const bool bReloadPath = EvaluateReload(Weapon, Def, LeftGrip, FrameT, CamT, HandFrame, MagOffset, MagRot, MagPivot, bMagVisible, ReloadIK, MagGrip, RackGrip, DropTarget);
			ReloadFingerCurl = bReloadPath ? -0.2f * MagGrip : 0.f;
			Weapon->SetReloadMagazine(MagOffset, MagRot, MagPivot, bMagVisible);
			// Keep-out for the support arm: wherever the left elbow ended up last frame, if it is in the picture and close
			// to the lens (a hand pulled in to the magazine well folds the arm, and a jump, landing or slide shoves the
			// weapon back toward the body and folds it further), the elbow is pulled down until it drops out of the frame,
			// then let back slowly so it settles at the edge instead of bouncing.
			{
				const FVector Elbow = CamT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("lowerarm_l")));
				const FVector Wrist = CamT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("hand_l")));
				const float TanH = FMath::Tan(FMath::DegreesToRadians(CurrentViewmodelFOV * 0.5f));
				const float TanV = TanH / FMath::Max(ViewAspect(), 1.f);
				// Height above the bottom edge of the frame (as a fraction of the half-height), at the elbow and half-way
				// down the forearm.
				auto AboveBottom = [TanV](const FVector& P) { return P.X > 2.f ? P.Z / (P.X * TanV) + 1.f : -1.f; };
				const FVector MidForearm = (Elbow + Wrist) * 0.5f;
				const float Inside = FMath::Max(AboveBottom(Elbow), AboveBottom(MidForearm) * 0.7f);
				const bool bNear = FMath::Min(Elbow.X, MidForearm.X) < 38.f;
				const bool bArmBusy = bReloadPath || ReloadWeight.Value > 0.05f;
				if (bArmBusy && bNear && Inside > -0.05f)
				{
					ElbowGuard = FMath::Min(ElbowGuard + Dt * (3.f + 10.f * FMath::Clamp(Inside + 0.05f, 0.f, 1.f)), 1.f);
				}
				else if (!bArmBusy || !bNear || Inside < -0.25f)
				{
					ElbowGuard = FMath::Max(ElbowGuard - Dt * (bArmBusy ? 0.8f : 3.f), 0.f);
				}
			}
			// Through the whole magazine change the elbow is carried low and in, as it is in a real reload; only the
			// stretch at the pouch takes it all the way down.
			const float ReloadElbow = bReloadPath ? 0.55f * FMath::Clamp(ReloadWeight.Value, 0.f, 1.f) : 0.f;
			ElbowDrop = FMath::Lerp(ElbowDrop, FMath::Max3(DropTarget, ReloadElbow, ElbowGuard), CampusMath::ExpAlpha(14.f, Dt));
			{
				// Pole pulled down and out (camera space), so the elbow swings below the frame instead of up beside it.
				const FVector Drop = CamT.TransformVectorNoScale(Tuned(TEXT("elbowdrop"), FRotator::ZeroRotator, FVector(-6.f, -12.f, -45.f)).Value) * ElbowDrop;
				PoleCS = Arms->GetComponentTransform().InverseTransformVectorNoScale(Drop);
			}
			// Support-hand wrist turn, authored in camera space (pitch / yaw / roll as seen by the player) and carried into
			// the arms' component space: the palm comes round onto the magazine, then onto the charging handle.
			{
				const FQuat MagTurn = Tuned(TEXT("rlhandmag"), Def->bPistolAnims ? FRotator(10.f, 0.f, 10.f) : FRotator(25.f, 15.f, 15.f), FVector::ZeroVector).Key.Quaternion();
				// A handle on the right (AK) is racked over the top: the palm comes down onto the receiver and the fingers
				// hook round its far side.
				FVector RackHandle;
				float RackSide = -1.f;
				const bool bRightHandle = Weapon->GetRackHandle(RackHandle, RackSide) && RackSide > 0.f;
				// With an optic on top, the right-side handle is racked from the side instead: palm toward the receiver,
				// fingers hooked up over the handle (solved with Tools/hand_turn.py).
				const FRotator RightTurn = Weapon->HasOptic() ? FRotator(46.6f, -9.2f, -141.3f) : FRotator(50.f, 0.f, 140.f);
				const FQuat RackTurn = Tuned(TEXT("rlhandrack"), bRightHandle ? RightTurn : FRotator(0.f, 20.f, 35.f), FVector::ZeroVector).Key.Quaternion();
				// On a foregrip the wrist is turned onto it, and comes off it while the hand is away for the magazine.
				const FQuat GripTurn = Foregrip ? Tuned(TEXT("fgripturn"), Foregrip->HandTurn, FVector::ZeroVector).Key.Quaternion() : FQuat::Identity;
				const float OnGrip = (bReloadPath ? 1.f - FMath::Max3(MagGrip, RackGrip, DropTarget) : 1.f) * (1.f - CampusMath::SmoothStep01(TacSprintAlpha.Value / 0.22f) * NotADS);
				const FQuat CamDelta = FQuat::Slerp(FQuat::Identity, RackTurn, RackGrip) * FQuat::Slerp(FQuat::Identity, MagTurn, MagGrip) * FQuat::Slerp(FQuat::Identity, GripTurn, OnGrip);
				const FQuat Qc = CamT.GetRotation().Inverse() * Arms->GetComponentQuat();
				TurnCS = Qc.Inverse() * CamDelta * Qc;
				TurnWeight = 1.f;
			}
			// a cancelled reload returns around the receiver rather than taking a straight shortcut through it
			if (!bSmoothLeftHandInit)
			{
				SmoothLeftHand = HandFrame;
				bSmoothLeftHandInit = true;
			}
			if (bWasReloadPath && !bReloadPath)
			{
				ReloadReturnStart = SmoothLeftHand;
				ReloadReturnTurn = ReloadLastTurn;
				ReloadReturnFingers = ReloadLastFingers;
				ReloadReturnTime = 0.f;
			}
			bWasReloadPath = bReloadPath;
			if (bReloadPath)
			{
				ReloadReturnTime = -1.f;
				SmoothLeftHand = HandFrame;
				ReloadLastTurn = TurnCS;
				ReloadLastFingers = ReloadFingerCurl;
			}
			else if (ReloadReturnTime >= 0.f)
			{
				ReloadReturnTime += Dt;
				SmoothLeftHand = ReloadReach(ReloadReturnStart, HandFrame, ReloadReturnTime / 0.22f, ReloadSideClearance);
				const float Return = CampusMath::SmootherStep01(ReloadReturnTime / 0.22f);
				TurnCS = FQuat::Slerp(ReloadReturnTurn, TurnCS, Return).GetNormalized();
				ReloadFingerCurl = FMath::Lerp(ReloadReturnFingers, 0.f, Return);
				if (ReloadReturnTime >= 0.22f)
				{
					ReloadReturnTime = -1.f;
				}
			}
			else
			{
				SmoothLeftHand = FMath::Lerp(SmoothLeftHand, HandFrame, CampusMath::ExpAlpha(13.f, Dt));
			}
			Effector = HandRWorld.InverseTransformPosition(FrameT.TransformPosition(SmoothLeftHand));
			IKTarget = bReloadPath ? ReloadIK : (Def->bLeftHandIK || ReloadReturnTime >= 0.f ? 1.f : 0.f);
			// Tactical sprint: the support hand leaves the weapon and pumps with the run, arm kept long so the elbow
			// (which bends toward the animated one, up under the handguard) never swings into the frame. It lets go
			// before the weapon starts to rise and takes hold again only once it is nearly back down, so it never
			// follows the handguard up through the view. The regular sprint keeps it on the handguard.
			const float Pump = CampusMath::SmoothStep01(TacSprintAlpha.Value / 0.22f) * NotADS;
			if (Pump > 0.01f && Def->bLeftHandIK && !bReloadPath)
			{
				const float Swing = FMath::Sin(BobPhase);
				const FVector PumpCam(21.f + 19.f * Swing, -16.f + 2.f * Swing, -48.f + 4.f * Swing);
				const FVector PumpWorld = Character->FirstPersonCamera->GetComponentTransform().TransformPosition(PumpCam);
				Effector = FMath::Lerp(Effector, HandRWorld.InverseTransformPosition(PumpWorld), Pump);
			}
			if (HolsterTime >= 0.f)
			{
				IKTarget = 0.f;
			}
			if (EquipTime >= 0.f)
			{
				IKTarget *= CampusMath::SmoothStep01((EquipTime / FMath::Max(EquipDuration, 0.01f) - 0.35f) / 0.4f);
			}
			if (bThrowing && ThrowIK > 0.f)
			{
				// The support hand leaves the handguard for the grenade: its path is authored in camera space.
				Effector = FMath::Lerp(Effector, HandRWorld.InverseTransformPosition(CamT.TransformPosition(ThrowHandCam)), ThrowIK);
				IKTarget = 1.f;
				PoleCS = Arms->GetComponentTransform().InverseTransformVectorNoScale(CamT.TransformVectorNoScale(ThrowElbowCam * ThrowIK));
				const FQuat Qc = CamT.GetRotation().Inverse() * Arms->GetComponentQuat();
				TurnCS = Qc.Inverse() * FQuat::Slerp(FQuat::Identity, ThrowTurnCam, ThrowIK) * Qc;
				TurnWeight = 1.f;
			}
		}
		else if (Def && HandR != INDEX_NONE)
		{
			// Weapons the animation holds (pistols) or that need one hand (a knife): the support hand only goes
			// somewhere of its own beside a blade (a guard low left), for a grenade or onto a ledge.
			ElbowDrop = 0.f;
			ElbowGuard = 0.f;
			const FTransform HandRWorld = ArmsAnim->GetOffsetHandR(HandsOffset) * Arms->GetComponentTransform();
			const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
			const FQuat Qc = CamT.GetRotation().Inverse() * Arms->GetComponentQuat();
			FVector PoleCam = FVector::ZeroVector;
			FQuat TurnCam = FQuat::Identity;
			float TurnAlpha = 0.f;
			auto Reach = [&](const FVector& Cam, float Alpha)
			{
				const FVector Target = HandRWorld.InverseTransformPosition(CamT.TransformPosition(Cam));
				Effector = IKTarget > 0.f ? FMath::Lerp(Effector, Target, Alpha) : Target;
				IKTarget = FMath::Max(IKTarget, Alpha);
			};
			if (Def->IsMelee())
			{
				// Fist up at the bottom left of the frame, elbow tucked: it rides the bob and swings against each cut.
				const FVector Guard = Tuned(TEXT("knifeguard"), FRotator::ZeroRotator, FVector(26.f, -17.f, -31.f)).Value;
				Reach(Guard + KnifeGuardShift + BobPos * 0.6f, HolsterTime >= 0.f ? 0.f : 1.f);
				PoleCam = FVector(-6.f, -12.f, -45.f);
			}
			if (bThrowing && ThrowIK > 0.f)
			{
				Reach(ThrowHandCam, ThrowIK);
				PoleCam = FMath::Lerp(PoleCam, ThrowElbowCam, ThrowIK);
				TurnCam = ThrowTurnCam;
				TurnAlpha = ThrowIK;
			}
			PoleCS = Arms->GetComponentTransform().InverseTransformVectorNoScale(CamT.TransformVectorNoScale(PoleCam));
			TurnCS = Qc.Inverse() * FQuat::Slerp(FQuat::Identity, TurnCam, TurnAlpha) * Qc;
			TurnWeight = TurnAlpha > 0.f ? 1.f : 0.f;
			if (Weapon)
			{
				Weapon->SetReloadMagazine(FVector::ZeroVector, FRotator::ZeroRotator, FVector::ZeroVector, true);
			}
		}
		else
		{
			ElbowDrop = 0.f;
			ElbowGuard = 0.f;
			if (Weapon)
			{
				Weapon->SetReloadMagazine(FVector::ZeroVector, FRotator::ZeroRotator, FVector::ZeroVector, true);
			}
		}
		const bool bMantling = Move && Move->IsMantling();
		if (HandR != INDEX_NONE)
		{
			const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
			const FTransform HandRWorld = Arms->GetBoneTransform(HandR);
			FMinimalViewInfo HandView;
			HandView.Location = CamT.GetLocation();
			HandView.Rotation = (CamT.GetRotation() * CameraRotationOffset.Quaternion()).Rotator();
			HandView.FOV = CurrentFOV;
			HandView.FirstPersonFOV = Character->FirstPersonCamera->bEnableFirstPersonFieldOfView ? CurrentViewmodelFOV : CurrentFOV;
			HandView.FirstPersonScale = Character->FirstPersonCamera->bEnableFirstPersonScale ? Character->FirstPersonCamera->FirstPersonScale : 1.f;
			const FVector MantleHandCam = CamT.InverseTransformPositionNoScale(HandView.TransformFirstPersonToWorld(MantlePlantWorld, false));
			if (bMantling && !bMantleHandWasMoving)
			{
				bMantleHandActive = true;
				bMantleHandReturning = false;
				bMantleHandHasRest = true;
				MantleHandTime = 0.f;
				MantleHandAlpha = 0.f;
				MantleHandCamera = CamT.InverseTransformPositionNoScale(Arms->GetBoneLocation(TEXT("hand_l")));
				MantleHandStartCamera = MantleHandCamera;
				MantleRestEffector = HandRWorld.InverseTransformPosition(Arms->GetBoneLocation(TEXT("hand_l")));
			}
			// 手枪的普通握姿没有独立 IK，残余权重消退前仍要保留有效的回握目标
			if (bMantleHandHasRest && IKTarget <= 0.f && (bMantleHandActive || LeftHandIKAlpha > 0.001f))
			{
				Effector = MantleRestEffector;
			}
			if (bMantleHandActive)
			{
				constexpr float ReachDuration = 0.2f;
				constexpr float ReturnDuration = 0.2f;
				constexpr float UnsupportedReachTime = 0.15f;
				constexpr float HandTravelSpeed = 240.f;
				const FVector BaseCam = CamT.InverseTransformPositionNoScale(HandRWorld.TransformPosition(Effector));
				const FVector Shoulder = Arms->GetBoneLocation(TEXT("upperarm_l"));
				const FVector Elbow = Arms->GetBoneLocation(TEXT("lowerarm_l"));
				const FVector Wrist = Arms->GetBoneLocation(TEXT("hand_l"));
				const double Reach = (FVector::Dist(Shoulder, Elbow) + FVector::Dist(Elbow, Wrist)) * 1.02;
				const FVector ToPlant = CamT.TransformPosition(MantleHandCam) - Shoulder;
				const bool bReachable = bMantling && MantleHandCam.X > 4.f && ToPlant.SizeSquared() <= FMath::Square(Reach);
				if (!bMantleHandReturning && (!bMantling || bThrowing || ReloadTime >= 0.f || HolsterTime >= 0.f || Move->GetMantleAlpha() >= 0.65f
					|| (!bReachable && MantleHandTime >= UnsupportedReachTime)))
				{
					bMantleHandReturning = true;
					MantleHandTime = 0.f;
					MantleHandStartCamera = MantleHandCamera;
					MantleHandReturnAlpha = MantleHandAlpha;
				}
				MantleHandTime += Dt;
				FVector Desired;
				if (bMantleHandReturning)
				{
					const float Return = CampusMath::SmootherStep01(MantleHandTime / ReturnDuration);
					MantleHandAlpha = MantleHandReturnAlpha * (1.f - Return);
					Desired = FMath::Lerp(MantleHandStartCamera, BaseCam, Return);
				}
				else
				{
					MantleHandAlpha = CampusMath::SmootherStep01(MantleHandTime / ReachDuration);
					// 够不到的台沿只做自然的伸手尝试，不把目标钉在屏幕边缘伪装成撑住
					const FVector PlantCam = CamT.InverseTransformPositionNoScale(Shoulder + ToPlant.GetClampedToMaxSize(Reach));
					Desired = FMath::Lerp(MantleHandStartCamera, PlantCam, MantleHandAlpha);
				}
				MantleHandCamera += (Desired - MantleHandCamera).GetClampedToMaxSize(HandTravelSpeed * Dt);
				// 动画先应用本帧双手偏移，再解左手 IK，目标必须使用同一帧的右手坐标系
				const FTransform RightAfterOffset = ArmsAnim->GetOffsetHandR(HandsOffset) * Arms->GetComponentTransform();
				Effector = RightAfterOffset.InverseTransformPosition(CamT.TransformPosition(MantleHandCamera));
				IKTarget = FMath::Max(IKTarget, MantleHandAlpha);
				const TPair<FRotator, FVector> Turn = Tuned(TEXT("mantleturn"), FRotator(-15.4f, 38.4f, 130.f), FVector(-4.f, -26.f, -26.f));
				const FQuat Qc = CamT.GetRotation().Inverse() * Arms->GetComponentQuat();
				const FQuat PlantTurn = Qc.Inverse() * Turn.Key.Quaternion() * Qc;
				TurnCS = FQuat::Slerp(FQuat::Slerp(FQuat::Identity, TurnCS, TurnWeight), PlantTurn, MantleHandAlpha).GetNormalized();
				TurnWeight = 1.f;
				const FVector PlantPole = Arms->GetComponentTransform().InverseTransformVectorNoScale(CamT.TransformVectorNoScale(Turn.Value));
				PoleCS = FMath::Lerp(PoleCS, PlantPole, MantleHandAlpha);
				if (bMantleHandReturning && MantleHandTime >= ReturnDuration && FVector::DistSquared(MantleHandCamera, BaseCam) < 1.f)
				{
					bMantleHandActive = false;
				}
			}
		}
		bMantleHandWasMoving = bMantling;
		LeftHandIKAlpha = FMath::Lerp(LeftHandIKAlpha, IKTarget, CampusMath::ExpAlpha(bThrowing || bMantleHandActive ? 30.f : 16.f, Dt));
		ArmsAnim->SetLeftElbowPole(PoleCS);
		ArmsAnim->SetLeftHandTurn(TurnCS, TurnWeight);
		ArmsAnim->SetLeftHandIK(Effector, LeftHandIKAlpha);
		ArmsAnim->SetLeftFingers(FMath::Lerp(bThrowing ? ThrowFingers : ReloadFingerCurl, 0.85f, MantleHandAlpha),
			FMath::Lerp(bThrowing ? ThrowThumb : 0.f, 0.6f, MantleHandAlpha));
		ArmsAnim->SetHandsOffset(HandsOffset);
	}
	if (bThrowing)
	{
		PlaceHandGrenade();
	}

	UpdateFlashPost();
	const ACampusPlayerController* PC = Cast<ACampusPlayerController>(Character->GetController());
	// The hands come into view on the final flare, the weapon already rising, so it is up as the boots touch down.
	const bool bIntro = PC && PC->IsIntroActive() && PC->GetIntroAlpha() < 0.88f;
	UpdateScopeView(Weapon, Stats, Character->GetADSAlpha(), Zoom);
	bool bHideFP = bIntro;
	if (bFPHidden && !bHideFP)
	{
		if (RevealFrames < 0)
		{
			// Touchdown from the deploy drop: a soft give in the knees, the weapon brought up from low ready.
			RaiseFromLowReady(0.55f);
			FTimerHandle Handle;
			GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() { CampusAudio::Play2D(this, TEXT("Land"), 0.8f, 0.04f); }), 0.3f, false);
			RevealFrames = 4;
		}
		// The arms stayed unposed while hidden: they are shown only once the animation and the hand IK have run on
		// the new pose for a few frames, never in whatever they held before.
		if (--RevealFrames > 0)
		{
			bHideFP = true;
		}
	}
	else if (bHideFP)
	{
		RevealFrames = -1;
	}
	if (bHideFP != bFPHidden || (Weapon && Weapon != HiddenWeapon.Get()))
	{
		bFPHidden = bHideFP;
		HiddenWeapon = Weapon;
		Character->ArmsMesh->SetVisibility(!bHideFP, false);
		if (Weapon)
		{
			Weapon->SetFirstPersonHidden(bHideFP);
		}
	}
}

bool UCampusViewmodelComponent::EvaluateReload(const ACampusWeapon* Weapon, const FCampusWeaponDef* Def, const FVector& HandGrip, const FTransform& FrameT, const FTransform& CamT, FVector& OutHand, FVector& OutMagOffset,
	FRotator& OutMagRot, FVector& OutMagPivot, bool& bOutMagVisible, float& OutIK, float& OutMagGrip, float& OutRackGrip, float& OutElbowDrop) const
{
	OutMagGrip = 0.f;
	OutRackGrip = 0.f;
	OutElbowDrop = 0.f;
	OutHand = HandGrip;
	OutMagOffset = FVector::ZeroVector;
	OutMagRot = FRotator::ZeroRotator;
	OutMagPivot = FVector::ZeroVector;
	bOutMagVisible = true;
	OutIK = 1.f;
	FBox Mag(ForceInit);
	if (ReloadTime < 0.f || !Weapon || !Def || !Weapon->GetMagazineBox(Mag))
	{
		return false;
	}
	// Magazines turn about the front of their lip: rocked forward to unhook, hooked in front-first and rocked back.
	OutMagPivot = FVector(Mag.Max.X, Mag.GetCenter().Y, Mag.Max.Z);
	const float RockOut = Def->bPistolAnims ? 0.f : (Def->bRockInMagazine ? 20.f : 6.f);
	const float RockIn = Def->bPistolAnims ? 0.f : (Def->bRockInMagazine ? 22.f : 5.f);
	const float A = FMath::Clamp(ReloadTime / FMath::Max(ReloadDuration, 0.1f), 0.f, 1.f);
	const float I = FMath::Clamp(Weapon->GetStats().ReloadInsertFraction, 0.5f, 0.8f);
	const bool bPistol = Def->bPistolAnims;
	// Wrist targets: hand_l sits a few centimetres off where the palm touches.
	const float Out = Def->ReloadHandOut;
	const FVector Seat = bPistol
		? FVector(Mag.GetCenter().X, Mag.GetCenter().Y - 2.f, Mag.Min.Z - 5.f)
		: FVector(Mag.GetCenter().X - 0.5f, FMath::Min(static_cast<float>(Mag.Min.Y) - 3.4f - Out, ReloadSideClearance), Mag.Max.Z - Mag.GetSize().Z * 0.3f - 2.f);
	const FVector Drawn = Seat + (bPistol ? FVector(0.f, 0.f, -12.f) : FVector(1.f, -3.f, -9.f));
	const FVector Lined = Seat + (bPistol ? FVector(0.f, 0.f, -4.5f) : FVector(0.3f, -0.4f, -6.f));
	const FVector Pouch = FrameT.InverseTransformPosition(CamT.TransformPosition(FVector(14.f, -18.f, -46.f)));
	// Empty rifle reloads finish by slapping the bolt catch above the magazine well, or by racking the charging handle.
	FVector Catch = bPistol ? Seat : FVector(Mag.Max.X - 1.f, Mag.Min.Y - 3.2f - Out, Mag.Max.Z + 3.f);
	if (!bPistol)
	{
		Catch.Y = FMath::Min(static_cast<float>(Catch.Y), ReloadSideClearance);
	}
	FVector Handle;
	float HandleSide = 1.f;
	const bool bRack = !bPistol && Def->bRackHandle && Weapon->GetRackHandle(Handle, HandleSide) && (HandleSide < 0.f || !Weapon->HasOptic() || Def->bRackUnderOptic);
	if (bRack)
	{
		// The palm wraps the handle from outside and a little below it. Under an optic the hand stays beside the
		// receiver, lower, and racks from the side: over the top it would pass through the sight.
		const bool bUnderOptic = HandleSide > 0.f && Weapon->HasOptic();
		Catch = Handle + (bUnderOptic ? FVector(-1.5f, HandleSide * 5.5f, -5.5f) : FVector(-1.5f, HandleSide * 4.2f, -2.5f));
	}
	auto Seg = [A](float T0, float T1) { return CampusMath::SmootherStep01((A - T0) / FMath::Max(T1 - T0, 0.001f)); };
	bool bCarry = false;
	float MagPitch = 0.f;
	FVector Hand;
	if (A < 0.10f)
	{
		Hand = bPistol ? FMath::Lerp(HandGrip, Seat, Seg(0.f, 0.10f)) : ReloadReach(HandGrip, Seat, A / 0.10f, ReloadSideClearance);
	}
	else if (A < 0.16f)
	{
		Hand = Seat;
	}
	else if (A < 0.27f)
	{
		// Release pressed, the old magazine is rocked off its catch and drawn out of the well.
		Hand = FMath::Lerp(Seat, Drawn, Seg(0.16f, 0.27f));
		bCarry = true;
		MagPitch = RockOut * Seg(0.16f, 0.22f);
	}
	else if (A < 0.38f)
	{
		// Stowed in the dump pouch, out of view.
		Hand = FMath::Lerp(Drawn, Pouch, Seg(0.27f, 0.38f));
		bCarry = true;
		MagPitch = RockOut + 25.f * Seg(0.27f, 0.38f);
		bOutMagVisible = A < 0.34f;
	}
	else if (A < I - 0.18f)
	{
		Hand = Pouch;
		bOutMagVisible = false;
	}
	else if (A < I - 0.06f)
	{
		// The fresh magazine comes up already tipped to hook in at the front, and lines up under the well.
		Hand = FMath::Lerp(Pouch, Lined, Seg(I - 0.18f, I - 0.06f));
		bCarry = true;
		MagPitch = RockIn + 20.f * (1.f - Seg(I - 0.18f, I - 0.08f));
	}
	else if (A < I)
	{
		// Up into the well, then rocked back until it clicks.
		Hand = FMath::Lerp(Lined, Seat, Seg(I - 0.06f, I - 0.025f));
		bCarry = true;
		MagPitch = RockIn * (1.f - Seg(I - 0.035f, I));
	}
	else if (A < I + 0.05f)
	{
		// Palm slap to seat it.
		Hand = Seat + FVector(0.f, 0.f, 1.3f * FMath::Sin(PI * (A - I) / 0.05f));
	}
	else if (!bReloadEmpty || bPistol)
	{
		Hand = bPistol ? FMath::Lerp(Seat, HandGrip, Seg(I + 0.05f, I + 0.2f)) : ReloadReach(Seat, HandGrip, (A - I - 0.05f) / 0.15f, ReloadSideClearance);
	}
	else if (bRack)
	{
		// Reach to the handle, rack it fully back with it, let it fly home and return to the handguard.
		if (A < 0.80f)
		{
			Hand = ReloadReach(Seat, Catch, (A - I - 0.05f) / FMath::Max(0.74f - I, 0.01f), ReloadSideClearance);
		}
		else if (A < 0.89f)
		{
			Hand = Catch - FVector(6.5f * Seg(0.80f, 0.86f), 0.f, 0.f);
		}
		else
		{
			Hand = ReloadReach(Catch - FVector(6.5f, 0.f, 0.f), HandGrip, (A - 0.89f) / 0.09f, ReloadSideClearance);
		}
	}
	else if (A < 0.80f)
	{
		Hand = ReloadReach(Seat, Catch, (A - I - 0.05f) / FMath::Max(0.75f - I, 0.01f), ReloadSideClearance);
	}
	else if (A < 0.86f)
	{
		Hand = Catch - FVector(0.f, 0.f, FMath::Sin(PI * (A - 0.80f) / 0.06f));
	}
	else
	{
		Hand = ReloadReach(Catch, HandGrip, (A - 0.86f) / 0.11f, ReloadSideClearance);
	}
	OutHand = Hand;
	if (bCarry)
	{
		OutMagRot = FRotator(MagPitch, 0.f, 0.f);
		// 弹匣绕唇口旋转后仍要把同一个抓点放在手里，不能只补平移
		OutMagOffset = Hand - (OutMagPivot + OutMagRot.RotateVector(Seat - OutMagPivot));
	}
	// How the wrist is turned: wrapped round the magazine from the reach to the slap, then round the charging handle.
	const float MagRelease = !bReloadEmpty || bPistol ? Seg(I + 0.05f, I + 0.18f) : (bRack ? Seg(I + 0.06f, 0.72f) : Seg(I + 0.05f, I + 0.15f));
	OutMagGrip = Seg(0.02f, 0.10f) * (1.f - MagRelease);
	// From drawing the old magazine to lining up the new one the hand works low and close to the body, where the bent
	// arm would push the elbow up into the left edge of the view.
	OutElbowDrop = Seg(0.17f, 0.27f) * (1.f - Seg(I - 0.06f, I - 0.01f));
	OutRackGrip = bRack && bReloadEmpty ? Seg(0.70f, 0.79f) * (1.f - Seg(0.89f, 0.97f)) : 0.f;
	// Weapons gripped two-handed by the animation (pistols) blend into the path and back out of it.
	OutIK = Def->bLeftHandIK ? 1.f : Seg(0.f, 0.08f) * (1.f - Seg(0.9f, 1.f));
	return true;
}

bool UCampusViewmodelComponent::GetScopeEyepieceScreen(FVector2D& OutCenterUV, float& OutRadius) const
{
	if (GetWallObstructionAlpha() > 0.08f)
	{
		return false;
	}
	const ACampusWeapon* Weapon = Character ? Character->GetCurrentWeapon() : nullptr;
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	FVector Lens;
	float LensRadius = 0.f;
	if (!Weapon || !PC || !PC->PlayerCameraManager || !Character->FirstPersonCamera || !Weapon->GetScopeEyepiece(Lens, LensRadius))
	{
		return false;
	}
	// First-person primitives project with the viewmodel FOV. Measured from the camera component, which moves with the
	// weapon this frame (the camera manager's cached view is a frame old), turned by the kick and shake the camera
	// manager adds on top of it.
	const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
	const FQuat ViewRot = CamT.GetRotation() * CameraRotationOffset.Quaternion();
	const FVector Rel = ViewRot.UnrotateVector(Lens - CamT.GetLocation() - ViewRot.RotateVector(CameraLocationOffset));
	if (Rel.X < 1.f)
	{
		return false;
	}
	const float Aspect = ViewAspect();
	const float TanHalf = FMath::Tan(FMath::DegreesToRadians(Character->FirstPersonCamera->FirstPersonFieldOfView * 0.5f));
	OutCenterUV = FVector2D(0.5f + 0.5f * Rel.Y / Rel.X / TanHalf, 0.5f - 0.5f * Rel.Z / Rel.X / TanHalf * Aspect);
	OutRadius = 0.5f * LensRadius / Rel.X / TanHalf * Aspect;
	return true;
}

void UCampusViewmodelComponent::SetScopeComposite(float Live, const FVector2D& CenterUV, float Radius, const FVector2D& Roll, float Gain, const FVector2D& PupilShadow)
{
	UCameraComponent* Cam = Character->FirstPersonCamera;
	if (!Cam)
	{
		return;
	}
	if (!ScopeViewMID)
	{
		UMaterialInterface* Material = Live > 0.f ? CampusAssets::ScopeViewMaterial() : nullptr;
		if (!Material)
		{
			return;
		}
		ScopeViewMID = UMaterialInstanceDynamic::Create(Material, this);
		ScopeViewIndex = Cam->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0.f, ScopeViewMID));
	}
	TArray<FWeightedBlendable>& Blendables = Cam->PostProcessSettings.WeightedBlendables.Array;
	if (Blendables.IsValidIndex(ScopeViewIndex) && Blendables[ScopeViewIndex].Object == ScopeViewMID)
	{
		Blendables[ScopeViewIndex].Weight = Live > 0.f ? 1.f : 0.f;
	}
	ScopeViewMID->SetScalarParameterValue(TEXT("Live"), Live);
	if (Live > 0.f)
	{
		ScopeViewMID->SetTextureParameterValue(TEXT("View"), ScopeTarget);
		ScopeViewMID->SetVectorParameterValue(TEXT("Center"), FLinearColor(CenterUV.X, CenterUV.Y, 0.f));
		ScopeViewMID->SetScalarParameterValue(TEXT("Radius"), Radius);
		ScopeViewMID->SetScalarParameterValue(TEXT("Aspect"), ViewAspect());
		ScopeViewMID->SetVectorParameterValue(TEXT("Basis"), FLinearColor(Roll.X, Roll.Y, 0.f));
		ScopeViewMID->SetScalarParameterValue(TEXT("Gain"), Gain);
		ScopeViewMID->SetVectorParameterValue(TEXT("Shadow"), FLinearColor(PupilShadow.X, PupilShadow.Y, 0.f));
	}
}

void UCampusViewmodelComponent::PrepareScopeTarget(int32 RequestedSize, bool bResetBudget)
{
	const int32 PreviousSize = ScopeTarget ? ScopeTarget->SizeX : 0;
	const int32 ViewSize = ScopeCaptureAllocation(PreviousSize, RequestedSize, bResetBudget);
	if (PreviousSize == ViewSize)
	{
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusScopeResize] old=%d new=%d want=%d ready=%d resetBudget=%d"),
		PreviousSize, ViewSize, RequestedSize, ScopeFramesRendered, bResetBudget ? 1 : 0);
	if (!ScopeTarget)
	{
		ScopeTarget = NewObject<UTextureRenderTarget2D>(this);
		ScopeTarget->RenderTargetFormat = RTF_RGBA16f;
		ScopeTarget->ClearColor = FLinearColor::Black;
		ScopeTarget->bAutoGenerateMips = true;
		ScopeTarget->MipsSamplerFilter = TF_Trilinear;
		ScopeTarget->Filter = TF_Trilinear;
		ScopeTarget->InitAutoFormat(ViewSize, ViewSize);
	}
	else
	{
		ScopeTarget->ResizeTarget(ViewSize, ViewSize);
	}
	ScopeFramesRendered = 0;
}

void UCampusViewmodelComponent::UpdateScopeView(ACampusWeapon* Weapon, const FCampusWeaponStats* Stats, float RawADS, float MainZoom)
{
	const bool bScope = Weapon && Weapon->HasScopeOptic();
	const float Live = bScope && GetWallObstructionAlpha() <= 0.08f ? CampusMath::SmoothStep01((RawADS - 0.45f) / 0.35f) : 0.f;
	FVector2D LensUV(0.5f, 0.5f);
	float LensRadius = 0.f;
	const USceneComponent* Sight = bScope ? Weapon->GetFPSight() : nullptr;
	const bool bView = Live > 0.001f && Sight && GetScopeEyepieceScreen(LensUV, LensRadius) && LensRadius > 0.01f;
	const APlayerController* PC = Cast<APlayerController>(Character->GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	TArray<FSceneViewStateReference>* EyeStates = LocalPlayer ? &(LocalPlayer->*GetPrivateMember(FLocalPlayerViewStates())) : nullptr;
	const FSceneViewStateInterface* EyeState = EyeStates && EyeStates->Num() > 0 ? (*EyeStates)[0].GetReference() : nullptr;
	if ((CVarScopeDebug.GetValueOnGameThread() & 1) && EyeState)
	{
		static double LastLog = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastLog > 0.5)
		{
			LastLog = Now;
			UE_LOG(LogTemp, Display, TEXT("[CampusScope] live %.2f eye exposure %.4f (EV %.2f)"), Live, EyeState->GetPreExposure(), -FMath::Log2(FMath::Max(EyeState->GetPreExposure(), 1e-6f)));
		}
	}
	if (!bView)
	{
		if (ScopeCapture)
		{
			ScopeCapture->bCaptureEveryFrame = false;
		}
		ScopeFramesRendered = 0;
		if (bScope)
		{
			Weapon->SetScopeEyepieceView(0.f, nullptr, 1.f, FVector2D::ZeroVector);
		}
		SetScopeComposite(0.f, LensUV, 0.f, FVector2D(1.f, 0.f), 1.f, FVector2D::ZeroVector);
		return;
	}
	const float ViewHeight = CampusGraphics::ViewHeightPixels();
	const int32 PixelBudget = CampusGraphics::OpticCaptureSize(1.f);
	const USceneComponent* Optic = Weapon->GetScopeEyepieceComponent();
	const bool bResetBudget = !FMath::IsNearlyEqual(ViewHeight, ScopeCaptureViewHeight, 0.5f) || PixelBudget != ScopeCapturePixelBudget || Optic != ScopeCaptureOptic.Get();
	const int32 WantSize = CampusGraphics::OpticCaptureSize(2.f * LensRadius);
	PrepareScopeTarget(WantSize, bResetBudget);
	ScopeCaptureViewHeight = ViewHeight;
	ScopeCapturePixelBudget = PixelBudget;
	ScopeCaptureOptic = Optic;
	const int32 ViewSize = ScopeTarget->SizeX;
	if (!ScopeCapture)
	{
		ScopeCapture = NewObject<USceneCaptureComponent2D>(Character);
		ScopeCapture->TextureTarget = ScopeTarget;
		ScopeCapture->bCaptureOnMovement = false;
		ScopeCapture->bAlwaysPersistRenderingState = true;
		ScopeCapture->ShowFlags.SetMotionBlur(false);
		// The capture keeps a Lumen scene and global distance field of its own. Sharing the eye's (a shared view
		// origin) had it update the eye's surface cache from its own narrow view every frame: the lighting round the
		// eyepiece shifted and dark blotches came up on the ground while aiming, and the eye's card captures tripled.
		ScopeCapture->RegisterComponent();
	}
	// The eye's own body and weapon are behind or beside the optic, never in its view.
	ScopeCapture->HiddenActors.Reset();
	ScopeCapture->HiddenActors.Add(Character);
	ScopeCapture->HiddenActors.Add(Weapon);
	// The rain streaks fall round the eye, a metre or two off: magnified six times they cross the picture as dark
	// smears. The drops a real scope sees are far out and too small to resolve.
	if (ACampusEnvironment* Env = ACampusEnvironment::Find(GetWorld()))
	{
		ScopeCapture->HiddenActors.Add(Env);
	}
	// Post-processed, linear, without a tone curve: Lumen clamps the light of each ray relative to the view's exposure
	// (to keep out fireflies), and only a post-processed view has one. At exposure 1 the clamp cut the noon sky on a
	// wall to a hundredth. The capture is exposed like the eye and the lens takes the exposure back out.
	ScopeCapture->CaptureSource = SCS_FinalColorHDR;
	ScopeCapture->bCaptureEveryFrame = !(CVarScopeDebug.GetValueOnGameThread() & 8);
	// Coming back after the eye was off the optic, its temporal histories (upscaler, Lumen) belong to an old view.
	if (ScopeFramesRendered == 0)
	{
		ScopeCapture->bCameraCutThisFrame = true;
	}
	const float EyeExposure = EyeState ? EyeState->GetPreExposure() : 0.f;
	float LensGain = 1.f;
	{
		FPostProcessSettings& PP = ScopeCapture->PostProcessSettings;
		// A capture renders without dynamic GI unless told: walls lit only by the sky and the sun, missing the bounce
		// of the sunlit yard, the street lamps at night and the glow of the storm's fog, it went several times too dark.
		const bool bLumen = CampusGraphics::LumenGI() && !(CVarScopeDebug.GetValueOnGameThread() & 4);
		PP.bOverride_DynamicGlobalIlluminationMethod = true;
		// Without Lumen, screen space GI sees nothing to bounce in a view this narrow, and comes out as noise.
		PP.DynamicGlobalIlluminationMethod = bLumen ? EDynamicGlobalIlluminationMethod::Lumen : EDynamicGlobalIlluminationMethod::None;
		PP.bOverride_ReflectionMethod = true;
		PP.ReflectionMethod = bLumen ? EReflectionMethod::Lumen : EReflectionMethod::None;
		// Its own surface cache only feeds the bounce of a picture a few hundred pixels across: half the texel density.
		PP.bOverride_LumenSurfaceCacheResolution = true;
		PP.LumenSurfaceCacheResolution = 0.5f;
		// Manual exposure without a physical camera is 1 at no bias, doubling per stop.
		const float Exposure = EyeExposure > 0.f ? EyeExposure : 1.f;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = FMath::Log2(Exposure);
		LensGain = 1.f / Exposure;
		// The grade, white balance, vignette, bloom, grain and local contrast of the view are applied once, to the whole
		// frame with the eyepiece in it: neutral here.
		PP.bOverride_WhiteTemp = true;
		PP.WhiteTemp = 6500.f;
		PP.bOverride_WhiteTint = true;
		PP.WhiteTint = 0.f;
		PP.bOverride_SceneColorTint = true;
		PP.SceneColorTint = FLinearColor::White;
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.f;
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.f;
		PP.bOverride_SceneFringeIntensity = true;
		PP.SceneFringeIntensity = 0.f;
		PP.bOverride_LocalExposureHighlightContrastScale = true;
		PP.LocalExposureHighlightContrastScale = 1.f;
		PP.bOverride_LocalExposureShadowContrastScale = true;
		PP.LocalExposureShadowContrastScale = 1.f;
	}
	// The magnified view is a second scene render every frame, about one capture texel per display pixel of the
	// eyepiece, anti-aliased by its own temporal upscaler (the sight picture is laid over the main view after the main
	// view's). The short-range stochastic passes (screen-space AO and reflections, distance field AO, contact
	// shadows) cost more than they show in a picture this small and stay out of it, as does the volumetric fog on low.
	{
		FEngineShowFlags& Flags = ScopeCapture->ShowFlags;
		Flags.SetGlobalIllumination(true);
		Flags.SetLumenGlobalIllumination(true);
		Flags.SetLumenReflections(CampusGraphics::LumenGI() && !CampusGraphics::LowOpticView());
		Flags.SetScreenSpaceReflections(false);
		Flags.SetLumenShortRangeAmbientOcclusion(false);
		Flags.SetAmbientOcclusion(false);
		Flags.SetDistanceFieldAO(false);
		Flags.SetContactShadows(false);
		Flags.SetVolumetricFog(!CampusGraphics::LowOpticView());
		Flags.SetTemporalAA(true);
		Flags.SetAntiAliasing(true);
		Flags.SetEyeAdaptation(true);
		Flags.SetBloom(false);
		Flags.SetLensFlares(false);
		Flags.SetDepthOfField(false);
		Flags.SetGrain(false);
		Flags.SetVignette(false);
	}

	// The eyepiece shows the world magnified relative to the view around it: its circle on screen spans an angle of
	// the scene, and the optic fills it with that angle divided by the extra magnification.
	const float Aspect = ViewAspect();
	const float TanHalfV = FMath::Tan(FMath::DegreesToRadians(Character->FirstPersonCamera->FieldOfView * 0.5f)) / Aspect;
	const float Magnification = FMath::Max((Stats ? Stats->ADSZoom : 3.f) / FMath::Max(MainZoom, 1.f), 1.f);
	const float HalfAngle = FMath::Atan(2.f * LensRadius * TanHalfV / Magnification);
	ScopeCapture->FOVAngle = FMath::RadiansToDegrees(2.f * HalfAngle);
	ScopeCapture->SetWorldLocationAndRotation(Character->FirstPersonCamera->GetComponentLocation(), Sight->GetComponentQuat());
	// Texture streaming only knows the main view's field of view: without this the magnified view gets the mips of
	// something six times smaller on screen.
	IStreamingManager::Get().AddViewInformation(ScopeCapture->GetComponentLocation(), ViewSize, ViewSize / FMath::Max(FMath::Tan(HalfAngle), 0.001f));
	// Exit-pupil shading slides opposite to where the eyepiece drifts from the eye's line.
	const FVector2D Off((LensUV.X - 0.5f) * Aspect, LensUV.Y - 0.5f);
	const FVector2D Shadow = (-Off / FMath::Max(2.f * LensRadius, 0.05f) * 0.6f).ClampAxes(-0.22f, 0.22f);
	// Screen axes (right, down) onto the capture's, which turns with the optic: the picture and its reticle roll with
	// the scope when the rifle is canted.
	const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
	const FQuat SightInView = (CamT.GetRotation() * CameraRotationOffset.Quaternion()).Inverse() * Sight->GetComponentQuat();
	const FVector Up = SightInView.GetUpVector();
	const float Roll = FMath::Atan2(Up.Y, Up.Z);
	// The glass itself goes dark under the picture: nothing bright in the main view for its reflections, bounce and
	// exposure to pick up.
	// The auto exposure meters the whole frame as it does at the hip: the sight picture stands in for the middle of
	// the view the scope body hides, and the dark tube for the dark receiver beside it at the hip. Measured at dusk on
	// the yard, the eye stays within a tenth of a stop of its exposure at the hip; leaving either out moved it by a
	// quarter to a half.
	// The picture only goes up once the capture has drawn into the target as it is: a new or resized target holds
	// whatever was in that memory, and its small mips (sampled while the eyepiece is still small on screen) can be
	// huge values the bloom spread over the whole view, the white flash the first time the scope came up.
	const bool bPictureReady = ScopeFramesRendered >= 2;
	ScopeFramesRendered = FMath::Min(ScopeFramesRendered + 1, 1000);
	Weapon->SetScopeEyepieceView(Live, ScopeTarget, 0.f, Shadow);
	SetScopeComposite((!bPictureReady || (CVarScopeDebug.GetValueOnGameThread() & 2)) ? 0.f : Live, LensUV, LensRadius, FVector2D(FMath::Cos(Roll), FMath::Sin(Roll)), LensGain, Shadow);
}

FString UCampusViewmodelComponent::GetDebugString() const
{
	if (!Character)
	{
		return TEXT("no character");
	}
	USkeletalMeshComponent* Arms = Character->ArmsMesh;
	const FTransform PivotT = Character->ViewmodelPivot->GetComponentTransform();
	const FTransform CamT = Character->FirstPersonCamera->GetComponentTransform();
	auto BoneInCam = [&](const TCHAR* Bone)
	{
		return Arms->GetBoneIndex(Bone) != INDEX_NONE ? CamT.InverseTransformPosition(Arms->GetBoneLocation(Bone)).ToString() : FString(TEXT("-"));
	};
	FString GunSpace = TEXT("no weapon");
	const ACampusWeapon* Weapon = Character->GetCurrentWeapon();
	if (const USceneComponent* SightComp = Weapon ? Weapon->GetFPSight() : nullptr)
	{
		const FQuat SightCam = CamT.GetRotation().Inverse() * SightComp->GetComponentQuat();
		const USceneComponent* FrameComp = Weapon->GetFPFrame();
		GunSpace = FString::Printf(TEXT("sight-cam rot=%s fwd=%s up=%s frame-cam=%s"), *SightCam.Rotator().ToString(),
			*SightCam.GetForwardVector().ToString(), *SightCam.GetUpVector().ToString(),
			FrameComp ? *CamT.InverseTransformPosition(FrameComp->GetComponentLocation()).ToString() : TEXT("-"));
	}
	if (const USkeletalMeshComponent* GunMesh = Weapon ? Weapon->GetFPMesh() : nullptr)
	{
		const FTransform GunT = GunMesh->GetComponentTransform();
		const FTransform FrameT = Weapon->GetFPFrame()->GetComponentTransform();
		GunSpace = FString::Printf(TEXT("frame-space: hand_l=%s hand_r=%s IKAlpha=%.2f | gun-space: hand_r=%s hand_l=%s ik_hand_l=%s ik_hand_gun=%s | gun-socket HandGrip_R(arms)=%s"),
			*FrameT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("hand_l"))).ToString(),
			*FrameT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("hand_r"))).ToString(), LeftHandIKAlpha,
			*GunT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("hand_r"))).ToString(),
			*GunT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("hand_l"))).ToString(),
			Arms->GetBoneIndex(TEXT("ik_hand_l")) != INDEX_NONE ? *GunT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("ik_hand_l"))).ToString() : TEXT("-"),
			Arms->GetBoneIndex(TEXT("ik_hand_gun")) != INDEX_NONE ? *GunT.InverseTransformPosition(Arms->GetBoneLocation(TEXT("ik_hand_gun"))).ToString() : TEXT("-"),
			*GunT.InverseTransformPosition(Arms->GetSocketLocation(TEXT("HandGrip_R"))).ToString());
	}
	const UCampusFPAnimInstance* Anim = Character->GetArmsAnim();
	// Left hand frame in camera space: toward the fingers, toward the thumb, and the palm normal (X fwd, Y right, Z up).
	auto Dir = [&](const TCHAR* From, const TCHAR* To)
	{
		return Arms->GetBoneIndex(From) != INDEX_NONE && Arms->GetBoneIndex(To) != INDEX_NONE
			? CamT.InverseTransformVectorNoScale(Arms->GetBoneLocation(To) - Arms->GetBoneLocation(From)).GetSafeNormal() : FVector::ZeroVector;
	};
	const FVector LFingers = Dir(TEXT("hand_l"), TEXT("middle_01_l"));
	const FVector LAcross = Dir(TEXT("pinky_01_l"), TEXT("index_01_l"));
	GunSpace += FString::Printf(TEXT(" | lhand cam fingers=%s across(pinky->index)=%s palm=%s"), *LFingers.ToCompactString(), *LAcross.ToCompactString(),
		*FVector::CrossProduct(LFingers, LAcross).GetSafeNormal().ToCompactString());
	const TCHAR* AimBlock = Character->GetAimBlockReason();
	GunSpace += FString::Printf(TEXT(" | AimHeld=%d CanAim=%d AimBlock=%s RawADS=%.3f EffectiveADS=%.3f | WallTarget=%.3f WallHit=%s/%s WallDist=%.1f WallPenetrating=%d WallCorrection=%s Clearance=%s ScopeSize=%d ScopeFrames=%d"),
		Character->IsAimHeld() ? 1 : 0, Character->CanAim() ? 1 : 0, AimBlock ? AimBlock : TEXT("none"), Character->GetRawADSAlpha(), Character->GetADSAlpha(),
		WallTargetAlpha, *GetNameSafe(WallHitActor.Get()), *GetNameSafe(WallHitComponent.Get()), WallHitDistance, bWallHitPenetrating ? 1 : 0,
		*WallCorrection.ToCompactString(), *WeaponClearanceBox.ToString(), ScopeTarget ? ScopeTarget->SizeX : 0, ScopeFramesRendered);
	GunSpace += FString::Printf(TEXT(" | MantleHand active=%d returning=%d alpha=%.3f target=%s"),
		bMantleHandActive ? 1 : 0, bMantleHandReturning ? 1 : 0, MantleHandAlpha, *MantleHandCamera.ToCompactString());
	return FString::Printf(TEXT("ReloadA=%.3f Insert=%.2f ElbowDrop=%.2f | Alphas sprint=%.2f tac=%.2f slide=%.2f lowered=%.2f reload=%.2f block=%.2f ads=%.2f equip=%.2f inspect=%.2f bob=%.2f | Loc=%s Speed=%.0f Slide=%d Tac=%d | ArmsVisible=%d Mesh=%s ArmsRel=%s Pivot=%s | HandsOffset=%s ReachErr=%.2f | Sight=%s Grip=%s Calibrated=%d | cam-space: head=%s hand_r=%s hand_l=%s lowerarm_l=%s upperarm_l=%s lowerarm_r=%s | %s"),
		ReloadTime >= 0.f ? ReloadTime / FMath::Max(ReloadDuration, 0.1f) : -1.f, Weapon ? Weapon->GetStats().ReloadInsertFraction : -1.f, ElbowDrop,
		SprintAlpha.Value, TacSprintAlpha.Value, SlideAlpha.Value, LoweredAlpha, ReloadWeight.Value, BlockAlpha, ADSPoseAlpha, EquipTime, InspectTime, BobPhase,
		*Character->GetActorLocation().ToString(), Character->GetVelocity().Size2D(), Character->IsSliding() ? 1 : 0, Character->IsTacSprinting() ? 1 : 0,
		Arms->IsVisible() ? 1 : 0, *GetNameSafe(Arms->GetSkeletalMeshAsset()), *Arms->GetRelativeLocation().ToString(),
		*Character->ViewmodelPivot->GetRelativeTransform().ToString(), *HandsOffset.ToString(), Anim ? Anim->GetHandsReachError() : -1.f,
		*CalibSight.ToString(), *CalibGrip.ToString(), bCalibrated ? 1 : 0,
		*BoneInCam(TEXT("head")), *BoneInCam(TEXT("hand_r")), *BoneInCam(TEXT("hand_l")), *BoneInCam(TEXT("lowerarm_l")), *BoneInCam(TEXT("upperarm_l")), *BoneInCam(TEXT("lowerarm_r")), *GunSpace);
}

void UCampusViewmodelComponent::OnWeaponFired(const FCampusWeaponStats& Stats, float InADSAlpha, bool bFirstShot)
{
	const float K = Stats.VisualKick;
	const float ADSScale = FMath::Lerp(1.f, 0.45f, InADSAlpha);
	const float Now = GetWorld()->GetTimeSeconds();
	ShotString = (bFirstShot || Now - LastShotTime > 0.35f) ? 0 : ShotString + 1;
	LastShotTime = Now;
	// The rifle torques one way and back on alternate shots instead of jittering at random.
	const float Torque = ShotString % 2 == 0 ? 1.f : -1.f;
	// Three layers: a hard snap straight back into the shoulder, the main kick (back, up, a little roll), and a slow
	// climb that builds over the first rounds of a string and settles once the trigger is released.
	RecoilSnapPos.AddImpulse(FVector(-260.f, 0.f, 16.f) * K * ADSScale);
	RecoilPos.AddImpulse(FVector(-FMath::FRandRange(100.f, 125.f) * K * ADSScale, FMath::FRandRange(-6.f, 6.f) * K, FMath::FRandRange(10.f, 16.f) * K * ADSScale));
	RecoilRot.AddImpulse(FVector(FMath::FRandRange(80.f, 100.f) * K * ADSScale, FMath::FRandRange(-12.f, 12.f) * K * ADSScale, (Torque * 30.f + FMath::FRandRange(-12.f, 12.f)) * K * ADSScale));
	const float Build = FMath::Clamp(1.f - ShotString / 12.f, 0.25f, 1.f);
	RecoilClimb.AddImpulse(FVector(9.f * Build, FMath::FRandRange(-3.f, 3.f), Torque * 2.f) * K * ADSScale);
	CamKick.AddImpulse(FVector(FMath::FRandRange(15.f, 20.f) * K * (bFirstShot ? 1.3f : 1.f), FMath::FRandRange(-5.f, 5.f) * K, (Torque * 7.f + FMath::FRandRange(-4.f, 4.f)) * K));
	ShakeAmount = FMath::Min(ShakeAmount + 0.35f * K, 0.9f);
	FOVKick.AddImpulse(-12.f * K * (1.f - 0.5f * InADSAlpha));
	CancelInspect();
}

void UCampusViewmodelComponent::OnLanded(float FallSpeed)
{
	const float V = FMath::Clamp((FallSpeed - 200.f) / 800.f, 0.f, 1.5f);
	if (V <= 0.f)
	{
		return;
	}
	// Mid-reload the weapon is held in close with the support arm folded: the landing shakes it far less, or it would
	// throw the gun and the arm into the lens.
	const float Hands = 1.f - 0.6f * FMath::Clamp(ReloadWeight.Value, 0.f, 1.f);
	ImpactPos.AddImpulse(FVector(0.f, 0.f, -70.f * V * Hands));
	ImpactRot.AddImpulse(FVector(-45.f * V * Hands, 0.f, FMath::FRandRange(-20.f, 20.f) * V * Hands));
	CamHeight.AddImpulse(-130.f * V);
	CamImpact.AddImpulse(FVector(-18.f * V, 0.f, FMath::FRandRange(-8.f, 8.f) * V));
}

void UCampusViewmodelComponent::OnJumped()
{
	const float Hands = 1.f - 0.6f * FMath::Clamp(ReloadWeight.Value, 0.f, 1.f);
	ImpactPos.AddImpulse(FVector(0.f, 0.f, -30.f * Hands));
	ImpactRot.AddImpulse(FVector(-20.f * Hands, 0.f, 0.f));
	CamImpact.AddImpulse(FVector(6.f, 0.f, 0.f));
}

void UCampusViewmodelComponent::OnCrouchChanged(float HeightDelta)
{
	CamHeight.Value += HeightDelta;
	if (HeightDelta < 0.f && bSlideJumpPending)
	{
		return;
	}
	// Dropping into a crouch the weapon dips and rolls in; standing up it lifts slightly.
	const float Down = HeightDelta > 0.f ? 1.f : -0.6f;
	ImpactPos.AddImpulse(FVector(0.f, 0.f, -22.f * Down));
	ImpactRot.AddImpulse(FVector(-14.f * Down, 0.f, 26.f * Down));
}

void UCampusViewmodelComponent::OnDamaged(const FVector& FromDirection, float Damage)
{
	const float S = FMath::Clamp(Damage / 30.f, 0.4f, 2.f);
	CamImpact.AddImpulse(FVector(FMath::FRandRange(25.f, 50.f), FMath::FRandRange(-35.f, 35.f), FMath::FRandRange(-30.f, 30.f)) * S);
	ImpactRot.AddImpulse(FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-50.f, 50.f)) * S);
	ImpactPos.AddImpulse(FVector(-30.f, FMath::FRandRange(-20.f, 20.f), -15.f) * S);
}

void UCampusViewmodelComponent::OnReloadStart(float Duration, bool bEmpty)
{
	ReloadTime = 0.f;
	ReloadDuration = Duration;
	bReloadEmpty = bEmpty;
	bReloadMagKicked = false;
	CancelInspect();
}

void UCampusViewmodelComponent::SetDebugReload(float Progress, bool bEmpty)
{
	DebugReloadProgress = Progress;
	if (Progress < 0.f)
	{
		ReloadTime = -1.f;
		return;
	}
	const ACampusWeapon* Weapon = Character ? Character->GetCurrentWeapon() : nullptr;
	ReloadDuration = Weapon ? (bEmpty ? Weapon->GetStats().EmptyReloadTime : Weapon->GetStats().ReloadTime) : 2.f;
	bReloadEmpty = bEmpty;
	bReloadMagKicked = true;
	ReloadTime = Progress * ReloadDuration;
}

void UCampusViewmodelComponent::OnReloadInsert()
{
	RecoilRot.AddImpulse(FVector(55.f, -10.f, 25.f));
	RecoilPos.AddImpulse(FVector(25.f, 0.f, 30.f));
	CamImpact.AddImpulse(FVector(4.f, 0.f, 2.f));
}

void UCampusViewmodelComponent::OnReloadBolt()
{
	RecoilPos.AddImpulse(FVector(-60.f, 8.f, 0.f));
	RecoilRot.AddImpulse(FVector(-20.f, 25.f, -30.f));
	CamImpact.AddImpulse(FVector(-3.f, 2.f, 0.f));
}

void UCampusViewmodelComponent::OnReloadEnd()
{
	ReloadTime = -1.f;
}

void UCampusViewmodelComponent::OnReloadCancel(bool bInserted)
{
	ReloadTime = -1.f;
	// Snap the weapon back up onto target.
	RecoilPos.AddImpulse(FVector(bInserted ? -22.f : -12.f, 0.f, 14.f));
	RecoilRot.AddImpulse(FVector(bInserted ? -24.f : -14.f, 8.f, 22.f));
}

void UCampusViewmodelComponent::OnSlideCancel(bool bJumping)
{
	bSlideJumpPending = bJumping;
	if (bJumping)
	{
		return;
	}
	CamImpact.AddImpulse(FVector(6.f, 0.f, -8.f));
	ImpactPos.AddImpulse(FVector(10.f, 0.f, 22.f));
}

void UCampusViewmodelComponent::OnEquip(float Duration)
{
	KnifeTime = -1.f;
	HolsterTime = -1.f;
	EquipTime = 0.f;
	bEquipSettled = false;
	EquipDuration = FMath::Max(Duration, 0.1f);
	ReloadTime = -1.f;
	CancelInspect();
	bCalibrated = false;
	if (Character && Character->GetCurrentWeapon())
	{
		Calibrate(Character->GetCurrentWeapon(), 0.f, true);
	}
}

void UCampusViewmodelComponent::OnHolster(float Duration)
{
	EquipTime = -1.f;
	HolsterTime = 0.f;
	HolsterDuration = FMath::Max(Duration, 0.05f);
	ReloadTime = -1.f;
	CancelInspect();
}

void UCampusViewmodelComponent::OnMelee()
{
	MeleeTime = 0.f;
	bMeleeFading = false;
	MeleeFade = 1.f;
	CancelInspect();
	CamImpact.AddImpulse(FVector(-6.f, -10.f, -8.f));
}

void UCampusViewmodelComponent::OnKnifeSlash(bool bHeavy)
{
	KnifeTime = 0.f;
	bKnifeHeavy = bHeavy;
	bKnifeThrustKicked = false;
	CancelInspect();
	if (bHeavy)
	{
		CamImpact.AddImpulse(FVector(-3.f, 2.f, 0.f));
		return;
	}
	++KnifeSwing;
	// The head turns a touch with the cut, the way it goes.
	const float Side = (KnifeSwing & 1) ? 1.f : -1.f;
	CamImpact.AddImpulse(FVector(-4.f, 9.f * Side, 7.f * Side));
}

void UCampusViewmodelComponent::InterruptActions()
{
	CancelInspect();
	// Past the strike the melee recovery yields to whatever comes next.
	if (MeleeTime >= 0.3f && !bMeleeFading)
	{
		bMeleeFading = true;
		MeleeFade = 1.f;
	}
}

void UCampusViewmodelComponent::RaiseFromLowReady(float Duration)
{
	// Not the draw timeline (early in a draw the support hand is still off the weapon): the weapon is in both hands
	// from the first frame, knocked down and canted by the landing and springing back up onto the aim line.
	HolsterTime = -1.f;
	EquipTime = -1.f;
	ReloadTime = -1.f;
	CancelInspect();
	const float K = FMath::Clamp(0.55f / FMath::Max(Duration, 0.1f), 0.5f, 1.5f);
	ImpactPos.AddImpulse(FVector(-8.f, 2.f, -60.f) * K);
	ImpactRot.AddImpulse(FVector(-22.f, 5.f, 14.f) * K);
	// The knees taking the landing.
	CamHeight.AddImpulse(-45.f * K);
}

void UCampusViewmodelComponent::CancelHolster(float RaiseDuration)
{
	if (HolsterTime < 0.f)
	{
		return;
	}
	const float Lowered = FMath::Clamp(LoweredAlpha, 0.f, 1.f);
	HolsterTime = -1.f;
	EquipDuration = FMath::Max(RaiseDuration, 0.05f);
	// Start the raise where the weapon is now: solve (1 - A)^k = Lowered.
	const float A = 1.f - FMath::Pow(Lowered, 1.f / EquipEaseExponent);
	EquipTime = A * EquipDuration;
	bEquipSettled = A >= 0.72f;
}

void UCampusViewmodelComponent::OnInspect()
{
	if (InspectTime < 0.f && ReloadTime < 0.f && EquipTime < 0.f && MeleeTime < 0.f)
	{
		InspectTime = 0.f;
	}
}

void UCampusViewmodelComponent::CancelInspect()
{
	if (InspectTime >= 0.f && !bInspectFading)
	{
		bInspectFading = true;
		InspectFade = 1.f;
	}
}

// ------------------------------------------------------------------ grenades

bool UCampusViewmodelComponent::EvaluateThrow(float& OutLower, FVector& OutHandCam, float& OutIK, FVector& OutElbowCam, FQuat& OutTurnCam, float& OutFingers, float& OutThumb) const
{
	OutLower = 0.f;
	OutIK = 0.f;
	OutFingers = 0.f;
	OutThumb = 0.f;
	OutHandCam = FVector::ZeroVector;
	OutElbowCam = FVector::ZeroVector;
	OutTurnCam = FQuat::Identity;
	if (ThrowTime < 0.f)
	{
		return false;
	}
	auto Seg = [](float A, float B, float X) { return CampusMath::SmootherStep01((X - A) / FMath::Max(B - A, 0.001f)); };
	auto Lin = [](float A, float B, float X) { return FMath::Clamp((X - A) / FMath::Max(B - A, 0.001f), 0.f, 1.f); };
	auto Key = [this](const TCHAR* Name, const FVector& Default)
	{
		const FTransform* Override = PoseOverrides.Find(FName(Name));
		return Override ? Override->GetLocation() : Default;
	};
	auto Turn = [this](const TCHAR* Name, const FRotator& Default)
	{
		const FTransform* Override = PoseOverrides.Find(FName(Name));
		return Override ? Override->GetRotation() : Default.Quaternion();
	};
	auto Bezier = [](const FVector& A, const FVector& Ctrl, const FVector& B, float T)
	{
		return FMath::Lerp(FMath::Lerp(A, Ctrl, T), FMath::Lerp(Ctrl, B, T), T);
	};
	// Camera-space wrist targets (cm). Draw: down to the pouch below the frame, up into view low left in a fist, to the
	// gun hand's fingers for the pin, then held cocked low left. Throw (overhand): the arm draws back and up beside the
	// ear (out of frame), whips over the top to the release point ahead of and above the sights, and follows through
	// down across the body and out under the right of the frame, the wrist snapping over.
	const FVector Pouch = Key(TEXT("tpouch"), FVector(18.f, -14.f, -42.f));
	const FVector Present = Key(TEXT("tpresent"), FVector(33.f, -14.f, -17.f));
	const FVector Pin = Key(TEXT("tpin"), FVector(31.f, 0.f, -16.f));
	// Cooked and ready (the off-hand throw of the modern COD games): the arm comes up at the left of the view, forearm
	// standing, the grenade gripped high beside the sights with the palm turned toward the target.
	const FVector Hold = Key(TEXT("thold"), FVector(35.f, -24.f, -5.f));
	const FVector Wind = Key(TEXT("twind"), FVector(4.f, -27.f, 9.f));
	const FVector Release = Key(TEXT("trelease"), FVector(40.f, -11.f, 2.f));
	const FVector Follow = Key(TEXT("tfollow"), FVector(30.f, 10.f, -42.f));
	// Camera-space wrist turns on top of the handguard grip (solved with Tools/hand_turn.py from the measured hand
	// frame). Held: fingers standing up round the grenade's far side, palm toward the middle of the view, thumb over
	// the lever. Drawn back: the palm turns to face the target. Release: fingers up, palm forward and a little down as
	// the wrist snaps over; follow-through: palm down, the hand rolling out under the right of the frame.
	const FQuat HoldTurn = Turn(TEXT("throwturn"), FRotator(74.1f, 124.7f, -169.7f));
	const FQuat WindTurn = Turn(TEXT("tturnwind"), FRotator(53.1f, 116.5f, -123.2f));
	const FQuat ReleaseTurn = Turn(TEXT("tturnrelease"), FRotator(54.6f, 54.1f, 175.4f));
	const FQuat FollowTurn = Turn(TEXT("tturnfollow"), FRotator(-26.5f, 65.8f, 106.0f));
	// Present (coming up out of the pouch, before the pin): the grenade in a loose fist, palm up toward the face.
	const FQuat PresentTurn = Turn(TEXT("tturnpresent"), FRotator(25.f, -80.f, -30.f));
	// Elbow poles: down and out to the left under the raised forearm; drawn back at shoulder height for the wind-up
	// (never above the hand, the forearm stands up from it); still below the hand at the release; down across the body after.
	const FVector ElbowLow(-6.f, -24.f, -30.f);
	const FVector ElbowUp(-14.f, -30.f, -6.f);
	const FVector ElbowRelease(-4.f, -24.f, -16.f);
	const FVector ElbowFollow(2.f, -6.f, -30.f);
	// Fingers: the handguard curl opened round a 6 cm grenade (X); the thumb, which lies along the handguard, comes in
	// past the open hand to close over the top beside the forefinger (Y); at the release the hand opens right out (Z)
	// as the grenade rolls off the fingertips.
	const FVector Fingers = Key(TEXT("tfingers"), FVector(0.35f, 1.7f, 0.85f));
	const float Tau = ThrowTime;
	// Automation: "tfreeze" (X = seconds after release) poses a held grenade as that instant of the throw.
	const FTransform* Freeze = Tau > 0.6f ? PoseOverrides.Find(TEXT("tfreeze")) : nullptr;
	const float ReleaseAt = Freeze ? Tau - Freeze->GetLocation().X : ThrowReleaseAt;
	FVector H;
	if (ThrowCancelAt >= 0.f)
	{
		// Back to the pouch and up onto the handguard.
		const float C = Seg(0.f, 0.3f, Tau - ThrowCancelAt);
		H = FMath::Lerp(Hold, Pouch, C);
		OutIK = 1.f - C;
		OutLower = 1.f - C;
		OutTurnCam = HoldTurn;
		OutElbowCam = ElbowLow;
		OutFingers = Fingers.X * (1.f - C);
		OutThumb = Fingers.Y * (1.f - C);
	}
	else if (ReleaseAt < 0.f)
	{
		H = Tau < 0.12f ? FMath::Lerp(ThrowStartHand, Pouch, Seg(0.f, 0.12f, Tau)) : FMath::Lerp(Pouch, Present, Seg(0.12f, 0.3f, Tau));
		H = FMath::Lerp(H, Hold, Seg(0.34f, 0.52f, Tau));
		// Pin pull: up to the fingers of the gun hand and straight back.
		H = FMath::Lerp(H, Pin, FMath::Sin(PI * FMath::Clamp((Tau - 0.18f) / 0.18f, 0.f, 1.f)));
		// Held: the arm settles and shifts its weight.
		H += FVector(0.f, 0.5f * FMath::Sin(Tau * 3.1f), 0.6f * FMath::Sin(Tau * 2.3f)) * Seg(0.45f, 0.7f, Tau);
		OutIK = 1.f;
		OutLower = Seg(0.f, 0.22f, Tau);
		// Brought up in a fist for the pin, then cocked: the wrist turns over as the arm rises into the ready hold.
		OutTurnCam = FQuat::Slerp(PresentTurn, HoldTurn, Seg(0.34f, 0.56f, Tau));
		OutElbowCam = FMath::Lerp(FVector(-4.f, -12.f, -28.f), ElbowLow, Seg(0.34f, 0.56f, Tau));
		// Off the handguard and open on the way to the pouch, closed round the grenade as it comes up.
		const float Grip = Seg(0.02f, 0.1f, Tau);
		OutFingers = FMath::Lerp(Grip * 0.7f, Fingers.X, Seg(0.1f, 0.2f, Tau));
		OutThumb = FMath::Lerp(Grip * 0.6f, Fingers.Y, Seg(0.1f, 0.2f, Tau));
	}
	else
	{
		// Phases (s after release): draw back 0..0.11, whip 0.11..0.17 (the grenade leaves the hand at 0.17, in step
		// with ACampusCharacter::UpdateThrow), follow-through 0.17..0.36.
		const float R = Tau - ReleaseAt;
		if (R < 0.11f)
		{
			const float T = Seg(0.f, 0.11f, R);
			H = Bezier(Hold, FVector(16.f, -26.f, -4.f), Wind, T);
			OutTurnCam = FQuat::Slerp(HoldTurn, WindTurn, T);
			OutElbowCam = FMath::Lerp(ElbowLow, ElbowUp, T);
		}
		else if (R < 0.17f)
		{
			// Fastest part of the swing: nearly constant speed over the top.
			const float T = FMath::Lerp(Lin(0.11f, 0.17f, R), Seg(0.11f, 0.17f, R), 0.3f);
			H = Bezier(Wind, FVector(24.f, -18.f, 16.f), Release, T);
			OutTurnCam = FQuat::Slerp(WindTurn, ReleaseTurn, T);
			OutElbowCam = FMath::Lerp(ElbowUp, ElbowRelease, T);
		}
		else
		{
			// Decelerating follow-through: most of the drop out of the frame happens straight after the release.
			const float T = 1.f - FMath::Pow(1.f - Lin(0.17f, 0.36f, R), 3.f);
			H = Bezier(Release, FVector(46.f, 6.f, -8.f), Follow, T);
			OutTurnCam = FQuat::Slerp(ReleaseTurn, FollowTurn, T);
			OutElbowCam = FMath::Lerp(ElbowRelease, ElbowFollow, T);
		}
		// The hand comes back up onto the handguard from below as the weapon rises.
		OutIK = 1.f - Seg(0.42f, 0.72f, R);
		OutLower = Seg(0.f, 0.22f, ReleaseAt) * (1.f - Seg(0.36f, 0.68f, R));
		// The grip holds through the swing, the hand opens as the grenade leaves it, and closes again on the way back.
		const float Open = Seg(0.155f, 0.2f, R) * (1.f - Seg(0.3f, 0.6f, R));
		OutFingers = FMath::Lerp(Fingers.X, Fingers.Z, Open) * OutIK;
		OutThumb = FMath::Lerp(Fingers.Y, Fingers.Z, Open) * OutIK;
	}
	OutHandCam = H;
	return true;
}

void UCampusViewmodelComponent::ClearHandGrenade()
{
	for (UStaticMeshComponent* P : HandGrenade)
	{
		if (P)
		{
			P->DestroyComponent();
		}
	}
	HandGrenade.Reset();
	if (HandGrenadeRoot)
	{
		HandGrenadeRoot->DestroyComponent();
		HandGrenadeRoot = nullptr;
	}
	bHandGrenadePlaced = false;
}

void UCampusViewmodelComponent::PlaceHandGrenade()
{
	USkeletalMeshComponent* Arms = Character ? Character->ArmsMesh.Get() : nullptr;
	if (!Arms || !HandGrenadeRoot)
	{
		return;
	}
	// Out of sight in the pouch at first, and gone once thrown (or put away).
	const bool bShow = ThrowTime >= 0.1f && !bHandGrenadeGone && (ThrowCancelAt < 0.f || ThrowTime - ThrowCancelAt < 0.16f);
	if (HandGrenadeRoot->IsVisible() != bShow)
	{
		HandGrenadeRoot->SetVisibility(bShow, true);
	}
	if (bHandGrenadePlaced || Arms->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE)
	{
		return;
	}
	// Seated afresh each frame while the fingers close round it, then held where they settled.
	const bool bSettled = ThrowTime >= 0.26f;
	CampusThrowables::SeatInLeftHand(Arms, HandGrenadeRoot, ThrowType, PoseOverrides.Find(TEXT("grenade")));
	bHandGrenadePlaced = bSettled;
}

bool UCampusViewmodelComponent::GetHandGrenadeLocation(FVector& OutWorld) const
{
	if (!Character || !HandGrenadeRoot || !bHandGrenadePlaced || bHandGrenadeGone)
	{
		return false;
	}
	OutWorld = Character->FirstPersonToWorldVisual(HandGrenadeRoot->GetComponentLocation());
	return true;
}

void UCampusViewmodelComponent::OnThrowStart(ECampusThrowable Type, float Rate)
{
	ClearHandGrenade();
	ThrowType = Type;
	ThrowTime = 0.f;
	ThrowReleaseAt = -1.f;
	ThrowRate = FMath::Max(Rate, 0.1f);
	ThrowCancelAt = -1.f;
	bHandGrenadeGone = false;
	ReloadTime = -1.f;
	CancelInspect();
	ThrowStartHand = FVector(28.f, -6.f, -14.f);
	if (Character && Character->ArmsMesh && Character->ArmsMesh->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE)
	{
		ThrowStartHand = Character->FirstPersonCamera->GetComponentTransform().InverseTransformPosition(Character->ArmsMesh->GetBoneLocation(TEXT("hand_l")));
	}
	if (Character && Character->ArmsMesh)
	{
		HandGrenadeRoot = NewObject<USceneComponent>(Character);
		HandGrenadeRoot->SetupAttachment(Character->ArmsMesh, TEXT("hand_l"));
		HandGrenadeRoot->RegisterComponent();
		HandGrenadeRoot->SetVisibility(false, true);
		CampusThrowables::BuildModel(Character, HandGrenadeRoot, Type, true, true, true, HandGrenade);
		HandGrenadeRoot->SetVisibility(false, true);
	}
	RecoilRot.AddImpulse(FVector(-12.f, 8.f, -16.f));
}

void UCampusViewmodelComponent::OnThrowPin()
{
	for (UStaticMeshComponent* P : HandGrenade)
	{
		if (P && P->ComponentHasTag(TEXT("Ring")))
		{
			P->SetVisibility(false);
		}
	}
	ImpactPos.AddImpulse(FVector(-6.f, 4.f, -5.f));
}

void UCampusViewmodelComponent::OnThrowSpoon()
{
	for (UStaticMeshComponent* P : HandGrenade)
	{
		if (P && P->ComponentHasTag(TEXT("Lever")) && P->IsVisible())
		{
			P->SetVisibility(false);
			const FVector At = Character->FirstPersonToWorldVisual(P->GetComponentLocation());
			const FRotator Aim = Character->GetControlRotation();
			CampusThrowables::SpawnSpoon(GetWorld(), ThrowType, FTransform(P->GetComponentQuat(), At), Character->GetVelocity() + Aim.RotateVector(FVector(60.f, -160.f, 260.f)));
		}
	}
}

void UCampusViewmodelComponent::OnThrowRelease()
{
	if (ThrowTime < 0.f)
	{
		return;
	}
	ThrowReleaseAt = ThrowTime;
	// The body winds into the throw: a small lean back, then the snap forward.
	CamImpact.AddImpulse(FVector(-6.f, -4.f, -6.f));
}

void UCampusViewmodelComponent::OnThrowCancel()
{
	if (ThrowTime >= 0.f && ThrowReleaseAt < 0.f && ThrowCancelAt < 0.f)
	{
		ThrowCancelAt = ThrowTime;
	}
}

void UCampusViewmodelComponent::OnThrowLaunched()
{
	bHandGrenadeGone = true;
	if (HandGrenadeRoot)
	{
		HandGrenadeRoot->SetVisibility(false, true);
	}
	CamImpact.AddImpulse(FVector(10.f, 6.f, 8.f));
	ImpactRot.AddImpulse(FVector(8.f, -10.f, 14.f));
}

void UCampusViewmodelComponent::OnThrowEnd()
{
	ThrowTime = -1.f;
	ThrowReleaseAt = -1.f;
	ThrowCancelAt = -1.f;
	bHandGrenadeGone = false;
	ClearHandGrenade();
	if (Character)
	{
		if (UCampusFPAnimInstance* Anim = Character->GetArmsAnim())
		{
			Anim->SetLeftHandTurn(FQuat::Identity, 0.f);
			Anim->SetLeftElbowPole(FVector::ZeroVector);
		}
	}
	// The weapon settles back into the shoulder.
	RecoilPos.AddImpulse(FVector(-14.f, 0.f, -8.f));
	RecoilRot.AddImpulse(FVector(-10.f, 4.f, 12.f));
}

void UCampusViewmodelComponent::OnExplosion(float Strength, const FVector& From)
{
	const float K = FMath::Clamp(Strength, 0.f, 1.5f);
	if (K <= 0.01f || !Character)
	{
		return;
	}
	// Knocked away from the blast: the head snaps back from it and the weapon is thrown about in the hands.
	const FVector Away = Character->FirstPersonCamera->GetComponentTransform().InverseTransformVectorNoScale((Character->GetPawnViewLocation() - From).GetSafeNormal());
	CamImpact.AddImpulse(FVector(FMath::FRandRange(25.f, 45.f) + 20.f * Away.X, -Away.Y * 50.f, FMath::FRandRange(-40.f, 40.f)) * K);
	ImpactRot.AddImpulse(FVector(FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-80.f, 80.f)) * K);
	ImpactPos.AddImpulse(FVector(-40.f, -Away.Y * 30.f, -30.f) * K);
	CamHeight.AddImpulse(-60.f * K);
	ShakeAmount = FMath::Min(ShakeAmount + 2.5f * K, 3.f);
	Rumble = FMath::Min(Rumble + 1.1f * K, 1.4f);
	FOVKick.AddImpulse(-25.f * K);
}

void UCampusViewmodelComponent::OnFlashbanged(float Blind, const FVector& From)
{
	if (!Character || !Character->FirstPersonCamera)
	{
		return;
	}
	float White = 0.f;
	float Ghost = 0.f;
	GetFlashState(White, Ghost);
	if (Blind < White)
	{
		return;
	}
	FlashBlind = FMath::Clamp(Blind, 0.f, 1.f);
	FlashTime = GetWorld()->GetTimeSeconds();
	// The frame at the instant of the flash, burned in: it hangs over the view as the white fades.
	FVector2D View(1280.f, 720.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(View);
	}
	const int32 W = 640;
	const int32 H = FMath::Clamp(FMath::RoundToInt(W * View.Y / FMath::Max(View.X, 1.f)), 200, 640);
	if (!FlashImage)
	{
		FlashImage = NewObject<UTextureRenderTarget2D>(this);
		FlashImage->RenderTargetFormat = RTF_RGBA8;
		FlashImage->ClearColor = FLinearColor::Black;
		FlashImage->InitAutoFormat(W, H);
		FlashCapture = NewObject<USceneCaptureComponent2D>(Character);
		FlashCapture->SetupAttachment(Character->FirstPersonCamera);
		FlashCapture->CaptureSource = SCS_FinalColorLDR;
		FlashCapture->TextureTarget = FlashImage;
		FlashCapture->bCaptureEveryFrame = false;
		FlashCapture->bCaptureOnMovement = false;
		FlashCapture->ShowFlags.SetMotionBlur(false);
		FlashCapture->RegisterComponent();
	}
	else if (FlashImage->SizeY != H)
	{
		FlashImage->ResizeTarget(W, H);
	}
	// A one-off capture has no eye adaptation of its own: expose it as the eye is adapted now (sunlit yard ~EV 14,
	// a classroom ~EV 9), a touch brighter, as the burned-in image is.
	{
		FPostProcessSettings& PP = FlashCapture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = -FMath::Lerp(14.3f, 9.3f, FMath::Clamp((CampusAudio::GetEnclosure() - 0.3f) / 0.45f, 0.f, 1.f)) + 0.8f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 1.5f;
	}
	FlashCapture->HiddenActors.Reset();
	FlashCapture->HiddenActors.Add(Character);
	if (ACampusWeapon* Weapon = Character->GetCurrentWeapon())
	{
		FlashCapture->HiddenActors.Add(Weapon);
	}
	FlashCapture->FOVAngle = Character->FirstPersonCamera->FieldOfView;
	FlashCapture->CaptureScene();
	CamImpact.AddImpulse(FVector(18.f, FMath::FRandRange(-20.f, 20.f), FMath::FRandRange(-15.f, 15.f)) * FlashBlind);
}

void UCampusViewmodelComponent::GetFlashState(float& OutWhite, float& OutGhost) const
{
	OutWhite = 0.f;
	OutGhost = 0.f;
	if (FlashBlind <= 0.f || !GetWorld())
	{
		return;
	}
	// Full flash: ~1.5 s of pure white, the eye recovering over ~2.5 s more, the afterimage lingering longest.
	const float T = GetWorld()->GetTimeSeconds() - FlashTime;
	const float Hold = 0.2f + 1.3f * FlashBlind;
	const float Fade = 0.7f + 1.9f * FlashBlind;
	const float Recover = FMath::Clamp((T - Hold) / Fade, 0.f, 1.f);
	OutWhite = FlashBlind * (T < Hold ? 1.f : FMath::Square(1.f - Recover));
	OutGhost = FlashBlind * FMath::Clamp(T / 0.05f, 0.f, 1.f) * (1.f - FMath::SmoothStep(0.f, 1.f, (T - Hold) / (Fade * 1.6f)));
}

void UCampusViewmodelComponent::UpdateFlashPost()
{
	if (!Character || !Character->FirstPersonCamera)
	{
		return;
	}
	float White = 0.f;
	float Ghost = 0.f;
	GetFlashState(White, Ghost);
	FPostProcessSettings& PP = Character->FirstPersonCamera->PostProcessSettings;
	const bool bOn = White > 0.002f;
	if (!bOn && !PP.bOverride_ColorGain)
	{
		if (FlashBlind > 0.f && GetWorld()->GetTimeSeconds() - FlashTime > 12.f)
		{
			FlashBlind = 0.f;
		}
		return;
	}
	// The eye is saturated: the scene behind the white is blown out and blooms hard as it comes back.
	PP.bOverride_ColorGain = bOn;
	PP.ColorGain = FVector4(1.f, 1.f, 1.f, 1.f + 2.5f * White);
	PP.bOverride_BloomIntensity = bOn;
	PP.BloomIntensity = 0.5f + 3.f * White;
	PP.bOverride_ColorSaturation = bOn;
	PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f - 0.6f * White);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusScopeCaptureStabilityTest, "CampusOps.Experience.Viewmodel.ScopeCaptureStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusScopeCaptureStabilityTest::RunTest(const FString&)
{
	UCampusViewmodelComponent* VM = NewObject<UCampusViewmodelComponent>();
	VM->PrepareScopeTarget(512, true);
	UTextureRenderTarget2D* Target = VM->ScopeTarget;
	if (!TestNotNull(TEXT("the optic allocates a real render target"), Target))
	{
		return false;
	}
	const int32 SettledSize = Target->SizeX;
	TestTrue(TEXT("the optic reserves bounded room for its near-eye recoil motion"), SettledSize >= 512 && SettledSize <= 768);
	VM->ScopeFramesRendered = 8;
	for (const int32 Request : { 512, 528, 512, 496, 528, 544, 512, 528 })
	{
		VM->PrepareScopeTarget(Request, false);
		TestTrue(TEXT("small settled lens changes cannot reallocate the sight picture"), VM->ScopeTarget == Target && Target->SizeX == SettledSize);
		TestTrue(TEXT("firing cannot put a settled capture back into its black warmup frames"), VM->ScopeFramesRendered >= 2);
	}
	VM->PrepareScopeTarget(1024, false);
	TestTrue(TEXT("a substantially larger eyepiece still receives a larger capture"), Target->SizeX >= 1024 && Target->SizeX > SettledSize && VM->ScopeFramesRendered == 0);
	const int32 LargeSize = Target->SizeX;
	VM->ScopeFramesRendered = 8;
	VM->PrepareScopeTarget(512, true);
	TestTrue(TEXT("a new viewport, graphics budget or optic can release the previous larger allocation"), Target->SizeX < LargeSize && Target->SizeX >= 512 && VM->ScopeFramesRendered == 0);
	VM->PrepareScopeTarget(4096, true);
	TestTrue(TEXT("headroom cannot exceed the optic memory limit"), Target->SizeX <= 2048 && Target->SizeY <= 2048);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWallBlendTest, "CampusOps.Experience.Viewmodel.WallBlendContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWallBlendTest::RunTest(const FString&)
{
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
	{
		float Alpha = 0.f;
		for (int32 Sample = 0; Sample < FMath::RoundToInt(0.5f / Step); ++Sample)
		{
			const float Previous = Alpha;
			Alpha = BlendWallAlpha(Alpha, 1.f, Step);
			TestTrue(TEXT("Approach stays inside the previous and requested pose"), Alpha >= Previous && Alpha <= 1.f);
		}
		TestTrue(TEXT("Close wall reaches the tucked pose promptly at every frame rate"), Alpha > 0.99f);
		for (int32 Sample = 0; Sample < FMath::RoundToInt(1.f / Step); ++Sample)
		{
			const float Previous = Alpha;
			Alpha = BlendWallAlpha(Alpha, 0.f, Step);
			TestTrue(TEXT("Leaving the wall never rebounds or crosses the ready pose"), Alpha <= Previous && Alpha >= 0.f);
		}
		TestTrue(TEXT("Release settles at every frame rate"), Alpha < 0.002f);
	}
	const float OneStep = BlendWallAlpha(0.2f, 0.8f, 1.f / 30.f);
	const float TwoSteps = BlendWallAlpha(BlendWallAlpha(0.2f, 0.8f, 1.f / 60.f), 0.8f, 1.f / 60.f);
	TestTrue(TEXT("A constant contact has the same response across frame rates"), FMath::IsNearlyEqual(OneStep, TwoSteps, 0.00001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWallTuckTest, "CampusOps.Experience.Viewmodel.WallTuckPreservesActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWallTuckTest::RunTest(const FString&)
{
	TestFalse(TEXT("Normal hip and ADS carries choose a lowered chest-level wall ready"), PreferHighWallReady(0.f));
	TestFalse(TEXT("A slightly lowered sprint follows the same low ready"), PreferHighWallReady(-9.f));
	TestFalse(TEXT("An already distinctly lowered action retains muzzle-down ready"), PreferHighWallReady(-30.f));
	TestTrue(TEXT("An already high tactical carry retains muzzle-up ready"), PreferHighWallReady(76.f));
	const FQuat Calibration = FRotator(4.f, -6.f, 2.f).Quaternion();
	const FVector GripLocal(12.f, 3.f, -4.f);
	for (const FRotator Gun : { FRotator::ZeroRotator, FRotator(2.f, -10.f, 34.f), FRotator(-9.f, -28.f, 36.f), FRotator(-30.f, -28.f, 36.f), FRotator(76.f, 0.f, 95.f) })
	{
		const bool bHighReady = PreferHighWallReady(Gun.Pitch);
		const FQuat Rotation = Gun.Quaternion() * Calibration.Inverse();
		const FVector Grip(23.f, 7.5f, -20.f);
		const FTransform Pose(Rotation, Grip - Rotation.RotateVector(GripLocal));
		TestTrue(TEXT("No contact leaves the authored action untouched"), WallTuckPose(Pose, Calibration, GripLocal, 0.f, -80.f, FVector(-4.f, 0.f, 2.f), bHighReady).Equals(Pose));
		FVector PreviousForward = Gun.Vector();
		for (int32 Sample = 1; Sample <= 100; ++Sample)
		{
			const FTransform Tucked = WallTuckPose(Pose, Calibration, GripLocal, Sample / 100.f, -80.f, FVector(-4.f, 0.f, 2.f), bHighReady);
			const FRotator TuckedGun = (Tucked.GetRotation() * Calibration).Rotator();
			TestTrue(TEXT("Wall tuck keeps the action's side and wrist cant"), FMath::Abs(FMath::FindDeltaAngleDegrees(Gun.Yaw, TuckedGun.Yaw)) < 0.01f
				&& FMath::Abs(FMath::FindDeltaAngleDegrees(Gun.Roll, TuckedGun.Roll)) < 0.01f);
			TestTrue(TEXT("The muzzle follows a continuous arc without flipping"), FVector::DotProduct(PreviousForward, TuckedGun.Vector()) > 0.999f);
			TestTrue(TEXT("The firing hand stays in front of the eye"), Tucked.TransformPosition(GripLocal).X >= 18.5f - KINDA_SMALL_NUMBER);
			PreviousForward = TuckedGun.Vector();
		}
		const FTransform Tucked = WallTuckPose(Pose, Calibration, GripLocal, 1.f, -80.f, FVector(-4.f, 0.f, 2.f), bHighReady);
		if (bHighReady)
		{
			TestTrue(TEXT("An already raised tactical carry is never swung down through the eye"), (Tucked.GetRotation() * Calibration).Rotator().Pitch > 75.f);
		}
		else
		{
			TestTrue(TEXT("Low ready lowers the wrist below the chest without withdrawing behind the eye"), Tucked.TransformPosition(GripLocal).Equals(FVector(19.f, 8.f, -36.f), 0.001f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWallLongWeaponTest, "CampusOps.Experience.Viewmodel.WallLongWeaponVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWallLongWeaponTest::RunTest(const FString&)
{
	FMinimalViewInfo View;
	View.Location = FVector::ZeroVector;
	View.Rotation = FRotator::ZeroRotator;
	View.FOV = 100.f;
	View.FirstPersonFOV = 74.f;
	View.FirstPersonScale = 0.6f;
	for (const FBox Box : { FBox(FVector(-30.2f, -3.82f, -10.2f), FVector(82.62f, 3.82f, 21.32f)),
		FBox(FVector(-35.8f, -3.37f, -19.9f), FVector(77.62f, 2.82f, 21.37f)) })
	{
		const FVector Center = Box.GetCenter();
		const FVector Probes[] = { FVector(Box.Max.X, Center.Y, Center.Z), FVector(Center.X, Box.Min.Y, Box.Max.Z), FVector(Center.X, Box.Max.Y, Box.Max.Z) };
		FWallContacts Contacts;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Probes); ++Index)
		{
			Contacts.Add({ Index, -FVector::ForwardVector, FVector(30.4f, 0.f, 0.f) });
		}
		auto CorrectionFor = [&](const FTransform& Gun)
		{
			FWallProbeEnds Ends;
			for (const FVector& Probe : Probes)
			{
				Ends.Add(View.TransformWorldToFirstPerson(Gun.TransformPosition(Probe), false));
			}
			return WallProbeCorrection(Ends, Contacts);
		};
		const FTransform OldLow(FRotator(-48.f, 0.f, 0.f), FVector(19.f, 7.5f, -25.9f));
		const FVector OldPush = CorrectionFor(OldLow);
		TestTrue(TEXT("The former shallow tuck reproduces a long weapon being pushed behind the eye"), OldLow.GetLocation().X + OldPush.X / View.FirstPersonScale < 1.f);
		const FTransform Ready(FQuat::Identity, FVector(23.f, 7.5f, -20.f));
		const FTransform Close = WallTuckPose(Ready, FQuat::Identity, FVector::ZeroVector, 1.f, -80.f, FVector(-4.f, 0.f, 2.f));
		const FVector Push = CorrectionFor(Close);
		TestTrue(TEXT("A long suppressed rifle clears a capsule-distance wall without retreating behind the eye"), Close.GetLocation().X + Push.X / View.FirstPersonScale >= 18.f);
		FWallProbeEnds CloseEnds;
		for (const FVector& Probe : Probes)
		{
			CloseEnds.Add(View.TransformWorldToFirstPerson(Close.TransformPosition(Probe), false));
		}
		const float Advance = WallForwardAdvance(CloseEnds, Contacts, FVector(View.FirstPersonScale, 0.f, 0.f), 30.f - Close.GetLocation().X);
		FTransform Comfortable = Close;
		Comfortable.AddToTranslation(FVector(Advance, 0.f, 0.f));
		TestTrue(TEXT("The hand uses available clearance instead of staying next to the eye"), Comfortable.GetLocation().X > 23.f && Comfortable.GetLocation().X <= 30.f);
		TestTrue(TEXT("Moving the hand forward retains the wall safety margin"), CorrectionFor(Comfortable).IsNearlyZero(0.001f));
		TestTrue(TEXT("Low ready keeps the wrist below the chest"), Close.GetLocation().Z <= -34.f && Close.GetLocation().Z >= -36.f);
		const float TanHalfV = FMath::Tan(FMath::DegreesToRadians(View.FirstPersonFOV * 0.5f)) / (16.f / 9.f);
		const float StockTop = Box.Min.X < -33.f ? 7.9f : 13.4f;
		for (const FVector StockPoint : { FVector(Box.Min.X, Center.Y, 0.f), FVector(Box.Min.X, Center.Y, StockTop), FVector(-3.f, Center.Y, 0.f), FVector(-3.f, Center.Y, StockTop) })
		{
			const FVector InView = Comfortable.TransformPosition(StockPoint);
			TestTrue(TEXT("The stock upper edge stays in the lower half instead of forming a central vertical obstruction"), InView.Z <= InView.X * TanHalfV * 0.1f && InView.Z < 2.f);
			TestTrue(TEXT("The lowered stock keeps its distance from the eye"), InView.X > 15.f && InView.Size() > 18.f);
		}
		FTransform High = WallTuckPose(Ready, FQuat::Identity, FVector::ZeroVector, 1.f, -80.f, FVector(-4.f, 0.f, 2.f), true);
		FWallProbeEnds HighEnds;
		for (const FVector& Probe : Probes)
		{
			HighEnds.Add(View.TransformWorldToFirstPerson(High.TransformPosition(Probe), false));
		}
		High.AddToTranslation(FVector(WallForwardAdvance(HighEnds, Contacts, FVector(View.FirstPersonScale, 0.f, 0.f), 30.f - High.GetLocation().X), 0.f, 0.f));
		TestTrue(TEXT("The high tactical carry still satisfies the close-wall clearance planes"), CorrectionFor(High).IsNearlyZero(0.001f));
		TestTrue(TEXT("High ready lowers and moves the wrist outboard"), High.GetLocation().Z <= -50.f && High.GetLocation().Y >= 18.f);
		const FVector Scope = High.TransformPosition(FVector(6.f, 0.f, Box.Max.Z));
		TestTrue(TEXT("The high-ready eyepiece stays below the viewport instead of beside the eye"), Scope.Z < -FMath::Max(0.0, Scope.X) * TanHalfV && Scope.Size() > 30.f);
		for (const bool bHighReady : { false, true })
		{
			for (int32 Pitch = 0; Pitch <= 76; ++Pitch)
			{
				const float Carry = Pitch / 76.f;
				const FTransform Moving(FRotator(static_cast<float>(Pitch), -27.f * Carry, 95.f * Carry), Ready.GetLocation());
				const FTransform Safe = WallTuckPose(Moving, FQuat::Identity, FVector::ZeroVector, 1.f, -80.f, FVector(-4.f, 0.f, 2.f), bHighReady);
				const FVector MovingPush = CorrectionFor(Safe);
				TestTrue(TEXT("Every sprint transition angle retains a safe close pose, including 20 and 30 degrees"), Safe.GetLocation().X + MovingPush.X / View.FirstPersonScale >= 18.f);
				TestTrue(TEXT("The chosen high or low ready never reverses during a sprint transition"), Safe.Rotator().Pitch * (bHighReady ? 1.f : -1.f) > 75.f);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusReloadReachTest, "CampusOps.Experience.Viewmodel.ReloadReceiverClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusReloadReachTest::RunTest(const FString&)
{
	const FVector Grip(28.f, -4.8f, 5.4f);
	const FVector Magazine(11.3f, -8.5f, -2.f);
	TestTrue(TEXT("Reach starts on the authored grip"), ReloadReach(Grip, Magazine, 0.f, -8.5f).Equals(Grip));
	TestTrue(TEXT("Reach ends on the magazine"), ReloadReach(Grip, Magazine, 1.f, -8.5f).Equals(Magazine));
	const FBox ReceiverWithGlove(FVector(-9.f, -5.9f, -1.f), FVector(43.f, 5.9f, 16.f));
	TestTrue(TEXT("Original straight path reproduces the MK14 receiver intersection"), ReceiverWithGlove.IsInside(FMath::Lerp(Grip, Magazine, 0.25f)));
	for (int32 Sample = 25; Sample <= 75; ++Sample)
	{
		const float T = Sample / 100.f;
		TestFalse(TEXT("Outbound hand clears the receiver"), ReceiverWithGlove.IsInside(ReloadReach(Grip, Magazine, T, -8.5f)));
		TestFalse(TEXT("Cancelled reload return clears the receiver"), ReceiverWithGlove.IsInside(ReloadReach(Magazine, Grip, T, -8.5f)));
	}
	TestTrue(TEXT("Return settles exactly on the grip"), ReloadReach(Magazine, Grip, 2.f, -8.5f).Equals(Grip));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusKnifeContactPoseTest, "CampusOps.Experience.Viewmodel.KnifeContactTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusKnifeContactPoseTest::RunTest(const FString&)
{
	for (const bool bHeavy : { false, true })
	{
		const float Contact = bHeavy ? KnifeStabContact : KnifeSlashContact;
		const float Duration = bHeavy ? KnifeStabDuration : KnifeSlashDuration;
		TestEqual(TEXT("Strike begins in the guard"), KnifeStrikeWeight(0.f, bHeavy), 0.f);
		TestEqual(TEXT("Blade reaches its strike pose at damage contact"), KnifeStrikeWeight(Contact, bHeavy), 1.f);
		TestTrue(TEXT("Blade is still approaching immediately before contact"), KnifeStrikeWeight(Contact - 0.02f, bHeavy) < 1.f);
		TestEqual(TEXT("Recovery returns completely to the guard"), KnifeStrikeWeight(Duration, bHeavy), 0.f);
	}
	return true;
}
#endif
