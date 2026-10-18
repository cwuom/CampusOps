#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusTypes.h"
#include "Map/CampusSurfaces.h"
#include "CampusSchoolBuilder.generated.h"

class UInstancedStaticMeshComponent;

USTRUCT(BlueprintType)
struct FCampusSpawnPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn") FTransform Transform;
	/** None = flex spawn usable by either team in TDM. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn") ECampusTeam Team = ECampusTeam::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn") bool bGunfight = false;
};

USTRUCT(BlueprintType)
struct FCampusMapRect
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap") FVector2D Min = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap") FVector2D Max = FVector2D::ZeroVector;
	/** 0 building, 1 sports surface, 2 low cover, 3 green. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap") uint8 Kind = 0;
};

/** Named camera / prop anchors used by the main menu. */
USTRUCT(BlueprintType)
struct FCampusMenuAnchors
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FTransform Operator;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FTransform WeaponDesk;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector CourtyardFocus = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector GateFocus = FVector::ZeroVector;
	/** Establishing shot of the title screen, the options backdrop and the deploy push-out (camera, look-at). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector TitleCamera = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector TitleTarget = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector OptionsCamera = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector OptionsTarget = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector DeployCamera = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") FVector DeployTarget = FVector::ZeroVector;
	/** Weapons hung on the armoury wall behind the operator. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu") TArray<FTransform> RackSlots;
};

/**
 * Procedurally builds a Taiwanese junior high school ("光華國中") as an instanced-mesh greybox:
 * admin building with central passage, two classroom wings with outdoor corridors, courtyard with
 * flag platform, basketball courts, school store, sports field and the surrounding streets.
 * Three-lane layout for 5v5 TDM plus a compact Gunfight arena on the basketball courts.
 */
UCLASS()
class CAMPUSOPS_API ACampusSchoolBuilder : public AActor
{
	GENERATED_BODY()

public:
	ACampusSchoolBuilder();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UFUNCTION(CallInEditor, Category = "Campus")
	void Rebuild();

	/** Toggles the Gunfight arena barriers (collision + visuals). */
	void SetGunfightMode(bool bEnabled);

	static ACampusSchoolBuilder* Find(const UWorld* World);

	/** Which layout this builder generates. */
	UPROPERTY(EditAnywhere, Category = "Campus") ECampusMap MapKind = ECampusMap::Campus;
	UPROPERTY(EditAnywhere, Category = "Campus") bool bBuildSurroundings = true;
	UPROPERTY(EditAnywhere, Category = "Campus") bool bBuildInteriors = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") TArray<FCampusSpawnPoint> SpawnPoints;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") TArray<FVector> Hotspots;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") TArray<FVector> GunfightHotspots;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") TArray<FCampusMapRect> MinimapRects;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") FBox PlayableBounds = FBox(ForceInit);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") FBox GunfightBounds = FBox(ForceInit);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") FVector GunfightFlag = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") FCampusMenuAnchors MenuAnchors;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Campus|Generated") int32 InstanceCount = 0;

	static constexpr int32 NumShapes = 4;
	static constexpr int32 NumMats = CampusSurfaces::Num;
	/** rgb, roughness, metallic, emissive, tile, dirt, then the box half extents (cm, 0 for non-box shapes) for edge bevels. */
	static constexpr int32 NumCustomFloats = 11;

	/** Component tag read by bullet impact FX (0 concrete, 1 metal, 2 wood, 3 dirt). */
	static FName SurfaceTag(uint8 Impact);

private:
	/** Shape x collision x material family batches, plus invisible blockers and gunfight barriers. */
	UPROPERTY(VisibleAnywhere, Category = "Campus") TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
	/** Imported props, one component per mesh piece; recreated on every rebuild. */
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> PropComponents;
	/** Interior lights and signs; recreated on every rebuild. */
	UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> RuntimeComponents;
	/** Hinged doors in every single doorway (spawned by the server, replicated). */
	UPROPERTY(Transient) TArray<TObjectPtr<class ACampusDoor>> Doors;
	/** Breakable see-through window panes (spawned locally on every machine). */
	UPROPERTY(Transient) TObjectPtr<class ACampusWindowGlass> WindowGlass;
	UPROPERTY(Transient) TArray<TObjectPtr<class ACampusWater>> WaterBodies;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> SignTargets;
	/** Live content of the HQ's screens (menu map only). */
	UPROPERTY(Transient) TObjectPtr<class ACampusHQScreens> Screens;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<UInstancedStaticMeshComponent> Blockers;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<UInstancedStaticMeshComponent> GunfightBlockers;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<UInstancedStaticMeshComponent> GunfightVisuals;
	UPROPERTY(VisibleAnywhere, Category = "Campus") TObjectPtr<UInstancedStaticMeshComponent> Ramps;

	friend struct FCampusSchoolBuildContext;
};
