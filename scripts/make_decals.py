#!/usr/bin/env python3
"""Writes the factory's decal images (ADR-0060), from noise and simple shapes, so they carry no outside licence.

    python scripts/make_decals.py

Into assets/Factory/Decals/, RGBA PNG with the coverage in alpha, colors in sRGB:
    blood_splatter.png      a splash with droplets around it, dark red
    blood_drips.png         a smear that runs down, longer than wide
    grime.png               dirt, densest at the center, for floors and wall bases
    mold.png                patches of greenish black mold
    scorch.png              a burn mark, black at the center, brown at its edge
    bullet_hole.png         a hole in plaster or stone, with chipped edges
    bullet_hole_normal.png  the normal map of its crater
    bullet_hole_metal.png   a dent in metal, bright at the rim
"""

import math
import random
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Decals"
SIZE = 256


def write_png(name, pixels):
    """pixels: function (u, v) -> (r, g, b, a), each 0 to 1, u and v from -1 to 1."""
    rows = []
    for y in range(SIZE):
        row = bytearray([0])
        for x in range(SIZE):
            u = (x + 0.5) / SIZE * 2.0 - 1.0
            v = (y + 0.5) / SIZE * 2.0 - 1.0
            for channel in pixels(u, v):
                row.append(max(0, min(255, int(round(channel * 255.0)))))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    (OUT / f"{name}.png").write_bytes(png)


class Noise:
    """Value noise in octaves, from a seed."""

    def __init__(self, seed):
        generator = random.Random(seed)
        self.table = [generator.random() for _ in range(256)]
        self.permutation = list(range(256))
        generator.shuffle(self.permutation)

    def lattice(self, x, y):
        return self.table[self.permutation[(self.permutation[x & 255] + y) & 255]]

    def value(self, x, y):
        x0, y0 = math.floor(x), math.floor(y)
        tx, ty = x - x0, y - y0
        sx, sy = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = self.lattice(x0, y0) + (self.lattice(x0 + 1, y0) - self.lattice(x0, y0)) * sx
        b = self.lattice(x0, y0 + 1) + (self.lattice(x0 + 1, y0 + 1) - self.lattice(x0, y0 + 1)) * sx
        return a + (b - a) * sy

    def fbm(self, x, y, octaves=5):
        total, amplitude, frequency, norm = 0.0, 1.0, 1.0, 0.0
        for _ in range(octaves):
            total += self.value(x * frequency, y * frequency) * amplitude
            norm += amplitude
            amplitude *= 0.5
            frequency *= 2.0
        return total / norm


def smooth(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def blood_splatter():
    noise = Noise(11)
    generator = random.Random(5)
    drops = [(generator.uniform(-0.85, 0.85), generator.uniform(-0.85, 0.85), generator.uniform(0.015, 0.06)) for _ in range(40)]

    def pixel(u, v):
        r = math.hypot(u, v)
        angle = math.atan2(v, u)
        edge = 0.42 + 0.22 * noise.fbm(math.cos(angle) * 2 + 3, math.sin(angle) * 2 + 3)
        coverage = 1.0 - smooth(edge - 0.04, edge + 0.02, r)
        for x, y, size in drops:
            if abs(u - x) < size * 2 and abs(v - y) < size * 2:
                coverage = max(coverage, 1.0 - smooth(size * 0.7, size, math.hypot(u - x, v - y)))
        darkness = 0.6 + 0.4 * noise.fbm(u * 6, v * 6)
        return (0.32 * darkness, 0.02 * darkness, 0.02 * darkness, coverage * 0.95)

    return pixel


def blood_drips():
    noise = Noise(23)

    def pixel(u, v):
        # A smear at the top, and drips that run down from it, each its own length.
        top = 1.0 - smooth(0.25, 0.4, math.hypot(u * 0.8, (v + 0.55) * 1.6))
        column = (u + 1.0) * 6.0
        index = math.floor(column)
        within = column - index
        length = 0.2 + 1.2 * noise.value(index * 1.7, 2.0)
        width = 0.18 + 0.12 * noise.value(index * 3.1, 5.0)
        drip = (1.0 - smooth(width * 0.6, width, abs(within - 0.5))) * (1.0 - smooth(-0.5 + length - 0.1, -0.5 + length, v))
        drip *= smooth(-0.6, -0.45, v) * (1.0 if noise.value(index * 2.3, 9.0) > 0.35 else 0.0)
        coverage = max(top, drip)
        shade = 0.55 + 0.45 * noise.fbm(u * 5, v * 9)
        return (0.28 * shade, 0.015 * shade, 0.015 * shade, coverage * 0.9)

    return pixel


def grime():
    noise = Noise(37)

    def pixel(u, v):
        density = noise.fbm(u * 3 + 7, v * 3 + 7) * (1.0 - smooth(0.3, 1.0, math.hypot(u, v)))
        speckle = noise.fbm(u * 24, v * 24, 3)
        coverage = smooth(0.15, 0.55, density + 0.25 * speckle)
        return (0.13, 0.11, 0.08, coverage * 0.85)

    return pixel


def mold():
    noise = Noise(41)

    def pixel(u, v):
        patches = noise.fbm(u * 4 + 1, v * 4 + 2) * (1.0 - smooth(0.5, 1.0, math.hypot(u, v)))
        coverage = smooth(0.3, 0.42, patches + 0.15 * noise.fbm(u * 30, v * 30, 2))
        tint = noise.value(u * 8, v * 8)
        return (0.06 + 0.04 * tint, 0.09 + 0.05 * tint, 0.04, coverage * 0.9)

    return pixel


def scorch():
    noise = Noise(53)

    def pixel(u, v):
        r = math.hypot(u, v) + 0.25 * (noise.fbm(u * 3, v * 3) - 0.5)
        coverage = 1.0 - smooth(0.55, 0.95, r)
        burnt = 1.0 - smooth(0.0, 0.6, r)
        color = (0.02 + 0.14 * (1 - burnt), 0.015 + 0.08 * (1 - burnt), 0.01 + 0.04 * (1 - burnt))
        return (*color, coverage)

    return pixel


def crater_height(u, v, noise):
    r = math.hypot(u, v) * (1.0 + 0.15 * (noise.fbm(u * 4, v * 4) - 0.5))
    return -1.0 + smooth(0.0, 0.35, r) * 1.0 + 0.25 * math.exp(-((r - 0.4) / 0.08) ** 2)


def bullet_hole():
    noise = Noise(61)

    def pixel(u, v):
        r = math.hypot(u, v) * (1.0 + 0.3 * (noise.fbm(u * 5, v * 5) - 0.5))
        hole = 1.0 - smooth(0.12, 0.16, r)
        chipped = 1.0 - smooth(0.2, 0.55, r)
        cracks = smooth(0.62, 0.7, noise.fbm(math.atan2(v, u) * 2 + 4, r * 3, 3)) * (1.0 - smooth(0.4, 0.9, r))
        coverage = max(hole, chipped * 0.8, cracks * 0.7)
        shade = 0.03 if hole > 0.5 else 0.35 + 0.15 * noise.value(u * 20, v * 20)
        return (shade, shade * 0.95, shade * 0.9, coverage)

    return pixel


def bullet_hole_normal():
    noise = Noise(61)
    step = 2.0 / SIZE

    def pixel(u, v):
        dx = (crater_height(u + step, v, noise) - crater_height(u - step, v, noise)) / (2 * step)
        dy = (crater_height(u, v + step, noise) - crater_height(u, v - step, noise)) / (2 * step)
        nx, ny, nz = -dx * 0.08, dy * 0.08, 1.0
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        return (nx / length * 0.5 + 0.5, ny / length * 0.5 + 0.5, nz / length * 0.5 + 0.5, 1.0)

    return pixel


def bullet_hole_metal():
    noise = Noise(67)

    def pixel(u, v):
        r = math.hypot(u, v)
        dent = 1.0 - smooth(0.1, 0.14, r)
        rim = math.exp(-((r - 0.18) / 0.05) ** 2)
        scratches = smooth(0.7, 0.8, noise.fbm(math.atan2(v, u) * 6, r * 2, 2)) * (1.0 - smooth(0.2, 0.6, r))
        coverage = max(dent, rim * 0.9, scratches * 0.5)
        shade = 0.05 if dent > 0.5 else 0.55 + 0.3 * rim
        return (shade, shade, shade * 1.02, coverage)

    return pixel


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, make in [("blood_splatter", blood_splatter), ("blood_drips", blood_drips), ("grime", grime),
                       ("mold", mold), ("scorch", scorch), ("bullet_hole", bullet_hole),
                       ("bullet_hole_normal", bullet_hole_normal), ("bullet_hole_metal", bullet_hole_metal)]:
        write_png(name, make())
        print(f"Wrote {name}.png")


if __name__ == "__main__":
    main()
