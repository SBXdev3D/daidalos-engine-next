dai_add_test(NAME daidalos_platform_tests
    SOURCES
        EventTranslationTests.cpp
        PlatformTests.cpp
        WindowTests.cpp
    DEPS
        daidalos::platform
        SDL3::SDL3)
# EventTranslationTests checks a private function of the module.
target_include_directories(daidalos_platform_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/platform/src")
