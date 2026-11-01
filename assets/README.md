# Assets

The sandbox loads models from this directory, which is its asset root. Downloaded assets are not committed:

```bash
python scripts/fetch_sample_assets.py
```

| Model | Source | Licence |
|---|---|---|
| Lantern | [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern) | CC0 1.0 (model and textures) |

Each model's `LICENSE.md` is downloaded with it.

`Factory/` comes from `scripts/import_legacy_level.py`. Its art and sounds are of unknown origin, so it is for local use only (see [docs/DEMO.md](../docs/DEMO.md)).

A model joins this table only if its licence allows showing it anywhere, including in paid posts. Sponza does not qualify: its files are under the CRYENGINE Limited License Agreement rather than an open licence. It can still be tried locally by placing it here and starting the sandbox with `+sandbox.scene <path>`.
