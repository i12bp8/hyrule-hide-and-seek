#!/usr/bin/env python3
"""Preview every tunic colour using your own game files.

Runs the mod's real recolour code (tests/recolor_tool.cpp) on Link's body and cap models and writes
one PNG with the tunic, skirt and cap textures for every colour. Needs numpy and Pillow.

    python tests/recolor_preview.py /path/to/extracted/disc out.png

The disc path is the folder with res/Object/Kmdl.arc. Nothing from the game is written anywhere
except the preview image you ask for; keep that image to yourself.
"""

from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from find_spawns import rarc_files  # noqa: E402

COLOURS = ["Green", "Red", "Blue", "Yellow", "Purple", "Orange", "Cyan", "Pink",
           "White", "Black", "Lime", "Teal", "Brown", "Navy", "Magenta", "Gold"]
SHOW = {"al.bmd": ["al_upbody", "al_skirt"], "al_head.bmd": ["al_cap"]}


def rgb565(v: int) -> tuple[int, int, int]:
    return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31


def decode_cmpr(data: bytes, w: int, h: int) -> np.ndarray:
    out = np.zeros((h, w, 4), np.uint8)
    pos = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for sub in range(4):
                c0, c1, bits = struct.unpack_from(">HHI", data, pos)
                pos += 8
                a, b = rgb565(c0), rgb565(c1)
                if c0 > c1:
                    pal = [a + (255,), b + (255,),
                           tuple((2 * x + y) // 3 for x, y in zip(a, b)) + (255,),
                           tuple((x + 2 * y) // 3 for x, y in zip(a, b)) + (255,)]
                else:
                    pal = [a + (255,), b + (255,), tuple((x + y) // 2 for x, y in zip(a, b)) + (255,), (0, 0, 0, 0)]
                bx = tx + (sub % 2) * 4
                by = ty + (sub // 2) * 4
                for py in range(4):
                    for px in range(4):
                        idx = (bits >> (30 - 2 * (py * 4 + px))) & 3
                        if by + py < h and bx + px < w:
                            out[by + py, bx + px] = pal[idx]
    return out


def textures(bmd: bytes) -> dict[str, np.ndarray]:
    sections = struct.unpack_from(">I", bmd, 0x0C)[0]
    off = 0x20
    for _ in range(sections):
        tag, size = struct.unpack_from(">4sI", bmd, off)
        if tag == b"TEX1":
            count, _pad, hdr, names = struct.unpack_from(">HHII", bmd, off + 8)
            ncount = struct.unpack_from(">H", bmd, off + names)[0]
            labels = []
            for i in range(ncount):
                _hash, noff = struct.unpack_from(">HH", bmd, off + names + 4 + i * 4)
                start = off + names + noff
                labels.append(bmd[start:bmd.index(b"\0", start)].decode())
            out = {}
            for i in range(count):
                h = off + hdr + i * 0x20
                fmt, _alpha, w, ht = struct.unpack_from(">BBHH", bmd, h)
                img = h + struct.unpack_from(">I", bmd, h + 0x1C)[0]
                if fmt == 0x0E:
                    out[labels[i]] = decode_cmpr(bmd[img:img + w * ht // 2], w, ht)
            return out
        off += size
    return {}


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disc, out = Path(sys.argv[1]), Path(sys.argv[2])
    kmdl = disc / "res/Object/Kmdl.arc"
    if not kmdl.exists():
        kmdl = disc / "files/res/Object/Kmdl.arc"
    files = {k.split("/")[-1]: v for k, v in rarc_files(kmdl.read_bytes()).items()}
    with tempfile.TemporaryDirectory() as tmp:
        tool = Path(tmp) / "recolor_tool"
        subprocess.run(["c++", "-std=c++20", "-O2", f"-I{ROOT/'tests/shim'}", f"-I{ROOT/'src'}",
                        str(ROOT / "src/recolor.cpp"), str(ROOT / "tests/recolor_tool.cpp"), "-o", str(tool)],
                       check=True)
        rows = []
        for colour in range(16):
            tiles = []
            for model, names in SHOW.items():
                src = Path(tmp) / model
                dst = Path(tmp) / f"{colour}_{model}"
                src.write_bytes(files[model])
                subprocess.run([str(tool), str(src), str(dst), str(colour)], check=True)
                tex = textures(dst.read_bytes())
                for n in names:
                    t = tex[n]
                    img = Image.fromarray(t).resize((t.shape[1] * 128 // t.shape[0], 128), Image.NEAREST)
                    tiles.append(img)
            width = sum(t.width for t in tiles) + 8 * len(tiles)
            row = Image.new("RGBA", (width + 110, 128), (40, 40, 40, 255))
            x = 110
            for t in tiles:
                row.paste(t, (x, 0), t)
                x += t.width + 8
            from PIL import ImageDraw
            ImageDraw.Draw(row).text((8, 56), COLOURS[colour], fill=(255, 255, 255, 255))
            rows.append(row)
    sheet = Image.new("RGBA", (max(r.width for r in rows), sum(r.height + 4 for r in rows)), (20, 20, 20, 255))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height + 4
    sheet.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
