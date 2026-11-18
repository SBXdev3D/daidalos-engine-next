dai_add_module(NAME navigation
    SOURCES
        include/daidalos/navigation/NavMesh.hpp
        src/NavMesh.cpp
    PUBLIC_DEPS
        daidalos::core
        glm::glm
    PRIVATE_DEPS
        RecastNavigation::Recast
        RecastNavigation::Detour)
