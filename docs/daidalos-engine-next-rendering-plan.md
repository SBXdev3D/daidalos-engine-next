# Daídalos Engine Next — Rendering Technology Plan

> **Audience:** the AI agent implementing the engine.
> **Scope:** rendering architecture and the set of technologies to implement.
> **Status:** plan v1.2 (2026-11). All owner decisions are made; ready for implementation.
>
> **Owner decisions (v1.1):**
> - **Use case: games.** Real-time performance and frame-time stability come first. The reference path tracer is a dev and validation tool only, not a player-facing mode.
> - **Minimum GPU: NVIDIA RTX 30xx or equivalent** (AMD RX 6000 / RDNA 2, Intel Arc A-series).
> - **No Nanite-style cluster LOD DAG for now.** Use classic discrete LODs per meshlet set; the DAG stays in Phase 7 as a possible later addition.
> - **Shading language: Slang** (§3.1), migrated pass by pass.
> - **Denoiser: NVIDIA NRD** (§7.4), behind an engine-side `IDenoiser` interface.
> - **Target frame rate: 60 fps** on every supported tier (§12).
> - **Output resolution: 1080p (minimum tier), 1440p (recommended tier)** (§12).

> **Engine decisions (2026-11-05, ADR-0028).** This plan is the long-term target of the engine's Phase 9, which departs from it as follows:
> - Its phases 0 to 5 are steps 9.1 to 9.12 of the engine roadmap (`OVERVIEW.md`); its Phase 6 (neural tier) is out of scope and its Phase 7 is deferred.
> - The geometry pipeline (§4: meshlets, mesh shaders, LODs, Hi-Z, visibility buffer, portals) is deferred until a measurement shows rasterization is the bottleneck. The forward pipeline and its prepass stay.
> - NRD, SHaRC, RTXDI and DLSS are not used: the NVIDIA RTX SDKs License (section 4(e)) conflicted with the engine's CC BY-NC-SA 4.0 licence. The denoiser, radiance cache and ReSTIR are own implementations; upscaling uses FSR 2 (MIT). Since ADR-0047 the engine is MIT or Apache 2.0 and the conflict is gone; the own implementations stay, and DLSS may be considered later as an optional path.
> - Slang is compiled by `slangc` at build time and for hot reload; the engine does not embed the compiler.
> - One binding model: descriptor indexing. `VK_EXT_descriptor_heap` waits for KHR.
> - Layered material lobes, IES profiles and area lights come when content needs them.
> - Verified on one GPU only, an RTX 3090: 60 fps at 1080p.
>
> **Engine technical spec (2026-11-12, ADR-0041).** The owner's engine technical spec widens the target to a horror game. Physical light units, dynamic resolution, quality presets and persistent pipeline caches join Phase 9 (steps 9.10 to 9.15); layered materials, IES profiles and area lights get a phase of their own (Phase 11). NVIDIA's SDKs stay out.

---

## 1. Context and goals

- The engine is **indoor-only**: rooms, corridors, buildings and interiors. There is no terrain, no open world, no large-scale foliage and no sky-dominated lighting. Sunlight and skylight come in only through openings (windows, doors, skylights).
- The engine is currently **Vulkan 1.3**. The target is **Vulkan 1.4 as the minimum baseline**.
- **Philosophy: go all-in on modern hardware.** There is no legacy path and no rasterization-only fallback for lighting. Hardware ray tracing is required.
- **The quality bar is physically based, fully dynamic lighting.** Moving a lamp, opening a door or changing a material must update direct light, GI, reflections and shadows in real time, with no baking.

### What indoor-only implies (design drivers)
1. **Indirect light dominates the image.** Most of what you see in an interior is bounced light, so GI quality is the top priority.
2. **There are many local lights** (lamps, spots, emissive panels, screens), so we need many-light sampling.
3. **There are many glossy surfaces** (floors, glass, metal, mirrors, lacquered wood), so we need ray-traced reflections.
4. **Walls are thin and visibility is high-contrast.** Light leaking is the worst possible artifact, so any technique that leaks (coarse probe grids) is excluded or must be leak-proofed.
5. **Visibility is heavily occluded.** Portals and cells plus GPU occlusion culling give large gains.
6. **Light shafts through windows and dust** are a signature look, so we need volumetrics with ray-traced shadowing.

### Non-goals
- Terrain, large-scale vegetation, ocean and sky/weather systems beyond a sky model seen through windows.
- Mobile, WebGPU, or GPUs without hardware RT.
- Baked lightmaps and offline light baking. These can be added later as an optional "static scene" acceleration, but no system may *depend* on them.

---

## 2. Hardware and API baseline

### 2.1 Hardware tiers (decided)
| Tier | GPUs | What it enables |
|---|---|---|
| **Minimum** | NVIDIA RTX 30xx, AMD RX 6000 (RDNA 2), Intel Arc A-series | Vulkan 1.4, HW RT (ray query), mesh shaders. Full feature set at reduced GI/reflection resolution (see §12 scalability). |
| **Recommended** | RTX 40xx+, RX 7000+/9000, Arc B-series | Full ReSTIR DI/GI at target resolution. SER and opacity micromap fast paths (Ada and newer). |
| **Neural tier** | RTX 50xx (cooperative vector), RDNA 3+/Arc with cooperative matrix | Neural texture compression, neural materials, neural radiance cache |

Notes on the minimum tier:
- RDNA 2 has the weakest RT throughput in the minimum tier and no matrix/WMMA units. **Profile on an RX 6700 XT-class GPU as the worst-case target**, not only on an RTX 3060.
- Shader execution reordering and hardware opacity micromaps are not available on RTX 30xx / RDNA 2. They must stay optional fast paths.
- The neural tier is **optional at runtime**. Every neural feature must have a non-neural fallback (see §10). Detect cooperative vector/matrix support per device; don't infer it from the vendor.

### 2.2 Migrate to Vulkan 1.4
Raise `VkApplicationInfo::apiVersion` to `VK_API_VERSION_1_4`. Features that are core in 1.4 and that we use:
- `pushDescriptor` (core in 1.4): used for small per-pass bindings when the descriptor heap is not available.
- `dynamicRenderingLocalRead`: used for tile-local reads in raster passes (decals and similar).
- `maintenance5` / `maintenance6`: `vkCmdBindIndexBuffer2`, `VkPipelineCreateFlags2`, and the like.
- `hostImageCopy` (optional in 1.4 core): CPU-side texture uploads and streaming without staging buffers. Required by Roadmap 2026.
- `shaderSubgroupRotate`, `shaderFloatControls2`, `shaderExpectAssume`.

Features already core since 1.2/1.3 that we must enable explicitly:
- `bufferDeviceAddress`, `descriptorIndexing` (all `*UpdateAfterBind` and `runtimeDescriptorArray`), `timelineSemaphore`, `synchronization2`, `dynamicRendering`, `maintenance4`, `shaderFloat16`, `shaderInt8`, `shaderInt64`, `subgroupSizeControl`, `computeFullSubgroups`, `shaderIntegerDotProduct`, `scalarBlockLayout`, `vulkanMemoryModel`.

### 2.3 Required device extensions
| Extension | Purpose |
|---|---|
| `VK_KHR_acceleration_structure` (+ `VK_KHR_deferred_host_operations`) | BLAS/TLAS |
| `VK_KHR_ray_query` | **Main RT path.** Inline rays from compute and fragment shaders. |
| `VK_KHR_ray_tracing_pipeline` | Reference path tracer and RT-pipeline passes where shader binding table divergence helps |
| `VK_KHR_ray_tracing_maintenance1` | Indirect trace calls and AS queries |
| `VK_KHR_ray_tracing_position_fetch` | Fetch hit triangle positions without a duplicate vertex buffer |
| `VK_EXT_mesh_shader` | Meshlet pipeline, task-shader culling |
| `VK_EXT_shader_image_atomic_int64` | Single-pass 64-bit visibility buffer (depth + IDs) |
| `VK_KHR_shader_clock` | In-shader GPU profiling (Roadmap 2026) |
| `VK_EXT_memory_budget`, `VK_EXT_memory_priority` | Residency and streaming decisions |
| `VK_EXT_swapchain_colorspace`, `VK_EXT_hdr_metadata` | HDR10 / scRGB output |
| `VK_KHR_present_id2` / `VK_KHR_present_wait2` (or v1 variants) | Frame pacing, latency control |

### 2.4 Optional extensions (detect at runtime, enable fast paths)
| Extension | Purpose | Fallback |
|---|---|---|
| `VK_EXT_descriptor_heap` | **Preferred bindless model.** Descriptors addressed by heap offset, with no descriptor sets or pipeline layouts. | Descriptor indexing with one global bindless set |
| `VK_EXT_opacity_micromap` | Fast alpha-tested RT (plants, fabrics, grilles, curtains) | Any-hit / alpha test in ray query |
| `VK_NV_ray_tracing_invocation_reorder` (or the EXT equivalent if exposed) | Shader execution reordering for divergent RT | None needed |
| `VK_KHR_fragment_shading_rate` | VRS on dark or low-detail regions (Roadmap 2026) | Full rate |
| `VK_KHR_compute_shader_derivatives` | Texture LOD/derivatives in compute material passes (Roadmap 2026) | Analytic derivatives from the visibility buffer (store barycentric derivatives) |
| `VK_KHR_cooperative_matrix` | Cross-vendor tile-level MLP inference (denoiser, radiance cache) | Plain FP16 compute |
| `VK_NV_cooperative_vector` | Per-pixel inline MLP inference (neural materials, NTC) | Cooperative matrix, then conventional path |
| `VK_EXT_shader_object` | Less pipeline-state overhead for tools and debug passes | Regular pipelines |

> **Rule:** the binding model must be abstracted so that `VK_EXT_descriptor_heap` and the descriptor-indexing fallback are interchangeable behind one interface. The extension is still EXT; Khronos intends to promote it to KHR.

---

## 3. Core architecture

### 3.1 Shading language
- **Slang** compiled to SPIR-V. Reasons: modules and generics for material and BSDF composition, automatic differentiation (needed later to train neural materials), and first-class support in NVIDIA's neural shading samples.
- **Decided: Slang is the engine's shading language.** All new shaders are written in Slang.
- **Integration:**
  - Embed the Slang compiler API in the engine and tools (no shelling out to `slangc`) for runtime hot reload and offline builds.
  - Target SPIR-V (Vulkan 1.4 profile); enable the capabilities matching §2.3/§2.4 and gate optional features with Slang capability checks.
  - Use the **reflection API** to generate binding/heap layouts and C++ mirror structs automatically. No hand-written duplicate structs.
  - Cache compiled SPIR-V keyed by source hash + defines + target; precompile every permutation shipped in a game build.
- **Code organization:**
  - Slang `module`s for shared libraries: `math`, `sampling`, `bsdf`, `lights`, `restir`, `radiance_cache`, `scene`.
  - Materials and BSDF layers are expressed as Slang **interfaces + generics** (e.g. `IBSDFLayer`, `IMaterial`), not macro permutations.
  - The same BSDF modules are compiled for raster/compute passes, RT hit shaders and the reference path tracer, and optionally for CPU/C++ unit tests.
  - Keep differentiable functions marked `[Differentiable]` where useful (BSDFs, neural material decoders) to enable Phase 6 training.
- **Migration:** existing shaders are migrated pass by pass. HLSL sources can mostly compile as-is in Slang; GLSL sources are rewritten. Old and new shaders may coexist during the migration, but each pass is either fully migrated or not at all.
- **Debugging:** emit debug info (`-g`, NonSemantic.Shader.DebugInfo) in debug builds for RenderDoc/Nsight source-level debugging.

### 3.2 Render graph
- A frame graph with automatic barrier generation (`synchronization2`), transient resource aliasing and pass culling.
- **Async compute** for GI, denoising, AS builds and volumetrics, using a dedicated compute queue and timeline semaphores.
- Resources are declared per pass. Every pass gets GPU timestamps and debug labels (`VK_EXT_debug_utils`).

### 3.3 Memory
- VMA (Vulkan Memory Allocator) or an equivalent custom allocator. Large pooled allocations, buffer device address everywhere.
- Driven by the memory budget: texture and mesh streaming are keyed on the **portal/cell graph** (load adjacent rooms first).

### 3.4 GPU-driven scene
- The scene lives in GPU buffers: instances, meshlets, materials, lights, transforms. The CPU only uploads deltas.
- Persistent instance IDs are shared across raster, the RT TLAS and the light lists.
- TLAS rebuild or refit each frame. BLAS compaction for static meshes, refit for deformables, rebuild in the background when quality degrades.

---

## 4. Geometry pipeline

### 4.1 Meshlets
- Offline: split meshes into meshlets (≤64 vertices / ≤124 triangles; tune per vendor) with **meshoptimizer**. Store meshlet bounds (sphere + normal cone).
- **LOD (decided): classic discrete LODs.** Generate 3 to 5 LOD levels per mesh offline (meshoptimizer simplification), each split into its own meshlets. Select the LOD per instance in the culling compute pass by projected screen size, with hysteresis to avoid flicker. Use dithered/temporal cross-fade, which the TAA/upscaler resolves. Indoor view distances are short, so most instances stay at LOD 0 to 1.
- RT acceleration structures use a fixed mid-detail LOD per mesh (or LOD 0 for hero props) so that BLAS memory stays bounded.
- A Nanite-style cluster LOD DAG is **out of scope for now** (see Phase 7). Keep the meshlet data layout compatible so it can be added later without changing the asset format.

### 4.2 Culling
1. **Portal/cell culling (CPU or GPU):** the scene is authored as cells (rooms) linked by portals (doors, windows). Traverse from the camera cell, clipping the frustum through portals.
2. **Instance culling (compute):** frustum + previous-frame Hi-Z.
3. **Meshlet culling (task shader):** frustum, normal-cone backface, Hi-Z occlusion.
4. **Two-phase occlusion:** draw last frame's visible set, build Hi-Z, test the rest, draw the newly visible.
5. Small-triangle culling in the mesh shader.

### 4.3 Visibility buffer
- Rasterize **instance ID + meshlet ID + triangle ID** (packed) plus depth.
  - Preferred: a single-pass 64-bit atomic write (`VK_EXT_shader_image_atomic_int64`), with depth in the high bits and the payload in the low bits. This also enables a compute rasterizer for micro-triangles later.
  - Fallback: a conventional `R32G32_UINT` render target with a depth buffer.
- **Material pass:** classify screen tiles by material/shader permutation, then run indirect compute dispatches per bin. Reconstruct attributes (position, UV, normal, tangent) and analytic derivatives from barycentrics.
- Output: a thin G-buffer for lighting and denoising (normal, roughness, base color/albedo, metalness/F0, motion vectors, linear depth, material ID). Keep it as compact as possible.

### 4.4 Alpha-tested and transparent geometry
- Alpha-tested: a separate meshlet bin with alpha test in the visibility pass; opacity micromaps for RT.
- Transparent (glass, liquids): see §7.5.

---

## 5. Materials

### 5.1 BSDF model
A layered, energy-conserving material model (in the spirit of OpenPBR / UE Substrate):
- Base: GGX microfacet specular plus Lambert or a better diffuse model (e.g. EON / Oren-Nayar energy-preserving), multi-scattering energy compensation.
- Layers: **clear coat** (lacquered wood, car paint, varnish), **sheen** (fabrics, velvet; Charlie/Estevez-Kulla), **thin-film iridescence**, **transmission** (thin and thick), **subsurface** (screen-space or ray-traced diffusion for skin, wax, marble; low priority).
- Anisotropy (brushed metal).
- The same BSDF code is used by raster shading, ReSTIR, the RT reflection hit shader and the reference path tracer. **One BSDF implementation, never duplicated.**

### 5.2 Textures
- Baseline: BC7 (color), BC5 (normals), BC4 (masks), with virtual texturing or sparse residency optional.
- Neural texture compression (NTC) for the neural tier (§10.1). The BCn path stays the fallback.

### 5.3 Decals
- Deferred decals in the visibility-buffer material pass. Indoor scenes need lots of wear, dirt and stains.

---

## 6. Direct lighting

### 6.1 Light types
- Point / spot (with **IES profiles**, essential for architectural lighting).
- **Area lights: rectangle, disk, tube/line.** Use LTC (linearly transformed cosines) for the unshadowed analytic integral.
- **Emissive meshes** (screens, neon, light panels), sampled by ReSTIR via an emissive-triangle light list.
- Directional sun plus sky, entering **only through portals and windows**. Use portal-guided sampling: sample the window opening, not the whole sky dome.

### 6.2 Many-light sampling: ReSTIR DI
- Spatiotemporal reservoir resampling (ReSTIR DI) over all lights, including emissive triangles.
- Initial candidates come from a **light BVH or a world-space light grid**, so we stay efficient with thousands of lights.
- Shadowing: **ray-query visibility rays** (1 shadow ray per pixel after resampling).
- Unbiased or biased mode is a toggle. Default is biased for stability.
- Reference: Bitterli et al. 2020 (ReSTIR); NVIDIA RTXDI as a design reference.

### 6.3 Shadows
- All shadows are ray traced. No shadow maps in the main path.
- Soft shadows come naturally from area-light sampling.
- Optional: contact-hardening improvements via a short-range screen-space ray for very fine detail.

---

## 7. Global illumination, reflections and transparency

### 7.1 GI design (top priority)
**Requirement: zero light leaking through walls, fully dynamic, multi-bounce.**

Recommended architecture:
1. **ReSTIR GI:** per-pixel secondary-bounce paths, resampled spatiotemporally (Ouyang et al. 2021).
2. **World-space radiance cache** to terminate paths after 1 to 2 bounces and supply infinite bounces:
   - **Baseline:** a spatial hash-grid radiance cache (SHaRC-style), adaptive cell size by distance. Works on all RT hardware.
   - **Neural tier:** a neural radiance cache (NRC, Müller et al. 2021), an online-trained small MLP. Swappable with the hash grid behind the same interface.
3. **Leak prevention:** cache lookups happen *at ray hit points* (world space), so walls never average both sides. Hash cells include a normal-orientation bucket. Probe grids are not used as the primary GI.
4. Emissive surfaces and area lights feed GI through the same light sampling as ReSTIR DI (next-event estimation at secondary hits).

### 7.2 Specular reflections
- Ray-traced reflections for roughness below a threshold (~0.4 to 0.5). Above it, use ReSTIR GI / radiance cache sampling with the GGX lobe.
- The hit shader uses a reduced material evaluation plus a radiance cache lookup for indirect light at the hit.
- Mirrors and very smooth surfaces: allow 2 to 3 recursive bounces.

### 7.3 Ambient occlusion
- No separate SSAO in the final image. Occlusion comes from GI.
- Optional RTAO debug view for artists.

### 7.4 Denoising
- **Decided: NVIDIA NRD** (Real-time Denoisers), integrated through its Vulkan path (NRI) so it runs on NVIDIA, AMD and Intel.
  - **ReLAX** for diffuse and specular from ReSTIR DI/GI and RT reflections (designed for ReSTIR-style signals).
  - **SIGMA** for ray-traced shadows where a separate shadow signal is kept (e.g. sun through windows, hero lights).
  - **ReBLUR** kept as an alternative to A/B test against ReLAX per signal.
- **Abstraction:** all denoising goes through an engine-side `IDenoiser` interface in the render graph. NRD is the first implementation; DLSS Ray Reconstruction and a possible future in-house denoiser plug into the same interface.
- **G-buffer requirements:** adopt NRD's input conventions from Phase 1 (packed normal + roughness, linear view-Z, motion vectors including deformable geometry, diffuse/specular hit distances, radiance/hit-distance packing helpers). Designing the thin G-buffer (§4.3) around these avoids a second pass of conversions.
- Expose NRD tuning (history length, anti-lag, firefly suppression) in a debug UI and save per-scene presets.
- Verify the NRD license terms before shipping a game build.
- Neural tier option: DLSS Ray Reconstruction through NVIDIA Streamline, which replaces the denoiser and upscaler together on RTX.
- Requirements: correct motion vectors for all geometry, including deforming geometry; disocclusion handling; specular reprojection with virtual motion (parallax-correct reflections).

### 7.5 Transparency and glass
- **Glass and windows:** ray-traced refraction and transmission with the shared BSDF (thin-glass approximation for windows, thick dielectric for bottles and glasses). Rough glass reuses the reflection denoiser path.
- Other transparents (particles, holograms): sorted forward pass, plus weighted blended OIT as the fallback.
- Transparents receive ReSTIR DI lighting and a radiance cache lookup for GI.

### 7.6 Reference path tracer
- A dedicated **ground-truth mode** using `VK_KHR_ray_tracing_pipeline`, progressive and unbiased, with the same scene, BSDFs and lights.
- Used to validate every real-time technique (A/B toggle, image diff). **Implement it early.** It is the test oracle for everything in §6 and §7.

---

## 8. Participating media

- Froxel-based volumetric fog (camera-aligned 3D grid, ~160×90×128) with temporal reprojection.
- Per-froxel lighting: ReSTIR DI light samples plus **ray-query shadowing**. This produces the light shafts through windows and lamp cones.
- Local fog volumes (dusty rooms, steam, smoke). Optional heterogeneous volumes via 3D textures.
- Multiple-scattering approximation for dense media.

---

## 9. Image formation and output

### 9.1 Anti-aliasing and upscaling
- Render at reduced internal resolution. Upscaler is pluggable behind one interface:
  - In-house **TAA/TAAU** (always available; also the reference).
  - **AMD FidelityFX FSR** (Vulkan SDK), **NVIDIA DLSS** (Super Resolution, Ray Reconstruction, Frame Generation via Streamline), **Intel XeSS**.
- Jittered projection (Halton), reactive and transparency masks, exposure input.
- Frame generation is optional and paired with a latency reducer (Reflex / Anti-Lag) where available.

### 9.2 Camera and post
- Physically based camera: aperture, shutter and ISO drive exposure. Auto-exposure via histogram with manual override.
- Bloom (energy-conserving, multi-scale), physically based depth of field, per-pixel motion blur, lens effects (vignetting, chromatic aberration, optional), film grain (optional).
- Tone mapping: **AgX** (default) and ACES as an option. Proper display transforms.
- **HDR output** (HDR10 PQ / scRGB) with `VK_EXT_hdr_metadata`, plus SDR output.

### 9.3 Variable rate shading
- VRS (`VK_KHR_fragment_shading_rate`) or compute-based rate control in dark, low-contrast or motion-blurred regions. Low priority.

---

## 10. Neural rendering tier (phase 3+)

Every neural feature sits behind a feature flag with a conventional fallback.

| Feature | Technique | API path | Fallback |
|---|---|---|---|
| 10.1 **Neural texture compression** | Per-material latent textures plus a small MLP decoder (NVIDIA RTX NTC SDK as reference) | Cooperative vector inline in the material pass; or decode-on-load to BCn | BCn textures |
| 10.2 **Neural materials** | MLP approximation of complex layered BSDFs (fabric, car paint), trained offline with Slang autodiff | Cooperative vector | Analytic layered BSDF (§5.1) |
| 10.3 **Neural radiance cache** | Online-trained MLP (Müller 2021) | Cooperative matrix (train and infer in compute) | Hash-grid cache (§7.1) |
| 10.4 **Neural denoiser / reconstruction** | DLSS RR (vendor), or own small U-Net | Streamline / cooperative matrix | NRD / own denoiser (§7.4) |
| 10.5 **Neural upscaling / frame gen** | DLSS / FSR / XeSS | Vendor SDKs | TAAU |

Rules:
- Keep inference cost per feature within an explicit ms budget (§12).
- Weights are versioned assets in the asset pipeline.
- Never put a neural feature on the critical path of correctness. The reference path tracer stays the ground truth.

---

## 11. Tools, debug and validation

- **Debug views:** visibility buffer IDs, meshlets, overdraw, Hi-Z, G-buffer channels, ReSTIR reservoir weights/age, radiance cache cells, denoiser inputs/outputs, light-count heatmap, ray count heatmap, shading rate.
- **GPU profiling:** per-pass timestamps in the render graph, in-shader `shaderClock` instrumentation, RenderDoc and Nsight / Radeon GPU Profiler compatible labels.
- **Validation:** Vulkan validation layers including **synchronization validation** in debug builds; GPU-assisted validation for bindless out-of-bounds access.
- **Image regression tests:** fixed camera paths in test scenes, real-time output vs the reference path tracer, with automated FLIP/PSNR metrics and thresholds.
- **Test scenes (indoor):**
  1. Cornell box (energy conservation, color bleeding).
  2. Single room with one window and sun (light shafts, leaking test: a thin wall between a lit and a dark room).
  3. Many-lights corridor (500+ lights).
  4. Glossy showroom (mirrors, glass, metal, lacquered floor).
  5. Furnished apartment (production-like content, fabrics, decals, emissive screens).

---

## 12. Performance targets

**Decided: 60 fps (16.6 ms per frame) is the target on every supported tier.** Quality presets and internal resolution scale to hold it; the frame rate is never traded away for quality by default.

Output resolutions per tier (decided):
- **Minimum tier (RTX 3060 / RX 6700 XT class): 1080p output, 60 fps (16.6 ms), upscaler at ~58 to 67% internal resolution, "Medium" preset.**
- **Recommended tier (RTX 4070 class): 1440p output, 60 fps, upscaler at ~67% internal resolution, "High" preset.**
- Frame-time stability matters more than average fps: no hitches above 2× the frame budget from AS rebuilds, streaming or shader compilation (precompile all pipelines; use pipeline caches).

### Scalability presets (required for the minimum tier)
| Setting | Medium (min tier) | High | Ultra |
|---|---|---|---|
| ReSTIR GI resolution | half-res, upsampled | full-res | full-res + extra bounce |
| Radiance cache | hash grid, coarse cells | hash grid | hash grid or NRC |
| RT reflections | half-res, roughness cutoff 0.3 | full-res, cutoff 0.45 | full-res, 2 to 3 bounces on mirrors |
| ReSTIR DI | 1 shadow ray, reduced candidates | 1 shadow ray | 1 to 2 shadow rays |
| Volumetric froxels | 128×72×64 | 160×90×128 | 240×135×128 |
| Upscaler | DLSS/FSR/XeSS Performance or Balanced | Quality | Quality / DLAA |

Budget at the High preset:

Indicative frame budget:
| Stage | Budget (ms) |
|---|---|
| Culling + visibility buffer | 1.5 |
| Material pass / G-buffer | 1.5 |
| ReSTIR DI + shadow rays | 2.5 |
| ReSTIR GI + radiance cache | 3.5 |
| Reflections | 2.0 |
| Denoising | 1.5 |
| Volumetrics | 1.0 |
| Upscale + post + UI | 1.5 |
| Headroom | ~1.6 |

---

## 13. Implementation roadmap

Each phase ends with its acceptance criteria met before the next starts.

### Phase 0 — Foundation
- Vulkan 1.4 migration, feature/extension detection, capability tiers.
- Slang toolchain: embedded compiler, module layout, reflection-driven bindings, SPIR-V cache, hot reload. Migrate existing shaders pass by pass.
- Render graph with automatic barriers, async compute, timestamps.
- Bindless abstraction (descriptor heap path plus descriptor indexing fallback).
- **Accept:** validation clean (including sync validation); a triangle scene runs on both bindless paths; per-pass GPU timings visible.

### Phase 1 — Geometry and visibility
- Meshlet asset pipeline (meshoptimizer) with discrete LOD generation, mesh/task shader pipeline, GPU LOD selection.
- GPU instance and meshlet culling, two-phase Hi-Z occlusion.
- Portal/cell scene representation and traversal.
- Visibility buffer plus tile-binned material pass, thin G-buffer, motion vectors.
- **Accept:** the furnished apartment scene renders with correct culling; debug views working; at least 2× fewer meshlets drawn vs frustum-only culling in multi-room scenes.

### Phase 2 — Ray tracing core and reference
- BLAS/TLAS management (compaction, refit, rebuild), opacity micromaps.
- Shared BSDF library (§5.1).
- **Reference path tracer** (§7.6).
- Ray-query shadows for a few analytic lights.
- **Accept:** path tracer converges on the Cornell box matching a known reference; real-time shadows match the path tracer direct-only output.

### Phase 3 — Direct lighting at scale
- All light types (IES, LTC area lights, emissive meshes, portal-guided sun/sky).
- Light BVH or grid, ReSTIR DI.
- `IDenoiser` interface + first NRD integration (ReLAX for ReSTIR DI output, SIGMA for dedicated shadow signals).
- **Accept:** the 500+ light corridor holds budget; noise-free after denoising; FLIP vs reference under threshold.

### Phase 4 — Global illumination and reflections
- ReSTIR GI, hash-grid radiance cache, leak-proofing.
- RT reflections, glass/transmission; extend NRD (ReLAX) to GI diffuse and reflection specular signals.
- **Accept:** zero visible leaking in the thin-wall test; multi-bounce matches the reference within threshold; stable under camera motion and when lights move or doors open.

### Phase 5 — Image formation
- Volumetric fog with RT shadows.
- TAAU plus vendor upscalers, physically based camera, bloom, DOF, motion blur, AgX, HDR output.
- **Accept:** full frame at target resolution and frame rate on the Recommended tier.

### Phase 6 — Neural tier
- NTC, neural radiance cache, neural materials, DLSS Ray Reconstruction integration.
- **Accept:** each feature A/B-togglable with measured quality (FLIP) and cost (ms), and its fallback still working.

### Phase 7 — Optional extensions
- Cluster LOD DAG, compute micro-rasterizer, VRS, SER, subsurface scattering, heterogeneous volumes.

---

## 14. Decisions

Resolved:
- ✅ Use case: **games** (§1).
- ✅ Minimum hardware: **RTX 30xx or equivalent** (§2.1).
- ✅ Cluster LOD DAG: **not in scope for now**; discrete LODs (§4.1).
- ✅ Shading language: **Slang** (§3.1).
- ✅ Denoiser: **NRD** behind `IDenoiser` (§7.4).
- ✅ Frame rate: **60 fps** on all tiers (§12).

- ✅ Output resolution: **1080p on the minimum tier, 1440p on the recommended tier** (§12).

No open decisions remain. Implementation can start at Phase 0.

---

## 15. Key references (by name)
- Bitterli et al., *Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting* (ReSTIR), SIGGRAPH 2020.
- Ouyang et al., *ReSTIR GI: Path Resampling for Real-Time Path Tracing*, HPG 2021.
- Lin et al., *Generalized Resampled Importance Sampling* (ReSTIR PT / GRIS), SIGGRAPH 2022.
- Müller et al., *Real-Time Neural Radiance Caching for Path Tracing*, SIGGRAPH 2021.
- Heitz et al., *Real-Time Polygonal-Light Shading with Linearly Transformed Cosines*, SIGGRAPH 2016.
- Karis et al., *Nanite: A Deep Dive*, SIGGRAPH 2021 (cluster LOD DAG, visibility buffer).
- Burns & Hunt, *The Visibility Buffer*, JCGT 2013.
- NVIDIA RTXDI, NRD, SHaRC, RTX Neural Texture Compression, RTX Neural Shading SDKs (design references).
- meshoptimizer (Arseny Kapoulkine), meshlet generation and simplification.
- Khronos: Vulkan 1.4 spec, Vulkan Roadmap 2026 milestone, `VK_EXT_descriptor_heap` proposal.
- OpenPBR Surface specification (layered material model reference).
