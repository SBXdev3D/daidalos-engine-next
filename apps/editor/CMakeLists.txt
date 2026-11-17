# The level editor (ADR-0055): development builds only, since it draws its interface with the debug overlay.
find_package(imguizmo CONFIG REQUIRED)

dai_add_app(NAME editor
    SOURCES
        src/Editor.cpp
        src/Editor.hpp
        src/History.cpp
        src/History.hpp
        src/Inspector.cpp
        src/Inspector.hpp
        src/Main.cpp
        src/Picking.cpp
        src/Picking.hpp
    DEPS
        daidalos::app
        daidalos::scene
        imguizmo::imguizmo
    CONFIG
        editor.cfg)

# Play starts the sandbox on the level: beside the editor once installed, else where the build puts it.
if(TARGET sandbox)
    target_compile_definitions(editor PRIVATE DAI_SANDBOX_PATH="$<TARGET_FILE:sandbox>")
    add_dependencies(editor sandbox)
endif()
