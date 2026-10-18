#include "Map/CampusWindowGlass.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"

namespace
{
	struct FGlassFaces
	{
		int32 Count = 0;
		float Area = 0.f;
		float Longest = 0.f;
	};

	FGlassFaces ReadGlassFaces(const FProcMeshSection& Section)
	{
		FGlassFaces Result;
		TArray<int32> Groups;
		Groups.Init(INDEX_NONE, Section.ProcVertexBuffer.Num());
		for (int32 i = 0; i + 2 < Section.ProcIndexBuffer.Num(); i += 3)
		{
			const int32 A = Section.ProcIndexBuffer[i];
			const int32 B = Section.ProcIndexBuffer[i + 1];
			const int32 C = Section.ProcIndexBuffer[i + 2];
			int32 Group = Groups[A];
			if (Group == INDEX_NONE)
			{
				Group = Result.Count++;
			}
			Groups[A] = Groups[B] = Groups[C] = Group;
			Result.Area += FVector::CrossProduct(Section.ProcVertexBuffer[B].Position - Section.ProcVertexBuffer[A].Position,
				Section.ProcVertexBuffer[C].Position - Section.ProcVertexBuffer[A].Position).Size() * 0.5f;
		}
		for (int32 A = 0; A < Groups.Num(); ++A)
		{
			for (int32 B = A + 1; B < Groups.Num(); ++B)
			{
				if (Groups[A] != INDEX_NONE && Groups[A] == Groups[B])
				{
					Result.Longest = FMath::Max(Result.Longest, (float)FVector::Distance(Section.ProcVertexBuffer[A].Position, Section.ProcVertexBuffer[B].Position));
				}
			}
		}
		Result.Count /= 2;
		Result.Area *= 0.5f;
		return Result;
	}

	FGlassFaces RenderGlass(const TArray<CampusGlassGeo::FGlassPoly>& Pieces, bool bDropOntoFloor = false)
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
		if (bDropOntoFloor)
		{
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Collision = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Collision);
			Collision->SetBoxExtent(FVector(4096.f, 4096.f, 5.f));
			Collision->SetCollisionProfileName(TEXT("BlockAll"));
			Collision->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -5.f));
		}
		ACampusGlassShards* Shards = World->SpawnActor<ACampusGlassShards>();
		Shards->Init(FVector(0.f, 0.f, 300.f), FVector::RightVector, FVector::UpVector, FVector::ForwardVector, 1.2f,
			Pieces, FVector(0.f, 0.f, 300.f), FVector::ForwardVector, 0.f);
		if (bDropOntoFloor)
		{
			for (int32 Frame = 0; Frame < 180; ++Frame)
			{
				Shards->Tick(1.f / 60.f);
			}
		}
		UProceduralMeshComponent* Mesh = Shards->FindComponentByClass<UProceduralMeshComponent>();
		const FProcMeshSection* Section = Mesh ? Mesh->GetProcMeshSection(0) : nullptr;
		const FGlassFaces Result = Section ? ReadGlassFaces(*Section) : FGlassFaces();
		World->DestroyWorld(false);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusGlassLongFragmentTest, "CampusOps.Experience.Visual.GlassLongFragments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusGlassLongFragmentTest::RunTest(const FString&)
{
	const TArray<CampusGlassGeo::FGlassPoly> Pieces = { { FVector2f(0.f, 0.f), FVector2f(160.f, 0.f), FVector2f(160.f, 2.f), FVector2f(0.f, 2.f) } };
	const FGlassFaces Faces = RenderGlass(Pieces);
	TestTrue(TEXT("a detached narrow strip is drawn as shorter chips"), Faces.Count > 1 && Faces.Longest <= 12.2f);
	TestTrue(TEXT("splitting keeps the original 320 square centimetres of glass"), FMath::IsNearlyEqual(Faces.Area, 320.f, 0.02f));
	const FGlassFaces Compact = RenderGlass({ { FVector2f(0.f, 0.f), FVector2f(8.f, 0.f), FVector2f(8.f, 8.f), FVector2f(0.f, 8.f) } });
	TestEqual(TEXT("an ordinary compact plate keeps its shape"), Compact.Count, 1);
	TestTrue(TEXT("an ordinary plate keeps all its area"), FMath::IsNearlyEqual(Compact.Area, 64.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusGlassFragmentBudgetTest, "CampusOps.Experience.Visual.GlassFragmentBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusGlassFragmentBudgetTest::RunTest(const FString&)
{
	TArray<CampusGlassGeo::FGlassPoly> Pieces;
	for (int32 i = 0; i < 390; ++i)
	{
		const float X = (i % 30) * 6.f;
		const float Y = 20.f + (i / 30) * 6.f;
		Pieces.Add({ FVector2f(X, Y), FVector2f(X + 4.f, Y), FVector2f(X + 4.f, Y + 4.f), FVector2f(X, Y + 4.f) });
	}
	Pieces.Add({ FVector2f(0.f, 0.f), FVector2f(1600.f, 0.f), FVector2f(1600.f, 2.f), FVector2f(0.f, 2.f) });
	const FGlassFaces Faces = RenderGlass(Pieces);
	TestTrue(TEXT("busy breaks stay within the actor's 420 fragment budget"), Faces.Count <= 420);
	TestTrue(TEXT("the budget never discards glass to hide excess fragments"), FMath::IsNearlyEqual(Faces.Area, 9440.f, 0.1f));
	TArray<CampusGlassGeo::FGlassPoly> Oversubscribed;
	const CampusGlassGeo::FGlassPoly Compact = { FVector2f(0.f, 0.f), FVector2f(4.f, 0.f), FVector2f(4.f, 4.f), FVector2f(0.f, 4.f) };
	Oversubscribed.Init(Compact, 421);
	const TArray<CampusGlassGeo::FGlassPoly> Preserved = CampusGlassGeo::ShortenDetachedStrips(Oversubscribed, 420);
	TestEqual(TEXT("already over-budget input is preserved rather than silently discarded"), Preserved.Num(), 421);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusGlassLandingBudgetTest, "CampusOps.Experience.Visual.GlassLandingBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCampusGlassLandingBudgetTest::RunTest(const FString&)
{
	TArray<CampusGlassGeo::FGlassPoly> Pieces;
	for (int32 i = 0; i < 418; ++i)
	{
		const float X = 40.f + (i % 30) * 6.f;
		const float Y = (i / 30) * 6.f;
		Pieces.Add({ FVector2f(X, Y), FVector2f(X + 4.f, Y), FVector2f(X + 4.f, Y + 4.f), FVector2f(X, Y + 4.f) });
	}
	Pieces.Add({ FVector2f(0.f, 0.f), FVector2f(20.f, 0.f), FVector2f(20.f, 20.f), FVector2f(0.f, 20.f) });
	const FGlassFaces Faces = RenderGlass(Pieces, true);
	TestEqual(TEXT("a large plate landing near capacity can replace itself with only two chips"), Faces.Count, 420);
	TestTrue(TEXT("the landing preserves all 7088 square centimetres instead of dropping excess chips"), FMath::IsNearlyEqual(Faces.Area, 7088.f, 0.5f));
	return true;
}
#endif
