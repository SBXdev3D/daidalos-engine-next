#!/usr/bin/env python3
"""Converts the Factory's source textures to KTX2 and points the level's materials at them (ADR-0046).

    python scripts/convert_factory_textures.py [--tool path/to/texture-tool]

The sources are in assets/Factory/Source/, a set per surface, under the legacy names (door_d.png, but
wall_2d.png after a digit): d is the albedo (sRGB), n the normals (OpenGL convention: green up), r the
roughness, and optionally m the metalness and h the height, which the engine does not use yet. Each set
becomes d, n and mr KTX2 files next to the glTF files (door_mr.ktx2). Outputs newer than their sources
are kept.

The materials listed below take the new textures. Their roughness then comes from the map alone, and so
does their metalness when the set has a map of it. Textures that no material uses any more leave the glTF
files. import_legacy_level.py writes the legacy materials back: run this script after it.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FACTORY = ROOT / "assets" / "Factory"
SOURCE = FACTORY / "Source"
# Materials by glTF file, and the source set that each one takes.
MATERIALS = {
    "Factory.gltf": {
        "ceiling_2d.png": "ceiling_2",
        "crate_1d.png": "crate_1",
        "crate_2d.png": "crate_2",
        "crate_3d.png": "crate_3",
        "door_d.png": "door",
        "door_red_d.png": "door",
        "ground_1d.png": "ground_1",
        "lift_d.png": "lift",
        "lockers_d.png": "lockers",
        "vent_slab_d.png": "vent_slab",
        "wall_2d.png": "wall_2",
    },
    "zombie.gltf": {"zombie_d.png": "zombie"},
}
# The red door keeps its own albedo, with the relief of the door it is a variant of.
KEEP_BASE_COLOR = {"door_red_d.png"}
BASISU = "KHR_texture_basisu"


def find_tool():
    names = ("texture-tool.exe", "texture-tool")
    candidates = [path for name in names for path in (ROOT / "build").glob(f"*/apps/texture-tool/**/{name}")]
    if not candidates:
        sys.exit("texture-tool not found under build/: build it, or pass --tool")
    # Release builds encode much faster than debug ones.
    candidates.sort(key=lambda path: ("Release" not in path.parts, -path.stat().st_mtime))
    return candidates[0]


def convert(tool, mode, inputs, output):
    if output.exists() and all(output.stat().st_mtime >= source.stat().st_mtime for source in inputs):
        return
    subprocess.run([str(tool), mode, *map(str, inputs), str(output)], check=True)


def file_name(name, suffix):
    """The legacy naming: wall_2d, door_d."""
    return f"{name}{suffix}" if name[-1].isdigit() else f"{name}_{suffix}"


def convert_set(tool, name):
    """Converts a source set, and returns the file names of its albedo, normal and metallic-roughness maps."""
    def source(suffix, required=True):
        path = SOURCE / f"{file_name(name, suffix)}.png"
        if required and not path.exists():
            sys.exit(f"Missing source texture: {path}")
        return path if path.exists() else None

    metallic = source("m", required=False)
    outputs = {suffix: f"{file_name(name, suffix)}.ktx2" for suffix in ("d", "n", "mr")}
    convert(tool, "color", [source("d")], FACTORY / outputs["d"])
    convert(tool, "normal", [source("n")], FACTORY / outputs["n"])
    convert(tool, "metallic-roughness", [source("r")] + ([metallic] if metallic else []), FACTORY / outputs["mr"])
    return outputs | {"metallic": metallic is not None}


def texture_index(gltf, uri):
    """Returns the texture of a KTX2 image, adding both if the file has none yet."""
    for index, texture in enumerate(gltf["textures"]):
        source = texture.get("extensions", {}).get(BASISU, {}).get("source")
        if source is not None and gltf["images"][source]["uri"] == uri:
            return index
    gltf["images"].append({"uri": uri, "mimeType": "image/ktx2"})
    gltf["textures"].append({"sampler": 0, "extensions": {BASISU: {"source": len(gltf["images"]) - 1}}})
    return len(gltf["textures"]) - 1


def texture_slots(material):
    """The texture references of a material: dictionaries holding an 'index'."""
    pbr = material.get("pbrMetallicRoughness", {})
    slots = [pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture")]
    slots += [material.get(key) for key in ("normalTexture", "occlusionTexture", "emissiveTexture")]
    return [slot for slot in slots if slot is not None]


def drop_unused(gltf):
    """Removes the textures that no material uses, then the images that no texture uses."""
    used = sorted({slot["index"] for material in gltf["materials"] for slot in texture_slots(material)})
    remap = {old: new for new, old in enumerate(used)}
    gltf["textures"] = [gltf["textures"][old] for old in used]
    for material in gltf["materials"]:
        for slot in texture_slots(material):
            slot["index"] = remap[slot["index"]]

    def sources(texture):
        return [s for s in (texture.get("source"), texture.get("extensions", {}).get(BASISU, {}).get("source"))
                if s is not None]

    used = sorted({source for texture in gltf["textures"] for source in sources(texture)})
    remap = {old: new for new, old in enumerate(used)}
    gltf["images"] = [gltf["images"][old] for old in used]
    for texture in gltf["textures"]:
        if "source" in texture:
            texture["source"] = remap[texture["source"]]
        if BASISU in texture.get("extensions", {}):
            texture["extensions"][BASISU]["source"] = remap[texture["extensions"][BASISU]["source"]]


def update_gltf(path, materials, sets):
    gltf = json.loads(path.read_text(encoding="utf-8"))
    for material in gltf["materials"]:
        name = material.get("name")
        if name not in materials:
            continue
        textures = sets[materials[name]]
        pbr = material["pbrMetallicRoughness"]
        if name not in KEEP_BASE_COLOR:
            pbr["baseColorTexture"] = {"index": texture_index(gltf, textures["d"])}
        material["normalTexture"] = {"index": texture_index(gltf, textures["n"])}
        pbr["metallicRoughnessTexture"] = {"index": texture_index(gltf, textures["mr"])}
        pbr["roughnessFactor"] = 1.0
        if textures["metallic"]:
            pbr["metallicFactor"] = 1.0
    drop_unused(gltf)
    for key in ("extensionsUsed", "extensionsRequired"):
        extensions = gltf.setdefault(key, [])
        if BASISU not in extensions:
            extensions.append(BASISU)
    path.write_text(json.dumps(gltf), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tool", type=Path, help="texture-tool executable (default: the newest under build/)")
    args = parser.parse_args()
    tool = args.tool or find_tool()

    names = sorted({name for materials in MATERIALS.values() for name in materials.values()})
    sets = {name: convert_set(tool, name) for name in names}
    for file, materials in MATERIALS.items():
        update_gltf(FACTORY / file, materials, sets)
        print(f"Updated {file}")


if __name__ == "__main__":
    main()
