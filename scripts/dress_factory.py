# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0

"""Dresses the factory with the effects of ADR-0053 (step 9.24): fire in the pit of the south room, drips,
a shorting cable, dust in the lamps' light, flies, moths, rain through the big room's open roof, and what
each material is made of, for impacts by surface.

    python scripts/dress_factory.py [assets/Factory]

It edits Factory.gltf in place, and can be run again: the nodes it adds carry {"dressing": true} in their
extras, and it removes them before adding them again. Materials get {"surface": ...} in their extras.

The committed level is no longer what scripts/import_legacy_level.py writes: later steps edited it (the
lamps' lights, the shaft's glass, emissive door lamps), so this script edits the committed file rather than
adding to the importer's lists. The Phase 10 editor takes over from both.

Meters, Y up, -Z ahead from the start; places were found with the level's triangles (floors, ceilings and
walls), and every node is named as a level's are (`particles;effect=...`, `weather;...`, `attractor;...`,
`ribbon;...`).
"""

import json
import math
import sys
from pathlib import Path

# What each material of the factory is made of: walls, floors and ceilings are stone and concrete, the
# crates wood except the one with metal straps, lamps and door lights have glass over them.
SURFACES = {
    "ceiling_2d.png": "concrete",
    "ground_1d.png": "concrete",
    "wall_2d.png": "concrete",
    "crate_1d.png": "wood",
    "crate_3d.png": "wood",
    "crate_2d.png": "metal",
    "shaft_glass": "glass",
    "door_light_green.tga": "glass",
    "door_light_red.tga": "glass",
    "glow_white.tga": "glass",
    "black.png": "metal",  # The railings, bars and fans, painted black.
    "door_d.png": "metal",
    "door_red_d.png": "metal",
    "magnetic_card_reader_d.png": "metal",
    "alarm_d.tga": "metal",
    "security_camera_d.tga": "metal",
    "ladder_d.png": "metal",
    "spot_off_d.png": "metal",
    "spot_white_d.tga": "metal",
    "grid_1d.png": "metal",
    "grid_2d.png": "metal",
    "lift_d.png": "metal",
    "lockers_d.png": "metal",
    "pipe_floor_d.png": "metal",
    "switch_d.tga": "metal",
    "vent_1d.png": "metal",
    "vent_slab_d.png": "metal",
    "ventilation_shaft_d.png": "metal",
}


def quat_from_up(direction):
    """The rotation that turns +Y to a direction, as (x, y, z, w)."""
    length = math.sqrt(sum(value * value for value in direction))
    d = [value / length for value in direction]
    axis = (d[2], 0.0, -d[0])  # cross((0, 1, 0), d)
    cosine = d[1]
    if cosine < -0.9999:
        return [1.0, 0.0, 0.0, 0.0]
    w = 1.0 + cosine
    norm = math.sqrt(axis[0] ** 2 + axis[2] ** 2 + w * w)
    return [axis[0] / norm, 0.0, axis[2] / norm, w / norm]


# The big room's roof is open over its middle (x -8.5 to 8.5, z -48 to -30.5), its edge 8.4 m up; the pit at
# its center goes down to -5.6 m.
OPENING = (-8.5, 8.5, -48.0, -30.5)
ROOF = 8.4


def dressing():
    """The nodes to add: (name, translation, rotation or None, scale or None)."""
    up = None
    toward_left = quat_from_up((-1.0, 0.0, 0.0))
    rain_top, rain_bottom = ROOF + 1.4, -1.4
    nodes = [
        # The pit of the south room, open to the sky at the end of the foggy corridor: a fire burns at its
        # bottom, 2.8 m down, and lights its walls; its smoke rises out of the pit.
        ("particles;effect=fire", (-0.8, -2.75, -87.0), up, None),
        ("particles;effect=fire", (1.0, -2.75, -88.2), up, None),
        ("particles;effect=fire", (0.2, -2.75, -86.0), up, None),
        ("particles;effect=fire", (-1.6, -2.75, -88.6), up, None),
        ("particles;effect=heat_haze", (0.0, -1.9, -87.5), up, None),
        ("particles;effect=embers", (0.0, -2.7, -87.5), up, None),
        ("particles;effect=smoke_column", (0.0, -2.2, -87.8), up, None),
        # The corridor to it: water drips from its ceiling, 2.8 m up, one of them through its lamp's light.
        ("particles;effect=drip", (0.35, 2.75, -66.4), up, None),
        ("particles;effect=drip", (-0.7, 2.75, -73.0), up, None),
        ("particles;effect=drip", (-0.4, 2.75, -73.6), up, None),
        # The corridor from the start room: a cut cable shorts on its right wall, before the first door.
        ("particles;effect=cable_sparks", (1.7, 2.45, -10.0), toward_left, None),
        ("ribbon;effect=arc;target=cable_arc_end", (1.7, 2.6, -10.0), up, None),
        ("cable_arc_end", (1.7, 2.1, -10.5), up, None),
        # Dust in the lamps' light: the start room, and the big room's entrance.
        ("particles;effect=dust_motes", (0.0, 1.6, -5.0), up, None),
        ("particles;effect=dust_motes", (0.0, 2.5, -18.0), up, None),
        # Moths around the lamp at the big room's entrance.
        ("particles;effect=moths", (0.0, 5.0, -17.0), up, None),
        ("attractor;effect=moths;strength=2;vortex=4;radius=1.5", (0.0, 5.2, -17.0), up, None),
        # Flies over the underground's green fog, where a zombie patrols, and in the last room, where zombies
        # wait.
        ("particles;effect=flies", (-36.75, -7.6, -55.0), up, None),
        ("attractor;effect=flies;strength=3;vortex=2;radius=2", (-36.75, -7.7, -55.0), up, None),
        ("particles;effect=flies", (45.5, 3.6, -82.0), up, None),
        ("attractor;effect=flies;strength=3;vortex=2;radius=2", (45.5, 3.5, -82.0), up, None),
        # Rain through the big room's open roof, from above its edge to below its floor, so that it falls
        # into the pit too; it splashes where it lands.
        ("weather;effect=rain;rate=30;reach=18",
         ((OPENING[0] + OPENING[1]) / 2, (rain_top + rain_bottom) / 2, (OPENING[2] + OPENING[3]) / 2), up,
         (OPENING[1] - OPENING[0], rain_top - rain_bottom, OPENING[3] - OPENING[2])),
    ]
    # Water running off the roof's edge, under it, around the opening.
    for x, z in ((-9.2, -33.0), (9.2, -36.5), (-9.2, -44.0), (9.2, -46.0), (-3.0, -29.8), (4.5, -48.7)):
        nodes.append(("particles;effect=drip", (x, ROOF - 0.05, z), up, None))
    return nodes


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/Factory")
    path = directory / "Factory.gltf"
    gltf = json.loads(path.read_text(encoding="utf-8"))

    # Remove what an earlier run added: its nodes are the last ones, and nothing points at them.
    nodes = gltf["nodes"]
    first = next((index for index, node in enumerate(nodes) if node.get("extras", {}).get("dressing")), len(nodes))
    if any(not node.get("extras", {}).get("dressing") for node in nodes[first:]):
        sys.exit("Dressing nodes must be the last ones of Factory.gltf")
    del nodes[first:]
    scene = gltf["scenes"][0]
    scene["nodes"] = [index for index in scene["nodes"] if index < first]

    for name, translation, rotation, scale in dressing():
        node = {"name": name, "translation": [round(value, 4) for value in translation],
                "extras": {"dressing": True}}
        if rotation is not None:
            node["rotation"] = [round(value, 7) for value in rotation]
        if scale is not None:
            node["scale"] = [round(value, 4) for value in scale]
        nodes.append(node)
        scene["nodes"].append(len(nodes) - 1)

    unknown = []
    for material in gltf["materials"]:
        extras = material.setdefault("extras", {})
        extras.pop("surface", None)
        if material["name"] in SURFACES:
            extras["surface"] = SURFACES[material["name"]]
        else:
            unknown.append(material["name"])
        if not extras:
            del material["extras"]

    path.write_text(json.dumps(gltf), encoding="utf-8")
    print(f"{path}: {len(dressing())} nodes, surfaces for {len(gltf['materials']) - len(unknown)} materials; "
          f"none for {', '.join(unknown)}")


if __name__ == "__main__":
    main()
