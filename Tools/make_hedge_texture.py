"""Seamless clipped-hedge (boxwood / 月橘) leaf surface for the surface pipeline.

Writes SourceArt/PolyHaven/Textures/hedge_leaves/hedge_leaves_{diff,arm,nor_dx}.jpg (2048 px = 70 cm). Small oval
leaves in many overlapping layers: the deeper ones darker and occluded, near-black gaps between them, the outer
layer partly lighter new growth; every leaf tilted its own way so it catches the light differently.
usage: make_hedge_texture.py [size]
"""
import os
import sys
import numpy as np
from PIL import Image

S = int(sys.argv[1]) if len(sys.argv) > 1 else 2048
OUT = os.path.join(os.path.dirname(__file__), "..", "SourceArt", "PolyHaven", "Textures", "hedge_leaves")
PX_PER_CM = S / 70.0
rng = np.random.default_rng(20261015)

height = np.full((S, S), -1.0, np.float32)
albedo = np.zeros((S, S, 3), np.float32)
rough = np.full((S, S), 0.75, np.float32)
depth = np.zeros((S, S), np.float32)

# Interior: shadowed twigs and old leaves seen through the gaps.
gap = np.array([0.012, 0.022, 0.008], np.float32)
albedo[:] = gap

COUNT = int(14500 * (S / 2048) ** 2)
order = np.sort(rng.random(COUNT))
for z in order:
    length = rng.uniform(1.2, 2.0) * PX_PER_CM
    width = length * rng.uniform(0.45, 0.6)
    r = int(np.ceil(length * 0.55)) + 1
    cx, cy = rng.uniform(0, S), rng.uniform(0, S)
    ang = rng.uniform(0, np.pi)
    ys, xs = np.mgrid[-r:r + 1, -r:r + 1].astype(np.float32)
    c, s = np.cos(ang), np.sin(ang)
    u = (xs * c + ys * s) / (length * 0.5)
    v = (-xs * s + ys * c) / (width * 0.5)
    inside = u * u + v * v
    mask = inside < 1.0
    if not mask.any():
        continue
    # Leaf surface: a shallow dome with a midrib crease, tilted as a whole.
    tx, ty = rng.normal(0, 0.55, 2)
    dome = np.sqrt(np.clip(1.0 - inside, 0, 1))
    h = z * 6.0 + dome * 0.9 - 0.25 * np.exp(-(v * v) / 0.02) * dome + (u * tx + v * ty) * 0.8
    # Colour: deep leaves dark and blue-green, the outer ones brighter, some of them yellow-green new growth.
    young = z > 0.82 and rng.random() < 0.22
    base = np.array([0.05, 0.1, 0.03]) if not young else np.array([0.12, 0.19, 0.045])
    base = base * (0.3 + 0.85 * z * z) * rng.uniform(0.8, 1.2)
    hue = rng.normal(0, 0.06, 3)
    col = np.clip(base * (1.0 + hue), 0, 1)
    shade = (0.75 + 0.25 * dome)[..., None] * (1.0 - 0.18 * np.clip(inside - 0.75, 0, 1) / 0.25)[..., None]
    rib = 1.0 + 0.25 * np.exp(-(v * v) / 0.01)[..., None]
    leaf = col[None, None, :] * shade * rib
    yy = (np.arange(-r, r + 1) + int(cy)) % S
    xx = (np.arange(-r, r + 1) + int(cx)) % S
    sub_h = height[np.ix_(yy, xx)]
    top = mask & (h > sub_h)
    if not top.any():
        continue
    sub_h[top] = h[top]
    height[np.ix_(yy, xx)] = sub_h
    sub_a = albedo[np.ix_(yy, xx)]
    sub_a[top] = leaf[top]
    albedo[np.ix_(yy, xx)] = sub_a
    sub_d = depth[np.ix_(yy, xx)]
    sub_d[top] = z
    depth[np.ix_(yy, xx)] = sub_d
    sub_r = rough[np.ix_(yy, xx)]
    sub_r[top] = rng.uniform(0.42, 0.58)
    rough[np.ix_(yy, xx)] = sub_r

# Ambient occlusion: the deeper a visible leaf sits in the mass, the less sky it sees; gaps are dark.
h = np.where(height < -0.5, -1.0, height)
blur = h.copy()
for k in (2, 4, 8, 16):
    blur = 0.5 * blur + 0.125 * (np.roll(blur, k, 0) + np.roll(blur, -k, 0) + np.roll(blur, k, 1) + np.roll(blur, -k, 1))
ao = np.clip(0.55 + (h - blur) * 0.35 + depth * 0.45, 0.15, 1.0)
ao[height < -0.5] = 0.2

# Normal map (DirectX: green down) from the height field.
gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5
strength = 1.6
n = np.stack([-gx * strength, gy * strength, np.ones_like(h)], -1)
n /= np.linalg.norm(n, axis=-1, keepdims=True)

os.makedirs(OUT, exist_ok=True)
srgb = np.clip(albedo, 0, 1) ** (1 / 2.2)
Image.fromarray((srgb * 255).astype(np.uint8)).save(os.path.join(OUT, "hedge_leaves_diff.jpg"), quality=92)
arm = np.stack([ao, rough, np.zeros_like(ao)], -1)
Image.fromarray((np.clip(arm, 0, 1) * 255).astype(np.uint8)).save(os.path.join(OUT, "hedge_leaves_arm.jpg"), quality=92)
Image.fromarray(((n * 0.5 + 0.5) * 255).astype(np.uint8)).save(os.path.join(OUT, "hedge_leaves_nor_dx.jpg"), quality=92)
print("hedge_leaves written:", OUT, "leaves:", COUNT)
