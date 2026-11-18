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
        TextTests.cpp
        TransformTests.cpp
        WorldTests.cpp
    DEPS
        daidalos::scene)
target_compile_definitions(daidalos_scene_tests PRIVATE DAI_PROJECT_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
