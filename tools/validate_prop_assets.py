#!/usr/bin/env python3
"""Validate every prop archive/model name against an extracted game disc.

Twilight Princess resource lookup is case-sensitive on some platforms. A typo can therefore turn
a hider completely invisible even though the same prop works on another machine. This checks both
named resources and numeric model IDs without modifying the disc:

    python tools/validate_prop_assets.py /path/to/disc/files
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

from find_spawns import rarc_files, yaz0


RESOURCE = r'(?:nullptr|"[^"]+")'
NUMBER = r'-?(?:\d+(?:\.\d*)?|\.\d+)f?'
PROP_ENTRY = re.compile(
    rf'\{{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*({RESOURCE})\s*,\s*'
    rf'({RESOURCE})\s*,\s*({RESOURCE})\s*,\s*{NUMBER}\s*,\s*{NUMBER}\s*,\s*'
    rf'{NUMBER}\s*,\s*(-?\d+)',
    re.MULTILINE,
)


def token_value(token: str) -> str | None:
    return None if token == "nullptr" else token[1:-1]


def resources_by_id(data: bytes) -> dict[int, str]:
    """Return the exact path of each file-ID resource in a RARC archive."""

    data = yaz0(data)
    if data[:4] != b"RARC":
        raise ValueError("not a RARC archive")
    info = 0x20
    _nodes, node_off, _entries, entry_off, _strsize, str_off = struct.unpack_from(
        ">IIIIII", data, info
    )
    node_off += info
    entry_off += info
    str_off += info

    def name_at(offset: int) -> str:
        end = data.index(b"\0", str_off + offset)
        return data[str_off + offset : end].decode("shift_jis", "replace")

    found: dict[int, str] = {}

    def walk(node: int, prefix: str) -> None:
        _kind, _name, _hash, count, first = struct.unpack_from(
            ">4sIHHI", data, node_off + node * 16
        )
        for ordinal in range(count):
            entry = entry_off + (first + ordinal) * 20
            file_id, _hash, attr, name_offset, data_offset, _size = struct.unpack_from(
                ">HHHHII", data, entry
            )
            name = name_at(name_offset)
            if name in {".", ".."}:
                continue
            if (attr >> 8) & 0x02:
                walk(data_offset, f"{prefix}{name}/")
            else:
                found[file_id] = f"{prefix}{name}"

    walk(0, "")
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disc", type=Path, help="extracted disc folder containing res/Object")
    args = parser.parse_args()

    object_root = args.disc / "res" / "Object"
    if not object_root.exists():
        object_root = args.disc / "files" / "res" / "Object"
    if not object_root.exists():
        sys.exit(f"no res/Object under {args.disc}")

    source = (Path(__file__).parents[1] / "src" / "props.cpp").read_text()
    start = source.find("constexpr PropInfo kProps[]")
    end = source.find("struct CarryMap", start)
    if start < 0 or end < 0:
        sys.exit("could not find kProps in src/props.cpp")
    entries = PROP_ENTRY.findall(source[start:end])
    if not entries:
        sys.exit("could not parse any prop entries")

    animation_archives = {}
    for row in re.findall(r'\{[^{}]*"(?:Mgeneral|Wgeneral)"\s*\}', source[start:end]):
        names = re.findall(r'"([^"\n]+)"', row)
        animation_archives[names[0]] = names[-1]

    errors: list[str] = []
    cache: dict[str, tuple[set[str], dict[int, str]]] = {}
    for name, archive, bmd_token, idle_token, move_token, index_text in entries:
        archive_path = object_root / f"{archive}.arc"
        if not archive_path.is_file():
            errors.append(f"{name}: archive not found with exact case: {archive_path.name}")
            continue
        if archive not in cache:
            raw = archive_path.read_bytes()
            exact_names = {Path(path).name for path in rarc_files(raw)}
            cache[archive] = exact_names, resources_by_id(raw)
        exact_names, by_id = cache[archive]

        bmd = token_value(bmd_token)
        if bmd is not None:
            if bmd not in exact_names:
                errors.append(f"{name}: {archive}.arc has no exact-case resource {bmd}")
        else:
            index = int(index_text)
            indexed_name = by_id.get(index)
            if indexed_name is None:
                errors.append(f"{name}: {archive}.arc has no resource ID {index}")
            elif not indexed_name.casefold().endswith(".bmd"):
                errors.append(
                    f"{name}: {archive}.arc resource ID {index} is {indexed_name}, not a model"
                )

        animation_arc = animation_archives.get(name, archive)
        animation_names = exact_names if animation_arc == archive else {Path(path).name for path in rarc_files((object_root / f"{animation_arc}.arc").read_bytes())}
        for label, token in (("idle", idle_token), ("move", move_token)):
            resource = token_value(token)
            if resource is not None and resource not in animation_names:
                errors.append(
                    f"{name}: {animation_arc}.arc has no exact-case {label} animation {resource}"
                )

    if errors:
        print("prop asset validation failed:")
        for error in errors:
            print(f"  - {error}")
        raise SystemExit(1)
    print(f"validated {len(entries)} props across {len(cache)} exact-case archives")


if __name__ == "__main__":
    main()
