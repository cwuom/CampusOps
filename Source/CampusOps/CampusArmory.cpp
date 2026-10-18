#include "CampusArmory.h"
#include "CampusPerks.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "CampusArmory"

FText FCampusDisplayStats::Label(int32 Index)
{
	switch (Index)
	{
	case 0: return LOCTEXT("StatAccuracy", "精准度");
	case 1: return LOCTEXT("StatDamage", "伤害");
	case 2: return LOCTEXT("StatRange", "射程");
	case 3: return LOCTEXT("StatFireRate", "射速");
	case 4: return LOCTEXT("StatMobility", "机动性");
	default: return LOCTEXT("StatControl", "操控性");
	}
}

namespace
{
	const FLinearColor ColBlack(0.012f, 0.012f, 0.013f);
	const FLinearColor ColGunmetal(0.04f, 0.042f, 0.046f);
	const FLinearColor ColTan(0.32f, 0.25f, 0.15f);
	const FLinearColor ColOD(0.075f, 0.085f, 0.05f);
	const FLinearColor ColWood(0.20f, 0.085f, 0.03f);
	const FLinearColor ColLens(0.015f, 0.04f, 0.07f);
	const FLinearColor ColLaser(1.0f, 0.05f, 0.03f);

	FCampusVisualPart Box(const FVector& Loc, const FVector& Size, const FLinearColor& Col, const FRotator& Rot = FRotator::ZeroRotator, float Rough = 0.55f, float Metal = 0.35f)
	{
		return FCampusVisualPart(ECampusShape::Cube, Loc, Size, Col, Rot, Rough, Metal);
	}

	/** Cylinder lying along the weapon's X axis. */
	FCampusVisualPart TubeX(const FVector& Loc, float Diameter, float Length, const FLinearColor& Col, float Rough = 0.4f, float Metal = 0.7f, float Emissive = 0.f)
	{
		return FCampusVisualPart(ECampusShape::Cylinder, Loc, FVector(Diameter, Diameter, Length), Col, FRotator(90.f, 0.f, 0.f), Rough, Metal, Emissive);
	}

	FCampusVisualPart TubeZ(const FVector& Loc, float Diameter, float Length, const FLinearColor& Col, const FRotator& Rot = FRotator::ZeroRotator, float Rough = 0.5f, float Metal = 0.3f)
	{
		return FCampusVisualPart(ECampusShape::Cylinder, Loc, FVector(Diameter, Diameter, Length), Col, Rot, Rough, Metal);
	}

	using ESlot = ECampusAttachmentSlot;
	using ERole = ECampusPartRole;

	FCampusModelPart Part(const TCHAR* Mesh, ERole Role = ERole::Static, ESlot HideWith = ESlot::Count, ESlot ShowWith = ESlot::Count)
	{
		return FCampusModelPart(Mesh, Role, HideWith, ShowWith);
	}

	/** A part modelled for another weapon of the family (same frame): the RPK and Saiga share the AK's, the HK416 the M4's. */
	FCampusModelPart Shared(const TCHAR* Folder, const TCHAR* Mesh, ERole Role = ERole::Static, ESlot HideWith = ESlot::Count, ESlot ShowWith = ESlot::Count)
	{
		FCampusModelPart P(TEXT(""), Role, HideWith, ShowWith);
		P.Mesh = FString::Printf(TEXT("/Game/Campus/Weapons/%s/%s.%s"), Folder, Mesh, Mesh);
		return P;
	}

	void SetAnchors(FCampusWeaponDef& W, const FVector& Muzzle, const FVector& Barrel, const FVector& Laser, const FVector& Optic, const FVector& Stock, const FVector& Under, const FVector& Mag)
	{
		W.Anchors.Add(ESlot::Muzzle, Muzzle);
		W.Anchors.Add(ESlot::Barrel, Barrel);
		W.Anchors.Add(ESlot::Laser, Laser);
		W.Anchors.Add(ESlot::Optic, Optic);
		W.Anchors.Add(ESlot::Stock, Stock);
		W.Anchors.Add(ESlot::Underbarrel, Under);
		W.Anchors.Add(ESlot::Magazine, Mag);
		W.Anchors.Add(ESlot::RearGrip, FVector::ZeroVector);
		W.Muzzle = Muzzle;
	}
}

UCampusArmory::UCampusArmory()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Campus Armory");

	// ------------------------------------------------------------------ Attachments
	auto Add = [this](const TCHAR* Id, const TCHAR* Name, ECampusAttachmentSlot Slot) -> FCampusAttachmentDef&
	{
		FCampusAttachmentDef& A = AttachmentDefs.AddDefaulted_GetRef();
		A.Id = FName(Id);
		A.DisplayName = FText::FromString(Name);
		A.Slot = Slot;
		return A;
	};

	{
		FCampusAttachmentDef& A = Add(TEXT("muzzle_suppressor"), TEXT("单体式消音器"), ECampusAttachmentSlot::Muzzle);
		A.Parts = { TubeX(FVector(0.9f, 0, 0), 2.6f, 2.0f, ColGunmetal), TubeX(FVector(10.f, 0, 0), 3.9f, 18.f, ColBlack, 0.6f, 0.45f) };
		A.Model = { Part(TEXT("SM_Att_Suppressor")) };
		A.MuzzleExtension = 20.6f;
		A.Mods.bSuppressor = true;
		A.Mods.Range = 1.15f;
		A.Mods.ADSTime = 1.06f;
		A.Mods.VerticalRecoil = 0.97f;
		A.Mods.VisualKick = 0.92f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("muzzle_comp"), TEXT("战术补偿器"), ECampusAttachmentSlot::Muzzle);
		A.Parts = { TubeX(FVector(2.6f, 0, 0), 2.9f, 5.2f, ColGunmetal), Box(FVector(3.4f, 0, 1.35f), FVector(2.8f, 1.6f, 0.6f), ColBlack) };
		A.Model = { Part(TEXT("SM_Att_Comp")) };
		A.MuzzleExtension = 5.6f;
		A.Mods.VerticalRecoil = 0.86f;
		A.Mods.HorizontalRecoil = 1.06f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("muzzle_flash"), TEXT("消焰器"), ECampusAttachmentSlot::Muzzle);
		A.Parts = { TubeX(FVector(3.0f, 0, 0), 2.5f, 6.0f, ColBlack), Box(FVector(5.2f, 0, 0), FVector(1.6f, 2.9f, 0.5f), ColBlack), Box(FVector(5.2f, 0, 0), FVector(1.6f, 0.5f, 2.9f), ColBlack) };
		A.Model = { Part(TEXT("SM_Att_FlashHider3")) };
		A.MuzzleExtension = 5.8f;
		A.Mods.HorizontalRecoil = 0.9f;
		A.Mods.VisualKick = 0.9f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("barrel_long"), TEXT("16.5\" 加长枪管"), ECampusAttachmentSlot::Barrel);
		A.Parts = { Box(FVector(7.f, 0, 0), FVector(15.f, 5.0f, 5.6f), ColBlack, FRotator::ZeroRotator, 0.6f, 0.2f), TubeX(FVector(19.f, 0, 0.4f), 2.0f, 10.f, ColGunmetal) };
		A.bBarrelTube = true;
		A.MuzzleExtension = 12.f;
		A.Mods.Range = 1.25f;
		A.Mods.VerticalRecoil = 0.92f;
		A.Mods.ADSTime = 1.08f;
		A.Mods.MoveSpeed = 0.97f;
		A.Mods.Penetration = 1.15f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("barrel_short"), TEXT("CQB 短枪管"), ECampusAttachmentSlot::Barrel);
		A.Parts = { Box(FVector(2.f, 0, 0), FVector(6.f, 5.4f, 6.0f), ColTan, FRotator::ZeroRotator, 0.7f, 0.1f) };
		A.bBarrelTube = true;
		A.MuzzleExtension = -5.f;
		A.Mods.ADSTime = 0.9f;
		A.Mods.MoveSpeed = 1.03f;
		A.Mods.Range = 0.8f;
		A.Mods.VerticalRecoil = 1.06f;
		A.Mods.HipSpread = 0.92f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("laser_tac"), TEXT("1mW 战术激光"), ECampusAttachmentSlot::Laser);
		A.Parts = { Box(FVector(0, 1.3f, 0), FVector(6.f, 2.4f, 2.6f), ColBlack), TubeX(FVector(3.2f, 1.3f, 0.45f), 0.9f, 0.5f, ColLaser, 0.2f, 0.f, 30.f) };
		A.Model = { Part(TEXT("SM_Att_Laser")) };
		A.LaserOffset = FVector(3.8f, 2.55f, 0.75f);
		A.bLaser = true;
		A.Mods.ADSTime = 0.92f;
		A.Mods.HipSpread = 0.8f;
		A.Mods.SprintToFire = 0.92f;
	}
	{
		// Compact weapon light (M600 style) on the 3 o'clock rail: clamp, knurled body, tail cap, crenellated bezel, lens.
		FCampusAttachmentDef& A = Add(TEXT("light_tac"), TEXT("M600 战术枪灯"), ECampusAttachmentSlot::Laser);
		A.Parts = {
			Box(FVector(-0.6f, 0.9f, 0.f), FVector(4.4f, 1.8f, 2.2f), ColBlack, FRotator::ZeroRotator, 0.6f, 0.35f),
			Box(FVector(-0.6f, 0.9f, -1.35f), FVector(1.4f, 0.7f, 0.6f), ColGunmetal, FRotator::ZeroRotator, 0.4f, 0.8f),
			TubeX(FVector(0.4f, 2.5f, 0.f), 2.3f, 8.4f, ColBlack, 0.55f, 0.45f),
			TubeX(FVector(1.2f, 2.5f, 0.f), 2.38f, 0.35f, ColGunmetal, 0.45f, 0.7f),
			TubeX(FVector(-0.2f, 2.5f, 0.f), 2.38f, 0.35f, ColGunmetal, 0.45f, 0.7f),
			TubeX(FVector(-4.3f, 2.5f, 0.f), 2.45f, 1.6f, ColGunmetal, 0.45f, 0.7f),
			TubeX(FVector(-5.4f, 2.5f, 0.f), 1.5f, 0.6f, ColBlack, 0.85f, 0.f),
			TubeX(FVector(5.0f, 2.5f, 0.f), 2.9f, 1.8f, ColBlack, 0.5f, 0.5f),
			TubeX(FVector(5.75f, 2.5f, 0.f), 2.3f, 0.2f, FLinearColor(0.6f, 0.62f, 0.64f), 0.06f, 1.f)
		};
		A.LaserOffset = FVector(5.95f, 2.5f, 0.f);
		A.bFlashlight = true;
		A.Mods.ADSTime = 1.02f;
		A.Mods.HipSpread = 0.95f;
	}
	{
		// Open mini reflex on a riser (RMR style): base, battery block, window posts under a sloped hood, front emitter cover.
		FCampusAttachmentDef& A = Add(TEXT("optic_reddot"), TEXT("反射式红点瞄准镜"), ECampusAttachmentSlot::Optic);
		const float H = 3.2f;
		A.Parts = {
			Box(FVector(0.2f, 0, 0.45f), FVector(5.4f, 2.5f, 0.9f), ColBlack, FRotator::ZeroRotator, 0.6f, 0.35f),
			Box(FVector(0.2f, -1.45f, 0.5f), FVector(1.4f, 0.6f, 0.8f), ColGunmetal, FRotator::ZeroRotator, 0.4f, 0.8f),
			Box(FVector(0.2f, 0, 1.35f), FVector(4.2f, 2.6f, 0.9f), ColBlack, FRotator::ZeroRotator, 0.5f, 0.45f),
			Box(FVector(-1.55f, 0, 2.15f), FVector(1.0f, 2.7f, 0.8f), ColBlack, FRotator::ZeroRotator, 0.5f, 0.45f),
			Box(FVector(0.15f, -1.3f, H), FVector(2.4f, 0.32f, 2.5f), ColBlack, FRotator(0.f, 0.f, -6.f), 0.45f, 0.5f),
			Box(FVector(0.15f, 1.3f, H), FVector(2.4f, 0.32f, 2.5f), ColBlack, FRotator(0.f, 0.f, 6.f), 0.45f, 0.5f),
			Box(FVector(0.15f, 0, H + 1.28f), FVector(2.5f, 2.55f, 0.32f), ColBlack, FRotator(-7.f, 0.f, 0.f), 0.45f, 0.5f),
			Box(FVector(1.15f, 0, 2.1f), FVector(0.5f, 2.3f, 0.5f), ColGunmetal, FRotator::ZeroRotator, 0.35f, 0.7f),
			Box(FVector(-1.05f, 1.45f, 1.75f), FVector(0.8f, 0.25f, 0.6f), ColGunmetal, FRotator::ZeroRotator, 0.35f, 0.7f),
			Box(FVector(-1.05f, -1.45f, 1.75f), FVector(0.8f, 0.25f, 0.6f), ColGunmetal, FRotator::ZeroRotator, 0.35f, 0.7f)
		};
		A.Model = { Part(TEXT("SM_Att_RedDot")) };
		A.Reticle = ECampusReticle::Dot;
		A.SightHeight = 2.91f;
		A.ReticleForward = 0.9f;
		A.Window = FVector2D(3.2f, 2.36f);
		A.Mods.ZoomOverride = 1.25f;
		A.Mods.ADSTime = 1.03f;
	}
	{
		// Holographic window sight (EOTech style): base with battery cap, protective hood, rear control box.
		FCampusAttachmentDef& A = Add(TEXT("optic_holo"), TEXT("全息瞄准镜"), ECampusAttachmentSlot::Optic);
		A.Parts = {
			Box(FVector(-0.5f, 0, 0.8f), FVector(8.6f, 3.4f, 1.6f), ColBlack),
			TubeX(FVector(-0.2f, 0, 0.75f), 1.5f, 6.6f, ColGunmetal, 0.45f, 0.6f),
			Box(FVector(-3.7f, 0, 2.5f), FVector(2.6f, 3.6f, 2.2f), ColBlack),
			Box(FVector(-5.05f, -0.75f, 2.6f), FVector(0.25f, 0.9f, 0.6f), ColGunmetal, FRotator::ZeroRotator, 0.4f, 0.6f),
			Box(FVector(-5.05f, 0.75f, 2.6f), FVector(0.25f, 0.9f, 0.6f), ColGunmetal, FRotator::ZeroRotator, 0.4f, 0.6f),
			Box(FVector(1.3f, -2.0f, 3.5f), FVector(4.4f, 0.4f, 4.8f), ColBlack),
			Box(FVector(1.3f, 2.0f, 3.5f), FVector(4.4f, 0.4f, 4.8f), ColBlack),
			Box(FVector(1.3f, 0, 5.85f), FVector(4.4f, 4.4f, 0.45f), ColBlack),
			Box(FVector(3.35f, 0, 1.75f), FVector(0.3f, 3.8f, 0.4f), ColBlack),
			Box(FVector(-0.6f, 0, 1.75f), FVector(0.3f, 3.8f, 0.4f), ColBlack),
			Box(FVector(1.3f, 2.25f, 5.0f), FVector(1.2f, 0.25f, 0.8f), ColGunmetal, FRotator::ZeroRotator, 0.4f, 0.6f)
		};
		A.Model = { Part(TEXT("SM_Att_Holo")) };
		A.Reticle = ECampusReticle::Ring;
		A.SightHeight = 3.8f;
		A.ReticleForward = 2.55f;
		A.Window = FVector2D(3.1f, 2.7f);
		A.Mods.ZoomOverride = 1.3f;
		A.Mods.ADSTime = 1.05f;
	}
	{
		// Fixed 3.5x prism scope (ACOG style): ring mount with thumb nuts, objective bell, body, ocular and a top fibre housing.
		FCampusAttachmentDef& A = Add(TEXT("optic_scope"), TEXT("3.5倍战术镜"), ECampusAttachmentSlot::Optic);
		const float H = 4.4f;
		A.Parts = {
			Box(FVector(0.5f, 0, 0.6f), FVector(11.f, 2.6f, 1.2f), ColBlack),
			Box(FVector(-3.5f, 0, 1.9f), FVector(1.8f, 2.8f, 2.6f), ColBlack),
			Box(FVector(4.5f, 0, 1.9f), FVector(1.8f, 2.8f, 2.6f), ColBlack),
			TubeZ(FVector(-3.5f, -1.75f, 0.7f), 1.2f, 0.7f, ColGunmetal, FRotator(0.f, 0.f, 90.f), 0.4f, 0.8f),
			TubeZ(FVector(4.5f, -1.75f, 0.7f), 1.2f, 0.7f, ColGunmetal, FRotator(0.f, 0.f, 90.f), 0.4f, 0.8f),
			TubeX(FVector(0.5f, 0, H), 3.3f, 12.f, ColBlack, 0.55f, 0.35f),
			Box(FVector(0.5f, 0, H + 1.4f), FVector(9.f, 2.6f, 1.6f), ColBlack, FRotator::ZeroRotator, 0.55f, 0.35f),
			Box(FVector(1.5f, 0, H + 2.35f), FVector(6.f, 1.0f, 0.5f), FLinearColor(0.02f, 0.05f, 0.02f), FRotator::ZeroRotator, 0.2f, 0.f),
			TubeX(FVector(8.6f, 0, H), 4.0f, 3.0f, ColBlack, 0.5f, 0.4f),
			TubeX(FVector(11.3f, 0, H), 4.6f, 2.6f, ColBlack, 0.5f, 0.4f),
			TubeX(FVector(-7.8f, 0, H), 3.6f, 3.4f, ColBlack, 0.5f, 0.4f),
			TubeX(FVector(-10.4f, 0, H), 3.9f, 2.0f, FLinearColor(0.02f, 0.02f, 0.02f), 0.85f, 0.f)
		};
		A.Model = { Part(TEXT("SM_Att_Acog")) };
		A.Reticle = ECampusReticle::Scope;
		A.SightHeight = 4.0f;
		A.LensFrontRearDiameter = FVector(8.75f, -9.0f, 3.8f);
		(void)H;
		A.Mods.ZoomOverride = 3.0f;
		A.Mods.ADSTime = 1.15f;
		A.Mods.AimStability = 0.85f;
		A.Mods.MoveSpeed = 0.98f;
	}
	{
		// 6x rifle scope (30 mm tube, 50 mm objective): two-piece rings, capped turrets, side focus, power ring, eyecup.
		FCampusAttachmentDef& A = Add(TEXT("optic_sniper"), TEXT("6倍狙击镜"), ECampusAttachmentSlot::Optic);
		const float Axis = 4.7f;
		A.Parts = {
			Box(FVector(0.f, 0, 0.6f), FVector(14.f, 2.6f, 1.2f), ColBlack),
			TubeX(FVector(-1.f, 0, Axis), 1.5f, 20.f, ColBlack, 0.5f, 0.3f),
			TubeX(FVector(11.5f, 0, Axis), 2.9f, 7.f, ColBlack, 0.5f, 0.3f),
			TubeX(FVector(-12.f, 0, Axis), 2.1f, 5.f, ColBlack, 0.5f, 0.3f)
		};
		A.Model = { Part(TEXT("SM_Att_Sniper")) };
		A.Reticle = ECampusReticle::Scope;
		A.SightHeight = Axis;
		A.LensFrontRearDiameter = FVector(14.0f, -13.7f, 3.8f);
		A.Mods.ZoomOverride = 6.0f;
		A.Mods.ADSTime = 1.3f;
		A.Mods.AimStability = 0.78f;
		A.Mods.MoveSpeed = 0.96f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("stock_tac"), TEXT("战术枪托"), ECampusAttachmentSlot::Stock);
		A.Parts = { Box(FVector(-4.f, 0, -1.5f), FVector(6.f, 3.8f, 9.f), ColTan, FRotator::ZeroRotator, 0.75f, 0.05f), Box(FVector(-7.3f, 0, -1.5f), FVector(0.8f, 4.2f, 11.f), ColBlack, FRotator::ZeroRotator, 0.9f, 0.f) };
		A.Model = { Part(TEXT("SM_Att_StockTac")) };
		A.Mods.ADSMoveSpeed = 1.15f;
		A.Mods.VerticalRecoil = 1.03f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("stock_heavy"), TEXT("重型枪托"), ECampusAttachmentSlot::Stock);
		A.Parts = { Box(FVector(-5.f, 0, -1.5f), FVector(10.f, 4.2f, 10.f), ColBlack), Box(FVector(-10.5f, 0, -1.5f), FVector(1.2f, 4.6f, 12.5f), ColGunmetal, FRotator::ZeroRotator, 0.9f, 0.f), Box(FVector(-4.f, 0, 4.2f), FVector(7.f, 3.2f, 1.6f), ColBlack) };
		A.Model = { Part(TEXT("SM_Att_StockHeavy")) };
		A.Mods.VerticalRecoil = 0.92f;
		A.Mods.HorizontalRecoil = 0.92f;
		A.Mods.AimStability = 1.25f;
		A.Mods.ADSTime = 1.06f;
		A.Mods.MoveSpeed = 0.98f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("grip_vertical"), TEXT("指挥官垂直握把"), ECampusAttachmentSlot::Underbarrel);
		A.Parts = { Box(FVector(0, 0, -0.6f), FVector(4.2f, 2.6f, 1.2f), ColBlack), TubeZ(FVector(0, 0, -5.4f), 3.0f, 9.5f, ColBlack) };
		A.Model = { Part(TEXT("SM_Att_VertGrip")) };
		// Fist round the grip, the wrist behind and to the left of it.
		A.bForegrip = true;
		A.HandGrip = FVector(-2.2f, -4.0f, -5.4f);
		A.HandTurn = FRotator(0.f, 25.f, 70.f);
		A.Mods.VerticalRecoil = 0.88f;
		A.Mods.HorizontalRecoil = 0.95f;
		A.Mods.ADSTime = 1.03f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("grip_angled"), TEXT("斜角握把"), ECampusAttachmentSlot::Underbarrel);
		A.Parts = { Box(FVector(0, 0, -2.0f), FVector(8.5f, 2.6f, 3.0f), ColBlack, FRotator(-22.f, 0.f, 0.f)) };
		A.Model = { Part(TEXT("SM_Att_AngledGrip")) };
		// The heel of the hand against the ramp, thumb over the handguard.
		A.bForegrip = true;
		A.HandGrip = FVector(-4.6f, -4.2f, -1.2f);
		A.HandTurn = FRotator(-10.f, 10.f, 20.f);
		A.Mods.ADSTime = 0.93f;
		A.Mods.VerticalRecoil = 0.97f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("mag_extended"), TEXT("45发扩容弹匣"), ECampusAttachmentSlot::Magazine);
		A.Parts = { Box(FVector(1.2f, 0, -9.f), FVector(4.6f, 2.6f, 18.f), ColBlack, FRotator(8.f, 0.f, 0.f), 0.6f, 0.2f) };
		A.bPerWeaponModel = true;
		A.Mods.MagSizeScale = 1.5f;
		A.Mods.ReloadTime = 1.12f;
		A.Mods.ADSTime = 1.05f;
		A.Mods.MoveSpeed = 0.98f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("mag_fast"), TEXT("双联快拔弹匣"), ECampusAttachmentSlot::Magazine);
		A.Parts = { Box(FVector(0.4f, -1.4f, -7.f), FVector(4.2f, 2.4f, 12.f), ColBlack), Box(FVector(0.4f, 1.4f, -7.f), FVector(4.2f, 2.4f, 12.f), ColBlack), Box(FVector(0.4f, 0, -6.f), FVector(4.6f, 5.4f, 1.2f), ColOD, FRotator::ZeroRotator, 0.9f, 0.f) };
		A.bPerWeaponModel = true;
		A.Mods.ReloadTime = 0.8f;
	}
	{
		FCampusAttachmentDef& A = Add(TEXT("grip_stippled"), TEXT("防滑握把胶带"), ECampusAttachmentSlot::RearGrip);
		A.Parts = { Box(FVector(-0.6f, 0, -4.5f), FVector(3.6f, 3.0f, 8.f), ColTan, FRotator(-12.f, 0.f, 0.f), 0.95f, 0.f) };
		A.bPerWeaponModel = true;
		A.Mods.SprintToFire = 0.88f;
		A.Mods.ADSTime = 0.96f;
		A.Mods.AimStability = 0.95f;
	}

	TArray<FName> RifleAttachments;
	for (const FCampusAttachmentDef& A : AttachmentDefs)
	{
		RifleAttachments.Add(A.Id);
	}

	// ------------------------------------------------------------------ Weapons
	const TSoftObjectPtr<USkeletalMesh> RifleMesh(FSoftObjectPath(TEXT("/Game/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle")));
	const TSoftObjectPtr<USkeletalMesh> PistolMesh(FSoftObjectPath(TEXT("/Game/Weapons/Pistol/Meshes/SKM_Pistol.SKM_Pistol")));

	auto RifleAnchors = [](FCampusWeaponDef& W, float Scale)
	{
		// Measured from SKM_Rifle: origin at the grip, muzzle socket at (Y 50.9, Z 9.9), bounds Y -27..51, Z -11..17.6
		W.Anchors.Add(ECampusAttachmentSlot::Muzzle, FVector(50.9f, 0.f, 9.9f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::Barrel, FVector(43.f, 0.f, 9.4f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::Laser, FVector(32.f, 2.9f, 8.f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::Optic, FVector(6.f, 0.f, 14.5f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::Stock, FVector(-25.f, 0.f, 6.f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::Underbarrel, FVector(34.f, 0.f, 0.f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::Magazine, FVector(11.f, 0.f, -8.f) * Scale);
		W.Anchors.Add(ECampusAttachmentSlot::RearGrip, FVector(0.f, 0.f, -2.f) * Scale);
		W.IronSight = FVector(2.f, 0.f, 17.f) * Scale;
		W.Muzzle = FVector(50.9f, 0.f, 9.9f) * Scale;
		W.EjectPort = FVector(8.f, 3.f, 9.f) * Scale;
	};

	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("M4A1");
		W.DisplayName = FText::FromString(TEXT("M4A1"));
		W.Description = LOCTEXT("M4Desc", "全能型全自动突击步枪，射速与后坐力平衡，适合中距离交战。");
		W.Class = ECampusWeaponClass::AssaultRifle;
		W.FireModes = { ECampusFireMode::Auto, ECampusFireMode::Burst, ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		// 14.5" barrel, 12" M-LOK rail, A2 birdcage; frame origin = the palm on the pistol grip, bore 9.5 cm above it.
		W.Model = {
			Part(TEXT("SM_M4A1_Receiver")), Part(TEXT("SM_M4A1_Bolt"), ERole::Bolt), Part(TEXT("SM_M4A1_Charging"), ERole::Charging),
			Part(TEXT("SM_M4A1_Grip"), ERole::Static, ESlot::RearGrip), Part(TEXT("SM_M4A1_Stock"), ERole::Static, ESlot::Stock),
			Part(TEXT("SM_M4A1_Handguard")), Part(TEXT("SM_M4A1_Barrel")), Part(TEXT("SM_M4A1_FlashHider"), ERole::Static, ESlot::Muzzle),
			Part(TEXT("SM_M4A1_Mag"), ERole::Magazine, ESlot::Magazine),
			Part(TEXT("SM_M4A1_RearSight"), ERole::Static, ESlot::Optic),
			Part(TEXT("SM_M4A1_FrontSight"), ERole::Static, ESlot::Optic), Part(TEXT("SM_M4A1_FrontSightFolded"), ERole::Static, ESlot::Count, ESlot::Optic) };
		SetAnchors(W, FVector(54.4f, 0.f, 9.5f), FVector(48.1f, 0.f, 9.5f), FVector(43.0f, 2.2f, 9.4f), FVector(6.5f, 0.f, 12.55f), FVector(-1.8f, 0.f, 8.9f),
			FVector(39.0f, 0.f, 7.2f), FVector(12.8f, 0.f, 4.3f));
		W.IronSight = FVector(0.4f, 0.f, 14.45f);
		W.FrontSightX = 45.4f;
		W.IronEyeRelief = 9.5f;
		W.EjectPort = FVector(10.2f, 1.3f, 9.6f);
		W.ExposedBarrel = 6.3f;
		W.BarrelRadius = 0.95f;
		W.MuzzleDeviceLength = 5.6f;
		W.LeftHandGrip = FVector(28.f, -4.2f, 5.5f);
		W.FireSound = TEXT("Gun_AR");
		W.Attachments = RifleAttachments;
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 28.f; S.DamageMid = 23.f; S.DamageFar = 19.f; S.RangeNear = 3000.f; S.RangeMid = 5000.f;
		S.RPM = 800.f; S.MagSize = 30; S.ReserveMags = 4;
		S.ReloadTime = 2.1f; S.EmptyReloadTime = 2.6f; S.ADSTime = 0.24f;
		S.RecoilPitch = 0.42f; S.RecoilYaw = 0.18f; S.RecoilYawBias = 0.04f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("AK47");
		W.DisplayName = FText::FromString(TEXT("AK-47"));
		W.Description = LOCTEXT("AKDesc", "大口径突击步枪，三枪致命，但后坐力凶猛，需要压枪。");
		W.Class = ECampusWeaponClass::AssaultRifle;
		W.FireModes = { ECampusFireMode::Auto, ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		W.MeshTint = FLinearColor(0.05f, 0.048f, 0.045f);
		// AKM: stamped receiver, wood furniture, slant brake; mounts appear only when an optic / grip / laser is fitted.
		W.Model = {
			Part(TEXT("SM_AK47_Receiver")), Part(TEXT("SM_AK47_Bolt"), ERole::Bolt), Part(TEXT("SM_AK47_Handguard")),
			Part(TEXT("SM_AK47_Stock"), ERole::Static, ESlot::Stock), Part(TEXT("SM_AK47_Grip"), ERole::Static, ESlot::RearGrip),
			Part(TEXT("SM_AK47_Brake"), ERole::Static, ESlot::Muzzle), Part(TEXT("SM_AK47_Mag"), ERole::Magazine, ESlot::Magazine),
			Part(TEXT("SM_AK47_OpticMount"), ERole::Static, ESlot::Count, ESlot::Optic), Part(TEXT("SM_AK47_UnderRail"), ERole::Static, ESlot::Count, ESlot::Underbarrel),
			Part(TEXT("SM_AK47_SideRail"), ERole::Static, ESlot::Count, ESlot::Laser) };
		SetAnchors(W, FVector(57.0f, 0.f, 9.0f), FVector(57.0f, 0.f, 9.0f), FVector(35.5f, 2.3f, 7.2f), FVector(10.0f, -0.4f, 13.2f), FVector(-3.0f, 0.f, 8.0f),
			FVector(34.0f, 0.f, 4.78f), FVector(12.7f, 0.f, 4.5f));
		W.IronSight = FVector(21.6f, 0.f, 13.3f);
		W.FrontSightX = 53.4f;
		// Open notch far forward on the receiver: the cheek sits on the stock comb, the eye well behind the dust cover.
		W.IronEyeRelief = 38.f;
		W.EjectPort = FVector(13.0f, 1.7f, 8.6f);
		W.ExposedBarrel = 0.f;
		W.BarrelRadius = 0.82f;
		W.MuzzleDeviceLength = 3.4f;
		W.BoltTravel = 7.5f;
		// Support hand at the back of the lower handguard (against the receiver), so the arm isn't stretched across the
		// view to reach the long AK furniture.
		W.LeftHandGrip = FVector(25.5f, -4.4f, 5.0f);
		// The long stamped receiver sits close to the cheek: carried a little further out at the hip, barrel straighter
		// ahead, and held out further for the reload so its side doesn't fill the frame.
		W.HipOffset = FVector(9.f, 8.f, -4.f);
		W.HipRotation = FRotator(-1.f, -2.f, -5.f);
		W.ReloadRotation = FRotator(-1.f, -13.f, 31.f);
		W.ReloadOffset = FVector(15.f, -3.f, -2.f);
		// Tactical sprint rolled less and carried a little further out, so the long curved magazine hangs down
		// below the frame instead of sweeping across the bottom of it.
		W.TacSprintOffset = FVector(30.f, 22.f, -32.f);
		W.TacSprintRotation = FRotator(73.f, 0.f, 82.f);
		W.bRockInMagazine = true;
		W.bRackHandle = true;
		W.FireSound = TEXT("Gun_AK");
		W.Attachments = RifleAttachments;
		W.BodyParts = {
			Box(FVector(30.f, 0, 2.f), FVector(12.f, 5.2f, 5.2f), ColWood, FRotator::ZeroRotator, 0.7f, 0.f),
			Box(FVector(12.f, 0, -8.f), FVector(4.4f, 2.4f, 13.f), FLinearColor(0.09f, 0.05f, 0.03f), FRotator(14.f, 0.f, 0.f), 0.5f, 0.3f)
		};
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 40.f; S.DamageMid = 32.f; S.DamageFar = 27.f; S.RangeNear = 2500.f; S.RangeMid = 4500.f;
		S.RPM = 600.f; S.MagSize = 30; S.ReserveMags = 4;
		S.ReloadTime = 2.4f; S.EmptyReloadTime = 2.9f; S.ADSTime = 0.28f;
		S.MoveSpeedMultiplier = 0.94f; S.ADSMoveSpeedMultiplier = 0.5f; S.HipSpread = 3.6f;
		S.RecoilPitch = 0.68f; S.RecoilYaw = 0.34f; S.RecoilYawBias = -0.06f; S.FirstShotRecoilScale = 1.3f;
		S.VisualKick = 1.35f; S.Penetration = 30.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("MP5");
		W.DisplayName = FText::FromString(TEXT("MP5"));
		W.Description = LOCTEXT("MP5Desc", "经典冲锋枪，开镜迅速、移动灵活，教室近战的王者。");
		W.Class = ECampusWeaponClass::SMG;
		W.FireModes = { ECampusFireMode::Auto, ECampusFireMode::Burst, ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		W.MeshScale = FVector(0.86f);
		RifleAnchors(W, 0.86f);
		// MP5A2: receiver tube, cocking tube, drum sight, slimline handguard; claw mount for optics.
		W.Model = {
			Part(TEXT("SM_MP5_Receiver")), Part(TEXT("SM_MP5_Charging"), ERole::Charging), Part(TEXT("SM_MP5_Handguard")),
			Part(TEXT("SM_MP5_Stock"), ERole::Static, ESlot::Stock), Part(TEXT("SM_MP5_Grip"), ERole::Static, ESlot::RearGrip),
			Part(TEXT("SM_MP5_Mag"), ERole::Magazine, ESlot::Magazine), Part(TEXT("SM_MP5_ClawMount"), ERole::Static, ESlot::Count, ESlot::Optic),
			Part(TEXT("SM_MP5_UnderRail"), ERole::Static, ESlot::Count, ESlot::Underbarrel), Part(TEXT("SM_MP5_SideRail"), ERole::Static, ESlot::Count, ESlot::Laser) };
		W.Anchors.Reset();
		SetAnchors(W, FVector(36.6f, 0.f, 8.6f), FVector(36.6f, 0.f, 8.6f), FVector(26.5f, 2.4f, 8.2f), FVector(6.5f, 0.f, 13.15f), FVector(-6.0f, 0.f, 8.6f),
			FVector(26.0f, 0.f, 4.92f), FVector(11.6f, 0.f, 4.8f));
		W.IronSight = FVector(-2.0f, 0.f, 13.0f);
		W.FrontSightX = 32.8f;
		// Far enough back that the drum frames the front post instead of filling the bottom of the view.
		W.IronEyeRelief = 12.f;
		W.bRackHandle = true;
		W.EjectPort = FVector(12.5f, 1.8f, 9.0f);
		W.ExposedBarrel = 0.f;
		W.BarrelRadius = 0.72f;
		W.MuzzleDeviceLength = 0.f;
		W.LeftHandGrip = FVector(25.f, -4.2f, 5.0f);
		W.MeshScale = FVector(1.f);
		W.FireSound = TEXT("Gun_SMG");
		W.Attachments = RifleAttachments;
		W.HipOffset = FVector(4.f, 7.f, -3.5f);
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 26.f; S.DamageMid = 20.f; S.DamageFar = 16.f; S.RangeNear = 1200.f; S.RangeMid = 2200.f;
		S.RPM = 800.f; S.MagSize = 30; S.ReserveMags = 5;
		S.ReloadTime = 1.9f; S.EmptyReloadTime = 2.3f; S.ADSTime = 0.19f; S.SprintToFireTime = 0.17f; S.EquipTime = 0.45f;
		S.MoveSpeedMultiplier = 1.02f; S.ADSMoveSpeedMultiplier = 0.62f; S.HipSpread = 2.2f; S.MoveSpread = 1.0f;
		S.RecoilPitch = 0.30f; S.RecoilYaw = 0.16f; S.VisualKick = 0.8f; S.ADSZoom = 1.1f; S.Penetration = 15.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("MK14");
		W.DisplayName = FText::FromString(TEXT("MK14 EBR"));
		W.Description = LOCTEXT("MK14Desc", "半自动精确射手步枪，两枪击倒，统治操场与长走廊。");
		W.Class = ECampusWeaponClass::Marksman;
		W.FireModes = { ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		W.MeshTint = FLinearColor(0.16f, 0.14f, 0.09f);
		// EBR chassis (FDE) with full-length top rail, side and bottom rails; 18" barrel and brake.
		W.Model = {
			Part(TEXT("SM_MK14_Chassis")), Part(TEXT("SM_MK14_OpRod"), ERole::Bolt), Part(TEXT("SM_MK14_Stock"), ERole::Static, ESlot::Stock),
			Part(TEXT("SM_MK14_Grip"), ERole::Static, ESlot::RearGrip), Part(TEXT("SM_MK14_Brake"), ERole::Static, ESlot::Muzzle),
			Part(TEXT("SM_MK14_Mag"), ERole::Magazine, ESlot::Magazine),
			Part(TEXT("SM_MK14_RearSight"), ERole::Static, ESlot::Optic),
			Part(TEXT("SM_MK14_FrontSight"), ERole::Static, ESlot::Optic), Part(TEXT("SM_MK14_FrontSightFolded"), ERole::Static, ESlot::Count, ESlot::Optic) };
		SetAnchors(W, FVector(62.0f, 0.f, 9.4f), FVector(36.0f, 0.f, 9.4f), FVector(28.0f, 2.9f, 9.0f), FVector(6.0f, 0.f, 13.15f), FVector(-6.0f, 0.f, 9.0f),
			FVector(28.0f, 0.f, 5.0f), FVector(11.8f, 0.f, 4.4f));
		W.IronSight = FVector(-2.0f, 0.f, 15.05f);
		W.FrontSightX = 33.5f;
		W.IronEyeRelief = 9.5f;
		W.bRackHandle = true;
		// The EBR chassis is twice the width of the magazine and the op rod handle sits under a scope's front ring.
		W.bRackUnderOptic = false;
		W.ReloadHandOut = 1.4f;
		W.EjectPort = FVector(14.0f, 2.7f, 10.4f);
		W.ExposedBarrel = 26.f;
		W.BarrelRadius = 1.05f;
		W.MuzzleDeviceLength = 5.4f;
		W.BoltTravel = 6.f;
		W.LeftHandGrip = FVector(28.f, -4.8f, 5.4f);
		W.FireSound = TEXT("Gun_DMR");
		W.Attachments = RifleAttachments;
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 55.f; S.DamageMid = 48.f; S.DamageFar = 42.f; S.RangeNear = 4000.f; S.RangeMid = 7000.f;
		S.HeadMultiplier = 1.6f; S.RPM = 330.f; S.MagSize = 15; S.ReserveMags = 4;
		S.ReloadTime = 2.6f; S.EmptyReloadTime = 3.0f; S.ADSTime = 0.32f; S.SprintToFireTime = 0.3f;
		S.MoveSpeedMultiplier = 0.92f; S.ADSMoveSpeedMultiplier = 0.48f; S.HipSpread = 4.5f;
		S.RecoilPitch = 1.35f; S.RecoilYaw = 0.25f; S.FirstShotRecoilScale = 1.0f; S.RecoilRecovery = 0.55f;
		S.VisualKick = 1.6f; S.ADSZoom = 1.3f; S.Penetration = 40.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("HK416");
		W.DisplayName = FText::FromString(TEXT("HK416"));
		W.Description = LOCTEXT("HK416Desc", "活塞式突击步枪，后坐更柔和、弹道稳定，适合持续压制。");
		W.Class = ECampusWeaponClass::AssaultRifle;
		W.FireModes = { ECampusFireMode::Auto, ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		// M4 lower and upper receiver with the HK quad rail, FDE furniture and steel magazines.
		W.Model = {
			Shared(TEXT("M4A1"), TEXT("SM_M4A1_Receiver")), Shared(TEXT("M4A1"), TEXT("SM_M4A1_Bolt"), ERole::Bolt), Shared(TEXT("M4A1"), TEXT("SM_M4A1_Charging"), ERole::Charging),
			Part(TEXT("SM_HK416_Grip"), ERole::Static, ESlot::RearGrip), Part(TEXT("SM_HK416_Stock"), ERole::Static, ESlot::Stock),
			Part(TEXT("SM_HK416_Handguard")), Shared(TEXT("M4A1"), TEXT("SM_M4A1_FlashHider"), ERole::Static, ESlot::Muzzle),
			Part(TEXT("SM_HK416_Mag"), ERole::Magazine, ESlot::Magazine),
			Shared(TEXT("M4A1"), TEXT("SM_M4A1_RearSight"), ERole::Static, ESlot::Optic),
			Part(TEXT("SM_HK416_FrontSight"), ERole::Static, ESlot::Optic), Part(TEXT("SM_HK416_FrontSightFolded"), ERole::Static, ESlot::Count, ESlot::Optic) };
		SetAnchors(W, FVector(50.0f, 0.f, 9.5f), FVector(46.1f, 0.f, 9.5f), FVector(38.0f, 2.15f, 9.4f), FVector(6.5f, 0.f, 12.55f), FVector(-1.8f, 0.f, 8.9f),
			FVector(36.0f, 0.f, 7.25f), FVector(12.8f, 0.f, 4.3f));
		W.IronSight = FVector(0.4f, 0.f, 14.45f);
		W.FrontSightX = 42.6f;
		W.IronEyeRelief = 9.5f;
		W.EjectPort = FVector(10.2f, 1.3f, 9.6f);
		W.ExposedBarrel = 3.9f;
		W.BarrelRadius = 0.95f;
		W.MuzzleDeviceLength = 5.6f;
		W.LeftHandGrip = FVector(27.f, -4.3f, 5.4f);
		W.FireSound = TEXT("Gun_AR");
		W.Attachments = RifleAttachments;
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 30.f; S.DamageMid = 25.f; S.DamageFar = 20.f; S.RangeNear = 3200.f; S.RangeMid = 5200.f;
		S.RPM = 760.f; S.MagSize = 30; S.ReserveMags = 4;
		S.ReloadTime = 2.2f; S.EmptyReloadTime = 2.7f; S.ADSTime = 0.25f;
		S.MoveSpeedMultiplier = 0.95f; S.HipSpread = 3.3f;
		S.RecoilPitch = 0.37f; S.RecoilYaw = 0.15f; S.RecoilYawBias = -0.02f; S.FirstShotRecoilScale = 1.35f; S.RecoilRecovery = 0.4f;
		S.VisualKick = 0.95f; S.Penetration = 28.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("UMP45");
		W.DisplayName = FText::FromString(TEXT("UMP45"));
		W.Description = LOCTEXT("UMPDesc", ".45 口径冲锋枪，射速偏慢但单发伤害高，近距离稳定可控。");
		W.Class = ECampusWeaponClass::SMG;
		W.FireModes = { ECampusFireMode::Auto, ECampusFireMode::Burst, ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		W.Model = {
			Part(TEXT("SM_UMP45_Receiver")), Part(TEXT("SM_UMP45_Charging"), ERole::Charging),
			Part(TEXT("SM_UMP45_Stock"), ERole::Static, ESlot::Stock), Part(TEXT("SM_UMP45_Grip"), ERole::Static, ESlot::RearGrip),
			Part(TEXT("SM_UMP45_Mag"), ERole::Magazine, ESlot::Magazine),
			Part(TEXT("SM_UMP45_RearSight"), ERole::Static, ESlot::Optic), Part(TEXT("SM_UMP45_RearSightFolded"), ERole::Static, ESlot::Count, ESlot::Optic),
			Part(TEXT("SM_UMP45_FrontSight"), ERole::Static, ESlot::Optic), Part(TEXT("SM_UMP45_FrontSightFolded"), ERole::Static, ESlot::Count, ESlot::Optic) };
		SetAnchors(W, FVector(36.2f, 0.f, 8.6f), FVector(36.2f, 0.f, 8.6f), FVector(25.75f, 1.95f, 8.6f), FVector(5.0f, 0.f, 12.55f), FVector(-6.0f, 0.f, 8.8f),
			FVector(25.75f, 0.f, 4.65f), FVector(13.2f, 0.f, 4.4f));
		W.IronSight = FVector(-2.6f, 0.f, 14.35f);
		W.FrontSightX = 26.5f;
		W.IronEyeRelief = 11.f;
		W.bRackHandle = true;
		W.EjectPort = FVector(12.0f, 1.8f, 9.2f);
		W.ExposedBarrel = 0.f;
		W.BarrelRadius = 0.85f;
		W.MuzzleDeviceLength = 0.f;
		W.LeftHandGrip = FVector(24.f, -4.4f, 5.6f);
		W.FireSound = TEXT("Gun_SMG");
		W.Attachments = RifleAttachments;
		W.HipOffset = FVector(4.f, 7.f, -3.5f);
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 34.f; S.DamageMid = 26.f; S.DamageFar = 20.f; S.RangeNear = 1100.f; S.RangeMid = 2000.f;
		S.RPM = 650.f; S.MagSize = 25; S.ReserveMags = 5;
		S.ReloadTime = 2.0f; S.EmptyReloadTime = 2.5f; S.ADSTime = 0.2f; S.SprintToFireTime = 0.18f; S.EquipTime = 0.48f;
		S.MoveSpeedMultiplier = 1.0f; S.ADSMoveSpeedMultiplier = 0.6f; S.HipSpread = 2.4f; S.MoveSpread = 1.1f;
		S.RecoilPitch = 0.4f; S.RecoilYaw = 0.13f; S.RecoilYawBias = 0.03f; S.VisualKick = 0.9f; S.ADSZoom = 1.1f; S.Penetration = 18.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("RPK");
		W.DisplayName = FText::FromString(TEXT("RPK"));
		W.Description = LOCTEXT("RPKDesc", "75 发弹鼓轻机枪，火力持久、远距稳定，但开镜和移动都很笨重。");
		W.Class = ECampusWeaponClass::LMG;
		W.FireModes = { ECampusFireMode::Auto, ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		W.MeshTint = FLinearColor(0.05f, 0.048f, 0.045f);
		W.Model = {
			Part(TEXT("SM_RPK_Receiver")), Shared(TEXT("AK47"), TEXT("SM_AK47_Bolt"), ERole::Bolt), Shared(TEXT("AK47"), TEXT("SM_AK47_Handguard")),
			Part(TEXT("SM_RPK_Stock"), ERole::Static, ESlot::Stock), Part(TEXT("SM_RPK_Grip"), ERole::Static, ESlot::RearGrip),
			Shared(TEXT("AK47"), TEXT("SM_AK47_Brake"), ERole::Static, ESlot::Muzzle), Part(TEXT("SM_RPK_Mag"), ERole::Magazine, ESlot::Magazine),
			Shared(TEXT("AK47"), TEXT("SM_AK47_OpticMount"), ERole::Static, ESlot::Count, ESlot::Optic), Shared(TEXT("AK47"), TEXT("SM_AK47_UnderRail"), ERole::Static, ESlot::Count, ESlot::Underbarrel),
			Shared(TEXT("AK47"), TEXT("SM_AK47_SideRail"), ERole::Static, ESlot::Count, ESlot::Laser) };
		SetAnchors(W, FVector(69.0f, 0.f, 9.0f), FVector(69.0f, 0.f, 9.0f), FVector(35.5f, 2.3f, 7.2f), FVector(10.0f, -0.4f, 13.2f), FVector(-3.0f, 0.f, 8.0f),
			FVector(34.0f, 0.f, 4.78f), FVector(12.7f, 0.f, 4.5f));
		W.IronSight = FVector(21.6f, 0.f, 13.3f);
		W.FrontSightX = 65.4f;
		W.IronEyeRelief = 38.f;
		W.EjectPort = FVector(13.0f, 1.7f, 8.6f);
		W.ExposedBarrel = 0.f;
		W.BarrelRadius = 0.95f;
		W.MuzzleDeviceLength = 3.4f;
		W.BoltTravel = 7.5f;
		W.LeftHandGrip = FVector(25.5f, -4.4f, 5.0f);
		W.HipOffset = FVector(9.f, 8.f, -4.5f);
		W.HipRotation = FRotator(-1.f, -2.f, -5.f);
		W.ReloadRotation = FRotator(-1.f, -13.f, 32.f);
		W.ReloadOffset = FVector(15.f, -3.f, -2.f);
		W.TacSprintOffset = FVector(30.f, 22.f, -34.f);
		W.TacSprintRotation = FRotator(73.f, 0.f, 82.f);
		W.bRockInMagazine = true;
		W.bRackHandle = true;
		W.FireSound = TEXT("Gun_AK");
		// The drum is the RPK's magazine: no box magazine options.
		W.Attachments = RifleAttachments.FilterByPredicate([](const FName& Id) { return Id != TEXT("mag_extended") && Id != TEXT("mag_fast"); });
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 36.f; S.DamageMid = 30.f; S.DamageFar = 26.f; S.RangeNear = 3000.f; S.RangeMid = 6000.f;
		S.RPM = 620.f; S.MagSize = 75; S.ReserveMags = 2;
		S.ReloadTime = 4.3f; S.EmptyReloadTime = 4.9f; S.ADSTime = 0.4f; S.SprintToFireTime = 0.34f; S.TacSprintToFireTime = 0.5f; S.EquipTime = 0.8f;
		S.MoveSpeedMultiplier = 0.86f; S.ADSMoveSpeedMultiplier = 0.42f; S.HipSpread = 4.2f; S.MoveSpread = 2.6f;
		S.RecoilPitch = 0.5f; S.RecoilYaw = 0.22f; S.RecoilYawBias = -0.04f; S.FirstShotRecoilScale = 1.15f; S.RecoilRecovery = 0.3f;
		S.SpreadPerShot = 0.25f; S.VisualKick = 1.2f; S.AimStability = 1.15f; S.Penetration = 35.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("SAIGA12");
		W.DisplayName = FText::FromString(TEXT("Saiga-12K"));
		W.Description = LOCTEXT("SaigaDesc", "半自动弹匣式霰弹枪，8 颗弹丸近距离一击毙命，距离一远威力骤减。");
		W.Class = ECampusWeaponClass::Shotgun;
		W.FireModes = { ECampusFireMode::Semi };
		W.Mesh = RifleMesh;
		W.MeshTint = FLinearColor(0.03f, 0.03f, 0.032f);
		W.Model = {
			Part(TEXT("SM_SAIGA12_Receiver")), Shared(TEXT("AK47"), TEXT("SM_AK47_Bolt"), ERole::Bolt), Part(TEXT("SM_SAIGA12_Handguard")),
			Part(TEXT("SM_SAIGA12_Stock"), ERole::Static, ESlot::Stock), Part(TEXT("SM_SAIGA12_Grip"), ERole::Static, ESlot::RearGrip),
			Part(TEXT("SM_SAIGA12_Mag"), ERole::Magazine, ESlot::Magazine),
			Shared(TEXT("AK47"), TEXT("SM_AK47_OpticMount"), ERole::Static, ESlot::Count, ESlot::Optic), Shared(TEXT("AK47"), TEXT("SM_AK47_UnderRail"), ERole::Static, ESlot::Count, ESlot::Underbarrel),
			Shared(TEXT("AK47"), TEXT("SM_AK47_SideRail"), ERole::Static, ESlot::Count, ESlot::Laser) };
		SetAnchors(W, FVector(52.0f, 0.f, 9.0f), FVector(52.0f, 0.f, 9.0f), FVector(35.5f, 2.3f, 7.2f), FVector(10.0f, -0.4f, 13.2f), FVector(-3.0f, 0.f, 8.0f),
			FVector(34.0f, 0.f, 4.78f), FVector(12.7f, 0.f, 3.0f));
		W.IronSight = FVector(21.6f, 0.f, 13.3f);
		W.FrontSightX = 49.2f;
		W.IronEyeRelief = 36.f;
		W.EjectPort = FVector(13.0f, 1.7f, 8.6f);
		W.ExposedBarrel = 0.f;
		W.BarrelRadius = 1.15f;
		W.MuzzleDeviceLength = 0.f;
		W.BoltTravel = 7.5f;
		W.LeftHandGrip = FVector(25.5f, -4.5f, 5.0f);
		W.HipOffset = FVector(9.f, 8.f, -4.f);
		W.HipRotation = FRotator(-1.f, -2.f, -5.f);
		W.ReloadRotation = FRotator(-1.f, -13.f, 32.f);
		W.ReloadOffset = FVector(15.f, -3.f, -2.f);
		W.TacSprintOffset = FVector(30.f, 22.f, -32.f);
		W.TacSprintRotation = FRotator(73.f, 0.f, 82.f);
		W.bRockInMagazine = true;
		W.bRackHandle = true;
		W.FireSound = TEXT("Gun_Shotgun");
		// Choke-less smoothbore: barrel swaps change how tight the pattern holds, which the range modifier stands for.
		W.Attachments = RifleAttachments;
		FCampusWeaponStats& S = W.Stats;
		// Per pellet (8): a centred hit inside 6 m is a kill, past 13 m it takes two or three.
		S.Pellets = 8; S.PelletSpread = 3.4f;
		S.DamageNear = 17.f; S.DamageMid = 11.f; S.DamageFar = 6.f; S.RangeNear = 600.f; S.RangeMid = 1300.f;
		S.HeadMultiplier = 1.25f; S.LimbMultiplier = 0.85f;
		S.RPM = 230.f; S.MagSize = 8; S.ReserveMags = 4;
		S.ReloadTime = 2.6f; S.EmptyReloadTime = 3.1f; S.ADSTime = 0.28f; S.SprintToFireTime = 0.24f;
		S.MoveSpeedMultiplier = 0.95f; S.ADSMoveSpeedMultiplier = 0.55f; S.HipSpread = 1.2f; S.MoveSpread = 0.8f; S.AirSpread = 2.5f;
		S.SpreadPerShot = 0.6f; S.MaxBloom = 1.6f;
		S.RecoilPitch = 2.1f; S.RecoilYaw = 0.55f; S.RecoilYawBias = 0.1f; S.FirstShotRecoilScale = 1.0f; S.RecoilRecovery = 0.55f;
		S.VisualKick = 2.0f; S.ADSZoom = 1.1f; S.Penetration = 8.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("X16");
		W.DisplayName = FText::FromString(TEXT("X16"));
		W.Description = LOCTEXT("X16Desc", "半自动副武器，切枪极快，主武器打空时的救命稻草。");
		W.Class = ECampusWeaponClass::Pistol;
		W.FireModes = { ECampusFireMode::Semi };
		W.Mesh = PistolMesh;
		W.bPistolAnims = true;
		W.bTintMesh = false;
		// Photoscanned service pistol (Poly Haven), re-framed by the weapon kit: frame, recoiling slide, magazine, hammer, trigger.
		W.Model = {
			Part(TEXT("SM_X16_pistol_a")), Part(TEXT("SM_X16_slide_a"), ERole::Slide), Part(TEXT("SM_X16_magazine_loaded"), ERole::Magazine),
			Part(TEXT("SM_X16_hammer_a"), ERole::Hammer), Part(TEXT("SM_X16_trigger_a"), ERole::Trigger), Part(TEXT("SM_X16_Rail"), ERole::Static, ESlot::Count, ESlot::Laser) };
		W.BoltTravel = 2.6f;
		W.LaserRotation = FQuat(FVector::XAxisVector, -HALF_PI).Rotator();
		W.FireSound = TEXT("Gun_Pistol");
		// Measured from the re-framed service pistol: slide X -2.8..11.5, Z 4.6..7.8; accessory rail under the dust cover.
		W.Anchors.Add(ECampusAttachmentSlot::Muzzle, FVector(11.5f, 0.f, 6.0f));
		W.Anchors.Add(ECampusAttachmentSlot::Laser, FVector(5.1f, 0.f, 2.08f));
		W.Anchors.Add(ECampusAttachmentSlot::Optic, FVector(2.2f, 0.f, 7.8f));
		W.Anchors.Add(ECampusAttachmentSlot::Magazine, FVector(-1.4f, 0.f, -6.f));
		W.Anchors.Add(ECampusAttachmentSlot::RearGrip, FVector::ZeroVector);
		W.IronSight = FVector(-1.6f, 0.f, 7.95f);
		W.FrontSightX = 10.4f;
		W.Muzzle = FVector(11.5f, 0.f, 6.0f);
		W.EjectPort = FVector(5.5f, 1.4f, 7.2f);
		W.Attachments = { TEXT("muzzle_suppressor"), TEXT("muzzle_comp"), TEXT("laser_tac"), TEXT("light_tac"), TEXT("optic_reddot"), TEXT("mag_extended"), TEXT("grip_stippled") };
		W.HipOffset = FVector(8.f, 4.f, -4.f);
		W.SprintOffset = FVector(0.f, 1.f, -4.f);
		W.SprintRotation = FRotator(-30.f, -14.f, 14.f);
		W.TacSprintOffset = FVector(28.f, 14.f, -18.f);
		W.TacSprintRotation = FRotator(72.f, 0.f, 95.f);
		W.TacSprintLean = 6.f;
		W.AimDistance = 30.f;
		W.ReloadRotation = FRotator(12.f, -6.f, 30.f);
		W.ReloadOffset = FVector(-4.f, -4.f, 5.f);
		W.bLeftHandIK = false;
		FCampusWeaponStats& S = W.Stats;
		S.DamageNear = 32.f; S.DamageMid = 26.f; S.DamageFar = 20.f; S.RangeNear = 1200.f; S.RangeMid = 2500.f;
		S.HeadMultiplier = 1.5f; S.RPM = 520.f; S.MagSize = 15; S.ReserveMags = 4;
		S.ReloadTime = 1.5f; S.EmptyReloadTime = 1.9f; S.ADSTime = 0.14f; S.SprintToFireTime = 0.12f; S.TacSprintToFireTime = 0.25f; S.EquipTime = 0.35f;
		S.MoveSpeedMultiplier = 1.05f; S.ADSMoveSpeedMultiplier = 0.7f; S.HipSpread = 2.0f; S.MoveSpread = 1.0f;
		S.RecoilPitch = 0.85f; S.RecoilYaw = 0.2f; S.FirstShotRecoilScale = 1.0f; S.RecoilRecovery = 0.6f;
		S.VisualKick = 1.1f; S.ADSZoom = 1.08f; S.Penetration = 10.f;
	}
	{
		FCampusWeaponDef& W = Weapons.AddDefaulted_GetRef();
		W.Id = TEXT("KNIFE");
		W.DisplayName = LOCTEXT("KnifeName", "战术刀");
		W.Description = LOCTEXT("KnifeDesc", "近战副武器：左键轻挥出手快、两刀击倒，右键重刺蓄力、一刀致命。拔出最快、跑得最快，但必须贴身。");
		W.Class = ECampusWeaponClass::Melee;
		W.FireModes = { ECampusFireMode::Semi };
		W.Mesh = PistolMesh;
		// Held in the pistol grip: the handle runs down through the fist along the grip's rake, the blade out of the top.
		W.bPistolAnims = true;
		W.bTintMesh = false;
		W.bLeftHandIK = false;
		// The wrist is cocked forward in a hammer grip: the knife tips 24 degrees forward about the middle of the fist.
		FCampusModelPart Knife = Part(TEXT("SM_KNIFE_Knife"));
		Knife.Transform = FTransform(FRotator(-24.f, 0.f, 0.f), FVector(1.12f, 0.f, -0.53f));
		W.Model = { Knife };
		// Along the blade, out of the fist: the tip is the "muzzle" (the strike reaches past it).
		W.Muzzle = FVector(13.f, 0.f, 15.f);
		W.IronSight = FVector(1.5f, 0.f, 8.f);
		W.EjectPort = FVector(0.f, 0.f, 4.f);
		W.FireSound = NAME_None;
		// Low right, blade forward and up past the knuckles; the edge turned down and out.
		W.HipOffset = FVector(12.f, 9.f, -5.f);
		W.HipRotation = FRotator(-22.f, -12.f, -12.f);
		W.SprintOffset = FVector(-2.f, 2.f, -6.f);
		W.SprintRotation = FRotator(-24.f, -10.f, 18.f);
		W.TacSprintOffset = FVector(24.f, 15.f, -24.f);
		W.TacSprintRotation = FRotator(20.f, 0.f, 70.f);
		W.TacSprintLean = 4.f;
		FCampusWeaponStats& S = W.Stats;
		// Shown on the stat bars as the light slash (two to kill); the stab's reach is RangeNear. The damage, reach and
		// cooldowns that count are in the character's melee.
		S.DamageNear = 55.f; S.DamageMid = 55.f; S.DamageFar = 55.f; S.RangeNear = 200.f; S.RangeMid = 200.f;
		S.HeadMultiplier = 1.f; S.LowerTorsoMultiplier = 1.f; S.LimbMultiplier = 1.f;
		S.RPM = 110.f; S.MagSize = 1; S.ReserveMags = 0;
		S.ReloadTime = 0.f; S.EmptyReloadTime = 0.f; S.ADSTime = 0.1f; S.SprintToFireTime = 0.08f; S.TacSprintToFireTime = 0.14f; S.EquipTime = 0.3f;
		S.MoveSpeedMultiplier = 1.08f; S.ADSMoveSpeedMultiplier = 1.f; S.HipSpread = 0.f; S.MoveSpread = 0.f; S.AirSpread = 0.f;
		S.SpreadPerShot = 0.f; S.MaxBloom = 0.f;
		S.RecoilPitch = 0.f; S.RecoilYaw = 0.f; S.RecoilYawBias = 0.f; S.VisualKick = 0.f; S.ADSZoom = 1.f; S.Penetration = 0.f;
	}
}

const FCampusWeaponDef* UCampusArmory::FindWeapon(FName Id) const
{
	return Weapons.FindByPredicate([Id](const FCampusWeaponDef& W) { return W.Id == Id; });
}

const FCampusAttachmentDef* UCampusArmory::FindAttachment(FName Id) const
{
	return AttachmentDefs.FindByPredicate([Id](const FCampusAttachmentDef& A) { return A.Id == Id; });
}

TArray<const FCampusWeaponDef*> UCampusArmory::GetWeapons(bool bSecondary, bool bIncludeMelee) const
{
	TArray<const FCampusWeaponDef*> Out;
	for (const FCampusWeaponDef& W : Weapons)
	{
		if (W.IsSecondary() == bSecondary && (bIncludeMelee || !W.IsMelee()))
		{
			Out.Add(&W);
		}
	}
	return Out;
}

TArray<const FCampusAttachmentDef*> UCampusArmory::GetAttachmentsForSlot(FName WeaponId, ECampusAttachmentSlot Slot) const
{
	TArray<const FCampusAttachmentDef*> Out;
	if (const FCampusWeaponDef* W = FindWeapon(WeaponId))
	{
		for (const FName& Id : W->Attachments)
		{
			const FCampusAttachmentDef* A = FindAttachment(Id);
			if (A && A->Slot == Slot && W->Anchors.Contains(Slot))
			{
				Out.Add(A);
			}
		}
	}
	return Out;
}

FCampusWeaponStats UCampusArmory::ComputeStats(const FCampusWeaponLoadout& Loadout) const
{
	const FCampusWeaponDef* W = FindWeapon(Loadout.WeaponId);
	if (!W)
	{
		return FCampusWeaponStats();
	}
	FCampusWeaponStats S = W->Stats;
	float MagScale = 1.f;
	for (const FName& Id : Loadout.Attachments)
	{
		const FCampusAttachmentDef* A = FindAttachment(Id);
		if (!A)
		{
			continue;
		}
		const FCampusStatModifiers& M = A->Mods;
		S.DamageNear *= M.Damage;
		S.DamageMid *= M.Damage;
		S.DamageFar *= M.Damage;
		S.RangeNear *= M.Range;
		S.RangeMid *= M.Range;
		S.RecoilPitch *= M.VerticalRecoil;
		S.RecoilYaw *= M.HorizontalRecoil;
		S.HipSpread *= M.HipSpread;
		S.MoveSpread *= M.HipSpread;
		S.ADSTime *= M.ADSTime;
		S.MoveSpeedMultiplier *= M.MoveSpeed;
		S.ADSMoveSpeedMultiplier *= M.ADSMoveSpeed;
		S.ReloadTime *= M.ReloadTime;
		S.EmptyReloadTime *= M.ReloadTime;
		S.SprintToFireTime *= M.SprintToFire;
		S.TacSprintToFireTime *= M.SprintToFire;
		S.AimStability *= M.AimStability;
		S.VisualKick *= M.VisualKick;
		S.Penetration *= M.Penetration;
		MagScale *= M.MagSizeScale;
		if (M.ZoomOverride > 0.f)
		{
			S.ADSZoom = M.ZoomOverride;
		}
		if (M.bSuppressor)
		{
			S.bSuppressed = true;
		}
	}
	S.MagSize = FMath::Max(1, FMath::RoundToInt(S.MagSize * MagScale));
	return S;
}

void UCampusArmory::Sanitize(FCampusWeaponLoadout& Loadout, bool bSecondary) const
{
	const FCampusWeaponDef* W = FindWeapon(Loadout.WeaponId);
	if (!W || W->IsSecondary() != bSecondary)
	{
		TArray<const FCampusWeaponDef*> Options = GetWeapons(bSecondary);
		Loadout.WeaponId = Options.Num() > 0 ? Options[0]->Id : NAME_None;
		Loadout.Attachments.Reset();
		W = FindWeapon(Loadout.WeaponId);
	}
	if (!W)
	{
		return;
	}

	TSet<ECampusAttachmentSlot> UsedSlots;
	TArray<FName> Clean;
	for (const FName& Id : Loadout.Attachments)
	{
		const FCampusAttachmentDef* A = FindAttachment(Id);
		if (!A || !W->Attachments.Contains(Id) || !W->Anchors.Contains(A->Slot) || UsedSlots.Contains(A->Slot))
		{
			continue;
		}
		UsedSlots.Add(A->Slot);
		Clean.Add(Id);
	}
	Loadout.Attachments = MoveTemp(Clean);
}

void UCampusArmory::SanitizeLoadout(FCampusLoadout& Loadout) const
{
	Sanitize(Loadout.Primary, false);
	Sanitize(Loadout.Secondary, true);
	if (Loadout.Lethal >= ECampusThrowable::Count || !CampusPerks::IsLethalThrowable(Loadout.Lethal))
	{
		Loadout.Lethal = ECampusThrowable::Frag;
	}
	if (Loadout.Tactical >= ECampusThrowable::Count || CampusPerks::IsLethalThrowable(Loadout.Tactical))
	{
		Loadout.Tactical = ECampusThrowable::Flash;
	}
	CampusPerks::Sanitize(Loadout.Perks);
}

bool UCampusArmory::SetAttachment(FCampusWeaponLoadout& Loadout, FName AttachmentId, ECampusAttachmentSlot Slot) const
{
	const FName Existing = GetAttachmentInSlot(Loadout, Slot);
	if (!Existing.IsNone())
	{
		Loadout.Attachments.Remove(Existing);
	}
	if (!AttachmentId.IsNone())
	{
		Loadout.Attachments.Add(AttachmentId);
	}
	return true;
}

int32 UCampusArmory::CountModdableSlots(FName WeaponId) const
{
	int32 Count = 0;
	for (ECampusAttachmentSlot Slot : TEnumRange<ECampusAttachmentSlot>())
	{
		if (GetAttachmentsForSlot(WeaponId, Slot).Num() > 0)
		{
			++Count;
		}
	}
	return Count;
}

FName UCampusArmory::GetAttachmentInSlot(const FCampusWeaponLoadout& Loadout, ECampusAttachmentSlot Slot) const
{
	for (const FName& Id : Loadout.Attachments)
	{
		const FCampusAttachmentDef* A = FindAttachment(Id);
		if (A && A->Slot == Slot)
		{
			return Id;
		}
	}
	return NAME_None;
}

const FCampusAttachmentDef* UCampusArmory::GetOptic(const FCampusWeaponLoadout& Loadout) const
{
	return FindAttachment(GetAttachmentInSlot(Loadout, ECampusAttachmentSlot::Optic));
}

float UCampusArmory::GetBarrelExtension(const FCampusWeaponLoadout& Loadout) const
{
	const FCampusAttachmentDef* A = FindAttachment(GetAttachmentInSlot(Loadout, ECampusAttachmentSlot::Barrel));
	return A ? A->MuzzleExtension : 0.f;
}

float UCampusArmory::GetMuzzleDeviceExtension(const FCampusWeaponLoadout& Loadout) const
{
	const FCampusAttachmentDef* A = FindAttachment(GetAttachmentInSlot(Loadout, ECampusAttachmentSlot::Muzzle));
	return A ? A->MuzzleExtension : 0.f;
}

bool UCampusArmory::HasLaser(const FCampusWeaponLoadout& Loadout) const
{
	const FCampusAttachmentDef* A = FindAttachment(GetAttachmentInSlot(Loadout, ECampusAttachmentSlot::Laser));
	return A && A->bLaser;
}

FCampusLoadout UCampusArmory::MakeDefaultLoadout(int32 Index) const
{
	FCampusLoadout L;
	L.Secondary.WeaponId = TEXT("X16");
	switch (Index)
	{
	case 1:
		L.Name = TEXT("近战突击");
		L.Primary.WeaponId = TEXT("MP5");
		L.Primary.Attachments = { TEXT("grip_stippled"), TEXT("stock_tac"), TEXT("mag_fast"), TEXT("muzzle_flash") };
		L.Lethal = ECampusThrowable::Frag;
		L.Tactical = ECampusThrowable::Flash;
		L.Perks = { ECampusPerk::Ghost, ECampusPerk::ColdBlooded, ECampusPerk::Ninja };
		break;
	case 2:
		L.Name = TEXT("远程压制");
		L.Primary.WeaponId = TEXT("MK14");
		L.Primary.Attachments = { TEXT("optic_sniper"), TEXT("muzzle_suppressor"), TEXT("stock_heavy"), TEXT("grip_vertical") };
		L.Secondary.Attachments = { TEXT("optic_reddot") };
		L.Lethal = ECampusThrowable::Thermite;
		L.Tactical = ECampusThrowable::Smoke;
		L.Perks = { ECampusPerk::FlakJacket, ECampusPerk::TacMask, ECampusPerk::Scavenger };
		break;
	default:
		L.Name = TEXT("标准突击");
		L.Primary.WeaponId = TEXT("M4A1");
		L.Primary.Attachments = { TEXT("optic_reddot"), TEXT("grip_vertical"), TEXT("muzzle_comp") };
		L.Lethal = ECampusThrowable::Frag;
		L.Tactical = ECampusThrowable::Smoke;
		L.Perks = { ECampusPerk::Lightweight, ECampusPerk::FastHands, ECampusPerk::QuickFix };
		break;
	}
	SanitizeLoadout(L);
	return L;
}

FCampusLoadout UCampusArmory::MakeRandomLoadout(FRandomStream& Stream) const
{
	FCampusLoadout L;
	L.Name = TEXT("Gunfight");
	TArray<const FCampusWeaponDef*> Primaries = GetWeapons(false);
	// Everyone in a gunfight round gets the same loadout, bots included: a sidearm they can shoot back with.
	TArray<const FCampusWeaponDef*> Secondaries = GetWeapons(true, false);
	if (Primaries.Num() > 0)
	{
		L.Primary.WeaponId = Primaries[Stream.RandRange(0, Primaries.Num() - 1)]->Id;
	}
	if (Secondaries.Num() > 0)
	{
		L.Secondary.WeaponId = Secondaries[Stream.RandRange(0, Secondaries.Num() - 1)]->Id;
	}
	const int32 Count = Stream.RandRange(1, 3);
	for (int32 i = 0; i < Count; ++i)
	{
		const ECampusAttachmentSlot Slot = (ECampusAttachmentSlot)Stream.RandRange(0, (int32)ECampusAttachmentSlot::Count - 1);
		TArray<const FCampusAttachmentDef*> Options = GetAttachmentsForSlot(L.Primary.WeaponId, Slot);
		if (Options.Num() > 0)
		{
			SetAttachment(L.Primary, Options[Stream.RandRange(0, Options.Num() - 1)]->Id, Slot);
		}
	}
	L.Lethal = Stream.FRand() < 0.5f ? ECampusThrowable::Frag : ECampusThrowable::Thermite;
	L.Tactical = Stream.FRand() < 0.5f ? ECampusThrowable::Flash : ECampusThrowable::Smoke;
	for (int32 Slot = 0; Slot < CampusPerks::NumSlots; ++Slot)
	{
		const TArray<ECampusPerk> Options = CampusPerks::PerksForSlot(Slot);
		L.Perks.Add(Options[Stream.RandRange(0, Options.Num() - 1)]);
	}
	SanitizeLoadout(L);
	return L;
}

FCampusDisplayStats UCampusArmory::ToDisplay(const FCampusWeaponStats& S)
{
	FCampusDisplayStats D;
	const int32 ShotsToKill = FMath::CeilToInt(100.f / FMath::Max(S.DamageNear, 1.f));
	D.Damage = FMath::Clamp((7.f - ShotsToKill) / 5.f, 0.05f, 1.f);
	D.FireRate = FMath::Clamp(S.RPM / 1000.f, 0.05f, 1.f);
	D.Range = FMath::Clamp(S.RangeNear / 5000.f, 0.05f, 1.f);
	D.Accuracy = FMath::Clamp(1.f - (S.HipSpread / 6.f) * 0.45f - (S.RecoilPitch / 1.6f) * 0.45f, 0.05f, 1.f);
	D.Mobility = FMath::Clamp((S.MoveSpeedMultiplier - 0.85f) / 0.25f * 0.55f + (1.f - S.ADSTime / 0.4f) * 0.45f, 0.05f, 1.f);
	D.Control = FMath::Clamp(1.f - (S.RecoilPitch * (1.f + S.RecoilYaw)) / 1.8f, 0.05f, 1.f);
	return D;
}

void UCampusArmory::DescribeModifiers(const FCampusAttachmentDef& Def, TArray<FText>& OutPros, TArray<FText>& OutCons)
{
	const FCampusStatModifiers& M = Def.Mods;
	auto Check = [&OutPros, &OutCons](float Value, bool bHigherIsBetter, const FText& Label)
	{
		if (FMath::IsNearlyEqual(Value, 1.f, 0.001f))
		{
			return;
		}
		const bool bGood = (Value > 1.f) == bHigherIsBetter;
		(bGood ? OutPros : OutCons).Add(Label);
	};
	Check(M.Damage, true, LOCTEXT("ModDamage", "伤害"));
	Check(M.Range, true, LOCTEXT("ModRange", "伤害距离"));
	Check(M.VerticalRecoil, false, LOCTEXT("ModVRecoil", "垂直后坐力控制"));
	Check(M.HorizontalRecoil, false, LOCTEXT("ModHRecoil", "水平后坐力控制"));
	Check(M.HipSpread, false, LOCTEXT("ModHip", "腰射精准度"));
	Check(M.ADSTime, false, LOCTEXT("ModADS", "开镜速度"));
	Check(M.MoveSpeed, true, LOCTEXT("ModMove", "移动速度"));
	Check(M.ADSMoveSpeed, true, LOCTEXT("ModADSMove", "开镜移动速度"));
	Check(M.ReloadTime, false, LOCTEXT("ModReload", "换弹速度"));
	Check(M.SprintToFire, false, LOCTEXT("ModS2F", "冲刺后射击速度"));
	Check(M.AimStability, true, LOCTEXT("ModStability", "瞄准稳定性"));
	Check(M.VisualKick, false, LOCTEXT("ModKick", "枪口跳动控制"));
	Check(M.Penetration, true, LOCTEXT("ModPen", "穿透力"));
	Check(M.MagSizeScale, true, LOCTEXT("ModMag", "弹匣容量"));
	if (M.ZoomOverride > 1.5f)
	{
		OutPros.Add(LOCTEXT("ModZoom", "高倍放大"));
	}
	if (M.bSuppressor)
	{
		OutPros.Add(LOCTEXT("ModSuppressed", "射击时不在雷达上暴露"));
		OutPros.Add(LOCTEXT("ModSound", "枪声抑制"));
	}
	if (Def.bLaser)
	{
		OutCons.Add(LOCTEXT("ModLaserVisible", "激光会暴露位置"));
	}
}

#undef LOCTEXT_NAMESPACE
