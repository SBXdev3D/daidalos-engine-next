dai_add_module(NAME input
    SOURCES
        include/daidalos/input/InputState.hpp
        src/InputState.cpp
    PUBLIC_DEPS
        daidalos::platform)
