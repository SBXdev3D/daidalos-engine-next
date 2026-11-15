# Converts textures to KTX2 for the engine. A development tool: the release archive leaves it out.
add_executable(texture-tool src/Main.cpp src/Flipbook.cpp src/Flipbook.hpp src/StbImageWrite.cpp)
target_link_libraries(texture-tool PRIVATE daidalos::resources KTX::ktx glm::glm)
set_target_properties(texture-tool PROPERTIES FOLDER "apps")
dai_configure_target(texture-tool)
target_include_directories(texture-tool SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
