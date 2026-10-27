dai_add_test(NAME daidalos_app_tests
    SOURCES
        ApplicationTests.cpp
        StartupSequenceTests.cpp
    DEPS
        daidalos::app
        SDL3::SDL3)
