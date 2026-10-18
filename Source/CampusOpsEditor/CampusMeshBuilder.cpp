#include "CampusMeshBuilder.h"
#include "CampusEditorCommon.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "Algo/Reverse.h"

namespace CampusMesh
{
	namespace
	{
		FVector2D EdgeNormal2D(const FVector2D& A, const FVector2D& B)
		{
			// Outward normal of a counter-clockwise polygon edge.
			const FVector2D D = (B - A).GetSafeNormal();
			return FVector2D(D.Y, -D.X);
		}

		float AngleBetweenDeg(const FVector2D& A, const FVector2D& B)
		{
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A, B), -1.f, 1.f)));
		}

		bool PointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
		{
			const float D1 = FVector2D::CrossProduct(B - A, P - A);
			const float D2 = FVector2D::CrossProduct(C - B, P - B);
			const float D3 = FVector2D::CrossProduct(A - C, P - C);
			const bool bNeg = (D1 < 0.f) || (D2 < 0.f) || (D3 < 0.f);
			const bool bPos = (D1 > 0.f) || (D2 > 0.f) || (D3 > 0.f);
			return !(bNeg && bPos);
		}
	}

	float SignedArea(const TArray<FVector2D>& Polygon)
	{
		float A = 0.f;
		for (int32 i = 0; i < Polygon.Num(); ++i)
		{
			const FVector2D& P = Polygon[i];
			const FVector2D& Q = Polygon[(i + 1) % Polygon.Num()];
			A += P.X * Q.Y - Q.X * P.Y;
		}
		return A * 0.5f;
	}

	TArray<int32> Triangulate(const TArray<FVector2D>& Polygon)
	{
		TArray<int32> Out;
		const int32 N = Polygon.Num();
		if (N < 3)
		{
			return Out;
		}
		TArray<int32> V;
		V.Reserve(N);
		const bool bCCW = SignedArea(Polygon) > 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			V.Add(bCCW ? i : N - 1 - i);
		}
		int32 Guard = 0;
		while (V.Num() > 3 && Guard++ < N * N)
		{
			bool bClipped = false;
			for (int32 i = 0; i < V.Num(); ++i)
			{
				const int32 I0 = V[(i + V.Num() - 1) % V.Num()];
				const int32 I1 = V[i];
				const int32 I2 = V[(i + 1) % V.Num()];
				const FVector2D& A = Polygon[I0];
				const FVector2D& B = Polygon[I1];
				const FVector2D& C = Polygon[I2];
				if (FVector2D::CrossProduct(B - A, C - B) <= 1.e-6f)
				{
					continue;
				}
				bool bEar = true;
				for (int32 j = 0; j < V.Num() && bEar; ++j)
				{
					const int32 K = V[j];
					if (K != I0 && K != I1 && K != I2 && PointInTriangle(Polygon[K], A, B, C))
					{
						bEar = false;
					}
				}
				if (bEar)
				{
					Out.Append({ I0, I1, I2 });
					V.RemoveAt(i);
					bClipped = true;
					break;
				}
			}
			if (!bClipped)
			{
				// Degenerate input: fan the rest.
				break;
			}
		}
		for (int32 i = 1; i + 1 < V.Num(); ++i)
		{
			Out.Append({ V[0], V[i], V[i + 1] });
		}
		return Out;
	}

	TArray<FVector2D> Offset(const TArray<FVector2D>& Polygon, float Distance)
	{
		const int32 N = Polygon.Num();
		TArray<FVector2D> Out;
		Out.SetNum(N);
		const float Sign = SignedArea(Polygon) >= 0.f ? 1.f : -1.f;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& Prev = Polygon[(i + N - 1) % N];
			const FVector2D& Cur = Polygon[i];
			const FVector2D& Next = Polygon[(i + 1) % N];
			const FVector2D N0 = EdgeNormal2D(Prev, Cur) * Sign;
			const FVector2D N1 = EdgeNormal2D(Cur, Next) * Sign;
			FVector2D Bis = (N0 + N1).GetSafeNormal();
			if (Bis.IsNearlyZero())
			{
				Bis = N0;
			}
			const float CosHalf = FMath::Max(FVector2D::DotProduct(Bis, N0), 0.4f);
			Out[i] = Cur - Bis * (Distance / CosHalf);
		}
		return Out;
	}

	void Arc(TArray<FVector2D>& Out, const FVector2D& Center, float Radius, float A0Deg, float A1Deg, int32 Segments)
	{
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float A = FMath::DegreesToRadians(FMath::Lerp(A0Deg, A1Deg, (float)i / FMath::Max(Segments, 1)));
			Out.Add(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
		}
	}

	TArray<FVector2D> RoundedRect(const FVector2D& Min, const FVector2D& Max, float Radius, int32 CornerSegments)
	{
		TArray<FVector2D> Out;
		const float R = FMath::Min(Radius, 0.49f * FMath::Min(Max.X - Min.X, Max.Y - Min.Y));
		Arc(Out, FVector2D(Max.X - R, Min.Y + R), R, -90.f, 0.f, CornerSegments);
		Arc(Out, FVector2D(Max.X - R, Max.Y - R), R, 0.f, 90.f, CornerSegments);
		Arc(Out, FVector2D(Min.X + R, Max.Y - R), R, 90.f, 180.f, CornerSegments);
		Arc(Out, FVector2D(Min.X + R, Min.Y + R), R, 180.f, 270.f, CornerSegments);
		return Out;
	}

	// ------------------------------------------------------------------ builder

	void FBuilder::BeginPrimitive()
	{
		// Stable per-primitive random for subtle tone variation (vertex B).
		uint32 H = ++PrimitiveCounter * 2654435761u;
		H ^= H >> 15;
		CurrentRandom = (H % 1000u) / 999.f;
	}

	void FBuilder::Tri(const FVector& A, const FVector& B, const FVector& C, const FVector& NA, const FVector& NB, const FVector& NC, const FVector& FaceNormal, float Edge)
	{
		const FTransform& X = Local;
		auto P = [&X](const FVector& V) { return X.TransformPosition(V); };
		auto N = [&X](const FVector& V)
		{
			const FVector S = X.GetScale3D();
			const FVector Inv(V.X / (FMath::Abs(S.X) > 1.e-6 ? S.X : 1.0), V.Y / (FMath::Abs(S.Y) > 1.e-6 ? S.Y : 1.0), V.Z / (FMath::Abs(S.Z) > 1.e-6 ? S.Z : 1.0));
			return X.GetRotation().RotateVector(Inv).GetSafeNormal();
		};
		FTri T;
		FVector WA = P(A);
		FVector WB = P(B);
		FVector WC = P(C);
		FVector WNA = N(NA);
		FVector WNB = N(NB);
		FVector WNC = N(NC);
		const FVector WF = N(FaceNormal);
		// Engine convention: the front-face normal is (C - A) x (B - A).
		const FVector G = FVector::CrossProduct(WC - WA, WB - WA);
		if (G.SizeSquared() < 1.e-12)
		{
			return;
		}
		if (FVector::DotProduct(G, WF) < 0.f)
		{
			Swap(WB, WC);
			Swap(WNB, WNC);
		}
		T.P[0] = FVector3f(WA);
		T.P[1] = FVector3f(WB);
		T.P[2] = FVector3f(WC);
		T.N[0] = FVector3f(WNA);
		T.N[1] = FVector3f(WNB);
		T.N[2] = FVector3f(WNC);
		T.Edge = Edge;
		T.Cavity = CurrentCavity;
		T.Random = CurrentRandom;
		T.Slot = CurrentSlot;
		Tris.Add(T);
	}

	void FBuilder::Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& NA, const FVector& NB, const FVector& NC, const FVector& ND, const FVector& FaceNormal, float Edge)
	{
		Tri(A, B, C, NA, NB, NC, FaceNormal, Edge);
		Tri(A, C, D, NA, NC, ND, FaceNormal, Edge);
	}

	void FBuilder::Polygon(const TArray<FVector>& Points, const FVector& Normal, bool bEdge)
	{
		BeginPrimitive();
		Local = Frame;
		for (int32 i = 1; i + 1 < Points.Num(); ++i)
		{
			Tri(Points[0], Points[i], Points[i + 1], Normal, Normal, Normal, Normal, bEdge ? 1.f : 0.f);
		}
	}

	void FBuilder::Box(const FVector& Center, const FVector& Size, float Bevel, const FRotator& Rot)
	{
		BeginPrimitive();
		Local = FTransform(Rot, Center) * Frame;
		const FVector H = Size.GetAbs() * 0.5f;
		const float B = FMath::Clamp(Bevel, 0.f, 0.45f * H.GetMin());
		auto Corner = [&H, B](int32 Axis, const FVector& S)
		{
			FVector P(S.X * (H.X - B), S.Y * (H.Y - B), S.Z * (H.Z - B));
			P[Axis] = S[Axis] * H[Axis];
			return P;
		};
		// Main faces.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 U = (Axis + 1) % 3;
			const int32 V = (Axis + 2) % 3;
			for (const float Sa : { -1.f, 1.f })
			{
				FVector S[4];
				const float UV[4][2] = { { 1.f, 1.f }, { -1.f, 1.f }, { -1.f, -1.f }, { 1.f, -1.f } };
				for (int32 k = 0; k < 4; ++k)
				{
					S[k][Axis] = Sa;
					S[k][U] = UV[k][0];
					S[k][V] = UV[k][1];
				}
				FVector Nrm = FVector::ZeroVector;
				Nrm[Axis] = Sa;
				FlatQuad(Corner(Axis, S[0]), Corner(Axis, S[1]), Corner(Axis, S[2]), Corner(Axis, S[3]), Nrm, 0.f);
			}
		}
		if (B <= 1.e-4f)
		{
			return;
		}
		// Edge chamfers: between faces A (sign sa) and B (sign sb), running along the third axis.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 A0 = (Axis + 1) % 3;
			const int32 A1 = (Axis + 2) % 3;
			for (const float Sa : { -1.f, 1.f })
			{
				for (const float Sb : { -1.f, 1.f })
				{
					FVector SP;
					SP[A0] = Sa;
					SP[A1] = Sb;
					SP[Axis] = 1.f;
					FVector SM = SP;
					SM[Axis] = -1.f;
					FVector Nrm = FVector::ZeroVector;
					Nrm[A0] = Sa;
					Nrm[A1] = Sb;
					Nrm.Normalize();
					FlatQuad(Corner(A0, SP), Corner(A0, SM), Corner(A1, SM), Corner(A1, SP), Nrm, 1.f);
				}
			}
		}
		// Corner triangles.
		for (const float Sx : { -1.f, 1.f })
		{
			for (const float Sy : { -1.f, 1.f })
			{
				for (const float Sz : { -1.f, 1.f })
				{
					const FVector S(Sx, Sy, Sz);
					const FVector Nrm = S.GetSafeNormal();
					Tri(Corner(0, S), Corner(1, S), Corner(2, S), Nrm, Nrm, Nrm, Nrm, 1.f);
				}
			}
		}
	}

	void FBuilder::Prism(const TArray<FVector2D>& ProfileXZ, float Y0, float Y1, float Bevel, float SmoothAngle, const FTransform& T)
	{
		BeginPrimitive();
		Local = T * Frame;
		if (ProfileXZ.Num() < 3)
		{
			return;
		}
		TArray<FVector2D> P = ProfileXZ;
		if (SignedArea(P) < 0.f)
		{
			Algo::Reverse(P);
		}
		const int32 N = P.Num();
		const float Lo = FMath::Min(Y0, Y1);
		const float Hi = FMath::Max(Y0, Y1);
		const float B = FMath::Clamp(Bevel, 0.f, 0.45f * (Hi - Lo));
		const TArray<FVector2D> In = B > 0.f ? Offset(P, B) : P;
		auto At = [](const FVector2D& V, float Y) { return FVector(V.X, Y, V.Y); };

		TArray<FVector2D> EdgeN;
		EdgeN.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			EdgeN[i] = EdgeNormal2D(P[i], P[(i + 1) % N]);
		}
		// Per-vertex smoothing across shallow corners.
		TArray<bool> Smooth;
		Smooth.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			Smooth[i] = AngleBetweenDeg(EdgeN[(i + N - 1) % N], EdgeN[i]) < SmoothAngle;
		}
		auto VertexN = [&](int32 Vertex, int32 Edge)
		{
			const FVector2D Nn = Smooth[Vertex] ? (EdgeN[(Vertex + N - 1) % N] + EdgeN[Vertex]).GetSafeNormal() : EdgeN[Edge];
			return FVector(Nn.X, 0.f, Nn.Y);
		};
		for (int32 i = 0; i < N; ++i)
		{
			const int32 j = (i + 1) % N;
			const FVector Face(EdgeN[i].X, 0.f, EdgeN[i].Y);
			const FVector Ni = VertexN(i, i);
			const FVector Nj = VertexN(j, i);
			Quad(At(P[i], Lo + B), At(P[j], Lo + B), At(P[j], Hi - B), At(P[i], Hi - B), Ni, Nj, Nj, Ni, Face, 0.f);
			if (B > 0.f)
			{
				const FVector Up = (Face + FVector(0.f, 1.f, 0.f)).GetSafeNormal();
				const FVector Dn = (Face + FVector(0.f, -1.f, 0.f)).GetSafeNormal();
				Quad(At(P[i], Hi - B), At(P[j], Hi - B), At(In[j], Hi), At(In[i], Hi), (Ni + FVector::YAxisVector).GetSafeNormal(), (Nj + FVector::YAxisVector).GetSafeNormal(), FVector::YAxisVector, FVector::YAxisVector, Up, 1.f);
				Quad(At(P[i], Lo + B), At(P[j], Lo + B), At(In[j], Lo), At(In[i], Lo), (Ni - FVector::YAxisVector).GetSafeNormal(), (Nj - FVector::YAxisVector).GetSafeNormal(), -FVector::YAxisVector, -FVector::YAxisVector, Dn, 1.f);
			}
		}
		const TArray<int32> Idx = Triangulate(In);
		for (int32 k = 0; k + 2 < Idx.Num(); k += 3)
		{
			const FVector2D& A = In[Idx[k]];
			const FVector2D& Bv = In[Idx[k + 1]];
			const FVector2D& C = In[Idx[k + 2]];
			Tri(At(A, Hi), At(Bv, Hi), At(C, Hi), FVector::YAxisVector, FVector::YAxisVector, FVector::YAxisVector, FVector::YAxisVector, 0.f);
			Tri(At(A, Lo), At(Bv, Lo), At(C, Lo), -FVector::YAxisVector, -FVector::YAxisVector, -FVector::YAxisVector, -FVector::YAxisVector, 0.f);
		}
	}

	void FBuilder::PrismZ(const TArray<FVector2D>& ProfileXY, float Z0, float Z1, float Bevel, float SmoothAngle, const FTransform& T)
	{
		// An XZ prism turned +90 degrees about X maps (x, e, z) to (x, -z, e): negate the profile's Y going in.
		TArray<FVector2D> P = ProfileXY;
		for (FVector2D& V : P)
		{
			V.Y = -V.Y;
		}
		Prism(P, Z0, Z1, Bevel, SmoothAngle, FTransform(FQuat(FVector::XAxisVector, HALF_PI)) * T);
	}

	void FBuilder::Lathe(const TArray<FVector2D>& ProfileXR, int32 Segments, const FTransform& T, float SmoothAngle, bool bSmoothAround, float PhaseDeg, bool bCapEnds)
	{
		BeginPrimitive();
		Local = T * Frame;
		const int32 N = ProfileXR.Num();
		if (N < 2 || Segments < 3)
		{
			return;
		}
		const bool bClosed = ProfileXR[0].Equals(ProfileXR[N - 1], 1.e-3f);
		const int32 NumSeg = N - 1;
		TArray<FVector2D> SegN;
		SegN.SetNum(NumSeg);
		TArray<float> SegLen;
		SegLen.SetNum(NumSeg);
		for (int32 j = 0; j < NumSeg; ++j)
		{
			const FVector2D D = ProfileXR[j + 1] - ProfileXR[j];
			SegLen[j] = D.Size();
			const FVector2D Dn = D.GetSafeNormal();
			SegN[j] = FVector2D(-Dn.Y, Dn.X);
		}
		auto ProfileN = [&](int32 Vertex, int32 Seg)
		{
			const int32 Other = Vertex == Seg ? Seg - 1 : Seg + 1;
			int32 O = Other;
			if (bClosed)
			{
				O = (Other + NumSeg) % NumSeg;
			}
			if (O < 0 || O >= NumSeg || SegLen[O] < 1.e-5f)
			{
				return SegN[Seg];
			}
			const FVector2D A = SegN[Seg];
			const FVector2D Bn = SegN[O];
			return AngleBetweenDeg(A, Bn) < SmoothAngle ? (A + Bn).GetSafeNormal() : A;
		};
		auto Ring = [](const FVector2D& XR, float Angle) { return FVector(XR.X, XR.Y * FMath::Cos(Angle), XR.Y * FMath::Sin(Angle)); };
		auto RingN = [](const FVector2D& Nxr, float Angle) { return FVector(Nxr.X, Nxr.Y * FMath::Cos(Angle), Nxr.Y * FMath::Sin(Angle)).GetSafeNormal(); };
		const float Phase = FMath::DegreesToRadians(PhaseDeg);
		for (int32 s = 0; s < Segments; ++s)
		{
			const float A0 = Phase + 2.f * PI * s / Segments;
			const float A1 = Phase + 2.f * PI * (s + 1) / Segments;
			const float AM = 0.5f * (A0 + A1);
			for (int32 j = 0; j < NumSeg; ++j)
			{
				if (SegLen[j] < 1.e-5f)
				{
					continue;
				}
				const FVector2D& P0 = ProfileXR[j];
				const FVector2D& P1 = ProfileXR[j + 1];
				const FVector2D N0 = ProfileN(j, j);
				const FVector2D N1 = ProfileN(j + 1, j);
				const float Na0 = bSmoothAround ? A0 : AM;
				const float Na1 = bSmoothAround ? A1 : AM;
				const float Edge = SegLen[j] < 0.35f ? 1.f : 0.f;
				Quad(Ring(P0, A0), Ring(P1, A0), Ring(P1, A1), Ring(P0, A1), RingN(N0, Na0), RingN(N1, Na0), RingN(N1, Na1), RingN(N0, Na1), RingN(SegN[j], AM), Edge);
			}
		}
		if (bClosed || !bCapEnds)
		{
			return;
		}
		auto Cap = [&](const FVector2D& End, float Dir)
		{
			if (End.Y <= 1.e-3f)
			{
				return;
			}
			const FVector C(End.X, 0.f, 0.f);
			const FVector Nrm(Dir, 0.f, 0.f);
			for (int32 s = 0; s < Segments; ++s)
			{
				const float A0 = Phase + 2.f * PI * s / Segments;
				const float A1 = Phase + 2.f * PI * (s + 1) / Segments;
				Tri(C, Ring(End, A0), Ring(End, A1), Nrm, Nrm, Nrm, Nrm, 0.f);
			}
		};
		Cap(ProfileXR[0], ProfileXR[1].X >= ProfileXR[0].X ? -1.f : 1.f);
		Cap(ProfileXR[N - 1], ProfileXR[N - 1].X >= ProfileXR[N - 2].X ? 1.f : -1.f);
	}

	void FBuilder::Tube(float X0, float X1, float Radius, int32 Segments, const FTransform& T, float Chamfer)
	{
		const float C = FMath::Clamp(Chamfer, 0.f, FMath::Min(Radius * 0.4f, (X1 - X0) * 0.4f));
		if (C > 0.f)
		{
			Lathe({ FVector2D(X0, Radius - C), FVector2D(X0 + C, Radius), FVector2D(X1 - C, Radius), FVector2D(X1, Radius - C) }, Segments, T);
		}
		else
		{
			Lathe({ FVector2D(X0, Radius), FVector2D(X1, Radius) }, Segments, T);
		}
	}

	void FBuilder::HollowTube(float X0, float X1, float OuterR, float InnerR, int32 Segments, const FTransform& T, float Chamfer)
	{
		const float C = FMath::Clamp(Chamfer, 0.f, FMath::Min((OuterR - InnerR) * 0.4f, (X1 - X0) * 0.3f));
		Lathe({ FVector2D(X0, OuterR - C), FVector2D(X0 + C, OuterR), FVector2D(X1 - C, OuterR), FVector2D(X1, OuterR - C), FVector2D(X1, InnerR + C),
			FVector2D(X1 - C, InnerR), FVector2D(X0 + C, InnerR), FVector2D(X0, InnerR + C), FVector2D(X0, OuterR - C) }, Segments, T);
	}

	void FBuilder::Picatinny(const FVector& Start, float Length, const FRotator& Rot, float Width)
	{
		const FTransform Saved = Frame;
		Frame = FTransform(Rot, Start) * Frame;
		// Spine (the recoil slots cut down to it), then the dovetailed teeth on a 1 cm pitch.
		Box(FVector(Length * 0.5f, 0.f, 0.24f), FVector(Length, Width * 0.78f, 0.48f), 0.08f);
		const int32 Teeth = FMath::Max(1, FMath::FloorToInt(Length / 1.0f));
		const float Used = Teeth * 1.0f;
		const float Start0 = (Length - Used) * 0.5f;
		for (int32 i = 0; i < Teeth; ++i)
		{
			const float X = Start0 + i * 1.0f + 0.5f;
			Box(FVector(X, 0.f, 0.47f), FVector(0.47f, Width, 0.22f), 0.07f);
			Box(FVector(X, 0.f, 0.74f), FVector(0.47f, Width * 0.8f, 0.36f), 0.07f);
		}
		Frame = Saved;
	}

	UStaticMesh* FBuilder::Save(const FString& PackageName, const TArray<UMaterialInterface*>& SlotMaterials, bool bNanite, bool bCollision) const
	{
		if (Tris.Num() == 0)
		{
			UE_LOG(LogCampusBootstrap, Warning, TEXT("Mesh %s has no triangles"), *PackageName);
			return nullptr;
		}
		const FString Name = FPackageName::GetShortName(PackageName);
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Name);
		if (!Mesh)
		{
			Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		Mesh->PreEditChange(nullptr);
		if (Mesh->GetNumSourceModels() != 1)
		{
			Mesh->SetNumSourceModels(1);
		}
		FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bUseMikkTSpace = true;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bRemoveDegenerates = true;
		Source.BuildSettings.bUseFullPrecisionUVs = false;

		// Only slots that carry triangles become sections, in slot order, so section i uses material i.
		TArray<int32> UsedSlots;
		for (const FTri& T : Tris)
		{
			UsedSlots.AddUnique(T.Slot);
		}
		UsedSlots.Sort();

		FMeshDescription* MD = Mesh->CreateMeshDescription(0);
		MD->Empty();
		FStaticMeshAttributes Attributes(*MD);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		UVs.SetNumChannels(1);

		TMap<int32, FPolygonGroupID> Groups;
		for (int32 Slot : UsedSlots)
		{
			const FPolygonGroupID G = MD->CreatePolygonGroup();
			SlotNames[G] = FName(*FString::Printf(TEXT("Slot%d"), Slot));
			Groups.Add(Slot, G);
		}
		MD->ReserveNewVertices(Tris.Num() * 3);
		MD->ReserveNewVertexInstances(Tris.Num() * 3);
		MD->ReserveNewTriangles(Tris.Num());
		for (const FTri& T : Tris)
		{
			const FVector3f FaceN = FVector3f::CrossProduct(T.P[2] - T.P[0], T.P[1] - T.P[0]).GetSafeNormal();
			const FVector3f A = FaceN.GetAbs();
			const int32 Axis = (A.X >= A.Y && A.X >= A.Z) ? 0 : ((A.Y >= A.Z) ? 1 : 2);
			TArray<FVertexInstanceID, TFixedAllocator<3>> Corners;
			for (int32 k = 0; k < 3; ++k)
			{
				const FVertexID V = MD->CreateVertex();
				Positions[V] = T.P[k];
				const FVertexInstanceID VI = MD->CreateVertexInstance(V);
				Normals[VI] = T.N[k];
				Colors[VI] = FVector4f(T.Edge, T.Cavity, T.Random, 1.f);
				const FVector3f& P = T.P[k];
				FVector2f UV = Axis == 0 ? FVector2f(P.Y, -P.Z) : (Axis == 1 ? FVector2f(P.X, -P.Z) : FVector2f(P.X, P.Y));
				UVs.Set(VI, 0, UV * 0.1f);
				Corners.Add(VI);
			}
			MD->CreateTriangle(Groups[T.Slot], Corners);
		}
		Mesh->CommitMeshDescription(0);

		Mesh->GetStaticMaterials().Reset();
		for (int32 Slot : UsedSlots)
		{
			UMaterialInterface* Mat = SlotMaterials.IsValidIndex(Slot) ? SlotMaterials[Slot] : nullptr;
			const FName SlotName(*FString::Printf(TEXT("Slot%d"), Slot));
			Mesh->GetStaticMaterials().Add(FStaticMaterial(Mat, SlotName, SlotName));
		}
		Mesh->GetNaniteSettings().bEnabled = bNanite;

		Mesh->CreateBodySetup();
		if (UBodySetup* Body = Mesh->GetBodySetup())
		{
			Body->RemoveSimpleCollision();
			Body->CollisionTraceFlag = bCollision ? CTF_UseSimpleAsComplex : CTF_UseDefault;
			if (bCollision)
			{
				FBox Bounds(ForceInit);
				for (const FTri& T : Tris)
				{
					for (const FVector3f& P : T.P)
					{
						Bounds += FVector(P);
					}
				}
				FKBoxElem Box(Bounds.GetSize().X, Bounds.GetSize().Y, Bounds.GetSize().Z);
				Box.Center = Bounds.GetCenter();
				Body->AggGeom.BoxElems.Add(Box);
			}
			Body->InvalidatePhysicsData();
		}
		Mesh->Build(false);
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();
		CampusEditor::SaveAsset(Mesh);
		return Mesh;
	}
}
