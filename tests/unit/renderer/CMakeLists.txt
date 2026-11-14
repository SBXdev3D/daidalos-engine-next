dai_add_test(NAME daidalos_renderer_tests
    SOURCES
        CanvasTests.cpp
        ContextTests.cpp
        DisplayTransformTests.cpp
        ExposureTests.cpp

        LightSamplingTests.cpp
        LightUnitsTests.cpp
        JitterTests.cpp
        PrimitivesTests.cpp
        RenderGraphTests.cpp
        ShaderCompilerTests.cpp
    DEPS
        daidalos::renderer)
# RenderGraphTests and ShaderCompilerTests check private code of the module.
target_include_directories(daidalos_renderer_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/renderer/src")
target_compile_definitions(daidalos_renderer_tests PRIVATE DAI_SLANGC_PATH="${SLANGC_EXECUTABLE}")
