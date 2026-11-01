dai_add_module(NAME app
    SOURCES
        include/daidalos/app/Application.hpp
        include/daidalos/app/StartupSequence.hpp
        src/Application.cpp
        src/StartupSequence.cpp
    PUBLIC_DEPS
        daidalos::audio
        daidalos::core
        daidalos::input
        daidalos::logging
        daidalos::platform
        daidalos::renderer
        daidalos::scene)

if(DAI_ENABLE_DEV_TOOLS)
    target_link_libraries(daidalos_app PUBLIC daidalos::debugui)
endif()
