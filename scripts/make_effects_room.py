# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0

"""Writes the effects test room (ADR-0053), a level of the factory's materials with a station for each effect of
the list, a courtyard where it rains under a storm, a skylight and a broken roof.

    python scripts/make_effects_room.py [assets/Factory]

It writes EffectsRoom.gltf and EffectsRoom.bin next to Factory.gltf, whose textures and effect files it uses;
Then `python scripts/gltf_to_level.py assets/Factory/EffectsRoom.gltf` turns its marker nodes into
EffectsRoom.level.json (ADR-0054), which `+sandbox.scene Factory/EffectsRoom.level.json` loads. Its stations are nodes named as the factory's are
(`particles;effect=...`, `ribbon;...`, `weather;...`, `lightning;...`, `attractor;...`, `wind;...`), so the room
also tries what the level editor will write. Meters, Y up; the player starts at the origin looking along -Z.
"""

import json
import math
import struct
import sys
from pathlib import Path

HALL = (-8.0, 8.0, -26.0, 2.0)  # x from, x to, z from, z to
HALL_HEIGHT = 5.0
YARD = (-8.0, 8.0, -40.0, -26.0)
YARD_WALLS = 4.0
WALL = 0.3
SKYLIGHT = (-6.0, -3.0, -20.0, -16.0)
SNOW_HOLE = (3.0, 6.0, -8.0, -5.0)
ASH_HOLE = (3.0, 6.0, -14.0, -11.0)
DOOR = (-1.0, 1.0, 2.5)  # x from, x to, height
WINDOW = (3.0, 7.0, 1.0, 3.0)  # x from, x to, y from, y to


def box(low, high):
    """The faces of a box, outward, with texture coordinates of one repeat per two meters."""
    positions, normals, uvs, indices = [], [], [], []
    for axis in range(3):
        for side in (0, 1):
            normal = [0.0, 0.0, 0.0]
            normal[axis] = 1.0 if side else -1.0
            u_axis, v_axis = [a for a in range(3) if a != axis]
            start = len(positions)
            for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
                point = [0.0, 0.0, 0.0]
                point[axis] = high[axis] if side else low[axis]
                point[u_axis] = high[u_axis] if u else low[u_axis]
                point[v_axis] = high[v_axis] if v else low[v_axis]
                positions.append(tuple(point))
                normals.append(tuple(normal))
                uvs.append((point[u_axis] * 0.5, point[v_axis] * 0.5))
            # Counterclockwise seen from outside: the winding depends on which side and axis.
            flip = (side == 1) == (axis == 1)
            quad = [0, 1, 2, 0, 2, 3] if not flip else [0, 2, 1, 0, 3, 2]
            indices += [start + i for i in quad]
    return positions, normals, uvs, indices


def outward(positions, normals, indices):
    """Turns triangles whose winding disagrees with their normal."""
    fixed = []
    for i in range(0, len(indices), 3):
        a, b, c = indices[i:i + 3]
        pa, pb, pc = positions[a], positions[b], positions[c]
        e1 = [pb[k] - pa[k] for k in range(3)]
        e2 = [pc[k] - pa[k] for k in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        fixed += [a, b, c] if sum(n[k] * normals[a][k] for k in range(3)) >= 0 else [a, c, b]
    return fixed


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def add_box(self, low, high):
        p, n, u, i = box(low, high)
        start = len(self.positions)
        self.positions += p
        self.normals += n
        self.uvs += u
        self.indices += [start + k for k in outward(p, n, i)]


def slab_with_holes(mesh, x0, x1, z0, z1, y0, y1, holes):
    """A slab across x and z, cut by rectangular holes along x, in strips."""
    xs = sorted({x0, x1} | {h[0] for h in holes} | {h[1] for h in holes})
    for xa, xb in zip(xs, xs[1:]):
        cuts = sorted((h[2], h[3]) for h in holes if h[0] <= xa and xb <= h[1])
        z = z0
        for za, zb in cuts:
            if za > z:
                mesh.add_box((xa, y0, z), (xb, y1, za))
            z = max(z, zb)
        if z < z1:
            mesh.add_box((xa, y0, z), (xb, y1, z1))


def build():
    floor, walls, ceiling, crates, glass = Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    hx0, hx1, hz0, hz1 = HALL
    yx0, yx1, yz0, yz1 = YARD
    # Floors of the hall and the courtyard.
    floor.add_box((hx0 - WALL, -0.3, yz0 - WALL), (hx1 + WALL, 0.0, hz1 + WALL))
    # The hall's ceiling, with the skylight and two broken patches open.
    slab_with_holes(ceiling, hx0, hx1, hz0, hz1, HALL_HEIGHT, HALL_HEIGHT + 0.3, [SKYLIGHT, SNOW_HOLE, ASH_HOLE])
    # The hall's side walls and the wall behind the start.
    walls.add_box((hx0 - WALL, 0.0, hz0), (hx0, HALL_HEIGHT, hz1))
    walls.add_box((hx1, 0.0, hz0), (hx1 + WALL, HALL_HEIGHT, hz1))
    walls.add_box((hx0, 0.0, hz1), (hx1, HALL_HEIGHT, hz1 + WALL))
    # The wall between the hall and the courtyard, with a doorway and a window.
    d0, d1, dh = DOOR
    w0, w1, wy0, wy1 = WINDOW
    z0, z1 = hz0 - WALL, hz0
    walls.add_box((hx0, 0.0, z0), (d0, HALL_HEIGHT, z1))
    walls.add_box((d0, dh, z0), (d1, HALL_HEIGHT, z1))
    walls.add_box((d1, 0.0, z0), (w0, HALL_HEIGHT, z1))
    walls.add_box((w0, 0.0, z0), (w1, wy0, z1))
    walls.add_box((w0, wy1, z0), (w1, HALL_HEIGHT, z1))
    walls.add_box((w1, 0.0, z0), (hx1, HALL_HEIGHT, z1))
    glass.add_box((w0, wy0, z0 + 0.13), (w1, wy1, z1 - 0.13))
    glass.add_box((SKYLIGHT[0], HALL_HEIGHT + 0.12, SKYLIGHT[2]), (SKYLIGHT[1], HALL_HEIGHT + 0.16, SKYLIGHT[3]))
    # The courtyard's low walls, open to the sky.
    walls.add_box((yx0 - WALL, 0.0, yz0), (yx0, YARD_WALLS, z0))
    walls.add_box((yx1, 0.0, yz0), (yx1 + WALL, YARD_WALLS, z0))
    walls.add_box((yx0 - WALL, 0.0, yz0 - WALL), (yx1 + WALL, YARD_WALLS, yz0))
    # Crates to stand effects on, and the corpse the flies keep to.
    for x, z, size in ((-6.0, -3.0, 0.8), (6.0, -9.0, 0.6)):
        crates.add_box((x - size / 2, 0.0, z - size / 2), (x + size / 2, size, z + size / 2))
    corpse = Mesh()
    corpse.add_box((5.2, 0.0, -15.6), (6.8, 0.35, -15.0))
    burning = Mesh()
    burning.add_box((-0.4, 0.0, -0.4), (0.4, 0.8, 0.4))
    return {"floor": floor, "walls": walls, "ceiling": ceiling, "crates": crates, "glass": glass,
            "corpse": corpse, "burning": burning}


def quat_from_up(direction):
    """The rotation of +Y onto a direction, as glTF's [x, y, z, w]."""
    d = [c / math.sqrt(sum(v * v for v in direction)) for c in direction]
    axis = (d[2], 0.0, -d[0])  # cross((0, 1, 0), d)
    cosine = d[1]
    if cosine < -0.9999:
        return [1.0, 0.0, 0.0, 0.0]
    w = 1.0 + cosine
    norm = math.sqrt(axis[0] ** 2 + axis[2] ** 2 + w * w)
    return [axis[0] / norm, 0.0, axis[2] / norm, w / norm]


def stations():
    """The level's named nodes: (name, translation, rotation or None, scale or None)."""
    left, right = -6.0, 6.0
    up = None
    toward_right = quat_from_up((1.0, 0.0, 0.0))
    toward_left = quat_from_up((-1.0, 0.0, 0.0))
    return [
        ("player", (0.0, 1.4, 0.0), up, None),
        ("sky;name=sky1", (0.0, 0.0, 0.0), up, None),
        # Fire and heat, on the left.
        ("particles;effect=candle", (left, 0.8, -3.0), up, None),
        ("particles;effect=fire", (left, 0.8, -6.5), up, None),
        ("particles;effect=heat_haze", (left, 1.4, -6.5), up, None),
        ("particles;effect=embers;surface=burning_crate", (left, 0.0, -6.5), up, None),
        ("particles;effect=smoke_column", (left, 0.2, -10.0), up, None),
        ("particles;effect=cigarette_smoke", (left + 1.5, 1.2, -10.0), up, None),
        ("particles;effect=explosion;burst=1:8", (left + 1.0, 1.5, -13.0), up, None),
        # Water.
        ("particles;effect=pipe_spray", (-7.8, 1.2, -16.0), toward_right, None),
        ("particles;effect=drip", (-5.0, 4.95, -22.5), up, None),
        ("particles;effect=drip", (-5.6, 4.95, -23.0), up, None),
        ("particles;effect=bubbles", (-3.0, 0.05, -23.0), up, None),
        ("particles;effect=spores", (-3.0, 0.6, -23.0), up, None),
        # Electricity, on the right.
        ("particles;effect=cable_sparks", (7.9, 2.6, -3.0), toward_left, None),
        ("ribbon;effect=arc;target=arc_end", (7.9, 1.8, -5.0), up, None),
        ("arc_end", (7.4, 1.0, -5.6), up, None),
        ("ribbon;effect=laser", (-7.9, 0.5, -1.0), toward_right, None),
        # Air.
        ("particles;effect=dust_motes", (right, 1.5, -9.0), up, None),
        ("particles;effect=ceiling_dust", (right - 1.5, 4.95, -12.0), up, None),
        ("particles;effect=ceiling_debris", (right - 1.5, 4.95, -12.0), up, None),
        ("particles;effect=papers", (0.0, 3.0, -8.0), up, None),
        ("wind;velocity=1.2,0,-0.6;gust=0.6;gust_frequency=0.3;edge=0.3", (0.0, 2.5, -8.0), up, (6.0, 5.0, 6.0)),
        ("particles;effect=breath", (0.0, 1.6, -1.0), up, None),
        # Life and the uncanny.
        ("particles;effect=flies", (right, 0.8, -15.3), up, None),
        ("attractor;effect=flies;strength=3;vortex=2;radius=2", (right, 0.7, -15.3), up, None),
        ("particles;effect=moths", (right, 3.6, -19.0), up, None),
        ("attractor;effect=moths;strength=2;vortex=4;radius=1.5", (right, 4.0, -19.0), up, None),
        # Impacts, as bursts out of the right wall's far end.
        ("particles;effect=impact_dust;burst=1:2", (7.95, 1.5, -21.5), toward_left, None),
        ("particles;effect=impact_chips;burst=6:2", (7.95, 1.5, -21.5), toward_left, None),
        ("particles;effect=impact_splinters;burst=6:2", (7.95, 1.5, -23.0), toward_left, None),
        ("particles;effect=impact_glass;burst=8:2", (7.95, 1.5, -24.5), toward_left, None),
        ("particles;effect=sparks;burst=12:2", (7.95, 2.5, -21.5), toward_left, None),
        ("particles;effect=blood;burst=20:2", (7.95, 2.5, -23.0), toward_left, None),
        ("particles;effect=shell_casings;burst=1:0.5", (5.0, 1.4, -24.0), quat_from_up((1.0, 0.7, 0.2)), None),
        ("particles;effect=tracer", (-7.9, 1.6, -25.0), toward_right, None),
        # Weather: rain in the courtyard and on the skylight, snow and ash through the broken roof.
        ("weather;effect=rain;rate=30;reach=18", (0.0, 9.0, -28.0), up, (16.6, 7.0, 28.0)),
        ("weather;effect=snow;rate=40;reach=15", ((SNOW_HOLE[0] + SNOW_HOLE[1]) / 2, 6.0, (SNOW_HOLE[2] + SNOW_HOLE[3]) / 2),
         up, (SNOW_HOLE[1] - SNOW_HOLE[0], 1.0, SNOW_HOLE[3] - SNOW_HOLE[2])),
        ("weather;effect=ash;rate=40;reach=15", ((ASH_HOLE[0] + ASH_HOLE[1]) / 2, 6.0, (ASH_HOLE[2] + ASH_HOLE[3]) / 2),
         up, (ASH_HOLE[1] - ASH_HOLE[0], 1.0, ASH_HOLE[3] - ASH_HOLE[2])),
        ("lightning;bolt=lightning;light=moon;flash=400;interval=5:12;duration=0.5", (0.0, 25.0, -90.0), up,
         (80.0, 40.0, 10.0)),
    ]


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/Factory")
    factory = json.loads((out / "Factory.gltf").read_text(encoding="utf-8"))
    # The factory's materials, with their textures and images.
    wanted = {"floor": "ground_1d.png", "walls": "wall_2d.png", "ceiling": "ceiling_2d.png", "crates": "crate_1d.png",
              "burning": "crate_2d.png", "corpse": "crate_3d.png", "glass": "shaft_glass"}
    images, textures, materials, image_map, texture_map, material_index = [], [], [], {}, {}, {}

    def copy_texture(index):
        if index in texture_map:
            return texture_map[index]
        texture = json.loads(json.dumps(factory["textures"][index]))
        for holder in [texture] + [texture.get("extensions", {}).get("KHR_texture_basisu", {})]:
            if "source" in holder:
                source = holder["source"]
                if source not in image_map:
                    image_map[source] = len(images)
                    images.append(factory["images"][source])
                holder["source"] = image_map[source]
        texture_map[index] = len(textures)
        textures.append(texture)
        return texture_map[index]

    def remap(value):
        if isinstance(value, dict):
            return {k: ({**v, "index": copy_texture(v["index"])} if k.endswith("Texture") else remap(v))
                    for k, v in value.items()}
        return value

    surfaces = {"floor": "concrete", "walls": "concrete", "ceiling": "concrete", "crates": "wood",
                "burning": "wood", "corpse": "flesh", "glass": "glass"}
    by_name = {m["name"]: m for m in factory["materials"]}
    for part, name in wanted.items():
        material = remap(json.loads(json.dumps(by_name[name])))
        material["extras"] = {"surface": surfaces[part]}
        material_index[part] = len(materials)
        materials.append(material)

    meshes = build()
    blob = bytearray()
    views, accessors = [], []

    def add(data, fmt, count, kind, component, target, minmax=None):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
        if minmax:
            accessor["min"], accessor["max"] = minmax
        accessors.append(accessor)
        return len(accessors) - 1

    def primitive(mesh, part):
        p = add(b"".join(struct.pack("<3f", *v) for v in mesh.positions), "", len(mesh.positions), "VEC3", 5126,
                34962, ([min(v[i] for v in mesh.positions) for i in range(3)],
                        [max(v[i] for v in mesh.positions) for i in range(3)]))
        n = add(b"".join(struct.pack("<3f", *v) for v in mesh.normals), "", len(mesh.normals), "VEC3", 5126, 34962)
        t = add(b"".join(struct.pack("<2f", *v) for v in mesh.uvs), "", len(mesh.uvs), "VEC2", 5126, 34962)
        i = add(b"".join(struct.pack("<I", v) for v in mesh.indices), "", len(mesh.indices), "SCALAR", 5125, 34963)
        return {"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t}, "indices": i,
                "material": material_index[part]}

    gltf_meshes = [
        {"name": "level", "primitives": [primitive(meshes[k], k) for k in ("floor", "walls", "ceiling", "crates", "corpse")]},
        {"name": "glass", "primitives": [primitive(meshes["glass"], "glass")]},
        {"name": "burning_crate", "primitives": [primitive(meshes["burning"], "burning")]},
    ]
    nodes = [{"name": "level", "mesh": 0}, {"name": "glass", "mesh": 1},
             {"name": "burning_crate", "mesh": 2, "translation": [-6.0, 0.0, -6.5]}]
    # The moon through the windows, which lightning flashes, and lamps over the stations.
    lights = [{"type": "directional", "color": [0.75, 0.8, 1.0], "intensity": 0.25, "name": "moon"}]
    nodes.append({"name": "moon", "rotation": [-0.4, 0.1, 0.05, 0.91],
                  "extensions": {"KHR_lights_punctual": {"light": 0}}})
    for x, z in ((-4.0, -4.0), (4.0, -4.0), (-4.0, -12.0), (4.0, -12.0), (-4.0, -21.0), (4.0, -21.0)):
        lights.append({"type": "spot", "color": [1.0, 0.92, 0.8], "intensity": 250, "range": 12,
                       "spot": {"innerConeAngle": 0.5, "outerConeAngle": 0.9}})
        nodes.append({"name": "lamp light", "translation": [x, HALL_HEIGHT - 0.1, z],
                      "rotation": [-0.7071067811865476, 0.0, 0.0, 0.7071067811865476],
                      "extensions": {"KHR_lights_punctual": {"light": len(lights) - 1}}})
    for name, translation, rotation, scale in stations():
        node = {"name": name, "translation": [round(v, 4) for v in translation]}
        if rotation:
            node["rotation"] = [round(v, 6) for v in rotation]
        if scale:
            node["scale"] = list(scale)
        nodes.append(node)

    used = set(factory.get("extensionsUsed", []))
    gltf = {
        "asset": {"version": "2.0", "generator": "scripts/make_effects_room.py"},
        "extensionsUsed": sorted(used),
        "extensionsRequired": sorted(set(factory.get("extensionsRequired", []))),
        "extensions": {"KHR_lights_punctual": {"lights": lights}},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": gltf_meshes,
        "materials": materials,
        "textures": textures,
        "images": images,
        "samplers": factory.get("samplers", [{}]),
        "buffers": [{"uri": "EffectsRoom.bin", "byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    (out / "EffectsRoom.bin").write_bytes(bytes(blob))
    (out / "EffectsRoom.gltf").write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
