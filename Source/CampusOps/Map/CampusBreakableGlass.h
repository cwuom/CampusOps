#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Map/CampusGlassGeometry.h"
#include "CampusBreakableGlass.generated.h"

class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Glazing that breaks the way glass does. The first round fractures the pane round the strike into irregular pieces
 * (a crushed ring of chips that drops out as the hole, radial wedges, larger plates beyond) with the crack lines
 * showing between them; every further strike fractures the piece it lands in again. Pieces fall out once they have
 * taken enough, or are no longer held by the frame through the glass around them, and a pane down to a little glass
 * gives way altogether. A blast close by blows a pane out at once, further away it fractures and loses what faces it.
 *
 * Panes are thin boxes in this component's space (thin along local X or Y), so the glass rides along with a moving
 * parent such as a door leaf. Only weapon traces collide with it. Every machine runs the same deterministic fracture
 * from the same shots, so the panes stay in step without replication.
 */
UCLASS(ClassGroup = Campus)
class CAMPUSOPS_API UCampusBreakableGlass : public USceneComponent
{
	GENERATED_BODY()

public:
	UCampusBreakableGlass();

	/**
	 * Tempered panes (curtain walls, glazed storefronts) stand in the way of people until they break, and break the way
	 * tempered glass does: the first strike crazes the sheet, the next (or anything heavy) brings the whole of it down
	 * in small pieces, leaving a jagged rim in the frame and the opening clear to walk through.
	 * InTempered per pane: 0 annealed, 1 tempered, 2 tempered and frameless (a balustrade held only along its foot),
	 * 3 a door's safety light (tempered, comes down whole at the first strike).
	 */
	void SetPanes(const TArray<FBox>& InPanes, const TArray<uint8>& InTempered = TArray<uint8>());
	/** A round crossing the glass at Location (world) along Direction. False if there is no glass there. */
	bool Shoot(const FVector& Location, const FVector& Direction, bool bEffects);
	/** Something heavy (a grenade) hitting the pane: knocks a hole of Radius cm around Location. */
	bool Smash(const FVector& Location, const FVector& Direction, float Radius, bool bEffects);
	/** A blast at Center (world): panes in sight of it within Radius crack, lose pieces or blow out. */
	void Blast(const FVector& Center, float Radius, bool bEffects, const AActor* Ignore);

	static bool ShootAt(UWorld* World, const FVector& Location, const FVector& Direction);
	static bool SmashAt(UWorld* World, const FVector& Location, const FVector& Direction, float Radius);
	static void BlastAt(UWorld* World, const FVector& Center, float Radius, const AActor* Ignore);
	static bool IsGlass(const UPrimitiveComponent* Component);
	static const FName GlassTag;

	/** World bounds of every pane that still has glass in it. */
	TArray<FBox> GetPaneBounds() const;

private:
	struct FFrag
	{
		CampusGlassGeo::FGlassPoly Poly;
		FVector2f Center = FVector2f::ZeroVector;
		float Area = 0.f;
		float Damage = 0.f;
		bool bGone = false;
	};
	struct FPane
	{
		FBox Box;
		/** Width across and height, cm. */
		FVector2f Size = FVector2f::ZeroVector;
		TArray<FFrag> Frags;
		/** Strike marks: pane position, star size, the face the round came in through (+1 / -1). */
		TArray<FVector4f> Strikes;
		/** Mesh sections of the broken pane (glass, crack lines, crack glints); -1 while it is whole. */
		int32 Slot = -1;
		int32 Hits = 0;
		bool bDestroyed = false;
		bool bTempered = false;
		/** Held only along its foot (a balustrade): no frame round the rest of it to keep glass in. */
		bool bFrameless = false;
		/** A door's tempered light: gone at the first strike, nothing left in the beads, no pawn blocker of its own. */
		bool bSafety = false;
		bool IsSplit() const { return Slot >= 0; }
	};

	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Whole;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Broken;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> CrackStars;
	/** Invisible pawn blockers of the tempered panes still standing. */
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Solid;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CrackLineMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CrackGlintMaterial;
	TArray<FPane> Panes;
	/** Pane index of every instance in Whole, in order. */
	TArray<int32> WholeToPane;
	/** Pane index of every instance in Solid, in order. */
	TArray<int32> SolidToPane;
	int32 NumSlots = 0;

	UInstancedStaticMeshComponent* MakeSet(const TCHAR* Name, bool bCollide);
	int32 FindPane(const FVector& Local, float Slack) const;
	void Axes(const FPane& P, FVector& U, FVector& N) const;
	/** Pane-space coordinates of a local point: across the width and up from the lower corner, cm. */
	FVector2f PaneCoords(const FPane& P, const FVector& Local) const;
	/** Takes the pane out of the whole-glass set: it is one piece until the first fracture. */
	void Split(int32 PaneIndex);
	/** Fractures the piece under At (pane coords) round it; Scale sizes the pattern. False when At is over a hole. */
	bool Fracture(int32 PaneIndex, const FVector2f& At, float Scale);
	/** Damages the pieces round At, drops failed and unsupported ones; returns the pieces that fell. */
	void Damage(int32 PaneIndex, const FVector2f& At, float Amount, float Spread, TArray<int32>& OutFallen);
	void Release(int32 PaneIndex, const TArray<int32>& Fallen, const FVector& HitLocal, const FVector& DirectionWorld, bool bEffects, float Strength);
	void AddStrike(int32 PaneIndex, const FVector2f& At, const FVector& DirectionLocal, float Size);
	void RebuildPane(int32 PaneIndex);
	void RebuildStrikes();
	/** The bits of a burst tempered pane still stuck in the frame's glazing channel, pane coords. */
	void Remnants(int32 PaneIndex, TArray<CampusGlassGeo::FGlassPoly>& Out) const;
	/** Opens the way through a tempered pane once it has come down. */
	void ClearSolid(int32 PaneIndex);
};
