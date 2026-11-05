dai_add_module(NAME renderer
    SOURCES
        include/daidalos/renderer/Material.hpp
        include/daidalos/renderer/Particles.hpp
        include/daidalos/renderer/Primitives.hpp
        include/daidalos/renderer/Renderer.hpp
        include/daidalos/renderer/SceneView.hpp
        include/daidalos/ui/Canvas.hpp
        include/daidalos/ui/Font.hpp
        src/AmbientOcclusionPass.cpp
        src/AmbientOcclusionPass.hpp
        src/Canvas.cpp
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
    SOURCES shaders/Mesh.hlsl shaders/Prepass.hlsl shaders/ShadowMap.hlsl shaders/Ssao.hlsl shaders/SsaoBlur.hlsl
        shaders/Tonemap.hlsl shaders/ParticleDraw.hlsl shaders/Fxaa.hlsl shaders/Ui.hlsl
    COMPUTE shaders/ParticleEmit.hlsl shaders/ParticleSimulate.hlsl shaders/ParticleFinish.hlsl shaders/ParticleSort.hlsl
    INCLUDES shaders/AmbientOcclusion.hlsli shaders/MeshCommon.hlsli shaders/MeshTypes.hlsli shaders/Particles.hlsli)

target_compile_definitions(daidalos_renderer
    PUBLIC DAI_DEV_TOOLS=$<BOOL:${DAI_ENABLE_DEV_TOOLS}>
    PRIVATE
        DAI_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        DAI_DXC_PATH="${DIRECTX_DXC_TOOL}")

# The Dear ImGui overlay exists in development builds only.
if(DAI_ENABLE_DEV_TOOLS)
    target_sources(daidalos_renderer PRIVATE include/daidalos/debugui/DebugUi.hpp src/DebugUi.cpp)
    target_link_libraries(daidalos_renderer PUBLIC imgui::imgui)
    dai_add_shaders(TARGET daidalos_renderer SOURCES shaders/DebugUi.hlsl)
endif()
