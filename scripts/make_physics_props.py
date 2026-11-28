#!/usr/bin/env python3
# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Writes the physics gallery's props (ADR-0128): the loose objects it needs that the showcase kit lacks.

    python -I scripts/make_physics_props.py [--tool path/to/texture-tool]

Their materials are seamless textures from Poly Haven (CC0), repeated at their real size: the script downloads
the ones it lacks (color, OpenGL normals, and occlusion-roughness-metal at 1k) into Props/Source, ignored by git,
converts them to KTX2 into Props/textures with texture-tool, and credits them in Props/TEXTURES.md. The door
keeps the factory's door; the oily lane is a plain dark gloss.

Writes, under assets/Factory/Props, a .gltf and .bin for each:
- plank: a domino, 40 cm wide, 90 cm high and 8 cm thick, standing on its origin (rough wood);
- seesaw_board and paddle: boards of the see-saw and the paddle wheel, about their center (rough wood);
- shelf_side and shelf_board: the glued shelf's parts, about their center (plywood);
- door: a steel door, 1 m wide and 2.1 m high, its hinge edge on its origin (the factory's door);
- sign: a painted steel plate 1.2 m by 0.5 m about its center;
- piston: the head of a piston, 0.6 m square and 0.8 m long about its center (painted steel);
- belt, lane and oily_lane: a conveyor's rubber belt, 7.7 m by 0.9 m, and lanes to push crates along, 7 m by
  1.2 m, of tread plate and of oily steel (dark and glossy), about their center;
- chain_link: an oval link of 8 mm steel bar, 16 cm long, about its center, long along Y (rusted steel);
- hook: a crane hook, its eye on its origin, 45 cm long (rusted steel);
- steel_ball: a wrecking ball of 45 cm radius, with an eye on top, about its center (rusted steel);
- rubber_ball: a red rubber ball of 15 cm radius;
- boulder: a lumpy rock about 2 m across, a little flat, about its center (rock);
- ball_red, ball_yellow, ball_blue, ball_green, ball_white: the pool's plastic balls, 16 cm across.

Their sizes are their real sizes: the level gives their bodies the same sizes (a box, a sphere), so a joint's
anchor is a point on the model.
"""

import argparse
import concurrent.futures
import hashlib
import json
import math
import struct
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "Factory" / "Props"
API = "https://api.polyhaven.com"
# The client says what it is, without anything about who runs it.
HEADERS = {"User-Agent": "daidalos-engine-next-asset-fetcher"}

# Poly Haven textures (CC0) and what they are made of, for impacts and sounds (ADR-0125).
TEXTURES = {
    "rough_wood": "wood",
    "plywood": "wood",
    "rusty_metal_02": "metal",  # White paint, rusting: the sign.
    "rust_coarse_01": "metal",  # Rusted steel: the ball, the hook, the chain.
    "blue_metal_plate": "metal",  # Painted machine steel: the piston.
    "rubber_tiles": "rubber",  # Black rubber in segments: the belt.
    "metal_plate": "metal",  # Tread plate: the dry lane.
    "rubberized_track": "rubber",  # Red rubber: the ball.
    "rock_face": "concrete",  # Weathered brown rock: the boulder.
}
# The factory's door (in assets/Factory), once across the door's face.
DOOR = ("door_", "metal", (1.0, 2.1))
# Plain colors, without a texture: linear color, roughness, surface.
PLAIN = {
    "oil": ([0.03, 0.025, 0.02], 0.08, "metal"),  # Steel under a film of oil: dark and glossy.
}
# The pool's balls, of glossy plastic in a ball pit's colors (linear).
BALL_COLORS = {
    "red": [0.62, 0.04, 0.03],
    "yellow": [0.8, 0.52, 0.03],
    "blue": [0.03, 0.15, 0.62],
    "green": [0.06, 0.42, 0.08],
    "white": [0.78, 0.78, 0.76],
}
PLAIN.update({f"plastic_{name}": (color, 0.3, "plastic") for name, color in BALL_COLORS.items()})
BALL_RADIUS = 0.08
TEXTURE_SIZE = 512  # Texels: the props are small.


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def vertex(self, p, n, uv):
        self.positions.append(tuple(p))
        self.normals.append(tuple(n))
        self.uvs.append(tuple(uv))
        return len(self.positions) - 1

    def box(self, low, high):
        """A box with outward faces, its texture coordinates in meters."""
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
                    corners.append(self.vertex(point, normal, (point[u_axis], -point[v_axis])))
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
                            (segment / segments * 2.0 * math.pi * radius, theta * radius))
        for ring in range(rings):
            for segment in range(segments):
                a = start + ring * (segments + 1) + segment
                b = a + segments + 1
                # Counterclockwise from outside.
                self.indices += [a, a + 1, b, a + 1, b + 1, b]

    def lumpy(self, radius, bumps, rings=16, segments=32):
        """A sphere of the radius whose surface rises and falls by bumps(direction), a factor near 1: a rock."""
        start = len(self.positions)

        def point(theta, phi):
            d = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            return [radius * bumps(d) * d[k] for k in range(3)]

        for ring in range(rings + 1):
            theta = math.pi * ring / rings
            for segment in range(segments + 1):
                phi = 2.0 * math.pi * segment / segments
                # The normal from the surface's slopes, a little off the poles, where phi does not move it.
                t = min(max(theta, 1e-3), math.pi - 1e-3)
                e = 1e-3
                dt = [b - a for a, b in zip(point(t - e, phi), point(t + e, phi))]
                dp = [b - a for a, b in zip(point(t, phi - e), point(t, phi + e))]
                n = normalize([dp[1] * dt[2] - dp[2] * dt[1], dp[2] * dt[0] - dp[0] * dt[2],
                               dp[0] * dt[1] - dp[1] * dt[0]])
                p = point(theta, phi)
                if sum(n[k] * p[k] for k in range(3)) < 0:
                    n = [-c for c in n]
                self.vertex(p, n, (segment / segments * 2.0 * math.pi * radius, theta * radius))
        for ring in range(rings):
            for segment in range(segments):
                a = start + ring * (segments + 1) + segment
                b = a + segments + 1
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
                            (length, s / sides * 2.0 * math.pi * radius))
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


def normalize(v):
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    return [c / length for c in v]


def boulder_bumps(d):
    """How far the boulder's surface stands out along direction d, about 1: a few broad lumps, flattened a little
    top and bottom, so that it tumbles more than it rolls."""
    x, y, z = d
    lumps = (0.09 * math.sin(2.3 * x + 0.7) * math.sin(2.9 * z + 1.9) + 0.07 * math.sin(3.7 * y + 2.6 * x + 0.3) +
             0.05 * math.sin(5.3 * z - 4.1 * y + 1.1) + 0.03 * math.sin(7.9 * x + 6.7 * z))
    return (1.0 + lumps) * (1.0 - 0.18 * y * y)


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


def get_json(url):
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url, path, md5=None):
    if path.exists() and (md5 is None or hashlib.md5(path.read_bytes()).hexdigest() == md5):
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=120) as response:
        data = response.read()
    if md5 is not None and hashlib.md5(data).hexdigest() != md5:
        raise RuntimeError(f"{url}: the download does not match its checksum")
    path.write_bytes(data)


def find_tool():
    names = ("texture-tool.exe", "texture-tool")
    candidates = [path for name in names for path in (ROOT / "build").glob(f"*/apps/texture-tool/**/{name}")]
    if not candidates:
        sys.exit("texture-tool not found under build/: build it, or pass --tool")
    candidates.sort(key=lambda path: ("Release" not in path.parts, -path.stat().st_mtime))
    return candidates[0]


# What each texture's maps are encoded as, and their names under Props/textures.
MAPS = (("Diffuse", "d", "color"), ("nor_gl", "n", "normal"), ("arm", "arm", "data"))


def fetch_textures(tool):
    """Downloads and converts the textures that are missing, and returns {id: (meters across, meters high)}.
    Writes Props/TEXTURES.md with their authors."""
    sizes, credits = {}, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        for texture in TEXTURES:
            info = get_json(f"{API}/info/{texture}")
            width, height = info.get("dimensions", [1000, 1000])
            sizes[texture] = (width / 1000.0, height / 1000.0)
            credits.append((info.get("name", texture), texture, ", ".join(info.get("authors", {}))))
            files = None
            jobs = []
            for slot, suffix, role in MAPS:
                output = OUT / "textures" / f"{texture}_{suffix}.ktx2"
                if output.exists():
                    continue
                files = files or get_json(f"{API}/files/{texture}")
                entry = files[slot]["1k"]["jpg"]
                source = OUT / "Source" / Path(entry["url"]).name

                def convert(entry=entry, source=source, output=output, role=role):
                    download(entry["url"], source, entry.get("md5"))
                    output.parent.mkdir(parents=True, exist_ok=True)
                    subprocess.run([str(tool), "--max-size", str(TEXTURE_SIZE), role, str(source), str(output)],
                                   check=True, capture_output=True)

                jobs.append(pool.submit(convert))
            for job in jobs:
                job.result()
    lines = ["# The physics gallery's prop textures", "",
             "From [Poly Haven](https://polyhaven.com), under CC0 1.0 (public domain): free for any use, without",
             "attribution. Their authors are credited here all the same. Fetched and converted by",
             "`scripts/make_physics_props.py` (ADR-0128).", "", "| Texture | Authors | Source |", "|---|---|---|"]
    lines += [f"| {name} (`{texture}`) | {authors} | https://polyhaven.com/a/{texture} |"
              for name, texture, authors in credits]
    (OUT / "TEXTURES.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return sizes


def material(kind, sizes):
    """The material of a kind, its images, and its repeat in meters across and high."""
    if kind in PLAIN:
        color, roughness, surface = PLAIN[kind]
        return {"name": kind, "pbrMetallicRoughness": {"baseColorFactor": color + [1.0], "metallicFactor": 0.0,
                                                       "roughnessFactor": roughness},
                "extras": {"surface": surface}}, [], (1.0, 1.0)
    if kind == "door":
        prefix, surface, repeat = DOOR
        images = [f"../{prefix}{suffix}.ktx2" for suffix in ("d", "n", "mrh")]
        metallic = 0.0
    else:
        surface, repeat = TEXTURES[kind], sizes[kind]
        images = [f"textures/{kind}_{suffix}.ktx2" for _, suffix, _ in MAPS]
        metallic = 1.0  # The map says how metal each texel is.
    return {
        "name": kind,
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2},
                                 "metallicFactor": metallic, "roughnessFactor": 1.0},
        "normalTexture": {"index": 1},
        "occlusionTexture": {"index": 2},
        "extras": {"surface": surface},
    }, images, repeat


def write(name, mesh, kind, sizes):
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

    made, images, repeat = material(kind, sizes)
    uvs = [(u / repeat[0], v / repeat[1]) for u, v in mesh.uvs]
    positions = [rounded(p) for p in mesh.positions]
    bounds = ([min(p[i] for p in positions) for i in range(3)], [max(p[i] for p in positions) for i in range(3)])
    attributes = {
        "POSITION": add(b"".join(struct.pack("<3f", *p) for p in positions), len(positions), "VEC3", 5126, 34962,
                        bounds),
        "NORMAL": add(b"".join(struct.pack("<3f", *rounded(n)) for n in mesh.normals), len(positions), "VEC3", 5126,
                      34962),
        "TEXCOORD_0": add(b"".join(struct.pack("<2f", *rounded(t)) for t in uvs), len(positions), "VEC2", 5126,
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
    gltf["materials"] = [made]
    if images:
        gltf["textures"] = [{"sampler": 0, "extensions": {"KHR_texture_basisu": {"source": i}}} for i in range(3)]
        gltf["images"] = [{"uri": uri, "mimeType": "image/ktx2"} for uri in images]
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
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tool", type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    sizes = fetch_textures(args.tool or find_tool())

    def write_prop(name, mesh, kind):
        write(name, mesh, kind, sizes)

    write_prop("plank", board((0.4, 0.9, 0.08), base=True), "rough_wood")
    write_prop("seesaw_board", board((3.0, 0.08, 0.45)), "rough_wood")
    write_prop("paddle", board((2.4, 0.08, 0.8)), "rough_wood")
    write_prop("shelf_side", board((0.06, 1.8, 0.45)), "plywood")
    write_prop("shelf_board", board((1.2, 0.05, 0.45)), "plywood")
    write_prop("sign", board((1.2, 0.5, 0.04)), "rusty_metal_02")
    write_prop("piston", board((0.8, 0.6, 0.6)), "blue_metal_plate")
    write_prop("belt", board((7.7, 0.1, 0.9)), "rubber_tiles")
    write_prop("lane", board((7.0, 0.04, 1.2)), "metal_plate")
    write_prop("oily_lane", board((7.0, 0.04, 1.2)), "oil")
    door = Mesh()
    door.box((0.0, 0.0, -0.03), (1.0, 2.1, 0.03))
    write_prop("door", door, "door")

    link = Mesh()
    link.tube(stadium(LINK_HALF_STRAIGHT, LINK_RADIUS), LINK_BAR, closed=True, sides=8)
    write_prop("chain_link", link, "rust_coarse_01")

    hook = Mesh()
    hook.tube(ring((0.0, -0.05), 0.04), 0.012, closed=True, sides=8)  # The eye.
    shank = [(0.0, -0.09 - 0.02 * i, 0.0) for i in range(11)]  # Down to -0.29.
    bend = [(0.11 - 0.11 * math.cos(math.radians(a)), -0.29 - 0.11 * math.sin(math.radians(a)), 0.0)
            for a in range(10, 200, 10)]  # Round the bottom and up the far side to the tip.
    hook.tube(shank + bend, 0.025, closed=False, sides=10)
    write_prop("hook", hook, "rust_coarse_01")

    ball = Mesh()
    ball.sphere((0.0, 0.0, 0.0), 0.45)
    ball.box((-0.04, 0.42, -0.04), (0.04, 0.52, 0.04))  # The neck.
    ball.tube(ring((0.0, 0.57), 0.05), 0.015, closed=True, sides=8)  # The eye the chain holds.
    write_prop("steel_ball", ball, "rust_coarse_01")

    rubber = Mesh()
    rubber.sphere((0.0, 0.0, 0.0), 0.15)
    write_prop("rubber_ball", rubber, "rubberized_track")

    boulder = Mesh()
    boulder.lumpy(1.0, boulder_bumps, rings=20, segments=40)
    write_prop("boulder", boulder, "rock_face")

    for color in BALL_COLORS:
        ball = Mesh()
        ball.sphere((0.0, 0.0, 0.0), BALL_RADIUS, rings=8, segments=16)
        write_prop(f"ball_{color}", ball, f"plastic_{color}")


if __name__ == "__main__":
    main()
