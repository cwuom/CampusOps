#include "Player/CampusViewmodelComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/CampusCharacter.h"
#include "Weapons/CampusWeapon.h"

namespace
{
	const FBox BackWallWeaponBounds(FVector(-35.8f, -4.f, -19.9f), FVector(82.62f, 4.f, 21.37f));

	class FWeaponBackWallWorld
	{
	public:
		FWeaponBackWallWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Viewer = World->SpawnActor<ACampusCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, Spawn);
			Weapon = World->SpawnActor<ACampusWeapon>();
			Viewer->Weapons.Add(Weapon);
			Viewer->CurrentWeaponIndex = 0;
		}

		~FWeaponBackWallWorld() { World->DestroyWorld(false); }

		void AddBackWall()
		{
			AActor* Actor = World->SpawnActor<AActor>();
			Wall = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Wall);
			Wall->SetBoxExtent(FVector(1.f, 200.f, 200.f));
			Wall->SetCollisionObjectType(ECC_WorldStatic);
			Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Wall->SetCollisionResponseToAllChannels(ECR_Block);
			Wall->RegisterComponent();
			Actor->SetActorLocation(FVector(-35.f, 0.f, 160.f));
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

		double NearestRenderedX(const FTransform& Pose) const
		{
			double Nearest = TNumericLimits<double>::Max();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point(Corner & 1 ? BackWallWeaponBounds.Max.X : BackWallWeaponBounds.Min.X,
					Corner & 2 ? BackWallWeaponBounds.Max.Y : BackWallWeaponBounds.Min.Y,
					Corner & 4 ? BackWallWeaponBounds.Max.Z : BackWallWeaponBounds.Min.Z);
				Nearest = FMath::Min(Nearest, RenderPoint(Pose, Point).X);
			}
			return Nearest;
		}

		bool HitsBackWall(const FVector& End) const
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBackWallFixture), false, Viewer);
			Params.AddIgnoredActor(Weapon);
			return World->SweepSingleByChannel(Hit, Viewer->FirstPersonCamera->GetComponentLocation(), End, FQuat::Identity,
				ECC_Pawn, FCollisionShape::MakeSphere(4.f), Params) && Hit.GetComponent() == Wall;
		}

		UWorld* World = nullptr;
		ACampusCharacter* Viewer = nullptr;
		ACampusWeapon* Weapon = nullptr;
		UBoxComponent* Wall = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWeaponBackWallTest, "CampusOps.Experience.Viewmodel.Clearance.BackWallKeepsADS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWeaponBackWallTest::RunTest(const FString&)
{
	FWeaponBackWallWorld Fixture;
	UCampusViewmodelComponent* VM = Fixture.Viewer->Viewmodel;
	VM->Character = Fixture.Viewer;
	VM->WeaponClearanceBox = BackWallWeaponBounds;
	VM->CalibGunRot = FQuat::Identity;
	VM->CalibGrip = FVector::ZeroVector;
	Fixture.Viewer->bAimHeld = true;
	Fixture.Viewer->ADSAlpha = 1.f;
	const FTransform Ready(FQuat::Identity, FVector(23.f, 7.5f, -20.f));
	auto Solve = [&](const FTransform& Authored, double& Nearest)
	{
		VM->BlockAlpha = 0.f;
		VM->WallCorrection = FVector::ZeroVector;
		VM->bWallPoseActive = false;
		VM->bWallHighReady = false;
		Nearest = TNumericLimits<double>::Max();
		FTransform Pose = Authored;
		for (int32 Frame = 0; Frame < 45; ++Frame)
		{
			Pose = Authored;
			VM->ConstrainWeaponPose(Fixture.Weapon, Pose, 1.f / 60.f);
			Nearest = FMath::Min(Nearest, Fixture.NearestRenderedX(Pose));
		}
		return Pose;
	};
	double Nearest = 0.f;
	TestTrue(TEXT("the empty control leaves the authored pose unchanged"), Solve(Ready, Nearest).Equals(Ready, 0.01f));
	Fixture.AddBackWall();
	const FVector Eye = Fixture.Viewer->FirstPersonCamera->GetComponentLocation();
	TestTrue(TEXT("the real back wall blocks pawn traces at the capsule boundary"), Fixture.HitsBackWall(Eye - FVector(80.f, 0.f, 0.f)));
	const FVector StockUpper(BackWallWeaponBounds.Min.X, BackWallWeaponBounds.Min.Y, BackWallWeaponBounds.Max.Z);
	TestTrue(TEXT("all authored weapon corners are already clear of the back wall"), Fixture.NearestRenderedX(Ready) > -34.f);
	TestFalse(TEXT("the actual eye-to-stock segment does not touch the safe back wall"), Fixture.HitsBackWall(Fixture.RenderPoint(Ready, StockUpper)));
	TestTrue(TEXT("a safe back wall leaves the authored weapon pose unchanged"), Solve(Ready, Nearest).Equals(Ready, 0.01f));
	TestTrue(TEXT("a safe back wall cannot interrupt the held sight picture"),
		Fixture.Viewer->IsAimHeld() && Fixture.Viewer->GetRawADSAlpha() > 0.99f && Fixture.Viewer->GetADSAlpha() > 0.99f);

	// 枪托真实穿墙时仍要纠正，防止把后向实际扫描和预测扫描一起删掉
	const FTransform Penetrating(FQuat::Identity, FVector(-30.f, 7.5f, -20.f));
	TestTrue(TEXT("the second authored stock really crosses the back wall"), Fixture.NearestRenderedX(Penetrating) < -34.f);
	TestTrue(TEXT("the actual penetrating stock segment hits the wall"), Fixture.HitsBackWall(Fixture.RenderPoint(Penetrating, StockUpper)));
	Solve(Penetrating, Nearest);
	TestTrue(*FString::Printf(TEXT("final safety keeps all rendered stock corners outside the wall every frame (nearest %.2f cm)"), Nearest), Nearest >= -34.1f);
	// 转向后墙体留在身侧，但实际枪体已完全清开，预测范围不能继续压掉瞄准
	Fixture.Wall->SetBoxExtent(FVector(200.f, 1.f, 200.f));
	Fixture.Wall->GetOwner()->SetActorLocation(FVector(0.f, 45.f, 160.f));
	VM->WallAdvance = 0.f;
	TestTrue(TEXT("a nearby parallel side wall leaves the clear authored pose unchanged"), Solve(Ready, Nearest).Equals(Ready, 0.01f));
	TestTrue(TEXT("turning away from a wall restores held ADS even with that wall still beside the player"),
		Fixture.Viewer->IsAimHeld() && Fixture.Viewer->GetADSAlpha() > 0.99f);
	return true;
}
#endif
