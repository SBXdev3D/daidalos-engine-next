add_subdirectory(unit/core)
add_subdirectory(unit/platform)
add_subdirectory(unit/resources)
add_subdirectory(unit/rhi)
add_subdirectory(unit/renderer)
add_subdirectory(unit/navigation)
add_subdirectory(unit/physics)
add_subdirectory(unit/audio)
add_subdirectory(unit/scene)
add_subdirectory(unit/app)
add_subdirectory(unit/editor)
add_subdirectory(unit/sandbox)
add_subdirectory(gpu/rhi)
add_subdirectory(gpu/renderer)
add_subdirectory(gpu/scene)

# Every asset path the project names matches its file's spelling, case included, so that Linux finds what
# Windows finds (ADR-0074).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME assets.case_matches_files
             COMMAND Python3::Interpreter -I "${PROJECT_SOURCE_DIR}/scripts/check_asset_case.py")
endif()
