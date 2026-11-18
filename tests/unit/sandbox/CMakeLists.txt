# The sandbox's logic that needs no window: its flashlight (ADR-0058) and its controls (ADR-0076).
dai_add_test(NAME daidalos_sandbox_tests
    SOURCES
        ControlsTests.cpp
        FlashlightTests.cpp
        ${PROJECT_SOURCE_DIR}/apps/sandbox/src/Controls.cpp
        ${PROJECT_SOURCE_DIR}/apps/sandbox/src/Flashlight.cpp
    DEPS
        daidalos::scene)
target_include_directories(daidalos_sandbox_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/sandbox/src)
