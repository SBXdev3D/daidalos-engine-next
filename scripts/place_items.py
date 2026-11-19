#!/usr/bin/env python3
"""Puts the factory's items where they would really be: resting on a desk, a shelf, a crate or the floor.

    python -I scripts/place_items.py

The items are Poly Haven's CC0 models (scripts/fetch_showcase_props.py): a medical box for health, an old gas
mask for armour, a metal ammo can for ammunition, a vintage flashlight, and a bolt-action rifle, which replaces
the shotgun. The red card keeps its model, at a keycard's size, lying flat.

Each item has a spot chosen by hand, beside or on what the room's dressing already has, and a way of lying.
The script finds the surface under the spot from the level's triangles and the props' (a ray down), lowers
the item until its lowest vertex touches it, and writes the item's model, transform and kind into
assets/Factory/Factory.level.json. The rifle leans against a wall, its butt on the floor. Each item also gets a
"glint" tag: how high above its origin the flashlight's glint shows (Factory.cpp). Run once: the level file is
then edited in the editor like any other.
"""

import json
import math
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTORY = ROOT / "assets" / "Factory"
LEVEL = FACTORY / "Factory.level.json"

MEDICAL_BOX = "Showcase/medical_box/medical_box.gltf"
GAS_MASK = "Showcase/old_gas_mask/old_gas_mask.gltf"
AMMO_CAN = "Showcase/ammo_box/ammo_box.gltf"
FLASHLIGHT = "Showcase/vintage_flashlight/vintage_flashlight.gltf"
RIFLE = "Showcase/bolt_action_rifle_7_62/bolt_action_rifle_7_62.gltf"
CARD = "Props/card.gltf"


# Quaternions as [x, y, z, w], as glTF and the level files write them.
def axis_angle(axis, degrees):
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def qrot(q, v):
    x, y, z, w = q
    cx = y * v[2] - z * v[1]
    cy = z * v[0] - x * v[2]
    cz = x * v[1] - y * v[0]
    ccx = y * cz - z * cy
    ccy = z * cx - x * cz
    ccz = x * cy - y * cx
    return [v[0] + 2 * (w * cx + ccx), v[1] + 2 * (w * cy + ccy), v[2] + 2 * (w * cz + ccz)]


UP = [0.0, 1.0, 0.0]
RIGHT = [1.0, 0.0, 0.0]


def read_accessor(gltf, buffers, index):
    accessor = gltf["accessors"][index]
    view = gltf["bufferViews"][accessor["bufferView"]]
    data = buffers[view["buffer"]]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    code = {5126: "f", 5125: "I", 5123: "H", 5121: "B"}[accessor["componentType"]]
    stride = view.get("byteStride", struct.calcsize(code) * components)
    values = [struct.unpack_from("<" + code * components, data, offset + i * stride) for i in range(accessor["count"])]
    return values if components > 1 else [value[0] for value in values]


_models = {}


def model_triangles(model):
    """The model's triangles, in its own space: [(a, b, c)]."""
    if model in _models:
        return _models[model]
    path = FACTORY / model
    gltf = json.loads(path.read_text(encoding="utf-8"))
    buffers = [(path.parent / buffer["uri"]).read_bytes() for buffer in gltf["buffers"]]
    triangles = []

    def visit(index, parent):
        node = gltf["nodes"][index]
        t = node.get("translation", [0, 0, 0])
        r = node.get("rotation", [0, 0, 0, 1])
        s = node.get("scale", [1, 1, 1])

        def transform(v):
            v = qrot(r, [v[0] * s[0], v[1] * s[1], v[2] * s[2]])
            return parent([v[0] + t[0], v[1] + t[1], v[2] + t[2]])

        if "mesh" in node:
            for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
                points = [transform(v) for v in read_accessor(gltf, buffers, primitive["attributes"]["POSITION"])]
                indices = (read_accessor(gltf, buffers, primitive["indices"]) if "indices" in primitive
                           else list(range(len(points))))
                triangles.extend((points[indices[i]], points[indices[i + 1]], points[indices[i + 2]])
                                 for i in range(0, len(indices), 3))
        for child in node.get("children", []):
            visit(child, transform)

    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        visit(root, lambda v: v)
    _models[model] = triangles
    return triangles


def model_points(model):
    return [point for triangle in model_triangles(model) for point in triangle]


class Level:
    def __init__(self):
        self.data = json.loads(LEVEL.read_text(encoding="utf-8"))
        self.by_id = {entity["id"]: entity for entity in self.data["entities"]}

    def world(self, entity):
        """The entity's world transform as a function of a point."""
        steps = []
        while entity is not None:
            transform = entity.get("transform", {})
            steps.append((transform.get("translation", [0, 0, 0]), transform.get("rotation", [0, 0, 0, 1]),
                          transform.get("scale", [1, 1, 1])))
            entity = self.by_id.get(entity.get("parent"))

        def apply(v):
            for t, r, s in steps:
                v = qrot(r, [v[0] * s[0], v[1] * s[1], v[2] * s[2]])
                v = [v[0] + t[0], v[1] + t[1], v[2] + t[2]]
            return v

        return apply

    def triangles_near(self, x, z, radius=3.0):
        """The world's triangles near a column: the level's and those of the props around it."""
        found = []
        for entity in self.data["entities"]:
            model = entity.get("model")
            if not model or entity.get("game", {}).get("kind") in KINDS:
                continue
            apply = self.world(entity)
            origin = apply([0.0, 0.0, 0.0])
            if model != "Factory.gltf" and math.hypot(origin[0] - x, origin[2] - z) > radius + 3.0:
                continue
            for a, b, c in model_triangles(model):
                if model == "Factory.gltf":
                    if min(a[0], b[0], c[0]) > x + radius or max(a[0], b[0], c[0]) < x - radius:
                        continue
                    if min(a[2], b[2], c[2]) > z + radius or max(a[2], b[2], c[2]) < z - radius:
                        continue
                found.append((apply(a), apply(b), apply(c)))
        return found

    def surface_below(self, x, z, top):
        """The height of the first surface under (x, top, z)."""
        best = None
        for a, b, c in self.triangles_near(x, z, 0.5):
            t = ray_down(a, b, c, [x, top, z])
            if t is not None and (best is None or t < best):
                best = t
        if best is None:
            raise RuntimeError(f"nothing under {x}, {z}")
        return top - best

    def next_id(self):
        return max(self.by_id) + 1


def ray_down(a, b, c, origin):
    """Distance along -y from origin to the triangle, or None (Moller-Trumbore)."""
    direction = [0.0, -1.0, 0.0]
    e1 = [b[k] - a[k] for k in range(3)]
    e2 = [c[k] - a[k] for k in range(3)]
    p = [direction[1] * e2[2] - direction[2] * e2[1], direction[2] * e2[0] - direction[0] * e2[2],
         direction[0] * e2[1] - direction[1] * e2[0]]
    det = sum(e1[k] * p[k] for k in range(3))
    if abs(det) < 1e-12:
        return None
    inv = 1.0 / det
    tv = [origin[k] - a[k] for k in range(3)]
    u = sum(tv[k] * p[k] for k in range(3)) * inv
    if u < 0.0 or u > 1.0:
        return None
    q = [tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0]]
    v = sum(direction[k] * q[k] for k in range(3)) * inv
    if v < 0.0 or u + v > 1.0:
        return None
    t = sum(e2[k] * q[k] for k in range(3)) * inv
    return t if t > 1e-5 else None


def resting(model, rotation, scale=1.0):
    """How far below its origin the model's lowest point is, and its top above it, once turned."""
    heights = [qrot(rotation, [p[0] * scale, p[1] * scale, p[2] * scale])[1] for p in model_points(model)]
    return -min(heights), max(heights)


def mask_lying():
    """The gas mask on its back, face up, the hose along +z, tilted so that the mask and the filter at the
    hose's end both rest on the floor."""
    flat = axis_angle(RIGHT, -90.0)
    points = [qrot(flat, p) for p in model_points(GAS_MASK)]
    mask = [p for p in points if p[2] < 0.3]
    filter_ = [p for p in points if p[2] > 0.75]
    mask_low = min(mask, key=lambda p: p[1])
    filter_low = min(filter_, key=lambda p: p[1])
    # Turning about x lowers the filter's end by the difference of the two lowest points.
    tilt = math.degrees(math.atan2(filter_low[1] - mask_low[1], filter_low[2] - mask_low[2]))
    return qmul(axis_angle(RIGHT, tilt), flat)


LEAN = 14.0  # Degrees from upright of a rifle leaning on a wall.


def leaning():
    """The rifle upright on its butt, barrel up, its sights to the side, leaning LEAN degrees towards +z."""
    upright = axis_angle([0.0, 0.0, 1.0], 90.0)  # The barrel, along +x, points up.
    return qmul(axis_angle(RIGHT, LEAN), upright)


def lean_against(entity, model, wall_z):
    """Moves an entity along z until its farthest point along +z touches a wall at wall_z."""
    rotation = entity["transform"]["rotation"]
    far = max(qrot(rotation, p)[2] for p in model_points(model))
    entity["transform"]["translation"][2] = round(wall_z - 0.004 - far, 4)


KINDS = {"health", "armor", "handgun_ammo", "shotgun_ammo", "rifle_ammo", "flashlight", "card", "shotgun", "rifle"}


def place(level, entity, model, x, z, yaw, lie=(0.0, 0.0, 0.0, 1.0), scale=1.0, top=None, kind=None, name=None):
    """Rests the model at (x, z), turned by yaw degrees about up after lie, on the first surface below top."""
    rotation = qmul(axis_angle(UP, yaw), list(lie))
    below, above = resting(model, rotation, scale)
    surface = level.surface_below(x, z, top if top is not None else 10.0)
    entity["model"] = model
    entity.pop("parent", None)
    entity["transform"] = {"translation": [round(x, 4), round(surface + below + 0.001, 4), round(z, 4)],
                           "rotation": [round(value, 7) for value in rotation]}
    if scale != 1.0:
        entity["transform"]["scale"] = [scale, scale, scale]
    if kind is not None:
        entity["game"]["kind"] = kind
    if name is not None:
        entity["name"] = name
    # Seven tenths of the way up the item, from its origin, which is below of its height above its lowest point.
    entity["game"]["glint"] = f"{round(0.7 * (above + below) - below, 3)}"
    print(f"{entity['name']:16} on the surface at {surface:.3f} m, ({x}, {z})")


def item(level, name):
    for entity in level.data["entities"]:
        if entity.get("name") == name:
            return entity
    raise RuntimeError(f"no entity named {name}")


def main():
    level = Level()
    side = axis_angle(RIGHT, -90.0)  # A long thing on its left side: what stood on +z faces up.

    # The east wing's loading floor: the flashlight on a shelf of the shelving by the wall, at hand height.
    place(level, item(level, "Flashlight 16"), FLASHLIGHT, 21.625, -43.2, -3.3 + 8.0, top=1.3)
    # The offices: the medical box on a desk, along it.
    place(level, item(level, "Health 17"), MEDICAL_BOX, 50.25, -29.3, -91.4 + 4.0, top=3.9)
    # The other office: an ammo can on the desk by the wall, another on the floor beside it.
    place(level, item(level, "Handgun ammo 18"), AMMO_CAN, 41.45, -49.45, 97.0, top=3.9)
    place(level, item(level, "Handgun ammo 19"), AMMO_CAN, 40.4, -50.5, 84.0, top=3.9)
    # The west rooms: the gas mask dropped on the floor along the wall.
    place(level, item(level, "Armor 20"), GAS_MASK, -18.8, -51.4, 6.0, lie=mask_lying(), top=1.0)

    # The south-west corner: the rifle leans against the wall, its butt on the floor, and its ammunition can
    # stands at its foot.
    rifle = item(level, "Shotgun 49")
    place(level, rifle, RIFLE, -16.0, -19.5, 0.0, lie=leaning(), top=1.0, kind="rifle", name="Rifle 49")
    lean_against(rifle, RIFLE, wall_z=-19.25)
    place(level, item(level, "Shotgun ammo 21"), AMMO_CAN, -15.55, -19.52, 93.0, top=1.0, kind="rifle_ammo",
          name="Rifle ammo 21")

    # The card room: the red card flat on the crate, a keycard's size.
    card = item(level, "Card 26")
    place(level, card, CARD, 50.45, -78.4, 17.0, lie=axis_angle(RIGHT, -90.0), scale=0.3, top=3.6)

    with LEVEL.open("w", encoding="utf-8", newline="\r\n") as file:
        file.write(dump(level.data) + "\n")


def dump(data):
    """As the engine writes level files: two spaces, sorted keys, arrays of numbers and strings on one line."""
    text = json.dumps(data, indent=2, sort_keys=True, ensure_ascii=False)
    return re.sub(r"\[\s+([^\[\]{}]*?)\s+\]",
                  lambda match: "[" + ", ".join(part.strip() for part in match.group(1).split(",")) + "]", text)


if __name__ == "__main__":
    main()
