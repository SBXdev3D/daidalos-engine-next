dai_add_test(NAME daidalos_renderer_tests
    SOURCES
        LightClustersTests.cpp
        PrimitivesTests.cpp
        ShadowTests.cpp
        ShaderCompilerTests.cpp
    DEPS
        daidalos::renderer)
# ShaderCompilerTests and ShadowTests check private code of the module.
target_include_directories(daidalos_renderer_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/renderer/src")
target_compile_definitions(daidalos_renderer_tests PRIVATE DAI_DXC_PATH="${DIRECTX_DXC_TOOL}")
