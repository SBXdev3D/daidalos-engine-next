dai_add_module(NAME input
    SOURCES
        include/daidalos/input/ActionMap.hpp
        include/daidalos/input/InputState.hpp
        src/ActionMap.cpp
        src/InputState.cpp
    PUBLIC_DEPS
        daidalos::platform)
