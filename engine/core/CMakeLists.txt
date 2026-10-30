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
        src/Assert.cpp
        src/CVarRegistry.cpp
        src/Error.cpp
        src/FrameTimer.cpp
    PUBLIC_DEPS
        tl::expected)

# The profiling macros expand to Tracy calls in every module that uses them, so Tracy is a public dependency.
if(DAI_ENABLE_PROFILER)
    target_link_libraries(daidalos_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(daidalos_core PUBLIC DAI_PROFILER_ENABLED)
endif()
