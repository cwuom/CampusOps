#pragma once

#include "CoreMinimal.h"

/** Tiny offline synthesizer used to generate placeholder SFX (16-bit mono WAV). */
class FCampusSynth
{
public:
	static constexpr int32 SampleRate = 48000;

	explicit FCampusSynth(float Seconds, int32 Seed = 1);

	/** Filtered white noise burst with exponential decay. */
	void Noise(float Start, float Duration, float Amp, float AttackMs, float DecayTau, float LowpassHz, float HighpassHz);
	/** Noise whose lowpass cutoff sweeps Fc0 -> Fc1 -> Fc2 (whooshes, whizzes). */
	void SweepNoise(float Start, float Duration, float Amp, float Fc0, float Fc1, float Fc2, float AttackFrac);
	/** Sine (or soft square) with exponential pitch glide and decay. */
	void Tone(float Start, float Duration, float FreqStart, float FreqEnd, float Amp, float AttackMs, float DecayTau, float Square = 0.f);
	void Click(float Start, float Amp, float Brightness = 6000.f);
	/** Adds a low-passed, delayed copy of the current signal (discrete early reflection / slap-back). */
	void Echo(float DelaySeconds, float Gain, float LowpassHz);
	void Reverb(float Mix, float Room);
	void SoftClip(float Drive);
	void Normalize(float Peak);
	TArray<uint8> ToWav() const;

private:
	int32 Index(float Time) const { return FMath::Clamp(FMath::RoundToInt(Time * SampleRate), 0, Buffer.Num()); }
	TArray<float> Buffer;
	FRandomStream Rand;
};

namespace CampusSynthLibrary
{
	/** Returns (name, wav bytes) for every generated sound. */
	TArray<TPair<FString, TArray<uint8>>> BuildAll();
}
