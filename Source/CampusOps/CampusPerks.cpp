#include "CampusPerks.h"

#define LOCTEXT_NAMESPACE "CampusPerks"

namespace
{
	using EPerk = ECampusPerk;
	using EGroup = ECampusPerkGroup;

	const TArray<FCampusPerkInfo>& Catalogue()
	{
		static TArray<FCampusPerkInfo> Perks;
		if (Perks.Num() == 0)
		{
			auto Add = [](EPerk Perk, int32 Slot, EGroup Group, const FText& Name, const FText& Desc)
			{
				FCampusPerkInfo& I = Perks.AddDefaulted_GetRef();
				I.Perk = Perk;
				I.Slot = Slot;
				I.Group = Group;
				I.Name = Name;
				I.Description = Desc;
			};
			Add(EPerk::None, 0, EGroup::Enforcer, LOCTEXT("None", "无"), FText::GetEmpty());

			Add(EPerk::Lightweight, 0, EGroup::Enforcer, LOCTEXT("Lightweight", "轻装上阵"), LOCTEXT("LightweightDesc", "移动与冲刺速度 +7%，滑铲更远。"));
			Add(EPerk::Ghost, 0, EGroup::Recon, LOCTEXT("Ghost", "幽灵"), LOCTEXT("GhostDesc", "未消音开火时暴露位置的范围 -60%，不会被敌方 AI 互相通报。"));
			Add(EPerk::FlakJacket, 0, EGroup::Strategist, LOCTEXT("Flak", "防爆衣"), LOCTEXT("FlakDesc", "爆炸伤害 -50%，铝热剂灼烧伤害 -60%，不会被爆炸掀飞。"));
			Add(EPerk::Bandolier, 0, EGroup::Strategist, LOCTEXT("Bandolier", "子弹带"), LOCTEXT("BandolierDesc", "额外携带 1 枚致命和 1 枚战术投掷物，主副武器各多 1 个弹匣。"));

			Add(EPerk::FastHands, 1, EGroup::Enforcer, LOCTEXT("FastHands", "快手"), LOCTEXT("FastHandsDesc", "换弹速度 +25%，切枪与投掷速度 +30%。"));
			Add(EPerk::Dexterity, 1, EGroup::Enforcer, LOCTEXT("Dexterity", "灵巧"), LOCTEXT("DexterityDesc", "冲刺后射击速度 +30%，跳跃与滑铲中射击的散布 -50%。"));
			Add(EPerk::ColdBlooded, 1, EGroup::Recon, LOCTEXT("ColdBlooded", "冷血"), LOCTEXT("ColdBloodedDesc", "敌方 AI 发现你后需要更久才能开火，瞄准你时误差更大。"));
			Add(EPerk::TacMask, 1, EGroup::Strategist, LOCTEXT("TacMask", "战术面罩"), LOCTEXT("TacMaskDesc", "震撼弹的致盲与耳鸣效果 -65%。"));

			Add(EPerk::QuickFix, 2, EGroup::Enforcer, LOCTEXT("QuickFix", "强心剂"), LOCTEXT("QuickFixDesc", "受伤后更早开始回血，回血速度 +40%；击杀后立即开始回血。"));
			Add(EPerk::Ninja, 2, EGroup::Recon, LOCTEXT("Ninja", "忍者"), LOCTEXT("NinjaDesc", "脚步声对敌人静音，敌方 AI 听不到你的奔跑与冲刺。"));
			Add(EPerk::HighAlert, 2, EGroup::Recon, LOCTEXT("HighAlert", "警觉"), LOCTEXT("HighAlertDesc", "视野外的敌人看到你时，屏幕边缘会朝其方向闪烁。"));
			Add(EPerk::Scavenger, 2, EGroup::Strategist, LOCTEXT("Scavenger", "拾荒者"), LOCTEXT("ScavengerDesc", "每次击杀为主副武器各补充 1 个弹匣，每两次击杀补充 1 枚致命投掷物。"));
		}
		return Perks;
	}
}

namespace CampusPerks
{
	const FCampusPerkInfo& Info(ECampusPerk Perk)
	{
		const TArray<FCampusPerkInfo>& All = Catalogue();
		const FCampusPerkInfo* Found = All.FindByPredicate([Perk](const FCampusPerkInfo& I) { return I.Perk == Perk; });
		return Found ? *Found : All[0];
	}

	TArray<ECampusPerk> PerksForSlot(int32 Slot)
	{
		TArray<ECampusPerk> Out;
		for (const FCampusPerkInfo& I : Catalogue())
		{
			if (I.Perk != ECampusPerk::None && I.Slot == Slot)
			{
				Out.Add(I.Perk);
			}
		}
		return Out;
	}

	ECampusPerk DefaultPerk(int32 Slot)
	{
		switch (Slot)
		{
		case 0: return ECampusPerk::Lightweight;
		case 1: return ECampusPerk::FastHands;
		default: return ECampusPerk::QuickFix;
		}
	}

	FLinearColor GroupColor(ECampusPerkGroup Group)
	{
		switch (Group)
		{
		case ECampusPerkGroup::Enforcer: return FLinearColor(0.95f, 0.22f, 0.16f);
		case ECampusPerkGroup::Recon: return FLinearColor(0.22f, 0.58f, 1.0f);
		case ECampusPerkGroup::Strategist: return FLinearColor(0.32f, 0.86f, 0.36f);
		default: return FLinearColor::White;
		}
	}

	FText GroupName(ECampusPerkGroup Group)
	{
		switch (Group)
		{
		case ECampusPerkGroup::Enforcer: return LOCTEXT("GEnforcer", "突击");
		case ECampusPerkGroup::Recon: return LOCTEXT("GRecon", "侦察");
		case ECampusPerkGroup::Strategist: return LOCTEXT("GStrategist", "战略");
		default: return FText::GetEmpty();
		}
	}

	FText SpecialtyName(ECampusPerkGroup Group)
	{
		switch (Group)
		{
		case ECampusPerkGroup::Enforcer: return LOCTEXT("SEnforcer", "突击专精 · 猛攻");
		case ECampusPerkGroup::Recon: return LOCTEXT("SRecon", "侦察专精 · 洞察");
		case ECampusPerkGroup::Strategist: return LOCTEXT("SStrategist", "战略专精 · 军需官");
		default: return LOCTEXT("SNone", "无专精");
		}
	}

	FText SpecialtyDescription(ECampusPerkGroup Group)
	{
		switch (Group)
		{
		case ECampusPerkGroup::Enforcer: return LOCTEXT("SEnforcerDesc", "击杀后 5 秒内移动速度 +10%，并立即开始回血。");
		case ECampusPerkGroup::Recon: return LOCTEXT("SReconDesc", "重生后与每次击杀后，25 米内的敌人会被透视标记 3 秒。");
		case ECampusPerkGroup::Strategist: return LOCTEXT("SStrategistDesc", "致命与战术投掷物每 25 秒各自动补充 1 枚。");
		default: return LOCTEXT("SNoneDesc", "三个技能属于同一组时获得该组的专精加成。");
		}
	}

	ECampusPerkGroup GetSpecialty(const TArray<ECampusPerk>& Perks)
	{
		if (Perks.Num() < NumSlots)
		{
			return ECampusPerkGroup::Count;
		}
		const ECampusPerkGroup First = Info(Perks[0]).Group;
		for (int32 i = 0; i < NumSlots; ++i)
		{
			if (Perks[i] == ECampusPerk::None || Info(Perks[i]).Group != First)
			{
				return ECampusPerkGroup::Count;
			}
		}
		return First;
	}

	void Sanitize(TArray<ECampusPerk>& Perks)
	{
		Perks.SetNum(NumSlots);
		for (int32 i = 0; i < NumSlots; ++i)
		{
			if (Perks[i] == ECampusPerk::None || Info(Perks[i]).Slot != i || Info(Perks[i]).Perk != Perks[i])
			{
				Perks[i] = DefaultPerk(i);
			}
		}
	}

	bool IsLethalThrowable(ECampusThrowable Type)
	{
		return Type == ECampusThrowable::Frag || Type == ECampusThrowable::Thermite;
	}
}

#undef LOCTEXT_NAMESPACE
