#include "CampusAssets.h"
#include "CampusOps.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimInstance.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/ReverbEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Weapons/CampusFX.h"
#include "UObject/Package.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace CampusAssets
{
	const TCHAR* MapCampus = TEXT("/Game/Campus/Maps/Map_Campus");
	const TCHAR* MapMenu = TEXT("/Game/Campus/Maps/Map_MainMenu");

	static TMap<FString, TWeakObjectPtr<UObject>>& Cache()
	{
		static TMap<FString, TWeakObjectPtr<UObject>> Map;
		return Map;
	}

	static TSet<FString>& MissingPaths()
	{
		static TSet<FString> Set;
		return Set;
	}

	UObject* LoadPath(const TCHAR* Path)
	{
		const FString Key(Path);
		if (TWeakObjectPtr<UObject>* Found = Cache().Find(Key))
		{
			if (UObject* Obj = Found->Get())
			{
				return Obj;
			}
		}
		if (MissingPaths().Contains(Key))
		{
			return nullptr;
		}

		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Obj)
		{
			Cache().Add(Key, Obj);
		}
		else
		{
			MissingPaths().Add(Key);
			UE_LOG(LogCampus, Verbose, TEXT("Missing asset %s"), Path);
		}
		return Obj;
	}

	UStaticMesh* Shape(ECampusShape InShape)
	{
		switch (InShape)
		{
		case ECampusShape::Cylinder: return Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		case ECampusShape::Sphere: return Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		case ECampusShape::Cone: return Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone.Cone"));
		default: return Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
		}
	}

	UStaticMesh* WeaponMesh(const FString& Folder, const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		if (Name.StartsWith(TEXT("/")))
		{
			return Load<UStaticMesh>(*Name);
		}
		const FString Path = FString::Printf(TEXT("/Game/Campus/Weapons/%s/%s.%s"), *Folder, *Name, *Name);
		return Load<UStaticMesh>(*Path);
	}

	UStaticMesh* PlaneMesh()
	{
		return Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane"));
	}

	UStaticMesh* TerrainMesh()
	{
		return Load<UStaticMesh>(TEXT("/Game/Campus/Meshes/SM_Campus_Terrain.SM_Campus_Terrain"));
	}

	USkeletalMesh* OperatorMesh(int32 Index)
	{
		return (Index % 2 == 0)
			? Load<USkeletalMesh>(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"))
			: Load<USkeletalMesh>(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	}

	USkeletalMesh* RifleMesh()
	{
		return Load<USkeletalMesh>(TEXT("/Game/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle"));
	}

	USkeletalMesh* PistolMesh()
	{
		return Load<USkeletalMesh>(TEXT("/Game/Weapons/Pistol/Meshes/SKM_Pistol.SKM_Pistol"));
	}

	UClass* ThirdPersonAnimClass(bool bPistol)
	{
		return bPistol
			? Load<UClass>(TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C"))
			: Load<UClass>(TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Rifle.ABP_TP_Rifle_C"));
	}

	UAnimSequenceBase* Anim(EAnim InAnim)
	{
		switch (InAnim)
		{
		case EAnim::RifleIdle: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));
		case EAnim::RifleReload: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload"));
		case EAnim::RifleEquip: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Equip.MM_Rifle_Equip"));
		case EAnim::RifleFire: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire.MM_Rifle_Fire"));
		case EAnim::PistolIdle: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS"));
		case EAnim::PistolReload: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Reload.MM_Pistol_Reload"));
		case EAnim::PistolEquip: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Equip.MM_Pistol_Equip"));
		case EAnim::PistolFire: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Fire.MM_Pistol_Fire"));
		case EAnim::HitFrontLight1: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_01.MM_HitReact_Front_Lgt_01"));
		case EAnim::HitFrontLight2: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_02.MM_HitReact_Front_Lgt_02"));
		case EAnim::HitFrontLight3: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_03.MM_HitReact_Front_Lgt_03"));
		case EAnim::HitFrontLight4: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_04.MM_HitReact_Front_Lgt_04"));
		case EAnim::HitFrontMedium1: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_01.MM_HitReact_Front_Med_01"));
		case EAnim::HitFrontMedium2: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_02.MM_HitReact_Front_Med_02"));
		case EAnim::HitFrontHeavy: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Hvy_01.MM_HitReact_Front_Hvy_01"));
		case EAnim::HitBack: return Load<UAnimSequenceBase>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Back_Med_01.MM_HitReact_Back_Med_01"));
		default: return nullptr;
		}
	}

	static UMaterialInterface* BasicShapeMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	UMaterialInterface* LitMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Lit.M_Campus_Lit"));
		return M ? M : BasicShapeMaterial();
	}

	UMaterialInterface* InstancedMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Instanced.M_Campus_Instanced"));
		return M ? M : BasicShapeMaterial();
	}

	UMaterialInterface* SoldierMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Campus/Materials/M_Campus_Soldier.M_Campus_Soldier"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UMaterialInterface* SurfaceMaterial(ECampusMat Mat)
	{
		static TWeakObjectPtr<UMaterialInterface> Cache[CampusSurfaces::Num];
		TWeakObjectPtr<UMaterialInterface>& Slot = Cache[FMath::Clamp((int32)Mat, 0, CampusSurfaces::Num - 1)];
		if (!Slot.IsValid())
		{
			Slot = LoadObject<UMaterialInterface>(nullptr, *CampusSurfaces::MaterialPath(Mat), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		return Slot.IsValid() ? Slot.Get() : InstancedMaterial();
	}

	UMaterialInterface* AdditiveMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Additive.M_Campus_Additive"));
		return M ? M : BasicShapeMaterial();
	}

	UMaterialInterface* UnlitMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Unlit.M_Campus_Unlit"));
		return M ? M : BasicShapeMaterial();
	}

	UMaterialInterface* DecalMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Decal.M_Campus_Decal"));
	}

	UMaterialInterface* BloodDecalMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_BloodDecal.M_Campus_BloodDecal"));
	}

	UMaterialInterface* ReticleMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Reticle.M_Campus_Reticle"));
		return M ? M : AdditiveMaterial();
	}

	UMaterialInterface* ScopeLensMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_ScopeLens.M_Campus_ScopeLens"));
	}

	UMaterialInterface* ScopeViewMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_ScopeView.M_Campus_ScopeView"));
	}
	void AttachSoldierGear(USkeletalMeshComponent* Body, TArray<TObjectPtr<UStaticMeshComponent>>& InOutParts)
	{
		USkeletalMesh* Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
		if (!Mesh || InOutParts.Num() > 0)
		{
			return;
		}
		// The pieces are authored in the male mannequin's reference-pose space: undo that bone's reference transform
		// and they ride the bone exactly where they were modelled. On the slighter female frame each piece keeps its
		// place on the bone, taken in a little about it.
		struct FPiece
		{
			const TCHAR* Name;
			const TCHAR* Bone;
			float Refit;
		};
		static const FPiece Pieces[] = {
			{ TEXT("SM_Gear_Carrier"), TEXT("spine_04"), 0.9f }, { TEXT("SM_Gear_Belt"), TEXT("pelvis"), 0.88f }, { TEXT("SM_Gear_Helmet"), TEXT("head"), 0.95f },
			{ TEXT("SM_Gear_KneePad_L"), TEXT("calf_l"), 0.92f }, { TEXT("SM_Gear_KneePad_R"), TEXT("calf_r"), 0.92f } };
		USkeletalMesh* Authored = OperatorMesh(0);
		const bool bRefit = Authored && Authored != Mesh;
		const FReferenceSkeleton& Ref = (Authored ? Authored : Mesh)->GetRefSkeleton();
		for (const FPiece& Piece : Pieces)
		{
			UStaticMesh* GearMesh = WeaponMesh(TEXT("Gear"), Piece.Name);
			const int32 Bone = Ref.FindBoneIndex(Piece.Bone);
			if (!GearMesh || Bone == INDEX_NONE || Mesh->GetRefSkeleton().FindBoneIndex(Piece.Bone) == INDEX_NONE)
			{
				continue;
			}
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Body->GetOwner() ? static_cast<UObject*>(Body->GetOwner()) : Body);
			C->SetStaticMesh(GearMesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCanEverAffectNavigation(false);
			// Moves with the body every frame: kept out of the distance fields (see the weapon rigs).
			C->bAffectDistanceFieldLighting = false;
			C->SetFirstPersonPrimitiveType(Body->FirstPersonPrimitiveType);
			C->bCastHiddenShadow = Body->bCastHiddenShadow;
			C->SetupAttachment(Body, Piece.Bone);
			FTransform OnBone = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Bone).Inverse();
			if (bRefit)
			{
				OnBone = OnBone * FTransform(FQuat::Identity, FVector::ZeroVector, FVector(Piece.Refit));
			}
			C->SetRelativeTransform(OnBone);
			C->RegisterComponent();
			InOutParts.Add(C);
		}
	}

	void ColorSoldierGear(const TArray<TObjectPtr<UStaticMeshComponent>>& Parts, int32 Team)
	{
		// Slot13 = GearTan, Slot15 = GearShell (weapon kit slot indices); the black kit swaps both.
		auto Kit = [](const TCHAR* Name) { return Load<UMaterialInterface>(*FString::Printf(TEXT("/Game/Campus/Weapons/Materials/%s.%s"), Name, Name)); };
		UMaterialInterface* Fabric = Kit(Team == 1 ? TEXT("MI_Weapon_GearDark") : TEXT("MI_Weapon_GearTan"));
		UMaterialInterface* Shell = Kit(Team == 1 ? TEXT("MI_Weapon_Matte") : TEXT("MI_Weapon_GearShell"));
		for (UStaticMeshComponent* C : Parts)
		{
			if (!C)
			{
				continue;
			}
			const int32 FabricIndex = C->GetMaterialIndex(TEXT("Slot13"));
			const int32 ShellIndex = C->GetMaterialIndex(TEXT("Slot15"));
			if (FabricIndex != INDEX_NONE && Fabric)
			{
				C->SetMaterial(FabricIndex, Fabric);
			}
			if (ShellIndex != INDEX_NONE && Shell)
			{
				C->SetMaterial(ShellIndex, Shell);
			}
		}
	}

	UMaterialInterface* SmokeMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Smoke.M_Campus_Smoke"));
	}

	UMaterialInterface* RainMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Rain.M_Campus_Rain"));
	}

	UMaterialInterface* RainSheetMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_RainSheet.M_Campus_RainSheet"));
	}

	UMaterialInterface* WaterMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Water.M_Campus_Water"));
	}

	UMaterialInterface* WaterSimMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_WaterSim.M_Campus_WaterSim"));
	}

	UMaterialInterface* CausticsMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Caustics.M_Campus_Caustics"));
	}

	UMaterialInterface* RippleMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Ripple.M_Campus_Ripple"));
	}

	UMaterialParameterCollection* EnvironmentCollection()
	{
		return Load<UMaterialParameterCollection>(TEXT("/Game/Campus/Materials/MPC_Campus_Env.MPC_Campus_Env"));
	}

	UTextureRenderTarget2D* RainMaskTarget()
	{
		return Load<UTextureRenderTarget2D>(TEXT("/Game/Campus/Materials/RT_Campus_RainMask.RT_Campus_RainMask"));
	}

	UMaterialInterface* FireMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Fire.M_Campus_Fire"));
	}

	UMaterialInterface* GlassCrackMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_GlassCrack.M_Campus_GlassCrack"));
	}

	UMaterialInterface* ScorchMaterial()
	{
		return Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Scorch.M_Campus_Scorch"));
	}

	UMaterialInterface* SignMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Sign.M_Campus_Sign"));
		return M ? M : LitMaterial();
	}

	UMaterialInterface* DisplayMaterial()
	{
		UMaterialInterface* M = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Display.M_Campus_Display"));
		return M ? M : SignMaterial();
	}

	UMaterialInstanceDynamic* MakeFlashMID(UObject* Outer, const FLinearColor& Color, float Intensity)
	{
		UMaterialInterface* Parent = Load<UMaterialInterface>(TEXT("/Game/Campus/Materials/M_Campus_Flash.M_Campus_Flash"));
		if (!Parent)
		{
			return MakeGlowMID(Outer, Color, Intensity, true);
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		return MID;
	}

	FName SoundVariation(FName Id)
	{
		static TMap<FName, TArray<FName>> Variations;
		TArray<FName>* Found = Variations.Find(Id);
		if (!Found)
		{
			TArray<FName> List = { Id };
			for (int32 i = 2; i <= 4; ++i)
			{
				const FName Candidate(*FString::Printf(TEXT("%s_%d"), *Id.ToString(), i));
				if (Sound(Candidate))
				{
					List.Add(Candidate);
				}
			}
			Found = &Variations.Add(Id, MoveTemp(List));
		}
		return (*Found)[FMath::RandRange(0, Found->Num() - 1)];
	}

	UMaterialInstanceDynamic* MakeLitMID(UObject* Outer, const FLinearColor& Color, float Roughness, float Metallic, float Emissive)
	{
		UMaterialInterface* Parent = LitMaterial();
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		MID->SetScalarParameterValue(TEXT("Metallic"), Metallic);
		MID->SetScalarParameterValue(TEXT("Emissive"), Emissive);
		return MID;
	}

	UMaterialInstanceDynamic* MakeGlowMID(UObject* Outer, const FLinearColor& Color, float Intensity, bool bAdditive)
	{
		UMaterialInterface* Parent = bAdditive ? AdditiveMaterial() : UnlitMaterial();
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		return MID;
	}

	USoundBase* Sound(FName Id)
	{
		if (Id.IsNone())
		{
			return nullptr;
		}
		const FString Name = Id.ToString();
		const FString Path = FString::Printf(TEXT("/Game/Campus/Audio/SFX_%s.SFX_%s"), *Name, *Name);
		return Load<USoundBase>(*Path);
	}

	USoundAttenuation* Attenuation(EAttenuation Type)
	{
		static USoundAttenuation* Cached[3] = { nullptr, nullptr, nullptr };
		const int32 Index = (int32)Type;
		if (!Cached[Index])
		{
			USoundAttenuation* Att = NewObject<USoundAttenuation>(GetTransientPackage());
			Att->AddToRoot();
			FSoundAttenuationSettings& S = Att->Attenuation;
			S.bAttenuate = true;
			S.bSpatialize = true;
			S.AttenuationShape = EAttenuationShape::Sphere;
			S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
			S.bAttenuateWithLPF = true;
			switch (Type)
			{
			case EAttenuation::Gunshot:
				S.AttenuationShapeExtents = FVector(400.f, 0.f, 0.f);
				S.FalloffDistance = 15000.f;
				S.LPFRadiusMin = 2500.f;
				S.LPFRadiusMax = 12000.f;
				break;
			case EAttenuation::Impact:
				S.AttenuationShapeExtents = FVector(150.f, 0.f, 0.f);
				S.FalloffDistance = 4000.f;
				S.LPFRadiusMin = 1500.f;
				S.LPFRadiusMax = 4000.f;
				break;
			default:
				S.AttenuationShapeExtents = FVector(100.f, 0.f, 0.f);
				S.FalloffDistance = 2500.f;
				S.LPFRadiusMin = 1000.f;
				S.LPFRadiusMax = 2500.f;
				break;
			}
			Cached[Index] = Att;
		}
		return Cached[Index];
	}

	USoundConcurrency* GunshotConcurrency()
	{
		static USoundConcurrency* Cached = nullptr;
		if (!Cached)
		{
			Cached = NewObject<USoundConcurrency>(GetTransientPackage());
			Cached->AddToRoot();
			Cached->Concurrency.MaxCount = 24;
			Cached->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
		}
		return Cached;
	}
}

namespace CampusAudio
{
	namespace
	{
		float GCategoryVolume[(int32)ECategory::Count] = { 1.f, 1.f, 1.f };
	}

	void SetCategoryVolume(ECategory Category, float Volume)
	{
		GCategoryVolume[FMath::Clamp((int32)Category, 0, (int32)ECategory::Count - 1)] = FMath::Clamp(Volume, 0.f, 1.f);
	}

	float CategoryVolume(ECategory Category)
	{
		return GCategoryVolume[FMath::Clamp((int32)Category, 0, (int32)ECategory::Count - 1)];
	}

	void Play2D(const UObject* WorldContext, FName Id, float Volume, float PitchVariance, ECategory Category)
	{
		const float Mixed = Volume * CategoryVolume(Category);
		if (Mixed <= 0.001f)
		{
			return;
		}
		if (USoundBase* Snd = CampusAssets::Sound(CampusAssets::SoundVariation(Id)))
		{
			const float Pitch = 1.f + FMath::FRandRange(-PitchVariance, PitchVariance);
			UGameplayStatics::PlaySound2D(WorldContext, Snd, Mixed, Pitch);
		}
	}

	void PlayAt(const UObject* WorldContext, FName Id, const FVector& Location, float Volume, float PitchVariance, CampusAssets::EAttenuation Attenuation)
	{
		const float Mixed = Volume * CategoryVolume(ECategory::Effects);
		if (Mixed <= 0.001f)
		{
			return;
		}
		if (USoundBase* Snd = CampusAssets::Sound(CampusAssets::SoundVariation(Id)))
		{
			const float Pitch = 1.f + FMath::FRandRange(-PitchVariance, PitchVariance);
			USoundConcurrency* Concurrency = Attenuation == CampusAssets::EAttenuation::Gunshot ? CampusAssets::GunshotConcurrency() : nullptr;
			UGameplayStatics::PlaySoundAtLocation(WorldContext, Snd, Location, FRotator::ZeroRotator, Mixed, Pitch, 0.f, CampusAssets::Attenuation(Attenuation), Concurrency);
		}
	}

	namespace
	{
		float GEnclosure = 0.f;
		float GEnclosureTarget = 0.f;
		float GProbeTimer = 0.f;
		int32 GReverbState = -1;
		TWeakObjectPtr<UWorld> GReverbWorld;

		UReverbEffect* EnvironmentReverb(int32 State)
		{
			static UReverbEffect* Effects[3] = { nullptr, nullptr, nullptr };
			if (!Effects[State])
			{
				UReverbEffect* R = NewObject<UReverbEffect>(GetTransientPackage());
				R->AddToRoot();
				switch (State)
				{
				case 0:
					// Open yard between concrete blocks: sparse, dark, late reflections.
					R->Density = 0.3f; R->Diffusion = 0.55f; R->Gain = 0.22f; R->GainHF = 0.55f; R->DecayTime = 1.7f; R->DecayHFRatio = 0.45f;
					R->ReflectionsGain = 0.12f; R->ReflectionsDelay = 0.045f; R->LateGain = 0.75f; R->LateDelay = 0.06f;
					break;
				case 1:
					// Covered walkway: a hard ceiling and one open side.
					R->Density = 0.85f; R->Diffusion = 0.85f; R->Gain = 0.34f; R->GainHF = 0.7f; R->DecayTime = 1.25f; R->DecayHFRatio = 0.65f;
					R->ReflectionsGain = 0.7f; R->ReflectionsDelay = 0.012f; R->LateGain = 1.1f; R->LateDelay = 0.02f;
					break;
				default:
					// Classroom: concrete, tile and glass all round - short, dense and bright.
					R->Density = 1.f; R->Diffusion = 1.f; R->Gain = 0.45f; R->GainHF = 0.78f; R->DecayTime = 0.95f; R->DecayHFRatio = 0.8f;
					R->ReflectionsGain = 1.3f; R->ReflectionsDelay = 0.006f; R->LateGain = 1.35f; R->LateDelay = 0.011f;
					break;
				}
				R->AirAbsorptionGainHF = 0.994f;
				Effects[State] = R;
			}
			return Effects[State];
		}
	}

	void UpdateEnvironment(UWorld* World, const FVector& Listener, const AActor* Ignore, float DeltaSeconds)
	{
		if (!World)
		{
			return;
		}
		if (GReverbWorld.Get() != World)
		{
			GReverbWorld = World;
			GReverbState = -1;
			GProbeTimer = 0.f;
		}
		GProbeTimer -= DeltaSeconds;
		if (GProbeTimer <= 0.f)
		{
			GProbeTimer = 0.2f;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusAudioProbe), false, Ignore);
			FHitResult Hit;
			const bool bRoof = World->LineTraceSingleByChannel(Hit, Listener, Listener + FVector(0.f, 0.f, 900.f), ECC_Visibility, Params);
			float Walls = 0.f;
			for (int32 i = 0; i < 8; ++i)
			{
				const FVector Dir = FRotator(0.f, i * 45.f + 22.5f, 0.f).Vector();
				if (World->LineTraceSingleByChannel(Hit, Listener, Listener + Dir * 1400.f, ECC_Visibility, Params))
				{
					Walls += FMath::Lerp(1.f, 0.4f, Hit.Distance / 1400.f);
				}
			}
			GEnclosureTarget = (bRoof ? 0.45f : 0.f) + 0.55f * Walls / 8.f;
		}
		GEnclosure = FMath::FInterpTo(GEnclosure, GEnclosureTarget, DeltaSeconds, 5.f);
		// Hysteresis, so standing in a doorway does not flicker between spaces.
		int32 State = GReverbState < 0 ? (GEnclosure > 0.72f ? 2 : (GEnclosure > 0.42f ? 1 : 0)) : GReverbState;
		if (GReverbState >= 0)
		{
			if (GEnclosure > 0.76f) State = 2;
			else if (GEnclosure < 0.38f) State = 0;
			else if ((GReverbState == 2 && GEnclosure < 0.66f) || (GReverbState == 0 && GEnclosure > 0.46f)) State = 1;
		}
		if (State != GReverbState)
		{
			GReverbState = State;
			UGameplayStatics::ActivateReverbEffect(World, EnvironmentReverb(State), TEXT("CampusEnvironment"), 1.f, 1.f, 0.6f);
		}
	}

	float GetEnclosure()
	{
		return GEnclosure;
	}

	void PlayGunTail(const UObject* WorldContext, float Volume)
	{
		static USoundConcurrency* TailConcurrency = nullptr;
		if (!TailConcurrency)
		{
			// A sustained burst rolls one tail into the next rather than stacking a wall of them.
			TailConcurrency = NewObject<USoundConcurrency>(GetTransientPackage());
			TailConcurrency->AddToRoot();
			TailConcurrency->Concurrency.MaxCount = 3;
			TailConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
		}
		const float Indoor = FMath::Clamp((GEnclosure - 0.35f) / 0.4f, 0.f, 1.f);
		for (const TPair<FName, float>& Layer : { TPair<FName, float>(TEXT("Gun_Tail_Indoor"), Indoor), TPair<FName, float>(TEXT("Gun_Tail_Outdoor"), 1.f - Indoor) })
		{
			if (Layer.Value < 0.05f)
			{
				continue;
			}
			if (USoundBase* Snd = CampusAssets::Sound(CampusAssets::SoundVariation(Layer.Key)))
			{
				UGameplayStatics::PlaySound2D(WorldContext, Snd, Volume * Layer.Value * CategoryVolume(ECategory::Effects), 1.f + FMath::FRandRange(-0.04f, 0.04f), 0.f, TailConcurrency);
			}
		}
	}

	FName FootstepSound(const FHitResult& Floor)
	{
		switch (CampusFX::ResolveSurface(Floor))
		{
		case ECampusSurface::Dirt: return TEXT("Footstep_Dirt");
		case ECampusSurface::Metal: return TEXT("Footstep_Metal");
		case ECampusSurface::Wood: return TEXT("Footstep_Wood");
		default: return TEXT("Footstep");
		}
	}
}
