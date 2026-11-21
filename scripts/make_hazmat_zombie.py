#!/usr/bin/env python3
"""Makes the factory's zombie: Zombie Hazmat by LxNazarov (CC-BY 4.0) with Quaternius' CC0 animations.

    python -I scripts/make_hazmat_zombie.py zombie_hazmat.zip "Universal Animation Library[Standard].zip"
        "Universal Animation Library 2[Standard].zip" [--blender path] [--tool path/to/texture-tool]

The three archives are the ones their sites give: the model from
https://sketchfab.com/3d-models/zombie-hazmat-49b3b4307f6a4d2386fdb02354158d04 (glTF), the libraries from
https://quaternius.itch.io/universal-animation-library and https://quaternius.itch.io/universal-animation-library-2
(Standard). The model's own clips come from Mixamo, whose files may not be passed on: Blender replaces them with
Quaternius' (scripts/retarget_zombie_in_blender.py). The textures become KTX2 at 1024 texels, as the other
models' (texture-tool). Writes assets/Factory/Monsters/zombie_hazmat/.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "Factory" / "Monsters" / "zombie_hazmat"
BASISU = "KHR_texture_basisu"
TEXTURE_SIZE = 1024


def find_tool():
    names = ("texture-tool.exe", "texture-tool")
    candidates = [path for name in names for path in (ROOT / "build").glob(f"*/apps/texture-tool/**/{name}")]
    if not candidates:
        sys.exit("texture-tool not found under build/: build it, or pass --tool")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def find_blender():
    for candidate in (shutil.which("blender"), "C:/Installation/Blender/blender.exe",
                      "C:/Program Files/Blender Foundation/Blender/blender.exe"):
        if candidate and Path(candidate).exists():
            return Path(candidate)
    sys.exit("Blender not found: pass --blender")


def only(folder, pattern):
    found = sorted(folder.rglob(pattern))
    if len(found) != 1:
        sys.exit(f"Expected one {pattern} in {folder}, found {len(found)}")
    return found[0]


def roles(gltf):
    """How texture-tool encodes each image: as color, normals or data."""
    out = {}
    for material in gltf["materials"]:
        pbr = material.get("pbrMetallicRoughness", {})
        for slot, role in ((pbr.get("baseColorTexture"), "color"), (pbr.get("metallicRoughnessTexture"), "data"),
                           (material.get("normalTexture"), "normal"), (material.get("occlusionTexture"), "data"),
                           (material.get("emissiveTexture"), "color")):
            if slot is not None:
                out[gltf["textures"][slot["index"]]["source"]] = role
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("model")
    parser.add_argument("library1")
    parser.add_argument("library2")
    parser.add_argument("--blender", type=Path)
    parser.add_argument("--tool", type=Path)
    args = parser.parse_args()
    blender = args.blender or find_blender()
    tool = args.tool or find_tool()

    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        for name, archive in (("model", args.model), ("ual1", args.library1), ("ual2", args.library2)):
            with zipfile.ZipFile(archive) as files:
                files.extractall(work / name)
        model = only(work / "model", "scene.gltf")
        ual1 = only(work / "ual1", "UAL1_Standard.glb")
        ual2 = only(work / "ual2", "UAL2_Standard.glb")
        exported = work / "exported"
        subprocess.run([str(blender), "-b", "--factory-startup", "-P",
                        str(ROOT / "scripts" / "retarget_zombie_in_blender.py"), "--", str(model), str(ual1),
                        str(ual2), str(exported)], check=True)

        gltf_path = exported / "zombie_hazmat.gltf"
        gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
        if OUT.exists():
            shutil.rmtree(OUT)
        (OUT / "textures").mkdir(parents=True)
        encoding = roles(gltf)
        for index, image in enumerate(gltf["images"]):
            source = exported / image["uri"]
            output = Path("textures") / (Path(image["uri"]).stem + ".ktx2")
            subprocess.run([str(tool), "--max-size", str(TEXTURE_SIZE), encoding.get(index, "data"), str(source),
                            str(OUT / output)], check=True, capture_output=True)
            gltf["images"][index] = {"uri": output.as_posix(), "mimeType": "image/ktx2"}
        for texture in gltf["textures"]:
            texture["extensions"] = {BASISU: {"source": texture.pop("source")}}
        for name in ("extensionsUsed", "extensionsRequired"):
            gltf[name] = sorted(set(gltf.get(name, [])) | {BASISU})
        gltf["asset"]["copyright"] = ("Zombie Hazmat by LxNazarov, CC-BY 4.0; animations by Quaternius, CC0 "
                                      "(assets/Factory/Monsters/LICENSE.md)")
        (OUT / "zombie_hazmat.gltf").write_text(json.dumps(gltf, indent=1), encoding="utf-8")
        shutil.copy(exported / "zombie_hazmat.bin", OUT / "zombie_hazmat.bin")
    print(f"Wrote {OUT.relative_to(ROOT)}: clips {[a['name'] for a in gltf['animations']]}")


if __name__ == "__main__":
    main()
