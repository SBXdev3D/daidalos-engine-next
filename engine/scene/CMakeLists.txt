dai_add_module(NAME scene
    SOURCES
        include/daidalos/scene/Scene.hpp
        src/Scene.cpp
    PUBLIC_DEPS
        daidalos::audio
        daidalos::core
        daidalos::ecs
        daidalos::physics
        daidalos::renderer)
