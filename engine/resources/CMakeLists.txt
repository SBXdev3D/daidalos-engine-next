dai_add_module(NAME resources
    SOURCES
        include/daidalos/resources/AssetPath.hpp
        include/daidalos/resources/Cache.hpp
        include/daidalos/resources/ImageData.hpp
        include/daidalos/resources/ModelData.hpp
        src/AssetPath.cpp
        src/ImageData.cpp
        src/ModelData.cpp
        src/StbImage.cpp
    PUBLIC_DEPS
        daidalos::core
        daidalos::math
    PRIVATE_DEPS
        daidalos::logging
        fastgltf::fastgltf
        KTX::ktx)

target_include_directories(daidalos_resources SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
