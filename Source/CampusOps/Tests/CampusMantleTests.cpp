#include "Player/CampusMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/CampusCharacter.h"

namespace
{
	class FMantleCollisionWorld
	{
	public:
		FMantleCollisionWorld(float TopHeight, float FrontFace)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			World->InitializeActorsForPlay(FURL());
			AddBox(FVector(0.f, 0.f, -5.f), FVector(1000.f, 1000.f, 5.f));
			// 同时覆盖贴近台面和伸手抓沿，避免只在单一高度验证攀爬曲线
			AddBox(FVector(FrontFace + 100.f, 0.f, TopHeight * 0.5f), FVector(100.f, 150.f, TopHeight * 0.5f));
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<ACampusCharacter>(FVector(0.f, 0.f, 94.f), FRotator::ZeroRotator, Spawn);
			Move = Character->GetCampusMovement();
			Move->SetMovementMode(MOVE_Walking);
			Move->Velocity = FVector(260.f, 0.f, 0.f);
		}

		~FMantleCollisionWorld() { World->DestroyWorld(false); }

		UBoxComponent* AddBox(const FVector& At, const FVector& Extent)
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

		bool StartMantle(float Step)
		{
			Move->bWantsToMantle = true;
			for (int32 Attempt = 0; Attempt < 3 && !Move->IsMantling(); ++Attempt)
			{
				Move->UpdateCharacterStateBeforeMovement(Step);
			}
			return Move->IsMantling();
		}

		void StepMantle(float Step)
		{
			Move->UpdateCharacterStateBeforeMovement(Step);
			Move->PhysCustom(Step, 0);
			Move->UpdateCharacterStateAfterMovement(Step);
		}

		bool HasBlockingOverlap() const
		{
			const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
			const FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusMantleFixtureOverlap), false, Character);
			return World->OverlapBlockingTestByChannel(Character->GetActorLocation(), FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
		}

		UWorld* World = nullptr;
		ACampusCharacter* Character = nullptr;
		UCampusMovementComponent* Move = nullptr;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCampusMantleCollisionTest, "CampusOps.Experience.Movement.MantleSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCampusMantleCollisionTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Scenario : { TEXT("CloseLedgeClearance"), TEXT("CrouchTransition"), TEXT("DynamicBlocker") })
	{
		OutNames.Add(Scenario);
		OutCommands.Add(Scenario);
	}
}

bool FCampusMantleCollisionTest::RunTest(const FString& Parameters)
{
	auto RunCase = [&](float TopHeight, float FrontFace, float Step)
	{
		FMantleCollisionWorld Fixture(TopHeight, FrontFace);
		const FString Rate = FString::Printf(TEXT(" at height %.0f cm, front %.0f cm, %.0f fps"), TopHeight, FrontFace, 1.f / Step);
		if (Parameters == TEXT("CrouchTransition"))
		{
			Fixture.Move->Crouch(false);
			if (!TestTrue(TEXT("the character really starts crouched") + Rate, Fixture.Character->bIsCrouched))
			{
				return false;
			}
			// 使用真实站起请求，保留基类在移动更新中调整胶囊的时序
			Fixture.Character->UnCrouch();
		}
		if (!TestFalse(TEXT("the starting capsule is clear") + Rate, Fixture.HasBlockingOverlap())
			|| !TestTrue(TEXT("the real ledge detection enters mantle movement") + Rate, Fixture.StartMantle(Step)))
		{
			return false;
		}

		bool bInsertedBlocker = false;
		bool bOverlappedDuringMantle = false;
		bool bFiniteVelocity = true;
		float PeakSpeed = 0.f;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(2.f / Step) && Fixture.Move->IsMantling(); ++Frame)
		{
			if (Parameters == TEXT("DynamicBlocker") && !bInsertedBlocker && Fixture.Move->GetMantleAlpha() >= 0.25f)
			{
				// 起攀后才出现的窄障碍，起点和终点均容得下胶囊
				UBoxComponent* Blocker = Fixture.AddBox(FVector(42.f, 0.f, 250.f), FVector(2.f, 150.f, 150.f));
				const FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusMantleFixtureBlocker), false, Fixture.Character);
				FHitResult Hit;
				if (!TestTrue(TEXT("the inserted obstacle really blocks pawn movement") + Rate,
					Fixture.World->SweepSingleByChannel(Hit, FVector(0.f, 0.f, 200.f), FVector(80.f, 0.f, 200.f), FQuat::Identity,
						ECC_Pawn, FCollisionShape::MakeSphere(2.f), Params) && Hit.GetComponent() == Blocker)
					|| !TestFalse(TEXT("the obstacle appears ahead without trapping the starting capsule") + Rate, Fixture.HasBlockingOverlap()))
				{
					return false;
				}
				bInsertedBlocker = true;
			}
			Fixture.StepMantle(Step);
			bOverlappedDuringMantle |= Fixture.HasBlockingOverlap();
			bFiniteVelocity &= !Fixture.Move->Velocity.ContainsNaN();
			PeakSpeed = FMath::Max(PeakSpeed, static_cast<float>(Fixture.Move->Velocity.Size()));
		}
		TestFalse(TEXT("mantle finishes or aborts within its bounded duration") + Rate, Fixture.Move->IsMantling());
		TestFalse(TEXT("the real capsule never cuts through a blocking surface") + Rate, bOverlappedDuringMantle);
		TestTrue(TEXT("mantle movement and cancellation keep a finite bounded velocity") + Rate,
			bFiniteVelocity && PeakSpeed < 2000.f);
		if (Parameters == TEXT("DynamicBlocker"))
		{
			TestTrue(TEXT("the obstacle was inserted during the climb") + Rate, bInsertedBlocker);
			TestTrue(TEXT("a blocked mantle exits before crossing to the far side") + Rate, Fixture.Character->GetActorLocation().X < 20.f);
			TestTrue(TEXT("a blocked mantle returns to falling movement") + Rate, Fixture.Move->IsFalling());
		}
		else
		{
			const UCapsuleComponent* Capsule = Fixture.Character->GetCapsuleComponent();
			TestTrue(TEXT("the mantle actually carries the character onto the ledge") + Rate, Fixture.Character->GetActorLocation().X > FrontFace + 35.f);
			TestTrue(TEXT("the final capsule feet clear the ledge top") + Rate,
				Fixture.Character->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight() >= TopHeight);
		}
		return true;
	};
	bool bValidFixtures = true;
	for (const float TopHeight : { 45.f, 110.f, 170.f })
	{
		if (Parameters == TEXT("DynamicBlocker") && TopHeight != 110.f) continue;
		for (const float FrontFace : { 35.f, 65.f })
		{
			if (Parameters == TEXT("DynamicBlocker") && FrontFace != 35.f) continue;
			for (const float Step : { 1.f / 30.f, 1.f / 60.f, 1.f / 144.f })
			{
				bValidFixtures &= RunCase(TopHeight, FrontFace, Step);
			}
		}
	}
	return bValidFixtures;
}
#endif
