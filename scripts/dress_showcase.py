#!/usr/bin/env python3
"""Dresses the factory with the showcase kit (ADR-0082): props along the walls, lamps, grime and water.

    python -I scripts/dress_showcase.py [--seed N] [--map ROOM]

The rooms below each have a purpose, and what tells it: props stand with their backs to walls and a walkway
in front, away from doors, ladders, pickups, triggers and effects, under enough ceiling; hanging lamps hang
from ceilings away from walls, wall lamps sit on walls at head height; decals of grime, mold and blood and wet
patches go on floors and wall bases. Everything a run adds hangs from one entity per room named
"Dressing: <room>", which the next run replaces, so the script can be run again after a change. Props that a
player could walk into get a static box (a body without mass, ADR-0083). The level is written in the
engine's layout by the editor afterwards (editor +editor.resave true), as other scripts' levels are.

Placement reads the level's geometry from Factory.gltf: floors (faces looking up), ceilings (faces looking
down) and walls (vertical faces between knee and head height), on a grid of CELL meters. --map prints a room's
grid: '#' wall, '.' free floor, 'x' taken, ' ' no floor.
"""

import argparse
import base64
import json
import math
import random
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTORY = ROOT / "assets" / "Factory"
LEVEL = FACTORY / "Factory.level.json"
KIT = FACTORY / "Showcase"
CELL = 0.25
GROUP = "Dressing: "
WALKWAY = 1.2      # Meters kept free in front of a prop.
DOOR_CLEAR = 2.5   # Meters kept free around a door.
THING_CLEAR = 1.6  # Meters kept free around what the game uses: pickups, triggers, effects, spawns.
HEADROOM = 0.15

# Room: (floor level, interior rectangle x0, x1, z0, z1 in meters, what it holds).
# A recipe item is (model or prefab, count, how): "wall" stands against a wall, "cluster" stands against a wall
# next to the previous item of the same recipe line, "hang" hangs from the ceiling, "mount" sits on a wall at
# head height, "decal:<image>" and "wet" paint the floor near walls.
ROOMS = {
    "Start room": (0.0, (-5, 6, -8, 1), [
        ("metal_tool_chest", 1, "wall"), ("steel_frame_shelves_01", 1, "wall"), ("tool_cart", 1, "wall"),
        ("Barrel_02", 2, "cluster"), ("metal_jerrycan", 2, "cluster"), ("korean_fire_extinguisher_01", 1, "wall"),
        ("decal:Decals/grime.png", 4, "floor"), ("industrial_wall_lamp", 2, "mount")]),
    "Airlock": (0.0, (-5, 6, -19, -16), [
        ("WetFloorSign_01", 1, "wall"), ("utility_box_01", 1, "wall"), ("wet", 1, "floor")]),
    "Loading floor": (0.0, (-14, 25, -57, -20), [
        ("wooden_crate_01", 6, "cluster"), ("old_military_crate", 4, "cluster"), ("wooden_crate_02", 5, "cluster"),
        ("Barrel_01", 6, "cluster"), ("barrel_03", 4, "cluster"), ("steel_frame_shelves_02", 4, "wall"),
        ("steel_frame_shelves_01", 3, "wall"), ("metal_tool_chest", 2, "wall"), ("industrial_storage_cart", 2, "wall"),
        ("portable_generator", 1, "wall"), ("hand_truck", 1, "wall"), ("propane_tank", 2, "cluster"),
        ("cement_bag", 4, "cluster"), ("plastic_crate_01", 3, "cluster"), ("trashbag", 2, "cluster"),
        ("hanging_industrial_lamp", 5, "hang"), ("overhead_crane", 1, "crane"),
        ("decal:Decals/grime.png", 8, "floor"), ("decal:Decals/mold.png", 3, "floor"), ("wet", 3, "floor")]),
    "Workshop": (0.0, (30, 50, -48, -30), [
        ("drill_press_01", 1, "wall"), ("old_military_compressor", 1, "wall"), ("tool_cart", 1, "wall"),
        ("metal_office_desk", 1, "wall"), ("metal_stool_01", 2, "cluster"), ("steel_frame_shelves_02", 2, "wall"),
        ("metal_toolbox", 2, "cluster"), ("power_box_01", 1, "wall"), ("utility_box_02", 1, "wall"),
        ("wooden_crate_02", 3, "cluster"), ("Barrel_02", 3, "cluster"), ("industrial_storage_cart", 1, "wall"),
        ("hanging_industrial_lamp", 2, "hang"), ("decal:Decals/grime.png", 5, "floor"),
        ("decal:Decals/scorch.png", 1, "floor")]),
    "West corridor": (0.0, (-44, -21, -40, -37), [
        ("caged_hanging_light", 3, "hang"), ("decal:Decals/grime.png", 3, "floor")]),
    "Yard": (0.0, (-8, 17, -99, -79), [
        ("Barrel_01", 4, "cluster"), ("Barrel_02", 3, "cluster"), ("propane_tank", 3, "cluster"),
        ("metal_jerrycan", 3, "cluster"), ("trashbag", 4, "cluster"), ("old_military_crate", 3, "cluster"),
        ("plastic_crate_01", 3, "cluster"), ("cement_bag", 4, "cluster"), ("security_light", 2, "mount"),
        ("decal:Decals/blood_splatter.png", 2, "floor"), ("wet", 4, "floor")]),
    "Offices": (2.8, (30, 51, -50, -27), [
        ("metal_office_desk", 2, "wall"), ("metal_stool_01", 2, "wall"), ("steel_frame_shelves_01", 2, "wall"),
        ("metal_toolbox", 1, "wall"), ("plastic_crate_01", 2, "cluster"), ("utility_box_01", 1, "wall"),
        ("caged_hanging_light", 2, "hang"), ("decal:Decals/grime.png", 3, "floor")]),
    "Store": (2.8, (30, 51, -68, -57), [
        ("wooden_crate_01", 3, "cluster"), ("Barrel_02", 3, "cluster"), ("steel_frame_shelves_02", 1, "wall"),
        ("cement_bag", 2, "cluster"), ("caged_hanging_light", 1, "hang")]),
    "Card room": (2.8, (47, 56, -82, -75), [
        ("old_military_crate", 2, "cluster"), ("trashbag", 1, "wall"),
        ("decal:Decals/blood_drips.png", 2, "floor")]),
    "West store": (2.8, (-24, -14, -64, -54), [
        ("steel_frame_shelves_01", 2, "wall"), ("wooden_crate_02", 3, "cluster"), ("metal_jerrycan", 2, "cluster")]),
    "Far west room": (2.8, (-58, -44, -85, -68), [
        ("Barrel_01", 4, "cluster"), ("wooden_crate_01", 4, "cluster"), ("cement_bag", 3, "cluster"),
        ("portable_generator", 1, "wall"), ("hanging_industrial_lamp", 2, "hang"),
        ("decal:Decals/mold.png", 3, "floor")]),
    "Pumps": (-8.4, (-40, -34, -61, -34), [
        ("Barrel_01", 3, "cluster"), ("utility_box_02", 2, "wall"), ("power_box_01", 1, "wall"),
        ("propane_tank", 2, "cluster"), ("caged_hanging_light", 3, "hang"), ("decal:Decals/mold.png", 4, "floor"),
        ("wet", 3, "floor")]),
    "Under the hall": (-5.6, (-4, 4, -37, -30), [
        ("portable_generator", 1, "wall"), ("tool_cart", 1, "wall"), ("barrel_03", 2, "cluster"),
        ("caged_hanging_light", 1, "hang"), ("wet", 1, "floor")]),
    "Pit": (-2.8, (-4, 5, -96, -83), [
        ("trashbag", 3, "cluster"), ("Barrel_02", 2, "cluster"), ("cement_bag", 2, "cluster"), ("wet", 2, "floor")]),
}
PREFABS = {"hanging_industrial_lamp", "industrial_wall_lamp", "caged_hanging_light", "security_light"}


# --- Geometry -----------------------------------------------------------------------------------------------

def quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def node_matrix(node):
    if "matrix" in node:
        m = node["matrix"]
        return [[m[0], m[4], m[8], m[12]], [m[1], m[5], m[9], m[13]], [m[2], m[6], m[10], m[14]], [0, 0, 0, 1]]
    t = node.get("translation", [0, 0, 0])
    r = quat_matrix(node.get("rotation", [0, 0, 0, 1]))
    s = node.get("scale", [1, 1, 1])
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def transform(m, p):
    return [m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3)]


def read_gltf(path, with_triangles):
    """The triangles (world space) or the bounds of a glTF file, and its nodes' names with their positions."""
    gltf = json.loads(path.read_text(encoding="utf-8"))
    buffers = []
    for buffer in gltf.get("buffers", []):
        uri = buffer["uri"]
        buffers.append(base64.b64decode(uri.split(",", 1)[1]) if uri.startswith("data:")
                       else (path.parent / uri).read_bytes())

    def accessor(index):
        a = gltf["accessors"][index]
        view = gltf["bufferViews"][a["bufferView"]]
        data = buffers[view["buffer"]]
        offset = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        if a["type"] == "VEC3":
            stride = view.get("byteStride", 12)
            return [struct.unpack_from("<3f", data, offset + i * stride) for i in range(a["count"])]
        fmt = {5121: "B", 5123: "H", 5125: "I"}[a["componentType"]]
        return list(struct.unpack_from(f"<{a['count']}{fmt}", data, offset))

    triangles, names = [], []
    low, high = [1e9] * 3, [-1e9] * 3

    def walk(index, parent):
        node = gltf["nodes"][index]
        world = multiply(parent, node_matrix(node))
        names.append((node.get("name", ""), transform(world, [0, 0, 0])))
        if "mesh" in node:
            for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
                attribute = primitive["attributes"]["POSITION"]
                if not with_triangles:
                    a = gltf["accessors"][attribute]
                    for corner in range(8):
                        p = [a["max"][i] if corner >> i & 1 else a["min"][i] for i in range(3)]
                        q = transform(world, p)
                        for i in range(3):
                            low[i], high[i] = min(low[i], q[i]), max(high[i], q[i])
                    continue
                if node.get("name", "").startswith("door"):
                    continue
                positions = [transform(world, p) for p in accessor(attribute)]
                indices = accessor(primitive["indices"]) if "indices" in primitive else list(range(len(positions)))
                for i in range(0, len(indices) - 2, 3):
                    triangles.append((positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]]))
        for child in node.get("children", []):
            walk(child, world)

    identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        walk(root, identity)
    return (triangles, names) if with_triangles else (low, high)


class Grid:
    """Floors, ceilings and walls of the level on a grid of CELL meters."""

    def __init__(self, triangles):
        self.floors, self.ceilings, self.walls = {}, {}, {}
        for a, b, c in triangles:
            u = [b[i] - a[i] for i in range(3)]
            v = [c[i] - a[i] for i in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            length = math.sqrt(sum(x * x for x in n)) or 1.0
            ny = n[1] / length
            if abs(ny) > 0.7:
                self._flat(a, b, c, n, self.floors if ny > 0 else self.ceilings)
            elif abs(ny) < 0.3:
                self._upright(a, u, v)

    def _flat(self, a, b, c, n, into):
        x0, x1 = min(a[0], b[0], c[0]), max(a[0], b[0], c[0])
        z0, z1 = min(a[2], b[2], c[2]), max(a[2], b[2], c[2])

        def side(p, q, r):
            return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])

        for ix in range(math.floor(x0 / CELL), math.floor(x1 / CELL) + 1):
            for iz in range(math.floor(z0 / CELL), math.floor(z1 / CELL) + 1):
                p = ((ix + 0.5) * CELL, (iz + 0.5) * CELL)
                corners = [(a[0], a[2]), (b[0], b[2]), (c[0], c[2])]
                d = [side(p, corners[0], corners[1]), side(p, corners[1], corners[2]), side(p, corners[2], corners[0])]
                if (min(d) < 0 < max(d)) or abs(n[1]) < 1e-9:
                    continue
                y = a[1] - (n[0] * (p[0] - a[0]) + n[2] * (p[1] - a[2])) / n[1]
                into.setdefault((ix, iz), []).append(y)

    def _upright(self, a, u, v):
        steps = max(4, int(max(math.sqrt(sum(x * x for x in u)), math.sqrt(sum(x * x for x in v))) / (CELL / 2)))
        for i in range(steps + 1):
            for j in range(steps + 1 - i):
                s, t = i / steps, j / steps
                p = [a[k] + s * u[k] + t * v[k] for k in range(3)]
                self.walls.setdefault((math.floor(p[0] / CELL), math.floor(p[2] / CELL)), []).append(p[1])

    def floor(self, cell, level):
        return any(abs(h - level) < 0.15 for h in self.floors.get(cell, []))

    def wall(self, cell, level):
        return any(level + 0.3 <= h <= level + 1.8 for h in self.walls.get(cell, []))

    def ceiling(self, cell, level):
        """The lowest ceiling above the floor at level, or None under the sky."""
        above = [h for h in self.ceilings.get(cell, []) if h > level + 0.5]
        return min(above) if above else None


def cell_of(x, z):
    return (math.floor(x / CELL), math.floor(z / CELL))


# --- Placing ------------------------------------------------------------------------------------------------

def yaw_quaternion(degrees):
    half = math.radians(degrees) / 2
    return [0.0, round(math.sin(half), 7), 0.0, round(math.cos(half), 7)]


def floor_decal_quaternion(degrees):
    # The decal's -Z down onto the floor, then turned about the vertical.
    y = yaw_quaternion(degrees)
    x = [-math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5)]
    return [round(v, 7) for v in (
        y[3] * x[0] + y[0] * x[3] + y[1] * x[2] - y[2] * x[1],
        y[3] * x[1] - y[0] * x[2] + y[1] * x[3] + y[2] * x[0],
        y[3] * x[2] + y[0] * x[1] - y[1] * x[0] + y[2] * x[3],
        y[3] * x[3] - y[0] * x[0] - y[1] * x[1] - y[2] * x[2])]


# The direction a wall is in, seen from a cell, and the yaw that turns a model's front (+Z) away from it.
DIRECTIONS = [((0, -1), 0.0), ((0, 1), 180.0), ((-1, 0), 90.0), ((1, 0), 270.0)]


class Room:
    def __init__(self, name, level, rect, grid, keepout, rng):
        self.name, self.level, self.grid, self.rng = name, level, grid, rng
        x0, x1, z0, z1 = rect
        self.cells = [(ix, iz) for ix in range(math.floor(x0 / CELL), math.floor(x1 / CELL) + 1)
                      for iz in range(math.floor(z0 / CELL), math.floor(z1 / CELL) + 1)]
        self.taken = set(keepout)
        self.previous = None

    def free(self, cell):
        return cell not in self.taken and self.grid.floor(cell, self.level) and not self.grid.wall(cell, self.level)

    def footprint(self, cx, cz, sx, sz):
        return [(ix, iz) for ix in range(math.floor((cx - sx / 2) / CELL), math.floor((cx + sx / 2) / CELL) + 1)
                for iz in range(math.floor((cz - sz / 2) / CELL), math.floor((cz + sz / 2) / CELL) + 1)]

    def fits(self, cx, cz, sx, sz, height, direction):
        cells = self.footprint(cx, cz, sx, sz)
        if not all(self.free(c) for c in cells):
            return False
        for c in cells:
            ceiling = self.grid.ceiling(c, self.level)
            if ceiling is not None and ceiling < self.level + height + HEADROOM:
                return False
        # A walkway in front: floor and no wall.
        dx, dz = -direction[0], -direction[1]
        reach = WALKWAY + (sz if dz else sx) / 2
        for step in range(1, int(reach / CELL) + 1):
            for c in self.footprint(cx + dx * step * CELL, cz + dz * step * CELL, sx if dz else CELL, sz if dx else CELL):
                if not self.grid.floor(c, self.level) or self.grid.wall(c, self.level):
                    return False
        return True

    def wall_spots(self):
        """Cells beside a wall, with the wall's direction: where something can stand with its back to it."""
        spots = []
        for cell in self.cells:
            if not self.free(cell):
                continue
            for (dx, dz), yaw in DIRECTIONS:
                if self.grid.wall((cell[0] + dx, cell[1] + dz), self.level):
                    spots.append((cell, (dx, dz), yaw))
        return spots

    def stand(self, size, center, near=None):
        """Where a prop of that size stands against a wall: (x, z, yaw), or None. near: next to that point."""
        spots = self.wall_spots()
        if near is not None:
            spots.sort(key=lambda s: (s[0][0] * CELL - near[0]) ** 2 + (s[0][1] * CELL - near[1]) ** 2)
            spots = spots[:40]
        else:
            self.rng.shuffle(spots)
        for cell, direction, yaw in spots:
            sx, sz = (size[0], size[2]) if direction[1] else (size[2], size[0])
            # Back to the wall: the cell's wall-side edge, then half the prop's depth out.
            wx = (cell[0] + 0.5) * CELL + direction[0] * CELL / 2
            wz = (cell[1] + 0.5) * CELL + direction[1] * CELL / 2
            depth = sz if direction[1] else sx
            cx = wx - direction[0] * (depth / 2 + 0.02)
            cz = wz - direction[1] * (depth / 2 + 0.02)
            if direction[0] == 0:
                cz = wz - direction[1] * (depth / 2 + 0.02)
            if self.fits(cx, cz, sx, sz, size[1], direction):
                for c in self.footprint(cx, cz, sx + 0.3, sz + 0.3):
                    self.taken.add(c)
                jitter = self.rng.uniform(-6, 6)
                return cx, cz, yaw + jitter
        return None

    def hang_spot(self, spacing, placed):
        """A cell under a ceiling, two meters from walls and spacing meters from the other lamps."""
        spots = [c for c in self.cells if self.grid.floor(c, self.level) and self.grid.ceiling(c, self.level)]
        self.rng.shuffle(spots)
        reach = int(2.0 / CELL)
        for cell in spots:
            x, z = (cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL
            if any((x - px) ** 2 + (z - pz) ** 2 < spacing ** 2 for px, pz in placed):
                continue
            if any(self.grid.wall((cell[0] + dx, cell[1] + dz), self.level)
                   for dx in range(-reach, reach + 1, 2) for dz in range(-reach, reach + 1, 2)):
                continue
            return x, z, self.grid.ceiling(cell, self.level)
        return None


def circuit(room):
    """The power circuit of a room's lamps: its name in lower case with underscores ("loading_floor")."""
    return re.sub(r"[^a-z0-9]+", "_", room.lower()).strip("_")


def keepouts(level, gameplay, ladders, doors):
    """Cells kept free on a floor: around doors, what the game uses, and ladders."""
    out = set()

    def disc(x, z, radius):
        for ix in range(math.floor((x - radius) / CELL), math.floor((x + radius) / CELL) + 1):
            for iz in range(math.floor((z - radius) / CELL), math.floor((z + radius) / CELL) + 1):
                if ((ix + 0.5) * CELL - x) ** 2 + ((iz + 0.5) * CELL - z) ** 2 <= radius * radius:
                    out.add((ix, iz))

    for x, y, z in doors:
        if abs(y - level) < 1.0:
            disc(x, z, DOOR_CLEAR)
    for x, y, z in gameplay:
        if level - 0.5 < y < level + 2.5:
            disc(x, z, THING_CLEAR)
    for low, high in ladders:
        if low[1] - 1 < level < high[1] + 1:
            for ix in range(math.floor((low[0] - 1) / CELL), math.floor((high[0] + 1) / CELL) + 1):
                for iz in range(math.floor((low[2] - 1) / CELL), math.floor((high[2] + 1) / CELL) + 1):
                    out.add((ix, iz))
    return out


def world_positions(level):
    """Each level entity's world position, through its parents."""
    by_id = {e["id"]: e for e in level["entities"]}

    def position(entity):
        t = entity.get("transform", {}).get("translation", [0, 0, 0])
        parent = by_id.get(entity.get("parent", 0))
        if parent is None:
            return list(t)
        r = quat_matrix(parent.get("transform", {}).get("rotation", [0, 0, 0, 1]))
        s = parent.get("transform", {}).get("scale", [1, 1, 1])
        p = position(parent)
        return [p[i] + sum(r[i][k] * t[k] * s[k] for k in range(3)) for i in range(3)]

    return {e["id"]: position(e) for e in level["entities"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed", type=int, default=16)
    parser.add_argument("--map")
    arguments = parser.parse_args()
    rng = random.Random(arguments.seed)

    triangles, names = read_gltf(FACTORY / "Factory.gltf", True)
    grid = Grid(triangles)
    doors = [p for name, p in names if name.startswith("door")]
    ladders = []
    for name, _ in names:
        if name.startswith("ladder"):
            values = dict(part.split("=", 1) for part in name.split(";")[1:] if "=" in part)
            if "min" in values and "max" in values:
                ladders.append(([float(v) for v in values["min"].replace(",", " ").split()],
                                [float(v) for v in values["max"].replace(",", " ").split()]))

    level = json.loads(LEVEL.read_text(encoding="utf-8"))
    # Last run's dressing goes, with what hangs from it.
    gone = {e["id"] for e in level["entities"] if e.get("name", "").startswith(GROUP)}
    while True:
        more = {e["id"] for e in level["entities"] if e.get("parent", 0) in gone} - gone
        if not more:
            break
        gone |= more
    level["entities"] = [e for e in level["entities"] if e["id"] not in gone]
    positions = world_positions(level)
    gameplay = [positions[e["id"]] for e in level["entities"]
                if "game" in e or "trigger" in e or "particles" in e or "body" in e]
    next_id = max(e["id"] for e in level["entities"]) + 1

    bounds = {}
    for model in {item for room in ROOMS.values() for item, _, _ in room[2]}:
        path = KIT / model / f"{model}.gltf"
        if path.exists():
            bounds[model] = read_gltf(path, False)

    added = 0
    for name, (floor, rect, recipe) in ROOMS.items():
        room = Room(name, floor, rect, grid, keepouts(floor, gameplay, ladders, doors), rng)
        group_id = next_id
        next_id += 1
        children = []
        lamps = []
        for item, count, how in recipe:
            near = None
            for _ in range(count):
                entity = None
                if item.startswith("decal:") or item == "wet":
                    spots = [s for s in room.wall_spots()]
                    if not spots:
                        break
                    cell, direction, _ = rng.choice(spots)
                    x, z = (cell[0] + 0.5) * CELL, (cell[1] + 0.5) * CELL
                    size = rng.uniform(1.2, 2.2)
                    if item == "wet":
                        entity = {"name": "Wet patch", "wet": {"puddles": 0.6, "puddle_height": 0.12},
                                  "transform": {"translation": [x, floor + 0.15, z],
                                                "scale": [size * 1.5, 0.5, size * 1.5]}}
                    else:
                        image = item.split(":", 1)[1]
                        entity = {"name": Path(image).stem.replace("_", " ").capitalize(),
                                  "decal": {"texture": image, "roughness": 0.85},
                                  "transform": {"translation": [x, floor, z],
                                                "rotation": floor_decal_quaternion(rng.uniform(0, 360)),
                                                "scale": [size, size, 0.6]}}
                elif how == "hang":
                    spot = room.hang_spot(6.0, lamps)
                    if spot is None:
                        break
                    x, z, ceiling = spot
                    lamps.append((x, z))
                    entity = {"name": item.replace("_", " ").capitalize(), "prefab": f"Prefabs/{item}.prefab.json",
                              "transform": {"translation": [x, ceiling, z]}}
                elif how == "mount":
                    spots = room.wall_spots()
                    rng.shuffle(spots)
                    for cell, direction, yaw in spots:
                        x = (cell[0] + 0.5) * CELL + direction[0] * CELL / 2
                        z = (cell[1] + 0.5) * CELL + direction[1] * CELL / 2
                        if all((x - px) ** 2 + (z - pz) ** 2 > 36 for px, pz in lamps):
                            lamps.append((x, z))
                            entity = {"name": item.replace("_", " ").capitalize(),
                                      "prefab": f"Prefabs/{item}.prefab.json",
                                      "transform": {"translation": [x, floor + 2.5, z], "rotation": yaw_quaternion(yaw)}}
                            break
                elif how == "crane":
                    low, high = bounds[item]
                    cells = [c for c in room.cells if grid.floor(c, floor) and grid.ceiling(c, floor)]
                    if not cells:
                        break
                    # Under the room's usual ceiling, over the cells that have it: low ledges do not count.
                    heights = sorted(grid.ceiling(c, floor) for c in cells)
                    ceiling = heights[len(heights) // 2]
                    cells = [c for c in cells if grid.ceiling(c, floor) >= ceiling - 0.1]
                    xs = [c[0] * CELL for c in cells]
                    zs = [c[1] * CELL for c in cells]
                    if ceiling - floor < (high[1] - low[1]) + 3.0:
                        print(f"{name}: no room for the crane under {ceiling - floor:.1f} m")
                        break
                    entity = {"name": "Overhead crane", "model": f"Showcase/{item}/{item}.gltf",
                              "transform": {"translation": [(min(xs) + max(xs)) / 2, ceiling - high[1] - 0.05,
                                                            (min(zs) + max(zs)) / 2]}}
                else:
                    low, high = bounds[item]
                    size = [high[i] - low[i] for i in range(3)]
                    spot = room.stand(size, None, near if how == "cluster" else None)
                    if spot is None:
                        break
                    x, z, yaw = spot
                    near = (x, z)
                    center = [(low[i] + high[i]) / 2 for i in range(3)]
                    entity = {"name": item.replace("_", " ").capitalize(), "model": f"Showcase/{item}/{item}.gltf",
                              "transform": {"translation": [round(x, 3), floor - low[1], round(z, 3)],
                                            "rotation": yaw_quaternion(yaw)}}
                    if size[1] > 0.3 and size[0] * size[2] > 0.08:
                        entity["body"] = {"size": [round(v, 3) for v in size], "center": [round(v, 3) for v in center],
                                          "mass": 0}
                if entity is None:
                    break
                entity["id"] = next_id
                entity["parent"] = group_id
                next_id += 1
                children.append(entity)
                if "prefab" in entity:
                    # The lamp's model and its light, on the room's power circuit, so that a scare can cut the
                    # room's lights (ADR-0084); a lamp in four buzzes, or is failing.
                    prefab = json.loads((FACTORY / entity.pop("prefab")).read_text(encoding="utf-8"))
                    model, light = prefab["entities"][0], prefab["entities"][1]
                    entity["model"] = (Path("Prefabs") / model["model"]).as_posix().replace("Prefabs/../", "")
                    lit = dict(light["light"])
                    roll = rng.random()
                    lit["animation"] = {"circuit": circuit(name),
                                        "pattern": "failing" if roll < 0.08 else "buzz" if roll < 0.25 else "steady"}
                    children.append({"id": next_id, "parent": entity["id"], "name": entity["name"] + " light",
                                     "light": lit, "transform": light.get("transform", {})})
                    next_id += 1
        if children:
            level["entities"].append({"id": group_id, "name": GROUP + name})
            level["entities"].extend(children)
            added += len(children)
            print(f"{name}: {len(children)} things")
        if arguments.map == name:
            x0, x1, z0, z1 = rect
            for iz in range(math.floor(z1 / CELL), math.floor(z0 / CELL) - 1, -1):
                row = ""
                for ix in range(math.floor(x0 / CELL), math.floor(x1 / CELL) + 1):
                    c = (ix, iz)
                    row += "#" if grid.wall(c, floor) else "x" if c in room.taken else "." if grid.floor(c, floor) else " "
                print(row)
    LEVEL.write_text(json.dumps(level, indent=2, sort_keys=True), encoding="utf-8")
    print(f"{added} things in {len(ROOMS)} rooms")


if __name__ == "__main__":
    main()
