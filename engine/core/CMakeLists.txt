dai_add_module(NAME core
    SOURCES
        include/daidalos/core/Assert.hpp
        include/daidalos/core/Error.hpp
        include/daidalos/core/Result.hpp
        src/Assert.cpp
        src/Error.cpp
    PUBLIC_DEPS
        tl::expected)
