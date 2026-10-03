#!/usr/bin/env python3
"""Check every disguise's models, animations and collision against an extracted disc.

    python tools/validate_prop_assets.py /path/to/disc/files

Requires the C++20 compiler used to build the mod. The catalogue is read from the compiled C++
definitions, including carry/citizen helpers, so validation cannot silently skip new disguises.
Nothing is written to the disc; the temporary catalogue exporter is removed after the check.
"""
from __future__ import annotations

import argparse
import os
import shlex
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from find_spawns import yaz0

ROOT = Path(__file__).resolve().parents[1]
FIELDS = (("model", ".bmd"), ("extra", ".bmd"), ("extraBtk", ".btk"),
          ("idle", ".bck"), ("move", ".bck"), ("btk", ".btk"),
          ("btp", ".btp"), ("dzb", (".dzb", ".kcl")))
DUMP_SOURCE = r'''
#include "props.hpp"
#include <iostream>
void resource(hs::PropRes r) {
    std::cout << "\t";
    if (r.name) std::cout << r.name;
    else if (r.index >= 0) std::cout << "#" << r.index;
}
int main() {
    for (int i = 0; i < hs::prop_count(); ++i) {
        const auto& p = hs::prop_info(i);
        std::cout << p.name << "\t" << p.arc << "\t" << (p.animArc ? p.animArc : p.arc);
        for (auto r : {p.model, p.extra, p.extraBtk, p.idle, p.move, p.btk, p.btp, p.solid.dzb}) resource(r);
        std::cout << "\n";
    }
}
'''


def catalogue():
    with tempfile.TemporaryDirectory(prefix="hs-prop-catalogue-") as tmp:
        source = Path(tmp) / "catalogue.cpp"
        executable = Path(tmp) / "catalogue"
        source.write_text(DUMP_SOURCE)
        subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O0",
                        "-I" + str(ROOT / "src"), str(ROOT / "src/props.cpp"),
                        str(ROOT / "src/maps.cpp"), str(source), "-o", str(executable)], check=True)
        return [line.split("\t") for line in subprocess.check_output([str(executable)], text=True).splitlines()]


def resources_by_index(raw: bytes):
    """Match JKRArchive::findIdxResource: indices are entry ordinals, not file IDs."""
    data = yaz0(raw)
    if data[:4] != b"RARC":
        raise ValueError("not a RARC archive")
    header = struct.unpack_from(">I", data, 8)[0]
    count, entries, _, strings = struct.unpack_from(">IIII", data, header + 8)
    entries += header
    strings += header
    result = {}
    for index in range(count):
        _, _, flags_and_name, _, _ = struct.unpack_from(">HHIII", data, entries + index * 20)
        flags = flags_and_name >> 24
        if flags & 2 or not flags & 1:
            continue
        start = strings + (flags_and_name & 0xFFFFFF)
        end = data.index(b"\0", start)
        result[index] = data[start:end].decode("shift_jis", "replace")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disc", type=Path, help="extracted disc folder containing res/Object")
    args = parser.parse_args()
    objects = args.disc / "res/Object"
    if not objects.is_dir():
        objects = args.disc / "files/res/Object"
    if not objects.is_dir():
        sys.exit(f"no res/Object under {args.disc}")
    cache = {}
    errors = []
    checked = 0
    entries = catalogue()
    for row in entries:
        name, archive, animation_arc, *resources = row
        if len(resources) != len(FIELDS):
            sys.exit(f"invalid catalogue row: {name}")
        for (label, extensions), resource in zip(FIELDS, resources):
            if not resource:
                continue
            arc = animation_arc if label in {"idle", "move"} else archive
            if arc not in cache:
                path = objects / f"{arc}.arc"
                if not path.is_file():
                    errors.append(f"{name}: missing exact-case archive {path.name}")
                    cache[arc] = {}
                else:
                    cache[arc] = resources_by_index(path.read_bytes())
            by_index = cache[arc]
            resolved = by_index.get(int(resource[1:])) if resource.startswith("#") else resource
            if resolved not in by_index.values():
                errors.append(f"{name}: {arc}.arc has no exact-case {label} resource {resource}")
            elif not resolved.endswith(extensions):
                errors.append(f"{name}: {arc}.arc {label} {resource} resolves to {resolved}")
            checked += 1
    if errors:
        print("prop asset validation failed:")
        for error in errors:
            print(f"  - {error}")
        raise SystemExit(1)
    print(f"validated {len(entries)} catalogue entries, {checked} resources and {len(cache)} exact-case archives")


if __name__ == "__main__":
    main()
