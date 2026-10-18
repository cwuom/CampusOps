#include "Map/CampusSchoolBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace
{
	class FHotelCoverWorld
	{
	public:
		FHotelCoverWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
			if (World)
			{
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FHotelCoverWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusHotelCabanaCoverTest, "CampusOps.Experience.Map.Hotel.CabanaCoverCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusHotelCabanaCoverTest::RunTest(const FString&)
{
	FHotelCoverWorld Fixture;
	if (!TestNotNull(TEXT("the hotel cover fixture has a query world"), Fixture.World))
	{
		return false;
	}
	ACampusSchoolBuilder* Builder = Fixture.World->SpawnActorDeferred<ACampusSchoolBuilder>(ACampusSchoolBuilder::StaticClass(), FTransform::Identity,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("the fixture can create the real hotel layout builder"), Builder))
	{
		return false;
	}
	Builder->MapKind = ECampusMap::Hotel;
	Builder->bBuildSurroundings = false;
	Builder->FinishSpawning(FTransform::Identity);
	TestTrue(TEXT("the real hotel layout generated geometry"), Builder->InstanceCount > 0);

	// 眼高处避开日床和顶棚，确保命中来自白色侧帘本身
	constexpr float EyeZ = 160.f;
	for (const float CenterY : { -2050.f, -900.f, 900.f, 2050.f })
	{
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Curtain(-1680.f, CenterY + Side * 146.f, EyeZ);
			for (const float Direction : { -1.f, 1.f })
			{
				const FVector Start = Curtain + FVector(0.f, Direction * 80.f, 0.f);
				const FVector End = Curtain - FVector(0.f, Direction * 80.f, 0.f);
				for (const ECollisionChannel Channel : { ECC_Pawn, ECC_Visibility })
				{
					const FString Label = FString::Printf(TEXT("cabana y=%.0f side=%.0f direction=%.0f %s"), CenterY, Side, Direction,
						Channel == ECC_Pawn ? TEXT("pawn clearance sweep") : TEXT("visibility trace"));
					FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusHotelCover), Channel == ECC_Visibility);
					FHitResult Hit;
					const bool bHit = Channel == ECC_Pawn
						? Fixture.World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Channel, FCollisionShape::MakeSphere(4.f), Params)
						: Fixture.World->LineTraceSingleByChannel(Hit, Start, End, Channel, Params);
					if (TestTrue(*(Label + TEXT(" blocks from both sides")), bHit))
					{
						TestTrue(*(Label + TEXT(" hits the generated hotel cover")), Hit.GetActor() == Builder);
						TestTrue(*(Label + TEXT(" hits the thin curtain rather than distant furniture")), FMath::Abs(Hit.ImpactPoint.Y - Curtain.Y) < 3.f);
					}
				}
			}
		}

		// 开口跨过前排木柱所在平面，胶囊底部高于日床，避免把家具当作入口封堵
		const FVector Outside(-1900.f, CenterY, EyeZ);
		const FVector Inside(-1720.f, CenterY, EyeZ);
		for (const float Direction : { -1.f, 1.f })
		{
			const FVector Start = Direction > 0.f ? Outside : Inside;
			const FVector End = Direction > 0.f ? Inside : Outside;
			const FString Label = FString::Printf(TEXT("cabana y=%.0f front direction=%.0f"), CenterY, Direction);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusHotelCoverOpening), false);
			FHitResult Hit;
			TestFalse(*(Label + TEXT(" leaves the opening clear for a pawn-sized capsule")), Fixture.World->SweepSingleByChannel(Hit, Start, End,
				FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 92.f), Params));
			Params.bTraceComplex = true;
			TestFalse(*(Label + TEXT(" leaves the view through the opening clear")), Fixture.World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params));
		}
	}
	return true;
}
#endif
