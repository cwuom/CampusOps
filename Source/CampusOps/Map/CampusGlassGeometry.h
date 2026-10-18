#pragma once

#include "CoreMinimal.h"

/** 2D fracture geometry for glass: convex polygons in pane coordinates (cm across the pane, cm up). */
namespace CampusGlassGeo
{
	using FGlassPoly = TArray<FVector2f, TInlineAllocator<12>>;

	/** Deterministic 0..1 from a position and a salt (every machine fractures the same pane the same way). */
	inline float Hash01(const FVector2f& P, uint32 Salt)
	{
		const int32 X = FMath::RoundToInt(P.X * 8.f);
		const int32 Y = FMath::RoundToInt(P.Y * 8.f);
		uint32 H = (uint32)X * 73856093u ^ (uint32)Y * 19349663u ^ (Salt + 1u) * 83492791u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return (H & 0xFFFFFF) / float(0xFFFFFF);
	}

	/** Keeps the part of a convex polygon with Dot(N, p) <= D. */
	inline FGlassPoly Clip(const FGlassPoly& In, const FVector2f& N, float D)
	{
		FGlassPoly Out;
		const int32 Num = In.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector2f& A = In[i];
			const FVector2f& B = In[(i + 1) % Num];
			const float Da = FVector2f::DotProduct(N, A) - D;
			const float Db = FVector2f::DotProduct(N, B) - D;
			if (Da <= 0.f)
			{
				Out.Add(A);
			}
			if ((Da < 0.f) != (Db < 0.f) && FMath::Abs(Da - Db) > KINDA_SMALL_NUMBER)
			{
				Out.Add(A + (B - A) * (Da / (Da - Db)));
			}
		}
		return Out;
	}

	inline float Area(const FGlassPoly& P)
	{
		float A = 0.f;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			const FVector2f& U = P[i];
			const FVector2f& V = P[(i + 1) % P.Num()];
			A += U.X * V.Y - V.X * U.Y;
		}
		return FMath::Abs(A) * 0.5f;
	}

	inline FVector2f Centroid(const FGlassPoly& P)
	{
		FVector2f C = FVector2f::ZeroVector;
		for (const FVector2f& V : P)
		{
			C += V;
		}
		return P.Num() > 0 ? C / (float)P.Num() : C;
	}

	inline bool Contains(const FGlassPoly& P, const FVector2f& Q)
	{
		if (P.Num() < 3)
		{
			return false;
		}
		float Sign = 0.f;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			const FVector2f E = P[(i + 1) % P.Num()] - P[i];
			const float C = E.X * (Q.Y - P[i].Y) - E.Y * (Q.X - P[i].X);
			if (FMath::Abs(C) < 1.e-4f)
			{
				continue;
			}
			if (Sign == 0.f)
			{
				Sign = FMath::Sign(C);
			}
			else if (FMath::Sign(C) != Sign)
			{
				return false;
			}
		}
		return true;
	}

	inline float SegmentDistance(const FVector2f& Q, const FVector2f& A, const FVector2f& B)
	{
		const FVector2f AB = B - A;
		const float T = FMath::Clamp(FVector2f::DotProduct(Q - A, AB) / FMath::Max(AB.SizeSquared(), 1.e-6f), 0.f, 1.f);
		return FVector2f::Distance(Q, A + AB * T);
	}

	/** 0 inside the polygon, else the distance to its outline. */
	inline float Distance(const FGlassPoly& P, const FVector2f& Q)
	{
		if (Contains(P, Q))
		{
			return 0.f;
		}
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < P.Num(); ++i)
		{
			Best = FMath::Min(Best, SegmentDistance(Q, P[i], P[(i + 1) % P.Num()]));
		}
		return Best;
	}

	/** Two fragments of the same fracture touch along an edge (or a corner). */
	inline bool Touching(const FGlassPoly& A, const FGlassPoly& B, float Eps = 0.05f)
	{
		for (const FVector2f& V : A)
		{
			for (int32 i = 0; i < B.Num(); ++i)
			{
				if (SegmentDistance(V, B[i], B[(i + 1) % B.Num()]) < Eps)
				{
					return true;
				}
			}
		}
		return false;
	}

	/** Voronoi split of a convex polygon by the seeds inside it (each cell clipped by its bisectors with the others). */
	inline void Voronoi(const FGlassPoly& Poly, const TArray<FVector2f>& Seeds, TArray<FGlassPoly>& OutCells)
	{
		TArray<FVector2f> Inside;
		for (const FVector2f& S : Seeds)
		{
			if (Contains(Poly, S) && !Inside.ContainsByPredicate([&](const FVector2f& O) { return FVector2f::DistSquared(O, S) < 0.25f; }))
			{
				Inside.Add(S);
			}
		}
		if (Inside.Num() < 2)
		{
			OutCells.Add(Poly);
			return;
		}
		for (int32 i = 0; i < Inside.Num(); ++i)
		{
			FGlassPoly Cell = Poly;
			for (int32 j = 0; j < Inside.Num() && Cell.Num() >= 3; ++j)
			{
				if (i != j)
				{
					const FVector2f N = Inside[j] - Inside[i];
					Cell = Clip(Cell, N, FVector2f::DotProduct(N, (Inside[i] + Inside[j]) * 0.5f));
				}
			}
			if (Cell.Num() >= 3 && Area(Cell) > 0.05f)
			{
				OutCells.Add(MoveTemp(Cell));
			}
		}
	}

	// the budget limits new cuts; incoming pieces are kept even when already over that limit
	inline TArray<FGlassPoly> ShortenDetachedStrips(const TArray<FGlassPoly>& Pieces, int32 Budget)
	{
		TArray<FGlassPoly> Result = Pieces;
		for (int32 i = 0; i < Result.Num() && Result.Num() < Budget;)
		{
			const FGlassPoly& Poly = Result[i];
			FVector2f Axis(1.f, 0.f);
			float LongestEdge = 0.f;
			for (int32 k = 0; k < Poly.Num(); ++k)
			{
				const FVector2f Edge = Poly[(k + 1) % Poly.Num()] - Poly[k];
				if (Edge.SizeSquared() > LongestEdge)
				{
					LongestEdge = Edge.SizeSquared();
					Axis = Edge.GetSafeNormal();
				}
			}
			const FVector2f Across(-Axis.Y, Axis.X);
			FBox2f Bounds(ForceInit);
			for (const FVector2f& Q : Poly)
			{
				Bounds += FVector2f(FVector2f::DotProduct(Q, Axis), FVector2f::DotProduct(Q, Across));
			}
			// keep ordinary plates and the original crack pattern, shortening only detached strips
			if (Poly.Num() < 3 || Bounds.GetSize().X <= FMath::Max(8.f, Bounds.GetSize().Y * 6.f))
			{
				++i;
				continue;
			}
			const float Middle = Bounds.GetCenter().X;
			FGlassPoly A = Clip(Poly, Axis, Middle);
			FGlassPoly B = Clip(Poly, -Axis, -Middle);
			if (A.Num() < 3 || B.Num() < 3 || Area(A) < 0.05f || Area(B) < 0.05f)
			{
				++i;
				continue;
			}
			Result[i] = MoveTemp(A);
			Result.Add(MoveTemp(B));
		}
		return Result;
	}

	/**
	 * How a pane fractures from a strike at At: a crushed ring of small chips round the hole, radial wedges further out
	 * (seeds on jittered spokes), and a few large irregular plates beyond. Scale grows the whole pattern (a grenade).
	 */
	inline void ImpactSeeds(const FVector2f& At, float Scale, uint32 Salt, TArray<FVector2f>& OutSeeds)
	{
		const int32 Spokes = 7 + (int32)(Hash01(At, Salt) * 4.f);
		const float Twist = Hash01(At, Salt + 1) * 2.f * PI;
		OutSeeds.Add(At);
		for (int32 s = 0; s < Spokes; ++s)
		{
			const float Ang = Twist + (s + 0.5f * Hash01(At, Salt + 10 + s)) * 2.f * PI / Spokes;
			const FVector2f Dir(FMath::Cos(Ang), FMath::Sin(Ang));
			// Along each spoke: a chip at the hole, a wedge, and every other spoke a long outer plate.
			const float R0 = Scale * (1.6f + 1.6f * Hash01(At, Salt + 30 + s));
			const float R1 = Scale * (7.f + 9.f * Hash01(At, Salt + 50 + s));
			const float R2 = Scale * (22.f + 26.f * Hash01(At, Salt + 70 + s));
			OutSeeds.Add(At + Dir * R0);
			OutSeeds.Add(At + Dir.GetRotated(14.f * (Hash01(At, Salt + 90 + s) - 0.5f)) * R1);
			if (s % 2 == 0)
			{
				OutSeeds.Add(At + Dir.GetRotated(20.f * (Hash01(At, Salt + 110 + s) - 0.5f)) * R2);
			}
		}
	}
}
