#include "Player/CampusViewmodelComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/CampusCharacter.h"
#include "Weapons/CampusFX.h"
#include "Weapons/CampusWeapon.h"

namespace
{
	const FBox LongWeaponBox(FVector(-35.8f, -4.f, -19.9f), FVector(82.62f, 4.f, 21.37f));
	class FWeaponClearanceWorld
	{
	public:
		FWeaponClearanceWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
			Viewer = AddCharacter(FVector(0.f, 0.f, 100.f));
			Weapon = World->SpawnActor<ACampusWeapon>();
			Viewer->Weapons.Add(Weapon);
			Viewer->CurrentWeaponIndex = 0;
		}

		~FWeaponClearanceWorld() { World->DestroyWorld(false); }

		ACampusCharacter* AddCharacter(const FVector& Location)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<ACampusCharacter>(Location, FRotator::ZeroRotator, Spawn);
		}

		UBoxComponent* AddObstacle(const FVector& Location, const FVector& Extent, bool bVisibleToTrace = true)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Box->SetCollisionResponseToChannel(ECC_Visibility, bVisibleToTrace ? ECR_Block : ECR_Ignore);
			Box->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Box;
		}

		FVector RenderPoint(const FTransform& Pose, const FVector& LocalPoint) const
		{
			const UCameraComponent* Camera = Viewer->FirstPersonCamera;
			const FTransform CameraTransform = Camera->GetComponentTransform();
			FMinimalViewInfo View;
			View.Location = CameraTransform.GetLocation();
			View.Rotation = (CameraTransform.GetRotation() * Viewer->Viewmodel->CameraRotationOffset.Quaternion()).Rotator();
			View.FOV = Viewer->Viewmodel->CurrentFOV;
			View.FirstPersonFOV = Camera->bEnableFirstPersonFieldOfView ? Viewer->Viewmodel->CurrentViewmodelFOV : View.FOV;
			View.FirstPersonScale = Camera->bEnableFirstPersonScale ? Camera->FirstPersonScale : 1.f;
			return View.TransformWorldToFirstPerson(CameraTransform.TransformPosition(Pose.TransformPosition(LocalPoint)), false);
		}

		FBox RenderBounds(const FTransform& Pose) const
		{
			FBox Bounds(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point(Corner & 1 ? LongWeaponBox.Max.X : LongWeaponBox.Min.X,
					Corner & 2 ? LongWeaponBox.Max.Y : LongWeaponBox.Min.Y,
					Corner & 4 ? LongWeaponBox.Max.Z : LongWeaponBox.Min.Z);
				Bounds += RenderPoint(Pose, Point);
			}
			return Bounds;
		}

		bool Hits(UPrimitiveComponent* Component, ECollisionChannel Channel, const FVector& End) const
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusClearanceFixture), false, Viewer);
			Params.AddIgnoredActor(Weapon);
			return World->SweepSingleByChannel(Hit, Viewer->FirstPersonCamera->GetComponentLocation(), End, FQuat::Identity,
				Channel, FCollisionShape::MakeSphere(4.f), Params) && Hit.GetComponent() == Component;
		}

		UWorld* World = nullptr;
		ACampusCharacter* Viewer = nullptr;
		ACampusWeapon* Weapon = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWeaponClearanceFloorTest, "CampusOps.Experience.Viewmodel.Clearance.LowEyeFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWeaponClearanceFloorTest::RunTest(const FString&)
{
	for (const float Pitch : { -35.f, -65.f, -88.f })
	{
		FWeaponClearanceWorld Fixture;
		UBoxComponent* Floor = Fixture.AddObstacle(FVector(0.f, 0.f, -5.f), FVector(1000.f, 1000.f, 5.f));
		Fixture.Viewer->FirstPersonCamera->SetWorldLocationAndRotation(FVector(0.f, 0.f, 55.f), FRotator(Pitch, 0.f, 0.f));
		UCampusViewmodelComponent* VM = Fixture.Viewer->Viewmodel;
		VM->Character = Fixture.Viewer;
		VM->WeaponClearanceBox = LongWeaponBox;
		VM->CalibGunRot = FQuat::Identity;
		VM->CalibGrip = FVector::ZeroVector;
		const FTransform Ready(FQuat::Identity, FVector(23.f, 7.5f, -20.f));
		TestTrue(TEXT("the low-eye fixture has a real blocking floor"), Fixture.Hits(Floor, ECC_Visibility, FVector(0.f, 0.f, -10.f)));
		if (Pitch <= -65.f)
		{
			TestTrue(TEXT("the authored long weapon really intersects the floor before avoidance"), Fixture.RenderBounds(Ready).Min.Z < 0.f);
		}
		float LowestCorner = MAX_flt;
		for (int32 Frame = 0; Frame < 45; ++Frame)
		{
			FTransform Pose = Ready;
			VM->ConstrainWeaponPose(Fixture.Weapon, Pose, 1.f / 60.f);
			LowestCorner = FMath::Min(LowestCorner, static_cast<float>(Fixture.RenderBounds(Pose).Min.Z));
			TestTrue(TEXT("a floor contact cannot turn the muzzle down into a wall tuck"), Pose.Rotator().Pitch >= -0.1f);
		}
		TestTrue(*FString::Printf(TEXT("all eight rendered corners clear the floor while looking down at %.0f degrees (lowest %.2f cm)"), Pitch, LowestCorner), LowestCorner >= -0.1f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWeaponClearanceCornerTest, "CampusOps.Experience.Viewmodel.Clearance.ThinWallCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWeaponClearanceCornerTest::RunTest(const FString&)
{
	for (const float Side : { -1.f, 1.f })
	{
		FWeaponClearanceWorld Fixture;
		const float FrontFace = 34.f;
		const float SideFace = 18.f;
		UBoxComponent* FrontWall = Fixture.AddObstacle(FVector(FrontFace + 1.f, 0.f, 160.f), FVector(1.f, 300.f, 200.f));
		UBoxComponent* SideWall = Fixture.AddObstacle(FVector(0.f, Side * (SideFace + 1.f), 160.f), FVector(300.f, 1.f, 200.f));
		UCampusViewmodelComponent* VM = Fixture.Viewer->Viewmodel;
		VM->Character = Fixture.Viewer;
		VM->WeaponClearanceBox = LongWeaponBox;
		VM->CalibGunRot = FQuat::Identity;
		VM->CalibGrip = FVector::ZeroVector;
		const FTransform Ready(FRotator(-9.f, Side * 28.f, Side * 35.f), FVector(23.f, Side * 7.5f, -20.f));
		const FVector Eye = Fixture.Viewer->FirstPersonCamera->GetComponentLocation();
		TestTrue(TEXT("the front wall is a real two-centimetre collision slab"), Fixture.Hits(FrontWall, ECC_Visibility, Eye + FVector(80.f, 0.f, 0.f)));
		TestTrue(TEXT("the side wall is a real two-centimetre collision slab"), Fixture.Hits(SideWall, ECC_Visibility, Eye + FVector(0.f, Side * 80.f, 0.f)));
		TestTrue(TEXT("the authored pose reaches through the corner before avoidance"), Fixture.RenderBounds(Ready).Max.X > FrontFace);
		float FurthestFront = -MAX_flt;
		float FurthestSide = -MAX_flt;
		for (int32 Frame = 0; Frame < 45; ++Frame)
		{
			FTransform Pose = Ready;
			VM->ConstrainWeaponPose(Fixture.Weapon, Pose, 1.f / 60.f);
			const FBox Bounds = Fixture.RenderBounds(Pose);
			FurthestFront = FMath::Max(FurthestFront, static_cast<float>(Bounds.Max.X));
			FurthestSide = FMath::Max(FurthestSide, static_cast<float>(Side > 0.f ? Bounds.Max.Y : -Bounds.Min.Y));
		}
		TestTrue(*FString::Printf(TEXT("all eight rendered corners stay before the front slab (furthest %.2f cm)"), FurthestFront), FurthestFront <= FrontFace + 0.1f);
		TestTrue(*FString::Printf(TEXT("all eight rendered corners stay inside the %.0f side slab (furthest %.2f cm)"), Side, FurthestSide), FurthestSide <= SideFace + 0.1f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWeaponClearanceChannelTest, "CampusOps.Experience.Viewmodel.Clearance.EnvironmentChannels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWeaponClearanceChannelTest::RunTest(const FString&)
{
	FWeaponClearanceWorld Fixture;
	UCampusViewmodelComponent* VM = Fixture.Viewer->Viewmodel;
	VM->Character = Fixture.Viewer;
	VM->WeaponClearanceBox = LongWeaponBox;
	VM->CalibGunRot = FQuat::Identity;
	VM->CalibGrip = FVector::ZeroVector;
	Fixture.Viewer->bAimHeld = true;
	Fixture.Viewer->ADSAlpha = 1.f;
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
			VM->ConstrainWeaponPose(Fixture.Weapon, Pose, 1.f / 60.f);
		}
		return Pose;
	};
	TestTrue(TEXT("an empty environment leaves the authored pose untouched"), Solve().Equals(Ready, 0.01f));
	UBoxComponent* Wall = Fixture.AddObstacle(FVector(35.f, 0.f, 160.f), FVector(1.f, 200.f, 200.f));
	const FTransform VisibleWallPose = Solve();
	const float VisibleWallAlpha = VM->GetWallObstructionAlpha();
	TestTrue(TEXT("the visibility wall control actually requires avoidance"), VisibleWallAlpha > 0.05f && !VisibleWallPose.Equals(Ready, 0.1f));
	Wall->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	const FVector End = Fixture.Viewer->FirstPersonCamera->GetComponentLocation() + FVector(80.f, 0.f, 0.f);
	TestFalse(TEXT("the map-style blocker is intentionally invisible to visibility traces"), Fixture.Hits(Wall, ECC_Visibility, End));
	TestTrue(TEXT("the same blocker still genuinely blocks the pawn channel"), Fixture.Hits(Wall, ECC_Pawn, End));
	const FTransform PawnWallPose = Solve();
	TestTrue(TEXT("a pawn-blocking environment wall cannot lose avoidance when visibility is disabled"), FMath::IsNearlyEqual(VM->GetWallObstructionAlpha(), VisibleWallAlpha, 0.01f));
	TestTrue(TEXT("the blocker retains the same safe rendered pose across visibility responses"), PawnWallPose.Equals(VisibleWallPose, 0.1f));

	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ACampusCharacter* NPC = Fixture.AddCharacter(FVector(55.f, 0.f, 100.f));
	NPC->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	TestTrue(TEXT("the real pawn control blocks an ordinary visibility query"), Fixture.Hits(NPC->GetCapsuleComponent(), ECC_Visibility, End));
	TestTrue(TEXT("another pawn does not fold the weapon"), Solve().Equals(Ready, 0.01f));
	TestTrue(TEXT("another pawn cannot release held ADS"), Fixture.Viewer->IsAimHeld() && Fixture.Viewer->GetADSAlpha() > 0.99f);
	NPC->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NPC->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ACampusShellCasing* Casing = Fixture.World->SpawnActor<ACampusShellCasing>();
	Casing->Init(FVector::ZeroVector, false);
	const FVector ProbeEnd = Fixture.RenderPoint(Ready, FVector(LongWeaponBox.Max.X, 0.f, LongWeaponBox.GetCenter().Z));
	Casing->SetActorLocation(FMath::Lerp(Fixture.Viewer->FirstPersonCamera->GetComponentLocation(), ProbeEnd, 0.55f), false, nullptr, ETeleportType::TeleportPhysics);
	UStaticMeshComponent* Shell = Casing->FindComponentByClass<UStaticMeshComponent>();
	if (!TestNotNull(TEXT("the real casing has a collision body"), Shell))
	{
		return false;
	}
	TestTrue(TEXT("the casing retains physical collision against the world"), Fixture.Hits(Shell, ECC_WorldStatic, ProbeEnd));
	TestTrue(TEXT("a physical casing does not fold the weapon"), Solve().Equals(Ready, 0.01f));
	TestTrue(TEXT("a physical casing cannot release held ADS"), Fixture.Viewer->IsAimHeld() && Fixture.Viewer->GetADSAlpha() > 0.99f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWallRepeatTest, "CampusOps.Experience.Viewmodel.Clearance.RepeatedContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWallRepeatTest::RunTest(const FString&)
{
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
	{
		FWeaponClearanceWorld Fixture;
		Fixture.AddObstacle(FVector(35.f, 0.f, 160.f), FVector(1.f, 40.f, 200.f));
		UCampusViewmodelComponent* VM = Fixture.Viewer->Viewmodel;
		VM->Character = Fixture.Viewer;
		VM->WeaponClearanceBox = LongWeaponBox;
		VM->CalibGunRot = FQuat::Identity;
		VM->CalibGrip = FVector::ZeroVector;
		const FTransform Ready(FQuat::Identity, FVector(23.f, 7.5f, -20.f));
		FTransform Previous = Ready;
		float PeakTurn = 0.f;
		float PeakTravel = 0.f;
		FString PeakDetail;
		bool bClippedWall = false;
		bool bStockCrossedView = false;
		// 连续横移反复进入窄墙边缘，静态贴墙或只测试平滑标量捕获不到姿态求解器的跳变
		for (int32 Frame = 0; Frame < FMath::RoundToInt(8.f / Step); ++Frame)
		{
			const float Age = Frame * Step;
			Fixture.Viewer->FirstPersonCamera->SetWorldLocation(FVector(0.f, 90.f * FMath::Cos(PI * Age * 0.5f), 160.f));
			FTransform Pose = Ready;
			VM->ConstrainWeaponPose(Fixture.Weapon, Pose, Step);
			const FVector StockTop = Pose.TransformPosition(FVector(-35.8f, 0.f, 8.f));
			bStockCrossedView |= VM->BlockAlpha > 0.05f && StockTop.X > 4.f && StockTop.Z > -0.1f * StockTop.X;
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point(Corner & 1 ? LongWeaponBox.Max.X : LongWeaponBox.Min.X,
					Corner & 2 ? LongWeaponBox.Max.Y : LongWeaponBox.Min.Y,
					Corner & 4 ? LongWeaponBox.Max.Z : LongWeaponBox.Min.Z);
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusRepeatClearance), false, Fixture.Viewer);
				Params.AddIgnoredActor(Fixture.Weapon);
				bClippedWall |= Fixture.World->SweepSingleByChannel(Hit, Fixture.Viewer->FirstPersonCamera->GetComponentLocation(),
					Fixture.RenderPoint(Pose, Point), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(4.f), Params);
			}
			if (Frame > 0)
			{
				const float Turn = static_cast<float>(FMath::RadiansToDegrees(Pose.GetRotation().AngularDistance(Previous.GetRotation()))) / Step;
				const float Travel = static_cast<float>(FVector::Dist(Pose.GetLocation(), Previous.GetLocation())) / Step;
				if (Travel > PeakTravel)
				{
					PeakDetail = FString::Printf(TEXT("t=%.3f target=%.3f block=%.3f high=%d previous=%s current=%s correction=%s"), Age,
						VM->WallTargetAlpha, VM->BlockAlpha, VM->bWallHighReady, *Previous.ToString(), *Pose.ToString(), *VM->WallCorrection.ToString());
				}
				PeakTurn = FMath::Max(PeakTurn, Turn);
				PeakTravel = FMath::Max(PeakTravel, Travel);
			}
			Previous = Pose;
		}
		TestTrue(*FString::Printf(TEXT("repeated wall-edge contact has no abrupt wrist rotation at %.0f fps (%.1f deg/s)"), 1.f / Step, PeakTurn), PeakTurn <= 500.f);
		TestFalse(TEXT("smoothing repeated contact never allows a rendered corner through the finite wall"), bClippedWall);
		TestFalse(TEXT("the stock stays below the central view throughout contact and recovery"), bStockCrossedView);
		TestTrue(*FString::Printf(TEXT("repeated wall-edge contact cannot teleport the grip at %.0f fps (%.1f cm/s)"), 1.f / Step, PeakTravel), PeakTravel <= 300.f);
		AddInfo(FString::Printf(TEXT("RepeatedContact fps=%.0f peakTurn=%.1f peakTravel=%.1f"), 1.f / Step, PeakTurn, PeakTravel));
		AddInfo(PeakDetail);
	}
	return true;
}
#endif
