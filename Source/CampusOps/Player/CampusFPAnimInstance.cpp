#include "Player/CampusFPAnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "TwoBoneIK.h"

static const FName CampusDefaultSlot(TEXT("DefaultSlot"));

namespace
{
	float SolveOffsetArm(FTransform& UpperCS, FTransform& LowerCS, FTransform& HandCS, const FTransform& Offset, const FVector& PoleOffset = FVector::ZeroVector)
	{
		const FTransform Target = HandCS * Offset;
		// 保留动画的肘部方向，预测与实际节点也必须使用相同的伸展限制
		AnimationCore::SolveTwoBoneIK(UpperCS, LowerCS, HandCS, LowerCS.GetLocation() + PoleOffset, Target.GetLocation(), true, 1.0, 1.1);
		HandCS.SetRotation(Target.GetRotation());
		return static_cast<float>(FVector::Dist(HandCS.GetLocation(), Target.GetLocation()));
	}
}

FCampusAnimNode_HandsOffset::FCampusAnimNode_HandsOffset()
{
	static const TCHAR* Names[] = { TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l") };
	for (int32 i = 0; i < 6; ++i)
	{
		Chain[i] = FBoneReference(FName(Names[i]));
	}
}

void FCampusAnimNode_HandsOffset::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (FBoneReference& Bone : Chain)
	{
		Bone.Initialize(RequiredBones);
	}
}

bool FCampusAnimNode_HandsOffset::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	for (const FBoneReference& Bone : Chain)
	{
		if (!Bone.IsValidToEvaluate(RequiredBones))
		{
			return false;
		}
	}
	return true;
}

void FCampusAnimNode_HandsOffset::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	AnimatedUpperR = Output.Pose.GetComponentSpaceTransform(Chain[0].GetCompactPoseIndex(Bones));
	AnimatedLowerR = Output.Pose.GetComponentSpaceTransform(Chain[1].GetCompactPoseIndex(Bones));
	AnimatedHandR = Output.Pose.GetComponentSpaceTransform(Chain[2].GetCompactPoseIndex(Bones));
	bHasAnimatedHand = true;
	ReachError = 0.f;
	// The IK after this node reads its pole in the space of lowerarm_l as it leaves here, not as it ends up.
	auto HandOnPole = [this](const FTransform& LowerLeftCS)
	{
		if (LeftPoleOut)
		{
			*LeftPoleOut = LowerLeftCS.InverseTransformVectorNoScale(LeftPoleOffsetCS);
		}
	};
	if (Offset.Equals(FTransform::Identity, 1.e-4) && RightPoleOffsetCS.IsNearlyZero())
	{
		HandOnPole(Output.Pose.GetComponentSpaceTransform(Chain[4].GetCompactPoseIndex(Bones)));
		return;
	}
	for (int32 Arm = 0; Arm < 2; ++Arm)
	{
		const FCompactPoseBoneIndex Upper = Chain[Arm * 3].GetCompactPoseIndex(Bones);
		const FCompactPoseBoneIndex Lower = Chain[Arm * 3 + 1].GetCompactPoseIndex(Bones);
		const FCompactPoseBoneIndex Hand = Chain[Arm * 3 + 2].GetCompactPoseIndex(Bones);
		FTransform UpperCS = Output.Pose.GetComponentSpaceTransform(Upper);
		FTransform LowerCS = Output.Pose.GetComponentSpaceTransform(Lower);
		FTransform HandCS = Output.Pose.GetComponentSpaceTransform(Hand);
		ReachError = FMath::Max(ReachError, SolveOffsetArm(UpperCS, LowerCS, HandCS, Offset, Arm == 0 ? RightPoleOffsetCS : FVector::ZeroVector));
		if (Arm == 1)
		{
			HandOnPole(LowerCS);
		}
		OutBoneTransforms.Add(FBoneTransform(Upper, UpperCS));
		OutBoneTransforms.Add(FBoneTransform(Lower, LowerCS));
		OutBoneTransforms.Add(FBoneTransform(Hand, HandCS));
	}
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

FCampusAnimNode_LeftFingers::FCampusAnimNode_LeftFingers()
{
	for (const TCHAR* Finger : { TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky"), TEXT("thumb") })
	{
		for (int32 Joint = 1; Joint <= 3; ++Joint)
		{
			Bones.Add(FBoneReference(FName(*FString::Printf(TEXT("%s_%02d_l"), Finger, Joint))));
			IsThumb.Add(FCString::Strcmp(Finger, TEXT("thumb")) == 0);
		}
	}
}

void FCampusAnimNode_LeftFingers::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Source.Initialize(Context);
}

void FCampusAnimNode_LeftFingers::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	for (FBoneReference& Bone : Bones)
	{
		Bone.Initialize(Context.AnimInstanceProxy->GetRequiredBones());
	}
}

void FCampusAnimNode_LeftFingers::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
}

void FCampusAnimNode_LeftFingers::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	if (FMath::Abs(FingerOpen) <= 0.001f && FMath::Abs(ThumbOpen) <= 0.001f)
	{
		return;
	}
	const FBoneContainer& Required = Output.Pose.GetBoneContainer();
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (!Bones[i].IsValidToEvaluate(Required))
		{
			continue;
		}
		const FCompactPoseBoneIndex Index = Bones[i].GetCompactPoseIndex(Required);
		FTransform& Local = Output.Pose[Index];
		// Below 0 the joint carries on past the grip, curling tighter than the animation.
		const FQuat Grip = Local.GetRotation();
		FQuat Delta = Grip.Inverse() * Required.GetRefPoseTransform(Index).GetRotation();
		Delta.EnforceShortestArcWith(FQuat::Identity);
		FVector Axis;
		float Angle;
		Delta.ToAxisAndAngle(Axis, Angle);
		Local.SetRotation((Grip * FQuat(Axis, Angle * (IsThumb[i] ? ThumbOpen : FingerOpen))).GetNormalized());
	}
}

FCampusFPAnimInstanceProxy::FCampusFPAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
	SlotNode.SlotName = CampusDefaultSlot;
	SlotNode.bAlwaysUpdateSourcePose = true;
	SlotNode.Source.SetLinkNode(&BasePlayer);
	BasePlayer.SetLoopAnimation(true);

	LeftFingers.Source.SetLinkNode(&SlotNode);
	ToComponent.LocalPose.SetLinkNode(&LeftFingers);
	HandsOffset.ComponentPose.SetLinkNode(&ToComponent);
	LeftHandIK.ComponentPose.SetLinkNode(&HandsOffset);
	LeftHandTurn.ComponentPose.SetLinkNode(&LeftHandIK);
	ToLocal.ComponentPose.SetLinkNode(&LeftHandTurn);

	LeftHandTurn.BoneToModify.BoneName = TEXT("hand_l");
	LeftHandTurn.TranslationMode = BMM_Ignore;
	LeftHandTurn.ScaleMode = BMM_Ignore;
	LeftHandTurn.RotationMode = BMM_Additive;
	LeftHandTurn.RotationSpace = BCS_ComponentSpace;
	LeftHandTurn.Alpha = 0.f;

	LeftHandIK.IKBone.BoneName = TEXT("hand_l");
	LeftHandIK.EffectorLocationSpace = BCS_BoneSpace;
	LeftHandIK.EffectorTarget = FBoneSocketTarget(TEXT("hand_r"));
	// The animated elbow is the pole, so the arm keeps its authored bend direction.
	LeftHandIK.JointTargetLocationSpace = BCS_BoneSpace;
	LeftHandIK.JointTarget = FBoneSocketTarget(TEXT("lowerarm_l"));
	LeftHandIK.JointTargetLocation = FVector::ZeroVector;
	HandsOffset.LeftPoleOut = &LeftHandIK.JointTargetLocation;
	// A little stretch keeps the glove on the handguard at the edge of reach instead of floating off it.
	LeftHandIK.bAllowStretching = true;
	LeftHandIK.StartStretchRatio = 1.0;
	LeftHandIK.MaxStretchScale = 1.06;
	LeftHandIK.bTakeRotationFromEffectorSpace = false;
	LeftHandIK.bMaintainEffectorRelRot = false;
	LeftHandIK.Alpha = 0.f;
}

void FCampusFPAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	if (const UCampusFPAnimInstance* Instance = Cast<UCampusFPAnimInstance>(InAnimInstance))
	{
		UAnimSequenceBase* Desired = Instance->BasePose;
		if (BasePlayer.GetSequence() != Desired)
		{
			BasePlayer.SetSequence(Desired);
		}
		HandsOffset.Offset = Instance->HandsOffset;
		HandsOffset.RightPoleOffsetCS = Instance->RightElbowPoleOffset;
		LeftHandIK.EffectorLocation = Instance->LeftHandEffector;
		LeftHandIK.Alpha = Instance->LeftHandAlpha;
		HandsOffset.LeftPoleOffsetCS = Instance->LeftElbowPoleOffset;
		LeftHandTurn.Rotation = Instance->LeftHandTurnDelta.Rotator();
		LeftHandTurn.Alpha = Instance->LeftHandTurnAlpha;
		LeftFingers.FingerOpen = Instance->LeftFingerOpen;
		LeftFingers.ThumbOpen = Instance->LeftThumbOpen;
	}
}

void FCampusFPAnimInstanceProxy::PostEvaluate(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::PostEvaluate(InAnimInstance);

	if (UCampusFPAnimInstance* Instance = Cast<UCampusFPAnimInstance>(InAnimInstance))
	{
		Instance->AnimatedUpperR = HandsOffset.AnimatedUpperR;
		Instance->AnimatedLowerR = HandsOffset.AnimatedLowerR;
		Instance->AnimatedHandR = HandsOffset.AnimatedHandR;
		Instance->bHasAnimatedHand = HandsOffset.bHasAnimatedHand;
		Instance->HandsReachError = HandsOffset.ReachError;
	}
}

FTransform UCampusFPAnimInstance::GetOffsetHandR(const FTransform& Offset) const
{
	if (!bHasAnimatedHand)
	{
		return AnimatedHandR * Offset;
	}
	if (Offset.Equals(FTransform::Identity, 1.e-4) && RightElbowPoleOffset.IsNearlyZero())
	{
		return AnimatedHandR;
	}
	FTransform UpperCS = AnimatedUpperR;
	FTransform LowerCS = AnimatedLowerR;
	FTransform HandCS = AnimatedHandR;
	SolveOffsetArm(UpperCS, LowerCS, HandCS, Offset, RightElbowPoleOffset);
	return HandCS;
}

FAnimInstanceProxy* UCampusFPAnimInstance::CreateAnimInstanceProxy()
{
	return new FCampusFPAnimInstanceProxy(this);
}

void UCampusFPAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FCampusFPAnimInstanceProxy*>(InProxy);
}

void UCampusFPAnimInstance::PlayAction(UAnimSequenceBase* Sequence, float Duration, float BlendIn, float BlendOut)
{
	if (!Sequence)
	{
		return;
	}
	const float Length = Sequence->GetPlayLength();
	const float Rate = (Duration > KINDA_SMALL_NUMBER && Length > KINDA_SMALL_NUMBER) ? Length / Duration : 1.f;
	StopSlotAnimation(0.05f, CampusDefaultSlot);
	PlaySlotAnimationAsDynamicMontage(Sequence, CampusDefaultSlot, BlendIn, BlendOut, Rate);
}

void UCampusFPAnimInstance::StopActions(float BlendOut)
{
	StopSlotAnimation(BlendOut, CampusDefaultSlot);
}
