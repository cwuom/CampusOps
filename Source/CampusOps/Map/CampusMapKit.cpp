#include "Map/CampusSchoolLayout.h"
#include "Map/CampusSchoolBuilder.h"

namespace
{
	uint32 KitHash(uint32 A, uint32 B)
	{
		uint32 H = A * 73856093u ^ B * 19349663u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return H;
	}

	float KitRand(uint32 Seed, uint32 Salt)
	{
		return (KitHash(Seed, Salt) & 0xFFFF) / 65535.f;
	}
}

void FCampusSchoolBuildContext::Fixture(const FVector& Ceiling, bool bAlongY, float Lumens)
{
	Box(EPal::Stainless, Ceiling - FVector(0.f, 0.f, 3.f), bAlongY ? FVector(28.f, 128.f, 6.f) : FVector(128.f, 28.f, 6.f), false);
	Box(EPal::LightPanel, Ceiling - FVector(0.f, 0.f, 6.5f), bAlongY ? FVector(22.f, 120.f, 1.f) : FVector(120.f, 22.f, 1.f), false);
	CeilingLight(Ceiling - FVector(0.f, 0.f, 12.f), bAlongY, Lumens);
}

void FCampusSchoolBuildContext::StraightStair(const FVector& Bottom, const FVector& Top, float Width, EPal Tread, EPal Rail, bool bRails)
{
	const FVector Flat(Top.X - Bottom.X, Top.Y - Bottom.Y, 0.f);
	const float Run = Flat.Size();
	const float Rise = Top.Z - Bottom.Z;
	if (Run < 10.f || Rise < 5.f)
	{
		return;
	}
	const bool bAlongX = FMath::Abs(Flat.X) >= FMath::Abs(Flat.Y);
	const FVector Dir = Flat / Run;
	const FVector Across = FVector::CrossProduct(FVector::UpVector, Dir);
	const int32 Steps = FMath::Max(2, FMath::RoundToInt(Rise / 18.f));
	const float Step = Run / Steps;
	const float StepRise = Rise / Steps;
	const float Yaw = Dir.Rotation().Yaw;
	// Open steel treads (5 cm plates with a nosing) on two inclined stringers; the invisible ramp is what is walked on.
	for (int32 i = 0; i < Steps; ++i)
	{
		const FVector C = Bottom + Dir * (Step * (i + 0.5f)) + FVector(0.f, 0.f, StepRise * (i + 1) - 2.5f);
		Box(Tread, C, FVector(Step + 2.f, Width - 8.f, 5.f), false, Yaw);
	}
	const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
	const float Length = FMath::Sqrt(Run * Run + Rise * Rise);
	for (const float Side : { -1.f, 1.f })
	{
		const FVector Mid = (Bottom + Top) * 0.5f + Across * Side * (Width * 0.5f - 3.f) - FVector(0.f, 0.f, 6.f);
		BoxR(Rail, Mid, FVector(Length, 6.f, 26.f), FRotator(Pitch, Yaw, 0.f), false);
		if (bRails)
		{
			BoxR(Rail, Mid + FVector(0.f, 0.f, 100.f), FVector(Length, 5.f, 5.f), FRotator(Pitch, Yaw, 0.f), false);
			const int32 Posts = FMath::Max(2, FMath::RoundToInt(Run / 130.f));
			for (int32 p = 0; p <= Posts; ++p)
			{
				const float T = (float)p / Posts;
				const FVector Base = FMath::Lerp(Bottom, Top, T) + Across * Side * (Width * 0.5f - 3.f);
				Box(Rail, Base + FVector(0.f, 0.f, 50.f), FVector(4.f, 4.f, 100.f), false);
			}
		}
	}
	Ramp(Bottom, Top, Width - 6.f, bAlongX);
	// A short level lip at the top so the capsule steps off the ramp onto the deck instead of catching its edge.
	Ramp(Top, Top + Dir * 20.f, Width - 6.f, bAlongX);
}

namespace
{
	struct FSpan
	{
		float A0;
		float A1;
	};

	/** Splits [A0, A1] at the openings into glazed spans. */
	TArray<FSpan> GlazedSpans(float A0, float A1, const TArray<float>& Cuts0, const TArray<float>& Cuts1)
	{
		TArray<FSpan> Out;
		float Cursor = A0;
		for (int32 i = 0; i < Cuts0.Num(); ++i)
		{
			if (Cuts0[i] > Cursor)
			{
				Out.Add({ Cursor, FMath::Min(Cuts0[i], A1) });
			}
			Cursor = FMath::Max(Cursor, Cuts1[i]);
		}
		if (Cursor < A1)
		{
			Out.Add({ Cursor, A1 });
		}
		return Out;
	}
}

void FCampusSchoolBuildContext::GlassWall(bool bAlongX, float Fixed, float A0, float A1, float Z0, float Z1, float Bay, const TArray<FOpening>& Openings, EPal Frame)
{
	// Along-wall position A, offset N off the wall's centre plane, height Z.
	auto At = [&](float A, float N, float Z) { return bAlongX ? FVector(A, Fixed + N, Z) : FVector(Fixed + N, A, Z); };
	auto Solid = [&](EPal P, float Aa, float Na, float Za, float Ab, float Nb, float Zb) { BoxMM(P, At(Aa, Na, Za), At(Ab, Nb, Zb)); };
	auto Trim = [&](EPal P, float Aa, float Na, float Za, float Ab, float Nb, float Zb) { Deco(P, At(Aa, Na, Za), At(Ab, Nb, Zb)); };
	// Black EPDM gaskets round a pane on both faces, where the glass sits in the frame.
	auto Gaskets = [&](float Pa, float Pb, float Za, float Zb)
	{
		for (const float S : { 1.f, -1.f })
		{
			const float N0 = S * 1.f;
			const float N1 = S * 1.7f;
			const float Na = FMath::Min(N0, N1);
			const float Nb = FMath::Max(N0, N1);
			Trim(EPal::Black, Pa, Na, Za, Pa + 1.3f, Nb, Zb);
			Trim(EPal::Black, Pb - 1.3f, Na, Za, Pb, Nb, Zb);
			Trim(EPal::Black, Pa, Na, Za, Pb, Nb, Za + 1.3f);
			Trim(EPal::Black, Pa, Na, Zb - 1.3f, Pb, Nb, Zb);
		}
	};
	// A mullion: the 6 cm box section and its snap-on pressure caps on both faces.
	auto Mullion = [&](float A, float Za, float Zb)
	{
		Solid(Frame, A - 3.f, -7.f, Za, A + 3.f, 7.f, Zb);
		Trim(Frame, A - 2.5f, 7.f, Za, A + 2.5f, 10.f, Zb);
		Trim(Frame, A - 2.5f, -10.f, Za, A + 2.5f, -7.f, Zb);
	};

	TArray<FOpening> Sorted = Openings;
	Sorted.Sort([](const FOpening& A, const FOpening& Bo) { return A.A0 < Bo.A0; });
	TArray<float> C0;
	TArray<float> C1;
	for (const FOpening& O : Sorted)
	{
		C0.Add(O.A0);
		C1.Add(O.A1);
		// Door frame and the glazed transom over the doorway.
		Solid(Frame, O.A0 - 6.f, -8.f, Z0, O.A0, 8.f, Z1);
		Solid(Frame, O.A1, -8.f, Z0, O.A1 + 6.f, 8.f, Z1);
		Solid(Frame, O.A0, -8.f, O.Z1, O.A1, 8.f, O.Z1 + 8.f);
		if (O.Z1 + 20.f < Z1 - 12.f)
		{
			WindowPane(At(O.A0 + 2.f, -1.f, O.Z1 + 8.f), At(O.A1 - 2.f, 1.f, Z1 - 12.f), true);
			Gaskets(O.A0 + 2.f, O.A1 - 2.f, O.Z1 + 8.f, Z1 - 12.f);
			GlazingBlocker(At(O.A0, -3.f, O.Z1), At(O.A1, 3.f, Z1));
		}
	}
	// A low sill: an easy step over once the glass is out.
	const float Kick = 12.f;
	Solid(Frame, A0, -8.f, Z1 - 12.f, A1, 8.f, Z1);
	for (const FSpan& S : GlazedSpans(A0, A1, C0, C1))
	{
		Solid(Frame, S.A0, -7.f, Z0, S.A1, 7.f, Z0 + Kick);
		// Brushed threshold cap on the sill, a shade proud of it on both faces.
		Trim(EPal::Stainless, S.A0, -8.5f, Z0 + Kick - 1.f, S.A1, 8.5f, Z0 + Kick + 0.4f);
		const int32 Bays = FMath::Max(1, FMath::RoundToInt((S.A1 - S.A0) / Bay));
		const float W = (S.A1 - S.A0) / Bays;
		for (int32 i = 0; i < Bays; ++i)
		{
			const float A = S.A0 + W * i;
			Mullion(A, Z0 + Kick, Z1 - 12.f);
			WindowPane(At(A + 3.f, -1.f, Z0 + Kick), At(A + W - 3.f, 1.f, Z1 - 12.f), true);
			Gaskets(A + 3.f, A + W - 3.f, Z0 + Kick, Z1 - 12.f);
		}
		Mullion(S.A1, Z0 + Kick, Z1 - 12.f);
		GlazingBlocker(At(S.A0, -3.f, Z0), At(S.A1, 3.f, Z1));
	}
}

void FCampusSchoolBuildContext::GlassWallX(float Y, float X0, float X1, float Z0, float Z1, float Bay, const TArray<FOpening>& Openings, EPal Frame)
{
	GlassWall(true, Y, X0, X1, Z0, Z1, Bay, Openings, Frame);
}

void FCampusSchoolBuildContext::GlassWallY(float X, float Y0, float Y1, float Z0, float Z1, float Bay, const TArray<FOpening>& Openings, EPal Frame)
{
	GlassWall(false, X, Y0, Y1, Z0, Z1, Bay, Openings, Frame);
}

void FCampusSchoolBuildContext::GlassRail(const FVector& A, const FVector& Bo, float Height, float BlockHeight)
{
	const bool bAlongX = FMath::Abs(Bo.X - A.X) >= FMath::Abs(Bo.Y - A.Y);
	const FVector Min(FMath::Min(A.X, Bo.X), FMath::Min(A.Y, Bo.Y), A.Z);
	const FVector Max(FMath::Max(A.X, Bo.X), FMath::Max(A.Y, Bo.Y), A.Z);
	const FVector T = bAlongX ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
	// Laminated panes in a floor shoe, a round steel cap rail on top.
	Deco(EPal::Stainless, Min - T * 4.f, Max + T * 4.f + FVector(0.f, 0.f, 10.f));
	const float Len = bAlongX ? Max.X - Min.X : Max.Y - Min.Y;
	// Tempered panels between the joints that shatter one at a time when shot. A plain guard (block no taller than
	// the rail) is open under its solid cap once a panel is out; a roof edge keeps its tall invisible wall.
	const bool bEdge = BlockHeight > Height + 10.f;
	const FVector Along = bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
	const int32 Panels = FMath::Max(1, FMath::CeilToInt(Len / 150.f - 0.01f));
	for (int32 i = 0; i < Panels; ++i)
	{
		const float S0 = Len * i / Panels + (i > 0 ? 1.f : 0.f);
		const float S1 = Len * (i + 1) / Panels - (i + 1 < Panels ? 1.f : 0.f);
		WindowPane(Min + Along * S0 - T * 1.f + FVector(0.f, 0.f, 10.f), Min + Along * S1 + T * 1.f + FVector(0.f, 0.f, Height - 6.f), true, true);
	}
	const FVector Cap0 = Min + FVector(0.f, 0.f, Height - 3.f);
	const FVector Cap1 = Max + FVector(0.f, 0.f, Height - 3.f);
	CylR(EPal::Stainless, (Cap0 + Cap1) * 0.5f, 6.f, Len, bAlongX ? FRotator(90.f, 0.f, 0.f) : FRotator(90.f, 90.f, 0.f), !bEdge);
	// Panel joints every 1.5 m.
	for (float S = 0.f; S <= Len; S += 150.f)
	{
		const FVector P = bAlongX ? FVector(Min.X + S, Min.Y, Min.Z) : FVector(Min.X, Min.Y + S, Min.Z);
		Deco(EPal::Stainless, P - T * 2.f - (bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f)) + FVector(0.f, 0.f, 10.f),
			P + T * 2.f + (bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f)) + FVector(0.f, 0.f, Height - 6.f));
	}
	if (bEdge)
	{
		Blocker(Min - T * 5.f, Max + T * 5.f + FVector(0.f, 0.f, BlockHeight));
	}
	else
	{
		GlazingBlocker(Min - T * 5.f, Max + T * 5.f + FVector(0.f, 0.f, BlockHeight));
	}
}

void FCampusSchoolBuildContext::SkyTower(const FVector& Ground, float Width, float Depth, float Height, EPal Skin, uint32 Seed, bool bShadow)
{
	const ECampusMat Saved = MatOverride;
	MatOverride = bShadow ? ECampusMat::Tower : ECampusMat::TowerFar;
	// Stone podium, then the shaft in up to three tiers stepping in, each capped by a dark mechanical floor.
	const float Podium = FMath::Min(1400.f, Height * 0.08f);
	MatOverride = bShadow ? ECampusMat::Count : ECampusMat::FacadeFar;
	BoxMM(EPal::Stone2, Ground + FVector(-Width * 0.6f, -Depth * 0.6f, 0.f), Ground + FVector(Width * 0.6f, Depth * 0.6f, Podium), bShadow);
	MatOverride = bShadow ? ECampusMat::Tower : ECampusMat::TowerFar;
	const uint32 Form = Seed % 4;
	const int32 Tiers = Form == 0 ? 1 : (Form == 1 ? 2 : (Height > 15000.f ? 3 : 2));
	float Z = Ground.Z + Podium;
	float W = Width;
	float D = Depth;
	const float Shaft = Height - Podium;
	for (int32 t = 0; t < Tiers; ++t)
	{
		const float Share = Tiers == 1 ? 1.f : (t == 0 ? 0.55f : (t == 1 ? (Tiers == 2 ? 0.45f : 0.28f) : 0.17f));
		const float TierH = Shaft * Share;
		const float OffsetX = Form == 3 ? Width * 0.12f * t : 0.f;
		const float OffsetY = Form == 1 ? Depth * 0.08f * t : 0.f;
		BoxMM(Skin, FVector(Ground.X + OffsetX - W * 0.5f, Ground.Y + OffsetY - D * 0.5f, Z), FVector(Ground.X + OffsetX + W * 0.5f, Ground.Y + OffsetY + D * 0.5f, Z + TierH - 400.f), bShadow);
		// Mechanical floor: louvred band a little proud of the glass.
		BoxMM(EPal::TowerDark, FVector(Ground.X + OffsetX - W * 0.5f - 15.f, Ground.Y + OffsetY - D * 0.5f - 15.f, Z + TierH - 400.f), FVector(Ground.X + OffsetX + W * 0.5f + 15.f, Ground.Y + OffsetY + D * 0.5f + 15.f, Z + TierH), bShadow);
		Z += TierH;
		const float Step = 0.7f + 0.15f * KitRand(Seed, 10 + t);
		if (t + 1 < Tiers)
		{
			W *= Form == 1 ? 0.92f : Step;
			D *= Form == 3 ? 0.92f : Step;
		}
	}
	MatOverride = bShadow ? ECampusMat::Count : ECampusMat::FacadeFar;
	// Crown: a parapet frame, rooftop plant, sometimes a spire or a helipad; a red aviation lamp on the highest point.
	const float Crown = 300.f + 500.f * KitRand(Seed, 20);
	const FVector Top(Ground.X + (Form == 3 ? Width * 0.12f * (Tiers - 1) : 0.f), Ground.Y + (Form == 1 ? Depth * 0.08f * (Tiers - 1) : 0.f), Z);
	BoxMM(EPal::Galvanized, Top + FVector(-W * 0.5f, -D * 0.5f, 0.f), Top + FVector(W * 0.5f, D * 0.5f, 20.f), false);
	if (KitRand(Seed, 21) < 0.45f)
	{
		BoxMM(EPal::HvacGrey, Top + FVector(-W * 0.3f, -D * 0.25f, 0.f), Top + FVector(W * 0.2f, D * 0.25f, Crown), false);
	}
	float Highest = Z + Crown;
	if (Height > 14000.f && KitRand(Seed, 22) < 0.35f)
	{
		const float Spire = 1500.f + 2500.f * KitRand(Seed, 23);
		Cyl(EPal::Galvanized, Top, 120.f, Spire, false);
		Highest = Z + Spire;
	}
	else
	{
		for (const float Sx : { -1.f, 1.f })
		{
			Deco(EPal::TowerDark, Top + FVector(Sx * W * 0.5f - (Sx > 0.f ? 60.f : 0.f), -D * 0.5f, 0.f), Top + FVector(Sx * W * 0.5f + (Sx < 0.f ? 60.f : 0.f), D * 0.5f, Crown));
		}
	}
	Sphere(EPal::AviationRed, FVector(Top.X, Top.Y, Highest + 40.f), 60.f, false);
	MatOverride = Saved;
}
