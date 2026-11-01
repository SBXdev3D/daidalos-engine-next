#!/usr/bin/env python3
"""Converts a level of the legacy Daidalos engine to glTF, for the sandbox.

    python scripts/import_legacy_level.py ../daidalos-engine/Resources [--scene factory]

The result goes to assets/<Scene>/, which git ignores: the origin of the legacy assets is unknown,
so they stay out of the repository (see docs/DEMO.md).

The static geometry is merged by material into one node, "level". Everything the game needs to find
is a node of its own, whose name holds its kind and attributes: "door;id=-5;radius=-1;closable=false".

Legacy scenes are left-handed, in units of about 3 per meter (the camera stood 4.7 above the floor).
They are converted to the engine's conventions: right-handed, in meters, -Z forward.
"""

import argparse
import json
import math
import shutil
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
METERS_PER_UNIT = 0.35
# Brushes that were markers for the legacy sky, or that are not visible geometry.
SKIPPED_TEXTURES = {"sky_d.png", "collision.png", "default.png"}
# Textures with holes in them.
ALPHA_TESTED_TEXTURES = {"grid_1d.png", "grid_2d.png", "bars_d.tga", "bars_d.png"}
# Node names in the legacy scene, and the kind that the game knows them by.
ENTITY_KINDS = {
    "door_entity": "door",
    "zombie_enemy_entity": "zombie",
    "handgun_ammo_entity": "handgun_ammo",
    "shotgun_ammo_entity": "shotgun_ammo",
    "health_bonus_entity": "health",
    "armor_bonus_entity": "armor",
    "flashlight_entity": "flashlight",
    "magnetic_card_entity": "card",
    "magnetic_card_reader_entity": "card_reader",
    "spot_entity": "spot",
    "alarm_entity": "alarm",
    "fan_entity": "fan",
    "security_camera_entity": "security_camera",
    "teleporter_entity": "teleporter",
    "area_trigger_entity": "area_trigger",
    "player": "player",
}
# Attributes that the game reads. Others stay behind.
ENTITY_ATTRIBUTES = {
    "triggerRadius": "radius",
    "closable": "closable",
    "color": "color",
    "health": "health",
    "patrol_waypoints": "patrol",
    "triggerNodeIds": "targets",
    "lightColor": "light_color",
    "lightRange": "light_range",
    "lightEnabled": "light_enabled",
}
# Kinds that are drawn as a model in place of their legacy animated mesh.
BOX_MESHES = {"door": "door_bbox.obj"}


def tga_to_png(source, target):
    """Converts an uncompressed or RLE 24 or 32-bit TGA: the engine does not read TGA."""
    data = source.read_bytes()
    id_length, color_map, image_type = data[0], data[1], data[2]
    width, height = struct.unpack_from("<2H", data, 12)
    depth, descriptor = data[16], data[17]
    if color_map != 0 or image_type not in (2, 10) or depth not in (24, 32):
        sys.exit(f"Unsupported TGA: {source}")
    size = depth // 8
    offset = 18 + id_length
    pixels = bytearray()
    if image_type == 2:
        pixels += data[offset:offset + width * height * size]
    else:
        while len(pixels) < width * height * size:
            packet = data[offset]
            offset += 1
            count = (packet & 0x7F) + 1
            if packet & 0x80:
                pixels += data[offset:offset + size] * count
                offset += size
            else:
                pixels += data[offset:offset + count * size]
                offset += count * size

    rows = bytearray()
    for row in range(height):
        y = row if descriptor & 0x20 else height - 1 - row
        rows.append(0)
        for x in range(width):
            pixel = pixels[(y * width + x) * size:(y * width + x + 1) * size]
            rows += bytes((pixel[2], pixel[1], pixel[0], pixel[3] if size == 4 else 255))

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    signature = bytes((0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A))
    target.write_bytes(signature + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0))
                       + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


def vector(text):
    return [float(value) for value in text.split(":")]


def rotation_matrix(degrees):
    """The legacy rotation (pitch, yaw, roll), with Direct3D's left-handed row-vector convention."""
    pitch, yaw, roll = (math.radians(value % 360.0) for value in degrees)
    cy, sy, cp, sp, cr, sr = (math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch),
                              math.cos(roll), math.sin(roll))
    return [
        [cr * cy + sr * sp * sy, sr * cp, -cr * sy + sr * sp * cy],
        [-sr * cy + cr * sp * sy, cr * cp, sr * sy + cr * sp * cy],
        [cp * sy, -sp, cp * cy],
    ]


def place(point, position, rotation, scale):
    x, y, z = (point[axis] * scale[axis] for axis in range(3))
    return [
        x * rotation[0][axis] + y * rotation[1][axis] + z * rotation[2][axis] + position[axis]
        for axis in range(3)
    ]


def to_engine(point):
    """Left-handed units to right-handed meters: mirroring Z reverses the winding of triangles."""
    return [point[0] * METERS_PER_UNIT, point[1] * METERS_PER_UNIT, -point[2] * METERS_PER_UNIT]


def read_obj(path):
    """Triangles as lists of three (position, uv)."""
    positions, uvs, triangles = [], [], []
    for line in path.read_text(encoding="latin-1").splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "v":
            positions.append([float(value) for value in parts[1:4]])
        elif parts[0] == "vt":
            uvs.append([float(value) for value in parts[1:3]])
        elif parts[0] == "f":
            corners = []
            for corner in parts[1:]:
                fields = corner.split("/")
                uv = uvs[int(fields[1]) - 1] if len(fields) > 1 and fields[1] else [0.0, 0.0]
                corners.append((positions[int(fields[0]) - 1], uv))
            for index in range(1, len(corners) - 1):
                triangles.append([corners[0], corners[index], corners[index + 1]])
    return triangles


def read_md2(path):
    """The triangles of the first frame of a Quake 2 model."""
    data = path.read_bytes()
    header = struct.unpack_from("<17i", data)
    if header[0] != 0x32504449:
        sys.exit(f"{path} is not an MD2 file")
    skin_width, skin_height = header[2], header[3]
    vertex_count, uv_count, triangle_count = header[6], header[7], header[8]
    uv_offset, triangle_offset, frame_offset = header[12], header[13], header[14]

    scale = struct.unpack_from("<3f", data, frame_offset)
    translate = struct.unpack_from("<3f", data, frame_offset + 12)
    positions = []
    for index in range(vertex_count):
        packed = struct.unpack_from("<3B", data, frame_offset + 56 + index * 4)
        positions.append([packed[axis] * scale[axis] + translate[axis] for axis in range(3)])
    uvs = [
        [s / skin_width, t / skin_height]
        for s, t in (struct.unpack_from("<2h", data, uv_offset + index * 4) for index in range(uv_count))
    ]
    triangles = []
    for index in range(triangle_count):
        values = struct.unpack_from("<6H", data, triangle_offset + index * 12)
        triangles.append([(positions[values[corner]], uvs[values[corner + 3]]) for corner in range(3)])
    return triangles


class Geometry:
    """Flat-shaded triangles of one material."""

    def __init__(self):
        self.positions = []
        self.normals = []
        self.uvs = []

    def add(self, triangles, position, rotation, scale):
        for triangle in triangles:
            # Mirrored into the right-handed world, so the corners are swapped.
            a, b, c = (to_engine(place(triangle[index][0], position, rotation, scale)) for index in (0, 2, 1))
            edge1 = [b[axis] - a[axis] for axis in range(3)]
            edge2 = [c[axis] - a[axis] for axis in range(3)]
            normal = [edge1[1] * edge2[2] - edge1[2] * edge2[1], edge1[2] * edge2[0] - edge1[0] * edge2[2],
                      edge1[0] * edge2[1] - edge1[1] * edge2[0]]
            length = math.sqrt(sum(value * value for value in normal))
            if length < 1e-9:
                continue
            normal = [value / length for value in normal]
            for point, corner in zip((a, b, c), (0, 2, 1)):
                self.positions.append(point)
                self.normals.append(normal)
                self.uvs.append(triangle[corner][1])


class GltfWriter:
    def __init__(self, source, output):
        self.source = source
        self.output = output
        self.buffer = bytearray()
        self.gltf = {"asset": {"version": "2.0", "generator": "import_legacy_level.py"}, "scene": 0,
                     "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "materials": [], "accessors": [],
                     "bufferViews": [], "textures": [], "images": [], "samplers": [{}]}
        self.materials = {}

    def accessor(self, values, kind, with_bounds):
        data = b"".join(struct.pack(f"<{len(value)}f", *value) for value in values)
        self.gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(self.buffer), "byteLength": len(data)})
        self.buffer += data
        accessor = {"bufferView": len(self.gltf["bufferViews"]) - 1, "componentType": 5126, "count": len(values),
                    "type": kind}
        if with_bounds:
            accessor["min"] = [min(value[axis] for value in values) for axis in range(3)]
            accessor["max"] = [max(value[axis] for value in values) for axis in range(3)]
        self.gltf["accessors"].append(accessor)
        return len(self.gltf["accessors"]) - 1

    def material(self, texture):
        if texture in self.materials:
            return self.materials[texture]
        source = self.source / "Textures" / texture
        if not source.is_file():
            sys.exit(f"Missing texture: {source}")
        image = texture
        if source.suffix.lower() == ".tga":
            image = f"{source.stem}.png"
            tga_to_png(source, self.output / image)
        else:
            shutil.copyfile(source, self.output / image)
        self.gltf["images"].append({"uri": image})
        self.gltf["textures"].append({"source": len(self.gltf["images"]) - 1, "sampler": 0})
        material = {"name": texture, "pbrMetallicRoughness": {
            "baseColorTexture": {"index": len(self.gltf["textures"]) - 1},
            "metallicFactor": 0.0, "roughnessFactor": 0.9}}
        if texture in ALPHA_TESTED_TEXTURES:
            material["alphaMode"] = "MASK"
            material["doubleSided"] = True
        self.gltf["materials"].append(material)
        self.materials[texture] = len(self.gltf["materials"]) - 1
        return self.materials[texture]

    def mesh(self, groups):
        primitives = []
        for texture, geometry in sorted(groups.items()):
            primitives.append({"attributes": {
                "POSITION": self.accessor(geometry.positions, "VEC3", True),
                "NORMAL": self.accessor(geometry.normals, "VEC3", False),
                "TEXCOORD_0": self.accessor(geometry.uvs, "VEC2", False),
            }, "material": self.material(texture)})
        self.gltf["meshes"].append({"primitives": primitives})
        return len(self.gltf["meshes"]) - 1

    def node(self, name, translation=None, rotation_y=0.0, mesh=None):
        node = {"name": name}
        if translation is not None:
            node["translation"] = translation
        if rotation_y:
            half = math.radians(rotation_y) / 2.0
            node["rotation"] = [0.0, math.sin(half), 0.0, math.cos(half)]
        if mesh is not None:
            node["mesh"] = mesh
        self.gltf["nodes"].append(node)
        self.gltf["scenes"][0]["nodes"].append(len(self.gltf["nodes"]) - 1)

    def write(self, name):
        self.gltf["buffers"] = [{"uri": f"{name}.bin", "byteLength": len(self.buffer)}]
        (self.output / f"{name}.bin").write_bytes(self.buffer)
        (self.output / f"{name}.gltf").write_text(json.dumps(self.gltf), encoding="utf-8")


def transform_of(node):
    def read(tag, default):
        element = node.find(tag)
        return vector(element.text) if element is not None and element.text else default
    return read("position", [0.0, 0.0, 0.0]), read("rotation", [0.0, 0.0, 0.0]), read("scale", [1.0, 1.0, 1.0])


def is_enabled(node):
    enabled = node.find("enabled")
    return enabled is None or (enabled.text or "").strip() != "false"


def texture_of(node):
    element = node.find("diffuse_texture")
    return element.text.strip() if element is not None and element.text else None


def read_triangles(models, file):
    path = models / file
    if path.suffix.lower() == ".obj":
        return read_obj(path)
    if path.suffix.lower() == ".md2":
        return read_md2(path)
    return None # Animated models come later (docs/DEMO.md).


def entity_name(kind, node):
    fields = [kind]
    if node.get("id") is not None:
        fields.append(f"id={node.get('id')}")
    attributes = node.find("attributes")
    if attributes is not None:
        for element in attributes:
            if element.tag in ENTITY_ATTRIBUTES and element.text:
                fields.append(f"{ENTITY_ATTRIBUTES[element.tag]}={element.text.strip()}")
    return ";".join(fields)


def convert(source, scene, output):
    models = source / "Models"
    root = ET.fromstring((source / "Scenes" / f"{scene}.des").read_text(encoding="latin-1"))
    writer = GltfWriter(source, output)
    level = {}
    cache = {}
    counts = {}

    def triangles_of(file):
        if file not in cache:
            cache[file] = read_triangles(models, file)
        return cache[file]

    def add_static(file, texture, position, rotation, scale):
        triangles = triangles_of(file)
        if triangles is None or texture is None or texture in SKIPPED_TEXTURES:
            return
        level.setdefault(texture, Geometry()).add(triangles, position, rotation_matrix(rotation), scale)

    def add_entity(kind, node, position, rotation, scale):
        mesh = None
        file = BOX_MESHES.get(kind, node.get("file"))
        texture = texture_of(node)
        if file and texture and texture not in SKIPPED_TEXTURES:
            triangles = triangles_of(file)
            if triangles is not None:
                # Local geometry: the mesh keeps the entity's rotation and scale, and the node its position.
                geometry = Geometry()
                geometry.add(triangles, [0.0, 0.0, 0.0], rotation_matrix(rotation), scale)
                mesh = writer.mesh({texture: geometry})
        writer.node(entity_name(kind, node), to_engine(position), mesh=mesh)
        counts[kind] = counts.get(kind, 0) + 1

    def walk(parent, origin):
        for node in parent:
            if node.tag == "multi_brush":
                walk(node, origin)
            elif node.tag == "brush":
                position, rotation, scale = transform_of(node)
                add_static(node.get("file"), texture_of(node), position, rotation, scale)
            elif node.tag == "node":
                position, rotation, scale = transform_of(node)
                absolute = [origin[axis] + position[axis] for axis in range(3)]
                kind = ENTITY_KINDS.get(node.get("name", ""))
                if not is_enabled(node):
                    pass
                elif kind is not None:
                    add_entity(kind, node, absolute, rotation, scale)
                elif node.get("type") == "5" and node.get("file"):
                    add_static(node.get("file"), texture_of(node), absolute, rotation, scale)
                walk(node, absolute)

    walk(root, [0.0, 0.0, 0.0])
    writer.node("level", mesh=writer.mesh(level))
    writer.write(scene.capitalize())
    triangles = sum(len(geometry.positions) // 3 for geometry in level.values())
    print(f"{scene}: {triangles} level triangles in {len(level)} materials, entities: {counts}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="Resources directory of the legacy engine")
    parser.add_argument("--scene", default="factory")
    arguments = parser.parse_args()

    output = ROOT / "assets" / arguments.scene.capitalize()
    output.mkdir(parents=True, exist_ok=True)
    convert(arguments.source, arguments.scene, output)


if __name__ == "__main__":
    main()
