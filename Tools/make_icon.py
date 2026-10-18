"""Application icon: a lime reticle over a chevron shield on a dark gunmetal tile (the HUD accent colour)."""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

N = 1024
ACCENT = (231, 255, 110)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tile():
    img = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    mask = Image.new('L', (N, N), 0)
    ImageDraw.Draw(mask).rounded_rectangle((24, 24, N - 24, N - 24), radius=200, fill=255)
    # Vertical gunmetal gradient.
    grad = Image.new('RGBA', (N, N))
    gd = ImageDraw.Draw(grad)
    for y in range(N):
        t = y / (N - 1)
        c = (int(38 - 20 * t), int(43 - 22 * t), int(48 - 24 * t), 255)
        gd.line((0, y, N, y), fill=c)
    img.paste(grad, (0, 0), mask)
    # Thin bevel along the top edge.
    rim = Image.new('L', (N, N), 0)
    ImageDraw.Draw(rim).rounded_rectangle((24, 24, N - 24, N - 24), radius=200, outline=255, width=10)
    img.paste((255, 255, 255, 40), (0, 0), rim)
    return img


def shield(draw, cx, cy, w, h, width, color):
    top = cy - h * 0.5
    pts = [
        (cx - w * 0.5, top),
        (cx, top + h * 0.12),
        (cx + w * 0.5, top),
        (cx + w * 0.5, top + h * 0.48),
        (cx, top + h),
        (cx - w * 0.5, top + h * 0.48),
    ]
    draw.line(pts + [pts[0], pts[1]], fill=color, width=width, joint='curve')


def main():
    img = tile()
    glow = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    art = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    for layer, extra in ((glow, 18), (art, 0)):
        d = ImageDraw.Draw(layer)
        col = ACCENT + (255,)
        cx, cy = N / 2, N / 2 + 10
        shield(d, cx, cy, 600, 680, 46 + extra, col)
        # Reticle: ring, four ticks with a gap, centre dot.
        r = 150
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=col, width=30 + extra)
        for a in range(4):
            ang = a * math.pi / 2
            dx, dy = math.cos(ang), math.sin(ang)
            d.line((cx + dx * 60, cy + dy * 60, cx + dx * 215, cy + dy * 215), fill=col, width=30 + extra)
        d.ellipse((cx - 22, cy - 22, cx + 22, cy + 22), fill=col)
    glow = glow.filter(ImageFilter.GaussianBlur(28))
    glow.putalpha(glow.getchannel('A').point(lambda v: int(v * 0.45)))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, art)

    out_dir = os.path.join(ROOT, 'Build', 'Windows')
    os.makedirs(out_dir, exist_ok=True)
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img.save(os.path.join(out_dir, 'Application.ico'), sizes=sizes)
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, 'Saved', 'AutoShots', 'small', 'icon_preview.png'))
    print('icon written')


if __name__ == '__main__':
    main()
