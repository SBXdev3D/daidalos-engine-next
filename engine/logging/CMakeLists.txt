dai_add_module(NAME logging
    SOURCES
        include/daidalos/logging/Log.hpp
        src/Log.cpp
    PUBLIC_DEPS
        daidalos::core
    PRIVATE_DEPS
        spdlog::spdlog)
