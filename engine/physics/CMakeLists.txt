dai_add_module(NAME physics
    SOURCES
        include/daidalos/physics/PhysicsWorld.hpp
        src/PhysicsWorld.cpp
    PUBLIC_DEPS
        daidalos::core
        daidalos::ecs
        daidalos::math
    PRIVATE_DEPS
        daidalos::logging
        Jolt::Jolt)
