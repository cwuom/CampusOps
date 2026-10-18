#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CampusPropLibrary.generated.h"

class UStaticMesh;

USTRUCT(BlueprintType)
struct FCampusPropEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop") FName Id;
	/** All pieces share the model's pivot, so they are placed with the same transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop") TArray<TObjectPtr<UStaticMesh>> Meshes;
	/** Combined local bounds of all pieces (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop") FBox Bounds = FBox(ForceInit);
	/** Impact surface: 0 concrete, 1 metal, 2 wood, 3 dirt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prop") uint8 Impact = 0;
};

/** CC0 props (Poly Haven) imported by the CampusBootstrap commandlet for the procedural school. */
UCLASS(BlueprintType)
class CAMPUSOPS_API UCampusPropLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Props") TArray<FCampusPropEntry> Props;

	const FCampusPropEntry* Find(FName Id) const;

	static const TCHAR* PackagePath;
	static UCampusPropLibrary* Get();
};
