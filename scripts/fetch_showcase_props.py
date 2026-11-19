#!/usr/bin/env python3
"""Fetches the factory showcase's kit of props from Poly Haven, all CC0 (ADR-0082).

    python -I scripts/fetch_showcase_props.py [--tool path/to/texture-tool] [--only id ...]

For each model below: downloads its glTF file with 1k textures into assets/Factory/Showcase/<id>/, keeps the
default material of each part (Poly Haven's material variants are dropped), converts the textures it uses to
KTX2 with texture-tool, at 512 texels for all but the large pieces (base color as color, normals as normals, the packed occlusion-roughness-metal map as
data) and points the materials at them, writes what each material is made of into its extras, and records
the model's authors and licence in assets/Factory/Showcase/LICENSE.md. The JPG files stay beside, ignored by
git, so that a second run converts nothing again. Prints the kit's triangle count at the end.
"""

import argparse
import concurrent.futures
import hashlib
import json
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / "assets" / "Factory" / "Showcase"
API = "https://api.polyhaven.com"
BASISU = "KHR_texture_basisu"
VARIANTS = "KHR_materials_variants"
# The client says what it is, without anything about who runs it.
HEADERS = {"User-Agent": "daidalos-engine-next-asset-fetcher"}

# Model id: what it is made of (scene surfaces: impacts, footsteps and porosity, ADR-0053 and ADR-0061).
MODELS = {
    # Containers.
    "Barrel_01": "metal", "Barrel_02": "metal", "barrel_03": "metal", "metal_jerrycan": "metal",
    "propane_tank": "metal", "wooden_crate_01": "wood", "wooden_crate_02": "wood", "old_military_crate": "wood",
    "plastic_crate_01": "", "cement_bag": "cloth", "trashbag": "",
    # Storage and tools.
    "steel_frame_shelves_01": "metal", "steel_frame_shelves_02": "metal", "metal_tool_chest": "metal",
    "metal_toolbox": "metal", "tool_cart": "metal", "industrial_storage_cart": "metal", "hand_truck": "metal",
    "metal_office_desk": "metal", "metal_stool_01": "metal",
    # Machines and power.
    "power_box_01": "metal", "utility_box_01": "metal", "utility_box_02": "metal",
    "old_military_compressor": "metal", "portable_generator": "metal", "drill_press_01": "metal",
    "overhead_crane": "metal",
    # Pipes, ducts, cables.
    "modular_industrial_pipes_01": "metal", "modular_airduct_circular_01": "metal",
    "modular_electric_cables": "",
    # Lamps.
    "mounted_fluorescent_lights": "metal", "hanging_industrial_lamp": "metal", "industrial_wall_lamp": "metal",
    "caged_hanging_light": "metal", "security_light": "metal",
    # Clutter.
    "WetFloorSign_01": "", "korean_fire_extinguisher_01": "metal", "old_gas_mask": "cloth",
    # The game's items (pickups and the weapons in hand): the gas mask above is the armour.
    "service_pistol": "metal", "bolt_action_rifle_7_62": "metal", "ammo_box": "metal", "medical_box": "metal",
    "vintage_flashlight": "metal",
}

# The weapons are seen in the hand, a hand's width from the eye: their textures at 2k, kept whole.
RESOLUTION = {"service_pistol": "2k", "bolt_action_rifle_7_62": "2k"}

# Poly Haven lays some models out as a catalogue: variants and loose parts side by side. One weapon is kept of
# each: the pistol with wooden grips (its "a" parts; the "b" parts are taped), without the loose magazines and
# round; the rifle with its bolt closed ("a") and without the loose round.
DROP_NODES = {
    "service_pistol": {"service_pistol_pistol_b", "service_pistol_slide_b", "service_pistol_hammer_b",
                       "service_pistol_trigger_b", "service_pistol_magazine_loaded",
                       "service_pistol_magazine_empty", "service_pistol_bullet"},
    "bolt_action_rifle_7_62": {"bolt_action_rifle_7_62_bolt_b", "bolt_action_rifle_7_62_bullet_54mm"},
}
SHIFT = {}


# Large pieces keep 1k textures; the rest, seen from a step or two away, take 512 (ADR-0082).
LARGE = {"overhead_crane", "steel_frame_shelves_01", "steel_frame_shelves_02", "old_military_compressor",
         "modular_industrial_pipes_01", "modular_airduct_circular_01", "drill_press_01", "metal_office_desk",
         "portable_generator"}
SMALL_TEXTURES = 512


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


def texture_slots(material):
    """(slot, role) of each texture a material uses: role is how texture-tool encodes its image."""
    pbr = material.get("pbrMetallicRoughness", {})
    slots = [(pbr.get("baseColorTexture"), "color"), (pbr.get("metallicRoughnessTexture"), "data"),
             (material.get("normalTexture"), "normal"), (material.get("occlusionTexture"), "data"),
             (material.get("emissiveTexture"), "color")]
    return [(slot, role) for slot, role in slots if slot is not None]


def keep_defaults(gltf):
    """Drops material variants and the materials, textures and images only they used."""
    gltf.get("extensions", {}).pop(VARIANTS, None)
    if not gltf.get("extensions", True):
        gltf.pop("extensions")
    for name in ("extensionsUsed", "extensionsRequired"):
        if name in gltf:
            gltf[name] = [e for e in gltf[name] if e != VARIANTS]
    used = []
    for mesh in gltf.get("meshes", []):
        for primitive in mesh["primitives"]:
            primitive.get("extensions", {}).pop(VARIANTS, None)
            if not primitive.get("extensions"):
                primitive.pop("extensions", None)
            if "material" in primitive and primitive["material"] not in used:
                used.append(primitive["material"])
    remap = {old: new for new, old in enumerate(sorted(used))}
    gltf["materials"] = [gltf["materials"][old] for old in sorted(used)]
    for mesh in gltf.get("meshes", []):
        for primitive in mesh["primitives"]:
            if "material" in primitive:
                primitive["material"] = remap[primitive["material"]]
    textures = sorted({slot["index"] for material in gltf["materials"] for slot, _ in texture_slots(material)})
    remap = {old: new for new, old in enumerate(textures)}
    gltf["textures"] = [gltf["textures"][old] for old in textures]
    for material in gltf["materials"]:
        for slot, _ in texture_slots(material):
            slot["index"] = remap[slot["index"]]
    images = sorted({texture["source"] for texture in gltf["textures"]})
    remap = {old: new for new, old in enumerate(images)}
    gltf["images"] = [gltf["images"][old] for old in images]
    for texture in gltf["textures"]:
        texture["source"] = remap[texture["source"]]


def drop_nodes(gltf, names, shift):
    """Removes the top-level nodes of these names and the meshes only they drew; moves the others by shift,
    if any."""
    if not names and not shift:
        return
    scene = gltf["scenes"][gltf.get("scene", 0)]
    if any(gltf["nodes"][index].get("children") for index in scene["nodes"]):
        raise RuntimeError("drop_nodes handles flat scenes only")
    kept = [index for index in range(len(gltf["nodes"])) if gltf["nodes"][index].get("name") not in names]
    gltf["nodes"] = [gltf["nodes"][index] for index in kept]
    scene["nodes"] = list(range(len(kept)))
    meshes = sorted({node["mesh"] for node in gltf["nodes"] if "mesh" in node})
    remap = {old: new for new, old in enumerate(meshes)}
    gltf["meshes"] = [gltf["meshes"][old] for old in meshes]
    for node in gltf["nodes"]:
        if "mesh" in node:
            node["mesh"] = remap[node["mesh"]]
        if shift:
            node["translation"] = [a + b for a, b in zip(node.get("translation", [0.0, 0.0, 0.0]), shift)]


def triangles(gltf):
    count = 0
    for mesh in gltf.get("meshes", []):
        for primitive in mesh["primitives"]:
            accessor = primitive.get("indices", primitive["attributes"]["POSITION"])
            count += gltf["accessors"][accessor]["count"] // 3
    return count


def fetch(model, surface, tool, pool):
    directory = KIT / model
    resolution = RESOLUTION.get(model, "1k")
    files = get_json(f"{API}/files/{model}")["gltf"][resolution]["gltf"]
    gltf_path = directory / f"{model}.gltf"
    original = directory / "Source" / f"{model}_{resolution}.gltf"
    download(files["url"], original, files.get("md5"))
    jobs = []
    for relative, entry in files["include"].items():
        target = directory / relative if relative.endswith(".bin") else directory / "Source" / relative
        jobs.append(pool.submit(download, entry["url"], target, entry.get("md5")))
    for job in jobs:
        job.result()

    gltf = json.loads(original.read_text(encoding="utf-8"))
    drop_nodes(gltf, DROP_NODES.get(model, set()), SHIFT.get(model))
    keep_defaults(gltf)
    roles = {}
    for material in gltf["materials"]:
        for slot, role in texture_slots(material):
            roles[gltf["textures"][slot["index"]]["source"]] = role
    conversions = []
    for index, image in enumerate(gltf["images"]):
        source = directory / "Source" / image["uri"]
        output = directory / "textures" / (Path(image["uri"]).stem.replace(f"_{resolution}", "") + ".ktx2")
        if not output.exists() or output.stat().st_mtime < source.stat().st_mtime:
            output.parent.mkdir(parents=True, exist_ok=True)
            size = [] if model in LARGE or model in RESOLUTION else ["--max-size", str(SMALL_TEXTURES)]
            conversions.append(pool.submit(subprocess.run, [str(tool), *size, roles.get(index, "data"), str(source),
                                                           str(output)], check=True, capture_output=True))
        gltf["images"][index] = {"uri": output.relative_to(directory).as_posix(), "mimeType": "image/ktx2"}
    for conversion in conversions:
        conversion.result()
    for texture in gltf["textures"]:
        texture["extensions"] = {BASISU: {"source": texture.pop("source")}}
    for name in ("extensionsUsed", "extensionsRequired"):
        gltf[name] = sorted(set(gltf.get(name, [])) | {BASISU})
    for material in gltf["materials"]:
        if surface:
            material.setdefault("extras", {})["surface"] = surface
        # Blending is not drawn (ADR-0042): glass becomes glass, the rest cut out at half alpha.
        if material.get("alphaMode") == "BLEND":
            del material["alphaMode"]
            if "glass" in material.get("name", "").lower():
                material.setdefault("extensions", {})["KHR_materials_transmission"] = {"transmissionFactor": 0.95}
                material["pbrMetallicRoughness"]["roughnessFactor"] = min(
                    material["pbrMetallicRoughness"].get("roughnessFactor", 1.0), 0.1)
                material.setdefault("extras", {})["surface"] = "glass"
                gltf["extensionsUsed"] = sorted(set(gltf.get("extensionsUsed", [])) | {"KHR_materials_transmission"})
            else:
                material["alphaMode"] = "MASK"
                material["alphaCutoff"] = 0.5
    gltf_path.write_text(json.dumps(gltf, indent=1), encoding="utf-8")
    return triangles(gltf)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tool", type=Path)
    parser.add_argument("--only", nargs="*")
    arguments = parser.parse_args()
    tool = arguments.tool or find_tool()
    models = {model: MODELS[model] for model in (arguments.only or MODELS)}
    licence = ["# The factory showcase's props", "",
               "All from [Poly Haven](https://polyhaven.com), under CC0 1.0 (public domain): free for any use,",
               "without attribution. Their authors are credited here all the same. Fetched and converted by",
               "`scripts/fetch_showcase_props.py` (ADR-0082).", "",
               "| Model | Authors | Source |", "|---|---|---|"]
    total = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        for model, surface in models.items():
            info = get_json(f"{API}/info/{model}")
            count = fetch(model, surface, tool, pool)
            total += count
            authors = ", ".join(info.get("authors", {}).keys())
            licence.append(f"| {info.get('name', model)} (`{model}`) | {authors} | https://polyhaven.com/a/{model} |")
            print(f"{model}: {count} triangles")
    if arguments.only:
        # The other models' rows stay as they are; these replace theirs, or join the end.
        def key(line):
            return line.split("(`")[1].split("`)")[0] if line.startswith("| ") and "(`" in line else None
        rows = {key(line): line for line in licence if key(line)}
        kept = (KIT / "LICENSE.md").read_text(encoding="utf-8").splitlines()
        licence = [rows.pop(key(line), line) if key(line) else line for line in kept] + list(rows.values())
    (KIT / "LICENSE.md").write_text("\n".join(licence) + "\n", encoding="utf-8")
    print(f"{len(models)} models, {total} triangles")


if __name__ == "__main__":
    main()
