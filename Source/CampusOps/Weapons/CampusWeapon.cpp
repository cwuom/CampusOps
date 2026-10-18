#include "Weapons/CampusWeapon.h"
#include "Weapons/CampusFX.h"
#include "Game/CampusEnvironment.h"
#include "Player/CampusCharacter.h"
#include "Player/CampusViewmodelComponent.h"
#include "Player/CampusFPAnimInstance.h"
#include "Player/CampusPlayerController.h"
#include "Game/CampusGameModeBase.h"
#include "Game/CampusGameState.h"
#include "Map/CampusWindowGlass.h"
#include "Map/CampusBreakableGlass.h"
#include "CampusArmory.h"
#include "CampusAssets.h"
#include "CampusOps.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AnimationRuntime.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Net/UnrealNetwork.h"

namespace
{
	ECampusHitZone ZoneFromBone(FName Bone)
	{
		const FString B = Bone.ToString();
		if (B.StartsWith(TEXT("head")) || B.StartsWith(TEXT("neck")))
		{
			return ECampusHitZone::Head;
		}
		if (B == TEXT("spine_03") || B == TEXT("spine_04") || B == TEXT("spine_05") || B.StartsWith(TEXT("clavicle")))
		{
			return ECampusHitZone::UpperTorso;
		}
		if (B == TEXT("pelvis") || B == TEXT("spine_01") || B == TEXT("spine_02"))
		{
			return ECampusHitZone::LowerTorso;
		}
		return ECampusHitZone::Limb;
	}

	constexpr float WeaponLightCandela = 6000.f;
	constexpr float WeaponLightInner = 9.f;
	constexpr float WeaponLightOuter = 30.f;
	/** None: volumetric fog is accumulated over frames, so in fog and rain a shaft swept with the view left trails. */
	constexpr float WeaponLightScatter = 0.f;
	/** Closer than this the light dims with the square of the distance: a wall up close is lit as brightly as at this range. */
	constexpr float WeaponLightNear = 300.f;
	/** Lens brightness: the owner's view, and others' (looking away .. straight down the beam). */
	constexpr float WeaponLensOwn = 14.f;
	constexpr float WeaponLensSide = 1.5f;
	constexpr float WeaponLensFacing = 9.f;

	enum class ERigMode : uint8
	{
		FirstPerson,
		ThirdPerson,
		Preview
	};

	void ConfigurePrimitive(UPrimitiveComponent* P, ERigMode Mode)
	{
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->bReceivesDecals = false;
		// A weapon is a dozen small parts that move every frame: in the distance fields they would have the global
		// field re-voxelised round them every frame, for an occlusion nobody could see at that size.
		P->bAffectDistanceFieldLighting = false;
		switch (Mode)
		{
		case ERigMode::FirstPerson:
			P->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
			P->SetOnlyOwnerSee(true);
			P->SetCastShadow(false);
			break;
		case ERigMode::ThirdPerson:
			P->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
			P->bCastHiddenShadow = true;
			break;
		default:
			break;
		}
	}

	template <typename T>
	T* NewRigComponent(AActor* Owner, USceneComponent* Parent, FName Socket, ERigMode Mode, TArray<TObjectPtr<UActorComponent>>* Track)
	{
		T* Comp = NewObject<T>(Owner);
		Comp->SetupAttachment(Parent, Socket);
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Comp))
		{
			ConfigurePrimitive(Prim, Mode);
		}
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);
		if (Track)
		{
			Track->Add(Comp);
		}
		return Comp;
	}

	struct FRigBuildResult
	{
		FVector Muzzle = FVector::ZeroVector;
	};

	/** Shared builder for gameplay rigs and menu previews. */
	FRigBuildResult BuildRigInternal(AActor* Owner, USceneComponent* AttachTo, FName Socket, const FCampusWeaponLoadout& Loadout, ERigMode Mode, FCampusWeaponRig& Rig, TArray<TObjectPtr<UMaterialInstanceDynamic>>* OutFlash, TArray<TObjectPtr<UActorComponent>>* Track, bool bPreviewInHand = false)
	{
		FRigBuildResult Result;
		const UCampusArmory& Armory = UCampusArmory::Get();
		const FCampusWeaponDef* Def = Armory.FindWeapon(Loadout.WeaponId);
		if (!Def || !AttachTo || !Owner)
		{
			return Result;
		}

		Rig.Frame = NewRigComponent<USceneComponent>(Owner, AttachTo, Socket, Mode, Track);

		const bool bModel = Def->Model.Num() > 0;
		USkeletalMesh* MeshAsset = bModel ? nullptr : Def->Mesh.LoadSynchronous();
		if (!bModel)
		{
			Rig.Mesh = NewRigComponent<USkeletalMeshComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
			Rig.Mesh->SetSkeletalMesh(MeshAsset);
			Rig.Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		}

		FVector MuzzleMesh = FVector::ZeroVector;
		bool bHasMuzzle = false;
		if (MeshAsset)
		{
			if (const USkeletalMeshSocket* MuzzleSocket = MeshAsset->FindSocket(TEXT("Muzzle")))
			{
				const FReferenceSkeleton& RefSkel = MeshAsset->GetRefSkeleton();
				const int32 BoneIndex = RefSkel.FindBoneIndex(MuzzleSocket->BoneName);
				const FTransform BoneCS = BoneIndex != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkel, BoneIndex) : FTransform::Identity;
				MuzzleMesh = (MuzzleSocket->GetSocketLocalTransform() * BoneCS).GetLocation();
				bHasMuzzle = !MuzzleMesh.IsNearlyZero();
			}
		}

		// The mannequin's hand socket holds the template weapons barrel-along-+Y; kit models are authored barrel-along-+X.
		FVector Fwd = bModel ? FVector::YAxisVector : FVector::ForwardVector;
		if (bHasMuzzle)
		{
			const FVector A = MuzzleMesh.GetAbs();
			if (A.X >= A.Y && A.X >= A.Z) Fwd = FVector(FMath::Sign(MuzzleMesh.X), 0.f, 0.f);
			else if (A.Y >= A.X && A.Y >= A.Z) Fwd = FVector(0.f, FMath::Sign(MuzzleMesh.Y), 0.f);
			else Fwd = FVector(0.f, 0.f, FMath::Sign(MuzzleMesh.Z));
		}
		const FVector Up = FMath::Abs(Fwd.Z) > 0.5f ? FVector::ForwardVector : FVector::UpVector;
		const FQuat FrameRot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		// Hand-socket rigs keep the mesh unrotated relative to the socket; previews keep the frame (barrel = +X) unrotated.
		// A knife's blade rises out of the fist along the grip: on the display it is laid down, edge under, tip forward.
		const FQuat DisplayRot = Def->IsMelee() ? FRotator(-FMath::RadiansToDegrees(FMath::Atan2(Def->Muzzle.Z, Def->Muzzle.X)), 0.f, 0.f).Quaternion() : FQuat::Identity;
		Rig.Frame->SetRelativeRotation(Mode == ERigMode::Preview && !bPreviewInHand ? DisplayRot : FrameRot);
		if (Rig.Mesh)
		{
			Rig.Mesh->SetRelativeRotation(FrameRot.Inverse());
			Rig.Mesh->SetRelativeScale3D(Def->MeshScale);
			Rig.Mesh->SetRelativeLocation(Def->MeshOffset);
		}
		if (Def->bTintMesh && MeshAsset && Rig.Mesh)
		{
			UMaterialInstanceDynamic* Tint = CampusAssets::MakeLitMID(Owner, Def->MeshTint, 0.42f, 0.55f);
			for (int32 i = 0; i < MeshAsset->GetMaterials().Num(); ++i)
			{
				Rig.Mesh->SetMaterial(i, Tint);
			}
		}

		TSet<ECampusAttachmentSlot> Fitted;
		for (const FName& Id : Loadout.Attachments)
		{
			if (const FCampusAttachmentDef* A = Armory.FindAttachment(Id))
			{
				Fitted.Add(A->Slot);
			}
		}

		const FVector MuzzleFrame = bHasMuzzle ? FrameRot.Inverse().RotateVector(MuzzleMesh * Def->MeshScale) + Def->MeshOffset : Def->Muzzle;
		// A barrel never gets shorter than a stub in front of the handguard (and not at all where none is exposed).
		const float BarrelExt = bModel ? FMath::Max(Armory.GetBarrelExtension(Loadout), FMath::Min(0.f, 1.5f - Def->ExposedBarrel)) : Armory.GetBarrelExtension(Loadout);
		const float DeviceExt = Fitted.Contains(ECampusAttachmentSlot::Muzzle) ? Armory.GetMuzzleDeviceExtension(Loadout) : Def->MuzzleDeviceLength;
		const FVector MuzzleAnchor = MuzzleFrame + FVector(BarrelExt, 0.f, 0.f);
		Result.Muzzle = MuzzleAnchor + FVector(DeviceExt, 0.f, 0.f);

		const FString WeaponFolder = Def->Id.ToString();
		auto AddModelPart = [&](const FString& Folder, const FCampusModelPart& Part, USceneComponent* Parent, const FTransform& At) -> UStaticMeshComponent*
		{
			UStaticMesh* PartMesh = CampusAssets::WeaponMesh(Folder, Part.Mesh);
			if (!PartMesh)
			{
				return nullptr;
			}
			UStaticMeshComponent* C = NewRigComponent<UStaticMeshComponent>(Owner, Parent, NAME_None, Mode, Track);
			C->SetStaticMesh(PartMesh);
			C->SetRelativeTransform(Part.Transform * At);
			Rig.Parts.Add(C);
			switch (Part.Role)
			{
			case ECampusPartRole::Magazine: Rig.Magazine = C; Rig.MagazineRest = C->GetRelativeTransform(); break;
			case ECampusPartRole::Bolt: Rig.Bolt = C; Rig.BoltRest = C->GetRelativeTransform(); break;
			case ECampusPartRole::Charging: Rig.Charging = C; Rig.ChargingRest = C->GetRelativeTransform(); break;
			case ECampusPartRole::Slide: Rig.Slide = C; Rig.SlideRest = C->GetRelativeTransform(); break;
			case ECampusPartRole::Hammer: Rig.Hammer = C; break;
			default: break;
			}
			return C;
		};
		// Exposed barrel between the handguard and the muzzle thread: one tube, stretched to the weapon.
		auto AddBarrelTube = [&](float Length)
		{
			if (Length <= 0.05f)
			{
				return;
			}
			FCampusModelPart Tube(TEXT("SM_Att_BarrelTube"));
			Tube.Transform.SetScale3D(FVector(Length / 10.f, Def->BarrelRadius, Def->BarrelRadius));
			AddModelPart(TEXT("Attachments"), Tube, Rig.Frame, FTransform(Def->GetAnchor(ECampusAttachmentSlot::Barrel)));
		};
		if (bModel)
		{
			for (const FCampusModelPart& Part : Def->Model)
			{
				if ((Part.HideWith != ECampusAttachmentSlot::Count && Fitted.Contains(Part.HideWith))
					|| (Part.ShowWith != ECampusAttachmentSlot::Count && !Fitted.Contains(Part.ShowWith)))
				{
					continue;
				}
				// The stock muzzle device rides on the (possibly swapped) barrel.
				const FTransform At = Part.HideWith == ECampusAttachmentSlot::Muzzle ? FTransform(MuzzleAnchor) : FTransform::Identity;
				AddModelPart(WeaponFolder, Part, Rig.Frame, At);
			}
			// Only where there is a barrel slot to measure from (a pistol's barrel is in its slide, a knife has none: the
			// anchor would fall back to the grip and leave a stub of tube sticking out of the hand).
			if (!Fitted.Contains(ECampusAttachmentSlot::Barrel) && Def->Anchors.Contains(ECampusAttachmentSlot::Barrel))
			{
				AddBarrelTube(Def->ExposedBarrel);
			}
		}

		auto AddPart = [&](const FCampusVisualPart& Part, const FVector& Anchor, const FQuat& MountRot = FQuat::Identity) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* C = NewRigComponent<UStaticMeshComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
			C->SetStaticMesh(CampusAssets::Shape(Part.Shape));
			UMaterialInstanceDynamic* MID = Part.Emissive > 0.f
				? CampusAssets::MakeGlowMID(Owner, Part.Color, Part.Emissive, false)
				: CampusAssets::MakeLitMID(Owner, Part.Color, Part.Roughness, Part.Metallic);
			C->SetMaterial(0, MID);
			C->SetRelativeLocation(Anchor + MountRot.RotateVector(Part.Location));
			C->SetRelativeRotation((MountRot * Part.Rotation.Quaternion()).Rotator());
			C->SetRelativeScale3D(Part.Size / 100.f);
			Rig.Parts.Add(C);
			return C;
		};

		if (!bModel)
		{
			for (const FCampusVisualPart& Part : Def->BodyParts)
			{
				AddPart(Part, FVector::ZeroVector);
			}
		}

		const FCampusAttachmentDef* Optic = nullptr;
		for (const FName& Id : Loadout.Attachments)
		{
			const FCampusAttachmentDef* A = Armory.FindAttachment(Id);
			if (!A)
			{
				continue;
			}
			const FVector Anchor = A->Slot == ECampusAttachmentSlot::Muzzle ? MuzzleAnchor : Def->GetAnchor(A->Slot);
			const FQuat MountRot = (A->bLaser || A->bFlashlight) ? Def->LaserRotation.Quaternion() : FQuat::Identity;
			// Pistol optics sit on the slide and ride its recoil.
			USceneComponent* Mount = (A->Slot == ECampusAttachmentSlot::Optic && Rig.Slide) ? static_cast<USceneComponent*>(Rig.Slide.Get()) : Rig.Frame.Get();
			const FTransform MountT = Mount == Rig.Frame.Get() ? FTransform(MountRot, Anchor) : FTransform(MountRot, Anchor) * Rig.SlideRest.Inverse();
			bool bModelled = false;
			if (bModel && A->bBarrelTube)
			{
				AddBarrelTube(Def->ExposedBarrel + BarrelExt);
				bModelled = true;
			}
			else if (bModel && A->bPerWeaponModel)
			{
				// The weapon's own variant, authored in weapon frame space (magazines, grips).
				FCampusModelPart Variant(*FString::Printf(TEXT("SM_%s_%s"), *WeaponFolder, *A->Id.ToString()), A->Slot == ECampusAttachmentSlot::Magazine ? ECampusPartRole::Magazine : ECampusPartRole::Static);
				bModelled = AddModelPart(WeaponFolder, Variant, Rig.Frame, FTransform::Identity) != nullptr;
			}
			if (!bModelled && bModel && A->Model.Num() > 0)
			{
				for (const FCampusModelPart& Part : A->Model)
				{
					UStaticMeshComponent* C = AddModelPart(TEXT("Attachments"), Part, Mount, MountT);
					if (C && A->Slot == ECampusAttachmentSlot::Optic && A->Reticle == ECampusReticle::Scope)
					{
						Rig.ScopeBody.Add(C);
					}
				}
				bModelled = true;
			}
			if (!bModelled)
			{
				for (const FCampusVisualPart& Part : A->Parts)
				{
					AddPart(Part, Anchor, MountRot);
				}
			}
			if (A->Slot == ECampusAttachmentSlot::Optic)
			{
				Optic = A;
			}
			if (A->bFlashlight)
			{
				Rig.LightOrigin = NewRigComponent<USceneComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
				Rig.LightOrigin->SetRelativeLocation(Anchor + MountRot.RotateVector(A->LaserOffset));
				Rig.LightOrigin->SetRelativeRotation(MountRot.Rotator());
				if (Mode != ERigMode::Preview)
				{
					// Lit lens: a hot disc in the bezel, shown only while the light is on.
					Rig.LightLens = NewRigComponent<UStaticMeshComponent>(Owner, Rig.LightOrigin, NAME_None, Mode, Track);
					Rig.LightLens->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cylinder));
					Rig.LightLens->SetMaterial(0, CampusAssets::MakeGlowMID(Owner, FLinearColor(1.f, 0.97f, 0.9f), WeaponLensOwn, true));
					Rig.LightLens->SetRelativeLocation(FVector(-0.12f, 0.f, 0.f));
					Rig.LightLens->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
					Rig.LightLens->SetRelativeScale3D(FVector(0.021f, 0.021f, 0.002f));
					Rig.LightLens->SetCastShadow(false);
					Rig.LightLens->SetHiddenInGame(true);
				}
			}
			if (A->bLaser)
			{
				Rig.LaserOrigin = NewRigComponent<USceneComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
				Rig.LaserOrigin->SetRelativeLocation(Anchor + MountRot.RotateVector(A->LaserOffset));
				if (Mode != ERigMode::Preview)
				{
					Rig.LaserBeam = NewRigComponent<UStaticMeshComponent>(Owner, Rig.LaserOrigin, NAME_None, Mode, Track);
					Rig.LaserBeam->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cylinder));
					Rig.LaserBeam->SetMaterial(0, CampusAssets::MakeGlowMID(Owner, FLinearColor(1.f, 0.04f, 0.02f), 40.f, true));
					Rig.LaserBeam->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
					Rig.LaserBeam->SetRelativeScale3D(FVector(0.0025f, 0.0025f, 0.1f));
					Rig.LaserBeam->SetCastShadow(false);
					Rig.LaserBeam->SetHiddenInGame(true);
				}
			}
		}

		Rig.Sight = NewRigComponent<USceneComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
		FVector SightLoc = Def->IronSight;
		if (Optic)
		{
			const FVector OpticAnchor = Def->GetAnchor(ECampusAttachmentSlot::Optic);
			SightLoc = OpticAnchor + FVector(Optic->ReticleForward, 0.f, Optic->SightHeight);
			// Glass pane facing the eye; Mark > 0 draws the parallax-free reticle on it, 0 is plain coated glass.
			USceneComponent* GlassParent = Rig.Slide ? static_cast<USceneComponent*>(Rig.Slide.Get()) : Rig.Frame.Get();
			auto MakeGlass = [&](const FVector& Loc, const FVector2D& Size, bool bRound, float Mark) -> UStaticMeshComponent*
			{
				UStaticMeshComponent* G = NewRigComponent<UStaticMeshComponent>(Owner, GlassParent, NAME_None, Mode, Track);
				G->SetStaticMesh(CampusAssets::PlaneMesh());
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(CampusAssets::ReticleMaterial(), Owner);
				MID->SetVectorParameterValue(TEXT("Color"), Optic->ReticleColor);
				MID->SetScalarParameterValue(TEXT("Ring"), Optic->Reticle == ECampusReticle::Ring ? 1.f : 0.f);
				MID->SetScalarParameterValue(TEXT("Round"), bRound ? 1.f : 0.f);
				MID->SetScalarParameterValue(TEXT("DotMOA"), Optic->Reticle == ECampusReticle::Ring ? 6.f : 9.f);
				MID->SetScalarParameterValue(TEXT("Intensity"), 16.f * Mark);
				MID->SetScalarParameterValue(TEXT("LensOpacity"), Mark > 0.f ? 0.07f : 0.08f);
				// Each optic has handled differently: its own patch of the grime map.
				const uint32 Seed = GetTypeHash(Optic->Id) ^ GetTypeHash(Loc.X);
				MID->SetVectorParameterValue(TEXT("DirtOffset"), FLinearColor((Seed % 97) / 97.f, ((Seed / 97) % 89) / 89.f, 0.f));
				G->SetMaterial(0, MID);
				G->SetRelativeLocation(Loc);
				G->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
				G->SetRelativeScale3D(FVector(Size.Y / 100.f, Size.X / 100.f, 1.f));
				G->SetCastShadow(false);
				return G;
			};
			if (Optic->Reticle == ECampusReticle::Dot || Optic->Reticle == ECampusReticle::Ring)
			{
				const bool bRound = Optic->Window.Y <= 0.f;
				Rig.Reticle = MakeGlass(SightLoc, bRound ? FVector2D(Optic->Window.X, Optic->Window.X) : Optic->Window, bRound, 1.f);
			}
			const FVector& Lens = Optic->LensFrontRearDiameter;
			if (Lens.Z > 0.f)
			{
				UStaticMeshComponent* Front = MakeGlass(OpticAnchor + FVector(Lens.X, 0.f, Optic->SightHeight), FVector2D(Lens.Z, Lens.Z), true, 0.f);
				Rig.Parts.Add(Front);
				Rig.ScopeBody.Add(Front);
				UStaticMeshComponent* Rear = MakeGlass(OpticAnchor + FVector(Lens.Y, 0.f, Optic->SightHeight), FVector2D(Lens.Z, Lens.Z), true, 0.f);
				Rig.Parts.Add(Rear);
				if (Optic->Reticle == ECampusReticle::Scope)
				{
					// Eyepiece: dark multi-coated glass seen from outside; with the eye on it, the magnified view
					// through the optic (picture in picture), the scope body and the rest of the view unchanged.
					if (UMaterialInterface* LensMat = CampusAssets::ScopeLensMaterial())
					{
						UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(LensMat, Owner);
						// The glass plane is stood up on its side: its UV runs up / across the view, the capture's across / down.
						MID->SetScalarParameterValue(TEXT("SwapUV"), 1.f);
						MID->SetScalarParameterValue(TEXT("FlipU"), 1.f);
						Rear->SetMaterial(0, MID);
						Rig.ScopeEyepiece = MID;
						Rig.ScopeEyepieceComp = Rear;
					}
				}
			}
		}
		Rig.Sight->SetRelativeLocation(SightLoc);

		Rig.Muzzle = NewRigComponent<USceneComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
		Rig.Muzzle->SetRelativeLocation(Result.Muzzle);
		Rig.Eject = NewRigComponent<USceneComponent>(Owner, Rig.Frame, NAME_None, Mode, Track);
		Rig.Eject->SetRelativeLocation(Def->EjectPort);

		if (Mode != ERigMode::Preview)
		{
			auto MakeFlash = [&](const FRotator& Rot, const FVector& Loc, const FVector& Scale) -> UStaticMeshComponent*
			{
				UStaticMeshComponent* F = NewRigComponent<UStaticMeshComponent>(Owner, Rig.Muzzle, NAME_None, Mode, Track);
				F->SetStaticMesh(CampusAssets::PlaneMesh());
				UMaterialInstanceDynamic* MID = CampusAssets::MakeFlashMID(Owner, FLinearColor(1.f, 0.62f, 0.25f), 30.f);
				F->SetMaterial(0, MID);
				if (OutFlash)
				{
					OutFlash->Add(MID);
				}
				F->SetRelativeRotation(Rot);
				F->SetRelativeLocation(Loc);
				F->SetRelativeScale3D(Scale);
				F->SetCastShadow(false);
				F->SetHiddenInGame(true);
				return F;
			};
			Rig.Flash = MakeFlash(FRotator(90.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), FVector(0.16f));
			Rig.FlashSide = MakeFlash(FRotator(0.f, 0.f, 90.f), FVector(9.f, 0.f, 0.f), FVector(0.24f, 0.09f, 1.f));
		}
		return Result;
	}
}

ACampusWeapon::ACampusWeapon()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(Root);
	MuzzleLight->SetIntensityUnits(ELightUnits::Candelas);
	MuzzleLight->SetIntensity(4000.f);
	MuzzleLight->SetAttenuationRadius(600.f);
	MuzzleLight->SetLightColor(FLinearColor(1.f, 0.7f, 0.4f));
	MuzzleLight->SetCastShadows(false);
	// No glow in the rain, fog or smoke round the shooter, and no wash over translucent rain and glass.
	MuzzleLight->SetVolumetricScatteringIntensity(0.f);
	MuzzleLight->SetAffectTranslucentLighting(false);
	MuzzleLight->SetVisibility(false);

	// Weapon light: a soft hot spot inside a wide, even spill, reaching down a corridor without burning out the night.
	WeaponLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("WeaponLight"));
	WeaponLight->SetupAttachment(Root);
	WeaponLight->SetIntensityUnits(ELightUnits::Candelas);
	WeaponLight->SetIntensity(WeaponLightCandela);
	WeaponLight->SetAttenuationRadius(5500.f);
	WeaponLight->SetInnerConeAngle(WeaponLightInner);
	WeaponLight->SetOuterConeAngle(WeaponLightOuter);
	WeaponLight->SetSourceRadius(1.1f);
	WeaponLight->SetSoftSourceRadius(1.1f);
	WeaponLight->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
	WeaponLight->SetCastShadows(true);
	// A light that sweeps with the view: MegaLights' temporal accumulation and Lumen's surface cache (which relights
	// over several frames) both leave a trail of the beam behind it, so it is shaded directly and has no bounce.
	WeaponLight->bAllowMegaLights = false;
	WeaponLight->SetIndirectLightingIntensity(0.f);
	// Only a faint shaft in fog; rain and glass are not lit (the translucency volume is coarse and lit the air in
	// front of the lens as a glowing blob, even aimed at open sky).
	WeaponLight->SetVolumetricScatteringIntensity(WeaponLightScatter);
	WeaponLight->bCastVolumetricShadow = false;
	WeaponLight->SetAffectTranslucentLighting(false);
	WeaponLight->SetVisibility(false);
}

void ACampusWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusWeapon, Loadout);
	DOREPLIFETIME_CONDITION(ACampusWeapon, AmmoInMag, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACampusWeapon, AmmoReserve, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACampusWeapon, bLightOn, COND_SkipOwner);
}

const FCampusWeaponDef* ACampusWeapon::GetDef() const
{
	return UCampusArmory::Get().FindWeapon(Loadout.WeaponId);
}

FText ACampusWeapon::GetDisplayName() const
{
	const FCampusWeaponDef* Def = GetDef();
	return Def ? Def->DisplayName : FText::GetEmpty();
}

void ACampusWeapon::ApplyStats()
{
	Stats = UCampusArmory::Get().ComputeStats(Loadout);
	if (const ACampusCharacter* C = Cast<ACampusCharacter>(GetOwner()))
	{
		if (C->HasPerk(ECampusPerk::FastHands))
		{
			Stats.ReloadTime *= 0.75f;
			Stats.EmptyReloadTime *= 0.75f;
			Stats.EquipTime *= 0.7f;
		}
		if (C->HasPerk(ECampusPerk::Dexterity))
		{
			Stats.SprintToFireTime *= 0.7f;
			Stats.TacSprintToFireTime *= 0.7f;
			Stats.AirSpread *= 0.5f;
		}
		if (C->HasPerk(ECampusPerk::Bandolier))
		{
			Stats.ReserveMags += 1;
		}
	}
	if (const FCampusWeaponDef* Def = GetDef())
	{
		if (Def->FireModes.Num() > 0 && !Def->FireModes.Contains(FireMode))
		{
			FireMode = Def->FireModes[0];
		}
		else if (Def->FireModes.Num() > 0 && !bVisualsBuilt)
		{
			FireMode = Def->FireModes[0];
		}
	}
}

void ACampusWeapon::InitializeWeapon(const FCampusWeaponLoadout& InLoadout)
{
	Loadout = InLoadout;
	bLightOn = false;
	ApplyStats();
	AmmoInMag = Stats.MagSize;
	AmmoReserve = Stats.MagSize * Stats.ReserveMags;
}

void ACampusWeapon::RefreshStats()
{
	ApplyStats();
	AmmoInMag = FMath::Min(AmmoInMag, Stats.MagSize);
}

void ACampusWeapon::AddReserveMagazines(int32 Count)
{
	if (HasAuthority() && Count > 0)
	{
		AmmoReserve = FMath::Min(AmmoReserve + Stats.MagSize * Count, Stats.MagSize * (Stats.ReserveMags + 2));
		ClientSyncAmmo(AmmoInMag, AmmoReserve);
	}
}

int32 ACampusWeapon::TakeFromReserve(int32 Wanted)
{
	const int32 Taken = FMath::Clamp(Wanted, 0, AmmoReserve);
	if (!ACampusGameState::GetRules(GetWorld()).bInfiniteAmmo)
	{
		AmmoReserve -= Taken;
	}
	return Taken;
}

bool ACampusWeapon::ConsumesAmmo() const
{
	return !ACampusGameState::GetRules(GetWorld()).bNoReload;
}

void ACampusWeapon::OnRep_Loadout()
{
	ApplyStats();
	if (!HasAuthority() && AmmoInMag == 0 && AmmoReserve == 0)
	{
		AmmoInMag = Stats.MagSize;
		AmmoReserve = Stats.MagSize * Stats.ReserveMags;
	}
	RebuildVisuals();
}

void ACampusWeapon::BeginPlay()
{
	Super::BeginPlay();
	if (ACampusCharacter* C = Cast<ACampusCharacter>(GetOwner()))
	{
		AttachToCharacter(C);
	}
}

void ACampusWeapon::OnRep_Owner()
{
	Super::OnRep_Owner();
	if (ACampusCharacter* C = Cast<ACampusCharacter>(GetOwner()))
	{
		AttachToCharacter(C);
	}
}

void ACampusWeapon::Destroyed()
{
	DestroyRig(FPRig);
	DestroyRig(TPRig);
	if (SmokeTrail)
	{
		// Whatever smoke is in the air drifts off on its own.
		SmokeTrail->SetLifeSpan(2.5f);
		SmokeTrail = nullptr;
	}
	Super::Destroyed();
}

void ACampusWeapon::AttachToCharacter(ACampusCharacter* InCharacter)
{
	if (!InCharacter)
	{
		return;
	}
	if (OwnerCharacter != InCharacter)
	{
		OwnerCharacter = InCharacter;
		AttachToActor(InCharacter, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		bVisualsBuilt = false;
	}
	RebuildVisuals();
}

void ACampusWeapon::DestroyRig(FCampusWeaponRig& Rig)
{
	auto Kill = [](UActorComponent* C)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	};
	for (UStaticMeshComponent* P : Rig.Parts)
	{
		Kill(P);
	}
	Kill(Rig.Flash);
	Kill(Rig.FlashSide);
	Kill(Rig.Reticle);
	Kill(Rig.LaserBeam);
	Kill(Rig.LaserOrigin);
	Kill(Rig.LightLens);
	Kill(Rig.LightOrigin);
	Kill(Rig.Sight);
	Kill(Rig.Muzzle);
	Kill(Rig.Eject);
	Kill(Rig.Mesh);
	Kill(Rig.Frame);
	Rig = FCampusWeaponRig();
}

void ACampusWeapon::BuildRig(AActor* Owner, USceneComponent* AttachTo, FName Socket, const FCampusWeaponLoadout& InLoadout, bool bFirstPerson, FCampusWeaponRig& OutRig, TArray<TObjectPtr<UMaterialInstanceDynamic>>* OutFlashMaterials)
{
	BuildRigInternal(Owner, AttachTo, Socket, InLoadout, bFirstPerson ? ERigMode::FirstPerson : ERigMode::ThirdPerson, OutRig, OutFlashMaterials, nullptr);
}

void ACampusWeapon::RebuildVisuals()
{
	if (!OwnerCharacter || Loadout.WeaponId.IsNone() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (bVisualsBuilt && BuiltWeaponId == Loadout.WeaponId && BuiltAttachments == Loadout.Attachments)
	{
		RefreshViewerVisibility();
		return;
	}
	DestroyRig(FPRig);
	DestroyRig(TPRig);
	FlashMaterials.Reset();

	BuildRig(this, OwnerCharacter->ArmsMesh, TEXT("HandGrip_R"), Loadout, true, FPRig, &FlashMaterials);
	BuildRig(this, OwnerCharacter->GetMesh(), TEXT("HandGrip_R"), Loadout, false, TPRig, &FlashMaterials);

	bVisualsBuilt = true;
	BuiltWeaponId = Loadout.WeaponId;
	BuiltAttachments = Loadout.Attachments;
	RefreshViewerVisibility();
	if (OwnerCharacter->Viewmodel && OwnerCharacter->GetCurrentWeapon() == this)
	{
		OwnerCharacter->Viewmodel->ResetCalibration();
	}
}

USceneComponent* ACampusWeapon::BuildPreview(AActor* Owner, USceneComponent* Parent, const FCampusWeaponLoadout& InLoadout, TArray<TObjectPtr<UActorComponent>>& OutComponents, FName Socket)
{
	FCampusWeaponRig Rig;
	BuildRigInternal(Owner, Parent, Socket, InLoadout, ERigMode::Preview, Rig, nullptr, &OutComponents, !Socket.IsNone());
	return Rig.Frame;
}

FVector ACampusWeapon::GetSlotFocusPoint(const FCampusWeaponLoadout& InLoadout, ECampusAttachmentSlot Slot)
{
	const UCampusArmory& Armory = UCampusArmory::Get();
	const FCampusWeaponDef* Def = Armory.FindWeapon(InLoadout.WeaponId);
	if (!Def)
	{
		return FVector::ZeroVector;
	}
	FVector P = Def->GetAnchor(Slot);
	switch (Slot)
	{
	case ECampusAttachmentSlot::Muzzle:
		P = Def->Muzzle + FVector(Armory.GetBarrelExtension(InLoadout) + Armory.GetMuzzleDeviceExtension(InLoadout) * 0.5f, 0.f, 0.f);
		break;
	case ECampusAttachmentSlot::Optic:
		P += FVector(0.f, 0.f, 3.f);
		break;
	case ECampusAttachmentSlot::Magazine:
		P += FVector(0.f, 0.f, -6.f);
		break;
	case ECampusAttachmentSlot::Underbarrel:
		P += FVector(0.f, 0.f, -4.f);
		break;
	default:
		break;
	}
	return P;
}

// ------------------------------------------------------------------ visibility / state

void ACampusWeapon::RefreshViewerVisibility()
{
	const bool bLocal = OwnerCharacter && OwnerCharacter->IsLocalHuman();
	const bool bAlive = OwnerCharacter && OwnerCharacter->IsAlive();
	if (FPRig.Frame)
	{
		FPRig.Frame->SetVisibility(bEquipped && bLocal && bAlive && !bFirstPersonHidden, true);
	}
	if (TPRig.Frame)
	{
		TPRig.Frame->SetVisibility(bEquipped, true);
	}
	// The lights ride on the rigs but are switched by themselves: shown with the rig above, a muzzle light attached at
	// the last shot would stay lit on a dead body. The weapon light is put back by UpdateFlashlight if it is on.
	MuzzleLight->SetVisibility(FlashHideTime > 0.f && bEquipped && bAlive);
	WeaponLight->SetVisibility(false);
}

void ACampusWeapon::SetFirstPersonHidden(bool bInHidden)
{
	bFirstPersonHidden = bInHidden;
	RefreshViewerVisibility();
}

void ACampusWeapon::SetEquipped(bool bInEquipped)
{
	bEquipped = bInEquipped;
	if (!bEquipped)
	{
		bReloading = false;
		BurstRemaining = 0;
		bSemiLatched = false;
	}
	RefreshViewerVisibility();
}

void ACampusWeapon::OnEquipped()
{
	ShotsInSpray = 0;
	bSemiLatched = false;
	BurstRemaining = 0;
	if (OwnerCharacter && OwnerCharacter->IsLocalHuman())
	{
		CampusAudio::Play2D(this, TEXT("Weapon_Equip"), 0.7f, 0.05f);
	}
}

void ACampusWeapon::OnHolster()
{
	CancelReload();
	BurstRemaining = 0;
	bSemiLatched = false;
}

void ACampusWeapon::OnOwnerDied()
{
	bReloading = false;
	BurstRemaining = 0;
	if (FPRig.LaserBeam) FPRig.LaserBeam->SetHiddenInGame(true);
	if (TPRig.LaserBeam) TPRig.LaserBeam->SetHiddenInGame(true);
	RefreshViewerVisibility();
}

bool ACampusWeapon::HasScopeOptic() const
{
	const FCampusAttachmentDef* Optic = UCampusArmory::Get().GetOptic(Loadout);
	return Optic && Optic->Reticle == ECampusReticle::Scope;
}

bool ACampusWeapon::HasOptic() const
{
	return UCampusArmory::Get().GetOptic(Loadout) != nullptr;
}

bool ACampusWeapon::GetScopeEyepiece(FVector& OutCenter, float& OutRadius) const
{
	const FCampusAttachmentDef* Optic = UCampusArmory::Get().GetOptic(Loadout);
	const FCampusWeaponDef* Def = GetDef();
	const USceneComponent* Parent = FPRig.Slide ? static_cast<const USceneComponent*>(FPRig.Slide.Get()) : FPRig.Frame.Get();
	if (!Optic || Optic->Reticle != ECampusReticle::Scope || !Def || !Parent || Optic->LensFrontRearDiameter.Z <= 0.f)
	{
		return false;
	}
	const FVector Local = Def->GetAnchor(ECampusAttachmentSlot::Optic) + FVector(Optic->LensFrontRearDiameter.Y, 0.f, Optic->SightHeight);
	OutCenter = Parent->GetComponentTransform().TransformPosition(Local);
	OutRadius = Optic->LensFrontRearDiameter.Z * 0.5f * Parent->GetComponentScale().X;
	return true;
}

void ACampusWeapon::SetReloadMagazine(const FVector& Offset, const FRotator& Rotation, const FVector& Pivot, bool bVisible)
{
	if (UStaticMeshComponent* Mag = FPRig.Magazine)
	{
		// Rotation is about Pivot (frame space), e.g. the front lug the magazine rocks on.
		const FQuat Rot = FPRig.MagazineRest.GetRotation() * Rotation.Quaternion();
		const FVector Location = FPRig.MagazineRest.GetLocation() + Offset + Pivot - Rotation.RotateVector(Pivot);
		if (!Mag->GetRelativeLocation().Equals(Location, 0.01f) || !Mag->GetRelativeRotation().Quaternion().Equals(Rot, 1.e-4f))
		{
			Mag->SetRelativeLocationAndRotation(Location, Rot);
		}
		// Never on its own: while the first-person weapon is hidden (deploy drop, dead) the magazine stays hidden too.
		const bool bShow = bVisible && FPRig.Frame && FPRig.Frame->IsVisible();
		if (Mag->IsVisible() != bShow)
		{
			Mag->SetVisibility(bShow);
		}
	}
}

bool ACampusWeapon::GetRackHandle(FVector& OutPoint, float& OutSide) const
{
	const UStaticMeshComponent* Part = FPRig.Charging ? FPRig.Charging.Get() : FPRig.Bolt.Get();
	const UStaticMesh* Mesh = Part ? Part->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return false;
	}
	const FBox Box = Mesh->GetBoundingBox().TransformBy(FPRig.Charging ? FPRig.ChargingRest : FPRig.BoltRest);
	// The handle is whatever sticks furthest out to the side.
	OutSide = FMath::Abs(Box.Max.Y) >= FMath::Abs(Box.Min.Y) ? 1.f : -1.f;
	OutPoint = FVector(Box.Max.X - 0.8f, OutSide > 0.f ? Box.Max.Y : Box.Min.Y, Box.GetCenter().Z);
	return true;
}

bool ACampusWeapon::GetMagazineBox(FBox& OutBox) const
{
	const UStaticMeshComponent* Mag = FPRig.Magazine;
	const UStaticMesh* Mesh = Mag ? Mag->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return false;
	}
	OutBox = Mesh->GetBoundingBox().TransformBy(FPRig.MagazineRest);
	return true;
}

void ACampusWeapon::SetScopeBodyHidden(bool bHide)
{
	for (UStaticMeshComponent* C : FPRig.ScopeBody)
	{
		if (C && C->IsVisible() == bHide)
		{
			C->SetVisibility(!bHide);
		}
	}
}

void ACampusWeapon::SetScopeEyepieceView(float Live, UTexture* View, float Gain, const FVector2D& PupilShadow)
{
	if (UMaterialInstanceDynamic* MID = FPRig.ScopeEyepiece)
	{
		MID->SetScalarParameterValue(TEXT("Live"), Live);
		MID->SetScalarParameterValue(TEXT("Gain"), Gain);
		MID->SetVectorParameterValue(TEXT("Shadow"), FLinearColor(PupilShadow.X, PupilShadow.Y, 0.f));
		if (View)
		{
			MID->SetTextureParameterValue(TEXT("View"), View);
		}
	}
}

UMaterialInstanceDynamic* ACampusWeapon::GetScopeEyepieceMaterial() const
{
	return FPRig.ScopeEyepiece;
}

const USceneComponent* ACampusWeapon::GetScopeEyepieceComponent() const
{
	return FPRig.ScopeEyepieceComp;
}

float ACampusWeapon::GetScopeEyepieceOffset() const
{
	const FCampusAttachmentDef* Optic = UCampusArmory::Get().GetOptic(Loadout);
	return (Optic && Optic->Reticle == ECampusReticle::Scope) ? Optic->ReticleForward - Optic->LensFrontRearDiameter.Y : 0.f;
}

bool ACampusWeapon::HasLaser() const
{
	return UCampusArmory::Get().HasLaser(Loadout);
}

bool ACampusWeapon::HasFlashlight() const
{
	const UCampusArmory& Armory = UCampusArmory::Get();
	const FCampusAttachmentDef* A = Armory.FindAttachment(Armory.GetAttachmentInSlot(Loadout, ECampusAttachmentSlot::Laser));
	return A && A->bFlashlight;
}

void ACampusWeapon::ToggleFlashlight()
{
	if (!HasFlashlight() || !OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}
	bLightOn = !bLightOn;
	CampusAudio::Play2D(this, TEXT("FireMode_Switch"), 0.5f, 0.06f);
	if (!HasAuthority())
	{
		ServerSetFlashlight(bLightOn);
	}
}

void ACampusWeapon::ServerSetFlashlight_Implementation(bool bOn)
{
	bLightOn = bOn && HasFlashlight();
}

void ACampusWeapon::UpdateFlashlight(float DeltaSeconds)
{
	const bool bLocal = OwnerCharacter && OwnerCharacter->IsLocalHuman() && !bFirstPersonHidden;
	FCampusWeaponRig& Rig = bLocal ? FPRig : TPRig;
	FCampusWeaponRig& Other = bLocal ? TPRig : FPRig;
	const bool bOn = IsFlashlightOn() && bEquipped && OwnerCharacter && OwnerCharacter->IsAlive() && Rig.LightOrigin;
	if (Other.LightLens) Other.LightLens->SetHiddenInGame(true);
	if (Rig.LightLens) Rig.LightLens->SetHiddenInGame(!bOn);
	if (bOn && WeaponLight->GetAttachParent() != Rig.LightOrigin)
	{
		WeaponLight->AttachToComponent(Rig.LightOrigin, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		LightPullBack = 0.f;
		LightScale = 1.f;
	}
	if (WeaponLight->IsVisible() != bOn)
	{
		WeaponLight->SetVisibility(bOn);
	}
	if (!bOn)
	{
		return;
	}

	const FVector Lens = Rig.LightOrigin->GetComponentLocation();
	const FVector Fwd = Rig.LightOrigin->GetForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusWeaponLight), false, OwnerCharacter);
	Params.AddIgnoredActor(this);
	FHitResult Hit;

	// Muzzle pushed into a wall: the beam starts on the near side of it, instead of lighting the room beyond.
	float PullBack = 0.f;
	if (GetWorld()->LineTraceSingleByChannel(Hit, OwnerCharacter->GetPawnViewLocation(), Lens, ECC_Visibility, Params))
	{
		PullBack = FVector::Dist(Hit.ImpactPoint, Lens) + 6.f;
	}
	LightPullBack = PullBack >= LightPullBack ? PullBack : FMath::FInterpTo(LightPullBack, PullBack, DeltaSeconds, 10.f);
	WeaponLight->SetRelativeLocation(FVector(-LightPullBack, 0.f, 0.f));

	const FVector Start = Lens - Fwd * LightPullBack;
	const float Dist = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Fwd * WeaponLightNear, ECC_Visibility, Params) ? Hit.Distance : WeaponLightNear;
	LightScale = FMath::FInterpTo(LightScale, FMath::Clamp(FMath::Square(Dist / WeaponLightNear), 0.1f, 1.f), DeltaSeconds, 12.f);
	WeaponLight->SetIntensity(WeaponLightCandela * LightScale);

	// Someone else's light seen from in front: a hot point that brightens the more squarely it is aimed at this
	// view, capped, with only a faint shaft in the air between, so it never hides the person holding it.
	float Glare = 0.f;
	if (!bLocal)
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			const FVector ToView = (PC->PlayerCameraManager->GetCameraLocation() - Lens).GetSafeNormal();
			Glare = FMath::SmoothStep(FMath::Cos(FMath::DegreesToRadians(WeaponLightOuter)), FMath::Cos(FMath::DegreesToRadians(WeaponLightInner)), (float)FVector::DotProduct(Fwd, ToView));
		}
	}
	WeaponLight->SetVolumetricScatteringIntensity(WeaponLightScatter * FMath::Lerp(1.f, 0.3f, Glare));
	if (UMaterialInstanceDynamic* LensMID = Rig.LightLens ? Cast<UMaterialInstanceDynamic>(Rig.LightLens->GetMaterial(0)) : nullptr)
	{
		const float LensIntensity = bLocal ? WeaponLensOwn : FMath::Lerp(WeaponLensSide, WeaponLensFacing, Glare);
		LensMID->SetScalarParameterValue(TEXT("Intensity"), LensIntensity * FMath::Max(LightScale, 0.4f));
	}
}

const FCampusAttachmentDef* ACampusWeapon::GetForegrip() const
{
	for (const FName& Id : Loadout.Attachments)
	{
		const FCampusAttachmentDef* A = UCampusArmory::Get().FindAttachment(Id);
		if (A && A->bForegrip)
		{
			return A;
		}
	}
	return nullptr;
}

bool ACampusWeapon::IsLocallyControlledShooter() const
{
	return OwnerCharacter && OwnerCharacter->IsLocalHuman();
}

bool ACampusWeapon::IsBotShooter() const
{
	return HasAuthority() && OwnerCharacter && OwnerCharacter->GetController() && !OwnerCharacter->IsPlayerControlled();
}

float ACampusWeapon::GetReloadProgress() const
{
	if (!bReloading || ReloadDuration <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(float(GetWorld()->GetTimeSeconds() - ReloadStart) / ReloadDuration, 0.f, 1.f);
}

float ACampusWeapon::GetCurrentSpread() const
{
	if (!OwnerCharacter)
	{
		return Stats.HipSpread;
	}
	const float ADS = OwnerCharacter->GetADSAlpha();
	const float Speed01 = FMath::Clamp(OwnerCharacter->GetVelocity().Size2D() / 470.f, 0.f, 1.f);
	const bool bAir = OwnerCharacter->GetCharacterMovement() && OwnerCharacter->GetCharacterMovement()->IsFalling();
	float Hip = Stats.HipSpread + Stats.MoveSpread * Speed01 + (bAir ? Stats.AirSpread : 0.f) + Bloom;
	if (OwnerCharacter->bIsCrouched)
	{
		Hip *= 0.85f;
	}
	if (OwnerCharacter->IsSliding())
	{
		Hip *= 1.2f;
	}
	// First-round accuracy: a tapped shot from a standing or slowly walking player goes where the crosshair is; the
	// cone opens with the string (bloom) and with pace. Players only, the bots' aim error is their own.
	if (ShotsInSpray <= 1 && !bAir && Bloom < 0.05f && !IsBotShooter())
	{
		Hip *= FMath::Lerp(0.3f, 1.f, FMath::Clamp((Speed01 - 0.2f) / 0.5f, 0.f, 1.f));
	}
	const float AimSpread = Stats.ADSSpread + Bloom * 0.08f + (bAir ? 1.5f : 0.f);
	return FMath::Lerp(Hip, AimSpread, ADS);
}

// ------------------------------------------------------------------ tick

void ACampusWeapon::TickBarrelSmoke(float DeltaSeconds)
{
	if (BarrelHeat <= 0.f)
	{
		if (SmokeTrail && SmokeTrail->IsIdle() && !bEquipped)
		{
			SmokeTrail->Destroy();
			SmokeTrail = nullptr;
		}
		return;
	}
	// A rifle barrel stops smoking about five seconds after a long string.
	BarrelHeat = FMath::Max(0.f, BarrelHeat - DeltaSeconds * 0.2f);
	if (!SmokeTrail || !bEquipped || bFirstPersonHidden || !FPRig.Muzzle || !OwnerCharacter || BarrelHeat < 0.06f)
	{
		return;
	}
	// The wisp only shows between shots: while firing the muzzle blast blows it away.
	if (GetWorld()->GetTimeSeconds() - LastFireTime < 0.14)
	{
		WispTimer = 0.f;
		return;
	}
	WispTimer -= DeltaSeconds;
	if (WispTimer > 0.f)
	{
		return;
	}
	WispTimer = 0.03f;
	const float Heat = FMath::Clamp((BarrelHeat - 0.06f) / 0.94f, 0.f, 1.f);
	const FVector Muzzle = OwnerCharacter->FirstPersonToWorldVisual(FPRig.Muzzle->GetComponentLocation());
	const FVector Fwd = FPRig.Muzzle->GetComponentQuat().GetForwardVector();
	// Thin while the eye is on the sights, so it never hides the target. A wisp a few centimetres from the eye fills a
	// lot of the view, so it stays faint and narrow. In rain the drops knock it down and the wind tears it away fast.
	const float Aim = OwnerCharacter->GetADSAlpha();
	const ACampusEnvironment* Env = ACampusEnvironment::Find(GetWorld());
	const float Rain = Env ? FMath::Clamp(Env->GetRainAmount(), 0.f, 1.f) : 0.f;
	const float Opacity = (0.03f + 0.11f * Heat) * FMath::Lerp(1.f, 0.4f, Aim) * (Stats.bSuppressed ? 1.3f : 1.f) * FMath::Lerp(1.f, 0.45f, Rain);
	const FVector Wind = FVector(18.f, 10.f, -6.f) * Rain;
	SmokeTrail->Emit(Muzzle + Fwd * 1.5f, FVector(0.f, 0.f, 16.f + 14.f * Heat) + Fwd * 4.f + FMath::VRand() * 3.f + Wind + OwnerCharacter->GetVelocity() * 0.85f,
		0.7f, 4.f + 4.f * Heat, (1.f + 0.8f * Heat) * FMath::Lerp(1.f, 0.55f, Rain), Opacity);
}

void ACampusWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();

	if (FlashHideTime > 0.f && Now >= FlashHideTime)
	{
		FlashHideTime = 0.f;
		for (FCampusWeaponRig* Rig : { &FPRig, &TPRig })
		{
			if (Rig->Flash) Rig->Flash->SetHiddenInGame(true);
			if (Rig->FlashSide) Rig->FlashSide->SetHiddenInGame(true);
		}
		MuzzleLight->SetVisibility(false);
	}

	TickBarrelSmoke(DeltaSeconds);
	UpdateFlashlight(DeltaSeconds);

	if (!OwnerCharacter || !bEquipped)
	{
		return;
	}

	Bloom = FMath::Max(0.f, Bloom - Stats.BloomRecovery * DeltaSeconds);

	const bool bShooter = IsLocallyControlledShooter() || IsBotShooter();
	if (bShooter || HasAuthority())
	{
		TickReload();
	}

	const FCampusWeaponDef* Def = GetDef();
	if (bShooter && OwnerCharacter->IsAlive() && Def && Def->IsMelee())
	{
		// A blade is swung, not fired: holding the trigger keeps cutting at the swing's own pace.
		if (OwnerCharacter->WantsToFire() && OwnerCharacter->CanFireNow())
		{
			OwnerCharacter->MeleeStrike();
		}
	}
	else if (bShooter && OwnerCharacter->IsAlive())
	{
		const bool bWantsFire = OwnerCharacter->WantsToFire() && OwnerCharacter->CanFireNow() && !bReloading;
		if (bWantsFire)
		{
			switch (FireMode)
			{
			case ECampusFireMode::Auto:
				if (CanFire())
				{
					FireOnce();
				}
				break;
			case ECampusFireMode::Semi:
				if (!bSemiLatched && CanFire())
				{
					FireOnce();
					bSemiLatched = true;
				}
				break;
			case ECampusFireMode::Burst:
				if (BurstRemaining <= 0 && !bSemiLatched)
				{
					BurstRemaining = Stats.BurstCount;
					bSemiLatched = true;
				}
				break;
			}
		}
		if (FireMode == ECampusFireMode::Burst && BurstRemaining > 0 && !bReloading)
		{
			if (AmmoInMag <= 0)
			{
				BurstRemaining = 0;
			}
			else if (Now - LastFireTime >= Stats.GetShotInterval() * 0.8f && OwnerCharacter->CanFireNow())
			{
				FireOnce();
				--BurstRemaining;
			}
		}

		if (AmmoInMag <= 0 && !bReloading && AmmoReserve > 0 && Now - LastFireTime > 0.25f)
		{
			RequestReload();
		}
	}

	UpdateLaser(DeltaSeconds);
	AnimateParts(DeltaSeconds);
}

void ACampusWeapon::AnimateParts(float DeltaSeconds)
{
	const FCampusWeaponDef* Def = GetDef();
	if (!Def || (!FPRig.Bolt && !FPRig.Slide && !FPRig.Charging && !TPRig.Bolt && !TPRig.Slide))
	{
		return;
	}
	BoltKick = FMath::Max(0.f, BoltKick - DeltaSeconds / FMath::Clamp(Stats.GetShotInterval() * 0.85f, 0.04f, 0.12f));
	// Snaps back hard, returns a touch slower.
	float Back = BoltKick > 0.f ? FMath::Sin(PI * FMath::Pow(1.f - BoltKick, 0.55f)) : 0.f;
	// Slide (and bolt-hold-open) locks back on the last round until the reload releases it.
	const float T = GetReloadProgress();
	const bool bEmptyHold = AmmoInMag <= 0 && (!bReloading || T < 0.84f);
	if (Def->bPistolAnims && bEmptyHold)
	{
		Back = 1.f;
	}
	// Empty reloads end with the charging handle / bolt racked back and released.
	float Rack = 0.f;
	if (bReloading && bEmptyReload)
	{
		Rack = FMath::SmoothStep(0.80f, 0.86f, T) * (1.f - FMath::SmoothStep(0.88f, 0.91f, T));
	}
	const float Travel = Def->BoltTravel * Back;
	const float RackTravel = 6.5f * Rack;
	for (FCampusWeaponRig* Rig : { &FPRig, &TPRig })
	{
		if (Rig->Bolt)
		{
			Rig->Bolt->SetRelativeLocation(Rig->BoltRest.GetLocation() - FVector(FMath::Max(Travel, Rig->Charging ? 0.f : RackTravel), 0.f, 0.f));
		}
		if (Rig->Slide)
		{
			Rig->Slide->SetRelativeLocation(Rig->SlideRest.GetLocation() - FVector(FMath::Max(Travel, Def->BoltTravel * Rack), 0.f, 0.f));
		}
		if (Rig->Charging)
		{
			Rig->Charging->SetRelativeLocation(Rig->ChargingRest.GetLocation() - FVector(RackTravel, 0.f, 0.f));
		}
	}
}

bool ACampusWeapon::CanFire() const
{
	const FCampusWeaponDef* Def = GetDef();
	if (bReloading || AmmoInMag <= 0 || (Def && Def->IsMelee()))
	{
		return false;
	}
	return GetWorld()->GetTimeSeconds() - LastFireTime >= Stats.GetShotInterval();
}

void ACampusWeapon::OnTriggerPressed()
{
	// Pulling the trigger interrupts a reload whenever there is something to shoot: either the old magazine
	// still has rounds, or the new one is already seated (the bolt / settle part is skipped).
	if (bReloading && (AmmoInMag > 0 || bReloadInserted))
	{
		CancelReload();
		return;
	}
	if (AmmoInMag <= 0 && !bReloading)
	{
		if (IsLocallyControlledShooter())
		{
			CampusAudio::Play2D(this, TEXT("Gun_DryFire"), 0.7f, 0.05f);
		}
		RequestReload();
	}
}

void ACampusWeapon::OnTriggerReleased()
{
	bSemiLatched = false;
	ShotsInSpray = 0;
}

void ACampusWeapon::CycleFireMode()
{
	const FCampusWeaponDef* Def = GetDef();
	if (!Def || Def->FireModes.Num() < 2)
	{
		return;
	}
	const int32 Index = Def->FireModes.IndexOfByKey(FireMode);
	FireMode = Def->FireModes[(Index + 1) % Def->FireModes.Num()];
	BurstRemaining = 0;
	if (IsLocallyControlledShooter())
	{
		CampusAudio::Play2D(this, TEXT("FireMode_Switch"), 0.6f, 0.03f);
	}
	if (!HasAuthority())
	{
		ServerSetFireMode(FireMode);
	}
}

void ACampusWeapon::ServerSetFireMode_Implementation(ECampusFireMode Mode)
{
	const FCampusWeaponDef* Def = GetDef();
	if (Def && Def->FireModes.Contains(Mode))
	{
		FireMode = Mode;
	}
}

// ------------------------------------------------------------------ firing

void ACampusWeapon::FireOnce()
{
	ACampusCharacter* C = OwnerCharacter;
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const float Interval = Stats.GetShotInterval();
	LastFireTime = (Now - LastFireTime < Interval * 2.0) ? FMath::Min(LastFireTime + Interval, Now) : Now;

	if (ConsumesAmmo())
	{
		AmmoInMag = FMath::Max(0, AmmoInMag - 1);
	}
	const bool bFirst = ShotsInSpray == 0;
	++ShotsInSpray;
	BoltKick = 1.f;

	const FVector Origin = C->GetAimOrigin();
	const FVector AimDir = C->GetAimDirection();
	const float SpreadRad = FMath::DegreesToRadians(GetCurrentSpread()) * FMath::Pow(FMath::FRand(), 1.6f);
	FVector Perp = FVector::CrossProduct(AimDir, FVector::UpVector).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		Perp = FVector::RightVector;
	}
	Perp = Perp.RotateAngleAxisRad(FMath::FRand() * 2.f * PI, AimDir);
	const FVector Dir = AimDir.RotateAngleAxisRad(SpreadRad, Perp).GetSafeNormal();
	Bloom = FMath::Min(Bloom + Stats.SpreadPerShot, Stats.MaxBloom);

	FCampusShot Shot;
	if (Stats.Pellets > 1)
	{
		// The charge leaves as a cone around the aimed line: each pellet traced on its own, reported together.
		Shot.Origin = Origin;
		Shot.Direction = Dir;
		Shot.End = Origin + Dir * 25000.f;
		const float Cone = FMath::DegreesToRadians(Stats.PelletSpread) * FMath::Lerp(1.f, 0.8f, C->GetADSAlpha());
		for (int32 p = 0; p < Stats.Pellets; ++p)
		{
			// Even coverage of the cone: one pellet down the middle, the rest on a jittered sunflower spiral.
			const float R = p == 0 ? 0.f : FMath::Sqrt((p + FMath::FRandRange(-0.3f, 0.3f)) / (float)Stats.Pellets) * Cone;
			const float A = p * 2.39996f + FMath::FRandRange(-0.25f, 0.25f);
			const FVector Axis = Perp.RotateAngleAxisRad(A, Dir);
			FCampusShot Pellet;
			TraceShot(Origin, Dir.RotateAngleAxisRad(R, Axis).GetSafeNormal(), Pellet);
			Shot.Hits.Append(Pellet.Hits);
			Shot.PelletEnds.Add(Pellet.End);
		}
	}
	else
	{
		TraceShot(Origin, Dir, Shot);
	}

	const float ADS = C->GetADSAlpha();
	const float RecoilScale = (bFirst ? Stats.FirstShotRecoilScale : 1.f) * FMath::Lerp(1.f, Stats.ADSRecoilScale, ADS) * (C->bIsCrouched ? 0.85f : 1.f);
	const float Pitch = Stats.RecoilPitch * RecoilScale * FMath::FRandRange(0.85f, 1.15f);
	const float Yaw = (FMath::FRandRange(-Stats.RecoilYaw, Stats.RecoilYaw) + Stats.RecoilYawBias) * RecoilScale;
	C->AddAimRecoil(Pitch, Yaw);
	C->NotifyShotFired(Stats.bSuppressed);

	if (C->IsLocalHuman())
	{
		PlayLocalShotFX(Shot);
		if (C->Viewmodel)
		{
			C->Viewmodel->OnWeaponFired(Stats, ADS, bFirst);
		}
	}

	if (HasAuthority())
	{
		LastServerFireTime = Now;
		ProcessShotAuthority(Shot);
		MulticastShotFX(Shot);
		if (ACampusGameModeBase* GM = World->GetAuthGameMode<ACampusGameModeBase>())
		{
			GM->NotifyGunshot(C, Origin, Stats.bSuppressed);
		}
	}
	else
	{
		ServerFire(Shot);
	}
}

void ACampusWeapon::TraceShot(const FVector& Origin, const FVector& Direction, FCampusShot& Out) const
{
	Out.Origin = Origin;
	Out.Direction = Direction;
	Out.Hits.Reset();

	const float MaxRange = 25000.f;
	FVector Start = Origin;
	const FVector FinalEnd = Origin + Direction * MaxRange;
	Out.End = FinalEnd;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusShot), false);
	Params.AddIgnoredActor(this);
	if (OwnerCharacter)
	{
		Params.AddIgnoredActor(OwnerCharacter);
	}

	float Budget = Stats.Penetration;
	uint8 Penetrations = 0;
	for (int32 Iteration = 0; Iteration < 6; ++Iteration)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, FinalEnd, CAMPUS_TRACE_WEAPON, Params))
		{
			break;
		}
		FCampusShotHit& H = Out.Hits.AddDefaulted_GetRef();
		H.Actor = Hit.GetActor();
		H.Location = Hit.ImpactPoint;
		H.Normal = Hit.ImpactNormal;
		H.Bone = Hit.BoneName;
		H.Surface = (uint8)CampusFX::ResolveSurface(Hit);
		H.Penetrations = Penetrations;
		Out.End = Hit.ImpactPoint;
		if (UCampusBreakableGlass::IsGlass(Hit.GetComponent()))
		{
			// Glass barely slows a round: it holes and cracks the pane and goes on through.
			Params.AddIgnoredComponent(Hit.GetComponent());
			Start = Hit.ImpactPoint + Direction * 1.f;
			Out.End = FinalEnd;
			continue;
		}

		// Water soaks a round up within a hand's breadth: it ends at the splash.
		if (Cast<ACampusCharacter>(Hit.GetActor()) || Budget <= 1.f || !Hit.GetComponent() || (ECampusSurface)H.Surface == ECampusSurface::Water)
		{
			break;
		}

		const FVector ProbeStart = Hit.ImpactPoint + Direction * Budget;
		FHitResult Exit;
		FCollisionQueryParams ExitParams(SCENE_QUERY_STAT(CampusShotExit), false);
		if (!Hit.GetComponent()->LineTraceComponent(Exit, ProbeStart, Hit.ImpactPoint + Direction * 0.5f, ExitParams))
		{
			break;
		}
		const float Thickness = FVector::Dist(Hit.ImpactPoint, Exit.ImpactPoint);
		if (Thickness > Budget)
		{
			break;
		}
		Budget -= Thickness;
		++Penetrations;
		Start = Exit.ImpactPoint + Direction * 1.f;
		Out.End = FinalEnd;
	}
}

void ACampusWeapon::ProcessShotAuthority(const FCampusShot& Shot)
{
	ACampusCharacter* Shooter = OwnerCharacter;
	if (!Shooter)
	{
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		// No impact effects run here, but the server's panes must break too so later rounds pass.
		for (const FCampusShotHit& Hit : Shot.Hits)
		{
			if ((ECampusSurface)Hit.Surface == ECampusSurface::Glass)
			{
				ACampusWindowGlass::BreakAt(GetWorld(), Hit.Location, Shot.Direction);
			}
		}
	}
	// A round counts on its first victim; a shotgun's pellets add up on everyone they hit, dealt as one wound each.
	struct FPelletWound
	{
		ACampusCharacter* Victim;
		float Damage;
		ECampusHitZone Zone;
		FName Bone;
	};
	TArray<FPelletWound, TInlineAllocator<4>> Wounds;
	const bool bPellets = Stats.Pellets > 1;
	for (const FCampusShotHit& Hit : Shot.Hits)
	{
		ACampusCharacter* Victim = Cast<ACampusCharacter>(Hit.Actor);
		if (!Victim || Victim == Shooter || !Victim->IsAlive())
		{
			continue;
		}
		if (FVector::Dist(FVector(Hit.Location), Victim->GetActorLocation()) > 260.f)
		{
			continue;
		}
		const float Distance = FVector::Dist(FVector(Shot.Origin), FVector(Hit.Location));
		const ECampusHitZone Zone = ZoneFromBone(Hit.Bone);
		float Damage = Stats.GetDamageAtDistance(Distance);
		switch (Zone)
		{
		case ECampusHitZone::Head: Damage *= Stats.HeadMultiplier; break;
		case ECampusHitZone::LowerTorso: Damage *= Stats.LowerTorsoMultiplier; break;
		case ECampusHitZone::Limb: Damage *= Stats.LimbMultiplier; break;
		default: break;
		}
		Damage *= FMath::Pow(0.65f, (float)Hit.Penetrations);
		if (!bPellets)
		{
			Victim->ApplyCampusDamage(Damage, Zone, Shooter->GetController(), Shooter, Loadout.WeaponId, Shot.Direction, Hit.Bone);
			break;
		}
		FPelletWound* W = Wounds.FindByPredicate([Victim](const FPelletWound& X) { return X.Victim == Victim; });
		if (!W)
		{
			W = &Wounds.Add_GetRef({ Victim, 0.f, Zone, Hit.Bone });
		}
		W->Damage += Damage;
		if (Zone == ECampusHitZone::Head)
		{
			W->Zone = Zone;
			W->Bone = Hit.Bone;
		}
	}
	for (const FPelletWound& W : Wounds)
	{
		W.Victim->ApplyCampusDamage(W.Damage, W.Zone, Shooter->GetController(), Shooter, Loadout.WeaponId, Shot.Direction, W.Bone);
	}
}

void ACampusWeapon::ServerFire_Implementation(const FCampusShot& Shot)
{
	ACampusCharacter* C = OwnerCharacter;
	if (!C || !C->IsAlive() || !bEquipped)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastServerFireTime < Stats.GetShotInterval() * 0.6f)
	{
		return;
	}
	if (bReloading)
	{
		if (GetReloadProgress() > 0.8f)
		{
			if (!bReloadInserted)
			{
				AmmoInMag += TakeFromReserve(Stats.MagSize - AmmoInMag);
			}
			bReloading = false;
		}
		else
		{
			return;
		}
	}
	if (AmmoInMag <= 0)
	{
		return;
	}
	if (FVector::DistSquared(FVector(Shot.Origin), C->GetPawnViewLocation()) > FMath::Square(250.f))
	{
		return;
	}
	LastServerFireTime = Now;
	if (ConsumesAmmo())
	{
		--AmmoInMag;
	}
	C->NotifyShotFired(Stats.bSuppressed);
	ProcessShotAuthority(Shot);
	MulticastShotFX(Shot);
	if (ACampusGameModeBase* GM = GetWorld()->GetAuthGameMode<ACampusGameModeBase>())
	{
		GM->NotifyGunshot(C, Shot.Origin, Stats.bSuppressed);
	}
}

void ACampusWeapon::MulticastShotFX_Implementation(const FCampusShot& Shot)
{
	if (!OwnerCharacter || OwnerCharacter->IsLocalHuman())
	{
		return;
	}
	if (!HasAuthority())
	{
		OwnerCharacter->NotifyShotFired(Stats.bSuppressed);
	}
	BoltKick = 1.f;
	PlayRemoteShotFX(Shot);
}

void ACampusWeapon::ShowMuzzleFlash(bool bFirstPerson)
{
	FCampusWeaponRig& Rig = bFirstPerson ? FPRig : TPRig;
	const float Scale = Stats.bSuppressed ? 0.35f : FMath::FRandRange(0.85f, 1.25f);
	if (Rig.Flash)
	{
		Rig.Flash->SetHiddenInGame(false);
		Rig.Flash->SetRelativeRotation(FRotator(90.f, 0.f, FMath::FRandRange(0.f, 360.f)));
		Rig.Flash->SetRelativeScale3D(FVector(0.2f * Scale));
	}
	if (Rig.FlashSide)
	{
		Rig.FlashSide->SetHiddenInGame(Stats.bSuppressed);
		Rig.FlashSide->SetRelativeRotation(FRotator(0.f, 0.f, FMath::FRandRange(60.f, 120.f)));
		Rig.FlashSide->SetRelativeScale3D(FVector(0.3f * Scale, 0.11f * Scale, 1.f));
	}
	// Seen through the sights the flash is a short orange flicker round the muzzle, not a white bloom over the target.
	for (UMaterialInstanceDynamic* MID : FlashMaterials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Intensity"), FMath::FRandRange(1.2f, 2.2f));
		}
	}
	if (Rig.Muzzle && !Stats.bSuppressed)
	{
		// A real flash lasts about a millisecond; held here over a couple of frames, it has to be far weaker than its
		// peak or every burst floods a dark room or a wet night yard orange. A glint on the weapon and the nearest wall.
		MuzzleLight->AttachToComponent(Rig.Muzzle, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		MuzzleLight->SetIntensity(FMath::FRandRange(380.f, 700.f));
		MuzzleLight->SetAttenuationRadius(320.f);
		MuzzleLight->SetVisibility(true);
	}
	// Held over a full frame at 30-60 fps so every shot reads.
	FlashHideTime = GetWorld()->GetTimeSeconds() + 0.045f;
}

void ACampusWeapon::PlayImpactFX(const FCampusShot& Shot) const
{
	for (int32 i = 0; i < Shot.Hits.Num(); ++i)
	{
		const FCampusShotHit& Hit = Shot.Hits[i];
		// A shotgun's pellets land together: a few impact sounds carry the whole pattern.
		CampusFX::SpawnImpact(GetWorld(), Hit.Location, Hit.Normal, (ECampusSurface)Hit.Surface, Stats.Pellets <= 1 || i % 3 == 0);
		if ((ECampusSurface)Hit.Surface == ECampusSurface::Glass)
		{
			ACampusWindowGlass::BreakAt(GetWorld(), Hit.Location, Shot.Direction);
		}
	}
}

void ACampusWeapon::PlayLocalShotFX(const FCampusShot& Shot)
{
	UWorld* World = GetWorld();
	const FCampusWeaponDef* Def = GetDef();
	ShowMuzzleFlash(true);

	const FName Sound = Stats.bSuppressed ? FName(TEXT("Gun_Suppressed")) : (Def ? Def->FireSound : FName(TEXT("Gun_AR")));
	CampusAudio::Play2D(this, Sound, 0.85f, 0.035f);
	CampusAudio::Play2D(this, TEXT("Gun_Mech"), 0.35f, 0.06f);
	CampusAudio::PlayGunTail(this, Stats.bSuppressed ? 0.25f : 0.6f);

	if (FPRig.Muzzle)
	{
		const FVector MuzzleWorld = OwnerCharacter->FirstPersonToWorldVisual(FPRig.Muzzle->GetComponentLocation());
		if (Shot.PelletEnds.Num() > 0)
		{
			for (int32 p = 0; p < Shot.PelletEnds.Num(); p += 2)
			{
				CampusFX::SpawnTracer(World, MuzzleWorld, Shot.PelletEnds[p], FLinearColor(1.f, 0.62f, 0.3f), 26000.f);
			}
		}
		else if (Stats.RPM < 600.f || (ShotsInSpray % 2) == 1)
		{
			CampusFX::SpawnTracer(World, MuzzleWorld, Shot.End);
		}
		// Each round leaves a little more heat in the barrel (a full magazine of rifle fire gets it smoking hard), and
		// throws a short burst of propellant smoke out of the muzzle.
		const float HeatPerShot = (Def && Def->bPistolAnims ? 0.07f : 0.045f) * (Stats.bSuppressed ? 1.4f : 1.f);
		BarrelHeat = FMath::Min(BarrelHeat + HeatPerShot, 1.f);
		if (!SmokeTrail)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SmokeTrail = World->SpawnActor<ACampusSmokeTrail>(ACampusSmokeTrail::StaticClass(), FTransform(MuzzleWorld), Params);
		}
		if (SmokeTrail)
		{
			const FVector Fwd = FVector(Shot.Direction).GetSafeNormal();
			const float Puff = Stats.bSuppressed ? 1.5f : 1.f;
			const ACampusEnvironment* Env = ACampusEnvironment::Find(World);
			const float Rain = Env ? FMath::Clamp(Env->GetRainAmount(), 0.f, 1.f) : 0.f;
			// Propellant gas thrown out ahead of the muzzle, thinning to nothing within half a second (sooner in rain).
			for (int32 i = 0; i < 3; ++i)
			{
				SmokeTrail->Emit(MuzzleWorld + Fwd * FMath::FRandRange(6.f, 16.f), Fwd * FMath::FRandRange(90.f, 190.f) + FMath::VRand() * 18.f + OwnerCharacter->GetVelocity() * 0.6f,
					2.f, 11.f * Puff, 0.42f * FMath::Lerp(1.f, 0.7f, Rain), (0.04f + 0.06f * BarrelHeat) * Puff * FMath::Lerp(1.f, 0.6f, Rain));
			}
		}
	}
	PlayImpactFX(Shot);

	if (FPRig.Eject)
	{
		FTransform Eject = FPRig.Eject->GetComponentTransform();
		Eject.SetLocation(OwnerCharacter->FirstPersonToWorldVisual(Eject.GetLocation()));
		CampusFX::SpawnShell(World, Eject, OwnerCharacter->GetVelocity(), Def && Def->bPistolAnims);
	}

	if (ACampusPlayerController* PC = Cast<ACampusPlayerController>(OwnerCharacter->GetController()))
	{
		// One marker per trigger pull: a shotgun's pellets on target are summed into it.
		float Total = 0.f;
		bool bHead = false;
		for (const FCampusShotHit& Hit : Shot.Hits)
		{
			const ACampusCharacter* Victim = Cast<ACampusCharacter>(Hit.Actor);
			if (Victim && Victim->IsAlive() && OwnerCharacter->IsEnemyOf(Victim))
			{
				const ECampusHitZone Zone = ZoneFromBone(Hit.Bone);
				float Damage = Stats.GetDamageAtDistance(FVector::Dist(FVector(Shot.Origin), FVector(Hit.Location)));
				switch (Zone)
				{
				case ECampusHitZone::Head: Damage *= Stats.HeadMultiplier; break;
				case ECampusHitZone::LowerTorso: Damage *= Stats.LowerTorsoMultiplier; break;
				case ECampusHitZone::Limb: Damage *= Stats.LimbMultiplier; break;
				default: break;
				}
				Total += Damage;
				bHead |= Zone == ECampusHitZone::Head;
				if (Stats.Pellets <= 1)
				{
					break;
				}
			}
		}
		if (Total > 0.f)
		{
			PC->ShowLocalHitmarker(bHead, Total);
		}
	}
}

void ACampusWeapon::PlayRemoteShotFX(const FCampusShot& Shot)
{
	UWorld* World = GetWorld();
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FCampusWeaponDef* Def = GetDef();
	ShowMuzzleFlash(false);
	const FVector Muzzle = TPRig.Muzzle ? TPRig.Muzzle->GetComponentLocation() : GetActorLocation();
	const FName Sound = Stats.bSuppressed ? FName(TEXT("Gun_Suppressed")) : (Def ? Def->FireSound : FName(TEXT("Gun_AR")));
	CampusAudio::PlayAt(this, Sound, Muzzle, 1.f, 0.04f, CampusAssets::EAttenuation::Gunshot);
	if (Shot.PelletEnds.Num() > 0)
	{
		for (int32 p = 0; p < Shot.PelletEnds.Num(); p += 3)
		{
			CampusFX::SpawnTracer(World, Muzzle, Shot.PelletEnds[p], FLinearColor(1.f, 0.62f, 0.3f), 26000.f);
		}
	}
	else
	{
		CampusFX::SpawnTracer(World, Muzzle, Shot.End);
	}
	CampusFX::SpawnMuzzleSmoke(World, Muzzle, FVector(Shot.Direction), Stats.bSuppressed ? 1.4f : 0.8f);
	PlayImpactFX(Shot);
	if (OwnerCharacter)
	{
		OwnerCharacter->PlayBodyFire();
	}

	if (APlayerController* LocalPC = World->GetFirstPlayerController())
	{
		if (const APawn* Viewer = LocalPC->GetPawn())
		{
			if (Viewer != OwnerCharacter)
			{
				const FVector Ear = Viewer->GetPawnViewLocation();
				if (TPRig.Eject && FVector::DistSquared(Ear, Muzzle) < FMath::Square(1500.f))
				{
					CampusFX::SpawnShell(World, TPRig.Eject->GetComponentTransform(), OwnerCharacter->GetVelocity(), Def && Def->bPistolAnims);
				}
				const FVector Closest = FMath::ClosestPointOnSegment(Ear, Muzzle, FVector(Shot.End));
				const float D = FVector::Dist(Ear, Closest);
				if (D < 240.f && FVector::Dist(Closest, FVector(Shot.End)) > 120.f && FVector::Dist(Closest, Muzzle) > 300.f)
				{
					CampusAudio::PlayAt(this, TEXT("Bullet_Whiz"), Closest, 0.9f, 0.15f, CampusAssets::EAttenuation::Impact);
				}
			}
		}
	}
}

void ACampusWeapon::UpdateLaser(float DeltaSeconds)
{
	if (!OwnerCharacter || (!FPRig.LaserBeam && !TPRig.LaserBeam))
	{
		return;
	}
	const bool bLocal = OwnerCharacter->IsLocalHuman();
	const bool bAiming = OwnerCharacter->IsAlive() && (bLocal ? OwnerCharacter->GetADSAlpha() > 0.6f : OwnerCharacter->bRepAiming);

	auto UpdateRig = [this](FCampusWeaponRig& Rig, bool bShow)
	{
		if (!Rig.LaserBeam || !Rig.LaserOrigin)
		{
			return;
		}
		Rig.LaserBeam->SetHiddenInGame(!bShow);
		if (!bShow)
		{
			return;
		}
		const FVector Start = Rig.LaserOrigin->GetComponentLocation();
		const FVector Dir = Rig.LaserOrigin->GetForwardVector();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusLaser), false, OwnerCharacter);
		float Length = 3000.f;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * Length, ECC_Visibility, Params))
		{
			Length = Hit.Distance;
		}
		Rig.LaserBeam->SetRelativeLocation(FVector(Length * 0.5f, 0.f, 0.f));
		Rig.LaserBeam->SetRelativeScale3D(FVector(0.0025f, 0.0025f, Length / 100.f));
	};
	UpdateRig(FPRig, bLocal && bAiming && bEquipped);
	UpdateRig(TPRig, !bLocal && bAiming && bEquipped);
}

// ------------------------------------------------------------------ reload

void ACampusWeapon::RequestReload()
{
	const FCampusWeaponDef* Def = GetDef();
	if (bReloading || !OwnerCharacter || !OwnerCharacter->IsAlive() || OwnerCharacter->IsThrowing() || AmmoInMag >= Stats.MagSize || AmmoReserve <= 0 || (Def && Def->IsMelee()))
	{
		return;
	}
	StartReloadInternal();
	if (!HasAuthority())
	{
		ServerStartReload();
	}
	else
	{
		MulticastReloadFX(bEmptyReload);
	}
}

void ACampusWeapon::StartReloadInternal()
{
	bReloading = true;
	bReloadInserted = false;
	ReloadTaken = 0;
	bReloadMagOutPlayed = false;
	bReloadBoltPlayed = false;
	bEmptyReload = AmmoInMag <= 0;
	ReloadDuration = bEmptyReload ? Stats.EmptyReloadTime : Stats.ReloadTime;
	ReloadStart = GetWorld()->GetTimeSeconds();
	BurstRemaining = 0;

	if (OwnerCharacter && OwnerCharacter->IsLocalHuman())
	{
		// First person is fully procedural (the viewmodel moves the weapon, the support hand and the magazine);
		// the mannequin's third-person reload clip does not fit the first-person arms.
		if (OwnerCharacter->Viewmodel)
		{
			OwnerCharacter->Viewmodel->OnReloadStart(ReloadDuration, bEmptyReload);
		}
		CampusAudio::Play2D(this, TEXT("Reload_Start"), 0.5f, 0.05f);
	}
}

void ACampusWeapon::TickReload()
{
	if (!bReloading)
	{
		return;
	}
	const float T = GetReloadProgress();
	const bool bLocal = OwnerCharacter && OwnerCharacter->IsLocalHuman();

	if (!bReloadMagOutPlayed && T > 0.16f)
	{
		bReloadMagOutPlayed = true;
		if (bLocal)
		{
			CampusAudio::Play2D(this, TEXT("Reload_MagOut"), 0.75f, 0.04f);
		}
	}
	if (!bReloadInserted && T >= Stats.ReloadInsertFraction)
	{
		bReloadInserted = true;
		const int32 Taken = TakeFromReserve(Stats.MagSize - AmmoInMag);
		ReloadTaken = Taken;
		AmmoInMag += Taken;
		if (bLocal)
		{
			CampusAudio::Play2D(this, TEXT("Reload_MagIn"), 0.85f, 0.04f);
			if (OwnerCharacter->Viewmodel)
			{
				OwnerCharacter->Viewmodel->OnReloadInsert();
			}
		}
	}
	if (bEmptyReload && !bReloadBoltPlayed && T > 0.85f)
	{
		bReloadBoltPlayed = true;
		if (bLocal)
		{
			CampusAudio::Play2D(this, TEXT("Reload_Bolt"), 0.85f, 0.04f);
			if (OwnerCharacter->Viewmodel)
			{
				OwnerCharacter->Viewmodel->OnReloadBolt();
			}
		}
	}
	if (T >= 1.f)
	{
		bReloading = false;
		if (bLocal && OwnerCharacter->Viewmodel)
		{
			OwnerCharacter->Viewmodel->OnReloadEnd();
		}
		if (HasAuthority() && OwnerCharacter && OwnerCharacter->IsPlayerControlled() && !OwnerCharacter->IsLocallyControlled())
		{
			ClientSyncAmmo(AmmoInMag, AmmoReserve);
		}
	}
}

void ACampusWeapon::CancelReload()
{
	if (!bReloading)
	{
		return;
	}
	bReloading = false;
	const bool bInserted = bReloadInserted;
	if (OwnerCharacter && OwnerCharacter->IsLocalHuman())
	{
		if (UCampusFPAnimInstance* Anim = OwnerCharacter->GetArmsAnim())
		{
			Anim->StopActions(0.12f);
		}
		if (OwnerCharacter->Viewmodel)
		{
			OwnerCharacter->Viewmodel->OnReloadCancel(bInserted);
		}
		if (!bInserted)
		{
			CampusAudio::Play2D(this, TEXT("Reload_MagIn"), 0.4f, 0.06f);
		}
	}
	if (!HasAuthority())
	{
		ServerCancelReload(bInserted);
	}
}

void ACampusWeapon::ServerStartReload_Implementation()
{
	if (!bReloading && OwnerCharacter && OwnerCharacter->IsAlive() && AmmoInMag < Stats.MagSize && AmmoReserve > 0)
	{
		StartReloadInternal();
		MulticastReloadFX(bEmptyReload);
	}
}

void ACampusWeapon::ServerCancelReload_Implementation(bool bInserted)
{
	if (!bReloading)
	{
		return;
	}
	// Keep the server's magazine in step with what the shooter saw when cancelling.
	if (bInserted && !bReloadInserted)
	{
		AmmoInMag += TakeFromReserve(Stats.MagSize - AmmoInMag);
	}
	else if (!bInserted && bReloadInserted)
	{
		AmmoInMag -= ReloadTaken;
		if (!ACampusGameState::GetRules(GetWorld()).bInfiniteAmmo)
		{
			AmmoReserve += ReloadTaken;
		}
	}
	bReloading = false;
}

void ACampusWeapon::MulticastReloadFX_Implementation(bool bEmpty)
{
	if (OwnerCharacter && !OwnerCharacter->IsLocalHuman() && GetNetMode() != NM_DedicatedServer)
	{
		CampusAudio::PlayAt(this, TEXT("Reload_MagOut"), OwnerCharacter->GetActorLocation(), 0.6f, 0.05f, CampusAssets::EAttenuation::Foley);
		OwnerCharacter->PlayBodyReload(bEmpty ? Stats.EmptyReloadTime : Stats.ReloadTime);
	}
}

void ACampusWeapon::ClientSyncAmmo_Implementation(int32 Mag, int32 Reserve)
{
	if (!bReloading)
	{
		AmmoInMag = Mag;
		AmmoReserve = Reserve;
	}
}
