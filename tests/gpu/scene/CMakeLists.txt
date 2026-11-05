dai_add_test(NAME daidalos_scene_gpu_tests
    SOURCES
        AssetLibraryTests.cpp
        SceneTests.cpp
    DEPS
        daidalos::app)
target_compile_definitions(daidalos_scene_gpu_tests PRIVATE DAI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets"
    DAI_PROJECT_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
