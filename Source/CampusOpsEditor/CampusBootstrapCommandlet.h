#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CampusBootstrapCommandlet.generated.h"

/**
 * Generates every asset the code expects:
 *   UnrealEditor-Cmd.exe CampusOps.uproject -run=CampusBootstrap [-skipaudio] [-skipmaterials] [-skipimport] [-reimport] [-skipmaps] [-inspect]
 * Photoscanned textures and props are imported from SourceArt/PolyHaven (run Tools/FetchAssets.ps1 first).
 * -menuartonly imports only the three menu screenshots from SourceArt/Menu
 */
UCLASS()
class UCampusBootstrapCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCampusBootstrapCommandlet();
	virtual int32 Main(const FString& Params) override;
};
