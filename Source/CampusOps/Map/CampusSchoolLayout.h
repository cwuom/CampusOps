#pragma once

#include "CoreMinimal.h"
#include "CampusTypes.h"
#include "Map/CampusSurfaces.h"

class ACampusSchoolBuilder;

/** Palette of greybox surfaces evoking a Taiwanese junior high school. */
enum class EPal : uint8
{
	Asphalt, Concrete, ConcreteDark, PaverRed, PaverGray, Terrazzo,
	WallCream, WallWhite, FacadeBeige, FacadeBrick, TileGreen, Column, Railing, DoorGreen,
	WindowFrame, Glass, Board, BoardFrame, WoodDesk, DeskFrame, Locker, Cork, Stainless,
	Track, LineWhite, Grass, CourtGreen, CourtBlue, Hedge, Leaves, Trunk, Bronze, Stone,
	RoofGreen, RoofBlue, RoofTile, RedBoard, Gold, LightPanel, ClockFace, Black,
	ScooterWhite, ScooterRed, ScooterBlue, Tire, BinBlue, BinYellow, BinGreen, MatBlue,
	BarrierYellow, BarrierBlack, FarBeige, FarGray, FarPink, FarBlue, IronBars,
	SignGreen, SignOrange, SignRed, SignBlue, Mountain, TankBlue, TankWhite, PillarRed,
	Orange, Shelf, Fridge, Flag,
	/** See-through glass for real window openings (facade-only windows keep the opaque Glass). */
	GlassClear,
	/** Bare planting soil under shrubs and grass clumps. */
	Soil, GrassMown,
	/** Book covers (cloth / laminated board) and page edges. */
	BookRed, BookBlue, BookGreen, BookCream, BookBrown, BookGray, BookPage, BookOchre, BookTeal, BookCharcoal,
	/** Distant apartment blocks and towers (procedural facade material). */
	CityBeige, CityGray, CityPink, CityBlue, CityWhite, CityTile,
	/** The same colours on the outer districts (no shadows, culled early). */
	FarCityBeige, FarCityGray, FarCityPink, FarCityBlue, FarCityWhite, FarCityTile,
	/** Iron window cage bars (鐵窗) and the welded mesh fence, as masked grille panels. */
	GrilleIron, GrilleMesh,
	// ---- Skyline hotel
	/** Curtain-wall towers (the hotel's own shaft and its neighbours), lit offices at dusk. */
	TowerBlue, TowerGreen, TowerSilver, TowerBronze, TowerDark,
	Marble, MarbleDark, Teak, PoolTile, Water, Leather, Brass, Carpet, LedWarm, LedCool, Neon, AviationRed,
	HeliDeck, HeliYellow, HeliWhite, Galvanized, HvacGrey, RoofDeck, Canvas, Stone2, Bottle, Velvet,
	// ---- Special operations HQ
	SteelDark, Epoxy, ScreenBlue, ScreenAmber, ScreenGreen, Olive, Sandbag, ConcreteHQ, RubberMat,
	/** Street markings (lane lines, zebra crossings), and the grey of a roadway seen from far above. */
	RoadPaint, Roadway,
	HotelWall, HotelDado,
	Count
};

/** Builds all geometry into the builder's instanced mesh batches. */
struct FCampusSchoolBuildContext
{
	explicit FCampusSchoolBuildContext(ACampusSchoolBuilder* InBuilder);
	void Build();

private:
	struct FBatch
	{
		TArray<FTransform> Transforms;
		TArray<float> Custom;
	};

	struct FOpening
	{
		float A0;
		float A1;
		float Z0;
		float Z1;
		bool bWindow;
	};

	struct FPropBatch
	{
		class UStaticMesh* Mesh = nullptr;
		bool bSolid = true;
		/** Shrubs and grass fade out with distance; grass much sooner. */
		bool bFoliage = false;
		bool bGrass = false;
		uint8 Impact = 0;
		TArray<FTransform> Transforms;
	};

	/** Single doorway that gets a hinged door leaf (hinge on the opening's low edge, leaf along local +X). */
	struct FDoorSpec
	{
		FVector Hinge;
		float Yaw;
		float Width;
		float Height;
	};

	/** Runtime light: fluorescent rect fixture (Kind 0), point (1) or spot (2). */
	struct FLightSpec
	{
		FVector Location;
		bool bAlongY;
		float Lumens;
		uint8 Kind = 0;
		float Temperature = 5600.f;
		float Radius = 0.f;
		FRotator Rotation = FRotator(-90.f, 0.f, 0.f);
		float Cone = 40.f;
		FLinearColor Color = FLinearColor::White;
		bool bClassroom = false;
	};

	/** Board with text drawn into a render target at runtime (Chinese school signage). */
	struct FSignSpec
	{
		FVector Center;
		float Yaw;
		float Width;
		float Height;
		float Depth;
		FString Text;
		FLinearColor Back;
		FLinearColor Ink;
		uint8 Font;
		bool bVertical;
		float Emissive;
		/** Texture resolution (pixels per cm of sign height). */
		float PxPerCm = 3.f;
	};

	/** Imported prop waiting to be instanced: checked against the finished structure first so it never sinks into it. */
	struct FPendingProp
	{
		FName Id;
		TArray<class UStaticMesh*> Pieces;
		/** Union of the pieces' bounds, model space. */
		FBox Bounds = FBox(ForceInit);
		/** Anchor offset and uniform scale applied before the placement transform. */
		FTransform Local = FTransform::Identity;
		FVector Location = FVector::ZeroVector;
		float FacingYaw = 0.f;
		bool bSolid = true;
		bool bHanging = false;
		bool bWallMounted = false;
		bool bTree = false;
		/** Bushes and grass may brush walls (they would in reality); they shrink but are never left out. */
		bool bFoliage = false;
		uint8 Impact = 0;
	};

	/**
	 * Geometry audit (-CampusAudit, game worlds): every primitive's bounds as it is added, the signs and the wall
	 * openings, checked once the map is laid out for coplanar overlapping faces (flicker), pieces touching nothing
	 * (floating) and anything standing in a doorway or window.
	 */
	struct FAuditBox { FBox Box; EPal Pal; uint8 Shape; bool bAxis; };
	TArray<FAuditBox> AuditBoxes;
	TArray<FBox> AuditOpenings;
	bool bAudit = false;
	void AuditGeometry() const;

	/** Axis-aligned cube as authored, with where its instance went, for SeparateCoplanarFaces. */
	struct FFlushBox { FBox Box; int32 Batch; int32 Instance; EPal Pal; };
	TArray<FFlushBox> FlushBoxes;
	/**
	 * Faces of different surfaces laid in one plane and overlapping fight over the depth test, and the tiny per-palette
	 * growth in Add is not enough at grazing angles (sub-pixel vertex snapping alone moves a far wall by more). Each such
	 * face of the higher palette is pushed out a few millimetres past the lower one; nothing else moves.
	 */
	void SeparateCoplanarFaces();

	ACampusSchoolBuilder* B;
	const class UCampusPropLibrary* Library = nullptr;
	TArray<FBatch> Batches;
	TArray<FPropBatch> PropBatches;
	TArray<FPendingProp> PendingProps;
	/** World bounds of solid, axis-aligned structure (walls, slabs, pillars, planters). */
	TArray<FBox> SolidBoxes;
	TArray<FLightSpec> Lights;
	TArray<FSignSpec> Signs;
	TArray<FDoorSpec> Doors;
	struct FDisplaySpec { FTransform Transform; uint8 Kind; FVector4f UV; float Emissive; };
	TArray<FDisplaySpec> Displays;
	/** Where the screens' live feeds look from: the drone first (circling DroneTarget), then the security cameras. */
	struct FScreenCamera { FVector Location; FRotator Rotation; float FOV; FString Name; };
	TArray<FScreenCamera> ScreenCameras;
	FVector DroneTarget = FVector::ZeroVector;
	/** See-through window panes; in game worlds they become breakable glass instead of batched boxes. */
	TArray<FBox> WindowPanes;
	/** Per pane: tempered glazing that blocks people until it is broken (see UCampusBreakableGlass::SetPanes). */
	TArray<uint8> WindowPaneTempered;
	/** Open water to wade in (box up to the water line); in game worlds each becomes a live water surface. */
	TArray<FBox> WaterVolumes;
	void WaterBody(const FBox& Volume);
	/** bFrameless: held only along its foot (a glass balustrade), so a burst pane leaves glass only there. */
	void WindowPane(const FVector& Min, const FVector& Max, bool bTempered = false, bool bFrameless = false);
	/**
	 * An aluminium sliding window in a wall opening from A0 to A1 along the wall (X for a wall on Y = Fixed, Y for one
	 * on X = Fixed): outer frame, two sashes on staggered tracks, the second slid over the first so one half stands open.
	 */
	void SlidingWindow(bool bAlongY, float Fixed, float A0, float A1, float Z0, float Z1);
	/** Game worlds stop people at tempered glass itself; the editor build keeps a blocker so the baked navmesh does too. */
	void GlazingBlocker(const FVector& Min, const FVector& Max);
	FBatch Blockers;
	FBatch Ramps;
	FBatch GunfightBlockers;
	FBatch GunfightVisuals;
	/** The terrain mesh (city plain + mountain ranges) goes in with the surroundings. */
	bool bTerrain = false;
	/** Where the terrain sits relative to the builder (the hotel's street level is far below its roof). */
	FVector TerrainOffset = FVector::ZeroVector;
	/** Forces every piece added into this material family (Count = each palette entry's own). */
	ECampusMat MatOverride = ECampusMat::Count;
	/** Trim colours of WallX / WallY openings (door frames, window frames). */
	EPal DoorTrim = EPal::DoorGreen;
	EPal WindowTrim = EPal::WindowFrame;

	// Imported props (Poly Haven). Yaw is the direction the model's front faces. Variant keeps pieces whose name
	// starts or ends with it, Exclude drops pieces containing it (files often hold several variants side by side);
	// the selection is re-centred.
	// Each returns false when the prop was not imported so callers can fall back to primitives.
	bool HasProp(FName Id) const;
	FVector PropSize(FName Id) const;
	/** Bottom of the model rests on Location; Height > 0 scales it uniformly to that height. */
	bool Prop(FName Id, const FVector& Location, float Yaw, float Height = 0.f, bool bSolid = true, const TCHAR* Variant = nullptr, const TCHAR* Exclude = nullptr);
	/** Top of the model touches Ceiling (lamps, fans). */
	bool PropHang(FName Id, const FVector& Ceiling, float Yaw, const TCHAR* Variant = nullptr);
	/** Back of the model sits on WallPoint, front facing Yaw (the wall's outward normal). */
	bool PropOnWall(FName Id, const FVector& WallPoint, float Yaw, float Height = 0.f, bool bSolid = true, const TCHAR* Variant = nullptr, const TCHAR* Exclude = nullptr);
	bool PlaceProp(FName Id, const FVector& Location, float FacingYaw, float Height, bool bSolid, uint8 AnchorMode, const TCHAR* Variant, const TCHAR* Exclude);
	/** Mown turf over Area at height Z (modelled blade patches); false when the patches are missing. */
	bool Turf(const FBox2D& Area, float Z, int32 Seed);
	/** A clipped hedge filling Hedge (box from the soil to the cut), with a people blocker. */
	void ClippedHedge(const FBox& Hedge, int32 Seed);
	/** Height of one variant (pieces matching it), model space; 0 when missing. */
	float PropVariantHeight(FName Id, const TCHAR* Variant) const { return PropVariantSize(Id, Variant).Z; }
	FVector PropVariantSize(FName Id, const TCHAR* Variant) const;
	/**
	 * Dense planting over an area: a jittered grid of clumps with random variant and yaw, no collision.
	 * Sizes are ScaleMin..ScaleMax times each variant's own height, or absolute heights (cm) when bAbsolute.
	 * Plants are shrunk (or left out) so their spread stays within Overhang of the area.
	 */
	void Plant(FName Id, TConstArrayView<const TCHAR*> Variants, const FBox2D& Area, float Z, float Spacing, float SizeMin, float SizeMax, int32 Seed, bool bAbsolute = false, float Overhang = 25.f);

	// primitives
	void Add(ECampusShape Shape, EPal P, const FTransform& T, bool bSolid);
	void Box(EPal P, const FVector& Center, const FVector& Size, bool bSolid = true, float Yaw = 0.f);
	void BoxMM(EPal P, const FVector& Min, const FVector& Max, bool bSolid = true);
	void BoxR(EPal P, const FVector& Center, const FVector& Size, const FRotator& Rot, bool bSolid = true);
	void Cyl(EPal P, const FVector& Base, float Diameter, float Height, bool bSolid = true);
	void CylR(EPal P, const FVector& Center, float Diameter, float Length, const FRotator& Rot, bool bSolid = false);
	void Sphere(EPal P, const FVector& Center, float Diameter, bool bSolid = false);
	void Cone(EPal P, const FVector& Base, float Diameter, float Height, bool bSolid = false);
	void Blocker(const FVector& Min, const FVector& Max);
	void Ramp(const FVector& Bottom, const FVector& Top, float Width, bool bAlongX);
	void GunfightWall(const FVector& Min, const FVector& Max);
	void Deco(EPal P, const FVector& Min, const FVector& Max) { BoxMM(P, Min, Max, false); }
	/** Painted ceiling just under a slab whose underside is at Z. */
	void CeilingAt(float X0, float Y0, float X1, float Y1, float Z) { Deco(EPal::WallWhite, FVector(X0, Y0, Z - 1.2f), FVector(X1, Y1, Z - 0.2f)); }

	// composites
	/** Skin (if not Count) covers the face on the SkinSide (+1 / -1 along the thickness axis), e.g. facade tiles over painted interior walls. */
	void WallX(EPal P, float Y, float X0, float X1, float Z0, float Z1, float Thick, TArray<FOpening> Openings, EPal Skin = EPal::Count, float SkinSide = 0.f);
	void WallY(EPal P, float X, float Y0, float Y1, float Z0, float Z1, float Thick, TArray<FOpening> Openings, EPal Skin = EPal::Count, float SkinSide = 0.f);
	/** Tiled dado on a wall face (Min/Max span the face thickness), broken at the wall's doorways. */
	void Dado(EPal P, bool bAlongX, float A0, float A1, float Face0, float Face1, float Z0, float Height, const TArray<FOpening>& Openings);
	/** bHotel: stone treads with brass nosings, glass balustrades and lit brass handrails instead of the school finish. */
	void Stair(const FBox2D& Well, bool bAlongX, bool bStartAtMax, bool bSideAMin, float Z0, float Z1, bool bHotel = false);
	void Tree(const FVector& Base, float Scale);
	void Bench(const FVector& Base, float Yaw);
	void StudentDesk(const FVector& Base, float Yaw);
	/** Freestanding double-sided library bookcase on Base (floor), long side along X or Y, stocked with books. */
	void Bookcase(const FVector& Base, bool bLongX, float Length, int32 Seed);
	/** A few books on a table top: a stack, a single one, sometimes one lying open. */
	void TableBooks(const FVector& Top, float Yaw, int32 Seed);
	void OfficeDesk(const FVector& Base, float Yaw);
	void Scooter(const FVector& Base, float Yaw, int32 Variant);
	void Bicycle(const FVector& Base, float Yaw);
	void Hoop(float X, float Baseline, float Inward);
	void Classroom(const FBox2D& Room, float Floor, float Ceiling, int32 BoardSide, int32 Variant);
	void FacadeWindowsY(float X, float Y0, float Y1, float Z0, float Z1, float Spacing, float Outward);
	void FacadeWindowsX(float Y, float X0, float X1, float Z0, float Z1, float Spacing, float Outward);
	void Lamp(const FVector& Base);
	void RowHouse(const FVector& FrontCenter, float Yaw, float Width, int32 Seed);
	/**
	 * Distant apartment block or tower on the ground at C (procedural facade), with its roof clutter. bSlabs adds real
	 * slab edges; bFar is the outer districts' version (no shadows, just the stair house and a tank on the roof).
	 */
	void CityBlock(const FVector& C, float Yaw, float Width, float Depth, int32 Floors, uint32 Seed, bool bSlabs, bool bFar = false);
	/** Text faces Yaw (the board's front normal); Font 0 serif title, 1 sans black, 2 sans bold. */
	void Sign(const FVector& Center, float Yaw, float Width, float Height, const TCHAR* Text, const FLinearColor& Back, const FLinearColor& Ink, uint8 Font = 0, bool bVertical = false, float Emissive = 0.f, float Depth = 3.f, float PxPerCm = 3.f);
	/**
	 * An LCD panel (ACampusHQScreens): shows the part UV (min u, min v, max u, max v) of picture Kind (ECampusHQDisplay)
	 * on the box's local +X face; Width along local Y, Height along local Z.
	 */
	void Display(uint8 Kind, const FVector& Center, const FRotator& Rotation, float Width, float Height, const FVector4f& UV = FVector4f(0.f, 0.f, 1.f, 1.f), float Emissive = 1.6f);
	/** UV of picture Kind filling a panel of this shape without stretching (cropped to the middle). */
	static FVector4f FillUV(uint8 Kind, float Width, float Height);
	/** Twin-tube fluorescent fixture (~4500 lm, ~400 lux on the desks of a lit classroom). */
	void CeilingLight(const FVector& Location, bool bAlongY, float Lumens = 4500.f);
	void PointLight(const FVector& Location, float Lumens, float Temperature, float Radius, const FLinearColor& Color = FLinearColor::White);
	/** Surface-mounted twin-tube batten under a ceiling at Ceiling (the lit diffuser and its light). */
	void Fixture(const FVector& Ceiling, bool bAlongY, float Lumens = 4500.f);
	void SpotLight(const FVector& Location, const FRotator& Rotation, float Lumens, float Temperature, float Radius, float Cone, const FLinearColor& Color = FLinearColor::White);

	// ---- shared builders for the newer maps
	/** Straight flight from Bottom to Top (Z rises), Width wide, with treads, stringers, an invisible ramp and side rails. */
	void StraightStair(const FVector& Bottom, const FVector& Top, float Width, EPal Tread, EPal Rail, bool bRails = true);
	/** Curtain wall along X (or Y): a low sill with a threshold cap, capped mullions every Bay cm, gasketed tempered panes
	 *  that stop people until they are shot out (then the bay can be walked through), a head; Openings (doorways, never
	 *  glazed) cut through it. */
	void GlassWall(bool bAlongX, float Fixed, float A0, float A1, float Z0, float Z1, float Bay, const TArray<FOpening>& Openings, EPal Frame);
	void GlassWallX(float Y, float X0, float X1, float Z0, float Z1, float Bay, const TArray<FOpening>& Openings, EPal Frame);
	/** The hotel's rooftop name sign on the south (S = -1) or north (S = 1) edge. */
	void HotelRoofSign(float S);
	/** Finishes of a stair core: the lift lobby above and the service-level landing at the foot of the stair. */
	void HotelCoreFinish(float S, float Y0, float Y1, float Outer, float Inner, float WellY0, float WellY1);
	/** The sky lounge's ceiling, bar detailing, soft furnishings, plants and lamps. */
	void HotelLoungeFinish(const FVector& Bar, float Zg);
	/** Upholstered sofa centred on Base, the sitter facing Yaw. */
	void Sofa(const FVector& Base, float Yaw, float Width);
	void GlassWallY(float X, float Y0, float Y1, float Z0, float Z1, float Bay, const TArray<FOpening>& Openings, EPal Frame);
	/** Frameless glass balustrade with a steel cap, from A to B (axis aligned), and an invisible wall well above it. */
	void GlassRail(const FVector& A, const FVector& B, float Height, float BlockHeight);
	/** Curtain-wall skyscraper standing on Ground (its base), with setbacks, a crown, mechanical floors and roof gear. */
	void SkyTower(const FVector& Ground, float Width, float Depth, float Height, EPal Skin, uint32 Seed, bool bShadow);

	// ---- Skyline hotel rooftop (CampusHotelLayout.cpp)
	void BuildHotel();
	void HotelDeck();
	void HotelEdges();
	void HotelPool();
	void HotelLounge();
	void HotelHelipad();
	void HotelCores();
	void HotelMechanical();
	void HotelLowerLevel();
	void HotelTerraces();
	void HotelSkyline();
	/** The streets 180 m below: avenues with lanes, crossings and trees, traffic, a park, a rail corridor and an elevated expressway. Adds the ground it uses to Taken. */
	void HotelStreets(TArray<FBox2D>& Taken);
	void HotelMetadata();
	void Lounger(const FVector& Base, float Yaw);
	void Cabana(const FVector& Center, float Yaw);
	void Planter(const FVector& Min, const FVector& Max, int32 Seed);
	void CoolingTower(const FVector& Base, float Size);

	// ---- Special operations HQ (CampusHQLayout.cpp)
	void BuildHQ();

	// sections
	void BuildGround();
	void BuildPerimeter();
	void BuildAdmin();
	void BuildWestWing();
	void BuildNorthWing();
	void BuildWalkway();
	void BuildCourtyard();
	void BuildEastLane();
	void BuildField();
	void BuildPlaza();
	void BuildWestAlley();
	void BuildGunfightArena();
	void BuildSurroundings();
	void BuildDetails();
	void BuildSigns();
	void BuildMetadata();
	void FlushRuntime();

	void Rect(float X0, float Y0, float X1, float Y1, uint8 Kind);
	void Spawn(const FVector& Location, float Yaw, ECampusTeam Team, bool bGunfight);
	/** Nudges queued props out of walls and roofs (or leaves out the ones that cannot fit), then batches them. */
	void ResolveProps();
	void Flush();
};
