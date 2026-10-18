#include "Map/CampusSchoolLayout.h"
#include "Map/CampusSchoolBuilder.h"

/*
 * 天際酒店 (Skyline Hotel): the roof of a 52-storey hotel tower in Taipei's Xinyi district, 180 m above the street.
 *
 * Teams spawn on the south and north terraces; three lanes run between them, mirrored about Y = 0:
 *   west   - the infinity pool deck: a 52 m pool (waist deep, wade it for cover), loungers and cabanas;
 *   middle - the glass sky lounge (island bar, booths, kitchen) with the helipad on its roof (two outside stairs);
 *   east   - the plant deck: machine room, cooling towers, air handlers and ducts, tight and broken up.
 * Below the deck a service level (lockers, linen, a long corridor) links both stair cores, the lounge kitchen and the
 * machine room: the flank route. Everything is close: about 90 x 70 m, built for pushing.
 */

namespace
{
	constexpr float HX = 3400.f;
	constexpr float HY = 4600.f;
	constexpr float LowFloor = -400.f;
	constexpr float LowCeil = -30.f;
	constexpr float Street = -18000.f;
	// Sky lounge
	constexpr float LX0 = -1300.f, LX1 = 1300.f, LY = 1800.f, LH = 420.f, LRoof = 450.f;
	constexpr float KitchenX = 700.f;
	// Infinity pool
	constexpr float PX0 = -3300.f, PX1 = -2300.f, PY = 2600.f, PoolFloor = -60.f, WaterZ = -12.f;
	// Stair cores (south one at -Y, the north one mirrored)
	constexpr float CX0 = 300.f, CX1 = 1300.f, CY0 = 2500.f, CY1 = 3300.f, CoreH = 420.f;
	// Machine room
	constexpr float MX0 = 2300.f, MX1 = 3200.f, MY = 1000.f, MH = 420.f;
	// Lower level: the N-S corridor and the cross corridor to the machine room stair
	constexpr float CorX0 = 300.f, CorX1 = 800.f;
	constexpr float XCorY0 = -700.f, XCorY1 = -300.f;

	uint32 HHash(int32 A, int32 B)
	{
		uint32 H = (uint32)A * 2654435761u ^ (uint32)B * 40503u;
		H ^= H >> 15;
		H *= 0x2c1b3c6du;
		H ^= H >> 12;
		return H;
	}
	float HRand(int32 A, int32 B) { return (HHash(A, B) & 0xFFFF) / 65535.f; }

	// Openings in the roof slab: the pool and the four stairs down to the service level.
	struct FDeckHole { float X0, Y0, X1, Y1; };
	const FDeckHole DeckHoles[] = {
		{ -HX + 25.f, -PY, PX1, PY },
		// Core stairs: the arrival strip at the north end of each well stays slab.
		{ CX0, -CY1 + 50.f, 800.f, -CY0 - 50.f - 120.f },
		{ CX0, CY0 + 50.f + 120.f, 800.f, CY1 - 50.f },
		// Kitchen stair (arrival strip at its south end) and the machine room stair (arrival strip at its south end).
		{ 800.f, 420.f, 1250.f, 900.f },
		{ 2400.f, -180.f, 2800.f, 400.f },
	};

	/** Splits a floor rectangle into the pieces that lie outside every slab opening (nothing may be laid over a stair). */
	template <typename FnType>
	void ForEachOutsideHoles(float X0, float Y0, float X1, float Y1, FnType&& Fn)
	{
		TArray<float, TInlineAllocator<16>> Xs = { X0, X1 };
		TArray<float, TInlineAllocator<16>> Ys = { Y0, Y1 };
		for (const FDeckHole& H : DeckHoles)
		{
			if (H.X1 <= X0 || H.X0 >= X1 || H.Y1 <= Y0 || H.Y0 >= Y1)
			{
				continue;
			}
			Xs.AddUnique(FMath::Clamp(H.X0, X0, X1));
			Xs.AddUnique(FMath::Clamp(H.X1, X0, X1));
			Ys.AddUnique(FMath::Clamp(H.Y0, Y0, Y1));
			Ys.AddUnique(FMath::Clamp(H.Y1, Y0, Y1));
		}
		Xs.Sort();
		Ys.Sort();
		for (int32 i = 0; i + 1 < Xs.Num(); ++i)
		{
			for (int32 j = 0; j + 1 < Ys.Num(); ++j)
			{
				if (Xs[i + 1] - Xs[i] < 0.5f || Ys[j + 1] - Ys[j] < 0.5f)
				{
					continue;
				}
				const float Cx = (Xs[i] + Xs[i + 1]) * 0.5f;
				const float Cy = (Ys[j] + Ys[j + 1]) * 0.5f;
				bool bHole = false;
				for (const FDeckHole& H : DeckHoles)
				{
					bHole |= Cx > H.X0 && Cx < H.X1 && Cy > H.Y0 && Cy < H.Y1;
				}
				if (!bHole)
				{
					Fn(Xs[i], Ys[j], Xs[i + 1], Ys[j + 1]);
				}
			}
		}
	}
}

void FCampusSchoolBuildContext::BuildHotel()
{
	DoorTrim = EPal::SteelDark;
	WindowTrim = EPal::SteelDark;
	HotelDeck();
	HotelEdges();
	HotelPool();
	HotelLounge();
	HotelHelipad();
	HotelCores();
	HotelMechanical();
	HotelLowerLevel();
	HotelTerraces();
	HotelSkyline();
	HotelMetadata();
}

// ------------------------------------------------------------------ deck and shell

void FCampusSchoolBuildContext::HotelDeck()
{
	// The roof slab, with holes for the pool and the four stairs down to the service level.
	ForEachOutsideHoles(-HX, -HY, HX, HY, [this](float X0, float Y0, float X1, float Y1)
	{
		BoxMM(EPal::RoofDeck, FVector(X0, Y0, LowCeil), FVector(X1, Y1, 0.f));
	});
	// Finishes: stone pavers on the terraces, teak on the pool deck, a membrane with walkway tiles on the plant deck.
	// Each one stops at the slab openings, so no stair is ever roofed over by a floor that is not there.
	auto Finish = [this](EPal Pal, float X0, float Y0, float X1, float Y1, float Top)
	{
		ForEachOutsideHoles(X0, Y0, X1, Y1, [this, Pal, Top](float A0, float B0, float A1, float B1)
		{
			Deco(Pal, FVector(A0, B0, 0.f), FVector(A1, B1, Top));
		});
	};
	for (const float S : { -1.f, 1.f })
	{
		const float Y0 = S * 2600.f;
		const float Y1 = S * HY;
		Finish(EPal::Stone2, -HX, FMath::Min(Y0, Y1), HX, FMath::Max(Y0, Y1), 1.f);
	}
	Finish(EPal::Teak, PX1, -PY, LX0, PY, 1.5f);
	Finish(EPal::HeliDeck, 1500.f, -2600.f, HX, 2600.f, 1.f);
	for (float Y = -2500.f; Y < 2500.f; Y += 120.f)
	{
		Deco(EPal::RoofDeck, FVector(1530.f, Y + 5.f, 1.f), FVector(1610.f, Y + 115.f, 2.5f));
	}
	Finish(EPal::Stone2, LX0, -2600.f, 1500.f, -LY, 1.f);
	Finish(EPal::Stone2, LX0, LY, 1500.f, 2600.f, 1.f);

	// The tower below: one curtain-wall shaft from the street to the service level, and a skin round the service
	// storey itself (whose corridors run inside it) so the facade reads unbroken from the neighbours.
	BoxMM(EPal::TowerBlue, FVector(-HX + 20.f, -HY + 20.f, Street), FVector(HX - 20.f, HY - 20.f, LowFloor - 40.f));
	Deco(EPal::TowerBlue, FVector(-HX + 20.f, -HY + 20.f, LowFloor - 40.f), FVector(-HX + 60.f, HY - 20.f, LowCeil));
	Deco(EPal::TowerBlue, FVector(HX - 60.f, -HY + 20.f, LowFloor - 40.f), FVector(HX - 20.f, HY - 20.f, LowCeil));
	Deco(EPal::TowerBlue, FVector(-HX + 20.f, -HY + 20.f, LowFloor - 40.f), FVector(HX - 20.f, -HY + 60.f, LowCeil));
	Deco(EPal::TowerBlue, FVector(-HX + 20.f, HY - 60.f, LowFloor - 40.f), FVector(HX - 20.f, HY - 20.f, LowCeil));
	// Slab edge band under the deck, round the outside of the shaft.
	Deco(EPal::SteelDark, FVector(-HX - 10.f, -HY - 10.f, -60.f), FVector(-HX + 20.f, HY + 10.f, LowCeil + 1.f));
	Deco(EPal::SteelDark, FVector(HX - 20.f, -HY - 10.f, -60.f), FVector(HX + 10.f, HY + 10.f, LowCeil + 1.f));
	Deco(EPal::SteelDark, FVector(-HX - 10.f, -HY - 10.f, -60.f), FVector(HX + 10.f, -HY + 20.f, LowCeil + 1.f));
	Deco(EPal::SteelDark, FVector(-HX - 10.f, HY - 20.f, -60.f), FVector(HX + 10.f, HY + 10.f, LowCeil + 1.f));
}

void FCampusSchoolBuildContext::HotelEdges()
{
	// Glass balustrades round the whole roof; the invisible wall above them keeps everyone on it.
	const float Rail = 115.f;
	const float Block = 900.f;
	GlassRail(FVector(-HX + 15.f, -HY + 15.f, 0.f), FVector(HX - 15.f, -HY + 15.f, 0.f), Rail, Block);
	GlassRail(FVector(-HX + 15.f, HY - 15.f, 0.f), FVector(HX - 15.f, HY - 15.f, 0.f), Rail, Block);
	GlassRail(FVector(HX - 15.f, -HY + 15.f, 0.f), FVector(HX - 15.f, HY - 15.f, 0.f), Rail, Block);
	GlassRail(FVector(-HX + 15.f, -HY + 15.f, 0.f), FVector(-HX + 15.f, HY - 15.f, 0.f), Rail, Block);

	// The roof edge as a tower like this finishes it: a honed granite coping over the curtain wall, a bronze reveal
	// under it carrying a warm cove line that grazes down the facade, and the window-cleaning davit sockets along it.
	// Nothing stands taller than the balustrade, so the skyline stays open all round.
	for (const float S : { -1.f, 1.f })
	{
		// South / north edges (along X), then west / east (along Y).
		Deco(EPal::MarbleDark, FVector(-HX - 30.f, S * HY - 30.f, -8.f), FVector(HX + 30.f, S * HY + 30.f, 2.f));
		Deco(EPal::Brass, FVector(-HX - 26.f, S * (HY + 26.f) - 4.f, -26.f), FVector(HX + 26.f, S * (HY + 26.f) + 4.f, -8.f));
		Deco(EPal::LedWarm, FVector(-HX - 24.f, S * (HY + 31.f) - 1.f, -24.f), FVector(HX + 24.f, S * (HY + 31.f) + 1.f, -20.f));
		Deco(EPal::MarbleDark, FVector(S * HX - 30.f, -HY - 30.f, -8.f), FVector(S * HX + 30.f, HY + 30.f, 2.f));
		Deco(EPal::Brass, FVector(S * (HX + 26.f) - 4.f, -HY - 26.f, -26.f), FVector(S * (HX + 26.f) + 4.f, HY + 26.f, -8.f));
		Deco(EPal::LedWarm, FVector(S * (HX + 31.f) - 1.f, -HY - 24.f, -24.f), FVector(S * (HX + 31.f) + 1.f, HY + 24.f, -20.f));
		for (float A = -HX + 400.f; A < HX - 200.f; A += 800.f)
		{
			Cyl(EPal::Stainless, FVector(A, S * (HY - 45.f), 0.f), 9.f, 6.f, false);
		}
		for (float A = -HY + 400.f; A < HY - 200.f; A += 800.f)
		{
			// Along the west edge the infinity pool runs to the parapet: no deck there for a fitting to sit on.
			if (S < 0.f && FMath::Abs(A) < PY)
			{
				continue;
			}
			Cyl(EPal::Stainless, FVector(S * (HX - 45.f), A, 0.f), 9.f, 6.f, false);
		}
	}
	// Aviation obstruction lights on short masts at the four corners, inside the balustrade.
	for (const float Sx : { -1.f, 1.f })
	{
		for (const float Sy : { -1.f, 1.f })
		{
			const FVector C(Sx * (HX - 70.f), Sy * (HY - 70.f), 0.f);
			Box(EPal::Concrete, C + FVector(0.f, 0.f, 10.f), FVector(50.f, 50.f, 20.f), true);
			Cyl(EPal::Galvanized, C + FVector(0.f, 0.f, 20.f), 6.f, 280.f, true);
			Cyl(EPal::SteelDark, C + FVector(0.f, 0.f, 300.f), 14.f, 22.f, false);
			Sphere(EPal::AviationRed, C + FVector(0.f, 0.f, 326.5f), 11.f, false);
		}
	}
}

// ------------------------------------------------------------------ west: the infinity pool deck

void FCampusSchoolBuildContext::HotelPool()
{
	// Basin: tiled floor and walls; the west wall stops short of the deck so the water spills over the edge into a
	// dark trough, and from the deck the pool seems to run on into the sky.
	const float Trough = -HX + 25.f;
	BoxMM(EPal::PoolTile, FVector(PX0, -PY, PoolFloor - 30.f), FVector(PX1, PY, PoolFloor));
	BoxMM(EPal::PoolTile, FVector(PX1, -PY, PoolFloor - 30.f), FVector(PX1 + 30.f, PY, 0.f));
	BoxMM(EPal::PoolTile, FVector(Trough, -PY - 30.f, PoolFloor - 30.f), FVector(PX1 + 30.f, -PY, 0.f));
	BoxMM(EPal::PoolTile, FVector(Trough, PY, PoolFloor - 30.f), FVector(PX1 + 30.f, PY + 30.f, 0.f));
	BoxMM(EPal::MarbleDark, FVector(PX0 - 30.f, -PY, PoolFloor - 30.f), FVector(PX0, PY, WaterZ - 1.f));
	BoxMM(EPal::MarbleDark, FVector(Trough, -PY, -75.f), FVector(PX0 - 30.f, PY, -45.f));
	Deco(EPal::Water, FVector(Trough, -PY, -42.f), FVector(PX0 - 30.f, PY, -40.f));
	// Lane lines on the floor and coping round the deck side.
	for (float X = PX0 + 125.f; X < PX1; X += 250.f)
	{
		Deco(EPal::MarbleDark, FVector(X - 12.f, -PY + 200.f, PoolFloor), FVector(X + 12.f, PY - 200.f, PoolFloor + 0.5f));
	}
	Deco(EPal::Marble, FVector(PX1, -PY - 30.f, 0.f), FVector(PX1 + 45.f, PY + 30.f, 3.f));
	Deco(EPal::Marble, FVector(PX0, -PY - 45.f, 0.f), FVector(PX1 + 45.f, -PY, 3.f));
	Deco(EPal::Marble, FVector(PX0, PY, 0.f), FVector(PX1 + 45.f, PY + 45.f, 3.f));
	// The water itself: a dark, glassy sheet waist-deep on a wader (crouching in it hides a player from the deck).
	WaterBody(FBox(FVector(PX0, -PY, PoolFloor), FVector(PX1, PY, WaterZ)));
	// Steps down at both ends on the deck side.
	for (const float S : { -1.f, 1.f })
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const float Y0 = S * (PY - 60.f - 70.f * i);
			const float Y1 = S * (PY - 130.f - 70.f * i);
			BoxMM(EPal::PoolTile, FVector(PX1 - 260.f, FMath::Min(Y0, Y1), PoolFloor), FVector(PX1, FMath::Max(Y0, Y1), -20.f * (i + 1)));
		}
		// Underwater lamps along the long walls.
	}
	for (float Y = -PY + 400.f; Y < PY; Y += 800.f)
	{
		PointLight(FVector(PX1 - 20.f, Y, PoolFloor + 25.f), 900.f, 9000.f, 700.f, FLinearColor(0.55f, 0.9f, 1.f));
		Deco(EPal::LedCool, FVector(PX1 - 2.f, Y - 20.f, PoolFloor + 15.f), FVector(PX1, Y + 20.f, PoolFloor + 35.f));
	}

	// Hot tubs at both ends of the deck: raised round basins (good chest-high cover).
	for (const float S : { -1.f, 1.f })
	{
		const FVector C(-1900.f, S * 2950.f, 0.f);
		Cyl(EPal::Marble, C, 360.f, 75.f, true);
		Cyl(EPal::Water, C + FVector(0.f, 0.f, 75.f), 320.f, 1.f, false);
		PointLight(C + FVector(0.f, 0.f, 60.f), 500.f, 9000.f, 400.f, FLinearColor(0.6f, 0.9f, 1.f));
	}

	// Deck furniture: a row of loungers along the water, cabanas and umbrellas along the lounge side, planters
	// between them so the lane is broken into short sightlines.
	for (float Y = -2200.f; Y <= 2200.f; Y += 330.f)
	{
		if (FMath::Abs(Y) < 250.f)
		{
			continue;
		}
		Lounger(FVector(PX1 + 150.f, Y, 1.5f), 180.f);
	}
	for (const float S : { -1.f, 1.f })
	{
		Cabana(FVector(-1680.f, S * 900.f, 0.f), 0.f);
		Cabana(FVector(-1680.f, S * 2050.f, 0.f), 0.f);
		Planter(FVector(-2050.f, S * 1400.f - 140.f, 0.f), FVector(-1850.f, S * 1400.f + 140.f, 70.f), 11 + (int32)S);
		// Market umbrella over a table. The canopy is solid (nobody stands up into it off the table or jumps through
		// it, and the rain lands on it); from below it shows its ribs, the stretchers down to the runner on the pole
		// and a valance round the rim.
		const FVector U(-1980.f, S * 300.f, 0.f);
		const float RimZ = 226.f;
		const float RimR = 140.f;
		Cyl(EPal::SteelDark, U, 5.f, 268.f, true);
		Cone(EPal::Canvas, U + FVector(0.f, 0.f, RimZ), RimR * 2.f, 40.f, true);
		Sphere(EPal::Teak, U + FVector(0.f, 0.f, RimZ + 44.f), 7.f, false);
		Cyl(EPal::SteelDark, U + FVector(0.f, 0.f, RimZ - 6.f), 9.f, 6.f, false);
		Cyl(EPal::SteelDark, U + FVector(0.f, 0.f, 168.f), 7.f, 9.f, false);
		for (int32 k = 0; k < 8; ++k)
		{
			const float Ang = (k + 0.5f) * PI / 4.f;
			const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
			const FVector Hub = U + FVector(0.f, 0.f, RimZ - 2.f) + Dir * 5.f;
			const FVector Tip = U + FVector(0.f, 0.f, RimZ - 1.5f) + Dir * (RimR - 3.f);
			CylR(EPal::Teak, (Hub + Tip) * 0.5f, 1.6f, FVector::Dist(Hub, Tip), (Tip - Hub).Rotation() + FRotator(-90.f, 0.f, 0.f), false);
			const FVector Runner = U + FVector(0.f, 0.f, 172.f) + Dir * 3.f;
			const FVector Mid = U + FVector(0.f, 0.f, RimZ - 2.f) + Dir * (RimR * 0.42f);
			CylR(EPal::Teak, (Runner + Mid) * 0.5f, 1.2f, FVector::Dist(Runner, Mid), (Mid - Runner).Rotation() + FRotator(-90.f, 0.f, 0.f), false);
			// Valance: one flap per panel, hanging from the rim between two ribs.
			const float FlapAng = (k + 1.f) * PI / 4.f;
			const FVector FlapDir(FMath::Cos(FlapAng), FMath::Sin(FlapAng), 0.f);
			Box(EPal::Canvas, U + FlapDir * (RimR * 0.93f) + FVector(0.f, 0.f, RimZ - 5.f), FVector(1.f, 2.f * RimR * 0.93f * FMath::Tan(PI / 8.f) + 1.f, 11.f), false, FMath::RadiansToDegrees(FlapAng));
		}
		// The crank that opens it.
		Box(EPal::SteelDark, U + FVector(4.f, 0.f, 140.f), FVector(6.f, 2.f, 2.f), false, 0.f);
		Cyl(EPal::Teak, U, 90.f, 72.f, true);
	}
	// Towel stand / pool bar kiosk at the middle of the deck: a hard piece of cover between the two halves.
	BoxMM(EPal::Teak, FVector(-1960.f, -160.f, 0.f), FVector(-1760.f, 160.f, 110.f));
	Deco(EPal::MarbleDark, FVector(-1975.f, -175.f, 110.f), FVector(-1745.f, 175.f, 116.f));
	Deco(EPal::LedWarm, FVector(-1962.f, -150.f, 104.f), FVector(-1958.f, 150.f, 108.f));

	// Festoon lights strung over the deck on steel posts.
	for (float Y = -2400.f; Y <= 2400.f; Y += 800.f)
	{
		const float PostA = PX1 + 60.f;
		const float PostB = LX0 - 230.f;
		Cyl(EPal::SteelDark, FVector(PostA, Y, 0.f), 10.f, 330.f, true);
		Cyl(EPal::SteelDark, FVector(PostB, Y, 0.f), 10.f, 330.f, true);
		// The cable sags between the posts and every bulb hangs from it on its socket.
		auto CableZ = [&](float X) { return 322.f - 24.f * FMath::Sin((X - PostA) / (PostB - PostA) * PI); };
		FVector Prev(PostA, Y, CableZ(PostA));
		for (float X = PostA + 90.f; ; X += 90.f)
		{
			const float Xc = FMath::Min(X, PostB);
			const FVector Next(Xc, Y, CableZ(Xc));
			CylR(EPal::SteelDark, (Prev + Next) * 0.5f, 1.2f, FVector::Dist(Prev, Next) + 0.5f, (Next - Prev).Rotation() + FRotator(-90.f, 0.f, 0.f), false);
			if (Xc < PostB - 30.f)
			{
				Cyl(EPal::SteelDark, Next - FVector(0.f, 0.f, 6.f), 2.6f, 6.f, false);
				Sphere(EPal::LedWarm, Next - FVector(0.f, 0.f, 10.f), 9.f, false);
			}
			Prev = Next;
			if (Xc >= PostB)
			{
				break;
			}
		}
		PointLight(FVector((PX1 + LX0 - 170.f) * 0.5f, Y, 290.f), 700.f, 2700.f, 900.f);
	}
	Rect(PX0, -PY, PX1, PY, 1);
	Rect(-1830.f, 750.f, -1530.f, 1050.f, 2);
	Rect(-1830.f, -1050.f, -1530.f, -750.f, 2);
	Rect(-1830.f, 1900.f, -1530.f, 2200.f, 2);
	Rect(-1830.f, -2200.f, -1530.f, -1900.f, 2);
}

void FCampusSchoolBuildContext::Lounger(const FVector& Base, float Yaw)
{
	const FRotator R(0.f, Yaw, 0.f);
	auto At = [&](const FVector& L) { return Base + R.RotateVector(L); };
	// Low teak frame, cushion, raised back rest; low enough to shoot over, solid enough to stop a crouched player.
	Box(EPal::Teak, At(FVector(0.f, 0.f, 18.f)), FVector(190.f, 70.f, 12.f), true, Yaw);
	Box(EPal::Canvas, At(FVector(-10.f, 0.f, 29.f)), FVector(160.f, 64.f, 10.f), false, Yaw);
	BoxR(EPal::Canvas, At(FVector(72.f, 0.f, 50.f)), FVector(55.f, 64.f, 10.f), FRotator(40.f, Yaw, 0.f), false);
	for (const float Sx : { -85.f, 85.f })
	{
		for (const float Sy : { -30.f, 30.f })
		{
			Box(EPal::Teak, At(FVector(Sx, Sy, 6.f)), FVector(6.f, 6.f, 12.f), false, Yaw);
		}
	}
}

void FCampusSchoolBuildContext::Cabana(const FVector& Center, float Yaw)
{
	// Teak frame, canvas roof and back curtain; a daybed inside. Its back and sides block sight, the front is open.
	const float W = 300.f;
	const float D = 250.f;
	for (const float Sx : { -1.f, 1.f })
	{
		for (const float Sy : { -1.f, 1.f })
		{
			Box(EPal::Teak, Center + FVector(Sx * (D * 0.5f - 8.f), Sy * (W * 0.5f - 8.f), 140.f), FVector(14.f, 14.f, 280.f), true, Yaw);
		}
	}
	Box(EPal::Canvas, Center + FVector(0.f, 0.f, 286.f), FVector(D + 30.f, W + 30.f, 12.f), false, Yaw);
	Box(EPal::Canvas, Center + FVector(D * 0.5f - 4.f, 0.f, 145.f), FVector(4.f, W - 20.f, 270.f), true, Yaw);
	// 侧帘与背帘同样遮挡视线，必须提供实体碰撞供枪体避障和弹道查询
	Box(EPal::Canvas, Center + FVector(0.f, -W * 0.5f + 4.f, 145.f), FVector(D - 20.f, 4.f, 270.f), true, Yaw);
	Box(EPal::Canvas, Center + FVector(0.f, W * 0.5f - 4.f, 145.f), FVector(D - 20.f, 4.f, 270.f), true, Yaw);
	Box(EPal::Teak, Center + FVector(20.f, 0.f, 22.f), FVector(D - 70.f, W - 60.f, 44.f), true, Yaw);
	Box(EPal::Canvas, Center + FVector(20.f, 0.f, 50.f), FVector(D - 80.f, W - 70.f, 12.f), false, Yaw);
}

void FCampusSchoolBuildContext::Planter(const FVector& Min, const FVector& Max, int32 Seed)
{
	BoxMM(EPal::Teak, Min, Max);
	Deco(EPal::Soil, FVector(Min.X + 6.f, Min.Y + 6.f, Max.Z), FVector(Max.X - 6.f, Max.Y - 6.f, Max.Z + 2.f));
	const FBox2D Area(FVector2D(Min.X + 15.f, Min.Y + 15.f), FVector2D(Max.X - 15.f, Max.Y - 15.f));
	// A trimmed bed of real shrubs filling the trough (leafy and uneven, not a box), ornamental grass and taller shrubs
	// coming up through it and spilling over the edges.
	static const TCHAR* const Grass[] = { TEXT("mid_a_LOD0"), TEXT("mid_b_LOD0"), TEXT("large_a_LOD0"), TEXT("tall_a_LOD0"), TEXT("tall_b_LOD0") };
	static const TCHAR* const Shrubs[] = { TEXT("_a"), TEXT("_b"), TEXT("_c"), TEXT("_d") };
	if (HasProp(TEXT("shrub_02")))
	{
		Plant(TEXT("shrub_02"), Shrubs, Area, Max.Z + 2.f, 48.f, 38.f, 55.f, Seed + 7, true, 18.f);
	}
	else
	{
		Deco(EPal::Hedge, FVector(Min.X + 12.f, Min.Y + 12.f, Max.Z), FVector(Max.X - 12.f, Max.Y - 12.f, Max.Z + 35.f));
	}
	Plant(TEXT("grass_medium_01"), Grass, Area, Max.Z + 2.f, 55.f, 45.f, 75.f, Seed, true, 40.f);
	Plant(TEXT("shrub_02"), Shrubs, Area, Max.Z + 2.f, 150.f, 70.f, 100.f, Seed + 3, true, 50.f);
}

// ------------------------------------------------------------------ middle: the sky lounge

void FCampusSchoolBuildContext::HotelLounge()
{
	// Floor, ceiling and roof slab.
	Deco(EPal::Marble, FVector(LX0, -LY, 0.f), FVector(KitchenX, LY, 2.f));
	BoxMM(EPal::Concrete, FVector(LX0 - 10.f, -LY - 10.f, LH), FVector(LX1 + 10.f, LY + 10.f, LRoof));
	Deco(EPal::WallWhite, FVector(LX0, -LY, LH - 14.f), FVector(LX1, LY, LH - 1.f));

	// Glazed walls: wide open doorways on every side (no doors to fight through, just thresholds).
	const float Zg = LH - 14.f;
	GlassWallX(-LY, LX0, LX1, 0.f, Zg, 260.f, { { -1150.f, -800.f, 0.f, 260.f, false }, { 800.f, 1150.f, 0.f, 260.f, false } }, EPal::SteelDark);
	GlassWallX(LY, LX0, LX1, 0.f, Zg, 260.f, { { -1150.f, -800.f, 0.f, 260.f, false }, { 800.f, 1150.f, 0.f, 260.f, false } }, EPal::SteelDark);
	GlassWallY(LX0, -LY, LY, 0.f, Zg, 260.f, { { -1200.f, -900.f, 0.f, 260.f, false }, { 900.f, 1200.f, 0.f, 260.f, false } }, EPal::SteelDark);
	// The east doorways sit at the far ends of the wall, under the pad stairs' landings, not under their flights.
	GlassWallY(LX1, -LY, -900.f, 0.f, Zg, 260.f, { { -1760.f, -1500.f, 0.f, 260.f, false } }, EPal::SteelDark);
	GlassWallY(LX1, 900.f, LY, 0.f, Zg, 260.f, { { 1500.f, 1760.f, 0.f, 260.f, false } }, EPal::SteelDark);

	// Kitchen block (back of house): solid walls, swing doors from the bar, a service door out to the plant deck.
	WallY(EPal::WallWhite, KitchenX, -900.f, 900.f, 0.f, Zg, 20.f, { { -520.f, -400.f, 0.f, 215.f, false }, { 400.f, 520.f, 0.f, 215.f, false } }, EPal::MarbleDark, -1.f);
	WallY(EPal::WallWhite, LX1 - 10.f, -900.f, 900.f, 0.f, Zg, 20.f, { { -120.f, 0.f, 0.f, 215.f, false } }, EPal::TowerDark, 1.f);
	WallX(EPal::WallWhite, -900.f, KitchenX, LX1, 0.f, Zg, 20.f, {}, EPal::MarbleDark, -1.f);
	WallX(EPal::WallWhite, 900.f, KitchenX, LX1, 0.f, Zg, 20.f, {}, EPal::MarbleDark, 1.f);
	ForEachOutsideHoles(KitchenX, -900.f, LX1 - 20.f, 900.f, [this](float X0, float Y0, float X1, float Y1)
	{
		Deco(EPal::Terrazzo, FVector(X0, Y0, 0.f), FVector(X1, Y1, 2.f));
	});
	// Stainless lines: a cooking range along the east wall, a prep island, shelving; the stair down at the north end.
	BoxMM(EPal::Stainless, FVector(LX1 - 90.f, -860.f, 0.f), FVector(LX1 - 25.f, -160.f, 92.f));
	BoxMM(EPal::Stainless, FVector(LX1 - 90.f, 40.f, 0.f), FVector(LX1 - 25.f, 280.f, 92.f));
	BoxMM(EPal::Stainless, FVector(900.f, -600.f, 0.f), FVector(1060.f, -100.f, 92.f));
	BoxMM(EPal::Black, FVector(LX1 - 85.f, -840.f, 92.f), FVector(LX1 - 30.f, -600.f, 95.f), false);
	BoxMM(EPal::Stainless, FVector(LX1 - 70.f, -860.f, 200.f), FVector(LX1 - 20.f, -160.f, 260.f), false);
	BoxMM(EPal::Shelf, FVector(KitchenX + 15.f, -860.f, 0.f), FVector(KitchenX + 60.f, -620.f, 200.f));
	Fixture(FVector(1000.f, -500.f, Zg), false, 4500.f);
	Fixture(FVector(1000.f, 200.f, Zg), false, 4500.f);
	Stair(FBox2D(FVector2D(800.f, 300.f), FVector2D(1250.f, 900.f)), false, false, true, LowFloor, 0.f);
	WallY(EPal::WallWhite, 795.f, 420.f, 900.f, 0.f, Zg, 10.f, {}, EPal::Count);
	Sign(FVector(KitchenX - 12.f, 460.f, 245.f), 180.f, 90.f, 26.f, TEXT("員工通道"), FLinearColor(0.08f, 0.09f, 0.1f), FLinearColor(0.95f, 0.85f, 0.6f), 2, false, 0.6f);

	// Island bar under a brass ring chandelier: counter round a lit back bar of bottles, stools round it.
	const FVector Bar(-350.f, 0.f, 0.f);
	BoxMM(EPal::MarbleDark, Bar + FVector(-420.f, -280.f, 0.f), Bar + FVector(420.f, -220.f, 108.f));
	BoxMM(EPal::MarbleDark, Bar + FVector(-420.f, 220.f, 0.f), Bar + FVector(420.f, 280.f, 108.f));
	BoxMM(EPal::MarbleDark, Bar + FVector(-420.f, -220.f, 0.f), Bar + FVector(-360.f, 220.f, 108.f));
	BoxMM(EPal::MarbleDark, Bar + FVector(360.f, -220.f, 0.f), Bar + FVector(420.f, 220.f, 108.f));
	Deco(EPal::Marble, Bar + FVector(-430.f, -290.f, 108.f), Bar + FVector(430.f, 290.f, 114.f));
	Deco(EPal::LedWarm, Bar + FVector(-418.f, -282.f, 96.f), Bar + FVector(418.f, -280.f, 99.f));
	Deco(EPal::LedWarm, Bar + FVector(-418.f, 280.f, 96.f), Bar + FVector(418.f, 282.f, 99.f));
	BoxMM(EPal::Shelf, Bar + FVector(-220.f, -60.f, 0.f), Bar + FVector(220.f, 60.f, 230.f));
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Z = 120.f + Row * 38.f;
		Deco(EPal::Brass, Bar + FVector(-222.f, -62.f, Z - 3.f), Bar + FVector(222.f, 62.f, Z));
		for (float X = -200.f; X <= 200.f; X += 18.f)
		{
			for (const float Sy : { -1.f, 1.f })
			{
				const float H2 = 22.f + 8.f * HRand((int32)X, Row);
				Cyl(EPal::Bottle, Bar + FVector(X, Sy * 52.f, Z), 7.f, H2, false);
			}
		}
	}
	for (float X = -380.f; X <= 380.f; X += 95.f)
	{
		for (const float Sy : { -1.f, 1.f })
		{
			const FVector S = Bar + FVector(X, Sy * 345.f, 0.f);
			Cyl(EPal::SteelDark, S, 8.f, 70.f, false);
			Cyl(EPal::Leather, S + FVector(0.f, 0.f, 70.f), 40.f, 8.f, false);
		}
	}
	{
		const FVector Ring = Bar + FVector(0.f, 0.f, 330.f);
		for (int32 i = 0; i < 24; ++i)
		{
			const float A = i / 24.f * 2.f * PI;
			const float A2 = (i + 1) / 24.f * 2.f * PI;
			const FVector P0 = Ring + FVector(FMath::Cos(A) * 360.f, FMath::Sin(A) * 220.f, 0.f);
			const FVector P1 = Ring + FVector(FMath::Cos(A2) * 360.f, FMath::Sin(A2) * 220.f, 0.f);
			const FVector Mid = (P0 + P1) * 0.5f;
			const float Len = FVector::Dist(P0, P1);
			BoxR(EPal::Brass, Mid, FVector(Len + 2.f, 8.f, 8.f), FRotator(0.f, (P1 - P0).Rotation().Yaw, 0.f), false);
			BoxR(EPal::LedWarm, Mid - FVector(0.f, 0.f, 5.f), FVector(Len, 4.f, 2.f), FRotator(0.f, (P1 - P0).Rotation().Yaw, 0.f), false);
		}
		for (const float Sx : { -1.f, 1.f })
		{
			CylR(EPal::Brass, Ring + FVector(Sx * 250.f, 0.f, 45.f), 2.f, 90.f, FRotator::ZeroRotator, false);
		}
		PointLight(Bar + FVector(0.f, 0.f, 300.f), 3500.f, 2600.f, 1100.f);
	}

	// Booths along the west glass: high leather backs (cover from the deck), a table in each.
	for (const float Y : { -1500.f, -600.f, 600.f, 1500.f })
	{
		const float X = LX0 + 20.f;
		// Channel-tufted high back on a dark plinth, a loose seat cushion, rolled ends.
		BoxMM(EPal::SteelDark, FVector(X, Y - 200.f, 0.f), FVector(X + 120.f, Y + 200.f, 12.f));
		BoxMM(EPal::Leather, FVector(X, Y - 200.f, 12.f), FVector(X + 45.f, Y + 200.f, 125.f));
		for (float C = Y - 180.f; C < Y + 180.f; C += 36.f)
		{
			Deco(EPal::Leather, FVector(X + 45.f, C + 2.f, 48.f), FVector(X + 58.f, C + 34.f, 120.f));
		}
		Deco(EPal::Brass, FVector(X - 1.f, Y - 201.f, 123.f), FVector(X + 46.f, Y + 201.f, 126.f));
		Deco(EPal::Leather, FVector(X + 45.f, Y - 150.f, 12.f), FVector(X + 120.f, Y + 150.f, 44.f));
		for (const float Sy : { -1.f, 1.f })
		{
			BoxMM(EPal::Leather, FVector(X + 45.f, Sy < 0.f ? Y - 200.f : Y + 150.f, 12.f), FVector(X + 220.f, Sy < 0.f ? Y - 150.f : Y + 200.f, 95.f));
			CylR(EPal::Leather, FVector(X + 132.f, Y + Sy * 175.f, 95.f), 50.f, 175.f, FRotator(90.f, 0.f, 0.f), false);
		}
		Cyl(EPal::MarbleDark, FVector(X + 160.f, Y, 0.f), 110.f, 74.f, true);
		PointLight(FVector(X + 150.f, Y, 260.f), 400.f, 2400.f, 450.f);
		Cyl(EPal::Brass, FVector(X + 150.f, Y, 250.f), 34.f, 26.f, false);
		CylR(EPal::Black, FVector(X + 150.f, Y, 340.f), 1.f, 150.f, FRotator::ZeroRotator, false);
	}
	// Lounge sofas and a grand piano at the south end, a long private dining table at the north end, high tables
	// and four stone columns in between: the room is broken into short, fast angles.
	Sofa(FVector(-150.f, -1605.f, 0.f), 90.f, 600.f);
	Sofa(FVector(-150.f, -1255.f, 0.f), -90.f, 600.f);
	Box(EPal::MarbleDark, FVector(-150.f, -1430.f, 22.f), FVector(240.f, 110.f, 44.f), true);
	Box(EPal::Black, FVector(450.f, -1450.f, 50.f), FVector(240.f, 150.f, 100.f), true);
	BoxR(EPal::Black, FVector(450.f, -1360.f, 135.f), FVector(230.f, 6.f, 110.f), FRotator(0.f, 0.f, -35.f), false);
	BoxMM(EPal::Teak, FVector(-650.f, 1380.f, 0.f), FVector(150.f, 1520.f, 76.f));
	for (float X = -600.f; X <= 100.f; X += 100.f)
	{
		for (const float Sy : { 1330.f, 1570.f })
		{
			Box(EPal::Velvet, FVector(X, Sy, 45.f), FVector(48.f, 48.f, 90.f), false);
		}
	}
	for (const FVector& T : { FVector(-950.f, -300.f, 0.f), FVector(-950.f, 300.f, 0.f), FVector(400.f, -700.f, 0.f), FVector(400.f, 700.f, 0.f), FVector(150.f, -1000.f, 0.f), FVector(150.f, 1000.f, 0.f) })
	{
		Cyl(EPal::SteelDark, T, 10.f, 100.f, false);
		Cyl(EPal::MarbleDark, T + FVector(0.f, 0.f, 100.f), 70.f, 4.f, true);
	}
	for (const FVector& C : { FVector(-700.f, -950.f, 0.f), FVector(-700.f, 950.f, 0.f), FVector(150.f, -500.f, 0.f), FVector(150.f, 500.f, 0.f) })
	{
		BoxMM(EPal::Stone2, C - FVector(40.f, 40.f, 0.f), C + FVector(40.f, 40.f, Zg));
		Deco(EPal::Brass, C - FVector(42.f, 42.f, -Zg + 30.f), C + FVector(42.f, 42.f, Zg - 20.f));
	}
	// Recessed warm downlights across the ceiling.
	for (float X = -1100.f; X <= 500.f; X += 530.f)
	{
		for (float Y = -1500.f; Y <= 1500.f; Y += 600.f)
		{
			if (FMath::Abs(X + 300.f) < 450.f && FMath::Abs(Y) < 350.f)
			{
				continue;
			}
			Cyl(EPal::Brass, FVector(X, Y, Zg - 3.f), 24.f, 2.f, false);
			Cyl(EPal::LedWarm, FVector(X, Y, Zg - 4.f), 16.f, 1.f, false);
			// Neutral-warm downlight against the warmer cove and lamps, so the room is not one orange.
			PointLight(FVector(X, Y, Zg - 25.f), 650.f, 3500.f, 650.f);
		}
	}
	// The lounge's name board, hung inside the west glazing on two brass rods: a smoked-bronze stone panel in a thin
	// brass frame, the lettering brass and softly backlit, and a picture light washing it from the ceiling, so it reads
	// as an object in the room rather than a glowing card.
	{
		const float SX = LX0 + 14.f;
		const float SZ = 300.f;
		const float HalfW = 172.f;
		const float HalfH = 42.f;
		Sign(FVector(SX, 0.f, SZ), 0.f, HalfW * 2.f - 6.f, HalfH * 2.f - 6.f, TEXT("SKY LOUNGE  天際"), FLinearColor(0.032f, 0.027f, 0.022f), FLinearColor(0.86f, 0.62f, 0.32f), 0, false, 0.55f, 4.f, 4.f);
		Deco(EPal::MarbleDark, FVector(SX - 4.f, -HalfW, SZ - HalfH), FVector(SX - 2.f, HalfW, SZ + HalfH));
		for (const float S : { -1.f, 1.f })
		{
			Deco(EPal::Brass, FVector(SX - 4.f, -HalfW - 1.5f, SZ + S * HalfH - 1.5f), FVector(SX + 3.f, HalfW + 1.5f, SZ + S * HalfH + 1.5f));
			Deco(EPal::Brass, FVector(SX - 4.f, S * HalfW - 1.5f, SZ - HalfH), FVector(SX + 3.f, S * HalfW + 1.5f, SZ + HalfH));
			Cyl(EPal::Brass, FVector(SX - 1.f, S * (HalfW - 30.f), SZ + HalfH + 1.5f), 1.2f, Zg - SZ - HalfH - 1.5f, false);
			Cyl(EPal::Brass, FVector(SX - 1.f, S * (HalfW - 30.f), Zg - 3.f), 6.f, 3.f, false);
		}
		SpotLight(FVector(SX + 70.f, 0.f, Zg - 8.f), FRotator(-62.f, 180.f, 0.f), 420.f, 2900.f, 320.f, 42.f);
	}
	HotelLoungeFinish(Bar, Zg);

	Rect(LX0, -LY, LX1, LY, 0);
}

void FCampusSchoolBuildContext::Sofa(const FVector& Base, float Yaw, float Width)
{
	// Velvet sofa on a recessed dark plinth: a deep base, three seat cushions, back cushions leaning into a frame,
	// square arms. Yaw is the way the sitter faces. The base, back and arms stop people and rounds (low cover).
	const FRotator R(0.f, Yaw, 0.f);
	const FVector F = R.Vector();
	const FVector Side = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
	auto At = [&](float Fwd, float Right, float Z) { return Base + F * Fwd + Side * Right + FVector(0.f, 0.f, Z); };
	Box(EPal::SteelDark, At(-2.f, 0.f, 5.f), FVector(78.f, Width - 24.f, 10.f), false, Yaw);
	Box(EPal::Velvet, At(0.f, 0.f, 25.f), FVector(88.f, Width, 30.f), true, Yaw);
	Box(EPal::Velvet, At(-37.f, 0.f, 48.f), FVector(16.f, Width, 76.f), true, Yaw);
	const int32 N = 3;
	const float Inner = Width - 44.f;
	const float Cw = Inner / N;
	for (int32 i = 0; i < N; ++i)
	{
		const float Off = -Inner * 0.5f + Cw * (i + 0.5f);
		Box(EPal::Velvet, At(8.f, Off, 47.f), FVector(64.f, Cw - 3.f, 14.f), false, Yaw);
		BoxR(EPal::Velvet, At(-22.f, Off, 70.f), FVector(18.f, Cw - 4.f, 48.f), FRotator(10.f, Yaw, 0.f), false);
	}
	for (const float S : { -1.f, 1.f })
	{
		Box(EPal::Velvet, At(0.f, S * (Width * 0.5f - 11.f), 41.f), FVector(88.f, 22.f, 62.f), true, Yaw);
	}
	// A pair of scatter cushions.
	BoxR(EPal::Canvas, At(-8.f, -Inner * 0.38f, 66.f), FVector(14.f, 40.f, 40.f), FRotator(14.f, Yaw + 12.f, 0.f), false);
	BoxR(EPal::Canvas, At(-8.f, Inner * 0.38f, 66.f), FVector(14.f, 40.f, 40.f), FRotator(14.f, Yaw - 10.f, 0.f), false);
}

void FCampusSchoolBuildContext::HotelLoungeFinish(const FVector& Bar, float Zg)
{
	// Ceiling: a plaster soffit dropped round the glass (it hides the curtain-wall heads) with a cove on its inner
	// edge washing the raised centre with warm light; a teak slat raft floats over the bar.
	const float Sb = Zg - 55.f;
	const float St = Zg - 22.f;
	auto Soffit = [&](const FVector& Min, const FVector& Max, const FVector& LipMin, const FVector& LipMax)
	{
		BoxMM(EPal::WallWhite, FVector(Min.X, Min.Y, Sb), FVector(Max.X, Max.Y, St), false);
		Deco(EPal::WallWhite, FVector(LipMin.X, LipMin.Y, St), FVector(LipMax.X, LipMax.Y, St + 8.f));
		Deco(EPal::LedWarm, FVector(LipMin.X, LipMin.Y, St), FVector(LipMax.X, LipMax.Y, St + 1.f));
	};
	for (const float S : { -1.f, 1.f })
	{
		const float Yo = S * LY;
		const float Yi = S * (LY - 130.f);
		Soffit(FVector(LX0, FMath::Min(Yo, Yi), 0.f), FVector(LX1, FMath::Max(Yo, Yi), 0.f),
			FVector(LX0 + 130.f, FMath::Min(Yi, Yi - S * 4.f), 0.f), FVector(LX1 - 130.f, FMath::Max(Yi, Yi - S * 4.f), 0.f));
		for (float X = -1050.f; X <= 1050.f; X += 525.f)
		{
			PointLight(FVector(X, S * (LY - 150.f), Zg - 10.f), 450.f, 2700.f, 450.f);
		}
		// The east glass either side of the kitchen.
		Soffit(FVector(LX1 - 130.f, S < 0.f ? -LY + 130.f : 900.f, 0.f), FVector(LX1, S < 0.f ? -900.f : LY - 130.f, 0.f),
			FVector(LX1 - 134.f, S < 0.f ? -LY + 130.f : 900.f, 0.f), FVector(LX1 - 130.f, S < 0.f ? -900.f : LY - 130.f, 0.f));
	}
	Soffit(FVector(LX0, -LY + 130.f, 0.f), FVector(LX0 + 130.f, LY - 130.f, 0.f), FVector(LX0 + 130.f, -LY + 130.f, 0.f), FVector(LX0 + 134.f, LY - 130.f, 0.f));
	for (float Y = -1400.f; Y <= 1400.f; Y += 700.f)
	{
		PointLight(FVector(LX0 + 150.f, Y, Zg - 10.f), 450.f, 2700.f, 450.f);
	}
	for (float Y = -290.f; Y <= 290.f; Y += 20.f)
	{
		Deco(EPal::Teak, Bar + FVector(-430.f, Y - 3.5f, Zg - 42.f), Bar + FVector(430.f, Y + 3.5f, Zg - 32.f));
	}
	Deco(EPal::SteelDark, Bar + FVector(-440.f, -300.f, Zg - 32.f), Bar + FVector(440.f, 300.f, Zg - 26.f));

	// The bar itself: fluted teak fronts, a brass foot rail, pendants over the counter.
	for (const float Sy : { -1.f, 1.f })
	{
		for (float X = -414.f; X <= 414.f; X += 12.f)
		{
			Deco(EPal::Teak, Bar + FVector(X - 2.f, Sy * 282.f - 1.5f, 8.f), Bar + FVector(X + 2.f, Sy * 282.f + 1.5f, 100.f));
		}
		Deco(EPal::Brass, Bar + FVector(-420.f, Sy * 281.f - 2.f, 0.f), Bar + FVector(420.f, Sy * 281.f + 2.f, 8.f));
		CylR(EPal::Brass, Bar + FVector(0.f, Sy * 300.f, 22.f), 5.f, 800.f, FRotator(90.f, 0.f, 0.f), false);
		for (const float X : { -380.f, 0.f, 380.f })
		{
			Cyl(EPal::Brass, Bar + FVector(X, Sy * 300.f, 0.f), 4.f, 22.f, false);
		}
		for (const float X : { -300.f, 0.f, 300.f })
		{
			const FVector P = Bar + FVector(X, Sy * 250.f, 0.f);
			CylR(EPal::Black, P + FVector(0.f, 0.f, (244.f + Zg - 26.f) * 0.5f), 0.8f, Zg - 26.f - 244.f, FRotator::ZeroRotator, false);
			Cone(EPal::Brass, P + FVector(0.f, 0.f, 218.f), 30.f, 26.f, false);
			Sphere(EPal::LedWarm, P + FVector(0.f, 0.f, 220.f), 9.f, false);
		}
	}
	PointLight(Bar + FVector(0.f, -250.f, 205.f), 500.f, 2500.f, 420.f);
	PointLight(Bar + FVector(0.f, 250.f, 205.f), 500.f, 2500.f, 420.f);

	// Wool rugs under the sofa group and the dining table soften the marble.
	Deco(EPal::Carpet, FVector(-560.f, -1720.f, 2.f), FVector(260.f, -1140.f, 3.f));
	Deco(EPal::Carpet, FVector(-720.f, 1300.f, 2.f), FVector(220.f, 1600.f, 3.f));
	// Linen drapes gathered at the corners and beside the west doorways, hanging in folds from the soffit.
	auto Drape = [&](float X, float Y, bool bAlongX)
	{
		for (int32 k = 0; k < 4; ++k)
		{
			const float O = (k - 1.5f) * 18.f;
			const float D = (k % 2) ? 6.f : -2.f;
			const FVector C = bAlongX ? FVector(X + O, Y + D, 0.f) : FVector(X + D, Y + O, 0.f);
			Box(EPal::Canvas, C + FVector(0.f, 0.f, (Sb - 4.f) * 0.5f), bAlongX ? FVector(22.f, 10.f, Sb - 4.f) : FVector(10.f, 22.f, Sb - 4.f), false);
		}
	};
	Drape(LX0 + 50.f, -LY + 18.f, true);
	Drape(LX0 + 50.f, LY - 18.f, true);
	Drape(LX0 + 18.f, -1260.f, false);
	Drape(LX0 + 18.f, -840.f, false);
	Drape(LX0 + 18.f, 840.f, false);
	Drape(LX0 + 18.f, 1260.f, false);
	// Big potted olive trees by the columns and in the corners, floor lamps by the sofas.
	int32 PotSeed = 70;
	for (const FVector& P : { FVector(-1130.f, -1640.f, 0.f), FVector(-1130.f, 1640.f, 0.f), FVector(560.f, -1640.f, 0.f), FVector(560.f, 1640.f, 0.f), FVector(-700.f, -1080.f, 0.f), FVector(-700.f, 1080.f, 0.f) })
	{
		Cyl(EPal::Stone2, P, 70.f, 62.f, true);
		Deco(EPal::Soil, P + FVector(-28.f, -28.f, 56.f), P + FVector(28.f, 28.f, 60.f));
		static const TCHAR* const Shrubs[] = { TEXT("_a"), TEXT("_b"), TEXT("_c"), TEXT("_d") };
		Plant(TEXT("shrub_02"), Shrubs, FBox2D(FVector2D(P.X - 30.f, P.Y - 30.f), FVector2D(P.X + 30.f, P.Y + 30.f)), 58.f, 60.f, 110.f, 140.f, PotSeed++, true, 45.f);
	}
	for (const float X : { -520.f, 220.f })
	{
		const FVector L(X, -1430.f, 0.f);
		Cyl(EPal::Brass, L, 34.f, 3.f, false);
		Cyl(EPal::Brass, L, 3.f, 150.f, false);
		Cyl(EPal::Canvas, L + FVector(0.f, 0.f, 145.f), 44.f, 34.f, false);
		PointLight(L + FVector(0.f, 0.f, 160.f), 700.f, 2600.f, 500.f);
	}
	// On the coffee table: a stack of books and a bowl.
	Box(EPal::Leather, FVector(-210.f, -1440.f, 48.f), FVector(32.f, 24.f, 8.f), false);
	Box(EPal::Canvas, FVector(-212.f, -1438.f, 55.f), FVector(28.f, 22.f, 6.f), false);
	Cyl(EPal::Brass, FVector(-80.f, -1420.f, 44.f), 30.f, 10.f, false);
}

void FCampusSchoolBuildContext::HotelHelipad()
{
	const float Z = LRoof;
	// Pad surface with the yellow touchdown circle, the H, and an edge curb low enough to vault.
	Deco(EPal::HeliDeck, FVector(LX0, -LY, Z), FVector(LX1, LY, Z + 1.f));
	for (int32 i = 0; i < 48; ++i)
	{
		const float A = i / 48.f * 2.f * PI;
		const float A2 = (i + 1) / 48.f * 2.f * PI;
		const FVector P0(FMath::Cos(A) * 850.f, FMath::Sin(A) * 850.f, Z + 1.f);
		const FVector P1(FMath::Cos(A2) * 850.f, FMath::Sin(A2) * 850.f, Z + 1.f);
		BoxR(EPal::HeliYellow, (P0 + P1) * 0.5f, FVector(FVector::Dist(P0, P1) + 2.f, 45.f, 1.f), FRotator(0.f, (P1 - P0).Rotation().Yaw, 0.f), false);
		if (i % 2 == 0)
		{
			Cyl(EPal::LedWarm, FVector(FMath::Cos(A) * 1000.f, FMath::Sin(A) * 1000.f, Z), 14.f, 6.f, false);
		}
	}
	Deco(EPal::HeliWhite, FVector(-250.f, -350.f, Z + 1.5f), FVector(-170.f, 350.f, Z + 2.f));
	Deco(EPal::HeliWhite, FVector(170.f, -350.f, Z + 1.5f), FVector(250.f, 350.f, Z + 2.f));
	Deco(EPal::HeliWhite, FVector(-170.f, -40.f, Z + 1.5f), FVector(170.f, 40.f, Z + 2.f));
	BoxMM(EPal::Concrete, FVector(LX0 - 10.f, -LY - 10.f, Z), FVector(LX1 + 10.f, -LY + 20.f, Z + 45.f));
	BoxMM(EPal::Concrete, FVector(LX0 - 10.f, LY - 20.f, Z), FVector(LX1 + 10.f, LY + 10.f, Z + 45.f));
	BoxMM(EPal::Concrete, FVector(LX0 - 10.f, -LY, Z), FVector(LX0 + 20.f, LY, Z + 45.f));
	BoxMM(EPal::Concrete, FVector(LX1 - 20.f, -LY, Z), FVector(LX1 + 10.f, -1680.f, Z + 45.f));
	BoxMM(EPal::Concrete, FVector(LX1 - 20.f, -1520.f, Z), FVector(LX1 + 10.f, 1520.f, Z + 45.f));
	BoxMM(EPal::Concrete, FVector(LX1 - 20.f, 1680.f, Z), FVector(LX1 + 10.f, LY, Z + 45.f));
	// Safety netting round the pad: frames bolted to the slab edge, raking up and out, with the net between them.
	for (const float S : { -1.f, 1.f })
	{
		const FRotator Rake(0.f, 0.f, -S * 10.f);
		const FVector Mid(0.f, S * (LY + 10.f + 74.f), Z + 10.f + 13.f);
		for (float X = LX0; X <= LX1; X += 260.f)
		{
			BoxR(EPal::Galvanized, FVector(X, Mid.Y, Mid.Z), FVector(4.f, 150.f, 4.f), Rake, false);
		}
		BoxR(EPal::GrilleMesh, Mid, FVector(LX1 - LX0, 148.f, 1.f), Rake, false);
	}
	// Cover on the pad: two rooftop AC units and skylight boxes near the edges, the windsock mast in a corner.
	BoxMM(EPal::HvacGrey, FVector(-1150.f, -1450.f, Z), FVector(-850.f, -1150.f, Z + 140.f));
	BoxMM(EPal::HvacGrey, FVector(850.f, 1150.f, Z), FVector(1150.f, 1450.f, Z + 140.f));
	Cyl(EPal::Galvanized, FVector(-1000.f, -1300.f, Z + 140.f), 160.f, 20.f, false);
	Cyl(EPal::Galvanized, FVector(1000.f, 1300.f, Z + 140.f), 160.f, 20.f, false);
	for (const FVector& C : { FVector(950.f, -1300.f, Z), FVector(-950.f, 1300.f, Z) })
	{
		BoxMM(EPal::SteelDark, C - FVector(180.f, 120.f, 0.f), C + FVector(180.f, 120.f, 90.f));
		Deco(EPal::GlassClear, C - FVector(170.f, 110.f, -90.f), C + FVector(170.f, 110.f, 92.f));
	}
	Cyl(EPal::Galvanized, FVector(-1220.f, 1720.f, Z), 8.f, 520.f, false);
	BoxR(EPal::Orange, FVector(-1150.f, 1720.f, Z + 490.f), FVector(150.f, 30.f, 30.f), FRotator(-15.f, 0.f, 0.f), false);
	SpotLight(FVector(0.f, -1750.f, Z + 60.f), FRotator(-10.f, 90.f, 0.f), 4000.f, 5000.f, 2200.f, 35.f);

	// Outside stairs up the east side (one per half), each with a small landing onto the pad.
	for (const float S : { -1.f, 1.f })
	{
		// Stood well off the lounge's glass wall (its mullion caps and the roof slab edge), clear of its doorway.
		constexpr float SX = 1440.f;
		constexpr float Out = SX + 80.f;
		StraightStair(FVector(SX, S * 860.f, 0.f), FVector(SX, S * 1530.f, Z), 150.f, EPal::Galvanized, EPal::SteelDark);
		// Grated landing beside the pad, level with it, through the gap in the curb.
		BoxMM(EPal::Galvanized, FVector(LX1 - 20.f, FMath::Min(S * 1520.f, S * 1720.f), Z - 15.f), FVector(Out, FMath::Max(S * 1520.f, S * 1720.f), Z));
		// The side guard starts where the stair's own handrail ends.
		Deco(EPal::SteelDark, FVector(Out - 6.f, FMath::Min(S * 1532.f, S * 1720.f), Z), FVector(Out, FMath::Max(S * 1532.f, S * 1720.f), Z + 100.f));
		Blocker(FVector(Out - 6.f, FMath::Min(S * 1530.f, S * 1720.f), Z), FVector(Out + 10.f, FMath::Max(S * 1530.f, S * 1720.f), Z + 110.f));
		// The end guard runs from the outer face of the pad's curb, not through it.
		Deco(EPal::SteelDark, FVector(LX1 + 10.f, FMath::Min(S * 1720.f, S * 1726.f), Z), FVector(Out, FMath::Max(S * 1720.f, S * 1726.f), Z + 100.f));
		Blocker(FVector(LX1 - 20.f, FMath::Min(S * 1720.f, S * 1730.f), Z), FVector(Out + 10.f, FMath::Max(S * 1720.f, S * 1730.f), Z + 110.f));
	}
	Rect(-850.f, -850.f, 850.f, 850.f, 1);
}

// ------------------------------------------------------------------ stair cores

void FCampusSchoolBuildContext::HotelCores()
{
	for (const float S : { -1.f, 1.f })
	{
		const float Y0 = S < 0.f ? -CY1 : CY0;
		const float Y1 = S < 0.f ? -CY0 : CY1;
		const float Outer = S < 0.f ? Y0 : Y1;
		const float Inner = S < 0.f ? Y1 : Y0;
		// Stone pavilion: doorways onto the terrace and toward the lounge, glazed corners, a lit canopy.
		// Marble on the slab only: the stair opening (the hole in the deck slab, minus its arrival strip) stays open, so
		// the stair reads from the lobby instead of hiding under a floor that is not there.
		const float HoleY0 = S < 0.f ? -CY1 + 50.f : CY0 + 50.f + 120.f;
		const float HoleY1 = S < 0.f ? -CY0 - 50.f - 120.f : CY1 - 50.f;
		Deco(EPal::Marble, FVector(800.f, Y0, 0.f), FVector(CX1, Y1, 2.f));
		Deco(EPal::Marble, FVector(CX0, Y0, 0.f), FVector(800.f, HoleY0, 2.f));
		Deco(EPal::Marble, FVector(CX0, HoleY1, 0.f), FVector(800.f, Y1, 2.f));
		// Glass balustrade along the lobby side of the opening (the stair's own guard closes its head).
		GlassRail(FVector(805.f, HoleY0, 0.f), FVector(805.f, HoleY1, 0.f), 105.f, 105.f);
		BoxMM(EPal::Stone2, FVector(CX0 - 20.f, Y0 - 20.f, CoreH), FVector(CX1 + 20.f, Y1 + 20.f, CoreH + 40.f));
		Deco(EPal::WallWhite, FVector(CX0, Y0, CoreH - 2.f), FVector(CX1, Y1, CoreH));
		WallX(EPal::Stone2, Outer, CX0, CX1, 0.f, CoreH, 30.f, { { 880.f, 1160.f, 0.f, 260.f, false } }, EPal::Count);
		WallX(EPal::Stone2, Inner, CX0, CX1, 0.f, CoreH, 30.f, { { 880.f, 1160.f, 0.f, 260.f, false } }, EPal::Count);
		WallY(EPal::Stone2, CX0, Y0, Y1, 0.f, CoreH, 30.f, { { Y0 + 110.f, Y1 - 110.f, 120.f, 360.f, true } }, EPal::Count);
		WallY(EPal::Stone2, CX1, Y0, Y1, 0.f, CoreH, 30.f, {}, EPal::Count);
		// Brass band under the coping, lit canopies over both doorways, vertical stone fins on the blank east face.
		Deco(EPal::Brass, FVector(CX0 - 17.f, Y0 - 17.f, CoreH - 40.f), FVector(CX1 + 17.f, Y1 + 17.f, CoreH - 30.f));
		for (const float Face : { Outer, Inner })
		{
			const float Dir = Face == Outer ? S : -S;
			BoxMM(EPal::SteelDark, FVector(840.f, FMath::Min(Face, Face + Dir * 160.f), 285.f), FVector(1200.f, FMath::Max(Face, Face + Dir * 160.f), 297.f), false);
			Deco(EPal::LedWarm, FVector(850.f, FMath::Min(Face + Dir * 20.f, Face + Dir * 150.f), 283.f), FVector(1190.f, FMath::Max(Face + Dir * 20.f, Face + Dir * 150.f), 285.f));
			PointLight(FVector(1020.f, Face + Dir * 90.f, 260.f), 900.f, 2800.f, 500.f);
		}
		for (float Y = Y0 + 80.f; Y < Y1 - 40.f; Y += 120.f)
		{
			BoxMM(EPal::Stone2, FVector(CX1 + 15.f, Y - 12.f, 0.f), FVector(CX1 + 40.f, Y + 12.f, CoreH - 40.f), false);
		}
		// Brass lift doors on the east wall, the floor indicator over them.
		for (const float Yd : { -170.f, 170.f })
		{
			const float Yc = (Y0 + Y1) * 0.5f + Yd;
			Deco(EPal::Brass, FVector(CX1 - 17.f, Yc - 60.f, 0.f), FVector(CX1 - 14.f, Yc + 60.f, 230.f));
			Deco(EPal::Black, FVector(CX1 - 18.f, Yc - 1.f, 0.f), FVector(CX1 - 13.f, Yc + 1.f, 230.f));
			Deco(EPal::Neon, FVector(CX1 - 18.f, Yc - 20.f, 250.f), FVector(CX1 - 15.f, Yc + 20.f, 262.f));
		}
		// Stair down to the service level; the arrival strip is the end nearest the lounge.
		const float WellY0 = S < 0.f ? -CY1 + 50.f : CY0 + 50.f;
		const float WellY1 = S < 0.f ? -CY0 - 50.f : CY1 - 50.f;
		Stair(FBox2D(FVector2D(CX0 + 15.f, WellY0), FVector2D(800.f, WellY1)), false, S < 0.f, true, LowFloor, 0.f, true);
		WallY(EPal::WallWhite, 815.f, WellY0, WellY1, LowFloor, 0.f, 20.f, {}, EPal::Count);
		WallX(EPal::WallWhite, S * (CY1 - 40.f), CX0 - 20.f, 825.f, LowFloor, 0.f, 20.f, {}, EPal::Count);
		HotelCoreFinish(S, Y0, Y1, Outer, Inner, WellY0, WellY1);
		Sign(FVector(1000.f, Outer + S * 18.f, 330.f), S < 0.f ? -90.f : 90.f, 300.f, 60.f, TEXT("天際酒店 SKYLINE"), FLinearColor(0.03f, 0.03f, 0.035f), FLinearColor(1.f, 0.82f, 0.5f), 0, false, 3.f);
		Rect(CX0, Y0, CX1, Y1, 0);
	}
}

void FCampusSchoolBuildContext::HotelCoreFinish(float S, float Y0, float Y1, float Outer, float Inner, float WellY0, float WellY1)
{
	// The pavilion: a lift lobby, not a stone shed. A cove of warm light round the ceiling, a dark stone border let
	// into the marble, brass sconces either side of the lifts, the level sign at the head of the stair.
	const float Zc = CoreH - 2.f;
	for (const float Face : { Outer, Inner })
	{
		const float In = Face == Outer ? -S : S;
		Deco(EPal::WallWhite, FVector(CX0 + 15.f, FMath::Min(Face, Face + In * 45.f), Zc - 38.f), FVector(CX1 - 15.f, FMath::Max(Face, Face + In * 45.f), Zc));
		Deco(EPal::LedWarm, FVector(CX0 + 15.f, FMath::Min(Face + In * 45.f, Face + In * 47.f), Zc - 38.f), FVector(CX1 - 15.f, FMath::Max(Face + In * 45.f, Face + In * 47.f), Zc - 36.f));
		Deco(EPal::MarbleDark, FVector(CX0 + 15.f, FMath::Min(Face + In * 15.f, Face + In * 35.f), 2.f), FVector(CX1 - 15.f, FMath::Max(Face + In * 15.f, Face + In * 35.f), 2.6f));
	}
	Deco(EPal::MarbleDark, FVector(CX1 - 35.f, Y0 + 15.f, 2.f), FVector(CX1 - 15.f, Y1 - 15.f, 2.6f));
	for (float X = CX0 + 150.f; X < CX1 - 100.f; X += 330.f)
	{
		PointLight(FVector(X, Outer - S * 60.f, Zc - 20.f), 380.f, 2800.f, 380.f);
		PointLight(FVector(X, Inner + S * 60.f, Zc - 20.f), 380.f, 2800.f, 380.f);
	}
	const float Yc = (Y0 + Y1) * 0.5f;
	for (const float Yd : { -300.f, 0.f, 300.f })
	{
		const FVector At(CX1 - 20.f, Yc + Yd, 210.f);
		Box(EPal::Brass, At, FVector(4.f, 14.f, 30.f), false);
		Box(EPal::Canvas, At + FVector(-8.f, 0.f, 4.f), FVector(10.f, 12.f, 20.f), false);
		PointLight(At + FVector(-25.f, 0.f, 0.f), 260.f, 2700.f, 300.f);
	}
	// At the stair head: a slim dark-steel wayfinding totem at the edge of the opening with a brass cap, the level set in
	// its black glass face in brass and warm white, a light line under it so it stands off the marble.
	const float SignY = S < 0.f ? WellY0 + 120.f : WellY1 - 120.f;
	const FLinearColor Face(0.006f, 0.006f, 0.007f);
	Box(EPal::Stainless, FVector(826.f, SignY, 2.f), FVector(14.f, 48.f, 4.f), true);
	Box(EPal::SteelDark, FVector(826.f, SignY, 78.f), FVector(10.f, 42.f, 148.f), true);
	Box(EPal::Brass, FVector(826.f, SignY, 152.6f), FVector(10.4f, 42.4f, 1.2f), false);
	Box(EPal::LedWarm, FVector(833.f, SignY, 4.5f), FVector(0.6f, 40.f, 0.6f), false);
	Box(EPal::Black, FVector(831.3f, SignY, 106.f), FVector(0.6f, 36.f, 80.f), false);
	Sign(FVector(831.9f, SignY, 128.f), 0.f, 30.f, 24.f, TEXT("B1"), Face, FLinearColor(0.92f, 0.76f, 0.48f), 3, false, 0.35f, 0.4f, 8.f);
	Box(EPal::Brass, FVector(831.9f, SignY, 113.f), FVector(0.4f, 22.f, 0.4f), false);
	Sign(FVector(831.9f, SignY, 104.f), 0.f, 30.f, 9.f, TEXT("服務層"), Face, FLinearColor(0.9f, 0.88f, 0.84f), 3, false, 0.3f, 0.4f, 10.f);
	Sign(FVector(831.9f, SignY, 96.f), 0.f, 30.f, 4.5f, TEXT("SERVICE LEVEL"), Face, FLinearColor(0.56f, 0.54f, 0.5f), 3, false, 0.25f, 0.4f, 14.f);
	Sign(FVector(831.9f, SignY, 80.f), 0.f, 12.f, 12.f, TEXT("↓"), Face, FLinearColor(0.92f, 0.76f, 0.48f), 3, false, 0.35f, 0.4f, 8.f);

	// The service level at the foot: painted two-tone (a hard-wearing grey dado under white), the floor marked in the
	// stair's own colours, the lit exit sign, an emergency light and the hose cabinet.
	const float Z0 = LowFloor;
	const float WallFace = S * (CY1 - 40.f) - S * 10.f;
	Deco(EPal::ConcreteHQ, FVector(CX0, FMath::Min(WallFace, WallFace - S * 1.f), Z0), FVector(825.f, FMath::Max(WallFace, WallFace - S * 1.f), Z0 + 110.f));
	Deco(EPal::HeliYellow, FVector(CX0, FMath::Min(WallFace - S * 1.f, WallFace - S * 1.5f), Z0 + 110.f), FVector(825.f, FMath::Max(WallFace - S * 1.f, WallFace - S * 1.5f), Z0 + 116.f));
	Deco(EPal::ConcreteHQ, FVector(804.f, WellY0, Z0), FVector(805.f, WellY1, Z0 + 110.f));
	Sign(FVector(802.f, (WellY0 + WellY1) * 0.5f, Z0 + 175.f), 180.f, 120.f, 70.f, TEXT("B1"), FLinearColor(0.85f, 0.85f, 0.82f), FLinearColor(0.08f, 0.09f, 0.1f), 1, false, 0.f);
	const float Foot = S < 0.f ? WellY0 + 40.f : WellY1 - 40.f;
	Box(EPal::Black, FVector(802.f, Foot, Z0 + 250.f), FVector(4.f, 44.f, 18.f), false);
	Sign(FVector(799.f, Foot, Z0 + 250.f), 180.f, 40.f, 14.f, TEXT("安全門 EXIT"), FLinearColor(0.05f, 0.45f, 0.18f), FLinearColor(1.f, 1.f, 1.f), 2, false, 0.5f);
	Box(EPal::LightPanel, FVector(802.f, Foot - S * 90.f, Z0 + 260.f), FVector(6.f, 30.f, 10.f), false);
	PointLight(FVector(780.f, Foot - S * 90.f, Z0 + 250.f), 300.f, 5200.f, 450.f);
	Box(EPal::RedBoard, FVector(802.f, (WellY0 + WellY1) * 0.5f + S * 160.f, Z0 + 110.f), FVector(8.f, 70.f, 90.f), true);
	Box(EPal::Stainless, FVector(797.f, (WellY0 + WellY1) * 0.5f + S * 160.f, Z0 + 110.f), FVector(1.f, 60.f, 80.f), false);
}

// ------------------------------------------------------------------ east: the plant deck

void FCampusSchoolBuildContext::CoolingTower(const FVector& Base, float Size)
{
	const float H2 = Size * 0.85f;
	BoxMM(EPal::HvacGrey, Base - FVector(Size * 0.5f, Size * 0.5f, 0.f), Base + FVector(Size * 0.5f, Size * 0.5f, H2));
	// Louvre bands round the base and the fan stack on top.
	for (float Z = 30.f; Z < H2 * 0.55f; Z += 22.f)
	{
		for (const float S : { -1.f, 1.f })
		{
			Deco(EPal::Galvanized, Base + FVector(-Size * 0.5f, S * (Size * 0.5f + 2.f), Z) - FVector(0.f, 2.f, 0.f), Base + FVector(Size * 0.5f, S * (Size * 0.5f + 2.f), Z + 6.f) + FVector(0.f, 2.f, 0.f));
		}
	}
	Cyl(EPal::Galvanized, Base + FVector(0.f, 0.f, H2), Size * 0.75f, 90.f, true);
	Cyl(EPal::Black, Base + FVector(0.f, 0.f, H2 + 88.f), Size * 0.68f, 3.f, false);
	BoxMM(EPal::Galvanized, Base + FVector(-Size * 0.5f - 60.f, -40.f, 0.f), Base + FVector(-Size * 0.5f, 40.f, 6.f), false);
}

void FCampusSchoolBuildContext::HotelMechanical()
{
	// Machine room: concrete box with louvres, two doors on its west side and roller openings at both ends; inside,
	// generators and pumps round the stair down.
	BoxMM(EPal::ConcreteDark, FVector(MX0 - 20.f, -MY - 20.f, MH), FVector(MX1 + 20.f, MY + 20.f, MH + 30.f));
	ForEachOutsideHoles(MX0, -MY, MX1, MY, [this](float X0, float Y0, float X1, float Y1)
	{
		Deco(EPal::Epoxy, FVector(X0, Y0, 0.f), FVector(X1, Y1, 2.f));
	});
	WallY(EPal::ConcreteDark, MX0, -MY, MY, 0.f, MH, 25.f, { { -640.f, -520.f, 0.f, 215.f, false }, { 520.f, 640.f, 0.f, 215.f, false } }, EPal::Count);
	WallY(EPal::ConcreteDark, MX1, -MY, MY, 0.f, MH, 25.f, {}, EPal::Count);
	WallX(EPal::ConcreteDark, -MY, MX0, MX1, 0.f, MH, 25.f, { { 2500.f, 2900.f, 0.f, 300.f, false } }, EPal::Count);
	WallX(EPal::ConcreteDark, MY, MX0, MX1, 0.f, MH, 25.f, { { 2500.f, 2900.f, 0.f, 300.f, false } }, EPal::Count);
	for (float Y = -MY + 150.f; Y < MY; Y += 400.f)
	{
		Deco(EPal::Galvanized, FVector(MX0 - 16.f, Y - 80.f, 250.f), FVector(MX0 - 12.f, Y + 80.f, 360.f));
		Deco(EPal::Galvanized, FVector(MX1 + 12.f, Y - 80.f, 250.f), FVector(MX1 + 16.f, Y + 80.f, 360.f));
	}
	for (const float S : { -1.f, 1.f })
	{
		Deco(EPal::BarrierYellow, FVector(2500.f, S * MY - 14.f, 300.f), FVector(2900.f, S * MY + 14.f, 312.f));
		// Generators (chest-high, good cover inside) and an electrical wall. The first stands well clear of the west
		// doors, leaving a lane in front of them to walk in and round it.
		BoxMM(EPal::Olive, FVector(2470.f, S * 550.f - 150.f, 0.f), FVector(2700.f, S * 550.f + 150.f, 150.f));
		BoxMM(EPal::Olive, FVector(2850.f, S * 600.f - 120.f, 0.f), FVector(3120.f, S * 600.f + 120.f, 180.f));
		// Exhaust stack up from the generator to a rain cap under the roof.
		Cyl(EPal::Galvanized, FVector(2585.f, S * 550.f, 150.f), 30.f, 230.f, false);
		Cyl(EPal::SteelDark, FVector(2585.f, S * 550.f, 380.f), 44.f, 8.f, false);
	}
	BoxMM(EPal::HvacGrey, FVector(MX1 - 70.f, -420.f, 0.f), FVector(MX1 - 15.f, 420.f, 220.f));
	for (float Y = -380.f; Y <= 380.f; Y += 95.f)
	{
		Deco(EPal::ScreenGreen, FVector(MX1 - 72.f, Y - 6.f, 170.f), FVector(MX1 - 70.f, Y + 6.f, 176.f));
	}
	Stair(FBox2D(FVector2D(2400.f, -300.f), FVector2D(2800.f, 400.f)), false, false, true, LowFloor, 0.f);
	WallX(EPal::WallWhite, 410.f, 2380.f, 2820.f, LowFloor, 0.f, 20.f, {}, EPal::Count);
	// Railings round the open well on the machine room floor.
	for (const float X : { 2395.f, 2805.f })
	{
		Deco(EPal::BarrierYellow, FVector(X - 3.f, -180.f, 100.f), FVector(X + 3.f, 400.f, 106.f));
		Deco(EPal::BarrierYellow, FVector(X - 2.f, -180.f, 50.f), FVector(X + 2.f, 400.f, 54.f));
		for (float Y = -180.f; Y <= 400.f; Y += 145.f)
		{
			Deco(EPal::BarrierYellow, FVector(X - 3.f, Y - 3.f, 0.f), FVector(X + 3.f, Y + 3.f, 106.f));
		}
		Blocker(FVector(X - 5.f, -180.f, 0.f), FVector(X + 5.f, 400.f, 110.f));
	}
	Fixture(FVector(2750.f, -700.f, MH), true, 4500.f);
	Fixture(FVector(2750.f, 700.f, MH), true, 4500.f);
	Sign(FVector(MX0 - 16.f, -580.f, 270.f), 180.f, 110.f, 40.f, TEXT("機房 重地"), FLinearColor(0.75f, 0.06f, 0.05f), FLinearColor(1.f, 1.f, 1.f), 2);
	Rect(MX0, -MY, MX1, MY, 0);

	// Outside, mirrored on both halves: cooling towers, a run of air handlers along the lane, a low duct to vault, an
	// overhead duct to the lounge, water tanks and pipe racks along the east edge, a louvred screen at the terrace.
	for (const float S : { -1.f, 1.f })
	{
		CoolingTower(FVector(2600.f, S * 1700.f, 0.f), 420.f);
		CoolingTower(FVector(3000.f, S * 2400.f, 0.f), 380.f);
		Rect(2390.f, S * 1490.f, 2810.f, S * 1910.f, 2);
		Rect(2810.f, S * 2210.f, 3190.f, S * 2590.f, 2);
		for (const float Y0 : { 1150.f, 1750.f })
		{
			BoxMM(EPal::HvacGrey, FVector(1700.f, FMath::Min(S * Y0, S * (Y0 + 450.f)), 0.f), FVector(1900.f, FMath::Max(S * Y0, S * (Y0 + 450.f)), 175.f));
			Deco(EPal::Galvanized, FVector(1695.f, FMath::Min(S * (Y0 + 40.f), S * (Y0 + 410.f)), 60.f), FVector(1905.f, FMath::Max(S * (Y0 + 40.f), S * (Y0 + 410.f)), 150.f));
		}
		Rect(1700.f, S * 1150.f, 1900.f, S * 2350.f, 2);
		BoxMM(EPal::Galvanized, FVector(2080.f, FMath::Min(S * 1300.f, S * 2500.f), 0.f), FVector(2200.f, FMath::Max(S * 1300.f, S * 2500.f), 85.f));
		for (float Y = 1350.f; Y < 2500.f; Y += 300.f)
		{
			Deco(EPal::SteelDark, FVector(2075.f, S * Y - 6.f, 0.f), FVector(2205.f, S * Y + 6.f, 90.f));
		}
		// Overhead duct (head height clear) from the machine room to the kitchen roof.
		BoxMM(EPal::Galvanized, FVector(LX1 + 10.f, S * 650.f - 60.f, 300.f), FVector(MX0 - 20.f, S * 650.f + 60.f, 380.f), false);
		for (float X = 1500.f; X < MX0; X += 300.f)
		{
			Box(EPal::SteelDark, FVector(X, S * 650.f, 150.f), FVector(10.f, 10.f, 300.f), true);
		}
		// Water tanks on stands along the edge.
		for (const float Y : { 1350.f, 3000.f })
		{
			const FVector T(3200.f, S * Y, 0.f);
			for (const float Ox : { -70.f, 70.f })
			{
				for (const float Oy : { -70.f, 70.f })
				{
					Box(EPal::SteelDark, T + FVector(Ox, Oy, 60.f), FVector(10.f, 10.f, 120.f), true);
				}
			}
			Cyl(EPal::TankWhite, T + FVector(0.f, 0.f, 120.f), 220.f, 200.f, true);
		}
		// Pipe rack over the walkway east of the machine room.
		for (float Y = 1050.f; Y < 2100.f; Y += 350.f)
		{
			for (const float X : { 3230.f, 3370.f })
			{
				Box(EPal::Galvanized, FVector(X, S * Y, 140.f), FVector(10.f, 10.f, 280.f), false);
			}
			Box(EPal::Galvanized, FVector(3300.f, S * Y, 280.f), FVector(150.f, 10.f, 10.f), false);
		}
		for (const float X : { 3260.f, 3300.f, 3340.f })
		{
			CylR(EPal::Galvanized, FVector(X, S * 1575.f, 292.f), 18.f, 1100.f, FRotator(90.f, 90.f, 0.f), false);
		}
		// Louvred screen between the plant and the terrace, with two ways through.
		for (const FVector2D& Seg : { FVector2D(1500.f, 1650.f), FVector2D(2050.f, 2850.f) })
		{
			BoxMM(EPal::TowerDark, FVector(Seg.X, S * 3000.f - 10.f, 0.f), FVector(Seg.Y, S * 3000.f + 10.f, 260.f));
			for (float Z = 20.f; Z < 260.f; Z += 30.f)
			{
				Deco(EPal::Galvanized, FVector(Seg.X, S * 3000.f - 14.f, Z), FVector(Seg.Y, S * 3000.f + 14.f, Z + 5.f));
			}
		}
		// Electrical cabinets by the machine room doors.
		BoxMM(EPal::HvacGrey, FVector(2150.f, S * 900.f - 60.f, 0.f), FVector(2260.f, S * 900.f + 60.f, 190.f));
		Deco(EPal::BarrierYellow, FVector(2148.f, S * 900.f - 20.f, 120.f), FVector(2150.f, S * 900.f + 20.f, 150.f));
	}
	// The window cleaning crane (BMU) parked on its rails at the south-east corner, boom out over the street.
	Deco(EPal::Galvanized, FVector(3290.f, -2700.f, 0.f), FVector(3310.f, 2700.f, 10.f));
	Deco(EPal::Galvanized, FVector(3360.f, -2700.f, 0.f), FVector(3380.f, 2700.f, 10.f));
	BoxMM(EPal::BarrierYellow, FVector(3230.f, -2050.f, 0.f), FVector(3390.f, -1750.f, 180.f));
	Box(EPal::BarrierYellow, FVector(3310.f, -1900.f, 400.f), FVector(70.f, 70.f, 440.f), true);
	BoxR(EPal::BarrierYellow, FVector(3700.f, -1900.f, 600.f), FVector(900.f, 50.f, 50.f), FRotator(12.f, 0.f, 0.f), false);
	CylR(EPal::Black, FVector(4120.f, -1900.f, 420.f), 2.f, 300.f, FRotator::ZeroRotator, false);
	BoxMM(EPal::Galvanized, FVector(3990.f, -2020.f, 230.f), FVector(4250.f, -1780.f, 280.f), false);
}

// ------------------------------------------------------------------ the service level (flank route)

void FCampusSchoolBuildContext::HotelLowerLevel()
{
	const float Z0 = LowFloor;
	const float Z1 = LowCeil;
	auto Floor = [&](float X0, float Y0, float X1, float Y1, EPal P)
	{
		BoxMM(EPal::ConcreteDark, FVector(X0, Y0, Z0 - 30.f), FVector(X1, Y1, Z0));
		Deco(P, FVector(X0, Y0, Z0), FVector(X1, Y1, Z0 + 1.f));
		// Open above the stairs: their upper flights rise through the slab opening, not through a ceiling.
		ForEachOutsideHoles(X0, Y0, X1, Y1, [this, Z1](float A0, float B0, float A1, float B1)
		{
			CeilingAt(A0, B0, A1, B1, Z1);
		});
	};
	// The long north-south corridor from core to core, rooms off its east side, the cross corridor to the machine
	// room stair.
	Floor(CorX0, -CY1 + 50.f, CorX1, CY1 - 50.f, EPal::Epoxy);
	Floor(CorX1, -2400.f, 1300.f, -900.f, EPal::RubberMat);
	Floor(CorX1, 1100.f, 1300.f, 2400.f, EPal::Terrazzo);
	Floor(800.f, 300.f, 1250.f, 900.f, EPal::Epoxy);
	Floor(CorX1, XCorY0, 2800.f, XCorY1, EPal::Epoxy);
	Floor(2400.f, XCorY1, 2800.f, 400.f, EPal::Epoxy);

	const float T = 20.f;
	// West wall of the corridor (the core stair wells' outer walls carry on above it).
	WallY(EPal::WallWhite, CorX0 - T * 0.5f, -CY1 + 40.f, CY1 - 40.f, Z0, Z1, T, {}, EPal::Count);
	// East wall with the room doors, the kitchen stair opening and the cross corridor.
	WallY(EPal::WallWhite, CorX1 + T * 0.5f, -CY0 - 50.f, CY0 + 50.f, Z0, Z1, T, {
		{ -1300.f, -1180.f, Z0, Z0 + 215.f, false },
		{ XCorY0, XCorY1, Z0, Z1, false },
		{ 300.f, 420.f, Z0, Z1, false },
		{ 1600.f, 1720.f, Z0, Z1 - 150.f, false } }, EPal::Count);
	// Locker room and linen store.
	WallX(EPal::WallWhite, -2400.f, CorX1, 1300.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, -900.f, CorX1, 1300.f, Z0, Z1, T, {}, EPal::Count);
	WallY(EPal::WallWhite, 1300.f, -2400.f, -900.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, 1100.f, CorX1, 1300.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, 2400.f, CorX1, 1300.f, Z0, Z1, T, {}, EPal::Count);
	WallY(EPal::WallWhite, 1300.f, 1100.f, 2400.f, Z0, Z1, T, {}, EPal::Count);
	for (float Y = -2300.f; Y < -1000.f; Y += 60.f)
	{
		if (Y > -1350.f && Y < -1130.f)
		{
			continue;
		}
		BoxMM(EPal::Locker, FVector(1230.f, Y, Z0), FVector(1290.f, Y + 56.f, Z0 + 190.f));
	}
	BoxMM(EPal::Teak, FVector(950.f, -2100.f, Z0), FVector(1010.f, -1400.f, Z0 + 45.f));
	for (float Y = 1200.f; Y < 2300.f; Y += 260.f)
	{
		BoxMM(EPal::Shelf, FVector(1150.f, Y, Z0), FVector(1290.f, Y + 220.f, Z0 + 210.f));
		BoxMM(EPal::Canvas, FVector(1155.f, Y + 10.f, Z0 + 90.f), FVector(1285.f, Y + 210.f, Z0 + 160.f), false);
	}
	BoxMM(EPal::Stainless, FVector(880.f, 1500.f, Z0), FVector(1000.f, 1900.f, Z0 + 95.f));
	// Kitchen stair room walls, cross corridor walls, machine room stair surround.
	WallY(EPal::WallWhite, 1260.f, 300.f, 900.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, 290.f, CorX1, 1260.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, 910.f, CorX1, 1260.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, XCorY0 - T * 0.5f, CorX1, 2800.f, Z0, Z1, T, {}, EPal::Count);
	WallX(EPal::WallWhite, XCorY1 + T * 0.5f, CorX1, 2400.f, Z0, Z1, T, {}, EPal::Count);
	WallY(EPal::WallWhite, 2810.f, XCorY0, 400.f, Z0, Z1, T, {}, EPal::Count);
	WallY(EPal::WallWhite, 2390.f, XCorY1, 400.f, Z0, Z1, T, {}, EPal::Count);
	// Corridor dressing: laundry carts and pallets (cover), pipes along the ceiling, fluorescent strips, signage.
	for (const FVector& C : { FVector(450.f, -1900.f, Z0), FVector(650.f, -200.f, Z0), FVector(420.f, 1300.f, Z0), FVector(1500.f, -500.f, Z0), FVector(2100.f, -560.f, Z0) })
	{
		BoxMM(EPal::Stainless, C - FVector(60.f, 45.f, 0.f), C + FVector(60.f, 45.f, 95.f));
		BoxMM(EPal::Canvas, C - FVector(55.f, 40.f, -95.f), C + FVector(55.f, 40.f, 115.f), false);
	}
	BoxMM(EPal::WoodDesk, FVector(350.f, 2000.f, Z0), FVector(470.f, 2120.f, Z0 + 110.f));
	BoxMM(EPal::WoodDesk, FVector(1800.f, -680.f, Z0), FVector(1920.f, -560.f, Z0 + 90.f));
	for (const float X : { 340.f, 380.f })
	{
		CylR(EPal::Galvanized, FVector(X, 0.f, Z1 - 30.f), 12.f, 2 * (CY0 + 50.f), FRotator(90.f, 90.f, 0.f), false);
	}
	CylR(EPal::Galvanized, FVector(1800.f, XCorY0 + 40.f, Z1 - 30.f), 12.f, 2000.f, FRotator(90.f, 0.f, 0.f), false);
	// Pipe hangers: a threaded rod down from the slab to a strap under each pipe, every three metres.
	auto Hanger = [&](float X, float Y, bool bAlongY)
	{
		Cyl(EPal::Galvanized, FVector(X, Y, Z1 - 37.f), 1.4f, 37.f, false);
		Box(EPal::Galvanized, FVector(X, Y, Z1 - 37.f), bAlongY ? FVector(16.f, 3.f, 1.5f) : FVector(3.f, 16.f, 1.5f), false);
	};
	for (float Y = -CY0 + 100.f; Y < CY0; Y += 300.f)
	{
		Hanger(340.f, Y, true);
		Hanger(380.f, Y, true);
	}
	for (float X = 900.f; X < 2800.f; X += 300.f)
	{
		Hanger(X, XCorY0 + 40.f, false);
	}
	for (float Y = -2300.f; Y <= 2300.f; Y += 700.f)
	{
		Fixture(FVector(550.f, Y, Z1), true, 4500.f);
	}
	for (float X = 1100.f; X <= 2600.f; X += 600.f)
	{
		Fixture(FVector(X, -500.f, Z1), false, 4500.f);
	}
	Fixture(FVector(1050.f, -1650.f, Z1), true, 4500.f);
	Fixture(FVector(1050.f, 1750.f, Z1), true, 4500.f);
	Sign(FVector(CorX0 + 1.5f, -1200.f, Z0 + 230.f), 0.f, 240.f, 50.f, TEXT("52F 員工通道"), FLinearColor(0.05f, 0.25f, 0.12f), FLinearColor(0.95f, 0.95f, 0.9f), 2, false, 1.f);
	Sign(FVector(CorX0 + 1.5f, 1200.f, Z0 + 230.f), 0.f, 240.f, 50.f, TEXT("52F 員工通道"), FLinearColor(0.05f, 0.25f, 0.12f), FLinearColor(0.95f, 0.95f, 0.9f), 2, false, 1.f);
	Sign(FVector(1700.f, XCorY1 - 1.5f, Z0 + 230.f), -90.f, 260.f, 50.f, TEXT("往機房 →"), FLinearColor(0.05f, 0.25f, 0.12f), FLinearColor(0.95f, 0.95f, 0.9f), 2, false, 1.f);
}

// ------------------------------------------------------------------ spawn terraces and the plazas between

void FCampusSchoolBuildContext::HotelTerraces()
{
	for (const float S : { -1.f, 1.f })
	{
		// Rows of planters break up the spawn terrace (spawn cover), with outdoor sofas and a fire table between.
		for (const float X : { -2600.f, -1500.f, -350.f, 1900.f, 2900.f })
		{
			Planter(FVector(X - 260.f, S * 3700.f - 60.f, 0.f), FVector(X + 260.f, S * 3700.f + 60.f, 85.f), (int32)(X * S));
		}
		Rect(-2860.f, S * 3640.f, -2340.f, S * 3760.f, 3);
		Rect(-1760.f, S * 3640.f, -1240.f, S * 3760.f, 3);
		Rect(1640.f, S * 3640.f, 2160.f, S * 3760.f, 3);
		const FVector Fire(-900.f, S * 4150.f, 0.f);
		BoxMM(EPal::MarbleDark, Fire - FVector(160.f, 70.f, 0.f), Fire + FVector(160.f, 70.f, 45.f));
		Deco(EPal::Neon, Fire - FVector(120.f, 15.f, -45.f), Fire + FVector(120.f, 15.f, 47.f));
		PointLight(Fire + FVector(0.f, 0.f, 70.f), 1200.f, 1900.f, 600.f);
		BoxMM(EPal::Canvas, FVector(-1150.f, S * 4380.f - 45.f, 0.f), FVector(-650.f, S * 4380.f + 45.f, 80.f));
		HotelRoofSign(S);
		// Plaza between the core and the lounge: a stone sculpture and benches (mid-lane cover), the pool deck end.
		const FVector Sculpt(-200.f, S * 2200.f, 0.f);
		BoxMM(EPal::Stone2, Sculpt - FVector(110.f, 110.f, 0.f), Sculpt + FVector(110.f, 110.f, 60.f));
		BoxR(EPal::Brass, Sculpt + FVector(0.f, 0.f, 170.f), FVector(60.f, 140.f, 220.f), FRotator(15.f, 30.f, 10.f), true);
		Box(EPal::Teak, FVector(-850.f, S * 2250.f, 22.f), FVector(220.f, 60.f, 44.f), true);
		Box(EPal::Teak, FVector(-2700.f, S * 3050.f, 22.f), FVector(60.f, 260.f, 44.f), true);
		Planter(FVector(-2300.f, S * 3150.f - 100.f, 0.f), FVector(-1900.f, S * 3150.f + 100.f, 60.f), 40 + (int32)S);
		Rect(-310.f, S * 2090.f, -90.f, S * 2310.f, 2);
	}
}

void FCampusSchoolBuildContext::HotelRoofSign(float S)
{
	// The hotel's name the way Taipei towers carry theirs: one lightbox per character, each a deep steel can with a
	// bronze return round its face, facing the city from the edge of the roof. From the deck it is what such signs
	// are from behind: the backs of the cans, the steel frame they hang on, raking struts down to ballast blocks, the
	// conduit feeding each box.
	const TCHAR* Chars[] = { TEXT("天"), TEXT("際"), TEXT("酒"), TEXT("店") };
	const int32 N = UE_ARRAY_COUNT(Chars);
	const float Can = 230.f;
	const float Gap = 26.f;
	const float Span = N * Can + (N - 1) * Gap;
	const float X0 = 1000.f - Span * 0.5f;
	const float Z0 = 175.f;
	const float Z1 = Z0 + Can;
	const float Face = S * (HY - 40.f);
	const float Back = S * (HY - 88.f);
	const float Frame = S * (HY - 104.f);
	auto YSpan = [&](float A, float Bv) { return FVector2D(FMath::Min(A, Bv), FMath::Max(A, Bv)); };
	for (int32 i = 0; i < N; ++i)
	{
		const float Xa = X0 + i * (Can + Gap);
		const float Xc = Xa + Can * 0.5f;
		const FVector2D CanY = YSpan(Back, Face - S * 2.f);
		BoxMM(EPal::SteelDark, FVector(Xa, CanY.X, Z0), FVector(Xa + Can, CanY.Y, Z1));
		// Read from outside: on the south edge the viewer's left is +X, on the north edge it is -X.
		const TCHAR* Char = Chars[S < 0.f ? N - 1 - i : i];
		Sign(FVector(Xc, Face - S * 1.f, (Z0 + Z1) * 0.5f), S * 90.f, Can - 16.f, Can - 16.f, Char, FLinearColor(0.035f, 0.028f, 0.022f), FLinearColor(1.f, 0.86f, 0.62f), 0, false, 7.f, 3.f, 2.f);
		// Bronze return round the face, standing a little proud of it.
		const FVector2D Ret = YSpan(Face - S * 4.f, Face + S * 3.f);
		Deco(EPal::Brass, FVector(Xa, Ret.X, Z0), FVector(Xa + Can, Ret.Y, Z0 + 8.f));
		Deco(EPal::Brass, FVector(Xa, Ret.X, Z1 - 8.f), FVector(Xa + Can, Ret.Y, Z1));
		Deco(EPal::Brass, FVector(Xa, Ret.X, Z0), FVector(Xa + 8.f, Ret.Y, Z1));
		Deco(EPal::Brass, FVector(Xa + Can - 8.f, Ret.X, Z0), FVector(Xa + Can, Ret.Y, Z1));
		// Service hatch and the conduit tail on the back of each can.
		const FVector2D Hatch = YSpan(Back - S * 1.5f, Back);
		Deco(EPal::Galvanized, FVector(Xc - 45.f, Hatch.X, Z0 + 60.f), FVector(Xc + 45.f, Hatch.Y, Z0 + 150.f));
		Deco(EPal::Black, FVector(Xc - 6.f, FMath::Min(Back, Frame), Z0 + 20.f), FVector(Xc + 6.f, FMath::Max(Back, Frame), Z0 + 32.f));
	}
	// The frame: posts between and outside the cans, two chords, X-bracing in the back plane.
	TArray<float> Posts;
	for (int32 k = 0; k <= N; ++k)
	{
		Posts.Add(X0 - Gap * 0.5f + k * (Can + Gap));
	}
	for (const float X : Posts)
	{
		BoxMM(EPal::Concrete, FVector(X - 30.f, Frame - 30.f, 0.f), FVector(X + 30.f, Frame + 30.f, 30.f));
		BoxMM(EPal::SteelDark, FVector(X - 8.f, Frame - 8.f, 30.f), FVector(X + 8.f, Frame + 8.f, Z1 + 15.f));
		// A raking strut back to a ballast block on the deck, behind an invisible guard so nobody walks through it.
		const float Foot = S * (HY - 380.f);
		const FVector Top(X, Frame, Z0 + 150.f);
		const FVector Bottom(X, Foot, 30.f);
		const FVector Mid = (Top + Bottom) * 0.5f;
		BoxR(EPal::SteelDark, Mid, FVector(FVector::Dist(Top, Bottom), 10.f, 10.f), (Top - Bottom).Rotation(), false);
		BoxMM(EPal::Concrete, FVector(X - 40.f, Foot - 40.f, 0.f), FVector(X + 40.f, Foot + 40.f, 40.f));
		const FVector2D Guard = YSpan(Frame, Foot);
		Blocker(FVector(X - 12.f, Guard.X, 0.f), FVector(X + 12.f, Guard.Y, Z0 + 160.f));
	}
	const float XL = Posts[0] - 20.f;
	const float XR = Posts.Last() + 20.f;
	for (const float Z : { Z0 + 10.f, Z1 })
	{
		BoxMM(EPal::SteelDark, FVector(XL, Frame - 6.f, Z), FVector(XR, Frame + 6.f, Z + 12.f), false);
	}
	for (int32 k = 0; k + 1 < Posts.Num(); ++k)
	{
		const float Xa = Posts[k];
		const float Xb = Posts[k + 1];
		const float Run = Xb - Xa;
		const float Rise = Z1 - Z0 - 10.f;
		const float Len = FMath::Sqrt(Run * Run + Rise * Rise);
		const float Ang = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
		const FVector C((Xa + Xb) * 0.5f, Frame, (Z0 + Z1) * 0.5f + 5.f);
		BoxR(EPal::Galvanized, C, FVector(Len, 5.f, 5.f), FRotator(Ang, 0.f, 0.f), false);
		BoxR(EPal::Galvanized, C, FVector(Len, 5.f, 5.f), FRotator(-Ang, 0.f, 0.f), false);
	}
	// Cable tray along the frame, and the feed coming up out of the deck at one end in a weatherproof box.
	Deco(EPal::Galvanized, FVector(XL, Frame - S * 14.f - 10.f, Z0 + 18.f), FVector(XR, Frame - S * 14.f + 10.f, Z0 + 26.f));
	Box(EPal::HvacGrey, FVector(XR + 50.f, Frame, 70.f), FVector(50.f, 30.f, 80.f), true);
	Cyl(EPal::Black, FVector(XR + 50.f, Frame, 110.f), 4.f, Z0 - 90.f, false);
	// The faces wash the air in front of them: a warm glow that lights the coping and the glass below.
	PointLight(FVector(1000.f, S * (HY + 120.f), (Z0 + Z1) * 0.5f), 2500.f, 3000.f, 900.f);
}

// ------------------------------------------------------------------ the view: Xinyi and the basin round it

void FCampusSchoolBuildContext::HotelSkyline()
{
	// Street level (avenues, crossings, traffic, the park, the railway and the expressway), the towers of the district,
	// the walk-ups beyond, and the mountains round the basin.
	Deco(EPal::Roadway, FVector(-140000.f, -140000.f, Street - 40.f), FVector(140000.f, 140000.f, Street));
	// Ground already built on: the street fabric below keeps clear of it.
	TArray<FBox2D> Taken;
	HotelStreets(Taken);
	// Towers: a few close neighbours taller than this one, a ring of the district's office towers, and the
	// landmark supertall to the north-east.
	struct FTowerSpec { float X, Y, W, D, H; EPal Skin; };
	const FTowerSpec Near[] = {
		{ -16000.f, 9000.f, 4200.f, 4200.f, 24000.f, EPal::TowerBlue },
		{ 15000.f, -12000.f, 5000.f, 3600.f, 21000.f, EPal::TowerSilver },
		{ 21000.f, 14000.f, 3800.f, 3800.f, 26000.f, EPal::TowerGreen },
		{ -22000.f, -15000.f, 4600.f, 3200.f, 17000.f, EPal::TowerBronze },
		{ 4000.f, 24000.f, 3600.f, 5200.f, 19500.f, EPal::TowerDark },
		{ -5000.f, -26000.f, 4800.f, 4800.f, 22000.f, EPal::TowerBlue },
	};
	auto Take = [&](float X, float Y, float Half) { Taken.Add(FBox2D(FVector2D(X - Half, Y - Half), FVector2D(X + Half, Y + Half))); };
	uint32 Seed = 1;
	for (const FTowerSpec& T : Near)
	{
		SkyTower(FVector(T.X, T.Y, Street), T.W, T.D, T.H, T.Skin, Seed++, true);
		Take(T.X, T.Y, FMath::Max(T.W, T.D) * 0.6f + 300.f);
	}
	const EPal Skins[] = { EPal::TowerBlue, EPal::TowerSilver, EPal::TowerGreen, EPal::TowerBronze, EPal::TowerDark };
	for (int32 i = 0; i < 46; ++i)
	{
		const float Ang = HRand(i, 3) * 2.f * PI;
		const float R = 30000.f + 70000.f * FMath::Pow(HRand(i, 4), 0.8f);
		// Square to the street grid, in the middle of a block.
		const float X = FMath::FloorToFloat(FMath::Cos(Ang) * R / 6000.f) * 6000.f + 3000.f;
		const float Y = FMath::FloorToFloat(FMath::Sin(Ang) * R / 6000.f) * 6000.f + 3000.f;
		const float H2 = 7000.f + 15000.f * HRand(i, 5) * (R < 60000.f ? 1.f : 0.6f);
		const float W = 2600.f + 2400.f * HRand(i, 6);
		const FBox2D Foot(FVector2D(X - W * 0.6f, Y - W * 0.6f), FVector2D(X + W * 0.6f, Y + W * 0.6f));
		if (Taken.ContainsByPredicate([&](const FBox2D& T) { return T.Intersect(Foot); }))
		{
			continue;
		}
		SkyTower(FVector(X, Y, Street), W, W * (0.7f + 0.6f * HRand(i, 7)), H2, Skins[i % 5], 100 + i, false);
		Take(X, Y, W * 0.6f + 200.f);
	}
	Take(62000.f, 48000.f, 4200.f);
	{
		// Landmark: eight flared segments on a tapering base, a pinnacle and a spire (508 m), ~1 km out.
		const FVector L(62000.f, 48000.f, Street);
		const ECampusMat Saved = MatOverride;
		MatOverride = ECampusMat::TowerFar;
		BoxMM(EPal::TowerGreen, L + FVector(-3600.f, -3600.f, 0.f), L + FVector(3600.f, 3600.f, 9000.f), false);
		BoxMM(EPal::TowerGreen, L + FVector(-3000.f, -3000.f, 9000.f), L + FVector(3000.f, 3000.f, 11500.f), false);
		float Z = 11500.f;
		for (int32 Seg = 0; Seg < 8; ++Seg)
		{
			for (int32 k = 0; k < 4; ++k)
			{
				const float H2 = 1000.f;
				const float W = 2300.f + 220.f * k;
				BoxMM(EPal::TowerGreen, L + FVector(-W, -W, Z + k * H2), L + FVector(W, W, Z + (k + 1) * H2), false);
			}
			BoxMM(EPal::TowerDark, L + FVector(-2500.f, -2500.f, Z + 3950.f), L + FVector(2500.f, 2500.f, Z + 4050.f), false);
			Z += 4000.f;
		}
		BoxMM(EPal::TowerGreen, L + FVector(-1500.f, -1500.f, Z), L + FVector(1500.f, 1500.f, Z + 3200.f), false);
		MatOverride = ECampusMat::Count;
		Cyl(EPal::Galvanized, L + FVector(0.f, 0.f, Z + 3200.f), 300.f, 6000.f, false);
		Sphere(EPal::AviationRed, L + FVector(0.f, 0.f, Z + 9300.f), 200.f, false);
		MatOverride = Saved;
	}
	// The city at street level, block by block: each 60 m block between the avenue grid and the lanes is split into
	// lots, and every lot gets its own building, from four-storey walk-ups with rooftop tin to twenty-storey slabs.
	// Close to the hotel (the Xinyi grid) a share of the lots carry office towers instead, set square to the streets.
	auto OnAvenue = [](float V) { return FMath::IsNearlyZero(FMath::Fmod(FMath::Abs(V), 12000.f), 1.f); };
	for (float X = -66000.f; X < 66000.f; X += 6000.f)
	{
		for (float Y = -66000.f; Y < 66000.f; Y += 6000.f)
		{
			const float Cx = X + 3000.f;
			const float Cy = Y + 3000.f;
			const float R = FMath::Sqrt(Cx * Cx + Cy * Cy);
			if (R < 8500.f)
			{
				continue;
			}
			// Set back from an avenue (its carriageway and pavement), only a lane's width from the plain block edges.
			const float L0 = OnAvenue(X) ? 1300.f : 350.f;
			const float L1 = OnAvenue(X + 6000.f) ? 1300.f : 350.f;
			const float M0 = OnAvenue(Y) ? 1300.f : 350.f;
			const float M1 = OnAvenue(Y + 6000.f) ? 1300.f : 350.f;
			const float SplitX = FMath::Lerp(X + L0, X + 6000.f - L1, 0.38f + 0.24f * HRand((int32)X, (int32)Y + 11));
			const float SplitY = FMath::Lerp(Y + M0, Y + 6000.f - M1, 0.38f + 0.24f * HRand((int32)X + 5, (int32)Y));
			const bool bNear = R < 34000.f;
			const bool bFar = R > 30000.f;
			for (int32 Lot = 0; Lot < 4; ++Lot)
			{
				const float Ax = (Lot & 1) ? SplitX + 160.f : X + L0;
				const float Bx = (Lot & 1) ? X + 6000.f - L1 : SplitX - 160.f;
				const float Ay = (Lot & 2) ? SplitY + 160.f : Y + M0;
				const float By = (Lot & 2) ? Y + 6000.f - M1 : SplitY - 160.f;
				const float W = Bx - Ax;
				const float D = By - Ay;
				if (W < 600.f || D < 600.f)
				{
					continue;
				}
				const FBox2D LotBox(FVector2D(Ax, Ay), FVector2D(Bx, By));
				if (Taken.ContainsByPredicate([&](const FBox2D& T) { return T.Intersect(LotBox); }))
				{
					continue;
				}
				const uint32 H = HHash((int32)(Ax * 0.1f) + Lot * 131, (int32)(Ay * 0.1f) - Lot * 17);
				const FVector C((Ax + Bx) * 0.5f, (Ay + By) * 0.5f, Street);
				const float Pick = (H & 0xFFFF) / 65535.f;
				if (bNear && Pick < 0.16f && W > 1500.f && D > 1500.f)
				{
					// An office tower, its height falling away from the district's core.
					const float Tall = FMath::Lerp(16000.f, 6000.f, FMath::Clamp((R - 9000.f) / 25000.f, 0.f, 1.f)) * (0.6f + 0.8f * HRand((int32)H, 3));
					const EPal TowerSkin = Skins[(H >> 5) % UE_ARRAY_COUNT(Skins)];
					SkyTower(C, FMath::Min(W, D) * 0.8f, FMath::Min(W, D) * (0.55f + 0.25f * HRand((int32)H, 9)), Tall, TowerSkin, H, false);
					continue;
				}
				// Older fabric: walk-ups and mid-rise blocks, taller nearer the centre and along the avenues.
				const bool bAvenue = (Lot & 1 ? L1 : L0) > 1000.f || (Lot & 2 ? M1 : M0) > 1000.f;
				const float District = FMath::PerlinNoise2D(FVector2D(C.X, C.Y) / 22000.f) * 0.5f + 0.5f;
				int32 Floors = 3 + (int32)((H >> 8) % 4) + FMath::RoundToInt(District * District * 10.f) + (bAvenue ? 3 : 0) + (R < 26000.f ? 3 : 0);
				if (((H >> 20) & 7) == 0)
				{
					Floors += 8 + (int32)((H >> 12) % 8);
				}
				const float Fill = 0.78f + 0.18f * HRand((int32)H, 5);
				// CityBlock's width runs along its local Y.
				const bool bSquare = ((H >> 14) & 1) != 0;
				CityBlock(C, bSquare ? 0.f : 90.f, (bSquare ? D : W) * Fill, (bSquare ? W : D) * Fill, Floors, H, false, bFar);
			}
		}
	}
	bTerrain = true;
	TerrainOffset = FVector(0.f, 0.f, Street);
}

void FCampusSchoolBuildContext::HotelStreets(TArray<FBox2D>& Taken)
{
	// What the edge looks straight down on, after the view from a Tokyo rooftop deck: dark roadway with lane lines and
	// zebra crossings (scrambles at the junctions nearest the tower, people waiting at the corners), pavements lined
	// with trees, traffic in every lane, an elevated expressway on piers down one avenue, a rail corridor with trains,
	// and a park forest filling a few blocks. Detail thins out with distance, where it would be sub-pixel.
	const float Z = Street;
	constexpr float Grid = 12000.f;
	constexpr float Half = 900.f;
	constexpr float Walk = 1250.f;
	constexpr float Extent = 72000.f;
	const FBox2D Plaza(FVector2D(-7500.f, -7500.f), FVector2D(7500.f, 7500.f));
	// One block between the avenues: a pocket park, not a forest across half the view.
	const FBox2D Park(FVector2D(-47100.f, -23100.f), FVector2D(-36900.f, -12900.f));
	const float RailY = 30000.f;
	const float RailHalf = 1500.f;
	const float ExpX = -12000.f;
	const float ExpZ = Z + 1450.f;
	Taken.Add(Plaza);
	Taken.Add(Park);
	Taken.Add(FBox2D(FVector2D(-Extent, RailY - RailHalf - 100.f), FVector2D(Extent, RailY + RailHalf + 100.f)));
	auto Rand = [](float A, float B, int32 K) { return HRand((int32)(A * 0.37f) + K * 7919, (int32)(B * 0.53f) - K * 104729); };

	static const EPal Paints[] = { EPal::CityWhite, EPal::Black, EPal::ScooterWhite, EPal::Stainless, EPal::CityGray, EPal::ScooterRed, EPal::ScooterBlue, EPal::CityWhite, EPal::Black, EPal::TankWhite };
	auto Car = [&](const FVector& P, float Yaw, uint32 H, bool bDetail)
	{
		const FVector F = FRotator(0.f, Yaw, 0.f).Vector();
		const bool bBus = (H % 23) == 0;
		const bool bTruck = !bBus && (H % 9) == 0;
		const float L = bBus ? 1050.f : (bTruck ? 720.f : 440.f + 30.f * (float)((H >> 4) % 4));
		const float W = bBus ? 250.f : (bTruck ? 220.f : 180.f);
		const EPal Paint = bBus ? EPal::TileGreen : (bTruck ? EPal::CityWhite : Paints[(H >> 7) % UE_ARRAY_COUNT(Paints)]);
		if (!bDetail)
		{
			Box(Paint, P + FVector(0.f, 0.f, 70.f), FVector(L, W, 140.f), false, Yaw);
			return;
		}
		if (bBus || bTruck)
		{
			Box(Paint, P + FVector(0.f, 0.f, 165.f), FVector(L, W, 290.f), false, Yaw);
			Box(bBus ? EPal::Glass : EPal::SteelDark, P + F * (L * 0.5f - 40.f) + FVector(0.f, 0.f, 200.f), FVector(80.f, W + 2.f, 90.f), false, Yaw);
		}
		else
		{
			Box(Paint, P + FVector(0.f, 0.f, 50.f), FVector(L, W, 70.f), false, Yaw);
			Box(EPal::Glass, P - F * (L * 0.06f) + FVector(0.f, 0.f, 112.f), FVector(L * 0.48f, W * 0.86f, 55.f), false, Yaw);
			Box(Paint, P - F * (L * 0.06f) + FVector(0.f, 0.f, 141.f), FVector(L * 0.42f, W * 0.8f, 4.f), false, Yaw);
		}
		Box(EPal::LightPanel, P + F * (L * 0.5f + 1.f) + FVector(0.f, 0.f, 60.f), FVector(2.f, W * 0.8f, 8.f), false, Yaw);
		Box(EPal::SignRed, P - F * (L * 0.5f + 1.f) + FVector(0.f, 0.f, 70.f), FVector(2.f, W * 0.85f, 8.f), false, Yaw);
	};
	// Along-axis spans of an avenue left once the plaza round the tower and the park are taken out.
	auto FreeSpans = [&](float A, bool bAlongY, float Q0, float Q1)
	{
		TArray<FVector2D, TInlineAllocator<4>> Spans = { FVector2D(Q0, Q1) };
		for (const FBox2D& Box : { Plaza, Park })
		{
			const float C0 = bAlongY ? Box.Min.X : Box.Min.Y;
			const float C1 = bAlongY ? Box.Max.X : Box.Max.Y;
			if (A + Half <= C0 || A - Half >= C1)
			{
				continue;
			}
			const float B0 = bAlongY ? Box.Min.Y : Box.Min.X;
			const float B1 = bAlongY ? Box.Max.Y : Box.Max.X;
			TArray<FVector2D, TInlineAllocator<4>> Next;
			for (const FVector2D& S : Spans)
			{
				if (S.Y <= B0 || S.X >= B1)
				{
					Next.Add(S);
					continue;
				}
				if (S.X < B0)
				{
					Next.Add(FVector2D(S.X, B0));
				}
				if (S.Y > B1)
				{
					Next.Add(FVector2D(B1, S.Y));
				}
			}
			Spans = Next;
		}
		return Spans;
	};
	// World point from (across, along) for an avenue on X = A (bAlongY) or Y = A.
	auto P2 = [](bool bAlongY, float Across, float Along) { return bAlongY ? FVector2D(Across, Along) : FVector2D(Along, Across); };
	auto Strip = [&](EPal Pal, bool bAlongY, float Across0, float Across1, float Along0, float Along1, float Z0, float Z1)
	{
		const FVector2D A = P2(bAlongY, Across0, Along0);
		const FVector2D B = P2(bAlongY, Across1, Along1);
		Deco(Pal, FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), Z0), FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), Z1));
	};

	// ---------------------------------------------------------------- avenues
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const bool bAlongY = Axis == 0;
		const float Heading = bAlongY ? 90.f : 0.f;
		for (float A = -60000.f; A <= 60000.f; A += Grid)
		{
			for (float Q0 = -Extent; Q0 < Extent; Q0 += Grid)
			{
				const float Q1 = Q0 + Grid;
				for (const FVector2D& Span : FreeSpans(A, bAlongY, Q0 + Half, Q1 - Half))
				{
					const float S0 = Span.X;
					const float S1 = Span.Y;
					if (S1 - S0 < 200.f)
					{
						continue;
					}
					const FVector2D Mid = P2(bAlongY, A, (S0 + S1) * 0.5f);
					const float R = Mid.Size();
					const bool bNear = R < 40000.f;
					const bool bAtStart = FMath::IsNearlyEqual(S0, Q0 + Half, 1.f) && Q0 > -60001.f;
					const bool bAtEnd = FMath::IsNearlyEqual(S1, Q1 - Half, 1.f) && Q1 < 60001.f;
					Strip(EPal::Roadway, bAlongY, A - Half, A + Half, S0, S1, Z, Z + 2.f);
					Strip(EPal::PaverGray, bAlongY, A + Half, A + Walk, S0, S1, Z, Z + 15.f);
					Strip(EPal::PaverGray, bAlongY, A - Walk, A - Half, S0, S1, Z, Z + 15.f);
					if (S1 - S0 > 1600.f)
					{
						Strip(EPal::RoadPaint, bAlongY, A - 22.f, A - 10.f, S0 + 700.f, S1 - 700.f, Z + 2.f, Z + 2.6f);
						Strip(EPal::RoadPaint, bAlongY, A + 10.f, A + 22.f, S0 + 700.f, S1 - 700.f, Z + 2.f, Z + 2.6f);
					}
					if (bNear && S1 - S0 > 1600.f)
					{
						for (const float Off : { -450.f, 450.f })
						{
							for (float Q = S0 + 900.f; Q + 500.f < S1 - 700.f; Q += 1000.f)
							{
								Strip(EPal::RoadPaint, bAlongY, A + Off - 7.f, A + Off + 7.f, Q, Q + 500.f, Z + 2.f, Z + 2.6f);
							}
						}
						// Zebra crossings and stop lines at the junctions.
						for (const int32 End : { 0, 1 })
						{
							if ((End == 0 && !bAtStart) || (End == 1 && !bAtEnd))
							{
								continue;
							}
							const float Edge = End == 0 ? S0 : S1;
							const float Dir = End == 0 ? 1.f : -1.f;
							for (float X = A - Half + 45.f; X < A + Half - 45.f; X += 90.f)
							{
								Strip(EPal::RoadPaint, bAlongY, X - 22.f, X + 22.f, FMath::Min(Edge + Dir * 120.f, Edge + Dir * 520.f), FMath::Max(Edge + Dir * 120.f, Edge + Dir * 520.f), Z + 2.f, Z + 2.6f);
							}
							const float StopAt = Edge + Dir * 650.f;
							// Across the lanes coming into this junction (traffic heading -Q runs on the + side).
							const float Side = End == 0 ? 1.f : -1.f;
							Strip(EPal::RoadPaint, bAlongY, FMath::Min(A, A + Side * Half), FMath::Max(A, A + Side * Half), StopAt - 15.f, StopAt + 15.f, Z + 2.f, Z + 2.6f);
						}
						// Street trees on both pavements.
						for (float Q = S0 + 1500.f; Q < S1 - 1500.f; Q += 1000.f)
						{
							for (const float Sd : { -1.f, 1.f })
							{
								const FVector2D T = P2(bAlongY, A + Sd * (Half + 180.f), Q + 120.f * (Rand(A, Q, 3 + (int32)Sd) - 0.5f));
								const float Crown = 420.f + 260.f * Rand(T.X, T.Y, 5);
								Cyl(EPal::Trunk, FVector(T.X, T.Y, Z + 15.f), 32.f, 380.f, false);
								Sphere(Rand(T.X, T.Y, 6) < 0.3f ? EPal::Hedge : EPal::Leaves, FVector(T.X, T.Y, Z + 380.f + Crown * 0.35f), Crown);
							}
						}
					}
					// Street lamps: arm heads over the kerb on both sides.
					for (float Q = S0 + 600.f; Q < S1 - 300.f; Q += 2400.f)
					{
						for (const float Sd : { -1.f, 1.f })
						{
							const FVector2D L = P2(bAlongY, A + Sd * (Half + 40.f), Q);
							if (bNear)
							{
								Cyl(EPal::Galvanized, FVector(L.X, L.Y, Z + 15.f), 18.f, 800.f, false);
							}
							Deco(EPal::LedWarm, FVector(L.X - 18.f, L.Y - 18.f, Z + 800.f), FVector(L.X + 18.f, L.Y + 18.f, Z + 812.f));
						}
					}
					// Traffic: two lanes each way, keeping left.
					if (R < 62000.f)
					{
						for (const float Sd : { -1.f, 1.f })
						{
							for (const float Lane : { 250.f, 650.f })
							{
								float Q = S0 + 300.f + 600.f * Rand(A, Lane * Sd, 9);
								while (Q < S1 - 300.f)
								{
									const uint32 H = HHash((int32)(Q * 0.1f) + (int32)Lane, (int32)(A * 0.1f) + (int32)(Sd * 13.f) + Axis * 977);
									const float Step = 650.f + 1500.f * ((H >> 3 & 0xFF) / 255.f);
									if ((H & 0xFF) < 150)
									{
										const FVector2D C = P2(bAlongY, A - Sd * Lane, Q);
										Car(FVector(C.X, C.Y, Z + 2.f), Heading + (Sd > 0.f ? 0.f : 180.f), H, bNear);
									}
									Q += Step;
								}
							}
						}
					}
				}
			}
		}
	}
	// Junction squares; the four nearest the tower are scrambles, with people at the corners and out on the crossings.
	static const EPal Clothes[] = { EPal::BookCharcoal, EPal::Black, EPal::CityWhite, EPal::BookBlue, EPal::Canvas, EPal::BookCharcoal, EPal::BookRed, EPal::BookGray, EPal::Black, EPal::BookCream };
	for (float X = -60000.f; X <= 60000.f; X += Grid)
	{
		for (float Y = -60000.f; Y <= 60000.f; Y += Grid)
		{
			const FVector2D J(X, Y);
			if (Plaza.IsInside(J) || Park.ExpandBy(Half).IsInside(J))
			{
				continue;
			}
			Deco(EPal::Roadway, FVector(X - Half, Y - Half, Z), FVector(X + Half, Y + Half, Z + 2.f));
			const bool bScramble = FMath::Abs(X) + FMath::Abs(Y) < Grid + 1.f && X != ExpX;
			if (!bScramble)
			{
				continue;
			}
			for (const float Diag : { 45.f, -45.f })
			{
				const FVector D = FRotator(0.f, Diag, 0.f).Vector();
				for (float T = -1100.f; T <= 1100.f; T += 90.f)
				{
					BoxR(EPal::RoadPaint, FVector(X, Y, Z + 2.3f) + D * T, FVector(44.f, 420.f, 0.6f), FRotator(0.f, Diag, 0.f), false);
				}
			}
			for (int32 i = 0; i < 260; ++i)
			{
				const float U = Rand(X + i, Y, 11);
				const float V = Rand(X, Y + i, 12);
				FVector2D P;
				if (i < 170)
				{
					// Waiting at a corner, bunched up at the kerb.
					const float Cx = (i & 1) ? 1.f : -1.f;
					const float Cy = (i & 2) ? 1.f : -1.f;
					P = J + FVector2D(Cx * (Half + 40.f + 300.f * U * U), Cy * (Half + 40.f + 300.f * V * V));
				}
				else
				{
					// Out on the crossing, strung along it.
					const float T = (U - 0.5f) * 2.f * (Half + 150.f);
					const float Lat = (V - 0.5f) * 380.f;
					switch (i % 4)
					{
					case 0: P = J + FVector2D(T, Half + 320.f + Lat * 0.5f); break;
					case 1: P = J + FVector2D(Half + 320.f + Lat * 0.5f, T); break;
					case 2: P = J + FVector2D(T * 0.7f, T * 0.7f) + FVector2D(Lat, -Lat) * 0.35f; break;
					default: P = J + FVector2D(T * 0.7f, -T * 0.7f) + FVector2D(Lat, Lat) * 0.35f; break;
					}
				}
				const float Tall = 155.f + 25.f * Rand(P.X, P.Y, 13);
				Box(Clothes[i % UE_ARRAY_COUNT(Clothes)], FVector(P.X, P.Y, Z + 15.f + Tall * 0.5f), FVector(38.f, 46.f, Tall), false, 360.f * U);
			}
		}
	}

	// ---------------------------------------------------------------- the forecourt round the tower
	Deco(EPal::Stone2, FVector(Plaza.Min.X, Plaza.Min.Y, Z), FVector(Plaza.Max.X, Plaza.Max.Y, Z + 15.f));
	for (const float S : { -1.f, 1.f })
	{
		// Drop-off loops off the avenues that end at the forecourt, planters and a double row of trees either side.
		Deco(EPal::Roadway, FVector(-Half, S < 0.f ? Plaza.Min.Y : HY + 600.f, Z + 15.f), FVector(Half, S < 0.f ? -HY - 600.f : Plaza.Max.Y, Z + 17.f));
		Deco(EPal::Roadway, FVector(S < 0.f ? Plaza.Min.X : HX + 600.f, -Half, Z + 15.f), FVector(S < 0.f ? -HX - 600.f : Plaza.Max.X, Half, Z + 17.f));
		for (float T = -6500.f; T <= 6500.f; T += 900.f)
		{
			for (const float Row : { 5600.f, 6600.f })
			{
				for (const bool bAlongY : { true, false })
				{
					const FVector2D P = P2(bAlongY, S * Row, T);
					if (FMath::Abs(T) < Half + 300.f)
					{
						continue;
					}
					Box(EPal::Stone2, FVector(P.X, P.Y, Z + 50.f), FVector(260.f, 260.f, 70.f), false);
					Sphere(EPal::Leaves, FVector(P.X, P.Y, Z + 520.f), 520.f + 160.f * Rand(P.X, P.Y, 21));
				}
			}
		}
	}

	// ---------------------------------------------------------------- the park: a paved pocket park with a pond
	{
		// Mostly paving: a promenade round the edge under a ring of street trees, paths crossing to a plaza with the pond,
		// and four lawns between them, each with a clump of trees. From the deck it reads as a green block, not a field.
		const FVector2D C = Park.GetCenter();
		const float Edge = 900.f;
		const float Path = 350.f;
		Deco(EPal::PaverGray, FVector(Park.Min.X, Park.Min.Y, Z), FVector(Park.Max.X, Park.Max.Y, Z + 4.f));
		const FBox2D Plaza2(C - FVector2D(1600.f, 1600.f), C + FVector2D(1600.f, 1600.f));
		Deco(EPal::Stone2, FVector(Plaza2.Min.X, Plaza2.Min.Y, Z), FVector(Plaza2.Max.X, Plaza2.Max.Y, Z + 6.f));
		const FBox2D Pond(C - FVector2D(900.f, 650.f), C + FVector2D(900.f, 650.f));
		Deco(EPal::Stone, FVector(Pond.Min.X - 60.f, Pond.Min.Y - 60.f, Z), FVector(Pond.Max.X + 60.f, Pond.Max.Y + 60.f, Z + 14.f));
		Deco(EPal::Water, FVector(Pond.Min.X, Pond.Min.Y, Z), FVector(Pond.Max.X, Pond.Max.Y, Z + 16.f));
		int32 Tree = 0;
		auto TreeAt = [&](const FVector2D& P, float Crown)
		{
			const int32 k = Tree++;
			Cyl(EPal::Trunk, FVector(P.X, P.Y, Z), 22.f, Crown * 0.9f, false);
			Sphere(HRand(k, 34) < 0.4f ? EPal::Hedge : EPal::Leaves, FVector(P.X, P.Y, Z + Crown * 1.25f + 120.f * HRand(k, 35)), Crown);
		};
		for (int32 Qx = 0; Qx < 2; ++Qx)
		{
			for (int32 Qy = 0; Qy < 2; ++Qy)
			{
				const FVector2D Lo(Qx ? C.X + Path : Park.Min.X + Edge, Qy ? C.Y + Path : Park.Min.Y + Edge);
				const FVector2D Hi(Qx ? Park.Max.X - Edge : C.X - Path, Qy ? Park.Max.Y - Edge : C.Y - Path);
				const FBox2D Lawn(Lo, Hi);
				// The plaza takes the lawn's inner corner.
				Deco(EPal::Grass, FVector(Lo.X, Lo.Y, Z), FVector(Hi.X, Hi.Y, Z + 7.f));
				Deco(EPal::Stone2, FVector(FMath::Max(Lo.X, Plaza2.Min.X), FMath::Max(Lo.Y, Plaza2.Min.Y), Z), FVector(FMath::Min(Hi.X, Plaza2.Max.X), FMath::Min(Hi.Y, Plaza2.Max.Y), Z + 8.f));
				Deco(EPal::Hedge, FVector(Lo.X, Lo.Y, Z), FVector(Hi.X, Lo.Y + 60.f, Z + 70.f));
				Deco(EPal::Hedge, FVector(Lo.X, Hi.Y - 60.f, Z), FVector(Hi.X, Hi.Y, Z + 70.f));
				// A clump on the outer half of each lawn.
				const FVector2D Outer((Qx ? Hi.X : Lo.X) * 0.65f + C.X * 0.35f, (Qy ? Hi.Y : Lo.Y) * 0.65f + C.Y * 0.35f);
				for (int32 i = 0; i < 9; ++i)
				{
					const int32 Seed = Qx * 40 + Qy * 20 + i;
					const FVector2D P = Outer + FVector2D(HRand(Seed, 36) - 0.5f, HRand(Seed, 37) - 0.5f) * 1400.f;
					if (Lawn.ExpandBy(-300.f).IsInside(P) && !Plaza2.ExpandBy(300.f).IsInside(P))
					{
						TreeAt(P, 300.f + 220.f * HRand(Seed, 33));
					}
				}
			}
		}
		// Street trees round the promenade, a crown every 8 m.
		for (float T = 400.f; T < Park.GetSize().X - 300.f; T += 800.f)
		{
			TreeAt(FVector2D(Park.Min.X + T, Park.Min.Y + Edge * 0.5f), 330.f + 120.f * HRand((int32)T, 38));
			TreeAt(FVector2D(Park.Min.X + T, Park.Max.Y - Edge * 0.5f), 330.f + 120.f * HRand((int32)T, 39));
			TreeAt(FVector2D(Park.Min.X + Edge * 0.5f, Park.Min.Y + T), 330.f + 120.f * HRand((int32)T, 40));
			TreeAt(FVector2D(Park.Max.X - Edge * 0.5f, Park.Min.Y + T), 330.f + 120.f * HRand((int32)T, 41));
		}
	}

	// ---------------------------------------------------------------- the railway: four tracks in a fenced cutting
	{
		const float Y0 = RailY - RailHalf;
		const float Y1 = RailY + RailHalf;
		for (float A = -Extent; A < Extent; A += Grid)
		{
			// Between two avenue crossings.
			const float X0 = A + (A > -Extent ? Half : 0.f);
			const float X1 = A + Grid - Half;
			Deco(EPal::Stone, FVector(X0, Y0 + 100.f, Z), FVector(X1, Y1 - 100.f, Z + 40.f));
			Deco(EPal::GrilleMesh, FVector(X0, Y0, Z), FVector(X1, Y0 + 3.f, Z + 200.f));
			Deco(EPal::GrilleMesh, FVector(X0, Y1 - 3.f, Z), FVector(X1, Y1, Z + 200.f));
		}
		for (const float Track : { -975.f, -325.f, 325.f, 975.f })
		{
			Deco(EPal::Roadway, FVector(-Extent, RailY + Track - 130.f, Z + 40.f), FVector(Extent, RailY + Track + 130.f, Z + 42.f));
			for (const float Rail : { -54.f, 54.f })
			{
				Deco(EPal::Galvanized, FVector(-Extent, RailY + Track + Rail - 4.f, Z + 42.f), FVector(Extent, RailY + Track + Rail + 4.f, Z + 58.f));
			}
			Deco(EPal::Black, FVector(-Extent, RailY + Track - 1.5f, Z + 560.f), FVector(Extent, RailY + Track + 1.5f, Z + 563.f));
		}
		for (float X = -Extent + 2500.f; X < Extent; X += 5000.f)
		{
			if (FMath::Abs(FMath::Fmod(FMath::Abs(X), Grid) - Grid * 0.5f) > Grid * 0.5f - Half - 200.f)
			{
				continue;
			}
			Cyl(EPal::Galvanized, FVector(X, Y0 + 150.f, Z + 40.f), 26.f, 720.f, false);
			Cyl(EPal::Galvanized, FVector(X, Y1 - 150.f, Z + 40.f), 26.f, 720.f, false);
			Deco(EPal::SteelDark, FVector(X - 12.f, Y0 + 150.f, Z + 700.f), FVector(X + 12.f, Y1 - 150.f, Z + 725.f));
		}
		// Commuter trains: silver cars with a green band.
		const float Tracks[] = { -975.f, -325.f, 325.f, 975.f };
		for (int32 t = 0; t < 5; ++t)
		{
			const int32 Cars = 8 + (int32)(HRand(t, 41) * 4.f);
			const float Ty = RailY + Tracks[t % 4];
			float X = FMath::Lerp(-55000.f, 35000.f, HRand(t, 42));
			for (int32 c = 0; c < Cars; ++c)
			{
				Deco(EPal::TankWhite, FVector(X, Ty - 145.f, Z + 100.f), FVector(X + 2000.f, Ty + 145.f, Z + 440.f));
				Deco(EPal::Glass, FVector(X + 60.f, Ty - 146.f, Z + 300.f), FVector(X + 1940.f, Ty + 146.f, Z + 380.f));
				Deco(EPal::TileGreen, FVector(X, Ty - 147.f, Z + 250.f), FVector(X + 2000.f, Ty + 147.f, Z + 285.f));
				Deco(EPal::HvacGrey, FVector(X + 20.f, Ty - 125.f, Z + 440.f), FVector(X + 1980.f, Ty + 125.f, Z + 460.f));
				X += 2060.f;
			}
		}
	}

	// ---------------------------------------------------------------- the elevated expressway, over one avenue
	{
		for (float Q0 = -Extent; Q0 < Extent; Q0 += Grid)
		{
			const float Q1 = Q0 + Grid;
			Deco(EPal::Concrete, FVector(ExpX - 1150.f, Q0, ExpZ), FVector(ExpX + 1150.f, Q1, ExpZ + 200.f));
			Deco(EPal::Roadway, FVector(ExpX - 1120.f, Q0, ExpZ + 200.f), FVector(ExpX + 1120.f, Q1, ExpZ + 202.f));
			for (const float Sd : { -1.f, 1.f })
			{
				Deco(EPal::Concrete, FVector(ExpX + Sd * 1150.f - (Sd > 0.f ? 30.f : 0.f), Q0, ExpZ + 200.f), FVector(ExpX + Sd * 1150.f + (Sd < 0.f ? 30.f : 0.f), Q1, ExpZ + 310.f));
				Deco(EPal::Galvanized, FVector(ExpX + Sd * 1150.f - 3.f, Q0, ExpZ + 310.f), FVector(ExpX + Sd * 1150.f + 3.f, Q1, ExpZ + 420.f));
				for (float Q = Q0 + 500.f; Q < Q1; Q += 1000.f)
				{
					Deco(EPal::RoadPaint, FVector(ExpX + Sd * 560.f - 7.f, Q, ExpZ + 202.f), FVector(ExpX + Sd * 560.f + 7.f, Q + 500.f, ExpZ + 202.6f));
				}
			}
			Deco(EPal::Concrete, FVector(ExpX - 25.f, Q0, ExpZ + 202.f), FVector(ExpX + 25.f, Q1, ExpZ + 285.f));
			for (float Q = Q0 + 1800.f; Q < Q1 - 1000.f; Q += 3000.f)
			{
				BoxMM(EPal::Concrete, FVector(ExpX - 160.f, Q - 260.f, Z), FVector(ExpX + 160.f, Q + 260.f, ExpZ), false);
				BoxMM(EPal::Concrete, FVector(ExpX - 1000.f, Q - 220.f, ExpZ - 180.f), FVector(ExpX + 1000.f, Q + 220.f, ExpZ), false);
			}
			for (float Q = Q0 + 1500.f; Q < Q1; Q += 3000.f)
			{
				Cyl(EPal::Galvanized, FVector(ExpX - 1120.f, Q, ExpZ + 200.f), 16.f, 620.f, false);
				Deco(EPal::LedWarm, FVector(ExpX - 1110.f, Q - 18.f, ExpZ + 812.f), FVector(ExpX - 1000.f, Q + 18.f, ExpZ + 822.f));
			}
			const bool bNear = FMath::Abs((Q0 + Q1) * 0.5f) < 45000.f;
			for (const float Sd : { -1.f, 1.f })
			{
				for (const float Lane : { 280.f, 840.f })
				{
					for (float Q = Q0 + 400.f * Rand(Q0, Lane, 51); Q < Q1; )
					{
						const uint32 H = HHash((int32)(Q * 0.1f) + (int32)(Lane * Sd), 4243);
						if ((H & 0xFF) < 170)
						{
							Car(FVector(ExpX - Sd * Lane, Q, ExpZ + 202.f), 90.f + (Sd > 0.f ? 0.f : 180.f), H, bNear);
						}
						Q += 600.f + 1100.f * ((H >> 9 & 0xFF) / 255.f);
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------ spawns, hotspots, gunfight

void FCampusSchoolBuildContext::HotelMetadata()
{
	B->PlayableBounds = FBox(FVector(-HX - 100.f, -HY - 100.f, LowFloor - 100.f), FVector(HX + 100.f, HY + 100.f, LRoof + 600.f));
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = -2800.f + i * 800.f;
		Spawn(FVector(X, -4250.f + (i % 2) * 120.f, 110.f), 90.f, ECampusTeam::Alpha, false);
		Spawn(FVector(X, 4250.f - (i % 2) * 120.f, 110.f), -90.f, ECampusTeam::Bravo, false);
	}
	// Flex spawns at the lane ends and down in the service corridor.
	for (const float S : { -1.f, 1.f })
	{
		const float Yaw = S < 0.f ? 90.f : -90.f;
		Spawn(FVector(-2000.f, S * 3150.f, 110.f), Yaw, ECampusTeam::None, false);
		Spawn(FVector(2300.f, S * 3400.f, 110.f), Yaw, ECampusTeam::None, false);
		Spawn(FVector(550.f, S * 2200.f, LowFloor + 110.f), Yaw, ECampusTeam::None, false);
		Spawn(FVector(-1400.f, S * 2900.f, 110.f), Yaw, ECampusTeam::None, false);
	}
	const FVector Spots[] = {
		FVector(-1900.f, -1500.f, 0.f), FVector(-1900.f, 0.f, 0.f), FVector(-1900.f, 1500.f, 0.f), FVector(-2800.f, -900.f, PoolFloor), FVector(-2800.f, 900.f, PoolFloor),
		FVector(-1000.f, -1000.f, 0.f), FVector(-1000.f, 1000.f, 0.f), FVector(-300.f, -500.f, 0.f), FVector(-300.f, 500.f, 0.f), FVector(400.f, -1300.f, 0.f), FVector(400.f, 1300.f, 0.f),
		FVector(1000.f, -300.f, 0.f), FVector(0.f, 0.f, LRoof), FVector(0.f, -1200.f, LRoof), FVector(0.f, 1200.f, LRoof), FVector(-900.f, 0.f, LRoof),
		FVector(1600.f, -1500.f, 0.f), FVector(1600.f, 1500.f, 0.f), FVector(2150.f, -2300.f, 0.f), FVector(2150.f, 2300.f, 0.f), FVector(2700.f, -500.f, 0.f), FVector(2700.f, 500.f, 0.f),
		FVector(550.f, -1500.f, LowFloor), FVector(550.f, 1500.f, LowFloor), FVector(1600.f, -500.f, LowFloor), FVector(550.f, 0.f, LowFloor),
		FVector(0.f, -2200.f, 0.f), FVector(0.f, 2200.f, 0.f), FVector(-2500.f, -3000.f, 0.f), FVector(-2500.f, 3000.f, 0.f), FVector(-1500.f, -3800.f, 0.f), FVector(-1500.f, 3800.f, 0.f),
		FVector(1800.f, -3700.f, 0.f), FVector(1800.f, 3700.f, 0.f), FVector(800.f, -2900.f, 0.f), FVector(800.f, 2900.f, 0.f)
	};
	for (const FVector& S : Spots)
	{
		B->Hotspots.Add(S);
	}
	// Gunfight on the helipad: the stairs up are walled off, one spawn at each end, the flag in the H.
	B->GunfightBounds = FBox(FVector(LX0, -LY, LRoof), FVector(LX1, LY, LRoof + 400.f));
	B->GunfightFlag = FVector(0.f, 0.f, LRoof);
	Spawn(FVector(-200.f, -1500.f, LRoof + 110.f), 90.f, ECampusTeam::Alpha, true);
	Spawn(FVector(200.f, -1500.f, LRoof + 110.f), 90.f, ECampusTeam::Alpha, true);
	Spawn(FVector(-200.f, 1500.f, LRoof + 110.f), -90.f, ECampusTeam::Bravo, true);
	Spawn(FVector(200.f, 1500.f, LRoof + 110.f), -90.f, ECampusTeam::Bravo, true);
	for (const FVector& G : { FVector(0.f, -800.f, LRoof), FVector(0.f, 800.f, LRoof), FVector(-900.f, 0.f, LRoof), FVector(900.f, 0.f, LRoof), FVector(0.f, 0.f, LRoof) })
	{
		B->GunfightHotspots.Add(G);
	}
	for (const float S : { -1.f, 1.f })
	{
		GunfightWall(FVector(LX1 - 25.f, S * 1520.f, LRoof), FVector(LX1 + 200.f, S * 1700.f, LRoof + 300.f));
	}
}
