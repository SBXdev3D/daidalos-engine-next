dai_add_test(NAME daidalos_resources_tests
    SOURCES
        AssetPathTests.cpp
        FontDataTests.cpp
        ImageDataTests.cpp
        ImageFileTests.cpp
        LightProfileTests.cpp
        ModelDataTests.cpp
    DEPS
        daidalos::resources
        KTX::ktx)
target_compile_definitions(daidalos_resources_tests PRIVATE DAI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets"
    DAI_TEST_FONT="${PROJECT_SOURCE_DIR}/assets/Fonts/Rajdhani-Medium.ttf")
