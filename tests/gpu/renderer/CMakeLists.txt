dai_add_test(NAME daidalos_renderer_gpu_tests
    SOURCES
        ../GpuFixture.hpp
        FxaaPassTests.cpp
        MeshPassTests.cpp
        PathTracerTests.cpp
        RendererTests.cpp
        UiPassTests.cpp
    DEPS
        daidalos::renderer)
if(DAI_ENABLE_DEV_TOOLS)
    target_sources(daidalos_renderer_gpu_tests PRIVATE DebugUiTests.cpp)
endif()
# FLIP compares the renderer with its path tracer (third_party/flip, tests only).
target_include_directories(daidalos_renderer_gpu_tests SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/flip")
# The tests run the private mesh pass on an offscreen target, without a window.
target_include_directories(daidalos_renderer_gpu_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${PROJECT_SOURCE_DIR}/engine/renderer/src")
target_compile_definitions(daidalos_renderer_gpu_tests PRIVATE
    DAI_SLANGC_PATH="${SLANGC_EXECUTABLE}"
    DAI_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/engine/renderer/shaders"
    DAI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
