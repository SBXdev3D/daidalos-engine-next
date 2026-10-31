dai_add_test(NAME daidalos_debugui_gpu_tests
    SOURCES
        ../GpuFixture.hpp
        DebugUiTests.cpp
    DEPS
        daidalos::debugui)
target_include_directories(daidalos_debugui_gpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
