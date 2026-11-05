dai_add_module(NAME resources
    SOURCES
        include/daidalos/resources/AssetPath.hpp
        include/daidalos/resources/Cache.hpp
        include/daidalos/resources/FontData.hpp
        include/daidalos/resources/ImageData.hpp
        include/daidalos/resources/ModelData.hpp
        src/AssetPath.cpp
        src/FontData.cpp
        src/ImageData.cpp
        src/ModelData.cpp
        src/StbImage.cpp
        src/StbTrueType.cpp
        src/Tangents.cpp
    PUBLIC_DEPS
        daidalos::core
    PRIVATE_DEPS
        fastgltf::fastgltf
        KTX::ktx
        mikktspace::mikktspace)

target_include_directories(daidalos_resources SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
