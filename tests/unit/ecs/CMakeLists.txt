dai_add_test(NAME daidalos_ecs_tests
    SOURCES
        AssertCapture.hpp
        EntityTests.cpp
        ScheduleTests.cpp
        WorldTests.cpp
    DEPS
        daidalos::ecs)
