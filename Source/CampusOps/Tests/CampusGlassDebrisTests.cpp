#include "Map/CampusWindowGlass.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"

namespace
{
	void AddGlassTestBox(UWorld* World, const FVector& Center, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Center);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusGlassDebrisUnsupportedFrameTest, "CampusOps.Experience.Glass.DebrisUnsupportedFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusGlassDebrisUnsupportedFrameTest::RunTest(const FString&)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	AddGlassTestBox(World, FVector(0.f, 0.f, -5.f), FVector(1000.f, 1000.f, 5.f));
	// the old pane-wide probes see these rails, although the falling chip is between them
	for (const float X : { -45.f, 45.f })
	{
		AddGlassTestBox(World, FVector(X, 0.f, 220.f), FVector(5.f, 100.f, 10.f));
	}
	const FVector Origin(0.f, 0.f, 260.f);
	const TArray<CampusGlassGeo::FGlassPoly> Pieces = { { FVector2f(-2.f, -2.f), FVector2f(2.f, -2.f), FVector2f(2.f, 2.f), FVector2f(-2.f, 2.f) } };
	ACampusGlassShards* Shards = World->SpawnActor<ACampusGlassShards>();
	Shards->Init(Origin, FVector::RightVector, FVector::UpVector, FVector::ForwardVector, 1.2f, Pieces, Origin + FVector::UpVector * 500.f, FVector::ForwardVector, 0.f);
	UProceduralMeshComponent* Mesh = Shards->FindComponentByClass<UProceduralMeshComponent>();
	if (!TestNotNull(TEXT("the fixture renders a real falling chip"), Mesh) || !TestNotNull(TEXT("the chip has glass geometry"), Mesh->GetProcMeshSection(0)))
	{
		World->DestroyWorld(false);
		return false;
	}
	for (int32 Frame = 0; Frame < 24; ++Frame)
	{
		Shards->Tick(1.f / 60.f);
	}
	const float FallingZ = Mesh->GetProcMeshSection(0)->SectionLocalBox.GetCenter().Z;
	TestTrue(TEXT("the chip follows gravity instead of being placed on the floor"), FallingZ > 220.f && FallingZ < 258.f);
	for (int32 Frame = 24; Frame < 360; ++Frame)
	{
		Shards->Tick(1.f / 60.f);
	}
	const FBox Bounds = Mesh->GetProcMeshSection(0)->SectionLocalBox;
	TestTrue(TEXT("a neighbouring frame cannot become an unsupported cached floor"), Bounds.Max.Z < 2.f);
	TestTrue(TEXT("the resting glass thickness stays above the real collision floor"), Bounds.Min.Z >= -0.01f);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusGlassDebrisSlideTest, "CampusOps.Experience.Glass.DebrisSlide", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusGlassDebrisSlideTest::RunTest(const FString&)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	AddGlassTestBox(World, FVector(0.f, 0.f, -5.f), FVector(1000.f, 1000.f, 5.f));
	const FVector Origin(0.f, 0.f, 0.7f);
	const TArray<CampusGlassGeo::FGlassPoly> Pieces = { { FVector2f(-2.f, -2.f), FVector2f(2.f, -2.f), FVector2f(2.f, 2.f), FVector2f(-2.f, 2.f) } };
	ACampusGlassShards* Shards = World->SpawnActor<ACampusGlassShards>();
	Shards->Init(Origin, FVector::RightVector, FVector::UpVector, FVector::ForwardVector, 1.2f, Pieces, Origin, FVector::ForwardVector, 0.f);
	UProceduralMeshComponent* Mesh = Shards->FindComponentByClass<UProceduralMeshComponent>();
	if (!TestNotNull(TEXT("the sliding fixture has real glass geometry"), Mesh ? Mesh->GetProcMeshSection(0) : nullptr))
	{
		World->DestroyWorld(false);
		return false;
	}
	FVector Previous = Mesh->GetProcMeshSection(0)->SectionLocalBox.GetCenter();
	FVector Contact = Previous;
	bool bContact = false;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Shards->Tick(1.f / 60.f);
		Contact = Mesh->GetProcMeshSection(0)->SectionLocalBox.GetCenter();
		if (FMath::IsNearlyEqual(Contact.Z, Previous.Z, 0.001f) && Contact.Z < 1.f)
		{
			bContact = true;
			break;
		}
		Previous = Contact;
	}
	TestTrue(TEXT("the low drop reaches the collision floor"), bContact);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Shards->Tick(1.f / 60.f);
	}
	const FVector Settled = Mesh->GetProcMeshSection(0)->SectionLocalBox.GetCenter();
	TestTrue(TEXT("remaining tangential momentum slides instead of sleeping at first low vertical contact"), FVector::DistSquared2D(Contact, Settled) > FMath::Square(0.3f));
	TestTrue(TEXT("the skid stays supported by the collision floor"), Settled.Z < 1.f && Settled.Z >= 0.f);
	World->DestroyWorld(false);
	return true;
}
#endif
