#pragma once

#include "CoreMinimal.h"

/** Imports the CC0 Poly Haven sources from SourceArt/PolyHaven and builds the photoscanned surface materials. */
namespace CampusImport
{
	/** imports the project's gameplay screenshots from SourceArt/Menu for menu cards */
	bool ImportMenuTextures(bool bReimport);
	void ImportSurfaceTextures(bool bReimport);
	void BuildWeatheringTextures();
	void BuildSurfaceMaterials();
	void BuildHotelInteriorMaterials();
	/** Ground and mountain ranges round the school out to ~12 km (Nanite heightfield, M_Campus_Terrain). */
	void BuildTerrainMesh();
	void ImportProps(bool bReimport);
	/** Opaque and foliage masters for imported props (Nanite + instanced usage). */
	void BuildPropMaterials();
	/** Camo uniform, plate carrier, gloves and boots painted onto the mannequin by body region. */
	void BuildSoldierMaterial();
	/** Re-parents the imported glTF material instances onto the prop masters and fixes texture settings. */
	void FixPropMaterials();
}
