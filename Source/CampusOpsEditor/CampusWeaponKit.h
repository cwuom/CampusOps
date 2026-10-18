#pragma once

#include "CoreMinimal.h"

namespace CampusWeaponKit
{
	/** Weapon materials, wear / grain / lens-dirt textures, and the modelled weapons + attachments (/Game/Campus/Weapons). */
	void BuildWeaponKit();
	/** Only the knife, with the kit's existing materials. */
	void BuildKnifeOnly();
	void BuildVisualDetails();
	/** Logs bounds of imported meshes the kit relies on (the Poly Haven service pistol). */
	void InspectPistol();
}
