#!/usr/bin/env python3
"""List authored actor placements from a Twilight Princess room archive.

This is a small companion to find_spawns.py for reviewing believable prop locations:

    python tools/list_stage_actors.py /path/to/disc/files F_SP103 0
    python tools/list_stage_actors.py /path/to/disc/files F_SP103 0 --name tubo

Nothing is written. Layer-specific chunks are included because the active story layer can vary.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from find_spawns import rarc_files


def actor_entries(buf: bytes):
    chunk_count = struct.unpack_from(">I", buf, 0)[0]
    for i in range(chunk_count):
        raw_tag, count, offset = struct.unpack_from(">4sII", buf, 4 + i * 12)
        tag = raw_tag.decode("ascii", "replace")
        if not (tag.startswith("ACT") or tag in {"TGOB", "SCOB"} or tag.startswith("SCO")):
            continue
        stride = 0x24 if tag.startswith("SCO") else 0x20
        for ordinal in range(count):
            entry = offset + ordinal * stride
            name, params, x, y, z, ax, ay, az, set_id = struct.unpack_from(
                ">8sI3f3hH", buf, entry
            )
            scale = struct.unpack_from("3B", buf, entry + 0x20) if stride == 0x24 else None
            yield (
                tag,
                ordinal,
                name.rstrip(b"\0").decode("shift_jis", "replace"),
                params,
                (x, y, z),
                (ax, ay, az),
                set_id,
                scale,
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disc", type=Path, help="extracted disc folder containing res/Stage")
    parser.add_argument("stage", help="stage code, such as F_SP103")
    parser.add_argument("room", type=int, help="room number")
    parser.add_argument("--name", default="", help="case-insensitive actor-name filter")
    args = parser.parse_args()

    root = args.disc / "res" / "Stage"
    if not root.exists():
        root = args.disc / "files" / "res" / "Stage"
    archive = root / args.stage / f"R{args.room:02d}_00.arc"
    if not archive.exists():
        sys.exit(f"not found: {archive}")

    wanted = args.name.casefold()
    for path, data in rarc_files(archive.read_bytes()).items():
        if not path.endswith(".dzr"):
            continue
        for tag, ordinal, name, params, pos, angles, set_id, scale in actor_entries(data):
            if wanted and wanted not in name.casefold():
                continue
            scale_text = "" if scale is None else f" scale={scale[0]},{scale[1]},{scale[2]}"
            print(
                f"{tag}[{ordinal:3}] {name:8} pos=({pos[0]:9.0f},{pos[1]:8.0f},{pos[2]:9.0f}) "
                f"rot=({angles[0]:6},{angles[1]:6},{angles[2]:6}) params={params:08x} "
                f"set={set_id:04x}{scale_text}"
            )


if __name__ == "__main__":
    main()
