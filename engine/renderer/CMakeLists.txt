dai_add_module(NAME renderer
    SOURCES
        include/daidalos/renderer/CornellBox.hpp
        include/daidalos/renderer/Material.hpp
        include/daidalos/renderer/Particles.hpp
        include/daidalos/renderer/Primitives.hpp
        include/daidalos/renderer/Renderer.hpp
        include/daidalos/renderer/SceneView.hpp
        include/daidalos/ui/Canvas.hpp
        include/daidalos/ui/Context.hpp
        include/daidalos/ui/Font.hpp
        src/AmbientOcclusionPass.cpp
        src/AmbientOcclusionPass.hpp
        src/Canvas.cpp
        src/Context.cpp
        src/CornellBox.cpp
        src/FxaaPass.cpp
        src/FxaaPass.hpp
        src/LightClusters.cpp
        src/LightClusters.hpp
        src/Material.cpp
        src/MeshPass.cpp
        src/MeshPass.hpp
        src/ParticlePass.cpp
        src/ParticlePass.hpp
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
        src/UiPass.cpp
        src/UiPass.hpp
    PUBLIC_DEPS
        daidalos::core
        daidalos::platform
        daidalos::resources
        daidalos::rhi)

dai_add_shaders(TARGET daidalos_renderer
    SOURCES shaders/Mesh.slang shaders/Prepass.slang shaders/Ssao.slang shaders/SsaoBlur.slang
        shaders/Tonemap.slang shaders/ParticleDraw.slang shaders/Fxaa.slang shaders/Ui.slang shaders/RayDebug.slang
    COMPUTE shaders/PathTracer.slang shaders/Skinning.slang shaders/ParticleEmit.slang shaders/ParticleSimulate.slang
        shaders/ParticleFinish.slang shaders/ParticleSort.slang
    INCLUDES shaders/Bsdf.slang shaders/RayQueries.slangh shaders/AmbientOcclusion.slangh shaders/MeshCommon.slangh shaders/MeshTypes.slangh shaders/Particles.slangh)

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
