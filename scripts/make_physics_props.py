#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Writes the physics gallery's props (ADR-0128): the loose objects it needs that the showcase kit lacks, textured
with the factory's materials, as make_crate.py writes the crate.

    python scripts/make_physics_props.py

Writes, under assets/Factory/Props, a .gltf and .bin for each:
- plank: a domino, 40 cm wide, 90 cm high and 8 cm thick, standing on its origin (wood);
- board, seesaw_board, paddle, shelf_side and shelf_board: boards of the see-saw, the paddle wheel and the glued
  shelf, about their center (wood);
- door: a steel door, 1 m wide and 2.1 m high, its hinge edge on its origin (door);
- sign: a steel plate 1.2 m by 0.5 m about its center (steel);
- piston: the head of a piston, 0.6 m square and 0.8 m long about its center (metal);
- belt, lane and oily_lane: a conveyor's belt, 7.7 m by 0.9 m (metal), and lanes to push crates along, 7 m by
  1.2 m, of steel and of oily steel (dark and glossy), about their center;
- chain_link: an oval link of 8 mm steel bar, 16 cm long, about its center, long along Y (metal);
- hook: a crane hook, its eye on its origin, 45 cm long (steel);
- steel_ball: a wrecking ball of 45 cm radius, with an eye on top, about its center (steel);
- rubber_ball: a red rubber ball of 15 cm radius (no texture).

Their sizes are their real sizes: the level gives their bodies the same sizes (a box, a sphere), so a joint's
anchor is a point on the model.
"""

import json
import math
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Props"

# Materials: the factory's textures (in assets/Factory), or a plain color.
MATERIALS = {
    "wood": ("crate_1", "wood"),
    "steel": ("crate_2", "metal"),
    "metal": ("lift_", "metal"),
    "door": ("door_", "metal"),
}
# Plain colors, without a texture: linear color, roughness, surface.
PLAIN = {
    "rubber": ([0.55, 0.06, 0.04], 0.55, "rubber"),
    "oil": ([0.03, 0.025, 0.02], 0.08, "metal"),  # Steel under a film of oil: dark and glossy.
}
TILE = 0.6  # Meters per texture repeat: the crate's face.


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def vertex(self, p, n, uv):
        self.positions.append(tuple(p))
        self.normals.append(tuple(n))
        self.uvs.append(tuple(uv))
        return len(self.positions) - 1

    def box(self, low, high):
        """A box with outward faces, its texture TILE meters a repeat."""
        for axis in range(3):
            for side in (0, 1):
                normal = [0.0, 0.0, 0.0]
                normal[axis] = 1.0 if side else -1.0
                u_axis, v_axis = [a for a in range(3) if a != axis]
                corners = []
                for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    point = [0.0, 0.0, 0.0]
                    point[axis] = high[axis] if side else low[axis]
                    point[u_axis] = high[u_axis] if u else low[u_axis]
                    point[v_axis] = high[v_axis] if v else low[v_axis]
                    corners.append(self.vertex(point, normal, (point[u_axis] / TILE, -point[v_axis] / TILE)))
                a, b, c, d = corners
                self.face([a, b, c, d], normal)

    def face(self, quad, normal):
        """Two triangles of a quad, wound to face along the normal."""
        a, b, c, d = quad
        pa, pb, pc = self.positions[a], self.positions[b], self.positions[c]
        e1 = [pb[k] - pa[k] for k in range(3)]
        e2 = [pc[k] - pa[k] for k in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(n[k] * normal[k] for k in range(3)) >= 0:
            self.indices += [a, b, c, a, c, d]
        else:
            self.indices += [a, c, b, a, d, c]

    def sphere(self, center, radius, rings=16, segments=32):
        start = len(self.positions)
        for ring in range(rings + 1):
            theta = math.pi * ring / rings
            for segment in range(segments + 1):
                phi = 2.0 * math.pi * segment / segments
                n = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
                self.vertex([center[k] + radius * n[k] for k in range(3)], n,
                            (segment / segments * 2.0 * math.pi * radius / TILE, theta * radius / TILE))
        for ring in range(rings):
            for segment in range(segments):
                a = start + ring * (segments + 1) + segment
                b = a + segments + 1
                # Counterclockwise from outside.
                self.indices += [a, a + 1, b, a + 1, b + 1, b]

    def tube(self, path, radius, closed, sides=10):
        """A bar of the given radius along a path in the XY plane."""
        start = len(self.positions)
        count = len(path)
        length = 0.0
        for i, p in enumerate(path):
            before = path[(i - 1) % count] if closed or i > 0 else p
            after = path[(i + 1) % count] if closed or i < count - 1 else p
            t = [after[k] - before[k] for k in range(3)]
            n = math.sqrt(sum(c * c for c in t))
            t = [c / n for c in t]
            side = [t[1], -t[0], 0.0]  # Across the path, in its plane.
            if i > 0:
                length += math.dist(p, path[i - 1])
            for s in range(sides + 1):
                angle = 2.0 * math.pi * s / sides
                normal = [side[k] * math.cos(angle) + (1.0 if k == 2 else 0.0) * math.sin(angle) for k in range(3)]
                self.vertex([p[k] + radius * normal[k] for k in range(3)], normal,
                            (length / TILE, s / sides * 2.0 * math.pi * radius / TILE))
        rings = count if closed else count - 1
        for i in range(rings):
            for s in range(sides):
                a = start + i * (sides + 1) + s
                b = start + ((i + 1) % count) * (sides + 1) + s
                quad = [a, a + 1, b + 1, b]
                outward = self.normals[a]
                self.face(quad, outward)
        if not closed:
            # Flat ends.
            for i, inward in ((0, 1), (count - 1, -1)):
                t = [path[min(i + inward, count - 1) if inward > 0 else i][k] -
                     path[i if inward > 0 else i - 1][k] for k in range(3)]
                n = math.sqrt(sum(c * c for c in t))
                normal = [-inward * c / n for c in t] if inward > 0 else [c / n for c in t]
                ring = [start + i * (sides + 1) + s for s in range(sides)]
                center = self.vertex(path[i], normal, (0.0, 0.0))
                rim = [self.vertex(self.positions[v], normal, (0.0, 0.0)) for v in ring]
                for s in range(sides):
                    a, b = rim[s], rim[(s + 1) % sides]
                    pa, pb, pc = self.positions[center], self.positions[a], self.positions[b]
                    e1 = [pb[k] - pa[k] for k in range(3)]
                    e2 = [pc[k] - pa[k] for k in range(3)]
                    cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                             e1[0] * e2[1] - e1[1] * e2[0])
                    if sum(cross[k] * normal[k] for k in range(3)) >= 0:
                        self.indices += [center, a, b]
                    else:
                        self.indices += [center, b, a]


def stadium(half_straight, radius, steps=8):
    """An oval in the XY plane, long along Y: two straights joined by half circles."""
    path = []
    for end, sign in ((half_straight, 1.0), (-half_straight, -1.0)):
        for i in range(steps + 1):
            angle = math.pi * i / steps
            path.append((sign * radius * math.cos(angle), end + sign * radius * math.sin(angle), 0.0))
    return path


def ring(center, radius, steps=16):
    return [(center[0] + radius * math.cos(2 * math.pi * i / steps), center[1] + radius * math.sin(2 * math.pi * i / steps),
             0.0) for i in range(steps)]


def material(kind):
    if kind in PLAIN:
        color, roughness, surface = PLAIN[kind]
        return {"name": kind, "pbrMetallicRoughness": {"baseColorFactor": color + [1.0], "metallicFactor": 0.0,
                                                       "roughnessFactor": roughness},
                "extras": {"surface": surface}}
    texture, surface = MATERIALS[kind]
    return {
        "name": kind,
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2},
                                 "metallicFactor": 0.0, "roughnessFactor": 1.0},
        "normalTexture": {"index": 1},
        "occlusionTexture": {"index": 2, "strength": 0.81},
        "extras": {"surface": surface, "height": 0.006},
    }, texture


def write(name, mesh, kind):
    data = bytearray()
    views, accessors = [], []

    def add(blob, count, element, component, target, bounds=None):
        while len(data) % 4:
            data.append(0)
        views.append({"buffer": 0, "byteOffset": len(data), "byteLength": len(blob), "target": target})
        data.extend(blob)
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": element}
        if bounds:
            accessor["min"], accessor["max"] = bounds
        accessors.append(accessor)
        return len(accessors) - 1

    def rounded(v):
        return [round(c, 6) + 0.0 for c in v]

    positions = [rounded(p) for p in mesh.positions]
    bounds = ([min(p[i] for p in positions) for i in range(3)], [max(p[i] for p in positions) for i in range(3)])
    attributes = {
        "POSITION": add(b"".join(struct.pack("<3f", *p) for p in positions), len(positions), "VEC3", 5126, 34962,
                        bounds),
        "NORMAL": add(b"".join(struct.pack("<3f", *rounded(n)) for n in mesh.normals), len(positions), "VEC3", 5126,
                      34962),
        "TEXCOORD_0": add(b"".join(struct.pack("<2f", *rounded(t)) for t in mesh.uvs), len(positions), "VEC2", 5126,
                          34962),
    }
    indices = add(b"".join(struct.pack("<I", i) for i in mesh.indices), len(mesh.indices), "SCALAR", 5125, 34963)
    gltf = {
        "asset": {"version": "2.0", "generator": "scripts/make_physics_props.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": [{"attributes": attributes, "indices": indices, "material": 0}]}],
    }
    if kind in PLAIN:
        gltf["materials"] = [material(kind)]
    else:
        made, texture = material(kind)
        gltf["materials"] = [made]
        gltf["textures"] = [{"sampler": 0, "extensions": {"KHR_texture_basisu": {"source": i}}} for i in range(3)]
        gltf["images"] = [{"uri": f"../{texture}{suffix}.ktx2", "mimeType": "image/ktx2"} for suffix in ("d", "n", "mrh")]
        gltf["samplers"] = [{}]
        gltf["extensionsUsed"] = ["KHR_texture_basisu"]
        gltf["extensionsRequired"] = ["KHR_texture_basisu"]
    gltf["buffers"] = [{"uri": f"{name}.bin", "byteLength": len(data)}]
    gltf["bufferViews"] = views
    gltf["accessors"] = accessors
    (OUT / f"{name}.bin").write_bytes(bytes(data))
    (OUT / f"{name}.gltf").write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"{name}: {len(positions)} vertices, {len(mesh.indices) // 3} triangles, "
          f"{[round(b, 3) for b in bounds[0]]} to {[round(b, 3) for b in bounds[1]]}")


def board(size, base=False):
    """A box of the size about its center, or standing on its origin."""
    mesh = Mesh()
    low = [-size[0] / 2, 0.0 if base else -size[1] / 2, -size[2] / 2]
    mesh.box(low, [low[i] + size[i] for i in range(3)])
    return mesh


# The chain link: the bar's radius, and the oval's inner half length and radius. Links hang 11.2 cm apart: a
# link's inner length, where the next one passes through it.
LINK_BAR = 0.008
LINK_HALF_STRAIGHT = 0.032
LINK_RADIUS = 0.032  # Of the oval's center line; the link is 8 cm wide outside, 16 cm long outside.


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write("plank", board((0.4, 0.9, 0.08), base=True), "wood")
    write("seesaw_board", board((3.0, 0.08, 0.45)), "wood")
    write("paddle", board((2.4, 0.08, 0.8)), "wood")
    write("shelf_side", board((0.06, 1.8, 0.45)), "wood")
    write("shelf_board", board((1.2, 0.05, 0.45)), "wood")
    write("sign", board((1.2, 0.5, 0.04)), "steel")
    write("piston", board((0.8, 0.6, 0.6)), "metal")
    write("belt", board((7.7, 0.1, 0.9)), "metal")
    write("lane", board((7.0, 0.04, 1.2)), "steel")
    write("oily_lane", board((7.0, 0.04, 1.2)), "oil")
    door = Mesh()
    door.box((0.0, 0.0, -0.03), (1.0, 2.1, 0.03))
    write("door", door, "door")

    link = Mesh()
    link.tube(stadium(LINK_HALF_STRAIGHT, LINK_RADIUS), LINK_BAR, closed=True, sides=8)
    write("chain_link", link, "metal")

    hook = Mesh()
    hook.tube(ring((0.0, -0.05), 0.04), 0.012, closed=True, sides=8)  # The eye.
    shank = [(0.0, -0.09 - 0.02 * i, 0.0) for i in range(11)]  # Down to -0.29.
    bend = [(0.11 - 0.11 * math.cos(math.radians(a)), -0.29 - 0.11 * math.sin(math.radians(a)), 0.0)
            for a in range(10, 200, 10)]  # Round the bottom and up the far side to the tip.
    hook.tube(shank + bend, 0.025, closed=False, sides=10)
    write("hook", hook, "steel")

    ball = Mesh()
    ball.sphere((0.0, 0.0, 0.0), 0.45)
    ball.box((-0.04, 0.42, -0.04), (0.04, 0.52, 0.04))  # The neck.
    ball.tube(ring((0.0, 0.57), 0.05), 0.015, closed=True, sides=8)  # The eye the chain holds.
    write("steel_ball", ball, "steel")

    rubber = Mesh()
    rubber.sphere((0.0, 0.0, 0.0), 0.15)
    write("rubber_ball", rubber, "rubber")


if __name__ == "__main__":
    main()
