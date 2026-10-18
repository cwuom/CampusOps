#include "Map/CampusSurfaces.h"

namespace CampusSurfaces
{
	const FCampusMatInfo& Info(ECampusMat Mat)
	{
		//   Name            Texture                      Scale  Tint  RScale RInst MInst Normal Ground Streak Wet  Macro Impact AntiTile
		static const FCampusMatInfo Table[Num] = {
			{ TEXT("PaintExt"),     TEXT("beige_wall_001"),           300.f, 0.88f, 1.f,  0.f,  1.f, 0.9f, 0.9f, 0.35f, 0.f, 0.7f, 0, 1.f },
			// Classroom walls and ceilings are smooth emulsion over the plaster: the photo's grains only as a faint
			// texture (at full strength every grain caught the lamps, like snow on a dark ceiling).
			{ TEXT("PaintInt"),     TEXT("white_plaster_02"),         100.f, 0.92f, 1.f,  0.f,  1.f, 0.2f, 0.6f, 0.1f, 0.f,  0.45f, 0, 1.f, 0.35f },
			{ TEXT("Concrete"),     TEXT("concrete_floor_worn_001"),  300.f, 0.35f, 1.f,  0.f,  1.f, 1.0f, 0.8f, 0.6f, 0.25f, 1.0f, 0, 1.f },
			{ TEXT("ConcreteWall"), TEXT("concrete_wall_008"),        271.f, 0.4f,  1.f,  0.f,  1.f, 1.0f, 1.0f, 0.6f, 0.f,  1.0f, 0, 1.f },
			{ TEXT("Terrazzo"),     TEXT("terrazzo_tiles"),           200.f, 0.15f, 1.f,  0.f,  1.f, 0.8f, 0.5f, 0.f,  0.f,  0.5f, 0, 0.f },
			{ TEXT("FacadeTile"),   TEXT("rectangular_facade_tiles"), 200.f, 0.75f, 1.f,  0.f,  1.f, 1.0f, 0.9f, 0.45f, 0.f, 0.8f, 0, 0.f },
			{ TEXT("WallTile"),     TEXT("square_tiled_wall"),        200.f, 0.85f, 1.f,  0.f,  1.f, 1.0f, 0.8f, 0.2f, 0.f,  0.5f, 0, 0.f },
			{ TEXT("PaverRed"),     TEXT("red_brick_pavers"),         180.f, 0.2f,  1.f,  0.f,  1.f, 1.0f, 0.6f, 0.f,  0.2f, 1.0f, 0, 0.f },
			{ TEXT("PaverGray"),    TEXT("square_concrete_pavers"),   180.f, 0.25f, 1.f,  0.f,  1.f, 1.0f, 0.6f, 0.f,  0.35f, 1.0f, 0, 0.f },
			{ TEXT("Asphalt"),      TEXT("asphalt_02"),               300.f, 0.15f, 1.f,  0.f,  1.f, 1.0f, 0.4f, 0.f,  0.45f, 1.0f, 0, 1.f },
			// Sports surfaces are swept and drained: no puddles, only light wear.
			{ TEXT("Track"),        TEXT("rubberized_track"),         200.f, 0.6f,  1.f,  0.f,  1.f, 1.0f, 0.3f, 0.f,  0.f,  0.45f, 3, 1.f, 0.6f },
			{ TEXT("Court"),        TEXT("concrete_floor_painted"),   200.f, 0.92f, 1.6f, 0.f,  1.f, 0.5f, 0.2f, 0.f,  0.f,  0.3f, 0, 1.f, 0.3f },
			// The photo set is a leaf-littered autumn lawn: keep its detail, take the colour from the palette (humid, green).
			// Roughness from the palette: the litter photo's wet-leaf roughness gives turf a pale sheen under a low sun.
			{ TEXT("Grass"),        TEXT("leafy_grass"),              140.f, 0.97f, 1.f,  1.f,  1.f, 1.0f, 0.f,  0.f,  0.f,  1.2f, 3, 1.f },
			// Bed soil is watered humus, darker and finer than the trodden park path in the photo.
			{ TEXT("Soil"),         TEXT("park_dirt"),                130.f, 0.8f,  1.f,  0.f,  1.f, 1.0f, 0.f,  0.f,  0.35f, 1.0f, 3, 1.f },
			{ TEXT("Wood"),         TEXT("wood_table_worn"),           55.f, 0.55f, 1.f,  0.f,  1.f, 0.8f, 0.3f, 0.f,  0.f,  0.4f, 2, 0.f },
			{ TEXT("PaintedMetal"), TEXT("green_metal_rust"),         100.f, 0.75f, 1.f,  0.f,  0.f, 1.0f, 0.5f, 0.4f, 0.f,  0.6f, 1, 1.f },
			// Lockers, frames and fittings: clean painted / brushed metal, colour and finish from the palette.
			{ TEXT("Metal"),        nullptr,                          100.f, 1.f,   1.f,  1.f,  1.f, 0.f,  0.3f, 0.2f, 0.f,  0.4f, 1, 0.f },
			{ TEXT("Corrugated"),   TEXT("corrugated_iron_02"),       270.f, 0.75f, 1.f,  0.f,  0.f, 1.0f, 0.3f, 0.6f, 0.f,  0.8f, 1, 0.f },
			// Washed-pebble render (洗石子): pea-sized stones, not cobbles.
			{ TEXT("Pebble"),       TEXT("pebble_embedded_concrete"), 110.f, 0.3f,  1.f,  0.f,  1.f, 0.8f, 0.6f, 0.35f, 0.f, 0.6f, 0, 1.f },
			{ TEXT("RoofTile"),     TEXT("clay_roof_tiles_02"),       250.f, 0.4f,  1.f,  0.f,  1.f, 1.0f, 0.f,  0.4f, 0.f,  0.8f, 0, 0.f },
			{ TEXT("Rubber"),       TEXT("rubber_tiles"),             200.f, 0.85f, 1.f,  0.f,  1.f, 0.8f, 0.2f, 0.f,  0.f,  0.4f, 3, 0.f },
			{ TEXT("Plain"),        nullptr,                          200.f, 1.f,   1.f,  1.f,  1.f, 0.f,  0.3f, 0.1f, 0.f,  0.3f, 1, 0.f },
			{ TEXT("Facade"),       nullptr,                          200.f, 1.f,   1.f,  1.f,  0.f, 0.f,  0.5f, 0.6f, 0.f,  0.6f, 0, 0.f },
			{ TEXT("FacadeFar"),    nullptr,                          200.f, 1.f,   1.f,  1.f,  0.f, 0.f,  0.5f, 0.6f, 0.f,  0.6f, 0, 0.f },
			{ TEXT("Grille"),       nullptr,                          100.f, 1.f,   1.f,  1.f,  0.f, 0.f,  0.3f, 0.3f, 0.f,  0.4f, 1, 0.f },
			{ TEXT("Glass"),        nullptr,                          200.f, 1.f,   1.f,  1.f,  1.f, 0.f,  0.f,  0.f,  0.f,  0.f,  1, 0.f },
			{ TEXT("Tower"),        nullptr,                          200.f, 1.f,   1.f,  1.f,  0.f, 0.f,  0.f,  0.f,  0.f,  0.f,  1, 0.f },
			{ TEXT("TowerFar"),     nullptr,                          200.f, 1.f,   1.f,  1.f,  0.f, 0.f,  0.f,  0.f,  0.f,  0.f,  1, 0.f },
			// The leaves keep their own colours (a little of the palette green evens out sets of hedges); no wall grime.
			{ TEXT("Hedge"),        TEXT("hedge_leaves"),              70.f, 0.25f, 1.f,  0.f,  1.f, 1.0f, 0.f,  0.f,  0.15f, 0.8f, 3, 1.f },
			{ TEXT("HotelPaint"),   TEXT("white_plaster_02"),         100.f, 1.f,  1.f,  1.f,  1.f, 0.08f, 0.f, 0.f, 0.f, 0.08f, 0, 1.f, 0.12f },
			{ TEXT("HotelDado"),    TEXT("white_plaster_02"),         100.f, 1.f,  1.f,  1.f,  1.f, 0.10f, 0.f, 0.f, 0.f, 0.10f, 0, 1.f, 0.15f },
		};
		return Table[FMath::Clamp((int32)Mat, 0, Num - 1)];
	}

	FString MaterialPath(ECampusMat Mat)
	{
		const FString Name = FString(TEXT("MI_Surface_")) + Info(Mat).Name;
		return FString::Printf(TEXT("/Game/Campus/Materials/Surfaces/%s.%s"), *Name, *Name);
	}

	FString TexturePath(const TCHAR* TextureSet, const TCHAR* Suffix)
	{
		const FString Name = FString::Printf(TEXT("T_%s_%s"), TextureSet, Suffix);
		return FString::Printf(TEXT("/Game/Campus/Textures/%s/%s.%s"), TextureSet, *Name, *Name);
	}
}
