#include "Map/CampusPropLibrary.h"
#include "Engine/StaticMesh.h"

const TCHAR* UCampusPropLibrary::PackagePath = TEXT("/Game/Campus/Props/DA_CampusProps");

const FCampusPropEntry* UCampusPropLibrary::Find(FName Id) const
{
	for (const FCampusPropEntry& Entry : Props)
	{
		if (Entry.Id == Id)
		{
			return Entry.Meshes.Num() > 0 ? &Entry : nullptr;
		}
	}
	return nullptr;
}

UCampusPropLibrary* UCampusPropLibrary::Get()
{
	static TWeakObjectPtr<UCampusPropLibrary> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UCampusPropLibrary>(nullptr, *(FString(PackagePath) + TEXT(".DA_CampusProps")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return Cached.Get();
}
