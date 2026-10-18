#pragma once

#include "CoreMinimal.h"
#include "CampusTypes.h"
#include "Map/CampusSurfaces.h"

class UStaticMesh;
class USkeletalMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAnimSequenceBase;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class UTextureRenderTarget2D;

/** Central place for every asset path the game uses, with graceful fallbacks when bootstrap assets are missing. */
namespace CampusAssets
{
	enum class EAnim : uint8
	{
		RifleIdle,
		RifleReload,
		RifleEquip,
		RifleFire,
		PistolIdle,
		PistolReload,
		PistolEquip,
		PistolFire,
		HitFrontLight1,
		HitFrontLight2,
		HitFrontLight3,
		HitFrontLight4,
		HitFrontMedium1,
		HitFrontMedium2,
		HitFrontHeavy,
		HitBack
	};

	enum class EAttenuation : uint8
	{
		Gunshot,
		Impact,
		Foley
	};

	CAMPUSOPS_API UObject* LoadPath(const TCHAR* Path);

	template <typename T>
	T* Load(const TCHAR* Path) { return Cast<T>(LoadPath(Path)); }

	CAMPUSOPS_API UStaticMesh* Shape(ECampusShape Shape);
	/** Weapon kit mesh: /Game/Campus/Weapons/<Folder>/<Name> (Name may also be a full object path). */
	CAMPUSOPS_API UStaticMesh* WeaponMesh(const FString& Folder, const FString& Name);
	CAMPUSOPS_API UStaticMesh* PlaneMesh();
	/** City plain and mountain ranges round the school (null before the bootstrap has built it). */
	CAMPUSOPS_API UStaticMesh* TerrainMesh();
	CAMPUSOPS_API USkeletalMesh* OperatorMesh(int32 Index);
	CAMPUSOPS_API USkeletalMesh* RifleMesh();
	CAMPUSOPS_API USkeletalMesh* PistolMesh();
	CAMPUSOPS_API UClass* ThirdPersonAnimClass(bool bPistol);
	CAMPUSOPS_API UAnimSequenceBase* Anim(EAnim Anim);

	/** Opaque lit material with Color/Roughness/Metallic/Emissive parameters. */
	CAMPUSOPS_API UMaterialInterface* LitMaterial();
	/** Lit material driven by 8 per-instance custom data floats (rgb, rough, metal, emissive, tile size, seam). */
	CAMPUSOPS_API UMaterialInterface* InstancedMaterial();
	/** Photoscanned surface (MI_Surface_*) driven by the same per-instance data (rgb, rough, metal, emissive, texture repeat, dirt). */
	CAMPUSOPS_API UMaterialInterface* SurfaceMaterial(ECampusMat Mat);
	/** Uniform painted onto the mannequin by body region; scalar "Team" picks the camo. */
	CAMPUSOPS_API UMaterialInterface* SoldierMaterial();
	/** Additive unlit soft sprite: Color, Intensity. */
	CAMPUSOPS_API UMaterialInterface* AdditiveMaterial();
	/** Opaque unlit: Color, Intensity. */
	CAMPUSOPS_API UMaterialInterface* UnlitMaterial();
	/** Deferred decal: Color, Opacity. */
	CAMPUSOPS_API UMaterialInterface* DecalMaterial();
	/** Blood spatter decal: Seed, Stretch (0 round .. 1 thrown), Opacity. */
	CAMPUSOPS_API UMaterialInterface* BloodDecalMaterial();
	/** Optic glass with a parallax-free reticle: Color, Intensity, Ring, Round, DotMOA, RingMOA, LensTint, LensOpacity. */
	CAMPUSOPS_API UMaterialInterface* ReticleMaterial();
	/** Magnified optic eyepiece: coated glass at the hip, the live magnified view (View texture) with the eye on it. */
	CAMPUSOPS_API UMaterialInterface* ScopeLensMaterial();
	/** Post process that lays a magnified optic's sight picture (View texture) into its eyepiece's circle on screen. */
	CAMPUSOPS_API UMaterialInterface* ScopeViewMaterial();
	/** Straps the load-bearing gear (helmet, plate carrier, belt, knee pads) onto a mannequin body (no-op if already on). */
	CAMPUSOPS_API void AttachSoldierGear(USkeletalMeshComponent* Body, TArray<TObjectPtr<UStaticMeshComponent>>& InOutParts);
	/** Team colours for the gear: 0 coyote, 1 black. */
	CAMPUSOPS_API void ColorSoldierGear(const TArray<TObjectPtr<UStaticMeshComponent>>& Parts, int32 Team);
	/** Camera-facing soft sprite driven by per-instance data (0 opacity, 1-3 colour). */
	CAMPUSOPS_API UMaterialInterface* SmokeMaterial();
	/** Translucent rain streak / splash: Color, Intensity, Opacity; fades towards the ends of the box. */
	CAMPUSOPS_API UMaterialInterface* RainMaterial();
	/** Rain curtain cards (per instance: column width, streak spacing, streak opacity, haze). */
	CAMPUSOPS_API UMaterialInterface* RainSheetMaterial();
	/** Translucent pool water, and the ripple rings spreading on it (per instance: age 0..1, strength). */
	CAMPUSOPS_API UMaterialInterface* WaterMaterial();
	CAMPUSOPS_API UMaterialInterface* RippleMaterial();
	/** One step of the water surface simulation (drawn into a render target), and the caustics decal under water. */
	CAMPUSOPS_API UMaterialInterface* WaterSimMaterial();
	CAMPUSOPS_API UMaterialInterface* CausticsMaterial();
	/** Shared environment parameters: Wetness, NightLights, Rain, and the rain mask capture (MaskX, MaskY, MaskSize, MaskTop). */
	CAMPUSOPS_API class UMaterialParameterCollection* EnvironmentCollection();
	/** Top-down depth of the map (what the sky can reach), written at runtime by the environment. */
	CAMPUSOPS_API class UTextureRenderTarget2D* RainMaskTarget();
	/** Additive flame sprite driven by per-instance data (0 intensity, 1-3 colour). */
	CAMPUSOPS_API UMaterialInterface* FireMaterial();
	/** Burn mark decal: Seed, Opacity. */
	CAMPUSOPS_API UMaterialInterface* ScorchMaterial();
	/** Translucent crack star round a bullet hole in glass (plane mesh). */
	CAMPUSOPS_API UMaterialInterface* GlassCrackMaterial();
	/** Runtime-drawn sign texture: SignTex, Tint, Roughness, Emissive. */
	CAMPUSOPS_API UMaterialInterface* SignMaterial();
	/** LCD panel: Screen (picture), UVMin / UVMax (the part of it on this panel), Emissive. */
	CAMPUSOPS_API UMaterialInterface* DisplayMaterial();

	CAMPUSOPS_API UMaterialInstanceDynamic* MakeLitMID(UObject* Outer, const FLinearColor& Color, float Roughness = 0.5f, float Metallic = 0.f, float Emissive = 0.f);
	CAMPUSOPS_API UMaterialInstanceDynamic* MakeGlowMID(UObject* Outer, const FLinearColor& Color, float Intensity, bool bAdditive);
	/** Textured muzzle flash petal (falls back to the soft additive sprite). */
	CAMPUSOPS_API UMaterialInstanceDynamic* MakeFlashMID(UObject* Outer, const FLinearColor& Color, float Intensity);
	/** Sound id with a random variation suffix ("Gun_AR" -> "Gun_AR", "Gun_AR_2", "Gun_AR_3") when variations exist. */
	CAMPUSOPS_API FName SoundVariation(FName Id);

	CAMPUSOPS_API USoundBase* Sound(FName Id);
	CAMPUSOPS_API USoundAttenuation* Attenuation(EAttenuation Type);
	CAMPUSOPS_API USoundConcurrency* GunshotConcurrency();

	/** Path of the generated school/menu maps. */
	extern CAMPUSOPS_API const TCHAR* MapCampus;
	extern CAMPUSOPS_API const TCHAR* MapMenu;
}

class UWorld;
class AActor;
struct FHitResult;

namespace CampusAudio
{
	/** Mix groups with their own volume in the options: weapons, impacts, voices and footsteps; weather and the map's
	 *  beds; menus and HUD cues. */
	enum class ECategory : uint8 { Effects, Ambience, Interface, Count };
	CAMPUSOPS_API void SetCategoryVolume(ECategory Category, float Volume);
	CAMPUSOPS_API float CategoryVolume(ECategory Category);

	CAMPUSOPS_API void Play2D(const UObject* WorldContext, FName Id, float Volume = 1.f, float PitchVariance = 0.f, ECategory Category = ECategory::Effects);
	CAMPUSOPS_API void PlayAt(const UObject* WorldContext, FName Id, const FVector& Location, float Volume = 1.f, float PitchVariance = 0.05f, CampusAssets::EAttenuation Attenuation = CampusAssets::EAttenuation::Impact);

	/**
	 * Probes the local listener's surroundings (open air, covered walkway, room) a few times a second: switches the
	 * world reverb to match and sets how enclosed the gun tails sound.
	 */
	CAMPUSOPS_API void UpdateEnvironment(UWorld* World, const FVector& Listener, const AActor* Ignore, float DeltaSeconds);
	/** 0 open air .. 1 closed room. */
	CAMPUSOPS_API float GetEnclosure();
	/** The space answering the local player's own shot: a room's boom and flutter, or the yard's echoes off the buildings. */
	CAMPUSOPS_API void PlayGunTail(const UObject* WorldContext, float Volume);
	/** Footstep sound for the floor under a character (concrete, dirt, metal, wood). */
	CAMPUSOPS_API FName FootstepSound(const FHitResult& Floor);
}
