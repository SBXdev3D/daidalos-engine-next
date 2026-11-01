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

# Jolt is built without RTTI, which the vptr check of UBSan needs from every class it sees.
target_compile_options(daidalos_physics PRIVATE $<$<CXX_COMPILER_ID:Clang>:-fno-sanitize=vptr>)
