dai_add_test(NAME daidalos_rhi_gpu_tests
    SOURCES
        ../GpuFixture.hpp
        DeviceTests.cpp
    DEPS
        daidalos::rhi)
target_include_directories(daidalos_rhi_gpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
