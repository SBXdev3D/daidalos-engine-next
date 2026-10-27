dai_add_module(NAME platform
    SOURCES
        include/daidalos/platform/CrashHandler.hpp
        include/daidalos/platform/Event.hpp
        include/daidalos/platform/Platform.hpp
        include/daidalos/platform/Window.hpp
        src/CrashHandler.cpp
        src/EventTranslation.cpp
        src/EventTranslation.hpp
        src/Platform.cpp
        src/PlatformState.hpp
        src/Window.cpp
    PUBLIC_DEPS
        daidalos::core
    PRIVATE_DEPS
        daidalos::logging
        SDL3::SDL3)

if(WIN32)
    target_link_libraries(daidalos_platform PRIVATE dbghelp)
endif()
