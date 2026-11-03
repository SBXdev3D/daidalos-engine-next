dai_add_module(NAME core
    SOURCES
        include/daidalos/core/Assert.hpp
        include/daidalos/core/CVarRegistry.hpp
        include/daidalos/core/Error.hpp
        include/daidalos/core/FrameTimer.hpp
        include/daidalos/core/Handle.hpp
        include/daidalos/core/Profile.hpp
        include/daidalos/core/Result.hpp
        include/daidalos/core/SlotPool.hpp
        include/daidalos/core/WorkerPool.hpp
        include/daidalos/logging/Log.hpp
        include/daidalos/math/Aabb.hpp
        include/daidalos/math/Axes.hpp
        include/daidalos/math/Projection.hpp
        include/daidalos/math/Transform.hpp
        src/Aabb.cpp
        src/Assert.cpp
        src/CVarRegistry.cpp
        src/Error.cpp
        src/FrameTimer.cpp
        src/Log.cpp
        src/Projection.cpp
        src/Transform.cpp
        src/WorkerPool.cpp
    PUBLIC_DEPS
        glm::glm
        Threads::Threads
        tl::expected
    PRIVATE_DEPS
        spdlog::spdlog)

# GLM is public on purpose (ADR-0005), so its conventions are too.
target_compile_definitions(daidalos_core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)

if(DAI_ENABLE_PROFILER)
    target_link_libraries(daidalos_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(daidalos_core PUBLIC DAI_PROFILER_ENABLED)
endif()
