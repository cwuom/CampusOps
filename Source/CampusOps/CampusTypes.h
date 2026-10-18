#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumRange.h"
#include "CampusTypes.generated.h"

class AActor;
class USkeletalMesh;

/** Trace channel used by all weapon traces (configured as "Weapon" in DefaultEngine.ini). */
#define CAMPUS_TRACE_WEAPON ECC_GameTraceChannel1

UENUM(BlueprintType)
enum class ECampusTeam : uint8
{
	Alpha = 0,
	Bravo = 1,
	None = 2
};

UENUM(BlueprintType)
enum class ECampusMatchMode : uint8
{
	TeamDeathmatch,
	Gunfight
};

/** Every map the builder can lay out. HQ is the main menu's operations centre, not a battlefield. */
UENUM(BlueprintType)
enum class ECampusMap : uint8
{
	Campus,
	Hotel,
	HQ,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ECampusWeather : uint8
{
	Clear,
	Cloudy,
	Overcast,
	Rain,
	Storm,
	Fog,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ECampusTimeOfDay : uint8
{
	Morning,
	Noon,
	Afternoon,
	Dusk,
	Night,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ECampusMatchPhase : uint8
{
	Warmup,
	Countdown,
	Playing,
	RoundOver,
	MatchOver
};

UENUM(BlueprintType)
enum class ECampusWeaponClass : uint8
{
	AssaultRifle,
	SMG,
	Marksman,
	Pistol,
	Shotgun,
	LMG,
	/** Secondary slot: a blade, swung instead of fired (no ammunition, no sights). */
	Melee
};

UENUM(BlueprintType)
enum class ECampusFireMode : uint8
{
	Auto,
	Burst,
	Semi
};

UENUM(BlueprintType)
enum class ECampusAttachmentSlot : uint8
{
	Muzzle,
	Barrel,
	Laser,
	Optic,
	Stock,
	Underbarrel,
	Magazine,
	RearGrip,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(ECampusAttachmentSlot, ECampusAttachmentSlot::Count);

UENUM(BlueprintType)
enum class ECampusHitZone : uint8
{
	Head,
	UpperTorso,
	LowerTorso,
	Limb
};

UENUM(BlueprintType)
enum class ECampusReticle : uint8
{
	None,
	Dot,
	Ring,
	Scope
};

UENUM(BlueprintType)
enum class ECampusShape : uint8
{
	Cube,
	Cylinder,
	Sphere,
	Cone
};

UENUM(BlueprintType)
enum class ECampusSurface : uint8
{
	Concrete,
	Metal,
	Wood,
	Dirt,
	Flesh,
	Glass,
	Water
};

/** Hand-thrown equipment: two lethals (G) and two tacticals (Q). */
UENUM(BlueprintType)
enum class ECampusThrowable : uint8
{
	Frag,
	Thermite,
	Flash,
	Smoke,
	Count UMETA(Hidden)
};

/** Perk families, colour coded in the loadout. Three perks of one family earn that family's specialty bonus. */
UENUM(BlueprintType)
enum class ECampusPerkGroup : uint8
{
	Enforcer,
	Recon,
	Strategist,
	Count UMETA(Hidden)
};

/** Three perk slots; every slot offers at least one perk of each family. */
UENUM(BlueprintType)
enum class ECampusPerk : uint8
{
	None,
	// Slot 1
	Lightweight,
	Ghost,
	FlakJacket,
	Bandolier,
	// Slot 2
	FastHands,
	Dexterity,
	ColdBlooded,
	TacMask,
	// Slot 3
	QuickFix,
	Ninja,
	HighAlert,
	Scavenger,
	Count UMETA(Hidden)
};

/** What a weapon model part does when the weapon is handled (drives the procedural reload / fire motion). */
UENUM(BlueprintType)
enum class ECampusPartRole : uint8
{
	Static,
	Magazine,
	/** Cycles back with every shot (bolt carrier, op rod). */
	Bolt,
	/** Pulled on an empty reload (charging handle, cocking lever). */
	Charging,
	/** Pistol slide: recoils every shot and locks back on empty; slide-mounted optics ride on it. */
	Slide,
	Trigger,
	Hammer
};

/**
 * One mesh of a weapon / attachment model, authored in weapon frame space (X forward along the bore, Z up, origin at
 * the pistol grip where the palm sits). Weapon parts live in /Game/Campus/Weapons/<WeaponId>/, generic attachment
 * parts in /Game/Campus/Weapons/Attachments/ (relative to the slot anchor).
 */
USTRUCT(BlueprintType)
struct FCampusModelPart
{
	GENERATED_BODY()

	/** Static mesh asset name (an absolute object path is used as is). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") FString Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") ECampusPartRole Role = ECampusPartRole::Static;
	/** Default hardware of a slot: hidden while an attachment of that slot is fitted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") ECampusAttachmentSlot HideWith = ECampusAttachmentSlot::Count;
	/** Mounting hardware (rails, risers): only shown while an attachment of that slot is fitted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") ECampusAttachmentSlot ShowWith = ECampusAttachmentSlot::Count;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") FTransform Transform = FTransform::Identity;

	FCampusModelPart() {}
	FCampusModelPart(const TCHAR* InMesh, ECampusPartRole InRole = ECampusPartRole::Static, ECampusAttachmentSlot InHideWith = ECampusAttachmentSlot::Count, ECampusAttachmentSlot InShowWith = ECampusAttachmentSlot::Count)
		: Mesh(InMesh), Role(InRole), HideWith(InHideWith), ShowWith(InShowWith) {}
};

/** A primitive used to build attachments and weapon body kits out of engine basic shapes. Coordinates are in "weapon frame" (X forward along barrel, Z up, origin at grip). */
USTRUCT(BlueprintType)
struct FCampusVisualPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	ECampusShape Shape = ECampusShape::Cube;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Size in centimeters (cube: X,Y,Z extents; cylinder: diameter in X/Y, height in Z). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FVector Size = FVector(1.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FLinearColor Color = FLinearColor(0.03f, 0.03f, 0.03f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	float Roughness = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	float Metallic = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	float Emissive = 0.0f;

	FCampusVisualPart() {}
	FCampusVisualPart(ECampusShape InShape, const FVector& InLoc, const FVector& InSize, const FLinearColor& InColor, const FRotator& InRot = FRotator::ZeroRotator, float InRough = 0.45f, float InMetal = 0.6f, float InEmissive = 0.f)
		: Shape(InShape), Location(InLoc), Rotation(InRot), Size(InSize), Color(InColor), Roughness(InRough), Metallic(InMetal), Emissive(InEmissive) {}
};

/** Final, attachment-modified weapon stats. */
USTRUCT(BlueprintType)
struct FCampusWeaponStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float DamageNear = 28.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float DamageMid = 23.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float DamageFar = 19.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float RangeNear = 2500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float RangeMid = 4500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float HeadMultiplier = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float LowerTorsoMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float LimbMultiplier = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage") float Penetration = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float RPM = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") int32 BurstCount = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") int32 MagSize = 30;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") int32 ReserveMags = 3;
	/** Shotguns: projectiles per shot (damage values are per pellet) and the cone they spread over (degrees, half angle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") int32 Pellets = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float PelletSpread = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float ReloadTime = 2.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float EmptyReloadTime = 2.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float ReloadInsertFraction = 0.62f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float ADSTime = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float SprintToFireTime = 0.22f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float TacSprintToFireTime = 0.38f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float EquipTime = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float MoveSpeedMultiplier = 0.96f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float ADSMoveSpeedMultiplier = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float ADSZoom = 1.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling") float AimStability = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float HipSpread = 3.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float MoveSpread = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float AirSpread = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float SpreadPerShot = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float MaxBloom = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float BloomRecovery = 7.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy") float ADSSpread = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float RecoilPitch = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float RecoilYaw = 0.20f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float RecoilYawBias = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float FirstShotRecoilScale = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float ADSRecoilScale = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float RecoilRecovery = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil") float VisualKick = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Misc") bool bSuppressed = false;

	float GetDamageAtDistance(float Distance) const
	{
		if (Distance <= RangeNear) return DamageNear;
		if (Distance <= RangeMid) return DamageMid;
		return DamageFar;
	}
	float GetShotInterval() const { return 60.f / FMath::Max(RPM, 1.f); }
};

/** Multiplicative / additive modifiers granted by an attachment. */
USTRUCT(BlueprintType)
struct FCampusStatModifiers
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float Damage = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float Range = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float VerticalRecoil = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float HorizontalRecoil = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float HipSpread = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float ADSTime = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float MoveSpeed = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float ADSMoveSpeed = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float ReloadTime = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float SprintToFire = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float AimStability = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float VisualKick = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float Penetration = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float MagSizeScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") float ZoomOverride = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mods") bool bSuppressor = false;
};

USTRUCT(BlueprintType)
struct FCampusWeaponDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") ECampusWeaponClass Class = ECampusWeaponClass::AssaultRifle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") TArray<ECampusFireMode> FireModes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon") FCampusWeaponStats Stats;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") TSoftObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FVector MeshScale = FVector(1.0);
	/** Extra offset of the mesh inside the weapon frame, used to re-center scaled meshes on the grip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FVector MeshOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") bool bPistolAnims = false;
	/** Replace the mesh materials with a flat tinted finish (the template rifle is white sci-fi plastic). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") bool bTintMesh = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FLinearColor MeshTint = FLinearColor(0.035f, 0.036f, 0.04f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") TArray<FCampusVisualPart> BodyParts;
	/** Modelled weapon (replaces Mesh + BodyParts when present). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") TArray<FCampusModelPart> Model;
	/** Exposed barrel between the Barrel anchor (handguard end) and the muzzle thread, and its radius: barrel attachments are fitted to these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") float ExposedBarrel = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") float BarrelRadius = 0.95f;
	/** Length of the stock muzzle device (flash hider / brake) when no muzzle attachment is fitted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") float MuzzleDeviceLength = 0.f;
	/** Lasers are modelled for a rail at 3 o'clock (outward +Y); pistols mount them under the dust cover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FRotator LaserRotation = FRotator::ZeroRotator;
	/** How far the bolt / op rod / slide runs back on each shot (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") float BoltTravel = 0.f;
	/** Attachment anchor points in weapon frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") TMap<ECampusAttachmentSlot, FVector> Anchors;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FVector IronSight = FVector(10.f, 0.f, 9.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FVector Muzzle = FVector(50.f, 0.f, 4.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual") FVector EjectPort = FVector(6.f, 2.f, 5.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio") FName FireSound = TEXT("Gun_AR");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gunsmith") TArray<FName> Attachments;

	/** Viewmodel placement relative to the aligned ADS pose, in camera space (cm / degrees). */
	/** Hip: lower right of the frame at about the ADS depth, muzzle converging on the crosshair, a slight cant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FVector HipOffset = FVector(5.f, 7.5f, -3.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FRotator HipRotation = FRotator(1.f, -4.f, -5.f);
	/** Sprint carry, relative to the hip pose (around the grip): turned across the body, muzzle low-left, canted, both hands on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FVector SprintOffset = FVector(4.f, 7.f, 3.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FRotator SprintRotation = FRotator(-8.f, -24.f, 36.f);
	/** Tactical sprint, absolute in camera space: grip position (cm) and weapon orientation - raised one-handed at the right edge, muzzle up, optic outward, grip and magazine below the frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FVector TacSprintOffset = FVector(31.f, 22.f, -29.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FRotator TacSprintRotation = FRotator(76.f, 0.f, 95.f);
	/** Degrees the raised weapon leans in toward the screen centre (it rocks +-4 degrees around this with the stride).
	 *  Kept small: leaning further carries the barrel across the middle of the view. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") float TacSprintLean = 6.f;
	/** Furthest the sight may sit from the eye when aiming (cm); closer sights keep their natural cheek-weld distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") float AimDistance = 26.f;
	/** Empty reloads: the support hand racks the charging / bolt handle instead of slapping the bolt catch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") bool bRackHandle = false;
	/** A right-side handle can still be racked from the side with an optic on top; false where the receiver is too
	 *  wide for the hand to get round (the empty reload then hits the bolt release on the near side instead). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") bool bRackUnderOptic = true;
	/** Extra clearance (cm) of the support hand from the magazine well on a wide receiver or chassis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") float ReloadHandOut = 0.f;
	/** Magazines that hook in at the front and rock back into the well (AK pattern). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") bool bRockInMagazine = false;
	/** Reload pose relative to the hip pose, about the grip: canted so the magazine well faces the support hand. */
	// Held out and low with the muzzle swung left: the stock leaves the frame to the right instead of coming at the eye.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FRotator ReloadRotation = FRotator(2.f, -10.f, 34.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FVector ReloadOffset = FVector(12.f, -5.f, 0.f);
	/** Irons: eye distance behind IronSight when no optic is fitted (0 keeps the cheek-weld distance, up to AimDistance). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") float IronEyeRelief = 0.f;
	/** Irons: X of the front post in weapon frame (the eye focuses on it; the rear sight and receiver blur). 0 = near the muzzle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") float FrontSightX = 0.f;
	/** Support hand IK target in weapon frame space (before MeshScale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") bool bLeftHandIK = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Viewmodel") FVector LeftHandGrip = FVector(27.f, -3.f, 0.f);

	const FVector& GetAnchor(ECampusAttachmentSlot Slot) const
	{
		if (const FVector* Found = Anchors.Find(Slot)) return *Found;
		return FVector::ZeroVector;
	}
	bool IsMelee() const { return Class == ECampusWeaponClass::Melee; }
	/** Pistols and blades go in the secondary slot. */
	bool IsSecondary() const { return Class == ECampusWeaponClass::Pistol || Class == ECampusWeaponClass::Melee; }
};

USTRUCT(BlueprintType)
struct FCampusAttachmentDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") ECampusAttachmentSlot Slot = ECampusAttachmentSlot::Muzzle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") FCampusStatModifiers Mods;
	/** Visual parts relative to the slot anchor (weapon frame axes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") TArray<FCampusVisualPart> Parts;
	/** Modelled attachment, relative to the slot anchor (replaces Parts when present). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") TArray<FCampusModelPart> Model;
	/** Uses the weapon's own variant (/Game/Campus/Weapons/<W>/SM_<W>_<Id>, in weapon frame space) when it exists: magazines, grips. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") bool bPerWeaponModel = false;
	/** Model is a unit barrel tube stretched to the weapon's barrel (radius and exposed length + MuzzleExtension). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") bool bBarrelTube = false;
	/** Laser emitter relative to the slot anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser") FVector LaserOffset = FVector(3.6f, 1.3f, 0.45f);
	/** How far this attachment pushes the muzzle forward (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attachment") float MuzzleExtension = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optic") ECampusReticle Reticle = ECampusReticle::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optic") FLinearColor ReticleColor = FLinearColor(1.f, 0.02f, 0.012f);
	/** Height of the optic aim point above the optic anchor (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optic") float SightHeight = 0.f;
	/** Forward position of the reticle relative to the optic anchor (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optic") float ReticleForward = 0.f;
	/** Optic window (width x height, cm) the reticle glass fills; round when Y == 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optic") FVector2D Window = FVector2D(2.6f, 0.f);
	/** Magnified optics: objective / ocular glass positions along the sight axis (cm from the anchor) and diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optic") FVector LensFrontRearDiameter = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser") bool bLaser = false;
	/** Weapon light: a lens at LaserOffset that the owner switches on and off (off whenever it is fitted or drawn). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Laser") bool bFlashlight = false;
	/** Foregrips: the support hand holds the grip instead of the handguard. HandGrip is the wrist target relative to the
	 *  slot anchor (weapon frame), HandTurn the wrist turn onto it (camera space pitch / yaw / roll). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip") bool bForegrip = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip") FVector HandGrip = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip") FRotator HandTurn = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct FCampusWeaponLoadout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") FName WeaponId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") TArray<FName> Attachments;
};

USTRUCT(BlueprintType)
struct FCampusLoadout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") FCampusWeaponLoadout Primary;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") FCampusWeaponLoadout Secondary;
	/** Equipment on the lethal (G) and tactical (Q) keys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") ECampusThrowable Lethal = ECampusThrowable::Frag;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") ECampusThrowable Tactical = ECampusThrowable::Flash;
	/** One perk per slot (index 0..2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout") TArray<ECampusPerk> Perks;
};

/** Host-chosen rules of a custom match (defaults are the standard rules). Replicated on the game state. */
USTRUCT(BlueprintType)
struct FCampusMatchRules
{
	GENERATED_BODY()

	UPROPERTY() bool bCustom = false;
	/** Human players take no damage. */
	UPROPERTY() bool bGodMode = false;
	UPROPERTY() int32 PlayerHealth = 100;
	UPROPERTY() int32 BotHealth = 100;
	/** Bottomless magazines: shots never empty the magazine, so there is nothing to reload. */
	UPROPERTY() bool bNoReload = false;
	/** Reloads never draw down the reserve. */
	UPROPERTY() bool bInfiniteAmmo = false;
	UPROPERTY() bool bInfiniteEquipment = false;
	UPROPERTY() bool bHealthRegen = true;
};

USTRUCT(BlueprintType)
struct FCampusKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY() FString KillerName;
	UPROPERTY() FString VictimName;
	UPROPERTY() ECampusTeam KillerTeam = ECampusTeam::None;
	UPROPERTY() ECampusTeam VictimTeam = ECampusTeam::None;
	UPROPERTY() FName WeaponId;
	UPROPERTY() bool bHeadshot = false;
	UPROPERTY() float Time = 0.f;
};

/** One bullet hit reported by the shooter (client-side hit detection, validated by the server). */
USTRUCT()
struct FCampusShotHit
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<AActor> Actor = nullptr;
	UPROPERTY() FVector_NetQuantize Location = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantizeNormal Normal = FVector::UpVector;
	UPROPERTY() FName Bone;
	UPROPERTY() uint8 Surface = 0;
	UPROPERTY() uint8 Penetrations = 0;
};

USTRUCT()
struct FCampusShot
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize Origin = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
	UPROPERTY() FVector_NetQuantize End = FVector::ZeroVector;
	UPROPERTY() TArray<FCampusShotHit> Hits;
	/** Shotguns: where each pellet ended (its hits are in Hits). */
	UPROPERTY() TArray<FVector_NetQuantize> PelletEnds;
};

namespace CampusText
{
	inline FText SlotName(ECampusAttachmentSlot Slot)
	{
		switch (Slot)
		{
		case ECampusAttachmentSlot::Muzzle: return NSLOCTEXT("Campus", "SlotMuzzle", "枪口");
		case ECampusAttachmentSlot::Barrel: return NSLOCTEXT("Campus", "SlotBarrel", "枪管");
		case ECampusAttachmentSlot::Laser: return NSLOCTEXT("Campus", "SlotLaser", "激光 / 枪灯");
		case ECampusAttachmentSlot::Optic: return NSLOCTEXT("Campus", "SlotOptic", "瞄具");
		case ECampusAttachmentSlot::Stock: return NSLOCTEXT("Campus", "SlotStock", "枪托");
		case ECampusAttachmentSlot::Underbarrel: return NSLOCTEXT("Campus", "SlotUnder", "下挂");
		case ECampusAttachmentSlot::Magazine: return NSLOCTEXT("Campus", "SlotMag", "弹匣");
		case ECampusAttachmentSlot::RearGrip: return NSLOCTEXT("Campus", "SlotGrip", "后握把");
		default: return FText::GetEmpty();
		}
	}

	inline FText WeaponClassName(ECampusWeaponClass Class)
	{
		switch (Class)
		{
		case ECampusWeaponClass::AssaultRifle: return NSLOCTEXT("Campus", "ClassAR", "突击步枪");
		case ECampusWeaponClass::SMG: return NSLOCTEXT("Campus", "ClassSMG", "冲锋枪");
		case ECampusWeaponClass::Marksman: return NSLOCTEXT("Campus", "ClassDMR", "精确射手步枪");
		case ECampusWeaponClass::Pistol: return NSLOCTEXT("Campus", "ClassPistol", "手枪");
		case ECampusWeaponClass::Shotgun: return NSLOCTEXT("Campus", "ClassShotgun", "霰弹枪");
		case ECampusWeaponClass::LMG: return NSLOCTEXT("Campus", "ClassLMG", "轻机枪");
		case ECampusWeaponClass::Melee: return NSLOCTEXT("Campus", "ClassMelee", "近战武器");
		default: return FText::GetEmpty();
		}
	}

	inline FText FireModeName(ECampusFireMode Mode)
	{
		switch (Mode)
		{
		case ECampusFireMode::Auto: return NSLOCTEXT("Campus", "FMAuto", "全自动");
		case ECampusFireMode::Burst: return NSLOCTEXT("Campus", "FMBurst", "三连发");
		case ECampusFireMode::Semi: return NSLOCTEXT("Campus", "FMSemi", "半自动");
		default: return FText::GetEmpty();
		}
	}

	inline FText ThrowableName(ECampusThrowable Type)
	{
		switch (Type)
		{
		case ECampusThrowable::Frag: return NSLOCTEXT("Campus", "TFrag", "破片手雷");
		case ECampusThrowable::Thermite: return NSLOCTEXT("Campus", "TThermite", "铝热剂");
		case ECampusThrowable::Flash: return NSLOCTEXT("Campus", "TFlash", "震撼弹");
		case ECampusThrowable::Smoke: return NSLOCTEXT("Campus", "TSmoke", "烟雾弹");
		default: return FText::GetEmpty();
		}
	}

	inline FName ThrowableId(ECampusThrowable Type)
	{
		switch (Type)
		{
		case ECampusThrowable::Frag: return TEXT("Frag");
		case ECampusThrowable::Thermite: return TEXT("Thermite");
		case ECampusThrowable::Flash: return TEXT("Flash");
		default: return TEXT("Smoke");
		}
	}

	inline FLinearColor TeamColor(ECampusTeam Team, bool bFriendly)
	{
		return bFriendly ? FLinearColor(0.20f, 0.62f, 1.0f) : FLinearColor(1.0f, 0.22f, 0.16f);
	}
}
