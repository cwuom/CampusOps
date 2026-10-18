#include "Player/CampusCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/CampusBotController.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Player/CampusMovementComponent.h"
#include "Player/CampusViewmodelComponent.h"
#include "Weapons/CampusFX.h"
#include "Weapons/CampusWeapon.h"

namespace
{
	class FMovementPresentationWorld
	{
	public:
		FMovementPresentationWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
		}

		~FMovementPresentationWorld() { World->DestroyWorld(false); }

		ACampusCharacter* AddCharacter(const FVector& At)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<ACampusCharacter>(At, FRotator::ZeroRotator, Spawn);
		}

		APlayerController* AddLocalController(ACampusCharacter* Character)
		{
			UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
			const bool bHadMotionControls = InputSettings->bEnableMotionControls;
			// 引擎构造器先开启运动控制，创建后立即关闭并恢复测试前设置
			InputSettings->bEnableMotionControls = true;
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Controller->SetMotionControlsEnabled(false);
			InputSettings->bEnableMotionControls = bHadMotionControls;
			Controller->SetAsLocalPlayerController();
			Controller->SetPlayerState(World->SpawnActor<APlayerState>());
			Controller->Possess(Character);
			return Controller;
		}

		UBoxComponent* AddWall(const FVector& At, const FVector& Extent)
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
			return Box;
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusSlideJumpContinuityTest, "CampusOps.Experience.Movement.SlideJumpContinuity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusSlideJumpContinuityTest::RunTest(const FString&)
{
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
	{
		FMovementPresentationWorld Fixture;
		Fixture.AddWall(FVector(0.f, 0.f, -5.f), FVector(2000.f, 2000.f, 5.f));
		float PeakPitch[2] = {};
		float PeakLift[2] = {};
		for (int32 Scenario = 0; Scenario < 2; ++Scenario)
		{
			ACampusCharacter* Character = Fixture.AddCharacter(FVector(0.f, Scenario * 500.f, 94.f));
			Fixture.AddLocalController(Character);
			if (!TestTrue(TEXT("the fixture executes the local human input path"), Character->IsLocalHuman()))
			{
				return false;
			}
			UCampusMovementComponent* Move = Character->GetCampusMovement();
			UCampusViewmodelComponent* VM = Character->Viewmodel;
			VM->Character = Character;
			Move->SetMovementMode(MOVE_Walking);
			Move->Velocity = FVector(700.f, 0.f, 0.f);
			if (Scenario == 1)
			{
				Move->Crouch(false);
				Move->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECampusCustomMovement::Slide));
				VM->bWasSliding = true;
				VM->SlideAlpha.Reset(1.f);
			}
			VM->CamHeight.Reset();
			VM->CamImpact.Reset();
			VM->ImpactPos.Reset();
			VM->ImpactRot.Reset();
			Character->Input_JumpStarted();
			Character->CheckJumpInput(Step);
			Move->UpdateCharacterStateBeforeMovement(Step);
			TestTrue(TEXT("both inputs actually enter falling movement"), Move->IsFalling());
			for (int32 Frame = 0; Frame < FMath::CeilToInt(0.35f / Step); ++Frame)
			{
				VM->TickComponent(Step, LEVELTICK_All, nullptr);
				PeakPitch[Scenario] = FMath::Max(PeakPitch[Scenario], FMath::Abs(VM->CameraRotationOffset.Pitch));
				PeakLift[Scenario] = FMath::Max(PeakLift[Scenario], static_cast<float>(VM->ImpactPos.Value.Z));
			}
		}
		TestTrue(TEXT("a slide jump does not stack a second camera launch onto the ordinary jump"), PeakPitch[1] <= PeakPitch[0] * 1.5f + 0.01f);
		TestTrue(TEXT("leaving the slide cannot reverse the jumping weapon dip into an upward pop"), PeakLift[1] <= PeakLift[0] + 0.05f);
	}
	for (const bool bNewSlide : { false, true })
	{
		FMovementPresentationWorld Fixture;
		Fixture.AddWall(FVector(0.f, 0.f, -5.f), FVector(1000.f, 1000.f, 5.f));
		UBoxComponent* Ceiling = Fixture.AddWall(FVector(0.f, 0.f, 150.f), FVector(200.f, 200.f, 5.f));
		ACampusCharacter* Character = Fixture.AddCharacter(FVector(0.f, 0.f, 94.f));
		Fixture.AddLocalController(Character);
		UCampusMovementComponent* Move = Character->GetCampusMovement();
		UCampusViewmodelComponent* VM = Character->Viewmodel;
		VM->Character = Character;
		Move->SetMovementMode(MOVE_Walking);
		Move->Crouch(false);
		Move->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECampusCustomMovement::Slide));
		Move->Velocity = FVector(700.f, 0.f, 0.f);
		VM->bWasSliding = true;
		VM->SlideAlpha.Reset(1.f);
		VM->CamHeight.Reset();
		VM->CamImpact.Reset();
		VM->ImpactPos.Reset();
		VM->ImpactRot.Reset();
		Character->Input_JumpStarted();
		Character->CheckJumpInput(1.f / 60.f);
		Move->UpdateCharacterStateBeforeMovement(1.f / 60.f);
		TestTrue(TEXT("the low ceiling delays the real uncrouch after the slide jump"), Move->IsFalling() && Character->bIsCrouched);
		VM->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
		if (bNewSlide)
		{
			Move->bWantsToCrouch = true;
			Move->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECampusCustomMovement::Slide));
			VM->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
			Move->CancelSlide();
			VM->CamImpact.Reset();
			VM->ImpactPos.Reset();
			VM->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
			TestTrue(TEXT("a later ordinary slide keeps its own exit feedback after a crouched slide jump"), VM->CameraRotationOffset.Pitch > 0.f && VM->ImpactPos.Value.Z > 0.f);
			continue;
		}
		Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FVector BeforeStand = VM->ImpactPos.Velocity;
		Move->UpdateCharacterStateBeforeMovement(1.f / 60.f);
		TestFalse(TEXT("standing actually completes once the ceiling clears"), Character->bIsCrouched);
		TestTrue(TEXT("a delayed stand cannot add a second launch to the slide jump"), VM->ImpactPos.Velocity.Equals(BeforeStand, 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusSlideCarryTest, "CampusOps.Experience.Movement.SlideCarry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusSlideCarryTest::RunTest(const FString&)
{
	FMovementPresentationWorld Fixture;
	ACampusCharacter* Character = Fixture.AddCharacter(FVector(0.f, 0.f, 94.f));
	Fixture.AddLocalController(Character);
	UCampusMovementComponent* Move = Character->GetCampusMovement();
	UCampusViewmodelComponent* VM = Character->Viewmodel;
	VM->Character = Character;
	auto SettlePose = [&]()
	{
		for (int32 Frame = 0; Frame < 90; ++Frame) VM->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
		const FTransform Arms = Character->ArmsMesh->GetRelativeTransform();
		return Arms.Inverse() * VM->HandsOffset * Arms * Character->ViewmodelPivot->GetRelativeTransform();
	};
	Move->SetMovementMode(MOVE_Walking);
	const FTransform Hip = SettlePose();
	Move->Crouch(false);
	Move->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECampusCustomMovement::Slide));
	VM->bWasSliding = true;
	const FTransform Slide = SettlePose();
	TestTrue(TEXT("sliding raises the muzzle slightly above the ordinary hip carry"), Slide.GetRotation().GetForwardVector().Z > Hip.GetRotation().GetForwardVector().Z + 0.03f);
	TestTrue(TEXT("the slide carry does not lower the firing hand toward the floor"), Slide.TransformPosition(VM->CalibGrip).Z >= Hip.TransformPosition(VM->CalibGrip).Z - 0.1f);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCampusViewmodelPawnContactTest, "CampusOps.Experience.Viewmodel.PawnContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCampusViewmodelPawnContactTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Scenario : { TEXT("PawnKeepsADS"), TEXT("WallBehindPawn") })
	{
		OutNames.Add(Scenario);
		OutCommands.Add(Scenario);
	}
}

bool FCampusViewmodelPawnContactTest::RunTest(const FString& Parameters)
{
	FMovementPresentationWorld Fixture;
	ACampusCharacter* Viewer = Fixture.AddCharacter(FVector(0.f, 0.f, 100.f));
	ACampusWeapon* Weapon = Fixture.World->SpawnActor<ACampusWeapon>();
	Viewer->Weapons.Add(Weapon);
	Viewer->CurrentWeaponIndex = 0;
	Viewer->bAimHeld = true;
	Viewer->ADSAlpha = 1.f;
	UCampusViewmodelComponent* VM = Viewer->Viewmodel;
	VM->Character = Viewer;
	VM->WeaponClearanceBox = FBox(FVector(-30.f, -4.f, -10.f), FVector(83.f, 4.f, 21.f));
	const FTransform Ready(FQuat::Identity, FVector(23.f, 7.5f, -20.f));
	auto Solve = [&]()
	{
		VM->BlockAlpha = 0.f;
		VM->WallCorrection = FVector::ZeroVector;
		VM->bWallPoseActive = false;
		VM->bWallHighReady = false;
		FTransform Pose = Ready;
		for (int32 Frame = 0; Frame < 45; ++Frame)
		{
			Pose = Ready;
			VM->ConstrainWeaponPose(Weapon, Pose, 1.f / 60.f);
		}
		return Pose;
	};
	if (Parameters == TEXT("WallBehindPawn"))
	{
		Fixture.AddWall(FVector(100.f, 0.f, 170.f), FVector(2.f, 100.f, 120.f));
	}
	const FTransform Baseline = Solve();
	const float BaselineBlock = VM->GetWallObstructionAlpha();
	ACampusCharacter* NPC = Fixture.AddCharacter(FVector(65.f, 0.f, 100.f));
	NPC->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusPawnContactFixture), false, Viewer);
	TestTrue(TEXT("the real pawn fixture blocks an ordinary visibility sweep"), Fixture.World->SweepSingleByChannel(Hit, Viewer->GetPawnViewLocation(), Viewer->GetPawnViewLocation() + FVector(110.f, 0.f, 0.f),
		FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(4.f), Params) && Hit.GetActor() == NPC);
	const FTransform WithPawn = Solve();
	if (Parameters == TEXT("PawnKeepsADS"))
	{
		TestTrue(TEXT("a nearby pawn cannot replace the held sight picture with a wall-ready pose"), WithPawn.Equals(Ready, 0.01f));
		TestTrue(TEXT("a nearby pawn cannot suppress effective ADS while the aim input remains held"), Viewer->IsAimHeld() && Viewer->GetADSAlpha() > 0.99f);
	}
	else
	{
		TestTrue(TEXT("the wall control genuinely requires an avoidance pose"), BaselineBlock > 0.05f && !Baseline.Equals(Ready, 0.1f));
		TestTrue(TEXT("a pawn in front cannot hide the wall from the avoidance query"), FMath::IsNearlyEqual(VM->GetWallObstructionAlpha(), BaselineBlock, 0.01f));
		TestTrue(TEXT("the wall behind a pawn still produces the same safe weapon pose"), WithPawn.Equals(Baseline, 0.1f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusViewmodelShellContactTest, "CampusOps.Experience.Viewmodel.ShellKeepsADS", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusViewmodelShellContactTest::RunTest(const FString&)
{
	FMovementPresentationWorld Fixture;
	ACampusCharacter* Viewer = Fixture.AddCharacter(FVector(0.f, 0.f, 100.f));
	ACampusWeapon* Weapon = Fixture.World->SpawnActor<ACampusWeapon>();
	Viewer->Weapons.Add(Weapon);
	Viewer->CurrentWeaponIndex = 0;
	Viewer->bAimHeld = true;
	Viewer->ADSAlpha = 1.f;
	UCampusViewmodelComponent* VM = Viewer->Viewmodel;
	VM->Character = Viewer;
	VM->WeaponClearanceBox = FBox(FVector(-30.f, -4.f, -10.f), FVector(83.f, 4.f, 21.f));
	const FTransform Ready(FQuat::Identity, FVector(23.f, 7.5f, -20.f));
	const FVector Eye = Viewer->GetPawnViewLocation();
	FMinimalViewInfo View;
	View.Location = Eye;
	View.Rotation = FRotator::ZeroRotator;
	View.FOV = 90.f;
	View.FirstPersonFOV = 74.f;
	View.FirstPersonScale = 0.6f;
	const FVector End = View.TransformWorldToFirstPerson(Viewer->FirstPersonCamera->GetComponentTransform().TransformPosition(Ready.TransformPosition(FVector(83.f, 0.f, 5.5f))), false);
	ACampusShellCasing* Casing = Fixture.World->SpawnActor<ACampusShellCasing>();
	Casing->Init(FVector::ZeroVector, false);
	Casing->SetActorLocation(FMath::Lerp(Eye, End, 0.55f), false, nullptr, ETeleportType::TeleportPhysics);
	UStaticMeshComponent* ShellMesh = Casing->FindComponentByClass<UStaticMeshComponent>();
	TestTrue(TEXT("the fixture creates a real simulated casing body"), ShellMesh && ShellMesh->IsSimulatingPhysics());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusShellContactFixture), false, Viewer);
	Params.AddIgnoredActor(Weapon);
	for (const ECollisionChannel WorldChannel : { ECC_WorldStatic, ECC_WorldDynamic })
	{
		FHitResult Hit;
		TestTrue(TEXT("ignoring view queries does not remove the casing's world collision"), Fixture.World->SweepSingleByChannel(Hit, Eye, End, FQuat::Identity, WorldChannel,
			FCollisionShape::MakeSphere(4.f), Params) && Hit.GetActor() == Casing);
	}
	bool bHeldADSPreserved = true;
	for (int32 Frame = 0; Frame < 45; ++Frame)
	{
		FTransform Pose = Ready;
		VM->ConstrainWeaponPose(Weapon, Pose, 1.f / 60.f);
		bHeldADSPreserved &= Viewer->IsAimHeld() && Viewer->GetADSAlpha() > 0.99f && Pose.Equals(Ready, 0.01f);
	}
	TestTrue(TEXT("a casing crossing the weapon probes cannot interrupt held ADS"), bHeldADSPreserved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusBotCloseSpacingTest, "CampusOps.Experience.Movement.BotCloseSpacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusBotCloseSpacingTest::RunTest(const FString&)
{
	for (const bool bWallBetween : { false, true })
	{
		FMovementPresentationWorld Fixture;
		ACampusCharacter* Bot = Fixture.AddCharacter(FVector(0.f, 0.f, 100.f));
		ACampusCharacter* Player = Fixture.AddCharacter(FVector(80.f, 0.f, 100.f));
		if (bWallBetween)
		{
			Fixture.AddWall(FVector(40.f, 0.f, 170.f), FVector(3.f, 100.f, 120.f));
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotSpacingFixture), false, Bot);
			Params.AddIgnoredActor(Player);
			TestTrue(TEXT("the close spacing fixture has a real wall separating the two pawns"), Fixture.World->LineTraceSingleByChannel(Hit, Bot->GetPawnViewLocation(), Player->GetPawnViewLocation(), ECC_Visibility, Params));
		}
		ACampusBotController* Controller = Fixture.World->SpawnActor<ACampusBotController>();
		Controller->Possess(Bot);
		Controller->Target = Player;
		Controller->SlideReleaseTime = 1.f;
		Controller->NextStrafeTime = 10.f;
		UCampusMovementComponent* Move = Bot->GetCampusMovement();
		Move->bWantsToSprint = true;
		Move->SetMovementMode(MOVE_Custom, static_cast<uint8>(ECampusCustomMovement::Slide));
		Move->Velocity = FVector(700.f, 0.f, 0.f);
		Controller->ThinkEngage(0.f);
		const FVector TowardPlayer = (Player->GetActorLocation() - Bot->GetActorLocation()).GetSafeNormal2D();
		if (bWallBetween)
		{
			TestTrue(TEXT("a player behind a wall cannot interrupt an NPC's separate navigation"), FVector::DotProduct(Move->Velocity, TowardPlayer) > 0.f && Move->IsSliding() && Move->bWantsToSprint);
		}
		else
		{
			TestTrue(TEXT("an active rush cannot keep pushing the NPC into a nearby player's camera"), FVector::DotProduct(Move->Velocity, TowardPlayer) <= 0.f);
			TestFalse(TEXT("camera clearance interrupts the slide even before the next strafe decision"), Move->IsSliding());
			TestFalse(TEXT("the close-range safety stop does not immediately resume sprinting"), Move->bWantsToSprint);
		}
	}
	return true;
}
#endif
