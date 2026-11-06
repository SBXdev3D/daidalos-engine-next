dai_add_app(NAME sandbox
    SOURCES
        src/Combat.cpp
        src/Combat.hpp
        src/Factory.cpp
        src/Factory.hpp
        src/Hud.cpp
        src/Hud.hpp
        src/Main.cpp
        src/Menus.cpp
        src/Menus.hpp
    DEPS
        daidalos::app
        daidalos::scene
    CONFIG
        sandbox.cfg)
