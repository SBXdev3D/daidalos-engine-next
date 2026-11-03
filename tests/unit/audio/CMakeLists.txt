dai_add_test(NAME daidalos_audio_tests
    SOURCES
        AudioTests.cpp
    DEPS
        daidalos::audio
        daidalos::scene)

target_compile_definitions(daidalos_audio_tests PRIVATE
    DAI_FACTORY_MUSIC="${PROJECT_SOURCE_DIR}/assets/Factory/Sounds/enter_the_maze.ogg")
