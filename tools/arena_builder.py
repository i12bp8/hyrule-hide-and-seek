#!/usr/bin/env python3
"""Build the Prop Hunt arenas from your own extracted game files.

    python tools/arena_builder.py /path/to/disc/files [--preview work/arena] [--only KEY]

For every map in MAPS below this reads the stage's room collision (KCL + PLC) and actor layout
(DZR), then:

  * computes the floor that is actually reachable from the round's spawn, inside the play area;
  * places extra *native* scenery (real pots, crates, barrels, pumpkins, furniture...) on that
    floor, against walls and in small clusters, so a map has plenty of real objects to hide among;
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
import math
import random
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
# area: union of circles the players cannot leave. spawn: (x, z, yaw degrees, y hint).
# palette: disguises offered on the map (names from src/props.cpp, in cycling order).
# scenery: (kind, count) of real objects to add; kinds are in TEMPLATES below.
# ---------------------------------------------------------------------------------------------


@dataclasses.dataclass
class MapSpec:
    key: str
    name: str
    stage: str
    room: int
    rooms: list[int]
    layer: int
    spawn: tuple[float, float, float, float]
    area: list[tuple[float, float, float]]
    palette: list[str]
    scenery: list[tuple[str, int]] = dataclasses.field(default_factory=list)
    large: bool = False
    cell: float = 40.0
    treasure_step: float = 300.0
    ymin: float = -1e9
    ymax: float = 1e9
    keep_clear: list[tuple[float, float, float]] = dataclasses.field(default_factory=list)
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

MAPS: list[MapSpec] = [
    MapSpec("ordon_village", "Ordon Village", "F_SP103", 0, [0], 4,
            spawn=(-650, 2300, 180, 200),
            area=[(-1000, 2600, 3400), (2600, 1500, 1900), (-3700, 2700, 1500), (-800, 5700, 1700)],
            palette=["Pumpkin", "Pot", "Big Pot", "Crate", "Barrel", "Cucco", "Hawk Grass",
                     "Pumpkin Leaves", "Small Rock", "Sign", "Nameplate", "Lily Pad", "Cat",
                     "Scarecrow"],
            scenery=[("Pot", 8), ("Big Pot", 4), ("Crate", 8), ("Barrel", 6), ("Pumpkin", 6),
                     ("Small Rock", 4)]),
    MapSpec("ordon_ranch", "Ordon Ranch", "F_SP00", 0, [0], 2,
            spawn=(-7400, -19300, 90, 15300),
            area=[(-7200, -18800, 4300), (-11400, -20400, 1700)],
            palette=["Goat", "Crate", "Barrel", "Pot", "Horse Grass", "Small Rock", "Big Rock",
                     "Pumpkin"],
            scenery=[("Crate", 10), ("Barrel", 8), ("Pot", 4), ("Horse Grass", 4),
                     ("Small Rock", 4), ("Big Rock", 3)], large=True, cell=50),
    MapSpec("kakariko", "Kakariko Village", "F_SP109", 0, [0], 2,
            spawn=(-1200, 2500, 180, 0),
            area=[(-1300, 1500, 3300), (-1500, 6600, 2600), (-1300, -3200, 2300)],
            palette=["Crate", "Barrel", "Pot", "Red Pot", "Big Pot", "Sign", "Cucco",
                     "Horse Grass", "Board Target", "Pole Target", "Small Rock", "Big Rock"],
            scenery=[("Crate", 8), ("Barrel", 10), ("Red Pot", 8), ("Pot", 4), ("Big Pot", 4),
                     ("Small Rock", 4), ("Big Rock", 3)]),
    MapSpec("graveyard", "Kakariko Graveyard", "F_SP111", 0, [0], 2,
            spawn=(13867, 920, 130, 100),
            area=[(12600, 100, 2700), (16600, -300, 2200), (20300, -300, 1500)],
            palette=["Skull", "Gravestone", "Pushable Grave", "Red Pot", "Big Pot", "Small Rock",
                     "Big Rock", "Pot"],
            scenery=[("Skull", 10), ("Red Pot", 6), ("Big Pot", 4), ("Small Rock", 4),
                     ("Big Rock", 3), ("Pot", 4)]),
    MapSpec("death_mountain", "Death Mountain", "F_SP110", 3, [3], 2,
            spawn=(2800, -3400, 240, -1000),
            area=[(400, -2700, 3400), (-3600, -4100, 1600)],
            palette=["Big Rock", "Small Rock", "Barrel", "Crate", "Pot", "Big Pot", "Red Pot"],
            scenery=[("Barrel", 8), ("Crate", 6), ("Small Rock", 6), ("Big Rock", 4),
                     ("Big Pot", 4)]),
    MapSpec("castle_town", "Castle Town", "F_SP116", 0, [0, 2, 3, 4], -1,
            spawn=(0, -800, 180, 0),
            area=[(300, -300, 3000), (-3800, 1400, 1700), (4600, 1400, 1700), (300, 4400, 2000)],
            palette=["Pot", "Crate", "Barrel", "Big Pot", "Cat", "Dog"] + CITIZENS,
            scenery=[("Pot", 8), ("Crate", 6), ("Barrel", 6), ("Big Pot", 4)],
            room_layers={0: 0, 2: 0, 3: 1, 4: 1}),
    MapSpec("sacred_grove", "Sacred Grove", "F_SP117", 1, [1, 3], 2,
            spawn=(0, 6500, 0, 1700),
            area=[(0, 6200, 3200), (-6500, 7000, 3600)],
            palette=["Skull", "Big Pot", "Small Rock", "Big Rock", "Hawk Grass", "Pot",
                     "Red Pot"],
            scenery=[("Skull", 8), ("Big Pot", 6), ("Small Rock", 4), ("Big Rock", 4),
                     ("Red Pot", 4)]),
    MapSpec("hidden_village", "Hidden Village", "F_SP128", 0, [0], 1,
            spawn=(5400, -4000, 0, 0),
            area=[(3300, -5300, 3800)],
            palette=["Cat", "Barrel", "Crate", "Pot", "Red Pot", "Bar Desk", "Lantern Post",
                     "Cucco"],
            scenery=[("Barrel", 6), ("Crate", 8), ("Pot", 6), ("Red Pot", 4)]),
    MapSpec("bulblin_camp", "Bulblin Camp", "F_SP118", 1, [1, 3], 3,
            spawn=(4500, -3300, 0, 260),
            area=[(2400, -3800, 4000), (2000, -11000, 4000)],
            palette=["Crate", "Barrel", "Skull", "Big Pot", "Red Pot", "Caravan Fence",
                     "Boar Bones", "Big Rock"],
            scenery=[("Crate", 8), ("Barrel", 8), ("Skull", 8), ("Big Pot", 4), ("Red Pot", 4)],
            large=True, cell=50),
    MapSpec("telmas_bar", "Telma's Bar", "R_SP116", 5, [5], 4,
            spawn=(3141, 4184, 180, -1150),
            area=[(2900, 3300, 1300)],
            palette=["Big Blue Pot", "Pot", "Red Pot", "Big Pot", "Barrel", "Crate", "Map Table",
                     "Cat"],
            scenery=[("Barrel", 4), ("Crate", 4), ("Pot", 4)], cell=20, ymax=-700),
    MapSpec("snowpeak_ruins", "Snowpeak Ruins", "D_MN11", 5, [5], 0,
            spawn=(4350, -7400, 0, 0),
            area=[(4350, -6150, 2100)],
            palette=["Chair", "Sofa", "Dining Table", "Yeto's Barrel", "Red Pot", "Big Blue Pot",
                     "Big Pot", "Pot"],
            scenery=[("Yeto's Barrel", 8), ("Red Pot", 8), ("Big Blue Pot", 4), ("Chair", 6),
                     ("Dining Table", 2)], cell=30, ymax=600),
    MapSpec("arbiters_grounds", "Arbiter's Grounds", "D_MN10", 0, [0], 0,
            spawn=(0, 14600, 180, 100),
            area=[(-800, 10700, 4200), (0, 15000, 2000)],
            palette=["Skull", "Big Pot", "Red Pot", "Oil Jar", "Pot", "Small Rock"],
            scenery=[("Skull", 10), ("Big Pot", 6), ("Red Pot", 6)], cell=30),
    MapSpec("hyrule_castle", "Hyrule Castle Grounds", "D_MN09", 11, [11, 14], 0,
            spawn=(0, 9000, 180, 0),
            area=[(0, 9500, 6200), (9000, 1000, 5000)],
            palette=["Barrel", "Crate", "Big Barrel", "Caravan Fence", "Lantern Post", "Pot",
                     "Big Pot", "Skull"],
            scenery=[("Barrel", 8), ("Crate", 8), ("Pot", 6), ("Big Pot", 4)], large=True,
            cell=50),
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
# Furniture and barrels stand against walls; pots and rocks also gather in open corners.
WALL_HUGGERS = {"Crate", "Barrel", "Big Pot", "Big Blue Pot", "Chair", "Sofa", "Yeto's Barrel",
                "Pot", "Red Pot", "Skull"}

# Records that would start a cutscene, message, hint or camera change during a round.
REMOVED_NAMES = ("TagEv", "TagEvt", "TagEvC", "EvtArea", "KMsg", "Mhint", "Mmsg", "TGSPITM",
                 "TGSPCAM", "Tag_ms", "TagStat", "TagSch", "CamArea", "CamAreC", "CamChg")

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


def actors(buf, layer):
    """Actors loaded for this layer. Scaled records (SCOB/SCOn) are matched by StageService with
    a CRC over their 0x23 meaningful bytes; plain records over 0x20."""
    tag_char = str(layer) if layer < 10 else "abcde"[layer - 10]
    for tag, cnt, off in chunks(buf):
        if tag in ("ACTR", "TGOB", "ACT" + tag_char):
            stride, size = 0x20, 0x20
        elif tag in ("SCOB", "TGSC", "SCO" + tag_char):
            stride, size = 0x24, 0x23
        else:
            continue
        for k in range(cnt):
            raw = buf[off + k * stride: off + k * stride + size]
            name, prm, x, y, z, ax, ay, az, sid = struct.unpack(">8sI3f3hH", raw[:0x20])
            yield dict(name=name.rstrip(b"\0").decode("latin1"), prm=prm, pos=(x, y, z),
                       ang=(ax, ay, az), crc=zlib.crc32(raw), scaled=size != 0x20)


def link_points(buf):
    for tag, cnt, off in chunks(buf):
        if not tag.startswith("PLY"):
            continue
        for k in range(cnt):
            name, prm, x, y, z, ax, ay, az, sid = struct.unpack_from(">8sI3f3hH", buf, off + k * 0x20)
            if name.rstrip(b"\0") == b"Link":
                yield dict(prm=prm, pos=(x, y, z), yaw=ay, point=az & 0xFF)

# ---------------------------------------------------------------------------------------------
# Walkable floor analysis
# ---------------------------------------------------------------------------------------------


class Floor:
    """Grid of floor levels with wall spans, for reachability and clearance checks."""

    def __init__(self, tris, box, cell, ymin, ymax):
        self.cell = cell
        self.x0, self.z0, x1, z1 = box
        self.w = int((x1 - self.x0) / cell) + 1
        self.h = int((z1 - self.z0) / cell) + 1
        self.levels = collections.defaultdict(list)   # (i, j) -> [y]
        self.walls = collections.defaultdict(list)    # (i, j) -> [(ylo, yhi)]
        self.hazard = collections.defaultdict(list)   # water/exit/void floors: (i, j) -> [y]
        a, b, c, n, f = tris
        for t in range(len(a)):
            ny = n[t][1]
            ys = (a[t][1], b[t][1], c[t][1])
            if max(ys) < ymin or min(ys) > ymax:
                continue
            if ny > 0.7 and not f["link_through"][t]:
                bad = f["wtr"][t] or f["exit"][t] != 0x3F or f["ground"][t] in (4, 9, 10)
                self._fill(a[t], b[t], c[t], self.hazard if bad else self.levels)
            elif abs(ny) < 0.6:
                self._wall(a[t], b[t], c[t])
        for key in self.levels:
            self.levels[key] = self._merge(self.levels[key])

    def ij(self, x, z):
        return int((x - self.x0) / self.cell), int((z - self.z0) / self.cell)

    def xz(self, i, j):
        return self.x0 + (i + 0.5) * self.cell, self.z0 + (j + 0.5) * self.cell

    def _fill(self, a, b, c, out):
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
        # Water or a loading/void floor at or above this level makes the spot useless.
        return any(abs(h - y) < 40 or h > y for h in self.hazard.get(key, ()))

    def level_near(self, key, y):
        best = None
        for level in self.levels.get(key, ()):
            if best is None or abs(level - y) < abs(best - y):
                best = level
        return best

    def reach(self, start, inside):
        key = self.ij(start[0], start[1])
        y = self.level_near(key, start[2])
        if y is None:
            for r in range(1, 6):
                for di in range(-r, r + 1):
                    for dj in range(-r, r + 1):
                        k = (key[0] + di, key[1] + dj)
                        if self.levels.get(k):
                            key, y = k, self.level_near(k, start[2])
                            break
                    if y is not None:
                        break
                if y is not None:
                    break
        if y is None:
            raise RuntimeError(f"no floor at spawn {start}")
        seen = {(key, round(y))}
        queue = collections.deque([(key, y)])
        out = {}
        while queue:
            k, y = queue.popleft()
            out.setdefault(k, []).append(y)
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    if di == dj == 0:
                        continue
                    nk = (k[0] + di, k[1] + dj)
                    if not inside(*self.xz(*nk)):
                        continue
                    for ny in self.levels.get(nk, ()):
                        if abs(ny - y) > 45 or (nk, round(ny)) in seen or self.blocked(nk, ny):
                            continue
                        seen.add((nk, round(ny)))
                        queue.append((nk, ny))
        return out


def wall_distance(floor, key, y, limit):
    """Distance in cells to the nearest wall at this height, up to `limit` cells."""
    for r in range(0, limit + 1):
        for di in range(-r, r + 1):
            for dj in (-r, r) if abs(di) != r else range(-r, r + 1):
                if floor.blocked((key[0] + di, key[1] + dj), y):
                    return r, (di, dj)
    return None, None


def plan_scenery(spec, floor, reach, natives, rng):
    cell = floor.cell
    sx, sz = spec.spawn[0], spec.spawn[1]
    placed = []
    occupied = [(n["pos"][0], n["pos"][2], 90) for n in natives]
    occupied += [(x, z, r) for x, z, r in spec.keep_clear]
    candidates = []
    for key, ys in reach.items():
        for y in ys:
            x, z = floor.xz(*key)
            if math.hypot(x - sx, z - sz) < 450 or floor.wet(key, y) or y < spec.water_y:
                continue
            # Flat: every neighbour within two cells has a level within 12 units.
            flat = True
            for di in range(-2, 3):
                for dj in range(-2, 3):
                    lvl = floor.level_near((key[0] + di, key[1] + dj), y)
                    if lvl is None or abs(lvl - y) > 12:
                        flat = False
            if not flat:
                continue
            dist, direction = wall_distance(floor, key, y, int(400 / cell))
            candidates.append((x, y, z, None if dist is None else dist * cell, direction))
    rng.shuffle(candidates)
    for kind, count in spec.scenery:
        radius = FOOTPRINT[kind]
        hugs = kind in WALL_HUGGERS
        made = 0
        tries = 0
        while made < count and tries < 4:
            tries += 1
            # Seeds spread over the map; each seed grows a cluster of 1-3 objects.
            for x, y, z, wall, direction in candidates:
                if made >= count:
                    break
                if wall is not None and wall < radius + 10:
                    continue
                if hugs and tries < 3 and (wall is None or wall > radius + 140):
                    continue
                if any(math.hypot(x - ox, z - oz) < radius + orad + 30 for ox, oz, orad in occupied):
                    continue
                # Keep clusters apart so the whole map gets objects.
                if any(math.hypot(x - p["x"], z - p["z"]) < 650 and p["seed"] for p in placed):
                    continue
                yaw = rng.randrange(0, 65536)
                if direction is not None and kind in ("Chair", "Sofa", "Dining Table", "Crate"):
                    yaw = int(math.degrees(math.atan2(-direction[0], -direction[1])) / 360 * 65536) & 0xFFFF
                placed.append(dict(kind=kind, x=x, y=y, z=z, yaw=yaw, seed=True))
                occupied.append((x, z, radius))
                made += 1
                # Grow a small cluster around the seed.
                for _ in range(rng.choice((0, 1, 1, 2))):
                    if made >= count:
                        break
                    ang = rng.random() * math.tau
                    d = radius * 2 + 25 + rng.random() * 40
                    cx, cz = x + math.cos(ang) * d, z + math.sin(ang) * d
                    key = floor.ij(cx, cz)
                    cy = floor.level_near(key, y)
                    if (cy is None or abs(cy - y) > 10 or key not in reach or floor.wet(key, cy) or cy < spec.water_y
                            or floor.blocked(key, cy)
                            or any(math.hypot(cx - ox, cz - oz) < radius + orad + 15 for ox, oz, orad in occupied)):
                        continue
                    placed.append(dict(kind=kind, x=cx, y=cy, z=cz, yaw=rng.randrange(0, 65536), seed=False))
                    occupied.append((cx, cz, radius))
                    made += 1
        if made < count:
            print(f"  {spec.key}: placed {made}/{count} {kind}")
    return placed

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
    import re
    entries = re.findall(r'(?:carry|citizen)\("([^"]+)"|\.name = "([^"]+)"', names[start:])
    for index, (a, b) in enumerate(entries):
        PROP_IDS[a or b] = index
    assert "Pot" in PROP_IDS and "Treasure Rupee" in PROP_IDS, body


def emit(results):
    out = ["// Generated by tools/arena_builder.py from the game disc. Do not edit by hand.",
           "// Re-run the builder after changing its MAPS definitions.", ""]
    for spec, data in results:
        ident = cpp_ident(spec.key)
        out.append(f"constexpr Circle kArea_{ident}[] = {{")
        out += [f"    {{{x:.0f}.0f, {z:.0f}.0f, {r:.0f}.0f}}," for x, z, r in spec.area]
        out.append("};")
        out.append(f"constexpr uint8_t kPalette_{ident}[] = {{")
        out.append("    " + ", ".join(str(PROP_IDS[p]) for p in spec.palette) + ",")
        out.append("};")
        out.append(f"constexpr Scenery kScenery_{ident}[] = {{")
        for s in data["scenery"]:
            name, prm, ax, az, room_bits = TEMPLATES[s["kind"]]
            room = data["room_of"](s)
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
        step = 600.0 if spec.large else spec.treasure_step
        out.append(f'    {{"{spec.name}", "{spec.stage}", {spec.room}, {spec.layer}, {0x200 + index}, '
                   f"{data['fallback']}, "
                   f"{x:.0f}.0f, {y:.0f}.0f, {z:.0f}.0f, {yaw}, 0x{data['spawn_prm']:08X}u, "
                   f"{'true' if spec.large else 'false'}, {step:.0f}.0f,")
        out.append(f"        kArea_{ident}, {len(spec.area)}, kPalette_{ident}, {len(spec.palette)},")
        out.append(f"        kScenery_{ident}, {len(data['scenery'])}, kRemoved_{ident}, "
                   f"{len(set(data['removed']))}}},")
    out.append("};")
    return "\n".join(out) + "\n"


def render(spec, floor, reach, natives, data, removed_pos, path):
    from PIL import Image, ImageDraw
    xs = [x - r for x, z, r in spec.area] + [x + r for x, z, r in spec.area]
    zs = [z - r for x, z, r in spec.area] + [z + r for x, z, r in spec.area]
    x0, x1, z0, z1 = min(xs) - 300, max(xs) + 300, min(zs) - 300, max(zs) + 300
    scale = 1100 / max(x1 - x0, z1 - z0)
    img = Image.new("RGB", (int((x1 - x0) * scale) + 1, int((z1 - z0) * scale) + 1), (20, 20, 24))
    d = ImageDraw.Draw(img, "RGBA")

    def p(x, z):
        return (x - x0) * scale, (z - z0) * scale
    c = floor.cell * scale
    for key, ys in floor.levels.items():
        x, z = floor.xz(*key)
        if x0 <= x <= x1 and z0 <= z <= z1:
            px, pz = p(x, z)
            col = (60, 110, 60) if key in reach else (55, 55, 60)
            d.rectangle([px - c / 2, pz - c / 2, px + c / 2, pz + c / 2], fill=col)
    for key in floor.hazard:
        x, z = floor.xz(*key)
        if x0 <= x <= x1 and z0 <= z <= z1 and key not in floor.levels:
            px, pz = p(x, z)
            d.rectangle([px - c / 2, pz - c / 2, px + c / 2, pz + c / 2], fill=(40, 80, 170))
    for key in floor.walls:
        x, z = floor.xz(*key)
        if x0 <= x <= x1 and z0 <= z <= z1:
            px, pz = p(x, z)
            d.rectangle([px - c / 2, pz - c / 2, px + c / 2, pz + c / 2], fill=(15, 15, 15))
    for x, z, r in spec.area:
        px, pz = p(x, z)
        d.ellipse([px - r * scale, pz - r * scale, px + r * scale, pz + r * scale], outline=(255, 230, 120), width=2)
    for n in natives:
        px, pz = p(n["pos"][0], n["pos"][2])
        d.rectangle([px - 3, pz - 3, px + 3, pz + 3], fill=(230, 230, 230))
    for x, z in removed_pos:
        px, pz = p(x, z)
        d.line([px - 4, pz - 4, px + 4, pz + 4], fill=(220, 60, 220), width=2)
        d.line([px - 4, pz + 4, px + 4, pz - 4], fill=(220, 60, 220), width=2)
    colors = {"Pot": "dodgerblue", "Big Pot": "red", "Crate": "saddlebrown", "Barrel": "sienna",
              "Skull": "white", "Red Pot": "orangered", "Big Blue Pot": "blue", "Pumpkin": "orange",
              "Small Rock": "gray", "Big Rock": "darkgray", "Horse Grass": "lime",
              "Chair": "khaki", "Sofa": "tan", "Dining Table": "wheat", "Yeto's Barrel": "peru"}
    for s in data["scenery"]:
        px, pz = p(s["x"], s["z"])
        d.ellipse([px - 5, pz - 5, px + 5, pz + 5], fill=colors.get(s["kind"], "yellow"), outline="black")
    x, y, z, yaw = data["spawn"]
    px, pz = p(x, z)
    d.ellipse([px - 8, pz - 8, px + 8, pz + 8], outline="cyan", width=3)
    a = math.radians(yaw * 360 / 65536)
    d.line([px, pz, px + math.sin(a) * 20, pz + math.cos(a) * 20], fill="cyan", width=3)
    img.save(path)


def build(spec, disc, rng):
    stage_dir = disc / "res" / "Stage" / spec.stage
    tris = [[], [], [], [], collections.defaultdict(list)]
    natives, removed, removed_pos = [], [], []
    spawn_prm = None
    native_points = []
    room_boxes = {}
    for room in spec.rooms:
        files = rarc_files((stage_dir / f"R{room:02d}_00.arc").read_bytes())
        a, b, c, n, f = parse_kcl(files["kcl/room.kcl"], files["plc/room.plc"])
        for lst, arr in zip(tris, (a, b, c, n)):
            lst.append(arr)
        for k, v in f.items():
            tris[4][k].append(v)
        room_boxes[room] = (a[:, 0].min(), a[:, 2].min(), a[:, 0].max(), a[:, 2].max())
        dzr = next(v for k, v in files.items() if k.endswith(".dzr"))
        for actor in actors(dzr, spec.layer_of(room)):
            if actor["name"].startswith(REMOVED_NAMES):
                removed.append(actor["crc"])
                removed_pos.append((actor["pos"][0], actor["pos"][2]))
            else:
                natives.append(actor)
        for point in link_points(dzr):
            point["room"] = room
            native_points.append(point)
            if room == spec.room and ((point["prm"] >> 12) & 0x1F) == 0 and point["prm"] >> 24 == 0xFF:
                spawn_prm = point["prm"]
    a = np.concatenate(tris[0]); b = np.concatenate(tris[1]); c = np.concatenate(tris[2])
    n = np.concatenate(tris[3]); f = {k: np.concatenate(v) for k, v in tris[4].items()}
    xs = [x - r for x, z, r in spec.area] + [x + r for x, z, r in spec.area]
    zs = [z - r for x, z, r in spec.area] + [z + r for x, z, r in spec.area]
    box = (min(xs) - 200, min(zs) - 200, max(xs) + 200, max(zs) + 200)
    floor = Floor((a, b, c, n, f), box, spec.cell, spec.ymin, spec.ymax)

    def inside(x, z):
        return any((x - cx) ** 2 + (z - cz) ** 2 <= r * r for cx, cz, r in spec.area)

    sx, sz, yaw_deg, yhint = spec.spawn
    reach = floor.reach((sx, sz, yhint), inside)
    key = floor.ij(sx, sz)
    sy = floor.level_near(key, yhint)
    if sy is None or abs(sy - yhint) > 150:
        raise RuntimeError(f"{spec.key}: no floor near the spawn height ({sy} vs {yhint})")
    if spawn_prm is None:
        spawn_prm = 0xFF000000 | (spec.room & 0x3F)
    prop_natives = [x for x in natives if not x["name"].startswith(("SwArea", "AND_SW", "ClearB", "Savmem", "scnChg", "mmvbg", "Digpl", "Drop", "ky_tag", "kytag", "Wljump", "noChgRm", "Hstop", "Grass", "flwr", "flower", "pflwr", "item", "atkItem", "Stream", "Fish", "Worm"))]
    scenery = plan_scenery(spec, floor, reach, prop_natives, rng) if spec.scenery else []

    def room_of(s):
        for room, (bx0, bz0, bx1, bz1) in room_boxes.items():
            if bx0 <= s["x"] <= bx1 and bz0 <= s["z"] <= bz1:
                return room
        return spec.room

    # Without StageService the round falls back to the nearest native standing start.
    safe = [p for p in native_points if p["room"] == spec.room and ((p["prm"] >> 12) & 0x1F) == 0
            and p["prm"] >> 24 == 0xFF]
    if not safe:
        raise RuntimeError(f"{spec.key}: no native standing start in room {spec.room}")
    fallback = min(safe, key=lambda p: math.hypot(p["pos"][0] - sx, p["pos"][2] - sz))["point"]
    yaw = int(round(yaw_deg / 360 * 65536))
    yaw = ((yaw + 32768) % 65536) - 32768
    data = dict(spawn=(sx, sy + 5, sz, yaw), spawn_prm=spawn_prm, fallback=fallback,
                scenery=scenery, removed=removed, room_of=room_of)
    reach_area = len(reach) * spec.cell * spec.cell
    print(f"{spec.key}: reachable {reach_area / 1e6:.1f} M units^2, {len(prop_natives)} natives, "
          f"{len(scenery)} scenery, {len(set(removed))} removed")
    return data, floor, reach, prop_natives, removed_pos


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
    results = []
    for spec in MAPS:
        if args.only and spec.key != args.only:
            continue
        rng = random.Random(zlib.crc32(spec.key.encode()))
        data, floor, reach, natives, removed_pos = build(spec, disc, rng)
        results.append((spec, data))
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            render(spec, floor, reach, natives, data, removed_pos, args.preview / f"{spec.key}.png")
    if not args.only:
        (ROOT / "src" / "arena_data.inc").write_text(emit(results))
        # The test bots pick disguises from the same palettes.
        import json
        palettes = [{"name": spec.name, "palette": [PROP_IDS[p] for p in spec.palette]} for spec, _ in results]
        (ROOT / "server" / "arenas.json").write_text(json.dumps(palettes, indent=1) + "\n")
        print("wrote src/arena_data.inc and server/arenas.json")


if __name__ == "__main__":
    main()
