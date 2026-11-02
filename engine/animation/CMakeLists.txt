dai_add_module(NAME animation
    SOURCES
        include/daidalos/animation/Animation.hpp
        src/Animation.cpp
    PUBLIC_DEPS
        daidalos::core
        daidalos::ecs
        daidalos::math
        daidalos::resources)
