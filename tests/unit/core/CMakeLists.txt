dai_add_test(NAME daidalos_core_tests
    SOURCES
        AabbTests.cpp
        AssertTests.cpp
        ConventionTests.cpp
        CVarRegistryTests.cpp
        ErrorTests.cpp
        FrameTimerTests.cpp
        LogTests.cpp
        MathTestUtilities.hpp
        ProfileTests.cpp
        ProjectionTests.cpp
        ResultTests.cpp
        SlotPoolTests.cpp
        WorkerPoolTests.cpp
    DEPS
        daidalos::core)
