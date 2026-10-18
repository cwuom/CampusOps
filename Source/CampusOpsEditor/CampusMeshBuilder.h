#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

namespace CampusMesh
{
	/**
	 * Procedural hard-surface modelling used for the weapon kit, attachments, gear and foliage: chamfered boxes,
	 * bevelled profile extrusions, lathed parts and Picatinny rails. Output is a static mesh asset with per-face
	 * normals (smoothed below a crease angle), box-projected UVs (1 UV unit = 10 cm) and vertex colour
	 * R = edge-wear mask (bevels), G = cavity / grime, B = per-primitive random.
	 */
	class FBuilder
	{
	public:
		/** Material slot used by the following primitives. */
		void SetSlot(int32 Slot) { CurrentSlot = Slot; }
		int32 GetSlot() const { return CurrentSlot; }
		/** Cavity amount (vertex G) for the following primitives: recesses collect grime. */
		void SetCavity(float Value) { CurrentCavity = FMath::Clamp(Value, 0.f, 1.f); }
		/** Transform applied on top of each primitive's own transform (stack-free: set and reset explicitly). */
		void SetFrame(const FTransform& T) { Frame = T; }
		void ResetFrame() { Frame = FTransform::Identity; }

		/** Box with all 12 edges chamfered by Bevel (cm). */
		void Box(const FVector& Center, const FVector& Size, float Bevel = 0.12f, const FRotator& Rot = FRotator::ZeroRotator);
		/** Same, positioned by its min / max corners (axis aligned in the current frame). */
		void BoxMM(const FVector& Min, const FVector& Max, float Bevel = 0.12f) { Box((Min + Max) * 0.5f, Max - Min, Bevel); }
		/**
		 * Extrudes a 2D profile drawn in the XZ plane (any winding, simple polygon) between Y0 and Y1, with the rims
		 * chamfered by Bevel. Profile edges meeting at less than SmoothAngle degrees share normals (curved outlines).
		 */
		void Prism(const TArray<FVector2D>& ProfileXZ, float Y0, float Y1, float Bevel = 0.12f, float SmoothAngle = 28.f, const FTransform& T = FTransform::Identity);
		/** Same profile, extruded along Z between Z0 and Z1 from an XY outline (top-down shapes). */
		void PrismZ(const TArray<FVector2D>& ProfileXY, float Z0, float Z1, float Bevel = 0.12f, float SmoothAngle = 28.f, const FTransform& T = FTransform::Identity);
		/**
		 * Revolves a profile of (X along the axis, radius) points around the local X axis. Segments around; edges of
		 * the profile meeting at more than SmoothAngle degrees stay hard. Ends with radius > 0 are capped.
		 * Segments <= 8 with bSmoothAround = false gives faceted (hex / octagon) parts.
		 */
		void Lathe(const TArray<FVector2D>& ProfileXR, int32 Segments, const FTransform& T = FTransform::Identity, float SmoothAngle = 35.f, bool bSmoothAround = true, float PhaseDeg = 0.f, bool bCapEnds = true);
		/** Plain chamfered cylinder along X from X0 to X1. */
		void Tube(float X0, float X1, float Radius, int32 Segments, const FTransform& T = FTransform::Identity, float Chamfer = 0.08f);
		/** Hollow tube (outer / inner radius) along X, both ends chamfered: suppressor cans, scope bells, sleeves. */
		void HollowTube(float X0, float X1, float OuterR, float InnerR, int32 Segments, const FTransform& T = FTransform::Identity, float Chamfer = 0.08f);
		/** MIL-STD-1913 rail along +X starting at Start, teeth up (+Z of Rot). Width 2.1 cm, height ~0.95 cm. */
		void Picatinny(const FVector& Start, float Length, const FRotator& Rot = FRotator::ZeroRotator, float Width = 2.1f);
		/** A flat convex polygon (already ordered) facing Normal. */
		void Polygon(const TArray<FVector>& Points, const FVector& Normal, bool bEdge = false);

		int32 NumTriangles() const { return Tris.Num(); }

		/** Writes (or overwrites) a static mesh asset. Slot i uses SlotMaterials[i]. */
		UStaticMesh* Save(const FString& PackageName, const TArray<UMaterialInterface*>& SlotMaterials, bool bNanite, bool bCollision = false) const;

	private:
		struct FTri
		{
			FVector3f P[3];
			FVector3f N[3];
			float Edge = 0.f;
			float Cavity = 0.f;
			float Random = 0.f;
			int32 Slot = 0;
		};

		/** Adds a triangle oriented so its front faces FaceNormal; per-corner normals for smooth shading. */
		void Tri(const FVector& A, const FVector& B, const FVector& C, const FVector& NA, const FVector& NB, const FVector& NC, const FVector& FaceNormal, float Edge);
		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& NA, const FVector& NB, const FVector& NC, const FVector& ND, const FVector& FaceNormal, float Edge);
		void FlatQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, float Edge) { Quad(A, B, C, D, Normal, Normal, Normal, Normal, Normal, Edge); }
		void BeginPrimitive();

		TArray<FTri> Tris;
		FTransform Frame = FTransform::Identity;
		FTransform Local = FTransform::Identity;
		int32 CurrentSlot = 0;
		float CurrentCavity = 0.f;
		float CurrentRandom = 0.f;
		uint32 PrimitiveCounter = 0;
	};

	/** Ear-clipping triangulation of a simple polygon; returns index triples. */
	TArray<int32> Triangulate(const TArray<FVector2D>& Polygon);
	/** Signed area (positive = counter-clockwise). */
	float SignedArea(const TArray<FVector2D>& Polygon);
	/** Inset (positive) or outset (negative) of a polygon by Distance along mitred vertex normals. */
	TArray<FVector2D> Offset(const TArray<FVector2D>& Polygon, float Distance);
	/** Rounded-corner rectangle / slot outline helpers for profiles. */
	TArray<FVector2D> RoundedRect(const FVector2D& Min, const FVector2D& Max, float Radius, int32 CornerSegments = 3);
	/** Appends an arc (center, radius, degrees from A0 to A1) to a profile. */
	void Arc(TArray<FVector2D>& Out, const FVector2D& Center, float Radius, float A0Deg, float A1Deg, int32 Segments);
}
