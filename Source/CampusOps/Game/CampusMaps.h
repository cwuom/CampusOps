#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CampusTypes.h"
#include "Map/CampusSchoolBuilder.h"
#include "CampusMaps.generated.h"

/** What the menu shows of a map that is not loaded: its minimap, written by the bootstrap when it builds the map. */
USTRUCT()
struct FCampusMapPreviewData
{
	GENERATED_BODY()

	UPROPERTY() ECampusMap Map = ECampusMap::Campus;
	UPROPERTY() TArray<FCampusMapRect> Rects;
	UPROPERTY() FBox PlayableBounds = FBox(ForceInit);
};

UCLASS()
class CAMPUSOPS_API UCampusMapCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY() TArray<FCampusMapPreviewData> Maps;

	const FCampusMapPreviewData* Find(ECampusMap Map) const;

	static const TCHAR* PackagePath;
	static const UCampusMapCatalog* Get();
};

/** Static facts about each map, and how its weather and time of day are rolled. */
namespace CampusMaps
{
	struct FMapInfo
	{
		ECampusMap Id = ECampusMap::Campus;
		/** Long package name of the generated level. */
		const TCHAR* Package = nullptr;
		FText Name;
		FText Latin;
		FText Location;
		FText Description;
		FText Lanes;
		ECampusTimeOfDay DefaultTime = ECampusTimeOfDay::Noon;
		/** Relative odds of each weather / time of day when rolled at random (indexed by the enums). */
		float WeatherOdds[(int32)ECampusWeather::Count] = {};
		float TimeOdds[(int32)ECampusTimeOfDay::Count] = {};
		bool bPlayable = true;
		bool bGunfight = true;
	};

	CAMPUSOPS_API const FMapInfo& Get(ECampusMap Map);
	CAMPUSOPS_API TArray<ECampusMap> Playable();
	/** The map whose package is PackageName (Campus when unknown). */
	CAMPUSOPS_API ECampusMap FromPackage(const FString& PackageName);

	/** Choice -1 rolls by the map's odds; anything else is taken as is. */
	CAMPUSOPS_API ECampusWeather ResolveWeather(ECampusMap Map, int32 Choice, FRandomStream& Stream);
	/** Choice -1 rolls by the map's odds, -2 is the map's own default time. */
	CAMPUSOPS_API ECampusTimeOfDay ResolveTime(ECampusMap Map, int32 Choice, FRandomStream& Stream);

	CAMPUSOPS_API FText WeatherName(ECampusWeather Weather);
	CAMPUSOPS_API FText TimeName(ECampusTimeOfDay Time);
	/** Short line for loading screens and the HUD: "黄昏 · 小雨". */
	CAMPUSOPS_API FText ConditionsLine(ECampusWeather Weather, ECampusTimeOfDay Time);
}
