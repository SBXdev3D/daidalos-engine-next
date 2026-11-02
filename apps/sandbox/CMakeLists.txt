dai_add_app(NAME sandbox
    SOURCES
        src/Combat.cpp
        src/Combat.hpp
        src/Factory.cpp
        src/Factory.hpp
        src/Main.cpp
    DEPS
        daidalos::animation
        daidalos::app
        daidalos::ecs
        daidalos::scene)
