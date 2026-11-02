dai_add_module(NAME renderer
    SOURCES
        include/daidalos/renderer/AssetLibrary.hpp
        include/daidalos/renderer/Components.hpp
        include/daidalos/renderer/Material.hpp
        include/daidalos/renderer/Primitives.hpp
        include/daidalos/renderer/Renderer.hpp
        include/daidalos/renderer/Scene.hpp
        src/AssetLibrary.cpp
        src/Material.cpp
        src/MeshPass.cpp
        src/MeshPass.hpp
        src/Primitives.cpp
        src/Renderer.cpp
        src/Scene.cpp
        src/ShaderCompiler.cpp
        src/ShaderCompiler.hpp
    PUBLIC_DEPS
        daidalos::animation
        daidalos::core
        daidalos::ecs
        daidalos::math
        daidalos::platform
        daidalos::resources
        daidalos::rhi
    PRIVATE_DEPS
        daidalos::logging)

dai_add_shaders(TARGET daidalos_renderer
    SOURCES shaders/Mesh.hlsl
    INCLUDES shaders/MeshTypes.hlsli)

target_compile_definitions(daidalos_renderer
    PUBLIC DAI_DEV_TOOLS=$<BOOL:${DAI_ENABLE_DEV_TOOLS}>
    PRIVATE
        DAI_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        DAI_DXC_PATH="${DIRECTX_DXC_TOOL}")
