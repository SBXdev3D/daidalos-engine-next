dai_add_test(NAME daidalos_scene_tests
    SOURCES
        AnimationTests.cpp
        AssertCapture.hpp
        EntityTests.cpp
        PerceptionTests.cpp
        SaveGameTests.cpp
        SequenceTests.cpp
        HierarchyTests.cpp
        LevelFileTests.cpp
        LightAnimationTests.cpp
        ParticleEffectFileTests.cpp
        ScheduleTests.cpp
        SystemsTests.cpp
        TransformTests.cpp
        WorldTests.cpp
    DEPS
        daidalos::scene)
