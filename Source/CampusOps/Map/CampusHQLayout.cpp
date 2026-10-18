#include "Map/CampusSchoolLayout.h"
#include "Map/CampusSchoolBuilder.h"
#include "Map/CampusHQScreens.h"

/*
 * 特勤处 (Special Operations HQ): the main menu's home. A tactical operations centre in a converted hangar - concrete,
 * steel trusses, a wall of screens, consoles, the briefing table, the armoury cage and the gunsmith's bench - with the
 * hangar door rolled up onto the helipad, a helicopter waiting under the floodlights and the city at night beyond.
 */

namespace
{
	constexpr float QX = 1400.f;
	constexpr float QY = 900.f;
	constexpr float QH = 650.f;
	constexpr float DoorX = 620.f;
	constexpr float DoorH = 480.f;
	constexpr float Mezz = 320.f;
}

void FCampusSchoolBuildContext::BuildHQ()
{
	DoorTrim = EPal::SteelDark;
	WindowTrim = EPal::SteelDark;

	// ---------------------------------------------------------------- shell
	BoxMM(EPal::ConcreteHQ, FVector(-QX - 40.f, -QY - 40.f, -40.f), FVector(QX + 40.f, QY + 40.f, 0.f));
	Deco(EPal::Epoxy, FVector(-QX, -QY, 0.f), FVector(QX, QY, 1.f));
	// Floor markings: a walkway border and hazard bands at the door.
	for (const float S : { -1.f, 1.f })
	{
		Deco(EPal::BarrierYellow, FVector(-QX + 60.f, S * (QY - 60.f) - 5.f, 1.f), FVector(QX - 60.f, S * (QY - 60.f) + 5.f, 1.5f));
		Deco(EPal::BarrierYellow, FVector(S * (QX - 60.f) - 5.f, -QY + 60.f, 1.f), FVector(S * (QX - 60.f) + 5.f, QY - 60.f, 1.5f));
	}
	for (float X = -DoorX; X < DoorX; X += 80.f)
	{
		Deco(EPal::BarrierYellow, FVector(X, -QY + 5.f, 1.f), FVector(X + 40.f, -QY + 45.f, 1.6f));
		Deco(EPal::BarrierBlack, FVector(X + 40.f, -QY + 5.f, 1.f), FVector(X + 80.f, -QY + 45.f, 1.6f));
	}
	WallY(EPal::ConcreteHQ, -QX - 20.f, -QY, QY, 0.f, QH, 40.f, {}, EPal::Count);
	WallY(EPal::ConcreteHQ, QX + 20.f, -QY, QY, 0.f, QH, 40.f, {}, EPal::Count);
	WallX(EPal::ConcreteHQ, QY + 20.f, -QX - 40.f, QX + 40.f, 0.f, QH, 40.f, {}, EPal::Count);
	WallX(EPal::ConcreteHQ, -QY - 20.f, -QX - 40.f, QX + 40.f, 0.f, QH, 40.f, { { -DoorX, DoorX, 0.f, DoorH, false } }, EPal::Count);
	BoxMM(EPal::SteelDark, FVector(-QX - 40.f, -QY - 40.f, QH), FVector(QX + 40.f, QY + 40.f, QH + 30.f));
	// The rolled-up hangar door in its housing, and the steel frame of the opening.
	BoxMM(EPal::SteelDark, FVector(-DoorX - 30.f, -QY - 70.f, DoorH), FVector(DoorX + 30.f, -QY + 10.f, DoorH + 90.f), false);
	for (const float S : { -1.f, 1.f })
	{
		BoxMM(EPal::BarrierYellow, FVector(S * DoorX - 25.f, -QY - 50.f, 0.f), FVector(S * DoorX + 25.f, -QY + 10.f, DoorH), false);
	}
	// Roof trusses with cable trays, and industrial pendants.
	for (float X = -QX + 200.f; X < QX; X += 400.f)
	{
		BoxMM(EPal::SteelDark, FVector(X - 15.f, -QY, QH - 60.f), FVector(X + 15.f, QY, QH - 40.f), false);
		BoxMM(EPal::SteelDark, FVector(X - 15.f, -QY, QH - 140.f), FVector(X + 15.f, QY, QH - 125.f), false);
		for (float Y = -QY + 100.f; Y < QY; Y += 200.f)
		{
			BoxR(EPal::SteelDark, FVector(X, Y, QH - 90.f), FVector(6.f, 140.f, 6.f), FRotator(0.f, 0.f, 35.f), false);
		}
		CylR(EPal::Black, FVector(X, 0.f, QH - 160.f), 1.f, 200.f, FRotator::ZeroRotator, false);
		Cone(EPal::SteelDark, FVector(X, 0.f, QH - 290.f), 60.f, 35.f, false);
		PointLight(FVector(X, 0.f, QH - 300.f), 2200.f, 3000.f, 900.f);
	}
	for (const float Y : { -QY + 120.f, QY - 400.f })
	{
		Deco(EPal::Galvanized, FVector(-QX, Y - 30.f, QH - 180.f), FVector(QX, Y + 30.f, QH - 172.f));
		Deco(EPal::Black, FVector(-QX, Y - 22.f, QH - 172.f), FVector(QX, Y + 22.f, QH - 160.f));
	}

	// ---------------------------------------------------------------- the screen wall and the mezzanine
	const float WallY0 = QY - 2.f;
	BoxMM(EPal::SteelDark, FVector(-760.f, WallY0 - 30.f, 90.f), FVector(760.f, WallY0, 440.f), false);
	// 6 x 3 panels of 241 x 106 cm with 6 cm bezels. Pictures span blocks of them, as on a real video wall; seen from
	// the floor, left to right: radar, the drone feed, the tactical map and the security cameras over two rows, then
	// telemetry, the radio log, the mission clock, readiness and weather along the bottom.
	struct FGroup { ECampusHQDisplay Kind; int32 Left0, Left1, Row0, Row1; };
	const FGroup Groups[] = {
		{ ECampusHQDisplay::Radar, 0, 0, 1, 2 }, { ECampusHQDisplay::Drone, 1, 1, 1, 2 }, { ECampusHQDisplay::Map, 2, 3, 1, 2 }, { ECampusHQDisplay::Cameras, 4, 5, 1, 2 },
		{ ECampusHQDisplay::Telemetry, 0, 0, 0, 0 }, { ECampusHQDisplay::Comms, 1, 2, 0, 0 }, { ECampusHQDisplay::Clock, 3, 3, 0, 0 },
		{ ECampusHQDisplay::Readiness, 4, 4, 0, 0 }, { ECampusHQDisplay::Weather, 5, 5, 0, 0 } };
	for (const FGroup& G : Groups)
	{
		const float GW = (G.Left1 - G.Left0) * 247.f + 241.f;
		const float GH = (G.Row1 - G.Row0) * 112.f + 106.f;
		for (int32 Left = G.Left0; Left <= G.Left1; ++Left)
		{
			for (int32 Row = G.Row0; Row <= G.Row1; ++Row)
			{
				// Facing -Y, the viewer's left is +X.
				const float X0 = -740.f + (5 - Left) * 247.f;
				const float Z0 = 100.f + Row * 112.f;
				const float U0 = (Left - G.Left0) * 247.f / GW;
				const float V0 = (G.Row1 - Row) * 112.f / GH;
				Display((uint8)G.Kind, FVector(X0 + 120.5f, WallY0 - 31.f, Z0 + 53.f), FRotator(0.f, -90.f, 0.f), 241.f, 106.f, FVector4f(U0, V0, U0 + 241.f / GW, V0 + 106.f / GH), 1.7f);
			}
		}
	}
	Sign(FVector(0.f, WallY0 - 34.f, 500.f), -90.f, 900.f, 90.f, TEXT("特勤处  ·  战术作战中心"), FLinearColor(0.02f, 0.025f, 0.03f), FLinearColor(0.85f, 0.9f, 1.f), 2, false, 2.f);
	SpotLight(FVector(0.f, 200.f, QH - 200.f), FRotator(-35.f, 90.f, 0.f), 2500.f, 6500.f, 1400.f, 45.f, FLinearColor(0.7f, 0.85f, 1.f));
	// Mezzanine walkway along the north wall with a stair at the west end.
	BoxMM(EPal::SteelDark, FVector(-QX, QY - 260.f, Mezz - 15.f), FVector(-800.f, QY, Mezz));
	BoxMM(EPal::SteelDark, FVector(800.f, QY - 260.f, Mezz - 15.f), FVector(QX, QY, Mezz));
	for (const float X0 : { -QX, 800.f })
	{
		Deco(EPal::Galvanized, FVector(X0, QY - 262.f, Mezz + 100.f), FVector(X0 + 600.f, QY - 258.f, Mezz + 105.f));
		for (float X = X0; X <= X0 + 600.f; X += 100.f)
		{
			Deco(EPal::Galvanized, FVector(X - 2.f, QY - 262.f, Mezz), FVector(X + 2.f, QY - 258.f, Mezz + 105.f));
			Deco(EPal::SteelDark, FVector(X - 6.f, QY - 266.f, 0.f), FVector(X + 6.f, QY - 254.f, Mezz - 15.f));
		}
	}
	StraightStair(FVector(-QX + 80.f, 0.f, 0.f), FVector(-QX + 80.f, QY - 270.f, Mezz), 110.f, EPal::Galvanized, EPal::SteelDark);

	// ---------------------------------------------------------------- consoles and the briefing table
	for (const float Y : { 470.f, 250.f })
	{
		BoxMM(EPal::SteelDark, FVector(-650.f, Y - 45.f, 0.f), FVector(650.f, Y + 45.f, 76.f));
		Deco(EPal::Black, FVector(-655.f, Y - 50.f, 76.f), FVector(655.f, Y + 50.f, 79.f));
		// Each operator's monitor repeats one of the wall's pictures.
		static const ECampusHQDisplay Consoles[] = { ECampusHQDisplay::Map, ECampusHQDisplay::Telemetry, ECampusHQDisplay::Cameras, ECampusHQDisplay::Comms, ECampusHQDisplay::Radar, ECampusHQDisplay::Drone };
		int32 Seat = Y > 300.f ? 0 : 3;
		for (float X = -560.f; X <= 560.f; X += 140.f)
		{
			BoxR(EPal::Black, FVector(X, Y + 20.f, 110.f), FVector(60.f, 4.f, 38.f), FRotator(0.f, 0.f, -12.f), false);
			const ECampusHQDisplay Kind = Consoles[Seat++ % UE_ARRAY_COUNT(Consoles)];
			Display((uint8)Kind, FVector(X, Y + 17.5f, 110.f), FRotator(12.f, -90.f, 0.f), 54.f, 32.f, FillUV((uint8)Kind, 54.f, 32.f), 1.3f);
			Cyl(EPal::Black, FVector(X, Y - 100.f, 0.f), 50.f, 48.f, false);
			Box(EPal::Leather, FVector(X, Y - 115.f, 75.f), FVector(50.f, 8.f, 55.f), false);
		}
	}
	{
		const FVector T(0.f, -120.f, 0.f);
		BoxMM(EPal::SteelDark, T + FVector(-290.f, -130.f, 0.f), T + FVector(290.f, 130.f, 88.f));
		// The table top is one big touch display with the tactical map, read from the door side.
		Display((uint8)ECampusHQDisplay::Map, T + FVector(0.f, 0.f, 88.f), FRotator(90.f, -90.f, 0.f), 550.f, 230.f, FillUV((uint8)ECampusHQDisplay::Map, 550.f, 230.f), 1.2f);
		PointLight(T + FVector(0.f, 0.f, 140.f), 600.f, 9000.f, 400.f, FLinearColor(0.5f, 0.75f, 1.f));
	}

	// ---------------------------------------------------------------- armoury (west) and the gunsmith (east)
	// Mesh cage with lockers and ammunition, sandbags and crates by the door.
	Box(EPal::GrilleMesh, FVector(-900.f, -350.f, 160.f), FVector(1.f, 900.f, 320.f), false, 0.f);
	for (float Y = -780.f; Y < 100.f; Y += 62.f)
	{
		BoxMM(EPal::Olive, FVector(-QX + 5.f, Y, 0.f), FVector(-QX + 65.f, Y + 58.f, 200.f));
		Deco(EPal::Black, FVector(-QX + 65.f, Y + 26.f, 110.f), FVector(-QX + 67.f, Y + 32.f, 140.f));
	}
	for (int32 i = 0; i < 6; ++i)
	{
		const FVector C(-1150.f + (i % 3) * 90.f, -650.f + (i / 3) * 70.f, 0.f);
		BoxMM(EPal::Olive, C, C + FVector(80.f, 55.f, 40.f + 40.f * (i % 2)));
	}
	for (float X = -1300.f; X < -950.f; X += 70.f)
	{
		BoxR(EPal::Sandbag, FVector(X, 500.f, 15.f), FVector(65.f, 40.f, 28.f), FRotator(0.f, 5.f * ((int32)X % 3), 0.f), true);
		BoxR(EPal::Sandbag, FVector(X + 35.f, 500.f, 42.f), FVector(65.f, 40.f, 26.f), FRotator(0.f, -4.f, 0.f), true);
	}
	// Weapon racks on the south wall, east of the door: slots the menu fills with real weapons.
	BoxMM(EPal::SteelDark, FVector(760.f, -QY + 2.f, 40.f), FVector(1360.f, -QY + 30.f, 260.f), false);
	Deco(EPal::WoodDesk, FVector(770.f, -QY + 30.f, 60.f), FVector(1350.f, -QY + 40.f, 70.f));
	for (int32 i = 0; i < 6; ++i)
	{
		const float X = 820.f + i * 98.f;
		B->MenuAnchors.RackSlots.Add(FTransform(FRotator(86.f, 90.f, 0.f), FVector(X, -QY + 48.f, 95.f)));
		Deco(EPal::SteelDark, FVector(X - 12.f, -QY + 30.f, 240.f), FVector(X + 12.f, -QY + 50.f, 248.f));
	}
	SpotLight(FVector(1060.f, -500.f, 420.f), FRotator(-55.f, -90.f, 0.f), 1600.f, 3800.f, 900.f, 40.f);
	// Gunsmith's bench: heavy top, drawers, pegboard and a task lamp; the display sits on it.
	const FVector Bench(1100.f, -300.f, 0.f);
	BoxMM(EPal::SteelDark, Bench + FVector(-160.f, -60.f, 0.f), Bench + FVector(160.f, 60.f, 78.f));
	Deco(EPal::WoodDesk, Bench + FVector(-170.f, -70.f, 78.f), Bench + FVector(170.f, 70.f, 84.f));
	for (float X = -140.f; X < 150.f; X += 70.f)
	{
		Deco(EPal::Galvanized, Bench + FVector(X, -62.f, 20.f), Bench + FVector(X + 60.f, -60.f, 70.f));
	}
	BoxMM(EPal::Shelf, FVector(QX - 10.f, -700.f, 90.f), FVector(QX, 100.f, 260.f), false);
	for (int32 i = 0; i < 18; ++i)
	{
		const float Y = -680.f + (i * 47 % 760);
		const float Z = 110.f + (i * 29 % 130);
		Deco(i % 3 == 0 ? EPal::BarrierYellow : EPal::SteelDark, FVector(QX - 16.f, Y, Z), FVector(QX - 10.f, Y + 6.f + (i % 4) * 5.f, Z + 30.f + (i % 3) * 15.f));
	}
	B->MenuAnchors.WeaponDesk = FTransform(FRotator::ZeroRotator, Bench + FVector(0.f, 0.f, 84.f));

	// ---------------------------------------------------------------- outside: the helipad, the helicopter, the city
	BoxMM(EPal::Asphalt, FVector(-30000.f, -30000.f, -60.f), FVector(30000.f, -QY - 40.f, -20.f));
	BoxMM(EPal::Asphalt, FVector(-30000.f, QY + 40.f, -60.f), FVector(30000.f, 30000.f, -20.f));
	BoxMM(EPal::Asphalt, FVector(-30000.f, -QY - 40.f, -60.f), FVector(-QX - 40.f, QY + 40.f, -20.f));
	BoxMM(EPal::Asphalt, FVector(QX + 40.f, -QY - 40.f, -60.f), FVector(30000.f, QY + 40.f, -20.f));
	BoxMM(EPal::HeliDeck, FVector(-1600.f, -4200.f, -20.f), FVector(1600.f, -QY - 40.f, 0.f));
	const FVector Pad(0.f, -2700.f, 0.f);
	for (int32 i = 0; i < 40; ++i)
	{
		const float A = i / 40.f * 2.f * PI;
		const float A2 = (i + 1) / 40.f * 2.f * PI;
		const FVector P0 = Pad + FVector(FMath::Cos(A) * 900.f, FMath::Sin(A) * 900.f, 1.f);
		const FVector P1 = Pad + FVector(FMath::Cos(A2) * 900.f, FMath::Sin(A2) * 900.f, 1.f);
		BoxR(EPal::HeliYellow, (P0 + P1) * 0.5f, FVector(FVector::Dist(P0, P1) + 2.f, 40.f, 1.f), FRotator(0.f, (P1 - P0).Rotation().Yaw, 0.f), false);
		if (i % 2 == 0)
		{
			Cyl(EPal::LedCool, Pad + FVector(FMath::Cos(A) * 1050.f, FMath::Sin(A) * 1050.f, 0.f), 16.f, 8.f, false);
		}
	}
	// Utility helicopter: cabin, nose, engine housing, tail boom and fin, skids, rotors.
	{
		const FVector H(0.f, -2700.f, 0.f);
		BoxMM(EPal::SteelDark, H + FVector(-130.f, -260.f, 70.f), H + FVector(130.f, 220.f, 290.f));
		BoxR(EPal::SteelDark, H + FVector(0.f, -330.f, 150.f), FVector(220.f, 160.f, 150.f), FRotator(0.f, 90.f, 0.f), false);
		BoxR(EPal::Glass, H + FVector(0.f, -395.f, 210.f), FVector(200.f, 70.f, 90.f), FRotator(-25.f, 90.f, 0.f), false);
		BoxMM(EPal::SteelDark, H + FVector(-90.f, -150.f, 290.f), H + FVector(90.f, 160.f, 350.f));
		BoxR(EPal::SteelDark, H + FVector(0.f, 560.f, 240.f), FVector(70.f, 720.f, 70.f), FRotator(4.f, 0.f, 0.f), false);
		BoxR(EPal::SteelDark, H + FVector(0.f, 930.f, 330.f), FVector(16.f, 120.f, 200.f), FRotator(0.f, 0.f, 0.f), false);
		for (const float S : { -1.f, 1.f })
		{
			CylR(EPal::Black, H + FVector(S * 140.f, -30.f, 12.f), 8.f, 480.f, FRotator(90.f, 90.f, 0.f), false);
			Box(EPal::Black, H + FVector(S * 125.f, -150.f, 40.f), FVector(6.f, 6.f, 60.f), false);
			Box(EPal::Black, H + FVector(S * 125.f, 100.f, 40.f), FVector(6.f, 6.f, 60.f), false);
		}
		Cyl(EPal::Black, H + FVector(0.f, 0.f, 350.f), 40.f, 40.f, false);
		for (int32 b = 0; b < 4; ++b)
		{
			BoxR(EPal::Black, H + FVector(0.f, 0.f, 392.f), FVector(1600.f, 50.f, 4.f), FRotator(0.f, 20.f + b * 45.f, 0.f), false);
		}
		Sphere(EPal::AviationRed, H + FVector(-135.f, -120.f, 260.f), 12.f, false);
		Sphere(EPal::ScreenGreen, H + FVector(135.f, -120.f, 260.f), 12.f, false);
		BoxMM(EPal::Black, H + FVector(-132.f, -40.f, 90.f), H + FVector(-130.f, 160.f, 250.f), false);
	}
	for (const FVector& Pole : { FVector(-1500.f, -1500.f, 0.f), FVector(1500.f, -1500.f, 0.f), FVector(-1500.f, -4000.f, 0.f), FVector(1500.f, -4000.f, 0.f) })
	{
		Cyl(EPal::Galvanized, Pole, 20.f, 900.f, false);
		BoxMM(EPal::LedCool, Pole + FVector(-40.f, -20.f, 890.f), Pole + FVector(40.f, 20.f, 910.f), false);
		SpotLight(Pole + FVector(0.f, 0.f, 880.f), (Pad + FVector(0.f, 0.f, 150.f) - (Pole + FVector(0.f, 0.f, 880.f))).Rotation(), 30000.f, 5600.f, 4000.f, 32.f);
	}
	// Perimeter fence and the far city.
	for (float X = -4000.f; X <= 4000.f; X += 300.f)
	{
		Cyl(EPal::Galvanized, FVector(X, -5200.f, -20.f), 8.f, 280.f, false);
	}
	Box(EPal::GrilleMesh, FVector(0.f, -5200.f, 120.f), FVector(1.f, 8000.f, 260.f), false, 90.f);
	for (int32 i = 0; i < 22; ++i)
	{
		const float X = -36000.f + i * 3400.f + (i * 977 % 1300);
		const float Y = -14000.f - (i * 1931 % 9000);
		SkyTower(FVector(X, Y, -20.f), 2400.f + (i * 331 % 2000), 2200.f + (i * 571 % 1800), 6000.f + (i * 2711 % 14000), (EPal)((int32)EPal::TowerBlue + i % 5), 300 + i, false);
	}
	for (int32 i = 0; i < 70; ++i)
	{
		const float Ang = -PI * 0.5f + (i / 70.f - 0.5f) * PI * 1.4f;
		const float R = 9000.f + (i * 3137 % 9000);
		CityBlock(FVector(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, -20.f), (float)(i % 4) * 90.f, 2000.f + (i * 173 % 900), 1800.f + (i * 311 % 900), 4 + i % 9, 1000 + i, false, true);
	}
	bTerrain = true;
	TerrainOffset = FVector(0.f, 0.f, -20.f);

	// The screens' live feeds: the drone over the pad, then security cameras on the hangar, its door, the armoury and
	// the perimeter.
	DroneTarget = Pad;
	auto Camera = [this](const FVector& At, const FVector& LookAt, float FOV, const TCHAR* Name)
	{
		ScreenCameras.Add({ At, (LookAt - At).Rotation(), FOV, Name });
	};
	Camera(Pad + FVector(0.f, 0.f, 3600.f), Pad, 32.f, TEXT("UAV-2"));
	Camera(FVector(1450.f, -QY - 80.f, 560.f), Pad + FVector(0.f, 0.f, 150.f), 62.f, TEXT("CAM 01  停機坪"));
	Camera(FVector(QX - 80.f, QY - 300.f, 560.f), FVector(0.f, -QY, 160.f), 78.f, TEXT("CAM 02  機庫大門"));
	Camera(FVector(-QX + 80.f, -QY + 80.f, 470.f), FVector(-1100.f, -250.f, 60.f), 74.f, TEXT("CAM 03  軍械庫"));
	Camera(FVector(3200.f, -4800.f, 450.f), Pad + FVector(0.f, 0.f, 200.f), 50.f, TEXT("CAM 04  外圍"));

	// ---------------------------------------------------------------- menu anchors
	FCampusMenuAnchors& M = B->MenuAnchors;
	// The operator stands in front of the briefing table, facing the door, the screen wall glowing behind him.
	M.Operator = FTransform(FRotator(0.f, -90.f, 0.f), FVector(-180.f, -420.f, 0.f));
	M.CourtyardFocus = FVector(0.f, 400.f, 250.f);
	M.GateFocus = FVector(0.f, -2700.f, 150.f);
	// From the hangar door corner across the floor: the briefing table, the operator and the glowing screen wall.
	M.TitleCamera = FVector(1150.f, -760.f, 240.f);
	M.TitleTarget = FVector(-350.f, 450.f, 210.f);
	M.OptionsCamera = FVector(-600.f, 350.f, 175.f);
	M.OptionsTarget = FVector(-1350.f, -300.f, 120.f);
	M.DeployCamera = FVector(0.f, -600.f, 190.f);
	M.DeployTarget = FVector(0.f, -2700.f, 180.f);

	B->PlayableBounds = FBox(FVector(-QX, -QY, 0.f), FVector(QX, QY, QH));
	Rect(-QX, -QY, QX, QY, 0);
}
