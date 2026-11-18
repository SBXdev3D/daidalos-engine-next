#!/usr/bin/env python3
"""Writes the factory's loose crate, a 60 cm box with the crate textures, as a glTF prop (ADR-0073).

    python scripts/make_crate.py

Writes assets/Factory/Props/crate.gltf and crate.bin: 24 vertices (4 a face, each face its own normal and
texture coordinates) and 36 indices. The material uses the factory's crate_1 textures, with their relief.
"""

import base64
import json
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "Factory" / "Props"
HALF = 0.3

# Each face: its normal, and two axes across it (u, v), so that the texture faces out the right way.
FACES = [
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
]


def main():
    positions, normals, uvs, indices = [], [], [], []
    for normal, u, v in FACES:
        first = len(positions)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append(tuple(HALF * (normal[i] + su * u[i] + sv * v[i]) for i in range(3)))
            normals.append(normal)
            uvs.append(((su + 1) / 2, (1 - sv) / 2))
        indices += [first, first + 1, first + 2, first, first + 2, first + 3]
    data = b"".join(struct.pack("<3f", *p) for p in positions)
    data += b"".join(struct.pack("<3f", *n) for n in normals)
    data += b"".join(struct.pack("<2f", *t) for t in uvs)
    data += b"".join(struct.pack("<H", i) for i in indices)
    count = len(positions)
    gltf = {
        "asset": {"version": "2.0", "generator": "make_crate.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "crate", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3,
                                    "material": 0}]}],
        "materials": [{
            "name": "crate",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2},
                                     "metallicFactor": 0.0, "roughnessFactor": 1.0},
            "normalTexture": {"index": 1},
            "occlusionTexture": {"index": 2, "strength": 0.81},
            "extras": {"surface": "wood", "height": 0.006},
        }],
        "textures": [{"sampler": 0, "extensions": {"KHR_texture_basisu": {"source": i}}} for i in range(3)],
        "images": [{"uri": f"../crate_1{suffix}.ktx2", "mimeType": "image/ktx2"} for suffix in ("d", "n", "mrh")],
        "samplers": [{}],
        "extensionsUsed": ["KHR_texture_basisu"],
        "extensionsRequired": ["KHR_texture_basisu"],
        "buffers": [{"uri": "crate.bin", "byteLength": len(data)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": count * 12, "target": 34962},
            {"buffer": 0, "byteOffset": count * 12, "byteLength": count * 12, "target": 34962},
            {"buffer": 0, "byteOffset": count * 24, "byteLength": count * 8, "target": 34962},
            {"buffer": 0, "byteOffset": count * 32, "byteLength": len(indices) * 2, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3",
             "min": [-HALF] * 3, "max": [HALF] * 3},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "crate.bin").write_bytes(data)
    (OUT / "crate.gltf").write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8")
    print(f"Wrote {OUT / 'crate.gltf'}")


if __name__ == "__main__":
    main()
