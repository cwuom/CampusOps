# Builds the game's recorded sound effects from the CC0 sources in SourceArt/Audio/Sources (run fetch_audio.py
# first). Output: SourceArt/Audio/SFX_<Name>.wav, which the bootstrap imports in place of the synthesized take.
# Usage: python BuildAudio.py [--sheet]
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import audio_lib as A

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "SourceArt", "Audio", "Sources"))
OUT = os.path.normpath(os.path.join(HERE, "..", "SourceArt", "Audio"))
SR = A.SR
rng = np.random.default_rng(7)

_cache = {}


def src(sid):
    if sid not in _cache:
        for ext in ("ogg", "mp3"):
            p = os.path.join(SRC, f"{sid}.{ext}")
            if os.path.exists(p):
                _cache[sid] = A.load(p)
                break
    return _cache[sid]


def transients(x, thresh_db=-24.0, gap=0.12):
    """Times (s) of the attacks louder than thresh_db below the peak."""
    env = A.envelope(x, 0.0005, 0.03)
    peak = env.max() + 1e-9
    level = peak * 10 ** (thresh_db / 20)
    out = []
    i = 0
    n = len(env)
    hop = int(0.002 * SR)
    while i < n:
        if env[i] > level:
            j = min(n, i + int(0.03 * SR))
            k = i + int(np.argmax(env[i:j]))
            out.append(k / SR)
            i = k + int(gap * SR)
        else:
            i += hop
    return out


def tone(f0, f1, dur, decay, amp=1.0, attack=0.0005):
    t = np.arange(int(dur * SR)) / SR
    f = f0 * (f1 / f0) ** (t / dur)
    ph = 2 * np.pi * np.cumsum(f) / SR
    env = np.minimum(t / attack, 1.0) * np.exp(-t / decay)
    return (np.sin(ph) * env * amp).astype(np.float32)


def noise(dur, decay, lo, hi, amp=1.0, attack=0.0003):
    t = np.arange(int(dur * SR)) / SR
    n = rng.standard_normal(len(t)).astype(np.float32)
    n = A.bandpass(n, lo, hi, 2)
    env = np.minimum(t / attack, 1.0) * np.exp(-t / decay)
    return (n * env * amp).astype(np.float32)


def tick(gain_db=0.0, bright=1.0):
    """The hit tick: a 2 ms broadband snap with a little metal in it (a pressed steel latch, not a beep)."""
    x = A.pad_to(A.normalize(noise(0.02, 0.0016, 2500 * bright, 15000, 1.0), -1), 0.03)
    x += A.normalize(noise(0.03, 0.004, 1500, 5200), -12)
    for f, a, d in ((5130 * bright, -20, 0.006), (7420 * bright, -24, 0.004)):
        x += A.gain_db(tone(f, f * 0.99, 0.03, d), a)
    return A.gain_db(A.fade(x, 0.0001, 0.005), gain_db)


def write(name, x, peak_db=-1.0, fade_out=0.04):
    x = A.fade(A.normalize(x, peak_db), 0.0005, fade_out)
    A.save_wav(os.path.join(OUT, f"SFX_{name}.wav"), x)
    built.append((name, x))
    print(f"  {name}: {A.seconds(x):.2f}s")


built = []


def at_peak(x, t0, t1):
    """Segment [t0, t1) shifted so it starts 3 ms before its loudest sample."""
    s = A.cut(x, t0, t1)
    p = int(np.argmax(np.abs(s)))
    return s[max(0, p - int(0.003 * SR)):]


# ------------------------------------------------------------------ explosions

def build_explosions():
    crack = src(734099)            # real oxyhydrogen detonation: the crack and the outdoor reverb
    blast = src(752630)            # recorded blast with shrapnel
    body = src(734100)             # recorded body / rumble
    far = src(784165)              # distant explosion, clean tail
    for take, (pitch, crack_db, blast_db, body_db) in enumerate([(1.0, 0, -2, -4), (0.95, -1, 0, -3), (1.05, 0, -3, -5)]):
        c = at_peak(crack, 0.0, 4.6)
        b = A.resample_pitch(at_peak(blast, 0.3, 4.2), pitch)
        d = A.lowpass(A.resample_pitch(at_peak(body, 0.0, 3.9), pitch * 0.92), 900)
        # The ground shock under it all: a sub-heavy push in the first 150 ms.
        sub = A.lowpass(tone(70 * pitch, 32, 0.6, 0.12, 1.0, 0.002), 140)
        x = A.mix((c, 0.0, crack_db), (b, 0.0, blast_db), (d, 0.004, body_db), (A.normalize(sub, -1), 0.0, -6))
        x = A.shelf(x, 120, 4.0, low=True)
        x = A.transient_shape(x, 3.0, 0.0)
        # Glued and loud: the body and the roll come up under the crack, as a mixed game explosion does.
        x = A.compress(A.normalize(x, -1), -16.0, 3.5, 0.004, 0.25, 6.0)
        x = A.saturate(A.normalize(x, -1), 1.8)
        write("Frag_Explode" if take == 0 else f"Frag_Explode_{take + 1}", A.pad_to(x, 4.2), -0.8, 0.9)
    # Debris pattering down after the blast.
    deb = A.highpass(A.cut(src(132929), 1.6, 5.8), 220)
    deb = A.fade(deb, 0.25, 0.8)
    write("Explosion_Debris", deb, -5.0, 0.8)
    # Heard from across the school: the low thump and a long roll.
    f = A.lowpass(at_peak(far, 0.0, 4.0), 2600)
    write("Explosion_Far", A.mix((f, 0.0, 0), (A.lowpass(at_peak(body, 0.0, 3.9), 400), 0.01, -6)), -2.0, 0.8)


def build_flash():
    cts = src(693421)              # real CTS 7290 flashbang
    lay = src(737326)              # recorded pyrotechnic, designed
    for take, pitch in enumerate([1.0, 0.96]):
        a = at_peak(cts, 0.0, 1.03)
        b = A.resample_pitch(at_peak(lay, 0.0, 4.0), pitch)
        x = A.mix((a, 0.0, 0), (b, 0.0, -5), (A.highpass(b, 3000), 0.0, -8))
        x = A.simple_reverb(x, 1.6, 0.18, 0.02, 5000, seed=11 + take)
        x = A.compress(A.normalize(x, -1), -14.0, 3.0, 0.002, 0.2, 4.0)
        x = A.saturate(A.normalize(x, -1), 1.8)
        write("Flash_Bang" if take == 0 else "Flash_Bang_2", A.pad_to(x, 3.2), -0.8, 0.8)


def build_smoke():
    gas = src(551429)
    air = src(457294)
    steady = A.highpass(A.cut(gas, 0.8, 8.3), 110)
    jet = A.highpass(A.cut(air, 10.5, 15.5), 160)
    hiss = A.loop_crossfade(steady, 16.0, 0.5, seed=1) * 0.85 + A.loop_crossfade(jet, 16.0, 0.6, seed=2) * 0.25
    hiss = A.compress(hiss, -26.0, 3.0, 0.05, 0.6, 0.0)
    # Fierce for the first seconds as the filler catches, steady after, fading as it burns out.
    t = np.arange(len(hiss)) / SR
    env = (0.6 + 0.4 * np.exp(-t / 2.5)) * np.minimum(t / 0.08, 1.0) * np.clip((16.0 - t) / 3.0, 0, 1)
    write("Smoke_Hiss", (hiss * env).astype(np.float32), -3.0, 0.5)
    # The fuse pop: a dull knock and a first rush of gas.
    knock = A.lowpass(A.resample_pitch(at_peak(src(276600), 0.15, 0.36), 0.75), 1600)
    rush = A.fade(A.highpass(A.cut(air, 10.0, 10.7), 300), 0.01, 0.4)
    write("Smoke_Pop", A.mix((knock, 0.0, 0), (rush, 0.01, -4)), -2.0, 0.2)


def build_thermite():
    sp = src(316682)
    base = A.cut(sp, 0.5, 16.5)
    burn = A.loop_crossfade(base, 7.0, 0.2, seed=4)
    heavy = A.loop_crossfade(A.resample_pitch(base, 0.72), 7.0, 0.2, seed=5)
    # Burning metal: the sparkler's fizz, a heavier crackle under it, and the roar of the jet.
    roar = A.lowpass(noise(7.0, 1e9, 60, 900, 1.0, 0.05), 700)
    x = burn * 1.0 + heavy * 0.8 + A.normalize(roar, -1) * 0.12
    x = A.shelf(x, 3000, 2.0, low=False)
    # The crackle is all needle-thin peaks: clip them so the fizz carries.
    x = A.saturate(A.normalize(x, 4.0), 3.0)
    x = A.compress(A.normalize(x, -1), -24.0, 4.0, 0.001, 0.08, 8.0)
    write("Thermite_Burn", x.astype(np.float32), -2.0, 0.4)
    ign = A.mix((at_peak(sp, 1.2, 2.4), 0.0, 0), (A.resample_pitch(at_peak(sp, 1.2, 2.4), 0.6), 0.0, -2), (A.normalize(noise(0.7, 0.2, 400, 6000), -1), 0.0, -6))
    write("Thermite_Ignite", A.saturate(A.normalize(ign, -1), 1.5), -1.0, 0.3)


def build_grenade_foley():
    pin = src(725813)
    ts = sorted(transients(pin, -20.0, 0.4), key=lambda t: -np.max(np.abs(A.cut(pin, t - 0.005, t + 0.05))))
    for i, t0 in enumerate(ts[:2]):
        p = A.highpass(A.cut(pin, t0 - 0.01, t0 + 0.32), 400)
        write("Grenade_Pin" if i == 0 else "Grenade_Pin_2", p, -3.0, 0.08)
    spoon = src(259553)
    write("Grenade_Spoon", A.cut(spoon, 0.12, 1.25), -5.0, 0.2)
    thud = at_peak(src(276600), 0.15, 0.36)
    clink = at_peak(spoon, 0.4, 0.75)
    for take, (p1, p2) in enumerate([(0.9, 0.55), (0.82, 0.62), (1.0, 0.5)]):
        x = A.mix((A.lowpass(A.resample_pitch(thud, p1), 5000), 0.0, 0), (A.resample_pitch(clink, p2), 0.002, -7))
        write("Grenade_Bounce" if take == 0 else f"Grenade_Bounce_{take + 1}", x, -3.0, 0.05)
    write("Grenade_Bounce_Soft", A.lowpass(A.resample_pitch(thud, 0.7), 700), -6.0, 0.05)


# ------------------------------------------------------------------ hit confirms

def build_hits():
    thud = at_peak(src(276600), 0.15, 0.36)       # gloved hand on a helmet: a hard, padded knock
    punch = at_peak(src(237982), 0.1, 0.6)         # body punch
    crunch_src = src(259542)                       # stabs: wet, crunchy transients
    crunches = [A.fade(at_peak(crunch_src, t - 0.02, t + 0.09), 0.0005, 0.03) for t in transients(crunch_src, -18.0, 0.5)]
    # Kill: the tick, the armour knock with a crunch behind it, and the weight of a body punch - short, dry, felt more
    # than heard; no pitch, no musical tail.
    for take in range(2):
        cr = A.highpass(crunches[(take * 3) % len(crunches)], 1400)
        body = A.lowpass(A.resample_pitch(thud, 0.88 - 0.05 * take), 4200)
        low = A.lowpass(punch, 220)
        x = A.mix((tick(0, 1.0 - 0.05 * take), 0.0, -2), (body, 0.0015, 0), (cr, 0.004, -9), (low, 0.0, -5))
        x = A.transient_shape(x, 2.0, -3.0)
        x = A.saturate(A.normalize(x, -1), 1.4)
        write("Hit_Kill" if take == 0 else "Hit_Kill_2", A.pad_to(x, 0.32), -1.5, 0.12)
    # Body hit: just the tick and a soft knock under it.
    for take in range(3):
        x = A.mix((tick(0, 0.95 + 0.05 * take), 0.0, 0), (A.lowpass(A.resample_pitch(thud, 1.15 + 0.06 * take), 2200), 0.001, -11))
        write("Hitmarker" if take == 0 else f"Hitmarker_{take + 1}", A.pad_to(x, 0.1), -2.0, 0.04)
    # Head: harder and brighter, a crack through the helmet with a short ringing ping of struck plate.
    for take in range(2):
        cr = A.highpass(crunches[(take * 2 + 1) % len(crunches)], 1800)
        ping = tone(3260 + 140 * take, 3200, 0.16, 0.035, 1.0, 0.0003) + 0.5 * tone(4990, 4950, 0.16, 0.022) + 0.3 * tone(7310, 7290, 0.16, 0.012)
        x = A.mix((tick(0, 1.15), 0.0, 0), (cr, 0.002, -5), (A.lowpass(A.resample_pitch(thud, 1.05), 5000), 0.001, -6), (A.normalize(ping, -1), 0.0, -15))
        x = A.saturate(A.normalize(x, -1), 1.3)
        write("Hit_Head" if take == 0 else "Hit_Head_2", A.pad_to(x, 0.26), -1.0, 0.1)
    # A bullet striking a body (heard in the world): wet crunch and a dull thump.
    for take in range(3):
        cr = crunches[(take * 2) % len(crunches)]
        x = A.mix((cr, 0.0, -2), (A.lowpass(A.resample_pitch(punch, 1.2 + 0.08 * take), 900), 0.0, 0))
        write("Impact_Flesh" if take == 0 else f"Impact_Flesh_{take + 1}", A.pad_to(x, 0.35), -1.5, 0.1)


def build_tinnitus():
    # The ring after a blast: a pure high tone with a slow beat, and the muffled hiss of blood in the ears.
    dur = 7.0
    t = np.arange(int(dur * SR)) / SR
    ring = np.sin(2 * np.pi * 3720 * t) + 0.35 * np.sin(2 * np.pi * 3733 * t) + 0.12 * np.sin(2 * np.pi * 7440 * t)
    env = np.minimum(t / 0.06, 1.0) * np.exp(-t / 2.6)
    hiss = A.lowpass(rng.standard_normal(len(t)).astype(np.float32), 500) * np.exp(-t / 3.0)
    x = ring * env * 0.5 + A.normalize(hiss, -1) * 0.12
    write("Tinnitus", x.astype(np.float32), -6.0, 1.0)


# ------------------------------------------------------------------ the body: breathing and heartbeat

def breath_cycles(x, min_gap, win_before, win_after, thresh_db=-22.0):
    """Cuts one breath (or beat) round each loud moment."""
    out = []
    for t in transients(A.lowpass(x, 3000), thresh_db, min_gap):
        s = A.cut(x, max(0.0, t - win_before), t + win_after)
        if A.seconds(s) > win_before + win_after * 0.8:
            out.append(A.fade(s, 0.04, 0.12))
    return out


def build_breath():
    # Winded after a run: whole in-out cycles from a recorded post-workout breath and from panting.
    run = A.highpass(src(319103), 90)
    pant = A.highpass(src(395563), 90)
    takes = breath_cycles(run, 0.9, 0.45, 0.75, -20.0)[:3] + breath_cycles(pant, 0.6, 0.3, 0.55, -18.0)[2:4]
    for i, x in enumerate(takes[:4]):
        write("Breath_Sprint" if i == 0 else f"Breath_Sprint_{i + 1}", A.compress(A.normalize(x, -1), -18.0, 2.5, 0.01, 0.2, 2.0), -5.0, 0.12)
    # At rest: a slow, quiet breath now and then (soft, the top end rolled off so it never sounds close-miked).
    calm = A.lowpass(A.highpass(src(240867), 80), 3500)
    calm_takes = breath_cycles(calm, 2.5, 0.6, 1.6, -14.0)[:2]
    for i, x in enumerate(calm_takes):
        write("Breath_Calm" if i == 0 else f"Breath_Calm_{i + 1}", A.normalize(x, -1), -14.0, 0.3)
    # Holding the breath on the scope: a sharp intake; letting it go: a long exhale.
    write("Breath_Hold", A.fade(A.highpass(A.cut(src(169931), 0.0, 0.85), 120), 0.01, 0.2), -6.0, 0.2)
    write("Breath_Exhale", A.fade(A.highpass(A.cut(src(146769), 2.1, 4.3), 100), 0.03, 0.5), -6.0, 0.4)


# ------------------------------------------------------------------ weather and places

def make_loop(x, length_s, xfade_s=2.0):
    """A seamless loop: the tail crossfades into the head, so the end runs straight into the start."""
    n = int(length_s * SR)
    xf = int(xfade_s * SR)
    seg = x[: n + xf].copy()
    if len(seg) < n + xf:
        seg = np.concatenate([seg, np.zeros(n + xf - len(seg), np.float32)])
    out = seg[:n].copy()
    w = np.sin(np.linspace(0, np.pi / 2, xf)) ** 2
    out[:xf] = seg[:xf] * w + seg[n:n + xf] * (1.0 - w)
    return out


def write_loop(name, x, peak_db):
    x = A.normalize(x, peak_db)
    A.save_wav(os.path.join(OUT, f"SFX_{name}.wav"), x)
    built.append((name, x))
    print(f"  {name}: {A.seconds(x):.2f}s (loop)")


def build_weather():
    rain = A.highpass(A.cut(src(525046), 200.0, 236.0), 60)
    write_loop("Rain_Loop", make_loop(A.compress(rain, -30.0, 2.0, 0.05, 0.5, 0.0), 32.0, 3.0), -4.0)
    # Heard from inside: the roof and the glass take the hiss away, the drumming and the gutters stay.
    patter = A.loop_crossfade(A.highpass(src(167034), 200), A.seconds(rain), 0.4, seed=9)
    inside = A.lowpass(rain, 750, 4) + A.lowpass(A.pad_to(patter, A.seconds(rain)), 1800) * 0.4
    write_loop("Rain_Indoor", make_loop(A.simple_reverb(inside, 0.6, 0.25, 0.01, 1500), 32.0, 3.0), -7.0)
    wind = A.highpass(A.cut(src(574566), 60.0, 102.0), 40)
    write_loop("Wind_Loop", make_loop(wind, 38.0, 3.0), -5.0)
    near = A.highpass(at_peak(src(446753), 0.0, 14.0), 25)
    write("Thunder_Near", A.saturate(A.normalize(A.shelf(near, 120, 3.0, low=True), -1), 1.3), -1.0, 2.5)
    clap = A.highpass(at_peak(src(194364), 12.0, 40.0), 25)
    write("Thunder_Near_2", A.normalize(clap, -1), -1.5, 3.0)
    far = A.lowpass(A.cut(src(244053), 6.0, 16.0), 900)
    write("Thunder_Far", A.fade(far, 0.4, 2.0), -4.0, 2.0)
    write("Thunder_Far_2", A.fade(A.lowpass(near, 500), 0.6, 3.0), -6.0, 3.0)
    # The deploy drop: wind swelling round the head as the ground rushes up, easing off at the flare.
    gust = A.cut(src(438885), 12.0, 16.0)
    t = np.arange(len(gust)) / SR
    env = np.minimum(t / 0.25, 1.0) * np.clip((3.8 - t) / 2.2, 0.0, 1.0) ** 1.4
    whoosh = A.highpass(gust, 300) * env + A.lowpass(gust, 300) * env * 0.6
    write("Drop_Wind", whoosh.astype(np.float32), -2.0, 0.3)


def bubbles(x, t0, t1, count, f_lo, f_hi, amp, seed):
    """Rising resonant chirps (air pockets collapsing): what makes moving water sound wet rather than like hiss."""
    r = np.random.default_rng(seed)
    for _ in range(count):
        f0 = r.uniform(f_lo, f_hi)
        b = tone(f0, f0 * r.uniform(1.4, 2.2), r.uniform(0.02, 0.06), r.uniform(0.008, 0.02), amp * r.uniform(0.3, 1.0))
        i = int(r.uniform(t0, t1) * SR)
        n = min(len(b), len(x) - i)
        if n > 0:
            x[i:i + n] += b[:n]
    return x


def build_water():
    # Wading: a leg pushing through knee-deep water, the swash round it and the gurgle as it lifts out.
    for k, name in enumerate(("Water_Wade", "Water_Wade_2", "Water_Wade_3")):
        dur = 0.55
        x = A.pad_to(noise(dur, 0.16, 250, 2600, 1.0, attack=0.05), dur)
        x += A.pad_to(noise(dur, 0.09, 80, 400, 0.6, attack=0.02), dur)
        x = bubbles(x, 0.08, 0.42, 14, 450, 1500, 0.22, 40 + k)
        x = bubbles(x, 0.25, 0.5, 6, 1500, 3200, 0.1, 50 + k)
        write(name, A.lowpass(x, 7000), -4.0, 0.08)
    # Jumping or dropping in: the slap, the plume and the droplets raining back.
    for k, name in enumerate(("Water_Splash", "Water_Splash_2")):
        dur = 1.4
        x = A.pad_to(noise(0.3, 0.012, 600, 9000, 1.0), dur)
        x += A.pad_to(noise(dur, 0.28, 200, 5000, 0.8, attack=0.01), dur)
        x += A.pad_to(noise(0.5, 0.12, 50, 300, 0.7, attack=0.004), dur)
        x = bubbles(x, 0.05, 0.9, 40, 300, 1600, 0.25, 60 + k)
        r = np.random.default_rng(70 + k)
        for _ in range(60):
            i = int(r.uniform(0.25, 1.25) * SR)
            d = noise(0.02, 0.004, 2000, 9000, r.uniform(0.05, 0.2))
            n = min(len(d), len(x) - i)
            x[i:i + n] += d[:n]
        write(name, x, -1.5, 0.2)
    # A round into the water: a hard plip, the cavity collapsing, a little spray.
    for k, name in enumerate(("Bullet_Water", "Bullet_Water_2")):
        dur = 0.35
        x = A.pad_to(noise(0.03, 0.004, 1800, 12000, 1.0), dur)
        x += A.pad_to(tone(700 + 150 * k, 2400, 0.06, 0.03, 0.8), dur)
        x += A.pad_to(noise(dur, 0.09, 1500, 8000, 0.35, attack=0.01), dur)
        x = bubbles(x, 0.03, 0.2, 6, 900, 2600, 0.2, 80 + k)
        write(name, x, -2.0, 0.05)


def build_ambience():
    write_loop("Amb_Rooftop", make_loop(A.highpass(A.cut(src(728589), 30.0, 72.0), 50), 38.0, 4.0), -9.0)
    write_loop("Amb_City", make_loop(A.highpass(A.cut(src(426894), 60.0, 102.0), 50), 38.0, 4.0), -10.0)
    write_loop("Amb_CityNight", make_loop(A.highpass(A.cut(src(427841), 20.0, 62.0), 40), 38.0, 4.0), -12.0)
    write_loop("Amb_HQ", make_loop(A.highpass(A.cut(src(474832), 30.0, 64.0), 40), 30.0, 3.0), -10.0)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    only = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not only or "classic" in only:
        build_explosions()
        build_flash()
        build_smoke()
        build_thermite()
        build_grenade_foley()
        build_hits()
        build_tinnitus()
    if not only or "body" in only:
        build_breath()
    if not only or "weather" in only:
        build_weather()
        build_ambience()
    if not only or "water" in only:
        build_water()
    if "--sheet" in sys.argv:
        work = os.path.normpath(os.path.join(HERE, "..", "Saved", "AudioWork"))
        os.makedirs(work, exist_ok=True)
        half = (len(built) + 1) // 2
        A.spectrogram_png(os.path.join(work, "built_a.png"), built[:half], 900, 110)
        A.spectrogram_png(os.path.join(work, "built_b.png"), built[half:], 900, 110)
