# The level editor (ADR-0055): development builds only, since it draws its interface with the debug overlay.
find_package(imguizmo CONFIG REQUIRED)

dai_add_app(NAME editor
    SOURCES
        src/AssetBrowser.cpp
        src/AssetBrowser.hpp
        src/Clipboard.cpp
        src/Clipboard.hpp
        src/Editor.cpp
        src/Editor.hpp
        src/History.cpp
        src/History.hpp
        src/Import.cpp
        src/Import.hpp
        src/Inspector.cpp
        src/Inspector.hpp
        src/Main.cpp
        src/Paint.cpp
        src/Paint.hpp
        src/Picking.cpp
        src/Picking.hpp
        src/Thumbnail.cpp
        src/Thumbnail.hpp
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
