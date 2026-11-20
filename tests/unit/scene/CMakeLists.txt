dai_add_test(NAME daidalos_scene_tests
    SOURCES
        AnimationTests.cpp
        EffectDocumentTests.cpp
        AssertCapture.hpp
        EntityTests.cpp
        GltfMaterialsTests.cpp
        PerceptionTests.cpp
        SaveGameTests.cpp
        SequenceTests.cpp
        HierarchyTests.cpp
        LevelFileTests.cpp
        LightAnimationTests.cpp
        ModelBatchTests.cpp
        ParticleEffectFileTests.cpp
        ScheduleTests.cpp
        SystemsTests.cpp
        TextTests.cpp
        TransformTests.cpp
        WorldTests.cpp
    DEPS
        daidalos::scene)
target_compile_definitions(daidalos_scene_tests PRIVATE DAI_PROJECT_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    DAI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
