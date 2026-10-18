#include "Map/CampusWindowGlass.h"
#include "Map/CampusBreakableGlass.h"
#include "Map/CampusSchoolBuilder.h"
#include "CampusAssets.h"
#include "CampusTypes.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

// ------------------------------------------------------------------ panes

ACampusWindowGlass::ACampusWindowGlass()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	Glass = CreateDefaultSubobject<UCampusBreakableGlass>(TEXT("Glass"));
	RootComponent = Glass;
}

void ACampusWindowGlass::SetPanes(const TArray<FBox>& InPanes, const TArray<uint8>& InTempered)
{
	Glass->SetPanes(InPanes, InTempered);
}

TArray<FBox> ACampusWindowGlass::GetPanes() const
{
	return Glass->GetPaneBounds();
}

bool ACampusWindowGlass::BreakAt(UWorld* World, const FVector& Location, const FVector& Direction)
{
	return UCampusBreakableGlass::ShootAt(World, Location, Direction);
}

// ------------------------------------------------------------------ shards

namespace
{
	constexpr int32 MaxDebrisFragments = 420;

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

ACampusGlassShards::ACampusGlassShards()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->bReceivesDecals = false;
}

namespace
{
	/** The building and what stands in it, not people: glass lands on the floor, not on whoever was standing there. */
	FCollisionResponseParams GlassRestResponse()
	{
		FCollisionResponseParams Response;
		Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
		Response.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);
		return Response;
	}
}

float ACampusGlassShards::FloorBelow(const FVector& From) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusGlassFloor), false, this);
	Params.AddIgnoredActor(Source.Get());
	const bool bSupported = GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 1500.f), ECC_Visibility, Params, GlassRestResponse())
		&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z >= 0.5f;
	return bSupported ? Hit.ImpactPoint.Z : From.Z - 1500.f;
}

void ACampusGlassShards::Resample(FShard& S, const FVector& PreviousPosition) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusGlassWall), false, this);
	Params.AddIgnoredActor(Source.Get());
	const FVector Level(S.Position.X, S.Position.Y, S.SampledAt.Z);
	// Glass does not pass through walls: it glances off them and drops.
	if (GetWorld()->LineTraceSingleByChannel(Hit, S.SampledAt, Level, ECC_Visibility, Params, GlassRestResponse()) && Hit.ImpactNormal.Z < 0.5f)
	{
		const FVector Normal2D = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		S.Position.X = Hit.Location.X + Normal2D.X * 2.f;
		S.Position.Y = Hit.Location.Y + Normal2D.Y * 2.f;
		const FVector Reflected = FMath::GetReflectionVector(S.Velocity, Normal2D);
		S.Velocity = FVector(Reflected.X * 0.25f, Reflected.Y * 0.25f, FMath::Min(S.Velocity.Z, 0.f));
	}
	// Over the edge of a landing or a stair, or onto a step: the floor under it now.
	const float Clearance = Thickness * 0.5f + 0.1f;
	S.FloorZ = FloorBelow(FVector(S.Position.X, S.Position.Y, FMath::Max(S.Position.Z, PreviousPosition.Z) + Clearance)) + Clearance;
	S.SampledAt = S.Position;
}

void ACampusGlassShards::AddShard(const FVector& Center, const CampusGlassGeo::FGlassPoly& Local, const FVector& Across, const FVector& Up, const FVector& Normal)
{
	FShard& S = Shards.AddDefaulted_GetRef();
	S.Poly = Local;
	for (const FVector2f& Q : Local)
	{
		S.Corners.Add(Across * Q.X + Up * Q.Y);
	}
	S.Normal = Normal;
	S.Across = Across;
	S.Up = Up;
	S.Position = Center;
	S.SampledAt = Center;
	S.Velocity = FVector::ZeroVector;
	S.Rotation = FQuat::Identity;
	S.Spin = FVector::ZeroVector;
	S.Area = CampusGlassGeo::Area(Local);
	S.FloorZ = Center.Z - 1500.f;
	S.Delay = 0.f;
	S.bResting = false;
	bTopologyDirty = true;
}

void ACampusGlassShards::Init(const FVector& Origin, const FVector& U, const FVector& Up, const FVector& N, float InThickness, const TArray<CampusGlassGeo::FGlassPoly>& Pieces,
	const FVector& HitPoint, const FVector& Direction, float Strength)
{
	Thickness = FMath::Max(InThickness, 0.3f);
	const TArray<CampusGlassGeo::FGlassPoly> DrawnPieces = CampusGlassGeo::ShortenDetachedStrips(Pieces, MaxDebrisFragments);
	// The pieces are simulated and drawn in world space.
	SetActorTransform(FTransform::Identity);
	const FVector Dir = Direction.GetSafeNormal();
	// Most of the glass is carried on through with the round; the rest drops back on the shooter's side.
	const float Through = FVector::DotProduct(Dir, N) >= 0.f ? 1.f : -1.f;
	FBox Bounds(ForceInit);
	for (const CampusGlassGeo::FGlassPoly& Poly : DrawnPieces)
	{
		for (const FVector2f& Q : Poly)
		{
			Bounds += Origin + U * Q.X + Up * Q.Y;
		}
	}
	const float FloorThrough = FloorBelow(Bounds.GetCenter() + N * Through * 45.f);
	const float FloorBack = FloorBelow(Bounds.GetCenter() - N * Through * 45.f);
	const FVector2f Hit2D((float)FVector::DotProduct(HitPoint - Origin, U), (float)FVector::DotProduct(HitPoint - Origin, Up));
	for (const CampusGlassGeo::FGlassPoly& Poly : DrawnPieces)
	{
		if (Poly.Num() < 3)
		{
			continue;
		}
		const FVector2f C = CampusGlassGeo::Centroid(Poly);
		CampusGlassGeo::FGlassPoly Local;
		for (const FVector2f& Q : Poly)
		{
			Local.Add(Q - C);
		}
		AddShard(Origin + U * C.X + Up * C.Y, Local, U, Up, N);
		FShard& S = Shards.Last();
		const float Dist = FVector2f::Distance(C, Hit2D);
		const float Near = FMath::Clamp(1.f - Dist / 70.f, 0.f, 1.f);
		const FVector2f Away = (C - Hit2D).GetSafeNormal();
		const float Push = FMath::Lerp(40.f, 330.f, Near * Near) * FMath::FRandRange(0.6f, 1.2f) * FMath::Lerp(0.7f, 1.5f, FMath::Clamp(Strength, 0.f, 1.f));
		const bool bBack = FMath::FRand() < 0.18f;
		S.Velocity = N * Through * (bBack ? -0.3f : 1.f) * Push + (U * Away.X + Up * Away.Y) * FMath::FRandRange(20.f, 90.f) * Near + FMath::VRand() * 15.f;
		// Big plates turn over slowly, chips flicker.
		S.Spin = FMath::VRand() * FMath::Lerp(3.f, 14.f, Near) / FMath::Max(FMath::Sqrt(S.Area / 30.f), 1.f);
		// The cracks run out from the strike: far pieces let go a moment later, the top ones last.
		S.Delay = Dist / 2400.f + (C.Y / FMath::Max(Bounds.GetSize().Z, 1.f)) * 0.05f * FMath::FRand();
		S.FloorZ = (FVector::DotProduct(S.Velocity, N) * Through >= 0.f ? FloorThrough : FloorBack) + Thickness * 0.5f + 0.1f;
	}
	BuildMesh(true);
}

void ACampusGlassShards::BuildMesh(bool bTopology)
{
	TArray<FVector> V, VS;
	TArray<FVector> Nrm, NrmS;
	TArray<FVector2D> UV, UVS;
	TArray<int32> Tris, TrisS;
	const float Half = Thickness * 0.5f;
	const float Clear = FMath::Clamp((Age - 28.f) / 1.5f, 0.f, 1.f);
	for (const FShard& S : Shards)
	{
		const float Scale = 1.f - Clear;
		const FVector Nw = S.Rotation.RotateVector(S.Normal);
		TArray<FVector, TInlineAllocator<12>> Wc;
		for (const FVector& K : S.Corners)
		{
			Wc.Add(S.Position + S.Rotation.RotateVector(K) * Scale);
		}
		for (const float Side : { 1.f, -1.f })
		{
			const int32 First = V.Num();
			for (int32 k = 0; k < Wc.Num(); ++k)
			{
				V.Add(Wc[k] + Nw * (Side * Half));
				Nrm.Add(Nw * Side);
				UV.Add(FVector2D(S.Poly[k].X / 100.f, S.Poly[k].Y / 100.f));
			}
			if (bTopology)
			{
				for (int32 k = 1; k + 1 < Wc.Num(); ++k)
				{
					AddTri(Tris, V, First, First + k, First + k + 1, Nw * Side);
				}
			}
		}
		// The broken edges: pale, frosted, catching the light even when the faces are clear.
		const FVector Mid = S.Position;
		for (int32 k = 0; k < Wc.Num(); ++k)
		{
			const FVector& A = Wc[k];
			const FVector& B = Wc[(k + 1) % Wc.Num()];
			FVector Out = FVector::CrossProduct(B - A, Nw).GetSafeNormal();
			if (FVector::DotProduct(Out, (A + B) * 0.5f - Mid) < 0.f)
			{
				Out = -Out;
			}
			const int32 First = VS.Num();
			VS.Append({ A + Nw * Half, B + Nw * Half, B - Nw * Half, A - Nw * Half });
			NrmS.Append({ Out, Out, Out, Out });
			UVS.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			if (bTopology)
			{
				AddTri(TrisS, VS, First, First + 1, First + 2, Out);
				AddTri(TrisS, VS, First, First + 2, First + 3, Out);
			}
		}
	}
	if (bTopology)
	{
		Mesh->CreateMeshSection(0, V, Tris, Nrm, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		Mesh->SetMaterial(0, CampusAssets::SurfaceMaterial(ECampusMat::Glass));
		Mesh->CreateMeshSection(1, VS, TrisS, NrmS, UVS, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		// transparent edges keep thin glass from reading as opaque green framing
		if (!Mesh->GetMaterial(1) || !Mesh->GetMaterial(1)->IsA<UMaterialInstanceDynamic>())
		{
			UMaterialInstanceDynamic* Edge = UMaterialInstanceDynamic::Create(CampusAssets::SurfaceMaterial(ECampusMat::Glass), this);
			if (Edge)
			{
				Edge->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.08f, 0.13f, 0.11f));
				Mesh->SetMaterial(1, Edge);
			}
		}
	}
	else
	{
		Mesh->UpdateMeshSection(0, V, Nrm, UV, TArray<FColor>(), TArray<FProcMeshTangent>());
		Mesh->UpdateMeshSection(1, VS, NrmS, UVS, TArray<FColor>(), TArray<FProcMeshTangent>());
	}
}

void ACampusGlassShards::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float Dt = FMath::Min(DeltaSeconds, 0.04f);
	const float Clear = FMath::Clamp((Age - 28.f) / 1.5f, 0.f, 1.f);
	bool bMoving = false;
	TArray<FShard> Broke;
	for (int32 i = Shards.Num() - 1; i >= 0; --i)
	{
		FShard& S = Shards[i];
		if (S.bResting)
		{
			continue;
		}
		if (Age < S.Delay)
		{
			bMoving = true;
			continue;
		}
		const FVector PreviousPosition = S.Position;
		S.Velocity.Z -= 980.f * Dt;
		S.Velocity *= FMath::Exp(-0.35f * Dt);
		S.Position += S.Velocity * Dt;
		const float SpinRate = S.Spin.Size();
		if (SpinRate > KINDA_SMALL_NUMBER)
		{
			S.Rotation = FQuat(S.Spin / SpinRate, SpinRate * Dt) * S.Rotation;
		}
		bMoving = true;
		if (FVector::DistSquared2D(S.Position, S.SampledAt) > FMath::Square(20.f))
		{
			Resample(S, PreviousPosition);
		}
		if (S.Position.Z > S.FloorZ)
		{
			continue;
		}
		// the pane-wide estimate and a nearby rail cannot support a chip at a different position
		const float Clearance = Thickness * 0.5f + 0.1f;
		S.FloorZ = FloorBelow(FVector(S.Position.X, S.Position.Y, PreviousPosition.Z + Clearance)) + Clearance;
		if (S.Position.Z > S.FloorZ)
		{
			continue;
		}
		S.Position.Z = S.FloorZ;
		const int32 AvailableChildren = MaxDebrisFragments - (Shards.Num() + Broke.Num() - 1);
		if (S.Velocity.Z < -90.f && S.Area > 120.f && AvailableChildren >= 2)
		{
			// A big plate hitting the floor bursts into pieces that skid off every way.
			TArray<FVector2f> Seeds;
			const int32 Count = FMath::Min(FMath::Clamp(FMath::RoundToInt(S.Area / 60.f), 3, 8), AvailableChildren);
			FBox2f B(ForceInit);
			for (const FVector2f& Q : S.Poly)
			{
				B += Q;
			}
			if (Count == 2)
			{
				const FVector2f Center = CampusGlassGeo::Centroid(S.Poly);
				const FVector2f First = S.Poly[0];
				FVector2f Other = First;
				for (const FVector2f& Q : S.Poly)
				{
					if (FVector2f::DistSquared(Q, First) > FVector2f::DistSquared(Other, First)) Other = Q;
				}
				Seeds.Add((Center + First) * 0.5f);
				Seeds.Add((Center + Other) * 0.5f);
			}
			else
			{
				for (int32 k = 0; k < Count; ++k)
				{
					Seeds.Add(FVector2f(FMath::FRandRange(B.Min.X, B.Max.X), FMath::FRandRange(B.Min.Y, B.Max.Y)));
				}
			}
			TArray<CampusGlassGeo::FGlassPoly> Parts;
			CampusGlassGeo::Voronoi(S.Poly, Seeds, Parts);
			const FVector Across = S.Rotation.RotateVector(S.Across);
			const FVector UpAxis = S.Rotation.RotateVector(S.Up);
			const FVector Nw = S.Rotation.RotateVector(S.Normal);
			for (const CampusGlassGeo::FGlassPoly& Part : Parts)
			{
				const FVector2f C = CampusGlassGeo::Centroid(Part);
				CampusGlassGeo::FGlassPoly Local;
				for (const FVector2f& Q : Part)
				{
					Local.Add(Q - C);
				}
				FShard& N = Broke.AddDefaulted_GetRef();
				N.Poly = Local;
				for (const FVector2f& Q : Local)
				{
					N.Corners.Add(Across * Q.X + UpAxis * Q.Y);
				}
				N.Normal = Nw;
				N.Across = Across;
				N.Up = UpAxis;
				N.Position = S.Position + Across * C.X + UpAxis * C.Y;
				N.Position.Z = S.FloorZ + 0.5f;
				N.SampledAt = N.Position;
				N.Rotation = FQuat::Identity;
				N.Area = CampusGlassGeo::Area(Local);
				N.FloorZ = S.FloorZ;
				N.Delay = 0.f;
				N.bResting = false;
				const FVector Out = FVector(N.Position.X - S.Position.X, N.Position.Y - S.Position.Y, 0.f).GetSafeNormal();
				N.Velocity = FVector(S.Velocity.X * 0.4f, S.Velocity.Y * 0.4f, -S.Velocity.Z * FMath::FRandRange(0.1f, 0.25f)) + Out * FMath::FRandRange(40.f, 140.f);
				N.Spin = FMath::VRand() * FMath::FRandRange(4.f, 12.f);
			}
			Shards.RemoveAtSwap(i);
			++Bounced;
			continue;
		}
		if (S.Velocity.Z < -90.f)
		{
			// Glass skips and skitters: a weak bounce that keeps most of the slide.
			S.Velocity = FVector(S.Velocity.X * 0.5f, S.Velocity.Y * 0.5f, -S.Velocity.Z * 0.22f);
			S.Spin *= 0.35f;
			++Bounced;
		}
		else if (S.Velocity.SizeSquared2D() > FMath::Square(8.f))
		{
			// low vertical speed still leaves a chip sliding off a sill under its remaining momentum
			S.Velocity = FVector(S.Velocity.X * 0.8f, S.Velocity.Y * 0.8f, 0.f);
			S.Spin *= 0.5f;
		}
		else
		{
			// Settles flat on the floor, whichever face down.
			const FVector Nw = S.Rotation.RotateVector(S.Normal);
			S.Rotation = FQuat::FindBetweenNormals(Nw, Nw.Z >= 0.f ? FVector::UpVector : -FVector::UpVector) * S.Rotation;
			S.bResting = true;
			S.Velocity = FVector::ZeroVector;
		}
	}
	if (Broke.Num() > 0)
	{
		Shards.Append(MoveTemp(Broke));
		bTopologyDirty = true;
	}
	if (bMoving || Clear > 0.f || bTopologyDirty)
	{
		BuildMesh(bTopologyDirty);
		bTopologyDirty = false;
	}
	if (Bounced > 0 && Age < 1.5f)
	{
		// The first pieces reaching the floor.
		CampusAudio::PlayAt(this, TEXT("Glass_Debris"), Shards.Num() > 0 ? Shards[0].Position : GetActorLocation(), 0.7f, 0.08f, CampusAssets::EAttenuation::Impact);
		Bounced = -1000;
	}
	if (Clear >= 1.f)
	{
		// Debris on the floor is swept away after a while.
		Destroy();
	}
	else if (!bMoving && Age > 3.f && Clear <= 0.f)
	{
		// Nothing to animate until the sweep: check back occasionally.
		SetActorTickInterval(0.5f);
	}
}
