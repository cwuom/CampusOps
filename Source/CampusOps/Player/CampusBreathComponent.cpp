#include "Player/CampusBreathComponent.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusMovementComponent.h"
#include "Weapons/CampusWeapon.h"
#include "CampusAssets.h"
#include "Engine/World.h"

UCampusBreathComponent::UCampusBreathComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false);
}

void UCampusBreathComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACampusCharacter>(GetOwner());
}

void UCampusBreathComponent::ResetAll()
{
	Exertion = 0.f;
	BreathReserve = 1.f;
	BreathShake = 0.f;
	bHoldingBreath = false;
	bBreathSpent = false;
}

void UCampusBreathComponent::SetHoldBreath(bool bHold)
{
	if (bHold == bHoldingBreath || (bHold && (bBreathSpent || BreathReserve < 0.15f)))
	{
		return;
	}
	bHoldingBreath = bHold;
	if (Character && Character->IsLocalHuman())
	{
		CampusAudio::Play2D(this, bHold ? TEXT("Breath_Hold") : TEXT("Breath_Exhale"), bHold ? 0.45f : 0.55f, 0.04f);
	}
}

float UCampusBreathComponent::GetSwayScale() const
{
	return bHoldingBreath ? 0.12f : 1.f + 1.4f * Exertion + 3.f * BreathShake;
}

void UCampusBreathComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Only the local player hears and feels their own breathing.
	if (!Character || !Character->IsLocalHuman())
	{
		return;
	}
	if (!Character->IsAlive())
	{
		ResetAll();
		return;
	}
	// Exertion: a tactical sprint winds the character fastest, a sprint less; walking or standing they get it back.
	float Target = 0.f;
	float Rate = 0.f;
	if (Character->IsSliding())
	{
		Target = 1.f;
		Rate = 0.2f;
	}
	else if (Character->IsTacSprinting())
	{
		Target = 1.f;
		Rate = 0.16f;
	}
	else if (Character->IsSprinting())
	{
		Target = 0.75f;
		Rate = 0.09f;
	}
	if (Rate > 0.f && Exertion < Target)
	{
		Exertion = FMath::Min(Target, Exertion + Rate * DeltaTime);
	}
	else
	{
		const UCampusMovementComponent* Move = Character->GetCampusMovement();
		const bool bStill = !Move || Move->Velocity.Size2D() < 150.f;
		Exertion = FMath::Max(0.f, Exertion - (bStill ? 0.11f : 0.07f) * DeltaTime);
	}

	// Held breath: about four seconds of steady sight, then the lungs give out and the sight shakes until it is caught.
	if (bHoldingBreath && (!Character->IsAiming() || !Character->GetCurrentWeapon()))
	{
		SetHoldBreath(false);
	}
	if (bHoldingBreath)
	{
		BreathReserve = FMath::Max(0.f, BreathReserve - DeltaTime / (4.2f * (1.f - 0.4f * Exertion)));
		if (BreathReserve <= 0.f)
		{
			bHoldingBreath = false;
			bBreathSpent = true;
			BreathShake = 1.f;
			CampusAudio::Play2D(this, TEXT("Breath_Exhale"), 0.8f, 0.05f);
		}
	}
	else
	{
		BreathReserve = FMath::Min(1.f, BreathReserve + DeltaTime / 3.5f);
		if (bBreathSpent && BreathReserve > 0.6f)
		{
			bBreathSpent = false;
		}
	}
	BreathShake = FMath::Max(0.f, BreathShake - DeltaTime * 0.45f);
	UpdateAudio();
}

void UCampusBreathComponent::UpdateAudio()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bHoldingBreath)
	{
		NextBreathTime = FMath::Max(NextBreathTime, Now + 0.6f);
		return;
	}
	if (Now < NextBreathTime)
	{
		return;
	}
	// Winded panting after a run (slowing as the breath comes back), gasping after a held breath, otherwise a quiet
	// breath now and then.
	if (BreathShake > 0.3f)
	{
		CampusAudio::Play2D(this, TEXT("Breath_Sprint"), 0.7f, 0.06f);
		NextBreathTime = Now + 0.75f;
	}
	else if (Exertion > 0.3f)
	{
		CampusAudio::Play2D(this, TEXT("Breath_Sprint"), 0.2f + 0.6f * Exertion, 0.06f);
		NextBreathTime = Now + FMath::Lerp(1.9f, 0.8f, Exertion) + FMath::FRandRange(-0.1f, 0.1f);
	}
	else
	{
		CampusAudio::Play2D(this, TEXT("Breath_Calm"), 0.12f, 0.05f);
		NextBreathTime = Now + FMath::FRandRange(4.5f, 6.5f);
	}
}
