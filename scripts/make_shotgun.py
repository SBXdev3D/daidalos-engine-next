#!/usr/bin/env python3
"""Builds the shotgun of the demo, which the legacy game had in its code but never as a model:

    python scripts/make_shotgun.py

It writes assets/Factory/shotgun.gltf and shotgun.bin: boxes and cylinders in two materials, metal and
wood, with the pump as a node of its own. The muzzle points down -Z, +Y is up, the origin is where the
hand holds the grip, and the units are meters. Two clips: "fire" kicks and pumps, "reload" tilts the
gun to load shells and pumps at the end.
"""

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "assets" / "Factory"

METAL = {"name": "shotgun_metal", "pbrMetallicRoughness": {
    "baseColorFactor": [0.28, 0.28, 0.3, 1.0], "metallicFactor": 0.8, "roughnessFactor": 0.45}}
WOOD = {"name": "shotgun_wood", "pbrMetallicRoughness": {
    "baseColorFactor": [0.42, 0.24, 0.11, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.8}}

PUMP_REST = (0.0, 0.004, -0.42)


class Mesh:
    """Flat-shaded triangles, each turned to face outward."""

    def __init__(self):
        self.positions = []
        self.normals = []

    def triangle(self, a, b, c, outward):
        edge1 = [b[i] - a[i] for i in range(3)]
        edge2 = [c[i] - a[i] for i in range(3)]
        normal = [edge1[1] * edge2[2] - edge1[2] * edge2[1], edge1[2] * edge2[0] - edge1[0] * edge2[2],
                  edge1[0] * edge2[1] - edge1[1] * edge2[0]]
        length = math.sqrt(sum(value * value for value in normal))
        if length < 1e-12:
            return
        normal = [value / length for value in normal]
        if sum(normal[i] * outward[i] for i in range(3)) < 0.0:
            b, c = c, b
            normal = [-value for value in normal]
        for point in (a, b, c):
            self.positions.append(tuple(point))
            self.normals.append(tuple(normal))

    def quad(self, a, b, c, d, outward):
        self.triangle(a, b, c, outward)
        self.triangle(a, c, d, outward)

    def hexahedron(self, corners):
        """Eight corners: the four of the back face (+Z), then the four of the front face, both in the
        order left-bottom, right-bottom, right-top, left-top. Faces may taper."""
        center = [sum(corner[i] for corner in corners) / 8.0 for i in range(3)]
        faces = [(0, 1, 2, 3), (5, 4, 7, 6), (4, 0, 3, 7), (1, 5, 6, 2), (3, 2, 6, 7), (4, 5, 1, 0)]
        for face in faces:
            points = [corners[index] for index in face]
            middle = [sum(point[i] for point in points) / 4.0 for i in range(3)]
            self.quad(*points, [middle[i] - center[i] for i in range(3)])

    def box(self, low, high):
        x0, y0, z0 = low
        x1, y1, z1 = high
        self.hexahedron([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
                         (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)])

    def cylinder(self, center, radius, front, back, segments=14):
        """Along Z, from z = front to z = back, around (x, y) = center."""
        cx, cy = center
        ring = [(cx + radius * math.cos(2 * math.pi * k / segments), cy + radius * math.sin(2 * math.pi * k / segments))
                for k in range(segments)]
        for k in range(segments):
            (x0, y0), (x1, y1) = ring[k], ring[(k + 1) % segments]
            outward = ((x0 + x1) / 2 - cx, (y0 + y1) / 2 - cy, 0.0)
            self.quad((x0, y0, front), (x1, y1, front), (x1, y1, back), (x0, y0, back), outward)
            self.triangle((cx, cy, front), (x0, y0, front), (x1, y1, front), (0.0, 0.0, -1.0))
            self.triangle((cx, cy, back), (x0, y0, back), (x1, y1, back), (0.0, 0.0, 1.0))


def build_body():
    metal, wood = Mesh(), Mesh()
    metal.cylinder((0.0, 0.034), 0.012, -0.78, -0.14)       # Barrel.
    metal.cylinder((0.0, 0.006), 0.011, -0.66, -0.14)       # Magazine tube, under the barrel.
    metal.box((-0.006, 0.046, -0.775), (0.006, 0.054, -0.76))  # Front sight.
    metal.box((-0.023, -0.022, -0.15), (0.023, 0.05, 0.07))  # Receiver.
    metal.box((-0.007, -0.052, -0.03), (0.007, -0.022, -0.024))  # Trigger guard, front.
    metal.box((-0.007, -0.056, -0.03), (0.007, -0.05, 0.04))  # Trigger guard, bottom.
    metal.box((-0.003, -0.045, 0.0), (0.003, -0.022, 0.006))  # Trigger.
    # The stock, from the back of the receiver down to the shoulder.
    wood.hexahedron([(-0.022, -0.115, 0.44), (0.022, -0.115, 0.44), (0.022, 0.02, 0.44), (-0.022, 0.02, 0.44),
                     (-0.02, -0.02, 0.07), (0.02, -0.02, 0.07), (0.02, 0.045, 0.07), (-0.02, 0.045, 0.07)])
    return metal, wood


def build_pump():
    wood = Mesh()
    wood.cylinder((0.0, 0.0), 0.021, -0.095, 0.095) # Around the node, which the clips move.
    return wood


class Writer:
    def __init__(self):
        self.buffer = bytearray()
        self.gltf = {"asset": {"version": "2.0", "generator": "make_shotgun.py"}, "scene": 0,
                     "scenes": [{"nodes": [0]}], "nodes": [], "meshes": [], "materials": [METAL, WOOD],
                     "accessors": [], "bufferViews": [], "animations": []}

    def accessor(self, values, kind, with_bounds=False):
        data = b"".join(struct.pack(f"<{len(value)}f", *value) for value in values)
        self.buffer += bytes(-len(self.buffer) % 4)
        self.gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(self.buffer), "byteLength": len(data)})
        self.buffer += data
        accessor = {"bufferView": len(self.gltf["bufferViews"]) - 1, "componentType": 5126, "count": len(values),
                    "type": kind}
        if with_bounds:
            accessor["min"] = [min(value[i] for value in values) for i in range(len(values[0]))]
            accessor["max"] = [max(value[i] for value in values) for i in range(len(values[0]))]
        self.gltf["accessors"].append(accessor)
        return len(self.gltf["accessors"]) - 1

    def mesh(self, name, parts):
        primitives = [{"attributes": {"POSITION": self.accessor(mesh.positions, "VEC3", True),
                                      "NORMAL": self.accessor(mesh.normals, "VEC3")}, "material": material}
                      for mesh, material in parts]
        self.gltf["meshes"].append({"name": name, "primitives": primitives})
        return len(self.gltf["meshes"]) - 1

    def animation(self, name, tracks):
        """tracks: (node, path, [(time, value), ...]), linear."""
        samplers, channels = [], []
        for node, path, keys in tracks:
            times = self.accessor([(time,) for time, _ in keys], "SCALAR", True)
            values = self.accessor([value for _, value in keys], "VEC4" if path == "rotation" else "VEC3")
            samplers.append({"input": times, "output": values, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path}})
        self.gltf["animations"].append({"name": name, "samplers": samplers, "channels": channels})

    def write(self, name):
        self.gltf["buffers"] = [{"uri": f"{name}.bin", "byteLength": len(self.buffer)}]
        (OUTPUT / f"{name}.bin").write_bytes(self.buffer)
        (OUTPUT / f"{name}.gltf").write_text(json.dumps(self.gltf), encoding="utf-8")


def turn(axis, degrees):
    half = math.radians(degrees) / 2.0
    return tuple(value * math.sin(half) for value in axis) + (math.cos(half),)


def combine(a, b):
    """The rotation a, then b, as quaternions (x, y, z, w): the Hamilton product b * a."""
    bx, by, bz, bw = b
    ax, ay, az, aw = a
    return (bw * ax + bx * aw + by * az - bz * ay, bw * ay - bx * az + by * aw + bz * ax,
            bw * az + bx * ay - by * ax + bz * aw, bw * aw - bx * ax - by * ay - bz * az)


def main():
    writer = Writer()
    metal, wood = build_body()
    body_mesh = writer.mesh("shotgun_body", [(metal, 0), (wood, 1)])
    pump_mesh = writer.mesh("shotgun_pump", [(build_pump(), 1)])
    writer.gltf["nodes"] = [
        {"name": "shotgun", "children": [1]},
        {"name": "body", "mesh": body_mesh, "children": [2]},
        {"name": "pump", "mesh": pump_mesh, "translation": list(PUMP_REST)},
    ]

    rest = (0.0, 0.0, 0.0)
    level = (0.0, 0.0, 0.0, 1.0)
    kick = turn((1.0, 0.0, 0.0), 9.0)
    back = (PUMP_REST[0], PUMP_REST[1], PUMP_REST[2] + 0.09)
    writer.animation("fire", [
        (1, "translation", [(0.0, rest), (0.05, (0.0, 0.012, 0.07)), (0.3, rest), (1.0, rest)]),
        (1, "rotation", [(0.0, level), (0.05, kick), (0.3, level), (1.0, level)]),
        (2, "translation", [(0.0, PUMP_REST), (0.4, PUMP_REST), (0.58, back), (0.78, PUMP_REST), (1.0, PUMP_REST)]),
    ])
    # Tilted to the side and down, a shell pushed in three times, then back and pumped.
    tilt = combine(turn((0.0, 0.0, 1.0), 28.0), turn((1.0, 0.0, 0.0), 12.0))
    low = (0.0, -0.07, 0.03)
    push = (0.0, -0.06, 0.03)
    writer.animation("reload", [
        (1, "translation", [(0.0, rest), (0.35, low), (0.8, push), (0.95, low), (1.4, push), (1.55, low),
                            (2.0, push), (2.15, low), (2.45, rest), (2.8, rest)]),
        (1, "rotation", [(0.0, level), (0.35, tilt), (2.15, tilt), (2.45, level), (2.8, level)]),
        (2, "translation", [(0.0, PUMP_REST), (2.45, PUMP_REST), (2.6, back), (2.75, PUMP_REST), (2.8, PUMP_REST)]),
    ])
    writer.write("shotgun")
    triangles = sum(len(mesh.positions) for mesh in (metal, wood)) // 3 + len(build_pump().positions) // 3
    print(f"shotgun: {triangles} triangles, clips fire and reload")


if __name__ == "__main__":
    main()
