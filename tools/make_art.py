#!/usr/bin/env python3
"""Draws res/icon.png and res/banner.png from scratch (no game art). Needs Pillow.

    python tools/make_art.py [--font /path/to/SerifBlack.ttf]
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SS = 4  # supersampling for smooth edges

SKY_TOP = (18, 22, 48)
SKY_BOTTOM = (70, 38, 88)
TWILIGHT = (255, 170, 60)


def gradient(w: int, h: int) -> Image.Image:
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        row = tuple(int(a + (b - a) * t) for a, b in zip(SKY_TOP, SKY_BOTTOM))
        for x in range(w):
            px[x, y] = row
    return img


def glow(size: tuple[int, int], centre: tuple[float, float], radius: float, colour, strength=160) -> Image.Image:
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = centre
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=colour + (strength,))
    return layer.filter(ImageFilter.GaussianBlur(radius * 0.6))


def pot(d: ImageDraw.ImageDraw, cx: float, base: float, s: float, eyes: bool = True) -> None:
    """A round clay pot standing on `base`, scale `s` (height ~ 2*s)."""
    body = (196, 104, 58)
    shade = (150, 72, 40)
    rim = (222, 136, 84)
    dark = (40, 18, 14)
    d.ellipse([cx - s, base - 1.75 * s, cx + s, base + 0.05 * s], fill=body)
    d.chord([cx - s, base - 1.75 * s, cx + s, base + 0.05 * s], 20, 160, fill=shade)
    d.rectangle([cx - 0.45 * s, base - 2.05 * s, cx + 0.45 * s, base - 1.55 * s], fill=body)
    d.ellipse([cx - 0.62 * s, base - 2.25 * s, cx + 0.62 * s, base - 1.9 * s], fill=rim)
    d.ellipse([cx - 0.46 * s, base - 2.19 * s, cx + 0.46 * s, base - 1.96 * s], fill=dark)
    # A band of pattern round the belly.
    d.arc([cx - 0.95 * s, base - 1.35 * s, cx + 0.95 * s, base - 0.55 * s], 10, 170, fill=rim, width=int(0.08 * s))
    if eyes:
        for ex in (-0.33, 0.33):
            x = cx + ex * s
            y = base - 1.0 * s
            d.ellipse([x - 0.16 * s, y - 0.22 * s, x + 0.16 * s, y + 0.22 * s], fill=(255, 236, 150))
            d.ellipse([x - 0.07 * s + 0.04 * s, y - 0.1 * s, x + 0.07 * s + 0.04 * s, y + 0.1 * s], fill=(30, 20, 10))


def crate(d: ImageDraw.ImageDraw, cx: float, base: float, s: float) -> None:
    wood = (150, 102, 58)
    edge = (104, 68, 36)
    d.rectangle([cx - s, base - 2 * s, cx + s, base], fill=wood, outline=edge, width=int(0.12 * s))
    d.line([cx - s, base - 2 * s, cx + s, base], fill=edge, width=int(0.14 * s))
    d.line([cx - s, base, cx + s, base - 2 * s], fill=edge, width=int(0.14 * s))


def barrel(d: ImageDraw.ImageDraw, cx: float, base: float, s: float) -> None:
    wood = (128, 84, 50)
    band = (70, 70, 80)
    d.rounded_rectangle([cx - 0.8 * s, base - 2.3 * s, cx + 0.8 * s, base], radius=0.35 * s, fill=wood)
    for y in (0.45, 1.15, 1.85):
        d.rectangle([cx - 0.82 * s, base - y * s - 0.08 * s, cx + 0.82 * s, base - y * s + 0.08 * s], fill=band)


def sword(d: ImageDraw.ImageDraw, x: float, y: float, s: float, angle: float) -> None:
    """A simple sword from hilt (x, y) pointing along `angle` (radians)."""
    ca, sa = math.cos(angle), math.sin(angle)

    def p(u, v):
        return (x + u * ca - v * sa, y + u * sa + v * ca)

    blade = [p(0.2 * s, -0.09 * s), p(2.6 * s, -0.07 * s), p(2.85 * s, 0), p(2.6 * s, 0.07 * s), p(0.2 * s, 0.09 * s)]
    d.polygon(blade, fill=(220, 226, 240))
    d.polygon([p(0.12 * s, -0.42 * s), p(0.24 * s, -0.42 * s), p(0.24 * s, 0.42 * s), p(0.12 * s, 0.42 * s)], fill=(90, 70, 170))
    d.polygon([p(-0.45 * s, -0.07 * s), p(0.12 * s, -0.07 * s), p(0.12 * s, 0.07 * s), p(-0.45 * s, 0.07 * s)], fill=(70, 50, 120))


def icon(out: Path) -> None:
    n = 512 * SS
    img = gradient(n, n).convert("RGBA")
    img.alpha_composite(glow((n, n), (n * 0.5, n * 0.62), n * 0.33, TWILIGHT, 120))
    d = ImageDraw.Draw(img)
    d.ellipse([n * 0.2, n * 0.8, n * 0.8, n * 0.9], fill=(10, 8, 20, 160))
    sword(d, n * 0.2, n * 0.27, n * 0.2, math.radians(28))
    pot(d, n * 0.5, n * 0.86, n * 0.28)
    img = img.resize((512, 512), Image.LANCZOS)
    mask = Image.new("L", (512, 512), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, 511, 511], radius=96, fill=255)
    img.putalpha(mask)
    img.save(out)


def banner(out: Path, font_path: str) -> None:
    w, h = 1400 * SS, 400 * SS
    img = gradient(w, h).convert("RGBA")
    img.alpha_composite(glow((w, h), (w * 0.2, h * 0.75), h * 0.55, TWILIGHT, 110))
    d = ImageDraw.Draw(img)
    ground = h * 0.9
    d.rectangle([0, ground, w, h], fill=(22, 16, 34))
    crate(d, w * 0.08, ground, h * 0.13)
    barrel(d, w * 0.31, ground, h * 0.14)
    pot(d, w * 0.2, ground, h * 0.2)
    pot(d, w * 0.39, ground, h * 0.11, eyes=False)
    sword(d, w * 0.03, h * 0.18, h * 0.16, math.radians(18))

    title = ImageFont.truetype(font_path, int(h * 0.22))
    sub = ImageFont.truetype(font_path, int(h * 0.08))
    x = w * 0.46
    for text, font, y, colour in (
        ("Hyrule", title, h * 0.14, (255, 214, 130)),
        ("Hide & Seek", title, h * 0.38, (255, 255, 255)),
        ("Online Prop Hunt  ·  2–16 players", sub, h * 0.7, (205, 190, 240)),
    ):
        d.text((x + h * 0.012, y + h * 0.012), text, font=font, fill=(0, 0, 0, 140))
        d.text((x, y), text, font=font, fill=colour)
    img.resize((1400, 400), Image.LANCZOS).save(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/usr/share/fonts/noto/NotoSerif-Black.ttf")
    args = ap.parse_args()
    res = ROOT / "res"
    res.mkdir(exist_ok=True)
    icon(res / "icon.png")
    banner(res / "banner.png", args.font)
    print("wrote res/icon.png and res/banner.png")


if __name__ == "__main__":
    main()
