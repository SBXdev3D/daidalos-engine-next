dai_add_test(NAME daidalos_core_tests
    SOURCES
        AssertTests.cpp
        CVarRegistryTests.cpp
        ErrorTests.cpp
        FrameTimerTests.cpp
        ResultTests.cpp
    DEPS
        daidalos::core)
