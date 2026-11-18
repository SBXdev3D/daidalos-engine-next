dai_add_module(NAME platform
    SOURCES
        include/daidalos/input/ActionMap.hpp
        include/daidalos/input/Bindings.hpp
        include/daidalos/input/InputState.hpp
        include/daidalos/input/Rumble.hpp
        include/daidalos/platform/CrashHandler.hpp
        include/daidalos/platform/Event.hpp
        include/daidalos/platform/InputCodes.hpp
        include/daidalos/platform/Platform.hpp
        include/daidalos/platform/Process.hpp
        include/daidalos/platform/Window.hpp
        src/ActionMap.cpp
        src/Bindings.cpp
        src/CrashHandler.cpp
        src/EventTranslation.cpp
        src/EventTranslation.hpp
        src/InputState.cpp
        src/Platform.cpp
        src/PlatformState.hpp
        src/Process.cpp
        src/Rumble.cpp
        src/Window.cpp
    PUBLIC_DEPS
        daidalos::core
    PRIVATE_DEPS
        SDL3::SDL3
        Vulkan::Headers)

if(WIN32)
    target_link_libraries(daidalos_platform PRIVATE dbghelp)
endif()
