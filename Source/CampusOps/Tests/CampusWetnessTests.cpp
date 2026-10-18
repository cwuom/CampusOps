#include "Player/CampusWetness.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Game/CampusEnvironment.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWetnessExposureTest, "CampusOps.Experience.Wetness.Exposure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWetnessExposureTest::RunTest(const FString& Parameters)
{
	const float Soaked = CampusWetness::Advance(0.f, 1.f, 10.f);
	TestTrue(TEXT("ten seconds outdoors visibly soaks clothing"), Soaked > 0.9f && Soaked < 1.f);
	const float Sheltered = CampusWetness::Advance(Soaked, 0.f, 1.f);
	TestTrue(TEXT("entering shelter retains most accumulated moisture"), Sheltered > Soaked * 0.98f && Sheltered < Soaked);
	TestTrue(TEXT("sheltered clothing eventually dries"), CampusWetness::Advance(Soaked, 0.f, 300.f) < 0.02f);
	TestEqual(TEXT("dry sheltered clothing stays dry"), CampusWetness::Advance(0.f, 0.f, 30.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusWetnessCadenceTest, "CampusOps.Experience.Wetness.Cadence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusWetnessCadenceTest::RunTest(const FString& Parameters)
{
	const float Expected = CampusWetness::Advance(0.f, 0.7f, 12.f);
	for (const float Step : { 1.f / 30.f, 1.f / 60.f, 0.2f, 0.5f })
	{
		float Value = 0.f;
		const int32 Count = FMath::RoundToInt(12.f / Step);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Value = CampusWetness::Advance(Value, 0.7f, Step);
		}
		TestTrue(TEXT("wetness is independent of render and sampling cadence"), FMath::IsNearlyEqual(Value, Expected, 0.0001f));
	}
	TestEqual(TEXT("paused time preserves wetness"), CampusWetness::Advance(0.4f, 1.f, 0.f), 0.4f);
	TestEqual(TEXT("negative elapsed time cannot reverse the simulation"), CampusWetness::Advance(0.4f, 1.f, -1.f), 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusRainShelterTest, "CampusOps.Experience.Wetness.Shelter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusRainShelterTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	ACampusEnvironment* Environment = World->SpawnActor<ACampusEnvironment>();
	AActor* Roof = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Roof);
	Roof->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(100.f, 100.f, 8.f));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Roof->SetActorLocation(FVector(0.f, 0.f, 300.f));
	Box->RegisterComponent();
	TestFalse(TEXT("a real roof shelters a head below it before the rain mask is ready"), Environment->IsRainExposed(FVector(0.f, 0.f, 170.f)));
	TestTrue(TEXT("open air beside the roof remains exposed"), Environment->IsRainExposed(FVector(300.f, 0.f, 170.f)));
	TestTrue(TEXT("a point above the roof remains exposed"), Environment->IsRainExposed(FVector(0.f, 0.f, 330.f)));
	World->DestroyWorld(false);
	return true;
}
#endif
