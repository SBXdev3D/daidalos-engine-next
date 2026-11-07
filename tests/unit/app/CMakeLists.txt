dai_add_test(NAME daidalos_app_tests
    SOURCES
        ApplicationTests.cpp
        ConsoleTests.cpp
    DEPS
        daidalos::app
        SDL3::SDL3)
