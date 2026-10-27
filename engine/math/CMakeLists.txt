dai_add_module(NAME math
    SOURCES
        include/daidalos/math/Aabb.hpp
        include/daidalos/math/Axes.hpp
        include/daidalos/math/Projection.hpp
        include/daidalos/math/Transform.hpp
        src/Aabb.cpp
        src/Projection.cpp
        src/Transform.cpp
    PUBLIC_DEPS
        glm::glm
    PRIVATE_DEPS
        daidalos::core)

# GLM types appear in public headers (ADR-0005), so every user of GLM must see the same configuration.
# Code includes GLM through this module only.
target_compile_definitions(daidalos_math PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)
