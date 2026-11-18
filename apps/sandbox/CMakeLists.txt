dai_add_app(NAME sandbox
    SOURCES
        src/Combat.cpp
        src/Combat.hpp
        src/Controls.cpp
        src/Controls.hpp
        src/Factory.cpp
        src/Factory.hpp
        src/Flashlight.cpp
        src/Flashlight.hpp
        src/Hud.cpp
        src/Hud.hpp
        src/Main.cpp
        src/Menus.cpp
        src/Menus.hpp
        src/PhotoMode.cpp
        src/PhotoMode.hpp
    DEPS
        daidalos::app
        daidalos::scene
    CONFIG
        sandbox.cfg)

# The release archive carries the committed level and fonts, with their licences, but not the sample
# assets that scripts/fetch_sample_assets.py downloads (ADR-0038).
# The factory's texture sources stay out: the level loads their KTX2 files (ADR-0046).
install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/Factory" "${PROJECT_SOURCE_DIR}/assets/Fonts" "${PROJECT_SOURCE_DIR}/assets/Text" DESTINATION assets
    PATTERN "Source" EXCLUDE)
install(FILES package/README.txt DESTINATION .)

# Every scene the sandbox supports, played for a few seconds with the validation layers on (ADR-0074): the
# same scenes run on Windows and Linux. A logged error or a crash fails the test. They take the GPU and a
# window, so they run one at a time; `ctest -LE smoke` leaves them out.
set(dai_smoke_scenes
    "factory=Factory/Factory.level.json"
    "effects-room=Factory/EffectsRoom.level.json"
    "cornell=cornell"
    "mirrors=mirrors"
    "corridor=corridor")
foreach(entry IN LISTS dai_smoke_scenes)
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 name)
    list(GET pair 1 scene)
    add_test(NAME sandbox.smoke.${name}
             COMMAND sandbox +renderer.validation true +sandbox.menu false +sandbox.frames 150
                     +sandbox.scene "${scene}"
             WORKING_DIRECTORY $<TARGET_FILE_DIR:sandbox>)
    set_tests_properties(sandbox.smoke.${name} PROPERTIES
        LABELS smoke RUN_SERIAL TRUE TIMEOUT 300
        FAIL_REGULAR_EXPRESSION "[[]error[]]|[[]critical[]]|Validation Error")
endforeach()
add_test(NAME sandbox.smoke.showcase
         COMMAND sandbox +renderer.validation true +sandbox.menu false +sandbox.frames 150 +sandbox.showcase true
         WORKING_DIRECTORY $<TARGET_FILE_DIR:sandbox>)
set_tests_properties(sandbox.smoke.showcase PROPERTIES
    LABELS smoke RUN_SERIAL TRUE TIMEOUT 300
    FAIL_REGULAR_EXPRESSION "[[]error[]]|[[]critical[]]|Validation Error")
