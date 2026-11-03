dai_add_test(NAME daidalos_scene_tests
    SOURCES
        AnimationTests.cpp
        AssertCapture.hpp
        EntityTests.cpp
        HierarchyTests.cpp
        ScheduleTests.cpp
        SystemsTests.cpp
        TransformTests.cpp
        WorldTests.cpp
    DEPS
        daidalos::scene)
