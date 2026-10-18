#include "Map/CampusBreakableGlass.h"
#include "Map/CampusWindowGlass.h"
#include "Map/CampusSchoolBuilder.h"
#include "CampusAssets.h"
#include "CampusTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "AI/NavigationSystemBase.h"
#include "UObject/UObjectIterator.h"

const FName UCampusBreakableGlass::GlassTag(TEXT("BreakableGlass"));

namespace
{
	using namespace CampusGlassGeo;

	/** Clear float glass as the palette draws it (GlassClear): near black, almost mirror smooth. */
	TArray<float> GlassLook(const FVector& Size)
	{
		return { 0.02f, 0.025f, 0.03f, 0.04f, 0.f, 0.f, 0.f, 0.f, (float)Size.X * 0.5f, (float)Size.Y * 0.5f, (float)Size.Z * 0.5f };
	}

	// A round knocks out the chips round its hole and weakens the wedges beyond: two or three close together bring
	// the middle of a pane down. Pieces this small or smaller take a strike's full weight.
	constexpr float BulletDamage = 0.65f;
	constexpr float BulletSpread = 26.f;
	constexpr float ChipArea = 14.f;
	constexpr int32 MaxFrags = 420;
	constexpr float CrackWidth = 0.16f;

	/** Adds a triangle facing Normal (engine front face: (C - A) x (B - A)). */
	void AddTri(TArray<int32>& Tris, const TArray<FVector>& V, int32 A, int32 B, int32 C, const FVector& Normal)
	{
		if (FVector::DotProduct(FVector::CrossProduct(V[C] - V[A], V[B] - V[A]), Normal) < 0.f)
		{
			Swap(B, C);
		}
		Tris.Append({ A, B, C });
	}
}

UCampusBreakableGlass::UCampusBreakableGlass()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

UInstancedStaticMeshComponent* UCampusBreakableGlass::MakeSet(const TCHAR* Name, bool bCollide)
{
	AActor* Owner = GetOwner();
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Owner ? static_cast<UObject*>(Owner) : this, MakeUniqueObjectName(Owner, UInstancedStaticMeshComponent::StaticClass(), Name));
	ISM->SetupAttachment(this);
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetCanEverAffectNavigation(false);
	ISM->bReceivesDecals = false;
	if (bCollide)
	{
		ISM->SetNumCustomDataFloats(ACampusSchoolBuilder::NumCustomFloats);
		ISM->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
		ISM->SetMaterial(0, CampusAssets::SurfaceMaterial(ECampusMat::Glass));
		// Only bullets stop at the glass: people, the camera and sight lines pass as they always have.
		ISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ISM->SetCollisionObjectType(ECC_WorldStatic);
		ISM->SetCollisionResponseToAllChannels(ECR_Ignore);
		ISM->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
		ISM->ComponentTags.Add(TEXT("Surface_Glass"));
		ISM->ComponentTags.Add(GlassTag);
	}
	else
	{
		ISM->SetStaticMesh(CampusAssets::PlaneMesh());
		ISM->SetMaterial(0, CampusAssets::GlassCrackMaterial());
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->TranslucencySortPriority = 2;
	}
	ISM->RegisterComponent();
	return ISM;
}

void UCampusBreakableGlass::SetPanes(const TArray<FBox>& InPanes, const TArray<uint8>& InTempered)
{
	if (!Whole)
	{
		Whole = MakeSet(TEXT("GlassWhole"), true);
		CrackStars = MakeSet(TEXT("GlassCracks"), false);
		AActor* Owner = GetOwner();
		Broken = NewObject<UProceduralMeshComponent>(Owner ? static_cast<UObject*>(Owner) : this, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("GlassBroken")));
		Broken->SetupAttachment(this);
		Broken->SetMobility(EComponentMobility::Movable);
		Broken->SetCanEverAffectNavigation(false);
		Broken->bReceivesDecals = false;
		Broken->SetCastShadow(false);
		Broken->bUseComplexAsSimpleCollision = true;
		Broken->bUseAsyncCooking = true;
		Broken->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Broken->SetCollisionObjectType(ECC_WorldStatic);
		Broken->SetCollisionResponseToAllChannels(ECR_Ignore);
		Broken->SetCollisionResponseToChannel(CAMPUS_TRACE_WEAPON, ECR_Block);
		Broken->ComponentTags.Add(TEXT("Surface_Glass"));
		Broken->ComponentTags.Add(GlassTag);
		Broken->RegisterComponent();
		CrackLineMaterial = CampusAssets::MakeLitMID(this, FLinearColor(0.08f, 0.09f, 0.09f), 0.35f, 0.f);
		CrackGlintMaterial = CampusAssets::MakeGlowMID(this, FLinearColor(0.8f, 0.86f, 0.9f), 1.2f, true);
	}
	Whole->ClearInstances();
	CrackStars->ClearInstances();
	Broken->ClearAllMeshSections();
	if (Solid)
	{
		Solid->ClearInstances();
	}
	NumSlots = 0;
	Panes.Reset();
	WholeToPane.Reset();
	SolidToPane.Reset();
	TArray<FTransform> Transforms;
	TArray<FTransform> SolidTransforms;
	Transforms.Reserve(InPanes.Num());
	for (int32 i = 0; i < InPanes.Num(); ++i)
	{
		const FBox& B = InPanes[i];
		FPane& P = Panes.AddDefaulted_GetRef();
		P.Box = B;
		P.bTempered = InTempered.IsValidIndex(i) && InTempered[i] != 0;
		P.bFrameless = InTempered.IsValidIndex(i) && InTempered[i] == 2;
		P.bSafety = InTempered.IsValidIndex(i) && InTempered[i] == 3;
		FVector U, N;
		Axes(P, U, N);
		P.Size = FVector2f((float)(B.GetSize() | U), (float)B.GetSize().Z);
		WholeToPane.Add(Panes.Num() - 1);
		Transforms.Add(FTransform(FQuat::Identity, B.GetCenter(), B.GetSize() / 100.f));
		if (P.bTempered && !P.bSafety)
		{
			// A few cm thicker than the glass so a sprinting capsule can't tunnel it, as tall as the sheet.
			SolidToPane.Add(i);
			SolidTransforms.Add(FTransform(FQuat::Identity, B.GetCenter(), (B.GetSize() + N * 6.f) / 100.f));
		}
	}
	Whole->AddInstances(Transforms, false, false);
	for (int32 i = 0; i < Panes.Num(); ++i)
	{
		Whole->SetCustomData(i, GlassLook(Panes[i].Box.GetSize()), i == Panes.Num() - 1);
	}
	if (SolidTransforms.Num() > 0)
	{
		if (!Solid)
		{
			AActor* Owner = GetOwner();
			Solid = NewObject<UInstancedStaticMeshComponent>(Owner ? static_cast<UObject*>(Owner) : this, MakeUniqueObjectName(Owner, UInstancedStaticMeshComponent::StaticClass(), TEXT("GlassSolid")));
			Solid->SetupAttachment(this);
			Solid->SetMobility(EComponentMobility::Movable);
			Solid->SetStaticMesh(CampusAssets::Shape(ECampusShape::Cube));
			Solid->SetHiddenInGame(true);
			Solid->SetVisibility(false);
			Solid->SetCastShadow(false);
			Solid->bReceivesDecals = false;
			// People and the navmesh only: rounds, sight lines and the camera still meet the glass itself.
			Solid->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Solid->SetCollisionObjectType(ECC_WorldStatic);
			Solid->SetCollisionResponseToAllChannels(ECR_Ignore);
			Solid->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Solid->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
			Solid->SetCanEverAffectNavigation(true);
			Solid->RegisterComponent();
		}
		Solid->AddInstances(SolidTransforms, false, false);
		FNavigationSystem::UpdateComponentData(*Solid);
	}
}

void UCampusBreakableGlass::ClearSolid(int32 PaneIndex)
{
	const int32 Instance = SolidToPane.IndexOfByKey(PaneIndex);
	if (Solid && Instance != INDEX_NONE)
	{
		// RemoveInstance dirties the navmesh round it: the bots can path through the opening from now on.
		Solid->RemoveInstance(Instance);
		SolidToPane.RemoveAt(Instance);
	}
}

void UCampusBreakableGlass::Remnants(int32 PaneIndex, TArray<FGlassPoly>& Out) const
{
	const FPane& P = Panes[PaneIndex];
	const float SX = P.Size.X;
	const float SY = P.Size.Y;
	const FVector2f Seed((float)PaneIndex * 1.37f, (float)P.Hits);
	// Tempered glass leaves a crumbly rim of little teeth in the channel; less at the sill, where the weight of the sheet
	// tore it out, most along the head and the jambs.
	auto Edge = [&](const FVector2f& From, const FVector2f& To, const FVector2f& In, float MaxDepth, uint32 Salt)
	{
		const FVector2f Along = To - From;
		const float Len = Along.Size();
		const FVector2f Dir = Along / FMath::Max(Len, 1.f);
		float T = 0.f;
		uint32 k = 0;
		while (T < Len - 1.f)
		{
			const float Step = FMath::Min(4.f + 12.f * Hash01(Seed, Salt + k * 3u), Len - T);
			const float Depth = MaxDepth * (0.25f + 0.75f * Hash01(Seed, Salt + k * 3u + 1u));
			const float Pick = Hash01(Seed, Salt + k * 3u + 2u);
			const FVector2f A = From + Dir * T;
			const FVector2f B = From + Dir * (T + Step);
			if (Pick < 0.2f)
			{
				// A gap where the rim came away with the sheet.
			}
			else if (Pick < 0.7f)
			{
				// A sharp tooth, leaning a little.
				const FVector2f Tip = A + Dir * (Step * (0.25f + 0.5f * Pick)) + In * Depth;
				Out.Add({ A, B, Tip });
			}
			else
			{
				// A broken-off stub with a slanted top.
				Out.Add({ A, B, B + In * Depth * 0.5f, A + In * Depth * 0.8f });
			}
			T += Step;
			++k;
		}
	};
	if (P.bFrameless)
	{
		// A balustrade panel is clamped only in its shoe: a jagged band stays along the foot, nothing hangs in the air.
		Edge(FVector2f(0.f, 0.f), FVector2f(SX, 0.f), FVector2f(0.f, 1.f), FMath::Min(9.f, SY * 0.08f), 4000u);
		return;
	}
	const float Head = FMath::Min(7.f, SY * 0.05f);
	const float Jamb = FMath::Min(6.f, SX * 0.05f);
	const float Sill = FMath::Min(2.f, SY * 0.02f);
	Edge(FVector2f(0.f, SY), FVector2f(SX, SY), FVector2f(0.f, -1.f), Head, 1000u);
	Edge(FVector2f(0.f, Sill), FVector2f(0.f, SY - Head), FVector2f(1.f, 0.f), Jamb, 2000u);
	Edge(FVector2f(SX, Sill), FVector2f(SX, SY - Head), FVector2f(-1.f, 0.f), Jamb, 3000u);
	Edge(FVector2f(Jamb, 0.f), FVector2f(SX - Jamb, 0.f), FVector2f(0.f, 1.f), Sill, 4000u);
}

void UCampusBreakableGlass::Axes(const FPane& P, FVector& U, FVector& N) const
{
	const FVector Size = P.Box.GetSize();
	const bool bThinX = Size.X < Size.Y;
	N = bThinX ? FVector::XAxisVector : FVector::YAxisVector;
	U = bThinX ? FVector::YAxisVector : FVector::XAxisVector;
}

FVector2f UCampusBreakableGlass::PaneCoords(const FPane& P, const FVector& Local) const
{
	FVector U, N;
	Axes(P, U, N);
	return FVector2f((float)((Local - P.Box.Min) | U), (float)(Local.Z - P.Box.Min.Z));
}

int32 UCampusBreakableGlass::FindPane(const FVector& Local, float Slack) const
{
	for (int32 i = 0; i < Panes.Num(); ++i)
	{
		if (!Panes[i].bDestroyed && Panes[i].Box.ExpandBy(Slack).IsInside(Local))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UCampusBreakableGlass::Split(int32 PaneIndex)
{
	FPane& P = Panes[PaneIndex];
	if (P.IsSplit())
	{
		return;
	}
	P.Slot = NumSlots++;
	FFrag& F = P.Frags.AddDefaulted_GetRef();
	F.Poly = { FVector2f(0.f, 0.f), FVector2f(P.Size.X, 0.f), P.Size, FVector2f(0.f, P.Size.Y) };
	F.Center = Centroid(F.Poly);
	F.Area = Area(F.Poly);
	const int32 Instance = WholeToPane.IndexOfByKey(PaneIndex);
	if (Instance != INDEX_NONE)
	{
		Whole->RemoveInstance(Instance);
		WholeToPane.RemoveAt(Instance);
	}
}

bool UCampusBreakableGlass::Fracture(int32 PaneIndex, const FVector2f& At, float Scale)
{
	FPane& P = Panes[PaneIndex];
	const int32 Index = P.Frags.IndexOfByPredicate([&](const FFrag& F) { return !F.bGone && Contains(F.Poly, At); });
	if (Index == INDEX_NONE)
	{
		// Through a hole already there.
		return false;
	}
	const uint32 Salt = (uint32)PaneIndex * 7919u + (uint32)P.Hits * 131u;
	++P.Hits;
	const FFrag Parent = P.Frags[Index];
	if (Parent.Area < ChipArea * Scale * Scale || P.Frags.Num() >= MaxFrags)
	{
		return true;
	}
	TArray<FVector2f> Seeds;
	ImpactSeeds(At, Scale, Salt, Seeds);
	// A few stray plates so the far side of a big piece doesn't stay one clean shape.
	FBox2f Extent(ForceInit);
	for (const FVector2f& V : Parent.Poly)
	{
		Extent += V;
	}
	for (int32 i = 0; i < 4; ++i)
	{
		Seeds.Add(FVector2f(FMath::Lerp(Extent.Min.X, Extent.Max.X, Hash01(At, Salt + 200 + i)), FMath::Lerp(Extent.Min.Y, Extent.Max.Y, Hash01(At, Salt + 300 + i))));
	}
	TArray<FGlassPoly> Cells;
	Voronoi(Parent.Poly, Seeds, Cells);
	if (Cells.Num() < 2)
	{
		return true;
	}
	P.Frags.RemoveAtSwap(Index);
	for (FGlassPoly& Cell : Cells)
	{
		FFrag& F = P.Frags.AddDefaulted_GetRef();
		F.Center = Centroid(Cell);
		F.Area = Area(Cell);
		// The pieces of an already cracked plate are weak from the start.
		F.Damage = Parent.Damage * 0.6f;
		F.Poly = MoveTemp(Cell);
	}
	return true;
}

void UCampusBreakableGlass::Damage(int32 PaneIndex, const FVector2f& At, float Amount, float Spread, TArray<int32>& OutFallen)
{
	FPane& P = Panes[PaneIndex];
	for (int32 i = 0; i < P.Frags.Num(); ++i)
	{
		FFrag& F = P.Frags[i];
		if (F.bGone)
		{
			continue;
		}
		const float D = Distance(F.Poly, At);
		const float Falloff = D <= 0.f ? 1.f : FMath::Pow(FMath::Clamp(1.f - D / Spread, 0.f, 1.f), 1.5f);
		if (Falloff <= 0.f)
		{
			continue;
		}
		// Small chips shatter outright, big plates shrug off a strike that only reaches them.
		const float SizeFactor = FMath::Clamp(FMath::Sqrt(ChipArea * 2.f / FMath::Max(F.Area, 0.5f)), 0.25f, 3.f);
		F.Damage += Amount * Falloff * SizeFactor;
		if (F.Damage >= 1.f)
		{
			F.bGone = true;
			OutFallen.Add(i);
		}
	}
	// Glass is only held by the frame: whatever is no longer connected to an edge through intact glass falls out.
	const int32 Num = P.Frags.Num();
	TArray<FBox2f> Boxes;
	Boxes.SetNum(Num);
	TArray<uint8> Held;
	Held.Init(0, Num);
	TArray<int32> Queue;
	float IntactArea = 0.f;
	const float Edge = 0.08f;
	for (int32 i = 0; i < Num; ++i)
	{
		const FFrag& F = P.Frags[i];
		Boxes[i] = FBox2f(ForceInit);
		for (const FVector2f& V : F.Poly)
		{
			Boxes[i] += V;
		}
		if (F.bGone)
		{
			continue;
		}
		IntactArea += F.Area;
		if (Boxes[i].Min.X < Edge || Boxes[i].Min.Y < Edge || Boxes[i].Max.X > P.Size.X - Edge || Boxes[i].Max.Y > P.Size.Y - Edge)
		{
			Held[i] = 1;
			Queue.Add(i);
		}
	}
	for (int32 q = 0; q < Queue.Num(); ++q)
	{
		const int32 a = Queue[q];
		const FBox2f Reach = Boxes[a].ExpandBy(0.1f);
		for (int32 b = 0; b < Num; ++b)
		{
			if (Held[b] || P.Frags[b].bGone || !Reach.Intersect(Boxes[b]) || !Touching(P.Frags[a].Poly, P.Frags[b].Poly))
			{
				continue;
			}
			Held[b] = 1;
			Queue.Add(b);
		}
	}
	// A pane down to a little glass, or crazed right across by a burst of rounds, gives way altogether. Tempered glass
	// holds its stress through the whole sheet: a second strike, or one heavy blow, and all of it lets go at once.
	// A door's safety glazing is a small tempered light: the first round through it brings all of it down.
	const bool bCollapse = IntactArea < 0.3f * P.Size.X * P.Size.Y || P.Hits >= (P.bSafety ? 1 : P.bTempered ? 2 : 5) || (P.bTempered && Amount >= 1.5f);
	for (int32 i = 0; i < Num; ++i)
	{
		FFrag& F = P.Frags[i];
		if (!F.bGone && (!Held[i] || bCollapse))
		{
			F.bGone = true;
			OutFallen.Add(i);
		}
	}
	P.bDestroyed = !P.Frags.ContainsByPredicate([](const FFrag& F) { return !F.bGone; });
}

void UCampusBreakableGlass::AddStrike(int32 PaneIndex, const FVector2f& At, const FVector& DirectionLocal, float Size)
{
	FPane& P = Panes[PaneIndex];
	if (P.Strikes.Num() >= 24)
	{
		return;
	}
	FVector U, N;
	Axes(P, U, N);
	// The star shows on the face the round came in through.
	const float Side = (DirectionLocal | N) > 0.f ? -1.f : 1.f;
	P.Strikes.Add(FVector4f(At.X, At.Y, Size, Side));
}

void UCampusBreakableGlass::RebuildPane(int32 PaneIndex)
{
	const FPane& P = Panes[PaneIndex];
	if (!P.IsSplit())
	{
		return;
	}
	const int32 GlassSection = P.Slot * 3;
	const int32 CrackSection = P.Slot * 3 + 1;
	const int32 GlintSection = P.Slot * 3 + 2;
	// What is left standing: the pieces still held, or the rim of a burst tempered sheet.
	TArray<FGlassPoly> Polys;
	TArray<float> PolyDamage;
	if (P.bDestroyed)
	{
		// Glazing beads hold a door light all round: nothing is left standing in them once it has come down.
		if (P.bTempered && !P.bSafety)
		{
			Remnants(PaneIndex, Polys);
			PolyDamage.Init(1.f, Polys.Num());
		}
	}
	else
	{
		for (const FFrag& F : P.Frags)
		{
			if (!F.bGone && F.Poly.Num() >= 3)
			{
				Polys.Add(F.Poly);
				PolyDamage.Add(F.Damage);
			}
		}
	}
	if (Polys.Num() == 0)
	{
		Broken->ClearMeshSection(GlassSection);
		Broken->ClearMeshSection(CrackSection);
		Broken->ClearMeshSection(GlintSection);
		return;
	}
	FVector U, N;
	Axes(P, U, N);
	const float Thick = (float)(P.Box.GetSize() | N);
	const FVector Base = P.Box.Min + N * (Thick * 0.5f);
	auto At = [&](const FVector2f& Q, float Side) { return Base + U * Q.X + FVector::UpVector * Q.Y + N * Side; };

	TArray<FVector> V, VC, VG;
	TArray<FVector> Nrm, NrmC, NrmG;
	TArray<FVector2D> UV, UVC, UVG;
	TArray<int32> Tris, TrisC, TrisG;
	auto Strip = [&](TArray<FVector>& SV, TArray<FVector>& SN, TArray<FVector2D>& SU, TArray<int32>& ST, const FVector2f& A, const FVector2f& B, const FVector2f& In, float From, float To, float Off, const FVector& FaceN)
	{
		const int32 First = SV.Num();
		SV.Append({ At(A + In * From, Off), At(B + In * From, Off), At(B + In * To, Off), At(A + In * To, Off) });
		SN.Append({ FaceN, FaceN, FaceN, FaceN });
		SU.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
		AddTri(ST, SV, First, First + 1, First + 2, FaceN);
		AddTri(ST, SV, First, First + 2, First + 3, FaceN);
	};
	const float OnFrame = 0.06f;
	auto IsFrameEdge = [&](const FVector2f& A, const FVector2f& B)
	{
		return (A.X < OnFrame && B.X < OnFrame) || (A.Y < OnFrame && B.Y < OnFrame)
			|| (A.X > P.Size.X - OnFrame && B.X > P.Size.X - OnFrame) || (A.Y > P.Size.Y - OnFrame && B.Y > P.Size.Y - OnFrame);
	};
	for (int32 PolyIndex = 0; PolyIndex < Polys.Num(); ++PolyIndex)
	{
		const FGlassPoly& Poly = Polys[PolyIndex];
		const FVector2f PolyCenter = Centroid(Poly);
		for (const float S : { 1.f, -1.f })
		{
			const FVector FaceN = N * S;
			const int32 First = V.Num();
			for (const FVector2f& Q : Poly)
			{
				V.Add(At(Q, S * Thick * 0.5f));
				Nrm.Add(FaceN);
				UV.Add(FVector2D(Q.X / 100.f, Q.Y / 100.f));
			}
			for (int32 k = 1; k + 1 < Poly.Num(); ++k)
			{
				AddTri(Tris, V, First, First + k, First + k + 1, FaceN);
			}
		}
		// Crack lines along every broken edge of the piece, on both faces: the fracture itself as a dark refracting
		// line, with a glint of scattered light just inside it, so it reads against a bright yard and a dim room alike.
		// Weakened pieces show theirs wider, crazed.
		const float W = CrackWidth * (1.f + 0.8f * FMath::Clamp(PolyDamage[PolyIndex], 0.f, 1.f));
		for (int32 k = 0; k < Poly.Num(); ++k)
		{
			const FVector2f& A = Poly[k];
			const FVector2f& B = Poly[(k + 1) % Poly.Num()];
			if (IsFrameEdge(A, B) || FVector2f::DistSquared(A, B) < 0.01f)
			{
				continue;
			}
			const FVector2f Mid = (A + B) * 0.5f;
			const FVector2f In = (PolyCenter - Mid).GetSafeNormal();
			for (const float S : { 1.f, -1.f })
			{
				const FVector FaceN = N * S;
				const float Off = S * (Thick * 0.5f + 0.03f);
				Strip(VC, NrmC, UVC, TrisC, A, B, In, 0.f, W, Off, FaceN);
				Strip(VG, NrmG, UVG, TrisG, A, B, In, W, W * 1.7f, Off + S * 0.01f, FaceN);
			}
		}
	}
	Broken->CreateMeshSection(GlassSection, V, Tris, Nrm, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	Broken->SetMaterial(GlassSection, CampusAssets::SurfaceMaterial(ECampusMat::Glass));
	Broken->CreateMeshSection(CrackSection, VC, TrisC, NrmC, UVC, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Broken->SetMaterial(CrackSection, CrackLineMaterial);
	Broken->CreateMeshSection(GlintSection, VG, TrisG, NrmG, UVG, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Broken->SetMaterial(GlintSection, CrackGlintMaterial);
}

void UCampusBreakableGlass::RebuildStrikes()
{
	CrackStars->ClearInstances();
	TArray<FTransform> CrackT;
	for (const FPane& P : Panes)
	{
		if (!P.IsSplit() || P.bDestroyed)
		{
			continue;
		}
		FVector U, N;
		Axes(P, U, N);
		const float Thick = (float)(P.Box.GetSize() | N);
		for (const FVector4f& K : P.Strikes)
		{
			// The crushed star round a hole stays while glass round it is left.
			const FVector2f At(K.X, K.Y);
			if (!P.Frags.ContainsByPredicate([&](const FFrag& F) { return !F.bGone && Distance(F.Poly, At) < K.Z * 0.25f; }))
			{
				continue;
			}
			const FVector Normal = N * K.W;
			const FVector Location = P.Box.Min + U * K.X + FVector::UpVector * K.Y + N * (Thick * 0.5f) + Normal * (Thick * 0.5f + 0.06f);
			const float Spin = Hash01(At, 23) * 2.f * PI;
			const FQuat Q = FRotationMatrix::MakeFromZX(Normal, U).ToQuat() * FQuat(FVector::UpVector, Spin);
			CrackT.Add(FTransform(Q, Location, FVector(K.Z / 100.f)));
		}
	}
	if (CrackT.Num() > 0)
	{
		CrackStars->AddInstances(CrackT, false, false);
	}
}

void UCampusBreakableGlass::Release(int32 PaneIndex, const TArray<int32>& Fallen, const FVector& HitLocal, const FVector& DirectionWorld, bool bEffects, float Strength)
{
	RebuildPane(PaneIndex);
	RebuildStrikes();
	const FPane& P = Panes[PaneIndex];
	if (P.bTempered && P.bDestroyed)
	{
		ClearSolid(PaneIndex);
	}
	if (!bEffects || Fallen.Num() == 0)
	{
		return;
	}
	FVector U, N;
	Axes(P, U, N);
	const FTransform& T = GetComponentTransform();
	const float Thick = (float)(P.Box.GetSize() | N);
	TArray<FGlassPoly> Pieces;
	Pieces.Reserve(Fallen.Num());
	float FallenArea = 0.f;
	for (const int32 i : Fallen)
	{
		Pieces.Add(P.Frags[i].Poly);
		FallenArea += P.Frags[i].Area;
	}
	if (P.bTempered && P.bDestroyed)
	{
		// A tempered sheet comes down as a cascade of small pieces, not as plates: every big piece crumbles on the way.
		TArray<FGlassPoly> Crumbs;
		int32 Budget = 260;
		for (const FGlassPoly& Piece : Pieces)
		{
			const float PieceArea = Area(Piece);
			const int32 Count = FMath::Clamp(FMath::RoundToInt(PieceArea / 420.f), 1, FMath::Min(28, Budget));
			if (Count <= 1)
			{
				Crumbs.Add(Piece);
				--Budget;
				continue;
			}
			FBox2f Extent(ForceInit);
			for (const FVector2f& Q : Piece)
			{
				Extent += Q;
			}
			TArray<FVector2f> Seeds;
			for (int32 s = 0; s < Count; ++s)
			{
				Seeds.Add(FVector2f(FMath::FRandRange(Extent.Min.X, Extent.Max.X), FMath::FRandRange(Extent.Min.Y, Extent.Max.Y)));
			}
			TArray<FGlassPoly> Cells;
			Voronoi(Piece, Seeds, Cells);
			Budget -= Cells.Num();
			Crumbs.Append(Cells.Num() > 0 ? Cells : TArray<FGlassPoly>({ Piece }));
		}
		Pieces = MoveTemp(Crumbs);
		Strength *= 0.5f;
	}
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Origin = T.TransformPosition(P.Box.Min + N * (Thick * 0.5f));
	if (ACampusGlassShards* Shards = World->SpawnActor<ACampusGlassShards>(ACampusGlassShards::StaticClass(), FTransform(Origin), Params))
	{
		// A door's leaf (and its pawn blocker) is what the light was set in; the building's walls still stop the pieces.
		if (!Cast<ACampusSchoolBuilder>(GetOwner()))
		{
			Shards->Source = GetOwner();
		}
		Shards->Init(Origin, T.TransformVectorNoScale(U), T.TransformVectorNoScale(FVector::UpVector), T.TransformVectorNoScale(N), Thick, Pieces,
			T.TransformPosition(HitLocal), DirectionWorld, Strength);
	}
	// A hole's worth of chips is a crunch; a sheet coming down is the full crash.
	const float Share = FMath::Clamp(FallenArea / 1800.f, 0.f, 1.f);
	if (Share > 0.08f)
	{
		CampusAudio::PlayAt(this, TEXT("Glass_Break"), T.TransformPosition(HitLocal), FMath::Lerp(0.55f, 1.f, Share), 0.06f, CampusAssets::EAttenuation::Gunshot);
	}
}

bool UCampusBreakableGlass::Shoot(const FVector& Location, const FVector& Direction, bool bEffects)
{
	if (!Whole)
	{
		return false;
	}
	const FVector Local = GetComponentTransform().InverseTransformPosition(Location);
	const int32 Index = FindPane(Local, 3.f);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Split(Index);
	const FVector2f At = PaneCoords(Panes[Index], Local);
	// Tempered glass crazes far out from the first strike instead of punching a neat hole.
	const bool bTempered = Panes[Index].bTempered;
	if (!Fracture(Index, At, bTempered ? 2.4f : 1.f))
	{
		return true;
	}
	const FVector DirLocal = GetComponentTransform().InverseTransformVectorNoScale(Direction);
	AddStrike(Index, At, DirLocal, (9.f + 7.f * Hash01(At, 11)) * (bTempered ? 2.f : 1.f));
	TArray<int32> Fallen;
	Damage(Index, At, BulletDamage, BulletSpread, Fallen);
	UE_LOG(LogTemp, Verbose, TEXT("[CampusGlass] pane %d strike %d at %s: %d pieces, %d fell%s"), Index, Panes[Index].Hits, *At.ToString(), Panes[Index].Frags.Num(), Fallen.Num(), Panes[Index].bDestroyed ? TEXT(", gone") : TEXT(""));
	Release(Index, Fallen, Local, Direction, bEffects, 0.35f);
	return true;
}

bool UCampusBreakableGlass::Smash(const FVector& Location, const FVector& Direction, float Radius, bool bEffects)
{
	if (!Whole)
	{
		return false;
	}
	const FVector Local = GetComponentTransform().InverseTransformPosition(Location);
	const int32 Index = FindPane(Local, 6.f);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Split(Index);
	const FVector2f At = PaneCoords(Panes[Index], Local);
	Fracture(Index, At, FMath::Max(Radius / 14.f, 1.5f));
	AddStrike(Index, At, GetComponentTransform().InverseTransformVectorNoScale(Direction), Radius);
	TArray<int32> Fallen;
	Damage(Index, At, 2.f, Radius, Fallen);
	Release(Index, Fallen, Local, Direction, bEffects, 0.6f);
	return true;
}

void UCampusBreakableGlass::Blast(const FVector& Center, float Radius, bool bEffects, const AActor* Ignore)
{
	if (!Whole)
	{
		return;
	}
	UWorld* World = GetWorld();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CampusBlastGlass), false, Ignore);
	Query.AddIgnoredActor(GetOwner());
	const FTransform& T = GetComponentTransform();
	const FVector LocalCenter = T.InverseTransformPosition(Center);
	for (int32 i = 0; i < Panes.Num(); ++i)
	{
		if (Panes[i].bDestroyed)
		{
			continue;
		}
		// The point of the pane nearest the blast takes the brunt.
		const FVector Near = Panes[i].Box.GetClosestPointTo(LocalCenter);
		const FVector NearWorld = T.TransformPosition(Near);
		const float Dist = FVector::Dist(NearWorld, Center);
		if (Dist > Radius)
		{
			continue;
		}
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Center, T.TransformPosition(Panes[i].Box.GetCenter()), ECC_Visibility, Query))
		{
			continue;
		}
		const float Strength = 1.f - Dist / Radius;
		Split(i);
		const FVector Dir = (NearWorld - Center).GetSafeNormal();
		const FVector2f At = PaneCoords(Panes[i], Near);
		// The overpressure shatters the pane right across from the point facing the blast.
		Fracture(i, At, 1.5f + 2.5f * Strength);
		TArray<int32> Fallen;
		if (Strength > 0.55f)
		{
			// Close in, the whole pane blows out.
			Damage(i, At, 10.f, 1.e5f, Fallen);
		}
		else
		{
			AddStrike(i, At, T.InverseTransformVectorNoScale(Dir), 30.f);
			Damage(i, At, 1.2f + 2.f * Strength, 50.f + 160.f * Strength, Fallen);
		}
		Release(i, Fallen, Near, Dir, bEffects, 0.4f + Strength);
	}
}

TArray<FBox> UCampusBreakableGlass::GetPaneBounds() const
{
	TArray<FBox> Out;
	for (const FPane& P : Panes)
	{
		if (!P.bDestroyed)
		{
			Out.Add(P.Box.TransformBy(GetComponentTransform()));
		}
	}
	return Out;
}

bool UCampusBreakableGlass::IsGlass(const UPrimitiveComponent* Component)
{
	return Component && Component->ComponentHasTag(GlassTag);
}

bool UCampusBreakableGlass::ShootAt(UWorld* World, const FVector& Location, const FVector& Direction)
{
	if (!World)
	{
		return false;
	}
	const bool bEffects = World->GetNetMode() != NM_DedicatedServer;
	for (TObjectIterator<UCampusBreakableGlass> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsRegistered() && It->Shoot(Location, Direction, bEffects))
		{
			return true;
		}
	}
	return false;
}

bool UCampusBreakableGlass::SmashAt(UWorld* World, const FVector& Location, const FVector& Direction, float Radius)
{
	if (!World)
	{
		return false;
	}
	const bool bEffects = World->GetNetMode() != NM_DedicatedServer;
	for (TObjectIterator<UCampusBreakableGlass> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsRegistered() && It->Smash(Location, Direction, Radius, bEffects))
		{
			return true;
		}
	}
	return false;
}

void UCampusBreakableGlass::BlastAt(UWorld* World, const FVector& Center, float Radius, const AActor* Ignore)
{
	if (!World)
	{
		return;
	}
	const bool bEffects = World->GetNetMode() != NM_DedicatedServer;
	for (TObjectIterator<UCampusBreakableGlass> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsRegistered())
		{
			It->Blast(Center, Radius, bEffects, Ignore);
		}
	}
}
