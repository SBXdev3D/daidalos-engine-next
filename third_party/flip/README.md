# FLIP

`FLIP.h` is NVIDIA's reference C++ implementation of ꟻLIP, a perceptual difference between two images
(Andersson et al. 2020), from https://github.com/NVlabs/flip, `src/cpp/FLIP.h` at commit
`d3ab02a4425eb167d8a4021b382a2bafc6e6c838`, unchanged. BSD 3-Clause licence, in `LICENSE`.

Only the GPU tests use it, to compare the renderer with its reference path tracer (ADR-0035). It is
not part of the engine.
