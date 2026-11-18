dai_add_module(NAME audio
    SOURCES
        include/daidalos/audio/Audio.hpp
        src/Audio.cpp
        src/Miniaudio.cpp
        src/SteamAudio.cpp
        src/SteamAudio.hpp
    PUBLIC_DEPS
        daidalos::core
        glm::glm
    PRIVATE_DEPS
        unofficial::steam-audio::phonon
        Threads::Threads
        ${CMAKE_DL_LIBS})

# miniaudio and stb_vorbis install in the same directory.
target_include_directories(daidalos_audio SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")

# This file only holds third-party code, which is not ours to clean of warnings.
set_source_files_properties(src/Miniaudio.cpp PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")

if(NOT MSVC)
    target_link_libraries(daidalos_audio PRIVATE m)
endif()
