# The level editor's logic that needs no window: its undo history (ADR-0055).
dai_add_test(NAME daidalos_editor_tests
    SOURCES
        HistoryTests.cpp
        ${PROJECT_SOURCE_DIR}/apps/editor/src/History.cpp
    DEPS
        daidalos::scene)
target_include_directories(daidalos_editor_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/editor/src)
