#include "Map/CampusSchoolLayout.h"
#include "Map/CampusSchoolBuilder.h"
#include "Map/CampusPropLibrary.h"
#include "Map/CampusDoor.h"
#include "Map/CampusWindowGlass.h"
#include "Map/CampusWater.h"
#include "Map/CampusHQScreens.h"
#include "Game/CampusGraphics.h"
#include "UI/CampusUIStyle.h"
#include "CampusAssets.h"
#include "CampusOps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "CanvasItem.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "HAL/IConsoleManager.h"
#include "AI/NavigationSystemBase.h"

namespace
{
	// Heights (cm)
	constexpr float F1 = 15.f;
	constexpr float H = 360.f;
	constexpr float F2 = F1 + H;
	constexpr float Slab = 20.f;
	constexpr float Ceil1 = F2 - Slab;
	constexpr float WallT = 20.f;
	constexpr float ExtT = 25.f;
	constexpr float StairArrival = 120.f;

	// West wing (勤學樓)
	constexpr float WW_X0 = -4200.f, WW_XC = -3430.f, WW_X1 = -3150.f, WW_Y0 = -3200.f, WW_Y1 = 3100.f;
	// North wing (力行樓)
	constexpr float NW_X0 = -4200.f, NW_X1 = 1400.f, NW_Y0 = 3100.f, NW_YC = 3380.f, NW_Y1 = 4150.f;
	// Admin building (行政大樓)
	constexpr float AD_X0 = -4200.f, AD_X1 = 4000.f, AD_Y0 = -4200.f, AD_YC = -3480.f, AD_Y1 = -3200.f;
	// Central passages (穿堂)
	constexpr float PS_X0 = -1275.f, PS_X1 = -475.f;
	constexpr float CX = -875.f;

	struct FPalEntry
	{
		FLinearColor Color;
		float Rough;
		float Metal;
		float Emissive;
		ECampusMat Mat;
		/** Weathering amount (0 = clean). */
		float Dirt;
		/** Texture repeat override in cm (0 = material family default). */
		float Tile;
	};

	const FPalEntry& Pal(EPal P)
	{
		using M = ECampusMat;
		static const FPalEntry Table[(int32)EPal::Count] = {
			{ FLinearColor(0.05f, 0.05f, 0.05f), 0.9f, 0.f, 0.f, M::Asphalt, 1.f, 0.f },         // Asphalt
			{ FLinearColor(0.30f, 0.29f, 0.27f), 0.85f, 0.f, 0.f, M::Concrete, 1.f, 0.f },       // Concrete
			{ FLinearColor(0.20f, 0.20f, 0.19f), 0.9f, 0.f, 0.f, M::ConcreteWall, 1.f, 0.f },    // ConcreteDark
			{ FLinearColor(0.30f, 0.14f, 0.10f), 0.85f, 0.f, 0.f, M::PaverRed, 0.9f, 0.f },      // PaverRed
			{ FLinearColor(0.36f, 0.35f, 0.33f), 0.8f, 0.f, 0.f, M::PaverGray, 0.9f, 0.f },      // PaverGray
			{ FLinearColor(0.48f, 0.47f, 0.44f), 0.5f, 0.f, 0.f, M::Terrazzo, 0.7f, 0.f },       // Terrazzo
			{ FLinearColor(0.64f, 0.61f, 0.53f), 0.85f, 0.f, 0.f, M::PaintExt, 1.f, 0.f },       // WallCream
			{ FLinearColor(0.70f, 0.70f, 0.67f), 0.85f, 0.f, 0.f, M::PaintInt, 0.8f, 0.f },      // WallWhite
			{ FLinearColor(0.56f, 0.49f, 0.39f), 0.7f, 0.f, 0.f, M::FacadeTile, 1.f, 0.f },      // FacadeBeige
			{ FLinearColor(0.40f, 0.17f, 0.11f), 0.7f, 0.f, 0.f, M::FacadeTile, 1.f, 0.f },      // FacadeBrick
			{ FLinearColor(0.26f, 0.40f, 0.34f), 0.35f, 0.f, 0.f, M::WallTile, 0.8f, 0.f },      // TileGreen
			{ FLinearColor(0.45f, 0.44f, 0.41f), 0.8f, 0.f, 0.f, M::Pebble, 1.f, 0.f },          // Column
			{ FLinearColor(0.09f, 0.25f, 0.17f), 0.45f, 0.f, 0.f, M::PaintedMetal, 1.f, 0.f },   // Railing
			{ FLinearColor(0.12f, 0.27f, 0.20f), 0.5f, 0.f, 0.f, M::PaintedMetal, 0.9f, 0.f },   // DoorGreen
			{ FLinearColor(0.55f, 0.56f, 0.57f), 0.35f, 0.9f, 0.f, M::Metal, 0.8f, 0.f },        // WindowFrame
			{ FLinearColor(0.03f, 0.04f, 0.045f), 0.04f, 0.f, 0.f, M::Plain, 0.5f, 0.f },        // Glass
			{ FLinearColor(0.045f, 0.11f, 0.075f), 0.8f, 0.f, 0.f, M::PaintInt, 0.4f, 0.f },     // Board
			{ FLinearColor(0.45f, 0.45f, 0.42f), 0.4f, 0.7f, 0.f, M::Metal, 0.5f, 0.f },         // BoardFrame
			{ FLinearColor(0.52f, 0.36f, 0.19f), 0.5f, 0.f, 0.f, M::Wood, 0.6f, 0.f },           // WoodDesk
			{ FLinearColor(0.10f, 0.20f, 0.36f), 0.4f, 0.4f, 0.f, M::Metal, 0.6f, 0.f },         // DeskFrame
			{ FLinearColor(0.50f, 0.49f, 0.45f), 0.45f, 0.4f, 0.f, M::Metal, 0.8f, 0.f },        // Locker
			{ FLinearColor(0.40f, 0.27f, 0.15f), 0.95f, 0.f, 0.f, M::Wood, 0.5f, 0.f },          // Cork
			{ FLinearColor(0.62f, 0.62f, 0.64f), 0.3f, 1.f, 0.f, M::Metal, 0.6f, 0.f },          // Stainless
			{ FLinearColor(0.36f, 0.10f, 0.07f), 0.9f, 0.f, 0.f, M::Track, 0.7f, 0.f },          // Track
			{ FLinearColor(0.78f, 0.78f, 0.75f), 0.8f, 0.f, 0.f, M::Court, 0.8f, 0.f },          // LineWhite
			{ FLinearColor(0.052f, 0.088f, 0.026f), 1.f, 0.f, 0.f, M::Grass, 0.f, 0.f },         // Grass
			{ FLinearColor(0.10f, 0.20f, 0.15f), 0.7f, 0.f, 0.f, M::Court, 0.8f, 0.f },          // CourtGreen
			{ FLinearColor(0.09f, 0.14f, 0.22f), 0.7f, 0.f, 0.f, M::Court, 0.8f, 0.f },          // CourtBlue
			{ FLinearColor(0.05f, 0.1f, 0.03f), 0.6f, 0.f, 0.f, M::Hedge, 0.f, 0.f },            // Hedge
			{ FLinearColor(0.05f, 0.14f, 0.035f), 0.9f, 0.f, 0.f, M::Grass, 0.f, 0.f },          // Leaves
			{ FLinearColor(0.15f, 0.10f, 0.065f), 0.9f, 0.f, 0.f, M::Wood, 0.4f, 0.f },          // Trunk
			{ FLinearColor(0.30f, 0.19f, 0.09f), 0.4f, 1.f, 0.f, M::Metal, 0.7f, 0.f },          // Bronze
			{ FLinearColor(0.46f, 0.45f, 0.42f), 0.7f, 0.f, 0.f, M::Pebble, 1.f, 0.f },          // Stone
			{ FLinearColor(0.12f, 0.27f, 0.20f), 0.5f, 0.5f, 0.f, M::Corrugated, 1.f, 0.f },     // RoofGreen
			{ FLinearColor(0.10f, 0.19f, 0.34f), 0.5f, 0.5f, 0.f, M::Corrugated, 1.f, 0.f },     // RoofBlue
			{ FLinearColor(0.42f, 0.17f, 0.08f), 0.6f, 0.f, 0.f, M::RoofTile, 1.f, 0.f },        // RoofTile
			{ FLinearColor(0.48f, 0.05f, 0.04f), 0.6f, 0.f, 0.f, M::PaintInt, 0.6f, 0.f },       // RedBoard
			{ FLinearColor(0.78f, 0.56f, 0.18f), 0.3f, 1.f, 0.f, M::Metal, 0.4f, 0.f },          // Gold
			{ FLinearColor(1.f, 0.97f, 0.90f), 0.5f, 0.f, 3.5f, M::Plain, 0.f, 0.f },            // LightPanel
			{ FLinearColor(0.90f, 0.90f, 0.85f), 0.5f, 0.f, 0.3f, M::Plain, 0.f, 0.f },          // ClockFace
			{ FLinearColor(0.02f, 0.02f, 0.02f), 0.6f, 0.f, 0.f, M::Plain, 0.5f, 0.f },          // Black
			{ FLinearColor(0.72f, 0.72f, 0.70f), 0.3f, 0.3f, 0.f, M::Metal, 0.5f, 0.f },         // ScooterWhite
			{ FLinearColor(0.48f, 0.05f, 0.04f), 0.3f, 0.3f, 0.f, M::Metal, 0.5f, 0.f },         // ScooterRed
			{ FLinearColor(0.06f, 0.14f, 0.38f), 0.3f, 0.3f, 0.f, M::Metal, 0.5f, 0.f },         // ScooterBlue
			{ FLinearColor(0.02f, 0.02f, 0.02f), 0.9f, 0.f, 0.f, M::Rubber, 0.5f, 0.f },         // Tire
			{ FLinearColor(0.05f, 0.17f, 0.46f), 0.5f, 0.f, 0.f, M::Plain, 0.8f, 0.f },          // BinBlue
			{ FLinearColor(0.72f, 0.52f, 0.05f), 0.5f, 0.f, 0.f, M::Plain, 0.8f, 0.f },          // BinYellow
			{ FLinearColor(0.08f, 0.34f, 0.12f), 0.5f, 0.f, 0.f, M::Plain, 0.8f, 0.f },          // BinGreen
			{ FLinearColor(0.08f, 0.20f, 0.50f), 0.8f, 0.f, 0.f, M::Rubber, 0.6f, 0.f },         // MatBlue
			{ FLinearColor(0.80f, 0.58f, 0.03f), 0.5f, 0.f, 0.f, M::PaintedMetal, 0.6f, 0.f },   // BarrierYellow
			{ FLinearColor(0.03f, 0.03f, 0.03f), 0.5f, 0.f, 0.f, M::Plain, 0.6f, 0.f },          // BarrierBlack
			{ FLinearColor(0.52f, 0.45f, 0.36f), 0.85f, 0.f, 0.f, M::FacadeTile, 1.2f, 0.f },    // FarBeige
			{ FLinearColor(0.38f, 0.38f, 0.37f), 0.85f, 0.f, 0.f, M::FacadeTile, 1.2f, 0.f },    // FarGray
			{ FLinearColor(0.53f, 0.36f, 0.34f), 0.85f, 0.f, 0.f, M::FacadeTile, 1.2f, 0.f },    // FarPink
			{ FLinearColor(0.32f, 0.40f, 0.46f), 0.85f, 0.f, 0.f, M::FacadeTile, 1.2f, 0.f },    // FarBlue
			{ FLinearColor(0.16f, 0.17f, 0.18f), 0.5f, 0.f, 0.f, M::PaintedMetal, 1.f, 0.f },    // IronBars
			{ FLinearColor(0.05f, 0.55f, 0.22f), 0.5f, 0.f, 3.f, M::Plain, 0.2f, 0.f },          // SignGreen
			{ FLinearColor(1.f, 0.42f, 0.05f), 0.5f, 0.f, 3.f, M::Plain, 0.2f, 0.f },            // SignOrange
			{ FLinearColor(0.85f, 0.06f, 0.05f), 0.5f, 0.f, 3.f, M::Plain, 0.2f, 0.f },          // SignRed
			{ FLinearColor(0.08f, 0.25f, 0.85f), 0.5f, 0.f, 3.f, M::Plain, 0.2f, 0.f },          // SignBlue
			{ FLinearColor(0.035f, 0.065f, 0.04f), 1.f, 0.f, 0.f, M::Plain, 0.f, 0.f },          // Mountain
			{ FLinearColor(0.08f, 0.22f, 0.52f), 0.4f, 0.f, 0.f, M::Plain, 0.8f, 0.f },          // TankBlue
			{ FLinearColor(0.70f, 0.70f, 0.68f), 0.35f, 0.8f, 0.f, M::Metal, 0.8f, 0.f },        // TankWhite
			{ FLinearColor(0.42f, 0.06f, 0.04f), 0.6f, 0.f, 0.f, M::PaintExt, 0.8f, 0.f },       // PillarRed
			{ FLinearColor(0.85f, 0.32f, 0.05f), 0.5f, 0.3f, 0.f, M::Plain, 0.4f, 0.f },         // Orange
			{ FLinearColor(0.40f, 0.31f, 0.21f), 0.7f, 0.f, 0.f, M::Wood, 0.5f, 0.f },           // Shelf
			{ FLinearColor(0.78f, 0.80f, 0.82f), 0.3f, 0.5f, 0.6f, M::Metal, 0.4f, 0.f },        // Fridge
			{ FLinearColor(0.08f, 0.22f, 0.62f), 0.8f, 0.f, 0.f, M::Plain, 0.f, 0.f },           // Flag
			{ FLinearColor(0.02f, 0.025f, 0.03f), 0.04f, 0.f, 0.f, M::Glass, 0.f, 0.f },         // GlassClear
			{ FLinearColor(0.085f, 0.06f, 0.04f), 0.95f, 0.f, 0.f, M::Soil, 1.f, 0.f },          // Soil
			{ FLinearColor(0.064f, 0.104f, 0.032f), 1.f, 0.f, 0.f, M::Grass, 0.f, 0.f },         // GrassMown (the light mowing stripe)
			// Library stock: faded cloth and laminated covers, not toy colours.
			{ FLinearColor(0.17f, 0.045f, 0.038f), 0.68f, 0.f, 0.f, M::Plain, 0.35f, 0.f },      // BookRed
			{ FLinearColor(0.05f, 0.075f, 0.14f), 0.66f, 0.f, 0.f, M::Plain, 0.35f, 0.f },       // BookBlue
			{ FLinearColor(0.055f, 0.10f, 0.07f), 0.68f, 0.f, 0.f, M::Plain, 0.35f, 0.f },       // BookGreen
			{ FLinearColor(0.42f, 0.38f, 0.30f), 0.62f, 0.f, 0.f, M::Plain, 0.35f, 0.f },        // BookCream
			{ FLinearColor(0.15f, 0.09f, 0.05f), 0.7f, 0.f, 0.f, M::Plain, 0.35f, 0.f },         // BookBrown
			{ FLinearColor(0.13f, 0.135f, 0.14f), 0.66f, 0.f, 0.f, M::Plain, 0.35f, 0.f },       // BookGray
			{ FLinearColor(0.55f, 0.52f, 0.46f), 0.85f, 0.f, 0.f, M::PaintInt, 0.2f, 30.f },     // BookPage
			{ FLinearColor(0.30f, 0.20f, 0.07f), 0.66f, 0.f, 0.f, M::Plain, 0.35f, 0.f },        // BookOchre
			{ FLinearColor(0.05f, 0.11f, 0.11f), 0.66f, 0.f, 0.f, M::Plain, 0.35f, 0.f },        // BookTeal
			{ FLinearColor(0.035f, 0.035f, 0.038f), 0.6f, 0.f, 0.f, M::Plain, 0.35f, 0.f },     // BookCharcoal
			// City facades: tile and paint colours of Taiwanese walk-ups and towers, sun-faded.
			{ FLinearColor(0.50f, 0.44f, 0.36f), 0.85f, 0.f, 0.f, M::Facade, 1.f, 0.f },        // CityBeige
			{ FLinearColor(0.40f, 0.40f, 0.39f), 0.85f, 0.f, 0.f, M::Facade, 1.f, 0.f },        // CityGray
			{ FLinearColor(0.52f, 0.38f, 0.35f), 0.85f, 0.f, 0.f, M::Facade, 1.f, 0.f },        // CityPink
			{ FLinearColor(0.34f, 0.40f, 0.45f), 0.85f, 0.f, 0.f, M::Facade, 1.f, 0.f },        // CityBlue
			{ FLinearColor(0.62f, 0.61f, 0.58f), 0.85f, 0.f, 0.f, M::Facade, 1.f, 0.f },        // CityWhite
			{ FLinearColor(0.36f, 0.30f, 0.25f), 0.85f, 0.f, 0.f, M::Facade, 1.f, 0.f },        // CityTile
			{ FLinearColor(0.50f, 0.44f, 0.36f), 0.85f, 0.f, 0.f, M::FacadeFar, 1.f, 0.f },     // FarCityBeige
			{ FLinearColor(0.40f, 0.40f, 0.39f), 0.85f, 0.f, 0.f, M::FacadeFar, 1.f, 0.f },     // FarCityGray
			{ FLinearColor(0.52f, 0.38f, 0.35f), 0.85f, 0.f, 0.f, M::FacadeFar, 1.f, 0.f },     // FarCityPink
			{ FLinearColor(0.34f, 0.40f, 0.45f), 0.85f, 0.f, 0.f, M::FacadeFar, 1.f, 0.f },     // FarCityBlue
			{ FLinearColor(0.62f, 0.61f, 0.58f), 0.85f, 0.f, 0.f, M::FacadeFar, 1.f, 0.f },     // FarCityWhite
			{ FLinearColor(0.36f, 0.30f, 0.25f), 0.85f, 0.f, 0.f, M::FacadeFar, 1.f, 0.f },     // FarCityTile
			// Grille panels: Tile = bar pitch (cm), Dirt = bar width (cm), Emissive = wire pitch (0 = two rails).
			{ FLinearColor(0.11f, 0.115f, 0.12f), 0.5f, 0.3f, 0.f, M::Grille, 1.6f, 12.f },     // GrilleIron
			{ FLinearColor(0.09f, 0.25f, 0.17f), 0.45f, 0.f, 20.f, M::Grille, 0.6f, 10.f },     // GrilleMesh
			// Curtain walls: Emissive = share of offices lit, Tile = bay width, Dirt = floor height (0 = defaults).
			{ FLinearColor(0.10f, 0.15f, 0.21f), 0.08f, 0.f, 0.35f, M::Tower, 400.f, 150.f },   // TowerBlue
			{ FLinearColor(0.10f, 0.17f, 0.15f), 0.08f, 0.f, 0.3f, M::Tower, 390.f, 160.f },    // TowerGreen
			{ FLinearColor(0.30f, 0.32f, 0.34f), 0.12f, 0.f, 0.4f, M::Tower, 380.f, 140.f },    // TowerSilver
			{ FLinearColor(0.20f, 0.14f, 0.09f), 0.1f, 0.f, 0.3f, M::Tower, 420.f, 180.f },     // TowerBronze
			{ FLinearColor(0.05f, 0.06f, 0.07f), 0.06f, 0.f, 0.45f, M::Tower, 400.f, 125.f },   // TowerDark
			{ FLinearColor(0.80f, 0.78f, 0.74f), 0.22f, 0.f, 0.f, M::Terrazzo, 0.15f, 160.f },  // Marble
			{ FLinearColor(0.07f, 0.07f, 0.075f), 0.18f, 0.f, 0.f, M::Terrazzo, 0.1f, 160.f },  // MarbleDark
			{ FLinearColor(0.34f, 0.19f, 0.09f), 0.55f, 0.f, 0.f, M::Wood, 0.5f, 0.f },          // Teak
			{ FLinearColor(0.12f, 0.36f, 0.42f), 0.15f, 0.f, 0.f, M::FacadeTile, 0.2f, 0.f },    // PoolTile (glass mosaic)
			{ FLinearColor(0.012f, 0.06f, 0.07f), 0.02f, 0.f, 0.f, M::Plain, 0.f, 0.f },         // Water
			{ FLinearColor(0.08f, 0.045f, 0.03f), 0.5f, 0.f, 0.f, M::Plain, 0.2f, 0.f },         // Leather
			{ FLinearColor(0.62f, 0.44f, 0.20f), 0.25f, 1.f, 0.f, M::Metal, 0.2f, 0.f },         // Brass
			{ FLinearColor(0.20f, 0.045f, 0.05f), 0.98f, 0.f, 0.f, M::PaintInt, 0.3f, 0.f },     // Carpet
			{ FLinearColor(1.f, 0.72f, 0.42f), 0.5f, 0.f, 6.f, M::Plain, 0.f, 0.f },             // LedWarm
			{ FLinearColor(0.70f, 0.86f, 1.f), 0.5f, 0.f, 6.f, M::Plain, 0.f, 0.f },             // LedCool
			{ FLinearColor(1.f, 0.55f, 0.18f), 0.5f, 0.f, 14.f, M::Plain, 0.f, 0.f },            // Neon
			{ FLinearColor(1.f, 0.05f, 0.03f), 0.5f, 0.f, 20.f, M::Plain, 0.f, 0.f },            // AviationRed
			{ FLinearColor(0.20f, 0.21f, 0.22f), 0.75f, 0.f, 0.f, M::Concrete, 0.6f, 0.f },      // HeliDeck
			{ FLinearColor(0.80f, 0.60f, 0.06f), 0.6f, 0.f, 0.f, M::Court, 0.5f, 0.f },          // HeliYellow
			{ FLinearColor(0.78f, 0.78f, 0.76f), 0.6f, 0.f, 0.f, M::Court, 0.5f, 0.f },          // HeliWhite
			{ FLinearColor(0.50f, 0.52f, 0.53f), 0.42f, 0.9f, 0.f, M::Metal, 0.7f, 0.f },        // Galvanized
			{ FLinearColor(0.60f, 0.60f, 0.57f), 0.5f, 0.2f, 0.f, M::PaintedMetal, 0.7f, 0.f },  // HvacGrey
			{ FLinearColor(0.33f, 0.33f, 0.32f), 0.85f, 0.f, 0.f, M::PaverGray, 0.8f, 0.f },     // RoofDeck
			{ FLinearColor(0.84f, 0.80f, 0.72f), 0.9f, 0.f, 0.f, M::PaintInt, 0.4f, 0.f },       // Canvas
			{ FLinearColor(0.52f, 0.50f, 0.46f), 0.6f, 0.f, 0.f, M::Pebble, 0.6f, 0.f },         // Stone2
			{ FLinearColor(0.35f, 0.20f, 0.06f), 0.1f, 0.f, 1.2f, M::Plain, 0.f, 0.f },          // Bottle
			{ FLinearColor(0.12f, 0.03f, 0.04f), 0.85f, 0.f, 0.f, M::Plain, 0.2f, 0.f },         // Velvet
			{ FLinearColor(0.06f, 0.065f, 0.07f), 0.4f, 0.8f, 0.f, M::Metal, 0.5f, 0.f },        // SteelDark
			{ FLinearColor(0.16f, 0.17f, 0.18f), 0.22f, 0.f, 0.f, M::Concrete, 0.4f, 0.f },      // Epoxy
			{ FLinearColor(0.12f, 0.42f, 0.75f), 0.3f, 0.f, 0.9f, M::Plain, 0.f, 0.f },          // ScreenBlue
			{ FLinearColor(0.95f, 0.55f, 0.12f), 0.3f, 0.f, 0.7f, M::Plain, 0.f, 0.f },          // ScreenAmber
			{ FLinearColor(0.15f, 0.75f, 0.35f), 0.3f, 0.f, 0.7f, M::Plain, 0.f, 0.f },          // ScreenGreen
			{ FLinearColor(0.15f, 0.17f, 0.10f), 0.6f, 0.2f, 0.f, M::PaintedMetal, 0.6f, 0.f },  // Olive
			{ FLinearColor(0.34f, 0.30f, 0.21f), 0.95f, 0.f, 0.f, M::Plain, 0.6f, 0.f },         // Sandbag
			{ FLinearColor(0.28f, 0.28f, 0.27f), 0.8f, 0.f, 0.f, M::ConcreteWall, 0.7f, 0.f },   // ConcreteHQ
			{ FLinearColor(0.03f, 0.03f, 0.03f), 0.9f, 0.f, 0.f, M::Rubber, 0.4f, 0.f },         // RubberMat
			{ FLinearColor(0.62f, 0.62f, 0.60f), 0.7f, 0.f, 0.f, M::Plain, 0.5f, 0.f },          // RoadPaint
			{ FLinearColor(0.085f, 0.085f, 0.085f), 0.85f, 0.f, 0.f, M::Asphalt, 0.8f, 0.f },    // Roadway
			{ FLinearColor(0.70f, 0.70f, 0.67f), 0.78f, 0.f, 0.f, M::HotelPaint, 0.12f, 0.f },
			{ FLinearColor(0.28f, 0.29f, 0.28f), 0.65f, 0.f, 0.f, M::HotelDado, 0.18f, 0.f },
		};
		return Table[(int32)P];
	}

	uint32 Hash(int32 A, int32 B)
	{
		uint32 H32 = (uint32)A * 73856093u ^ (uint32)B * 19349663u;
		H32 ^= H32 >> 13;
		H32 *= 0x5bd1e995u;
		H32 ^= H32 >> 15;
		return H32;
	}
}

FCampusSchoolBuildContext::FCampusSchoolBuildContext(ACampusSchoolBuilder* InBuilder)
	: B(InBuilder)
	, Library(UCampusPropLibrary::Get())
{
	Batches.SetNum(ACampusSchoolBuilder::NumShapes * 2 * ACampusSchoolBuilder::NumMats);
	const UWorld* World = InBuilder ? InBuilder->GetWorld() : nullptr;
	bAudit = World && World->IsGameWorld() && FParse::Param(FCommandLine::Get(), TEXT("CampusAudit"));
}

// ------------------------------------------------------------------ primitives

void FCampusSchoolBuildContext::Add(ECampusShape Shape, EPal P, const FTransform& T, bool bSolid)
{
	const EPal Finish = B->MapKind == ECampusMap::Hotel
		? (P == EPal::WallWhite ? EPal::HotelWall : (P == EPal::ConcreteHQ ? EPal::HotelDado : P)) : P;
	const FPalEntry& E = Pal(Finish);
	// Boxes are laid flush against each other everywhere (trims on walls, finishes on slabs, skins on facades), and two
	// different surfaces in exactly one plane flicker. Each surface grows by its own fraction of a millimetre to a few
	// millimetres, so no two of them can ever share a plane; nothing visible moves.
	FTransform Drawn = T;
	if (Shape == ECampusShape::Cube)
	{
		const float Grow = (0.02f + 0.003f * (float)(int32)P) * 2.f / 100.f;
		const FVector S = T.GetScale3D();
		Drawn.SetScale3D(FVector(S.X + FMath::Sign(S.X) * Grow, S.Y + FMath::Sign(S.Y) * Grow, S.Z + FMath::Sign(S.Z) * Grow));
	}
	if (bAudit)
	{
		const FRotator R = T.Rotator();
		auto Square = [](float Angle) { const float M = FMath::Fmod(FMath::Abs(Angle), 90.f); return M < 0.5f || M > 89.5f; };
		const bool bAxis = Shape == ECampusShape::Cube && Square(R.Yaw) && Square(R.Pitch) && Square(R.Roll);
		AuditBoxes.Add({ FBox(FVector(-50.f), FVector(50.f)).TransformBy(Drawn), P, (uint8)Shape, bAxis });
	}
	const ECampusMat Mat = MatOverride != ECampusMat::Count ? MatOverride : E.Mat;
	const int32 Index = ((int32)Shape * 2 + (bSolid ? 1 : 0)) * ACampusSchoolBuilder::NumMats + (int32)Mat;
	FBatch& Batch = Batches[Index];
	Batch.Transforms.Add(Drawn);
	if (Shape == ECampusShape::Cube)
	{
		const FRotator R = T.Rotator();
		auto Square = [](float Angle) { const float M = FMath::Fmod(FMath::Abs(Angle), 90.f); return M < 0.05f || M > 89.95f; };
		if (Square(R.Yaw) && Square(R.Pitch) && Square(R.Roll))
		{
			FlushBoxes.Add({ FBox(FVector(-50.f), FVector(50.f)).TransformBy(T), Index, Batch.Transforms.Num() - 1, P });
		}
	}
	const FVector3f Half = Shape == ECampusShape::Cube ? FVector3f(T.GetScale3D().GetAbs() * 50.f) : FVector3f::ZeroVector;
	Batch.Custom.Append({ E.Color.R, E.Color.G, E.Color.B, E.Rough, E.Metal, E.Emissive, E.Tile, E.Dirt, Half.X, Half.Y, Half.Z });

	if (bSolid && (Shape == ECampusShape::Cube || Shape == ECampusShape::Cylinder))
	{
		// Only axis-aligned pieces: their bounds are exact, so props can be fitted against them without false hits.
		auto Square = [](float Angle) { const float M = FMath::Fmod(FMath::Abs(Angle), 90.f); return M < 1.f || M > 89.f; };
		const FRotator R = T.Rotator();
		if (Square(R.Yaw) && FMath::Abs(R.Pitch) < 1.f && FMath::Abs(R.Roll) < 1.f)
		{
			SolidBoxes.Add(FBox(FVector(-50.f), FVector(50.f)).TransformBy(T));
		}
	}
}

void FCampusSchoolBuildContext::Sign(const FVector& Center, float Yaw, float Width, float Height, const TCHAR* Text, const FLinearColor& Back, const FLinearColor& Ink, uint8 Font, bool bVertical, float Emissive, float Depth, float PxPerCm)
{
	Signs.Add({ Center, Yaw, Width, Height, Depth, FString(Text), Back, Ink, Font, bVertical, Emissive, PxPerCm });
	if (bAudit)
	{
		// EPal::Count marks a sign in the audit.
		AuditBoxes.Add({ FBox(FVector(-50.f), FVector(50.f)).TransformBy(FTransform(FRotator(0.f, Yaw, 0.f), Center, FVector(Depth, Width, Height) / 100.f)), EPal::Count, 0, false });
	}
}

void FCampusSchoolBuildContext::Display(uint8 Kind, const FVector& Center, const FRotator& Rotation, float Width, float Height, const FVector4f& UV, float Emissive)
{
	Displays.Add({ FTransform(Rotation, Center, FVector(2.f, Width, Height) / 100.f), Kind, UV, Emissive });
}

FVector4f FCampusSchoolBuildContext::FillUV(uint8 Kind, float Width, float Height)
{
	const float Picture = ACampusHQScreens::Aspect((ECampusHQDisplay)Kind);
	const float Panel = Width / FMath::Max(Height, 1.f);
	if (Panel < Picture)
	{
		const float U = Panel / Picture;
		return FVector4f(0.5f - U * 0.5f, 0.f, 0.5f + U * 0.5f, 1.f);
	}
	const float V = Picture / Panel;
	return FVector4f(0.f, 0.5f - V * 0.5f, 1.f, 0.5f + V * 0.5f);
}

void FCampusSchoolBuildContext::CeilingLight(const FVector& Location, bool bAlongY, float Lumens)
{
	Lights.Add({ Location, bAlongY, Lumens });
}

void FCampusSchoolBuildContext::PointLight(const FVector& Location, float Lumens, float Temperature, float Radius, const FLinearColor& Color)
{
	FLightSpec& L = Lights.AddDefaulted_GetRef();
	L.Location = Location;
	L.bAlongY = false;
	L.Lumens = Lumens;
	L.Kind = 1;
	L.Temperature = Temperature;
	L.Radius = Radius;
	L.Color = Color;
}

void FCampusSchoolBuildContext::SpotLight(const FVector& Location, const FRotator& Rotation, float Lumens, float Temperature, float Radius, float Cone, const FLinearColor& Color)
{
	FLightSpec& L = Lights.AddDefaulted_GetRef();
	L.Location = Location;
	L.bAlongY = false;
	L.Lumens = Lumens;
	L.Kind = 2;
	L.Temperature = Temperature;
	L.Radius = Radius;
	L.Rotation = Rotation;
	L.Cone = Cone;
	L.Color = Color;
}

// ------------------------------------------------------------------ imported props

namespace
{
	enum class EAnchor : uint8 { Bottom, Top, Back };
}

bool FCampusSchoolBuildContext::HasProp(FName Id) const
{
	return Library && Library->Find(Id) != nullptr;
}

FVector FCampusSchoolBuildContext::PropSize(FName Id) const
{
	const FCampusPropEntry* Entry = Library ? Library->Find(Id) : nullptr;
	return Entry ? Entry->Bounds.GetSize() : FVector::ZeroVector;
}

// Poly Haven models face local +Y, so world yaw = facing yaw - 90.
bool FCampusSchoolBuildContext::PlaceProp(FName Id, const FVector& Location, float FacingYaw, float Height, bool bSolid, uint8 AnchorMode, const TCHAR* Variant, const TCHAR* Exclude)
{
	const FCampusPropEntry* Entry = Library ? Library->Find(Id) : nullptr;
	if (!Entry)
	{
		return false;
	}
	TArray<UStaticMesh*, TInlineAllocator<16>> Pieces;
	FBox Bounds(ForceInit);
	for (UStaticMesh* Mesh : Entry->Meshes)
	{
		if (!Mesh)
		{
			continue;
		}
		const FString Name = Mesh->GetName();
		if ((Variant && !Name.EndsWith(Variant) && !Name.StartsWith(Variant)) || (Exclude && Name.Contains(Exclude)))
		{
			continue;
		}
		Pieces.Add(Mesh);
		Bounds += Mesh->GetBoundingBox();
	}
	if (Pieces.Num() == 0)
	{
		return false;
	}
	const FVector Size = Bounds.GetSize();
	const float Scale = (Height > 0.f && Size.Z > 1.f) ? Height / Size.Z : 1.f;
	const FVector Center = Bounds.GetCenter();
	FVector AnchorPoint(Center.X, Center.Y, Bounds.Min.Z);
	if (AnchorMode == (uint8)EAnchor::Top)
	{
		AnchorPoint.Z = Bounds.Max.Z;
	}
	else if (AnchorMode == (uint8)EAnchor::Back)
	{
		AnchorPoint.Y = Bounds.Min.Y;
	}
	FPendingProp& Pending = PendingProps.AddDefaulted_GetRef();
	Pending.Id = Id;
	Pending.Pieces.Append(Pieces.GetData(), Pieces.Num());
	Pending.Bounds = Bounds;
	Pending.Local = FTransform(FQuat::Identity, -AnchorPoint * Scale, FVector(Scale));
	Pending.Location = Location;
	Pending.FacingYaw = FacingYaw;
	Pending.bSolid = bSolid;
	Pending.bHanging = AnchorMode == (uint8)EAnchor::Top;
	Pending.bWallMounted = AnchorMode == (uint8)EAnchor::Back;
	Pending.bTree = Id.ToString().Contains(TEXT("tree"));
	const FString IdText = Id.ToString();
	Pending.bFoliage = IdText.Contains(TEXT("shrub")) || IdText.Contains(TEXT("grass")) || IdText.Contains(TEXT("periwinkle")) || IdText.Contains(TEXT("fern")) || IdText.Contains(TEXT("calathea"));
	Pending.Impact = Entry->Impact;
	return true;
}

void FCampusSchoolBuildContext::ResolveProps()
{
	// Spatial hash over the solid structure.
	const float Cell = 500.f;
	auto ForCells = [Cell](const FBox& Box, TFunctionRef<void(const FIntPoint&)> Fn)
	{
		const int32 X0 = FMath::FloorToInt(Box.Min.X / Cell);
		const int32 X1 = FMath::FloorToInt(Box.Max.X / Cell);
		const int32 Y0 = FMath::FloorToInt(Box.Min.Y / Cell);
		const int32 Y1 = FMath::FloorToInt(Box.Max.Y / Cell);
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				Fn(FIntPoint(X, Y));
			}
		}
	};
	TMap<FIntPoint, TArray<int32>> Grid;
	for (int32 i = 0; i < SolidBoxes.Num(); ++i)
	{
		ForCells(SolidBoxes[i], [&](const FIntPoint& C) { Grid.FindOrAdd(C).Add(i); });
	}

	// Doorways and the floor their leaves sweep stay clear: nothing standing may block a door or be swung through.
	TArray<FBox> DoorZones;
	for (const FDoorSpec& D : Doors)
	{
		FBox Zone(ForceInit);
		const FTransform DoorT(FRotator(0.f, D.Yaw, 0.f), D.Hinge);
		for (const FVector& Corner : { FVector(-5.f, -D.Width - 15.f, 0.f), FVector(D.Width + 5.f, -D.Width - 15.f, 0.f), FVector(-5.f, D.Width + 15.f, D.Height), FVector(D.Width + 5.f, D.Width + 15.f, D.Height) })
		{
			Zone += DoorT.TransformPosition(Corner);
		}
		DoorZones.Add(Zone);
	}

	int32 Nudged = 0;
	int32 Dropped = 0;
	TMap<FName, int32> DroppedIds;
	for (const FPendingProp& P : PendingProps)
	{
		const FTransform Placement(FRotator(0.f, P.FacingYaw - 90.f, 0.f), P.Location);
		if (!P.bHanging && !P.bWallMounted)
		{
			const FBox Footprint = P.Bounds.TransformBy(P.Local * Placement).ExpandBy(FVector(-2.f));
			if (DoorZones.ContainsByPredicate([&](const FBox& Zone) { return Zone.Intersect(Footprint); }))
			{
				++Dropped;
				++DroppedIds.FindOrAdd(P.Id);
				continue;
			}
		}
		// Fits the prop at a uniform scale about its anchor; OutOffset is the horizontal move that clears the structure.
		auto Fit = [&](float Factor, FTransform& OutWorld, FVector& OutOffset) -> bool
		{
			FTransform Local = P.Local;
			Local.SetTranslation(Local.GetTranslation() * Factor);
			Local.SetScale3D(Local.GetScale3D() * Factor);
			OutWorld = Local * Placement;
			OutOffset = FVector::ZeroVector;
			FVector& Offset = OutOffset;
			const FBox Start = P.Bounds.TransformBy(OutWorld).ExpandBy(FVector(-2.f));
			const float Height = Start.GetSize().Z;
			bool bClear = false;
			for (int32 Iter = 0; Iter < 5 && !bClear; ++Iter)
			{
				const FBox Cur = Start.ShiftBy(Offset);
				float PushX = 0.f;
				float PushY = 0.f;
				bool bHit = false;
				TSet<int32> Seen;
				ForCells(Cur, [&](const FIntPoint& C)
				{
					const TArray<int32>* List = Grid.Find(C);
					if (!List)
					{
						return;
					}
					for (const int32 Index : *List)
					{
						bool bAlready = false;
						Seen.Add(Index, &bAlready);
						const FBox& S = SolidBoxes[Index];
						if (bAlready || !Cur.Intersect(S))
						{
							continue;
						}
						// Floors, kerbs, planters and steps under the prop are contact, not clipping.
						const float OverZ = FMath::Min(Cur.Max.Z, S.Max.Z) - FMath::Max(Cur.Min.Z, S.Min.Z);
						if (OverZ < 6.f || S.Max.Z < Cur.Min.Z + FMath::Max(Height * 0.35f, 25.f))
						{
							continue;
						}
						// Out along the shallower horizontal axis, away from the obstacle.
						const float OverX = FMath::Min(Cur.Max.X, S.Max.X) - FMath::Max(Cur.Min.X, S.Min.X);
						const float OverY = FMath::Min(Cur.Max.Y, S.Max.Y) - FMath::Max(Cur.Min.Y, S.Min.Y);
						bHit = true;
						if (OverX <= OverY)
						{
							const float D = Cur.GetCenter().X < S.GetCenter().X ? -OverX : OverX;
							PushX = FMath::Abs(D) > FMath::Abs(PushX) ? D : PushX;
						}
						else
						{
							const float D = Cur.GetCenter().Y < S.GetCenter().Y ? -OverY : OverY;
							PushY = FMath::Abs(D) > FMath::Abs(PushY) ? D : PushY;
						}
					}
				});
				bClear = !bHit;
				if (bHit)
				{
					Offset += FVector(PushX, PushY, 0.f);
				}
			}
			return bClear && Offset.Size2D() <= (P.bTree ? 160.f : 70.f);
		};

		FTransform World;
		FVector Offset = FVector::ZeroVector;
		// Ceiling lamps and wall-mounted fittings sit against the structure: each is checked for something to hang on
		// (a wall behind, a ceiling above) and pulled onto it, or left out rather than float in the air.
		const FString IdStr = P.Id.ToString();
		if ((P.bHanging || P.bWallMounted) && (IdStr.Contains(TEXT("shutter")) || IdStr.Contains(TEXT("door"))))
		{
			// Shutters and doors fill an opening: there is nothing behind them by design.
			World = P.Local * Placement;
		}
		else if (P.bHanging || P.bWallMounted)
		{
			auto SolidAt = [&](const FVector& Pt)
			{
				const TArray<int32>* List = Grid.Find(FIntPoint(FMath::FloorToInt(Pt.X / Cell), FMath::FloorToInt(Pt.Y / Cell)));
				return List && List->ContainsByPredicate([&](int32 Index) { return SolidBoxes[Index].ExpandBy(0.5f).IsInside(Pt); });
			};
			// Mounts the prop facing Yaw: finds the structure behind it (above it when hanging) within Reach and seats
			// the mounting face on it. False when there is nothing there to bear it.
			auto TryMount = [&](float Yaw, float Reach, FTransform& OutWorld) -> bool
			{
				const FTransform At(FRotator(0.f, Yaw - 90.f, 0.f), P.Location);
				OutWorld = P.Local * At;
				const FBox Placed = P.Bounds.TransformBy(OutWorld);
				const FVector Into = P.bHanging ? FVector::UpVector : At.TransformVectorNoScale(FVector(0.f, -1.f, 0.f)).GetSafeNormal();
				// Probe across the mounting face (centre and four inset points): most of it has to bear on the structure.
				const FVector Side = P.bHanging ? FVector::ForwardVector : FVector::CrossProduct(FVector::UpVector, Into).GetSafeNormal();
				const FVector Up = P.bHanging ? FVector::RightVector : FVector::UpVector;
				const FVector Half = Placed.GetExtent();
				const float SpanSide = 0.7f * FMath::Abs(FVector::DotProduct(Half, Side.GetAbs()));
				const float SpanUp = 0.7f * FMath::Abs(FVector::DotProduct(Half, Up.GetAbs()));
				const FVector Face = P.bHanging ? FVector(Placed.GetCenter().X, Placed.GetCenter().Y, Placed.Max.Z) : P.Location + FVector(0.f, 0.f, Half.Z);
				const FVector Probes[] = { Face, Face + Side * SpanSide, Face - Side * SpanSide, Face + Up * SpanUp, Face - Up * SpanUp };
				auto Hits = [&](float D)
				{
					int32 N = 0;
					for (const FVector& Pr : Probes)
					{
						N += SolidAt(Pr + Into * D) ? 1 : 0;
					}
					return N;
				};
				float Gap = -1.f;
				for (float D = 0.f; D <= Reach; D += 2.f)
				{
					if (Hits(D) >= 3)
					{
						Gap = D;
						break;
					}
				}
				if (Gap < 0.f)
				{
					return false;
				}
				// Set into the wall: back out until most of the face is clear of it.
				float Buried = 0.f;
				while (Gap == 0.f && Buried < 30.f && Hits(-(Buried + 2.f)) >= 3)
				{
					Buried += 2.f;
				}
				OutWorld.AddToTranslation(Into * (FMath::Max(Gap - 0.5f, 0.f) - Buried));
				return true;
			};
			bool bMounted = TryMount(P.FacingYaw, P.bHanging ? 160.f : 120.f, World);
			// Nothing behind it as authored: turn it to the nearest wall at hand instead (a camera on the column beside it).
			for (const float Turn : { 90.f, -90.f, 180.f })
			{
				if (bMounted || P.bHanging)
				{
					break;
				}
				bMounted = TryMount(P.FacingYaw + Turn, 60.f, World);
			}
			if (!bMounted)
			{
				UE_LOG(LogCampus, Display, TEXT("Props: %s at %s has nothing to %s, left out"), *IdStr, *P.Location.ToCompactString(), P.bHanging ? TEXT("hang from") : TEXT("mount on"));
				++Dropped;
				++DroppedIds.FindOrAdd(P.Id);
				continue;
			}
			Nudged += World.GetTranslation().Equals((P.Local * Placement).GetTranslation(), 0.5f) ? 0 : 1;
		}
		else
		{
			bool bFits = Fit(1.f, World, Offset);
			// Trees brushing a roof or an eave, and bushes against a wall, are shrunk to fit before anything else.
			for (const float Factor : { 0.8f, 0.65f })
			{
				if (bFits || !(P.bTree || P.bFoliage))
				{
					break;
				}
				bFits = Fit(Factor, World, Offset);
			}
			if (!bFits && P.bFoliage)
			{
				// Planted where it was, at the smaller size.
				Offset = FVector::ZeroVector;
				bFits = true;
			}
			if (!bFits)
			{
				// Better missing than half buried in a wall.
				++Dropped;
				++DroppedIds.FindOrAdd(P.Id);
				continue;
			}
			Nudged += Offset.IsNearlyZero() ? 0 : 1;
		}
		FTransform Placed = World;
		Placed.AddToTranslation(Offset);
		for (UStaticMesh* Mesh : P.Pieces)
		{
			FPropBatch* Batch = PropBatches.FindByPredicate([&](const FPropBatch& Bt) { return Bt.Mesh == Mesh && Bt.bSolid == P.bSolid; });
			if (!Batch)
			{
				Batch = &PropBatches.AddDefaulted_GetRef();
				Batch->Mesh = Mesh;
				Batch->bSolid = P.bSolid;
				Batch->bFoliage = P.bFoliage;
				Batch->bGrass = P.Id.ToString().Contains(TEXT("grass"));
				Batch->Impact = P.Impact;
			}
			Batch->Transforms.Add(Placed);
		}
		if (bAudit)
		{
			AuditBoxes.Add({ P.Bounds.TransformBy(Placed), (EPal)((int32)EPal::Count + 1), 9, false });
		}
	}
	FString DroppedList;
	for (const TPair<FName, int32>& Pair : DroppedIds)
	{
		DroppedList += FString::Printf(TEXT(" %s x%d"), *Pair.Key.ToString(), Pair.Value);
	}
	UE_LOG(LogCampus, Display, TEXT("Props: %d placed (%d nudged clear of the structure), %d left out because they could not fit:%s"),
		PendingProps.Num() - Dropped, Nudged, Dropped, *DroppedList);
	PendingProps.Reset();
}

FVector FCampusSchoolBuildContext::PropVariantSize(FName Id, const TCHAR* Variant) const
{
	const FCampusPropEntry* Entry = Library ? Library->Find(Id) : nullptr;
	if (!Entry)
	{
		return FVector::ZeroVector;
	}
	FBox Bounds(ForceInit);
	for (UStaticMesh* Mesh : Entry->Meshes)
	{
		const FString Name = Mesh ? Mesh->GetName() : FString();
		if (Mesh && (!Variant || Name.EndsWith(Variant) || Name.StartsWith(Variant)))
		{
			Bounds += Mesh->GetBoundingBox();
		}
	}
	return Bounds.IsValid ? Bounds.GetSize() : FVector::ZeroVector;
}

void FCampusSchoolBuildContext::Plant(FName Id, TConstArrayView<const TCHAR*> Variants, const FBox2D& Area, float Z, float Spacing, float SizeMin, float SizeMax, int32 Seed, bool bAbsolute, float Overhang)
{
	if (!HasProp(Id) || Variants.Num() == 0 || Spacing < 5.f)
	{
		return;
	}
	TArray<FVector, TInlineAllocator<16>> Sizes;
	for (const TCHAR* V : Variants)
	{
		Sizes.Add(PropVariantSize(Id, V));
	}
	// Plants may lean a little over the edge of their bed, never out across the paving.
	const FBox2D Allowed = Area.ExpandBy(Overhang);
	int32 Index = 0;
	for (float X = Area.Min.X + Spacing * 0.5f; X < Area.Max.X; X += Spacing)
	{
		for (float Y = Area.Min.Y + Spacing * 0.5f; Y < Area.Max.Y; Y += Spacing)
		{
			const uint32 HA = Hash(Index++ * 31 + Seed * 7919, Seed * 7 + 3);
			const uint32 HB = Hash((int32)HA, Seed + 11);
			const float JX = ((HA & 0xFF) / 255.f - 0.5f) * Spacing * 0.9f;
			const float JY = (((HA >> 8) & 0xFF) / 255.f - 0.5f) * Spacing * 0.9f;
			const int32 V = (int32)((HA >> 16) % (uint32)Variants.Num());
			const float Size = FMath::Lerp(SizeMin, SizeMax, (HB & 0xFFFF) / 65535.f);
			const FVector& Raw = Sizes[V];
			float ClumpHeight = bAbsolute ? Size : (Raw.Z > 1.f ? Raw.Z * Size : 0.f);
			const FVector P(FMath::Clamp(X + JX, Area.Min.X, Area.Max.X), FMath::Clamp(Y + JY, Area.Min.Y, Area.Max.Y), Z);
			const float Reach = Raw.Z > 1.f ? 0.5f * FMath::Max(Raw.X, Raw.Y) * ClumpHeight / Raw.Z : 0.f;
			const float Room = FMath::Min(FMath::Min(P.X - Allowed.Min.X, Allowed.Max.X - P.X), FMath::Min(P.Y - Allowed.Min.Y, Allowed.Max.Y - P.Y));
			if (Reach > Room)
			{
				if (Room < Reach * 0.6f)
				{
					continue;
				}
				ClumpHeight *= Room / Reach;
			}
			Prop(Id, P, (float)((HB >> 16) % 360), ClumpHeight, false, Variants[V]);
		}
	}
}

void FCampusSchoolBuildContext::ClippedHedge(const FBox& Hedge, int32 Seed)
{
	// A clipped hedge (月橘 / 黄杨 rows in school planters): one trimmed block of small leaves with soft, rounded
	// shoulders (the hedge surface material), and over it the growth since the last trim: runs of the top and faces a
	// centimetre or two proud of the cut, of uneven length, so the top undulates gently and the faces are not planes.
	const FVector Size = Hedge.GetSize();
	const bool bAlongX = Size.X >= Size.Y;
	const float Len = bAlongX ? Size.X : Size.Y;
	const float Depth = bAlongX ? Size.Y : Size.X;
	const float Mid = bAlongX ? Hedge.GetCenter().Y : Hedge.GetCenter().X;
	const float Start = bAlongX ? Hedge.Min.X : Hedge.Min.Y;
	auto At = [&](float Along, float Across, float Z) { return bAlongX ? FVector(Start + Along, Across, Z) : FVector(Across, Start + Along, Z); };
	auto Span = [&](float A0, float A1, float C0, float C1, float Z0, float Z1)
	{
		const FVector P0 = At(A0, C0, Z0);
		const FVector P1 = At(A1, C1, Z1);
		Deco(EPal::Hedge, P0.ComponentMin(P1), P0.ComponentMax(P1));
	};
	auto R = [&](int32 k, int32 Salt) { return (Hash(Seed * 977 + k * 31, Salt) & 0xFFFF) / 65535.f; };
	// Modelled sections (CampusWeaponKit::BuildHedge): leaves all over a rounded cut, so the outline is leafy and
	// soft, not the straight edge of a box. Three of them mixed and turned end for end, each stretched to its share.
	static const TCHAR* const SectionPaths[] = {
		TEXT("/Game/Campus/Meshes/SM_Hedge_Section_0.SM_Hedge_Section_0"), TEXT("/Game/Campus/Meshes/SM_Hedge_Section_1.SM_Hedge_Section_1"),
		TEXT("/Game/Campus/Meshes/SM_Hedge_Section_2.SM_Hedge_Section_2") };
	TArray<UStaticMesh*, TInlineAllocator<3>> Sections;
	for (const TCHAR* Path : SectionPaths)
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sections.Add(Mesh);
		}
	}
	UStaticMesh* EndPiece = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Campus/Meshes/SM_Hedge_End.SM_Hedge_End"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Sections.Num() > 0 && EndPiece)
	{
		const int32 Count = FMath::Max(2, FMath::RoundToInt(Len / 100.f));
		const float SegLen = Len / Count;
		for (int32 i = 0; i < Count; ++i)
		{
			// The two ends are the rounded pieces, closed end outward; the run between them mixes the three lengths.
			const bool bFirst = i == 0;
			const bool bLast = i == Count - 1;
			UStaticMesh* Mesh = (bFirst || bLast) ? EndPiece : Sections[Hash(Seed, i * 13 + 5) % Sections.Num()];
			FPendingProp& P = PendingProps.AddDefaulted_GetRef();
			P.Id = TEXT("hedge_section");
			P.Pieces.Add(Mesh);
			P.Bounds = Mesh->GetBoundingBox();
			P.Local = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(SegLen / 100.f, Depth / 44.f, Size.Z / 64.f * (0.98f + 0.04f * R(i, 21))));
			P.Location = At((i + 0.5f) * SegLen, Mid, Hedge.Min.Z);
			// The model runs along its X; placements turn it by FacingYaw - 90.
			const bool bTurned = bFirst || (!bLast && R(i, 22) < 0.5f);
			P.FacingYaw = (bAlongX ? 90.f : 180.f) + (bTurned ? 180.f : 0.f);
			P.bSolid = false;
			P.bFoliage = true;
			P.Impact = 3;
		}
		Blocker(Hedge.Min, Hedge.Max);
		return;
	}
	Deco(EPal::Hedge, Hedge.Min, Hedge.Max);
	int32 k = 0;
	for (float T = 0.f; T < Len - 10.f; ++k)
	{
		const float L = FMath::Min(25.f + 45.f * R(k, 1), Len - T);
		// Top: inset from the shoulders so the rounding still shows, 0.5..2.5 cm proud.
		if (R(k, 2) < 0.7f)
		{
			const float Inset = 4.f + 6.f * R(k, 3);
			Span(T + 2.f, T + L - 2.f, Mid - Depth * 0.5f + Inset, Mid + Depth * 0.5f - Inset - 2.f * R(k, 4), Hedge.Max.Z - 3.f, Hedge.Max.Z + 0.5f + 2.f * R(k, 5));
		}
		// Faces: a bulge on one side or the other, short of the top and the soil.
		for (const float Side : { -1.f, 1.f })
		{
			if (R(k, Side > 0.f ? 6 : 7) < 0.55f)
			{
				const float Out = 0.6f + 1.6f * R(k, Side > 0.f ? 8 : 9);
				const float Face = Mid + Side * Depth * 0.5f;
				Span(T + 3.f, T + L - 3.f, Face - Side * 3.f, Face + Side * Out, Hedge.Min.Z + 6.f + 8.f * R(k, 10), Hedge.Max.Z - 5.f - 6.f * R(k, 11));
			}
		}
		T += L;
	}
	Blocker(Hedge.Min, Hedge.Max);
}

bool FCampusSchoolBuildContext::Turf(const FBox2D& Area, float Z, int32 Seed)
{
	// Modelled turf (CampusWeaponKit::BuildTurf): square-metre patches of blades tiled over the lawn, each turned a
	// quarter at random and stretched to its cell, so the sward runs on with no seams and no visible repeat.
	static const TCHAR* const Paths[] = { TEXT("/Game/Campus/Meshes/SM_Turf_Patch_0.SM_Turf_Patch_0"), TEXT("/Game/Campus/Meshes/SM_Turf_Patch_1.SM_Turf_Patch_1") };
	TArray<UStaticMesh*, TInlineAllocator<2>> Patches;
	for (const TCHAR* Path : Paths)
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Patches.Add(Mesh);
		}
	}
	if (Patches.Num() == 0)
	{
		return false;
	}
	const FVector2D Size = Area.GetSize();
	const int32 NX = FMath::Max(1, FMath::CeilToInt(Size.X / 100.f));
	const int32 NY = FMath::Max(1, FMath::CeilToInt(Size.Y / 100.f));
	const FVector2D Cell(Size.X / NX, Size.Y / NY);
	for (int32 X = 0; X < NX; ++X)
	{
		for (int32 Y = 0; Y < NY; ++Y)
		{
			const uint32 Pick = Hash(Seed * 131 + X * 31, Y * 17 + 3);
			UStaticMesh* Mesh = Patches[Pick % Patches.Num()];
			const int32 Quarter = (Pick >> 4) % 4;
			// A quarter turn swaps which way the cell's sides run.
			const FVector2D Fit = (Quarter & 1) ? FVector2D(Cell.Y, Cell.X) : Cell;
			FPendingProp& P = PendingProps.AddDefaulted_GetRef();
			P.Id = TEXT("turf_grass");
			P.Pieces.Add(Mesh);
			P.Bounds = Mesh->GetBoundingBox();
			P.Local = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(Fit.X / 100.f, Fit.Y / 100.f, 0.85f + 0.3f * ((Pick >> 8) & 0xFF) / 255.f));
			P.Location = FVector(Area.Min.X + (X + 0.5f) * Cell.X, Area.Min.Y + (Y + 0.5f) * Cell.Y, Z);
			P.FacingYaw = 90.f + Quarter * 90.f;
			P.bSolid = false;
			P.bFoliage = true;
			P.Impact = 3;
		}
	}
	return true;
}

bool FCampusSchoolBuildContext::Prop(FName Id, const FVector& Location, float Yaw, float Height, bool bSolid, const TCHAR* Variant, const TCHAR* Exclude)
{
	return PlaceProp(Id, Location, Yaw, Height, bSolid, (uint8)EAnchor::Bottom, Variant, Exclude);
}

bool FCampusSchoolBuildContext::PropHang(FName Id, const FVector& Ceiling, float Yaw, const TCHAR* Variant)
{
	return PlaceProp(Id, Ceiling, Yaw, 0.f, false, (uint8)EAnchor::Top, Variant, nullptr);
}

bool FCampusSchoolBuildContext::PropOnWall(FName Id, const FVector& WallPoint, float Yaw, float Height, bool bSolid, const TCHAR* Variant, const TCHAR* Exclude)
{
	return PlaceProp(Id, WallPoint, Yaw, Height, bSolid, (uint8)EAnchor::Back, Variant, Exclude);
}

void FCampusSchoolBuildContext::Box(EPal P, const FVector& Center, const FVector& Size, bool bSolid, float Yaw)
{
	Add(ECampusShape::Cube, P, FTransform(FRotator(0.f, Yaw, 0.f), Center, Size / 100.f), bSolid);
}

void FCampusSchoolBuildContext::BoxMM(EPal P, const FVector& Min, const FVector& Max, bool bSolid)
{
	const FVector Size = (Max - Min).GetAbs();
	if (Size.X < 0.1f || Size.Y < 0.1f || Size.Z < 0.1f)
	{
		return;
	}
	Box(P, (Min + Max) * 0.5f, Size, bSolid);
}

void FCampusSchoolBuildContext::BoxR(EPal P, const FVector& Center, const FVector& Size, const FRotator& Rot, bool bSolid)
{
	Add(ECampusShape::Cube, P, FTransform(Rot, Center, Size / 100.f), bSolid);
}

void FCampusSchoolBuildContext::Cyl(EPal P, const FVector& Base, float Diameter, float Height, bool bSolid)
{
	Add(ECampusShape::Cylinder, P, FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, Height * 0.5f), FVector(Diameter, Diameter, Height) / 100.f), bSolid);
}

void FCampusSchoolBuildContext::CylR(EPal P, const FVector& Center, float Diameter, float Length, const FRotator& Rot, bool bSolid)
{
	Add(ECampusShape::Cylinder, P, FTransform(Rot, Center, FVector(Diameter, Diameter, Length) / 100.f), bSolid);
}

void FCampusSchoolBuildContext::Sphere(EPal P, const FVector& Center, float Diameter, bool bSolid)
{
	Add(ECampusShape::Sphere, P, FTransform(FRotator::ZeroRotator, Center, FVector(Diameter / 100.f)), bSolid);
}

void FCampusSchoolBuildContext::Cone(EPal P, const FVector& Base, float Diameter, float Height, bool bSolid)
{
	Add(ECampusShape::Cone, P, FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, Height * 0.5f), FVector(Diameter, Diameter, Height) / 100.f), bSolid);
}

void FCampusSchoolBuildContext::Blocker(const FVector& Min, const FVector& Max)
{
	const FVector Size = (Max - Min).GetAbs();
	Blockers.Transforms.Add(FTransform(FRotator::ZeroRotator, (Min + Max) * 0.5f, Size / 100.f));
	Blockers.Custom.Append({ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f });
}

void FCampusSchoolBuildContext::Ramp(const FVector& Bottom, const FVector& Top, float Width, bool bAlongX)
{
	const FVector D = Top - Bottom;
	const float Horizontal = FVector2D(D.X, D.Y).Size();
	const float Length = D.Size();
	const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(D.Z, Horizontal));
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	const FRotator Rot(Pitch, Yaw, 0.f);
	const float Thick = 8.f;
	const FVector Center = (Bottom + Top) * 0.5f - Rot.RotateVector(FVector::UpVector) * (Thick * 0.5f - 1.f);
	Ramps.Transforms.Add(FTransform(Rot, Center, FVector(Length + 10.f, Width, Thick) / 100.f));
	Ramps.Custom.Append({ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f });
}

void FCampusSchoolBuildContext::GunfightWall(const FVector& Min, const FVector& Max)
{
	const FVector Size = (Max - Min).GetAbs();
	GunfightBlockers.Transforms.Add(FTransform(FRotator::ZeroRotator, (Min + Max) * 0.5f, Size / 100.f));
	GunfightBlockers.Custom.Append({ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f });

	const bool bAlongX = Size.X >= Size.Y;
	const float Length = bAlongX ? Size.X : Size.Y;
	const int32 Segments = FMath::Max(1, FMath::RoundToInt(Length / 200.f));
	for (int32 i = 0; i < Segments; ++i)
	{
		const float T0 = (float)i / Segments;
		const float T1 = (float)(i + 1) / Segments;
		const FVector C = bAlongX
			? FVector(FMath::Lerp(Min.X, Max.X, (T0 + T1) * 0.5f), (Min.Y + Max.Y) * 0.5f, Min.Z + 55.f)
			: FVector((Min.X + Max.X) * 0.5f, FMath::Lerp(Min.Y, Max.Y, (T0 + T1) * 0.5f), Min.Z + 55.f);
		const FVector S = bAlongX ? FVector(Length / Segments - 10.f, 30.f, 110.f) : FVector(30.f, Length / Segments - 10.f, 110.f);
		const FPalEntry& E = Pal(i % 2 == 0 ? EPal::BarrierYellow : EPal::BarrierBlack);
		GunfightVisuals.Transforms.Add(FTransform(FRotator::ZeroRotator, C, S / 100.f));
		GunfightVisuals.Custom.Append({ E.Color.R, E.Color.G, E.Color.B, E.Rough, E.Metal, E.Emissive, E.Tile, E.Dirt, (float)S.X * 0.5f, (float)S.Y * 0.5f, (float)S.Z * 0.5f });
	}
}

// ------------------------------------------------------------------ walls

void FCampusSchoolBuildContext::WallX(EPal P, float Y, float X0, float X1, float Z0, float Z1, float Thick, TArray<FOpening> Openings, EPal Skin, float SkinSide)
{
	auto Seg = [&](const FVector& Min, const FVector& Max)
	{
		BoxMM(P, Min, Max);
		if (Skin != EPal::Count)
		{
			const float Face = SkinSide > 0.f ? Max.Y : Min.Y;
			BoxMM(Skin, FVector(Min.X, FMath::Min(Face, Face + SkinSide * 1.5f), Min.Z), FVector(Max.X, FMath::Max(Face, Face + SkinSide * 1.5f), Max.Z), false);
		}
	};
	Openings.Sort([](const FOpening& A, const FOpening& Bo) { return A.A0 < Bo.A0; });
	float Cursor = X0;
	const float Y0 = Y - Thick * 0.5f;
	const float Y1 = Y + Thick * 0.5f;
	for (const FOpening& O : Openings)
	{
		const float A0 = FMath::Clamp(O.A0, X0, X1);
		const float A1 = FMath::Clamp(O.A1, X0, X1);
		if (A0 > Cursor)
		{
			Seg(FVector(Cursor, Y0, Z0), FVector(A0, Y1, Z1));
		}
		if (O.Z0 > Z0)
		{
			Seg(FVector(A0, Y0, Z0), FVector(A1, Y1, O.Z0));
		}
		if (O.Z1 < Z1)
		{
			Seg(FVector(A0, Y0, O.Z1), FVector(A1, Y1, Z1));
		}
		if (bAudit && A1 - A0 > 10.f)
		{
			AuditOpenings.Add(FBox(FVector(A0 + 3.f, Y0 - 12.f, O.Z0 + 3.f), FVector(A1 - 3.f, Y1 + 12.f, O.Z1 - 6.f)));
		}
		if (O.bWindow)
		{
			Deco(WindowTrim, FVector(A0, Y0 - 2.f, O.Z0 - 3.f), FVector(A1, Y1 + 2.f, O.Z0));
			Deco(WindowTrim, FVector(A0, Y0 - 1.f, O.Z1), FVector(A1, Y1 + 1.f, O.Z1 + 3.f));
			SlidingWindow(false, Y, A0, A1, O.Z0, O.Z1);
		}
		else
		{
			Deco(DoorTrim, FVector(A0 - 4.f, Y0 - 2.f, O.Z0), FVector(A0, Y1 + 2.f, O.Z1 + 4.f));
			Deco(DoorTrim, FVector(A1, Y0 - 2.f, O.Z0), FVector(A1 + 4.f, Y1 + 2.f, O.Z1 + 4.f));
			Deco(DoorTrim, FVector(A0, Y0 - 2.f, O.Z1), FVector(A1, Y1 + 2.f, O.Z1 + 4.f));
			if (A1 - A0 <= 170.f && O.Z1 - O.Z0 <= 260.f)
			{
				Doors.Add({ FVector(A0, Y, O.Z0), 0.f, A1 - A0, O.Z1 - O.Z0 });
			}
		}
		Cursor = FMath::Max(Cursor, A1);
	}
	if (Cursor < X1)
	{
		Seg(FVector(Cursor, Y0, Z0), FVector(X1, Y1, Z1));
	}
}

void FCampusSchoolBuildContext::WindowPane(const FVector& Min, const FVector& Max, bool bTempered, bool bFrameless)
{
	const UWorld* World = B->GetWorld();
	if (World && World->IsGameWorld())
	{
		WindowPanes.Add(FBox(Min, Max));
		WindowPaneTempered.Add(bTempered ? (bFrameless ? 2 : 1) : 0);
	}
	else
	{
		Deco(EPal::GlassClear, Min, Max);
	}
}

void FCampusSchoolBuildContext::SlidingWindow(bool bAlongY, float Fixed, float A0, float A1, float Z0, float Z1)
{
	// (along, across) -> world, across measured from the wall's centre line.
	auto Piece = [&](EPal Pal, float L0, float L1, float C0, float C1, float H0, float H1)
	{
		const FVector Min = bAlongY ? FVector(Fixed + C0, L0, H0) : FVector(L0, Fixed + C0, H0);
		const FVector Max = bAlongY ? FVector(Fixed + C1, L1, H1) : FVector(L1, Fixed + C1, H1);
		Deco(Pal, Min, Max);
	};
	const EPal Frame = WindowTrim;
	// Outer frame: jambs, head and the sill track, 10 cm deep.
	Piece(Frame, A0, A0 + 5.f, -5.f, 5.f, Z0, Z1);
	Piece(Frame, A1 - 5.f, A1, -5.f, 5.f, Z0, Z1);
	Piece(Frame, A0, A1, -5.f, 5.f, Z1 - 5.f, Z1);
	Piece(Frame, A0, A1, -5.f, 5.f, Z0, Z0 + 3.f);
	// Two sashes, each a little over half the opening: the inner one stays put on its half, the outer one has been slid
	// across in front of it, so the other half is open air.
	const float Mid = (A0 + A1) * 0.5f;
	const float S0 = Z0 + 3.f;
	const float S1 = Z1 - 5.f;
	auto Sash = [&](float L0, float L1, float Track, bool bGlass)
	{
		const float C0 = Track - 1.6f;
		const float C1 = Track + 1.6f;
		Piece(Frame, L0, L0 + 4.f, C0, C1, S0, S1);
		Piece(Frame, L1 - 4.f, L1, C0, C1, S0, S1);
		Piece(Frame, L0, L1, C0, C1, S1 - 5.f, S1);
		Piece(Frame, L0, L1, C0, C1, S0, S0 + 6.f);
		// The meeting rail a third of the way up, as on most school windows.
		const float Rail = S0 + (S1 - S0) * 0.36f;
		Piece(Frame, L0 + 4.f, L1 - 4.f, C0 + 0.4f, C1 - 0.4f, Rail - 1.5f, Rail + 1.5f);
		if (bGlass)
		{
			const FVector Min = bAlongY ? FVector(Fixed + Track - 0.6f, L0 + 4.f, S0 + 6.f) : FVector(L0 + 4.f, Fixed + Track - 0.6f, S0 + 6.f);
			const FVector Max = bAlongY ? FVector(Fixed + Track + 0.6f, L1 - 4.f, S1 - 5.f) : FVector(L1 - 4.f, Fixed + Track + 0.6f, S1 - 5.f);
			WindowPane(Min, Max);
		}
	};
	Sash(A0 + 5.f, Mid + 2.f, -2.f, true);
	Sash(A0 + 9.f, Mid + 6.f, 2.f, false);
}

void FCampusSchoolBuildContext::WaterBody(const FBox& Volume)
{
	const UWorld* World = B->GetWorld();
	if (World && World->IsGameWorld())
	{
		WaterVolumes.Add(Volume);
	}
	else
	{
		Deco(EPal::Water, FVector(Volume.Min.X, Volume.Min.Y, Volume.Max.Z - 2.f), Volume.Max);
	}
}

void FCampusSchoolBuildContext::GlazingBlocker(const FVector& Min, const FVector& Max)
{
	const UWorld* World = B->GetWorld();
	if (!World || !World->IsGameWorld())
	{
		Blocker(Min, Max);
	}
}

void FCampusSchoolBuildContext::WallY(EPal P, float X, float Y0, float Y1, float Z0, float Z1, float Thick, TArray<FOpening> Openings, EPal Skin, float SkinSide)
{
	auto Seg = [&](const FVector& Min, const FVector& Max)
	{
		BoxMM(P, Min, Max);
		if (Skin != EPal::Count)
		{
			const float Face = SkinSide > 0.f ? Max.X : Min.X;
			BoxMM(Skin, FVector(FMath::Min(Face, Face + SkinSide * 1.5f), Min.Y, Min.Z), FVector(FMath::Max(Face, Face + SkinSide * 1.5f), Max.Y, Max.Z), false);
		}
	};
	Openings.Sort([](const FOpening& A, const FOpening& Bo) { return A.A0 < Bo.A0; });
	float Cursor = Y0;
	const float X0 = X - Thick * 0.5f;
	const float X1 = X + Thick * 0.5f;
	for (const FOpening& O : Openings)
	{
		const float A0 = FMath::Clamp(O.A0, Y0, Y1);
		const float A1 = FMath::Clamp(O.A1, Y0, Y1);
		if (A0 > Cursor)
		{
			Seg(FVector(X0, Cursor, Z0), FVector(X1, A0, Z1));
		}
		if (O.Z0 > Z0)
		{
			Seg(FVector(X0, A0, Z0), FVector(X1, A1, O.Z0));
		}
		if (O.Z1 < Z1)
		{
			Seg(FVector(X0, A0, O.Z1), FVector(X1, A1, Z1));
		}
		if (bAudit && A1 - A0 > 10.f)
		{
			AuditOpenings.Add(FBox(FVector(X0 - 12.f, A0 + 3.f, O.Z0 + 3.f), FVector(X1 + 12.f, A1 - 3.f, O.Z1 - 6.f)));
		}
		if (O.bWindow)
		{
			Deco(WindowTrim, FVector(X0 - 2.f, A0, O.Z0 - 3.f), FVector(X1 + 2.f, A1, O.Z0));
			Deco(WindowTrim, FVector(X0 - 1.f, A0, O.Z1), FVector(X1 + 1.f, A1, O.Z1 + 3.f));
			SlidingWindow(true, X, A0, A1, O.Z0, O.Z1);
		}
		else
		{
			Deco(DoorTrim, FVector(X0 - 2.f, A0 - 4.f, O.Z0), FVector(X1 + 2.f, A0, O.Z1 + 4.f));
			Deco(DoorTrim, FVector(X0 - 2.f, A1, O.Z0), FVector(X1 + 2.f, A1 + 4.f, O.Z1 + 4.f));
			Deco(DoorTrim, FVector(X0 - 2.f, A0, O.Z1), FVector(X1 + 2.f, A1, O.Z1 + 4.f));
			if (A1 - A0 <= 170.f && O.Z1 - O.Z0 <= 260.f)
			{
				Doors.Add({ FVector(X, A0, O.Z0), 90.f, A1 - A0, O.Z1 - O.Z0 });
			}
		}
		Cursor = FMath::Max(Cursor, A1);
	}
	if (Cursor < Y1)
	{
		Seg(FVector(X0, Cursor, Z0), FVector(X1, Y1, Z1));
	}
}

void FCampusSchoolBuildContext::FacadeWindowsY(float X, float Y0, float Y1, float Z0, float Z1, float Spacing, float Outward)
{
	const int32 Count = FMath::Max(1, FMath::FloorToInt((Y1 - Y0) / Spacing));
	const float Step = (Y1 - Y0) / Count;
	for (float Floor = Z0; Floor + 300.f <= Z1 + 1.f; Floor += H)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float C = Y0 + Step * (i + 0.5f);
			const float W = Step * 0.62f;
			Deco(EPal::Glass, FVector(X + Outward * 0.f, C - W * 0.5f, Floor + 95.f), FVector(X + Outward * 3.f, C + W * 0.5f, Floor + 250.f));
			Deco(EPal::WindowFrame, FVector(X, C - W * 0.5f - 4.f, Floor + 90.f), FVector(X + Outward * 6.f, C + W * 0.5f + 4.f, Floor + 95.f));
			Deco(EPal::WindowFrame, FVector(X, C - 2.f, Floor + 95.f), FVector(X + Outward * 4.f, C + 2.f, Floor + 250.f));
		}
	}
}

void FCampusSchoolBuildContext::FacadeWindowsX(float Y, float X0, float X1, float Z0, float Z1, float Spacing, float Outward)
{
	const int32 Count = FMath::Max(1, FMath::FloorToInt((X1 - X0) / Spacing));
	const float Step = (X1 - X0) / Count;
	for (float Floor = Z0; Floor + 300.f <= Z1 + 1.f; Floor += H)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float C = X0 + Step * (i + 0.5f);
			const float W = Step * 0.62f;
			Deco(EPal::Glass, FVector(C - W * 0.5f, Y, Floor + 95.f), FVector(C + W * 0.5f, Y + Outward * 3.f, Floor + 250.f));
			Deco(EPal::WindowFrame, FVector(C - W * 0.5f - 4.f, Y, Floor + 90.f), FVector(C + W * 0.5f + 4.f, Y + Outward * 6.f, Floor + 95.f));
			Deco(EPal::WindowFrame, FVector(C - 2.f, Y, Floor + 95.f), FVector(C + 2.f, Y + Outward * 4.f, Floor + 250.f));
		}
	}
}

// ------------------------------------------------------------------ stairs

void FCampusSchoolBuildContext::Stair(const FBox2D& Well, bool bAlongX, bool bStartAtMax, bool bSideAMin, float Z0, float Z1, bool bHotel)
{
	const int32 Ax = bAlongX ? 0 : 1;
	const int32 Ac = bAlongX ? 1 : 0;
	const float AxMin = Ax == 0 ? Well.Min.X : Well.Min.Y;
	const float AxMax = Ax == 0 ? Well.Max.X : Well.Max.Y;
	const float AcMin = Ac == 0 ? Well.Min.X : Well.Min.Y;
	const float AcMax = Ac == 0 ? Well.Max.X : Well.Max.Y;
	const float Dir = bStartAtMax ? -1.f : 1.f;
	const float Start = bStartAtMax ? AxMax : AxMin;
	const float Far = bStartAtMax ? AxMin : AxMax;
	const float Mid = (AcMin + AcMax) * 0.5f;
	const float AMin = bSideAMin ? AcMin : Mid + 6.f;
	const float AMax = bSideAMin ? Mid - 6.f : AcMax;
	const float BMin = bSideAMin ? Mid + 6.f : AcMin;
	const float BMax = bSideAMin ? AcMax : Mid - 6.f;
	// Arrival strip at the top / foot of the flights: wide enough to walk past the guard rail (the capsule is ~84 cm).
	const float Margin = StairArrival;
	const int32 Steps = 10;
	const float Run = 28.f;
	const float Zm = (Z0 + Z1) * 0.5f;
	const float Rise = (Zm - Z0) / Steps;

	auto P = [Ax](float AxisValue, float AcrossValue, float Z)
	{
		return Ax == 0 ? FVector(AxisValue, AcrossValue, Z) : FVector(AcrossValue, AxisValue, Z);
	};
	auto BoxAxis = [&](EPal Mat, float A0, float A1, float C0, float C1, float Za, float Zb, bool bSolid)
	{
		const FVector Pa = P(FMath::Min(A0, A1), C0, Za);
		const FVector Pb = P(FMath::Max(A0, A1), C1, Zb);
		BoxMM(Mat, FVector(FMath::Min(Pa.X, Pb.X), FMath::Min(Pa.Y, Pb.Y), Za), FVector(FMath::Max(Pa.X, Pb.X), FMath::Max(Pa.Y, Pb.Y), Zb), bSolid);
	};
	// Balustrade glass: tempered panes that shatter (the invisible guard behind them stays).
	auto GlassAxis = [&](float A0, float A1, float C0, float C1, float Za, float Zb)
	{
		const FVector Pa = P(FMath::Min(A0, A1), C0, Za);
		const FVector Pb = P(FMath::Max(A0, A1), C1, Zb);
		WindowPane(FVector(FMath::Min(Pa.X, Pb.X), FMath::Min(Pa.Y, Pb.Y), Za), FVector(FMath::Max(Pa.X, Pb.X), FMath::Max(Pa.Y, Pb.Y), Zb), true, true);
	};

	const float S0 = Start + Dir * Margin;
	const float S1 = S0 + Dir * Run * Steps;
	const EPal Tread = bHotel ? EPal::Marble : EPal::Terrazzo;
	for (int32 i = 0; i < Steps; ++i)
	{
		const float A0 = S0 + Dir * Run * i;
		const float A1 = A0 + Dir * Run;
		BoxAxis(Tread, A0, A1, AMin, AMax, Z0, Z0 + Rise * (i + 1), false);
		// Flight 2 climbs from the landing (S1) back toward S0: box i spans tread i up to the top of the flight, so the
		// stack forms the stair profile.
		BoxAxis(Tread, S1 - Dir * Run * i, S0, BMin, BMax, Zm, Zm + Rise * (i + 1), false);
		if (bHotel)
		{
			// Brass anti-slip nosing at the front edge of every tread.
			const float N1 = A1;
			BoxAxis(EPal::Brass, N1 - Dir * 3.f, N1, AMin + 2.f, AMax - 2.f, Z0 + Rise * (i + 1), Z0 + Rise * (i + 1) + 0.4f, false);
			const float N2 = S1 - Dir * Run * (i + 1);
			BoxAxis(EPal::Brass, N2, N2 + Dir * 3.f, BMin + 2.f, BMax - 2.f, Zm + Rise * (i + 1), Zm + Rise * (i + 1) + 0.4f, false);
		}
	}
	// walkable ramps
	const float ACenter = (AMin + AMax) * 0.5f;
	const float BCenter = (BMin + BMax) * 0.5f;
	Ramp(P(S0 - Dir * 10.f, ACenter, Z0), P(S1, ACenter, Zm), AMax - AMin, bAlongX);
	Ramp(P(S1, BCenter, Zm), P(S0, BCenter, Z1), BMax - BMin, bAlongX);
	Ramp(P(S0, BCenter, Z1), P(S0 - Dir * 15.f, BCenter, Z1), BMax - BMin, bAlongX);
	// landing (solid to the floor) and the solid mass under flight 2
	BoxAxis(Tread, S1, Far, AcMin, AcMax, Z0, Zm, true);
	BoxAxis(bHotel ? EPal::MarbleDark : EPal::ConcreteDark, S0, S1, BMin, BMax, Z0, Zm, true);
	if (bHotel)
	{
		// Between the flights: a low stone upstand carrying a frameless glass balustrade with a brass cap rail (the
		// flights stay open to each other and to the light). The glass stops people until it is shot out; then the
		// gap is open under the (solid) cap rail. The guard behind it is only for the baked navmesh.
		BoxAxis(EPal::MarbleDark, S0, S1, Mid - 6.f, Mid + 6.f, Z0, Zm + 12.f, true);
		GlassAxis(S0, S1, Mid - 1.f, Mid + 1.f, Zm + 12.f, Z1 + 100.f);
		BoxAxis(EPal::Brass, S0, S1, Mid - 3.f, Mid + 3.f, Z1 + 100.f, Z1 + 105.f, true);
		{
			const FVector Ga = P(S0, Mid - 6.f, Zm);
			const FVector Gb = P(S1, Mid + 6.f, Z1 + 105.f);
			GlazingBlocker(FVector(FMath::Min(Ga.X, Gb.X), FMath::Min(Ga.Y, Gb.Y), Zm), FVector(FMath::Max(Ga.X, Gb.X), FMath::Max(Ga.Y, Gb.Y), Z1 + 105.f));
		}
		// Brass wall handrails on brackets along the outer side of both flights, an LED strip under each lighting the
		// treads, and a glazed guard over the opening at the top.
		const float OutA = bSideAMin ? AcMin + 6.f : AcMax - 6.f;
		const float OutB = bSideAMin ? AcMax - 6.f : AcMin + 6.f;
		for (int32 F = 0; F < 2; ++F)
		{
			const float C = F == 0 ? OutA : OutB;
			const float Za = F == 0 ? Z0 : Zm;
			const FVector Lo = F == 0 ? P(S0, C, Za + 90.f) : P(S1, C, Za + 90.f);
			const FVector Hi = F == 0 ? P(S1, C, Zm + 90.f) : P(S0, C, Z1 + 90.f);
			// A cylinder stands along its Z: turn Z onto the run of the rail.
			CylR(EPal::Brass, (Lo + Hi) * 0.5f, 4.5f, FVector::Dist(Lo, Hi), FRotationMatrix::MakeFromZ((Hi - Lo).GetSafeNormal()).Rotator(), false);
			BoxR(EPal::LedWarm, (Lo + Hi) * 0.5f - FVector(0.f, 0.f, 8.f), FVector(FVector::Dist(Lo, Hi), 1.5f, 1.f), (Hi - Lo).Rotation(), false);
		}
		BoxAxis(EPal::Brass, S0 - Dir * 2.5f, S0 + Dir * 2.5f, AMin, AMax, Z1 + 100.f, Z1 + 105.f, true);
		GlassAxis(S0 - Dir * 1.f, S0 + Dir * 1.f, AMin + 2.f, AMax - 2.f, Z1 + 4.f, Z1 + 100.f);
		BoxAxis(EPal::SteelDark, S0 - Dir * 3.f, S0 + Dir * 3.f, AMin, AMax, Z1, Z1 + 6.f, false);
	}
	else
	{
		// divider / handrail
		BoxAxis(EPal::WallWhite, S0, S1, Mid - 6.f, Mid + 6.f, Z0, Z1 + 105.f, true);
		BoxAxis(EPal::Railing, S0, S1, Mid - 3.f, Mid + 3.f, Z1 + 105.f, Z1 + 110.f, false);
		// guard rail at the top over the flight-1 opening: top and bottom rails with square balusters, an invisible
		// blocker keeps it solid
		BoxAxis(EPal::Railing, S0 - Dir * 2.5f, S0 + Dir * 2.5f, AMin, AMax, Z1 + 102.f, Z1 + 108.f, false);
		BoxAxis(EPal::Railing, S0 - Dir * 1.5f, S0 + Dir * 1.5f, AMin, AMax, Z1 + 8.f, Z1 + 12.f, false);
		for (float C = AMin + 8.f; C < AMax - 4.f; C += 11.f)
		{
			BoxAxis(EPal::Railing, S0 - Dir * 1.f, S0 + Dir * 1.f, C - 1.f, C + 1.f, Z1 + 12.f, Z1 + 102.f, false);
		}
	}
	{
		// The hotel's glazed guard stops people with its own glass (and lets them through once it is shot out).
		const FVector Ra = P(S0 - Dir * 4.f, AMin, Z1);
		const FVector Rb = P(S0 + Dir * 4.f, AMax, Z1 + 108.f);
		const FVector GuardMin(FMath::Min(Ra.X, Rb.X), FMath::Min(Ra.Y, Rb.Y), Z1);
		const FVector GuardMax(FMath::Max(Ra.X, Rb.X), FMath::Max(Ra.Y, Rb.Y), Z1 + 108.f);
		if (bHotel)
		{
			GlazingBlocker(GuardMin, GuardMax);
		}
		else
		{
			Blocker(GuardMin, GuardMax);
		}
	}
	// The well has no floor slab: a band between the lower ceiling and the upper floor closes the slot the two storeys'
	// walls would otherwise leave to daylight all round it.
	// Recessed a few millimetres so it never shares a face with the floors and walls it meets.
	const float Band = 26.f;
	const float In = 0.3f;
	const float BandTop = Z1 - In;
	BoxMM(EPal::WallWhite, FVector(Well.Min.X - Band, Well.Min.Y - Band, Z1 - Slab), FVector(Well.Min.X - In, Well.Max.Y + Band, BandTop), true);
	BoxMM(EPal::WallWhite, FVector(Well.Max.X + In, Well.Min.Y - Band, Z1 - Slab), FVector(Well.Max.X + Band, Well.Max.Y + Band, BandTop), true);
	BoxMM(EPal::WallWhite, FVector(Well.Min.X - In, Well.Min.Y - Band, Z1 - Slab), FVector(Well.Max.X + In, Well.Min.Y - In, BandTop), true);
	BoxMM(EPal::WallWhite, FVector(Well.Min.X - In, Well.Max.Y + In, Z1 - Slab), FVector(Well.Max.X + In, Well.Max.Y + Band, BandTop), true);
}

// ------------------------------------------------------------------ props

void FCampusSchoolBuildContext::Dado(EPal P, bool bAlongX, float A0, float A1, float Face0, float Face1, float Z0, float Height, const TArray<FOpening>& Openings)
{
	TArray<FOpening> Gaps;
	for (const FOpening& O : Openings)
	{
		if (O.Z0 < Z0 + Height && O.A1 > A0 && O.A0 < A1)
		{
			Gaps.Add(O);
		}
	}
	Gaps.Sort([](const FOpening& L, const FOpening& R) { return L.A0 < R.A0; });
	float Cursor = A0;
	auto Span = [&](float S0, float S1)
	{
		if (S1 - S0 < 1.f)
		{
			return;
		}
		Deco(P, bAlongX ? FVector(S0, Face0, Z0) : FVector(Face0, S0, Z0), bAlongX ? FVector(S1, Face1, Z0 + Height) : FVector(Face1, S1, Z0 + Height));
	};
	for (const FOpening& O : Gaps)
	{
		Span(Cursor, FMath::Max(O.A0, A0));
		Cursor = FMath::Max(Cursor, O.A1);
	}
	Span(Cursor, A1);
}

void FCampusSchoolBuildContext::Tree(const FVector& Base, float Scale)
{
	{
		const uint32 Seed = Hash((int32)Base.X + 7, (int32)Base.Y);
		const FName Id = Scale >= 1.1f || Seed % 3 == 0 ? FName(TEXT("island_tree_01")) : FName(TEXT("island_tree_02"));
		if (Prop(Id, Base, (float)(Seed % 360), (Id == TEXT("island_tree_01") ? 650.f : 520.f) * Scale))
		{
			return;
		}
	}
	Cyl(EPal::Trunk, Base, 55.f * Scale, 420.f * Scale, true);
	Cyl(EPal::Trunk, Base + FVector(0.f, 0.f, 380.f * Scale), 35.f * Scale, 120.f * Scale, false);
	const uint32 Seed = Hash((int32)Base.X, (int32)Base.Y);
	for (int32 i = 0; i < 5; ++i)
	{
		const float A = (i / 5.f) * 2.f * PI + (Seed % 7);
		const FVector Off(FMath::Cos(A) * 160.f * Scale, FMath::Sin(A) * 160.f * Scale, (440.f + (i % 3) * 70.f) * Scale);
		Sphere(EPal::Leaves, Base + Off, (320.f + ((Seed >> i) % 4) * 40.f) * Scale);
	}
	Sphere(EPal::Leaves, Base + FVector(0.f, 0.f, 600.f * Scale), 420.f * Scale);
}

void FCampusSchoolBuildContext::Bench(const FVector& Base, float Yaw)
{
	if (Prop(TEXT("painted_wooden_bench"), Base, Yaw + 90.f))
	{
		return;
	}
	const FRotator R(0.f, Yaw, 0.f);
	Box(EPal::WoodDesk, Base + R.RotateVector(FVector(0.f, 0.f, 44.f)), FVector(180.f, 45.f, 5.f), true, Yaw);
	Box(EPal::WoodDesk, Base + R.RotateVector(FVector(0.f, -20.f, 75.f)), FVector(180.f, 5.f, 35.f), false, Yaw);
	Box(EPal::Black, Base + R.RotateVector(FVector(-75.f, 0.f, 21.f)), FVector(6.f, 40.f, 42.f), false, Yaw);
	Box(EPal::Black, Base + R.RotateVector(FVector(75.f, 0.f, 21.f)), FVector(6.f, 40.f, 42.f), false, Yaw);
}

void FCampusSchoolBuildContext::StudentDesk(const FVector& Base, float Yaw)
{
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](const FVector& L) { return Base + R.RotateVector(L); };
	// The student faces local +X (the board).
	if (HasProp(TEXT("SchoolDesk_01")) && HasProp(TEXT("SchoolChair_01")))
	{
		Prop(TEXT("SchoolDesk_01"), At(FVector(0.f, 0.f, 0.f)), Yaw + 180.f);
		Prop(TEXT("SchoolChair_01"), At(FVector(-52.f, 0.f, 0.f)), Yaw);
		return;
	}
	Box(EPal::WoodDesk, At(FVector(0.f, 0.f, 73.f)), FVector(45.f, 60.f, 3.f), true, Yaw);
	Box(EPal::DeskFrame, At(FVector(0.f, 0.f, 36.f)), FVector(38.f, 54.f, 70.f), true, Yaw);
	Box(EPal::WoodDesk, At(FVector(-42.f, 0.f, 44.f)), FVector(36.f, 38.f, 3.f), false, Yaw);
	Box(EPal::WoodDesk, At(FVector(-60.f, 0.f, 70.f)), FVector(3.f, 36.f, 30.f), false, Yaw);
	Box(EPal::DeskFrame, At(FVector(-44.f, 0.f, 21.f)), FVector(30.f, 32.f, 42.f), true, Yaw);
}

void FCampusSchoolBuildContext::Bookcase(const FVector& Base, bool bLongX, float Length, int32 Seed)
{
	const FVector U = bLongX ? FVector::XAxisVector : FVector::YAxisVector;
	const FVector V = bLongX ? FVector::YAxisVector : FVector::XAxisVector;
	const float Yaw = bLongX ? 0.f : 90.f;
	// Sizes are (along, across, up); the yaw turns them onto the case's axes.
	auto Part = [&](EPal P, float Along, float Across, float Z, const FVector& Size)
	{
		Box(P, Base + U * Along + V * Across + FVector(0.f, 0.f, Z), Size, false, Yaw);
	};
	auto PartR = [&](EPal P, float Along, float Across, float Z, const FVector& Size, float Lean)
	{
		BoxR(P, Base + U * Along + V * Across + FVector(0.f, 0.f, Z), Size, FRotator(Lean, Yaw, 0.f), false);
	};
	const float Depth = 46.f;
	const float Height = 200.f;
	const float Half = Length * 0.5f;
	const FVector C0 = Base - U * Half - V * (Depth * 0.5f);
	const FVector C1 = Base + U * Half + V * (Depth * 0.5f) + FVector(0.f, 0.f, Height);
	Blocker(C0.ComponentMin(C1), C0.ComponentMax(C1));

	// Carcass: end panels, top, plinth, centre spine, bay dividers and five fixed shelves.
	for (const float S : { -1.f, 1.f })
	{
		Part(EPal::Shelf, S * (Half - 1.25f), 0.f, Height * 0.5f, FVector(2.5f, Depth, Height));
	}
	Part(EPal::Shelf, 0.f, 0.f, Height - 1.25f, FVector(Length + 1.f, Depth + 1.f, 2.5f));
	Part(EPal::Black, 0.f, 0.f, 4.f, FVector(Length - 4.f, Depth - 6.f, 8.f));
	Part(EPal::Shelf, 0.f, 0.f, Height * 0.5f, FVector(Length - 5.f, 1.6f, Height - 10.f));
	const int32 Bays = FMath::Max(1, FMath::RoundToInt((Length - 5.f) / 90.f));
	const float BayW = (Length - 5.f) / Bays;
	for (int32 b = 1; b < Bays; ++b)
	{
		Part(EPal::Shelf, -Half + 2.5f + b * BayW, 0.f, Height * 0.5f, FVector(2.f, Depth, Height - 10.f));
	}
	constexpr int32 Levels = 5;
	const float Pitch = (Height - 2.5f - 9.f) / Levels;
	for (int32 k = 0; k < Levels; ++k)
	{
		Part(EPal::Shelf, 0.f, 0.f, 9.f + k * Pitch + 1.f, FVector(Length - 5.f, Depth, 2.f));
	}

	static const EPal Covers[] = { EPal::BookRed, EPal::BookBlue, EPal::BookGreen, EPal::BookCream, EPal::BookBrown, EPal::BookGray, EPal::BookOchre, EPal::BookTeal,
		EPal::BookCharcoal, EPal::BookCream, EPal::BookBlue, EPal::BookBrown };
	uint32 Counter = (uint32)Seed * 7919u;
	for (const float S : { -1.f, 1.f })
	{
		const float Front = S * (Depth * 0.5f - 0.8f);
		for (int32 k = 0; k < Levels; ++k)
		{
			const float Z0 = 9.f + k * Pitch + 2.f;
			const float MaxH = Pitch - 4.f;
			for (int32 b = 0; b < Bays; ++b)
			{
				const uint32 Shelf = Hash(Seed * 131 + k * 17 + b, S > 0.f ? 7 : 3);
				const float U0 = -Half + 2.5f + b * BayW + (b > 0 ? 1.f : 0.f) + 0.4f;
				float U1 = -Half + 2.5f + (b + 1) * BayW - (b + 1 < Bays ? 1.f : 0.f) - 0.4f;
				// Some compartments are part-borrowed; some end in a pile of books lying flat.
				if (Shelf % 7 == 0)
				{
					U1 = U0 + (U1 - U0) * (0.45f + (Shelf >> 4) % 30 * 0.01f);
				}
				const bool bPile = Shelf % 5 == 1 && U1 - U0 > 50.f;
				const float RowEnd = bPile ? U1 - 31.f : U1;
				float Pos = U0;
				// Books come in runs (a set, a series): same cover and height for a few spines, then a new run.
				uint32 Run = 0;
				int32 RunLeft = 0;
				while (true)
				{
					if (RunLeft <= 0)
					{
						Run = Hash((int32)Counter++, 4243);
						RunLeft = 1 + Run % 6;
					}
					const uint32 Hh = Hash((int32)Counter++, 313);
					--RunLeft;
					const float T = 1.8f + (Hh % 36) * 0.1f;
					if (Pos + T > RowEnd)
					{
						break;
					}
					const float Ht = FMath::Min(MaxH, 19.f + ((Run >> 6) % 100) * 0.12f + ((Hh >> 6) % 4) * 0.3f);
					const float D = 14.f + ((Run >> 13) % 7);
					const EPal Cover = Covers[(Run >> 17) % UE_ARRAY_COUNT(Covers)];
					const float Across = Front - S * (D * 0.5f + ((Hh >> 9) % 4) * 0.35f);
					Part(Cover, Pos + T * 0.5f, Across, Z0 + Ht * 0.5f, FVector(T, D, Ht));
					// Spine label or foil-stamped title band.
					if ((Run >> 22) % 3 != 0)
					{
						const EPal Band = (Run >> 22) % 3 == 2 ? EPal::Gold : (Cover == EPal::BookCream ? EPal::BookBrown : EPal::BookCream);
						Part(Band, Pos + T * 0.5f, Across + S * (D * 0.5f), Z0 + Ht * (0.72f + ((Run >> 25) % 3) * 0.06f), FVector(T * 0.86f, 0.3f, 1.4f));
					}
					Pos += T + ((Hh >> 14) % 9 == 0 ? 0.3f : 0.02f);
				}
				// A gap at the end of a run of books: the last one leans over against its neighbour.
				if (!bPile && U1 - Pos > 14.f && Shelf % 3 == 0)
				{
					const uint32 Hh = Hash((int32)Counter++, 977);
					const float T = 2.2f + (Hh % 20) * 0.1f;
					const float Ht = FMath::Min(MaxH, 21.f + (Hh >> 5) % 8);
					const float D = 15.f + (Hh >> 9) % 5;
					const float Lean = 16.f + (Hh >> 13) % 10;
					const float Rad = FMath::DegreesToRadians(Lean);
					const float CenterU = Pos + T * 0.5f * FMath::Cos(Rad) + Ht * 0.5f * FMath::Sin(Rad);
					PartR(Covers[(Hh >> 17) % UE_ARRAY_COUNT(Covers)], CenterU, Front - S * D * 0.5f, Z0 + Ht * 0.5f * FMath::Cos(Rad) + T * 0.5f * FMath::Sin(Rad), FVector(T, D, Ht), Lean);
				}
				if (bPile)
				{
					float Z = Z0;
					const int32 Count = 3 + Shelf % 4;
					for (int32 i = 0; i < Count && Z < Z0 + MaxH - 5.f; ++i)
					{
						const uint32 Hh = Hash((int32)Counter++, 555);
						const float T = 2.f + (Hh % 25) * 0.1f;
						const float L = 21.f + (Hh >> 5) % 8;
						const float D = 15.f + (Hh >> 9) % 5;
						const float Twist = (float)((int32)((Hh >> 13) % 9) - 4);
						BoxR(Covers[(Hh >> 17) % UE_ARRAY_COUNT(Covers)], Base + U * (U1 - 15.f) + V * (Front - S * D * 0.5f) + FVector(0.f, 0.f, Z + T * 0.5f), FVector(L, D, T), FRotator(0.f, Yaw + Twist, 0.f), false);
						BoxR(EPal::BookPage, Base + U * (U1 - 15.f) + V * (Front - S * D * 0.5f + S * 0.2f) + FVector(0.f, 0.f, Z + T * 0.5f), FVector(L - 0.8f, D - 0.2f, T - 0.5f), FRotator(0.f, Yaw + Twist, 0.f), false);
						Z += T;
					}
				}
			}
		}
	}
}

void FCampusSchoolBuildContext::TableBooks(const FVector& Top, float Yaw, int32 Seed)
{
	const FRotator R(0.f, Yaw, 0.f);
	static const EPal Covers[] = { EPal::BookRed, EPal::BookBlue, EPal::BookGreen, EPal::BookCream, EPal::BookBrown, EPal::BookGray, EPal::BookOchre, EPal::BookTeal };
	const uint32 Hh = Hash(Seed, 8081);
	// A short stack.
	float Z = Top.Z;
	const FVector StackAt = Top + R.RotateVector(FVector(-30.f + (Hh % 20), -12.f, 0.f));
	for (int32 i = 0; i < 2 + (int32)(Hh % 3); ++i)
	{
		const uint32 Bh = Hash(Seed * 7 + i, 99);
		const float T = 1.6f + (Bh % 25) * 0.1f;
		const FVector Size(21.f + (Bh >> 5) % 8, 15.f + (Bh >> 9) % 5, T);
		const FRotator Rot(0.f, Yaw + (float)((int32)((Bh >> 13) % 21) - 10), 0.f);
		BoxR(Covers[(Bh >> 17) % UE_ARRAY_COUNT(Covers)], FVector(StackAt.X, StackAt.Y, Z + T * 0.5f), Size, Rot, false);
		BoxR(EPal::BookPage, FVector(StackAt.X, StackAt.Y, Z + T * 0.5f) + Rot.RotateVector(FVector(0.2f, 0.f, 0.f)), Size - FVector(0.8f, 0.4f, 0.5f), Rot, false);
		Z += T;
	}
	// An open book: two page blocks bowing up from the spine on a cover laid flat.
	if ((Hh >> 4) % 2 == 0)
	{
		const FVector Open = Top + R.RotateVector(FVector(18.f, 8.f, 0.f));
		const float OpenYaw = Yaw + 90.f + (float)((int32)((Hh >> 8) % 31) - 15);
		const FRotator OR(0.f, OpenYaw, 0.f);
		BoxR(Covers[(Hh >> 12) % UE_ARRAY_COUNT(Covers)], Open + FVector(0.f, 0.f, 0.15f), FVector(42.f, 27.f, 0.3f), OR, false);
		for (const float S : { -1.f, 1.f })
		{
			BoxR(EPal::BookPage, Open + OR.RotateVector(FVector(S * 10.2f, 0.f, 0.f)) + FVector(0.f, 0.f, 0.9f), FVector(19.5f, 25.5f, 1.2f), FRotator(S * -4.f, OpenYaw, 0.f), false);
		}
	}
}

void FCampusSchoolBuildContext::OfficeDesk(const FVector& Base, float Yaw)
{
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](const FVector& L) { return Base + R.RotateVector(L); };
	// The person sits on local -Y, facing +Y.
	if (Prop(TEXT("metal_office_desk"), At(FVector(0.f, 0.f, 0.f)), Yaw - 90.f))
	{
		Prop(TEXT("plastic_monobloc_chair_01"), At(FVector(0.f, -75.f, 0.f)), Yaw + 90.f);
		return;
	}
	Box(EPal::WoodDesk, At(FVector(0.f, 0.f, 74.f)), FVector(140.f, 70.f, 4.f), true, Yaw);
	Box(EPal::Locker, At(FVector(0.f, 25.f, 37.f)), FVector(130.f, 15.f, 72.f), true, Yaw);
	Box(EPal::Locker, At(FVector(-55.f, 0.f, 37.f)), FVector(30.f, 60.f, 72.f), true, Yaw);
	Box(EPal::Black, At(FVector(20.f, 15.f, 98.f)), FVector(45.f, 4.f, 30.f), false, Yaw);
	Box(EPal::DeskFrame, At(FVector(0.f, -55.f, 25.f)), FVector(45.f, 45.f, 50.f), true, Yaw);
}

void FCampusSchoolBuildContext::Scooter(const FVector& Base, float Yaw, int32 Variant)
{
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](const FVector& L) { return Base + R.RotateVector(L); };
	// 125 cc step-through (local +X forward): 12" wheels, sloped leg shield, flat floorboard, rear cowl and seat.
	auto Part = [&](EPal P, const FVector& L, const FVector& Size, float Pitch = 0.f, bool bSolid = false)
	{
		BoxR(P, At(L), Size, FRotator(Pitch, Yaw, 0.f), bSolid);
	};
	const EPal Body = Variant % 3 == 0 ? EPal::ScooterWhite : (Variant % 3 == 1 ? EPal::ScooterRed : EPal::ScooterBlue);
	for (const float X : { 64.f, -60.f })
	{
		CylR(EPal::Tire, At(FVector(X, 0.f, 24.f)), 48.f, 11.f, FRotator(0.f, Yaw, 90.f), false);
		CylR(EPal::Stainless, At(FVector(X, 0.f, 24.f)), 27.f, 12.f, FRotator(0.f, Yaw, 90.f), false);
	}
	// front: fork up to the steering head, fender over the wheel, nose cover and the leg shield
	Part(EPal::Black, FVector(60.f, 0.f, 58.f), FVector(4.f, 16.f, 70.f), 10.f);
	Part(Body, FVector(66.f, 0.f, 51.f), FVector(36.f, 15.f, 4.f), -10.f);
	Part(Body, FVector(62.f, 0.f, 72.f), FVector(20.f, 30.f, 30.f), 18.f);
	Part(Body, FVector(50.f, 0.f, 66.f), FVector(9.f, 46.f, 76.f), 14.f, true);
	// floorboard and its side skirts
	Part(EPal::Black, FVector(12.f, 0.f, 33.f), FVector(66.f, 36.f, 3.f));
	Part(Body, FVector(12.f, 0.f, 26.f), FVector(70.f, 40.f, 11.f));
	// rear cowl over the engine, tapering into the tail
	Part(Body, FVector(-36.f, 0.f, 54.f), FVector(74.f, 38.f, 36.f), 0.f, true);
	Part(Body, FVector(-74.f, 0.f, 63.f), FVector(22.f, 30.f, 22.f), -14.f);
	Part(Body, FVector(-10.f, 0.f, 44.f), FVector(22.f, 34.f, 24.f), 30.f);
	Part(EPal::Black, FVector(-48.f, 9.f, 28.f), FVector(54.f, 13.f, 15.f));
	Part(EPal::Stainless, FVector(-50.f, -11.f, 28.f), FVector(40.f, 7.f, 7.f), -6.f);
	// seat, grab rail, tail light
	Part(EPal::Black, FVector(-34.f, 0.f, 77.f), FVector(82.f, 31.f, 10.f), -3.f);
	Part(EPal::Black, FVector(-4.f, 0.f, 78.f), FVector(18.f, 27.f, 7.f), 10.f);
	Part(EPal::Stainless, FVector(-71.f, 0.f, 81.f), FVector(16.f, 27.f, 2.5f));
	Part(EPal::ScooterRed, FVector(-86.f, 0.f, 65.f), FVector(3.f, 20.f, 6.f), -14.f);
	// steering head, handlebar with grips and levers, mirrors, headlight
	Part(Body, FVector(46.f, 0.f, 103.f), FVector(18.f, 24.f, 22.f), 14.f);
	Part(EPal::Black, FVector(43.f, 0.f, 113.f), FVector(6.f, 64.f, 3.5f));
	for (const float S : { -1.f, 1.f })
	{
		Part(EPal::Tire, FVector(43.f, S * 29.f, 113.f), FVector(5.f, 11.f, 4.5f));
		Part(EPal::Black, FVector(48.f, S * 26.f, 111.f), FVector(10.f, 2.f, 1.5f), -10.f);
		Part(EPal::Black, FVector(46.f, S * 22.f, 123.f), FVector(1.8f, 1.8f, 18.f), -12.f);
		Part(EPal::Black, FVector(47.f, S * 26.f, 133.f), FVector(2.f, 12.f, 7.f));
	}
	Part(EPal::Stainless, FVector(56.f, 0.f, 104.f), FVector(3.f, 16.f, 9.f), 14.f);
	// side stand down, leaning the scooter's weight onto it
	Part(EPal::Black, FVector(-2.f, -21.f, 10.f), FVector(3.f, 3.f, 24.f), 0.f);
}

void FCampusSchoolBuildContext::Bicycle(const FVector& Base, float Yaw)
{
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](const FVector& L) { return Base + R.RotateVector(L); };
	Box(EPal::IronBars, At(FVector(0.f, 0.f, 55.f)), FVector(110.f, 4.f, 6.f), false, Yaw);
	Box(EPal::IronBars, At(FVector(-20.f, 0.f, 70.f)), FVector(6.f, 4.f, 40.f), false, Yaw);
	Box(EPal::Black, At(FVector(-25.f, 0.f, 92.f)), FVector(24.f, 10.f, 5.f), false, Yaw);
	Box(EPal::IronBars, At(FVector(45.f, 0.f, 95.f)), FVector(5.f, 50.f, 4.f), false, Yaw);
	// Fork and stem: from the front axle up through the head tube to the bars.
	BoxR(EPal::IronBars, At(FVector(48.5f, 0.f, 64.f)), FVector(4.f, 4.f, 63.f), FRotator(0.f, Yaw, 0.f) + FRotator(-6.f, 0.f, 0.f), false);
	BoxR(EPal::IronBars, At(FVector(23.f, 0.f, 75.f)), FVector(48.f, 4.f, 4.f), FRotator(-20.f, Yaw, 0.f), false);
	CylR(EPal::Tire, At(FVector(52.f, 0.f, 33.f)), 66.f, 4.f, FRotator(0.f, Yaw, 90.f), false);
	CylR(EPal::Tire, At(FVector(-52.f, 0.f, 33.f)), 66.f, 4.f, FRotator(0.f, Yaw, 90.f), false);
	Box(EPal::IronBars, At(FVector(0.f, 0.f, 30.f)), FVector(100.f, 40.f, 60.f), true, Yaw);
}

void FCampusSchoolBuildContext::Hoop(float X, float Baseline, float Inward)
{
	const float PoleY = Baseline - Inward * 120.f;
	Cyl(EPal::RoofBlue, FVector(X, PoleY, 0.f), 22.f, 330.f, true);
	BoxMM(EPal::RoofBlue, FVector(X - 6.f, FMath::Min(PoleY, Baseline + Inward * 110.f), 315.f), FVector(X + 6.f, FMath::Max(PoleY, Baseline + Inward * 110.f), 327.f), false);
	Box(EPal::WallWhite, FVector(X, Baseline + Inward * 115.f, 345.f), FVector(180.f, 5.f, 105.f), true);
	Box(EPal::LineWhite, FVector(X, Baseline + Inward * 119.f, 330.f), FVector(60.f, 2.f, 45.f), false);
	Cyl(EPal::Orange, FVector(X, Baseline + Inward * 145.f, 304.f), 46.f, 3.f, false);
	Cyl(EPal::ConcreteDark, FVector(X, PoleY, 0.f), 60.f, 25.f, true);
}

void FCampusSchoolBuildContext::Lamp(const FVector& Base)
{
	if (Prop(TEXT("street_lamp_01"), Base, 0.f, 430.f))
	{
		return;
	}
	Cyl(EPal::Stainless, Base, 14.f, 430.f, true);
	Box(EPal::Stainless, Base + FVector(40.f, 0.f, 425.f), FVector(90.f, 8.f, 6.f), false);
	Box(EPal::LightPanel, Base + FVector(80.f, 0.f, 418.f), FVector(40.f, 18.f, 8.f), false);
}

void FCampusSchoolBuildContext::Classroom(const FBox2D& Room, float Floor, float Ceiling, int32 BoardSide, int32 Variant)
{
	if (!B->bBuildInteriors)
	{
		return;
	}
	const FVector2D C = Room.GetCenter();
	const FVector2D Ext = Room.GetExtent();
	const bool bAlongY = BoardSide == 0 || BoardSide == 1;
	CeilingAt(Room.Min.X, Room.Min.Y, Room.Max.X, Room.Max.Y, Ceiling);
	// Unit vector pointing from the room center towards the board wall.
	const FVector2D ToBoard = BoardSide == 0 ? FVector2D(0.f, -1.f) : BoardSide == 1 ? FVector2D(0.f, 1.f) : BoardSide == 2 ? FVector2D(-1.f, 0.f) : FVector2D(1.f, 0.f);
	const FVector2D Across(ToBoard.Y, -ToBoard.X);
	const float Depth = bAlongY ? Ext.Y : Ext.X;
	const float Width = bAlongY ? Ext.X : Ext.Y;
	const float FacingYaw = FMath::RadiansToDegrees(FMath::Atan2(ToBoard.Y, ToBoard.X));
	auto W = [&](float Forward, float Side, float Z) { const FVector2D P = C + ToBoard * Forward + Across * Side; return FVector(P.X, P.Y, Z); };

	// ceiling lights (fixture + emissive tubes) and fans
	for (int32 r = 0; r < 3; ++r)
	{
		for (int32 c = -1; c <= 1; c += 2)
		{
			const FVector P = W((r - 1) * Depth * 0.55f, c * Width * 0.4f, Ceiling);
			if (PropHang(TEXT("mounted_fluorescent_lights"), P, bAlongY ? 180.f : 90.f))
			{
				// The tube's own batten up to the ceiling (inside the fitting's shell when it is placed).
				Box(EPal::Stainless, P - FVector(0.f, 0.f, 2.25f), bAlongY ? FVector(8.f, 82.f, 4.5f) : FVector(82.f, 8.f, 4.5f), false);
				Box(EPal::LightPanel, P - FVector(0.f, 0.f, 5.f), bAlongY ? FVector(10.f, 80.f, 1.f) : FVector(80.f, 10.f, 1.f), false);
			}
			else
			{
				Box(EPal::LightPanel, P - FVector(0.f, 0.f, 4.f), bAlongY ? FVector(25.f, 120.f, 4.f) : FVector(120.f, 25.f, 4.f), false);
			}
			CeilingLight(P - FVector(0.f, 0.f, 16.f), bAlongY);
			Lights.Last().bClassroom = true;
		}
	}
	if (HasProp(TEXT("ceiling_fan")))
	{
		for (int32 r = -1; r <= 1; r += 2)
		{
			for (int32 c = -1; c <= 1; c += 2)
			{
				PropHang(TEXT("ceiling_fan"), W(r * Depth * 0.28f, c * Width * 0.18f, Ceiling), (float)(Hash(r * 13 + c, Variant) % 90));
			}
		}
	}
	else
	{
		Cyl(EPal::WallWhite, W(0.f, 0.f, Ceiling - 60.f), 8.f, 56.f, false);
		Box(EPal::WallWhite, W(0.f, 0.f, Ceiling - 62.f), FVector(120.f, 14.f, 2.f), false, FacingYaw + 30.f);
	}

	if (Variant == 4)
	{
		return;
	}
	PropOnWall(TEXT("wall_clock"), W(Depth - 2.f, Width * 0.55f, Floor + 250.f), FacingYaw + 180.f, 0.f, false);
	Prop(TEXT("metal_trash_can"), W(-Depth + 45.f, Width - 50.f, Floor), FacingYaw, 0.f, true, nullptr, TEXT("rust"));
	Prop(TEXT("korean_fire_extinguisher_01"), W(-Depth + 30.f, -Width + 30.f, Floor), FacingYaw);

	// board wall
	const float BoardOffset = Depth - 14.f;
	Box(EPal::BoardFrame, W(BoardOffset + 2.f, 0.f, Floor + 150.f), bAlongY ? FVector(440.f, 4.f, 140.f) : FVector(4.f, 440.f, 140.f), false);
	Box(EPal::Board, W(BoardOffset, 0.f, Floor + 150.f), bAlongY ? FVector(420.f, 3.f, 120.f) : FVector(3.f, 420.f, 120.f), false);
	Box(EPal::WoodDesk, W(BoardOffset - 70.f, 0.f, Floor + 10.f), bAlongY ? FVector(520.f, 130.f, 20.f) : FVector(130.f, 520.f, 20.f), true);
	OfficeDesk(W(BoardOffset - 85.f, Width * 0.45f, Floor + 20.f), FacingYaw + 180.f);
	// The classroom TV up in the corner on its wall bracket.
	Box(EPal::SteelDark, W(Depth - 4.f, -Width + 75.f, Floor + 215.f), bAlongY ? FVector(30.f, 8.f, 30.f) : FVector(8.f, 30.f, 30.f), false);
	Box(EPal::Black, W(Depth - 14.f, -Width + 75.f, Floor + 215.f), bAlongY ? FVector(100.f, 12.f, 60.f) : FVector(12.f, 100.f, 60.f), false);

	if (Variant == 0)
	{
		const int32 Rows = FMath::Clamp(FMath::FloorToInt((Depth * 2.f - 300.f) / 115.f), 3, 6);
		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < 4; ++c)
			{
				const float Side = (c - 1.5f) * FMath::Min(170.f, Width * 0.45f);
				StudentDesk(W(BoardOffset - 260.f - r * 115.f, Side, Floor), FacingYaw);
			}
		}
	}
	else if (Variant == 1)
	{
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 c = -1; c <= 1; c += 2)
			{
				const FVector P = W(BoardOffset - 260.f - r * 180.f, c * Width * 0.42f, Floor);
				Box(EPal::Stone, P + FVector(0.f, 0.f, 45.f), bAlongY ? FVector(180.f, 70.f, 90.f) : FVector(70.f, 180.f, 90.f), true);
				Box(EPal::Stainless, P + FVector(0.f, 0.f, 92.f), FVector(40.f, 40.f, 4.f), false);
			}
		}
	}
	else if (Variant == 2)
	{
		// Library: rows of double-sided bookcases, reading tables with chairs and books left out on them.
		for (int32 r = 0; r < 4; ++r)
		{
			const FVector P = W(BoardOffset - 240.f - r * 150.f, -Width * 0.35f, Floor);
			Bookcase(P, bAlongY, Width * 0.75f, Variant * 97 + r);
		}
		for (int32 r = 0; r < 3; ++r)
		{
			const FVector P = W(BoardOffset - 280.f - r * 200.f, Width * 0.55f, Floor);
			Box(EPal::WoodDesk, P + FVector(0.f, 0.f, 74.f), FVector(160.f, 90.f, 4.f), true);
			Box(EPal::DeskFrame, P + FVector(0.f, 0.f, 36.f), FVector(140.f, 20.f, 72.f), true);
			TableBooks(P + FVector(0.f, 0.f, 76.f), FacingYaw + 90.f * r, Variant * 31 + r);
			for (const float S : { -1.f, 1.f })
			{
				Prop(TEXT("SchoolChair_01"), P + FVector(S * 40.f, S * 70.f, 0.f), S > 0.f ? -90.f : 90.f);
			}
		}
	}
	else if (Variant == 3)
	{
		for (int32 r = 0; r < 2; ++r)
		{
			for (int32 c = -1; c <= 1; ++c)
			{
				OfficeDesk(W(BoardOffset - 240.f - r * 180.f, c * Width * 0.5f, Floor), FacingYaw);
			}
		}
	}

	// back wall lockers & bulletin board, both against the wall (kept short of the room's corners, where doors are)
	Box(EPal::Locker, W(-Depth + 20.5f, 0.f, Floor + 60.f), bAlongY ? FVector(Width * 1.1f, 40.f, 120.f) : FVector(40.f, Width * 1.1f, 120.f), true);
	Box(EPal::Cork, W(-Depth + 1.6f, 0.f, Floor + 190.f), bAlongY ? FVector(Width * 1.0f, 3.f, 90.f) : FVector(3.f, Width * 1.0f, 90.f), false);
}

void FCampusSchoolBuildContext::RowHouse(const FVector& FrontCenter, float Yaw, float Width, int32 Seed)
{
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](float Fwd, float Side, float Z) { return FrontCenter + R.RotateVector(FVector(Fwd, Side, Z)); };
	const uint32 Hh = Hash(Seed, 911);
	const int32 Floors = 3 + Hh % 4;
	const float FH = 320.f;
	const float Height = Floors * FH;
	const EPal Colors[4] = { EPal::FarBeige, EPal::FarGray, EPal::FarPink, EPal::FarBlue };
	const EPal Col = Colors[(Hh >> 3) % 4];
	const float Depth = 1100.f;
	// Neighbours never line up exactly.
	const float Setback = (float)((int32)((Hh >> 18) % 5) - 2) * 15.f;
	auto AtS = [&](float Fwd, float Side, float Z) { return At(Fwd + Setback, Side, Z); };
	Box(Col, AtS(-Depth * 0.5f, 0.f, Height * 0.5f), FVector(Depth, Width - 10.f, Height), true, Yaw);
	// floor slab edges and rain awnings (雨遮): the strong horizontal shadow lines of a Taiwanese street front
	for (int32 f = 1; f <= Floors; ++f)
	{
		Box(EPal::Concrete, AtS(18.f, 0.f, f * FH - 6.f), FVector(36.f, Width - 10.f, 14.f), false, Yaw);
		if (f < Floors && (Hh >> (f + 2)) % 2 == 0)
		{
			BoxR((Hh >> (f + 5)) % 2 ? EPal::RoofBlue : EPal::RoofGreen, AtS(48.f, 0.f, f * FH + 285.f), FVector(80.f, Width * 0.78f, 3.f), FRotator(-14.f, Yaw, 0.f), false);
		}
	}
	// roof parapet, tank stand, antenna, drainpipe
	for (const float S : { -1.f, 1.f })
	{
		Box(Col, AtS(-Depth * 0.5f, S * (Width * 0.5f - 12.f), Height + 50.f), FVector(Depth, 14.f, 100.f), false, Yaw);
	}
	Box(Col, AtS(-7.f, 0.f, Height + 50.f), FVector(14.f, Width - 10.f, 100.f), false, Yaw);
	Box(EPal::IronBars, AtS(-Depth * 0.2f, Width * 0.25f, Height + 60.f), FVector(140.f, 140.f, 6.f), false, Yaw);
	for (const FVector2D& Leg : { FVector2D(-60.f, -60.f), FVector2D(60.f, -60.f), FVector2D(-60.f, 60.f), FVector2D(60.f, 60.f) })
	{
		Box(EPal::IronBars, AtS(-Depth * 0.2f + Leg.X, Width * 0.25f + Leg.Y, Height + 30.f), FVector(6.f, 6.f, 60.f), false, Yaw);
	}
	if ((Hh >> 16) % 2 == 0)
	{
		Box(EPal::Stainless, AtS(-Depth * 0.7f, -Width * 0.3f, Height + 200.f), FVector(3.f, 3.f, 400.f), false, Yaw);
		Box(EPal::Stainless, AtS(-Depth * 0.7f, -Width * 0.3f, Height + 330.f), FVector(3.f, 90.f, 3.f), false, Yaw);
	}
	Box(EPal::ConcreteDark, AtS(8.f, -Width * 0.5f + 16.f, Height * 0.5f), FVector(10.f, 10.f, Height), false, Yaw);
	// ground floor shop: roll-down shutters (some tagged with graffiti) over a dark recess
	Box(EPal::ConcreteDark, At(5.f, 0.f, 150.f), FVector(20.f, Width - 60.f, 280.f), false, Yaw);
	if (HasProp(TEXT("rollershutter_door")))
	{
		for (int32 s = -1; s <= 1; s += 2)
		{
			const bool bTagged = (Hh >> (s + 3)) % 3 == 0;
			PropOnWall(TEXT("rollershutter_door"), At(16.f, s * Width * 0.22f, 0.f), Yaw, 255.f, false, bTagged ? TEXT("graffiti") : nullptr, bTagged ? nullptr : TEXT("graffiti"));
		}
	}
	// Shop signs: a lightbox board over the shutters and, on many, a blade sign standing out over the arcade - the
	// layered clutter of a Taiwanese street front (low resolution: they are only ever seen from across the road).
	static const TCHAR* ShopNames[] = { TEXT("早餐店"), TEXT("機車行"), TEXT("五金行"), TEXT("美髮院"), TEXT("牙醫診所"), TEXT("大藥局"), TEXT("小吃部"), TEXT("牛肉麵"),
		TEXT("文具行"), TEXT("補習班"), TEXT("眼鏡行"), TEXT("電器行"), TEXT("洗衣店"), TEXT("便當"), TEXT("飲料店"), TEXT("水電行"), TEXT("中醫診所"), TEXT("彩券行"),
		TEXT("麵包店"), TEXT("通訊行"), TEXT("自助餐"), TEXT("影印店"), TEXT("寵物店"), TEXT("冷氣行") };
	struct FSignLook { FLinearColor Back; FLinearColor Ink; };
	static const FSignLook Looks[] = {
		{ FLinearColor(0.60f, 0.04f, 0.03f), FLinearColor(0.95f, 0.92f, 0.80f) }, { FLinearColor(0.03f, 0.22f, 0.10f), FLinearColor(0.95f, 0.95f, 0.92f) },
		{ FLinearColor(0.04f, 0.10f, 0.35f), FLinearColor(0.95f, 0.85f, 0.20f) }, { FLinearColor(0.85f, 0.80f, 0.70f), FLinearColor(0.55f, 0.04f, 0.03f) },
		{ FLinearColor(0.80f, 0.55f, 0.05f), FLinearColor(0.10f, 0.08f, 0.06f) }, { FLinearColor(0.88f, 0.88f, 0.85f), FLinearColor(0.04f, 0.18f, 0.40f) } };
	const int32 NameCount = UE_ARRAY_COUNT(ShopNames);
	const FSignLook& Board = Looks[(Hh >> 5) % UE_ARRAY_COUNT(Looks)];
	Box(EPal::ConcreteDark, At(22.f, 0.f, 330.f), FVector(8.f, Width - 36.f, 76.f), false, Yaw);
	Sign(At(30.f, 0.f, 330.f), Yaw, Width - 40.f, 70.f, ShopNames[(Hh >> 8) % NameCount], Board.Back, Board.Ink, 1, false, 0.25f, 14.f, 1.1f);
	if ((Hh >> 7) % 2 == 0)
	{
		const FSignLook& Blade = Looks[(Hh >> 11) % UE_ARRAY_COUNT(Looks)];
		Sign(At(80.f, Width * 0.5f - 30.f, 540.f), Yaw + 90.f, 100.f, 280.f, ShopNames[(Hh >> 14) % NameCount], Blade.Back, Blade.Ink, 1, true, 0.25f, 14.f, 0.9f);
	}
	// iron window cages (鐵窗) per floor: a frame of flat bar with square bars, aluminium windows behind
	for (int32 f = 1; f < Floors; ++f)
	{
		const float CageW = Width * 0.7f;
		const float Zc = f * FH + 160.f;
		Box(EPal::ConcreteDark, AtS(1.f, 0.f, Zc), FVector(4.f, CageW - 4.f, 154.f), false, Yaw);
		Box(EPal::Glass, AtS(3.f, 0.f, Zc), FVector(2.f, CageW - 12.f, 144.f), false, Yaw);
		Box(EPal::Stainless, AtS(4.f, 0.f, Zc), FVector(3.f, 4.f, 144.f), false, Yaw);
		for (const float Z : { Zc - 85.f, Zc + 85.f })
		{
			Box(EPal::IronBars, AtS(26.f, 0.f, Z), FVector(52.f, CageW, 4.f), false, Yaw);
		}
		for (const float S : { -1.f, 1.f })
		{
			Box(EPal::IronBars, AtS(26.f, S * CageW * 0.5f, Zc), FVector(52.f, 4.f, 170.f), false, Yaw);
		}
		Box(EPal::GrilleIron, AtS(51.f, 0.f, Zc), FVector(2.f, CageW - 4.f, 166.f), false, Yaw);
		if ((Hh >> (f + 4)) % 3 != 0)
		{
			const bool bRust = (Hh >> (f + 7)) % 2 == 0;
			PropOnWall(TEXT("exterior_aircon_unit"), At(1.f, (f % 2 ? 1.f : -1.f) * Width * 0.4f, f * FH + 20.f), Yaw, 0.f, false, bRust ? TEXT("rusted") : nullptr, bRust ? nullptr : TEXT("rusted"));
		}
		if ((Hh >> (f + 10)) % 3 == 0)
		{
			Box(EPal::TankWhite, At(35.f, -Width * 0.35f, f * FH + 60.f), FVector(50.f, 70.f, 50.f), false, Yaw);
		}
	}
	// rooftop addition and water tanks
	if ((Hh >> 13) % 3 != 0)
	{
		Box((Hh >> 14) % 2 ? EPal::RoofBlue : EPal::RoofGreen, At(-Depth * 0.55f, 0.f, Height + 140.f), FVector(Depth * 0.7f, Width - 40.f, 280.f), false, Yaw);
	}
	Cyl((Hh >> 15) % 2 ? EPal::TankBlue : EPal::TankWhite, At(-Depth * 0.2f, Width * 0.25f, Height), 130.f, 150.f, false);

	// The back, facing the school: smaller windows behind iron cages, condensers, the gas water heater and the drain
	// pipes, and on many a tin-roofed lean-to over the back yard.
	const float BackX = -Depth;
	for (int32 f = 1; f < Floors; ++f)
	{
		const uint32 Hf = Hash(Seed * 7 + f, 913);
		const float Zc = f * FH + 150.f;
		const float Wx = Width * (0.36f + 0.1f * (Hf % 3));
		const float Side = (Hf % 2 ? 1.f : -1.f) * Width * 0.16f;
		Box(EPal::ConcreteDark, AtS(BackX - 1.f, Side, Zc), FVector(4.f, Wx, 128.f), false, Yaw);
		Box(EPal::Glass, AtS(BackX - 3.f, Side, Zc), FVector(2.f, Wx - 10.f, 118.f), false, Yaw);
		if ((Hf >> 3) % 4 != 0)
		{
			for (const float Z : { Zc - 72.f, Zc + 72.f })
			{
				Box(EPal::IronBars, AtS(BackX - 22.f, Side, Z), FVector(44.f, Wx + 8.f, 4.f), false, Yaw);
			}
			for (const float S : { -1.f, 1.f })
			{
				Box(EPal::IronBars, AtS(BackX - 22.f, Side + S * (Wx * 0.5f + 2.f), Zc), FVector(44.f, 4.f, 148.f), false, Yaw);
			}
			Box(EPal::GrilleIron, AtS(BackX - 43.f, Side, Zc), FVector(2.f, Wx, 142.f), false, Yaw);
		}
		if ((Hf >> 5) % 3 != 0)
		{
			const bool bRust = (Hf >> 7) % 2 == 0;
			PropOnWall(TEXT("exterior_aircon_unit"), AtS(BackX - 1.f, -Side * 1.6f, f * FH + 25.f), Yaw + 180.f, 0.f, false, bRust ? TEXT("rusted") : nullptr, bRust ? nullptr : TEXT("rusted"));
		}
		if ((Hf >> 9) % 4 == 0)
		{
			Box(EPal::TankWhite, AtS(BackX - 14.f, -Side * 1.6f, Zc + 30.f), FVector(28.f, 46.f, 70.f), false, Yaw);
		}
	}
	Box(EPal::ConcreteDark, AtS(BackX - 7.f, Width * 0.5f - 22.f, Height * 0.5f), FVector(10.f, 10.f, Height), false, Yaw);
	Box(EPal::Stainless, AtS(BackX - 5.f, -Width * 0.5f + 30.f, Height * 0.5f), FVector(5.f, 5.f, Height), false, Yaw);
	if ((Hh >> 19) % 3 != 2)
	{
		const EPal Tin = (Hh >> 20) % 2 ? EPal::RoofBlue : EPal::RoofGreen;
		const float Reach = 180.f + 60.f * ((Hh >> 21) % 3);
		BoxR(Tin, AtS(BackX - Reach * 0.5f, 0.f, 285.f - Reach * 0.09f), FVector(Reach, Width - 30.f, 4.f), FRotator(-10.f, Yaw + 180.f, 0.f), false);
		for (const float S : { -1.f, 1.f })
		{
			Box(EPal::IronBars, AtS(BackX - Reach + 10.f, S * (Width * 0.5f - 30.f), 135.f), FVector(6.f, 6.f, 270.f), false, Yaw);
		}
	}
}

// ------------------------------------------------------------------ sections

void FCampusSchoolBuildContext::BuildGround()
{
	BoxMM(EPal::Asphalt, FVector(-14000.f, -14000.f, -40.f), FVector(14000.f, 14000.f, 0.f));
	Deco(EPal::Concrete, FVector(-4800.f, -5600.f, 0.f), FVector(4800.f, 5600.f, 1.f));
	Deco(EPal::PaverRed, FVector(-3150.f, -3200.f, 1.f), FVector(1400.f, 3100.f, 2.f));
	Deco(EPal::PaverGray, FVector(-4800.f, -5600.f, 1.f), FVector(4800.f, -4200.f, 2.f));
	Deco(EPal::PaverRed, FVector(PS_X0 - 100.f, -5600.f, 2.f), FVector(PS_X1 + 100.f, -4200.f, 3.f));
	Deco(EPal::PaverGray, FVector(PS_X0, NW_Y1, 1.f), FVector(PS_X1, 4400.f, 2.f));
}

void FCampusSchoolBuildContext::BuildPerimeter()
{
	const float WallH = 240.f;
	auto WallSeg = [&](const FVector& Min, const FVector& Max)
	{
		BoxMM(EPal::WallCream, Min, Max);
		Deco(EPal::RoofTile, FVector(Min.X - 3.f, Min.Y - 3.f, Max.Z), FVector(Max.X + 3.f, Max.Y + 3.f, Max.Z + 8.f));
		Deco(EPal::FacadeBrick, FVector(Min.X - 1.f, Min.Y - 1.f, 0.f), FVector(Max.X + 1.f, Max.Y + 1.f, 60.f));
	};
	// south (gate opening at the central passage)
	WallSeg(FVector(-4815.f, -5615.f, 0.f), FVector(PS_X0 - 100.f, -5585.f, WallH));
	WallSeg(FVector(PS_X1 + 100.f, -5615.f, 0.f), FVector(4815.f, -5585.f, WallH));
	// gate pillars and retractable gate
	BoxMM(EPal::Stone, FVector(PS_X0 - 160.f, -5640.f, 0.f), FVector(PS_X0 - 100.f, -5560.f, 300.f));
	BoxMM(EPal::Stone, FVector(PS_X1 + 100.f, -5640.f, 0.f), FVector(PS_X1 + 160.f, -5560.f, 300.f));
	BoxMM(EPal::Stainless, FVector(PS_X1 - 380.f, -5615.f, 0.f), FVector(PS_X1 + 100.f, -5585.f, 150.f));
	Deco(EPal::Stainless, FVector(PS_X1 - 380.f, -5620.f, 150.f), FVector(PS_X1 + 100.f, -5580.f, 158.f));
	// west / east
	WallSeg(FVector(-4815.f, -5600.f, 0.f), FVector(-4785.f, 5600.f, WallH));
	WallSeg(FVector(4785.f, -5600.f, 0.f), FVector(4815.f, 5600.f, WallH));
	for (float Y = -5600.f; Y <= 5600.f; Y += 400.f)
	{
		BoxMM(EPal::WallCream, FVector(-4825.f, Y - 22.f, 0.f), FVector(-4775.f, Y + 22.f, WallH + 20.f));
		BoxMM(EPal::WallCream, FVector(4775.f, Y - 22.f, 0.f), FVector(4825.f, Y + 22.f, WallH + 20.f));
	}
	// north fence: green welded-mesh panels between posts, see-through onto the pitch
	for (float X = -4800.f; X <= 4800.f; X += 250.f)
	{
		Cyl(EPal::Railing, FVector(X, 5600.f, 0.f), 7.f, 320.f, true);
	}
	for (const float Z : { 4.f, 160.f, 316.f })
	{
		Deco(EPal::Railing, FVector(-4800.f, 5597.f, Z - 2.5f), FVector(4800.f, 5601.f, Z + 2.5f));
	}
	Box(EPal::GrilleMesh, FVector(0.f, 5599.f, 160.f), FVector(0.8f, 9600.f, 312.f), false, 90.f);
	// invisible boundary
	Blocker(FVector(-4900.f, -5700.f, 0.f), FVector(-4785.f, 5700.f, 3000.f));
	Blocker(FVector(4785.f, -5700.f, 0.f), FVector(4900.f, 5700.f, 3000.f));
	Blocker(FVector(-4900.f, -5700.f, 0.f), FVector(4900.f, -5585.f, 3000.f));
	Blocker(FVector(-4900.f, 5590.f, 0.f), FVector(4900.f, 5700.f, 3000.f));
}

void FCampusSchoolBuildContext::BuildAdmin()
{
	const float Top = F1 + 3.f * H;
	// floor
	BoxMM(EPal::Terrazzo, FVector(AD_X0, AD_Y0, 0.f), FVector(AD_X1, AD_Y1, F1));
	// upper mass (2F-3F) over offices, open passage below
	BoxMM(EPal::FacadeBeige, FVector(AD_X0, AD_Y0, Ceil1), FVector(AD_X1, AD_YC, Top));
	BoxMM(EPal::WallCream, FVector(AD_X0, AD_YC, Ceil1), FVector(AD_X1, AD_Y1, F2));
	// upper corridor balconies (visual)
	CeilingAt(PS_X0, AD_Y0, PS_X1, AD_Y1, Ceil1);
	CeilingAt(AD_X0, AD_YC, AD_X1, AD_Y1, Ceil1);
	CeilingAt(AD_X0, AD_YC, AD_X1, AD_Y1, Top - Slab);
	for (int32 f = 1; f < 3; ++f)
	{
		const float Z = F1 + f * H;
		BoxMM(EPal::Terrazzo, FVector(AD_X0, AD_YC, Z - Slab), FVector(AD_X1, AD_Y1, Z));
		CeilingAt(AD_X0, AD_YC, AD_X1, AD_Y1, Z - Slab);
		BoxMM(EPal::FacadeBeige, FVector(AD_X0, AD_Y1 - 20.f, Z), FVector(AD_X1, AD_Y1, Z + 110.f));
		Deco(EPal::Railing, FVector(AD_X0, AD_Y1 - 22.f, Z + 110.f), FVector(AD_X1, AD_Y1 + 2.f, Z + 116.f));
	}
	BoxMM(EPal::Terrazzo, FVector(AD_X0, AD_YC, Top - Slab), FVector(AD_X1, AD_Y1, Top));
	BoxMM(EPal::FacadeBeige, FVector(AD_X0 - 10.f, AD_Y0 - 10.f, Top), FVector(AD_X1 + 10.f, AD_Y1 + 10.f, Top + 25.f));
	BoxMM(EPal::FacadeBeige, FVector(AD_X0, AD_Y0, Top + 25.f), FVector(AD_X1, AD_Y0 + 20.f, Top + 130.f));
	BoxMM(EPal::FacadeBeige, FVector(AD_X0, AD_Y1 - 20.f, Top + 25.f), FVector(AD_X1, AD_Y1, Top + 130.f));
	FacadeWindowsX(AD_YC, AD_X0 + 100.f, AD_X1 - 100.f, F2, Top - 10.f, 450.f, 1.f);
	FacadeWindowsX(AD_Y0, AD_X0 + 100.f, AD_X1 - 100.f, F2, Top - 10.f, 450.f, -1.f);
	// arcade columns (1F-3F)
	for (float X = AD_X0 + 225.f; X < AD_X1; X += 450.f)
	{
		if (X > PS_X0 - 30.f && X < PS_X1 + 30.f)
		{
			continue;
		}
		BoxMM(EPal::Column, FVector(X - 22.f, AD_Y1 - 45.f, F1), FVector(X + 22.f, AD_Y1, Top));
	}

	struct FRoom { float X0; float X1; int32 Variant; };
	const FRoom Rooms[] = { { AD_X0, -3100.f, 3 }, { -3100.f, -2200.f, 3 }, { -2200.f, PS_X0, 3 }, { PS_X1, 800.f, 3 }, { 800.f, 2000.f, 3 }, { 2000.f, 3000.f, 1 }, { 3000.f, AD_X1, 3 } };

	// south exterior wall with windows, north (arcade side) wall with doors; a window the partition between two rooms
	// would run into is cut back to the larger side of it.
	TArray<FOpening> South;
	for (float X = AD_X0 + 225.f; X < AD_X1; X += 450.f)
	{
		if (X > PS_X0 - 150.f && X < PS_X1 + 150.f)
		{
			continue;
		}
		float A0 = X - 140.f;
		float A1 = X + 140.f;
		for (const FRoom& R : Rooms)
		{
			const float P = R.X1;
			if (P > A0 - 15.f && P < A1 + 15.f)
			{
				if (A1 - (P + 25.f) >= (P - 25.f) - A0)
				{
					A0 = P + 25.f;
				}
				else
				{
					A1 = P - 25.f;
				}
			}
		}
		if (A1 - A0 > 80.f)
		{
			South.Add({ A0, A1, F1 + 90.f, F1 + 235.f, true });
		}
	}
	South.Add({ PS_X0, PS_X1, 0.f, Ceil1, false });
	WallX(EPal::WallWhite, AD_Y0 + ExtT * 0.5f, AD_X0, AD_X1, F1, Ceil1, ExtT, South, EPal::FacadeBrick, -1.f);
	TArray<FOpening> North;
	for (const FRoom& R : Rooms)
	{
		const float DoorX = R.X1 - 140.f;
		North.Add({ DoorX - 55.f, DoorX + 55.f, F1, F1 + 215.f, false });
		if (R.X1 - R.X0 > 700.f)
		{
			North.Add({ R.X0 + 120.f, R.X0 + 360.f, F1 + 100.f, F1 + 230.f, true });
		}
		Classroom(FBox2D(FVector2D(R.X0 + 10.f, AD_Y0 + ExtT), FVector2D(R.X1 - 10.f, AD_YC - WallT * 0.5f)), F1, Ceil1, 2, R.Variant);
	}
	North.Add({ PS_X0, PS_X1, 0.f, Ceil1, false });
	WallX(EPal::WallCream, AD_YC, AD_X0, AD_X1, F1, Ceil1, WallT, North);
	// partitions
	for (float X : { -3100.f, -2200.f, 800.f, 2000.f, 3000.f })
	{
		WallY(EPal::WallWhite, X, AD_Y0 + ExtT, AD_YC - WallT * 0.5f, F1, Ceil1, WallT, {});
	}
	// passage side walls with doors into the offices, at the arcade end where the offices' boards and lockers stop short
	const float SideDoor0 = AD_YC - WallT * 0.5f - 115.f;
	const float SideDoor1 = AD_YC - WallT * 0.5f - 10.f;
	WallY(EPal::FacadeBeige, PS_X0, AD_Y0 + ExtT, AD_YC - WallT * 0.5f, F1, Ceil1, WallT, { { SideDoor0, SideDoor1, F1, F1 + 215.f, false } });
	WallY(EPal::FacadeBeige, PS_X1, AD_Y0 + ExtT, AD_YC - WallT * 0.5f, F1, Ceil1, WallT, { { SideDoor0, SideDoor1, F1, F1 + 215.f, false } });
	// end walls
	WallY(EPal::WallWhite, AD_X0 + ExtT * 0.5f, AD_Y0, AD_YC, F1, Ceil1, ExtT, {}, EPal::FacadeBrick, -1.f);
	// The end office's lockers and notice board stand against this wall: blank on the ground floor.
	WallY(EPal::WallWhite, AD_X1 - ExtT * 0.5f, AD_Y0, AD_YC, F1, Ceil1, ExtT, {}, EPal::FacadeBrick, 1.f);

	// motto board + clock above the passage (courtyard side), notice boards inside passage
	Deco(EPal::RedBoard, FVector(PS_X0 + 40.f, AD_Y1 - 4.f, F2 + 40.f), FVector(PS_X1 - 40.f, AD_Y1 + 6.f, F2 + 170.f));
	Cyl(EPal::ClockFace, FVector(CX, AD_Y0 - 2.f, F2 + 200.f), 110.f, 6.f, false);
	Deco(EPal::Cork, FVector(PS_X0 + 10.f, AD_Y0 + 70.f, F1 + 90.f), FVector(PS_X0 + 13.f, AD_Y0 + 430.f, F1 + 220.f));
	Deco(EPal::Cork, FVector(PS_X1 - 13.f, AD_Y0 + 70.f, F1 + 90.f), FVector(PS_X1 - 10.f, AD_Y0 + 430.f, F1 + 220.f));
	BoxMM(EPal::Glass, FVector(PS_X1 - 70.f, AD_Y0 + 440.f, F1), FVector(PS_X1 - 10.f, AD_Y0 + 560.f, F1 + 190.f));
	BoxMM(EPal::Gold, FVector(PS_X1 - 66.f, AD_Y0 + 470.f, F1 + 100.f), FVector(PS_X1 - 30.f, AD_Y0 + 500.f, F1 + 130.f), false);

	Rect(AD_X0, AD_Y0, AD_X1, AD_YC, 0);
}

void FCampusSchoolBuildContext::BuildWestWing()
{
	const float Top = F1 + 4.f * H;
	const float StairY1 = WW_Y0 + 450.f;
	// 1F floor
	BoxMM(EPal::Terrazzo, FVector(WW_X0, WW_Y0, 0.f), FVector(WW_X1, WW_Y1, F1));
	// 2F floor (corridor + arrival lip over the stair start)
	BoxMM(EPal::Terrazzo, FVector(WW_X0, StairY1, Ceil1), FVector(WW_X1, WW_Y1, F2));
	BoxMM(EPal::Terrazzo, FVector(WW_XC - 10.f - StairArrival, WW_Y0, Ceil1), FVector(WW_X1, StairY1, F2));
	// upper solid mass over classrooms (2F rooms + 3F/4F), stair well open on 2F
	BoxMM(EPal::FacadeBrick, FVector(WW_X0, StairY1, F2), FVector(WW_XC, WW_Y1, Top));
	BoxMM(EPal::FacadeBrick, FVector(WW_X0, WW_Y0, F2 + H - Slab), FVector(WW_XC, StairY1, Top));
	// 3F / 4F corridor balconies (visual)
	CeilingAt(WW_XC + WallT * 0.5f, WW_Y0, WW_X1, WW_Y1, Ceil1);
	CeilingAt(WW_XC, WW_Y0, WW_X1, WW_Y1, Top - Slab);
	for (int32 f = 2; f < 4; ++f)
	{
		const float Z = F1 + f * H;
		BoxMM(EPal::Terrazzo, FVector(WW_XC, WW_Y0, Z - Slab), FVector(WW_X1, WW_Y1, Z));
		CeilingAt(WW_XC, WW_Y0, WW_X1, WW_Y1, Z - Slab);
		BoxMM(EPal::FacadeBeige, FVector(WW_X1 - 20.f, WW_Y0, Z), FVector(WW_X1, WW_Y1, Z + 110.f));
		Deco(EPal::Railing, FVector(WW_X1 - 22.f, WW_Y0, Z + 110.f), FVector(WW_X1 + 2.f, WW_Y1, Z + 116.f));
	}
	BoxMM(EPal::Terrazzo, FVector(WW_XC, WW_Y0, Top - Slab), FVector(WW_X1, WW_Y1, Top));
	BoxMM(EPal::FacadeBeige, FVector(WW_X0 - 10.f, WW_Y0, Top), FVector(WW_X1 + 10.f, WW_Y1, Top + 25.f));
	BoxMM(EPal::FacadeBeige, FVector(WW_X0, WW_Y0, Top + 25.f), FVector(WW_X0 + 20.f, WW_Y1, Top + 130.f));
	BoxMM(EPal::FacadeBeige, FVector(WW_X1 - 20.f, WW_Y0, Top + 25.f), FVector(WW_X1, WW_Y1, Top + 130.f));
	FacadeWindowsY(WW_X0, WW_Y0 + 100.f, WW_Y1 - 100.f, F2, Top - 10.f, 450.f, -1.f);
	FacadeWindowsY(WW_XC, StairY1 + 50.f, WW_Y1 - 50.f, F2, Top - 10.f, 450.f, 1.f);
	// 2F parapet
	BoxMM(EPal::FacadeBeige, FVector(WW_X1 - 20.f, WW_Y0, F2), FVector(WW_X1, WW_Y1, F2 + 110.f));
	Deco(EPal::Railing, FVector(WW_X1 - 22.f, WW_Y0, F2 + 110.f), FVector(WW_X1 + 2.f, WW_Y1, F2 + 116.f));
	// columns
	for (float Y = WW_Y0 + 225.f; Y < WW_Y1; Y += 450.f)
	{
		BoxMM(EPal::Column, FVector(WW_X1 - 45.f, Y - 22.f, F1), FVector(WW_X1, Y + 22.f, Top));
	}

	struct FRoom { float Y0; float Y1; int32 Variant; };
	const FRoom Rooms[] = { { -2750.f, -1850.f, 0 }, { -1850.f, -950.f, 0 }, { -950.f, -50.f, 0 }, { -50.f, 400.f, 4 }, { 400.f, 1300.f, 0 }, { 1300.f, 2200.f, 0 }, { 2200.f, WW_Y1, 1 } };

	TArray<FOpening> West;
	TArray<FOpening> Corridor;
	for (const FRoom& R : Rooms)
	{
		const float Len = R.Y1 - R.Y0;
		if (R.Variant == 4)
		{
			Corridor.Add({ R.Y0 + 60.f, R.Y0 + 160.f, F1, F1 + 210.f, false });
			Corridor.Add({ R.Y1 - 160.f, R.Y1 - 60.f, F1, F1 + 210.f, false });
			// High windows either side of the partition that splits the washrooms.
			West.Add({ R.Y0 + 40.f, (R.Y0 + R.Y1) * 0.5f - 25.f, F1 + 170.f, F1 + 240.f, true });
			West.Add({ (R.Y0 + R.Y1) * 0.5f + 25.f, R.Y1 - 40.f, F1 + 170.f, F1 + 240.f, true });
			WallX(EPal::WallWhite, (R.Y0 + R.Y1) * 0.5f, WW_X0 + ExtT, WW_XC, F1, Ceil1, WallT, {});
			// The wash trough between the two doors, clear of both.
			Box(EPal::Terrazzo, FVector(WW_XC + 40.f, (R.Y0 + R.Y1) * 0.5f, F1 + 42.f), FVector(60.f, 110.f, 85.f), true);
			Box(EPal::Stainless, FVector(WW_XC + 40.f, (R.Y0 + R.Y1) * 0.5f, F1 + 86.f), FVector(50.f, 100.f, 3.f), false);
			continue;
		}
		Corridor.Add({ R.Y0 + 70.f, R.Y0 + 175.f, F1, F1 + 215.f, false });
		Corridor.Add({ R.Y1 - 175.f, R.Y1 - 70.f, F1, F1 + 215.f, false });
		Corridor.Add({ R.Y0 + 240.f, R.Y0 + Len * 0.5f - 15.f, F1 + 100.f, F1 + 235.f, true });
		Corridor.Add({ R.Y0 + Len * 0.5f + 15.f, R.Y1 - 240.f, F1 + 100.f, F1 + 235.f, true });
		West.Add({ R.Y0 + 120.f, R.Y0 + Len * 0.5f - 60.f, F1 + 90.f, F1 + 240.f, true });
		West.Add({ R.Y0 + Len * 0.5f + 60.f, R.Y1 - 120.f, F1 + 90.f, F1 + 240.f, true });
		Classroom(FBox2D(FVector2D(WW_X0 + ExtT, R.Y0 + WallT * 0.5f), FVector2D(WW_XC - WallT * 0.5f, R.Y1 - WallT * 0.5f)), F1, Ceil1, 0, R.Variant);
	}
	// C3 has a back exit into the west alley (flank route), between its two windows
	West.Add({ -548.f, -452.f, F1, F1 + 215.f, false });
	// stair well
	Corridor.Add({ WW_Y0, StairY1, F1, Ceil1, false });
	West.Add({ WW_Y0 + 150.f, WW_Y0 + 300.f, F1 + 230.f, F1 + 330.f, true });

	WallY(EPal::WallWhite, WW_X0 + ExtT * 0.5f, WW_Y0, WW_Y1, F1, Ceil1, ExtT, West, EPal::FacadeBrick, -1.f);
	WallY(EPal::WallCream, WW_XC, WW_Y0, WW_Y1, F1, Ceil1, WallT, Corridor);
	Dado(EPal::TileGreen, false, StairY1, WW_Y1, WW_XC + 10.f, WW_XC + 12.f, F1, 100.f, Corridor);
	for (const FRoom& R : Rooms)
	{
		WallX(EPal::WallWhite, R.Y0, WW_X0 + ExtT, WW_XC, F1, Ceil1, WallT, {});
	}
	WallX(EPal::WallWhite, WW_Y1 - WallT * 0.5f, WW_X0, WW_XC, F1, Ceil1, WallT, {});
	// stair well south wall (open to the admin arcade pocket)
	// A wide open doorway clear of the arcade column outside it; the flight starts just inside, so no leaf to swing into it.
	WallX(EPal::WallWhite, WW_Y0 + 10.f, WW_X0, WW_XC, F1, Top, 20.f, { { WW_XC - 300.f, WW_XC - 125.f, F1, F1 + 215.f, false } }, EPal::FacadeBrick, -1.f);
	// 2F walls of the stair well; the 3F mass starts a floor higher here, so the well needs its own outer wall and ceiling
	WallY(EPal::WallCream, WW_XC, WW_Y0, StairY1, F2, F2 + H - Slab, WallT, { { WW_Y0 + 20.f, StairY1 - 20.f, F2, F2 + 230.f, false } });
	WallY(EPal::WallWhite, WW_X0 + ExtT * 0.5f, WW_Y0, StairY1, F2, F2 + H - Slab, ExtT, {}, EPal::FacadeBrick, -1.f);
	CeilingAt(WW_X0 + ExtT, WW_Y0 + 20.f, WW_XC - WallT * 0.5f, StairY1, F2 + H - Slab);
	// plastered inner face of the classroom mass above, which the open 2F well looks onto
	Deco(EPal::WallWhite, FVector(WW_X0 + ExtT, StairY1 - WallT * 0.5f, F2), FVector(WW_XC - WallT * 0.5f, StairY1, F2 + H - Slab));
	Stair(FBox2D(FVector2D(WW_X0 + ExtT, WW_Y0 + 20.f), FVector2D(WW_XC - WallT * 0.5f, StairY1 - WallT * 0.5f)), true, true, true, F1, F2);

	// corridor props: rice steamer cabinets under the windows, the water dispenser on a blank stretch, bulletin boards
	Box(EPal::Stainless, FVector(WW_XC + 45.f, 720.f, F1 + 46.f), FVector(70.f, 140.f, 92.f), true);
	Box(EPal::Stainless, FVector(WW_XC + 35.f, -1000.f, F1 + 70.f), FVector(50.f, 45.f, 140.f), true);
	Box(EPal::Stainless, FVector(WW_XC + 45.f, 720.f, F2 + 46.f), FVector(70.f, 140.f, 92.f), true);
	// Bulletin boards on the blank wall between two classrooms' doors (each room's own stretch is windows).
	for (const float Yb : { -1850.f, -950.f })
	{
		Deco(EPal::BoardFrame, FVector(WW_XC + 10.f, Yb - 58.f, F1 + 98.f), FVector(WW_XC + 12.f, Yb + 58.f, F1 + 212.f));
		Deco(EPal::Cork, FVector(WW_XC + 10.f, Yb - 55.f, F1 + 101.f), FVector(WW_XC + 13.f, Yb + 55.f, F1 + 209.f));
	}

	Rect(WW_X0, WW_Y0, WW_XC, WW_Y1, 0);
	Rect(WW_XC, WW_Y0, WW_X1, WW_Y1, 2);
}

void FCampusSchoolBuildContext::BuildNorthWing()
{
	const float Top = F1 + 4.f * H;
	const float CornerX1 = WW_XC;
	const float StairEX0 = 950.f;
	BoxMM(EPal::Terrazzo, FVector(NW_X0, NW_Y0, 0.f), FVector(NW_X1, NW_Y1, F1));
	// 2F floor: corridor + junction + corner stair arrival strip + east stair arrival
	BoxMM(EPal::Terrazzo, FVector(CornerX1, NW_Y0, Ceil1), FVector(NW_X1, NW_YC, F2));
	BoxMM(EPal::Terrazzo, FVector(NW_X0, NW_Y0, Ceil1), FVector(CornerX1, NW_Y0 + 10.f + StairArrival, F2));
	BoxMM(EPal::Terrazzo, FVector(StairEX0, NW_YC, Ceil1), FVector(NW_X1, NW_YC + 10.f + StairArrival, F2));
	// upper masses
	BoxMM(EPal::FacadeBrick, FVector(CornerX1, NW_YC, F2), FVector(StairEX0, NW_Y1, Top));
	BoxMM(EPal::FacadeBrick, FVector(NW_X0, NW_Y0 + 70.f, F2 + H - Slab), FVector(CornerX1, NW_Y1, Top));
	BoxMM(EPal::FacadeBrick, FVector(NW_X0, NW_Y0, F2 + H - Slab), FVector(CornerX1, NW_Y0 + 70.f, Top));
	BoxMM(EPal::FacadeBrick, FVector(StairEX0, NW_YC, F2 + H - Slab), FVector(NW_X1, NW_Y1, Top));
	CeilingAt(CornerX1, NW_Y0, NW_X1, NW_YC - WallT * 0.5f, Ceil1);
	CeilingAt(PS_X0, NW_YC, PS_X1, NW_Y1, Ceil1);
	CeilingAt(WW_X1, NW_Y0, NW_X1, NW_YC, Top - Slab);
	for (int32 f = 2; f < 4; ++f)
	{
		const float Z = F1 + f * H;
		BoxMM(EPal::Terrazzo, FVector(WW_X1, NW_Y0, Z - Slab), FVector(NW_X1, NW_YC, Z));
		CeilingAt(WW_X1, NW_Y0, NW_X1, NW_YC, Z - Slab);
		BoxMM(EPal::FacadeBeige, FVector(WW_X1, NW_Y0, Z), FVector(NW_X1, NW_Y0 + 20.f, Z + 110.f));
		Deco(EPal::Railing, FVector(WW_X1, NW_Y0 - 2.f, Z + 110.f), FVector(NW_X1, NW_Y0 + 22.f, Z + 116.f));
	}
	BoxMM(EPal::Terrazzo, FVector(CornerX1, NW_Y0, Top - Slab), FVector(NW_X1, NW_YC, Top));
	BoxMM(EPal::FacadeBeige, FVector(NW_X0, NW_Y0 - 10.f, Top), FVector(NW_X1 + 10.f, NW_Y1 + 10.f, Top + 25.f));
	BoxMM(EPal::FacadeBeige, FVector(NW_X0, NW_Y1 - 20.f, Top + 25.f), FVector(NW_X1, NW_Y1, Top + 130.f));
	BoxMM(EPal::FacadeBeige, FVector(WW_X1, NW_Y0, Top + 25.f), FVector(NW_X1, NW_Y0 + 20.f, Top + 130.f));
	FacadeWindowsX(NW_Y1, NW_X0 + 100.f, NW_X1 - 100.f, F2, Top - 10.f, 450.f, 1.f);
	FacadeWindowsX(NW_YC, CornerX1 + 50.f, StairEX0 - 50.f, F2, Top - 10.f, 450.f, -1.f);
	// 2F parapet along the courtyard
	BoxMM(EPal::FacadeBeige, FVector(WW_X1, NW_Y0, F2), FVector(NW_X1, NW_Y0 + 20.f, F2 + 110.f));
	Deco(EPal::Railing, FVector(WW_X1, NW_Y0 - 2.f, F2 + 110.f), FVector(NW_X1, NW_Y0 + 22.f, F2 + 116.f));
	for (float X = WW_X1 + 225.f; X < NW_X1; X += 450.f)
	{
		BoxMM(EPal::Column, FVector(X - 22.f, NW_Y0, F1), FVector(X + 22.f, NW_Y0 + 45.f, Top));
	}

	struct FRoom { float X0; float X1; int32 Variant; };
	const FRoom Rooms[] = { { CornerX1, -2250.f, 1 }, { -2250.f, PS_X0, 0 }, { PS_X1, StairEX0, 2 } };
	TArray<FOpening> Front;
	TArray<FOpening> Back;
	for (const FRoom& R : Rooms)
	{
		const float Len = R.X1 - R.X0;
		Front.Add({ R.X0 + 70.f, R.X0 + 175.f, F1, F1 + 215.f, false });
		Front.Add({ R.X1 - 175.f, R.X1 - 70.f, F1, F1 + 215.f, false });
		Front.Add({ R.X0 + 240.f, R.X0 + Len * 0.5f - 15.f, F1 + 100.f, F1 + 235.f, true });
		Front.Add({ R.X0 + Len * 0.5f + 15.f, R.X1 - 240.f, F1 + 100.f, F1 + 235.f, true });
		Back.Add({ R.X0 + 120.f, R.X0 + Len * 0.5f - 70.f, F1 + 90.f, F1 + 240.f, true });
		Back.Add({ R.X0 + Len * 0.5f + 70.f, R.X1 - 120.f, F1 + 90.f, F1 + 240.f, true });
		Classroom(FBox2D(FVector2D(R.X0 + WallT * 0.5f, NW_YC + WallT * 0.5f), FVector2D(R.X1 - WallT * 0.5f, NW_Y1 - ExtT)), F1, Ceil1, 2, R.Variant);
	}
	// back doors to the field (flank routes), placed between window openings
	Back.Add({ -2890.f, -2790.f, F1, F1 + 215.f, false });
	Back.Add({ 187.f, 287.f, F1, F1 + 215.f, false });
	// passage
	Front.Add({ PS_X0, PS_X1, F1, Ceil1, false });
	Back.Add({ PS_X0, PS_X1, F1, Ceil1, false });
	// east stair entry
	Front.Add({ StairEX0 + 10.f, NW_X1 - 10.f, F1, Ceil1, false });
	Back.Add({ StairEX0 + 120.f, NW_X1 - 120.f, F1 + 230.f, F1 + 330.f, true });
	// corner stair: door into the alley, window to field
	Back.Add({ NW_X0 + 150.f, NW_X0 + 300.f, F1 + 230.f, F1 + 330.f, true });

	WallX(EPal::WallCream, NW_YC, CornerX1, NW_X1, F1, Ceil1, WallT, Front);
	WallX(EPal::WallWhite, NW_Y1 - ExtT * 0.5f, NW_X0, NW_X1, F1, Ceil1, ExtT, Back, EPal::FacadeBrick, 1.f);
	Dado(EPal::TileGreen, true, CornerX1, StairEX0, NW_YC - 12.f, NW_YC - 10.f, F1, 100.f, Front);
	for (float X : { -2250.f })
	{
		WallY(EPal::WallWhite, X, NW_YC, NW_Y1 - ExtT, F1, Ceil1, WallT, {});
	}
	WallY(EPal::FacadeBeige, PS_X0, NW_YC, NW_Y1, F1, Ceil1, WallT, {});
	WallY(EPal::FacadeBeige, PS_X1, NW_YC, NW_Y1, F1, Ceil1, WallT, {});
	WallY(EPal::WallWhite, StairEX0, NW_YC, NW_Y1, F1, Ceil1, WallT, {});
	WallY(EPal::WallWhite, NW_X1 - ExtT * 0.5f, NW_YC, NW_Y1, F1, Top, ExtT, {}, EPal::FacadeBrick, 1.f);
	// corner stair walls
	WallY(EPal::WallWhite, NW_X0 + ExtT * 0.5f, NW_Y0, NW_Y1, F1, Ceil1, ExtT, {}, EPal::FacadeBrick, -1.f);
	WallY(EPal::WallCream, CornerX1, NW_Y0 + 290.f, NW_Y1, F1, Ceil1, WallT, {});
	WallY(EPal::WallCream, CornerX1, NW_Y0, NW_Y1, F2, F2 + H - Slab, WallT, { { NW_Y0 + 10.f, NW_Y0 + 270.f, F2, F2 + 230.f, false } });
	WallX(EPal::WallCream, NW_YC, StairEX0, NW_X1, F2, F2 + H - Slab, WallT, { { StairEX0 + 20.f, NW_X1 - 20.f, F2, F2 + 230.f, false } });
	// Both stair wells are open on 2F under masses that start a floor higher: close their outer faces and ceilings.
	WallY(EPal::WallWhite, NW_X0 + ExtT * 0.5f, NW_Y0, NW_Y1, F2, F2 + H - Slab, ExtT, {}, EPal::FacadeBrick, -1.f);
	WallX(EPal::WallWhite, NW_Y1 - ExtT * 0.5f, NW_X0, CornerX1, F2, F2 + H - Slab, ExtT, {}, EPal::FacadeBrick, 1.f);
	WallX(EPal::WallWhite, NW_Y1 - ExtT * 0.5f, StairEX0, NW_X1, F2, F2 + H - Slab, ExtT, {}, EPal::FacadeBrick, 1.f);
	CeilingAt(NW_X0 + ExtT, NW_Y0, CornerX1 - WallT * 0.5f, NW_Y1 - ExtT, F2 + H - Slab);
	CeilingAt(StairEX0 + WallT * 0.5f, NW_YC + WallT * 0.5f, NW_X1 - ExtT, NW_Y1 - ExtT, F2 + H - Slab);
	// plastered faces of the neighbouring masses the open 2F wells look onto (the west wing to the south of the corner
	// stair, the classroom block west of the east stair)
	Deco(EPal::WallWhite, FVector(NW_X0 + ExtT, NW_Y0, F2), FVector(CornerX1 - WallT * 0.5f, NW_Y0 + 2.f, F2 + H - Slab));
	Deco(EPal::WallWhite, FVector(StairEX0, NW_YC + WallT * 0.5f, F2), FVector(StairEX0 + WallT * 0.5f, NW_Y1 - ExtT, F2 + H - Slab));
	Stair(FBox2D(FVector2D(NW_X0 + ExtT, NW_Y0 + 10.f), FVector2D(CornerX1 - WallT * 0.5f, NW_Y1 - ExtT)), false, false, false, F1, F2);
	Stair(FBox2D(FVector2D(StairEX0 + WallT * 0.5f, NW_YC + WallT * 0.5f), FVector2D(NW_X1 - ExtT, NW_Y1 - ExtT)), false, false, true, F1, F2);

	// passage notice board & school name over the field exit
	Deco(EPal::Cork, FVector(PS_X0 + 11.f, NW_YC + 100.f, F1 + 90.f), FVector(PS_X0 + 14.f, NW_Y1 - 100.f, F1 + 220.f));
	Deco(EPal::RedBoard, FVector(PS_X0 + 60.f, NW_Y1 + 2.f, Ceil1 + 30.f), FVector(PS_X1 - 60.f, NW_Y1 + 10.f, Ceil1 + 140.f));
	Box(EPal::Stainless, FVector(-1800.f, NW_YC - 45.f, F1 + 46.f), FVector(140.f, 70.f, 92.f), true);
	Box(EPal::Stainless, FVector(300.f, NW_YC - 35.f, F1 + 46.f), FVector(45.f, 50.f, 92.f), true);
	Box(EPal::Stainless, FVector(-1800.f, NW_YC - 45.f, F2 + 46.f), FVector(140.f, 70.f, 92.f), true);

	Rect(NW_X0, NW_YC, NW_X1, NW_Y1, 0);
	Rect(CornerX1, NW_Y0, NW_X1, NW_YC, 2);
	Rect(NW_X0, NW_Y0, CornerX1, NW_YC, 0);
}

void FCampusSchoolBuildContext::BuildWalkway()
{
	const float X0 = 1400.f;
	const float X1 = 1700.f;
	Deco(EPal::Terrazzo, FVector(X0, AD_Y1, 1.f), FVector(X1, NW_Y0, 3.f));
	for (float Y = AD_Y1 + 200.f; Y < NW_Y0; Y += 450.f)
	{
		BoxMM(EPal::PillarRed, FVector(X0 + 5.f, Y - 15.f, 0.f), FVector(X0 + 35.f, Y + 15.f, 320.f));
		BoxMM(EPal::PillarRed, FVector(X1 - 35.f, Y - 15.f, 0.f), FVector(X1 - 5.f, Y + 15.f, 320.f));
	}
	BoxMM(EPal::RoofGreen, FVector(X0 - 30.f, AD_Y1, 320.f), FVector(X1 + 30.f, NW_Y0, 335.f), true);
	// One fluorescent fitting under the canopy between each pair of pillars (not a continuous glowing strip).
	for (float Y = AD_Y1 + 425.f; Y < NW_Y0 - 100.f; Y += 450.f)
	{
		Fixture(FVector((X0 + X1) * 0.5f, Y, 320.f), true, 1800.f);
	}
	for (float Y : { -2400.f, -1200.f, 1200.f, 2400.f })
	{
		Bench(FVector(X0 + 60.f, Y, 0.f), 90.f);
	}
	Rect(X0, AD_Y1, X1, NW_Y0, 2);
}

void FCampusSchoolBuildContext::BuildCourtyard()
{
	// flag platform (升旗台)
	BoxMM(EPal::Stone, FVector(PS_X0, -2650.f, 0.f), FVector(PS_X1, -2250.f, 90.f));
	BoxMM(EPal::Stone, FVector(PS_X0 + 150.f, -2250.f, 0.f), FVector(PS_X1 - 150.f, -2190.f, 60.f), false);
	BoxMM(EPal::Stone, FVector(PS_X0 + 150.f, -2190.f, 0.f), FVector(PS_X1 - 150.f, -2130.f, 30.f), false);
	Ramp(FVector(CX, -2100.f, 0.f), FVector(CX, -2250.f, 90.f), 480.f, false);
	for (float X : { CX - 260.f, CX, CX + 260.f })
	{
		const float Height = X == CX ? 1350.f : 1180.f;
		Cyl(EPal::Stainless, FVector(X, -2560.f, 90.f), 12.f, Height, true);
		Sphere(EPal::Gold, FVector(X, -2560.f, 90.f + Height + 6.f), 16.f);
	}
	Deco(EPal::Flag, FVector(CX + 8.f, -2561.f, 1290.f), FVector(CX + 140.f, -2559.f, 1380.f));
	Deco(EPal::RedBoard, FVector(CX - 252.f, -2561.f, 1140.f), FVector(CX - 140.f, -2559.f, 1210.f));
	Deco(EPal::BinGreen, FVector(CX + 268.f, -2561.f, 1140.f), FVector(CX + 380.f, -2559.f, 1210.f));
	Rect(PS_X0, -2650.f, PS_X1, -2250.f, 2);

	// gardens with hedges & trees
	for (float GX : { -2200.f, 450.f })
	{
		const float X0 = GX - 500.f;
		const float X1 = GX + 500.f;
		const float Y0 = -1300.f;
		const float Y1 = -200.f;
		BoxMM(EPal::Stone, FVector(X0, Y0, 0.f), FVector(X1, Y0 + 30.f, 60.f));
		BoxMM(EPal::Stone, FVector(X0, Y1 - 30.f, 0.f), FVector(X1, Y1, 60.f));
		BoxMM(EPal::Stone, FVector(X0, Y0, 0.f), FVector(X0 + 30.f, Y1, 60.f));
		BoxMM(EPal::Stone, FVector(X1 - 30.f, Y0, 0.f), FVector(X1, Y1, 60.f));
		Deco(EPal::Grass, FVector(X0 + 30.f, Y0 + 30.f, 0.f), FVector(X1 - 30.f, Y1 - 30.f, 45.f));
		// Clipped hedges of real shrubs; an invisible guard stops people (hedges hide, they do not stop rounds).
		int32 HedgeSeed = (int32)GX;
		for (const FBox& Hedge : { FBox(FVector(X0 + 58.f, Y0 + 58.f, 45.f), FVector(X1 - 58.f, Y0 + 102.f, 108.f)), FBox(FVector(X0 + 58.f, Y1 - 102.f, 45.f), FVector(X0 + 382.f, Y1 - 58.f, 108.f)) })
		{
			ClippedHedge(Hedge, HedgeSeed);
			++HedgeSeed;
		}
		Tree(FVector(GX + 120.f, (Y0 + Y1) * 0.5f, 36.f), 1.1f);
		Rect(X0, Y0, X1, Y1, 3);
	}

	// statue of Confucius on a pedestal
	BoxMM(EPal::Stone, FVector(CX - 150.f, -900.f, 0.f), FVector(CX + 150.f, -600.f, 150.f));
	Cyl(EPal::Bronze, FVector(CX, -750.f, 150.f), 70.f, 140.f, true);
	Sphere(EPal::Bronze, FVector(CX, -750.f, 304.f), 34.f);
	Box(EPal::Bronze, FVector(CX, -722.f, 250.f), FVector(30.f, 8.f, 22.f), false);
	Rect(CX - 150.f, -900.f, CX + 150.f, -600.f, 2);

	// pavilion (涼亭)
	const FVector P(CX, 1300.f, 0.f);
	Deco(EPal::Stone, P + FVector(-420.f, -420.f, 0.f), P + FVector(420.f, 420.f, 12.f));
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Corner = P + FVector(i % 2 ? 330.f : -330.f, i / 2 ? 330.f : -330.f, 0.f);
		BoxMM(EPal::PillarRed, Corner - FVector(20.f, 20.f, 0.f), Corner + FVector(20.f, 20.f, 290.f));
	}
	BoxMM(EPal::RoofTile, P + FVector(-450.f, -450.f, 290.f), P + FVector(450.f, 450.f, 312.f));
	Cone(EPal::RoofTile, P + FVector(0.f, 0.f, 312.f), 1050.f, 230.f, false);
	Sphere(EPal::Gold, P + FVector(0.f, 0.f, 548.f), 30.f);
	Cyl(EPal::Stone, P, 130.f, 78.f, true);
	for (int32 i = 0; i < 4; ++i)
	{
		const float A = i * PI * 0.5f + PI * 0.25f;
		Cyl(EPal::Stone, P + FVector(FMath::Cos(A) * 110.f, FMath::Sin(A) * 110.f, 0.f), 42.f, 45.f, true);
	}
	Rect(CX - 450.f, 850.f, CX + 450.f, 1750.f, 2);

	// big banyan trees and benches
	Tree(FVector(-2650.f, 1950.f, 0.f), 1.5f);
	Tree(FVector(650.f, 2050.f, 0.f), 1.4f);
	Tree(FVector(-2700.f, -2750.f, 0.f), 1.2f);
	Tree(FVector(950.f, -2750.f, 0.f), 1.2f);
	for (const FVector& Loc : { FVector(-2650.f, 1550.f, 0.f), FVector(650.f, 1650.f, 0.f), FVector(-2200.f, 150.f, 0.f), FVector(450.f, 150.f, 0.f) })
	{
		Bench(Loc, 0.f);
	}
	BoxMM(EPal::Stone, FVector(-2900.f, 1700.f, 0.f), FVector(-2400.f, 2200.f, 45.f));
	BoxMM(EPal::Stone, FVector(400.f, 1800.f, 0.f), FVector(900.f, 2300.f, 45.f));

	// bulletin boards along the west corridor edge
	for (float Y : { -1500.f, 1800.f })
	{
		BoxMM(EPal::Railing, FVector(-2960.f, Y - 160.f, 0.f), FVector(-2950.f, Y - 150.f, 230.f));
		BoxMM(EPal::Railing, FVector(-2960.f, Y + 150.f, 0.f), FVector(-2950.f, Y + 160.f, 230.f));
		BoxMM(EPal::Cork, FVector(-2965.f, Y - 150.f, 80.f), FVector(-2945.f, Y + 150.f, 220.f));
		BoxMM(EPal::RoofGreen, FVector(-2990.f, Y - 175.f, 230.f), FVector(-2920.f, Y + 175.f, 238.f), false);
	}

	// recycling station (資源回收站)
	const FVector R(800.f, 2600.f, 0.f);
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Corner = R + FVector(i % 2 ? 290.f : -290.f, i / 2 ? 140.f : -140.f, 0.f);
		Cyl(EPal::Stainless, Corner, 10.f, 260.f, true);
	}
	BoxMM(EPal::RoofBlue, R + FVector(-320.f, -170.f, 255.f), R + FVector(320.f, 170.f, 265.f));
	const EPal Bins[6] = { EPal::BinBlue, EPal::BinYellow, EPal::BinGreen, EPal::BinBlue, EPal::BinYellow, EPal::BinGreen };
	for (int32 i = 0; i < 6; ++i)
	{
		Box(Bins[i], R + FVector(-250.f + i * 100.f, 60.f, 50.f), FVector(80.f, 60.f, 100.f), true);
	}
	Rect(R.X - 320.f, R.Y - 170.f, R.X + 320.f, R.Y + 170.f, 2);

	for (const FVector& L : { FVector(-2400.f, -1900.f, 0.f), FVector(600.f, -1900.f, 0.f), FVector(-2400.f, 2700.f, 0.f), FVector(-200.f, 2700.f, 0.f) })
	{
		Lamp(L);
	}
}

void FCampusSchoolBuildContext::BuildEastLane()
{
	// courts
	Deco(EPal::CourtBlue, FVector(2200.f, -3100.f, 1.f), FVector(4100.f, 3100.f, 2.f));
	for (float Y0 : { -2900.f, 100.f })
	{
		const float Y1 = Y0 + 2800.f;
		Deco(EPal::CourtGreen, FVector(2400.f, Y0, 2.f), FVector(3900.f, Y1, 3.f));
		const float L = 2.5f;
		Deco(EPal::LineWhite, FVector(2400.f, Y0, 3.f), FVector(3900.f, Y0 + 5.f, 3.f + L));
		Deco(EPal::LineWhite, FVector(2400.f, Y1 - 5.f, 3.f), FVector(3900.f, Y1, 3.f + L));
		Deco(EPal::LineWhite, FVector(2400.f, Y0, 3.f), FVector(2405.f, Y1, 3.f + L));
		Deco(EPal::LineWhite, FVector(3895.f, Y0, 3.f), FVector(3900.f, Y1, 3.f + L));
		Deco(EPal::LineWhite, FVector(2400.f, Y0 + 1397.f, 3.f), FVector(3900.f, Y0 + 1403.f, 3.f + L));
		for (int32 s = 0; s < 24; ++s)
		{
			const float A = s * 2.f * PI / 24.f;
			Box(EPal::LineWhite, FVector(3150.f + FMath::Cos(A) * 180.f, Y0 + 1400.f + FMath::Sin(A) * 180.f, 3.f + L * 0.5f), FVector(48.f, 5.f, L), false, FMath::RadiansToDegrees(A) + 90.f);
		}
		Deco(EPal::LineWhite, FVector(2905.f, Y0, 3.f), FVector(2910.f, Y0 + 580.f, 3.f + L));
		Deco(EPal::LineWhite, FVector(3390.f, Y0, 3.f), FVector(3395.f, Y0 + 580.f, 3.f + L));
		Deco(EPal::LineWhite, FVector(2905.f, Y0 + 575.f, 3.f), FVector(3395.f, Y0 + 580.f, 3.f + L));
		Deco(EPal::LineWhite, FVector(2905.f, Y1 - 580.f, 3.f), FVector(2910.f, Y1, 3.f + L));
		Deco(EPal::LineWhite, FVector(3390.f, Y1 - 580.f, 3.f), FVector(3395.f, Y1, 3.f + L));
		Deco(EPal::LineWhite, FVector(2905.f, Y1 - 580.f, 3.f), FVector(3395.f, Y1 - 575.f, 3.f + L));
		Hoop(3150.f, Y0, 1.f);
		Hoop(3150.f, Y1, -1.f);
		Rect(2400.f, Y0, 3900.f, Y1, 1);
	}

	// bleacher benches beside the courts
	for (float Y0 : { -2650.f, 450.f })
	{
		const float Y1 = Y0 + 2200.f;
		BoxMM(EPal::Concrete, FVector(1980.f, Y0, 0.f), FVector(2160.f, Y1, 40.f));
		BoxMM(EPal::Concrete, FVector(1880.f, Y0, 0.f), FVector(1980.f, Y1, 80.f));
		BoxMM(EPal::Concrete, FVector(1780.f, Y0, 0.f), FVector(1880.f, Y1, 120.f));
		Rect(1780.f, Y0, 2160.f, Y1, 2);
	}

	// equipment shed (體育器材室)
	const float SX0 = 4150.f, SX1 = 4700.f, SY0 = -500.f, SY1 = 500.f;
	BoxMM(EPal::Concrete, FVector(SX0, SY0, 0.f), FVector(SX1, SY1, 10.f));
	WallY(EPal::FacadeBeige, SX0 + 10.f, SY0, SY1, 10.f, 300.f, 20.f, { { -150.f, 50.f, 10.f, 225.f, false }, { 200.f, 400.f, 120.f, 220.f, true } });
	WallY(EPal::FacadeBeige, SX1 - 10.f, SY0, SY1, 10.f, 300.f, 20.f, {});
	WallX(EPal::FacadeBeige, SY0 + 10.f, SX0, SX1, 10.f, 300.f, 20.f, {});
	WallX(EPal::FacadeBeige, SY1 - 10.f, SX0, SX1, 10.f, 300.f, 20.f, {});
	BoxMM(EPal::RoofGreen, FVector(SX0 - 40.f, SY0 - 40.f, 300.f), FVector(SX1 + 40.f, SY1 + 40.f, 315.f));
	Box(EPal::MatBlue, FVector(4500.f, -300.f, 50.f), FVector(200.f, 120.f, 100.f), true);
	Box(EPal::Shelf, FVector(4640.f, 150.f, 100.f), FVector(60.f, 400.f, 200.f), true);
	for (int32 i = 0; i < 6; ++i)
	{
		Sphere(EPal::Orange, FVector(4400.f + (i % 3) * 40.f, 250.f + (i / 3) * 40.f, 21.5f), 24.f, false);
	}
	Rect(SX0, SY0, SX1, SY1, 0);

	// bicycle shed (腳踏車棚)
	for (float Y = -3000.f; Y <= -1000.f; Y += 400.f)
	{
		Cyl(EPal::Stainless, FVector(4370.f, Y, 0.f), 10.f, 240.f, true);
		Cyl(EPal::Stainless, FVector(4730.f, Y, 0.f), 10.f, 230.f, true);
	}
	BoxR(EPal::RoofGreen, FVector(4550.f, -2000.f, 240.f), FVector(460.f, 2080.f, 8.f), FRotator(0.f, 0.f, 3.f), true);
	for (int32 i = 0; i < 14; ++i)
	{
		Bicycle(FVector(4560.f, -2900.f + i * 140.f, 0.f), 0.f);
	}
	Rect(4350.f, -3000.f, 4750.f, -1000.f, 2);

	// school store (福利社)
	const float BX0 = 3700.f, BX1 = 4700.f, BY0 = 2400.f, BY1 = 3600.f;
	BoxMM(EPal::Terrazzo, FVector(BX0, BY0, 0.f), FVector(BX1, BY1, 12.f));
	WallY(EPal::FacadeBeige, BX0 + 12.f, BY0, BY1, 12.f, 380.f, 25.f, { { 2800.f, 3300.f, 110.f, 240.f, true } });
	WallY(EPal::FacadeBeige, BX1 - 12.f, BY0, BY1, 12.f, 380.f, 25.f, {});
	WallX(EPal::FacadeBeige, BY0 + 12.f, BX0, BX1, 12.f, 380.f, 25.f, { { 3900.f, 4080.f, 12.f, 230.f, false } });
	WallX(EPal::FacadeBeige, BY1 - 12.f, BX0, BX1, 12.f, 380.f, 25.f, { { 4350.f, 4460.f, 12.f, 225.f, false } });
	BoxMM(EPal::RoofBlue, FVector(BX0 - 60.f, BY0 - 60.f, 380.f), FVector(BX1 + 60.f, BY1 + 60.f, 396.f));
	// Counter: laminate top on a panelled body with a kick plate.
	Box(EPal::WoodDesk, FVector(BX0 + 60.f, 3050.f, 100.f), FVector(70.f, 520.f, 6.f), true);
	Box(EPal::DeskFrame, FVector(BX0 + 62.f, 3050.f, 50.f), FVector(60.f, 510.f, 94.f), true);
	Deco(EPal::Black, FVector(BX0 + 32.f, 2796.f, 0.f), FVector(BX0 + 92.f, 3304.f, 10.f));

	// Packs and cartons: mostly white, cream and cardboard with some coloured packaging, not a box of building blocks.
	static const EPal Goods[] = { EPal::ScooterWhite, EPal::WallCream, EPal::Cork, EPal::WoodDesk, EPal::ScooterRed, EPal::WallWhite, EPal::BinBlue,
		EPal::BinYellow, EPal::RedBoard, EPal::Shelf, EPal::BinGreen, EPal::ScooterWhite };
	// Gondola shelving (double sided): steel uprights and shelves, a pegboard spine, stocked with packs and boxes.
	for (int32 r = 0; r < 3; ++r)
	{
		const float GX = 4050.f + r * 200.f;
		const float GY0 = 2750.f, GY1 = 3350.f;
		Blocker(FVector(GX - 26.f, GY0, 0.f), FVector(GX + 26.f, GY1, 165.f));
		Deco(EPal::Stainless, FVector(GX - 1.f, GY0, 8.f), FVector(GX + 1.f, GY1, 165.f));
		for (float Y = GY0; Y <= GY1 + 1.f; Y += 150.f)
		{
			Deco(EPal::IronBars, FVector(GX - 26.f, Y - 2.f, 0.f), FVector(GX + 26.f, Y + 2.f, 165.f));
		}
		int32 Item = r * 977;
		for (const float Z : { 8.f, 46.f, 84.f, 122.f })
		{
			Deco(EPal::WindowFrame, FVector(GX - 26.f, GY0, Z - 2.f), FVector(GX + 26.f, GY1, Z));
			for (const float Side : { -1.f, 1.f })
			{
				float Y = GY0 + 4.f;
				while (true)
				{
					const uint32 PackHash = Hash(Item++, 4711);
					const float PackW = 9.f + (PackHash % 9);
					if (Y + PackW > GY1 - 4.f)
					{
						break;
					}
					const float PackD = 14.f + ((PackHash >> 4) % 8);
					const float PackT = 12.f + ((PackHash >> 8) % 16);
					const float PackX = Side < 0.f ? GX - 3.f - PackD : GX + 3.f;
					Deco(Goods[(PackHash >> 12) % UE_ARRAY_COUNT(Goods)], FVector(PackX, Y, Z), FVector(PackX + PackD, Y + PackW, Z + PackT));
					Y += PackW + 1.f + ((PackHash >> 16) % 3);
				}
			}
		}
		Deco(EPal::WindowFrame, FVector(GX - 26.f, GY0, 163.f), FVector(GX + 26.f, GY1, 165.f));
	}

	// Glass-door drinks cooler against the east wall: dark carcass, lit interior, rows of bottles behind three doors.
	{
		const float FX0 = 4602.f, FX1 = 4675.f, FY0 = 2690.f, FY1 = 2910.f;
		Blocker(FVector(FX0, FY0, 0.f), FVector(FX1, FY1, 200.f));
		Deco(EPal::IronBars, FVector(FX0, FY0, 0.f), FVector(FX1, FY0 + 5.f, 200.f));
		Deco(EPal::IronBars, FVector(FX0, FY1 - 5.f, 0.f), FVector(FX1, FY1, 200.f));
		Deco(EPal::IronBars, FVector(FX0, FY0, 182.f), FVector(FX1, FY1, 200.f));
		Deco(EPal::Black, FVector(FX0, FY0, 0.f), FVector(FX1, FY1, 14.f));
		Deco(EPal::WallWhite, FVector(FX1 - 4.f, FY0, 14.f), FVector(FX1, FY1, 182.f));
		Deco(EPal::SignRed, FVector(FX0 - 1.f, FY0 + 20.f, 186.f), FVector(FX0, FY1 - 20.f, 197.f));
		int32 Bottle = 0;
		for (const float Z : { 14.f, 56.f, 98.f, 140.f })
		{
			Deco(EPal::Stainless, FVector(FX0 + 6.f, FY0 + 5.f, Z - 1.f), FVector(FX1 - 4.f, FY1 - 5.f, Z));
			for (const float X : { FX0 + 14.f, FX0 + 24.f, FX0 + 34.f })
			{
				for (float Y = FY0 + 10.f; Y < FY1 - 8.f; Y += 7.5f)
				{
					const uint32 BottleHash = Hash(Bottle++, 1999);
					Cyl(Goods[BottleHash % UE_ARRAY_COUNT(Goods)], FVector(X, Y, Z), 6.2f, 20.f + (BottleHash >> 6) % 6, false);
				}
			}
			Deco(EPal::LightPanel, FVector(FX0 + 6.f, FY0 + 6.f, Z + 37.f), FVector(FX0 + 8.f, FY1 - 6.f, Z + 38.f));
		}
		for (int32 d = 0; d < 3; ++d)
		{
			const float Y0 = FY0 + 5.f + d * (FY1 - FY0 - 10.f) / 3.f;
			const float Y1 = Y0 + (FY1 - FY0 - 10.f) / 3.f;
			Deco(EPal::GlassClear, FVector(FX0 + 1.f, Y0 + 3.f, 17.f), FVector(FX0 + 2.f, Y1 - 3.f, 179.f));
			Deco(EPal::IronBars, FVector(FX0, Y0, 14.f), FVector(FX0 + 3.f, Y0 + 3.f, 182.f));
			Deco(EPal::IronBars, FVector(FX0, Y1 - 3.f, 14.f), FVector(FX0 + 3.f, Y1, 182.f));
			Deco(EPal::IronBars, FVector(FX0, Y0, 14.f), FVector(FX0 + 3.f, Y1, 17.f));
			Deco(EPal::IronBars, FVector(FX0, Y0, 179.f), FVector(FX0 + 3.f, Y1, 182.f));
			Deco(EPal::Stainless, FVector(FX0 - 3.f, Y1 - 9.f, 70.f), FVector(FX0, Y1 - 6.f, 130.f));
		}
	}
	// vending machines
	for (int32 i = 0; i < 2; ++i)
	{
		const float Y = 3420.f + i * 90.f;
		Box(i == 0 ? EPal::ScooterRed : EPal::ScooterBlue, FVector(BX0 - 40.f, Y, 90.f), FVector(70.f, 70.f, 180.f), true);
		Box(EPal::ClockFace, FVector(BX0 - 76.f, Y, 115.f), FVector(2.f, 52.f, 90.f), false);
	}
	Rect(BX0, BY0, BX1, BY1, 0);

	// side alley (east of admin) clutter
	for (float Y = -4100.f; Y < -3300.f; Y += 220.f)
	{
		Box(EPal::Locker, FVector(4060.f, Y, 45.f), FVector(80.f, 90.f, 90.f), true);
	}
	Box(EPal::BinGreen, FVector(4550.f, -3800.f, 65.f), FVector(220.f, 130.f, 130.f), true);
	Tree(FVector(4500.f, 1700.f, 0.f), 1.f);
	Tree(FVector(4500.f, -4300.f, 0.f), 0.9f);
	Lamp(FVector(2000.f, 0.f, 0.f));
	Lamp(FVector(4200.f, -1500.f, 0.f));
}

void FCampusSchoolBuildContext::BuildField()
{
	Deco(EPal::Track, FVector(-4800.f, 4400.f, 1.f), FVector(4800.f, 5130.f, 2.5f));
	for (int32 i = 0; i <= 6; ++i)
	{
		const float Y = 4400.f + i * 121.6f;
		Deco(EPal::LineWhite, FVector(-4800.f, Y - 2.5f, 2.5f), FVector(4800.f, Y + 2.5f, 3.f));
	}
	Deco(EPal::Grass, FVector(-4800.f, 5130.f, 1.f), FVector(4800.f, 5600.f, 2.f));
	Rect(-4800.f, 4400.f, 4800.f, 5130.f, 1);

	// podium stage (司令台)
	const float X0 = PS_X0 - 200.f, X1 = PS_X1 + 200.f, Y0 = 4190.f, Y1 = 4690.f;
	BoxMM(EPal::Stone, FVector(X0, Y0, 0.f), FVector(X1, Y1, 120.f));
	BoxMM(EPal::FacadeBeige, FVector(X0, Y0, 120.f), FVector(X1, Y0 + 25.f, 330.f));
	for (int32 i = 0; i < 4; ++i)
	{
		const float X = i % 2 ? X1 - 25.f : X0 + 25.f;
		const float Y = i / 2 ? Y1 - 25.f : Y0 + 25.f;
		BoxMM(EPal::Column, FVector(X - 20.f, Y - 20.f, 120.f), FVector(X + 20.f, Y + 20.f, 460.f));
	}
	BoxMM(EPal::RoofTile, FVector(X0 - 80.f, Y0 - 60.f, 460.f), FVector(X1 + 80.f, Y1 + 60.f, 482.f));
	Box(EPal::WoodDesk, FVector(CX, Y1 - 120.f, 175.f), FVector(80.f, 50.f, 110.f), true);
	Deco(EPal::RedBoard, FVector(CX - 300.f, Y0 + 25.f, 230.f), FVector(CX + 300.f, Y0 + 28.f, 320.f));
	for (float X : { X0 - 90.f, X1 + 90.f })
	{
		BoxMM(EPal::Stone, FVector(X - 90.f, Y0 + 120.f, 0.f), FVector(X + 90.f, Y0 + 320.f, 40.f), false);
		BoxMM(EPal::Stone, FVector(X - 90.f, Y0 + 120.f, 40.f), FVector(X + 90.f, Y0 + 220.f, 80.f), false);
	}
	Ramp(FVector(X0 - 180.f, Y0 + 400.f, 0.f), FVector(X0, Y0 + 400.f, 120.f), 160.f, true);
	Ramp(FVector(X1 + 180.f, Y0 + 400.f, 0.f), FVector(X1, Y0 + 400.f, 120.f), 160.f, true);
	Rect(X0, Y0, X1, Y1, 2);

	// bleachers (看台)
	BoxMM(EPal::Concrete, FVector(1800.f, 4200.f, 0.f), FVector(4400.f, 4280.f, 120.f));
	BoxMM(EPal::Concrete, FVector(1800.f, 4280.f, 0.f), FVector(4400.f, 4360.f, 80.f));
	BoxMM(EPal::Concrete, FVector(1800.f, 4360.f, 0.f), FVector(4400.f, 4400.f, 40.f));
	Rect(1800.f, 4200.f, 4400.f, 4400.f, 2);
	BoxMM(EPal::Concrete, FVector(-3900.f, 4250.f, 0.f), FVector(-3300.f, 4330.f, 100.f));
	Tree(FVector(-4450.f, 4300.f, 0.f), 1.2f);

	// beyond the fence: the football pitch, mown in 6 m stripes (also breaks up the turf texture), its markings and goal
	{
		int32 Stripe = 0;
		for (float Y = 5600.f; Y < 12000.f; Y += 600.f, ++Stripe)
		{
			Deco(Stripe % 2 ? EPal::GrassMown : EPal::Grass, FVector(-9000.f, Y, 0.f), FVector(9000.f, FMath::Min(Y + 600.f, 12000.f), 1.f));
		}
		const float GoalY = 7000.f;
		const float L = 1.4f;
		auto Line = [&](float X0, float Y0, float X1, float Y1) { Deco(EPal::LineWhite, FVector(X0, Y0, 1.f), FVector(X1, Y1, 1.f + L)); };
		Line(-3400.f, GoalY - 6.f, 3400.f, GoalY + 6.f);
		Line(-3406.f, GoalY, -3394.f, 8960.f);
		Line(3394.f, GoalY, 3406.f, 8960.f);
		// penalty area (40.3 x 16.5 m), goal area (18.3 x 5.5 m), penalty spot
		Line(-2015.f, GoalY, -2003.f, GoalY + 1650.f);
		Line(2003.f, GoalY, 2015.f, GoalY + 1650.f);
		Line(-2015.f, GoalY + 1644.f, 2015.f, GoalY + 1656.f);
		Line(-915.f, GoalY, -903.f, GoalY + 550.f);
		Line(903.f, GoalY, 915.f, GoalY + 550.f);
		Line(-915.f, GoalY + 544.f, 915.f, GoalY + 556.f);
		Deco(EPal::LineWhite, FVector(-12.f, GoalY + 1088.f, 1.f), FVector(12.f, GoalY + 1112.f, 1.f + L));
		// goal: posts and bar on the line, the frame running back to the net supports
		for (float X : { -366.f, 366.f })
		{
			BoxMM(EPal::WallWhite, FVector(X - 6.f, GoalY - 6.f, 0.f), FVector(X + 6.f, GoalY + 6.f, 244.f), false);
			BoxMM(EPal::IronBars, FVector(X - 2.f, GoalY - 180.f, 0.f), FVector(X + 2.f, GoalY - 176.f, 150.f), false);
			// Raking back from the crossbar down to the top of the net support.
			BoxR(EPal::IronBars, FVector(X, GoalY - 90.f, 198.f), FVector(4.f, 202.f, 4.f), FRotator(0.f, 0.f, -27.f), false);
			BoxMM(EPal::IronBars, FVector(X - 2.f, GoalY - 180.f, 0.f), FVector(X + 2.f, GoalY - 4.f, 4.f), false);
		}
		Deco(EPal::WallWhite, FVector(-372.f, GoalY - 6.f, 244.f), FVector(372.f, GoalY + 6.f, 256.f));
		Deco(EPal::IronBars, FVector(-368.f, GoalY - 180.f, 148.f), FVector(368.f, GoalY - 176.f, 152.f));
		Deco(EPal::IronBars, FVector(-368.f, GoalY - 180.f, 0.f), FVector(368.f, GoalY - 176.f, 4.f));
	}
	Deco(EPal::Track, FVector(-6000.f, 9000.f, 1.f), FVector(6000.f, 9730.f, 2.f));
}

void FCampusSchoolBuildContext::BuildPlaza()
{
	// flower beds (cover)
	for (const FBox2D& Bed : { FBox2D(FVector2D(-3800.f, -5000.f), FVector2D(-2000.f, -4650.f)), FBox2D(FVector2D(400.f, -5000.f), FVector2D(2600.f, -4650.f)), FBox2D(FVector2D(-3700.f, -4430.f), FVector2D(-1700.f, -4260.f)), FBox2D(FVector2D(100.f, -4430.f), FVector2D(3600.f, -4260.f)) })
	{
		BoxMM(EPal::Stone, FVector(Bed.Min.X, Bed.Min.Y, 0.f), FVector(Bed.Max.X, Bed.Max.Y, 60.f));
		// Planted beds are grown over (low creeping cover and leaf litter), not bare dirt.
		Deco(EPal::Grass, FVector(Bed.Min.X + 18.f, Bed.Min.Y + 18.f, 60.f), FVector(Bed.Max.X - 18.f, Bed.Max.Y - 18.f, 62.f));
		// precast coping round the rim, proud of the wall and a little above the soil
		const FVector2D Lo = Bed.Min - FVector2D(2.5f, 2.5f);
		const FVector2D Hi = Bed.Max + FVector2D(2.5f, 2.5f);
		BoxMM(EPal::Concrete, FVector(Lo.X, Lo.Y, 58.f), FVector(Hi.X, Bed.Min.Y + 18.f, 66.f));
		BoxMM(EPal::Concrete, FVector(Lo.X, Bed.Max.Y - 18.f, 58.f), FVector(Hi.X, Hi.Y, 66.f));
		BoxMM(EPal::Concrete, FVector(Lo.X, Bed.Min.Y + 18.f, 58.f), FVector(Bed.Min.X + 18.f, Bed.Max.Y - 18.f, 66.f));
		BoxMM(EPal::Concrete, FVector(Bed.Max.X - 18.f, Bed.Min.Y + 18.f, 58.f), FVector(Hi.X, Bed.Max.Y - 18.f, 66.f));
		Rect(Bed.Min.X, Bed.Min.Y, Bed.Max.X, Bed.Max.Y, 3);
	}
	// In the deep front beds: the tree models stand on a root disc wider than the narrow beds along the building. Set a
	// little into the soil so the disc reads as the mound round the trunk.
	for (float X : { -3300.f, -2500.f, 900.f, 1650.f, 2350.f })
	{
		Tree(FVector(X, -4825.f, 50.f), 0.85f);
	}

	// guard booth (警衛室)
	const float GX0 = -2300.f, GX1 = -1800.f, GY0 = -5560.f, GY1 = -5160.f;
	BoxMM(EPal::Terrazzo, FVector(GX0, GY0, 0.f), FVector(GX1, GY1, 10.f));
	WallY(EPal::FacadeBeige, GX1 - 10.f, GY0, GY1, 10.f, 280.f, 20.f, { { GY0 + 90.f, GY1 - 90.f, 100.f, 220.f, true } });
	WallY(EPal::FacadeBeige, GX0 + 10.f, GY0, GY1, 10.f, 280.f, 20.f, {});
	WallX(EPal::FacadeBeige, GY0 + 10.f, GX0, GX1, 10.f, 280.f, 20.f, {});
	WallX(EPal::FacadeBeige, GY1 - 10.f, GX0, GX1, 10.f, 280.f, 20.f, { { GX0 + 80.f, GX0 + 180.f, 10.f, 215.f, false }, { GX0 + 260.f, GX1 - 60.f, 100.f, 220.f, true } });
	BoxMM(EPal::RoofBlue, FVector(GX0 - 50.f, GY0 - 50.f, 280.f), FVector(GX1 + 50.f, GY1 + 50.f, 292.f));
	OfficeDesk(FVector(-1950.f, -5360.f, 10.f), 0.f);
	Rect(GX0, GY0, GX1, GY1, 0);

	// school name wall
	BoxMM(EPal::Stone, FVector(-150.f, -5520.f, 0.f), FVector(650.f, -5460.f, 190.f));
	Deco(EPal::Gold, FVector(-80.f, -5463.f, 80.f), FVector(580.f, -5457.f, 150.f));
	Rect(-150.f, -5520.f, 650.f, -5460.f, 2);

	// scooter parking (機車停車格)
	for (int32 i = 0; i < 12; ++i)
	{
		Scooter(FVector(-4550.f + i * 165.f, -5400.f, 0.f), 90.f + (i % 2 ? 4.f : -3.f), i);
		Deco(EPal::LineWhite, FVector(-4630.f + i * 165.f, -5560.f, 2.f), FVector(-4625.f + i * 165.f, -5250.f, 3.f));
	}
	for (int32 i = 0; i < 9; ++i)
	{
		Scooter(FVector(2900.f + i * 165.f, -5400.f, 0.f), 90.f + (i % 3) * 3.f, i + 5);
	}
	Rect(-4630.f, -5560.f, -2600.f, -5250.f, 2);
	Rect(2820.f, -5560.f, 4400.f, -5250.f, 2);
	Lamp(FVector(-1500.f, -4700.f, 0.f));
	Lamp(FVector(-200.f, -4700.f, 0.f));
}

void FCampusSchoolBuildContext::BuildWestAlley()
{
	for (float Y : { -2400.f, -1100.f, 900.f, 2100.f })
	{
		Box(EPal::Locker, FVector(-4150.f, Y, 40.f), FVector(70.f, 95.f, 80.f), true);
	}
	// discarded desks stacked against the wall
	StudentDesk(FVector(-4600.f, -1600.f, 0.f), 10.f);
	StudentDesk(FVector(-4600.f, -1500.f, 0.f), 175.f);
	StudentDesk(FVector(-4620.f, -1550.f, 76.f), 80.f);
	Box(EPal::BinGreen, FVector(-4580.f, 1300.f, 65.f), FVector(220.f, 130.f, 130.f), true);
	Cyl(EPal::TankBlue, FVector(-4580.f, 2900.f, 0.f), 140.f, 160.f, true);
	Box(EPal::Concrete, FVector(-4550.f, -300.f, 50.f), FVector(160.f, 160.f, 100.f), true);
	Tree(FVector(-4550.f, -2900.f, 0.f), 0.9f);
	Tree(FVector(-4550.f, 500.f, 0.f), 0.9f);
	Tree(FVector(-4550.f, 3500.f, 0.f), 0.9f);
	Rect(-4660.f, -380.f, -4440.f, -220.f, 2);
	Rect(-4690.f, 1235.f, -4470.f, 1365.f, 2);
}

void FCampusSchoolBuildContext::BuildGunfightArena()
{
	B->GunfightBounds = FBox(FVector(1750.f, -2000.f, 0.f), FVector(4150.f, 2000.f, 600.f));
	GunfightWall(FVector(1700.f, -2000.f, 0.f), FVector(1750.f, 2000.f, 600.f));
	GunfightWall(FVector(4150.f, -2000.f, 0.f), FVector(4200.f, 2000.f, 600.f));
	GunfightWall(FVector(1700.f, -2050.f, 0.f), FVector(4200.f, -2000.f, 600.f));
	GunfightWall(FVector(1700.f, 2000.f, 0.f), FVector(4200.f, 2050.f, 600.f));

	for (float S : { -1.f, 1.f })
	{
		Box(EPal::MatBlue, FVector(2650.f, S * 900.f, 45.f), FVector(200.f, 140.f, 90.f), true);
		Box(EPal::Stainless, FVector(3650.f, S * 700.f, 50.f), FVector(120.f, 70.f, 100.f), true);
		Box(EPal::WoodDesk, FVector(2400.f, S * 1450.f, 40.f), FVector(80.f, 80.f, 80.f), true);
		Box(EPal::WoodDesk, FVector(3900.f, S * 1450.f, 40.f), FVector(80.f, 80.f, 80.f), true);
		Rect(2550.f, S * 900.f - 70.f, 2750.f, S * 900.f + 70.f, 2);
	}
	Box(EPal::WoodDesk, FVector(2850.f, 0.f, 40.f), FVector(80.f, 80.f, 80.f), true);
	Box(EPal::WoodDesk, FVector(3450.f, 0.f, 40.f), FVector(80.f, 80.f, 80.f), true);
	B->GunfightFlag = FVector(2550.f, 0.f, 0.f);

	Spawn(FVector(3150.f, -1850.f, 110.f), 90.f, ECampusTeam::Alpha, true);
	Spawn(FVector(2850.f, -1850.f, 110.f), 90.f, ECampusTeam::Alpha, true);
	Spawn(FVector(3150.f, 1850.f, 110.f), -90.f, ECampusTeam::Bravo, true);
	Spawn(FVector(2850.f, 1850.f, 110.f), -90.f, ECampusTeam::Bravo, true);
	for (const FVector& V : { FVector(2400.f, -1200.f, 0.f), FVector(3900.f, -1200.f, 0.f), FVector(2400.f, 1200.f, 0.f), FVector(3900.f, 1200.f, 0.f), FVector(2550.f, 0.f, 0.f), FVector(3700.f, 0.f, 0.f), FVector(3150.f, -900.f, 0.f), FVector(3150.f, 900.f, 0.f) })
	{
		B->GunfightHotspots.Add(V);
	}
}

void FCampusSchoolBuildContext::BuildSurroundings()
{
	if (!B->bBuildSurroundings)
	{
		return;
	}
	// sidewalks & road markings
	Deco(EPal::Concrete, FVector(-7000.f, -5900.f, 0.f), FVector(7000.f, -5615.f, 15.f));
	Deco(EPal::Concrete, FVector(-7000.f, -7900.f, 0.f), FVector(7000.f, -7600.f, 15.f));
	Deco(EPal::Concrete, FVector(-5100.f, -7600.f, 0.f), FVector(-4815.f, 5600.f, 15.f));
	Deco(EPal::Concrete, FVector(4815.f, -7600.f, 0.f), FVector(5100.f, 5600.f, 15.f));
	for (float X = -6800.f; X < 6800.f; X += 600.f)
	{
		Deco(EPal::BinYellow, FVector(X, -6760.f, 0.f), FVector(X + 300.f, -6745.f, 1.f));
	}
	for (float Y = -7400.f; Y < 5400.f; Y += 600.f)
	{
		Deco(EPal::BinYellow, FVector(-6015.f, Y, 0.f), FVector(-6000.f, Y + 300.f, 1.f));
		Deco(EPal::BinYellow, FVector(6000.f, Y, 0.f), FVector(6015.f, Y + 300.f, 1.f));
	}
	// row houses (透天厝) on three sides
	int32 Seed = 0;
	for (float X = -8750.f; X <= 8750.f; X += 500.f)
	{
		RowHouse(FVector(X, -7900.f, 0.f), -90.f, 500.f, Seed++);
	}
	for (float Y = -7400.f; Y <= 5400.f; Y += 500.f)
	{
		RowHouse(FVector(-7100.f, Y, 0.f), 0.f, 500.f, Seed++);
		RowHouse(FVector(7100.f, Y, 0.f), 180.f, 500.f, Seed++);
	}
	// convenience store on the corner (bright stripes)
	BoxMM(EPal::WallWhite, FVector(-6400.f, -9100.f, 0.f), FVector(-4700.f, -7950.f, 420.f));
	Deco(EPal::SignGreen, FVector(-6400.f, -7955.f, 330.f), FVector(-4700.f, -7945.f, 350.f));
	Deco(EPal::SignOrange, FVector(-6400.f, -7955.f, 350.f), FVector(-4700.f, -7945.f, 370.f));
	Deco(EPal::SignRed, FVector(-6400.f, -7955.f, 370.f), FVector(-4700.f, -7945.f, 390.f));
	Deco(EPal::LightPanel, FVector(-6300.f, -7952.f, 30.f), FVector(-4800.f, -7948.f, 300.f));
	// utility poles with wires along the south road
	for (float X = -6500.f; X <= 6500.f; X += 2600.f)
	{
		if (!Prop(TEXT("modular_electricity_poles"), FVector(X, -5800.f, 0.f), 0.f, 1000.f, false, TEXT("preset_01_")))
		{
			Cyl(EPal::Stone, FVector(X, -5800.f, 0.f), 28.f, 1000.f, true);
			Box(EPal::Black, FVector(X, -5800.f, 950.f), FVector(10.f, 140.f, 10.f), false);
		}
		Prop(TEXT("water_manhole_cover"), FVector(X + 1300.f, -6750.f, -4.f), 0.f, 0.f, false);
		Prop(TEXT("street_lamp_01"), FVector(X + 650.f, -5700.f, 15.f), 90.f, 600.f);
	}
	for (float Z : { 920.f, 950.f })
	{
		Deco(EPal::Black, FVector(-6500.f, -5862.f, Z), FVector(6500.f, -5860.f, Z + 2.f));
		Deco(EPal::Black, FVector(-6500.f, -5740.f, Z), FVector(6500.f, -5738.f, Z + 2.f));
	}
	// Mid-rise city beyond the street blocks (公寓 / 大樓): wrap-around window bands and slab lines, rooftop stair
	// houses and water tanks, so the horizon between the row houses reads as a dense Taiwanese city. Lower towards
	// the north so the mountains stay in view.
	{
		int32 CitySeed = 6000;
		// Rings at ~130 m, ~250 m and ~420 m from the school: walk-ups close in, towers further out. Lower towards the
		// north so the mountains stay in view.
		const float RingRadius[3] = { 13500.f, 24500.f, 40000.f };
		const float RingStep[3] = { 6.f, 4.2f, 3.f };
		const float RingFloors[3] = { 9.f, 18.f, 28.f };
		const float RingNorth[3] = { 4.f, 6.f, 8.f };
		const int32 RingJitter[3] = { 2500, 6000, 6000 };
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float R0 = RingRadius[Ring];
			const float Step = RingStep[Ring];
			for (float A = 0.f; A < 360.f; A += Step)
			{
				const uint32 Hh = Hash(CitySeed++, 5151);
				const float Ang = FMath::DegreesToRadians(A + (Hh % 100) * 0.02f);
				const float Rad = R0 + (Hh >> 7) % RingJitter[Ring];
				const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
				const FVector C(Dir.X * Rad * 1.05f, Dir.Y * Rad, 0.f);
				const float North = FMath::Max(Dir.Y, 0.f);
				const int32 MaxFloors = FMath::RoundToInt(FMath::Lerp(RingFloors[Ring], RingNorth[Ring], North));
				const int32 Floors = 4 + (int32)((Hh >> 12) % (uint32)FMath::Max(MaxFloors - 3, 1));
				const float Wd = (Ring < 3 ? 1400.f : 1800.f) + (Hh >> 16) % 1400;
				const float Dp = (Ring < 3 ? 1200.f : 1500.f) + (Hh >> 20) % 1000;
				const float Yaw = FMath::RadiansToDegrees(Ang) + (float)((int32)((Hh >> 3) % 3) - 1) * 4.f;
				CityBlock(C, Yaw, Wd, Dp, Floors, Hh, Ring < 2);
			}
		}
		// Beyond, out to ~1.9 km, the city goes on as a continuous grid of blocks (the gaps are its streets), each
		// district on its own slightly rotated street grid, with clusters of towers where the district noise is high.
		const float Cell = 5600.f;
		const float Outer = 190000.f;
		for (float GX = -Outer; GX <= Outer; GX += Cell)
		{
			for (float GY = -Outer; GY <= Outer; GY += Cell)
			{
				const float R = FMath::Sqrt(GX * GX + GY * GY);
				if (R < 50000.f || R > Outer)
				{
					continue;
				}
				const uint32 Hh = Hash(CitySeed++, 7717);
				if (Hh % 100 < 22)
				{
					continue;
				}
				const FVector2D Dir(GX / R, GY / R);
				const float District = FMath::PerlinNoise2D(FVector2D(GX, GY) / 60000.f) * 0.5f + 0.5f;
				const float GridYaw = FMath::RoundToFloat(FMath::PerlinNoise2D(FVector2D(GX, GY) / 90000.f + FVector2D(5.3f, 1.1f)) * 3.f) * 12.f;
				const FVector C(GX + ((int32)((Hh >> 7) % 1200) - 600), GY + ((int32)((Hh >> 11) % 1200) - 600), 0.f);
				const float North = FMath::Max(Dir.Y, 0.f);
				const float Tall = FMath::Lerp(8.f + 22.f * District * District, 9.f, North);
				const int32 Floors = 4 + (int32)((Hh >> 15) % (uint32)FMath::Max(FMath::RoundToInt(Tall) - 3, 1));
				const float Wd = 2000.f + (Hh >> 19) % 2400;
				const float Dp = 1600.f + (Hh >> 23) % 2000;
				CityBlock(C, GridYaw + ((Hh >> 3) % 2) * 90.f, Wd, Dp, Floors, Hh, false, R > 100000.f);
			}
		}
	}
	// Ground plain and the mountain ranges (陽明山-like to the north) out to ~12 km: one Nanite heightfield.
	bTerrain = true;
}

void FCampusSchoolBuildContext::CityBlock(const FVector& C, float Yaw, float Width, float Depth, int32 Floors, uint32 Seed, bool bSlabs, bool bFar)
{
	static const EPal Walls[] = { EPal::CityBeige, EPal::CityGray, EPal::CityPink, EPal::CityBlue, EPal::CityWhite, EPal::CityTile, EPal::CityGray, EPal::CityBeige };
	static const EPal FarWalls[] = { EPal::FarCityBeige, EPal::FarCityGray, EPal::FarCityPink, EPal::FarCityBlue, EPal::FarCityWhite, EPal::FarCityTile, EPal::FarCityGray, EPal::FarCityBeige };
	const EPal Wall = (bFar ? FarWalls : Walls)[(Seed >> 24) % UE_ARRAY_COUNT(Walls)];
	const float FH = 310.f;
	const float Hgt = Floors * FH;
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](float X, float Y, float Z) { return C + R.RotateVector(FVector(X, Y, Z)); };
	const uint32 Form = (Seed >> 18) % 5;
	if (Form >= 2)
	{
		auto Part = [&](float X0, float Y0, float X1, float Y1, float Z0, float Top)
		{
			const float Dx = X1 - X0;
			const float Dy = Y1 - Y0;
			Box(Wall, At((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, (Z0 + Top) * 0.5f), FVector(Dx, Dy, Top - Z0), false, Yaw);
			if (!bFar)
			{
				Box(EPal::Concrete, At((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, Top + 8.f), FVector(Dx + 10.f, Dy + 10.f, 16.f), false, Yaw);
				Box(Wall, At(X0 + Dx * 0.3f, Y0 + Dy * 0.3f, Top + 120.f), FVector(Dx * 0.2f, Dy * 0.2f, 240.f), false, Yaw);
			}
		};
		if (Form == 2)
		{
			Part(-Depth * 0.5f, -Width * 0.5f, -Depth * 0.1f, Width * 0.5f, 0.f, Hgt);
			Part(-Depth * 0.1f, -Width * 0.5f, Depth * 0.5f, -Width * 0.12f, 0.f, Hgt * 0.78f);
		}
		else if (Form == 3)
		{
			Part(-Depth * 0.5f, -Width * 0.5f, Depth * 0.5f, -Width * 0.13f, 0.f, Hgt);
			Part(-Depth * 0.4f, Width * 0.13f, Depth * 0.4f, Width * 0.5f, 0.f, Hgt * 0.66f);
		}
		else
		{
			Part(-Depth * 0.5f, -Width * 0.5f, Depth * 0.5f, Width * 0.5f, 0.f, Hgt * 0.58f);
			Part(-Depth * 0.42f, -Width * 0.36f, Depth * 0.12f, Width * 0.36f, Hgt * 0.58f, Hgt);
		}
		return;
	}
	Box(Wall, C + FVector(0.f, 0.f, Hgt * 0.5f), FVector(Depth, Width, Hgt), false, Yaw);
	if (bFar)
	{
		Box(Wall, At(Depth * 0.15f, -Width * 0.2f, Hgt + 160.f), FVector(Depth * 0.3f, Width * 0.25f, 320.f), false, Yaw);
		if ((Seed >> 10) % 2 == 0)
		{
			Cyl((Seed >> 28) % 2 ? EPal::TankBlue : EPal::Stainless, At(-Depth * 0.3f, -Width * 0.36f, Hgt), 200.f, 220.f, false);
		}
		return;
	}
	if (bSlabs)
	{
		for (int32 f = 1; f < Floors; ++f)
		{
			Box(EPal::Concrete, C + FVector(0.f, 0.f, f * FH + 10.f), FVector(Depth + 24.f, Width + 24.f, 20.f), false, Yaw);
		}
	}
	// Roof: parapet coping, the stair / lift house, water tanks, and on many walk-ups a tin-roofed rooftop addition (頂樓加蓋).
	Box(EPal::Concrete, C + FVector(0.f, 0.f, Hgt + 8.f), FVector(Depth + 10.f, Width + 10.f, 16.f), false, Yaw);
	Box(Wall, At(Depth * 0.15f, -Width * 0.2f, Hgt + 160.f), FVector(Depth * 0.3f, Width * 0.25f, 320.f), false, Yaw);
	if (Floors <= 12 && (Seed >> 6) % 3 != 0)
	{
		const EPal Tin = (Seed >> 8) % 2 ? EPal::RoofBlue : EPal::RoofGreen;
		Box(EPal::CityWhite, At(-Depth * 0.12f, Width * 0.12f, Hgt + 130.f), FVector(Depth * 0.55f, Width * 0.55f, 260.f), false, Yaw);
		BoxR(Tin, At(-Depth * 0.12f, Width * 0.12f, Hgt + 275.f), FVector(Depth * 0.62f, Width * 0.62f, 6.f), FRotator(4.f, Yaw, 0.f), false);
	}
	const int32 Tanks = 1 + (Seed >> 10) % 3;
	for (int32 t = 0; t < Tanks; ++t)
	{
		Cyl((Seed >> (28 - t)) % 2 ? EPal::TankBlue : EPal::Stainless, At(-Depth * 0.32f + t * 240.f, -Width * 0.36f, Hgt + 16.f), 190.f, 210.f, false);
	}
	if ((Seed >> 9) % 4 == 0)
	{
		Box(EPal::IronBars, At(0.f, 0.f, Hgt + 420.f), FVector(30.f, Width * 0.6f, 500.f), false, Yaw);
	}
	else if ((Seed >> 13) % 3 == 0)
	{
		Box(EPal::Stainless, At(Depth * 0.3f, Width * 0.3f, Hgt + 300.f), FVector(4.f, 4.f, 600.f), false, Yaw);
	}
}

// ------------------------------------------------------------------ lived-in details (imported props)

void FCampusSchoolBuildContext::BuildDetails()
{
	if (!Library)
	{
		return;
	}
	auto Variant = [](uint32 Seed, const TCHAR* A, const TCHAR* Bv) { return Seed % 2 ? A : Bv; };

	// AC units between the windows of the outer facades, every floor above the ground floor
	auto FacadeAC = [&](bool bAlongY, float Fixed, float A0, float A1, float Facing, int32 Floors, int32 Seed)
	{
		const int32 Count = FMath::Max(1, FMath::FloorToInt((A1 - A0) / 450.f));
		const float Step = (A1 - A0) / Count;
		for (int32 f = 1; f < Floors; ++f)
		{
			for (int32 i = 0; i + 1 < Count; ++i)
			{
				const uint32 Hh = Hash(Seed + i * 31, f);
				if (Hh % 3 == 0)
				{
					continue;
				}
				const float A = A0 + Step * (i + 1);
				const FVector P = bAlongY ? FVector(Fixed, A, F1 + f * H + 40.f) : FVector(A, Fixed, F1 + f * H + 40.f);
				const bool bRust = (Hh >> 4) % 2 == 0;
				PropOnWall(TEXT("exterior_aircon_unit"), P, Facing, 0.f, false, bRust ? TEXT("rusted") : nullptr, bRust ? nullptr : TEXT("rusted"));
			}
		}
	};
	FacadeAC(true, WW_X0, WW_Y0 + 100.f, WW_Y1 - 100.f, 180.f, 4, 101);
	FacadeAC(false, NW_Y1, NW_X0 + 100.f, NW_X1 - 100.f, 90.f, 4, 211);
	FacadeAC(false, AD_Y0, AD_X0 + 100.f, AD_X1 - 100.f, -90.f, 3, 307);
	// condensers on the ground of the west alley (cover along the flank route)
	for (float Y : { -2300.f, -1250.f, 1000.f, 2400.f })
	{
		PropOnWall(TEXT("exterior_aircon_unit"), FVector(WW_X0, Y, 0.f), 180.f, 0.f, true, Variant(Hash((int32)Y, 5), TEXT("rusted"), nullptr), Variant(Hash((int32)Y, 5), nullptr, TEXT("rusted")));
	}

	// corridors: extinguishers, fire alarms, potted plants by the columns
	for (float Floor : { F1, F2 })
	{
		// on the wall between classroom doors, never in a doorway
		for (float Y : { -2500.f, -400.f, 1900.f })
		{
			Prop(TEXT("korean_fire_extinguisher_01"), FVector(WW_XC + 30.f, Y, Floor), 0.f);
			PropOnWall(TEXT("fire_alarm"), FVector(WW_XC + 10.f, Y + 70.f, Floor + 140.f), 0.f, 0.f, false);
		}
		for (float X : { -2700.f, -500.f, 700.f })
		{
			Prop(TEXT("korean_fire_extinguisher_01"), FVector(X, NW_YC - 30.f, Floor), -90.f);
			PropOnWall(TEXT("fire_alarm"), FVector(X + 70.f, NW_YC - 10.f, Floor + 140.f), -90.f, 0.f, false);
		}
	}
	int32 PlantIndex = 0;
	for (float Y = WW_Y0 + 225.f; Y < WW_Y1; Y += 900.f)
	{
		const TCHAR* Variants[] = { TEXT("_a"), TEXT("_b"), TEXT("_c"), TEXT("_d") };
		if (!Prop(TEXT("pachira_aquatica_01"), FVector(WW_X1 - 75.f, Y + 75.f, F1), (float)(Hash((int32)Y, 9) % 360), 150.f, true, Variants[PlantIndex++ % 4]))
		{
			break;
		}
	}
	for (float X = WW_X1 + 225.f; X < NW_X1; X += 1350.f)
	{
		Prop(PlantIndex++ % 2 ? FName(TEXT("potted_plant_01")) : FName(TEXT("potted_plant_02")), FVector(X + 75.f, NW_Y0 + 75.f, F1), (float)(Hash((int32)X, 3) % 360));
	}
	// washroom corner
	Prop(TEXT("WetFloorSign_01"), FVector(WW_XC + 110.f, 60.f, F1), 30.f);
	Prop(TEXT("metal_trash_can"), FVector(WW_XC + 40.f, 330.f, F1), 0.f, 0.f, true, nullptr, TEXT("rust"));

	// security cameras on the wings, wall clock in the central passage
	PropOnWall(TEXT("security_camera_01"), FVector(WW_X1, WW_Y0 + 180.f, F1 + 300.f), 0.f, 0.f, false);
	PropOnWall(TEXT("security_camera_01"), FVector(NW_X0 + 900.f, NW_Y0, F1 + 300.f), -90.f, 0.f, false);
	PropOnWall(TEXT("security_camera_01"), FVector(PS_X1 + 30.f, AD_Y1, F1 + 300.f), 90.f, 0.f, false);
	PropOnWall(TEXT("wall_clock"), FVector(PS_X0 + 12.f, -3700.f, F1 + 270.f), 0.f, 0.f, false);

	// west alley clutter
	Prop(TEXT("cardboard_box_01"), FVector(-4700.f, 1120.f, 0.f), 12.f);
	Prop(TEXT("cardboard_box_01"), FVector(-4705.f, 1125.f, 34.f), -20.f);
	Prop(TEXT("cardboard_box_01"), FVector(-4640.f, 1060.f, 0.f), 80.f);
	Prop(TEXT("trashbag"), FVector(-4650.f, 1450.f, 0.f), 40.f);
	Prop(TEXT("trashbag"), FVector(-4705.f, 1500.f, 0.f), 160.f);
	Prop(TEXT("trashbag"), FVector(-4600.f, 1520.f, 0.f), 250.f);
	for (int32 i = 0; i < 4; ++i)
	{
		Prop(TEXT("plastic_crate_01"), FVector(-4720.f, -1000.f + (i % 2) * 42.f, (i / 2) * 26.5f), 90.f);
	}
	Prop(TEXT("Barrel_01"), FVector(-4690.f, 2150.f, 0.f), 0.f);
	Prop(TEXT("Barrel_01"), FVector(-4630.f, 2215.f, 0.f), 40.f);
	Prop(TEXT("old_tyre"), FVector(-4740.f, -2150.f, 0.f), 10.f);
	Prop(TEXT("old_tyre"), FVector(-4735.f, -2080.f, 0.f), 25.f);
	PropOnWall(TEXT("utility_box_02"), FVector(-4785.f, 600.f, 0.f), 0.f);
	PropOnWall(TEXT("utility_box_01"), FVector(-4785.f, -2700.f, 0.f), 0.f);

	// east service alley behind the admin building
	Prop(TEXT("wooden_military_crate"), FVector(4420.f, -4000.f, 0.f), 90.f);
	Prop(TEXT("wooden_military_crate"), FVector(4420.f, -4000.f, 46.f), 85.f);
	Prop(TEXT("wooden_crate_01"), FVector(4430.f, -3720.f, 0.f), 95.f);
	Prop(TEXT("cardboard_box_01"), FVector(4300.f, -3560.f, 0.f), 30.f);
	Prop(TEXT("cement_bag"), FVector(4600.f, -3520.f, 0.f), 10.f);
	Prop(TEXT("cement_bag"), FVector(4605.f, -3520.f, 18.f), 25.f);

	// school store frontage: drink crates, gas cylinders, trolley, payphone
	const float StoreX = 3700.f;
	for (int32 i = 0; i < 6; ++i)
	{
		Prop(TEXT("plastic_crate_03"), FVector(StoreX - 45.f, 2560.f + (i % 3) * 50.f, (i / 3) * 26.6f), 180.f);
	}
	Prop(TEXT("propane_tank"), FVector(StoreX - 35.f, 3380.f, 0.f), 0.f);
	Prop(TEXT("propane_tank"), FVector(StoreX - 70.f, 3420.f, 0.f), 60.f);
	Prop(TEXT("hand_truck"), FVector(StoreX - 60.f, 2440.f, 0.f), 160.f);
	PropOnWall(TEXT("korean_public_payphone_01"), FVector(StoreX - 1.f, 3250.f, 110.f), 180.f, 0.f, false);
	Prop(TEXT("plastic_monobloc_chair_01"), FVector(StoreX - 160.f, 2900.f, 0.f), 200.f);
	Prop(TEXT("plastic_monobloc_chair_01"), FVector(StoreX - 150.f, 3010.f, 0.f), 160.f);

	// equipment shed
	Prop(TEXT("old_tyre"), FVector(4300.f, -380.f, 10.f), 90.f);
	Prop(TEXT("old_tyre"), FVector(4330.f, -380.f, 10.f), 95.f);
	Prop(TEXT("portable_generator"), FVector(4300.f, 380.f, 10.f), 180.f);
	Prop(TEXT("Barrel_01"), FVector(4620.f, -380.f, 10.f), 0.f);

	// plaza: barriers in front of the gate, hydrant and utility boxes along the wall
	Prop(TEXT("concrete_road_barrier"), FVector(PS_X0 + 80.f, -5250.f, 0.f), 90.f);
	Prop(TEXT("concrete_road_barrier_02"), FVector(PS_X1 - 80.f, -5250.f, 0.f), 95.f);
	Prop(TEXT("fire_hydrant"), FVector(PS_X0 - 260.f, -5540.f, 0.f), 90.f, 0.f, true, nullptr, TEXT("aged"));
	PropOnWall(TEXT("utility_box_02"), FVector(-2700.f, -5585.f, 0.f), 90.f);
	PropOnWall(TEXT("security_camera_01"), FVector(-2050.f, -5160.f, 260.f), 90.f, 0.f, false);
	for (float X : { -3300.f, 1200.f })
	{
		Prop(TEXT("water_manhole_cover"), FVector(X, -4550.f, -2.f), 0.f, 0.f, false);
	}

	// planting: clipped shrub hedges over the garden hedge cores, a grass-clump lawn in the gardens, shrub beds on the
	// plaza and rough grass along the field fence
	static const TCHAR* const Shrubs[] = { TEXT("_a"), TEXT("_b"), TEXT("_c"), TEXT("_d") };
	static const TCHAR* const GrassTall[] = { TEXT("mid_a_LOD0"), TEXT("mid_b_LOD0"), TEXT("mid_c_LOD0"), TEXT("large_a_LOD0"), TEXT("large_b_LOD0"), TEXT("large_c_LOD0"), TEXT("tall_a_LOD0"), TEXT("tall_b_LOD0") };
	static const TCHAR* const GrassShort[] = { TEXT("tiny_a_LOD0"), TEXT("tiny_b_LOD0"), TEXT("tiny_c_LOD0"), TEXT("tiny_d_LOD0"), TEXT("small_a_LOD0"), TEXT("small_b_LOD0") };
	static const TCHAR* const Periwinkle[] = { TEXT("03_LOD0"), TEXT("04_LOD0"), TEXT("05_LOD0"), TEXT("06_LOD0") };
	static const TCHAR* const Ferns[] = { TEXT("_a"), TEXT("_b"), TEXT("_c"), TEXT("_d") };
	static const TCHAR* const Calathea[] = { TEXT("_a"), TEXT("_b"), TEXT("_c"), TEXT("_d"), TEXT("_e") };
	int32 Seed = 1;
	for (float GX : { -2200.f, 450.f })
	{
		const float X0 = GX - 500.f;
		const float X1 = GX + 500.f;
		const float Y0 = -1300.f;
		const float Y1 = -200.f;
		for (const FBox2D& Lawn : { FBox2D(FVector2D(X0 + 34.f, Y0 + 126.f), FVector2D(X1 - 34.f, Y1 - 126.f)), FBox2D(FVector2D(X0 + 406.f, Y1 - 126.f), FVector2D(X1 - 34.f, Y1 - 34.f)) })
		{
			// Turf a week after mowing: a dense cover of short tufts of uneven height (not a carpet), clumps that have
			// grown up through it here and there, and the taller grass where the mower misses, along the kerbs.
			static const TCHAR* const GrassMid[] = { TEXT("small_a_LOD0"), TEXT("small_b_LOD0"), TEXT("mid_a_LOD0"), TEXT("mid_b_LOD0") };
			if (Turf(Lawn, 45.f, Seed++))
			{
				// A few coarser clumps (clover, a weed) the mower went over.
				Plant(TEXT("grass_medium_01"), GrassMid, Lawn, 45.f, 90.f, 0.45f, 0.7f, Seed++);
			}
			else
			{
				Plant(TEXT("grass_medium_01"), GrassShort, Lawn, 45.f, 13.f, 0.8f, 1.3f, Seed++);
				Plant(TEXT("grass_medium_01"), GrassMid, Lawn, 45.f, 38.f, 0.55f, 0.85f, Seed++);
			}
			for (const FBox2D& Edge : { FBox2D(Lawn.Min, FVector2D(Lawn.Max.X, Lawn.Min.Y + 30.f)), FBox2D(FVector2D(Lawn.Min.X, Lawn.Max.Y - 30.f), Lawn.Max),
				FBox2D(Lawn.Min, FVector2D(Lawn.Min.X + 30.f, Lawn.Max.Y)), FBox2D(FVector2D(Lawn.Max.X - 30.f, Lawn.Min.Y), Lawn.Max) })
			{
				Plant(TEXT("grass_medium_01"), GrassTall, Edge, 45.f, 70.f, 0.6f, 0.85f, Seed++);
			}
		}
	}
	for (const FBox2D& Bed : { FBox2D(FVector2D(-3800.f, -5000.f), FVector2D(-2000.f, -4650.f)), FBox2D(FVector2D(400.f, -5000.f), FVector2D(2600.f, -4650.f)), FBox2D(FVector2D(-3700.f, -4430.f), FVector2D(-1700.f, -4260.f)), FBox2D(FVector2D(100.f, -4430.f), FVector2D(3600.f, -4260.f)) })
	{
		const FBox2D Soil(Bed.Min + FVector2D(18.f, 18.f), Bed.Max - FVector2D(18.f, 18.f));
		const FBox2D Inner(Bed.Min + FVector2D(40.f, 40.f), Bed.Max - FVector2D(40.f, 40.f));
		const bool bDeep = Bed.GetSize().Y > 250.f;
		// Rows of broad-leaved calathea with ferns between them along the bed (the dense shrub_01 row mesh shades itself
		// almost black). The deep beds keep their middle for the trees.
		for (const float RowY : bDeep ? TArray<float>{ 0.18f, 0.82f } : TArray<float>{ 0.5f })
		{
			const float Y = FMath::Lerp(Inner.Min.Y, Inner.Max.Y, RowY);
			const FBox2D Row(FVector2D(Inner.Min.X, Y - 20.f), FVector2D(Inner.Max.X, Y + 20.f));
			Plant(TEXT("calathea_orbifolia_01"), Calathea, Row, 62.f, 62.f, 1.05f, 1.4f, Seed++, false, 30.f);
			Plant(TEXT("fern_02"), Ferns, FBox2D(Row.Min + FVector2D(31.f, 0.f), Row.Max), 62.f, 124.f, 0.8f, 1.1f, Seed++, false, 30.f);
		}
		// periwinkle (日日春) edging both long sides, ferns and calathea between the trees, short grass over all the soil
		for (const bool bFar : { false, true })
		{
			const FBox2D Edge(FVector2D(Soil.Min.X, bFar ? Soil.Max.Y - 26.f : Soil.Min.Y), FVector2D(Soil.Max.X, bFar ? Soil.Max.Y : Soil.Min.Y + 26.f));
			Plant(TEXT("periwinkle_plant"), Periwinkle, Edge, 62.f, 22.f, 0.95f, 1.3f, Seed++, false, 14.f);
		}
		if (bDeep)
		{
			const FBox2D Middle(FVector2D(Soil.Min.X + 30.f, Bed.GetCenter().Y - 55.f), FVector2D(Soil.Max.X - 30.f, Bed.GetCenter().Y + 55.f));
			Plant(TEXT("fern_02"), Ferns, Middle, 62.f, 120.f, 0.85f, 1.25f, Seed++, false, 20.f);
			Plant(TEXT("calathea_orbifolia_01"), Calathea, FBox2D(Middle.Min + FVector2D(60.f, 0.f), Middle.Max), 62.f, 240.f, 0.9f, 1.2f, Seed++, false, 20.f);
		}
		else
		{
			Plant(TEXT("shrub_03"), Shrubs, FBox2D(FVector2D(Soil.Min.X, Bed.GetCenter().Y - 30.f), FVector2D(Soil.Max.X, Bed.GetCenter().Y + 30.f)), 62.f, 95.f, 1.1f, 1.5f, Seed++, false, 20.f);
		}
		Plant(TEXT("grass_medium_01"), GrassShort, Soil, 62.f, 11.f, 1.1f, 1.6f, Seed++, false, 10.f);
		static const TCHAR* const GrassClump[] = { TEXT("small_a_LOD0"), TEXT("small_b_LOD0"), TEXT("mid_b_LOD0") };
		Plant(TEXT("grass_medium_01"), GrassClump, Soil, 62.f, 30.f, 0.8f, 1.1f, Seed++, false, 10.f);
		Plant(TEXT("grass_medium_01"), GrassTall, Soil, 62.f, 75.f, 0.6f, 0.9f, Seed++);
	}
	// rough grass inside the field fence
	const FBox2D FieldEdge(FVector2D(-4780.f, 5140.f), FVector2D(4780.f, 5590.f));
	Plant(TEXT("grass_medium_01"), GrassTall, FieldEdge, 2.f, 70.f, 0.8f, 1.2f, Seed++);
	Plant(TEXT("grass_medium_01"), GrassShort, FieldEdge, 2.f, 55.f, 0.9f, 1.3f, Seed++);
}

// ------------------------------------------------------------------ signage

void FCampusSchoolBuildContext::BuildSigns()
{
	const FLinearColor Maroon(0.19f, 0.022f, 0.018f);
	const FLinearColor Gold(0.95f, 0.73f, 0.32f);
	const FLinearColor White(0.93f, 0.93f, 0.91f);
	const FLinearColor Ink(0.035f, 0.035f, 0.04f);
	const FLinearColor Green(0.025f, 0.15f, 0.085f);
	const FLinearColor Blue(0.035f, 0.12f, 0.34f);
	const FLinearColor Red(0.6f, 0.035f, 0.03f);
	const FLinearColor Stone(0.44f, 0.43f, 0.40f);
	const float AdminTop = F1 + 3.f * H;
	const float WingTop = F1 + 4.f * H;

	// School name over the main gate side of the admin building, and the stone plaque on the gate pillar
	Sign(FVector(CX, AD_Y0 - 2.f, AdminTop + 78.f), -90.f, 1500.f, 92.f, TEXT("新北市立光華國民中學"), Maroon, Gold, 0, false, 0.f, 4.f);
	Sign(FVector(PS_X0 - 130.f, -5643.f, 165.f), -90.f, 50.f, 250.f, TEXT("光華國民中學"), Stone, Ink, 0, true);
	// Motto above the passage (courtyard side) and over the field exit
	Sign(FVector(CX, AD_Y1 + 8.f, F2 + 105.f), 90.f, 700.f, 124.f, TEXT("誠正勤樸"), Maroon, Gold, 0);
	Sign(FVector(CX, NW_Y1 + 12.f, Ceil1 + 85.f), 90.f, 680.f, 110.f, TEXT("光華國中"), Maroon, Gold, 0);
	Sign(FVector(CX, 4190.f + 27.f, 255.f), 90.f, 520.f, 92.f, TEXT("敦品勵學"), Maroon, Gold, 0);
	// Building names on the courtyard parapets
	Sign(FVector(WW_X1 + 3.f, -2300.f, WingTop + 78.f), 0.f, 460.f, 96.f, TEXT("勤學樓"), Green, White, 0);
	Sign(FVector(-2500.f, NW_Y0 - 3.f, WingTop + 78.f), -90.f, 460.f, 96.f, TEXT("力行樓"), Green, White, 0);
	Sign(FVector(2200.f, AD_Y1 + 3.f, AdminTop + 78.f), 90.f, 520.f, 96.f, TEXT("行政大樓"), Green, White, 0);
	// School store and the sports-day banner on the admin balcony
	Sign(FVector(3700.f - 1.f, 3050.f, 300.f), 180.f, 520.f, 72.f, TEXT("福利社"), Red, White, 1, false, 0.5f);
	Sign(FVector(2300.f, AD_Y1 + 0.6f, F1 + 2.f * H + 50.f), 90.f, 1300.f, 84.f, TEXT("賀！本校榮獲新北市中小學聯合運動會 團體總冠軍"), Red, FLinearColor(1.f, 0.88f, 0.5f), 1, false, 0.f, 1.f);
	// Class plates hung across the west wing corridor so they read down its length
	const float RoomStarts[] = { -2750.f, -1850.f, -950.f, 400.f, 1300.f, 2200.f };
	const TCHAR* Numerals[] = { TEXT("一"), TEXT("二"), TEXT("三"), TEXT("四"), TEXT("五"), TEXT("六") };
	// A class plate sticks out square from the wall over the door on a steel bracket screwed into it: the plate hangs
	// from the bracket's arm, so it is fixed at the wall and along its top.
	auto ClassPlate = [&](const FVector2D& WallPoint, const FVector2D& Out, float Z, const TCHAR* Name)
	{
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Out.Y, Out.X)) + 90.f;
		const FVector2D C = WallPoint + Out * 36.f;
		Sign(FVector(C.X, C.Y, Z), Yaw, 66.f, 24.f, Name, Blue, White, 2, false, 0.f, 2.f);
		const FVector2D A0 = WallPoint - Out * 4.f;
		const FVector2D A1 = WallPoint + Out * 70.f;
		Deco(EPal::SteelDark, FVector(FMath::Min(A0.X, A1.X) - (Out.X == 0.f ? 1.5f : 0.f), FMath::Min(A0.Y, A1.Y) - (Out.Y == 0.f ? 1.5f : 0.f), Z + 12.f),
			FVector(FMath::Max(A0.X, A1.X) + (Out.X == 0.f ? 1.5f : 0.f), FMath::Max(A0.Y, A1.Y) + (Out.Y == 0.f ? 1.5f : 0.f), Z + 15.f));
		const FVector2D Plate = WallPoint - Out * 0.5f;
		Deco(EPal::SteelDark, FVector(Plate.X - (Out.X == 0.f ? 6.f : 0.5f), Plate.Y - (Out.Y == 0.f ? 6.f : 0.5f), Z - 2.f), FVector(Plate.X + (Out.X == 0.f ? 6.f : 0.5f), Plate.Y + (Out.Y == 0.f ? 6.f : 0.5f), Z + 18.f));
	};
	for (int32 f = 0; f < 2; ++f)
	{
		// 1F: the corridor wall's face; 2F: the face of the classroom mass above it.
		const float Face = f == 0 ? WW_XC + 10.f : WW_XC;
		for (int32 i = 0; i < 6; ++i)
		{
			const FString Name = FString::Printf(TEXT("%s年%s班"), f == 0 ? TEXT("七") : TEXT("九"), Numerals[i]);
			ClassPlate(FVector2D(Face, RoomStarts[i] + 125.f), FVector2D(1.f, 0.f), (f == 0 ? F1 : F2) + 262.f, *Name);
		}
	}
	const float NorthRooms[] = { -3305.f, -2125.f, -350.f };
	const TCHAR* NorthNames[] = { TEXT("理化實驗室"), TEXT("八年一班"), TEXT("圖書室") };
	for (int32 i = 0; i < 3; ++i)
	{
		ClassPlate(FVector2D(NorthRooms[i], NW_YC - 10.f), FVector2D(0.f, -1.f), F1 + 262.f, NorthNames[i]);
	}
	// Exit signs at the stairs (glowing green). Over the open stair well there is no wall: they hang from the ceiling.
	// An exit sign is a dim lightbox, ~200 nits: readable in a dark stairwell, it does not light the ceiling round it.
	const FLinearColor ExitGreen(0.02f, 0.42f, 0.16f);
	auto HungExit = [&](const FVector& Center, float Yaw, float Ceiling)
	{
		Sign(Center, Yaw, 64.f, 22.f, TEXT("安全門  EXIT"), ExitGreen, White, 2, false, 0.5f, 4.f);
		const FVector Along = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		for (const float S : { -1.f, 1.f })
		{
			const FVector R = Center + Along * (S * 24.f);
			Cyl(EPal::Stainless, FVector(R.X, R.Y, Center.Z + 10.f), 1.2f, Ceiling - Center.Z - 10.f, false);
		}
		Box(EPal::Stainless, FVector(Center.X, Center.Y, Ceiling - 1.f), FVector(Yaw == 0.f || Yaw == 180.f ? 6.f : 60.f, Yaw == 0.f || Yaw == 180.f ? 60.f : 6.f, 2.f), false);
	};
	HungExit(FVector(WW_XC + 60.f, WW_Y0 + 300.f, Ceil1 - 35.f), 0.f, Ceil1);
	HungExit(FVector(WW_XC + 60.f, WW_Y0 + 300.f, F2 + H - Slab - 35.f), 0.f, F2 + H - Slab);
	HungExit(FVector(1175.f, NW_YC - 60.f, Ceil1 - 35.f), 90.f, Ceil1);
	// Corridor posters pinned on the west wing bulletin boards
	Sign(FVector(WW_XC + 13.4f, -1850.f, F1 + 155.f), 0.f, 90.f, 70.f, TEXT("校園安全宣導"), FLinearColor(0.85f, 0.82f, 0.7f), FLinearColor(0.5f, 0.05f, 0.04f), 1, false, 0.f, 0.6f);
	Sign(FVector(WW_XC + 13.4f, -950.f, F1 + 155.f), 0.f, 70.f, 90.f, TEXT("防災演練"), FLinearColor(0.95f, 0.75f, 0.2f), Ink, 1, true, 0.f, 0.6f);
}

// ------------------------------------------------------------------ metadata

void FCampusSchoolBuildContext::Rect(float X0, float Y0, float X1, float Y1, uint8 Kind)
{
	FCampusMapRect& R = B->MinimapRects.AddDefaulted_GetRef();
	R.Min = FVector2D(FMath::Min(X0, X1), FMath::Min(Y0, Y1));
	R.Max = FVector2D(FMath::Max(X0, X1), FMath::Max(Y0, Y1));
	R.Kind = Kind;
}

void FCampusSchoolBuildContext::Spawn(const FVector& Location, float Yaw, ECampusTeam Team, bool bGunfight)
{
	FCampusSpawnPoint& SP = B->SpawnPoints.AddDefaulted_GetRef();
	SP.Transform = FTransform(FRotator(0.f, Yaw, 0.f), Location);
	SP.Team = Team;
	SP.bGunfight = bGunfight;
}

void FCampusSchoolBuildContext::BuildMetadata()
{
	B->PlayableBounds = FBox(FVector(-4800.f, -5600.f, -100.f), FVector(4800.f, 5600.f, 1600.f));

	// TDM base spawns. Alpha's row runs in the lane between the two rows of plaza flower beds, clear of both.
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = -3500.f + i * 950.f;
		Spawn(FVector(X, -4540.f + (i % 2) * 40.f - 20.f, 110.f), 90.f, ECampusTeam::Alpha, false);
		Spawn(FVector(X, 4950.f + (i % 2) * 120.f, 110.f), -90.f, ECampusTeam::Bravo, false);
	}
	// flex spawns
	Spawn(FVector(-4500.f, -3400.f, 110.f), 90.f, ECampusTeam::None, false);
	Spawn(FVector(-4500.f, 3700.f, 110.f), -90.f, ECampusTeam::None, false);
	Spawn(FVector(4450.f, -3900.f, 110.f), 90.f, ECampusTeam::None, false);
	Spawn(FVector(4300.f, 3900.f, 110.f), -90.f, ECampusTeam::None, false);
	Spawn(FVector(2600.f, 3850.f, 110.f), -90.f, ECampusTeam::None, false);
	Spawn(FVector(2000.f, -3700.f, 110.f), 90.f, ECampusTeam::None, false);

	const FVector Spots[] = {
		FVector(CX, -1800.f, 0.f), FVector(CX, 0.f, 0.f), FVector(CX, 1300.f, 0.f), FVector(-2200.f, 600.f, 0.f), FVector(400.f, 600.f, 0.f),
		FVector(-2200.f, -2500.f, 0.f), FVector(400.f, -2500.f, 0.f), FVector(CX, 2600.f, 0.f),
		FVector(-3290.f, -2400.f, F1), FVector(-3290.f, -500.f, F1), FVector(-3290.f, 1800.f, F1), FVector(-3815.f, -1400.f, F1), FVector(-3815.f, 1750.f, F1),
		FVector(-3290.f, -1500.f, F2), FVector(-3290.f, 1500.f, F2), FVector(-1800.f, 3240.f, F2), FVector(300.f, 3240.f, F2),
		FVector(-2200.f, 3240.f, F1), FVector(200.f, 3240.f, F1), FVector(250.f, 3800.f, F1), FVector(-2850.f, 3800.f, F1),
		FVector(CX, -3700.f, F1), FVector(-2500.f, -3340.f, F1), FVector(1500.f, -3340.f, F1), FVector(-1700.f, -3850.f, F1), FVector(100.f, -3850.f, F1),
		FVector(2000.f, -2000.f, 0.f), FVector(3150.f, -1500.f, 0.f), FVector(3150.f, 1500.f, 0.f), FVector(4400.f, 0.f, 0.f), FVector(4200.f, 3000.f, 0.f), FVector(2600.f, 3800.f, 0.f),
		FVector(CX, 4900.f, 0.f), FVector(-3000.f, 4800.f, 0.f), FVector(2500.f, 4900.f, 0.f),
		FVector(-4500.f, -3000.f, 0.f), FVector(-4500.f, 0.f, 0.f), FVector(-4500.f, 2800.f, 0.f),
		FVector(-3000.f, -4900.f, 0.f), FVector(1500.f, -4900.f, 0.f)
	};
	for (const FVector& S : Spots)
	{
		B->Hotspots.Add(S);
	}

	FCampusMenuAnchors& M = B->MenuAnchors;
	// Out on the open courtyard paving in front of the west wing: nothing stands between the menu camera and the
	// operator, and the corridor colonnade behind him defocuses into the backdrop.
	M.Operator = FTransform(FRotator(0.f, 10.f, 0.f), FVector(-2780.f, -1850.f, 0.f));
	M.WeaponDesk = FTransform(FRotator(0.f, 0.f, 0.f), FVector(-3815.f, -1645.f, F1 + 20.f + 79.f));
	M.CourtyardFocus = FVector(CX, -600.f, 250.f);
	M.GateFocus = FVector(CX, -3700.f, 180.f);
}

// ------------------------------------------------------------------ audit

void FCampusSchoolBuildContext::AuditGeometry() const
{
	const FBox Region = B->PlayableBounds.IsValid ? B->PlayableBounds.ExpandBy(FVector(1500.f, 1500.f, 300.f)) : FBox(FVector(-8000.f), FVector(8000.f));
	auto PalName = [](EPal P) { return P == EPal::Count ? FString(TEXT("SIGN")) : ((int32)P > (int32)EPal::Count ? FString(TEXT("PROP")) : FString::Printf(TEXT("pal%d"), (int32)P)); };
	auto Small = [](const FBox& Bx) { return Bx.GetSize().GetMax() < 20000.f; };
	TArray<int32> Ids;
	for (int32 i = 0; i < AuditBoxes.Num(); ++i)
	{
		if (AuditBoxes[i].Box.Intersect(Region) && Small(AuditBoxes[i].Box))
		{
			Ids.Add(i);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusAudit] %d pieces in the play area (%d total), %d openings"), Ids.Num(), AuditBoxes.Num(), AuditOpenings.Num());

	// ---- flicker: same-facing faces in one plane (within 0.3 cm), overlapping, of different surfaces.
	struct FFace { int32 Id; FBox2D Rect; double C; };
	TMap<int64, TArray<FFace>> Planes;
	for (const int32 i : Ids)
	{
		const FAuditBox& A = AuditBoxes[i];
		if (!A.bAxis)
		{
			continue;
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 U = (Axis + 1) % 3;
			const int32 V = (Axis + 2) % 3;
			const FBox2D Rect(FVector2D(A.Box.Min[U], A.Box.Min[V]), FVector2D(A.Box.Max[U], A.Box.Max[V]));
			for (int32 Dir = 0; Dir < 2; ++Dir)
			{
				const double C = Dir ? A.Box.Max[Axis] : A.Box.Min[Axis];
				const int64 Key = ((int64)FMath::RoundToInt(C / 0.3) << 3) | (Axis << 1) | Dir;
				Planes.FindOrAdd(Key).Add({ i, Rect, C });
			}
		}
	}
	struct FHit { float Area; int32 A; int32 Bx; FVector At; };
	TArray<FHit> Flicker;
	for (TPair<int64, TArray<FFace>>& P : Planes)
	{
		TArray<FFace>& Faces = P.Value;
		// Neighbouring buckets are the same plane within tolerance too.
		for (const int64 Adj : { P.Key + 8 })
		{
			if (const TArray<FFace>* Next = Planes.Find(Adj))
			{
				Faces.Append(*Next);
			}
		}
	}
	TSet<uint64> Seen;
	for (const TPair<int64, TArray<FFace>>& P : Planes)
	{
		TArray<FFace> Faces = P.Value;
		Faces.Sort([](const FFace& L, const FFace& R) { return L.Rect.Min.X < R.Rect.Min.X; });
		for (int32 a = 0; a < Faces.Num(); ++a)
		{
			for (int32 b = a + 1; b < Faces.Num() && Faces[b].Rect.Min.X < Faces[a].Rect.Max.X; ++b)
			{
				const FAuditBox& BA = AuditBoxes[Faces[a].Id];
				const FAuditBox& BB = AuditBoxes[Faces[b].Id];
				if (Faces[a].Id == Faces[b].Id || BA.Pal == BB.Pal || FMath::Abs(Faces[a].C - Faces[b].C) > 0.002)
				{
					continue;
				}
				const FBox2D& RA = Faces[a].Rect;
				const FBox2D& RB = Faces[b].Rect;
				const double W = FMath::Min(RA.Max.X, RB.Max.X) - FMath::Max(RA.Min.X, RB.Min.X);
				const double Hh = FMath::Min(RA.Max.Y, RB.Max.Y) - FMath::Max(RA.Min.Y, RB.Min.Y);
				if (W < 3.0 || Hh < 3.0)
				{
					continue;
				}
				const uint64 PairKey = ((uint64)FMath::Min(Faces[a].Id, Faces[b].Id) << 32) | (uint64)FMath::Max(Faces[a].Id, Faces[b].Id);
				if (Seen.Contains(PairKey))
				{
					continue;
				}
				Seen.Add(PairKey);
				const FBox Overlap = BA.Box.Overlap(BB.Box.ExpandBy(0.3f));
				Flicker.Add({ (float)(W * Hh), Faces[a].Id, Faces[b].Id, Overlap.IsValid ? Overlap.GetCenter() : BA.Box.GetCenter() });
			}
		}
	}
	Flicker.Sort([](const FHit& L, const FHit& R) { return L.Area > R.Area; });
	TMap<FString, int32> FlickerKinds;
	for (const FHit& Hit : Flicker)
	{
		const EPal PA = AuditBoxes[Hit.A].Pal;
		const EPal PB = AuditBoxes[Hit.Bx].Pal;
		FlickerKinds.FindOrAdd(PalName(FMath::Min(PA, PB)) + TEXT("/") + PalName(FMath::Max(PA, PB)))++;
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusAudit] flicker: %d coplanar overlaps"), Flicker.Num());
	for (const TPair<FString, int32>& K : FlickerKinds)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusAudit]   flicker kind %s x%d"), *K.Key, K.Value);
	}
	for (int32 i = 0; i < FMath::Min(Flicker.Num(), 80); ++i)
	{
		const FHit& Hit = Flicker[i];
		UE_LOG(LogTemp, Display, TEXT("[CampusAudit]   flicker %s vs %s area %.0f at %s | %s | %s"), *PalName(AuditBoxes[Hit.A].Pal), *PalName(AuditBoxes[Hit.Bx].Pal), Hit.Area, *Hit.At.ToCompactString(),
			*AuditBoxes[Hit.A].Box.ToString(), *AuditBoxes[Hit.Bx].Box.ToString());
	}

	// ---- floating: a piece whose bounds (grown 2 cm) touch nothing else and that is off the ground.
	constexpr float Cell = 400.f;
	TMap<FIntVector, TArray<int32>> Grid;
	auto CellsOf = [&](const FBox& Bx, TFunctionRef<void(const FIntVector&)> Fn)
	{
		const FIntVector Lo(FMath::FloorToInt(Bx.Min.X / Cell), FMath::FloorToInt(Bx.Min.Y / Cell), FMath::FloorToInt(Bx.Min.Z / Cell));
		const FIntVector Hi(FMath::FloorToInt(Bx.Max.X / Cell), FMath::FloorToInt(Bx.Max.Y / Cell), FMath::FloorToInt(Bx.Max.Z / Cell));
		for (int32 X = Lo.X; X <= Hi.X; ++X) for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y) for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z) Fn(FIntVector(X, Y, Z));
	};
	TArray<int32> Big;
	for (int32 i = 0; i < AuditBoxes.Num(); ++i)
	{
		const FBox& Bx = AuditBoxes[i].Box;
		if (!Bx.ExpandBy(50.f).Intersect(Region))
		{
			continue;
		}
		if (!Small(Bx))
		{
			Big.Add(i);
			continue;
		}
		CellsOf(Bx, [&](const FIntVector& C) { Grid.FindOrAdd(C).Add(i); });
	}
	int32 Floating = 0;
	TMap<FString, int32> FloatKinds;
	for (const int32 i : Ids)
	{
		const FBox Grown = AuditBoxes[i].Box.ExpandBy(2.f);
		bool bTouch = false;
		CellsOf(Grown, [&](const FIntVector& C)
		{
			if (bTouch)
			{
				return;
			}
			if (const TArray<int32>* List = Grid.Find(C))
			{
				for (const int32 j : *List)
				{
					if (j != i && AuditBoxes[j].Box.Intersect(Grown))
					{
						bTouch = true;
						return;
					}
				}
			}
		});
		for (int32 k = 0; !bTouch && k < Big.Num(); ++k)
		{
			bTouch = AuditBoxes[Big[k]].Box.Intersect(Grown);
		}
		if (!bTouch)
		{
			++Floating;
			const FVector S = AuditBoxes[i].Box.GetSize();
			const FString Kind = FString::Printf(TEXT("%s shape %d size %.0fx%.0fx%.0f"), *PalName(AuditBoxes[i].Pal), AuditBoxes[i].Shape, S.X, S.Y, S.Z);
			if (!FloatKinds.Contains(Kind))
			{
				UE_LOG(LogTemp, Display, TEXT("[CampusAudit]   floating %s first at %s"), *Kind, *AuditBoxes[i].Box.GetCenter().ToCompactString());
			}
			FloatKinds.FindOrAdd(Kind)++;
		}
	}
	for (const TPair<FString, int32>& K : FloatKinds)
	{
		UE_LOG(LogTemp, Display, TEXT("[CampusAudit]   floating kind %s x%d"), *K.Key, K.Value);
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusAudit] floating: %d pieces touch nothing"), Floating);

	// ---- in the way: anything standing in a doorway or a window opening (frames and glazing excepted).
	int32 Blocked = 0;
	for (const FBox& O : AuditOpenings)
	{
		TSet<int32> Counted;
		CellsOf(O, [&](const FIntVector& C)
		{
			if (const TArray<int32>* List = Grid.Find(C))
			{
				for (const int32 j : *List)
				{
					if (Counted.Contains(j))
					{
						continue;
					}
					Counted.Add(j);
					const FAuditBox& A = AuditBoxes[j];
					if (A.Pal == WindowTrim || A.Pal == DoorTrim || A.Pal == EPal::GlassClear || A.Pal == EPal::WindowFrame || A.Pal == EPal::SteelDark)
					{
						continue;
					}
					const FBox Ov = A.Box.Overlap(O);
					if (Ov.IsValid && Ov.GetSize().GetMin() > 1.f)
					{
						++Blocked;
						if (Blocked <= 400)
						{
							UE_LOG(LogTemp, Display, TEXT("[CampusAudit]   in opening %s: %s at %s size %s"), *O.GetCenter().ToCompactString(), *PalName(A.Pal), *A.Box.GetCenter().ToCompactString(), *A.Box.GetSize().ToCompactString());
						}
					}
				}
			}
		});
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusAudit] openings: %d pieces stand in doorways or windows"), Blocked);
}

// ------------------------------------------------------------------ flush

void FCampusSchoolBuildContext::SeparateCoplanarFaces()
{
	// Outward offset of each face from its authored plane: Add's per-palette growth, plus what is added here.
	constexpr double Gap = 0.25;
	constexpr double Coplanar = 0.1;
	auto Growth = [](EPal P) { return 0.02 + 0.003 * (double)(int32)P; };
	const int32 Num = FlushBoxes.Num();
	TArray<double> Extra;
	Extra.SetNumZeroed(Num * 6);

	struct FFace { int32 Box; double Out; FBox2D Rect; };
	int32 Pushed = 0;
	double MaxExtra = 0.0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int32 U = (Axis + 1) % 3;
		const int32 V = (Axis + 2) % 3;
		for (int32 Dir = 0; Dir < 2; ++Dir)
		{
			// Faces bucketed by plane (1 mm cells); a face also looks in the cells either side of its own. Lower palettes
			// are settled first, so whatever a face has to clear has its final position.
			TArray<FFace> Faces;
			Faces.Reserve(Num);
			TMap<int64, TArray<int32>> Planes;
			for (int32 i = 0; i < Num; ++i)
			{
				const FBox& Bx = FlushBoxes[i].Box;
				const double Out = Dir ? Bx.Max[Axis] : -Bx.Min[Axis];
				Faces.Add({ i, Out, FBox2D(FVector2D(Bx.Min[U], Bx.Min[V]), FVector2D(Bx.Max[U], Bx.Max[V])) });
			}
			Faces.Sort([this](const FFace& L, const FFace& R) { return FlushBoxes[L.Box].Pal < FlushBoxes[R.Box].Pal; });
			for (int32 f = 0; f < Faces.Num(); ++f)
			{
				Planes.FindOrAdd(FMath::FloorToInt64(Faces[f].Out / Coplanar)).Add(f);
			}
			for (const FFace& H : Faces)
			{
				const EPal PH = FlushBoxes[H.Box].Pal;
				double& EH = Extra[H.Box * 6 + Axis * 2 + Dir];
				const int64 Key = FMath::FloorToInt64(H.Out / Coplanar);
				for (const int64 K : { Key - 1, Key, Key + 1 })
				{
					const TArray<int32>* Cell = Planes.Find(K);
					if (!Cell)
					{
						continue;
					}
					for (const int32 l : *Cell)
					{
						const FFace& L = Faces[l];
						const EPal PL = FlushBoxes[L.Box].Pal;
						if (PL >= PH)
						{
							// Cells are in palette order.
							break;
						}
						if (FMath::Abs(L.Out - H.Out) > Coplanar)
						{
							continue;
						}
						const double W = FMath::Min(L.Rect.Max.X, H.Rect.Max.X) - FMath::Max(L.Rect.Min.X, H.Rect.Min.X);
						const double Hh = FMath::Min(L.Rect.Max.Y, H.Rect.Max.Y) - FMath::Max(L.Rect.Min.Y, H.Rect.Min.Y);
						if (W <= 0.05 || Hh <= 0.05)
						{
							continue;
						}
						EH = FMath::Max(EH, (L.Out + Growth(PL) + Extra[L.Box * 6 + Axis * 2 + Dir] + Gap) - (H.Out + Growth(PH)));
					}
				}
				if (EH > 0.0)
				{
					++Pushed;
					MaxExtra = FMath::Max(MaxExtra, EH);
				}
			}
		}
	}

	for (int32 i = 0; i < Num; ++i)
	{
		const double* E = &Extra[i * 6];
		if (E[0] == 0.0 && E[1] == 0.0 && E[2] == 0.0 && E[3] == 0.0 && E[4] == 0.0 && E[5] == 0.0)
		{
			continue;
		}
		const FFlushBox& FB = FlushBoxes[i];
		FTransform& Drawn = Batches[FB.Batch].Transforms[FB.Instance];
		FVector Shift = FVector::ZeroVector;
		FVector Scale = Drawn.GetScale3D();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Lo = E[Axis * 2];
			const double Hi = E[Axis * 2 + 1];
			Shift[Axis] = (Hi - Lo) * 0.5;
			// The local axis lying along this world axis takes the extra length.
			FVector WorldAxis = FVector::ZeroVector;
			WorldAxis[Axis] = 1.0;
			const FVector Local = Drawn.GetRotation().UnrotateVector(WorldAxis);
			const int32 K = FMath::Abs(Local.X) > 0.5 ? 0 : FMath::Abs(Local.Y) > 0.5 ? 1 : 2;
			Scale[K] += FMath::Sign(Scale[K]) * (Lo + Hi) / 100.0;
		}
		Drawn.SetScale3D(Scale);
		Drawn.AddToTranslation(Shift);
	}
	UE_LOG(LogTemp, Display, TEXT("[CampusLayout] %d boxes, %d coplanar faces pushed clear (largest %.2f cm)"), Num, Pushed, MaxExtra);
}

void FCampusSchoolBuildContext::Flush()
{
	SeparateCoplanarFaces();
	int32 Total = 0;
	auto Apply = [&](UInstancedStaticMeshComponent* ISM, UStaticMesh* Mesh, const FBatch& Batch, UMaterialInterface* Material)
	{
		if (!ISM)
		{
			return;
		}
		ISM->ClearInstances();
		ISM->SetStaticMesh(Mesh);
		ISM->SetMaterial(0, Material);
		ISM->SetNumCustomDataFloats(ACampusSchoolBuilder::NumCustomFloats);
		if (Batch.Transforms.Num() == 0)
		{
			return;
		}
		ISM->AddInstances(Batch.Transforms, false, false, false);
		for (int32 i = 0; i < Batch.Transforms.Num(); ++i)
		{
			TArray<float> Data;
			Data.Append(&Batch.Custom[i * ACampusSchoolBuilder::NumCustomFloats], ACampusSchoolBuilder::NumCustomFloats);
			ISM->SetCustomData(i, Data, false);
		}
		ISM->MarkRenderStateDirty();
		// ClearInstances drops the component from the navigation octree and AddInstances (without per-instance
		// updates) never puts it back, which left the navmesh empty and the bots standing still.
		if (ISM->IsRegistered() && ISM->CanEverAffectNavigation())
		{
			FNavigationSystem::UpdateComponentData(*ISM);
		}
		Total += Batch.Transforms.Num();
	};

	for (int32 Shape = 0; Shape < ACampusSchoolBuilder::NumShapes; ++Shape)
	{
		UStaticMesh* Mesh = CampusAssets::Shape((ECampusShape)Shape);
		for (int32 Collide = 0; Collide < 2; ++Collide)
		{
			for (int32 Mat = 0; Mat < ACampusSchoolBuilder::NumMats; ++Mat)
			{
				const int32 Index = (Shape * 2 + Collide) * ACampusSchoolBuilder::NumMats + Mat;
				if (B->Batches.IsValidIndex(Index))
				{
					Apply(B->Batches[Index], Mesh, Batches[Index], CampusAssets::SurfaceMaterial((ECampusMat)Mat));
				}
			}
		}
	}
	UStaticMesh* Cube = CampusAssets::Shape(ECampusShape::Cube);
	UMaterialInterface* Plain = CampusAssets::SurfaceMaterial(ECampusMat::Plain);
	Apply(B->Blockers, Cube, Blockers, Plain);
	Apply(B->Ramps, Cube, Ramps, Plain);
	Apply(B->GunfightBlockers, Cube, GunfightBlockers, Plain);
	Apply(B->GunfightVisuals, Cube, GunfightVisuals, Plain);

	for (UInstancedStaticMeshComponent* Old : B->PropComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	B->PropComponents.Reset();
	ResolveProps();
	if (bAudit)
	{
		AuditGeometry();
	}
	int32 Props = 0;
	for (const FPropBatch& Batch : PropBatches)
	{
		if (!Batch.Mesh || Batch.Transforms.Num() == 0)
		{
			continue;
		}
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(B, NAME_None, RF_Transient);
		ISM->SetupAttachment(B->GetRootComponent());
		ISM->SetMobility(EComponentMobility::Static);
		ISM->SetStaticMesh(Batch.Mesh);
		ISM->bReceivesDecals = true;
		ISM->ComponentTags.Add(ACampusSchoolBuilder::SurfaceTag(Batch.Impact));
		if (Batch.bSolid)
		{
			ISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		}
		else
		{
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ISM->SetCanEverAffectNavigation(false);
		}
		if (Batch.bFoliage)
		{
			// Thousands of clumps: grass fades out past ~40 m, shrubs past ~90 m.
			ISM->SetCullDistances(Batch.bGrass ? 2800 : 7000, Batch.bGrass ? 4200 : 9000);
			// The wind's sway is only worked out where it can be seen moving.
			ISM->WorldPositionOffsetDisableDistance = 5000;
			ISM->bAffectDistanceFieldLighting = false;
			ISM->bReceivesDecals = false;
			// Wind-animated clumps would be rebuilt in the ray tracing scene every frame for no visible gain.
			ISM->bVisibleInRayTracing = false;
		}
		ISM->RegisterComponent();
		ISM->AddInstances(Batch.Transforms, false, false, false);
		if (Batch.bSolid)
		{
			FNavigationSystem::UpdateComponentData(*ISM);
		}
		B->PropComponents.Add(ISM);
		Props += Batch.Transforms.Num();
	}
	B->InstanceCount = Total + Props;
	FlushRuntime();
}

namespace
{
	UTextureRenderTarget2D* DrawSignTexture(UWorld* World, const FString& Text, float Width, float Height, const FLinearColor& Back, const FLinearColor& Ink, uint8 Font, bool bVertical, float PxPerCm)
	{
		const float Aspect = Width / FMath::Max(Height, 1.f);
		int32 TexH = FMath::Clamp(FMath::RoundToInt(Height * PxPerCm), 48, 512);
		const int32 TexW = FMath::Clamp(FMath::RoundToInt(TexH * Aspect), 64, 2048);
		TexH = FMath::Clamp(FMath::RoundToInt(TexW / Aspect), 32, 1024);
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(World, TexW, TexH, RTF_RGBA8_SRGB, Back, false);
		if (!RT)
		{
			return nullptr;
		}
		UCanvas* Canvas = nullptr;
		FVector2D Size(TexW, TexH);
		FDrawToRenderTargetContext Ctx;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RT, Canvas, Size, Ctx);
		if (Canvas)
		{
			FCanvasTileItem Bg(FVector2D::ZeroVector, Size, Back);
			Canvas->DrawItem(Bg);
			// Thin inset frame, as on enamel and engraved school boards (font 3: modern wayfinding, type only).
			const float Inset = FMath::Max(3.f, FMath::Min(Size.X, Size.Y) * 0.06f);
			const float Line = FMath::Max(1.5f, FMath::Min(Size.X, Size.Y) * 0.018f);
			const FLinearColor Frame = FMath::Lerp(Back, Ink, 0.55f);
			for (const FBox2D& R : { FBox2D(FVector2D(Inset, Inset), FVector2D(Size.X - Inset, Inset + Line)), FBox2D(FVector2D(Inset, Size.Y - Inset - Line), FVector2D(Size.X - Inset, Size.Y - Inset)),
				FBox2D(FVector2D(Inset, Inset), FVector2D(Inset + Line, Size.Y - Inset)), FBox2D(FVector2D(Size.X - Inset - Line, Inset), FVector2D(Size.X - Inset, Size.Y - Inset)) })
			{
				if (Font == 3)
				{
					break;
				}
				FCanvasTileItem Edge(R.Min, R.GetSize(), Frame);
				Canvas->DrawItem(Edge);
			}
			const CampusUI::EFont Family = Font == 0 ? CampusUI::EFont::Title : CampusUI::EFont::Sans;
			const FName Weight = Font >= 2 ? FName(TEXT("Bold")) : FName(TEXT("Black"));
			const UFont* FontAsset = CampusUI::FontAsset(Family);
			if (FontAsset)
			{
				auto DrawText = [&](const FString& Str, const FVector2D& At, float Points, int32 Tracking)
				{
					FSlateFontInfo Info = CampusUI::Font(Family, Points, Weight, Tracking);
					FCanvasTextItem Item(At, FText::FromString(Str), Info, Ink);
					Item.Font = FontAsset;
					Item.bCentreX = true;
					Item.bCentreY = true;
					Canvas->DrawItem(Item);
				};
				if (bVertical)
				{
					const int32 N = FMath::Max(Text.Len(), 1);
					const float Cell = (Size.Y - Inset * 2.f) / N;
					const float Points = FMath::Min(Cell, Size.X) * 0.52f;
					for (int32 i = 0; i < Text.Len(); ++i)
					{
						DrawText(Text.Mid(i, 1), FVector2D(Size.X * 0.5f, Inset + Cell * (i + 0.5f)), Points, 0);
					}
				}
				else
				{
					float Points = Size.Y * 0.42f;
					const int32 Tracking = Text.Len() <= 4 ? 300 : 60;
					if (FSlateApplication::IsInitialized())
					{
						const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
						const float Measured = Measure->Measure(Text, CampusUI::Font(Family, Points, Weight, Tracking)).X;
						if (Measured > Size.X * 0.88f)
						{
							Points *= Size.X * 0.88f / Measured;
						}
					}
					DrawText(Text, Size * 0.5f, Points, Tracking);
				}
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Ctx);
		return RT;
	}
}

void FCampusSchoolBuildContext::FlushRuntime()
{
	for (UActorComponent* Old : B->RuntimeComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	B->RuntimeComponents.Reset();
	B->SignTargets.Reset();
	UWorld* World = B->GetWorld();
	if (!World)
	{
		return;
	}
	const bool bDrawSigns = World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;

	if (World->IsGameWorld())
	{
		// Every machine builds its own panes (the layout is the same everywhere); shots break them on each.
		if (B->WindowGlass)
		{
			B->WindowGlass->Destroy();
			B->WindowGlass = nullptr;
		}
		if (WindowPanes.Num() > 0)
		{
			FActorSpawnParameters Params;
			Params.Owner = B;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			B->WindowGlass = World->SpawnActor<ACampusWindowGlass>(ACampusWindowGlass::StaticClass(), FTransform::Identity, Params);
			if (B->WindowGlass)
			{
				B->WindowGlass->SetPanes(WindowPanes, WindowPaneTempered);
			}
		}
		for (ACampusWater* Old : B->WaterBodies)
		{
			if (Old)
			{
				Old->Destroy();
			}
		}
		B->WaterBodies.Reset();
		for (const FBox& Volume : WaterVolumes)
		{
			FActorSpawnParameters Params;
			Params.Owner = B;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (ACampusWater* Water = World->SpawnActor<ACampusWater>(ACampusWater::StaticClass(), FTransform::Identity, Params))
			{
				Water->SetVolume(Volume);
				B->WaterBodies.Add(Water);
			}
		}
	}

	if (World->IsGameWorld() && World->GetNetMode() != NM_Client)
	{
		for (ACampusDoor* Old : B->Doors)
		{
			if (Old)
			{
				Old->Destroy();
			}
		}
		B->Doors.Reset();
		// Non-colliding boxes (boards, dados, trim) the door leaves must not swing through.
		TArray<FTransform> Decor;
		for (int32 Mat = 0; Mat < ACampusSchoolBuilder::NumMats; ++Mat)
		{
			const int32 Index = ((int32)ECampusShape::Cube * 2) * ACampusSchoolBuilder::NumMats + Mat;
			if (Batches.IsValidIndex(Index))
			{
				Decor.Append(Batches[Index].Transforms);
			}
		}
		for (const FDoorSpec& D : Doors)
		{
			ACampusDoor* Door = World->SpawnActorDeferred<ACampusDoor>(ACampusDoor::StaticClass(), FTransform(FRotator(0.f, D.Yaw, 0.f), D.Hinge), B, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Door)
			{
				Door->Setup(D.Width, D.Height);
				Door->FinishSpawning(FTransform(FRotator(0.f, D.Yaw, 0.f), D.Hinge));
				// Openings onto a stair flight or into a tight corner are left open rather than given a door that can't swing.
				if (!Door->MeasureClearance(Decor))
				{
					Door->Destroy();
					continue;
				}
				B->Doors.Add(Door);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[CampusDoors] %d of %d openings have a door"), B->Doors.Num(), Doors.Num());
	}

	if (bTerrain && World->GetNetMode() != NM_DedicatedServer)
	{
		if (UStaticMesh* TerrainMesh = CampusAssets::TerrainMesh())
		{
			UStaticMeshComponent* Terrain = NewObject<UStaticMeshComponent>(B, NAME_None, RF_Transient);
			Terrain->SetupAttachment(B->GetRootComponent());
			Terrain->SetMobility(EComponentMobility::Static);
			Terrain->SetStaticMesh(TerrainMesh);
			Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Terrain->SetCanEverAffectNavigation(false);
			// Kilometres away: no distance field, no decals, no bounce light worth tracing.
			Terrain->bAffectDistanceFieldLighting = false;
			Terrain->bAffectDynamicIndirectLighting = false;
			Terrain->bReceivesDecals = false;
			Terrain->bVisibleInRayTracing = false;
			Terrain->SetCastShadow(CampusGraphics::TerrainShadows());
			Terrain->SetRelativeLocation(TerrainOffset);
			Terrain->RegisterComponent();
			B->RuntimeComponents.Add(Terrain);
		}
	}

	// Fluorescent fixtures: real light so classrooms read as lit interiors. Shadowed when MegaLights is on (cheap), otherwise unshadowed and short.
	const bool bMegaLights = CampusGraphics::LocalLightShadows();
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		for (const FLightSpec& L : Lights)
		{
			if (L.Kind != 0)
			{
				ULocalLightComponent* Local = nullptr;
				if (L.Kind == 2)
				{
					USpotLightComponent* Spot = NewObject<USpotLightComponent>(B, NAME_None, RF_Transient);
					Spot->SetOuterConeAngle(L.Cone);
					Spot->SetInnerConeAngle(L.Cone * 0.6f);
					Local = Spot;
				}
				else
				{
					UPointLightComponent* Point = NewObject<UPointLightComponent>(B, NAME_None, RF_Transient);
					Point->SetSourceRadius(4.f);
					Local = Point;
				}
				Local->SetupAttachment(B->GetRootComponent());
				Local->SetMobility(EComponentMobility::Movable);
				Local->SetIntensityUnits(ELightUnits::Lumens);
				Local->SetIntensity(L.Lumens);
				Local->SetAttenuationRadius(L.Radius > 0.f ? L.Radius : 600.f);
				Local->SetUseTemperature(true);
				Local->SetTemperature(L.Temperature);
				Local->SetLightColor(L.Color);
				Local->SetCastShadows(bMegaLights && L.Lumens > 1500.f);
				// Decorative lamps: a faint halo in the fog, and none on translucency. MegaLights lights the translucency volume
				// with every one of them at full strength (the scattering scale doesn't apply there), so two dozen warm
				// downlights turned a smoke screen under them into a glowing orange cloud that read as a fire.
				Local->SetVolumetricScatteringIntensity(0.1f);
				Local->SetAffectTranslucentLighting(false);
				Local->RegisterComponent();
				Local->SetWorldLocationAndRotation(L.Location, L.Rotation);
				B->RuntimeComponents.Add(Local);
				continue;
			}
			URectLightComponent* Light = NewObject<URectLightComponent>(B, NAME_None, RF_Transient);
			Light->SetupAttachment(B->GetRootComponent());
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensityUnits(ELightUnits::Lumens);
			Light->SetIntensity(L.Lumens);
			if (L.bClassroom)
			{
				Light->ComponentTags.Add(TEXT("CampusClassroom"));
			}
			Light->SetSourceWidth(L.bAlongY ? 110.f : 24.f);
			Light->SetSourceHeight(L.bAlongY ? 24.f : 110.f);
			Light->SetBarnDoorAngle(80.f);
			Light->SetBarnDoorLength(12.f);
			Light->SetAttenuationRadius(L.Radius > 0.f ? L.Radius : (bMegaLights ? 800.f : 450.f));
			Light->SetUseTemperature(true);
			Light->SetTemperature(L.Temperature);
			Light->SetLightColor(L.Color);
			Light->SetCastShadows(bMegaLights);
			Light->SetVolumetricScatteringIntensity(0.15f);
			Light->RegisterComponent();
			Light->SetWorldLocationAndRotation(L.Location, FRotator(-90.f, 0.f, 0.f));
			B->RuntimeComponents.Add(Light);

			// Without Lumen nothing carries the light the floor and desks throw back up, and screen space GI loses it as
			// soon as the floor is off screen: the ceiling round a lit fixture went black. A broad, dim, unshadowed
			// source a little above desk height facing up stands in for that bounce (Lumen modes switch it off). Short
			// enough not to reach the ceiling of the floor above.
			URectLightComponent* Bounce = NewObject<URectLightComponent>(B, NAME_None, RF_Transient);
			Bounce->SetupAttachment(B->GetRootComponent());
			Bounce->SetMobility(EComponentMobility::Movable);
			Bounce->SetIntensityUnits(ELightUnits::Lumens);
			Bounce->SetIntensity(L.Lumens * 0.14f);
			if (L.bClassroom)
			{
				Bounce->ComponentTags.Add(TEXT("CampusClassroom"));
			}
			Bounce->SetSourceWidth(220.f);
			Bounce->SetSourceHeight(220.f);
			Bounce->SetAttenuationRadius(420.f);
			Bounce->SetUseTemperature(true);
			Bounce->SetTemperature(L.Temperature - 400.f);
			Bounce->SetLightColor(L.Color);
			Bounce->SetCastShadows(false);
			Bounce->SetSpecularScale(0.f);
			Bounce->SetVolumetricScatteringIntensity(0.f);
			Bounce->SetAffectTranslucentLighting(false);
			Bounce->ComponentTags.Add(CampusGraphics::BounceLightTag);
			Bounce->SetVisibility(CampusGraphics::GlobalIllumination() <= 1);
			Bounce->RegisterComponent();
			Bounce->SetWorldLocationAndRotation(L.Location - FVector(0.f, 0.f, 150.f), FRotator(90.f, 0.f, 0.f));
			B->RuntimeComponents.Add(Bounce);
		}
	}

	UStaticMesh* Cube = CampusAssets::Shape(ECampusShape::Cube);
	UMaterialInterface* SignMat = CampusAssets::SignMaterial();
	for (const FSignSpec& Sg : Signs)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(B, NAME_None, RF_Transient);
		Mesh->SetupAttachment(B->GetRootComponent());
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SignMat, B);
		MID->SetScalarParameterValue(TEXT("Emissive"), Sg.Emissive);
		UTextureRenderTarget2D* RT = bDrawSigns ? DrawSignTexture(World, Sg.Text, Sg.Width, Sg.Height, Sg.Back, Sg.Ink, Sg.Font, Sg.bVertical, Sg.PxPerCm) : nullptr;
		if (RT)
		{
			MID->SetTextureParameterValue(TEXT("SignTex"), RT);
			B->SignTargets.Add(RT);
		}
		else
		{
			MID->SetVectorParameterValue(TEXT("Tint"), Sg.Back);
		}
		Mesh->SetMaterial(0, MID);
		Mesh->RegisterComponent();
		Mesh->SetWorldTransform(FTransform(FRotator(0.f, Sg.Yaw, 0.f), Sg.Center, FVector(Sg.Depth, Sg.Width, Sg.Height) / 100.f));
		B->RuntimeComponents.Add(Mesh);
	}

	if (B->Screens)
	{
		B->Screens->Destroy();
		B->Screens = nullptr;
	}
	if (Displays.Num() > 0)
	{
		// The pictures are drawn live in game worlds; in the editor the panels stay dark.
		ACampusHQScreens* Screens = nullptr;
		if (bDrawSigns)
		{
			Screens = World->SpawnActorDeferred<ACampusHQScreens>(ACampusHQScreens::StaticClass(), FTransform::Identity, B);
			if (Screens)
			{
				for (const FScreenCamera& Cam : ScreenCameras)
				{
					Screens->CameraSpots.Add({ Cam.Location, Cam.Rotation, Cam.FOV, Cam.Name });
				}
				Screens->DroneTarget = DroneTarget;
				Screens->FinishSpawning(FTransform::Identity);
				B->Screens = Screens;
			}
		}
		UMaterialInterface* DisplayMat = CampusAssets::DisplayMaterial();
		for (const FDisplaySpec& D : Displays)
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(B, NAME_None, RF_Transient);
			Mesh->SetupAttachment(B->GetRootComponent());
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->SetCastShadow(false);
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(DisplayMat, B);
			MID->SetScalarParameterValue(TEXT("Emissive"), D.Emissive);
			MID->SetVectorParameterValue(TEXT("UVMin"), FLinearColor(D.UV.X, D.UV.Y, 0.f));
			MID->SetVectorParameterValue(TEXT("UVMax"), FLinearColor(D.UV.Z, D.UV.W, 0.f));
			if (Screens)
			{
				MID->SetTextureParameterValue(TEXT("Screen"), Screens->Target((ECampusHQDisplay)D.Kind));
			}
			Mesh->SetMaterial(0, MID);
			Mesh->RegisterComponent();
			Mesh->SetWorldTransform(D.Transform);
			B->RuntimeComponents.Add(Mesh);
		}
	}
}

void FCampusSchoolBuildContext::Build()
{
	B->SpawnPoints.Reset();
	B->Hotspots.Reset();
	B->GunfightHotspots.Reset();
	B->MinimapRects.Reset();
	B->MenuAnchors.RackSlots.Reset();

	if (B->MapKind == ECampusMap::Hotel)
	{
		BuildHotel();
		Flush();
		return;
	}
	if (B->MapKind == ECampusMap::HQ)
	{
		BuildHQ();
		Flush();
		return;
	}

	BuildGround();
	BuildPerimeter();
	BuildAdmin();
	BuildWestWing();
	BuildNorthWing();
	BuildWalkway();
	BuildCourtyard();
	BuildEastLane();
	BuildField();
	BuildPlaza();
	BuildWestAlley();
	BuildGunfightArena();
	BuildSurroundings();
	BuildDetails();
	BuildSigns();
	BuildMetadata();
	Flush();
}
