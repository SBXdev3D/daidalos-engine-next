#!/usr/bin/env python3
"""Converts a level of the legacy Daidalos engine to glTF, for the sandbox.

    python scripts/import_legacy_level.py ../daidalos-engine/Resources [--scene factory]

The sandbox does not need this script: its result, assets/Factory/, is committed. It is only useful to
convert the level again, or to bring more of the legacy content over. The level's sounds are copied too.

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
    "ladder_entity": "ladder",
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
# Animated models, each baked to a glTF file of its own with its texture. Their level nodes keep their
# rotation and name the file with "model=", and the game puts the model there. The scale is the one
# the legacy game gave them, baked in. Clips are the frame ranges the legacy game code played, with
# its speeds in frames per second.
ANIMATED_MODELS = {
    "zombie.ms3d": {"texture": "zombie_d.png", "scale": 0.24, "clips": {
        "walk": (22, 36, 8.0), "wait": (137, 168, 10.0), "attack": (116, 128, 8.0), "die": (91, 103, 9.0)}},
    "fan.ms3d": {"texture": "black.png", "scale": 2.0, "clips": {"spin": (1, 72, 80.0)}},
    # Not in the level: the player holds it.
    "handgun.ms3d": {"texture": "handgun_d.png", "scale": 3.0, "clips": {"fire": (1, 3, 5.0), "reload": (4, 6, 1.0)}},
}

# Normal maps by diffuse texture. The level's are read from its <normal_texture> elements; the
# handgun's was set by the legacy game code (CHandgun.cpp), because the player holds it.
NORMAL_MAPS = {"handgun_d.png": "handgun_n.png"}
# What the legacy level wrote where a brush had no normal map.
NO_NORMAL_MAP = {"black.png", "white.png", "default.png"}
# Every legacy brush used the "default" material of Materials/default.mtl, with a Blinn-Phong shininess
# Ns of 100. Its GGX equivalent: alpha = sqrt(2 / (Ns + 2)), and the glTF roughness is sqrt(alpha).
LEGACY_ROUGHNESS = round(math.sqrt(math.sqrt(2.0 / (100.0 + 2.0))), 3)

# Entities the legacy level lacks, in the engine's meters. The legacy game had a shotgun in its code and
# shotgun shells in this level, but nothing to pick the shotgun up from: it lies next to the shells.
# Its model comes from scripts/make_shotgun.py.
EXTRA_ENTITIES = [("shotgun;model=shotgun.gltf", (-15.75, 0.0, -21.0))]

# Two leftover tests, and the 31 MB ambient track, which waits for a compressed format in Phase 6.
# enter_the_maze.wav is 31 MB: it was converted once, and only enter_the_maze.ogg is committed.
SKIPPED_SOUNDS = {"test.wav", "test2.wav", "enter_the_maze.wav"}
# Authored right-handed and read left-handed by the legacy engine, which showed their lettering
# backwards. Their shape is symmetric in X, so mirroring them only corrects the texture.
MIRRORED_MODELS = {"lockers.obj"}


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
            # OBJ counts V from the bottom of the image, and the engine from the top.
            uvs.append([float(parts[1]), 1.0 - float(parts[2])])
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
    """The triangles of the first frame of a Quake 2 model, in the space of the legacy engine."""
    data = path.read_bytes()
    header = struct.unpack_from("<17i", data)
    if header[0] != 0x32504449:
        sys.exit(f"{path} is not an MD2 file")
    skin_width, skin_height = header[2], header[3]
    vertex_count, uv_count, triangle_count = header[6], header[7], header[8]
    uv_offset, triangle_offset, frame_offset = header[12], header[13], header[14]

    scale = struct.unpack_from("<3f", data, frame_offset)
    translate = struct.unpack_from("<3f", data, frame_offset + 12)
    # A frame starts with its scale, its translation and a name of 16 characters.
    vertices_offset = frame_offset + 12 + 12 + 16
    # As CMD2Mesh did: Quake's Z is up, then a quarter turn and one unit up.
    turn = rotation_matrix([0.0, 90.0, 0.0])
    positions = []
    for index in range(vertex_count):
        packed = struct.unpack_from("<3B", data, vertices_offset + index * 4)
        x, z, y = (packed[axis] * scale[axis] + translate[axis] for axis in range(3))
        positions.append(place([x, y, z], [0.0, 1.0, 0.0], turn, [1.0, 1.0, 1.0]))
    uvs = [
        [s / skin_width, t / skin_height]
        for s, t in (struct.unpack_from("<2h", data, uv_offset + index * 4) for index in range(uv_count))
    ]
    triangles = []
    for index in range(triangle_count):
        values = struct.unpack_from("<6H", data, triangle_offset + index * 12)
        triangles.append([(positions[values[corner]], uvs[values[corner + 3]]) for corner in range(3)])
    return triangles


def legacy_product(a, b):
    """CQuaternion::operator*, as the legacy engine wrote it: quaternions are (x, y, z, w)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (bw * ax + bx * aw + by * az - bz * ay,
            bw * ay + by * aw + bz * ax - bx * az,
            bw * az + bz * aw + bx * ay - by * ax,
            bw * aw - bx * ax - by * ay - bz * az)


def legacy_euler(angles):
    """CQuaternion::YawPitchRoll, from radians around X, Y and Z."""
    def around(axis, angle):
        sine = math.sin(angle / 2.0)
        return tuple(value * sine for value in axis) + (math.cos(angle / 2.0),)
    rotations = [around(axis, angle) for axis, angle in zip(((1, 0, 0), (0, 1, 0), (0, 0, 1)), angles)]
    return legacy_product(legacy_product(rotations[2], rotations[0]), rotations[1])


def legacy_matrix(rotation, translation):
    """CMatrix::RotationQuaternion, then the translation: a row-vector matrix, as rows."""
    x, y, z, w = rotation
    return [
        [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0],
        [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0],
        [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0],
        [translation[0], translation[1], translation[2], 1.0],
    ]


def multiply(a, b):
    return [[sum(a[row][k] * b[k][column] for k in range(4)) for column in range(4)] for row in range(4)]


def slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0.0:
        b, dot = tuple(-value for value in b), -dot
    if dot > 0.9995:
        result = [x + (y - x) * t for x, y in zip(a, b)]
    else:
        angle = math.acos(dot)
        wa, wb = math.sin((1.0 - t) * angle) / math.sin(angle), math.sin(t * angle) / math.sin(angle)
        result = [x * wa + y * wb for x, y in zip(a, b)]
    length = math.sqrt(sum(value * value for value in result))
    return tuple(value / length for value in result)


def quaternion_of(rotation):
    """The unit quaternion (x, y, z, w) of a column-vector rotation matrix, as rows."""
    m = rotation
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0.0:
        s = 0.5 / math.sqrt(trace + 1.0)
        q = ((m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s, 0.25 / s)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = 2.0 * math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2])
        q = (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = 2.0 * math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2])
        q = ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = 2.0 * math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1])
        q = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
    length = math.sqrt(sum(value * value for value in q))
    return tuple(value / length for value in q)


def engine_rotation(rows):
    """A legacy row-vector rotation, as the column-vector rotation that does the same in the engine's
    mirrored space."""
    sign = (1.0, 1.0, -1.0)
    return [[rows[column][row] * sign[row] * sign[column] for column in range(3)] for row in range(3)]


def engine_rigid(matrix, scale):
    """A legacy rigid row-vector matrix, as (rotation rows, translation) in the engine, in meters."""
    translation = (matrix[3][0] * scale, matrix[3][1] * scale, -matrix[3][2] * scale)
    return engine_rotation(matrix), translation


def read_ms3d(path):
    """A MilkShape 3D 1.4 model: vertices bound to one joint each, faces, joints and their keys."""
    data = path.read_bytes()
    if data[:10] != b"MS3D000000" or struct.unpack_from("<i", data, 10)[0] != 4:
        sys.exit(f"{path} is not a MilkShape 3D 1.4 file")
    offset = 14

    def read(layout):
        nonlocal offset
        values = struct.unpack_from("<" + layout, data, offset)
        offset += struct.calcsize("<" + layout)
        return values

    def text(raw):
        return raw.split(b"\0")[0].decode("latin-1")

    model = {"vertices": [], "faces": [], "groups": [], "joints": []}
    for _ in range(read("H")[0]):
        _, x, y, z, joint, _ = read("B3fbB")
        model["vertices"].append(((x, y, z), joint))
    for _ in range(read("H")[0]):
        values = read("H3H9f3f3fBB")
        model["faces"].append({"vertices": values[1:4], "normals": [values[4 + 3 * c:7 + 3 * c] for c in range(3)],
                               "uvs": [(values[13 + c], values[16 + c]) for c in range(3)]})
    for _ in range(read("H")[0]):
        read("B32s")
        count = read("H")[0]
        model["groups"].append(read(f"{count}H"))
        read("b")
    materials = read("H")[0]
    offset += materials * 361 # The level names the texture.
    fps = max(read("f")[0], 1.0)
    read("fi")
    names = []
    for _ in range(read("H")[0]):
        _, name, parent = read("B32s32s")
        rotation, position = read("3f"), read("3f")
        rotation_count, translation_count = read("HH")
        # Key times are in seconds: in frames, as the legacy engine counted.
        rotations = [(time * fps, tuple(angles)) for time, *angles in (read("4f") for _ in range(rotation_count))]
        translations = [(time * fps, tuple(values)) for time, *values in (read("4f") for _ in range(translation_count))]
        names.append(text(name))
        parent = text(parent)
        parent_index = names.index(parent) if parent in names else None
        if parent and parent_index is None:
            sys.exit(f"{path}: joint {names[-1]} comes before its parent {parent}")
        model["joints"].append({"name": names[-1], "parent": parent_index,
                                "bind": legacy_matrix(legacy_euler(rotation), position),
                                "rotations": rotations, "translations": translations})
    return model


def legacy_pose(model, frame):
    """The joints' local and global matrices at a frame, as CMS3DLoader::Animate computed them."""
    def sample(keys, mix, default):
        if not keys:
            return default
        if frame < keys[0][0]:
            return keys[0][1]
        if frame >= keys[-1][0]:
            return keys[-1][1]
        for (start, a), (end, b) in zip(keys, keys[1:]):
            if start <= frame < end:
                return mix(a, b, (frame - start) / (end - start))
        return keys[-1][1]

    lerp = lambda a, b, t: tuple(x + (y - x) * t for x, y in zip(a, b))
    locals_, globals_ = [], []
    for joint in model["joints"]:
        rotations = [(time, legacy_euler(angles)) for time, angles in joint["rotations"]]
        rotation = sample(rotations, slerp, (0.0, 0.0, 0.0, 1.0))
        translation = sample(joint["translations"], lerp, (0.0, 0.0, 0.0))
        local = multiply(multiply(legacy_matrix(rotation, (0, 0, 0)), legacy_matrix((0, 0, 0, 1), translation)),
                         joint["bind"])
        locals_.append(local)
        parent = joint["parent"]
        globals_.append(local if parent is None else multiply(local, globals_[parent]))
    return locals_, globals_


def bind_globals(model):
    globals_ = []
    for joint in model["joints"]:
        parent = joint["parent"]
        globals_.append(joint["bind"] if parent is None else multiply(joint["bind"], globals_[parent]))
    return globals_


def compose(rotation, translation):
    """A column-vector matrix, as rows, from rotation rows and a translation."""
    return [list(rotation[0]) + [translation[0]], list(rotation[1]) + [translation[1]],
            list(rotation[2]) + [translation[2]], [0.0, 0.0, 0.0, 1.0]]


def inverse_rigid(matrix):
    rotation = [[matrix[column][row] for column in range(3)] for row in range(3)]
    translation = [-sum(rotation[row][k] * matrix[k][3] for k in range(3)) for row in range(3)]
    return compose(rotation, translation)


def transform_point(matrix, point):
    return tuple(sum(matrix[row][k] * point[k] for k in range(3)) + matrix[row][3] for row in range(3))


def rotation_of_quaternion(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def convert_animated(source, file, spec, output):
    """Bakes a MilkShape model to a skinned glTF file of its own, in meters, with one animation per clip.
    Vertices that no joint moves are bound to an extra joint, "static", which never moves."""
    model = read_ms3d(source / "Models" / file)
    stem = Path(file).stem
    scale = METERS_PER_UNIT * spec["scale"]
    joints = model["joints"]
    static_joint = len(joints) if any(joint < 0 for _, joint in model["vertices"]) else None
    sign = (1.0, 1.0, -1.0)

    positions, normals, uvs, joint_ids, indices, corners = [], [], [], [], [], {}
    for group in model["groups"]:
        for face in (model["faces"][index] for index in group):
            # Mirrored into the right-handed world, so the corners are swapped.
            for corner in (0, 2, 1):
                vertex = face["vertices"][corner]
                normal = tuple(face["normals"][corner][axis] * sign[axis] for axis in range(3))
                key = (vertex, normal, face["uvs"][corner])
                if key not in corners:
                    corners[key] = len(positions)
                    point, joint = model["vertices"][vertex]
                    positions.append(tuple(point[axis] * scale * sign[axis] for axis in range(3)))
                    normals.append(normal)
                    uvs.append(face["uvs"][corner])
                    joint_ids.append(joint if joint >= 0 else static_joint)
                indices.append(corners[key])

    writer = GltfWriter(source, output)
    binds = [engine_rigid(matrix, scale) for matrix in bind_globals(model)]
    inverse_binds = [inverse_rigid(compose(*bind)) for bind in binds]
    if static_joint is not None:
        inverse_binds.append(compose([[1, 0, 0], [0, 1, 0], [0, 0, 1]], (0, 0, 0)))
    primitive = {"attributes": {
        "POSITION": writer.accessor(positions, "VEC3", True),
        "NORMAL": writer.accessor(normals, "VEC3", False),
        "TEXCOORD_0": writer.accessor(uvs, "VEC2", False),
        "JOINTS_0": writer.accessor([(joint, 0, 0, 0) for joint in joint_ids], "VEC4", False, "H"),
        "WEIGHTS_0": writer.accessor([(1.0, 0.0, 0.0, 0.0)] * len(positions), "VEC4", False),
    }, "indices": writer.accessor([(index,) for index in indices], "SCALAR", False, "I"),
        "material": writer.material(spec["texture"])}
    writer.gltf["meshes"].append({"name": stem, "primitives": [primitive]})

    root = writer.node(stem)
    mesh_node = writer.node(f"{stem}_mesh", mesh=0, root=False)
    writer.gltf["nodes"][mesh_node]["skin"] = 0
    joint_nodes = []
    for joint in joints:
        rotation, translation = engine_rigid(joint["bind"], scale)
        joint_nodes.append(writer.node(joint["name"], translation, quaternion_of(rotation), root=False))
    if static_joint is not None:
        joint_nodes.append(writer.node("static", root=False))
    children = {root: [mesh_node]}
    for index, joint in enumerate(joints):
        parent = root if joint["parent"] is None else joint_nodes[joint["parent"]]
        children.setdefault(parent, []).append(joint_nodes[index])
    if static_joint is not None:
        children[root].append(joint_nodes[static_joint])
    for node, nodes in children.items():
        writer.gltf["nodes"][node]["children"] = nodes
    # Matrices are stored column by column.
    columns = [tuple(matrix[row][column] for column in range(4) for row in range(4)) for matrix in inverse_binds]
    writer.gltf["skins"] = [{"joints": joint_nodes, "inverseBindMatrices": writer.accessor(columns, "MAT4", False)}]

    writer.gltf["animations"] = []
    for name, (first, last, fps) in spec["clips"].items():
        frames = range(first, last + 1)
        times = writer.accessor([((frame - first) / fps,) for frame in frames], "SCALAR", True)
        poses = [legacy_pose(model, frame) for frame in frames]
        check_skinning(model, poses[len(poses) // 2], scale, f"{file} {name}")
        samplers, channels = [], []
        for index in range(len(joints)):
            rotations, translations = [], []
            for locals_, _ in poses:
                rotation, translation = engine_rigid(locals_[index], scale)
                rotation = quaternion_of(rotation)
                # Neighbors on the same side of the sphere, so that interpolating takes the short way.
                if rotations and sum(a * b for a, b in zip(rotation, rotations[-1])) < 0.0:
                    rotation = tuple(-value for value in rotation)
                rotations.append(rotation)
                translations.append(translation)
            for path, values, kind in (("rotation", rotations, "VEC4"), ("translation", translations, "VEC3")):
                samplers.append({"input": times, "output": writer.accessor(values, kind, False),
                                 "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": joint_nodes[index], "path": path}})
        writer.gltf["animations"].append({"name": name, "samplers": samplers, "channels": channels})

    writer.write(stem)
    low = [min(point[axis] for point in positions) for axis in range(3)]
    high = [max(point[axis] for point in positions) for axis in range(3)]
    print(f"{stem}: {len(indices) // 3} triangles, {len(joint_nodes)} joints, clips {list(spec['clips'])}, "
          f"from {[round(v, 2) for v in low]} to {[round(v, 2) for v in high]} m")


def check_skinning(model, pose, scale, label):
    """Fails if the glTF data, read as the engine reads it, does not move the vertices where the legacy
    engine did."""
    locals_, globals_ = pose
    inverse_bind = [inverse_rigid(compose(*engine_rigid(matrix, scale))) for matrix in bind_globals(model)]
    engine_globals = []
    for index, joint in enumerate(model["joints"]):
        rotation, translation = engine_rigid(locals_[index], scale)
        local = compose(rotation_of_quaternion(quaternion_of(rotation)), translation)
        parent = joint["parent"]
        engine_globals.append(local if parent is None else multiply(engine_globals[parent], local))
    legacy_binds = bind_globals(model)
    for point, joint in model["vertices"]:
        if joint < 0:
            continue
        # Legacy: through the inverse bind matrix, then the joint's matrix, as row vectors.
        inverse = inverse_rigid([[legacy_binds[joint][column][row] for column in range(4)] for row in range(4)])
        expected = transform_point([[globals_[joint][column][row] for column in range(4)] for row in range(4)],
                                   transform_point(inverse, point))
        expected = (expected[0] * scale, expected[1] * scale, -expected[2] * scale)
        engine_point = (point[0] * scale, point[1] * scale, -point[2] * scale)
        actual = transform_point(multiply(engine_globals[joint], inverse_bind[joint]), engine_point)
        if max(abs(a - b) for a, b in zip(actual, expected)) > 1e-4:
            sys.exit(f"{label}: skinning differs from the legacy engine at {point}: {actual} for {expected}")


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

    def accessor(self, values, kind, with_bounds, component="f"):
        """Values are tuples of floats ("f"), unsigned shorts ("H") or unsigned ints ("I")."""
        data = b"".join(struct.pack(f"<{len(value)}{component}", *value) for value in values)
        # Every element of a buffer view starts on a multiple of four bytes.
        self.buffer += bytes(-len(self.buffer) % 4)
        self.gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(self.buffer), "byteLength": len(data)})
        self.buffer += data
        accessor = {"bufferView": len(self.gltf["bufferViews"]) - 1,
                    "componentType": {"f": 5126, "H": 5123, "I": 5125}[component], "count": len(values),
                    "type": kind}
        if with_bounds:
            size = len(values[0])
            accessor["min"] = [min(value[axis] for value in values) for axis in range(size)]
            accessor["max"] = [max(value[axis] for value in values) for axis in range(size)]
        self.gltf["accessors"].append(accessor)
        return len(self.gltf["accessors"]) - 1

    def texture(self, texture):
        """Copies a legacy texture next to the glTF file, as PNG, and returns its glTF texture index."""
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
        return len(self.gltf["textures"]) - 1

    def material(self, texture):
        if texture in self.materials:
            return self.materials[texture]
        material = {"name": texture, "pbrMetallicRoughness": {
            "baseColorTexture": {"index": self.texture(texture)},
            "metallicFactor": 0.0, "roughnessFactor": LEGACY_ROUGHNESS}}
        if texture in NORMAL_MAPS:
            material["normalTexture"] = {"index": self.texture(NORMAL_MAPS[texture])}
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

    def node(self, name, translation=None, rotation=None, mesh=None, root=True):
        """Rotation is a quaternion (x, y, z, w). Returns the index of the node."""
        node = {"name": name}
        if translation is not None:
            node["translation"] = list(translation)
        if rotation is not None and abs(rotation[3]) < 1.0 - 1e-7:
            node["rotation"] = list(rotation)
        if mesh is not None:
            node["mesh"] = mesh
        self.gltf["nodes"].append(node)
        index = len(self.gltf["nodes"]) - 1
        if root:
            self.gltf["scenes"][0]["nodes"].append(index)
        return index

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
        triangles = read_obj(path)
        if file in MIRRORED_MODELS:
            return [[([-c[0][0], c[0][1], c[0][2]], c[1]) for c in (t[0], t[2], t[1])] for t in triangles]
        return triangles
    if path.suffix.lower() == ".md2":
        return read_md2(path)
    return None # Animated models have files of their own: see ANIMATED_MODELS.


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
    for element in root.iter():
        diffuse, normal = element.find("diffuse_texture"), element.find("normal_texture")
        if diffuse is not None and normal is not None and diffuse.text and normal.text:
            diffuse_name, normal_name = diffuse.text.strip(), normal.text.strip()
            if normal_name != diffuse_name and normal_name not in NO_NORMAL_MAP:
                NORMAL_MAPS[diffuse_name] = normal_name
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
        bounds = ""
        file = BOX_MESHES.get(kind, node.get("file"))
        if file in ANIMATED_MODELS:
            if any(abs(value - ANIMATED_MODELS[file]["scale"]) > 1e-6 for value in scale):
                print(f"Warning: {kind} at {position} is scaled {scale}, and {file} is always "
                      f"{ANIMATED_MODELS[file]['scale']}")
            turn = quaternion_of(engine_rotation(rotation_matrix(rotation)))
            writer.node(f"{entity_name(kind, node)};model={Path(file).stem}.gltf", to_engine(position), turn)
            counts[kind] = counts.get(kind, 0) + 1
            return
        texture = texture_of(node)
        if file and texture and texture not in SKIPPED_TEXTURES:
            triangles = triangles_of(file)
            if triangles is not None:
                # Local geometry: the mesh keeps the entity's rotation and scale, and the node its position.
                geometry = Geometry()
                geometry.add(triangles, [0.0, 0.0, 0.0], rotation_matrix(rotation), scale)
                mesh = writer.mesh({texture: geometry})
                if kind == "ladder":
                    # The box the player climbs in, in the world.
                    origin = to_engine(position)
                    low = [origin[axis] + min(point[axis] for point in geometry.positions) for axis in range(3)]
                    high = [origin[axis] + max(point[axis] for point in geometry.positions) for axis in range(3)]
                    bounds = (f";min={low[0]:.3f},{low[1]:.3f},{low[2]:.3f}"
                              f";max={high[0]:.3f},{high[1]:.3f},{high[2]:.3f}")
        writer.node(entity_name(kind, node) + bounds, to_engine(position), mesh=mesh)
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
    for name, position in EXTRA_ENTITIES:
        writer.node(name, position)
        kind = name.split(";")[0]
        counts[kind] = counts.get(kind, 0) + 1
    writer.node("level", mesh=writer.mesh(level))
    writer.write(scene.capitalize())
    triangles = sum(len(geometry.positions) // 3 for geometry in level.values())
    print(f"{scene}: {triangles} level triangles in {len(level)} materials, entities: {counts}")


def copy_sounds(source, output):
    sounds = output / "Sounds"
    sounds.mkdir(exist_ok=True)
    for sound in sorted((source / "Sounds").glob("*.wav")):
        if sound.name not in SKIPPED_SOUNDS:
            shutil.copyfile(sound, sounds / sound.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="Resources directory of the legacy engine")
    parser.add_argument("--scene", default="factory")
    arguments = parser.parse_args()

    output = ROOT / "assets" / arguments.scene.capitalize()
    output.mkdir(parents=True, exist_ok=True)
    convert(arguments.source, arguments.scene, output)
    for file, spec in ANIMATED_MODELS.items():
        convert_animated(arguments.source, file, spec, output)
    copy_sounds(arguments.source, output)


if __name__ == "__main__":
    main()
