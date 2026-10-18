#pragma once

#include "CoreMinimal.h"

/** UI assets built from SourceArt/Fonts (SIL OFL) and procedural textures. */
namespace CampusUIAssets
{
	/** Font faces + composite fonts: Latin faces with a Noto CJK sub-font so Chinese, Latin and digits mix cleanly. */
	void BuildFonts();
	/** Gradients, soft shapes, vignettes, scope mask, bullet hole and smoke sprites. */
	void BuildTextures();
	/** small mip-filtered combat icons, without regenerating unrelated FX assets */
	void BuildCombatTextures();
	/** Reticle/lens, smoke sprite, sign and bullet hole materials. */
	void BuildMaterials();
}
