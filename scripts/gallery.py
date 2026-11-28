#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""What the galleries share (ADR-0122, ADR-0128): rooms 60 m apart built of boxes of the factory's materials, the
entities that stand in them, each room's shot of the tour, and the writing of the rooms' glTF, the level and the
tour, which cuts from room to room through black. --check lists where a camera passes too close to a wall or a
prop. make_effects_gallery.py and make_physics_gallery.py write their rooms with it.
"""

import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_effects_room import Mesh  # noqa: E402  (boxes with outward faces and 2 m texture repeats)


def add_face(mesh, corners, normal):
    """A flat face of three or four corners, facing along the normal, its texture 2 m a repeat across it."""
    axis = max(range(3), key=lambda i: abs(normal[i]))
    u_axis, v_axis = [a for a in range(3) if a != axis]
    start = len(mesh.positions)
    for p in corners:
        mesh.positions.append(tuple(p))
        mesh.normals.append(tuple(normal))
        mesh.uvs.append((p[u_axis] * 0.5, p[v_axis] * 0.5))
    for k in range(1, len(corners) - 1):
        i, j, m = start, start + k, start + k + 1
        pa, pb, pc = mesh.positions[i], mesh.positions[j], mesh.positions[m]
        e1 = [pb[n] - pa[n] for n in range(3)]
        e2 = [pc[n] - pa[n] for n in range(3)]
        cross = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        mesh.indices += [i, j, m] if sum(cross[n] * normal[n] for n in range(3)) >= 0 else [i, m, j]

ROOT = Path(__file__).resolve().parent.parent / "assets" / "Factory"
SPACING = 60.0
WALL = 0.3

# Room parts and the factory materials they take.
MATERIALS = {
    "floor": ("ground_1d.png", "concrete"),
    "walls": ("wall_2d.png", "concrete"),
    "ceiling": ("ceiling_2d.png", "concrete"),
    "metal": ("lift_d.png", "metal"),
    "grate": ("grid_1d.png", "metal"),
    "pipe": ("pipe_floor_d.png", "metal"),
    "wood": ("crate_1d.png", "wood"),
    "steel": ("crate_2d.png", "metal"),
    "lockers": ("lockers_d.png", "metal"),
    "vent": ("vent_slab_d.png", "metal"),
    "glass": ("shaft_glass", "glass"),
    "logs": ("crate_1d.png", "wood"),
}
# Parts of textures of their own, beside the factory's (KTX2 color, normals, occlusion-roughness-metal, relative to
# assets/Factory): the physics props' Poly Haven textures (make_physics_props.py), about 2 m a repeat as the
# rooms' texture coordinates are.
TEXTURED = {
    "rust": ("Props/textures/rust_coarse_01", "metal"),
}
# Glowing strips: the factory's white glow, tinted (linear color, strength, gives light).
GLOWS = {
    "glow_cyan": ([0.3, 0.8, 1.0], 25.0, True),
    "glow_orange": ([1.0, 0.35, 0.08], 30.0, True),
    "glow_green": ([0.35, 1.0, 0.3], 12.0, True),
    "glow_red": ([1.0, 0.08, 0.04], 15.0, False),
    "glow_ember": ([1.0, 0.3, 0.06], 4.0, False),
}

# Props of the showcase kit (CC0, Poly Haven): their boxes at rest, for --check, as [x, y, z] low and high.
PROPS = {
    "Barrel_01": ((-0.28, 0, -0.28), (0.28, 0.87, 0.28)),
    "Barrel_02": ((-0.24, 0, -0.24), (0.24, 0.88, 0.24)),
    "barrel_03": ((-0.32, 0, -0.32), (0.32, 0.93, 0.32)),
    "WetFloorSign_01": ((-0.15, 0, -0.19), (0.15, 0.63, 0.17)),
    "cement_bag": ((-0.23, 0, -0.35), (0.23, 0.18, 0.35)),
    "hand_truck": ((-0.3, 0, -0.1), (0.3, 1.4, 0.6)),
    "industrial_storage_cart": ((-0.8, 0, -0.55), (0.8, 1.38, 0.55)),
    "korean_fire_extinguisher_01": ((-0.14, 0, -0.12), (0.14, 0.66, 0.24)),
    "metal_jerrycan": ((-0.17, 0, -0.09), (0.18, 0.46, 0.08)),
    "metal_office_desk": ((-1.0, 0, -0.47), (1.0, 0.79, 0.48)),
    "metal_stool_01": ((-0.18, 0, -0.18), (0.18, 0.88, 0.18)),
    "metal_tool_chest": ((-0.37, 0, -0.2), (0.32, 0.65, 0.21)),
    "old_military_crate": ((-0.91, 0, -0.23), (0.9, 0.3, 0.31)),
    "plastic_crate_01": ((-0.15, 0, -0.2), (0.15, 0.26, 0.2)),
    "portable_generator": ((-0.41, 0, -0.28), (0.4, 0.58, 0.28)),
    "propane_tank": ((-0.17, 0, -0.17), (0.17, 0.55, 0.17)),
    "steel_frame_shelves_02": ((-0.3, 0, -0.25), (0.3, 2.14, 0.25)),
    "tool_cart": ((-0.66, 0, -0.38), (0.62, 0.96, 0.37)),
    "trashbag": ((-0.26, 0, -0.24), (0.27, 0.57, 0.22)),
    "utility_box_01": ((-0.26, 0, -0.21), (0.26, 1.12, 0.22)),
    "utility_box_02": ((-0.46, 0, -0.21), (0.46, 1.12, 0.22)),
    "wooden_crate_01": ((-0.41, 0, -0.2), (0.41, 0.34, 0.21)),
    "wooden_crate_02": ((-0.26, 0, -0.58), (0.26, 0.45, 0.58)),
    "power_box_01": ((-0.17, -0.25, -0.05), (0.18, 0.25, 0.06)),
    "modular_industrial_pipes_01": ((-0.7, -1.0, -0.13), (0.7, 0.95, 0.18)),
    "mounted_fluorescent_lights": ((-0.46, -0.04, -0.33), (0.46, 0.0, 0.32)),
    "security_light": ((-0.16, -0.36, -0.03), (0.16, 0.17, 0.39)),
    "industrial_wall_lamp": ((-0.14, -0.21, 0.0), (0.14, 0.21, 0.14)),
}


# Quaternions as [x, y, z, w].
def quat_axis(axis, degrees):
    half = math.radians(degrees) / 2.0
    n = math.sqrt(sum(a * a for a in axis))
    return [axis[0] / n * math.sin(half), axis[1] / n * math.sin(half), axis[2] / n * math.sin(half), math.cos(half)]


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def normalize(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def quat_from_to(a, b):
    """The shortest rotation of direction a onto direction b."""
    a, b = normalize(a), normalize(b)
    dot = sum(x * y for x, y in zip(a, b))
    if dot < -0.99999:
        axis = [1.0, 0.0, 0.0] if abs(a[0]) < 0.9 else [0.0, 0.0, 1.0]
        return quat_axis(axis, 180.0)
    cross = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    return normalize([cross[0], cross[1], cross[2], 1.0 + dot])


def yaw(degrees):
    return quat_axis([0, 1, 0], degrees)


def rotate(q, v):
    x, y, z, w = q
    u = [x, y, z]
    t = [2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])]
    return [v[i] + w * t[i] + (u[(i + 1) % 3] * t[(i + 2) % 3] - u[(i + 2) % 3] * t[(i + 1) % 3]) for i in range(3)]


def r(values, digits=4):
    return [round(v, digits) + 0.0 for v in values]


class Gallery:
    def __init__(self, generator):
        self.generator = generator  # The script that writes it, named in what it writes.
        self.meshes = {}  # part -> Mesh, for the level node
        self.named_meshes = {}  # node name -> (part, Mesh): nodes that emitters use
        self.entities = []
        self.boxes = []  # (low, high, what), world, for --check
        self.next_id = 2  # 1 is the rooms' model
        self.tour = []  # (room name, seconds, camera keys, events)
        self.overview = False  # Film each room from a high corner instead, to see where things go.

    def mesh(self, part):
        return self.meshes.setdefault(part, Mesh())

    def add(self, entity):
        entity["id"] = self.next_id
        self.next_id += 1
        self.entities.append(entity)
        return entity["id"]


class Room:
    """A room of the gallery: its walls about its origin, and what stands in it, in its own coordinates."""

    def __init__(self, gallery, name, index, width, depth, height, holes=(), open_roof=False, floor_part="floor",
                 wall_part="walls", without=()):
        self.g = gallery
        self.name = name
        self.ox = (index % 4) * SPACING
        self.oz = -(index // 4) * SPACING
        self.w, self.d, self.h = width, depth, height
        w, d, h = width, depth, height
        self.box(floor_part, (-w - WALL, -0.3, -d - WALL), (w + WALL, 0.0, d + WALL), "floor")
        if not open_roof:
            self.slab("ceiling", -w, w, -d, d, h, h + 0.3, holes)
        if "west" not in without:
            self.box(wall_part, (-w - WALL, 0.0, -d), (-w, h, d), "west wall")
        self.box(wall_part, (w, 0.0, -d), (w + WALL, h, d), "east wall")
        self.box(wall_part, (-w - WALL, 0.0, -d - WALL), (w + WALL, h, -d), "north wall")
        self.box(wall_part, (-w - WALL, 0.0, d), (w + WALL, h, d + WALL), "south wall")

    def world(self, p):
        return [p[0] + self.ox, p[1], p[2] + self.oz]

    def label(self, what):
        return f"{self.name}: {what}"

    # Geometry, in the room's coordinates.
    def box(self, part, low, high, what=None, node=None):
        lo, hi = self.world(low), self.world(high)
        if node:
            mesh = self.g.named_meshes.setdefault(node, (part, Mesh()))[1]
        else:
            mesh = self.g.mesh(part)
        mesh.add_box(tuple(lo), tuple(hi))
        self.g.boxes.append((lo, hi, self.label(what or part)))

    def slab(self, part, x0, x1, z0, z1, y0, y1, holes):
        xs = sorted({x0, x1} | {h[0] for h in holes} | {h[1] for h in holes})
        for xa, xb in zip(xs, xs[1:]):
            cuts = sorted((h[2], h[3]) for h in holes if h[0] <= xa and xb <= h[1])
            z = z0
            for za, zb in cuts:
                if za > z:
                    self.box(part, (xa, y0, z), (xb, y1, za), "ceiling")
                z = max(z, zb)
            if z < z1:
                self.box(part, (xa, y0, z), (xb, y1, z1), "ceiling")

    def column(self, part, x, z, size, y0, y1, what=None):
        self.box(part, (x - size / 2, y0, z - size / 2), (x + size / 2, y1, z + size / 2), what)

    def ramp(self, part, x0, x1, z_top, z_foot, height, what="ramp", base=0.0):
        """A ramp across x, from the floor at z_foot up to height at z_top, where it has a wall down to the floor;
        or from base, as if base were the floor."""
        mesh = self.g.mesh(part)
        a, b = (x0, base + height, z_top), (x1, base + height, z_top)  # Top edge.
        c, d = (x1, base, z_foot), (x0, base, z_foot)  # Foot.
        e, f = (x0, base, z_top), (x1, base, z_top)  # Under the top edge.
        rise = math.copysign(1.0, z_foot - z_top)
        slope = normalize([0.0, abs(z_foot - z_top), rise * height])
        faces = [([a, b, c, d], slope), ([e, f, b, a], [0.0, 0.0, -rise]), ([d, a, e], [-1.0, 0.0, 0.0]),
                 ([c, f, b], [1.0, 0.0, 0.0])]
        for corners, normal in faces:
            add_face(mesh, [self.world(p) for p in corners], normal)
        self.g.boxes.append((self.world((x0, base, min(z_top, z_foot))),
                             self.world((x1, base + height, max(z_top, z_foot))), self.label(what)))

    def surface_height(self, z, z_top, z_foot, height):
        """The height of a ramp's slope at z."""
        return height * (z - z_foot) / (z_top - z_foot)

    # Entities.
    def entity(self, what, position, rotation=None, scale=None, **components):
        transform = {"translation": r(self.world(position))}
        if rotation is not None:
            transform["rotation"] = r(rotation, 7)
        if scale is not None:
            transform["scale"] = r(scale)
        entity = {"name": self.label(what), "transform": transform}
        entity.update(components)
        return self.g.add(entity)

    def prop(self, model, position, turn=0.0, what=None, scale=None, body=None):
        """A prop of the showcase kit. body makes it a loose object (ADR-0125): {"mass": kg} and, if not a box,
        "shape"; a box, a cylinder or a sphere is sized from the prop's bounds."""
        rotation = yaw(turn) if turn else None
        components = {"model": f"Showcase/{model}/{model}.gltf"}
        if body is not None:
            component = dict(body)
            if component.get("shape", "box") != "hull" and model in PROPS:
                low, high = PROPS[model]
                component["size"] = r([high[i] - low[i] for i in range(3)])
                component["center"] = r([(high[i] + low[i]) / 2 for i in range(3)])
            components["body"] = component
        self.entity(what or model.replace("_", " "), position, rotation, scale, **components)
        if model in PROPS:
            low, high = PROPS[model]
            if scale:
                low = [low[i] * scale[i] for i in range(3)]
                high = [high[i] * scale[i] for i in range(3)]
            corners = [rotate(yaw(turn), [x, y, z]) for x in (low[0], high[0]) for y in (low[1], high[1])
                       for z in (low[2], high[2])]
            lo = [min(c[i] for c in corners) + position[i] for i in range(3)]
            hi = [max(c[i] for c in corners) + position[i] for i in range(3)]
            self.g.boxes.append((self.world(lo), self.world(hi), self.label(model)))

    def prefab(self, name, position, turn=0.0, what=None):
        self.entity(what or name.replace("_", " "), position, yaw(turn) if turn else None,
                    prefab=f"Prefabs/{name}.prefab.json")

    def particles(self, effect, position, up=None, burst=None, surface=None, what=None):
        component = {"effect": f"Effects/{effect}.effect.json"}
        if burst:
            component["burst"] = {"count": burst[0], "interval": burst[1]}
        if surface:
            component["surface"] = {"entity": 1, "node": surface}
        rotation = quat_from_to([0, 1, 0], up) if up else None
        return self.entity(what or effect.replace("_", " "), position, rotation, particles=component)

    def marker(self, what, position, up=None):
        """A named place for a sequence's cues, its +Y the way they spray."""
        self.entity(what, position, quat_from_to([0, 1, 0], up) if up else None)
        return self.label(what)

    def light(self, kind, position, intensity, color=(1.0, 0.9, 0.8), rng=10.0, direction=None, inner=30.0,
              outer=50.0, radius=0.05, animation=None, what="light"):
        component = {"type": kind, "intensity": intensity, "color": list(color), "range": rng, "radius": radius}
        if kind == "spot":
            component["inner_angle"] = inner
            component["outer_angle"] = outer
        if animation:
            component["animation"] = animation
        rotation = quat_from_to([0, 0, -1], direction) if direction else None
        return self.entity(what, position, rotation, light=component)

    def fog(self, center, size, density, albedo=(0.8, 0.82, 0.85), edge=0.3, falloff=None, noise=0.5,
            frequency=0.6, velocity=(0.05, 0.0, 0.02), glow=None, what="fog"):
        component = {"density": density, "albedo": list(albedo), "edge": edge, "noise": noise,
                     "noise_frequency": frequency, "noise_velocity": list(velocity)}
        if falloff:
            component["falloff_height"] = falloff
        if glow:
            component["glow"] = list(glow)
        self.entity(what, center, scale=size, fog=component)

    def sound(self, clip, position, volume=1.0, near=2.0, far=16.0, what="sound"):
        self.entity(what, position, sound={"clip": f"Sounds/Ambience/{clip}.wav", "volume": volume,
                                           "min_distance": near, "max_distance": far})

    def decal(self, texture, position, size, turn=0.0, roughness=0.8, what="decal", facing=None, depth=0.6):
        """A decal on the floor (projected down), or on a wall facing `facing` (projected against it)."""
        if facing:
            rotation = quat_from_to([0, 0, 1], facing)
        else:
            rotation = quat_mul(yaw(turn), [-0.7071068, 0.0, 0.0, 0.7071068])  # Its Z up: it projects down.
        component = {"texture": f"Decals/{texture}.png", "roughness": roughness}
        self.entity(what, position, rotation, [size, size, depth], decal=component)

    def ribbon(self, effect, position, target=None, up=None, what=None):
        component = {"effect": f"Effects/{effect}.ribbon.json"}
        target_id = None
        if target is not None:
            target_id = self.entity((what or effect) + " end", target)
            component["target"] = target_id
        rotation = quat_from_to([0, 1, 0], up) if up else None
        return self.entity(what or effect, position, rotation, ribbon=component)

    def shot(self, seconds, keys, events=()):
        """The room's part of the tour: camera keys in the room's coordinates and seconds from the cut, and
        timed cues in the same seconds."""
        if self.g.overview:
            corner = (self.w - 0.6, self.h - 0.4, self.d - 0.6)
            keys = [(0.0, corner, (-0.25 * self.w, 0.0, -0.25 * self.d)), (seconds, corner,
                                                                          (-0.25 * self.w, 0.0, -0.25 * self.d))]
        world_keys = []
        for key in keys:
            at, position, target = key[0], key[1], key[2]
            aperture = key[3] if len(key) > 3 else 0.0
            focus = key[4] if len(key) > 4 else math.dist(position, target)
            world_keys.append({"at": at, "position": r(self.world(position), 3),
                               "target": r(self.world(target), 3), "aperture": aperture, "focus": round(focus, 2)})
        self.g.tour.append((self.name, seconds, world_keys, list(events)))


def particles_cue(at, effect, entity, count):
    return {"at": at, "particles": {"effect": f"Factory/Effects/{effect}.effect.json", "entity": entity,
                                    "count": count}}


def blast_cue(at, entity, radius, strength):
    """Throws the loose objects around the entity (ADR-0127)."""
    return {"at": at, "blast": {"entity": entity, "radius": radius, "strength": strength}}


def sound_cue(at, clip, entity=None, volume=1.0):
    cue = {"clip": clip, "volume": volume}
    if entity:
        cue["entity"] = entity
    return {"at": at, "sound": cue}



# Writing -----------------------------------------------------------------------------------------------------

def write_gltf(g, path):
    factory = json.loads((ROOT / "Factory.gltf").read_text(encoding="utf-8"))
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

    by_name = {m["name"]: m for m in factory["materials"]}
    used = {part for part, mesh in g.meshes.items() if mesh.positions} | {part for part, _ in g.named_meshes.values()}
    for part, (name, surface) in MATERIALS.items():
        if part not in used:
            continue
        material = remap(json.loads(json.dumps(by_name[name])))
        material["name"] = part
        material["extras"] = {**material.get("extras", {}), "surface": surface}
        material_index[part] = len(materials)
        materials.append(material)
    for part, (prefix, surface) in TEXTURED.items():
        if part not in used:
            continue
        sources = []
        for suffix in ("d", "n", "arm"):
            images.append({"uri": f"{prefix}_{suffix}.ktx2", "mimeType": "image/ktx2"})
            textures.append({"sampler": 0, "extensions": {"KHR_texture_basisu": {"source": len(images) - 1}}})
            sources.append(len(textures) - 1)
        material_index[part] = len(materials)
        materials.append({"name": part,
                          "pbrMetallicRoughness": {"baseColorTexture": {"index": sources[0]},
                                                   "metallicRoughnessTexture": {"index": sources[2]},
                                                   "metallicFactor": 1.0, "roughnessFactor": 1.0},
                          "normalTexture": {"index": sources[1]}, "occlusionTexture": {"index": sources[2]},
                          "extras": {"surface": surface}})
    for part, (color, strength, light) in GLOWS.items():
        if part not in used:
            continue
        material = remap(json.loads(json.dumps(by_name["glow_white.tga"])))
        material["name"] = part
        material["pbrMetallicRoughness"]["baseColorFactor"] = color + [1.0]
        material["emissiveFactor"] = color
        material["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": strength}}
        material["extras"] = {"surface": "glass", **({} if light else {"light": False})}
        material_index[part] = len(materials)
        materials.append(material)

    blob = bytearray()
    views, accessors = [], []

    def add(data, count, kind, component, target, minmax=None):
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
        p = add(b"".join(struct.pack("<3f", *v) for v in mesh.positions), len(mesh.positions), "VEC3", 5126, 34962,
                ([min(v[i] for v in mesh.positions) for i in range(3)],
                 [max(v[i] for v in mesh.positions) for i in range(3)]))
        n = add(b"".join(struct.pack("<3f", *v) for v in mesh.normals), len(mesh.normals), "VEC3", 5126, 34962)
        t = add(b"".join(struct.pack("<2f", *v) for v in mesh.uvs), len(mesh.uvs), "VEC2", 5126, 34962)
        i = add(b"".join(struct.pack("<I", v) for v in mesh.indices), len(mesh.indices), "SCALAR", 5125, 34963)
        return {"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t}, "indices": i,
                "material": material_index[part]}

    solid = [part for part in g.meshes if part != "glass" and g.meshes[part].positions]
    gltf_meshes = [{"name": "rooms", "primitives": [primitive(g.meshes[part], part) for part in solid]}]
    nodes = [{"name": "rooms", "mesh": 0}]
    if "glass" in g.meshes:
        gltf_meshes.append({"name": "glass", "primitives": [primitive(g.meshes["glass"], "glass")]})
        nodes.append({"name": "glass", "mesh": len(gltf_meshes) - 1})
    for name, (part, mesh) in g.named_meshes.items():
        gltf_meshes.append({"name": name, "primitives": [primitive(mesh, part)]})
        nodes.append({"name": name, "mesh": len(gltf_meshes) - 1})
    gltf = {
        "asset": {"version": "2.0", "generator": g.generator},
        "extensionsUsed": sorted(set(factory.get("extensionsUsed", [])) - {"KHR_lights_punctual"}),
        "extensionsRequired": sorted(set(factory.get("extensionsRequired", []))),
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": gltf_meshes,
        "materials": materials,
        "textures": textures,
        "images": images,
        "samplers": factory.get("samplers", [{}]),
        "buffers": [{"uri": path.stem + ".bin", "byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    path.with_suffix(".bin").write_bytes(bytes(blob))
    path.write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")
    # The rooms are walls and floors (ADR-0117): physics, the navigation mesh, and sound, which they keep in its
    # room instead of letting every room's loops be heard in all the others.
    settings = {"comment": f"Written by {g.generator}.", "level_collision": True, "scale": 1.0}
    path.with_name(path.name + ".import.json").write_text(json.dumps(settings, indent=2, sort_keys=True) + "\n",
                                                          encoding="utf-8", newline="\n")


ENVIRONMENT = {
    "sky": {"faces": "Sky/sky1", "intensity": 9.6, "yaw": 180.0},
    "fog": {"albedo": [0.8, 0.82, 0.85], "anisotropy": 0.3},
    "exposure": {"ev100": 3.0, "auto": True, "min": 0.5, "max": 2.5},
}


def write_level(g, path, environment=None):
    """The level: the rooms' model, as entity 1, and what stands in them. path is <Name>.level.json, next to the
    <Name>.gltf of write_gltf."""
    name = path.name.removesuffix(".level.json")
    level = {"format": "daidalos-level", "version": 1, "environment": environment or ENVIRONMENT,
             "entities": [{"id": 1, "name": name, "model": name + ".gltf"}] + g.entities}
    path.write_text(json.dumps(level, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


HOLD = 1.6  # Seconds of black after each cut, while the room's effects start.
FADE_IN = 1.0
FADE_OUT = 0.9


def write_tour(g, path, comment):
    cues = [{"at": 0, "fade": {"to": 1, "seconds": 0}},
            {"at": 0, "mix": {"music": 0, "effects": 1, "ambience": 1, "interface": 1, "lowpass": 20000,
                              "seconds": 0}}]
    at = 0.0
    for name, seconds, keys, events in g.tour:
        cues.append({"at": round(at, 3), "camera": {"path": "smooth", "keys": keys}})
        cues.append({"at": round(at + HOLD, 3), "fade": {"to": 0, "seconds": FADE_IN}})
        cues.append({"at": round(at + seconds - FADE_OUT, 3), "fade": {"to": 1, "seconds": FADE_OUT}})
        for event in events:
            cue = json.loads(json.dumps(event))
            cue["at"] = round(at + event["at"], 3)
            cues.append(cue)
        at += seconds
    # Back to the player in the first room, out of the black.
    cues.append({"at": round(at + 0.8, 3), "fade": {"to": 0, "seconds": 2.0}})
    sequence = {"comment": comment, "cues": sorted(cues, key=lambda cue: cue["at"])}
    text = json.dumps(sequence, indent=1)
    path.write_text(text + "\n", encoding="utf-8", newline="\n")
    print(f"{path.name}: {len(cues)} cues, {at:.0f} s, {len(g.tour)} rooms")


def check(g):
    """Where the cameras pass closer than 0.3 m to a box, sampled along straight lines between their keys."""
    problems = 0
    for name, seconds, keys, _ in g.tour:
        for a, b in zip(keys, keys[1:]):
            for step in range(41):
                t = step / 40.0
                p = [a["position"][i] + (b["position"][i] - a["position"][i]) * t for i in range(3)]
                for low, high, what in g.boxes:
                    gap = math.sqrt(sum(max(low[i] - p[i], 0.0, p[i] - high[i]) ** 2 for i in range(3)))
                    if gap < 0.3:
                        print(f"{name}: camera at {a['at'] + (b['at'] - a['at']) * t:.1f} s is {gap:.2f} m from "
                              f"{what}")
                        problems += 1
                        break
    print(f"--check: {problems} close passes")
    return problems
