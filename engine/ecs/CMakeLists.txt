dai_add_module(NAME ecs
    SOURCES
        include/daidalos/ecs/ComponentPool.hpp
        include/daidalos/ecs/Entity.hpp
        include/daidalos/ecs/Schedule.hpp
        include/daidalos/ecs/World.hpp
        src/Schedule.cpp
        src/World.cpp
    PUBLIC_DEPS
        daidalos::core)
