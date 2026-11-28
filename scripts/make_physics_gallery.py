#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Writes the physics gallery (ADR-0128): ten rooms, each about one thing loose objects do, and the camera tour
that films them one after the other, fading to black between rooms, as the effects gallery does (ADR-0122).

    python scripts/make_physics_gallery.py [--check] [--only ROOM ...]

It writes, under assets/Factory:
- PhysicsGallery.gltf and .bin: the rooms' walls, floors, ceilings, ramps and ledges, of the factory's materials,
  and its .import.json, which makes them walls and floors for physics and sound;
- PhysicsGallery.level.json: the loose objects, their joints, the lights and the sounds;
- Sequences/physics_tour.sequence.json: the tour, with the cues that drop, push, blow, break and drive them.

    +sandbox.scene Factory/PhysicsGallery.level.json +sandbox.sequence Factory/Sequences/physics_tour.sequence.json

The rooms are swinging lamps, falling crates, rolling barrels, explosions, a wrecking ball, a boulder down stairs,
a pool filled with balls, joints that break, machines, and a chain reaction. What happens in them is the physics'
own: the cues only let a held body fall, push one, blow, snap a joint or drive a motor, and the same run gives the
same picture, so a cue timed to a landing stays on it. Its props are in Props (make_physics_props.py) and the showcase kit. The file is
the source: changes belong here, not in the editor.
"""

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gallery import (PROPS, ROOT, Gallery, Room, blast_cue, check, normalize, particles_cue, quat_axis,  # noqa: E402
                     quat_from_to, quat_mul, r, rotate, sound_cue, write_gltf, write_level, write_tour, yaw)

TOUR_COMMENT = ("The physics gallery's tour (ADR-0128): a shot of each room, fading through black between them, with "
                "the cues that set its loose objects off, made by scripts/make_physics_gallery.py; changes belong "
                "there. +sandbox.sequence plays it at the start.")

# The chain link of make_physics_props.py: links hang LINK_PITCH apart, the inner length of the one above.
LINK_PITCH = 0.112
LINK_SIZE = [0.08, 0.144, 0.04]
BALL_RADIUS = 0.08  # The pool's balls (make_physics_props.py).
CRATE = "Props/crate.gltf"  # 60 cm, about its center (make_crate.py).


def prop_path(model):
    return model if "/" in model else f"Showcase/{model}/{model}.gltf"


def showcase_body(model, mass, shape="box", **more):
    """A body the size of a showcase prop's box."""
    body = {"mass": mass, **more}
    if shape != "box":
        body["shape"] = shape
    if shape != "hull":
        low, high = PROPS[model]
        body["size"] = r([high[i] - low[i] for i in range(3)])
        body["center"] = r([(high[i] + low[i]) / 2 for i in range(3)])
    return body


def point_joint(to, anchor):
    joint = {"kind": "point", "anchor": r(anchor)}
    if to:
        joint["to"] = to
    return joint


def rope(to, anchor, to_anchor, length):
    """A distance joint that keeps two points at most length apart: the hidden rope along a chain (ADR-0126)."""
    joint = {"kind": "distance", "anchor": r(anchor), "to_anchor": r(to_anchor),
             "limits": {"lowest": 0.0, "highest": round(length, 4)}}
    if to:
        joint["to"] = to
    return joint


def hinge(to, anchor, axis, limits=None, friction=None, motor=None, break_force=None):
    joint = {"kind": "hinge", "anchor": r(anchor), "axis": r(axis)}
    if to:
        joint["to"] = to
    if limits:
        joint["limits"] = {"lowest": limits[0], "highest": limits[1]}
    if friction:
        joint["friction"] = friction
    if motor:
        joint["motor"] = motor
    if break_force:
        joint["break_force"] = break_force
    return joint


def glue(to, anchor, break_force):
    return {"kind": "fixed", "to": to, "anchor": r(anchor), "break_force": break_force}


def release_cue(at, entity):
    return {"at": at, "release": {"entity": entity}}


def impulse_cue(at, entity, impulse, point=None):
    cue = {"entity": entity, "impulse": r(impulse)}
    if point:
        cue["point"] = r(point)
    return {"at": at, "impulse": cue}


def shot_cue(at, room, entity, frm, point=None, weapon="handgun"):
    """A shot from an unseen gun at frm, in the room's coordinates, through a point of a loose object (ADR-0129):
    it pushes as the game's weapon does, 2 kg m/s per point of damage, 20 for the handgun and 100 for the rifle."""
    cue = {"entity": room.label(entity), "from": r(room.world(frm)), "weapon": weapon}
    if point:
        cue["point"] = r(point)
    return {"at": at, "shot": cue}


def from_behind(target, push, distance):
    """Where a gun stands to push target along push: distance meters back along it."""
    length = math.sqrt(sum(v * v for v in push))
    return [target[i] - push[i] / length * distance for i in range(3)]


def break_cue(at, entity, joint):
    return {"at": at, "break": {"entity": entity, "joint": joint}}


def motor_cue(at, entity, joint, mode, target, strength):
    """target in radians, or radians a second, for a hinge; meters, or meters a second, for a slider."""
    return {"at": at, "motor": {"entity": entity, "joint": joint, "mode": mode, "target": target,
                                "strength": strength}}


def shake_cue(at, strength, seconds=1.2):
    return {"at": at, "shake": {"strength": strength, "seconds": seconds}}


def explosion(at, where, radius, strength, shake=0.7, big=True):
    """A blast that throws (ADR-0127), with the effects gallery's fire, sparks, smoke and sound."""
    cues = [blast_cue(round(at + 0.02, 3), where, radius, strength),
            particles_cue(at, "explosion_flare", where, 1), particles_cue(at, "explosion_fireball", where, 9 if big else 4),
            particles_cue(round(at + 0.02, 3), "explosion_sparks", where, 160 if big else 80),
            particles_cue(round(at + 0.02, 3), "explosion_debris", where, 24 if big else 10),
            particles_cue(round(at + 0.05, 3), "explosion_shock", where, 70 if big else 30),
            particles_cue(round(at + 0.15, 3), "explosion_smoke", where, 14 if big else 6),
            sound_cue(at, "Factory/Sounds/Ambience/explosion.wav", where, 5.0 if big else 3.0)]
    if shake:
        cues.append(shake_cue(at, shake, 1.4))
    return cues


class PhysicsRoom(Room):
    """A gallery room with loose objects, chains and lamps."""

    def loose(self, what, model, position, body, rotation=None, scale=None, joints=None, extent=0.5):
        """A loose object (ADR-0125), with its joints (ADR-0126). Its id."""
        components = {"model": prop_path(model), "body": body}
        if joints:
            components["joints"] = joints
        made = self.entity(what, position, rotation, scale, **components)
        lo = [position[i] - extent for i in range(3)]
        hi = [position[i] + extent for i in range(3)]
        self.g.boxes.append((self.world(lo), self.world(hi), self.label(what)))
        return made

    def showcase(self, what, model, position, mass, turn=0.0, shape="box", rotation=None, **more):
        """A showcase prop as a loose object of its real weight, standing at position."""
        low, high = PROPS[model]
        extent = max(max(abs(v) for v in low), max(abs(v) for v in high))
        return self.loose(what, model, position, showcase_body(model, mass, shape, **more),
                          rotation if rotation is not None else (yaw(turn) if turn else None), extent=extent)

    def crate(self, what, position, mass=15.0, turn=0.0, scale=None, **more):
        """The factory's 60 cm crate, about its center."""
        body = {"size": [0.6, 0.6, 0.6], "mass": mass, "surface": "wood", **more}
        return self.loose(what, CRATE, position, body, yaw(turn) if turn else None, scale, extent=0.45)

    def lying(self, what, model, center, mass, turn=0.0, **more):
        """A cylinder of the showcase kit lying on its side along X (turned by turn about Y), its middle at
        center: it rolls along Z."""
        low, high = PROPS[model]
        half = (high[1] - low[1]) / 2
        rotation = quat_mul(yaw(turn), quat_axis([0, 0, 1], 90.0))
        offset = rotate(rotation, [0.0, -(high[1] + low[1]) / 2, 0.0])
        position = [center[i] + offset[i] for i in range(3)]
        return self.loose(what, model, position, showcase_body(model, mass, "cylinder", **more), rotation,
                          extent=half)

    def chain(self, what, top, end, scale=1.0, to=0, mass=0.4, group=None):
        """A chain of links from top (on the ceiling, or on entity `to`'s body) toward end, as many as fit, each
        held to the one above by a point joint (ADR-0126). Returns the last link's id, where the chain ends, and its
        length, and its group. What hangs from it holds to the last link at that end, with a rope to the top (hang),
        and is of its group, which nothing of keeps touching (ADR-0128): the links and the load knock together
        wherever the chain bends otherwise."""
        group = self.label(group or what)
        pitch = LINK_PITCH * scale
        span = [end[i] - top[i] for i in range(3)]
        count = max(1, round(math.sqrt(sum(c * c for c in span)) / pitch))
        down = normalize(span)
        facing = quat_from_to([0, 1, 0], [-c for c in down])  # The link's +Y toward the top.
        above = to
        for index in range(count):
            center = [top[i] + down[i] * pitch * (index + 0.5) for i in range(3)]
            rotation = facing if index % 2 == 0 else quat_mul(facing, quat_axis([0, 1, 0], 90.0))
            joint = point_joint(above, [0.0, LINK_PITCH / 2, 0.0])
            above = self.loose(f"{what} link {index + 1}", "Props/chain_link.gltf", center,
                               {"size": LINK_SIZE, "mass": round(mass * scale ** 3, 3), "surface": "metal",
                                "group": group},
                               rotation, [scale] * 3 if scale != 1.0 else None, [joint], extent=0.1 * scale)
        length = count * pitch
        return above, [top[i] + down[i] * length for i in range(3)], length, group

    def hang(self, chain, top, to=0):
        """The joints of what hangs from a chain at its anchor: to the last link, and a rope to the top, which
        keeps the chain from stretching under a heavy load."""
        last, _, length, _ = chain
        to_anchor = top if to else self.world(top)
        return lambda anchor: [point_joint(last, anchor), rope(to, anchor, to_anchor, length + 0.005)]

    def child_light(self, parent, what, translation, light, rotation=None):
        transform = {"translation": r(translation)}
        if rotation is not None:
            transform["rotation"] = r(rotation, 7)
        return self.g.add({"name": self.label(what), "parent": parent, "transform": transform, "light": light})


LAMP_LIGHT = {"type": "spot", "intensity": 900, "color": [1.0, 0.85, 0.65], "range": 14, "inner_angle": 30,
              "outer_angle": 60, "radius": 0.05}
DOWN = [-0.7071068, 0.0, 0.0, 0.7071068]  # A spot's -Z turned down.


def dress(room, lamps=(), fill=30.0, haze=0.012):
    """Caged lamps on the ceiling, a cold fill and a haze: the light the rooms share."""
    for x, z in lamps:
        room.prefab("caged_hanging_light", (x, room.h, z))
    room.light("point", (0.0, room.h - 1.0, room.d - 1.5), fill, (0.6, 0.7, 1.0), 2.0 * room.w, what="cold fill")
    room.fog((0.0, room.h / 2, 0.0), (2 * room.w, room.h, 2 * room.d), haze, what="haze")


# The rooms ---------------------------------------------------------------------------------------------------

# Where a hanging lamp's shade is, below where it hangs: what a shot at it aims at.
LAMP_SHADE = -1.1


def swinging_room(g):
    """Lamps on chains, shot one after another: their lights and the shadows of what stands under them sweep the
    room. A hook on a heavier chain, let go from the side, swings through the picture in front of them."""
    room = PhysicsRoom(g, "Swinging", 0, 7.0, 6.0, 4.6)
    events = []
    lamp = "hanging_industrial_lamp"
    # Each lamp is shot with the handgun (20 kg m/s) through its shade, from out of the picture: the way it is
    # pushed, how far back the gun stands, and when, far enough apart that each is seen to start on its own.
    for index, (x, z, push, back, at) in enumerate(((-2.8, -2.0, (20.0, 0.0, -6.0), 4.0, 2.6),
                                                    (0.0, -2.8, (0.0, 0.0, 19.0), 3.0, 5.2),
                                                    (2.8, -2.0, (-17.0, 0.0, 10.0), 4.2, 7.8))):
        top = (x, room.h - 0.05, z)
        room.box("rust", (x - 0.12, room.h - 0.05, z - 0.12), (x + 0.12, room.h, z + 0.12), "ceiling plate")
        made = room.chain(f"lamp {index + 1} chain", top, (x, top[1] - 0.784, z))
        name = f"lamp {index + 1}"
        lamp_id = room.loose(name, lamp, made[1], {"shape": "hull", "mass": 6.0, "surface": "metal", "group": made[3]},
                             joints=room.hang(made, top)([0.0, 0.0, 0.0]), extent=0.7)
        room.child_light(lamp_id, f"lamp {index + 1} light", (0.0, -1.22, 0.0), LAMP_LIGHT, DOWN)
        shade = [made[1][0], made[1][1] + LAMP_SHADE, made[1][2]]
        events.append(shot_cue(at, room, name, from_behind(shade, push, back), (0.0, LAMP_SHADE, 0.0)))
    # Under each lamp, something whose shadow it swings.
    room.showcase("stool", "metal_stool_01", (-2.6, 0.0, -1.7), 4.0, shape="cylinder")
    room.crate("crate", (0.2, 0.3, -2.9), turn=15.0)
    room.crate("crate 2", (-0.5, 0.3, -3.1), turn=-10.0)
    room.showcase("drum", "barrel_03", (2.9, 0.0, -2.2), 25.0, shape="cylinder")
    room.showcase("sign", "WetFloorSign_01", (1.7, 0.0, -1.2), 2.0, turn=-30.0, surface="plastic")
    # The hook, held out to the side on its chain, let go to swing across the room in front of the lamps.
    pivot = (0.0, room.h - 0.05, 0.6)
    room.box("rust", (-0.15, room.h - 0.05, 0.45), (0.15, room.h, 0.75), "hook plate")
    angle = math.radians(72.0)
    reach = 12 * LINK_PITCH * 1.5
    made = room.chain("hook chain", pivot, (pivot[0] - reach * math.sin(angle), pivot[1] - reach * math.cos(angle),
                                            pivot[2]), scale=1.5, mass=0.5)
    room.loose("hook", "Props/hook.gltf", made[1], {"shape": "hull", "mass": 30.0, "surface": "metal",
                                                     "held": True, "group": made[3]},
               joints=room.hang(made, pivot)([0.0, 0.0, 0.0]), extent=0.5)
    events.append(release_cue(10.6, room.label("hook")))
    # Along the walls, in the dark between the pools.
    room.prop("steel_frame_shelves_02", (-6.6, 0.0, -2.5), 90.0)
    room.prop("steel_frame_shelves_02", (6.6, 0.0, -3.0), -90.0)
    room.prop("metal_office_desk", (1.0, 0.0, -5.4))
    room.prop("Barrel_01", (-5.4, 0.0, -5.2))
    room.prop("barrel_03", (-4.7, 0.0, -5.5))
    room.light("point", (0.0, 4.0, 5.0), 4.0, (0.6, 0.7, 1.0), 12.0, what="cold fill")
    room.fog((0.0, 2.3, 0.0), (14.0, 4.6, 12.0), 0.008, noise=0.25, what="haze")
    room.entity("player", (0.0, 1.4, 5.0), game={"kind": "player"})
    room.shot(21.0, [
        (0.0, (-1.2, 1.3, 4.8), (0.0, 1.9, -2.2), 1.2),
        (10.5, (0.4, 1.2, 4.2), (0.0, 1.8, -2.2), 1.2),
        (21.5, (1.8, 1.4, 3.6), (-0.3, 1.6, -2.2), 1.2),
    ], events)


def falling_room(g):
    """Crates, sacks, a jerrycan and drums, held at the ceiling, let go one after another onto a pyramid of crates,
    which gives way."""
    room = PhysicsRoom(g, "Falling", 1, 7.0, 7.0, 7.0)
    z = -2.0
    # Three columns of crates, not bonded, so that a blow high on one sends its top flying and leans it on the next.
    for column, (x, rows) in enumerate(((-0.62, 5), (0.0, 4), (0.62, 5))):
        for row in range(rows):
            room.crate(f"stack {column} {row}", (x, 0.3 + 0.6 * row, z), turn=((row * 3 + column) % 5 - 2) * 2.0)
    room.showcase("drum", "Barrel_01", (-2.2, 0.0, z + 0.3), 20.0, shape="cylinder")
    room.showcase("drum 2", "barrel_03", (2.25, 0.0, z - 0.2), 25.0, shape="cylinder")
    room.showcase("long crate", "wooden_crate_02", (2.0, 0.0, z + 1.5), 30.0, turn=80.0)
    room.showcase("bag on the floor", "cement_bag", (-2.0, 0.0, z + 1.4), 25.0, turn=20.0, surface="cloth")
    events = []
    # Each lands on the pyramid's side or top, off its middle, so that it pushes crates off.
    drops = [
        (2.6, "crate", lambda w: room.crate(w, (0.35, 6.4, z + 0.1), held=True, fast=True, turn=25.0)),
        (3.6, "sack", lambda w: room.showcase(w, "cement_bag", (-0.95, 6.4, z - 0.1), 25.0, turn=35.0, held=True,
                                              surface="cloth")),
        (4.4, "jerrycan", lambda w: room.showcase(w, "metal_jerrycan", (0.9, 6.4, z + 0.15), 18.0, shape="hull",
                                                  held=True)),
        (5.1, "box", lambda w: room.showcase(w, "plastic_crate_01", (-0.3, 6.5, z - 0.2), 3.0, turn=40.0, held=True,
                                             surface="plastic")),
        (5.3, "box 2", lambda w: room.showcase(w, "plastic_crate_01", (0.4, 6.3, z + 0.3), 3.0, held=True,
                                               surface="plastic")),
        (6.4, "heavy crate", lambda w: room.showcase(w, "wooden_crate_02", (-0.55, 6.4, z + 0.1), 60.0, turn=15.0,
                                                     held=True, fast=True)),
        (8.0, "drum", lambda w: room.showcase(w, "barrel_03", (1.0, 6.2, z - 0.1), 70.0, shape="cylinder",
                                              held=True, fast=True)),
        (8.9, "sack 2", lambda w: room.showcase(w, "cement_bag", (-0.2, 6.4, z + 0.2), 25.0, turn=-30.0, held=True,
                                                surface="cloth")),
        (9.8, "crate 2", lambda w: room.crate(w, (-0.8, 6.4, z + 0.1), held=True, fast=True, turn=-15.0)),
        (10.0, "crate 3", lambda w: room.crate(w, (0.7, 6.4, z - 0.2), held=True, fast=True, turn=35.0)),
        (10.25, "crate 4", lambda w: room.crate(w, (0.1, 6.4, z + 0.4), held=True, fast=True)),
        (11.4, "drum 2", lambda w: room.showcase(w, "Barrel_02", (-0.3, 6.2, z), 60.0, shape="cylinder", held=True,
                                                 fast=True)),
    ]
    for at, what, make in drops:
        make(f"falling {what}")
        events.append(release_cue(at, room.label(f"falling {what}")))
    dress(room, lamps=((-4.0, -4.5), (4.0, 0.5)), fill=25.0)
    room.light("spot", (-2.0, 6.8, 3.0), 2400.0, (1.0, 0.9, 0.78), 14.0, (0.4, -1.0, -1.0), 25, 45, 0.2,
               what="light on the stack")
    room.shot(17.0, [
        (0.0, (-2.6, 1.2, 3.4), (0.0, 1.7, z), 1.2),
        (17.5, (2.2, 1.0, 3.0), (0.0, 1.2, z), 1.2),
    ], events)


def rolling_room(g):
    """Barrels and gas bottles, held on a ramp, let go one after another: they roll down and across the floor into
    stools, a sign and boxes, and crash into a low wall of crates."""
    room = PhysicsRoom(g, "Rolling", 2, 8.0, 9.0, 6.0)
    z_top, z_foot, height = -9.0, -2.5, 1.8
    room.ramp("rust", -2.6, 2.6, z_top, z_foot, height)
    rise = height / (z_foot - z_top)
    normal = normalize([0.0, 1.0, rise])

    def on_ramp(x, z, radius):
        y = room.surface_height(z, z_top, z_foot, height)
        return [x + normal[0] * radius, y + normal[1] * radius, z + normal[2] * radius]

    events = []
    for at, what, model, x, z, mass in ((2.4, "drum", "Barrel_01", -1.3, -7.9, 20.0),
                                        (3.6, "big drum", "barrel_03", 1.1, -8.1, 25.0),
                                        (4.8, "bottle", "propane_tank", -0.4, -7.2, 15.0),
                                        (5.1, "bottle 2", "propane_tank", 0.7, -7.0, 15.0),
                                        (6.4, "drum 2", "Barrel_02", 0.0, -8.0, 20.0)):
        low, high = PROPS[model]
        radius = (high[0] - low[0]) / 2
        room.lying(f"rolling {what}", model, on_ramp(x, z, radius), mass, held=True)
        events.append(release_cue(at, room.label(f"rolling {what}")))
    # What they run into.
    room.showcase("sign", "WetFloorSign_01", (-0.5, 0.0, -0.6), 2.0, turn=20.0, surface="plastic")
    room.showcase("stool", "metal_stool_01", (-1.3, 0.0, 0.2), 4.0, shape="cylinder")
    room.showcase("stool 2", "metal_stool_01", (0.9, 0.0, 0.8), 4.0, shape="cylinder")
    room.showcase("stool 3", "metal_stool_01", (0.1, 0.0, 1.8), 4.0, shape="cylinder")
    room.showcase("box", "plastic_crate_01", (1.4, 0.0, -0.8), 3.0, surface="plastic")
    room.showcase("box 2", "plastic_crate_01", (1.4, 0.26, -0.8), 3.0, turn=15.0, surface="plastic")
    room.showcase("extinguisher", "korean_fire_extinguisher_01", (-1.9, 0.0, 1.4), 8.0, shape="cylinder")
    # The low wall they end against.
    for row in range(2):
        for column in range(8):
            x = -2.17 + 0.62 * column + (0.31 if row else 0.0)
            if row and column == 7:
                continue
            room.crate(f"wall {row} {column}", (x, 0.3 + 0.6 * row, 4.0), 12.0, turn=(column % 3 - 1) * 4.0)
    room.prop("steel_frame_shelves_02", (-7.5, 0.0, -2.0), 90.0)
    room.prop("tool_cart", (6.5, 0.0, -6.0), 60.0)
    dress(room, lamps=((-3.0, -5.0), (3.0, 2.0)), fill=30.0)
    room.light("spot", (5.0, 5.8, -1.0), 1500.0, (1.0, 0.9, 0.78), 18.0, (-0.6, -1.0, -0.4), 25, 55, 0.2,
               what="light on the ramp")
    room.light("spot", (-4.0, 5.8, 6.0), 1500.0, (1.0, 0.9, 0.78), 18.0, (0.6, -1.0, -0.6), 25, 55, 0.2,
               what="light on the floor")
    room.shot(18.0, [
        (0.0, (5.0, 1.6, -1.5), (-0.5, 0.9, -5.5), 1.2),
        (6.0, (5.0, 1.5, -0.4), (-0.3, 0.6, -1.5), 1.2),
        (18.5, (4.6, 1.5, 0.8), (-0.3, 0.5, 2.8), 1.2),
    ], events)


def explosions_room(g):
    """Gas bottles, drums, crates, sacks and a cart, thrown by three blasts; the last blows a wall of crates apart
    toward the camera."""
    room = PhysicsRoom(g, "Explosions", 3, 8.0, 8.0, 7.0)
    for x in (-6.0, 6.0):
        for z in (-4.0, 2.0):
            room.column("walls", x, z, 0.7, 0.0, 7.0, "pillar")
    blasts = []
    for name, (x, z) in (("A", (-4.4, -4.6)), ("B", (4.4, -4.0))):
        for dx, dz in ((-0.4, 0.0), (0.4, 0.2), (0.0, -0.4)):
            room.showcase(f"bottle {name} {dx}", "propane_tank", (x + dx, 0.0, z + dz), 15.0, turn=30.0 * dx,
                          shape="cylinder")
        blasts.append(room.marker(f"blast {name}", (x, 0.6, z)))
    room.showcase("drum", "barrel_03", (-4.6, 0.0, -2.6), 20.0, shape="cylinder")
    room.showcase("drum 2", "Barrel_01", (-3.6, 0.0, -2.3), 20.0, shape="cylinder")
    room.showcase("drum 3", "Barrel_02", (4.0, 0.0, -2.3), 20.0, shape="cylinder")
    room.showcase("long crate", "wooden_crate_02", (-6.8, 0.0, -2.0), 30.0)
    room.showcase("sack", "cement_bag", (3.4, 0.0, -5.8), 25.0, turn=60.0, surface="cloth")
    room.showcase("sack 2", "cement_bag", (3.5, 0.18, -5.7), 25.0, turn=75.0, surface="cloth")
    room.showcase("cart", "industrial_storage_cart", (5.8, 0.0, -1.0), 40.0, turn=90.0, shape="hull")
    # The wall of crates, five wide and three high, with the last blast behind it.
    for row in range(3):
        for column in range(5):
            x = -1.24 + 0.62 * column + (0.15 if row % 2 else 0.0)
            room.crate(f"wall {row} {column}", (x, 0.3 + 0.6 * row, -3.0), turn=(7.0 * column - 14.0) * (row % 2))
    blasts.append(room.marker("blast C", (0.0, 0.5, -4.3)))
    room.prefab("caged_hanging_light", (-4.0, 7.0, -2.0))
    room.prefab("caged_hanging_light", (4.0, 7.0, 2.0))
    room.light("spot", (0.0, 6.6, 3.0), 2200.0, (1.0, 0.85, 0.7), 20.0, (0.0, -0.45, -1.0), 35, 60, 0.2,
               what="wash on the back wall")
    room.light("point", (0.0, 5.0, 6.0), 40.0, (0.6, 0.7, 1.0), 14.0, what="cold fill")
    room.fog((0.0, 3.5, 0.0), (16.0, 7.0, 16.0), 0.01, what="haze")
    events = (explosion(3.0, blasts[0], 3.6, 200.0) + explosion(7.4, blasts[1], 3.6, 190.0, 0.6) +
              explosion(12.0, blasts[2], 6.0, 140.0, 0.9))
    room.shot(19.0, [
        (0.0, (3.4, 1.6, 5.2), (-1.8, 1.0, -3.5), 1.2),
        (8.5, (2.6, 1.7, 6.0), (0.6, 1.0, -3.5), 1.2),
        (19.5, (1.6, 1.8, 7.2), (0.0, 0.6, -1.5), 1.2),
    ], events)


def wrecking_room(g):
    """A steel ball on a heavy chain, held out to the side, let go through a wall of crates."""
    room = PhysicsRoom(g, "Wrecking", 4, 8.0, 8.0, 9.0)
    pivot = (0.0, room.h - 0.15, 0.0)
    room.box("rust", (-8.0, room.h - 0.15, -0.25), (8.0, room.h, 0.25), "beam")
    angle = math.radians(60.0)
    chain_length = 26 * LINK_PITCH * 2.5
    end = (pivot[0] - chain_length * math.sin(angle), pivot[1] - chain_length * math.cos(angle), 0.0)
    made = room.chain("ball chain", pivot, end, scale=2.5, mass=0.3)
    eye = [0.0, 0.57, 0.0]
    center = [made[1][i] - eye[i] for i in range(3)]
    room.loose("ball", "Props/steel_ball.gltf", center,
               {"shape": "sphere", "size": [0.9, 0.9, 0.9], "mass": 400.0, "surface": "metal", "held": True,
                "fast": True, "group": made[3]}, joints=room.hang(made, pivot)(eye), extent=0.6)
    # The wall where the ball is lowest, five crates wide and four high, and drums behind it.
    for row in range(4):
        for column in range(5):
            z = -1.24 + 0.62 * column + (0.0 if row % 2 == 0 else 0.1)
            room.crate(f"wall {row} {column}", (0.9, 0.3 + 0.6 * row, z))
    for index, (x, z) in enumerate(((3.6, -0.8), (4.2, 0.4), (3.9, 1.5))):
        room.showcase(f"drum {index + 1}", "Barrel_01", (x, 0.0, z), 20.0, shape="cylinder")
    room.showcase("cart", "tool_cart", (5.5, 0.0, -3.5), 30.0, turn=20.0, shape="hull")
    dress(room, lamps=((-4.0, -4.0), (4.0, -4.0)), fill=35.0)
    room.light("spot", (-2.0, 8.5, 5.0), 3000.0, (1.0, 0.9, 0.78), 18.0, (0.3, -1.0, -0.8), 25, 50, 0.2,
               what="light on the wall")
    room.shot(17.0, [
        (0.0, (-2.6, 1.7, 7.2), (-0.8, 2.4, 0.0), 1.2),
        (17.5, (1.6, 1.4, 7.0), (1.0, 1.4, 0.0), 1.2),
    ], [release_cue(2.6, room.label("ball"))])


def stairs_room(g):
    """A big boulder dropped onto the top of a long flight of concrete stairs: it tumbles down them, step by step,
    into crates and drums at their foot, the camera backing away before it."""
    room = PhysicsRoom(g, "Stairs", 6, 15.0, 9.0, 14.0)
    events = []
    # The flight goes down along +X, from a landing against the west wall to the floor, between two parapets that
    # keep the boulder on it.
    z0, z1 = STAIRS_Z
    room.box("walls", (-room.w, 0.0, z0), (STAIRS_TOP_EDGE, STAIRS_HEIGHT, z1), "landing")
    steps = round(STAIRS_HEIGHT / STAIRS_RISE) - 1
    for step in range(steps):
        x = STAIRS_TOP_EDGE + STAIRS_TREAD * step
        top = STAIRS_HEIGHT - STAIRS_RISE * (step + 1)
        room.box("walls", (x, 0.0, z0), (x + STAIRS_TREAD, top, z1), f"step {step + 1}")
        for side, (a, b) in (("far", (z0 - 0.25, z0)), ("near", (z1, z1 + 0.25))):
            room.box("rust", (x, 0.0, a), (x + STAIRS_TREAD, top + 0.9, b), f"{side} parapet {step + 1}")
    for side, (a, b) in (("far", (z0 - 0.25, z0)), ("near", (z1, z1 + 0.25))):
        room.box("rust", (-room.w, 0.0, a), (STAIRS_TOP_EDGE, STAIRS_HEIGHT + 0.9, b), f"{side} landing parapet")
    # The boulder, 2 m across and 8 tonnes, held over the second step and a little turned, let go.
    zc = (z0 + z1) / 2
    room.loose("boulder", "Props/boulder.gltf", (STAIRS_TOP_EDGE + 1.3, STAIRS_HEIGHT + 5.0, zc + 0.3),
               {"shape": "hull", "mass": 8000.0, "surface": "concrete", "held": True, "fast": True},
               quat_mul(quat_axis([1, 0, 0], 25.0), quat_axis([0, 1, 0], 40.0)), extent=1.2)
    events.append(release_cue(2.6, room.label("boulder")))
    # At the foot of the stairs, what it runs into.
    foot = STAIRS_TOP_EDGE + STAIRS_TREAD * steps
    for row, count in enumerate((4, 3, 2, 1)):
        for column in range(count):
            room.crate(f"crate {row} {column}",
                       (foot + 3.5, 0.3 + 0.6 * row, zc - 0.62 * (count - 1) / 2 + 0.62 * column), 12.0,
                       turn=(column - row) * 4.0)
    room.showcase("drum", "Barrel_01", (foot + 2.0, 0.0, z1 + 1.4), 20.0, shape="cylinder")
    room.showcase("drum 2", "barrel_03", (foot + 5.5, 0.0, z0 + 0.3), 25.0, shape="cylinder")
    room.showcase("drum 3", "Barrel_02", (foot + 6.2, 0.0, zc + 0.4), 20.0, shape="cylinder")
    room.showcase("cart", "tool_cart", (foot + 3.0, 0.0, z0 - 1.2), 30.0, turn=10.0, shape="hull")
    room.prop("steel_frame_shelves_02", (14.4, 0.0, -6.0), -90.0)
    dress(room, lamps=((-11.0, -5.0), (-4.0, -5.0), (3.0, -5.0), (10.0, -5.0)), fill=60.0)
    room.light("spot", (-8.0, 13.6, 3.0), 7000.0, (1.0, 0.9, 0.78), 24.0, (0.0, -1.0, -0.6), 30, 55, 0.2,
               what="light on the landing")
    room.light("spot", (-3.0, 13.6, 3.0), 7000.0, (1.0, 0.9, 0.78), 24.0, (0.0, -1.0, -0.6), 30, 55, 0.2,
               what="light on the stairs")
    room.light("spot", (6.0, 13.6, 3.0), 6000.0, (1.0, 0.9, 0.78), 24.0, (0.0, -1.0, -0.6), 30, 60, 0.2,
               what="light on the foot")
    # From beyond the foot, up the flight; as the boulder comes down, the camera backs away before it.
    room.shot(18.0, [
        (0.0, (6.0, 3.4, 1.8), (-9.0, 8.5, zc), 1.2),
        (3.2, (6.0, 3.3, 1.8), (-8.0, 6.0, zc), 1.2),
        (4.6, (7.0, 3.2, 1.9), (-5.0, 3.8, zc), 1.2),
        (7.2, (12.0, 2.8, 2.2), (-0.5, 1.4, zc), 1.2),
        (9.5, (13.8, 2.6, 2.6), (4.0, 0.9, zc - 0.5), 1.2),
        (18.5, (14.0, 2.8, 3.0), (6.0, 0.6, zc - 1.0), 1.2),
    ], events)


# The stairs room's flight (ADR-0129): from a landing STAIRS_HEIGHT high, down along +X from STAIRS_TOP_EDGE in
# steps of STAIRS_RISE and STAIRS_TREAD (big ones, as a factory's), between z STAIRS_Z.
STAIRS_HEIGHT = 6.0
STAIRS_RISE = 0.4
STAIRS_TREAD = 0.7
STAIRS_TOP_EDGE = -10.0
STAIRS_Z = (-7.0, -2.0)


def pool_room(g):
    """A pool that a duct from the ceiling fills with plastic balls, hundreds of them: they bounce off one another,
    the rim and the crates in the pool, and pile up."""
    # The duct, square, through the ceiling over the pool, and above the ceiling, out of sight, a hopper: a channel
    # as wide as the duct whose floor slopes down to it, where the balls wait in rows across it.
    dx, dz, half = POOL_DUCT
    room = PhysicsRoom(g, "Pool", 5, 8.0, 7.0, 6.0, holes=[(dx - half, dx + half, dz - half, dz + half)])
    events = []
    # The pool: a basin of concrete on the floor.
    x0, x1, z0, z1, rim = -3.0, 2.6, -4.8, -1.2, 0.8
    for low, high, what in (((x0 - 0.25, 0.0, z0 - 0.25), (x1 + 0.25, rim, z0), "back"),
                            ((x0 - 0.25, 0.0, z1), (x1 + 0.25, rim, z1 + 0.25), "front"),
                            ((x0 - 0.25, 0.0, z0), (x0, rim, z1), "left"),
                            ((x1, 0.0, z0), (x1 + 0.25, rim, z1), "right")):
        room.box("walls", low, high, f"pool {what} rim")
    room.box("metal", (x0, 0.0, z0), (x1, 0.02, z1), "pool floor")
    # The duct, from the ceiling down to 3.2 m.
    t = 0.05
    bottom, top = 3.2, room.h + 0.3
    for low, high, what in (((dx - half - t, bottom, dz - half - t), (dx + half + t, top, dz - half), "back"),
                            ((dx - half - t, bottom, dz + half), (dx + half + t, top, dz + half + t), "front"),
                            ((dx - half - t, bottom, dz - half), (dx - half, top, dz + half), "left"),
                            ((dx + half, bottom, dz - half), (dx + half + t, top, dz + half), "right")):
        room.box("rust", low, high, f"duct {what}")
    # The hopper over the ceiling, along +Z from the duct.
    floor = room.h + 0.3
    length, rise = 6.0, 2.5
    z_foot, z_top = dz + half, dz + half + length
    room.ramp("rust", dx - half, dx + half, z_top, z_foot, rise, "hopper floor", base=floor)
    hopper_top = floor + rise + 2.0
    room.box("rust", (dx - half - t, floor, dz - half - t), (dx + half + t, hopper_top, dz - half), "hopper back")
    room.box("rust", (dx - half - t, floor, z_top), (dx + half + t, hopper_top, z_top + t), "hopper end")
    for xa, xb in ((dx - half - t, dx - half), (dx + half, dx + half + t)):
        room.box("rust", (xa, floor, dz - half), (xb, hopper_top, z_top), "hopper side")
    # The balls: across the channel five by five, up the slope, in layers; each row of every layer let go in
    # turn, the nearest the duct first, so that they pour down the duct in a stream.
    colors = ("red", "yellow", "blue", "green", "white")
    pitch = 2 * BALL_RADIUS + 0.04
    across = [dx + pitch * (column - 2) for column in range(5)]
    rows = int((length - 0.3) / pitch)
    count = 0
    for row in range(rows):
        z = z_foot + 0.15 + pitch * row
        ground = floor + room.surface_height(z, z_top, z_foot, rise)
        names = []
        for layer in range(POOL_LAYERS):
            for column, x in enumerate(across):
                name = f"ball {count + 1}"
                color = colors[(row * 7 + layer * 3 + column) % len(colors)]
                room.loose(name, f"Props/ball_{color}.gltf", (x, ground + BALL_RADIUS + 0.05 + pitch * layer, z),
                           {"shape": "sphere", "size": [2 * BALL_RADIUS] * 3, "mass": 0.12, "surface": "plastic",
                            "held": True}, extent=BALL_RADIUS)
                names.append(name)
                count += 1
        events += [release_cue(round(POOL_START + POOL_ROW_GAP * row, 3), room.label(name)) for name in names]
    # What the balls land on and around, in the pool.
    room.crate("crate", (0.3, 0.3, -3.6), 15.0, turn=20.0)
    room.crate("crate 2", (1.6, 0.3, -2.2), 15.0, turn=-35.0)
    room.showcase("drum", "barrel_03", (1.9, 0.0, -4.1), 25.0, shape="cylinder")
    room.loose("rubber ball", "Props/rubber_ball.gltf", (-2.5, 0.17, -1.6),
               {"shape": "sphere", "size": [0.3, 0.3, 0.3], "mass": 1.0, "surface": "rubber"}, extent=0.2)
    dress(room, lamps=((-4.0, 2.0), (4.0, 2.0)), fill=35.0)
    room.light("spot", (2.5, 5.8, 1.5), 2600.0, (1.0, 0.9, 0.78), 14.0, (-0.5, -1.0, -0.7), 30, 60, 0.2,
               what="light on the pool")
    room.light("spot", (-5.0, 5.8, -0.5), 1600.0, (1.0, 0.9, 0.78), 12.0, (0.6, -1.0, -0.6), 25, 50, 0.2,
               what="light on the duct")
    end = POOL_START + POOL_ROW_GAP * rows
    room.shot(end + 10.0, [
        (0.0, (1.2, 3.2, 3.8), (-0.8, 1.6, -3.0), 1.2),
        (POOL_START + 3.0, (0.9, 3.0, 3.2), (-0.6, 0.9, -3.0), 1.2),
        (end + 10.5, (-0.2, 3.6, 2.4), (-0.2, 0.2, -3.0), 1.2),
    ], events)
    return count


# The pool room's duct (ADR-0129): its middle (x, z) and half its width, inside; how many layers of balls wait in
# the hopper, when the first row is let go and how long after each other the rows are.
POOL_DUCT = (-1.2, -3.0, 0.52)
POOL_LAYERS = 7
POOL_START = 2.0
POOL_ROW_GAP = 0.15


# Where the second shot at the door comes from, in front of it, for the door as the first shot left it.
DOOR_SECOND_SHOT = (-6.1, 1.0, 1.0)


def joints_room(g):
    """In a row, the camera passing along them: a door on its hinge, shot; a sign on two chains, one of which
    snaps; a shelf glued together, blown apart; a see-saw that throws a crate."""
    room = PhysicsRoom(g, "Joints", 7, 10.0, 7.0, 4.6)
    events = []
    # The door, in a frame standing on its own.
    dx, dz = -7.0, -2.6
    room.box("rust", (dx - 0.17, 0.0, dz - 0.1), (dx - 0.05, 2.25, dz + 0.1), "door post")
    room.box("rust", (dx + 1.05, 0.0, dz - 0.1), (dx + 1.17, 2.25, dz + 0.1), "door post")
    room.box("rust", (dx - 0.17, 2.15, dz - 0.1), (dx + 1.17, 2.27, dz + 0.1), "lintel")
    room.loose("door", "Props/door.gltf", (dx, 0.02, dz), {"size": [1.0, 2.1, 0.06], "center": [0.5, 1.05, 0.0],
                                                           "mass": 30.0, "surface": "metal"},
               joints=[hinge(0, [0.0, 1.05, 0.0], [0.0, 1.0, 0.0], limits=(-110.0, 110.0), friction=4.0)],
               extent=1.1)
    # Shot with the rifle (100 kg m/s) near its free edge: from behind it, then back from in front.
    events += [shot_cue(2.4, room, "door", (dx + 0.9, 1.0, dz - 3.8), (0.9, 1.0, 0.0), "rifle"),
               shot_cue(4.8, room, "door", DOOR_SECOND_SHOT, (0.9, 1.0, 0.0), "rifle")]
    # The sign on two chains; the right one snaps.
    sx, sz = -2.4, -3.0
    links = 9
    sign_y = room.h - 0.05 - links * LINK_PITCH * 1.5 - 0.25
    holds = []
    for side in (-0.5, 0.5):
        top = (sx + side, room.h - 0.05, sz)
        room.box("rust", (top[0] - 0.08, room.h - 0.05, sz - 0.08), (top[0] + 0.08, room.h, sz + 0.08), "sign hook")
        made = room.chain(f"sign chain {'left' if side < 0 else 'right'}", top, (top[0], sign_y + 0.25, sz),
                          scale=1.5, group="sign")
        holds.append(room.hang(made, top)([side, 0.25, 0.0]))
    room.loose("sign", "Props/sign.gltf", (sx, sign_y, sz), {"size": [1.2, 0.5, 0.04], "mass": 10.0,
                                                             "surface": "metal", "group": room.label("sign")},
               joints=[holds[0][0], holds[1][0], holds[0][1], holds[1][1]], extent=0.7)
    events += [impulse_cue(6.6, room.label("sign"), (0.0, 0.0, 8.0), (0.4, 0.0, 0.0)),
               break_cue(7.8, room.label("sign"), 1), break_cue(7.8, room.label("sign"), 3)]
    # The glued shelf: two sides and three boards, each board glued to both sides, things on its boards.
    gx, gz = 1.8, -3.4
    left = room.loose("shelf side left", "Props/shelf_side.gltf", (gx - 0.63, 0.9, gz),
                      {"size": [0.06, 1.8, 0.45], "mass": 8.0, "surface": "wood"}, extent=1.0)
    right = room.loose("shelf side right", "Props/shelf_side.gltf", (gx + 0.63, 0.9, gz),
                       {"size": [0.06, 1.8, 0.45], "mass": 8.0, "surface": "wood"}, extent=1.0)
    for index, y in enumerate((0.1, 0.9, 1.7)):
        room.loose(f"shelf board {index + 1}", "Props/shelf_board.gltf", (gx, y, gz),
                   {"size": [1.2, 0.05, 0.45], "mass": 4.0, "surface": "wood"},
                   joints=[glue(left, [-0.6, 0.0, 0.0], 600.0), glue(right, [0.6, 0.0, 0.0], 600.0)], extent=0.7)
    room.showcase("shelf box", "plastic_crate_01", (gx - 0.3, 0.925, gz), 3.0, surface="plastic")
    room.showcase("shelf can", "metal_jerrycan", (gx + 0.3, 0.925, gz), 12.0, shape="hull")
    room.showcase("shelf box 2", "plastic_crate_01", (gx, 1.725, gz), 3.0, turn=20.0, surface="plastic")
    room.showcase("shelf bottle", "propane_tank", (gx - 0.25, 0.125, gz), 10.0, shape="cylinder")
    blast = room.marker("shelf blast", (gx + 0.3, 0.5, gz - 0.9))
    events += explosion(11.0, blast, 3.5, 160.0, 0.5, big=False)
    # The see-saw: a board on a hinge over a block, its left end down with a crate on it; a drum dropped on its
    # right end throws the crate.
    px, pz, tilt = 6.6, -2.4, 20.0
    room.box("rust", (px - 0.15, 0.0, pz - 0.3), (px + 0.15, 0.5, pz + 0.3), "see-saw block")
    pivot = (px, 0.56, pz)
    board_turn = quat_axis([0, 0, 1], tilt)
    room.loose("see-saw", "Props/seesaw_board.gltf", pivot, {"size": [3.0, 0.08, 0.45], "mass": 12.0,
                                                             "surface": "wood"}, board_turn,
               joints=[hinge(0, [0.0, 0.0, 0.0], [0.0, 0.0, 1.0])], extent=1.6)
    on_board = rotate(board_turn, [-1.15, 0.04 + 0.3, 0.0])
    room.crate("thrown crate", [pivot[i] + on_board[i] for i in range(3)], 8.0)
    g.entities[-1]["transform"]["rotation"] = r(board_turn, 7)
    over = rotate(board_turn, [1.2, 0.0, 0.0])
    room.showcase("dropped drum", "barrel_03", (pivot[0] + over[0], 3.4, pz), 60.0, shape="cylinder", held=True)
    events.append(release_cue(14.2, room.label("dropped drum")))
    dress(room, lamps=((-6.0, 0.0), (0.0, 1.0), (6.0, 0.0)), fill=30.0)
    for x, what in ((-6.0, "door"), (-2.4, "sign"), (1.8, "shelf"), (6.0, "see-saw")):
        room.light("spot", (x, 4.5, 1.5), 1300.0, (1.0, 0.9, 0.78), 10.0, (0.0, -1.0, -1.0), 30, 55, 0.2,
                   what=f"light on the {what}")
    room.shot(18.0, [
        (0.0, (-5.6, 1.5, 3.6), (-6.6, 1.1, -2.6), 1.2),
        (5.0, (-5.0, 1.5, 3.6), (-6.4, 1.1, -2.6), 1.2),
        (6.2, (-2.2, 1.4, 3.2), (-2.4, 2.4, -3.0), 1.2),
        (9.4, (-1.8, 1.4, 3.2), (-2.2, 2.2, -3.0), 1.2),
        (10.4, (1.6, 1.4, 3.0), (1.8, 0.9, -3.4), 1.2),
        (13.4, (2.4, 1.4, 3.0), (2.4, 0.8, -3.4), 1.2),
        (14.2, (5.6, 1.6, 3.6), (6.4, 1.3, -2.4), 1.2),
        (18.5, (5.2, 1.6, 4.2), (6.0, 1.6, -2.4), 1.2),
    ], events)


def machines_room(g):
    """A conveyor carries boxes to its end, where a turning paddle comes down on them and bats them back up over the
    belt, which brings them back to be hit again; beside it, a piston shoves crates off a ledge. The rooms before it
    play first, with the belt running: its boxes are held on it, and the paddle stands, until the room starts."""
    room = PhysicsRoom(g, "Machines", 8, 9.0, 8.0, 6.0)
    events = []
    bz = -3.0
    belt_end = -1.0
    belt_top = 1.6
    belt_center = belt_end - 7.7 / 2
    room.entity("belt", (belt_center, belt_top - 0.05, bz), model="Props/belt.gltf",
                body={"mass": 0.0, "size": [7.7, 0.1, 0.9], "surface": "metal", "conveyor": [0.8, 0.0, 0.0]})
    for x in (belt_end - 0.3, belt_center, belt_end - 7.4):
        for side in (-0.4, 0.4):
            room.box("rust", (x - 0.05, 0.0, bz + side - 0.05), (x + 0.05, belt_top - 0.1, bz + side + 0.05),
                     "belt leg")
    for index in range(7):
        x = -2.6 - 0.95 * index
        if index % 2:
            name = f"belt box {index + 1}"
            room.showcase(name, "plastic_crate_01", (x, belt_top, bz), 3.0, turn=90.0, surface="plastic",
                          held=True)
        else:
            name = f"belt crate {index + 1}"
            room.crate(name, (x, belt_top + 0.18, bz), 6.0, scale=[0.6, 0.6, 0.6], held=True)
        events.append(release_cue(0.0, room.label(name)))
    # The paddle, turned by a motor, its left side coming down past the belt's end, onto what arrives there.
    hub = (0.0, 2.35, bz)
    room.loose("paddle", "Props/paddle.gltf", hub, {"size": [2.4, 0.08, 0.8], "mass": 20.0, "surface": "wood"},
               joints=[hinge(0, [0.0, 0.0, 0.0], [0.0, 0.0, 1.0],
                             motor={"mode": "speed", "target": 0.0, "strength": 3000.0})], extent=1.3)
    events.append(motor_cue(0.0, room.label("paddle"), 0, "speed", round(math.radians(PADDLE_SPEED), 4), 3000.0))
    room.box("rust", (hub[0] - 0.1, 0.0, bz - 0.6), (hub[0] + 0.1, hub[1], bz - 0.45), "paddle post")
    # The piston on a ledge to the right, three crates in front of it, pushed off toward the camera.
    room.box("walls", (3.4, 0.0, -7.0), (6.2, 1.2, -3.8), "ledge")
    px = 4.8
    room.loose("piston", "Props/piston.gltf", (px, 1.5, -6.55), {"size": [0.8, 0.6, 0.6], "mass": 50.0,
                                                                 "surface": "metal"}, yaw(-90.0),
               joints=[{"kind": "slider", "axis": [1.0, 0.0, 0.0], "limits": {"lowest": 0.0, "highest": 2.3},
                        "motor": {"mode": "position", "target": 0.0, "strength": 4000.0}}], extent=0.5)
    for index, z in enumerate((-5.8, -5.18, -4.56)):
        room.crate(f"ledge crate {index + 1}", (px, 1.5, z), 15.0)
    events += [motor_cue(7.6, room.label("piston"), 0, "speed", 0.7, 4000.0),
               motor_cue(12.6, room.label("piston"), 0, "speed", -1.5, 4000.0)]
    dress(room, lamps=((-5.0, -1.0), (1.0, -5.0), (6.0, -2.0)), fill=30.0)
    room.light("spot", (-2.0, 5.8, 2.0), 1600.0, (1.0, 0.9, 0.78), 14.0, (0.2, -1.0, -1.0), 30, 55, 0.2,
               what="light on the belt")
    room.light("spot", (4.0, 5.8, 1.0), 1600.0, (1.0, 0.9, 0.78), 14.0, (0.2, -1.0, -1.0), 30, 55, 0.2,
               what="light on the ledge")
    room.sound("hum", (belt_center, 1.0, bz), 0.4, 2.0, 14.0)
    room.shot(17.0, [
        (0.0, (-2.2, 2.0, 3.6), (-0.8, 1.6, bz), 1.2),
        (6.4, (-0.6, 2.0, 4.0), (0.6, 1.5, bz), 1.2),
        (8.4, (1.8, 2.0, 3.6), (3.4, 1.1, -4.6), 1.2),
        (17.5, (1.2, 2.2, 4.4), (2.2, 1.2, -4.0), 1.2),
    ], events)


# Degrees a second: the paddle's left side comes down past the belt's end (ADR-0129: livelier than rising).
PADDLE_SPEED = -150.0


def finale_room(g):
    """A chain reaction: a ball rolls down a ramp into small dominoes that grow, the last and biggest slams a
    see-saw, which throws a crate onto gas bottles, which blow."""
    room = PhysicsRoom(g, "Finale", 9, 10.0, 9.0, 7.0)
    events = []
    ox = -4.5  # The ramp's middle, across X.
    z_top, z_foot, height = -9.0, -3.0, 2.0
    room.ramp("rust", ox - 0.8, ox + 0.8, z_top, z_foot, height)
    for x in (ox - 0.85, ox + 0.85):
        room.box("rust", (x - 0.05, 0.0, z_top), (x + 0.05, 0.25, z_foot), "ramp rail")
    rise = height / (z_foot - z_top)
    normal = normalize([0.0, 1.0, rise])
    radius = 0.45 * 0.6
    z = -8.4
    y = room.surface_height(z, z_top, z_foot, height)
    room.loose("ball", "Props/steel_ball.gltf",
               [ox + normal[0] * radius, y + normal[1] * radius, z + normal[2] * radius],
               {"shape": "sphere", "size": [0.9, 0.9, 0.9], "mass": 40.0, "surface": "metal", "held": True},
               scale=[0.6, 0.6, 0.6], extent=0.4)
    events.append(release_cue(2.4, room.label("ball")))
    # Dominoes from the ramp's foot along +Z, turning toward +X, small first so that the ball hits them high.
    x, zz, heading = ox, -1.6, -90.0  # Degrees from +X toward -Z: -90 is +Z.
    planks = []
    scale = 0.5
    travelled, next_at = 0.0, 0.0
    for length, turn in ((2.0, 0.0), (3.9, 90.0 / 3.9), (1.5, 0.0)):
        moved = 0.0
        while moved < length - 1e-6:
            if travelled >= next_at:
                planks.append((x, zz, math.cos(math.radians(heading)), -math.sin(math.radians(heading)), scale))
                next_at += 0.5 * 0.9 * scale
                scale = min(1.0, scale * 1.15)
            x += 0.05 * math.cos(math.radians(heading))
            zz -= 0.05 * math.sin(math.radians(heading))
            heading += turn * 0.05
            moved += 0.05
            travelled += 0.05
    hx, hz = math.cos(math.radians(heading)), -math.sin(math.radians(heading))
    for grown in (1.3, 1.7, 2.2):
        x += hx * 0.5 * 0.9 * scale
        zz += hz * 0.5 * 0.9 * scale
        scale = grown
        planks.append((x, zz, hx, hz, scale))
    for index, (px, pz, dx, dz, s) in enumerate(planks):
        # The last is a heavy beam, to slam the see-saw.
        mass = 100.0 if index == len(planks) - 1 else round(4.0 * s ** 3, 2)
        room.loose(f"plank {index + 1}", "Props/plank.gltf", (px, 0.0, pz),
                   {"size": [0.4, 0.9, 0.08], "center": [0.0, 0.45, 0.0], "mass": mass,
                    "surface": "wood"}, yaw(math.degrees(math.atan2(dx, dz))), [s] * 3 if s != 1.0 else None,
                   extent=0.5 * s)
    # The see-saw past the last plank, along +X, its near end up, far enough that the plank hits it falling fast.
    last_x, last_z, _, _, _ = planks[-1]
    tilt = -20.0
    board_turn = quat_axis([0, 0, 1], tilt)
    pivot = (last_x + 1.6 + 1.5 * math.cos(math.radians(tilt)), 0.56, last_z)
    room.box("rust", (pivot[0] - 0.15, 0.0, pivot[2] - 0.3), (pivot[0] + 0.15, 0.5, pivot[2] + 0.3), "see-saw block")
    room.loose("see-saw", "Props/seesaw_board.gltf", pivot, {"size": [3.0, 0.08, 0.45], "mass": 12.0,
                                                             "surface": "wood"}, board_turn,
               joints=[hinge(0, [0.0, 0.0, 0.0], [0.0, 0.0, 1.0])], extent=1.6)
    on_board = rotate(board_turn, [1.15, 0.04 + 0.3, 0.0])
    room.crate("thrown crate", [pivot[i] + on_board[i] for i in range(3)], 8.0)
    g.entities[-1]["transform"]["rotation"] = r(board_turn, 7)
    # The gas bottles where the crate lands, and the blast.
    bx, bz = FINALE_BOTTLES[0] + pivot[0], FINALE_BOTTLES[1] + pivot[2]
    for index, (dx, dz) in enumerate(((0.0, 0.0), (0.35, 0.3), (0.3, -0.35))):
        room.showcase(f"bottle {index + 1}", "propane_tank", (bx + dx, 0.0, bz + dz), 15.0, shape="cylinder")
    blast = room.marker("blast", (bx + 0.2, 0.5, bz))
    events += explosion(FINALE_BLAST, blast, 7.0, 260.0, 0.9)
    # Things for the blast to throw.
    room.showcase("drum", "Barrel_01", (bx + 1.6, 0.0, bz - 1.5), 20.0, shape="cylinder")
    room.showcase("drum 2", "barrel_03", (bx + 1.8, 0.0, bz + 1.4), 25.0, shape="cylinder")
    room.crate("crate", (bx - 0.5, 0.3, bz - 2.0), 15.0, turn=20.0)
    room.crate("crate 2", (bx - 0.3, 0.3, bz + 2.2), 15.0, turn=-10.0)
    room.crate("crate 3", (bx - 0.3, 0.9, bz + 2.2), 15.0, turn=10.0)
    dress(room, lamps=((-6.0, -5.0), (0.0, 1.0), (5.0, -2.0)), fill=35.0)
    room.light("spot", (-7.0, 6.8, 2.0), 1800.0, (1.0, 0.9, 0.78), 16.0, (0.5, -1.0, -1.2), 30, 55, 0.2,
               what="light on the ramp")
    room.light("spot", (pivot[0] - 1.0, 6.8, pivot[2] + 3.0), 2600.0, (1.0, 0.9, 0.78), 16.0, (0.3, -1.0, -0.6), 30,
               60, 0.2, what="light on the see-saw")
    room.shot(22.0, [
        (0.0, (-7.8, 2.2, 3.0), (ox, 1.0, -6.0), 1.2),
        (4.0, (-7.6, 1.8, 4.8), (ox + 0.6, 0.4, 0.0), 1.2),
        (8.0, (-3.0, 2.0, 7.0), (last_x - 1.0, 0.5, last_z), 1.2),
        (11.0, (0.0, 2.4, 7.6), (pivot[0], 0.6, pivot[2]), 1.2),
        (22.5, (-0.5, 3.0, 8.2), (pivot[0] + 1.5, 0.5, pivot[2]), 1.2),
    ], events)


# Where the finale's crate lands, from the see-saw's pivot, and when, from a recording of the room: the bottles
# stand there and blow then.
FINALE_BOTTLES = (2.0, 0.0)
FINALE_BLAST = 11.3


ROOMS = [swinging_room, falling_room, rolling_room, explosions_room, wrecking_room, stairs_room, pool_room,
         joints_room, machines_room, finale_room]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="list camera passes close to walls and props")
    parser.add_argument("--only", nargs="+", metavar="ROOM",
                        help="also write Sequences/physics_preview.sequence.json, the tour of these rooms alone, to "
                             "look at them without the others (not a file to keep)")
    parser.add_argument("--overview", action="store_true",
                        help="with --only: film the rooms from a high corner, to see where things go")
    args = parser.parse_args()
    g = Gallery("scripts/make_physics_gallery.py")
    for make in ROOMS:
        make(g)
    write_gltf(g, ROOT / "PhysicsGallery.gltf")
    write_level(g, ROOT / "PhysicsGallery.level.json")
    write_tour(g, ROOT / "Sequences" / "physics_tour.sequence.json", TOUR_COMMENT)
    if args.only:
        whole = g.tour
        if args.overview:
            g.overview = True
            g.tour = []
            for make in ROOMS:  # Again, for the shots from above; the level is written.
                make(g)
        g.tour = [shot for shot in g.tour if shot[0] in args.only]
        write_tour(g, ROOT / "Sequences" / "physics_preview.sequence.json", TOUR_COMMENT)
        g.tour = whole
    print(f"PhysicsGallery: {len(g.entities)} entities, {sum(len(m.indices) // 3 for m in g.meshes.values())} "
          f"triangles")
    if args.check:
        sys.exit(1 if check(g) else 0)


if __name__ == "__main__":
    main()
