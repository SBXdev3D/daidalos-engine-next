dai_add_module(NAME debugui
    SOURCES
        include/daidalos/debugui/DebugUi.hpp
        src/DebugUi.cpp
    PUBLIC_DEPS
        daidalos::core
        daidalos::platform
        daidalos::rhi
        imgui::imgui)

dai_add_shaders(TARGET daidalos_debugui
    SOURCES shaders/DebugUi.hlsl)
