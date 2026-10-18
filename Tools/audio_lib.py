# Small DSP toolkit for building game sounds out of recordings (numpy / scipy, decoding with PyAV).
import os
import numpy as np
import av
from scipy import signal

SR = 48000


def load(path):
    """Decodes any audio file to mono float32 at 48 kHz."""
    c = av.open(path)
    stream = c.streams.audio[0]
    res = av.AudioResampler(format="fltp", layout="mono", rate=SR)
    chunks = []
    for frame in c.decode(stream):
        for f in res.resample(frame):
            chunks.append(f.to_ndarray()[0])
    for f in res.resample(None):
        chunks.append(f.to_ndarray()[0])
    c.close()
    return np.concatenate(chunks).astype(np.float32) if chunks else np.zeros(1, np.float32)


def save_wav(path, x):
    from scipy.io import wavfile
    x = np.clip(x, -1.0, 1.0)
    wavfile.write(path, SR, (x * 32767.0).astype(np.int16))


def seconds(x):
    return len(x) / SR


def cut(x, t0, t1=None):
    a = max(0, int(t0 * SR))
    b = len(x) if t1 is None else min(len(x), int(t1 * SR))
    return x[a:b].copy()


def onset(x, threshold_db=-30.0):
    """First sample above threshold relative to the peak (seconds)."""
    peak = np.max(np.abs(x)) + 1e-9
    idx = np.argmax(np.abs(x) > peak * 10 ** (threshold_db / 20))
    return idx / SR


def fade(x, fade_in=0.002, fade_out=0.05):
    y = x.copy()
    n_in = min(len(y), int(fade_in * SR))
    n_out = min(len(y), int(fade_out * SR))
    if n_in > 0:
        y[:n_in] *= np.linspace(0, 1, n_in) ** 2
    if n_out > 0:
        y[-n_out:] *= np.linspace(1, 0, n_out) ** 2
    return y


def gain_db(x, db):
    return x * (10 ** (db / 20))


def normalize(x, peak_db=-1.0):
    p = np.max(np.abs(x)) + 1e-9
    return x * (10 ** (peak_db / 20) / p)


def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-9)


def _sos(kind, f, order=2):
    return signal.butter(order, f, btype=kind, fs=SR, output="sos")


def lowpass(x, f, order=2):
    return signal.sosfilt(_sos("lowpass", f, order), x).astype(np.float32)


def highpass(x, f, order=2):
    return signal.sosfilt(_sos("highpass", f, order), x).astype(np.float32)


def bandpass(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, hi], btype="bandpass", fs=SR, output="sos"), x).astype(np.float32)


def shelf(x, f, db, low=True):
    """Simple shelf: blend the low (or high) band in by db."""
    band = lowpass(x, f) if low else highpass(x, f)
    return x + band * (10 ** (db / 20) - 1)


def peak_eq(x, f, db, q=1.0):
    bw = f / q
    band = bandpass(x, max(20, f - bw / 2), min(SR / 2 - 100, f + bw / 2))
    return x + band * (10 ** (db / 20) - 1)


def resample_pitch(x, ratio):
    """Plays faster/higher by ratio (tape-style: pitch and length together)."""
    n = max(1, int(len(x) / ratio))
    return signal.resample(x, n).astype(np.float32)


def envelope(x, attack=0.001, release=0.05):
    a = np.exp(-1.0 / (attack * SR))
    r = np.exp(-1.0 / (release * SR))
    out = np.zeros_like(x)
    e = 0.0
    ax = np.abs(x)
    for i in range(len(x)):
        v = ax[i]
        e = a * e + (1 - a) * v if v > e else r * e + (1 - r) * v
        out[i] = e
    return out


def transient_shape(x, attack_db=4.0, sustain_db=-2.0):
    """Boosts the attack against the body (fast vs slow envelope)."""
    fast = envelope(x, 0.0005, 0.02) + 1e-6
    slow = envelope(x, 0.02, 0.15) + 1e-6
    ratio = np.clip(fast / slow, 0.0, 4.0)
    g = 10 ** ((attack_db * np.clip(ratio - 1.0, 0, 1) + sustain_db * np.clip(1.0 - ratio, 0, 1)) / 20)
    return (x * g).astype(np.float32)


def compress(x, threshold_db=-20.0, ratio=4.0, attack=0.002, release=0.15, makeup_db=0.0):
    """Feed-forward compressor on a peak envelope."""
    env = envelope(x, attack, release)
    level = 20 * np.log10(env + 1e-9)
    over = np.maximum(level - threshold_db, 0.0)
    g = 10 ** ((-over * (1.0 - 1.0 / ratio) + makeup_db) / 20)
    return (x * g).astype(np.float32)


def saturate(x, drive=1.5):
    return (np.tanh(x * drive) / np.tanh(drive)).astype(np.float32)


def mix(*layers):
    """Layers as (signal, offset seconds, gain dB)."""
    n = max(int(o * SR) + len(s) for s, o, g in layers)
    out = np.zeros(n, np.float32)
    for s, o, g in layers:
        a = int(o * SR)
        out[a:a + len(s)] += gain_db(s, g)
    return out


def pad_to(x, length_s):
    n = int(length_s * SR)
    return x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x), np.float32)])


def loop_crossfade(x, length_s, xfade=0.25, seed=0):
    """Builds a long steady texture from a short one by overlapping random slices with crossfades."""
    rng = np.random.default_rng(seed)
    xf = int(xfade * SR)
    # The first slice's fade-in is cut off, so the texture starts at full level.
    n = int(length_s * SR) + xf
    out = np.zeros(n + SR, np.float32)
    seg = int(min(len(x) * 0.6, 1.2 * SR))
    w = np.ones(seg, np.float32)
    w[:xf] = np.sin(np.linspace(0, np.pi / 2, xf)) ** 2
    w[-xf:] = np.cos(np.linspace(0, np.pi / 2, xf)) ** 2
    pos = 0
    while pos < n:
        a = rng.integers(0, max(1, len(x) - seg))
        out[pos:pos + seg] += x[a:a + seg] * w
        pos += seg - xf
    return out[xf:n]


def simple_reverb(x, decay=0.8, mix_amt=0.2, pre=0.01, damp=6000.0, seed=3):
    """Exponentially decaying filtered noise convolution: a dense, natural tail."""
    rng = np.random.default_rng(seed)
    n = int(decay * 1.6 * SR)
    t = np.arange(n) / SR
    ir = rng.standard_normal(n).astype(np.float32) * np.exp(-t * 6.9 / decay)
    ir = lowpass(ir, damp)
    ir[: int(pre * SR)] = 0
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
    wet = signal.fftconvolve(x, ir)[: len(x) + n].astype(np.float32)
    dry = np.concatenate([x, np.zeros(len(wet) - len(x), np.float32)])
    return dry * (1 - mix_amt * 0.5) + wet * mix_amt


def envelope_summary(x, step=0.05):
    """dB level per step, as a compact string."""
    hop = int(step * SR)
    vals = []
    for i in range(0, len(x), hop):
        seg = x[i:i + hop]
        vals.append(int(round(20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-9))))
    return vals


def spectrogram_png(path, items, width=900, row_h=150):
    """items: list of (label, signal). Writes a stacked spectrogram + waveform contact sheet."""
    import cv2
    rows = []
    for label, x in items:
        f, t, S = signal.spectrogram(x, SR, nperseg=1024, noverlap=768)
        S = 10 * np.log10(S + 1e-12)
        S = np.clip((S - S.max() + 80) / 80, 0, 1)
        img = (S[::-1, :] * 255).astype(np.uint8)
        img = cv2.resize(img, (width, row_h - 40), interpolation=cv2.INTER_AREA)
        img = cv2.applyColorMap(img, cv2.COLORMAP_INFERNO)
        wave = np.zeros((40, width, 3), np.uint8)
        hop = max(1, len(x) // width)
        for i in range(width):
            seg = x[i * hop:(i + 1) * hop]
            if len(seg):
                h = int(min(1.0, np.max(np.abs(seg))) * 19)
                cv2.line(wave, (i, 20 - h), (i, 20 + h), (200, 200, 200), 1)
        row = np.vstack([wave, img])
        cv2.putText(row, f"{label} ({seconds(x):.2f}s)", (5, 14), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 0), 1, cv2.LINE_AA)
        rows.append(row)
    cv2.imwrite(path, np.vstack(rows))
