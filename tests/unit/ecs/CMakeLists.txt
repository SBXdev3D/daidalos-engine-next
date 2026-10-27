dai_add_test(NAME daidalos_ecs_tests
    SOURCES
        AssertCapture.hpp
        EntityTests.cpp
        WorldTests.cpp
    DEPS
        daidalos::ecs)
