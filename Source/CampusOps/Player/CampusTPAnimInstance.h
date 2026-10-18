#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/AnimNode_SaveCachedPose.h"
#include "Animation/AnimNode_UseCachedPose.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "CampusTPAnimInstance.generated.h"

/** Third-person throw stages, in step with ACampusCharacter's throw (replicated to every machine). */
enum class ECampusBodyThrow : uint8 { None, Draw, Swing, PutAway };

/**
 * Procedural overhand throw for the body: the left arm by two-bone IK along a path laid round the shoulder in the
 * body's frame (pouch, chest-high hold, pin, wind-up beside the head, release above and ahead of it, follow-through
 * down across the body), the chest twisting back into the wind-up and through the release, the head held on the aim.
 * The path keeps the hand outside the head and torso, and every segment eases into the next so the arm never snaps.
 */
struct FCampusAnimNode_BodyThrow : public FAnimNode_SkeletalControlBase
{
	FCampusAnimNode_BodyThrow();

	ECampusBodyThrow Stage = ECampusBodyThrow::None;
	/** Seconds into the stage, at the throw's playback rate. */
	float Age = 0.f;
	/** The body's facing and up (component space). */
	FVector ForwardCS = FVector(0.f, 1.f, 0.f);
	FVector UpCS = FVector(0.f, 0.f, 1.f);
	/** Aim pitch (degrees, up positive): the release rises with it. */
	float AimPitch = 0.f;

protected:
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	FBoneReference Spine;
	FBoneReference Head;
	FBoneReference Upper;
	FBoneReference Lower;
	FBoneReference Hand;
	/** Hand target and elbow pole last evaluated (body frame, from the shoulder): where the next stage starts. */
	FVector LastHand = FVector::ZeroVector;
	FVector LastPole = FVector::ZeroVector;
	float LastTwist = 0.f;
	FVector StageHand = FVector::ZeroVector;
	FVector StagePole = FVector::ZeroVector;
	float StageTwist = 0.f;
	ECampusBodyThrow LastStage = ECampusBodyThrow::None;
};

/**
 * Native third-person graph: the template locomotion blueprint (rifle / pistol) runs as a linked graph, and an
 * upper-body slot layered from spine_01 plays fire, reload, equip and hit-react montages over it.
 */
struct FCampusTPAnimInstanceProxy : public FAnimInstanceProxy
{
	FCampusTPAnimInstanceProxy() = default;
	explicit FCampusTPAnimInstanceProxy(UAnimInstance* InAnimInstance);

	FAnimNode_LinkedAnimGraph Locomotion;

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &ToLocal; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override
	{
		OutNodes.Add(&ToLocal);
		OutNodes.Add(&BodyThrow);
		OutNodes.Add(&ToComponent);
		OutNodes.Add(&Layered);
		OutNodes.Add(&UpperSlot);
		OutNodes.Add(&UseBase);
		OutNodes.Add(&UseSlotSource);
		OutNodes.Add(&SaveLocomotion);
		OutNodes.Add(&Locomotion);
	}

private:
	FAnimNode_SaveCachedPose SaveLocomotion;
	FAnimNode_UseCachedPose UseBase;
	FAnimNode_UseCachedPose UseSlotSource;
	FAnimNode_Slot UpperSlot;
	FAnimNode_LayeredBoneBlend Layered;
	FAnimNode_ConvertLocalToComponentSpace ToComponent;
	FCampusAnimNode_BodyThrow BodyThrow;
	FAnimNode_ConvertComponentToLocalSpace ToLocal;
};

UCLASS(Transient, NotBlueprintable)
class CAMPUSOPS_API UCampusTPAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	static const FName UpperSlotName;

	/** Template locomotion blueprint run underneath the upper-body slot. */
	void SetLocomotionClass(TSubclassOf<UAnimInstance> InClass);
	/** One-shot upper-body animation, scaled to Duration seconds (0 = natural length). */
	void PlayUpper(UAnimSequenceBase* Sequence, float Duration = 0.f, float BlendIn = 0.1f, float BlendOut = 0.2f);
	void StopUpper(float BlendOut = 0.15f);

	/** Starts a throw stage now; Rate is the throw's playback rate. */
	void SetThrowStage(ECampusBodyThrow Stage, float Rate = 1.f);
	ECampusBodyThrow GetThrowStage() const;
	/** Seconds into the current stage at its playback rate; the stage ends itself once its motion is done. */
	float GetThrowAge() const;
	/** Body facing / up in world space and the aim pitch, for the throw path. */
	void SetThrowFrame(const FVector& Forward, const FVector& Up, float Pitch)
	{
		ThrowForward = Forward;
		ThrowUp = Up;
		ThrowPitch = Pitch;
	}

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> LocomotionClass;

	ECampusBodyThrow ThrowStage = ECampusBodyThrow::None;
	double ThrowStageStart = 0.0;
	float ThrowRate = 1.f;
	FVector ThrowForward = FVector::ForwardVector;
	FVector ThrowUp = FVector::UpVector;
	float ThrowPitch = 0.f;

	friend struct FCampusTPAnimInstanceProxy;
};
