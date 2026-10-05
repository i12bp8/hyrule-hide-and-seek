#!/usr/bin/env python3
"""Build the Prop Hunt arenas from your own extracted game files.

    python tools/arena_builder.py /path/to/disc/files [--preview work/arena] [--only KEY]

For every map in MAPS below this reads the stage's room table, room collision (KCL + PLC) and
actor layout (DZR/DZS), then:

  * takes the whole map: every room the game keeps loaded with the round's spawn room. Loading
    exits, closed doors, water and void are its only edges (the mod blocks the exits in game);
  * computes the floor that is actually reachable from the spawn;
  * dresses that floor with small themed groups of *native* scenery (real pots, crates, barrels,
    pumpkins, rocks, furniture...). Groups stand against walls or in open ground, never in a
    doorway, an exit, a narrow passage, water or next to the spawn, and stay well apart so the
    map looks lived-in rather than cluttered;
  * collects the stage records that would start an event, message or camera change mid-round so
    the mod can remove them for the round;
  * writes src/arena_data.inc, which src/maps.cpp includes, and optional preview images.

The mod adds/removes these records through Dusklight's StageService only while a round is being
played; nothing on the disc changes and normal story play is untouched. Re-run this after changing
MAPS. Requires numpy (and Pillow for --preview): `uv run --with numpy --with pillow python ...`.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import json
import math
import random
import re
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from find_spawns import rarc_files  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]

# ---------------------------------------------------------------------------------------------
# Map definitions. Coordinates are game units (x, z); the builder finds heights itself.
# spawn: (x, z, yaw degrees, y hint). palette: disguises offered on the map (names from
# src/props.cpp, in cycling order). themes: (group, weight) of scenery groups from GROUPS below.
# density: groups per million square units of reachable floor, up to max_groups.
# ---------------------------------------------------------------------------------------------


@dataclasses.dataclass
class MapSpec:
    key: str
    name: str
    stage: str
    room: int
    layer: int
    spawn: tuple[float, float, float, float]
    palette: list[str]
    themes: list[tuple[str, int]] = dataclasses.field(default_factory=list)
    # The rooms in the arena. Default: the rooms the game loads together with `room`.
    rooms: list[int] | None = None
    density: float = 0.9
    # Wide open maps: random rotation keeps them for rooms of six or more, two hunters sooner.
    large: bool = False
    max_groups: int = 26
    gap: float = 1100.0  # preferred distance between groups; shrinks to 800 if the map is tight
    native_gap: float = 420.0  # distance from the map's own pots, crates, barrels...
    cell: float = 40.0
    treasure_step: float = 300.0
    ymin: float = -1e9
    ymax: float = 1e9
    keep_clear: list[tuple[float, float, float]] = dataclasses.field(default_factory=list)
    # Builder only (players are never fenced in): the map's floor stops at this (x0, z0, x1, z1)
    # where leftover collision of the next area continues past a loading gate.
    clip: tuple[float, float, float, float] | None = None
    # Rooms whose post-story layer differs from the others. The round then lets the game choose
    # (layer -1), and the builder reads each room's own layer.
    room_layers: dict[int, int] = dataclasses.field(default_factory=dict)
    # Water drawn by separate actors (Zora's Domain, rivers) is not in the room collision.
    water_y: float = -1e9

    def layer_of(self, room):
        return self.room_layers.get(room, self.layer)


CITIZENS = [
    "Townsman", "Father", "Boy", "Old Man", "Tall Townsman", "Stout Townsman", "Merchant",
    "Young Man", "Townsman (Blue)", "Father (Green)", "Boy (Red)", "Old Man (Brown)",
    "Tall Townsman (Red)", "Stout Townsman (Blue)", "Merchant (Green)", "Young Man (Blue)",
    "Townswoman", "Lady", "Housewife", "Girl", "Old Woman", "Grandmother", "Young Woman",
    "Townswoman (Red)", "Lady (Blue)", "Housewife (Green)", "Girl (Blue)", "Old Woman (Green)",
    "Grandmother (Red)", "Young Woman (Green)",
]

# Every coat of the animals, so a disguise can match whichever real one stands nearby.
CUCCOS = ["White Cucco", "Black Cucco", "Brown Cucco"]
CATS = ["Black & White Cat", "Calico Cat", "Tabby Cat", "Orange Cat"]
DOGS = ["Tan Dog", "Patched Dog", "Brown Dog", "Black Dog"]

MAPS: list[MapSpec] = [
    MapSpec("ordon_village", "Ordon Village", "F_SP103", 0, 4,
            spawn=(-650, 2300, 180, 200),
            palette=["Pumpkin", "Pot", "Big Pot", "Crate", "Barrel", *CUCCOS, "Hawk Grass",
                     "Pumpkin Leaves", "Small Rock", "Sign", "Nameplate", "Lily Pad", *CATS,
                     "Scarecrow"],
            themes=[("pumpkins", 4), ("farm_store", 3), ("pots", 3), ("crates", 2),
                    ("rocks", 1)]),
    MapSpec("ordon_ranch", "Ordon Ranch", "F_SP00", 0, 2,
            spawn=(-7400, -19300, 90, 15300),
            palette=["Goat", "Crate", "Barrel", "Pot", "Horse Grass", "Small Rock", "Big Rock",
                     "Pumpkin"],
            themes=[("farm_store", 4), ("crates", 3), ("grass", 3), ("rocks", 3),
                    ("pumpkins", 1)], cell=50, density=0.5, large=True),
    MapSpec("kakariko", "Kakariko Village", "F_SP109", 0, 2,
            spawn=(-1200, 2500, 180, 0),
            palette=["Crate", "Barrel", "Pot", "Red Pot", "Big Pot", "Sign", *CUCCOS,
                     "Horse Grass", "Board Target", "Pole Target", "Small Rock", "Big Rock"],
            themes=[("barrels", 4), ("red_pots", 3), ("crates", 3), ("pots", 2), ("rocks", 2)]),
    MapSpec("graveyard", "Kakariko Graveyard", "F_SP111", 0, 2,
            spawn=(13867, 920, 130, 100),
            palette=["Skull", "Gravestone", "Pushable Grave", "Red Pot", "Big Pot", "Small Rock",
                     "Big Rock", "Pot"],
            themes=[("offerings", 4), ("skulls", 3), ("rocks", 2), ("big_pots", 1)]),
    MapSpec("death_mountain", "Death Mountain", "F_SP110", 3, 2,
            spawn=(2800, -3400, 240, -1000),
            palette=["Big Rock", "Small Rock", "Barrel", "Crate", "Pot", "Big Pot", "Red Pot"],
            themes=[("boulders", 4), ("rocks", 3), ("mine_store", 3), ("big_pots", 1)],
            cell=50, density=0.45),
    MapSpec("castle_town", "Castle Town", "F_SP116", 0, -1,
            spawn=(0, -800, 180, 0),
            palette=["Pot", "Crate", "Barrel", "Big Pot", *CATS, *DOGS] + CITIZENS,
            themes=[("market", 4), ("pots", 3), ("crates", 2), ("barrels", 2)],
            room_layers={0: 0, 2: 0, 3: 1, 4: 1}, clip=(-3000, -4100, 3300, 3900)),
    MapSpec("sacred_grove", "Sacred Grove", "F_SP117", 1, 2,
            spawn=(0, 6500, 0, 1700),
            palette=["Skull", "Big Pot", "Small Rock", "Big Rock", "Hawk Grass", "Pot",
                     "Red Pot"],
            themes=[("ruins", 4), ("rocks", 3), ("skulls", 2)]),
    MapSpec("hidden_village", "Hidden Village", "F_SP128", 0, 1,
            spawn=(5400, -4000, 0, 0),
            palette=[*CATS, "Barrel", "Crate", "Pot", "Red Pot", "Bar Desk", "Lantern Post",
                     *CUCCOS],
            themes=[("barrels", 3), ("crates", 3), ("red_pots", 2), ("pots", 2)]),
    MapSpec("bulblin_camp", "Bulblin Camp", "F_SP118", 1, 3,
            spawn=(4500, -3300, 0, 260),
            palette=["Crate", "Barrel", "Skull", "Big Pot", "Red Pot", "Caravan Fence",
                     "Boar Bones", "Big Rock"],
            themes=[("camp_store", 4), ("skulls", 3), ("rocks", 2), ("big_pots", 1)],
            cell=50, density=0.5, large=True),
    MapSpec("telmas_bar", "Telma's Bar", "R_SP116", 5, 4,
            spawn=(3141, 4184, 180, -1150),
            palette=["Big Blue Pot", "Pot", "Red Pot", "Big Pot", "Barrel", "Crate", "Map Table",
                     *CATS],
            themes=[("cellar", 3), ("blue_pots", 2), ("pots", 2)],
            cell=20, ymax=-700, density=4.0, max_groups=7, gap=420, native_gap=220),
    MapSpec("hyrule_castle", "Hyrule Castle Grounds", "D_MN09", 11, 0,
            spawn=(0, 9000, 180, 0),
            palette=["Barrel", "Crate", "Big Barrel", "Caravan Fence", "Lantern Post", "Pot",
                     "Big Pot", "Skull"],
            themes=[("castle_store", 4), ("pots", 2), ("big_pots", 2), ("skulls", 1)],
            cell=50, density=0.5, large=True),
]

# Native record templates: (actor name, parameters, angle.x, angle.z, room bits in params).
# Copied from real placements; carryables have no item (angle.x 0xFFFF) and no switch bits.
TEMPLATES = {
    "Pot": ("carry00", 0x00003FC0, 0xFFFF, 0x1041, True),
    "Big Pot": ("carry01", 0x00003FC0, 0xFFFF, 0x1043, True),
    "Crate": ("carry02", 0x00003FC0, 0xFFFF, 0x1045, True),
    "Barrel": ("carry04", 0x00003FC0, 0xFFFF, 0x1049, True),
    "Skull": ("carry05", 0x00003FC0, 0xFFFF, 0x104B, True),
    "Red Pot": ("carry07", 0x00003FC0, 0xFFFF, 0x104F, True),
    "Big Blue Pot": ("carry09", 0x00003FC0, 0xFFFF, 0x1055, True),
    "Pumpkin": ("Pumpkin", 0xF0FFFFFF, 0x0000, 0x0000, False),
    "Small Rock": ("stone", 0x00FFFF10, 0x0000, 0x0000, False),
    "Big Rock": ("stoneB", 0x00FFFF11, 0x0000, 0x0000, False),
    "Horse Grass": ("Obj_Uma", 0xFFFFFFFF, 0x0000, 0x0000, False),
    "Chair": ("HFtr", 0x00000000, 0x0000, 0x0000, False),
    "Sofa": ("HFtr", 0x00000001, 0x0000, 0x0000, False),
    "Dining Table": ("HFtr", 0x00000002, 0x0000, 0x0000, False),
    "Yeto's Barrel": ("HBarrel", 0x0000FF10, 0x0000, 0x0000, False),
}

# Footprint radius used for spacing and clearance.
FOOTPRINT = {"Pot": 40, "Big Pot": 55, "Crate": 45, "Barrel": 55, "Skull": 35, "Red Pot": 40,
             "Big Blue Pot": 55, "Pumpkin": 45, "Small Rock": 45, "Big Rock": 70,
             "Horse Grass": 40, "Chair": 60, "Sofa": 110, "Dining Table": 100,
             "Yeto's Barrel": 55}
# Flat-bottomed objects need flatter ground than round ones (height change per unit).
SLOPE = {"Crate": 0.1, "Chair": 0.08, "Sofa": 0.08, "Dining Table": 0.06, "Yeto's Barrel": 0.12,
         "Barrel": 0.14}
# These turn their back to the wall they stand against; the rest get a random turn.
FACES_OUT = {"Crate", "Chair", "Sofa"}

# Scenery groups. "wall" groups stand in a row along a wall, fence, cliff or house front; "open"
# groups stand as a loose cluster in open ground with room to walk around on every side.
GROUPS = {
    "pots": ("wall", [["Pot", "Pot"], ["Big Pot", "Pot"], ["Pot", "Pot", "Pot"], ["Pot"]]),
    "red_pots": ("wall", [["Red Pot", "Red Pot"], ["Red Pot", "Big Pot"], ["Red Pot"]]),
    "big_pots": ("wall", [["Big Pot"], ["Big Pot", "Big Pot"], ["Big Pot", "Pot"]]),
    "blue_pots": ("wall", [["Big Blue Pot", "Pot"], ["Big Blue Pot"], ["Pot", "Big Blue Pot", "Pot"]]),
    "crates": ("wall", [["Crate", "Crate"], ["Crate"], ["Crate", "Crate", "Crate"]]),
    "barrels": ("wall", [["Barrel", "Barrel"], ["Barrel"], ["Barrel", "Barrel", "Barrel"]]),
    "farm_store": ("wall", [["Crate", "Barrel"], ["Barrel", "Crate", "Pot"], ["Crate", "Pot"]]),
    "market": ("wall", [["Crate", "Pot", "Pot"], ["Barrel", "Crate"], ["Big Pot", "Pot"],
                        ["Crate", "Crate", "Barrel"]]),
    "mine_store": ("wall", [["Barrel", "Crate"], ["Crate", "Crate"], ["Barrel", "Barrel"]]),
    "camp_store": ("wall", [["Crate", "Barrel"], ["Barrel", "Barrel"], ["Crate", "Crate", "Skull"]]),
    "castle_store": ("wall", [["Barrel", "Crate"], ["Crate", "Crate", "Barrel"], ["Barrel", "Barrel"]]),
    "cellar": ("wall", [["Barrel", "Crate"], ["Crate", "Pot"], ["Barrel"]]),
    "skulls": ("wall", [["Skull"], ["Skull", "Skull"]]),
    "offerings": ("wall", [["Red Pot", "Skull"], ["Skull", "Red Pot", "Pot"], ["Big Pot", "Skull"]]),
    "ruins": ("wall", [["Big Pot", "Skull"], ["Small Rock", "Big Pot"], ["Skull", "Small Rock"]]),
    "rocks": ("wall", [["Big Rock", "Small Rock"], ["Small Rock"], ["Big Rock"]]),
    "boulders": ("open", [["Big Rock", "Small Rock"], ["Big Rock"], ["Small Rock", "Small Rock"]]),
    "pumpkins": ("open", [["Pumpkin", "Pumpkin"], ["Pumpkin", "Pumpkin", "Pumpkin"]]),
    "grass": ("open", [["Horse Grass"]]),
    "yeto": ("wall", [["Yeto's Barrel", "Yeto's Barrel"], ["Yeto's Barrel"]]),
    "parlour": ("wall", [["Chair"], ["Sofa"], ["Chair", "Chair"]]),
    "dining": ("open", [["Dining Table"]]),
}

# Records that would start a cutscene, message, hint or camera change during a round.
REMOVED_NAMES = ("TagEv", "TagEvt", "TagEvC", "EvtArea", "KMsg", "Mhint", "Mmsg", "TGSPITM",
                 "TGSPCAM", "Tag_ms", "TagStat", "TagSch", "CamArea", "CamAreC", "CamChg")
# Doors and gates stay shut during a round (nothing answers the A button), so they are walls.
DOOR_NAMES = ("door", "kdoor", "ndoor", "tadoor", "l9door", "pdoor", "smgdoor", "thdoor",
              "BkDoorL", "BkDoorR", "R_Gate", "CrvGate", "SkDoor", "L5Bdoor", "L5Mdoor", "L5door",
              "pdrobj", "L4Gate")
# Invisible helpers, tags and small decoration: no clearance needed around them.
INVISIBLE = ("SwArea", "AND_SW", "ClearB", "Savmem", "scnChg", "mmvbg", "Digpl", "Drop", "ky_tag",
             "kytag", "Wljump", "noChgRm", "Hstop", "Grass", "flwr", "flower", "pflwr", "item",
             "atkItem", "Stream", "Fish", "Worm", "readRm", "Tag", "Sw", "Event", "Evt", "Cam",
             "Alink", "Nsw", "swBall", "sound", "Snd", "Vrbox", "Mirror", "kytg", "Kytag", "LTag",
             "ArrowP", "Sq", "Rock")
# Real hiding objects already on the map: groups keep away from them so nothing piles up.
SCENERY_LIKE = ("carry", "Pumpkin", "stone", "Obj_Uma", "HFtr", "HBarrel", "Obj_kn2", "kkri",
                "Kakashi", "J_Tobi")

# ---------------------------------------------------------------------------------------------
# Disc parsing
# ---------------------------------------------------------------------------------------------


def parse_kcl(buf, plc):
    pos_off, nrm_off, prism_off, block_off = struct.unpack_from(">4I", buf, 0)
    n_pos = (nrm_off - pos_off) // 12
    n_nrm = (prism_off + 0x10 - nrm_off) // 12
    pos = np.frombuffer(buf, dtype=">f4", count=n_pos * 3, offset=pos_off).reshape(-1, 3).astype(np.float64)
    nrm = np.frombuffer(buf, dtype=">f4", count=n_nrm * 3, offset=nrm_off).reshape(-1, 3).astype(np.float64)
    dt = np.dtype([("h", ">f4"), ("p", ">u2"), ("f", ">u2"), ("e1", ">u2"), ("e2", ">u2"), ("e3", ">u2"), ("a", ">u2")])
    pr = np.frombuffer(buf, dtype=dt, count=(block_off - prism_off) // 0x10, offset=prism_off)[1:]
    h = pr["h"].astype(np.float64)
    a = pos[pr["p"].astype(int)]
    fn = nrm[pr["f"].astype(int)]
    e1, e2, e3 = (nrm[pr[k].astype(int)] for k in ("e1", "e2", "e3"))
    ca, cb = np.cross(e1, fn), np.cross(e2, fn)
    with np.errstate(divide="ignore", invalid="ignore"):
        b = a + cb * (h / np.einsum("ij,ij->i", cb, e3))[:, None]
        c = a + ca * (h / np.einsum("ij,ij->i", ca, e3))[:, None]
    num = struct.unpack_from(">H", plc, 6)[0]
    codes = np.frombuffer(plc, dtype=">u4", count=num * 5, offset=8).reshape(-1, 5).astype(np.int64)
    code = codes[np.clip(pr["a"].astype(int), 0, len(codes) - 1)]
    ok = np.isfinite(a).all(1) & np.isfinite(b).all(1) & np.isfinite(c).all(1)
    flags = dict(exit=code[:, 0] & 0x3F, wtr=(code[:, 4] >> 8) & 1, ground=(code[:, 1] >> 19) & 0x1F,
                 link_through=(code[:, 0] >> 16) & 1)
    return a[ok], b[ok], c[ok], fn[ok], {k: v[ok] for k, v in flags.items()}


def chunks(buf):
    for i in range(struct.unpack_from(">I", buf, 0)[0]):
        tag, cnt, off = struct.unpack_from(">4sII", buf, 4 + i * 12)
        yield tag.decode("ascii", "replace"), cnt, off


def layer_char(layer):
    return str(layer) if layer < 10 else "abcde"[layer - 10]


def actors(buf, layer):
    """Actors loaded for this layer. Scaled records (SCOB/SCOn/doors) are matched by StageService
    with a CRC over their 0x23 meaningful bytes; plain records over 0x20."""
    tag_char = layer_char(layer)
    for tag, cnt, off in chunks(buf):
        if tag in ("ACTR", "TGOB", "ACT" + tag_char):
            stride, size = 0x20, 0x20
        elif tag in ("SCOB", "TGSC", "SCO" + tag_char, "Door", "Doo" + tag_char):
            stride, size = 0x24, 0x23
        else:
            continue
        for k in range(cnt):
            raw = buf[off + k * stride: off + k * stride + size]
            name, prm, x, y, z, ax, ay, az, sid = struct.unpack(">8sI3f3hH", raw[:0x20])
            scale = tuple(v * 0.1 for v in raw[0x20:0x23]) if size != 0x20 else (1.0, 1.0, 1.0)
            yield dict(name=name.rstrip(b"\0").decode("latin1"), prm=prm, pos=(x, y, z),
                       ang=(ax, ay, az), crc=zlib.crc32(raw), scaled=size != 0x20, scale=scale)


def link_points(buf):
    for tag, cnt, off in chunks(buf):
        if not tag.startswith("PLY"):
            continue
        for k in range(cnt):
            name, prm, x, y, z, ax, ay, az, sid = struct.unpack_from(">8sI3f3hH", buf, off + k * 0x20)
            if name.rstrip(b"\0") == b"Link":
                yield dict(prm=prm, pos=(x, y, z), yaw=ay, point=az & 0xFF)


def room_table(stage_dzs):
    """RTBL: for each room, the rooms the game keeps loaded while Link stands in it."""
    out = {}
    for tag, cnt, off in chunks(stage_dzs):
        if tag != "RTBL":
            continue
        for room in range(cnt):
            entry = struct.unpack_from(">I", stage_dzs, off + room * 4)[0]
            num = stage_dzs[entry]
            rooms_off = struct.unpack_from(">I", stage_dzs, entry + 4)[0]
            out[room] = [b & 0x3F for b in stage_dzs[rooms_off:rooms_off + num]]
    return out

# ---------------------------------------------------------------------------------------------
# Walkable floor analysis
# ---------------------------------------------------------------------------------------------


class Floor:
    """Grid of floor levels with wall spans, for reachability and clearance checks."""

    def __init__(self, rooms, box, cell, ymin, ymax):
        self.cell = cell
        self.x0, self.z0, x1, z1 = box
        self.w = int((x1 - self.x0) / cell) + 1
        self.h = int((z1 - self.z0) / cell) + 1
        self.levels = collections.defaultdict(list)   # (i, j) -> [y]
        self.owner = collections.defaultdict(list)    # (i, j) -> [(y, room)]
        self.walls = collections.defaultdict(list)    # (i, j) -> [(ylo, yhi)]
        self.hazard = collections.defaultdict(list)   # water/void floors: (i, j) -> [y]
        self.exits = collections.defaultdict(list)    # loading floors: (i, j) -> [y]
        self.ceilings = collections.defaultdict(list) # downward faces: (i, j) -> [y]
        for room, (a, b, c, n, f) in rooms.items():
            for t in range(len(a)):
                ny = n[t][1]
                ys = (a[t][1], b[t][1], c[t][1])
                if max(ys) < ymin or min(ys) > ymax:
                    continue
                if ny > 0.7 and not f["link_through"][t]:
                    if f["exit"][t] != 0x3F:
                        self._fill(a[t], b[t], c[t], self.exits)
                    elif f["wtr"][t] or f["ground"][t] in (4, 9, 10):
                        self._fill(a[t], b[t], c[t], self.hazard)
                    else:
                        self._fill(a[t], b[t], c[t], self.levels, room)
                elif abs(ny) < 0.6:
                    self._wall(a[t], b[t], c[t])
                elif ny < -0.7:
                    self._fill(a[t], b[t], c[t], self.ceilings)
        for key in self.levels:
            self.levels[key] = self._merge(self.levels[key])
        # No headroom, no floor: the ground under a raised street or slab is not somewhere to
        # stand, even though the collision has no wall around it.
        # A ledge or kerb inside one cell is not a ceiling; the cover must span the neighbours.
        def covered(key, y):
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    k = (key[0] + di, key[1] + dj)
                    if not any(25 < h - y < 160 for h in self.levels.get(k, []) + self.ceilings.get(k, [])):
                        return False
            return True
        self.levels = collections.defaultdict(list, {
            key: [y for y in ys if not covered(key, y)] for key, ys in self.levels.items()})

    def ij(self, x, z):
        return int(math.floor((x - self.x0) / self.cell)), int(math.floor((z - self.z0) / self.cell))

    def xz(self, i, j):
        return self.x0 + (i + 0.5) * self.cell, self.z0 + (j + 0.5) * self.cell

    def _fill(self, a, b, c, out, room=None):
        xs, zs = (a[0], b[0], c[0]), (a[2], b[2], c[2])
        i0, j0 = self.ij(min(xs), min(zs))
        i1, j1 = self.ij(max(xs), max(zs))
        i0, j0, i1, j1 = max(i0, 0), max(j0, 0), min(i1, self.w - 1), min(j1, self.h - 1)
        if i1 < i0 or j1 < j0:
            return
        ii, jj = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1), indexing="ij")
        px = self.x0 + (ii + 0.5) * self.cell
        pz = self.z0 + (jj + 0.5) * self.cell
        d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if abs(d) < 1e-6:
            return
        l1 = ((b[2] - c[2]) * (px - c[0]) + (c[0] - b[0]) * (pz - c[2])) / d
        l2 = ((c[2] - a[2]) * (px - c[0]) + (a[0] - c[0]) * (pz - c[2])) / d
        l3 = 1 - l1 - l2
        inside = (l1 >= -1e-4) & (l2 >= -1e-4) & (l3 >= -1e-4)
        y = l1 * a[1] + l2 * b[1] + l3 * c[1]
        for i, j, yy in zip(ii[inside], jj[inside], y[inside]):
            out[(int(i), int(j))].append(float(yy))
            if room is not None:
                self.owner[(int(i), int(j))].append((float(yy), room))

    def _wall(self, a, b, c):
        lo, hi = min(a[1], b[1], c[1]), max(a[1], b[1], c[1])
        step = self.cell * 0.5
        pts = []
        for p, q in ((a, b), (b, c), (c, a)):
            n = max(1, int(math.dist((p[0], p[2]), (q[0], q[2])) / step))
            pts += [(p[0] + (q[0] - p[0]) * k / n, p[2] + (q[2] - p[2]) * k / n) for k in range(n + 1)]
        for x, z in pts:
            i, j = self.ij(x, z)
            if 0 <= i < self.w and 0 <= j < self.h:
                self.walls[(i, j)].append((lo, hi))

    @staticmethod
    def _merge(ys):
        ys = sorted(ys)
        out = []
        for y in ys:
            if out and y - out[-1] < 30:
                out[-1] = max(out[-1], y)
            else:
                out.append(y)
        return out

    def blocked(self, key, y):
        # A wall blocks Link only where it rises above step height; curbs and stairs do not.
        for lo, hi in self.walls.get(key, ()):
            if lo < y + 150 and hi > y + 55:
                return True
        return False

    def wet(self, key, y):
        # Water or void at or above this level makes the spot useless.
        return any(abs(h - y) < 40 or h > y for h in self.hazard.get(key, ()))

    def level_near(self, key, y):
        best = None
        for level in self.levels.get(key, ()):
            if best is None or abs(level - y) < abs(best - y):
                best = level
        return best

    def room_at(self, key, y, default):
        best = None
        for level, room in self.owner.get(key, ()):
            if best is None or abs(level - y) < abs(best[0] - y):
                best = (level, room)
        return default if best is None else best[1]

    def reach(self, starts, closed):
        """Floor connected to any of `starts` (x, z, y): the round spawn plus the game's own
        standing points, so every area the game lets Link stand in counts even where the walk
        between them (a ford, a ledge to climb) is beyond this grid's simple step rule."""
        seen = set()
        queue = collections.deque()
        for sx, sz, sy in starts:
            key = self.ij(sx, sz)
            y = self.level_near(key, sy)
            if y is None or abs(y - sy) > 60 or (key, round(y)) in seen or closed(sx, sz, y):
                continue
            seen.add((key, round(y)))
            queue.append((key, y))
        if not queue:
            raise RuntimeError(f"no floor at spawn {starts[0]}")
        out = {}
        while queue:
            k, y = queue.popleft()
            out.setdefault(k, []).append(y)
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    if di == dj == 0:
                        continue
                    nk = (k[0] + di, k[1] + dj)
                    for ny in self.levels.get(nk, ()):
                        if abs(ny - y) > 45 or (nk, round(ny)) in seen or self.blocked(nk, ny):
                            continue
                        # No corner cutting between the cells of a diagonal wall.
                        if di and dj and (self.blocked((k[0] + di, k[1]), ny) or self.blocked((k[0], k[1] + dj), ny)):
                            continue
                        if closed(*self.xz(*nk), ny):
                            continue
                        seen.add((nk, round(ny)))
                        queue.append((nk, ny))
        return out


class Closed:
    """Doors and exit volumes: walls for the round."""

    def __init__(self, doors, exits):
        self.doors = doors    # (x, y, z, radius)
        self.exits = exits    # (x, y, z, half_x, height, half_z, sin, cos)

    def __call__(self, x, z, y, margin=0.0, door_margin=None):
        door_margin = margin if door_margin is None else door_margin
        for dx, dy, dz, r in self.doors:
            if abs(y - dy) < 400 and (x - dx) ** 2 + (z - dz) ** 2 < (r + door_margin) ** 2:
                return True
        # Exit volumes span whole roads; treat them as full-height walls so the walk never
        # slips over or under one into the next area's leftover collision.
        for ex, ey, ez, hx, height, hz, sn, cs in self.exits:
            px, pz = x - ex, z - ez
            lx = px * cs - pz * sn
            lz = px * sn + pz * cs
            if abs(lx) <= hx + 40 + margin and abs(lz) <= hz + 40 + margin:
                return True
        return False

# ---------------------------------------------------------------------------------------------
# Scenery planning
# ---------------------------------------------------------------------------------------------


class Planner:
    def __init__(self, spec, floor, reach, natives, closed, points, rng):
        self.spec, self.floor, self.reach, self.closed, self.rng = spec, floor, reach, closed, rng
        self.cell = floor.cell
        self.placed = []
        self.failures = collections.Counter()
        self.groups = []
        self.spot_failures = collections.Counter()
        # Everything a new object must keep clear of: (x, z, radius).
        self.occupied = []
        self.scenery_like = []
        for n in natives:
            x, _, z = n["pos"]
            name = n["name"]
            if name.startswith(INVISIBLE):
                continue
            self.occupied.append((x, z, 140))
            if name.startswith(SCENERY_LIKE):
                self.scenery_like.append((x, z))
        self.avoid = [(x, z, r) for x, z, r in spec.keep_clear]
        self.avoid += [(p["pos"][0], p["pos"][2], 260) for p in points]
        self.avoid.append((spec.spawn[0], spec.spawn[1], 650))

    def level(self, x, z, y, tolerance=12):
        key = self.floor.ij(x, z)
        ys = self.reach.get(key)
        if not ys:
            return None
        best = min(ys, key=lambda v: abs(v - y))
        return best if abs(best - y) <= tolerance else None

    def walkable(self, x, z, y):
        key = self.floor.ij(x, z)
        ys = self.reach.get(key)
        return bool(ys) and any(abs(v - y) <= 45 for v in ys)

    def near_wall(self, x, z, y, radius):
        """Unit vector from the closest wall cell within `radius` to (x, z), and its distance."""
        cell = self.cell
        ci, cj = self.floor.ij(x, z)
        r = int(radius / cell) + 1
        best, vx, vz, count = None, 0.0, 0.0, 0
        for di in range(-r, r + 1):
            for dj in range(-r, r + 1):
                key = (ci + di, cj + dj)
                if not self.floor.blocked(key, y):
                    continue
                wx, wz = self.floor.xz(*key)
                d = math.hypot(x - wx, z - wz)
                if d > radius:
                    continue
                best = d if best is None else min(best, d)
                if d > 1e-3:
                    vx += (x - wx) / d / max(d, cell)
                    vz += (z - wz) / d / max(d, cell)
                    count += 1
        if best is None or count == 0:
            return None, None
        length = math.hypot(vx, vz)
        if length < 1e-6:
            return None, None
        return best, (vx / length, vz / length)

    def why(self, reason):
        self.spot_failures[reason] += 1
        return False

    def clear_spot(self, x, z, y, radius, slope=0.2):
        """The object's own footprint: level reachable floor (a gentle slope at most), dry, no
        wall, nothing on it."""
        floor = self.floor
        if y < self.spec.water_y:
            return self.why("water")
        r = radius + 10
        steps = int(r / (self.cell * 0.5)) + 1
        for a in range(-steps, steps + 1):
            for b in range(-steps, steps + 1):
                px, pz = x + a * self.cell * 0.5, z + b * self.cell * 0.5
                d = math.hypot(px - x, pz - z)
                if d > r:
                    continue
                key = floor.ij(px, pz)
                if floor.blocked(key, y):
                    # Wall cells are coarse; only the body itself must be free of them.
                    if d <= radius - 5:
                        return self.why("in wall")
                    continue
                if self.level(px, pz, y, 4 + slope * d) is None or floor.wet(key, y):
                    return self.why("uneven")
        # Water, void and loading floors keep a margin; doors and exits a large one.
        for a in range(-4, 5):
            for b in range(-4, 5):
                key = floor.ij(x + a * self.cell, z + b * self.cell)
                if floor.wet(key, y):
                    return self.why("water")
        if any(any(abs(h - y) < 300 for h in floor.exits.get(floor.ij(x + a * 80, z + b * 80), ()))
               for a in range(-5, 6) for b in range(-5, 6)):
            return self.why("exit floor")
        if self.closed(x, z, y, margin=300, door_margin=190):
            return self.why("door")
        for ax, az, ar in self.avoid:
            if math.hypot(x - ax, z - az) < ar + radius:
                return self.why("avoid")
        for ox, oz, orad in self.occupied:
            if math.hypot(x - ox, z - oz) < radius + orad:
                return self.why("occupied")
        return True

    def open_run(self, x, z, y, dx, dz, start, length):
        """Floor stays walkable from `start` to `start + length` along (dx, dz)."""
        d = start
        while d <= start + length:
            px, pz = x + dx * d, z + dz * d
            if not self.walkable(px, pz, y) or self.floor.blocked(self.floor.ij(px, pz), y):
                return False
            d += self.cell * 0.5
        return True

    def fail(self, reason):
        self.failures[reason] += 1
        return None

    def layout_wall(self, x, y, z, kinds):
        # Right against the wall first; a kerb or plinth can push the row a little further out.
        for extra in (0, 30, 60):
            items = self.layout_wall_at(x, y, z, kinds, extra)
            if items is not None:
                return items
        return None

    def layout_wall_at(self, x, y, z, kinds, extra):
        dist, normal = self.near_wall(x, z, y, 160)
        if normal is None:
            return self.fail("no wall")
        nx, nz = normal
        tx, tz = -nz, nx
        wx, wz = x - nx * dist, z - nz * dist
        widths = [FOOTPRINT[k] for k in kinds]
        total = sum(2 * w for w in widths) + 18 * (len(kinds) - 1)
        along = -total / 2
        items = []
        for kind, r in zip(kinds, widths):
            along += r
            off = r + 34 + extra + self.rng.random() * 10
            px, pz = wx + nx * off + tx * along, wz + nz * off + tz * along
            along += r + 18
            py = self.level(px, pz, y, 30)
            if py is None:
                return self.fail("wall: level")
            if not self.clear_spot(px, pz, py, r, SLOPE.get(kind, 0.2)):
                return self.fail("wall: spot")
            # The object must sit against the wall, not float in front of a gap in it.
            back, _ = self.near_wall(px, pz, py, r + 70 + extra)
            if back is None:
                return self.fail("wall: gap behind")
            # Never narrow a passage: keep a broad walkway in front of every object.
            if not self.open_run(px, pz, py, nx, nz, r + 20, 300):
                return self.fail("wall: walkway")
            yaw = math.degrees(math.atan2(nx, nz)) + (self.rng.random() - 0.5) * 16
            if kind not in FACES_OUT:
                yaw = self.rng.random() * 360
            items.append(dict(kind=kind, x=px, y=py, z=pz, yaw=int(yaw / 360 * 65536) & 0xFFFF))
        return items

    def layout_open(self, x, y, z, kinds):
        items = []
        angle = self.rng.random() * math.tau
        for index, kind in enumerate(kinds):
            r = FOOTPRINT[kind]
            if index == 0:
                px, pz = x, z
            else:
                angle += math.tau / max(3, len(kinds)) + (self.rng.random() - 0.5) * 0.8
                d = r + FOOTPRINT[kinds[0]] + 25 + self.rng.random() * 30
                px, pz = x + math.cos(angle) * d, z + math.sin(angle) * d
            py = self.level(px, pz, y, 30)
            if py is None or not self.clear_spot(px, pz, py, r, SLOPE.get(kind, 0.2)):
                return self.fail("open: spot")
            if any(math.hypot(px - o["x"], pz - o["z"]) < r + FOOTPRINT[o["kind"]] + 15 for o in items):
                return None
            items.append(dict(kind=kind, x=px, y=py, z=pz, yaw=self.rng.randrange(0, 65536)))
        # Open ground all around the cluster, so it never stands in a path.
        cx = sum(o["x"] for o in items) / len(items)
        cz = sum(o["z"] for o in items) / len(items)
        spread = max(math.hypot(o["x"] - cx, o["z"] - cz) + FOOTPRINT[o["kind"]] for o in items)
        for k in range(12):
            a = k * math.tau / 12
            if not self.open_run(cx, cz, y, math.cos(a), math.sin(a), spread + 10, 280):
                return self.fail("open: crowded")
        return items

    def plan(self):
        spec = self.spec
        if not spec.themes:
            return []
        area = len(self.reach) * self.cell * self.cell / 1e6
        target = max(4, min(spec.max_groups, round(area * spec.density)))
        candidates = {"wall": [], "open": []}
        for key, ys in self.reach.items():
            if key[0] % 2 or key[1] % 2:
                continue
            for y in ys:
                x, z = self.floor.xz(*key)
                if self.floor.wet(key, y) or y < spec.water_y:
                    continue
                dist, _ = self.near_wall(x, z, y, 150)
                kind = "wall" if dist is not None and dist <= 110 else "open" if dist is None else None
                if kind:
                    candidates[kind].append((x, y, z))
        for lst in candidates.values():
            self.rng.shuffle(lst)
        bag = [name for name, weight in spec.themes for _ in range(weight)]
        self.rng.shuffle(bag)
        anchors = []
        gap = spec.gap
        attempts = 0
        while len(anchors) < target and gap >= min(spec.gap, 800) * 0.999:
            progress = False
            for theme in list(bag):
                if len(anchors) >= target:
                    break
                placement, layouts = GROUPS[theme]
                for x, y, z in candidates[placement]:
                    attempts += 1
                    if any(math.hypot(x - ax, z - az) < gap for ax, az in anchors):
                        continue
                    if any(math.hypot(x - sx, z - sz) < spec.native_gap for sx, sz in self.scenery_like):
                        continue
                    # Prefer the fuller layouts; a tight spot gets a shorter row.
                    kinds = self.rng.choices(layouts, weights=[len(k) for k in layouts])[0]
                    layout = self.layout_wall if placement == "wall" else self.layout_open
                    items = None
                    for count in range(len(kinds), 0, -1):
                        items = layout(x, y, z, kinds[:count])
                        if items is not None:
                            break
                    if items is None:
                        continue
                    self.groups += [items] * len(items)
                    for o in items:
                        o["theme"] = theme
                        self.placed.append(o)
                        self.occupied.append((o["x"], o["z"], FOOTPRINT[o["kind"]] + 20))
                    anchors.append((x, z))
                    progress = True
                    break
            if not progress or len(anchors) < target:
                gap *= 0.9
        sizes = collections.Counter(collections.Counter(id(g) for g in self.groups).values())
        print(f"  {spec.key}: {len(anchors)}/{target} groups {dict(sorted(sizes.items()))}, {len(self.placed)} objects "
              f"(gap {gap:.0f}) {dict(self.failures.most_common(6))} {dict(self.spot_failures)}")
        return self.placed

# ---------------------------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------------------------


def cpp_ident(text):
    return "".join(ch if ch.isalnum() else "_" for ch in text)


PROP_IDS = {}


def load_prop_ids():
    source = (ROOT / "src" / "props.hpp").read_text()
    body = source[source.index("enum PropId"):source.index("kPropCount")]
    names = (ROOT / "src" / "props.cpp").read_text()
    # The catalogue initialisers are in PropId order; read their display names.
    start = names.index("constexpr PropInfo kProps[kPropCount] = {")
    entries = re.findall(r'(?:carry|citizen|cucco|cat|dog)\("([^"]+)"|\.name = "([^"]+)"', names[start:])
    for index, (a, b) in enumerate(entries):
        PROP_IDS[a or b] = index
    assert "Pot" in PROP_IDS and "Treasure Rupee" in PROP_IDS, body


def emit(results):
    out = ["// Generated by tools/arena_builder.py from the game disc. Do not edit by hand.",
           "// Re-run the builder after changing its MAPS definitions.", ""]
    for spec, data in results:
        ident = cpp_ident(spec.key)
        out.append(f"constexpr uint8_t kPalette_{ident}[] = {{")
        out.append("    " + ", ".join(str(PROP_IDS[p]) for p in spec.palette) + ",")
        out.append("};")
        out.append(f"constexpr Scenery kScenery_{ident}[] = {{")
        for s in data["scenery"]:
            name, prm, ax, az, room_bits = TEMPLATES[s["kind"]]
            room = s["room"]
            if room_bits:
                prm = (prm & ~0x3F) | (room & 0x3F)
            out.append(f'    {{"{name}", 0x{prm:08X}u, {s["x"]:.0f}.0f, {s["y"] + 1:.0f}.0f, {s["z"]:.0f}.0f, '
                       f'{ax if ax < 0x8000 else ax - 0x10000}, {s["yaw"] if s["yaw"] < 0x8000 else s["yaw"] - 0x10000}, '
                       f'{az}, {room}}},  // {s["kind"]}')
        if not data["scenery"]:
            out.append('    {"", 0u, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},')
        out.append("};")
        out.append(f"constexpr uint32_t kRemoved_{ident}[] = {{")
        crcs = sorted(set(data["removed"]))
        for i in range(0, len(crcs), 6):
            out.append("    " + " ".join(f"0x{c:08X}u," for c in crcs[i:i + 6]))
        if not crcs:
            out.append("    0u,")
        out.append("};")
        out.append("")
    out.append("constexpr MapInfo kMaps[] = {")
    for index, (spec, data) in enumerate(results):
        ident = cpp_ident(spec.key)
        x, y, z, yaw = data["spawn"]
        step = 600.0 if data["large"] else spec.treasure_step
        out.append(f'    {{"{spec.name}", "{spec.stage}", {spec.room}, {spec.layer}, {0x200 + index}, '
                   f"{data['fallback']}, "
                   f"{x:.0f}.0f, {y:.0f}.0f, {z:.0f}.0f, {yaw}, 0x{data['spawn_prm']:08X}u, "
                   f"{'true' if data['large'] else 'false'}, {step:.0f}.0f,")
        out.append(f"        kPalette_{ident}, {len(spec.palette)},")
        out.append(f"        kScenery_{ident}, {len(data['scenery'])}, kRemoved_{ident}, "
                   f"{len(set(data['removed']))}}},")
    out.append("};")
    return "\n".join(out) + "\n"


COLORS = {"Pot": "dodgerblue", "Big Pot": "red", "Crate": "saddlebrown", "Barrel": "sienna",
          "Skull": "white", "Red Pot": "orangered", "Big Blue Pot": "blue", "Pumpkin": "orange",
          "Small Rock": "gray", "Big Rock": "darkgray", "Horse Grass": "lime",
          "Chair": "khaki", "Sofa": "tan", "Dining Table": "wheat", "Yeto's Barrel": "peru"}


def render(spec, floor, reach, natives, data, removed_pos, closed, path):
    from PIL import Image, ImageDraw
    keys = list(reach)
    xs = [floor.xz(*k)[0] for k in keys]
    zs = [floor.xz(*k)[1] for k in keys]
    x0, x1, z0, z1 = min(xs) - 400, max(xs) + 400, min(zs) - 400, max(zs) + 400
    scale = 1400 / max(x1 - x0, z1 - z0)
    img = Image.new("RGB", (int((x1 - x0) * scale) + 1, int((z1 - z0) * scale) + 1), (20, 20, 24))
    d = ImageDraw.Draw(img, "RGBA")

    def p(x, z):
        return (x - x0) * scale, (z - z0) * scale
    c = max(1.0, floor.cell * scale)

    def cellbox(key, fill):
        x, z = floor.xz(*key)
        if x0 <= x <= x1 and z0 <= z <= z1:
            px, pz = p(x, z)
            d.rectangle([px - c / 2, pz - c / 2, px + c / 2, pz + c / 2], fill=fill)
    for key in floor.levels:
        if key in reach:
            cellbox(key, (45, 95, 120) if floor.wet(key, reach[key][0]) else (60, 110, 60))
        else:
            cellbox(key, (55, 55, 60))
    for key in floor.hazard:
        if key not in floor.levels:
            cellbox(key, (40, 80, 170))
    for key in floor.exits:
        cellbox(key, (200, 60, 200))
    for key in floor.walls:
        cellbox(key, (15, 15, 15))
    for x, y, z, r in closed.doors:
        px, pz = p(x, z)
        d.ellipse([px - r * scale, pz - r * scale, px + r * scale, pz + r * scale], outline=(255, 140, 0), width=2)
    for ex, ey, ez, hx, height, hz, sn, cs in closed.exits:
        corners = []
        for lx, lz in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz)):
            corners.append(p(ex + lx * cs + lz * sn, ez - lx * sn + lz * cs))
        d.polygon(corners, outline=(255, 60, 255), width=2)
    for n in natives:
        px, pz = p(n["pos"][0], n["pos"][2])
        d.rectangle([px - 2, pz - 2, px + 2, pz + 2], fill=(230, 230, 230))
    for x, z in removed_pos:
        px, pz = p(x, z)
        d.line([px - 4, pz - 4, px + 4, pz + 4], fill=(220, 60, 220), width=2)
        d.line([px - 4, pz + 4, px + 4, pz - 4], fill=(220, 60, 220), width=2)
    for s in data["scenery"]:
        px, pz = p(s["x"], s["z"])
        r = max(3, FOOTPRINT[s["kind"]] * scale)
        d.ellipse([px - r, pz - r, px + r, pz + r], fill=COLORS.get(s["kind"], "yellow"), outline="black")
    x, y, z, yaw = data["spawn"]
    px, pz = p(x, z)
    d.ellipse([px - 8, pz - 8, px + 8, pz + 8], outline="cyan", width=3)
    a = math.radians(yaw * 360 / 65536)
    d.line([px, pz, px + math.sin(a) * 20, pz + math.cos(a) * 20], fill="cyan", width=3)
    img.save(path)


def exact_floor(kcl, x, z, hint):
    """The collision floor height at (x, z) closest to `hint`, from the triangles themselves."""
    best = None
    for a, b, c, n, f in kcl.values():
        up = n[:, 1] > 0.7
        d = (b[:, 2] - c[:, 2]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 2] - c[:, 2])
        with np.errstate(divide="ignore", invalid="ignore"):
            l1 = ((b[:, 2] - c[:, 2]) * (x - c[:, 0]) + (c[:, 0] - b[:, 0]) * (z - c[:, 2])) / d
            l2 = ((c[:, 2] - a[:, 2]) * (x - c[:, 0]) + (a[:, 0] - c[:, 0]) * (z - c[:, 2])) / d
            l3 = 1 - l1 - l2
            inside = up & (np.abs(d) > 1e-6) & (l1 >= -1e-4) & (l2 >= -1e-4) & (l3 >= -1e-4)
            heights = l1 * a[:, 1] + l2 * b[:, 1] + l3 * c[:, 1]
        for y in heights[inside]:
            if best is None or abs(y - hint) < abs(best - hint):
                best = float(y)
    return best


def build(spec, disc, rng):
    stage_dir = disc / "res" / "Stage" / spec.stage
    stage_files = rarc_files((stage_dir / "STG_00.arc").read_bytes())
    stage_dzs = stage_files["dzs/stage.dzs"]
    table = room_table(stage_dzs)
    rooms = spec.rooms or table.get(spec.room) or [spec.room]
    if spec.room not in rooms:
        rooms = [spec.room] + rooms
    kcl = {}
    natives, removed, removed_pos, doors, exits = [], [], [], [], []
    spawn_prm = None
    native_points = []

    def collect(actor):
        name = actor["name"]
        if name.startswith(REMOVED_NAMES):
            removed.append(actor["crc"])
            removed_pos.append((actor["pos"][0], actor["pos"][2]))
            return
        natives.append(actor)
        x, y, z = actor["pos"]
        if name in DOOR_NAMES:
            doors.append((x, y, z, 170.0))
        elif name == "scnChg":
            sx, sy, sz = actor["scale"]
            a = actor["ang"][1] * math.tau / 65536
            exits.append((x, y, z, sx * 75, sy * 150, sz * 75, math.sin(a), math.cos(a)))

    for actor in actors(stage_dzs, spec.layer_of(spec.room)):
        if actor["name"] in DOOR_NAMES:
            collect(actor)
    for room in rooms:
        files = rarc_files((stage_dir / f"R{room:02d}_00.arc").read_bytes())
        kcl[room] = parse_kcl(files["kcl/room.kcl"], files["plc/room.plc"])
        dzr = next(v for k, v in files.items() if k.endswith(".dzr"))
        for actor in actors(dzr, spec.layer_of(room)):
            collect(actor)
        for point in link_points(dzr):
            point["room"] = room
            native_points.append(point)
            if room == spec.room and ((point["prm"] >> 12) & 0x1F) == 0 and point["prm"] >> 24 == 0xFF:
                spawn_prm = point["prm"]
    allx = np.concatenate([np.concatenate([a[:, 0], b[:, 0], c[:, 0]]) for a, b, c, n, f in kcl.values()])
    allz = np.concatenate([np.concatenate([a[:, 2], b[:, 2], c[:, 2]]) for a, b, c, n, f in kcl.values()])
    box = (float(allx.min()) - 200, float(allz.min()) - 200, float(allx.max()) + 200, float(allz.max()) + 200)
    if spec.clip:
        box = (max(box[0], spec.clip[0]), max(box[1], spec.clip[1]), min(box[2], spec.clip[2]), min(box[3], spec.clip[3]))
    floor = Floor(kcl, box, spec.cell, spec.ymin, spec.ymax)
    closed = Closed(doors, exits)

    sx, sz, yaw_deg, yhint = spec.spawn
    key = floor.ij(sx, sz)
    sy = exact_floor(kcl, sx, sz, yhint)
    if sy is None or abs(sy - yhint) > 150:
        raise RuntimeError(f"{spec.key}: no floor near the spawn height ({sy} vs {yhint})")
    reach = floor.reach([(sx, sz, sy)], closed)
    # The game's own standing points mark floor the step rule cannot connect (fords, ledges to
    # climb, Goron launches). Field rooms join seamlessly, so all of their points count; in
    # dungeons and interiors only points in rooms already reached, never behind a shut door.
    reached = {floor.room_at(k, ys[0], -1) for k, ys in reach.items()}
    base_area = len(reach) * spec.cell * spec.cell / 1e6
    starts = [(sx, sz, sy)]
    for point in native_points:
        if not spec.stage.startswith("F_") and point["room"] not in reached:
            continue
        x, y, z = point["pos"]
        a = point["yaw"] * math.tau / 65536
        # Points at a house door face out of it; step out of the closed doorway.
        for step in (0, 200, 300, 400):
            px, pz = x + math.sin(a) * step, z + math.cos(a) * step
            level = floor.level_near(floor.ij(px, pz), y)
            if level is not None and abs(level - y) <= 60 and not closed(px, pz, level):
                starts.append((px, pz, level))
                break
    reach = floor.reach(starts, closed)
    if spawn_prm is None:
        spawn_prm = 0xFF000000 | (spec.room & 0x3F)
    planner = Planner(spec, floor, reach, natives, closed, native_points, rng)
    scenery = planner.plan()
    for s in scenery:
        s["room"] = floor.room_at(floor.ij(s["x"], s["z"]), s["y"], spec.room)

    # Without StageService the round falls back to the nearest native standing start.
    safe = [p for p in native_points if p["room"] == spec.room and ((p["prm"] >> 12) & 0x1F) == 0
            and p["prm"] >> 24 == 0xFF]
    if not safe:
        raise RuntimeError(f"{spec.key}: no native standing start in room {spec.room}")
    fallback = min(safe, key=lambda p: math.hypot(p["pos"][0] - sx, p["pos"][2] - sz))["point"]
    yaw = int(round(yaw_deg / 360 * 65536))
    yaw = ((yaw + 32768) % 65536) - 32768
    area = len(reach) * spec.cell * spec.cell / 1e6
    reached_rooms = sorted({floor.room_at(k, ys[0], -1) for k, ys in reach.items()} - {-1})
    data = dict(spawn=(sx, sy + 5, sz, yaw), spawn_prm=spawn_prm, fallback=fallback,
                scenery=scenery, removed=removed, large=spec.large)
    print(f"{spec.key}: rooms {rooms} (reached {reached_rooms}), reachable {area:.1f} M units^2 "
          f"({base_area:.1f} from the spawn)"
          f"{' (large)' if data['large'] else ''}, {len(natives)} natives, {len(doors)} doors, "
          f"{len(exits)} exits, {len(scenery)} scenery, {len(set(removed))} removed")
    return data, floor, reach, natives, removed_pos, closed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disc", type=Path, help="extracted disc folder containing res/Stage")
    parser.add_argument("--preview", type=Path, help="write a PNG per map into this folder")
    parser.add_argument("--only", help="build one map key (no source output)")
    args = parser.parse_args()
    disc = args.disc if (args.disc / "res").exists() else args.disc / "files"
    load_prop_ids()
    for spec in MAPS:
        for p in spec.palette:
            if p not in PROP_IDS:
                sys.exit(f"{spec.key}: unknown prop {p}")
        for theme, _ in spec.themes:
            if theme not in GROUPS:
                sys.exit(f"{spec.key}: unknown scenery group {theme}")
    results = []
    for spec in MAPS:
        if args.only and spec.key != args.only:
            continue
        rng = random.Random(zlib.crc32(spec.key.encode()))
        data, floor, reach, natives, removed_pos, closed = build(spec, disc, rng)
        results.append((spec, data))
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            render(spec, floor, reach, natives, data, removed_pos, closed, args.preview / f"{spec.key}.png")
    if not args.only:
        (ROOT / "src" / "arena_data.inc").write_text(emit(results))
        # The test bots pick disguises from the same palettes.
        palettes = [{"name": spec.name, "palette": [PROP_IDS[p] for p in spec.palette]} for spec, _ in results]
        (ROOT / "server" / "arenas.json").write_text(json.dumps(palettes, indent=1) + "\n")
        print("wrote src/arena_data.inc and server/arenas.json")


if __name__ == "__main__":
    main()
