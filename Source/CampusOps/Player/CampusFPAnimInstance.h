#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "CampusFPAnimInstance.generated.h"

/**
 * Carries both hands (and the weapon rigidly attached to hand_r) by one component-space offset while the
 * shoulders keep the authored pose, so procedural weapon poses never swing the upper arms into the camera.
 */
struct FCampusAnimNode_HandsOffset : public FAnimNode_SkeletalControlBase
{
	FCampusAnimNode_HandsOffset();

	/** hand_new = hand_animated * Offset (component space). */
	FTransform Offset = FTransform::Identity;
	/** right arm as animated, before the offset (component space) */
	FTransform AnimatedUpperR = FTransform::Identity;
	FTransform AnimatedLowerR = FTransform::Identity;
	FTransform AnimatedHandR = FTransform::Identity;
	bool bHasAnimatedHand = false;
	/** Largest distance a hand fell short of its target (cm). */
	float ReachError = 0.f;
	/** Support-arm IK pole offset (component space), handed on in lowerarm_l space as this node leaves it. */
	FVector LeftPoleOffsetCS = FVector::ZeroVector;
	FVector RightPoleOffsetCS = FVector::ZeroVector;
	FVector* LeftPoleOut = nullptr;

protected:
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	/** upperarm, lowerarm, hand for the right then the left arm. */
	FBoneReference Chain[6];
};

/**
 * Opens the support hand's fingers part way from the animated grip toward the open reference pose (local space):
 * the tight handguard curl becomes a grip round something round and fat, like a grenade.
 */
struct FCampusAnimNode_LeftFingers : public FAnimNode_Base
{
	FPoseLink Source;
	/** 0 = the animated grip, 1 = the open reference hand, below 0 curled tighter than the grip. */
	float FingerOpen = 0.f;
	float ThumbOpen = 0.f;

	FCampusAnimNode_LeftFingers();
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;

private:
	TArray<FBoneReference> Bones;
	TArray<bool> IsThumb;
};

/** Native anim graph: looping base pose -> DefaultSlot (montages) -> left fingers -> hands offset -> left hand IK onto the weapon. No Anim Blueprint required. */
struct FCampusFPAnimInstanceProxy : public FAnimInstanceProxy
{
	FCampusFPAnimInstanceProxy() = default;
	explicit FCampusFPAnimInstanceProxy(UAnimInstance* InAnimInstance);

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void PostEvaluate(UAnimInstance* InAnimInstance) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &ToLocal; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override
	{
		OutNodes.Add(&ToLocal);
		OutNodes.Add(&LeftHandTurn);
		OutNodes.Add(&LeftHandIK);
		OutNodes.Add(&HandsOffset);
		OutNodes.Add(&ToComponent);
		OutNodes.Add(&LeftFingers);
		OutNodes.Add(&SlotNode);
		OutNodes.Add(&BasePlayer);
	}

private:
	FAnimNode_SequencePlayer_Standalone BasePlayer;
	FAnimNode_Slot SlotNode;
	FCampusAnimNode_LeftFingers LeftFingers;
	FAnimNode_ConvertLocalToComponentSpace ToComponent;
	FCampusAnimNode_HandsOffset HandsOffset;
	FAnimNode_TwoBoneIK LeftHandIK;
	/** Turns the support hand in place (component space) once IK has placed it: grips on magazines and handles. */
	FAnimNode_ModifyBone LeftHandTurn;
	FAnimNode_ConvertComponentToLocalSpace ToLocal;
};

UCLASS(Transient, NotBlueprintable)
class CAMPUSOPS_API UCampusFPAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FCampusMantleHandTest;
	friend class FCampusReloadMotionTest;
#endif

public:
	void SetBasePose(UAnimSequenceBase* InSequence) { BasePose = InSequence; }
	UAnimSequenceBase* GetBasePose() const { return BasePose; }

	/** Plays a one-shot animation over the base pose, scaled to fit Duration (seconds). */
	void PlayAction(UAnimSequenceBase* Sequence, float Duration, float BlendIn = 0.15f, float BlendOut = 0.2f);
	void StopActions(float BlendOut = 0.15f);

	/** Left hand target in hand_r bone space; the weapon is rigidly attached to the right hand so this is constant per weapon. */
	void SetLeftHandIK(const FVector& InEffectorInRightHand, float InAlpha)
	{
		LeftHandEffector = InEffectorInRightHand;
		LeftHandAlpha = InAlpha;
	}

	/** Moves the support arm's IK pole off the animated elbow (component space), to keep the elbow out of view. */
	void SetLeftElbowPole(const FVector& InOffsetCS) { LeftElbowPoleOffset = InOffsetCS; }

	/** Extra turn of the support hand about its wrist, component space (hand_new = Delta * hand), weighted. */
	void SetLeftHandTurn(const FQuat& InDelta, float InAlpha)
	{
		LeftHandTurnDelta = InDelta;
		LeftHandTurnAlpha = InAlpha;
	}

	/** Opens the support hand's fingers / thumb from the animated grip toward the open hand (0..1). */
	void SetLeftFingers(float InFingerOpen, float InThumbOpen)
	{
		LeftFingerOpen = InFingerOpen;
		LeftThumbOpen = InThumbOpen;
	}

	/** Procedural weapon pose applied to both hands, as a component-space offset (hand_new = hand * Offset). */
	void SetHandsOffset(const FTransform& InOffset) { HandsOffset = InOffset; }
	void SetRightElbowPole(const FVector& OffsetCS) { RightElbowPoleOffset = OffsetCS; }
	/** hand_r component-space transform from the animation alone (last evaluation), before the hands offset. */
	const FTransform& GetAnimatedHandR() const { return AnimatedHandR; }
	/** 与动画节点共用双骨求解，避免超出臂长时相机空间目标漂移 */
	FTransform GetOffsetHandR(const FTransform& Offset) const;
	bool HasAnimatedHand() const { return bHasAnimatedHand; }
	float GetHandsReachError() const { return HandsReachError; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> BasePose;

	FVector LeftHandEffector = FVector::ZeroVector;
	float LeftHandAlpha = 0.f;
	FVector LeftElbowPoleOffset = FVector::ZeroVector;
	FQuat LeftHandTurnDelta = FQuat::Identity;
	float LeftHandTurnAlpha = 0.f;
	float LeftFingerOpen = 0.f;
	float LeftThumbOpen = 0.f;
	FTransform HandsOffset = FTransform::Identity;
	FVector RightElbowPoleOffset = FVector::ZeroVector;
	FTransform AnimatedUpperR = FTransform::Identity;
	FTransform AnimatedLowerR = FTransform::Identity;
	FTransform AnimatedHandR = FTransform::Identity;
	bool bHasAnimatedHand = false;
	float HandsReachError = 0.f;

	friend struct FCampusFPAnimInstanceProxy;
};
