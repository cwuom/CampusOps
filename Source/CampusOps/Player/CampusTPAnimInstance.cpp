#include "Player/CampusTPAnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "TwoBoneIK.h"
#include "Engine/World.h"

const FName UCampusTPAnimInstance::UpperSlotName(TEXT("UpperBody"));

namespace CampusTPArm
{
	/** How long each stage's motion runs before the arm is back on the weapon (stage seconds). */
	constexpr float SwingLength = 0.7f;
	constexpr float PutAwayLength = 0.4f;

	float Smooth(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / FMath::Max(B - A, 0.001f), 0.f, 1.f);
		return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
	}

	FVector Bezier(const FVector& A, const FVector& Ctrl, const FVector& B, float T)
	{
		return FMath::Lerp(FMath::Lerp(A, Ctrl, T), FMath::Lerp(Ctrl, B, T), T);
	}

	// Body frame offsets from the left shoulder (cm): X forward, Y toward the body's middle (right), Z up. The head's
	// centre sits about 18 right and 14 up of the shoulder, the arm reaches about 55.
	const FVector Pouch(4.f, 2.f, -44.f);
	const FVector Present(22.f, 8.f, -28.f);
	const FVector Pin(30.f, 18.f, -22.f);
	const FVector Hold(27.f, 9.f, -19.f);
	const FVector WindCtrl(8.f, -16.f, 2.f);
	const FVector Wind(-16.f, -8.f, 18.f);
	const FVector WhipCtrl(8.f, -2.f, 34.f);
	const FVector Release(32.f, 6.f, 28.f);
	const FVector FollowCtrl(42.f, 18.f, 2.f);
	const FVector Follow(30.f, 24.f, -32.f);
	// Elbow pole directions: down and out under a held grenade, out to the side at shoulder height in the wind-up
	// (so the forearm stands up beside the head, never across it), still under the hand at the release.
	const FVector PoleLow(0.f, -0.6f, -1.f);
	const FVector PoleWind(-0.3f, -1.f, -0.15f);
	const FVector PoleRelease(0.2f, -0.8f, -0.45f);
	const FVector PoleFollow(0.1f, -0.4f, -1.f);
	// Chest twist about the spine (degrees, positive = left shoulder back).
	constexpr float TwistHold = 6.f;
	constexpr float TwistWind = 30.f;
	constexpr float TwistRelease = -12.f;
	constexpr float TwistFollow = -24.f;
}

FCampusAnimNode_BodyThrow::FCampusAnimNode_BodyThrow()
{
	Spine = FBoneReference(TEXT("spine_03"));
	Head = FBoneReference(TEXT("head"));
	Upper = FBoneReference(TEXT("upperarm_l"));
	Lower = FBoneReference(TEXT("lowerarm_l"));
	Hand = FBoneReference(TEXT("hand_l"));
}

void FCampusAnimNode_BodyThrow::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	Spine.Initialize(RequiredBones);
	Head.Initialize(RequiredBones);
	Upper.Initialize(RequiredBones);
	Lower.Initialize(RequiredBones);
	Hand.Initialize(RequiredBones);
}

bool FCampusAnimNode_BodyThrow::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return Stage != ECampusBodyThrow::None && Spine.IsValidToEvaluate(RequiredBones) && Head.IsValidToEvaluate(RequiredBones)
		&& Upper.IsValidToEvaluate(RequiredBones) && Lower.IsValidToEvaluate(RequiredBones) && Hand.IsValidToEvaluate(RequiredBones);
}

void FCampusAnimNode_BodyThrow::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	using namespace CampusTPArm;
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex SpineI = Spine.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex HeadI = Head.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex UpperI = Upper.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex LowerI = Lower.GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex HandI = Hand.GetCompactPoseIndex(Bones);

	if (Stage != LastStage)
	{
		// Each stage sets off from wherever the last one had the arm.
		StageHand = LastHand;
		StagePole = LastPole;
		StageTwist = LastTwist;
		LastStage = Stage;
	}

	// The throw path, and how far it overrides the animated arm.
	float Weight = 1.f;
	FVector H = Hold;
	FVector P = PoleLow;
	float Twist = TwistHold;
	const float T = Age;
	switch (Stage)
	{
	case ECampusBodyThrow::Draw:
		// Down to the pouch, up chest high, a reach across to the gun hand for the pin, then held.
		Weight = Smooth(0.f, 0.1f, T);
		H = T < 0.12f ? Pouch : FMath::Lerp(Pouch, Present, Smooth(0.12f, 0.3f, T));
		H = FMath::Lerp(H, Hold, Smooth(0.32f, 0.5f, T));
		H = FMath::Lerp(H, Pin, FMath::Sin(PI * FMath::Clamp((T - 0.2f) / 0.2f, 0.f, 1.f)));
		H += FVector(0.f, 0.6f * FMath::Sin(T * 2.9f), 0.7f * FMath::Sin(T * 2.1f)) * Smooth(0.45f, 0.8f, T);
		Twist = TwistHold * Smooth(0.1f, 0.4f, T);
		break;
	case ECampusBodyThrow::Swing:
		// Draw back 0..0.11, whip over the top 0..0.17 (released at 0.17, with ACampusCharacter's throw), follow
		// through to 0.36, then back onto the weapon.
		if (T < 0.11f)
		{
			const float A = Smooth(0.f, 0.11f, T);
			H = Bezier(StageHand, WindCtrl, Wind, A);
			P = FMath::Lerp(StagePole, PoleWind, A);
			Twist = FMath::Lerp(StageTwist, TwistWind, A);
		}
		else if (T < 0.17f)
		{
			const float L = FMath::Clamp((T - 0.11f) / 0.06f, 0.f, 1.f);
			const float A = FMath::Lerp(L, Smooth(0.11f, 0.17f, T), 0.3f);
			H = Bezier(Wind, WhipCtrl, Release, A);
			P = FMath::Lerp(PoleWind, PoleRelease, A);
			Twist = FMath::Lerp(TwistWind, TwistRelease, A);
		}
		else
		{
			const float A = 1.f - FMath::Pow(1.f - FMath::Clamp((T - 0.17f) / 0.19f, 0.f, 1.f), 3.f);
			H = Bezier(Release, FollowCtrl, Follow, A);
			P = FMath::Lerp(PoleRelease, PoleFollow, A);
			Twist = FMath::Lerp(TwistRelease, TwistFollow, A);
		}
		Weight = 1.f - Smooth(0.36f, SwingLength, T);
		break;
	case ECampusBodyThrow::PutAway:
		H = FMath::Lerp(StageHand, Pouch, Smooth(0.f, 0.25f, T));
		P = FMath::Lerp(StagePole, PoleLow, Smooth(0.f, 0.25f, T));
		Twist = FMath::Lerp(StageTwist, 0.f, Smooth(0.f, 0.3f, T));
		Weight = 1.f - Smooth(0.1f, PutAwayLength, T);
		break;
	default:
		break;
	}
	LastHand = H;
	LastPole = P;
	LastTwist = Twist;
	if (Weight <= 0.001f)
	{
		return;
	}

	const FVector F = ForwardCS.GetSafeNormal();
	const FVector U = (UpCS - F * (UpCS | F)).GetSafeNormal();
	const FVector R = U ^ F;
	// Throws aimed up or down tilt the path above the chest with the aim.
	const float Pitch = FMath::DegreesToRadians(FMath::Clamp(AimPitch, -45.f, 60.f) * 0.6f) * FMath::Clamp((H.Z + 10.f) / 30.f, 0.f, 1.f);
	auto ToCS = [&](const FVector& V)
	{
		const float Fw = V.X * FMath::Cos(Pitch) - V.Z * FMath::Sin(Pitch);
		const float Up = V.X * FMath::Sin(Pitch) + V.Z * FMath::Cos(Pitch);
		return F * Fw + R * V.Y + U * Up;
	};

	// The chest turns about the spine; the left shoulder swings back for positive twist.
	const FTransform SpineCS = Output.Pose.GetComponentSpaceTransform(SpineI);
	const FVector Pivot = SpineCS.GetLocation();
	const FVector ShoulderAnim = Output.Pose.GetComponentSpaceTransform(UpperI).GetLocation();
	float Sign = 1.f;
	{
		const FVector Turned = FQuat(U, 0.1f).RotateVector(ShoulderAnim - Pivot);
		Sign = ((Turned - (ShoulderAnim - Pivot)) | F) < 0.f ? 1.f : -1.f;
	}
	const FQuat TwistQ(U, Sign * FMath::DegreesToRadians(Twist * Weight));
	auto Turn = [&](const FTransform& X)
	{
		return FTransform(TwistQ * X.GetRotation(), Pivot + TwistQ.RotateVector(X.GetLocation() - Pivot), X.GetScale3D());
	};

	const FTransform HeadAnim = Output.Pose.GetComponentSpaceTransform(HeadI);
	FTransform UpperCS = Turn(Output.Pose.GetComponentSpaceTransform(UpperI));
	FTransform LowerCS = Turn(Output.Pose.GetComponentSpaceTransform(LowerI));
	FTransform HandCS = Turn(Output.Pose.GetComponentSpaceTransform(HandI));
	// The wrist keeps its animated set on the forearm.
	const FQuat WristRel = LowerCS.GetRotation().Inverse() * HandCS.GetRotation();

	const FVector Shoulder = UpperCS.GetLocation();
	const FVector Effector = FMath::Lerp(HandCS.GetLocation(), Shoulder + ToCS(H), Weight);
	const FVector Joint = FMath::Lerp(LowerCS.GetLocation(), Shoulder + ToCS(P.GetSafeNormal()) * 40.f, Weight);
	AnimationCore::SolveTwoBoneIK(UpperCS, LowerCS, HandCS, Joint, Effector, false, 1.0, 1.0);
	HandCS.SetRotation(LowerCS.GetRotation() * WristRel);

	OutBoneTransforms.Add(FBoneTransform(SpineI, Turn(SpineCS)));
	// The head stays on the aim while the chest turns under it.
	OutBoneTransforms.Add(FBoneTransform(HeadI, FTransform(HeadAnim.GetRotation(), Pivot + TwistQ.RotateVector(HeadAnim.GetLocation() - Pivot), HeadAnim.GetScale3D())));
	OutBoneTransforms.Add(FBoneTransform(UpperI, UpperCS));
	OutBoneTransforms.Add(FBoneTransform(LowerI, LowerCS));
	OutBoneTransforms.Add(FBoneTransform(HandI, HandCS));
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

FCampusTPAnimInstanceProxy::FCampusTPAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
	if (const UCampusTPAnimInstance* Instance = Cast<UCampusTPAnimInstance>(InAnimInstance))
	{
		Locomotion.InstanceClass = Instance->LocomotionClass;
	}
	SaveLocomotion.Pose.SetLinkNode(&Locomotion);
	UseBase.LinkToCachingNode.SetLinkNode(&SaveLocomotion);
	UseSlotSource.LinkToCachingNode.SetLinkNode(&SaveLocomotion);

	UpperSlot.SlotName = UCampusTPAnimInstance::UpperSlotName;
	UpperSlot.bAlwaysUpdateSourcePose = true;
	UpperSlot.Source.SetLinkNode(&UseSlotSource);

	Layered.BasePose.SetLinkNode(&UseBase);
	Layered.AddPose();
	Layered.BlendPoses[0].SetLinkNode(&UpperSlot);
	FBranchFilter SpineFilter;
	SpineFilter.BoneName = TEXT("spine_01");
	SpineFilter.BlendDepth = 0;
	Layered.LayerSetup[0].BranchFilters.Add(SpineFilter);
	Layered.bMeshSpaceRotationBlend = true;

	ToComponent.LocalPose.SetLinkNode(&Layered);
	BodyThrow.ComponentPose.SetLinkNode(&ToComponent);
	ToLocal.ComponentPose.SetLinkNode(&BodyThrow);
}

void FCampusTPAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UCampusTPAnimInstance* Instance = Cast<UCampusTPAnimInstance>(InAnimInstance))
	{
		BodyThrow.Stage = Instance->GetThrowStage();
		BodyThrow.Age = Instance->GetThrowAge();
		if (const USkeletalMeshComponent* Mesh = Instance->GetSkelMeshComponent())
		{
			BodyThrow.ForwardCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Instance->ThrowForward);
			BodyThrow.UpCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(Instance->ThrowUp);
		}
		BodyThrow.AimPitch = Instance->ThrowPitch;
		BodyThrow.Alpha = 1.f;
	}
}

void FCampusTPAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
	// Native graphs have no compiler-generated cached-pose queue, so the shared locomotion branch is updated here,
	// once, after both of its users have registered their weights.
	SaveLocomotion.PostGraphUpdate();
}

FAnimInstanceProxy* UCampusTPAnimInstance::CreateAnimInstanceProxy()
{
	return new FCampusTPAnimInstanceProxy(this);
}

void UCampusTPAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FCampusTPAnimInstanceProxy*>(InProxy);
}

void UCampusTPAnimInstance::SetLocomotionClass(TSubclassOf<UAnimInstance> InClass)
{
	if (LocomotionClass == InClass)
	{
		return;
	}
	LocomotionClass = InClass;
	GetProxyOnGameThread<FCampusTPAnimInstanceProxy>().Locomotion.SetAnimClass(InClass, this);
}

void UCampusTPAnimInstance::PlayUpper(UAnimSequenceBase* Sequence, float Duration, float BlendIn, float BlendOut)
{
	if (!Sequence)
	{
		return;
	}
	const float Length = Sequence->GetPlayLength();
	const float Rate = (Duration > KINDA_SMALL_NUMBER && Length > KINDA_SMALL_NUMBER) ? Length / Duration : 1.f;
	PlaySlotAnimationAsDynamicMontage(Sequence, UpperSlotName, BlendIn, BlendOut, Rate);
}

void UCampusTPAnimInstance::StopUpper(float BlendOut)
{
	StopSlotAnimation(BlendOut, UpperSlotName);
}

void UCampusTPAnimInstance::SetThrowStage(ECampusBodyThrow Stage, float Rate)
{
	ThrowStage = Stage;
	ThrowRate = FMath::Max(Rate, 0.1f);
	const UWorld* World = GetWorld();
	ThrowStageStart = World ? World->GetTimeSeconds() : 0.0;
}

float UCampusTPAnimInstance::GetThrowAge() const
{
	const UWorld* World = GetWorld();
	return World ? (float)(World->GetTimeSeconds() - ThrowStageStart) * ThrowRate : 0.f;
}

ECampusBodyThrow UCampusTPAnimInstance::GetThrowStage() const
{
	const float Age = GetThrowAge();
	if ((ThrowStage == ECampusBodyThrow::Swing && Age >= CampusTPArm::SwingLength) || (ThrowStage == ECampusBodyThrow::PutAway && Age >= CampusTPArm::PutAwayLength))
	{
		return ECampusBodyThrow::None;
	}
	return ThrowStage;
}
