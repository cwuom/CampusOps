#include "CampusSynth.h"

FCampusSynth::FCampusSynth(float Seconds, int32 Seed)
	: Rand(Seed)
{
	Buffer.SetNumZeroed(FMath::Max(1, FMath::RoundToInt(Seconds * SampleRate)));
}

void FCampusSynth::Noise(float Start, float Duration, float Amp, float AttackMs, float DecayTau, float LowpassHz, float HighpassHz)
{
	const int32 I0 = Index(Start);
	const int32 I1 = Index(Start + Duration);
	const float Lp = 1.f - FMath::Exp(-2.f * PI * LowpassHz / SampleRate);
	const float Hp = 1.f - FMath::Exp(-2.f * PI * FMath::Max(HighpassHz, 1.f) / SampleRate);
	float Low = 0.f;
	float HighState = 0.f;
	const float Attack = FMath::Max(AttackMs * 0.001f, 1.f / SampleRate);
	for (int32 i = I0; i < I1; ++i)
	{
		const float T = float(i - I0) / SampleRate;
		const float Env = FMath::Min(T / Attack, 1.f) * FMath::Exp(-T / FMath::Max(DecayTau, 0.0005f));
		const float X = Rand.FRandRange(-1.f, 1.f);
		Low += Lp * (X - Low);
		HighState += Hp * (Low - HighState);
		Buffer[i] += (Low - HighState) * Amp * Env * 1.8f;
	}
}

void FCampusSynth::SweepNoise(float Start, float Duration, float Amp, float Fc0, float Fc1, float Fc2, float AttackFrac)
{
	const int32 I0 = Index(Start);
	const int32 I1 = Index(Start + Duration);
	float Low = 0.f;
	float Low2 = 0.f;
	for (int32 i = I0; i < I1; ++i)
	{
		const float U = float(i - I0) / FMath::Max(I1 - I0, 1);
		const float Fc = U < 0.5f ? FMath::Lerp(Fc0, Fc1, U * 2.f) : FMath::Lerp(Fc1, Fc2, (U - 0.5f) * 2.f);
		const float A = 1.f - FMath::Exp(-2.f * PI * Fc / SampleRate);
		const float Env = U < AttackFrac ? FMath::Sin(U / AttackFrac * PI * 0.5f) : FMath::Square(FMath::Cos((U - AttackFrac) / (1.f - AttackFrac) * PI * 0.5f));
		const float X = Rand.FRandRange(-1.f, 1.f);
		Low += A * (X - Low);
		Low2 += A * (Low - Low2);
		Buffer[i] += (Low - Low2 * 0.6f) * Amp * Env * 2.5f;
	}
}

void FCampusSynth::Tone(float Start, float Duration, float FreqStart, float FreqEnd, float Amp, float AttackMs, float DecayTau, float Square)
{
	const int32 I0 = Index(Start);
	const int32 I1 = Index(Start + Duration);
	const float Attack = FMath::Max(AttackMs * 0.001f, 1.f / SampleRate);
	float Phase = 0.f;
	for (int32 i = I0; i < I1; ++i)
	{
		const float T = float(i - I0) / SampleRate;
		const float U = Duration > 0.f ? T / Duration : 0.f;
		const float Freq = FreqStart * FMath::Pow(FMath::Max(FreqEnd, 1.f) / FMath::Max(FreqStart, 1.f), U);
		Phase += 2.f * PI * Freq / SampleRate;
		float S = FMath::Sin(Phase);
		if (Square > 0.f)
		{
			S = FMath::Lerp(S, FMath::Tanh(S * 4.f), Square);
		}
		const float Env = FMath::Min(T / Attack, 1.f) * FMath::Exp(-T / FMath::Max(DecayTau, 0.0005f));
		Buffer[i] += S * Amp * Env;
	}
}

void FCampusSynth::Click(float Start, float Amp, float Brightness)
{
	Noise(Start, 0.012f, Amp, 0.05f, 0.0025f, Brightness * 1.6f, Brightness * 0.35f);
	Tone(Start, 0.02f, Brightness * 0.55f, Brightness * 0.5f, Amp * 0.35f, 0.05f, 0.004f);
}

void FCampusSynth::Echo(float DelaySeconds, float Gain, float LowpassHz)
{
	const int32 D = FMath::Max(1, FMath::RoundToInt(DelaySeconds * SampleRate));
	const TArray<float> Src = Buffer;
	const float A = 1.f - FMath::Exp(-2.f * PI * LowpassHz / SampleRate);
	float Low = 0.f;
	for (int32 i = D; i < Buffer.Num(); ++i)
	{
		Low += A * (Src[i - D] - Low);
		Buffer[i] += Low * Gain;
	}
}

void FCampusSynth::Reverb(float Mix, float Room)
{
	const int32 CombDelays[4] = { 1557, 1617, 1491, 1422 };
	const int32 AllpassDelays[2] = { 225, 556 };
	TArray<float> Wet;
	Wet.SetNumZeroed(Buffer.Num());
	const float Feedback = FMath::Clamp(0.70f + Room * 0.25f, 0.f, 0.95f);
	for (int32 c = 0; c < 4; ++c)
	{
		const int32 D = FMath::RoundToInt(CombDelays[c] * (0.8f + Room * 0.6f));
		TArray<float> Line;
		Line.SetNumZeroed(D);
		int32 Pos = 0;
		float Damp = 0.f;
		for (int32 i = 0; i < Buffer.Num(); ++i)
		{
			const float Out = Line[Pos];
			Damp = Out * 0.6f + Damp * 0.4f;
			Line[Pos] = Buffer[i] + Damp * Feedback;
			Pos = (Pos + 1) % D;
			Wet[i] += Out * 0.25f;
		}
	}
	for (int32 a = 0; a < 2; ++a)
	{
		const int32 D = AllpassDelays[a];
		TArray<float> Line;
		Line.SetNumZeroed(D);
		int32 Pos = 0;
		for (int32 i = 0; i < Wet.Num(); ++i)
		{
			const float Delayed = Line[Pos];
			const float In = Wet[i];
			Line[Pos] = In + Delayed * 0.5f;
			Wet[i] = Delayed - In * 0.5f;
			Pos = (Pos + 1) % D;
		}
	}
	for (int32 i = 0; i < Buffer.Num(); ++i)
	{
		Buffer[i] = Buffer[i] * (1.f - Mix * 0.35f) + Wet[i] * Mix;
	}
}

void FCampusSynth::SoftClip(float Drive)
{
	for (float& S : Buffer)
	{
		S = FMath::Tanh(S * Drive) / FMath::Tanh(Drive);
	}
}

void FCampusSynth::Normalize(float Peak)
{
	float Max = 0.f;
	for (const float S : Buffer)
	{
		Max = FMath::Max(Max, FMath::Abs(S));
	}
	if (Max > KINDA_SMALL_NUMBER)
	{
		const float Gain = Peak / Max;
		for (float& S : Buffer)
		{
			S *= Gain;
		}
	}
	// short fade-out to avoid clicks
	const int32 Fade = FMath::Min(Buffer.Num(), SampleRate / 200);
	for (int32 i = 0; i < Fade; ++i)
	{
		Buffer[Buffer.Num() - 1 - i] *= float(i) / Fade;
	}
}

TArray<uint8> FCampusSynth::ToWav() const
{
	const int32 NumSamples = Buffer.Num();
	const int32 DataBytes = NumSamples * 2;
	TArray<uint8> Out;
	Out.Reserve(44 + DataBytes);
	auto Write32 = [&Out](uint32 V) { Out.Add(V & 0xFF); Out.Add((V >> 8) & 0xFF); Out.Add((V >> 16) & 0xFF); Out.Add((V >> 24) & 0xFF); };
	auto Write16 = [&Out](uint16 V) { Out.Add(V & 0xFF); Out.Add((V >> 8) & 0xFF); };
	auto WriteTag = [&Out](const char* Tag) { for (int32 i = 0; i < 4; ++i) Out.Add((uint8)Tag[i]); };
	WriteTag("RIFF");
	Write32(36 + DataBytes);
	WriteTag("WAVE");
	WriteTag("fmt ");
	Write32(16);
	Write16(1);
	Write16(1);
	Write32(SampleRate);
	Write32(SampleRate * 2);
	Write16(2);
	Write16(16);
	WriteTag("data");
	Write32(DataBytes);
	for (const float S : Buffer)
	{
		const int16 V = (int16)FMath::Clamp(FMath::RoundToInt(S * 32767.f), -32768, 32767);
		Write16((uint16)V);
	}
	return Out;
}

namespace CampusSynthLibrary
{
	struct FGunRecipe
	{
		float BodyHz0, BodyHz1, BodyTau, BoomLp, BoomTau, CrackHp, CrackTau, TailTau, TailAmp, Reverb, Length;
	};

	static TArray<uint8> Gunshot(const FGunRecipe& R, int32 Seed)
	{
		FCampusSynth S(R.Length, Seed);
		FRandomStream V(Seed * 31 + 7);
		const float Jit = V.FRandRange(0.94f, 1.06f);
		// Muzzle blast: a very short broadband N-wave on top of the crack.
		S.Noise(0.f, 0.006f, 1.2f, 0.02f, 0.0016f, 16000.f, 1800.f);
		S.Noise(0.f, 0.035f, 1.0f, 0.08f, R.CrackTau, 11000.f, R.CrackHp);
		// Chest thump: saturated low sweep, the part that makes it feel heavy.
		S.Tone(0.f, 0.26f, R.BodyHz0 * Jit, R.BodyHz1, 1.05f, 0.3f, R.BodyTau, 0.35f);
		S.Tone(0.f, 0.18f, R.BodyHz0 * 0.5f * Jit, R.BodyHz1 * 0.7f, 0.55f, 0.6f, R.BodyTau * 1.4f);
		S.Noise(0.f, 0.32f, 0.8f, 0.5f, R.BoomTau, R.BoomLp, 30.f);
		S.Noise(0.001f, 0.06f, 0.42f, 0.2f, 0.012f, 5200.f, 900.f);
		// Action: bolt carrier and case ejection.
		S.Click(0.016f * Jit, 0.24f, 5200.f);
		S.Click(0.046f * Jit, 0.13f, 3800.f);
		// Slap-back off nearby walls, then the long outdoor tail.
		S.Echo(0.023f * V.FRandRange(0.9f, 1.1f), 0.32f, 3800.f);
		S.Echo(0.041f * V.FRandRange(0.9f, 1.1f), 0.22f, 2600.f);
		S.Echo(0.068f * V.FRandRange(0.9f, 1.1f), 0.16f, 1800.f);
		S.Echo(0.112f * V.FRandRange(0.9f, 1.1f), 0.1f, 1200.f);
		S.Noise(0.02f, R.Length - 0.02f, R.TailAmp, 25.f, R.TailTau, 1300.f, 80.f);
		S.Reverb(R.Reverb, 0.65f);
		S.SoftClip(2.3f);
		S.Normalize(0.98f);
		return S.ToWav();
	}

	TArray<TPair<FString, TArray<uint8>>> BuildAll()
	{
		TArray<TPair<FString, TArray<uint8>>> Out;
		auto Add = [&Out](const TCHAR* Name, TArray<uint8>&& Wav) { Out.Emplace(FString(Name), MoveTemp(Wav)); };
		// Three takes per weapon; the game picks one at random (Name, Name_2, Name_3) so automatic fire does not machine-gun a single sample.
		auto AddGun = [&Out](const TCHAR* Name, const FGunRecipe& R, int32 Seed)
		{
			Out.Emplace(FString(Name), Gunshot(R, Seed));
			Out.Emplace(FString::Printf(TEXT("%s_2"), Name), Gunshot(R, Seed + 100));
			Out.Emplace(FString::Printf(TEXT("%s_3"), Name), Gunshot(R, Seed + 200));
		};

		AddGun(TEXT("Gun_AR"), { 170.f, 52.f, 0.05f, 950.f, 0.06f, 1800.f, 0.006f, 0.34f, 0.24f, 0.28f, 1.3f }, 11);
		AddGun(TEXT("Gun_AK"), { 132.f, 42.f, 0.065f, 720.f, 0.08f, 1400.f, 0.007f, 0.4f, 0.28f, 0.32f, 1.5f }, 12);
		AddGun(TEXT("Gun_SMG"), { 225.f, 78.f, 0.03f, 1300.f, 0.036f, 2300.f, 0.0045f, 0.22f, 0.2f, 0.22f, 0.95f }, 13);
		AddGun(TEXT("Gun_DMR"), { 118.f, 38.f, 0.085f, 650.f, 0.1f, 1600.f, 0.008f, 0.65f, 0.32f, 0.38f, 1.9f }, 14);
		AddGun(TEXT("Gun_Pistol"), { 215.f, 68.f, 0.034f, 1200.f, 0.04f, 2000.f, 0.005f, 0.26f, 0.2f, 0.24f, 1.0f }, 15);
		// 12 gauge: a low, wide blast with less supersonic crack and a long, heavy boom rolling off the buildings.
		AddGun(TEXT("Gun_Shotgun"), { 92.f, 32.f, 0.12f, 480.f, 0.15f, 1100.f, 0.011f, 0.8f, 0.38f, 0.44f, 2.2f }, 16);

		{
			FCampusSynth S(0.45f, 21);
			S.Tone(0.f, 0.14f, 120.f, 60.f, 0.75f, 1.f, 0.025f);
			S.Noise(0.f, 0.09f, 0.55f, 0.5f, 0.018f, 2600.f, 200.f);
			S.Click(0.004f, 0.6f, 7000.f);
			S.Tone(0.006f, 0.05f, 2800.f, 2600.f, 0.16f, 0.1f, 0.012f);
			S.Click(0.04f, 0.35f, 5000.f);
			S.Reverb(0.12f, 0.3f);
			S.Normalize(0.9f);
			Add(TEXT("Gun_Suppressed"), S.ToWav());
		}
		{
			FCampusSynth S(0.12f, 22);
			S.Click(0.f, 0.7f, 6500.f);
			S.Click(0.036f, 0.5f, 4800.f);
			S.Tone(0.036f, 0.07f, 1900.f, 1750.f, 0.12f, 0.1f, 0.02f);
			S.Normalize(0.8f);
			Add(TEXT("Gun_Mech"), S.ToWav());
		}
		{
			FCampusSynth S(0.08f, 23);
			S.Click(0.f, 0.8f, 7000.f);
			S.Tone(0.f, 0.04f, 3200.f, 3000.f, 0.2f, 0.1f, 0.008f);
			S.Normalize(0.8f);
			Add(TEXT("Gun_DryFire"), S.ToWav());
			Add(TEXT("FireMode_Switch"), S.ToWav());
		}
		{
			FCampusSynth S(0.3f, 24);
			S.SweepNoise(0.f, 0.14f, 0.35f, 1500.f, 4200.f, 2500.f, 0.3f);
			S.Click(0.15f, 0.7f, 5500.f);
			S.Tone(0.15f, 0.05f, 900.f, 700.f, 0.25f, 0.2f, 0.015f);
			S.Normalize(0.85f);
			Add(TEXT("Reload_MagOut"), S.ToWav());
		}
		{
			FCampusSynth S(0.3f, 25);
			S.SweepNoise(0.f, 0.07f, 0.3f, 1800.f, 4500.f, 3000.f, 0.3f);
			S.Click(0.075f, 1.0f, 7000.f);
			S.Tone(0.075f, 0.08f, 190.f, 140.f, 0.6f, 0.3f, 0.025f);
			S.Click(0.09f, 0.4f, 4000.f);
			S.Normalize(0.9f);
			Add(TEXT("Reload_MagIn"), S.ToWav());
		}
		{
			FCampusSynth S(0.35f, 26);
			S.SweepNoise(0.f, 0.09f, 0.3f, 1200.f, 3800.f, 2400.f, 0.6f);
			S.Click(0.085f, 0.7f, 5500.f);
			S.Click(0.19f, 1.0f, 6500.f);
			S.Tone(0.19f, 0.1f, 160.f, 110.f, 0.5f, 0.3f, 0.03f);
			S.Normalize(0.9f);
			Add(TEXT("Reload_Bolt"), S.ToWav());
		}
		{
			FCampusSynth S(0.35f, 27);
			S.Noise(0.f, 0.3f, 0.4f, 40.f, 0.09f, 3000.f, 400.f);
			S.Normalize(0.5f);
			Add(TEXT("Reload_Start"), S.ToWav());
			Add(TEXT("ADS_In"), S.ToWav());
		}
		{
			FCampusSynth S(0.25f, 28);
			S.Noise(0.f, 0.2f, 0.35f, 20.f, 0.06f, 2500.f, 350.f);
			S.Normalize(0.4f);
			Add(TEXT("ADS_Out"), S.ToWav());
			Add(TEXT("Weapon_Holster"), S.ToWav());
		}
		{
			FCampusSynth S(0.45f, 29);
			S.Noise(0.f, 0.35f, 0.35f, 30.f, 0.1f, 3200.f, 400.f);
			S.Click(0.06f, 0.5f, 5000.f);
			S.Click(0.14f, 0.45f, 6000.f);
			S.Click(0.24f, 0.7f, 4500.f);
			S.Tone(0.24f, 0.06f, 220.f, 160.f, 0.3f, 0.3f, 0.02f);
			S.Normalize(0.75f);
			Add(TEXT("Weapon_Equip"), S.ToWav());
		}
		// Hit confirms are percussive, not musical: a dry transient with a padded low body, the way a modern shooter
		// layers them, so they read through gunfire without sounding like a game-show chime.
		for (int32 Take = 0; Take < 3; ++Take)
		{
			// Body hit: a crisp plastic-metal "tk" over a soft low puck.
			FCampusSynth S(0.09f, 31 + Take * 7);
			S.Noise(0.f, 0.005f, 0.95f, 0.02f, 0.0011f, 15000.f, 3600.f);
			S.Noise(0.f, 0.022f, 0.5f, 0.05f, 0.0042f, 7200.f, 1900.f);
			S.Tone(0.f, 0.016f, 2850.f + Take * 90.f, 2650.f, 0.16f, 0.05f, 0.0022f);
			S.Tone(0.f, 0.06f, 150.f, 82.f, 0.5f, 0.3f, 0.015f);
			S.SoftClip(1.6f);
			S.Normalize(0.86f);
			Add(Take == 0 ? TEXT("Hitmarker") : (Take == 1 ? TEXT("Hitmarker_2") : TEXT("Hitmarker_3")), S.ToWav());
		}
		for (int32 Take = 0; Take < 2; ++Take)
		{
			// Headshot: a harder crack and crunch with a brief, inharmonic helmet "tink" (struck plate, not a bell).
			FCampusSynth S(0.28f, 32 + Take * 7);
			S.Noise(0.f, 0.006f, 1.f, 0.02f, 0.0012f, 15500.f, 3000.f);
			S.Noise(0.f, 0.04f, 0.72f, 0.05f, 0.0075f, 8200.f, 1500.f);
			S.Tone(0.f, 0.12f, 3340.f + Take * 120.f, 3290.f, 0.3f, 0.1f, 0.022f);
			S.Tone(0.f, 0.1f, 5230.f, 5170.f, 0.18f, 0.1f, 0.016f);
			S.Tone(0.f, 0.08f, 7710.f, 7640.f, 0.09f, 0.1f, 0.011f);
			S.Tone(0.f, 0.1f, 165.f, 85.f, 0.55f, 0.3f, 0.02f);
			S.Noise(0.013f, 0.05f, 0.36f, 0.1f, 0.012f, 2600.f, 300.f);
			S.SoftClip(1.6f);
			S.Normalize(0.92f);
			Add(Take == 0 ? TEXT("Hit_Head") : TEXT("Hit_Head_2"), S.ToWav());
		}
		for (int32 Take = 0; Take < 2; ++Take)
		{
			// Kill confirm: snap, crack and a heavy, saturated low punch with a muffled second hit just behind it.
			FCampusSynth S(0.5f, 33 + Take * 7);
			S.Noise(0.f, 0.006f, 1.f, 0.02f, 0.0012f, 14500.f, 2600.f);
			S.Noise(0.f, 0.03f, 0.62f, 0.05f, 0.006f, 6200.f, 1200.f);
			S.Tone(0.f, 0.03f, 2650.f, 2480.f, 0.32f, 0.05f, 0.004f);
			S.Tone(0.f, 0.03f, 4120.f, 3960.f, 0.2f, 0.05f, 0.003f);
			S.Tone(0.f, 0.26f, 112.f - Take * 6.f, 42.f, 1.f, 0.3f, 0.06f, 0.45f);
			S.Noise(0.f, 0.12f, 0.7f, 0.3f, 0.03f, 720.f, 40.f);
			S.Noise(0.031f, 0.05f, 0.45f, 0.1f, 0.01f, 3500.f, 500.f);
			S.Tone(0.031f, 0.12f, 76.f, 44.f, 0.5f, 0.5f, 0.05f);
			S.Echo(0.045f, 0.16f, 1500.f);
			S.SoftClip(2.2f);
			S.Normalize(0.97f);
			Add(Take == 0 ? TEXT("Hit_Kill") : TEXT("Hit_Kill_2"), S.ToWav());
		}
		// The kill confirm a modern shooter plays on top of the hit: unmistakable on laptop speakers and through
		// gunfire, so its character sits in the mids (a meaty "thwack" with real harmonics, a hard crunch, a short
		// inharmonic ping of struck steel) rather than in a sub-bass thump only big speakers reproduce.
		for (int32 Take = 0; Take < 2; ++Take)
		{
			FCampusSynth S(0.42f, 61 + Take * 5);
			S.Noise(0.f, 0.006f, 1.f, 0.02f, 0.0012f, 14000.f, 3000.f);
			S.Noise(0.f, 0.06f, 0.8f, 0.1f, 0.012f, 4600.f, 650.f);
			S.Tone(0.f, 0.1f, 440.f - Take * 20.f, 170.f, 0.85f, 0.2f, 0.026f, 0.4f);
			S.Tone(0.f, 0.2f, 125.f, 55.f, 0.65f, 0.3f, 0.05f);
			S.Tone(0.004f, 0.26f, 1870.f + Take * 60.f, 1858.f, 0.3f, 0.3f, 0.075f);
			S.Tone(0.004f, 0.22f, 3010.f + Take * 80.f, 2994.f, 0.2f, 0.3f, 0.055f);
			S.Tone(0.004f, 0.16f, 4430.f, 4410.f, 0.12f, 0.3f, 0.035f);
			// A second, softer tick right behind: the confirm "double tap" read.
			S.Noise(0.048f, 0.025f, 0.45f, 0.05f, 0.004f, 9000.f, 2400.f);
			S.Tone(0.048f, 0.06f, 360.f, 200.f, 0.35f, 0.2f, 0.02f, 0.3f);
			S.Echo(0.05f, 0.12f, 2500.f);
			S.SoftClip(2.f);
			S.Normalize(0.98f);
			Add(Take == 0 ? TEXT("Kill_Confirm") : TEXT("Kill_Confirm_2"), S.ToWav());
		}
		for (int32 Take = 0; Take < 2; ++Take)
		{
			// Headshot kill: the confirm with a ringing helmet "tink" on top and a dry crunch of bone under it.
			FCampusSynth S(0.6f, 71 + Take * 5);
			S.Noise(0.f, 0.007f, 1.f, 0.02f, 0.0013f, 15500.f, 3200.f);
			S.Noise(0.f, 0.07f, 0.85f, 0.1f, 0.014f, 5200.f, 700.f);
			S.Tone(0.f, 0.11f, 470.f, 175.f, 0.8f, 0.2f, 0.028f, 0.4f);
			S.Tone(0.f, 0.22f, 130.f, 52.f, 0.6f, 0.3f, 0.055f);
			S.Tone(0.f, 0.45f, 2480.f + Take * 70.f, 2470.f, 0.42f, 0.2f, 0.13f);
			S.Tone(0.f, 0.38f, 3870.f + Take * 90.f, 3855.f, 0.28f, 0.2f, 0.1f);
			S.Tone(0.f, 0.3f, 6020.f, 5995.f, 0.16f, 0.2f, 0.07f);
			S.Tone(0.f, 0.22f, 8650.f, 8620.f, 0.08f, 0.2f, 0.045f);
			S.Noise(0.012f, 0.09f, 0.55f, 0.2f, 0.022f, 3200.f, 450.f);
			S.Noise(0.05f, 0.025f, 0.4f, 0.05f, 0.004f, 9500.f, 2600.f);
			S.Echo(0.06f, 0.14f, 3000.f);
			S.SoftClip(2.f);
			S.Normalize(0.98f);
			Add(Take == 0 ? TEXT("Kill_Headshot") : TEXT("Kill_Headshot_2"), S.ToWav());
		}
		// Grenades. Most of these are replaced by recordings (Tools/BuildAudio.py); the synth takes are the fallback.
		{
			// Drawn from the pouch: webbing and a knock of steel.
			FCampusSynth S(0.35f, 101);
			S.Noise(0.f, 0.28f, 0.45f, 30.f, 0.07f, 3200.f, 300.f);
			S.Click(0.16f, 0.35f, 4200.f);
			S.Normalize(0.6f);
			Add(TEXT("Grenade_Draw"), S.ToWav());
		}
		{
			// The arm snapping through the throw.
			FCampusSynth S(0.32f, 102);
			S.SweepNoise(0.f, 0.28f, 0.6f, 500.f, 2600.f, 700.f, 0.55f);
			S.Normalize(0.7f);
			Add(TEXT("Grenade_Throw"), S.ToWav());
		}
		{
			FCampusSynth S(0.25f, 103);
			S.Click(0.f, 0.8f, 6500.f);
			S.Tone(0.f, 0.15f, 3400.f, 3300.f, 0.2f, 0.1f, 0.03f);
			S.Click(0.05f, 0.5f, 5200.f);
			S.Normalize(0.8f);
			Add(TEXT("Grenade_Pin"), S.ToWav());
		}
		{
			FCampusSynth S(0.6f, 104);
			S.Tone(0.f, 0.4f, 2900.f, 2850.f, 0.3f, 0.1f, 0.06f);
			S.Tone(0.f, 0.3f, 4700.f, 4650.f, 0.15f, 0.1f, 0.04f);
			S.Click(0.f, 0.6f, 7000.f);
			S.Click(0.32f, 0.4f, 6000.f);
			S.Normalize(0.7f);
			Add(TEXT("Grenade_Spoon"), S.ToWav());
		}
		for (int32 Take = 0; Take < 3; ++Take)
		{
			FCampusSynth S(0.4f, 105 + Take);
			S.Noise(0.f, 0.02f, 0.9f, 0.1f, 0.004f, 7000.f, 500.f);
			S.Tone(0.f, 0.2f, 1700.f + Take * 140.f, 1650.f, 0.25f, 0.1f, 0.05f);
			S.Tone(0.f, 0.08f, 180.f, 120.f, 0.5f, 0.2f, 0.02f);
			S.Normalize(0.85f);
			Add(Take == 0 ? TEXT("Grenade_Bounce") : (Take == 1 ? TEXT("Grenade_Bounce_2") : TEXT("Grenade_Bounce_3")), S.ToWav());
		}
		{
			FCampusSynth S(0.2f, 108);
			S.Noise(0.f, 0.08f, 0.8f, 0.3f, 0.02f, 900.f, 60.f);
			S.Normalize(0.7f);
			Add(TEXT("Grenade_Bounce_Soft"), S.ToWav());
		}
		for (int32 Take = 0; Take < 3; ++Take)
		{
			FCampusSynth S(3.5f, 110 + Take);
			S.Noise(0.f, 0.01f, 1.f, 0.02f, 0.003f, 16000.f, 1200.f);
			S.Tone(0.f, 0.9f, 80.f, 30.f, 1.f, 1.f, 0.25f, 0.5f);
			S.Noise(0.f, 3.4f, 1.f, 1.f, 0.5f, 1800.f, 25.f);
			S.Noise(0.f, 0.4f, 0.6f, 0.5f, 0.08f, 6000.f, 400.f);
			S.Echo(0.12f, 0.3f, 900.f);
			S.Reverb(0.35f, 0.9f);
			S.SoftClip(2.4f);
			S.Normalize(0.98f);
			Add(Take == 0 ? TEXT("Frag_Explode") : (Take == 1 ? TEXT("Frag_Explode_2") : TEXT("Frag_Explode_3")), S.ToWav());
		}
		{
			FCampusSynth S(3.f, 113);
			FRandomStream R(1130);
			for (int32 i = 0; i < 60; ++i)
			{
				S.Click(0.2f + FMath::Square(R.FRand()) * 2.5f, R.FRandRange(0.1f, 0.5f), R.FRandRange(2000.f, 6000.f));
			}
			S.Normalize(0.6f);
			Add(TEXT("Explosion_Debris"), S.ToWav());
		}
		{
			FCampusSynth S(3.f, 114);
			S.Tone(0.f, 0.8f, 60.f, 30.f, 1.f, 5.f, 0.3f);
			S.Noise(0.f, 2.8f, 0.9f, 10.f, 0.7f, 600.f, 25.f);
			S.Reverb(0.4f, 0.9f);
			S.Normalize(0.8f);
			Add(TEXT("Explosion_Far"), S.ToWav());
		}
		for (int32 Take = 0; Take < 2; ++Take)
		{
			FCampusSynth S(2.5f, 115 + Take);
			S.Noise(0.f, 0.012f, 1.f, 0.02f, 0.004f, 18000.f, 2500.f);
			S.Noise(0.f, 0.5f, 0.8f, 0.2f, 0.09f, 9000.f, 300.f);
			S.Tone(0.f, 0.3f, 120.f, 50.f, 0.7f, 0.5f, 0.08f, 0.4f);
			S.Reverb(0.3f, 0.8f);
			S.SoftClip(2.f);
			S.Normalize(0.98f);
			Add(Take == 0 ? TEXT("Flash_Bang") : TEXT("Flash_Bang_2"), S.ToWav());
		}
		{
			FCampusSynth S(0.6f, 117);
			S.Tone(0.f, 0.1f, 160.f, 90.f, 0.8f, 0.5f, 0.03f);
			S.Noise(0.01f, 0.5f, 0.5f, 5.f, 0.15f, 6000.f, 400.f);
			S.Normalize(0.8f);
			Add(TEXT("Smoke_Pop"), S.ToWav());
		}
		{
			FCampusSynth S(16.f, 118);
			S.Noise(0.f, 16.f, 0.6f, 40.f, 6.f, 7000.f, 300.f);
			S.Normalize(0.6f);
			Add(TEXT("Smoke_Hiss"), S.ToWav());
		}
		{
			FCampusSynth S(1.2f, 119);
			S.SweepNoise(0.f, 0.6f, 0.7f, 600.f, 8000.f, 3000.f, 0.15f);
			S.Normalize(0.8f);
			Add(TEXT("Thermite_Ignite"), S.ToWav());
		}
		{
			FCampusSynth S(7.f, 120);
			FRandomStream R(1200);
			S.Noise(0.f, 7.f, 0.5f, 30.f, 60.f, 9000.f, 1200.f);
			for (int32 i = 0; i < 500; ++i)
			{
				S.Click(R.FRandRange(0.f, 6.9f), R.FRandRange(0.1f, 0.6f), R.FRandRange(3000.f, 9000.f));
			}
			S.Normalize(0.7f);
			Add(TEXT("Thermite_Burn"), S.ToWav());
		}
		{
			FCampusSynth S(6.f, 121);
			S.Tone(0.f, 6.f, 3720.f, 3720.f, 0.4f, 60.f, 2.6f);
			S.Tone(0.f, 6.f, 3733.f, 3733.f, 0.15f, 60.f, 2.6f);
			S.Normalize(0.5f);
			Add(TEXT("Tinnitus"), S.ToWav());
		}
		// Glass: the crack of the pane, the rush of fragments and a rain of small pieces hitting the floor.
		auto Tinkles = [](FCampusSynth& S, FRandomStream& R, int32 Count, float T0, float Spread, float F0, float F1, float Amp)
		{
			for (int32 i = 0; i < Count; ++i)
			{
				const float T = T0 + FMath::Square(R.FRand()) * Spread;
				const float F = R.FRandRange(F0, F1);
				const float A = Amp * R.FRandRange(0.3f, 1.f) * (1.f - 0.6f * (T - T0) / FMath::Max(Spread, 0.01f));
				S.Tone(T, 0.09f, F, F * R.FRandRange(0.97f, 1.f), A, 0.05f, R.FRandRange(0.005f, 0.03f));
				S.Tone(T, 0.06f, F * 1.53f, F * 1.5f, A * 0.4f, 0.05f, R.FRandRange(0.004f, 0.02f));
				S.Noise(T, 0.01f, A * 0.8f, 0.02f, 0.0018f, 15000.f, F * 0.8f);
			}
		};
		for (int32 Take = 0; Take < 3; ++Take)
		{
			FCampusSynth S(1.8f, 70 + Take);
			FRandomStream R(700 + Take * 13);
			S.Noise(0.f, 0.02f, 1.f, 0.02f, 0.0035f, 16000.f, 2000.f);
			S.Tone(0.f, 0.12f, 190.f, 90.f, 0.45f, 0.3f, 0.03f);
			S.Noise(0.f, 0.45f, 0.55f, 1.f, 0.11f, 12500.f, 2600.f);
			Tinkles(S, R, 70, 0.004f, 0.55f, 2600.f, 9200.f, 0.24f);
			// Pieces landing (most of the pane comes down a third of a second later).
			S.Noise(0.32f, 0.5f, 0.3f, 8.f, 0.14f, 9000.f, 1800.f);
			Tinkles(S, R, 55, 0.3f, 0.95f, 1500.f, 6500.f, 0.18f);
			S.Reverb(0.14f, 0.4f);
			S.Normalize(0.95f);
			Add(Take == 0 ? TEXT("Glass_Break") : (Take == 1 ? TEXT("Glass_Break_2") : TEXT("Glass_Break_3")), S.ToWav());
		}
		{
			FCampusSynth S(1.f, 74);
			FRandomStream R(741);
			S.Noise(0.f, 0.6f, 0.2f, 6.f, 0.16f, 9000.f, 1800.f);
			Tinkles(S, R, 45, 0.f, 0.8f, 1400.f, 6000.f, 0.2f);
			S.Normalize(0.8f);
			Add(TEXT("Glass_Debris"), S.ToWav());
		}
		for (int32 Take = 0; Take < 2; ++Take)
		{
			FCampusSynth S(0.35f, 75 + Take);
			FRandomStream R(751 + Take);
			S.Noise(0.f, 0.012f, 1.f, 0.02f, 0.0022f, 16000.f, 3500.f);
			Tinkles(S, R, 14, 0.002f, 0.18f, 3200.f, 9000.f, 0.22f);
			S.Normalize(0.8f);
			Add(Take == 0 ? TEXT("Impact_Glass") : TEXT("Impact_Glass_2"), S.ToWav());
		}
		// The space answering a shot, layered on the player's own gun: a room's boom and flutter, or the yard's echoes.
		for (int32 Take = 0; Take < 3; ++Take)
		{
			FCampusSynth S(0.9f, 80 + Take);
			FRandomStream R(801 + Take);
			S.Tone(0.f, 0.45f, 72.f, 44.f, 0.55f, 2.f, 0.11f, 0.3f);
			S.Noise(0.006f, 0.8f, 0.55f, 3.f, 0.15f, 3600.f, 110.f);
			S.Echo(0.0105f * R.FRandRange(0.9f, 1.1f), 0.45f, 4200.f);
			S.Echo(0.0185f * R.FRandRange(0.9f, 1.1f), 0.32f, 3200.f);
			S.Echo(0.031f * R.FRandRange(0.9f, 1.1f), 0.22f, 2200.f);
			S.Reverb(0.6f, 0.45f);
			S.SoftClip(1.3f);
			S.Normalize(0.85f);
			Add(Take == 0 ? TEXT("Gun_Tail_Indoor") : (Take == 1 ? TEXT("Gun_Tail_Indoor_2") : TEXT("Gun_Tail_Indoor_3")), S.ToWav());
		}
		for (int32 Take = 0; Take < 3; ++Take)
		{
			FCampusSynth S(2.2f, 85 + Take);
			FRandomStream R(851 + Take);
			// Discrete slaps off the classroom blocks across the yard, then the long low roll over the town.
			for (const float T : { 0.11f, 0.24f, 0.42f, 0.66f })
			{
				const float At = T * R.FRandRange(0.85f, 1.15f);
				S.Noise(At, 0.09f, 0.42f * (1.f - At * 0.8f), 4.f, 0.035f, 2600.f - At * 1500.f, 140.f);
				S.Tone(At, 0.12f, 95.f, 60.f, 0.25f * (1.f - At), 3.f, 0.04f);
			}
			S.Noise(0.05f, 2.1f, 0.42f, 80.f, 0.55f, 850.f, 35.f);
			S.Reverb(0.32f, 0.85f);
			S.Normalize(0.8f);
			Add(Take == 0 ? TEXT("Gun_Tail_Outdoor") : (Take == 1 ? TEXT("Gun_Tail_Outdoor_2") : TEXT("Gun_Tail_Outdoor_3")), S.ToWav());
		}
		{
			FCampusSynth S(0.35f, 41);
			S.Noise(0.f, 0.15f, 0.8f, 0.2f, 0.02f, 5000.f, 400.f);
			S.Noise(0.01f, 0.25f, 0.3f, 2.f, 0.06f, 2400.f, 150.f);
			S.Reverb(0.15f, 0.3f);
			S.Normalize(0.85f);
			Add(TEXT("Impact_Concrete"), S.ToWav());
		}
		{
			FCampusSynth S(0.5f, 42);
			S.Tone(0.f, 0.45f, 2400.f, 2350.f, 0.45f, 0.1f, 0.09f);
			S.Tone(0.f, 0.45f, 3720.f, 3690.f, 0.25f, 0.1f, 0.06f);
			S.Click(0.f, 0.6f, 7000.f);
			S.Normalize(0.85f);
			Add(TEXT("Impact_Metal"), S.ToWav());
		}
		{
			FCampusSynth S(0.2f, 43);
			S.Tone(0.f, 0.09f, 420.f, 300.f, 0.6f, 0.2f, 0.018f);
			S.Noise(0.f, 0.08f, 0.5f, 0.2f, 0.015f, 3000.f, 300.f);
			S.Normalize(0.85f);
			Add(TEXT("Impact_Wood"), S.ToWav());
		}
		{
			FCampusSynth S(0.25f, 44);
			S.Noise(0.f, 0.2f, 0.8f, 0.5f, 0.04f, 1500.f, 80.f);
			S.Normalize(0.8f);
			Add(TEXT("Impact_Dirt"), S.ToWav());
		}
		{
			FCampusSynth S(0.2f, 45);
			S.Tone(0.f, 0.12f, 110.f, 70.f, 0.8f, 1.f, 0.025f);
			S.Noise(0.f, 0.1f, 0.6f, 0.5f, 0.03f, 900.f, 60.f);
			S.Normalize(0.9f);
			Add(TEXT("Impact_Flesh"), S.ToWav());
		}
		{
			FCampusSynth S(0.32f, 46);
			S.SweepNoise(0.f, 0.3f, 0.6f, 2500.f, 7000.f, 1200.f, 0.45f);
			S.Tone(0.05f, 0.2f, 1800.f, 900.f, 0.08f, 20.f, 0.08f);
			S.Normalize(0.8f);
			Add(TEXT("Bullet_Whiz"), S.ToWav());
		}
		// Footsteps per surface, four takes each: a boot is a heel strike, a roll and a toe push-off, never one thud.
		for (int32 Surface = 0; Surface < 4; ++Surface)
		{
			static const TCHAR* Names[4] = { TEXT("Footstep"), TEXT("Footstep_Dirt"), TEXT("Footstep_Metal"), TEXT("Footstep_Wood") };
			for (int32 Take = 0; Take < 4; ++Take)
			{
				FCampusSynth S(0.32f, 470 + Surface * 10 + Take);
				FRandomStream R(4700 + Surface * 31 + Take * 7);
				const float Toe = R.FRandRange(0.045f, 0.075f);
				switch (Surface)
				{
				case 0:
					// Rubber sole on concrete and terrazzo: a dull heel, gritty scuff, a lighter toe.
					S.Noise(0.f, 0.03f, 0.75f, 0.4f, 0.007f, 4200.f, 260.f);
					S.Tone(0.f, 0.06f, R.FRandRange(100.f, 125.f), 68.f, 0.38f, 0.5f, 0.013f);
					S.Noise(0.004f, 0.09f, 0.16f, 6.f, 0.03f, 9500.f, 3200.f);
					S.Noise(Toe, 0.04f, 0.42f, 0.6f, 0.008f, 5200.f, 700.f);
					S.Noise(Toe + 0.01f, 0.08f, 0.12f, 8.f, 0.025f, 8000.f, 2500.f);
					break;
				case 1:
					// Grass and packed earth: soft, low, with a fine crunch of grit and dry blades.
					S.Noise(0.f, 0.12f, 0.75f, 4.f, 0.03f, 1500.f, 55.f);
					S.Tone(0.f, 0.08f, 85.f, 55.f, 0.3f, 2.f, 0.02f);
					for (int32 g = 0; g < 18; ++g)
					{
						S.Noise(R.FRandRange(0.f, 0.11f), 0.006f, R.FRandRange(0.05f, 0.18f), 0.1f, 0.0015f, 7000.f, 1800.f);
					}
					S.Noise(Toe, 0.08f, 0.35f, 3.f, 0.02f, 2400.f, 200.f);
					break;
				case 2:
					// Steel stairs and plate: a clank with the hollow ring of the structure.
					S.Noise(0.f, 0.02f, 0.85f, 0.2f, 0.004f, 9000.f, 600.f);
					S.Tone(0.f, 0.25f, R.FRandRange(400.f, 450.f), 420.f, 0.24f, 0.2f, 0.07f);
					S.Tone(0.f, 0.2f, R.FRandRange(860.f, 940.f), 890.f, 0.15f, 0.2f, 0.05f);
					S.Tone(0.f, 0.15f, 1370.f, 1360.f, 0.08f, 0.2f, 0.035f);
					S.Tone(0.f, 0.08f, 110.f, 70.f, 0.35f, 0.5f, 0.02f);
					S.Noise(Toe, 0.03f, 0.45f, 0.3f, 0.006f, 7000.f, 900.f);
					S.Tone(Toe, 0.15f, 640.f, 630.f, 0.1f, 0.2f, 0.04f);
					break;
				default:
					// Wooden boards (stage, gym): a resonant knock.
					S.Tone(0.f, 0.1f, R.FRandRange(220.f, 260.f), 180.f, 0.5f, 0.3f, 0.028f);
					S.Noise(0.f, 0.05f, 0.5f, 0.3f, 0.01f, 2600.f, 150.f);
					S.Noise(Toe, 0.04f, 0.32f, 0.5f, 0.009f, 3500.f, 400.f);
					S.Tone(Toe, 0.06f, 310.f, 260.f, 0.2f, 0.3f, 0.018f);
					break;
				}
				S.Normalize(0.72f);
				const FString Name = Take == 0 ? FString(Names[Surface]) : FString::Printf(TEXT("%s_%d"), Names[Surface], Take + 1);
				Out.Emplace(Name, S.ToWav());
			}
		}
		{
			FCampusSynth S(0.3f, 48);
			S.Noise(0.f, 0.2f, 0.9f, 1.f, 0.035f, 1400.f, 40.f);
			S.Tone(0.f, 0.12f, 80.f, 55.f, 0.6f, 1.f, 0.03f);
			S.Click(0.03f, 0.25f, 3500.f);
			S.Normalize(0.85f);
			Add(TEXT("Land"), S.ToWav());
		}
		{
			FCampusSynth S(0.6f, 49);
			S.Tone(0.f, 0.2f, 75.f, 50.f, 0.8f, 2.f, 0.06f);
			S.Noise(0.f, 0.3f, 0.7f, 2.f, 0.08f, 700.f, 40.f);
			S.Click(0.12f, 0.3f, 3000.f);
			S.Click(0.2f, 0.25f, 4000.f);
			S.Normalize(0.85f);
			Add(TEXT("Body_Fall"), S.ToWav());
		}
		{
			FCampusSynth S(0.25f, 50);
			S.Tone(0.f, 0.22f, 5200.f, 5100.f, 0.4f, 0.05f, 0.04f);
			S.Tone(0.f, 0.22f, 7900.f, 7800.f, 0.2f, 0.05f, 0.03f);
			S.Normalize(0.6f);
			Add(TEXT("Shell_Clink"), S.ToWav());
		}
		{
			FCampusSynth S(0.3f, 51);
			S.SweepNoise(0.f, 0.26f, 0.6f, 700.f, 3200.f, 800.f, 0.4f);
			S.Normalize(0.7f);
			Add(TEXT("Melee_Swing"), S.ToWav());
		}
		{
			FCampusSynth S(0.25f, 52);
			S.Tone(0.f, 0.15f, 130.f, 80.f, 0.9f, 0.5f, 0.03f);
			S.Noise(0.f, 0.08f, 0.7f, 0.2f, 0.02f, 3000.f, 200.f);
			S.SoftClip(1.6f);
			S.Normalize(0.95f);
			Add(TEXT("Melee_Hit"), S.ToWav());
		}
		{
			FCampusSynth S(0.3f, 53);
			S.Tone(0.f, 0.2f, 85.f, 60.f, 0.9f, 2.f, 0.045f);
			S.Noise(0.f, 0.12f, 0.6f, 1.f, 0.03f, 450.f, 30.f);
			S.Normalize(0.9f);
			Add(TEXT("Player_Hit"), S.ToWav());
		}
		{
			FCampusSynth S(0.9f, 54);
			S.Tone(0.f, 0.8f, 1046.f, 1046.f, 0.32f, 3.f, 0.22f);
			S.Tone(0.09f, 0.8f, 1568.f, 1568.f, 0.3f, 3.f, 0.25f);
			S.Tone(0.18f, 0.7f, 2093.f, 2093.f, 0.2f, 3.f, 0.25f);
			S.Reverb(0.3f, 0.5f);
			S.Normalize(0.7f);
			Add(TEXT("Medal"), S.ToWav());
		}
		{
			FCampusSynth S(1.6f, 55);
			S.SweepNoise(0.f, 0.7f, 0.4f, 200.f, 2200.f, 600.f, 0.8f);
			S.Tone(0.55f, 1.0f, 110.f, 108.f, 0.55f, 15.f, 0.5f, 0.5f);
			S.Tone(0.55f, 1.0f, 165.f, 163.f, 0.35f, 15.f, 0.5f, 0.5f);
			S.Tone(0.55f, 0.9f, 55.f, 50.f, 0.6f, 10.f, 0.4f);
			S.Reverb(0.35f, 0.7f);
			S.Normalize(0.85f);
			Add(TEXT("Match_Start"), S.ToWav());
		}
		{
			FCampusSynth S(0.05f, 61);
			S.Tone(0.f, 0.04f, 2400.f, 2400.f, 0.2f, 1.f, 0.01f);
			S.Normalize(0.35f);
			Add(TEXT("UI_Hover"), S.ToWav());
		}
		{
			FCampusSynth S(0.14f, 62);
			S.Tone(0.f, 0.08f, 1500.f, 1500.f, 0.35f, 1.f, 0.025f);
			S.Tone(0.035f, 0.09f, 2250.f, 2250.f, 0.3f, 1.f, 0.03f);
			S.Normalize(0.55f);
			Add(TEXT("UI_Select"), S.ToWav());
		}
		{
			FCampusSynth S(0.14f, 63);
			S.Tone(0.f, 0.12f, 1800.f, 1100.f, 0.35f, 1.f, 0.035f);
			S.Normalize(0.5f);
			Add(TEXT("UI_Back"), S.ToWav());
		}
		{
			FCampusSynth S(0.06f, 64);
			S.Tone(0.f, 0.05f, 1200.f, 1200.f, 0.3f, 0.5f, 0.012f);
			S.Noise(0.f, 0.01f, 0.2f, 0.05f, 0.002f, 9000.f, 2500.f);
			S.Normalize(0.45f);
			Add(TEXT("UI_Tab"), S.ToWav());
		}
		{
			FCampusSynth S(1.3f, 65);
			S.SweepNoise(0.f, 1.25f, 0.5f, 250.f, 2600.f, 300.f, 0.42f);
			S.Reverb(0.2f, 0.6f);
			S.Normalize(0.55f);
			Add(TEXT("UI_Whoosh"), S.ToWav());
		}
		{
			FCampusSynth S(2.0f, 66);
			S.SweepNoise(0.f, 1.2f, 0.6f, 200.f, 4000.f, 500.f, 0.7f);
			S.Tone(0.75f, 1.2f, 62.f, 38.f, 0.8f, 15.f, 0.45f);
			S.Noise(0.75f, 1.0f, 0.4f, 5.f, 0.3f, 600.f, 30.f);
			S.Reverb(0.3f, 0.8f);
			S.SoftClip(1.4f);
			S.Normalize(0.9f);
			Add(TEXT("UI_Deploy"), S.ToWav());
		}
		return Out;
	}
}
