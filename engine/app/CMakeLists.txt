dai_add_module(NAME app
    SOURCES
        include/daidalos/app/Application.hpp
        src/Application.cpp
    PUBLIC_DEPS
        daidalos::audio
        daidalos::core
        daidalos::platform
        daidalos::renderer
        daidalos::scene)
