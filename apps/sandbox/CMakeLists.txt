dai_add_app(NAME sandbox
    SOURCES
        src/Combat.cpp
        src/Combat.hpp
        src/Factory.cpp
        src/Factory.hpp
        src/Main.cpp
    DEPS
        daidalos::app
        daidalos::scene)

# The settings file goes next to the executable once, and is left alone after that: it is the player's.
add_custom_command(TARGET sandbox POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/sandbox.cfg
        -DDESTINATION=$<TARGET_FILE_DIR:sandbox>/sandbox.cfg -P ${PROJECT_SOURCE_DIR}/cmake/CopyIfMissing.cmake
    VERBATIM)
