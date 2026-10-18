#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CampusBreathComponent.generated.h"

class ACampusCharacter;

/**
 * Breathing of one character. Exertion (0..1) builds while sprinting and sliding and recovers at a walk: the local
 * player hears themselves pant after a run (slowing as the breath comes back) and a quiet breath now and then at rest,
 * and the weapon sways more while winded.
 *
 * Holding the breath (sprint key while aiming) steadies the sight for a few seconds; held too long the lungs give out
 * and the sight shakes until the breath is caught again.
 */
UCLASS(ClassGroup = Campus, meta = (BlueprintSpawnableComponent))
class CAMPUSOPS_API UCampusBreathComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCampusBreathComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void ResetAll();

	float GetExertion01() const { return Exertion; }
	/** Weapon sway multiplier: grows while winded, almost nothing while the breath is held. */
	float GetSwayScale() const;
	/** Shaking sight once a held breath has run out (0..1). */
	float GetBreathShake01() const { return BreathShake; }

	bool IsHoldingBreath() const { return bHoldingBreath; }
	/** Lungs left for holding the breath (1 full .. 0 empty). */
	float GetBreathReserve01() const { return BreathReserve; }
	/** Sprint pressed / released while aiming. */
	void SetHoldBreath(bool bHold);

private:
	void UpdateAudio();

	UPROPERTY(Transient) TObjectPtr<ACampusCharacter> Character;
	float Exertion = 0.f;
	float BreathReserve = 1.f;
	float BreathShake = 0.f;
	bool bHoldingBreath = false;
	bool bBreathSpent = false;
	float NextBreathTime = 0.f;
};
