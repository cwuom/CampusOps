#include "Menu/CampusMenuStage.h"
#include "Menu/CampusMenuPresentation.h"
#include "Map/CampusSchoolBuilder.h"
#include "Weapons/CampusWeapon.h"
#include "CampusArmory.h"
#include "CampusAssets.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	// How far the framings allow for the weapon being turned before the live aperture catches up (degrees).
	constexpr float DisplaySwayYaw = 9.f;

	/**
	 * The f-stop whose depth of field spans Depth cm at Dist, on the director's 36 mm sensor with a circle of confusion
	 * of about a pixel at 1080p. A weapon close to the lens at a wide aperture has its ends just out of focus, and the
	 * blurred foreground spreads its colour over the backdrop as a halo that crawls as the display sways.
	 */
	float ApertureCovering(float Authored, float Dist, float FOV, float Depth)
	{
		const float FocalMM = 18.f / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
		const float DistMM = FMath::Max(Dist, 10.f) * 10.f;
		const float Needed = Depth * 10.f * FocalMM * FocalMM / (2.f * 0.02f * DistMM * DistMM);
		return FMath::Clamp(Needed, Authored, 22.f);
	}
}

ACampusMenuStage::ACampusMenuStage()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	OperatorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("OperatorRoot"));
	OperatorRoot->SetupAttachment(Root);
	Operator = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Operator"));
	Operator->SetupAttachment(OperatorRoot);
	Operator->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Operator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Operator->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	OperatorLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("OperatorLight"));
	OperatorLight->SetupAttachment(OperatorRoot);
	OperatorLight->SetRelativeLocationAndRotation(FVector(260.f, -160.f, 260.f), FRotator(-25.f, 150.f, 0.f));
	// Physical units: a studio key has to beat ~10 EV of corridor daylight to shape the operator.
	OperatorLight->SetIntensityUnits(ELightUnits::Candelas);
	OperatorLight->SetIntensity(12000.f);
	OperatorLight->SetAttenuationRadius(900.f);
	OperatorLight->SetOuterConeAngle(38.f);
	OperatorLight->SetLightColor(FLinearColor(1.f, 0.86f, 0.7f));

	DisplayRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DisplayRoot"));
	DisplayRoot->SetupAttachment(Root);
	DisplayTable = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayTable"));
	DisplayTable->SetupAttachment(DisplayRoot);
	DisplayTable->SetRelativeLocation(FVector(0.f, 0.f, -40.f));
	DisplayTable->SetRelativeScale3D(FVector(1.1f, 0.6f, 0.8f));
	DisplayTable->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponPivot = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponPivot"));
	WeaponPivot->SetupAttachment(DisplayRoot);
	WeaponPivot->SetRelativeLocation(FVector(0.f, 0.f, 22.f));

	KeyLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(DisplayRoot);
	KeyLight->SetRelativeLocationAndRotation(FVector(-80.f, 140.f, 170.f), FRotator(-50.f, -60.f, 0.f));
	KeyLight->SetIntensityUnits(ELightUnits::Candelas);
	KeyLight->SetIntensity(20000.f);
	KeyLight->SetAttenuationRadius(500.f);
	KeyLight->SetOuterConeAngle(30.f);
	KeyLight->SetLightColor(FLinearColor(1.f, 0.9f, 0.78f));

	RimLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(DisplayRoot);
	RimLight->SetRelativeLocationAndRotation(FVector(60.f, -130.f, 90.f), FRotator(-20.f, 115.f, 0.f));
	RimLight->SetIntensityUnits(ELightUnits::Candelas);
	RimLight->SetIntensity(15000.f);
	RimLight->SetAttenuationRadius(400.f);
	RimLight->SetOuterConeAngle(35.f);
	RimLight->SetLightColor(FLinearColor(0.65f, 0.8f, 1.f));
}

void ACampusMenuStage::SetupFromBuilder(const ACampusSchoolBuilder* Builder)
{
	DisplayTable->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
	DisplayTable->SetMaterial(0, CampusAssets::MakeLitMID(this, FLinearColor(0.05f, 0.05f, 0.055f), 0.35f, 0.2f));
	if (!Builder)
	{
		return;
	}
	const FCampusMenuAnchors& A = Builder->MenuAnchors;
	OperatorRoot->SetWorldTransform(A.Operator);
	const bool bHQ = Builder->MapKind == ECampusMap::HQ;
	// On the gunsmith's bench the weapon floats a hand's width over the top; the old classroom desk had its own table.
	DisplayRoot->SetWorldTransform(bHQ ? FTransform(A.WeaponDesk.GetRotation(), A.WeaponDesk.GetLocation() + FVector(0.f, 0.f, 14.f)) : FTransform(FRotator::ZeroRotator, FVector(-3807.f, -1640.f, 15.f + 80.f)));
	DisplayTable->SetVisibility(!bHQ);
	CourtyardFocus = A.CourtyardFocus;
	GateFocus = A.GateFocus;
	TitleCamera = A.TitleCamera;
	TitleTarget = A.TitleTarget;
	OptionsCamera = A.OptionsCamera;
	OptionsTarget = A.OptionsTarget;
	DeployCamera = A.DeployCamera;
	DeployTarget = A.DeployTarget;
	if (bHQ)
	{
		// Interior night lighting: the key and rim lights are softer than the daylight corridor needed. The eye opens
		// up to the dim room, so a key much stronger than this burns the operator's tan gear and the weapon white.
		OperatorLight->SetIntensity(600.f);
		KeyLight->SetIntensity(800.f);
		RimLight->SetIntensity(1100.f);
	}

	for (UActorComponent* C : RackComponents)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	RackComponents.Reset();
	for (USceneComponent* R : RackRoots)
	{
		if (R)
		{
			R->DestroyComponent();
		}
	}
	RackRoots.Reset();
	static const TCHAR* RackWeapons[] = { TEXT("M4A1"), TEXT("AK47"), TEXT("HK416"), TEXT("MP5"), TEXT("MK14"), TEXT("RPK") };
	for (int32 i = 0; i < A.RackSlots.Num(); ++i)
	{
		FCampusWeaponLoadout L;
		L.WeaponId = RackWeapons[i % UE_ARRAY_COUNT(RackWeapons)];
		if (!UCampusArmory::Get().FindWeapon(L.WeaponId))
		{
			continue;
		}
		USceneComponent* Slot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
		Slot->SetupAttachment(Root);
		Slot->RegisterComponent();
		Slot->SetWorldTransform(A.RackSlots[i]);
		RackRoots.Add(Slot);
		ACampusWeapon::BuildPreview(this, Slot, L, RackComponents);
	}
}

void ACampusMenuStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	// The weapon holds still on its display (any drift of its silhouette over the blurred backdrop crawls through the
	// temporal upscaler) and turns only when it is dragged: round its vertical axis, and rolled about its bore to show
	// the top or the underside, coasting a moment after the mouse lets go.
	const float A = 1.f - FMath::Exp(-14.f * DeltaSeconds);
	ViewYaw = FMath::UnwindDegrees(ViewYaw + FMath::FindDeltaAngleDegrees(ViewYaw, TargetYaw) * A);
	ViewRoll = FMath::Lerp(ViewRoll, TargetRoll, A);
	WeaponPivot->SetRelativeRotation(FRotator(0.f, ViewYaw, ViewRoll));
}

void ACampusMenuStage::TurnWeapon(float DeltaYaw, float DeltaRoll)
{
	TargetYaw = FMath::UnwindDegrees(TargetYaw + DeltaYaw);
	TargetRoll = FMath::Clamp(TargetRoll + DeltaRoll, -80.f, 80.f);
}

void ACampusMenuStage::ResetWeaponTurn()
{
	TargetYaw = 0.f;
	TargetRoll = 0.f;
}

float ACampusMenuStage::ApertureToHoldWeapon(const FVector& Eye, const FVector& ViewDir, float FOV) const
{
	// The weapon's extent along the line of sight as it stands now, from every part on it.
	float Near = MAX_flt;
	float Far = -MAX_flt;
	for (const UActorComponent* C : WeaponComponents)
	{
		if (const UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C); P && P->IsRegistered())
		{
			const FBox B = P->Bounds.GetBox();
			for (int32 i = 0; i < 8; ++i)
			{
				const FVector Corner((i & 1) ? B.Max.X : B.Min.X, (i & 2) ? B.Max.Y : B.Min.Y, (i & 4) ? B.Max.Z : B.Min.Z);
				const float D = (float)FVector::DotProduct(Corner - Eye, ViewDir);
				Near = FMath::Min(Near, D);
				Far = FMath::Max(Far, D);
			}
		}
	}
	if (Far <= Near)
	{
		return 0.f;
	}
	return ApertureCovering(0.f, (Near + Far) * 0.5f, FOV, Far - Near + 2.f);
}

void ACampusMenuStage::ShowWeapon(const FCampusWeaponLoadout& Loadout)
{
	for (UActorComponent* C : WeaponComponents)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	WeaponComponents.Reset();
	if (Loadout.WeaponId != Shown.WeaponId)
	{
		ResetWeaponTurn();
	}
	Shown = Loadout;
	WeaponFrame = ACampusWeapon::BuildPreview(this, WeaponPivot, Loadout, WeaponComponents);
	if (WeaponFrame)
	{
		FBox Bounds(ForceInit);
		for (const UActorComponent* C : WeaponComponents)
		{
			if (const UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C); P && P->IsRegistered())
			{
				Bounds += P->CalcBounds(P->GetComponentTransform().GetRelativeTransform(WeaponPivot->GetComponentTransform())).GetBox();
			}
		}
		if (Bounds.IsValid) WeaponFrame->SetRelativeLocation(-Bounds.GetCenter());
	}
}

void ACampusMenuStage::SetOperator(int32 Index, const FCampusWeaponLoadout& Held)
{
	if (USkeletalMesh* Mesh = CampusAssets::OperatorMesh(Index))
	{
		if (Operator->GetSkeletalMeshAsset() != Mesh)
		{
			for (UStaticMeshComponent* Part : GearParts)
			{
				if (Part)
				{
					Part->DestroyComponent();
				}
			}
			GearParts.Reset();
		}
		Operator->SetSkeletalMesh(Mesh);
		if (UMaterialInterface* Soldier = CampusAssets::SoldierMaterial())
		{
			for (int32 i = 0; i < Operator->GetNumMaterials(); ++i)
			{
				Operator->SetMaterial(i, Soldier);
			}
		}
		CampusAssets::AttachSoldierGear(Operator, GearParts);
		CampusAssets::ColorSoldierGear(GearParts, 0);
	}
	const FCampusWeaponDef* Def = UCampusArmory::Get().FindWeapon(Held.WeaponId);
	if (UClass* AnimClass = CampusAssets::ThirdPersonAnimClass(Def && Def->bPistolAnims))
	{
		Operator->SetAnimInstanceClass(AnimClass);
	}
	for (UActorComponent* C : HeldComponents)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	HeldComponents.Reset();
	ACampusWeapon::BuildPreview(this, Operator, Held, HeldComponents, TEXT("HandGrip_R"));
}

FVector ACampusMenuStage::FramePointToWorld(const FVector& FramePoint) const
{
	return WeaponFrame ? WeaponFrame->GetComponentTransform().TransformPosition(FramePoint) : WeaponPivot->GetComponentLocation();
}

FVector ACampusMenuStage::GetSlotWorldPoint(ECampusAttachmentSlot Slot) const
{
	return FramePointToWorld(ACampusWeapon::GetSlotFocusPoint(Shown, Slot));
}

FVector ACampusMenuStage::GetWeaponCenterWorld() const
{
	const FBox Bounds = GetWeaponLocalBounds();
	return Bounds.IsValid ? DisplayRoot->GetComponentTransform().TransformPosition(Bounds.GetCenter()) : WeaponPivot->GetComponentLocation();
}

float ACampusMenuStage::GetWeaponDepth() const
{
	const FBox Local = GetWeaponLocalBounds();
	const float Length = Local.IsValid ? Local.GetSize().X : 85.f;
	const float Thickness = Local.IsValid ? Local.GetSize().Y : 6.f;
	return Thickness + Length * FMath::Sin(FMath::DegreesToRadians(DisplaySwayYaw)) + 3.f;
}

FBox ACampusMenuStage::GetWeaponLocalBounds() const
{
	const FTransform& D = DisplayRoot->GetComponentTransform();
	FBox Local(ForceInit);
	for (const UActorComponent* C : WeaponComponents)
	{
		if (const UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C); P && P->IsRegistered())
		{
			Local += P->CalcBounds(P->GetComponentTransform().GetRelativeTransform(D)).GetBox();
		}
	}
	return Local;
}

FCampusCameraShot ACampusMenuStage::FrameWeapon(const FBox2D& SafeRect, float FOV, const FVector& LocalDir, float Aperture, float Handheld, float Exposure, const FVector2D& ViewSize) const
{
	const FTransform& D = DisplayRoot->GetComponentTransform();
	FBox Local = GetWeaponLocalBounds();
	if (!Local.IsValid) Local = FBox(FVector(-42.5f, -5.f, 12.f), FVector(42.5f, 5.f, 32.f));
	const FVector Center = D.TransformPosition(Local.GetCenter());
	const FVector Away = D.TransformVectorNoScale(LocalDir.GetSafeNormal());
	const FRotator Rotation = (-Away).Rotation();
	const FVector Right = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
	const float Aspect = FMath::Max(ViewSize.X, 1.f) / FMath::Max(ViewSize.Y, 1.f);
	const float TanH = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
	const float TanV = TanH / Aspect;
	const FVector2D NdcCenter(SafeRect.GetCenter().X * 2.f / ViewSize.X - 1.f, 1.f - SafeRect.GetCenter().Y * 2.f / ViewSize.Y);
	const float CenterX = static_cast<float>(NdcCenter.X) * TanH;
	const float CenterY = static_cast<float>(NdcCenter.Y) * TanV;
	const float WidthFrac = static_cast<float>(SafeRect.GetSize().X / ViewSize.X) * 0.88f;
	const float HeightFrac = static_cast<float>(SafeRect.GetSize().Y / ViewSize.Y) * 0.88f;
	float Dist = 30.f;
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Corner((i & 1) ? Local.Max.X : Local.Min.X, (i & 2) ? Local.Max.Y : Local.Min.Y, (i & 4) ? Local.Max.Z : Local.Min.Z);
		const FVector Offset = D.TransformPosition(Corner) - Center;
		const float Depth = FVector::DotProduct(Offset, -Away);
		const float Horizontal = FVector::DotProduct(Offset, Right) - CenterX * Depth;
		const float Vertical = FVector::DotProduct(Offset, Up) - CenterY * Depth;
		Dist = FMath::Max(Dist, FMath::Abs(Horizontal) / (FMath::Max(WidthFrac, 0.01f) * TanH) - Depth);
		Dist = FMath::Max(Dist, FMath::Abs(Vertical) / (FMath::Max(HeightFrac, 0.01f) * TanV) - Depth);
	}
	Dist += 8.f;
	const FVector Aim = Center - Right * (CenterX * Dist) - Up * (CenterY * Dist);
	return FCampusCameraShot::LookAt(Aim + Away * Dist, Aim, FOV, ApertureCovering(Aperture, Dist, FOV, GetWeaponDepth()), Handheld, Exposure);
}

FCampusCameraShot ACampusMenuStage::GetShot(ECampusMenuShot Shot, ECampusAttachmentSlot Slot, const FVector2D& InViewSize) const
{
	const FVector Op = OperatorRoot->GetComponentLocation();
	const FVector OpFwd = OperatorRoot->GetForwardVector();
	const FVector OpRight = OperatorRoot->GetRightVector();
	const FVector2D ViewSize = InViewSize.X > 1.f && InViewSize.Y > 1.f ? InViewSize : FVector2D(1920.f, 1080.f);
	const CampusMenuPresentation::FWeaponLayout Layout = CampusMenuPresentation::WeaponLayout(ViewSize, Shot == ECampusMenuShot::GunsmithSlot, Shot == ECampusMenuShot::Loadouts);
	switch (Shot)
	{
	case ECampusMenuShot::Title:
		if (!TitleCamera.IsZero())
		{
			return FCampusCameraShot::LookAt(TitleCamera, TitleTarget, 52.f, 5.6f, 1.2f);
		}
		return FCampusCameraShot::LookAt(FVector(1300.f, -3000.f, 1500.f), FVector(-1500.f, 300.f, 300.f), 55.f, 8.f, 1.4f);
	// The camera looks back along -OpFwd, so +OpRight is screen-left. The lobby has the menu column on the left and the
	// loadout / map cards on the right: a small lead puts the operator in the clear band between them (~58% across).
	case ECampusMenuShot::Lobby:
		return FCampusCameraShot::LookAt(Op + OpFwd * 330.f + OpRight * 100.f + FVector(0.f, 0.f, 150.f), Op + FVector(0.f, 0.f, 118.f) + OpRight * 22.f, 40.f, 2.2f, 1.f);
	case ECampusMenuShot::Operator:
		return FCampusCameraShot::LookAt(Op + OpFwd * 430.f + OpRight * 40.f + FVector(0.f, 0.f, 120.f), Op + FVector(0.f, 0.f, 100.f) + OpRight * 70.f, 45.f, 2.0f, 0.8f, -0.6f);
	// Weapon framings are worked out from the weapon actually on the display (its length and centre, along the
	// display's own axes), so every weapon sits in the clear band between the menu panels: the left column and, on
	// the loadouts page, the loadout card on the right. The view looks back along the display's -Y with screen-right
	// along its +X (the muzzle).
	case ECampusMenuShot::Loadouts:
		return FrameWeapon(Layout.Weapon, 40.f, FVector(-0.15f, 0.95f, 0.33f), 2.8f, 0.6f, -0.8f, ViewSize);
	case ECampusMenuShot::Gunsmith:
		return FrameWeapon(Layout.Weapon, 42.f, FVector(0.f, 1.f, 0.17f), 4.0f, 0.4f, -1.0f, ViewSize);
	case ECampusMenuShot::GunsmithSlot:
	{
		FVector Dir(0.f, 1.f, 0.17f);
		switch (Slot)
		{
		case ECampusAttachmentSlot::Optic: Dir.Z += 0.2f; break;
		case ECampusAttachmentSlot::Underbarrel:
		case ECampusAttachmentSlot::Magazine:
		case ECampusAttachmentSlot::RearGrip: Dir.Z -= 0.12f; break;
		case ECampusAttachmentSlot::Muzzle:
		case ECampusAttachmentSlot::Barrel: Dir.X += 0.22f; break;
		case ECampusAttachmentSlot::Stock: Dir.X -= 0.22f; break;
		default: break;
		}
		return FrameWeapon(Layout.Weapon, 42.f, Dir, 4.f, 0.3f, -1.f, ViewSize);
	}
	case ECampusMenuShot::Options:
		if (!OptionsCamera.IsZero())
		{
			return FCampusCameraShot::LookAt(OptionsCamera, OptionsTarget, 50.f, 2.8f, 0.7f);
		}
		return FCampusCameraShot::LookAt(FVector(-3300.f, 900.f, 190.f), FVector(-3250.f, -1500.f, 120.f), 50.f, 5.6f, 0.7f);
	case ECampusMenuShot::Deploy:
		if (!DeployCamera.IsZero())
		{
			return FCampusCameraShot::LookAt(DeployCamera, DeployTarget, 62.f, 8.f, 0.3f);
		}
		return FCampusCameraShot::LookAt(GateFocus + FVector(0.f, 900.f, 20.f), GateFocus + FVector(0.f, -800.f, 0.f), 70.f, 11.f, 0.2f);
	default:
		return FCampusCameraShot::LookAt(Op + OpFwd * 330.f + FVector(0.f, 0.f, 150.f), Op + FVector(0.f, 0.f, 130.f), 40.f);
	}
}
