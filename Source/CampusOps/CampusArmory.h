#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CampusTypes.h"
#include "CampusArmory.generated.h"

/** 0..1 values shown as stat bars in the gunsmith. */
USTRUCT(BlueprintType)
struct FCampusDisplayStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stats") float Accuracy = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stats") float Damage = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stats") float Range = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stats") float FireRate = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stats") float Mobility = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stats") float Control = 0.f;

	static constexpr int32 Num = 6;
	float Get(int32 Index) const
	{
		switch (Index)
		{
		case 0: return Accuracy;
		case 1: return Damage;
		case 2: return Range;
		case 3: return FireRate;
		case 4: return Mobility;
		default: return Control;
		}
	}
	static FText Label(int32 Index);
};

/**
 * All weapon and attachment definitions. Defaults live in the constructor; everything is editable
 * under Project Settings > Game > Campus Armory and saved to DefaultGame.ini.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Campus Armory"))
class CAMPUSOPS_API UCampusArmory : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCampusArmory();

	static const UCampusArmory& Get() { return *GetDefault<UCampusArmory>(); }

	UPROPERTY(config, EditAnywhere, Category = "Armory")
	TArray<FCampusWeaponDef> Weapons;

	UPROPERTY(config, EditAnywhere, Category = "Armory")
	TArray<FCampusAttachmentDef> AttachmentDefs;

	const FCampusWeaponDef* FindWeapon(FName Id) const;
	const FCampusAttachmentDef* FindAttachment(FName Id) const;
	TArray<const FCampusWeaponDef*> GetWeapons(bool bSecondary, bool bIncludeMelee = true) const;
	TArray<const FCampusAttachmentDef*> GetAttachmentsForSlot(FName WeaponId, ECampusAttachmentSlot Slot) const;
	/** Slots of the weapon that have at least one attachment to fit (every one of them can be filled at once). */
	int32 CountModdableSlots(FName WeaponId) const;

	FCampusWeaponStats ComputeStats(const FCampusWeaponLoadout& Loadout) const;
	/** Removes unknown and duplicate-slot attachments (one attachment per slot, every slot may be filled). */
	void Sanitize(FCampusWeaponLoadout& Loadout, bool bSecondary) const;
	void SanitizeLoadout(FCampusLoadout& Loadout) const;
	/** Equips Attachment in its slot (replacing what was there). */
	bool SetAttachment(FCampusWeaponLoadout& Loadout, FName AttachmentId, ECampusAttachmentSlot Slot) const;
	FName GetAttachmentInSlot(const FCampusWeaponLoadout& Loadout, ECampusAttachmentSlot Slot) const;
	const FCampusAttachmentDef* GetOptic(const FCampusWeaponLoadout& Loadout) const;
	float GetBarrelExtension(const FCampusWeaponLoadout& Loadout) const;
	float GetMuzzleDeviceExtension(const FCampusWeaponLoadout& Loadout) const;
	bool HasLaser(const FCampusWeaponLoadout& Loadout) const;

	FCampusLoadout MakeDefaultLoadout(int32 Index) const;
	/** Random pair of weapons with a few random attachments, used by Gunfight rounds. */
	FCampusLoadout MakeRandomLoadout(FRandomStream& Stream) const;

	static FCampusDisplayStats ToDisplay(const FCampusWeaponStats& Stats);
	static void DescribeModifiers(const FCampusAttachmentDef& Def, TArray<FText>& OutPros, TArray<FText>& OutCons);
};
