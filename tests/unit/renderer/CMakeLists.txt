dai_add_test(NAME daidalos_renderer_tests
    SOURCES
        PrimitivesTests.cpp
        ShaderCompilerTests.cpp
    DEPS
        daidalos::renderer)
# ShaderCompilerTests checks private classes of the module.
target_include_directories(daidalos_renderer_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/renderer/src")
target_compile_definitions(daidalos_renderer_tests PRIVATE DAI_DXC_PATH="${DIRECTX_DXC_TOOL}")
