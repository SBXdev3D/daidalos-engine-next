# The level editor's logic that needs no window: its undo history (ADR-0055).
dai_add_test(NAME daidalos_editor_tests
    SOURCES
        ClipboardTests.cpp
        HistoryTests.cpp
        ImportTests.cpp
        ThumbnailTests.cpp
        ${PROJECT_SOURCE_DIR}/apps/editor/src/Clipboard.cpp
        ${PROJECT_SOURCE_DIR}/apps/editor/src/History.cpp
        ${PROJECT_SOURCE_DIR}/apps/editor/src/Import.cpp
        ${PROJECT_SOURCE_DIR}/apps/editor/src/Thumbnail.cpp
    DEPS
        daidalos::scene)
target_include_directories(daidalos_editor_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/editor/src)
target_compile_definitions(daidalos_editor_tests PRIVATE DAI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/tests/assets")
