#include "Game/CampusMaps.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "CampusMaps"

const TCHAR* UCampusMapCatalog::PackagePath = TEXT("/Game/Campus/Maps/DA_MapCatalog.DA_MapCatalog");

const FCampusMapPreviewData* UCampusMapCatalog::Find(ECampusMap Map) const
{
	return Maps.FindByPredicate([Map](const FCampusMapPreviewData& D) { return D.Map == Map; });
}

const UCampusMapCatalog* UCampusMapCatalog::Get()
{
	static TWeakObjectPtr<UCampusMapCatalog> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UCampusMapCatalog>(nullptr, PackagePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return Cached.Get();
}

namespace CampusMaps
{
	namespace
	{
		TArray<FMapInfo> MakeTable()
		{
			TArray<FMapInfo> T;
			T.SetNum((int32)ECampusMap::Count);
			{
				FMapInfo& M = T[(int32)ECampusMap::Campus];
				M.Id = ECampusMap::Campus;
				M.Package = TEXT("/Game/Campus/Maps/Map_Campus");
				M.Name = LOCTEXT("CampusName", "光華國中");
				M.Latin = LOCTEXT("CampusLatin", "GUANGHUA JUNIOR HIGH");
				M.Location = LOCTEXT("CampusLoc", "台湾 · 新北市");
				M.Description = LOCTEXT("CampusDesc", "行政大楼穿堂、双层教学大楼走廊、升旗台中庭、篮球场与福利社、操场司令台。三线路设计，篮球场为枪战场地。");
				M.Lanes = LOCTEXT("CampusLanes", "三线路全校区  ·  中远距离");
				M.DefaultTime = ECampusTimeOfDay::Noon;
				// Subtropical: mostly bright, often hazy-cloudy, a fair share of showers.
				const float W[] = { 42.f, 22.f, 10.f, 12.f, 6.f, 8.f };
				const float D[] = { 18.f, 34.f, 28.f, 13.f, 7.f };
				FMemory::Memcpy(M.WeatherOdds, W, sizeof(W));
				FMemory::Memcpy(M.TimeOdds, D, sizeof(D));
			}
			{
				FMapInfo& M = T[(int32)ECampusMap::Hotel];
				M.Id = ECampusMap::Hotel;
				M.Package = TEXT("/Game/Campus/Maps/Map_Hotel");
				M.Name = LOCTEXT("HotelName", "天际酒店");
				M.Latin = LOCTEXT("HotelLatin", "SKYLINE HOTEL ROOFTOP");
				M.Location = LOCTEXT("HotelLoc", "台北 · 信义区  ·  五十二楼顶");
				M.Description = LOCTEXT("HotelDesc", "五十二层酒店楼顶：无边泳池甲板、玻璃空中酒吧、设备层与冷却塔、直升机坪。紧凑三线路加楼下服务走廊绕后，适合快节奏冲锋。");
				M.Lanes = LOCTEXT("HotelLanes", "紧凑三线路 + 立体绕后  ·  近中距离");
				M.DefaultTime = ECampusTimeOfDay::Dusk;
				const float W[] = { 40.f, 22.f, 9.f, 12.f, 10.f, 7.f };
				const float D[] = { 8.f, 10.f, 14.f, 46.f, 22.f };
				FMemory::Memcpy(M.WeatherOdds, W, sizeof(W));
				FMemory::Memcpy(M.TimeOdds, D, sizeof(D));
			}
			{
				FMapInfo& M = T[(int32)ECampusMap::HQ];
				M.Id = ECampusMap::HQ;
				M.Package = TEXT("/Game/Campus/Maps/Map_MainMenu");
				M.Name = LOCTEXT("HQName", "特勤处");
				M.Latin = LOCTEXT("HQLatin", "SPECIAL OPERATIONS HQ");
				M.Location = LOCTEXT("HQLoc", "战术作战中心");
				M.DefaultTime = ECampusTimeOfDay::Night;
				const float W[] = { 60.f, 25.f, 0.f, 15.f, 0.f, 0.f };
				const float D[] = { 0.f, 0.f, 0.f, 0.f, 1.f };
				FMemory::Memcpy(M.WeatherOdds, W, sizeof(W));
				FMemory::Memcpy(M.TimeOdds, D, sizeof(D));
				M.bPlayable = false;
				M.bGunfight = false;
			}
			return T;
		}

		template <int32 N>
		int32 Roll(const float (&Odds)[N], FRandomStream& Stream)
		{
			float Total = 0.f;
			for (float O : Odds)
			{
				Total += FMath::Max(O, 0.f);
			}
			float R = Stream.FRandRange(0.f, FMath::Max(Total, KINDA_SMALL_NUMBER));
			for (int32 i = 0; i < N; ++i)
			{
				R -= FMath::Max(Odds[i], 0.f);
				if (R <= 0.f && Odds[i] > 0.f)
				{
					return i;
				}
			}
			return 0;
		}
	}

	const FMapInfo& Get(ECampusMap Map)
	{
		static const TArray<FMapInfo> Table = MakeTable();
		return Table[FMath::Clamp((int32)Map, 0, Table.Num() - 1)];
	}

	TArray<ECampusMap> Playable()
	{
		TArray<ECampusMap> Out;
		for (int32 i = 0; i < (int32)ECampusMap::Count; ++i)
		{
			if (Get((ECampusMap)i).bPlayable)
			{
				Out.Add((ECampusMap)i);
			}
		}
		return Out;
	}

	ECampusMap FromPackage(const FString& PackageName)
	{
		const FString Short = FPackageName::GetShortName(PackageName);
		for (int32 i = 0; i < (int32)ECampusMap::Count; ++i)
		{
			if (Short.Contains(FPackageName::GetShortName(Get((ECampusMap)i).Package)))
			{
				return (ECampusMap)i;
			}
		}
		return ECampusMap::Campus;
	}

	ECampusWeather ResolveWeather(ECampusMap Map, int32 Choice, FRandomStream& Stream)
	{
		if (Choice >= 0 && Choice < (int32)ECampusWeather::Count)
		{
			return (ECampusWeather)Choice;
		}
		return (ECampusWeather)Roll(Get(Map).WeatherOdds, Stream);
	}

	ECampusTimeOfDay ResolveTime(ECampusMap Map, int32 Choice, FRandomStream& Stream)
	{
		if (Choice >= 0 && Choice < (int32)ECampusTimeOfDay::Count)
		{
			return (ECampusTimeOfDay)Choice;
		}
		if (Choice == -2)
		{
			return Get(Map).DefaultTime;
		}
		return (ECampusTimeOfDay)Roll(Get(Map).TimeOdds, Stream);
	}

	FText WeatherName(ECampusWeather Weather)
	{
		switch (Weather)
		{
		case ECampusWeather::Clear: return LOCTEXT("WClear", "晴朗");
		case ECampusWeather::Cloudy: return LOCTEXT("WCloudy", "多云");
		case ECampusWeather::Overcast: return LOCTEXT("WOvercast", "阴天");
		case ECampusWeather::Rain: return LOCTEXT("WRain", "小雨");
		case ECampusWeather::Storm: return LOCTEXT("WStorm", "雷暴");
		case ECampusWeather::Fog: return LOCTEXT("WFog", "浓雾");
		default: return FText::GetEmpty();
		}
	}

	FText TimeName(ECampusTimeOfDay Time)
	{
		switch (Time)
		{
		case ECampusTimeOfDay::Morning: return LOCTEXT("TMorning", "清晨");
		case ECampusTimeOfDay::Noon: return LOCTEXT("TNoon", "正午");
		case ECampusTimeOfDay::Afternoon: return LOCTEXT("TAfternoon", "午后");
		case ECampusTimeOfDay::Dusk: return LOCTEXT("TDusk", "黄昏");
		case ECampusTimeOfDay::Night: return LOCTEXT("TNight", "夜晚");
		default: return FText::GetEmpty();
		}
	}

	FText ConditionsLine(ECampusWeather Weather, ECampusTimeOfDay Time)
	{
		return FText::Format(LOCTEXT("Conditions", "{0}  ·  {1}"), TimeName(Time), WeatherName(Weather));
	}
}

#undef LOCTEXT_NAMESPACE
