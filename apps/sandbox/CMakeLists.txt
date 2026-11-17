dai_add_app(NAME sandbox
    SOURCES
        src/Combat.cpp
        src/Combat.hpp
        src/Factory.cpp
        src/Factory.hpp
        src/Flashlight.cpp
        src/Flashlight.hpp
        src/Hud.cpp
        src/Hud.hpp
        src/Main.cpp
        src/Menus.cpp
        src/Menus.hpp
    DEPS
        daidalos::app
        daidalos::scene
    CONFIG
        sandbox.cfg)

# The release archive carries the committed level and fonts, with their licences, but not the sample
# assets that scripts/fetch_sample_assets.py downloads (ADR-0038).
# The factory's texture sources stay out: the level loads their KTX2 files (ADR-0046).
install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/Factory" "${PROJECT_SOURCE_DIR}/assets/Fonts" DESTINATION assets
    PATTERN "Source" EXCLUDE)
install(FILES package/README.txt DESTINATION .)
