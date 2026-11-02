#!/usr/bin/env python3
"""Regenerates the small assets of the tests, which are committed:

    python tests/assets/generate.py
"""

import base64
import json
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def write_png(path, width, height, rgba):
    def chunk(kind, data):
        checksum = zlib.crc32(kind + data)
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)

    rows = b"".join(b"\0" + bytes(rgba[y * width * 4 : (y + 1) * width * 4]) for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    )


def quad_buffer():
    """A quad facing +Z from -1 to 1. Returns the bytes and the views of its attributes."""
    positions = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    normals = [(0, 0, 1)] * 4
    uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
    indices = [0, 1, 2, 0, 2, 3]

    data = b"".join(struct.pack("<3f", *p) for p in positions)
    data += b"".join(struct.pack("<3f", *n) for n in normals)
    data += b"".join(struct.pack("<2f", *u) for u in uvs)
    data += struct.pack("<6H", *indices)
    return data


def quad_gltf(image_uri=None, with_normals=True):
    data = quad_buffer()
    views = [
        {"buffer": 0, "byteOffset": 0, "byteLength": 48},
        {"buffer": 0, "byteOffset": 48, "byteLength": 48},
        {"buffer": 0, "byteOffset": 96, "byteLength": 32},
        {"buffer": 0, "byteOffset": 128, "byteLength": 12},
    ]
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
    ]
    attributes = {"POSITION": 0, "TEXCOORD_0": 2}
    if with_normals:
        attributes["NORMAL"] = 1
    material = {
        "name": "Checker",
        "doubleSided": True,
        "alphaMode": "MASK",
        "alphaCutoff": 0.25,
        "pbrMetallicRoughness": {"baseColorFactor": [1, 0.5, 0.25, 1], "metallicFactor": 0.1, "roughnessFactor": 0.9},
    }
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Parent", "translation": [1, 2, 3], "children": [1]},
            {"name": "Child", "mesh": 0, "scale": [2, 2, 2]},
        ],
        "meshes": [{"primitives": [{"attributes": attributes, "indices": 3, "material": 0}]}],
        "materials": [material],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [
            {
                "byteLength": len(data),
                "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode(),
            }
        ],
    }
    if image_uri is not None:
        material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0}
        gltf["textures"] = [{"source": 0}]
        gltf["images"] = [{"uri": image_uri}]
    return gltf


def skinned_gltf():
    """The quad, skinned to two joints: "Hip" at the origin holds its bottom, and "Hand" one meter up
    holds its top. A third node, outside the scene, is moved by a channel that must be ignored.
    Animations: "lift" raises the hand linearly, "snap" turns the hip in steps, "spline" moves the hip
    with cubic spline keys."""
    data = bytearray(quad_buffer())

    def add(payload):
        data.extend(bytes(-len(data) % 4))
        offset = len(data)
        data.extend(payload)
        return {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}

    def floats(values):
        return b"".join(struct.pack("<f", value) for value in values)

    views = [
        {"buffer": 0, "byteOffset": 0, "byteLength": 48},
        {"buffer": 0, "byteOffset": 48, "byteLength": 48},
        {"buffer": 0, "byteOffset": 96, "byteLength": 32},
        {"buffer": 0, "byteOffset": 128, "byteLength": 12},
        # Bottom vertices on the hip, top ones on the hand. The last one has weights that do not add up.
        add(struct.pack("<16B", 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)),
        add(floats([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0])),
        # Column-major inverse bind matrices: identity, then one meter down.
        add(floats([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] + [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1])),
        add(floats([0, 1, 2])),
        add(floats([0, 1, 0, 0, 2, 0, 0, 3, 0])),
        add(floats([0, 1])),
        add(floats([0, 0, 0, 1, 0, 0.7071068, 0, 0.7071068])),
        # Cubic spline: in-tangent, value, out-tangent for each of two keys.
        add(floats([9, 9, 9, 1, 0, 0, 9, 9, 9, 9, 9, 9, 2, 0, 0, 9, 9, 9])),
    ]
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
        {"bufferView": 4, "componentType": 5121, "count": 4, "type": "VEC4"},
        {"bufferView": 5, "componentType": 5126, "count": 4, "type": "VEC4"},
        {"bufferView": 6, "componentType": 5126, "count": 2, "type": "MAT4"},
        {"bufferView": 7, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0], "max": [2]},
        {"bufferView": 8, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 9, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
        {"bufferView": 10, "componentType": 5126, "count": 2, "type": "VEC4"},
        {"bufferView": 11, "componentType": 5126, "count": 6, "type": "VEC3"},
    ]
    return {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Character", "children": [2, 1]},
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "Hip", "children": [3]},
            {"name": "Hand", "translation": [0, 1, 0]},
            {"name": "Elsewhere"},
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "JOINTS_0": 4,
                                                   "WEIGHTS_0": 5}, "indices": 3}]}],
        "skins": [{"joints": [2, 3], "inverseBindMatrices": 6}],
        "animations": [
            {"name": "lift", "samplers": [{"input": 7, "output": 8}],
             "channels": [{"sampler": 0, "target": {"node": 3, "path": "translation"}},
                          {"sampler": 0, "target": {"node": 4, "path": "translation"}}]},
            {"name": "snap", "samplers": [{"input": 9, "output": 10, "interpolation": "STEP"}],
             "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}]},
            {"name": "spline", "samplers": [{"input": 9, "output": 11, "interpolation": "CUBICSPLINE"}],
             "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}}]},
        ],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [
            {
                "byteLength": len(data),
                "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(data)).decode(),
            }
        ],
    }


def main():
    # Left column red, right column blue, with an opaque top row and a half transparent bottom row.
    write_png(HERE / "checker.png", 2, 2, [255, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0, 128, 0, 0, 255, 128])

    def save(name, gltf):
        (HERE / name).write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8")

    save("quad.gltf", quad_gltf("checker.png"))
    save("second-quad.gltf", quad_gltf("checker.png"))
    save("no-normals.gltf", quad_gltf(with_normals=False))
    save("missing-texture.gltf", quad_gltf("no-such-image.png"))
    broken = quad_gltf()
    broken["nodes"][1]["children"] = [0]
    save("cycle.gltf", broken)
    save("skinned.gltf", skinned_gltf())
    (HERE / "broken.gltf").write_text("{ this is not JSON\n", encoding="utf-8")


if __name__ == "__main__":
    main()
