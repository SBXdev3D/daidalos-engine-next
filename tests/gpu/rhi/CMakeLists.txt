dai_add_test(NAME daidalos_rhi_gpu_tests
    SOURCES
        ../GpuFixture.hpp
        AccelerationStructureTests.cpp
        DeviceTests.cpp
    DEPS
        daidalos::rhi)
target_include_directories(daidalos_rhi_gpu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
dai_add_shaders(TARGET daidalos_rhi_gpu_tests
    SOURCES shaders/TriangleTest.slang shaders/HalfTriangleTest.slang
    COMPUTE shaders/ComputeTest.slang shaders/RayQueryTest.slang shaders/StorageImageTest.slang)
