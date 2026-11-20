# The level editor (ADR-0055): development builds only, since it draws its interface with the debug overlay.
find_package(imguizmo CONFIG REQUIRED)

dai_add_app(NAME editor
    SOURCES
        src/AssetBrowser.cpp
        src/AssetBrowser.hpp
        src/Clipboard.cpp
        src/Clipboard.hpp
        src/Commands.cpp
        src/Commands.hpp
        src/Cues.cpp
        src/Cues.hpp
        src/EffectCurves.cpp
        src/EffectPanel.cpp
        src/EffectPanel.hpp
        src/Editor.cpp
        src/Editor.hpp
        src/History.cpp
        src/History.hpp
        src/Import.cpp
        src/Import.hpp
        src/Inspector.cpp
        src/Inspector.hpp
        src/Main.cpp
        src/MapCheck.cpp
        src/MapCheck.hpp
        src/Organise.cpp
        src/Organise.hpp
        src/Overlays.cpp
        src/Overlays.hpp
        src/MaterialPanel.cpp
        src/MaterialPanel.hpp
        src/Paint.cpp
        src/Paint.hpp
        src/Picking.cpp
        src/Picking.hpp
        src/SequencePanel.cpp
        src/SequencePanel.hpp
        src/Thumbnail.cpp
        src/Thumbnail.hpp
        src/Views.cpp
        src/Views.hpp
    DEPS
        daidalos::app
        daidalos::scene
        imguizmo::imguizmo
        sandbox_game
    CONFIG
        editor.cfg)

# Play starts the sandbox on the level: beside the editor once installed, else where the build puts it.
if(TARGET sandbox)
    target_compile_definitions(editor PRIVATE DAI_SANDBOX_PATH="$<TARGET_FILE:sandbox>")
    add_dependencies(editor sandbox)
endif()

# The editor started on the factory, an effect open, for a few seconds with the validation layers on: a start
# that crashes or logs an error fails (ADR-0114). It takes the GPU and a window.
add_test(NAME editor.smoke
         COMMAND editor +renderer.validation true +editor.frames 200 +editor.effect Factory/Effects/candle.effect.json
         WORKING_DIRECTORY $<TARGET_FILE_DIR:editor>)
set_tests_properties(editor.smoke PROPERTIES
    LABELS smoke RUN_SERIAL TRUE TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "[[]error[]]|[[]critical[]]|Validation Error")
