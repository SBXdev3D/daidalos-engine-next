# Converts textures to KTX2 for the engine. A development tool: the release archive leaves it out.
add_executable(texture-tool src/Main.cpp)
target_link_libraries(texture-tool PRIVATE daidalos::resources KTX::ktx)
set_target_properties(texture-tool PROPERTIES FOLDER "apps")
dai_configure_target(texture-tool)
