#pragma once

#include "CoreMinimal.h"
#include "CampusTypes.h"

struct FCampusPerkInfo
{
	ECampusPerk Perk = ECampusPerk::None;
	/** 0..2 */
	int32 Slot = 0;
	ECampusPerkGroup Group = ECampusPerkGroup::Enforcer;
	FText Name;
	FText Description;
};

/** Perk catalogue, family colours and the specialty earned by three perks of one family. */
namespace CampusPerks
{
	constexpr int32 NumSlots = 3;

	CAMPUSOPS_API const FCampusPerkInfo& Info(ECampusPerk Perk);
	CAMPUSOPS_API TArray<ECampusPerk> PerksForSlot(int32 Slot);
	CAMPUSOPS_API ECampusPerk DefaultPerk(int32 Slot);

	CAMPUSOPS_API FLinearColor GroupColor(ECampusPerkGroup Group);
	CAMPUSOPS_API FText GroupName(ECampusPerkGroup Group);
	CAMPUSOPS_API FText SpecialtyName(ECampusPerkGroup Group);
	CAMPUSOPS_API FText SpecialtyDescription(ECampusPerkGroup Group);

	/** The family all three perks share, or Count when they are mixed. */
	CAMPUSOPS_API ECampusPerkGroup GetSpecialty(const TArray<ECampusPerk>& Perks);
	/** One valid perk per slot, in slot order. */
	CAMPUSOPS_API void Sanitize(TArray<ECampusPerk>& Perks);

	CAMPUSOPS_API bool IsLethalThrowable(ECampusThrowable Type);
}
