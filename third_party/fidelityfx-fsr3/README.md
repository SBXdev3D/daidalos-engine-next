# FidelityFX FSR 3.1

AMD's FSR 3.1 upscaler, from the FidelityFX SDK, https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK,
tag `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`. MIT licence, in `LICENSE.txt`.

`gpu/` holds the HLSL headers the upscaler's passes are built from, copied unchanged from
`sdk/include/FidelityFX/gpu`: the core headers, `fsr3upscaler/` without AMD's callbacks, and `spd/ffx_spd.h`.
The engine replaces AMD's callbacks with its own, in `engine/renderer/shaders/Fsr3.slangh`, and runs the
passes itself (ADR-0050). AMD's host library and backends are not used.

To update: copy the same files from a newer 1.x release, then run the renderer's GPU tests.
