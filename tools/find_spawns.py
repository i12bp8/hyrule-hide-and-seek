#!/usr/bin/env python3
"""List Link spawn points in stages, read from your own extracted game files.

Warping to a spawn point that doesn't exist crashes the game, so every map in src/maps.cpp must use
a point this tool lists. Points with start mode 0 (standing) and no event (0xFF) are the safe ones.

    python tools/find_spawns.py /path/to/disc/files F_SP103 F_SP109 ...
    python tools/find_spawns.py /path/to/disc/files --all-safe F_SP103

The disc path is the folder that contains res/Stage (an extracted GZ2E01 disc). Nothing is written.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def yaz0(data: bytes) -> bytes:
    if data[:4] != b"Yaz0":
        return data
    size = struct.unpack_from(">I", data, 4)[0]
    out = bytearray()
    src = 16
    while len(out) < size:
        code = data[src]
        src += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> bit):
                out.append(data[src])
                src += 1
            else:
                b1, b2 = data[src], data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                count = b1 >> 4
                if count == 0:
                    count = data[src] + 0x12
                    src += 1
                else:
                    count += 2
                for _ in range(count):
                    out.append(out[-dist])
    return bytes(out)


def rarc_files(data: bytes) -> dict[str, bytes]:
    data = yaz0(data)
    assert data[:4] == b"RARC", "not a RARC archive"
    data_off = struct.unpack_from(">I", data, 0x0C)[0] + 0x20
    info = 0x20
    node_count, node_off, _entries, entry_off, _strsize, str_off = struct.unpack_from(">IIIIII", data, info)
    node_off += info
    entry_off += info
    str_off += info

    def name_at(off: int) -> str:
        end = data.index(b"\0", str_off + off)
        return data[str_off + off : end].decode("shift_jis", "replace")

    files: dict[str, bytes] = {}

    def walk(node: int, prefix: str) -> None:
        _type, name_off, _hash, count, first = struct.unpack_from(">4sIHHI", data, node_off + node * 16)
        for i in range(count):
            e = entry_off + (first + i) * 20
            _fid, _h, attr, noff, doff, dsize = struct.unpack_from(">HHHHII", data, e)
            name = name_at(noff & 0xFFFF)
            flags = attr >> 8
            if name in (".", ".."):
                continue
            if flags & 0x02:  # directory
                walk(doff, f"{prefix}{name}/")
            else:
                files[f"{prefix}{name}"] = data[data_off + doff : data_off + doff + dsize]

    walk(0, "")
    return files


def player_points(buf: bytes):
    count = struct.unpack_from(">I", buf, 0)[0]
    for i in range(count):
        tag, n, off = struct.unpack_from(">4sII", buf, 4 + i * 12)
        if not tag.startswith(b"PLY"):
            continue
        for k in range(n):
            name, prm, x, y, z, _ax, ay, az, _sid = struct.unpack_from(">8sI3f3hH", buf, off + k * 0x20)
            if name.rstrip(b"\0") != b"Link":
                continue
            yield tag.decode(), prm, (x, y, z), ay, az & 0xFF


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc", type=Path, help="extracted disc folder containing res/Stage")
    ap.add_argument("stages", nargs="+")
    ap.add_argument("--all-safe", action="store_true", help="only standing points without an event")
    args = ap.parse_args()

    root = args.disc / "res" / "Stage"
    if not root.exists():
        root = args.disc / "files" / "res" / "Stage"
    if not root.exists():
        sys.exit(f"no res/Stage under {args.disc}")

    for stage in args.stages:
        folder = root / stage
        if not folder.exists():
            print(f"{stage}: not found")
            continue
        for arc in sorted(folder.glob("R*_00.arc")):
            room = int(arc.name[1:3])
            for fname, data in rarc_files(arc.read_bytes()).items():
                if not fname.endswith(".dzr"):
                    continue
                for tag, prm, pos, yaw, point in player_points(data):
                    mode = (prm >> 12) & 0x1F
                    event = prm >> 24
                    safe = mode == 0 and event == 0xFF
                    if args.all_safe and not safe:
                        continue
                    print(f"{stage} room {room:2} point {point:3} mode {mode:2} event {event:3} "
                          f"pos ({pos[0]:9.0f} {pos[1]:8.0f} {pos[2]:9.0f}) yaw {yaw:6} "
                          f"{tag}{'  safe' if safe else ''}")


if __name__ == "__main__":
    main()
