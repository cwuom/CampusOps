#pragma once

#include "CoreMinimal.h"

namespace CampusWetness
{
	inline float Advance(float Current, float Exposure, float DeltaSeconds)
	{
		const float Target = FMath::Clamp(Exposure, 0.f, 1.f);
		const float Rate = Target > Current ? 0.28f : 0.014f;
		return FMath::Lerp(FMath::Clamp(Current, 0.f, 1.f), Target, 1.f - FMath::Exp(-Rate * FMath::Max(DeltaSeconds, 0.f)));
	}
}
