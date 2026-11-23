#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Writes the effects gallery (ADR-0122): twelve rooms, each dressed and lit around one kind of effect, and the
camera tour that films them one after the other, fading to black between rooms.

    python scripts/make_effects_gallery.py [--check]

It writes, under assets/Factory:
- EffectsGallery.gltf and .bin: the rooms' walls, floors, ceilings and fittings, of the factory's materials,
  and its .import.json, which makes them walls and floors for physics and sound;
- EffectsGallery.level.json: the props, lights, emitters, ribbons, fog, wind, weather, sounds and decals;
- Sequences/effects_tour.sequence.json: the tour.

    +sandbox.scene Factory/EffectsGallery.level.json +sandbox.sequence Factory/Sequences/effects_tour.sequence.json

The rooms stand 60 m apart, farther than an emitter's distance (50 m), so that a room's effects start only when
the camera reaches it; the tour cuts to each room while the screen is black and holds the black a moment, so
that smoke, steam and flies are there when the picture comes back. Each room's camera and its timed events are
written here next to the room, so that moving a prop and its shot stay together; the file is the source, as
for the factory tour: changes belong here, not in the editor. --check lists where a camera passes closer than
0.3 m to a wall or a prop's box.
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_effects_room import Mesh  # noqa: E402  (boxes with outward faces and 2 m texture repeats)

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
    def __init__(self):
        self.meshes = {}  # part -> Mesh, for the level node
        self.named_meshes = {}  # node name -> (part, Mesh): nodes that emitters use
        self.entities = []
        self.boxes = []  # (low, high, what), world, for --check
        self.next_id = 2  # 1 is the rooms' model
        self.tour = []  # (room name, seconds, camera keys, events)

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

    def prop(self, model, position, turn=0.0, what=None, scale=None):
        rotation = yaw(turn) if turn else None
        self.entity(what or model.replace("_", " "), position, rotation, scale,
                    model=f"Showcase/{model}/{model}.gltf")
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


def sound_cue(at, clip, entity=None, volume=1.0):
    cue = {"clip": clip, "volume": volume}
    if entity:
        cue["entity"] = entity
    return {"at": at, "sound": cue}


# The rooms ---------------------------------------------------------------------------------------------------

def fire_room(g):
    room = Room(g, "Fire", 0, 7.0, 7.0, 6.0)
    # A sunken hearth at the back: a low brick rim around burning logs.
    for low, high in (((-1.8, 0, -4.8), (1.8, 0.45, -4.5)), ((-1.8, 0, -2.5), (1.8, 0.45, -2.2)),
                      ((-1.8, 0, -4.5), (-1.5, 0.45, -2.5)), ((1.5, 0, -4.5), (1.8, 0.45, -2.5))):
        room.box("walls", low, high, "hearth")
    # Logs, crossed in two layers.
    for z in (-4.0, -3.6, -3.2):
        room.box("logs", (-1.2, 0.0, z - 0.12), (1.2, 0.2, z + 0.12), "logs", node="fire_logs")
    for x in (-0.8, -0.2, 0.4, 1.0):
        room.box("logs", (x - 0.12, 0.2, -4.2), (x + 0.12, 0.4, -2.8), "logs", node="fire_logs")
    # A chimney hood over it, with a dark flue.
    room.box("metal", (-2.0, 3.6, -5.0), (2.0, 3.8, -2.0), "hood")
    room.box("metal", (-0.8, 3.8, -4.2), (0.8, 6.0, -2.8), "flue")
    # Pillars along the sides.
    for x in (-5.0, 5.0):
        for z in (-5.0, 0.0, 5.0):
            room.column("walls", x, z, 0.6, 0.0, 6.0, "pillar")
    for x in (-0.6, 0.6):
        room.particles("bonfire", (x, 0.35, -3.5), what=f"hearth fire {x}")
    room.particles("fire", (0.0, 0.4, -3.1), what="hearth fire front")
    room.particles("embers", (0.0, 0.0, -3.5), surface="fire_logs", what="embers on the logs")
    room.particles("embers", (0.0, 0.6, -3.5), what="embers rising")
    room.particles("heat_haze", (0.0, 1.3, -3.5))
    room.particles("hearth_smoke", (0.0, 2.2, -3.5))
    # Burning barrels on the left, a fire on the right.
    for x, z, model in ((-4.0, -1.0, "Barrel_01"), (-3.3, -2.2, "barrel_03")):
        room.prop(model, (x, 0.0, z), 20.0)
        room.particles("barrel_fire", (x, 0.88, z), what=f"barrel fire {x}")
        room.particles("embers", (x, 1.0, z), what=f"barrel embers {x}")
    room.prop("Barrel_02", (-4.6, 0.0, 0.3), 70.0)
    room.prop("wooden_crate_02", (4.2, 0.0, -2.0), 80.0)
    room.prop("wooden_crate_01", (4.3, 0.45, -2.1), 95.0)
    room.prop("trashbag", (3.6, 0.0, -0.6), 30.0)
    # Candles on a desk at the side: a quiet light against the roar.
    room.prop("metal_office_desk", (4.5, 0.0, 2.8), -90.0)
    for z in (2.2, 2.75, 3.35):
        room.particles("candle", (4.5, 0.8, z), what=f"candle {z}")
    room.decal("scorch", (0.0, 0.0, -1.6), 2.6)
    room.decal("grime", (-3.8, 0.0, -1.4), 2.5, 40.0)
    # A cold lamp at the door, so the warm fire stands out.
    room.prefab("caged_hanging_light", (-2.5, 6.0, 5.0))
    room.light("point", (0.0, 4.5, 3.0), 30.0, (0.55, 0.65, 1.0), 10.0, what="cold fill")
    room.fog((0.0, 3.0, 0.0), (14.0, 6.0, 14.0), 0.012, (0.8, 0.75, 0.7), what="haze")
    room.sound("fire", (0.0, 0.5, -3.5), 1.0, 2.0, 18.0)
    room.entity("player", (0.0, 1.4, 5.0), game={"kind": "player"})
    room.shot(17.0, [
        (0.0, (-3.6, 1.0, 6.3), (0.0, 0.9, -3.4), 2.0),
        (9.0, (-1.8, 0.8, 1.6), (0.0, 0.8, -3.4), 2.0),
        (17.5, (2.0, 1.5, 0.0), (0.0, 1.0, -3.6), 2.0),
    ])


def explosion_room(g):
    room = Room(g, "Explosion", 1, 8.0, 8.0, 7.0)
    for x in (-6.0, 6.0):
        for z in (-4.0, 2.0):
            room.column("walls", x, z, 0.7, 0.0, 7.0, "pillar")
    # Gas bottles and drums, and where they blow.
    blasts = []
    for name, (x, z) in (("A", (-3.0, -4.0)), ("B", (3.2, -3.0)), ("C", (0.0, -6.0))):
        for dx, dz in ((-0.4, 0.0), (0.4, 0.2), (0.0, -0.4)):
            room.prop("propane_tank", (x + dx, 0.0, z + dz), 30.0 * dx)
        blasts.append(room.marker(f"blast {name}", (x, 0.6, z)))
    room.prop("barrel_03", (-5.0, 0.0, -6.2))
    room.prop("Barrel_01", (-4.3, 0.0, -6.6))
    room.prop("Barrel_02", (5.2, 0.0, -6.0))
    room.prop("old_military_crate", (5.0, 0.0, -1.0), 90.0)
    room.prop("wooden_crate_02", (-6.2, 0.0, 0.5))
    room.prop("cement_bag", (-5.5, 0.0, 2.6), 60.0)
    room.prop("cement_bag", (-5.4, 0.18, 2.5), 75.0)
    room.prop("industrial_storage_cart", (5.8, 0.0, 4.0), 90.0)
    room.prefab("caged_hanging_light", (-4.0, 7.0, -2.0))
    room.prefab("caged_hanging_light", (4.0, 7.0, 2.0))
    room.light("spot", (0.0, 6.6, 3.0), 2200.0, (1.0, 0.85, 0.7), 20.0, (0.0, -0.45, -1.0), 35, 60, 0.2,
               what="wash on the back wall")
    room.light("point", (0.0, 5.0, 6.0), 40.0, (0.6, 0.7, 1.0), 14.0, what="cold fill")
    room.fog((0.0, 3.5, 0.0), (16.0, 7.0, 16.0), 0.012, what="haze")
    shake = lambda at, s: {"at": at, "shake": {"strength": s, "seconds": 1.4}}

    def blast(at, where, strength=0.7):
        return [particles_cue(at, "explosion_flare", where, 1), particles_cue(at, "explosion_fireball", where, 9),
                particles_cue(at + 0.02, "explosion_sparks", where, 160),
                particles_cue(at + 0.02, "explosion_debris", where, 24),
                particles_cue(at + 0.05, "explosion_shock", where, 70),
                particles_cue(at + 0.15, "explosion_smoke", where, 14),
                sound_cue(at, "Factory/Sounds/Ambience/explosion.wav", where, 5.0), shake(at, strength)]
    room.shot(19.0, [
        (0.0, (4.5, 1.5, 3.8), (-0.5, 1.8, -4.5)),
        (19.5, (0.5, 1.3, 4.6), (0.0, 2.0, -4.8)),
    ], blast(3.2, blasts[0]) + blast(8.2, blasts[1], 0.6) + blast(13.0, blasts[2], 0.9) + blast(13.5, blasts[0], 0.5))


def electricity_room(g):
    room = Room(g, "Electricity", 2, 8.0, 7.0, 7.0, wall_part="walls")
    # Two coil towers, crowned, with an arc between them.
    for x in (-2.6, 2.6):
        room.column("metal", x, -3.0, 1.4, 0.0, 0.6, "tower base")
        room.column("steel", x, -3.0, 0.5, 0.6, 3.1, "tower")
        for y in (1.2, 1.7, 2.2, 2.7):
            room.column("pipe", x, -3.0, 0.75, y, y + 0.12, "coil ring")
        room.column("metal", x, -3.0, 1.0, 3.1, 3.35, "crown")
        room.box("glow_cyan", (x - 0.3, 3.35, -3.3), (x + 0.3, 3.4, -2.7), "crown glow")
    room.ribbon("arc", (-2.2, 3.3, -3.0), target=(2.2, 3.3, -3.0), what="tower arc")
    room.ribbon("arc", (-2.6, 3.4, -3.0), target=(-2.9, 5.2, -4.2), what="arc to the ceiling")
    room.ribbon("arc", (2.6, 1.3, -2.6), target=(3.6, 0.0, -1.8), what="arc to the floor")
    room.light("point", (0.0, 3.3, -3.0), 90.0, (0.55, 0.7, 1.0), 12.0,
               animation={"pattern": "flicker", "rate": 14, "depth": 0.6}, what="arc light")
    # Lightning that strikes between them, now and then. A flash multiplies its light, so the light is never
    # quite off.
    flash = room.light("point", (0.0, 5.5, -3.0), 1.0, (0.75, 0.8, 1.0), 16.0, what="strike light")
    room.entity("lightning", (0.0, 3.5, -3.0), scale=(1.5, 6.5, 1.5),
                lightning={"bolt": "Effects/lightning_indoor.ribbon.json", "flash": 900, "interval": [2.5, 5.0],
                           "duration": 0.35, "light": flash})
    # The cabinets along the back wall, a cable that sparks.
    room.box("lockers", (-7.9, 0.0, -6.9), (-4.0, 2.4, -6.3), "cabinets")
    room.box("lockers", (4.0, 0.0, -6.9), (7.9, 2.4, -6.3), "cabinets")
    room.prop("utility_box_02", (-1.5, 0.0, -6.6))
    room.prop("utility_box_01", (0.0, 0.0, -6.6))
    room.prop("utility_box_02", (1.5, 0.0, -6.6))
    room.prop("portable_generator", (5.0, 0.0, -1.0), -30.0)
    room.prop("tool_cart", (-5.5, 0.0, 0.5), 80.0)
    for z in (-1.5, 1.5):
        room.prop("power_box_01", (7.94, 2.3, z), -90.0)
    room.particles("cable_sparks", (7.85, 2.0, -1.5), up=(-1.0, -0.3, 0.0), what="sparking box")
    spark_box = room.marker("box sparks", (7.85, 2.2, 1.5), up=(-1.0, 0.2, 0.0))
    for x in (-4.0, 0.0, 4.0):
        room.prop("mounted_fluorescent_lights", (x, 6.95, 1.5))
    room.light("spot", (-4.0, 6.85, 1.5), 260.0, (0.85, 0.95, 1.0), 12.0, (0, -1, 0), 50, 75,
               animation={"pattern": "failing", "rate": 1.0, "depth": 0.9}, what="failing tube")
    room.light("spot", (4.0, 6.85, 1.5), 220.0, (0.85, 0.95, 1.0), 12.0, (0, -1, 0), 50, 75,
               animation={"pattern": "buzz", "rate": 1.0, "depth": 0.2}, what="buzzing tube")
    room.fog((0.0, 3.5, -1.0), (16.0, 7.0, 14.0), 0.03, (0.75, 0.82, 1.0), what="haze")
    room.decal("scorch", (3.6, 0.0, -1.8), 1.6)
    room.sound("hum", (0.0, 2.0, -3.0), 1.0, 2.0, 16.0)
    events = []
    for at in (5.5, 11.0, 14.2):
        events += [particles_cue(at, "sparks", spark_box, 90), particles_cue(at, "explosion_flare", spark_box, 1),
                   sound_cue(at, "Factory/Sounds/Ambience/zap.wav", spark_box, 1.5),
                   {"at": at, "shake": {"strength": 0.2, "seconds": 0.4}}]
    room.shot(17.0, [
        (0.0, (-1.2, 0.7, 4.5), (0.0, 3.2, -3.0), 2.8),
        (8.5, (0.8, 1.2, 1.2), (0.0, 3.2, -3.0), 2.8),
        (17.5, (4.5, 2.0, 2.5), (1.8, 2.4, -2.6), 2.8),
    ], events)


def rain_room(g):
    room = Room(g, "Rain", 3, 8.0, 9.0, 7.0, open_roof=True)
    # A porch over the camera's side, on pillars.
    room.box("ceiling", (-8.0, 3.2, 3.8), (8.0, 3.45, 9.0), "porch roof")
    for x in (-4.2, 4.2):
        room.column("walls", x, 3.95, 0.35, 0.0, 3.2, "porch pillar")
    # A lit door at the back, the light that shows the rain.
    room.box("metal", (-1.2, 0.0, -8.95), (1.2, 2.6, -8.85), "back door")
    room.prop("security_light", (0.0, 3.4, -8.95), 0.0)
    room.light("spot", (0.0, 3.2, -8.6), 1400.0, (1.0, 0.85, 0.65), 22.0, (0.0, -0.55, 1.0), 25, 55, 0.1,
               what="door lamp")
    room.light("spot", (-6.5, 6.5, -8.6), 900.0, (0.7, 0.8, 1.0), 24.0, (0.35, -0.6, 1.0), 20, 45, 0.1,
               what="yard lamp")
    room.prefab("hanging_industrial_lamp", (2.5, 3.2, 6.0))
    for x, z, model, turn in ((-5.0, -6.5, "Barrel_01", 0), (-4.4, -7.2, "Barrel_02", 30),
                              (5.5, -5.0, "trashbag", 10), (6.0, -5.6, "trashbag", 80), (4.5, -7.5, "hand_truck", 200),
                              (-6.5, 2.0, "wooden_crate_02", 5), (2.0, 5.5, "WetFloorSign_01", -20)):
        room.prop(model, (x, 0.0, z), turn)
    room.entity("rain", (0.0, 10.0, -2.6), scale=(16.0, 6.0, 12.8),
                weather={"effect": "Effects/rain.effect.json", "rate": 45, "reach": 20})
    room.entity("wet", (0.0, 3.0, 0.0), scale=(16.6, 8.0, 18.6),
                wet={"wetness": 0.9, "puddles": 0.6, "puddle_height": 1.5, "rain": 60, "streaks": 0.7})
    room.particles("drip_edge", (0.0, 3.18, 3.75), what="drips off the porch")
    room.particles("drip", (-2.0, 3.18, 5.0), what="drip under the porch")
    # The storm flashes a light of its own over the yard: flashing the moon would light every room of the level,
    # and the denoisers would show it in each of them as a burst of noise.
    flash = room.light("spot", (0.0, 20.0, 1.0), 10.0, (0.75, 0.8, 1.0), 32.0, (0.0, -1.0, -0.45), 50, 75, 1.0,
                       what="lightning flash")
    room.entity("lightning", (0.0, 30.0, -60.0), scale=(80.0, 30.0, 10.0),
                lightning={"bolt": "Effects/lightning.ribbon.json", "flash": 3000, "interval": [4.0, 8.0],
                           "duration": 0.5, "light": flash})
    room.fog((0.0, 3.5, -2.5), (16.0, 7.0, 13.0), 0.04, (0.75, 0.8, 0.85), what="rain haze")
    room.sound("rain", (0.0, 1.5, 0.0), 1.0, 4.0, 22.0)
    room.shot(17.0, [
        (0.0, (-3.0, 1.6, 7.6), (1.0, 2.0, -6.0), 2.8),
        (9.0, (-0.8, 1.5, 5.6), (0.0, 2.2, -7.0), 2.8),
        (17.5, (0.8, 1.2, 4.4), (0.0, 3.5, -7.5), 2.8),
    ])


def snow_room(g):
    hole = (-2.8, 2.8, -4.0, 0.5)
    room = Room(g, "Snow", 4, 7.0, 7.0, 7.0, holes=[hole, (3.5, 5.0, 2.0, 3.5)])
    # Broken edges around the hole: a beam that hangs, slabs that fell.
    room.box("steel", (-3.2, 6.6, -1.8), (3.2, 7.0, -1.5), "beam")
    room.box("ceiling", (-1.4, 0.0, -2.6), (0.3, 0.25, -1.2), "fallen slab")
    room.box("ceiling", (0.6, 0.0, -1.0), (1.8, 0.35, 0.2), "fallen slab")
    for model, x, z, turn in (("old_military_crate", -5.0, -5.5, 0), ("wooden_crate_02", 5.2, -5.0, 10),
                              ("wooden_crate_01", 5.0, -5.2, -10), ("steel_frame_shelves_02", -6.4, 1.0, 90),
                              ("steel_frame_shelves_02", -6.4, 2.2, 90), ("Barrel_02", 4.5, 3.8, 0),
                              ("hand_truck", -3.5, 4.5, 140)):
        room.prop(model, (x, 0.0, z), turn)
    room.entity("snow", (0.0, 8.5, -1.75), scale=(5.6, 1.0, 4.5),
                weather={"effect": "Effects/snow.effect.json", "rate": 90, "reach": 20})
    room.entity("snow 2", (4.25, 8.5, 2.75), scale=(1.5, 1.0, 1.5),
                weather={"effect": "Effects/snow.effect.json", "rate": 60, "reach": 20})
    room.entity("draft", (0.0, 3.5, -1.0), scale=(10.0, 7.0, 10.0),
                wind={"velocity": [0.5, 0.0, 0.3], "gust": 0.8, "gust_frequency": 0.25, "edge": 0.4})
    room.particles("breath", (-4.6, 1.6, 4.0), what="cold breath")  # Someone stands in the dark corner.
    room.fog((0.0, 3.5, -1.0), (14.0, 7.0, 14.0), 0.07, (0.85, 0.9, 1.0), noise=0.6, what="cold air")
    room.prefab("caged_hanging_light", (-5.0, 7.0, 4.5))
    room.light("spot", (0.0, 9.0, -1.75), 4000.0, (0.6, 0.72, 1.0), 16.0, (0.1, -1.0, 0.05), 20, 32, 0.3,
               what="moonlight through the hole")
    room.light("point", (-5.0, 2.5, 4.5), 20.0, (1.0, 0.75, 0.5), 7.0, what="warm corner")
    room.sound("wind", (0.0, 5.0, -1.5), 0.6, 3.0, 16.0)
    room.shot(17.0, [
        (0.0, (2.5, 1.0, 5.5), (-0.3, 6.5, -1.8), 2.0),
        (8.0, (1.6, 1.2, 4.4), (-0.3, 3.0, -1.8), 2.0),
        (17.5, (0.8, 0.9, 3.2), (-0.4, 0.2, -1.8), 2.0),
    ])


def wind_room(g):
    room = Room(g, "Wind", 5, 13.0, 3.5, 5.0)
    # A fan at the east end, and a light beside it that shines down the tunnel through the dust.
    room.entity("fan", (10.9, 2.5, 0.0), [0.0, 0.0, 0.7071068, 0.7071068], model="fan.gltf", game={"kind": "fan"})
    room.box("vent", (12.6, 0.0, -3.5), (13.0, 0.7, 3.5), "fan housing")
    room.box("vent", (12.6, 4.3, -3.5), (13.0, 5.0, 3.5), "fan housing")
    room.box("vent", (12.6, 0.7, -3.5), (13.0, 4.3, -1.8), "fan housing")
    room.box("vent", (12.6, 0.7, 1.8), (13.0, 4.3, 3.5), "fan housing")
    room.light("spot", (10.2, 4.6, 2.6), 3500.0, (1.0, 0.9, 0.75), 30.0, (-1.0, -0.22, -0.12), 18, 34, 0.15,
               what="light down the tunnel")
    for x in (-8.0, -2.0, 4.0):
        room.prefab("caged_hanging_light", (x, 5.0, 0.0))
    room.fog((0.0, 2.5, 0.0), (26.0, 5.0, 7.0), 0.025, (0.85, 0.85, 0.85), noise=0.7, frequency=0.5,
             velocity=(-1.5, 0.0, 0.0), what="dust in the air")
    room.entity("gale", (0.0, 2.5, 0.0), scale=(26.0, 5.0, 7.0),
                wind={"velocity": [-7.0, 0.0, 0.0], "gust": 0.6, "gust_frequency": 0.35, "edge": 0.2})
    room.particles("wind_dust", (9.0, 2.5, 0.0))
    room.particles("wind_ash", (9.0, 2.5, 0.0))
    room.particles("papers", (8.5, 0.8, 0.0), what="papers blown")
    room.particles("papers", (4.0, 0.3, 1.5), what="papers on the floor")
    room.particles("papers", (6.0, 2.5, -1.5), what="papers in the air")
    room.particles("wind_wisps", (9.0, 2.5, 0.0))
    room.particles("dust_motes", (6.0, 2.0, 0.0))
    # Pipes along the walls, grates on the floor.
    for z, turn in ((-3.35, 0.0), (3.35, 180.0)):
        for x in (-9.0, -4.0, 1.0, 6.0):
            room.prop("modular_industrial_pipes_01", (x, 2.2, z), turn)
    for x in range(-11, 11, 3):
        room.box("grate", (x, 0.0, -1.0), (x + 2.0, 0.02, 1.0), "grate")
    room.prop("cement_bag", (-6.0, 0.0, -2.6), 20.0)
    room.prop("plastic_crate_01", (-3.0, 0.0, 2.8), 10.0)
    room.prop("trashbag", (2.0, 0.0, -2.8), 50.0)
    room.light("point", (-10.0, 4.5, 0.0), 25.0, (0.6, 0.7, 1.0), 10.0, what="cold fill")
    room.sound("wind", (4.0, 2.5, 0.0), 1.0, 3.0, 20.0)
    room.shot(17.0, [
        (0.0, (-7.0, 1.6, 1.8), (10.0, 2.4, 0.0), 2.8),
        (17.5, (1.0, 1.2, -1.6), (10.0, 2.6, 0.5), 2.8),
    ])


def fog_room(g):
    room = Room(g, "Fog", 6, 8.0, 8.0, 6.0, without=("west",))
    # The west wall is slats, with a strong light in a closet behind them: beams through the fog.
    room.box("walls", (-11.0, 0.0, -8.3), (-8.3, 6.0, -8.0), "closet")
    room.box("walls", (-11.0, 0.0, 8.0), (-8.3, 6.0, 8.3), "closet")
    room.box("walls", (-11.3, 0.0, -8.3), (-11.0, 6.0, 8.3), "closet")
    room.box("ceiling", (-11.3, 6.0, -8.3), (-8.0, 6.3, 8.3), "closet roof")
    room.box("floor", (-11.3, -0.3, -8.3), (-8.0, 0.0, 8.3), "closet floor")
    for z in [-7.6 + 0.9 * i for i in range(18)]:
        room.box("wood", (-8.3, 0.0, z), (-8.0, 6.0, z + 0.45), "slat")
    room.light("spot", (-10.6, 5.0, 0.0), 9000.0, (1.0, 0.95, 0.85), 30.0, (1.0, -0.45, 0.0), 24, 40, 0.2,
               what="light behind the slats")
    room.fog((0.0, 3.0, 0.0), (16.0, 6.0, 16.0), 0.012, (0.85, 0.87, 0.9), noise=0.6, what="room fog")
    room.fog((0.0, 0.6, 0.0), (16.0, 1.2, 16.0), 0.35, (0.9, 0.9, 0.92), edge=0.15, falloff=0.3, noise=0.8,
             frequency=0.4, velocity=(0.15, 0.0, 0.05), what="ground fog")
    # A drum leaking a glowing gas in the far corner.
    room.prop("Barrel_02", (5.5, 0.0, -5.5), 15.0)
    room.prop("Barrel_01", (6.2, 0.0, -4.8), 40.0)
    room.fog((5.5, 1.0, -5.0), (3.0, 2.0, 3.0), 0.6, (0.4, 1.0, 0.4), edge=0.6, falloff=0.8, noise=0.8,
             frequency=1.2, velocity=(0.0, 0.1, 0.1), glow=(0.15, 1.2, 0.2), what="glowing gas")
    room.particles("spores", (5.5, 1.0, -5.0))
    room.light("point", (5.5, 1.2, -5.0), 12.0, (0.3, 1.0, 0.35), 6.0, what="gas glow")
    for model, x, z, turn in (("old_military_crate", 3.0, 3.0, 30), ("wooden_crate_02", -3.0, 5.5, 90),
                              ("steel_frame_shelves_02", 7.4, 2.0, -90), ("steel_frame_shelves_02", 7.4, 3.2, -90),
                              ("metal_stool_01", -2.0, -2.0, 0)):
        room.prop(model, (x, 0.0, z), turn)
    # A wisp drifting through it.
    room.ribbon("wisp", (2.0, 1.4, -3.0), what="wisp")
    room.sound("wind", (0.0, 3.0, 0.0), 0.35, 3.0, 16.0)
    path = [(2.0, 1.4, -3.0), (-1.0, 1.1, -4.5), (-3.5, 1.6, -2.0), (-2.0, 1.2, 1.5), (1.0, 1.7, 0.5),
            (3.0, 1.3, -2.5), (0.0, 1.0, -5.0)]
    events = [{"at": 0.0, "move": {"entity": room.label("wisp"), "to": r(room.world(path[0])), "seconds": 0.0}}]
    for index, point in enumerate(path[1:]):
        events.append({"at": 1.0 + 2.5 * index, "move": {"entity": room.label("wisp"), "to": r(room.world(point)),
                                                          "seconds": 2.5}})
    room.shot(17.0, [
        (0.0, (5.0, 1.0, 6.0), (-8.0, 2.2, -1.0), 2.0),
        (17.5, (-1.5, 0.8, 5.5), (-8.0, 2.6, -3.0), 2.0),
    ], events)


def cryo_room(g):
    room = Room(g, "Cryo", 7, 7.0, 7.0, 5.0, wall_part="walls")
    # A tank of liquid nitrogen in the corner, piped along the back wall to two valves.
    room.column("metal", -4.6, -4.6, 1.8, 0.0, 3.4, "tank")
    room.column("steel", -4.6, -4.6, 1.4, 3.4, 3.8, "tank cap")
    room.box("pipe", (-3.7, 2.5, -6.95), (5.0, 2.7, -6.75), "pipe")
    room.box("pipe", (-3.7, 2.5, -6.95), (-3.5, 2.7, -4.6), "pipe")
    room.box("pipe", (1.4, 1.2, -6.95), (1.6, 2.5, -6.75), "pipe down")
    room.box("steel", (1.2, 1.0, -6.95), (1.8, 1.25, -6.6), "valve")
    room.box("pipe", (4.8, 0.8, -6.95), (5.0, 2.5, -6.75), "pipe down")
    room.box("steel", (4.6, 0.6, -6.95), (5.2, 0.85, -6.6), "valve")
    # A strip of cold light along the walls.
    room.box("glow_cyan", (-6.98, 4.2, -6.0), (-6.94, 4.3, 6.0), "light strip")
    room.box("glow_cyan", (6.94, 4.2, -6.0), (6.98, 4.3, 6.0), "light strip")
    room.box("lockers", (6.4, 0.0, -2.0), (6.95, 2.2, 3.0), "lockers")
    room.particles("cryo_jet", (1.5, 1.1, -6.55), up=(0.15, -0.1, 1.0), what="leaking valve")
    room.particles("frost_glitter", (1.5, 1.2, -5.0), what="frost by the valve")
    room.particles("cryo_vapour", (1.5, 0.3, -4.5), what="vapour under the jet")
    valve = room.marker("second valve", (4.9, 0.75, -6.55), up=(-0.3, 0.05, 1.0))
    # A bench with an open dewar that pours over its edge.
    room.prop("metal_office_desk", (-2.0, 0.0, -1.0), 0.0)
    room.prop("propane_tank", (-2.3, 0.79, -1.1), 0.0, what="dewar")
    room.particles("cryo_vapour", (-2.3, 1.36, -1.1), what="vapour from the dewar")
    room.particles("cryo_vapour", (-1.6, 0.82, -0.55), what="vapour over the edge")
    room.particles("frost_glitter", (-2.0, 1.2, -0.8), what="frost over the bench")
    room.fog((0.0, 0.3, 0.0), (14.0, 0.6, 14.0), 0.45, (0.9, 0.95, 1.0), edge=0.15, falloff=0.2, noise=0.8,
             frequency=0.6, velocity=(0.1, 0.0, 0.15), what="cold layer")
    room.fog((0.0, 2.5, 0.0), (14.0, 5.0, 14.0), 0.01, (0.85, 0.92, 1.0), what="cold air")
    for x in (-3.0, 3.0):
        room.prop("mounted_fluorescent_lights", (x, 4.95, 1.0))
        room.light("spot", (x, 4.85, 1.0), 200.0, (0.8, 0.92, 1.0), 10.0, (0, -1, 0), 50, 75, what="tube")
    room.light("spot", (-2.0, 4.8, -6.6), 700.0, (0.7, 0.9, 1.0), 14.0, (0.0, -1.0, 1.3), 25, 45, 0.1,
               what="backlight on the bench")
    room.light("spot", (4.5, 4.8, -2.0), 500.0, (0.7, 0.9, 1.0), 14.0, (-0.6, -1.0, -1.0), 25, 45, 0.1,
               what="light on the jet")
    room.prop("metal_tool_chest", (4.5, 0.0, 4.5), 180.0)
    room.prop("plastic_crate_01", (-5.5, 0.0, 3.0), 20.0)
    room.sound("cryo", (1.5, 1.0, -5.5), 1.0, 2.0, 15.0)
    events = []
    for at in (4.5, 10.0):
        events += [particles_cue(at, "cryo_jet", valve, 60), particles_cue(at + 0.3, "cryo_jet", valve, 60),
                   particles_cue(at + 0.6, "cryo_jet", valve, 60),
                   sound_cue(at, "Factory/Sounds/Ambience/cryo.wav", valve, 0.8)]
    room.shot(17.0, [
        (0.0, (5.5, 0.6, 3.0), (1.0, 0.8, -5.0), 2.0),
        (8.5, (2.0, 0.9, 1.5), (-1.0, 0.8, -3.0), 2.0),
        (17.5, (-4.5, 1.4, 2.5), (-1.5, 0.9, -1.5), 2.0),
    ], events)


def water_room(g):
    room = Room(g, "Water", 8, 7.0, 7.0, 5.5)
    # Pipes under the ceiling, one burst.
    for z in (-3.0, -1.5):
        room.box("pipe", (-7.0, 4.6, z), (7.0, 4.9, z + 0.3), "ceiling pipe")
    room.box("steel", (0.6, 4.5, -3.1), (1.0, 5.0, -2.6), "pipe flange")
    room.particles("pipe_spray", (1.2, 4.6, -2.85), up=(0.6, -0.5, 0.4), what="burst pipe")
    for x, z in ((-3.0, -1.4), (-1.0, -1.35), (3.5, -2.9), (5.0, -1.4), (-4.5, -2.9)):
        room.particles("drip", (x, 4.58, z), what=f"drip {x}")
    # A drain pit with a grate, bubbling.
    room.box("grate", (-2.6, 0.0, 1.0), (-1.0, 0.03, 2.6), "drain grate")
    room.particles("bubbles", (-1.8, 0.03, 1.8))
    room.entity("wet", (0.0, 2.0, 0.0), scale=(14.0, 4.0, 14.0),
                wet={"wetness": 1.0, "puddles": 0.9, "puddle_height": 1.5, "rain": 0, "streaks": 0.8})
    for model, x, z, turn in (("WetFloorSign_01", 2.5, 2.5, 30), ("portable_generator", -5.0, -5.0, 20),
                              ("utility_box_02", 3.0, -6.6, 0), ("utility_box_01", 4.2, -6.6, 0),
                              ("metal_jerrycan", -4.4, -4.2, 70), ("Barrel_02", 5.8, 4.5, 0)):
        room.prop(model, (x, 0.0, z), turn)
    room.prefab("caged_hanging_light", (-5.0, 5.5, -5.0))
    room.light("point", (-6.5, 2.5, 5.5), 25.0, (0.3, 1.0, 0.5), 9.0, what="green exit light")
    room.box("glow_green", (-6.95, 2.3, 5.2), (-6.9, 2.6, 5.8), "exit sign")
    room.fog((0.0, 2.75, 0.0), (14.0, 5.5, 14.0), 0.01, what="damp air")
    room.light("spot", (6.5, 1.2, 2.0), 500.0, (0.9, 0.95, 1.0), 16.0, (-1.0, -0.12, -0.3), 25, 45, 0.1,
               what="light across the puddles")
    room.light("spot", (4.0, 4.4, -6.0), 1200.0, (0.8, 0.9, 1.0), 14.0, (-0.8, -0.4, 1.0), 20, 38, 0.1,
               what="light behind the spray")
    room.sound("water", (1.2, 3.0, -2.5), 1.0, 2.0, 16.0)
    room.shot(17.0, [
        (0.0, (-4.0, 0.4, 4.0), (1.2, 2.2, -2.6), 2.0),
        (17.5, (-1.0, 0.7, 1.6), (1.4, 3.4, -2.7), 2.0),
    ])


def steam_room(g):
    room = Room(g, "Steam", 9, 8.0, 7.0, 6.0)
    # Boilers along the back, a furnace mouth glowing between them.
    for x in (-5.0, 5.0):
        room.box("metal", (x - 1.6, 0.0, -6.9), (x + 1.6, 3.6, -4.8), "boiler")
        room.box("pipe", (x - 0.15, 3.6, -6.0), (x + 0.15, 6.0, -5.7), "boiler flue")
    room.box("walls", (-2.0, 0.0, -7.0), (2.0, 2.4, -6.0), "furnace")
    room.box("glow_ember", (-1.1, 0.3, -6.02), (1.1, 1.3, -5.98), "furnace mouth")
    room.light("point", (0.0, 0.9, -5.4), 160.0, (1.0, 0.45, 0.15), 12.0,
               animation={"pattern": "flicker", "rate": 3, "depth": 0.25}, what="furnace light")
    room.particles("smoke_column", (0.0, 2.5, -6.2), what="furnace smoke")
    room.particles("heat_haze", (0.0, 1.4, -5.8))
    # Vents in the floor that blow in turns.
    for index, x in enumerate((-4.0, 0.0, 4.0)):
        room.box("grate", (x - 0.6, 0.0, -1.6), (x + 0.6, 0.03, -0.4), "vent grate")
        room.particles("steam_vent", (x, 0.05, -1.0), what=f"vent {index}")
        room.particles("steam_vent", (x, 0.05, -1.0), burst=(50, 2.4 + 0.7 * index), what=f"vent burst {index}")
        room.particles("pipe_mist", (x, 0.1, -1.0), what=f"vent mist {index}")
    # Leaks on the boilers' pipes.
    room.particles("smoke_leak", (-3.4, 2.8, -4.7), up=(0.0, 0.0, 1.0), what="leak left")
    room.particles("smoke_leak", (5.0, 4.5, -5.6), what="leak on the flue")
    room.box("pipe", (-7.9, 3.2, 2.0), (7.9, 3.45, 2.25), "pipe")
    room.particles("smoke_leak", (2.5, 3.2, 2.12), up=(0.0, -1.0, 0.0), what="leak above")
    # A desk with a cigarette left burning.
    room.prop("metal_office_desk", (5.5, 0.0, 4.5), 180.0)
    room.prop("metal_stool_01", (5.0, 0.0, 3.4))
    room.particles("cigarette_smoke", (5.2, 0.8, 4.6))
    room.prop("tool_cart", (-5.5, 0.0, 3.5), 60.0)
    room.prop("korean_fire_extinguisher_01", (-7.6, 0.0, -2.0), 90.0)
    room.prefab("caged_hanging_light", (-3.0, 6.0, 3.5))
    room.light("spot", (0.0, 5.9, 2.0), 500.0, (0.85, 0.9, 1.0), 14.0, (0.0, -1.0, -0.7), 25, 45, what="cool spot")
    room.fog((0.0, 3.0, 0.0), (16.0, 6.0, 14.0), 0.04, (0.9, 0.9, 0.9), what="steamy air")
    room.sound("steam", (0.0, 1.0, -1.0), 1.0, 2.0, 16.0)
    room.shot(17.0, [
        (0.0, (-6.0, 1.2, 5.5), (0.0, 1.5, -3.0), 2.0),
        (17.5, (2.5, 1.0, 4.5), (-1.5, 1.8, -3.5), 2.0),
    ])


def creatures_room(g):
    room = Room(g, "Creatures", 10, 7.0, 7.0, 5.0)
    room.box("lockers", (-6.95, 0.0, -6.5), (-6.4, 2.2, 0.0), "lockers")
    # A slab with something on it, and the flies it draws.
    room.box("steel", (1.0, 0.0, -3.2), (3.4, 0.85, -2.0), "slab")
    room.prop("trashbag", (2.2, 0.85, -2.6), 90.0, "body bag", (1.0, 0.45, 3.0))
    room.particles("flies_swarm", (2.2, 1.3, -2.6))
    room.entity("flies' pull", (2.2, 1.2, -2.6),
                attractor={"effect": "Effects/flies_swarm.effect.json", "strength": 3, "vortex": 2, "radius": 1.2})
    room.decal("blood_splatter", (2.0, 0.0, -1.6), 1.6, 30.0, 0.3)
    room.decal("blood_drips", (2.3, 0.0, -2.6), 1.0)
    flies_at = room.marker("flies", (2.2, 1.3, -2.6))
    # A lamp, and moths around it.
    room.prefab("hanging_industrial_lamp", (-1.5, 3.9, 0.5))
    room.particles("moths_swarm", (-1.5, 2.6, 0.5))
    room.entity("moths' pull", (-1.5, 2.7, 0.5),
                attractor={"effect": "Effects/moths_swarm.effect.json", "strength": 2, "vortex": 4, "radius": 1.0})
    room.light("spot", (2.2, 4.9, -0.5), 600.0, (1.0, 0.9, 0.75), 10.0, (0.0, -1.0, -0.5), 20, 38, 0.1,
               what="light on the slab")
    moths_at = room.marker("moths", (-1.5, 2.6, 0.5))
    # A rotten corner, green and full of spores.
    room.prop("trashbag", (5.5, 0.0, 5.0), 20.0)
    room.prop("trashbag", (6.0, 0.0, 4.3), 80.0)
    room.decal("mold", (5.5, 0.0, 4.8), 3.0)
    room.particles("spores", (5.5, 0.8, 4.8))
    room.fog((5.5, 0.8, 4.8), (2.6, 1.6, 2.6), 0.4, (0.5, 1.0, 0.4), edge=0.6, falloff=0.6, glow=(0.08, 0.5, 0.1),
             what="rot")
    room.light("point", (5.5, 1.0, 4.8), 8.0, (0.4, 1.0, 0.3), 5.0, what="rot glow")
    room.ribbon("wisp", (-4.0, 1.5, 4.0), what="wisp")
    room.prop("metal_tool_chest", (-5.8, 0.0, 3.0), 90.0)
    room.prop("hand_truck", (4.5, 0.0, -5.5), 200.0)
    room.fog((0.0, 2.5, 0.0), (14.0, 5.0, 14.0), 0.012, what="stale air")
    room.sound("flies", (2.2, 1.3, -2.6), 1.0, 1.0, 10.0)
    path = [(-4.0, 1.5, 4.0), (-2.0, 1.2, 2.5), (-3.5, 1.8, 1.0), (-5.0, 1.3, 2.5), (-3.0, 1.6, 4.5)]
    events = [particles_cue(0.0, "flies_swarm", flies_at, 90), particles_cue(0.0, "moths_swarm", moths_at, 30)]
    for index, point in enumerate(path[1:]):
        events.append({"at": 1.0 + 3.5 * index, "move": {"entity": room.label("wisp"),
                                                          "to": r(room.world(point)), "seconds": 3.5}})
    room.shot(17.0, [
        (0.0, (-2.9, 2.3, 2.1), (-1.5, 2.6, 0.5), 2.0),
        (7.0, (-0.4, 2.1, 1.8), (-1.5, 2.6, 0.5), 2.0),
        (17.5, (3.8, 1.6, -0.4), (2.2, 1.1, -2.6), 2.0),
    ], events)


def impacts_room(g):
    room = Room(g, "Impacts", 11, 7.0, 9.0, 5.0)
    # Targets of five materials along the back wall, a lane lit over each.
    targets = []
    room.box("walls", (-6.5, 0.0, -8.8), (-4.0, 3.0, -8.4), "concrete target")
    targets.append(("concrete", (-5.25, 1.6, -8.4), ["impact_chips", "impact_puff", "sparks"], "concrete"))
    room.box("metal", (-3.3, 0.0, -8.9), (-0.8, 3.0, -8.7), "metal target")
    targets.append(("metal", (-2.05, 1.6, -8.7), ["sparks", "impact_puff"], "metal"))
    for y in (0.0, 0.9, 1.8):
        room.box("wood", (0.0, y, -8.9), (2.4, y + 0.85, -8.2), "wooden crates")
    targets.append(("wood", (1.2, 1.3, -8.2), ["impact_splinters", "impact_puff"], "wood"))
    room.box("steel", (3.0, 0.0, -8.9), (5.6, 0.15, -8.3), "glass frame")
    room.box("steel", (3.0, 2.85, -8.9), (5.6, 3.0, -8.3), "glass frame")
    room.box("glass", (3.15, 0.15, -8.62), (5.45, 2.85, -8.58), "glass pane")
    targets.append(("glass", (4.3, 1.6, -8.56), ["impact_glass", "glass_shards"], "glass"))
    room.prop("barrel_03", (6.4, 0.0, -8.3), 20.0)
    room.prop("Barrel_01", (6.2, 0.93, -8.35), 0.0)
    targets.append(("drum", (6.4, 0.6, -7.96), ["sparks", "impact_puff"], "metal"))
    for x in (-5.25, -2.05, 1.2, 4.3, 6.4):
        room.light("spot", (x, 4.9, -5.5), 450.0, (1.0, 0.95, 0.85), 10.0, (0.0, -1.0, -0.9), 18, 32,
                   what=f"target lamp {x}")
    room.box("glow_red", (-6.9, 4.3, -8.9), (6.9, 4.4, -8.85), "range light")
    # The firing line: a bench, the gun's place, casings.
    room.box("steel", (-3.0, 0.0, 5.0), (3.0, 1.0, 5.6), "bench")
    gun = room.marker("muzzle", (0.0, 1.45, 4.6))
    ejector = room.marker("ejector", (0.1, 1.5, 4.9), up=(1.0, 0.8, 0.2))
    room.prop("metal_toolbox", (2.2, 1.0, 5.3), 10.0)
    room.prop("metal_stool_01", (-1.5, 0.0, 6.4))
    room.fog((0.0, 2.5, -2.0), (14.0, 5.0, 14.0), 0.035, what="gun smoke haze")
    room.light("point", (0.0, 4.5, 6.0), 30.0, (0.6, 0.7, 1.0), 10.0, what="cold fill")
    room.light("point", (0.0, 4.5, -2.0), 40.0, (0.9, 0.9, 1.0), 10.0, what="lane light")
    events = []
    names = []
    for name, position, effects, surface in targets:
        target_at = room.marker(f"{name} hit", position, up=(0.0, 0.0, 1.0))
        direction = [position[i] - [0.0, 1.45, 4.6][i] for i in range(3)]
        aim = room.marker(f"aim at {name}", (0.0, 1.45, 4.6), up=direction)
        names.append((target_at, aim, effects, surface, math.dist(position, (0.0, 1.45, 4.6))))
    at = 2.8
    order = [0, 1, 2, 3, 4, 1, 0, 2, 4, 3, 1, 2, 0, 4, 1, 3, 2, 0, 4, 1]
    for shot, index in enumerate(order):
        target_at, aim, effects, surface, distance = names[index]
        hit = at + distance / 120.0
        events += [particles_cue(at, "muzzle_flash", gun, 3), particles_cue(at, "gun_smoke", gun, 6),
                   particles_cue(at, "tracer", aim, 1), particles_cue(at + 0.05, "shell_casings", ejector, 1),
                   sound_cue(at, "Factory/Sounds/handgun_fire.wav", gun, 1.4),
                   sound_cue(hit, f"Factory/Sounds/Surfaces/impact_{surface}_{shot % 3 + 1}.wav", target_at, 1.5)]
        for effect in effects:
            count = {"impact_chips": 6, "impact_puff": 3, "sparks": 30, "impact_splinters": 6, "impact_glass": 14,
                     "glass_shards": 5}[effect]
            events.append(particles_cue(hit, effect, target_at, count))
        at += 0.62 if shot % 5 != 4 else 1.1
    room.shot(17.0, [
        (0.0, (-5.0, 1.5, -3.5), (-3.2, 1.5, -8.5), 2.0),
        (8.5, (-1.0, 1.4, -4.2), (1.2, 1.4, -8.5), 2.0),
        (17.5, (3.2, 1.3, -4.6), (4.8, 1.3, -8.6), 2.0),
    ], events)


ROOMS = [fire_room, explosion_room, electricity_room, rain_room, snow_room, wind_room, fog_room, cryo_room,
         water_room, steam_room, creatures_room, impacts_room]


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
        "asset": {"version": "2.0", "generator": "scripts/make_effects_gallery.py"},
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
    settings = {"comment": "Written by scripts/make_effects_gallery.py.", "level_collision": True, "scale": 1.0}
    path.with_name(path.name + ".import.json").write_text(json.dumps(settings, indent=2, sort_keys=True) + "\n",
                                                          encoding="utf-8", newline="\n")


def write_level(g, path):
    environment = {
        "sky": {"faces": "Sky/sky1", "intensity": 9.6, "yaw": 180.0},
        "fog": {"albedo": [0.8, 0.82, 0.85], "anisotropy": 0.3},
        "exposure": {"ev100": 3.0, "auto": True, "min": 0.5, "max": 2.5},
    }
    level = {"format": "daidalos-level", "version": 1, "environment": environment,
             "entities": [{"id": 1, "name": "EffectsGallery", "model": "EffectsGallery.gltf"}] + g.entities}
    path.write_text(json.dumps(level, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


HOLD = 1.6  # Seconds of black after each cut, while the room's effects start.
FADE_IN = 1.0
FADE_OUT = 0.9


def write_tour(g, path):
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
    sequence = {"comment": "The effects gallery's tour (ADR-0122): a shot of each room, fading through black between "
                           "them, made by scripts/make_effects_gallery.py; changes belong there. "
                           "+sandbox.sequence plays it at the start.",
                "cues": sorted(cues, key=lambda cue: cue["at"])}
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="list camera passes close to walls and props")
    parser.add_argument("--only", nargs="+", metavar="ROOM",
                        help="also write Sequences/effects_preview.sequence.json, the tour of these rooms alone, to "
                             "look at them without the others (not a file to keep)")
    args = parser.parse_args()
    g = Gallery()
    # The moon, which lights the open rooms.
    g.moon = g.add({"name": "Moon", "transform": {"rotation": r(quat_from_to([0, 0, -1], [0.25, -1.0, -0.35]), 7)},
                    "light": {"type": "directional", "color": [0.7, 0.8, 1.0], "intensity": 18.0,
                              "angular_radius": 1.5}})
    for make in ROOMS:
        make(g)
    write_gltf(g, ROOT / "EffectsGallery.gltf")
    write_level(g, ROOT / "EffectsGallery.level.json")
    write_tour(g, ROOT / "Sequences" / "effects_tour.sequence.json")
    if args.only:
        whole = g.tour
        g.tour = [shot for shot in whole if shot[0] in args.only]
        write_tour(g, ROOT / "Sequences" / "effects_preview.sequence.json")
        g.tour = whole
    print(f"EffectsGallery: {len(g.entities)} entities, {sum(len(m.indices) // 3 for m in g.meshes.values())} "
          f"triangles")
    if args.check:
        sys.exit(1 if check(g) else 0)


if __name__ == "__main__":
    main()
