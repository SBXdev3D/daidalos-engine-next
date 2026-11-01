dai_add_app(NAME sandbox
    SOURCES
        src/Factory.cpp
        src/Factory.hpp
        src/Main.cpp
    DEPS
        daidalos::app
        daidalos::ecs
        daidalos::scene)
