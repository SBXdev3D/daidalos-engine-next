dai_add_module(NAME app
    SOURCES
        include/daidalos/app/Application.hpp
        include/daidalos/app/Console.hpp
        include/daidalos/app/Quality.hpp
        src/Application.cpp
        src/Console.cpp
        src/Quality.cpp
    PUBLIC_DEPS
        daidalos::audio
        daidalos::core
        daidalos::platform
        daidalos::renderer
        daidalos::scene)

# The developer tools draw with Dear ImGui, which only development builds have.
if(DAI_ENABLE_DEV_TOOLS)
    target_sources(daidalos_app PRIVATE include/daidalos/app/DevTools.hpp src/DevTools.cpp)
endif()
