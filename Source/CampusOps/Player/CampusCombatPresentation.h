#pragma once

#include "CoreMinimal.h"
#include "Game/CampusGameState.h"

namespace CampusCombatPresentation
{
	inline FString CleanPlayerName(const FString& Name)
	{
		FString Clean;
		for (const TCHAR Ch : Name)
		{
			// player names stay on one line and cannot reverse adjacent HUD text
			if (Ch >= 32 && !(Ch >= 127 && Ch <= 159) && Ch != 0x061c && Ch != 0x200e && Ch != 0x200f
				&& !(Ch >= 0x2028 && Ch <= 0x202e) && !(Ch >= 0x2066 && Ch <= 0x2069))
			{
				Clean.AppendChar(Ch);
			}
		}
		Clean.TrimStartAndEndInline();
		if (Clean.Len() > 64)
		{
			int32 End = 63;
			if (Clean[End - 1] >= 0xd800 && Clean[End - 1] <= 0xdbff) --End;
			Clean = Clean.Left(End) + TEXT("…");
		}
		return Clean.IsEmpty() ? NSLOCTEXT("CampusHUD", "UnknownPlayer", "未知玩家").ToString() : Clean;
	}

	inline int32 MedalPriority(ECampusScoreEvent Event)
	{
		switch (Event)
		{
		case ECampusScoreEvent::Streak5: return 8;
		case ECampusScoreEvent::TripleKill: return 7;
		case ECampusScoreEvent::FirstBlood: return 6;
		case ECampusScoreEvent::DoubleKill: return 5;
		case ECampusScoreEvent::Revenge: return 4;
		case ECampusScoreEvent::Longshot: return 3;
		case ECampusScoreEvent::Melee: return 2;
		case ECampusScoreEvent::Headshot: return 1;
		default: return 0;
		}
	}

	struct FMedal
	{
		ECampusScoreEvent Event = ECampusScoreEvent::Kill;
		float EarnedTime = 0.f;
		int32 Count = 1;
	};

	class FMedalQueue
	{
	public:
		static constexpr float DisplayTime = 1.65f;
		static constexpr int32 MaxPending = 4;

		void Push(ECampusScoreEvent Event, float Time)
		{
			if (MedalPriority(Event) == 0) return;
			RemoveExpired(Time);
			for (FMedal& Medal : Pending)
			{
				if (Medal.Event == Event)
				{
					Medal.Count = FMath::Min(Medal.Count + 1, 99);
					Medal.EarnedTime = Time;
					return;
				}
			}
			Pending.Add({ Event, Time, 1 });
			Pending.StableSort([](const FMedal& A, const FMedal& B) { return MedalPriority(A.Event) > MedalPriority(B.Event); });
			if (Pending.Num() > MaxPending) Pending.SetNum(MaxPending);
		}

		bool Tick(float Time)
		{
			RemoveExpired(Time);
			if (bActive && Time - StartTime >= DisplayTime) bActive = false;
			// collect all score events from one kill before choosing the leading medal
			if (bActive || Pending.IsEmpty() || Time - Pending[0].EarnedTime < 0.10f) return false;
			Active = Pending[0];
			Pending.RemoveAt(0);
			StartTime = Time;
			bActive = true;
			return true;
		}

		void Reset() { Pending.Reset(); bActive = false; StartTime = -100.f; }
		const FMedal* GetActive() const { return bActive ? &Active : nullptr; }
		float GetStartTime() const { return StartTime; }
		int32 NumPending() const { return Pending.Num(); }

	private:
		void RemoveExpired(float Time)
		{
			Pending.RemoveAll([Time](const FMedal& Medal) { return Time - Medal.EarnedTime > 6.f; });
		}
		TArray<FMedal> Pending;
		FMedal Active;
		float StartTime = -100.f;
		bool bActive = false;
	};

	inline float CenteredIconTop(float TextTop, float InkTop, float InkBottom, float IconSize, float AlphaTop = 0.f, float AlphaBottom = 1.f)
	{
		return TextTop + (InkTop + InkBottom) * 0.5f - IconSize * (AlphaTop + AlphaBottom) * 0.5f;
	}

	struct FMedalMotion
	{
		float Alpha;
		float TitleAlpha;
		float DetailAlpha;
		float BadgeScale;
		float Reveal;
		float OffsetY;
	};

	inline FMedalMotion MedalMotion(float Age)
	{
		auto Ease = [](float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); };
		const float Entry = Ease(Age / 0.12f);
		const float Settle = Ease((Age - 0.12f) / 0.12f);
		const float Exit = Ease((Age - 1.25f) / (FMedalQueue::DisplayTime - 1.25f));
		const float Alpha = Entry * (1.f - Exit);
		return {
			Alpha,
			Ease((Age - 0.055f) / 0.105f) * (1.f - Exit),
			Ease((Age - 0.10f) / 0.14f) * (1.f - Exit),
			Age < 0.12f ? FMath::Lerp(0.86f, 1.04f, Entry) : FMath::Lerp(1.04f, 1.f, Settle),
			Ease((Age - 0.035f) / 0.165f),
			-7.f * (1.f - Ease(Age / 0.18f)) - 3.f * Exit
		};
	}
}
