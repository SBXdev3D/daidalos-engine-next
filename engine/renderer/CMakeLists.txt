dai_add_module(NAME renderer
    SOURCES
        include/daidalos/renderer/CornellBox.hpp
        include/daidalos/renderer/DarkCorridor.hpp
        include/daidalos/renderer/DisplayTransform.hpp
        include/daidalos/renderer/LightUnits.hpp
        include/daidalos/renderer/MirrorRoom.hpp
        include/daidalos/renderer/Material.hpp
        include/daidalos/renderer/Particles.hpp
        include/daidalos/renderer/Portals.hpp
        include/daidalos/renderer/Primitives.hpp
        include/daidalos/renderer/Renderer.hpp
        include/daidalos/renderer/SceneView.hpp
        include/daidalos/ui/Canvas.hpp
        include/daidalos/ui/Context.hpp
        include/daidalos/ui/Font.hpp
        src/Canvas.cpp
        src/Context.cpp
        src/CornellBox.cpp
        src/DarkCorridor.cpp
        src/Bloom.cpp
        src/CameraEffects.cpp
        src/Bloom.hpp
        src/DisplayTransform.cpp
        src/DynamicResolution.cpp
        src/DynamicResolution.hpp
        src/Exposure.cpp
        src/FrameCapture.cpp
        src/FrameCapture.hpp
        src/Exposure.hpp
        src/LightUnits.cpp
        src/MirrorRoom.cpp
        src/Denoiser.cpp
        src/Denoiser.hpp
        src/DirectLighting.cpp
        src/DirectLighting.hpp
        src/GlobalIllumination.cpp
        src/GlobalIllumination.hpp

        src/LightSampling.cpp
        src/LightSampling.hpp
        src/Material.cpp
        src/MeshPass.cpp
        src/MeshPass.hpp
        src/ParticlePass.cpp
        src/Particles.cpp
        src/ParticlePass.hpp
        src/Portals.cpp
        src/Primitives.cpp
        src/PathTracerPass.cpp
        src/PathTracerPass.hpp
        src/RayTracingScene.cpp
        src/RayTracingScene.hpp
        src/RenderGraph.cpp
        src/RenderGraph.hpp
        src/Renderer.cpp
        src/ShaderCompiler.cpp
        src/ShaderCompiler.hpp
        src/Fsr3Upscaler.cpp
        src/Fsr3Upscaler.hpp
        src/TemporalResolvePass.cpp
        src/TemporalResolvePass.hpp
        src/UiPass.cpp
        src/UiPass.hpp
        src/VolumetricFog.cpp
        src/VolumetricFog.hpp
    PUBLIC_DEPS
        daidalos::core
        daidalos::platform
        daidalos::resources
        daidalos::rhi)

dai_add_shaders(TARGET daidalos_renderer
    SOURCES shaders/GBuffer.slang
        shaders/Output.slang shaders/ParticleDraw.slang shaders/ParticleDistortion.slang shaders/ParticleMesh.slang shaders/Ui.slang shaders/RayDebug.slang
        shaders/GBufferDebug.slang shaders/Composite.slang shaders/Glass.slang
    COMPUTE shaders/PathTracer.slang shaders/Skinning.slang shaders/TemporalResolve.slang shaders/ParticleEmit.slang shaders/ParticleSubEmit.slang shaders/ParticleSimulate.slang
        shaders/ParticleFinish.slang shaders/ParticleSort.slang shaders/DenoiseTemporal.slang shaders/DenoiseSpatial.slang
        shaders/GiUpdate.slang shaders/GiResolve.slang shaders/GiTrace.slang shaders/GiFill.slang shaders/GiReflect.slang
        shaders/RestirCandidates.slang shaders/RestirTemporal.slang shaders/RestirSpatial.slang
        shaders/RestirSpatialUnbiased.slang shaders/RestirShade.slang
        shaders/EmissiveAverage.slang shaders/EmissiveTriangles.slang shaders/ExposureHistogram.slang shaders/Bloom.slang
        shaders/FogLighting.slang shaders/FogIntegrate.slang shaders/FogSmoke.slang shaders/CameraEffects.slang
        shaders/MaterialGrid.slang
    INCLUDES shaders/Bsdf.slang shaders/RayQueries.slangh shaders/GlobalIllumination.slangh shaders/MeshCommon.slangh shaders/GBufferSurface.slangh shaders/MeshTypes.slangh shaders/Particles.slangh shaders/ParticleSpawning.slangh
        shaders/Denoiser.slangh shaders/Random.slangh shaders/Lights.slangh shaders/GBuffer.slangh shaders/Restir.slangh shaders/RestirSpatial.slangh shaders/Fog.slangh
        shaders/Decals.slangh shaders/Wetness.slangh shaders/ParticleTypes.slangh shaders/MaterialGrid.slangh)

# AMD FSR 3.1's upscaler (ADR-0050): AMD's passes, unchanged, through the engine's callbacks. Its headers
# test macros they leave undefined (15205) and keep code after a return (41000); its pyramids use quad
# operations.
dai_add_shaders(TARGET daidalos_renderer
    COMPUTE shaders/Fsr3PrepareInputs.slang shaders/Fsr3LumaPyramid.slang shaders/Fsr3ShadingChangePyramid.slang
        shaders/Fsr3ShadingChange.slang shaders/Fsr3PrepareReactivity.slang shaders/Fsr3LumaInstability.slang
        shaders/Fsr3Accumulate.slang
    INCLUDES shaders/Fsr3.slangh
    INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/third_party/fidelityfx-fsr3/gpu
    ARGUMENTS -warnings-disable 15205 -warnings-disable 41000 -DFFX_HLSL_SM=62 -capability spvGroupNonUniformQuad)

target_compile_definitions(daidalos_renderer
    PUBLIC DAI_DEV_TOOLS=$<BOOL:${DAI_ENABLE_DEV_TOOLS}>
    PRIVATE
        DAI_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        DAI_SLANGC_PATH="${SLANGC_EXECUTABLE}")

# The Dear ImGui overlay exists in development builds only.
if(DAI_ENABLE_DEV_TOOLS)
    target_sources(daidalos_renderer PRIVATE include/daidalos/debugui/DebugUi.hpp src/DebugUi.cpp)
    target_link_libraries(daidalos_renderer PUBLIC imgui::imgui)
    dai_add_shaders(TARGET daidalos_renderer SOURCES shaders/DebugUi.slang)
endif()
