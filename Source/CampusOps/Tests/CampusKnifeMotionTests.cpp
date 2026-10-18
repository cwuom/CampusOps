#include "Player/CampusViewmodelComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CampusAssets.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusFPAnimInstance.h"
#include "Player/CampusMovementComponent.h"
#include "Weapons/CampusWeapon.h"

namespace
{
	class FKnifeMotionWorld
	{
	public:
		FKnifeMotionWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ACampusCharacter>(FVector(0.f, 0.f, 94.f), FRotator::ZeroRotator, Spawn);
			UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
			const bool bHadMotionControls = InputSettings->bEnableMotionControls;
			InputSettings->bEnableMotionControls = true;
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Controller->SetMotionControlsEnabled(false);
			InputSettings->bEnableMotionControls = bHadMotionControls;
			Controller->SetAsLocalPlayerController();
			Controller->SetPlayerState(World->SpawnActor<APlayerState>());
			Controller->Possess(Character);
			Character->ApplyOperatorMesh(0);
			FCampusWeaponLoadout Loadout;
			Loadout.WeaponId = TEXT("KNIFE");
			Weapon = World->SpawnActor<ACampusWeapon>();
			Weapon->InitializeWeapon(Loadout);
			Weapon->SetOwner(Character);
			Weapon->AttachToCharacter(Character);
			Weapon->SetEquipped(true);
			Character->Weapons.Add(Weapon);
			Character->CurrentWeaponIndex = 0;
			Anim = Character->GetArmsAnim();
			if (Anim)
			{
				Anim->SetBasePose(CampusAssets::Anim(CampusAssets::EAnim::PistolIdle));
			}
			Character->GetCampusMovement()->SetMovementMode(MOVE_Walking);
		}

		~FKnifeMotionWorld() { World->DestroyWorld(false); }

		void EvaluateHands(float Step)
		{
			Character->Viewmodel->TickComponent(Step, LEVELTICK_All, nullptr);
			Character->ArmsMesh->TickAnimation(Step, false);
			Character->ArmsMesh->RefreshBoneTransforms();
		}

		FVector HandInCamera(FName Bone) const
		{
			return Character->FirstPersonCamera->GetComponentTransform().InverseTransformPositionNoScale(Character->ArmsMesh->GetBoneLocation(Bone));
		}

		FMinimalViewInfo RenderView() const
		{
			const UCameraComponent* Camera = Character->FirstPersonCamera;
			FMinimalViewInfo View;
			View.Location = Camera->GetComponentLocation();
			View.Rotation = (Camera->GetComponentQuat() * Character->Viewmodel->CameraRotationOffset.Quaternion()).Rotator();
			View.FOV = Character->Viewmodel->CurrentFOV;
			View.FirstPersonFOV = Camera->bEnableFirstPersonFieldOfView ? Character->Viewmodel->CurrentViewmodelFOV : View.FOV;
			View.FirstPersonScale = Camera->bEnableFirstPersonScale ? Camera->FirstPersonScale : 1.f;
			return View;
		}

		FVector RenderedBoneInCamera(FName Bone) const
		{
			const FMinimalViewInfo View = RenderView();
			return FTransform(View.Rotation, View.Location).InverseTransformPositionNoScale(
				View.TransformWorldToFirstPerson(Character->ArmsMesh->GetBoneLocation(Bone), false));
		}

		UWorld* World = nullptr;
		ACampusCharacter* Character = nullptr;
		ACampusWeapon* Weapon = nullptr;
		UCampusFPAnimInstance* Anim = nullptr;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCampusKnifeMotionTest, "CampusOps.Experience.Viewmodel.KnifeMotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCampusKnifeMotionTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Scenario : { TEXT("DynamicGuard"), TEXT("Sequence"), TEXT("EquipInterrupt"), TEXT("ElbowClearance") })
	{
		OutNames.Add(Scenario);
		OutCommands.Add(Scenario);
	}
}

bool FCampusKnifeMotionTest::RunTest(const FString& Parameters)
{
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
	{
		FKnifeMotionWorld Fixture;
		UCampusViewmodelComponent* VM = Fixture.Character->Viewmodel;
		VM->Character = Fixture.Character;
		const FString Case = FString::Printf(TEXT(" (%s, %.0f fps)"), *Parameters, 1.f / Step);
		if (!TestTrue(TEXT("the fixture executes the actual local viewmodel") + Case, Fixture.Character->IsLocalHuman())
			|| !TestNotNull(TEXT("the knife has its actual first-person animation instance") + Case, Fixture.Anim)
			|| !TestTrue(TEXT("the equipped definition is a knife") + Case, Fixture.Weapon->GetDef() && Fixture.Weapon->GetDef()->IsMelee())
			|| !TestTrue(TEXT("both actual wrist bones are available") + Case,
				Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE && Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE))
		{
			return false;
		}
		for (int32 Frame = 0; Frame < FMath::CeilToInt(1.5f / Step); ++Frame) Fixture.EvaluateHands(Step);

		if (Parameters == TEXT("DynamicGuard"))
		{
			float PeakGuardDrift = 0.f;
			// 定格后的误差会消退，比较姿态切换首帧才能发现左拳误用上一帧右手坐标的问题
			for (const int32 Swing : { 2, 1, 3 })
			{
				for (const float Age : { 0.04f, 0.10f, 0.15f, 0.22f, 0.30f, 0.45f })
				{
					VM->SetPoseOverride(TEXT("knifet"), FVector(Age, Swing, 0.f), FRotator::ZeroRotator);
					VM->bKnifeThrustKicked = true;
					Fixture.EvaluateHands(Step);
					const FVector FirstHand = Fixture.HandInCamera(TEXT("hand_l"));
					for (int32 Frame = 0; Frame < FMath::CeilToInt(0.25f / Step); ++Frame) Fixture.EvaluateHands(Step);
					const float Drift = static_cast<float>(FVector::Dist(FirstHand, Fixture.HandInCamera(TEXT("hand_l"))));
					PeakGuardDrift = FMath::Max(PeakGuardDrift, Drift);
				}
			}
			TestTrue(FString::Printf(TEXT("the guard reaches the same point on the first and settled pose frames (%.2f cm)%s"), PeakGuardDrift, *Case), PeakGuardDrift <= 1.5f);
			AddInfo(FString::Printf(TEXT("KnifeMotion%s peakGuardDrift=%.3f"), *Case, PeakGuardDrift));
			continue;
		}

		if (Parameters == TEXT("EquipInterrupt"))
		{
			for (const bool bHeavy : { false, true })
			{
				VM->OnKnifeSlash(bHeavy);
				for (int32 Frame = 0; Frame < FMath::CeilToInt(0.1f / Step); ++Frame) Fixture.EvaluateHands(Step);
				VM->OnHolster(0.2f);
				VM->OnEquip(0.5f);
				TestTrue(TEXT("equipping clears the old knife timeline before another weapon can inherit it") + Case, VM->KnifeTime < 0.f);
			}
			continue;
		}

		if (Parameters == TEXT("ElbowClearance"))
		{
			if (!TestTrue(TEXT("the real right elbow bone is available") + Case, Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("lowerarm_r")) != INDEX_NONE)) return false;
			float OcclusionTime = 0.f;
			float LongestOcclusion = 0.f;
			float PeakInterior = 0.f;
			float PeakWristTravel = 0.f;
			float MinElbowDistance = MAX_flt;
			float MinWristDistance = MAX_flt;
			FVector PeakElbow = FVector::ZeroVector;
			FVector PeakWrist = FVector::ZeroVector;
			FVector2D PeakProjection = FVector2D::ZeroVector;
			bool bFinite = true;
			const FVector RestWrist = Fixture.HandInCamera(TEXT("hand_r"));
			for (const bool bHeavy : { false, false, true })
			{
				VM->OnKnifeSlash(bHeavy);
				const float NextInputAt = bHeavy ? 1.f : 0.42f;
				for (int32 Frame = 0; Frame < FMath::CeilToInt(NextInputAt / Step); ++Frame)
				{
					Fixture.EvaluateHands(Step);
					const FVector Elbow = Fixture.RenderedBoneInCamera(TEXT("lowerarm_r"));
					const FVector Wrist = Fixture.RenderedBoneInCamera(TEXT("hand_r"));
					bFinite &= !Elbow.ContainsNaN() && !Wrist.ContainsNaN();
					MinElbowDistance = FMath::Min(MinElbowDistance, static_cast<float>(Elbow.Size()));
					MinWristDistance = FMath::Min(MinWristDistance, static_cast<float>(Wrist.Size()));
					PeakWristTravel = FMath::Max(PeakWristTravel, static_cast<float>(FVector::Dist(RestWrist, Fixture.HandInCamera(TEXT("hand_r")))));
					const float TanH = FMath::Tan(FMath::DegreesToRadians(VM->CurrentFOV * 0.5f));
					const float TanV = TanH / (16.f / 9.f);
					bool bOccluding = false;
					// 靠肘半段应从画面边缘送刀，允许手腕和刀刃进入视野，不允许近眼前臂持续横挡
					for (const float AlongArm : { 0.f, 0.25f, 0.5f })
					{
						const FVector Point = FMath::Lerp(Elbow, Wrist, AlongArm);
						if (Point.X <= 1.f || Point.SizeSquared() >= 50.f * 50.f) continue;
						const FVector2D Projection(Point.Y / (Point.X * TanH), Point.Z / (Point.X * TanV));
						const float Interior = static_cast<float>(FMath::Min(0.85 - FMath::Abs(Projection.X), 0.85 - FMath::Abs(Projection.Y)));
						bOccluding |= Interior > 0.f;
						if (Interior > PeakInterior)
						{
							PeakInterior = Interior;
							PeakProjection = Projection;
							PeakElbow = Fixture.HandInCamera(TEXT("lowerarm_r"));
							PeakWrist = Fixture.HandInCamera(TEXT("hand_r"));
						}
					}
					OcclusionTime = bOccluding ? OcclusionTime + Step : 0.f;
					LongestOcclusion = FMath::Max(LongestOcclusion, OcclusionTime);
				}
			}
			TestTrue(TEXT("knife elbow and wrist transforms remain finite during both cuts and the stab") + Case, bFinite);
			TestTrue(TEXT("the knife hand still performs a visible strike instead of removing the animation") + Case, PeakWristTravel >= 8.f);
			TestTrue(FString::Printf(TEXT("the near-eye proximal forearm does not linger inside the central 85 percent of the view (%.3f s)%s"), LongestOcclusion, *Case), LongestOcclusion <= 0.12f);
			AddInfo(FString::Printf(TEXT("KnifeElbow%s longestOcclusion=%.3f peakInterior=%.3f minRenderedElbowDistance=%.2f minRenderedWristDistance=%.2f wristTravel=%.2f peakElbowCam=%s peakWristCam=%s peakProjection=%s"),
				*Case, LongestOcclusion, PeakInterior, MinElbowDistance, MinWristDistance, PeakWristTravel, *PeakElbow.ToCompactString(), *PeakWrist.ToCompactString(), *PeakProjection.ToString()));
			continue;
		}

		bool bFinite = true;
		float ClosestHands = MAX_flt;
		float LargestStartStep = 0.f;
		for (const bool bHeavy : { false, false, true })
		{
			const FVector PreviousRight = Fixture.HandInCamera(TEXT("hand_r"));
			VM->OnKnifeSlash(bHeavy);
			Fixture.EvaluateHands(Step);
			LargestStartStep = FMath::Max(LargestStartStep, static_cast<float>(FVector::Dist(PreviousRight, Fixture.HandInCamera(TEXT("hand_r")))));
			const float NextInputAt = bHeavy ? 1.f : 0.42f;
			for (int32 Frame = 1; Frame < FMath::CeilToInt(NextInputAt / Step); ++Frame)
			{
				Fixture.EvaluateHands(Step);
				const FVector Left = Fixture.HandInCamera(TEXT("hand_l"));
				const FVector Right = Fixture.HandInCamera(TEXT("hand_r"));
				bFinite &= !Left.ContainsNaN() && !Right.ContainsNaN();
				ClosestHands = FMath::Min(ClosestHands, static_cast<float>(FVector::Dist(Left, Right)));
			}
		}
		TestTrue(TEXT("alternating light cuts and a heavy stab leave both wrist transforms finite") + Case, bFinite);
		TestTrue(FString::Printf(TEXT("the guard and knife wrists keep clearance (%.2f cm)%s"), ClosestHands, *Case), ClosestHands >= 12.f);
		TestTrue(FString::Printf(TEXT("starting the next allowed cut does not teleport the weapon hand (%.2f cm)%s"), LargestStartStep, *Case), LargestStartStep <= 8.f);
		TestTrue(TEXT("the heavy recovery eventually releases its timeline") + Case, VM->KnifeTime < 0.f);
		AddInfo(FString::Printf(TEXT("KnifeMotion%s closestHands=%.3f largestStartStep=%.3f"), *Case, ClosestHands, LargestStartStep));
	}
	return true;
}
#endif
