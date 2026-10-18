#include "Player/CampusViewmodelComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "CampusAssets.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
	class FReloadMotionWorld
	{
	public:
		explicit FReloadMotionWorld(FName WeaponId)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ACampusCharacter>(FVector(0.f, 0.f, 94.f), FRotator::ZeroRotator, Spawn);
			UInputSettings* Settings = GetMutableDefault<UInputSettings>();
			const bool bHadMotionControls = Settings->bEnableMotionControls;
			Settings->bEnableMotionControls = true;
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Controller->SetMotionControlsEnabled(false);
			Settings->bEnableMotionControls = bHadMotionControls;
			Controller->SetAsLocalPlayerController();
			Controller->SetPlayerState(World->SpawnActor<APlayerState>());
			Controller->Possess(Character);
			Character->ApplyOperatorMesh(0);
			FCampusWeaponLoadout Loadout;
			Loadout.WeaponId = WeaponId;
			Weapon = World->SpawnActor<ACampusWeapon>();
			Weapon->InitializeWeapon(Loadout);
			Weapon->SetOwner(Character);
			Weapon->AttachToCharacter(Character);
			Weapon->SetEquipped(true);
			Character->Weapons.Add(Weapon);
			Character->CurrentWeaponIndex = 0;
			Anim = Character->GetArmsAnim();
			if (Anim && Weapon->GetDef())
			{
				Anim->SetBasePose(CampusAssets::Anim(Weapon->GetDef()->bPistolAnims ? CampusAssets::EAnim::PistolIdle : CampusAssets::EAnim::RifleIdle));
			}
			Character->GetCampusMovement()->SetMovementMode(MOVE_Walking);
		}

		~FReloadMotionWorld() { World->DestroyWorld(false); }

		void EvaluateHands(float Step)
		{
			Character->Viewmodel->TickComponent(Step, LEVELTICK_All, nullptr);
			Character->ArmsMesh->TickAnimation(Step, false);
			Character->ArmsMesh->RefreshBoneTransforms();
		}

		FVector HandInCamera() const
		{
			return Character->FirstPersonCamera->GetComponentTransform().InverseTransformPosition(Character->ArmsMesh->GetBoneLocation(TEXT("hand_l")));
		}

		UStaticMeshComponent* Magazine() const
		{
			const FCampusWeaponDef* Def = Weapon->GetDef();
			USceneComponent* Frame = Weapon->GetFPFrame();
			if (!Def || !Frame) return nullptr;
			FString MagazineName;
			for (const FCampusModelPart& Part : Def->Model)
			{
				if (Part.Role == ECampusPartRole::Magazine) MagazineName = Part.Mesh;
			}
			for (USceneComponent* Child : Frame->GetAttachChildren())
			{
				UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Child);
				if (Mesh && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == MagazineName) return Mesh;
			}
			return nullptr;
		}

		UWorld* World = nullptr;
		ACampusCharacter* Character = nullptr;
		ACampusWeapon* Weapon = nullptr;
		UCampusFPAnimInstance* Anim = nullptr;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCampusReloadMotionTest, "CampusOps.Experience.Viewmodel.ReloadHands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCampusReloadMotionTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Weapon : { TEXT("M4A1"), TEXT("AK47"), TEXT("X16") })
	{
		for (const TCHAR* Scenario : { TEXT("MovingPouch"), TEXT("MagazineContact"), TEXT("Complete"), TEXT("CancelMagazine"), TEXT("CancelPouch"), TEXT("CancelBolt") })
		{
			const FString Name = FString(Weapon) + TEXT(".") + Scenario;
			OutNames.Add(Name);
			OutCommands.Add(Name);
		}
	}
}

bool FCampusReloadMotionTest::RunTest(const FString& Parameters)
{
	FString WeaponName;
	FString Scenario;
	Parameters.Split(TEXT("."), &WeaponName, &Scenario);
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
	{
		FReloadMotionWorld Fixture{ FName(*WeaponName) };
		UCampusViewmodelComponent* VM = Fixture.Character->Viewmodel;
		VM->Character = Fixture.Character;
		const FString Case = FString::Printf(TEXT(" (%s, %.0f fps)"), *Parameters, 1.f / Step);
		if (!TestTrue(TEXT("the fixture executes the real local human viewmodel") + Case, Fixture.Character->IsLocalHuman())
			|| !TestNotNull(TEXT("the real operator animation instance is available") + Case, Fixture.Anim)
			|| !TestNotNull(TEXT("the real weapon definition is available") + Case, Fixture.Weapon->GetDef())
			|| !TestNotNull(TEXT("the real weapon frame is attached to the skeleton") + Case, Fixture.Weapon->GetFPFrame())
			|| !TestNotNull(TEXT("the actual magazine mesh is available") + Case, Fixture.Magazine())) return false;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(1.5f / Step); ++Frame) Fixture.EvaluateHands(Step);
		const float RestAlpha = Fixture.Anim->LeftHandAlpha;
		const FQuat RestTurn = Fixture.Anim->LeftHandTurnDelta;
		const float Progress = Scenario == TEXT("Complete") ? 0.f : Scenario == TEXT("MovingPouch") || Scenario == TEXT("CancelPouch") ? 0.42f
			: Scenario == TEXT("CancelBolt") ? 0.84f : Scenario == TEXT("MagazineContact") ? 0.14f : 0.24f;
		VM->SetDebugReload(Progress, true);
		for (int32 Frame = 0; Frame < FMath::CeilToInt(0.4f / Step); ++Frame) Fixture.EvaluateHands(Step);
		if (Scenario == TEXT("MovingPouch"))
		{
			float PeakTargetError = 0.f;
			float PeakWristError = 0.f;
			for (int32 Frame = 0; Frame < FMath::CeilToInt(0.8f / Step); ++Frame)
			{
				const float Motion = FMath::Sin(Frame * Step * 12.f);
				const FCampusWeaponDef* Def = Fixture.Weapon->GetDef();
				VM->SetPoseOverride(TEXT("reload"), Def->ReloadOffset + FVector(Motion * 6.f, 0.f, Motion * 3.f),
					Def->ReloadRotation + FRotator(Motion * 8.f, 0.f, Motion * 16.f));
				Fixture.EvaluateHands(Step);
				const FTransform Camera = Fixture.Character->FirstPersonCamera->GetComponentTransform();
				const FVector Pouch = Camera.TransformPosition(FVector(14.f, -18.f, -46.f));
				const FTransform Right = Fixture.Character->ArmsMesh->GetBoneTransform(Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("hand_r")));
				PeakTargetError = FMath::Max(PeakTargetError, static_cast<float>(FVector::Dist(Right.TransformPosition(Fixture.Anim->LeftHandEffector), Pouch)));
				PeakWristError = FMath::Max(PeakWristError, static_cast<float>(FVector::Dist(Fixture.Character->ArmsMesh->GetBoneLocation(TEXT("hand_l")), Pouch)));
			}
			TestTrue(FString::Printf(TEXT("the pouch target stays in camera space while the weapon moves (%.3f cm)%s"), PeakTargetError, *Case), PeakTargetError <= 0.2f);
			TestTrue(FString::Printf(TEXT("the evaluated skeletal wrist follows the pouch during weapon motion (%.3f cm)%s"), PeakWristError, *Case), PeakWristError <= 3.f);
			AddInfo(FString::Printf(TEXT("ReloadHands%s targetError=%.3f wristError=%.3f"), *Case, PeakTargetError, PeakWristError));
			continue;
		}
		if (Scenario == TEXT("MagazineContact"))
		{
			UStaticMeshComponent* Magazine = Fixture.Magazine();
			FVector Seat;
			FVector MagOffset;
			FRotator MagRotation;
			FVector MagPivot;
			bool bVisible = false;
			float IK = 0.f;
			float MagGrip = 0.f;
			float RackGrip = 0.f;
			float Elbow = 0.f;
			TestTrue(TEXT("the actual seated reload phase provides the wrist contact") + Case,
				VM->EvaluateReload(Fixture.Weapon, Fixture.Weapon->GetDef(), FVector::ZeroVector, Fixture.Weapon->GetFPFrame()->GetComponentTransform(),
					Fixture.Character->FirstPersonCamera->GetComponentTransform(), Seat, MagOffset, MagRotation, MagPivot, bVisible, IK, MagGrip, RackGrip, Elbow));
			const FVector ContactLocal = Magazine->GetComponentTransform().InverseTransformPosition(Fixture.Weapon->GetFPFrame()->GetComponentTransform().TransformPosition(Seat));
			const float Insert = Fixture.Weapon->GetStats().ReloadInsertFraction;
			float PeakContactError = 0.f;
			float PeakWristError = 0.f;
			for (const float Sample : { 0.20f, 0.25f, 0.30f, Insert - 0.14f, Insert - 0.07f, Insert - 0.015f })
			{
				VM->SetDebugReload(Sample, true);
				for (int32 Frame = 0; Frame < FMath::CeilToInt(0.12f / Step); ++Frame) Fixture.EvaluateHands(Step);
				const FVector Contact = Magazine->GetComponentTransform().TransformPosition(ContactLocal);
				const FTransform Right = Fixture.Character->ArmsMesh->GetBoneTransform(Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("hand_r")));
				PeakContactError = FMath::Max(PeakContactError, static_cast<float>(FVector::Dist(Contact, Right.TransformPosition(Fixture.Anim->LeftHandEffector))));
				PeakWristError = FMath::Max(PeakWristError, static_cast<float>(FVector::Dist(Contact, Fixture.Character->ArmsMesh->GetBoneLocation(TEXT("hand_l")))));
			}
			TestTrue(FString::Printf(TEXT("the rotating magazine keeps its rigid contact under the requested wrist (%.3f cm)%s"), PeakContactError, *Case), PeakContactError <= 0.2f);
			TestTrue(FString::Printf(TEXT("the skeletal hand keeps hold of the actual rotated magazine (%.3f cm)%s"), PeakWristError, *Case), PeakWristError <= 3.f);
			AddInfo(FString::Printf(TEXT("ReloadHands%s contactError=%.3f wristError=%.3f"), *Case, PeakContactError, PeakWristError));
			continue;
		}
		FVector PreviousHand = Fixture.HandInCamera();
		FQuat PreviousTurn = Fixture.Anim->LeftHandTurnDelta;
		float PreviousFingers = Fixture.Anim->LeftFingerOpen;
		float PeakHandSpeed = 0.f;
		float PeakTurnSpeed = 0.f;
		float PeakFingerSpeed = 0.f;
		bool bZeroTargetWithWeight = false;
		bool bFinite = true;
		VM->SetDebugReload(-1.f, true);
		const bool bComplete = Scenario == TEXT("Complete");
		if (bComplete) VM->OnReloadStart(Fixture.Weapon->GetStats().EmptyReloadTime, true);
		else VM->OnReloadCancel(Progress >= Fixture.Weapon->GetStats().ReloadInsertFraction);
		const float ObserveDuration = (bComplete ? Fixture.Weapon->GetStats().EmptyReloadTime : 0.f) + 0.8f;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(ObserveDuration / Step); ++Frame)
		{
			Fixture.EvaluateHands(Step);
			const FVector Hand = Fixture.HandInCamera();
			PeakHandSpeed = FMath::Max(PeakHandSpeed, static_cast<float>(FVector::Dist(Hand, PreviousHand)) / Step);
			PeakTurnSpeed = FMath::Max(PeakTurnSpeed, static_cast<float>(FMath::RadiansToDegrees(Fixture.Anim->LeftHandTurnDelta.AngularDistance(PreviousTurn))) / Step);
			PeakFingerSpeed = FMath::Max(PeakFingerSpeed, FMath::Abs(Fixture.Anim->LeftFingerOpen - PreviousFingers) / Step);
			bZeroTargetWithWeight |= Fixture.Anim->LeftHandAlpha > 0.05f && Fixture.Anim->LeftHandEffector.IsNearlyZero(0.1f);
			bFinite &= !Hand.ContainsNaN() && !Fixture.Anim->LeftHandEffector.ContainsNaN();
			PreviousHand = Hand;
			PreviousTurn = Fixture.Anim->LeftHandTurnDelta;
			PreviousFingers = Fixture.Anim->LeftFingerOpen;
		}
		TestTrue(TEXT("all evaluated wrist positions stay finite through the reload and its return") + Case, bFinite);
		TestFalse(TEXT("residual reload IK never pulls the left wrist to the right wrist origin") + Case, bZeroTargetWithWeight);
		TestTrue(FString::Printf(TEXT("the actual support wrist returns without a frame snap (%.1f cm/s)%s"), PeakHandSpeed, *Case), PeakHandSpeed <= 650.f);
		TestTrue(FString::Printf(TEXT("the support wrist orientation blends out with the returning hand (%.1f deg/s)%s"), PeakTurnSpeed, *Case), PeakTurnSpeed <= 1800.f);
		TestTrue(FString::Printf(TEXT("the reload fingers close gradually during the return (%.1f/s)%s"), PeakFingerSpeed, *Case), PeakFingerSpeed <= 12.f);
		TestTrue(TEXT("the support wrist returns to its ordinary IK ownership") + Case, FMath::Abs(Fixture.Anim->LeftHandAlpha - RestAlpha) < 0.02f);
		TestTrue(TEXT("the support wrist returns to its ordinary grip orientation") + Case, Fixture.Anim->LeftHandTurnDelta.AngularDistance(RestTurn) < FMath::DegreesToRadians(3.f));
		AddInfo(FString::Printf(TEXT("ReloadHands%s peakHandSpeed=%.1f peakTurnSpeed=%.1f peakFingerSpeed=%.2f zeroTarget=%d endIK=%.3f restIK=%.3f"),
			*Case, PeakHandSpeed, PeakTurnSpeed, PeakFingerSpeed, bZeroTargetWithWeight ? 1 : 0, Fixture.Anim->LeftHandAlpha, RestAlpha));
	}
	return true;
}
#endif
