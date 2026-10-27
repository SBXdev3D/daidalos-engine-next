dai_add_module(NAME core
    SOURCES
        include/daidalos/core/Assert.hpp
        include/daidalos/core/CVarRegistry.hpp
        include/daidalos/core/Error.hpp
        include/daidalos/core/FrameTimer.hpp
        include/daidalos/core/Result.hpp
        src/Assert.cpp
        src/CVarRegistry.cpp
        src/Error.cpp
        src/FrameTimer.cpp
    PUBLIC_DEPS
        tl::expected)
