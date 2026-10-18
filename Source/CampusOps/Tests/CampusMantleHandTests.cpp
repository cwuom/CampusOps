#include "Player/CampusViewmodelComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "CampusAssets.h"
#include "Components/BoxComponent.h"
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
	class FMantleHandWorld
	{
	public:
		explicit FMantleHandWorld(FName WeaponId)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
			AddBox(FVector(0.f, 0.f, -5.f), FVector(1000.f, 1000.f, 5.f));
			AddBox(FVector(165.f, 0.f, 55.f), FVector(100.f, 150.f, 55.f));
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
			Move = Character->GetCampusMovement();
			Move->SetMovementMode(MOVE_Walking);
		}

		~FMantleHandWorld() { World->DestroyWorld(false); }

		void AddBox(const FVector& At, const FVector& Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Actor->SetActorLocation(At);
		}

		void EvaluateHands(float Step)
		{
			Character->Viewmodel->TickComponent(Step, LEVELTICK_All, nullptr);
			Character->ArmsMesh->TickAnimation(Step, false);
			Character->ArmsMesh->RefreshBoneTransforms();
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

		FVector RenderedHandWorld() const
		{
			return RenderView().TransformWorldToFirstPerson(Character->ArmsMesh->GetBoneLocation(TEXT("hand_l")), false);
		}

		FVector RenderedHandInCamera() const
		{
			const FMinimalViewInfo View = RenderView();
			return FTransform(View.Rotation, View.Location).InverseTransformPosition(RenderedHandWorld());
		}

		UWorld* World = nullptr;
		ACampusCharacter* Character = nullptr;
		ACampusWeapon* Weapon = nullptr;
		UCampusMovementComponent* Move = nullptr;
		UCampusFPAnimInstance* Anim = nullptr;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCampusMantleHandTest, "CampusOps.Experience.Viewmodel.MantleHands", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCampusMantleHandTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Weapon : { TEXT("M4A1"), TEXT("X16"), TEXT("KNIFE") })
	{
		for (const TCHAR* Exit : { TEXT("Complete"), TEXT("InterruptEarly"), TEXT("InterruptHold") })
		{
			const FString Name = FString(Weapon) + TEXT(".") + Exit;
			OutNames.Add(Name);
			OutCommands.Add(Name);
		}
		if (FCString::Strcmp(Weapon, TEXT("KNIFE")) != 0)
		{
			const FString Name = FString(Weapon) + TEXT(".ReloadHandoff");
			OutNames.Add(Name);
			OutCommands.Add(Name);
		}
	}
}

bool FCampusMantleHandTest::RunTest(const FString& Parameters)
{
	FString WeaponName;
	FString Exit;
	Parameters.Split(TEXT("."), &WeaponName, &Exit);
	const float InterruptAt = Exit == TEXT("InterruptEarly") ? 0.2f : Exit == TEXT("InterruptHold") ? 0.5f : 2.f;
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
	{
		FMantleHandWorld Fixture{ FName(*WeaponName) };
		UCampusViewmodelComponent* VM = Fixture.Character->Viewmodel;
		VM->Character = Fixture.Character;
		const FString Case = FString::Printf(TEXT(" (%s, %.0f fps)"), *Parameters, 1.f / Step);
		if (!TestTrue(TEXT("the fixture executes the real local human viewmodel") + Case, Fixture.Character->IsLocalHuman())
			|| !TestNotNull(TEXT("the operator has the real first-person animation instance") + Case, Fixture.Anim)
			|| !TestNotNull(TEXT("the weapon has its real definition") + Case, Fixture.Weapon->GetDef())
			|| !TestNotNull(TEXT("the weapon rig is attached to the actual skeletal hand") + Case, Fixture.Weapon->GetFPFrame())
			|| !TestTrue(TEXT("both real wrist bones are available") + Case,
				Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE && Fixture.Character->ArmsMesh->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE))
		{
			return false;
		}
		const FCampusWeaponDef* Def = Fixture.Weapon->GetDef();
		TestTrue(TEXT("the selected weapon exercises its intended support-hand branch") + Case,
			WeaponName == TEXT("M4A1") ? Def->bLeftHandIK : WeaponName == TEXT("X16") ? !Def->bLeftHandIK && Def->bPistolAnims : Def->IsMelee());
		for (int32 Frame = 0; Frame < FMath::CeilToInt(1.5f / Step); ++Frame) Fixture.EvaluateHands(Step);
		const float RestAlpha = Fixture.Anim->LeftHandAlpha;
		const FQuat RestTurn = Fixture.Anim->LeftHandTurnDelta;
		FVector PreviousHand = Fixture.RenderedHandInCamera();
		FQuat PreviousTurn = RestTurn;
		float PreviousFingers = Fixture.Anim->LeftFingerOpen;
		Fixture.Move->Velocity = FVector(260.f, 0.f, 0.f);
		Fixture.Move->bWantsToMantle = true;
		Fixture.Move->UpdateCharacterStateBeforeMovement(Step);
		if (!TestTrue(TEXT("the real ledge query starts the climb") + Case, Fixture.Move->IsMantling())) return false;
		if (Exit == TEXT("ReloadHandoff"))
		{
			while (Fixture.Move->IsMantling() && Fixture.Move->GetMantleAlpha() < 0.1f)
			{
				Fixture.Move->PhysCustom(Step, 0);
				Fixture.EvaluateHands(Step);
			}
			Fixture.Weapon->ClientSyncAmmo(Fixture.Weapon->GetStats().MagSize - 1, Fixture.Weapon->GetAmmoReserve());
			const float BeforeFingers = Fixture.Anim->LeftFingerOpen;
			Fixture.Character->DebugAction(TEXT("reload"));
			if (!TestTrue(TEXT("a real magazine deficit allows the actual reload input") + Case,
				Fixture.Weapon->IsReloading() && VM->ReloadTime >= 0.f && Fixture.Move->IsMantling())) return false;
			float PeakFingersAfterInput = -MAX_flt;
			for (int32 Frame = 0; Frame < FMath::CeilToInt(0.05f / Step); ++Frame)
			{
				if (Fixture.Move->IsMantling()) Fixture.Move->PhysCustom(Step, 0);
				Fixture.EvaluateHands(Step);
				PeakFingersAfterInput = FMath::Max(PeakFingersAfterInput, Fixture.Anim->LeftFingerOpen);
			}
			TestTrue(TEXT("reload input immediately starts releasing the climbing hand instead of extending it farther") + Case,
				PeakFingersAfterInput <= BeforeFingers + 0.03f);
			while (VM->ReloadTime < 0.26f)
			{
				if (Fixture.Move->IsMantling()) Fixture.Move->PhysCustom(Step, 0);
				Fixture.EvaluateHands(Step);
			}
			TestTrue(TEXT("the reload keeps progressing after the handoff") + Case, Fixture.Weapon->IsReloading() && VM->ReloadTime >= 0.26f);
			TestTrue(TEXT("the climbing hand yields ownership to the magazine path") + Case, VM->MantleHandAlpha < 0.01f);
			TestTrue(TEXT("the reload hand retains a finite nonzero target") + Case,
				!Fixture.Anim->LeftHandEffector.ContainsNaN() && !Fixture.Anim->LeftHandEffector.IsNearlyZero(0.1f));
			AddInfo(FString::Printf(TEXT("MantleHands%s reload=%.3f beforeFingers=%.3f peakFingersAfterInput=%.3f remainingClimb=%.3f endIK=%.3f"),
				*Case, VM->ReloadTime, BeforeFingers, PeakFingersAfterInput, VM->MantleHandAlpha, Fixture.Anim->LeftHandAlpha));
			continue;
		}
		const FVector Contact = Fixture.Move->GetMantleLedge() - FVector(0.f, 16.f, 0.f);
		bool bInterrupted = false;
		bool bUsedHand = false;
		bool bZeroTargetWithWeight = false;
		bool bFinite = true;
		float PeakHandSpeed = 0.f;
		int32 PeakHandFrame = INDEX_NONE;
		FVector PeakHandDelta = FVector::ZeroVector;
		float PeakTurnSpeed = 0.f;
		float PeakFingerSpeed = 0.f;
		float FloatingTime = 0.f;
		float LongestFloat = 0.f;
		float RecoveryTime = 0.f;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(2.f / Step); ++Frame)
		{
			if (Fixture.Move->IsMantling() && Fixture.Move->GetMantleAlpha() >= InterruptAt)
			{
				Fixture.Move->SetMovementMode(MOVE_Falling);
				bInterrupted = true;
			}
			if (Fixture.Move->IsMantling()) Fixture.Move->PhysCustom(Step, 0);
			Fixture.EvaluateHands(Step);
			const FVector Hand = Fixture.RenderedHandInCamera();
			const float HandSpeed = static_cast<float>(FVector::Dist(Hand, PreviousHand)) / Step;
			if (HandSpeed > PeakHandSpeed)
			{
				PeakHandSpeed = HandSpeed;
				PeakHandFrame = Frame;
				PeakHandDelta = Hand - PreviousHand;
			}
			PeakTurnSpeed = FMath::Max(PeakTurnSpeed, static_cast<float>(FMath::RadiansToDegrees(Fixture.Anim->LeftHandTurnDelta.AngularDistance(PreviousTurn))) / Step);
			PeakFingerSpeed = FMath::Max(PeakFingerSpeed, FMath::Abs(Fixture.Anim->LeftFingerOpen - PreviousFingers) / Step);
			bUsedHand |= Fixture.Anim->LeftFingerOpen > 0.1f;
			bZeroTargetWithWeight |= Fixture.Anim->LeftHandAlpha > 0.1f && Fixture.Anim->LeftHandEffector.IsNearlyZero(0.1f);
			bFinite &= !Hand.ContainsNaN() && !Fixture.Anim->LeftHandEffector.ContainsNaN();
			// 张开手撑住时必须接近实际台沿，否则应尽快回收而非锁在屏幕左下
			if (Fixture.Move->IsMantling() && Fixture.Move->GetMantleAlpha() >= 0.25f && Fixture.Anim->LeftFingerOpen > 0.4f
				&& FVector::Dist(Fixture.RenderedHandWorld(), Contact) > 25.f)
			{
				FloatingTime += Step;
				LongestFloat = FMath::Max(LongestFloat, FloatingTime);
			}
			else FloatingTime = 0.f;
			PreviousHand = Hand;
			PreviousTurn = Fixture.Anim->LeftHandTurnDelta;
			PreviousFingers = Fixture.Anim->LeftFingerOpen;
			if (!Fixture.Move->IsMantling()) RecoveryTime += Step;
			if (RecoveryTime >= 0.4f) break;
		}
		TestTrue(TEXT("the real hand participates in the climb") + Case, bUsedHand);
		TestTrue(TEXT("the normal or interrupted climb leaves time to return the hand") + Case, RecoveryTime >= 0.4f);
		TestEqual(TEXT("the requested interruption really happens") + Case, bInterrupted, InterruptAt < 1.f);
		TestTrue(TEXT("all observed hand positions remain finite") + Case, bFinite);
		TestFalse(TEXT("a fading hand IK never suddenly targets the right wrist origin") + Case, bZeroTargetWithWeight);
		TestTrue(FString::Printf(TEXT("the visible hand has no single-frame snap (peak %.1f cm/s)%s"), PeakHandSpeed, *Case), PeakHandSpeed <= 300.f);
		TestTrue(FString::Printf(TEXT("the wrist turn eases into and out of its planted pose (peak %.1f deg/s)%s"), PeakTurnSpeed, *Case), PeakTurnSpeed <= 1800.f);
		TestTrue(FString::Printf(TEXT("finger opening follows a gradual reach and return (peak %.1f/s)%s"), PeakFingerSpeed, *Case), PeakFingerSpeed <= 12.f);
		TestTrue(FString::Printf(TEXT("an unsupported open hand cannot hover away from the ledge (%.3f s)%s"), LongestFloat, *Case), LongestFloat <= 0.14f);
		TestTrue(TEXT("the support hand returns to its ordinary IK ownership after recovery") + Case, FMath::Abs(Fixture.Anim->LeftHandAlpha - RestAlpha) < 0.02f);
		TestTrue(TEXT("the wrist returns to the ordinary grip or guard orientation") + Case, Fixture.Anim->LeftHandTurnDelta.AngularDistance(RestTurn) < FMath::DegreesToRadians(10.f));
		TestTrue(TEXT("the climbing fingers close back onto the grip or guard") + Case, FMath::Abs(Fixture.Anim->LeftFingerOpen) < 0.05f);
		AddInfo(FString::Printf(TEXT("MantleHands%s peakHandSpeed=%.1f peakTurnSpeed=%.1f peakFingerSpeed=%.2f longestFloat=%.3f recovery=%.3f endIK=%.3f restIK=%.3f endTurn=%.2f endFingers=%.3f zeroTarget=%d"),
			*Case, PeakHandSpeed, PeakTurnSpeed, PeakFingerSpeed, LongestFloat, RecoveryTime, Fixture.Anim->LeftHandAlpha, RestAlpha,
			FMath::RadiansToDegrees(Fixture.Anim->LeftHandTurnDelta.AngularDistance(RestTurn)), Fixture.Anim->LeftFingerOpen, bZeroTargetWithWeight ? 1 : 0));
		AddInfo(FString::Printf(TEXT("MantleHands%s peakFrame=%d peakTime=%.3f peakDelta=%s"), *Case, PeakHandFrame, PeakHandFrame * Step, *PeakHandDelta.ToCompactString()));
	}
	return true;
}
#endif
