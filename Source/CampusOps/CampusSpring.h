#pragma once

#include "CoreMinimal.h"

/**
 * Damped springs parameterised by natural frequency (Hz) and damping ratio.
 * Integrated with fixed sub-steps so the result is frame-rate independent.
 */
namespace CampusSpring
{
	inline void Step(float& Value, float& Velocity, float Target, float Frequency, float DampingRatio, float DeltaTime)
	{
		const float Omega = 2.f * PI * FMath::Max(Frequency, 0.01f);
		const float K = Omega * Omega;
		const float C = 2.f * DampingRatio * Omega;
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime * 240.f), 1, 16);
		const float H = DeltaTime / Steps;
		for (int32 i = 0; i < Steps; ++i)
		{
			const float Accel = K * (Target - Value) - C * Velocity;
			Velocity += Accel * H;
			Value += Velocity * H;
		}
	}
}

struct FCampusSpringFloat
{
	float Value = 0.f;
	float Velocity = 0.f;

	void Update(float Target, float Frequency, float DampingRatio, float DeltaTime)
	{
		CampusSpring::Step(Value, Velocity, Target, Frequency, DampingRatio, DeltaTime);
	}
	void AddImpulse(float Impulse) { Velocity += Impulse; }
	void Reset(float InValue = 0.f) { Value = InValue; Velocity = 0.f; }
};

struct FCampusSpringVector
{
	FVector Value = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	void Update(const FVector& Target, float Frequency, float DampingRatio, float DeltaTime)
	{
		float VX = Value.X, VY = Value.Y, VZ = Value.Z;
		float WX = Velocity.X, WY = Velocity.Y, WZ = Velocity.Z;
		CampusSpring::Step(VX, WX, Target.X, Frequency, DampingRatio, DeltaTime);
		CampusSpring::Step(VY, WY, Target.Y, Frequency, DampingRatio, DeltaTime);
		CampusSpring::Step(VZ, WZ, Target.Z, Frequency, DampingRatio, DeltaTime);
		Value = FVector(VX, VY, VZ);
		Velocity = FVector(WX, WY, WZ);
	}
	void AddImpulse(const FVector& Impulse) { Velocity += Impulse; }
	void Reset(const FVector& InValue = FVector::ZeroVector) { Value = InValue; Velocity = FVector::ZeroVector; }

	/** Treat X/Y/Z as Pitch/Yaw/Roll degrees. */
	FRotator AsRotator() const { return FRotator(Value.X, Value.Y, Value.Z); }
};

namespace CampusMath
{
	/** Frame-rate independent exponential smoothing factor. */
	inline float ExpAlpha(float Speed, float DeltaTime) { return 1.f - FMath::Exp(-Speed * DeltaTime); }

	inline float SmoothStep01(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}

	inline float SmootherStep01(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * X * (X * (X * 6.f - 15.f) + 10.f);
	}

	inline float EaseOutBack(float X, float Overshoot = 1.2f)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		const float C3 = Overshoot + 1.f;
		const float T = X - 1.f;
		return 1.f + C3 * T * T * T + Overshoot * T * T;
	}

	/** Rotate a transform (in parent space) around a pivot point (in the same space). */
	inline FTransform RotateAround(const FTransform& T, const FQuat& Rotation, const FVector& Pivot)
	{
		FTransform Out = T;
		Out.SetLocation(Pivot + Rotation.RotateVector(T.GetLocation() - Pivot));
		Out.SetRotation(Rotation * T.GetRotation());
		return Out;
	}

	inline FTransform LerpTransform(const FTransform& A, const FTransform& B, float Alpha)
	{
		FTransform Out;
		Out.SetLocation(FMath::Lerp(A.GetLocation(), B.GetLocation(), Alpha));
		Out.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha).GetNormalized());
		Out.SetScale3D(FMath::Lerp(A.GetScale3D(), B.GetScale3D(), Alpha));
		return Out;
	}
}
