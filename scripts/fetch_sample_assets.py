#!/usr/bin/env python3
"""Downloads the sample scene of the sandbox into assets/, which git ignores.

    python scripts/fetch_sample_assets.py

Models come from Khronos' glTF-Sample-Assets. Only models with a licence that allows showing them
anywhere belong here: see assets/README.md.
"""

import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path

BASE_URL = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models"
MODELS = ["Lantern"]  # CC0 1.0.
ROOT = Path(__file__).resolve().parent.parent / "assets"


def download(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        path.write_bytes(response.read())
    print(path.relative_to(ROOT))


def fetch_model(name):
    base = f"{BASE_URL}/{name}"
    directory = ROOT / name
    download(f"{base}/LICENSE.md", directory / "LICENSE.md")

    gltf_path = directory / "glTF" / f"{name}.gltf"
    download(f"{base}/glTF/{name}.gltf", gltf_path)
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    for entry in gltf.get("buffers", []) + gltf.get("images", []):
        uri = entry.get("uri", "")
        if uri and not uri.startswith("data:"):
            local = urllib.parse.unquote(uri)
            download(f"{base}/glTF/{uri}", directory / "glTF" / local)


def main():
    try:
        for name in MODELS:
            fetch_model(name)
    except OSError as error:
        sys.exit(f"Download failed: {error}")


if __name__ == "__main__":
    main()
