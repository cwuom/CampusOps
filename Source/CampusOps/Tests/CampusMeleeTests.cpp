#include "Player/CampusCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CampusOps.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "Weapons/CampusWeapon.h"

namespace
{
	class FCampusMeleeContactCommand : public IAutomationLatentCommand
	{
	public:
		FCampusMeleeContactCommand(FAutomationTestBase* InTest, const FString& InScenario) : Test(InTest), Scenario(InScenario) {}

		virtual bool Update() override
		{
			if (!World)
			{
				const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
				World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
				World->InitializeActorsForPlay(FURL());
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Attacker = World->SpawnActor<ACampusCharacter>(FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, Spawn);
				Attacker->Weapons.Add(World->SpawnActor<ACampusWeapon>());
				Attacker->CurrentWeaponIndex = 0;
				Victim = World->SpawnActor<ACampusCharacter>(FVector(110.f, 0.f, 100.f), FRotator::ZeroRotator, Spawn);
				Victim->Health = 300.f;
				Victim->MaxHealth = 300.f;
				Victim->GetCapsuleComponent()->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
				FHitResult FixtureHit;
				const FVector Eye = Attacker->GetPawnViewLocation();
				FCollisionQueryParams FixtureParams(SCENE_QUERY_STAT(CampusMeleeFixture), false, Attacker);
				Test->TestTrue(TEXT("fixture places a hittable target within melee reach"), World->SweepSingleByChannel(FixtureHit, Eye, Eye + FVector(170.f, 0.f, 0.f), FQuat::Identity,
					CAMPUS_TRACE_WEAPON, FCollisionShape::MakeSphere(28.f), FixtureParams) && FixtureHit.GetActor() == Victim);
				Test->TestTrue(TEXT("fixture has authority and opposing targets"), Attacker->HasAuthority() && Victim->HasAuthority() && Attacker->IsEnemyOf(Victim));
				if (Scenario == TEXT("wall"))
				{
					AActor* Cover = World->SpawnActor<AActor>();
					UBoxComponent* Box = NewObject<UBoxComponent>(Cover);
					Cover->SetRootComponent(Box);
					Box->SetBoxExtent(FVector(3.f, 80.f, 150.f));
					Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
					Box->SetCollisionResponseToAllChannels(ECR_Ignore);
					Box->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
					Box->RegisterComponent();
					Cover->SetActorLocation(FVector(55.f, 0.f, 100.f));
				}
				Attacker->ServerMelee(Attacker->GetPawnViewLocation(), FVector::ForwardVector, 0);
				if (Scenario == TEXT("death"))
				{
					Attacker->bIsDead = true;
				}
				else if (Scenario == TEXT("switch"))
				{
					Attacker->Weapons.Add(World->SpawnActor<ACampusWeapon>());
					Attacker->RequestEquip(1);
				}
				Test->TestEqual(TEXT("windup does not damage before contact"), Victim->Health, 300.f);
				Attacker->ServerMelee(Attacker->GetPawnViewLocation(), FVector::ForwardVector, 0);
				FTimerHandle Canary;
				World->GetTimerManager().SetTimer(Canary, [this]() { bTimerAdvanced = true; }, 0.2f, false);
				World->GetTimerManager().Tick(0.f);
				return false;
			}
			if (Phase++ == 0)
			{
				World->GetTimerManager().Tick(0.1f);
				Test->TestEqual(TEXT("damage remains pending during windup"), Victim->Health, 300.f);
				return false;
			}
			World->GetTimerManager().Tick(0.15f);
			Test->TestTrue(TEXT("fixture advances real timer delegates through contact time"), bTimerAdvanced);
			Test->TestEqual(TEXT("contact respects cover, death, weapon changes and repeated requests"), Victim->Health, Scenario == TEXT("contact") ? 165.f : 300.f);
			World->DestroyWorld(false);
			return true;
		}

	private:
		FAutomationTestBase* Test;
		FString Scenario;
		UWorld* World = nullptr;
		ACampusCharacter* Attacker = nullptr;
		ACampusCharacter* Victim = nullptr;
		int32 Phase = 0;
		bool bTimerAdvanced = false;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCampusMeleeContactTest, "CampusOps.Experience.Melee", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCampusMeleeContactTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	for (const TCHAR* Scenario : { TEXT("contact"), TEXT("wall"), TEXT("death"), TEXT("switch") })
	{
		OutNames.Add(Scenario);
		OutCommands.Add(Scenario);
	}
}

bool FCampusMeleeContactTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCampusMeleeContactCommand(this, Parameters));
	return true;
}
#endif
