# Daidalos Engine Next
# Copyright (C) 2011-2026 Stéphane Baudoux
# SPDX-License-Identifier: MIT OR Apache-2.0

"""Writes the meshes of mesh particles, the images of ribbons and the sprites of small particles (ADR-0053), so
that the engine owns them.

    python scripts/make_particle_assets.py [assets/Factory/Effects]

Meshes are glTF binaries at their real size, in meters, lying flat: their +Y is what faces up when they rest,
and their origin is their middle. Ribbon images are white, their opacity across the ribbon (v) a soft profile.
"""

import json
import math
import random
import struct
import sys
import zlib
from pathlib import Path


def write_glb(path, positions, normals, uvs, indices, material):
    """One mesh of one primitive, with one material."""
    def pack(fmt, values):
        return b"".join(struct.pack(fmt, *value) if isinstance(value, tuple) else struct.pack(fmt, value)
                        for value in values)

    blobs = [pack("<3f", positions), pack("<3f", normals), pack("<2f", uvs), pack("<I", indices)]
    views, offset = [], 0
    for blob, target in zip(blobs, [34962, 34962, 34962, 34963]):
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob), "target": target})
        offset += len(blob)
    low = [min(p[i] for p in positions) for i in range(3)]
    high = [max(p[i] for p in positions) for i in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "scripts/make_particle_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": path.stem}],
        "meshes": [{"name": path.stem, "primitives": [
            {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [material],
        "buffers": [{"byteLength": offset}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": low, "max": high},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
    }
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = b"".join(blobs)
    binary += b"\0" * (-len(binary) % 4)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + binary
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks)


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def casing():
    """A pistol casing, 9 by 19 mm, lying along X: a tube, open at the mouth, closed at the base."""
    radius, half, sides = 0.00475, 0.0095, 12
    positions, normals, uvs, indices = [], [], [], []
    for x, u in ((-half, 0.0), (half, 1.0)):
        for i in range(sides + 1):
            angle = 2 * math.pi * i / sides
            y, z = math.cos(angle), math.sin(angle)
            positions.append((x, y * radius, z * radius))
            normals.append((0.0, y, z))
            uvs.append((u, i / sides))
    for i in range(sides):
        a, b = i, i + 1
        c, d = a + sides + 1, b + sides + 1
        indices += [a, b, c, b, d, c]
    # The base and the mouth, flat; the mouth darker inside is left to the light.
    for x, nx in ((-half, -1.0), (half, 1.0)):
        center = len(positions)
        positions.append((x, 0.0, 0.0))
        normals.append((nx, 0.0, 0.0))
        uvs.append((0.5, 0.5))
        for i in range(sides + 1):
            angle = 2 * math.pi * i / sides
            positions.append((x, math.cos(angle) * radius, math.sin(angle) * radius))
            normals.append((nx, 0.0, 0.0))
            uvs.append((0.5 + 0.5 * math.cos(angle), 0.5 + 0.5 * math.sin(angle)))
        for i in range(sides):
            a, b = center + 1 + i, center + 2 + i
            indices += [center, b, a] if nx < 0 else [center, a, b]
    return positions, normals, uvs, indices


def plate(outline, thickness):
    """A flat plate in the XZ plane through an outline, counterclockwise seen from +Y, with its edges."""
    positions, normals, uvs, indices = [], [], [], []
    count = len(outline)
    for y, ny in ((thickness / 2, 1.0), (-thickness / 2, -1.0)):
        start = len(positions)
        for x, z in outline:
            positions.append((x, y, z))
            normals.append((0.0, ny, 0.0))
            uvs.append((x * 5 + 0.5, z * 5 + 0.5))
        for i in range(1, count - 1):
            indices += [start, start + i + 1, start + i] if ny > 0 else [start, start + i, start + i + 1]
    for i in range(count):
        (x0, z0), (x1, z1) = outline[i], outline[(i + 1) % count]
        normal = normalize((z1 - z0, 0.0, -(x1 - x0)))
        start = len(positions)
        for x, z, y in ((x0, z0, thickness / 2), (x1, z1, thickness / 2), (x0, z0, -thickness / 2),
                        (x1, z1, -thickness / 2)):
            positions.append((x, y, z))
            normals.append(normal)
            uvs.append((0.0, 0.0))
        indices += [start, start + 1, start + 2, start + 1, start + 3, start + 2]
    return positions, normals, uvs, indices


def shard():
    """A shard of glass, a few centimeters across, 3 mm thick."""
    return plate([(-0.02, 0.012), (0.004, -0.018), (0.024, -0.004), (0.006, 0.02)], 0.003)


def paper():
    """A sheet of paper, 15 by 10 cm, as thin as a plane, seen from both sides."""
    positions = [(-0.075, 0.0, -0.05), (0.075, 0.0, -0.05), (0.075, 0.0, 0.05), (-0.075, 0.0, 0.05)]
    normals = [(0.0, 1.0, 0.0)] * 4
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    return positions, normals, uvs, [0, 2, 1, 0, 3, 2]


def lump(seed, size, flatness):
    """A broken lump: a box of size meters across, its corners pushed about, flat-faced, flattened by flatness."""
    rng = random.Random(seed)
    corners = [(x * size * 0.5 * rng.uniform(0.6, 1.0), y * size * 0.5 * flatness * rng.uniform(0.6, 1.0),
                z * size * 0.5 * rng.uniform(0.6, 1.0)) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
    # The six faces of the box, by corner (x, y, z bits), counterclockwise from outside.
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    positions, normals, uvs, indices = [], [], [], []
    for face in faces:
        for a, b, c in ((face[0], face[1], face[2]), (face[0], face[2], face[3])):
            pa, pb, pc = corners[a], corners[b], corners[c]
            e1 = tuple(pb[i] - pa[i] for i in range(3))
            e2 = tuple(pc[i] - pa[i] for i in range(3))
            n = normalize((e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]))
            start = len(positions)
            for p in (pa, pb, pc):
                positions.append(p)
                normals.append(n)
                uvs.append((p[0] / size + 0.5, p[2] / size + 0.5))
            indices += [start, start + 1, start + 2]
    return positions, normals, uvs, indices


def splinter():
    """A splinter of wood, 8 cm long, pointed at both ends, 4 mm thick."""
    return plate([(-0.04, 0.0), (-0.01, -0.005), (0.035, -0.003), (0.04, 0.001), (0.0, 0.006)], 0.004)


def atlas_cell(index, x, y):
    """The sprites of particles, in a grid of 4 by 4 cells: a color and opacity from -1 to 1 across the cell."""
    r = math.hypot(x, y)
    def soft(radius, power=2.0):
        return max(0.0, 1.0 - (r / radius) ** power)
    if index == 0:  # A raindrop's streak, along the cell's height: stretched along the drop's motion.
        return 1.0, 1.0, 1.0, max(0.0, 1.0 - (abs(x) / 0.18) ** 2) * max(0.0, 1.0 - abs(y) ** 4) * 0.8
    if index == 1:  # A ripple: a thin ring, lying on water.
        return 1.0, 1.0, 1.0, max(0.0, 1.0 - abs(r - 0.75) / 0.12) * 0.9
    if index == 2:  # A splash: droplets thrown up and out.
        value = 0.0
        for k in range(7):
            angle = k * 2.0 * math.pi / 7.0 + 0.4
            cx, cy = 0.55 * math.cos(angle), 0.55 * math.sin(angle) * 0.8 - 0.1
            value = max(value, max(0.0, 1.0 - math.hypot(x - cx, y - cy) / 0.16))
        return 1.0, 1.0, 1.0, min(1.0, value * 1.5)
    if index == 3:  # A soft dot: a mote of dust, a spore.
        return 1.0, 1.0, 1.0, soft(1.0) ** 2
    if index == 4:  # A snowflake: a soft star of six arms.
        arms = abs(math.cos(3.0 * math.atan2(y, x))) ** 8
        return 1.0, 1.0, 1.0, min(1.0, soft(0.9) * (0.35 + 0.65 * arms) + soft(0.25))
    if index == 5:  # A flake of ash: a grey irregular scrap.
        edge = 0.6 + 0.15 * math.sin(5.0 * math.atan2(y, x)) + 0.1 * math.sin(11.0 * math.atan2(y, x))
        return 0.5, 0.5, 0.5, 1.0 if r < edge else 0.0
    if index == 6:  # An ember: a white-hot core in a glow, which the effect's color tints.
        return 1.0, 1.0, 1.0, min(1.0, soft(0.3) + 0.5 * soft(1.0) ** 3)
    if index == 7:  # A bubble: a ring with a highlight.
        ring = max(0.0, 1.0 - abs(r - 0.8) / 0.12)
        highlight = max(0.0, 1.0 - math.hypot(x + 0.35, y + 0.35) / 0.2)
        return 1.0, 1.0, 1.0, min(1.0, 0.15 * soft(0.85, 8) + 0.7 * ring + highlight)
    if index in (8, 9):  # A fly, wings out then in: a dark body, grey wings.
        body = (x / 0.18) ** 2 + (y / 0.38) ** 2 < 1.0
        spread = 0.55 if index == 8 else 0.25
        wing = any(((x - side * spread) / 0.35) ** 2 + ((y + 0.1) / 0.18) ** 2 < 1.0 for side in (-1, 1))
        if body:
            return 0.03, 0.03, 0.02, 1.0
        return (0.5, 0.5, 0.55, 0.45) if wing else (0.0, 0.0, 0.0, 0.0)
    if index == 10:  # A moth: a pale body with broad wings.
        wing = any(((x - side * 0.45) / 0.45) ** 2 + (y / 0.55) ** 2 < 1.0 for side in (-1, 1))
        body = (x / 0.12) ** 2 + (y / 0.5) ** 2 < 1.0
        return (0.75, 0.7, 0.6, 1.0) if wing or body else (0.0, 0.0, 0.0, 0.0)
    if index == 11:  # A drop of water, round, for drips and spray.
        return 1.0, 1.0, 1.0, min(1.0, soft(0.6, 4) * 0.8 + max(0.0, 1.0 - math.hypot(x + 0.2, y + 0.2) / 0.15))
    return 0.0, 0.0, 0.0, 0.0


def write_atlas(path, cell=64, grid=4):
    def pixel(px, py):
        index = (py // cell) * grid + px // cell
        x = ((px % cell) + 0.5) / cell * 2.0 - 1.0
        y = ((py % cell) + 0.5) / cell * 2.0 - 1.0
        color = atlas_cell(index, x, y)
        return tuple(max(0, min(255, round(c * 255))) for c in color)
    write_png(path, cell * grid, cell * grid, pixel)


def write_png(path, width, height, pixel):
    """An RGBA image, pixel(x, y) giving four bytes."""
    rows = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def profile(v):
    """Opacity across a ribbon: a bright core with a soft glow around it, nothing at the edges."""
    d = abs(v - 0.5) * 2.0
    return min(1.0, math.exp(-(d / 0.18) ** 2) + 0.45 * math.exp(-(d / 0.5) ** 2)) * (1.0 - d ** 4)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/Factory/Effects")
    meshes = out / "Meshes"
    meshes.mkdir(parents=True, exist_ok=True)
    brass = {"name": "Brass", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.58, 0.25, 1.0],
                                                        "metallicFactor": 1.0, "roughnessFactor": 0.3}}
    glass = {"name": "Broken glass", "pbrMetallicRoughness": {"baseColorFactor": [0.04, 0.06, 0.05, 1.0],
                                                               "metallicFactor": 0.0, "roughnessFactor": 0.05}}
    sheet = {"name": "Paper", "doubleSided": True,
             "pbrMetallicRoughness": {"baseColorFactor": [0.72, 0.69, 0.6, 1.0], "metallicFactor": 0.0,
                                      "roughnessFactor": 0.95}}
    write_glb(meshes / "casing.glb", *casing(), brass)
    write_glb(meshes / "shard.glb", *shard(), glass)
    write_glb(meshes / "paper.glb", *paper(), sheet)
    concrete = {"name": "Concrete", "pbrMetallicRoughness": {"baseColorFactor": [0.42, 0.41, 0.39, 1.0],
                                                             "metallicFactor": 0.0, "roughnessFactor": 0.95}}
    wood = {"name": "Wood", "pbrMetallicRoughness": {"baseColorFactor": [0.45, 0.3, 0.16, 1.0], "metallicFactor": 0.0,
                                                     "roughnessFactor": 0.8}}
    write_glb(meshes / "chip.glb", *lump(3, 0.02, 0.6), concrete)
    write_glb(meshes / "debris.glb", *lump(5, 0.08, 0.7), concrete)
    write_glb(meshes / "splinter.glb", *splinter(), wood)
    write_atlas(out / "sprites.png")

    alpha = lambda v: max(0, min(255, round(profile(v) * 255)))
    write_png(out / "ribbon_glow.png", 8, 64, lambda x, y: (255, 255, 255, alpha((y + 0.5) / 64)))
    # Streaks along the ribbon, which repeat: brighter and dimmer stretches that a scroll moves along.
    rng = random.Random(7)
    waves = [(rng.randint(1, 6), rng.random() * 2 * math.pi, rng.random()) for _ in range(5)]
    def streak(x):
        u = (x + 0.5) / 128
        value = sum(weight * math.sin(2 * math.pi * k * u + phase) for k, phase, weight in waves)
        return 0.65 + 0.35 * math.tanh(value)
    write_png(out / "ribbon_streaks.png", 128, 64,
              lambda x, y: (255, 255, 255, max(0, min(255, round(profile((y + 0.5) / 64) * streak(x) * 255)))))


if __name__ == "__main__":
    main()
