cmake_minimum_required(VERSION 3.25)

project(DaidalosEngineNext
    VERSION 0.1.0
    DESCRIPTION "Modular C++ real-time 3D engine for indoor environments"
    LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset <name>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(DAI_BUILD_APPS "Build the applications in apps/" ON)
option(DAI_BUILD_TESTS "Build the unit tests in tests/" ON)
option(DAI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(DAI_ENABLE_ASAN "Build with AddressSanitizer (and UndefinedBehaviorSanitizer where supported)" OFF)
option(DAI_ENABLE_PROFILER "Build with the Tracy profiler client (needs the vcpkg feature 'profiler')" OFF)
option(DAI_ENABLE_DEV_TOOLS "Build the debug overlay and shader hot reload (off in shipping builds)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DaidalosSanitizers)
include(DaidalosShaders)
include(DaidalosTargets)

find_package(directx-dxc CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
if(DAI_ENABLE_PROFILER)
    find_package(Tracy CONFIG REQUIRED)
endif()

if(DAI_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    enable_testing()
endif()

add_subdirectory(engine)

if(DAI_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(DAI_BUILD_TESTS)
    add_subdirectory(tests)
endif()
