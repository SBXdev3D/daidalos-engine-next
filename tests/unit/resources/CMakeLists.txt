dai_add_test(NAME daidalos_resources_tests
    SOURCES
        AssetPathTests.cpp
        ImageDataTests.cpp
        ModelDataTests.cpp
    DEPS
        daidalos::resources
        KTX::ktx)
target_compile_definitions(daidalos_resources_tests PRIVATE DAI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
