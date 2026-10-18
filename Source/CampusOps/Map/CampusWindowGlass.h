#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Map/CampusGlassGeometry.h"
#include "CampusWindowGlass.generated.h"

class UInstancedStaticMeshComponent;
class UCampusBreakableGlass;

/**
 * The see-through panes of every real window opening. Spawned locally on every machine by the school builder (the
 * layout is identical everywhere); the glass itself (holes, cracks, pieces dropping out) is a breakable glass component.
 */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusWindowGlass : public AActor
{
	GENERATED_BODY()

public:
	ACampusWindowGlass();

	void SetPanes(const TArray<FBox>& InPanes, const TArray<uint8>& InTempered);
	/** A round through Location along Direction, on whatever breakable glass is there (windows and door panels). */
	static bool BreakAt(UWorld* World, const FVector& Location, const FVector& Direction);
	/** World bounds of the panes that still hold glass. */
	TArray<FBox> GetPanes() const;

private:
	UPROPERTY() TObjectPtr<UCampusBreakableGlass> Glass;
};

/**
 * Falling glass: the pieces that broke out of a pane, each its own irregular plate, carried on with the round (a few
 * dropping back on the shooter's side), tumbling down, shattering again if big when they hit the floor, and skittering
 * to rest.
 */
UCLASS(NotPlaceable, Transient)
class CAMPUSOPS_API ACampusGlassShards : public AActor
{
	GENERATED_BODY()

public:
	ACampusGlassShards();
	/**
	 * Pieces are polygons in pane coordinates (cm across along U, cm up along Up from Origin, the lower corner at the
	 * middle of the glass); N is the pane normal and Thickness its glass. They break from HitPoint (a round or blast
	 * travelling along Direction); Strength (0..1+) scales how hard they are thrown.
	 */
	void Init(const FVector& Origin, const FVector& U, const FVector& Up, const FVector& N, float Thickness, const TArray<CampusGlassGeo::FGlassPoly>& Pieces,
		const FVector& HitPoint, const FVector& Direction, float Strength);
	virtual void Tick(float DeltaSeconds) override;
	/** What the glass was set in when it is a thing of its own (a door): the pieces fall out of it, not onto it. */
	TWeakObjectPtr<AActor> Source;

private:
	struct FShard
	{
		/** Corners relative to the centre, world space at rest orientation. */
		TArray<FVector, TInlineAllocator<12>> Corners;
		/** The same piece in its own plane (for breaking it again). */
		CampusGlassGeo::FGlassPoly Poly;
		/** Rest orientation: the plate's normal and the in-plane axes its Poly is laid out on. */
		FVector Normal;
		FVector Across;
		FVector Up;
		FVector Position;
		/** Where FloorZ was last found; it is looked for again once the piece has slid a little way off it. */
		FVector SampledAt;
		FVector Velocity;
		FQuat Rotation;
		FVector Spin;
		float Area;
		float FloorZ;
		float Delay;
		bool bResting;
	};
	void AddShard(const FVector& Center, const CampusGlassGeo::FGlassPoly& Local, const FVector& Across, const FVector& Up, const FVector& Normal);
	void BuildMesh(bool bTopology);
	float FloorBelow(const FVector& From) const;
	void Resample(FShard& S, const FVector& PreviousPosition) const;

	UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Mesh;
	TArray<FShard> Shards;
	float Thickness = 0.6f;
	float Age = 0.f;
	int32 Bounced = 0;
	bool bTopologyDirty = true;
};
