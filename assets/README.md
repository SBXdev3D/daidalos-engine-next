# Assets

The sandbox loads models from this directory, which is its asset root. `Factory/`, the demo's level, is committed. Downloaded assets are not:

```bash
python scripts/fetch_sample_assets.py
```

| Model | Source | Licence |
|---|---|---|
| Lantern | [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern) | CC0 1.0 (model and textures) |

Each model's `LICENSE.md` is downloaded with it.

`Factory/` has its own terms, in [its licence file](Factory/LICENSE.md). It was converted once from the legacy engine, so playing the demo needs no script.

`Fonts/` holds Rajdhani Medium and Bold by the Indian Type Foundry, from [Google Fonts](https://github.com/google/fonts/tree/main/ofl/rajdhani), under the SIL Open Font License 1.1 in [OFL.txt](Fonts/OFL.txt). The game's interface draws with them (ADR-0027).

A model joins this table only if its licence allows showing it anywhere, including in paid posts. Sponza does not qualify: its files are under the CRYENGINE Limited License Agreement rather than an open licence. It can still be tried locally by placing it here and starting the sandbox with `+sandbox.scene <path>`.
