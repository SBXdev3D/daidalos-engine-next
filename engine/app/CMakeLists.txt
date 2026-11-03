dai_add_module(NAME app
    SOURCES
        include/daidalos/app/Application.hpp
        include/daidalos/app/StartupSequence.hpp
        src/Application.cpp
        src/StartupSequence.cpp
    PUBLIC_DEPS
        daidalos::audio
        daidalos::core
        daidalos::platform
        daidalos::renderer
        daidalos::scene)
