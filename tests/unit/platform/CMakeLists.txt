dai_add_test(NAME daidalos_platform_tests
    SOURCES
        ActionMapTests.cpp
        BindingsTests.cpp
        CrashHandlerTests.cpp
        EventTranslationTests.cpp
        InputStateTests.cpp
        PlatformTests.cpp
        ProcessTests.cpp
        WindowTests.cpp
    DEPS
        daidalos::platform
        SDL3::SDL3)
# EventTranslationTests checks a private function of the module.
target_include_directories(daidalos_platform_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/platform/src")

# The crash tests run a helper that crashes on purpose and check that it leaves a dump.
# AddressSanitizer handles crashes itself, so these tests are not built with it.
if(WIN32 AND NOT DAI_ENABLE_ASAN)
    add_executable(daidalos_crash_helper CrashHelper.cpp)
    target_link_libraries(daidalos_crash_helper PRIVATE daidalos::platform)
    set_target_properties(daidalos_crash_helper PROPERTIES FOLDER "tests")
    dai_configure_target(daidalos_crash_helper)

    add_dependencies(daidalos_platform_tests daidalos_crash_helper)
    target_compile_definitions(daidalos_platform_tests
        PRIVATE DAI_CRASH_HELPER_PATH="$<TARGET_FILE:daidalos_crash_helper>")
endif()
